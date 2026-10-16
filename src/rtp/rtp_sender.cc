#include "sm/rtp/rtp_sender.h"

#include <algorithm>

namespace sm::rtp {

std::vector<size_t> SplitEvenly(size_t bytes, size_t max_payload) {
  if (max_payload == 0) max_payload = 1;
  size_t n = std::max<size_t>(1, (bytes + max_payload - 1) / max_payload);
  std::vector<size_t> out(n, bytes / n);
  for (size_t i = 0; i < bytes % n; ++i) ++out[i];
  return out;
}

namespace {
loss::RtxConfig MakeRtxConfig(const RtpSenderConfig& c) {
  loss::RtxConfig r = c.rtx;
  r.rtx_ssrc = c.rtx_ssrc.value_or(0);
  r.rtx_payload_type = c.rtx_payload_type;
  if (!r.events) r.events = c.events;
  return r;
}
loss::FecConfig MakeFecConfig(const RtpSenderConfig& c) {
  loss::FecConfig f = c.fec_config;
  if (!f.events) f.events = c.events;
  return f;
}
}  // namespace

RtpSender::RtpSender(RtpSenderConfig cfg)
    : cfg_(std::move(cfg)),
      rtx_(MakeRtxConfig(cfg_)),
      red_(std::max(1, cfg_.red_distance)),
      fec_(MakeFecConfig(cfg_)),
      next_seq_(cfg_.initial_seq),
      mid_remaining_(cfg_.mid_packets) {}

RtpPacket RtpSender::NewPacket(uint8_t pt, uint32_t rtp_timestamp, bool marker) {
  RtpPacket p;
  p.ssrc = cfg_.ssrc;
  p.payload_type = pt;
  p.seq = next_seq_++;
  p.timestamp = rtp_timestamp;
  p.marker = marker;
  if (mid_remaining_ > 0 && !cfg_.mid.empty() && cfg_.extensions) {
    sync::ExtensionAccessor(*cfg_.extensions).SetString(p, sync::uri::kMid, cfg_.mid);
    --mid_remaining_;
    ++stats_.mid_packets;
  }
  return p;
}

void RtpSender::Queue(RtpPacket p, PacketKind kind, cc::PacketPriority prio) {
  out_.push_back({std::move(p), kind, prio});
}

void RtpSender::StoreAndQueueMedia(RtpPacket p, bool end_of_group, Timestamp now, bool keyframe, bool first) {
  ++stats_.media_packets;
  stats_.media_payload_bytes += static_cast<int64_t>(p.payload.size());
  sr_packets_ += 1;
  sr_octets_ += static_cast<uint32_t>(p.payload.size());
  last_rtp_timestamp_ = p.timestamp;
  last_media_time_ = now;
  // The history holds the packet as it goes out (design 7.1), RED-wrapped
  // when RED is in use, so a resend is identical apart from the RTX header.
  if (cfg_.rtx_ssrc) rtx_.OnPacketSent(p, now);
  if (cfg_.fec) {
    for (RtpPacket& f : fec_.AddMediaPacket(p, end_of_group)) {
      ++stats_.fec_packets;
      stats_.fec_payload_bytes += static_cast<int64_t>(f.payload.size());
      Queue(std::move(f), PacketKind::kFec, cc::PacketPriority::kFec);
    }
  }
  Queue(std::move(p), cfg_.video ? PacketKind::kVideo : PacketKind::kAudio,
        cfg_.video ? cc::PacketPriority::kVideo : cc::PacketPriority::kAudio);
  out_.back().is_key_frame = keyframe;
  out_.back().first_packet_of_frame = first;
}

void RtpSender::SendAudio(std::span<const uint8_t> frame, uint32_t rtp_timestamp, bool marker, Timestamp now) {
  ++stats_.frames;
  RtpPacket p;
  if (cfg_.red_payload_type) {
    p = NewPacket(*cfg_.red_payload_type, rtp_timestamp, marker);
    p.payload = red_.Encode(cfg_.payload_type, rtp_timestamp, frame);
  } else {
    p = NewPacket(cfg_.payload_type, rtp_timestamp, marker);
    p.payload.assign(frame.begin(), frame.end());
  }
  StoreAndQueueMedia(std::move(p), true, now);
}

void RtpSender::SendVideoFrame(std::span<const std::vector<uint8_t>> chunks, uint32_t rtp_timestamp, Timestamp now,
                               bool keyframe) {
  if (chunks.empty()) return;
  ++stats_.frames;
  for (size_t i = 0; i < chunks.size(); ++i) {
    bool last = i + 1 == chunks.size();
    RtpPacket p = NewPacket(cfg_.payload_type, rtp_timestamp, last);
    p.payload = chunks[i];
    StoreAndQueueMedia(std::move(p), last, now, keyframe, i == 0);
  }
}

void RtpSender::OnNack(std::span<const uint16_t> seqs, Timestamp now, TimeDelta rtt) {
  if (!cfg_.rtx_ssrc) {
    stats_.nacks_without_rtx += seqs.size();
    return;
  }
  for (RtpPacket& r : rtx_.OnNack(seqs, now, rtt)) {
    ++stats_.rtx_packets;
    Queue(std::move(r), PacketKind::kRtx, cc::PacketPriority::kRetransmission);
  }
}

SenderPacket RtpSender::MakePadding(size_t total_bytes, uint32_t rtp_timestamp, Timestamp now) {
  RtpPacket p;
  p.timestamp = rtp_timestamp;
  if (cfg_.rtx_ssrc) {
    p.ssrc = *cfg_.rtx_ssrc;
    p.payload_type = cfg_.rtx_payload_type;
    p.seq = rtx_.AllocateSeq();
  } else {
    p.ssrc = cfg_.ssrc;
    p.payload_type = cfg_.payload_type;
    p.seq = next_seq_++;
  }
  size_t room = total_bytes > RtpPacket::kFixedHeaderSize ? total_bytes - RtpPacket::kFixedHeaderSize : 1;
  p.padding_size = static_cast<uint8_t>(std::clamp<size_t>(room, 1, 255));
  ++stats_.padding_packets;
  SM_EVENT(cfg_.events, now, "rtp_sender", "padding", p.padding_size, p.ssrc);
  return {std::move(p), PacketKind::kPadding, cc::PacketPriority::kPadding};
}

std::vector<SenderPacket> RtpSender::TakePackets() {
  std::vector<SenderPacket> out;
  out.swap(out_);
  return out;
}

RtcpSenderState RtpSender::SenderState() const {
  RtcpSenderState s;
  s.ssrc = cfg_.ssrc;
  s.packet_count = sr_packets_;
  s.octet_count = sr_octets_;
  s.rtp_timestamp = last_rtp_timestamp_;
  s.reference_time = last_media_time_.IsFinite() ? last_media_time_ : Timestamp::Millis(0);
  s.clock_rate = cfg_.clock_rate;
  return s;
}

void RtpSender::ExportMetrics(MetricsWriter& w) const {
  w.Counter("media_packets", stats_.media_packets);
  w.Counter("media_payload_bytes", static_cast<uint64_t>(stats_.media_payload_bytes));
  w.Counter("frames", stats_.frames);
  w.Counter("fec_packets", stats_.fec_packets);
  w.Counter("rtx_packets", stats_.rtx_packets);
  w.Counter("padding_packets", stats_.padding_packets);
  w.Counter("nacks_without_rtx", stats_.nacks_without_rtx);
  w.Counter("mid_packets", stats_.mid_packets);
}

}  // namespace sm::rtp
