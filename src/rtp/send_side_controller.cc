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
      allocator_(cfg.events),
      transport_seq_(1),
      video_rate_(cfg.repair_window.ms(), 8000.0f),
      rtx_rate_(cfg.repair_window.ms(), 8000.0f),
      fec_rate_(cfg.repair_window.ms(), 8000.0f),
      target_(cfg.gcc.start_rate),
      pushback_target_(cfg.gcc.start_rate) {
  pacer_.SetTransportOverhead(DataSize::Bytes(cfg_.transport_overhead_bytes));
}

void SendSideController::Apply(const cc::NetworkControlUpdate& u, Timestamp now) {
  target_ = u.target_rate;
  if (allocation_configured_ && !cfg_.control_handler) allocator_.OnTargetChanged(target_, now);
  // GetPacingRates: the padding rate never exceeds the pushback target.
  if (allocation_configured_) gcc_.SetPacingLimits(allocator_.limits().min_allocatable, allocator_.limits().max_padding);
  pacer_.SetPacingRates(gcc_.PacingRate(), std::min(gcc_.PaddingRate(), pushback_target_));
  for (const auto& c : u.probes) pacer_.AddProbeCluster(c);
  outage_.SetCurrentRate(target_);
}

// libwebrtc runs UpdateTargetBitrate() whenever the network state may have
// changed: after feedback, on the process interval and for every sent packet.
void SendSideController::UpdatePushback(Timestamp now, bool window) {
  cwnd_.UpdateOutstandingData(gcc_.history().InFlight());
  cwnd_.UpdatePacingQueue(pacer_.QueueSize());
  if (window) cwnd_.UpdateWindow(target_);
  // MaybeTriggerOnNetworkChanged: the pushback target is at least the bandwidth estimator's minimum.
  pushback_target_ = std::max(gcc_.min_rate(), cwnd_.UpdateTargetBitrate(target_, now));
}

// GoogCC's MaybeTriggerOnNetworkChanged hands its TargetTransferRate to the transport's
// CongestionControlHandler (drop-frame-only pushback: the loss-based target and the reduce ratio). It runs
// after feedback, on the process interval and, with congestion-window pushback, for every sent packet.
void SendSideController::PostTargetRate(Timestamp now) {
  if (!cfg_.control_handler) return;
  cc::TargetTransferRate msg;
  msg.target_rate = target_;
  msg.cwnd_reduce_ratio = cwnd_.reduce_ratio();
  msg.fraction_loss = gcc_.send_side_bwe().fraction_loss();
  msg.round_trip_time = gcc_.send_side_bwe().round_trip_time();
  control_handler_.SetTargetRate(msg);
  UpdateControlState(now);
}

// RtpTransportControllerSend::UpdateControlState: only a message the handler passes on reaches
// the bitrate allocator (BitrateAllocator::OnNetworkEstimateChanged), and with it the streams.
void SendSideController::UpdateControlState(Timestamp now) {
  const std::optional<cc::TargetTransferRate> update = control_handler_.GetUpdate();
  if (!update) return;
  reported_reduce_ratio_ = update->cwnd_reduce_ratio;
  if (allocation_configured_) allocator_.OnTargetChanged(update->target_rate, now);
}

void SendSideController::WriteSendExtensions(RtpPacket& p, uint16_t transport_seq, Timestamp now) const {
  if (!extensions_) return;
  sync::ExtensionAccessor access(*extensions_);
  if (extensions_->IdOf(sync::uri::kTransportCc)) access.SetTransportSeq(p, transport_seq);
  if (extensions_->IdOf(sync::uri::kAbsSendTime)) access.SetAbsSendTime(p, now);
}

