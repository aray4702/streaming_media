#include "sm/rtp/send_side_controller.h"

#include <algorithm>

namespace sm::rtp {

namespace {
cc::GccConfig WithEvents(cc::GccConfig c, EventSink* e) {
  if (!c.events) c.events = e;
  return c;
}
cc::PacerConfig WithEvents(cc::PacerConfig c, EventSink* e) {
  if (!c.events) c.events = e;
  return c;
}
cc::CongestionWindowConfig WithEvents(cc::CongestionWindowConfig c, EventSink* e) {
  if (!c.events) c.events = e;
  return c;
}
cc::OutageConfig WithEvents(cc::OutageConfig c, EventSink* e) {
  if (!c.events) c.events = e;
  return c;
}
}  // namespace

SendSideController::SendSideController(SendSideConfig cfg, const sync::ExtensionMap* extensions)
    : cfg_(cfg),
      extensions_(extensions),
      gcc_(WithEvents(cfg.gcc, cfg.events)),
      pacer_(WithEvents(cfg.pacer, cfg.events)),
      outage_(WithEvents(cfg.outage, cfg.events)),
      cwnd_(WithEvents(cfg.cwnd, cfg.events)),
      target_(cfg.gcc.start_rate) {}

void SendSideController::Apply(const cc::NetworkControlUpdate& u) {
  target_ = u.target_rate;
  pacer_.SetPacingRates(u.pacing_rate, DataRate::Zero());
  for (const auto& c : u.probes) pacer_.AddProbeCluster(c);
  outage_.SetCurrentRate(target_);
}

// libwebrtc runs UpdateTargetBitrate() whenever the network state may have
// changed: after feedback, on the process interval and for every sent packet.
void SendSideController::UpdatePushback(Timestamp now, bool window) {
  cwnd_.UpdateOutstandingData(gcc_.history().InFlight());
  cwnd_.UpdatePacingQueue(pacer_.QueueSize());
  if (window) cwnd_.UpdateWindow(target_);
  cwnd_.UpdateTargetBitrate(target_, now);
}

void SendSideController::WriteSendExtensions(RtpPacket& p, uint16_t transport_seq, Timestamp now) const {
  if (!extensions_) return;
  sync::ExtensionAccessor access(*extensions_);
  if (extensions_->IdOf(sync::uri::kTransportCc)) access.SetTransportSeq(p, transport_seq);
  if (extensions_->IdOf(sync::uri::kAbsSendTime)) access.SetAbsSendTime(p, now);
}

void SendSideController::Start(Timestamp now) { Apply(gcc_.Start(now)); }

void SendSideController::Enqueue(SenderPacket p, Timestamp now) {
  // Reserve the extensions written at release, so the pacer budgets the size
  // that goes on the wire.
  WriteSendExtensions(p.packet, 0, now);
  int64_t size = static_cast<int64_t>(p.packet.size()) + cfg_.transport_overhead_bytes;
  cc::PacketPriority prio = p.priority;
  uint64_t id = next_id_++;
  queued_[id] = std::move(p);
  pacer_.Enqueue({id, prio, DataSize::Bytes(size), now});
}

std::vector<ReleasedPacket> SendSideController::Process(Timestamp now) {
  std::vector<ReleasedPacket> out;
  for (const auto& sent : pacer_.Process(now)) {
    SenderPacket sp;
    if (sent.is_padding) {
      if (!padding_sender_) continue;
      size_t room = static_cast<size_t>(std::max<int64_t>(0, sent.size.bytes() - cfg_.transport_overhead_bytes));
      sp = padding_sender_->MakePadding(room, padding_sender_->last_rtp_timestamp(), now);
      // The send extensions are written below: take their size off the
      // padding so the packet stays within the size the pacer accounted.
      WriteSendExtensions(sp.packet, 0, now);
      const int64_t over = static_cast<int64_t>(sp.packet.size()) + cfg_.transport_overhead_bytes - sent.size.bytes();
      if (over > 0) {
        sp.packet.padding_size = static_cast<uint8_t>(std::max<int64_t>(1, sp.packet.padding_size - over));
      }
      ++stats_.padding;
    } else {
      auto it = queued_.find(sent.id);
      if (it == queued_.end()) {
        ++stats_.unknown_ids;
        continue;
      }
      sp = std::move(it->second);
      queued_.erase(it);
    }
    ReleasedPacket r;
    r.transport_seq = transport_seq_++;
    WriteSendExtensions(sp.packet, r.transport_seq, now);
    r.bytes = sp.packet.Serialize();
    r.kind = sp.kind;
    r.wire_size = sent.size;
    r.probe_cluster = sent.probe_cluster;
    r.send_time = now;
    gcc_.OnPacketSent(r.transport_seq, now, sent.size, sent.probe_cluster);
    UpdatePushback(now, false);
    window_.emplace_back(now, sent.size.bytes());
    window_bytes_ += sent.size.bytes();
    ++stats_.released;
    stats_.released_bytes += sent.size.bytes();
    out.push_back(std::move(r));
  }
  if (!last_gcc_.IsFinite() || now - last_gcc_ >= cfg_.gcc_interval) {
    last_gcc_ = now;
    Apply(gcc_.OnProcessInterval(now, send_rate(now)));
    UpdatePushback(now, true);
  }
  if (!last_outage_.IsFinite() || now - last_outage_ >= cfg_.outage_interval) {
    last_outage_ = now;
    cc::OutageActions act = outage_.Process(now);
    if (act.pause_encoder) actions_.pause_encoder = true;
    if (act.resume_encoder) actions_.resume_encoder = true;
    if (act.request_keyframe) actions_.request_keyframe = true;
    if (act.reset_estimate) Apply(gcc_.ResetEstimate(act.reset_rate, now));
  }
  return out;
}

Timestamp SendSideController::NextProcessTime(Timestamp now) const {
  Timestamp next = pacer_.NextSendTime(now);
  if (last_gcc_.IsFinite()) next = std::min(next, last_gcc_ + cfg_.gcc_interval);
  if (last_outage_.IsFinite()) next = std::min(next, last_outage_ + cfg_.outage_interval);
  return std::max(next, now);
}

void SendSideController::OnTransportFeedback(const cc::TransportFeedback& fb, Timestamp now) {
  Apply(gcc_.OnTransportFeedback(fb, now));
  if (auto rtt = gcc_.last_feedback_max_rtt()) cwnd_.OnFeedbackRtt(*rtt);
  UpdatePushback(now, true);
  outage_.OnFeedback(now);
}

ControllerActions SendSideController::TakeActions() {
  ControllerActions a = actions_;
  actions_ = {};
  return a;
}

bool SendSideController::DropCapturedFrame() {
  const auto drop = cc::CongestionWindowPushback::FrameDropInterval(target_, cwnd_.reduce_ratio(), cfg_.video_min);
  if (drop.interval == 0) return false;
  if (cwnd_frame_counter_++ % static_cast<uint64_t>(drop.interval) != 0) return false;
  ++cwnd_frames_dropped_;
  return true;
}

DataRate SendSideController::send_rate(Timestamp now) {
  while (!window_.empty() && window_.front().first <= now - cfg_.send_rate_window) {
    window_bytes_ -= window_.front().second;
    window_.pop_front();
  }
  return DataRate::BitsPerSec(window_bytes_ * 8 * 1000000 / cfg_.send_rate_window.us());
}

SendRates SendSideController::ComputeRates(DataRate audio_wire_rate, double fec_protection) const {
  DataRate r = target_ - audio_wire_rate;
  r = r * (1.0 / (1.0 + fec_protection));
  SendRates out;
  out.target = target_;
  out.audio_rate = audio_wire_rate;
  out.video_target = std::clamp(r, cfg_.video_min, cfg_.video_max);
  // Pushback acts on the whole target; the encoder drops frames and takes
  // the matching share of the video target.
  const auto drop = cc::CongestionWindowPushback::FrameDropInterval(target_, cwnd_.reduce_ratio(), cfg_.video_min);
  if (drop.interval > 0) {
    out.frame_drop_interval = drop.interval;
    out.video_target = std::clamp(out.video_target - out.video_target * (1.0 / drop.interval), cfg_.video_min, cfg_.video_max);
  }
  return out;
}

void SendSideController::ExportMetrics(MetricsWriter& w) const {
  w.Counter("released", stats_.released);
  w.Counter("padding", stats_.padding);
  w.Counter("unknown_ids", stats_.unknown_ids);
  w.Counter("released_bytes", static_cast<uint64_t>(stats_.released_bytes));
  w.Gauge("target_kbps", target_.kbps());
  w.Counter("cwnd_frames_dropped", cwnd_frames_dropped_);
}

}  // namespace sm::rtp