void SendSideController::ConfigureAllocation(DataRate audio_wire_rate, bool video) {
  allocation_configured_ = true;
  if (audio_wire_rate > DataRate::Zero()) {
    cc::StreamAllocationConfig a;
    a.min = a.max = a.priority_rate = audio_wire_rate;
    a.enforce_min = true;
    allocator_.AddObserver("audio", a, [](DataRate) { return DataRate::Zero(); });
  }
  if (video) {
    cc::StreamAllocationConfig v;
    v.min = cfg_.video_min;
    v.max = cfg_.video_max;
    v.enforce_min = true;
    // OnBitrateUpdated: the protection is the share RTX and FEC take (FecControllerDefault).
    allocator_.AddObserver("video", v, [this](DataRate alloc) {
      // RtpVideoSender::OnBitrateUpdated sets encoder_target_rate_bps_ here, from the protection sent in
      // the last second, and the encoder keeps it until the next allocation.
      const VideoAllocationSplit split = SplitVideoAllocation(alloc);
      video_encoder_target_ = split.encoder_target;
      video_link_allocation_ = split.link_allocation;
      return split.protection;
    });
  }
  gcc_.SetMinAllocatable(allocator_.limits().min_allocatable);
  // SetAllocatedSendBitrateLimits: streams_config.max_total_allocated_bitrate.
  for (const auto& c : gcc_.OnMaxTotalAllocatedBitrate(allocator_.limits().max_allocatable, Timestamp::Micros(0))) pacer_.AddProbeCluster(c);
  target_ = std::max(target_, gcc_.target());
}

void SendSideController::Start(Timestamp now) {
  Apply(gcc_.Start(now), now);
  PostTargetRate(now);
}

void SendSideController::EnablePeriodicAlrProbing(bool enable) { gcc_.EnablePeriodicAlrProbing(enable); }

DataRate SendSideController::VideoPayloadRate() const {
  if (!allocation_configured_) return DataRate::Zero();
  const DataRate alloc = allocator_.allocation("video");
  return std::max(DataRate::Zero(), alloc - OverheadRate(alloc, cfg_.max_packet_size + cfg_.transport_overhead_bytes));
}

void SendSideController::SetPacingFactor(double factor) {
  gcc_.SetPacingFactor(factor);
  // OnStreamsConfig returns the new pacer config at once (GetPacingRates).
  pacer_.SetPacingRates(gcc_.PacingRate(), std::min(gcc_.PaddingRate(), pushback_target_));
}

void SendSideController::OnLossReport(int64_t packets_lost_delta, int64_t packets_delta, Timestamp now) {
  Apply(gcc_.OnTransportLossReport(packets_lost_delta, packets_delta, now), now);
}

void SendSideController::SetPaddingSender(RtpSender* sender) {
  padding_sender_ = sender;
  // Padding is made as libwebrtc's RTPSender::GeneratePadding does: RTX payload padding from the
  // history, then empty padding, each packet with its real size. Without RTX the pacer keeps
  // fixed-size placeholders that MakePadding() fills in when sent.
  if (!sender || !sender->config().rtx_ssrc) {
    pacer_.SetPaddingGenerator(nullptr);
    return;
  }
  pacer_.SetPaddingGenerator([this](DataSize target, Timestamp now) {
    std::vector<cc::PacedPacket> out;
    for (SenderPacket& sp : padding_sender_->GeneratePadding(static_cast<size_t>(target.bytes()), padding_sender_->last_rtp_timestamp(), now)) {
      WriteSendExtensions(sp.packet, 0, now);  // Reserve the extensions written at release.
      const uint64_t id = next_id_++;
      out.push_back({id, cc::PacketPriority::kPadding, DataSize::Bytes(static_cast<int64_t>(sp.packet.size()) + cfg_.transport_overhead_bytes), now});
      queued_[id] = std::move(sp);
      ++stats_.padding;
    }
    return out;
  });
}

void SendSideController::Enqueue(SenderPacket p, Timestamp now) {
  // Reserve the extensions written at release, so the pacer budgets the size
  // that goes on the wire.
  WriteSendExtensions(p.packet, 0, now);
  int64_t size = static_cast<int64_t>(p.packet.size()) + cfg_.transport_overhead_bytes;
  cc::PacketPriority prio = p.priority;
  uint64_t id = next_id_++;
  cc::PacedPacket pp{id, prio, DataSize::Bytes(size), now};
  pp.is_key_frame = p.is_key_frame;
  pp.first_packet_of_frame = p.first_packet_of_frame;
  queued_[id] = std::move(p);
  pacer_.Enqueue(pp);
  // A keyframe flushed the video stream's queue: those packets are never sent. The sender is not told:
  // libwebrtc's PacingController::EnqueuePacket drops the flushed RTX packets (pacing_controller.cc:227-231)
  // and RtpPacketHistory keeps them pending, so a later NACK for them is ignored (rtp_packet_history.cc:183).
  for (const cc::PacedPacket& f : pacer_.TakeFlushed()) {
    queued_.erase(f.id);
    ++stats_.flushed;
  }
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
    if (sp.sender) sp.sender->AssignSequenceNumber(sp);
    ReleasedPacket r;
    // PacketRouter::SendPacket (packet_router.cc:213-228): only a packet that carries the
    // transport-wide sequence number extension gets a number (from 1), and only a numbered packet is
    // reported to the bandwidth estimator (RtpTransportControllerSend::NotifyBweOfPacedSentPacket).
    const bool numbered = extensions_ && extensions_->IdOf(sync::uri::kTransportCc);
    if (numbered) r.transport_seq = transport_seq_++;
    WriteSendExtensions(sp.packet, r.transport_seq, now);
    r.bytes = sp.packet.Serialize();
    r.kind = sp.kind;
    r.wire_size = sent.size;
    r.probe_cluster = sent.probe_cluster;
    r.send_time = now;
    if (sp.sender) {
      sp.sender->OnPacketReleased(sp, now);
      // PacingController::ProcessPackets: the FEC the packet completed joins the queue.
      for (SenderPacket& f : sp.sender->TakeFecPackets()) Enqueue(std::move(f), now);
      if (numbered && (sp.kind == PacketKind::kAudio || sp.kind == PacketKind::kVideo || sp.kind == PacketKind::kFec)) {
        sent_media_[r.transport_seq] = {sp.sender, sp.packet.seq};
        // Feedback never covers more than a few seconds; bound the table.
        sent_media_.erase(static_cast<uint16_t>(r.transport_seq - 20000));
      }
    }
    if (numbered) gcc_.OnPacketSent(r.transport_seq, now, sent.size, sent.probe_cluster);
    UpdatePushback(now, false);
    if (cfg_.cwnd.enabled) PostTargetRate(now);
    if (r.kind == PacketKind::kRtx || r.kind == PacketKind::kFec || r.kind == PacketKind::kVideo) {
      repair_window_.push_back({now, sent.size.bytes(), r.kind});
      (r.kind == PacketKind::kVideo ? video_bytes_ : repair_bytes_) += sent.size.bytes();
      // RtpSenderEgress::UpdateRtpStats (rtp_sender_egress.cc:495): the RTP packet size, by media type.
      const int64_t rtp_bytes = static_cast<int64_t>(r.bytes.size());
      (r.kind == PacketKind::kVideo ? video_rate_ : r.kind == PacketKind::kRtx ? rtx_rate_ : fec_rate_)
          .Update(rtp_bytes, now.ms());
    }
    window_.emplace_back(now, sent.size.bytes());
    window_bytes_ += sent.size.bytes();
    ++stats_.released;
    stats_.released_bytes += sent.size.bytes();
    out.push_back(std::move(r));
  }
  ExpireRepair(now);
  last_process_ = now;
  if (!last_gcc_.IsFinite() || now - last_gcc_ >= cfg_.gcc_interval) {
    last_gcc_ = now;
    // The transport's pacer-queue task (kPacerQueueUpdateInterval, 25 ms, started with the controller's
    // and before it): the encoder pauses while the pacer's expected queue time is above 2 s.
    if (cfg_.control_handler) control_handler_.SetPacerQueue(pacer_.QueueSize().bytes() == 0 ? TimeDelta::Zero() : pacer_.ExpectedQueueTime());
    if (cfg_.control_handler) UpdateControlState(now);
    Apply(gcc_.OnProcessInterval(now, send_rate(now)), now);
    UpdatePushback(now, true);
    PostTargetRate(now);
  }
  // libwebrtc has no outage monitor: its sender keeps sending (audio at full rate, video at the target the
  // feedback time-out backs off to) and GoogCC recovers from the feedback that comes back. So does this one;
  // the monitor only follows the link (its metrics), and its decisions are not acted on.
  return out;
}

Timestamp SendSideController::NextProcessTime(Timestamp now) const {
  Timestamp next = pacer_.NextSendTime(now);
  if (last_gcc_.IsFinite()) next = std::min(next, last_gcc_ + cfg_.gcc_interval);
  return std::max(next, now);
}

void SendSideController::OnTransportFeedback(const cc::TransportFeedback& fb, Timestamp now) {
  if (!sent_media_.empty()) {
    // RtpVideoSender::OnPacketFeedbackVector (call/rtp_video_sender.cc:1001-1047). The feedback
    // demuxer hands each stream the packets of its media SSRC only (not RTX or padding), in
    // transport-wide order. A video stream's lost packets that no received packet of the stream
    // follows may go unseen by the receiver's NACK, so they are resent now ("early loss
    // detection"); the history's checks then hold back the NACK for them.
    std::unordered_map<RtpSender*, std::vector<uint16_t>> acked, early_loss;
    for (const auto& p : fb.packets) {
      auto it = sent_media_.find(p.seq);
      if (it == sent_media_.end()) continue;
      RtpSender* sender = it->second.sender;
      if (!p.received) {
        if (sender->config().video) early_loss[sender].push_back(it->second.seq);
        continue;
      }
      early_loss.erase(sender);
      acked[sender].push_back(it->second.seq);
      sent_media_.erase(it);
    }
    for (auto& [sender, seqs] : early_loss) {
      sender->OnEarlyLoss(seqs, now);
      for (SenderPacket& p : sender->TakePackets()) Enqueue(std::move(p), now);
    }
    for (auto& [sender, seqs] : acked) sender->OnAcknowledged(seqs);
  }
  Apply(gcc_.OnTransportFeedback(fb, now), now);
  if (auto rtt = gcc_.last_feedback_max_rtt()) cwnd_.OnFeedbackRtt(*rtt);
  UpdatePushback(now, true);
  PostTargetRate(now);
  outage_.OnFeedback(now);
}

ControllerActions SendSideController::TakeActions() {
  ControllerActions a = actions_;
  actions_ = {};
  return a;
}

bool SendSideController::DropCapturedFrame() {
  const DataRate base = !allocation_configured_ ? target_
                        : cfg_.control_handler ? video_encoder_target_
                                               : AllocatedVideoTarget(allocator_.allocation("video"));
  const auto drop = cc::CongestionWindowPushback::FrameDropInterval(base, reduce_ratio(), cfg_.video_min);
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
  ApplyPushback(out);
  return out;
}

void SendSideController::ApplyPushback(SendRates& out) const {
  // Pushback acts on the whole target; the encoder drops frames and takes
  // the matching share of the video target.
  // The allocator passes the reduce ratio to the video stream (cwnd_reduce_ratio
  // in BitrateAllocationUpdate), so the encoder takes it against its own target.
  const auto drop = cc::CongestionWindowPushback::FrameDropInterval(allocation_configured_ ? out.video_target : target_, reduce_ratio(), cfg_.video_min);
  if (drop.interval > 0) {
    out.frame_drop_interval = drop.interval;
    out.video_target = std::clamp(out.video_target - out.video_target * (1.0 / drop.interval), cfg_.video_min, cfg_.video_max);
  }
}

void SendSideController::ExpireRepair(Timestamp now) {
  while (!repair_window_.empty() && repair_window_.front().time <= now - cfg_.repair_window) {
    const Released& f = repair_window_.front();
    (f.kind == PacketKind::kVideo ? video_bytes_ : repair_bytes_) -= f.bytes;
    repair_window_.pop_front();
  }
}

double SendSideController::repair_share() const {
  if (!cfg_.send_rate_stats) {
    // FecControllerDefault::UpdateFecRates: (sent_nack + sent_fec) / (video + nack + fec).
    const int64_t total = video_bytes_ + repair_bytes_;
    return total > 0 ? static_cast<double>(repair_bytes_) / static_cast<double>(total) : 0.0;
  }
  if (!last_process_.IsFinite()) return 0.0;
  // RtpVideoSender::ProtectionRequest reads RtpSenderEgress::GetSendRates (rtp_video_sender.cc:973,
  // rtp_sender_egress.cc:340): each type's rate, or zero when its tracker has none.
  const int64_t now_ms = last_process_.ms();
  const int64_t video = video_rate_.Rate(now_ms).value_or(0);
  const int64_t nack = rtx_rate_.Rate(now_ms).value_or(0);
  const int64_t fec = fec_rate_.Rate(now_ms).value_or(0);
  // FecControllerDefault::UpdateFecRates: (sent_nack + sent_fec) / (video + nack + fec).
  const int64_t total = video + nack + fec;
  return total > 0 ? static_cast<double>(static_cast<float>(nack + fec) / static_cast<float>(total)) : 0.0;
}

DataRate SendSideController::OverheadRate(DataRate rate, int64_t packet_size_bytes) const {
  if (packet_size_bytes <= 0) return DataRate::Zero();
  // packet_rate = rate / packet_size, rounded up to whole Hz; times the overhead per packet.
  const int64_t pkt_bits = packet_size_bytes * 8;
  const int64_t packets_per_sec = (rate.bps() + pkt_bits - 1) / pkt_bits;
  return DataRate::BitsPerSec(packets_per_sec * (cfg_.rtp_overhead_bytes + cfg_.transport_overhead_bytes) * 8);
}

SendSideController::VideoAllocationSplit SendSideController::SplitVideoAllocation(DataRate alloc) const {
  // RtpVideoSender::OnBitrateUpdated (rtp_video_sender.cc:885-939). has_packet_feedback_: the payload
  // rate is the allocation less the overhead rate; FecControllerDefault::UpdateFecRates keeps the share
  // RTX and FEC do not take.
  const double keep = 1.0 - std::min(repair_share(), cfg_.max_repair_share);
  const int64_t pkt = cfg_.max_packet_size + cfg_.transport_overhead_bytes;
  const DataRate payload = std::max(DataRate::Zero(), alloc - OverheadRate(alloc, pkt));
  const DataRate encoder = payload * keep;  // encoder_target_rate_bps_, not yet capped at the encoder maximum.
  // The protection is the allocation less the encoder target and its packet overhead.
  DataRate enc_overhead = encoder.IsZero() ? DataRate::Zero() : OverheadRate(encoder, pkt - cfg_.rtp_overhead_bytes);
  enc_overhead = std::min(enc_overhead, std::max(DataRate::Zero(), alloc - encoder));
  VideoAllocationSplit out;
  out.protection = std::max(DataRate::Zero(), alloc - (encoder + enc_overhead));
  // VideoSendStreamImpl::OnBitrateUpdated (video_send_stream_impl.cc:902-914): the link allocation is the
  // encoder target less the protection, taken before the target is capped at the encoder maximum, and at
  // least the capped target. Above the maximum the encoder bitrate adjuster gets the difference as headroom.
  const DataRate link = encoder > out.protection ? encoder - out.protection : DataRate::Zero();
  out.encoder_target = std::min(cfg_.video_max, encoder);
  out.link_allocation = cfg_.link_headroom ? std::max(out.encoder_target, link) : out.encoder_target;
  return out;
}

DataRate SendSideController::AllocatedVideoTarget(DataRate alloc) const { return SplitVideoAllocation(alloc).encoder_target; }

SendRates SendSideController::AllocatedRates() const {
  // VideoSendStreamImpl::OnBitrateUpdated: the encoder gets the allocation less
  // the protection bitrate, at most its maximum. A paused stream gets nothing.
  SendRates out;
  out.target = target_;
  out.audio_rate = allocator_.allocation("audio");
  if (cfg_.control_handler) {
    out.video_target = video_encoder_target_;
    out.video_link_allocation = video_link_allocation_;
  } else {
    const VideoAllocationSplit split = SplitVideoAllocation(allocator_.allocation("video"));
    out.video_target = split.encoder_target;
    out.video_link_allocation = split.link_allocation;
  }
  out.encoder_paused = cfg_.control_handler && control_handler_.encoder_paused();
  ApplyPushback(out);
  return out;
}

SendRates SendSideController::ComputeMeasuredRates(DataRate audio_wire_rate) const {
  if (allocation_configured_) return AllocatedRates();
  // Capped at kProtectionOverheadRateThreshold (0.5), as in FecControllerDefault.
  const double share = std::min(repair_share(), cfg_.max_repair_share);
  DataRate r = (target_ - audio_wire_rate) * (1.0 - share);
  SendRates out;
  out.target = target_;
  out.audio_rate = audio_wire_rate;
  out.video_target = std::clamp(r, cfg_.video_min, cfg_.video_max);
  ApplyPushback(out);
  return out;
}

void SendSideController::ExportMetrics(MetricsWriter& w) const {
  w.Counter("released", stats_.released);
  w.Counter("padding", stats_.padding);
  w.Counter("unknown_ids", stats_.unknown_ids);
  w.Counter("flushed", stats_.flushed);
  w.Counter("released_bytes", static_cast<uint64_t>(stats_.released_bytes));
  w.Gauge("target_kbps", target_.kbps());
  w.Counter("cwnd_frames_dropped", cwnd_frames_dropped_);
}

}  // namespace sm::rtp
