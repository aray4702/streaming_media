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
// RTPSender::SetMaxRtpPacketSize (rtp_sender.cc:307-312) accepts a whole packet of
// 100 to IP_PACKET_SIZE (1500) bytes and never makes a packet larger.
constexpr size_t kMinMaxPacketSize = 100;
constexpr size_t kMaxMaxPacketSize = 1500;
RtpSenderConfig ClampPacketSize(RtpSenderConfig c) {
  c.max_payload_size = std::clamp(c.max_payload_size, kMinMaxPacketSize - RtpPacket::kFixedHeaderSize,
                                  kMaxMaxPacketSize - RtpPacket::kFixedHeaderSize);
  return c;
}
loss::RtxConfig MakeRtxConfig(const RtpSenderConfig& c) {
  loss::RtxConfig r = c.rtx;
  r.rtx_ssrc = c.rtx_ssrc.value_or(0);
  r.rtx_payload_type = c.rtx_payload_type;
  if (c.fec) {
    r.red_payload_type = c.ulpfec.red_payload_type;
    r.rtx_red_payload_type = c.ulpfec.red_rtx_payload_type;
  }
  if (!r.events) r.events = c.events;
  return r;
}
// rtp_sender.cc:54-62 and rtp_sender.h:44.
constexpr size_t kMinAudioPaddingLength = 50;
constexpr size_t kMinPayloadPaddingBytes = 50;
constexpr double kMaxPaddingSizeFactor = 3.0;
constexpr size_t kMaxPaddingLength = 255;
constexpr size_t kRtxHeaderSize = 2;
}  // namespace

RtpSender::RtpSender(RtpSenderConfig cfg)
    : cfg_(ClampPacketSize(std::move(cfg))),
      rtx_(MakeRtxConfig(cfg_)),
      red_(std::max(1, cfg_.red_distance)),
      next_seq_(cfg_.initial_seq),
      mid_remaining_(cfg_.mid_packets) {
  if (cfg_.fec) {
    ulpfec_ = std::make_unique<loss::ulpfec::UlpfecGenerator>(cfg_.ulpfec.red_payload_type,
                                                              cfg_.ulpfec.ulpfec_payload_type);
  }
}

RtpPacket RtpSender::NewPacket(uint8_t pt, uint32_t rtp_timestamp, bool marker) {
  RtpPacket p;
  p.ssrc = cfg_.ssrc;
  p.payload_type = pt;
  // libwebrtc assigns the sequence number when the pacer releases the packet
  // (PacketRouter::SendPacket -> ModuleRtpRtcpImpl2::AssignSequenceNumber), so a packet
  // the pacer discards leaves no gap. AssignSequenceNumber() does it.
  p.seq = 0;
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
  out_.back().sender = this;
}

void RtpSender::AssignSequenceNumber(SenderPacket& p) {
  if (p.kind != PacketKind::kAudio && p.kind != PacketKind::kVideo && p.kind != PacketKind::kFec) return;
  p.packet.seq = next_seq_++;
}

void RtpSender::OnPacketReleased(const SenderPacket& p, Timestamp now) {
  send_rates_[static_cast<size_t>(p.kind)].Update(static_cast<int64_t>(p.packet.size()), now.ms());
  if (ulpfec_ && p.kind == PacketKind::kVideo) {
    // RtpSenderEgress::SendPacket: the media packet without its RED header (media PT
    // restored), then the FEC that it completes.
    RtpPacket media = p.packet;
    if (media.payload_type == cfg_.ulpfec.red_payload_type && !media.payload.empty()) {
      media.payload_type = media.payload[0];
      media.payload.erase(media.payload.begin());
    }
    ulpfec_->AddPacketAndGenerateFec(media, p.is_key_frame);
    for (RtpPacket& f : ulpfec_->GetFecPackets()) {
      ++stats_.fec_packets;
      stats_.fec_payload_bytes += static_cast<int64_t>(f.payload.size());
      fec_out_.push_back({std::move(f), PacketKind::kFec, cc::PacketPriority::kFec});
      fec_out_.back().sender = this;
    }
  }
  if (!cfg_.rtx_ssrc) return;
  if (p.kind == PacketKind::kAudio || p.kind == PacketKind::kVideo) {
    rtx_.OnPacketSent(p.packet, now);
  } else if (p.kind == PacketKind::kRtx && p.packet.payload.size() >= 2) {
    rtx_.MarkPacketAsSent(static_cast<uint16_t>((p.packet.payload[0] << 8) | p.packet.payload[1]), now);
  }
}

void RtpSender::StoreAndQueueMedia(RtpPacket p, Timestamp now, bool keyframe, bool first) {
  ++stats_.media_packets;
  stats_.media_payload_bytes += static_cast<int64_t>(p.payload.size());
  sr_packets_ += 1;
  sr_octets_ += static_cast<uint32_t>(p.payload.size());
  last_rtp_timestamp_ = p.timestamp;
  last_media_time_ = now;
  Queue(std::move(p), cfg_.video ? PacketKind::kVideo : PacketKind::kAudio,
        cfg_.video ? cc::PacketPriority::kVideo : cc::PacketPriority::kAudio);
  out_.back().is_key_frame = keyframe;
  out_.back().first_packet_of_frame = first;
}

size_t RtpSender::MediaHeaderSize() const {
  RtpPacket p;
  if (cfg_.extensions) {
    const sync::ExtensionAccessor access(*cfg_.extensions);
    if (cfg_.extensions->IdOf(sync::uri::kTransportCc)) access.SetTransportSeq(p, 0);
    if (cfg_.extensions->IdOf(sync::uri::kAbsSendTime)) access.SetAbsSendTime(p, Timestamp::Micros(0));
    if (mid_remaining_ > 0 && !cfg_.mid.empty()) access.SetString(p, sync::uri::kMid, cfg_.mid);
  }
  return p.HeaderSize();
}

void RtpSender::SendAudio(std::span<const uint8_t> frame, uint32_t rtp_timestamp, bool marker, Timestamp now,
                          std::optional<audio::AudioLevel> level) {
  ++stats_.frames;
  RtpPacket p;
  if (cfg_.red_payload_type) {
    p = NewPacket(*cfg_.red_payload_type, rtp_timestamp, marker);
    p.payload = red_.Encode(cfg_.payload_type, rtp_timestamp, frame);
  } else {
    p = NewPacket(cfg_.payload_type, rtp_timestamp, marker);
    p.payload.assign(frame.begin(), frame.end());
  }
  if (level && cfg_.extensions && cfg_.extensions->IdOf(sync::uri::kAudioLevel)) {
    // RFC 6464, as audio::EncodeAudioLevelExtension (sm_rtp does not link sm_audio, which is optional).
    const uint8_t byte = static_cast<uint8_t>((level->voice ? 0x80 : 0) | std::min<int>(level->level, 127));
    sync::ExtensionAccessor(*cfg_.extensions).SetRaw(p, sync::uri::kAudioLevel, {byte});
    ++stats_.audio_level_packets;
  }
  StoreAndQueueMedia(std::move(p), now);
}

void RtpSender::SendVideoFrame(std::span<const std::vector<uint8_t>> chunks, uint32_t rtp_timestamp, Timestamp now,
                               bool keyframe) {
  if (chunks.empty()) return;
  ++stats_.frames;
  for (size_t i = 0; i < chunks.size(); ++i) {
    bool last = i + 1 == chunks.size();
    RtpPacket p;
    if (ulpfec_) {
      // BuildRedPayload: one RED block header (the media PT, F bit 0), then the payload.
      p = NewPacket(cfg_.ulpfec.red_payload_type, rtp_timestamp, last);
      p.payload.reserve(loss::ulpfec::kRedForFecHeaderLength + chunks[i].size());
      p.payload.push_back(cfg_.payload_type);
      p.payload.insert(p.payload.end(), chunks[i].begin(), chunks[i].end());
    } else {
      p = NewPacket(cfg_.payload_type, rtp_timestamp, last);
      p.payload = chunks[i];
    }
    StoreAndQueueMedia(std::move(p), now, keyframe, i == 0);
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

std::vector<SenderPacket> RtpSender::GeneratePadding(size_t target_bytes, uint32_t rtp_timestamp, Timestamp now) {
  std::vector<SenderPacket> out;
  if (!cfg_.rtx_ssrc) return out;
  size_t bytes_left = target_bytes;
  const bool bwe_extension =
      cfg_.extensions && (cfg_.extensions->IdOf(sync::uri::kTransportCc) || cfg_.extensions->IdOf(sync::uri::kAbsSendTime));
  // SupportsRtxPayloadPadding: a video stream (kRtxRedundantPayloads) that has a BWE extension.
  if (cfg_.video && bwe_extension) {
    // AppendPayloadPadding.
    while (bytes_left >= kMinPayloadPaddingBytes) {
      const size_t max_overshoot = static_cast<size_t>((kMaxPaddingSizeFactor - 1.0) * static_cast<double>(target_bytes) + 0.5);
      const size_t room = max_overshoot + bytes_left;
      std::optional<RtpPacket> r = rtx_.GetPayloadPaddingPacket(
          [&](const RtpPacket& original) { return original.payload.size() + kRtxHeaderSize <= room; });
      if (!r) break;
      bytes_left -= std::min(bytes_left, r->payload.size());
      ++stats_.padding_packets;
      ++stats_.payload_padding_packets;
      SM_EVENT(cfg_.events, now, "rtp_sender", "payload_padding", r->payload.size(), r->ssrc);
      out.push_back({std::move(*r), PacketKind::kPadding, cc::PacketPriority::kPadding});
      out.back().sender = this;
    }
  }
  // Without abs-send-time or transport-cc a media packet must go first, so the timestamps
  // used for estimation are right.
  if (!bwe_extension && stats_.media_packets == 0) return out;
  const size_t padding_bytes_in_packet =
      cfg_.video ? std::min(cfg_.max_payload_size, kMaxPaddingLength)
                 : std::clamp(bytes_left, kMinAudioPaddingLength, std::min(cfg_.max_payload_size, kMaxPaddingLength));
  while (bytes_left > 0) {
    // BuildPaddingPacket on the RTX SSRC.
    RtpPacket p;
    p.timestamp = rtp_timestamp;
    p.ssrc = *cfg_.rtx_ssrc;
    p.payload_type = cfg_.rtx_payload_type;
    p.seq = rtx_.AllocateSeq();
    p.padding_size = static_cast<uint8_t>(padding_bytes_in_packet);
    ++stats_.padding_packets;
    SM_EVENT(cfg_.events, now, "rtp_sender", "padding", p.padding_size, p.ssrc);
    out.push_back({std::move(p), PacketKind::kPadding, cc::PacketPriority::kPadding});
    out.back().sender = this;
    bytes_left -= std::min(bytes_left, padding_bytes_in_packet);
  }
  return out;
}

void RtpSender::SetFecParams(const loss::ulpfec::FecProtectionParams& delta,
                             const loss::ulpfec::FecProtectionParams& key) {
  fec_params_ = delta;
  key_fec_params_ = key;
  if (ulpfec_) ulpfec_->SetProtectionParameters(delta, key);
}

std::vector<SenderPacket> RtpSender::TakeFecPackets() {
  std::vector<SenderPacket> out;
  out.swap(fec_out_);
  return out;
}

size_t RtpSender::FecPacketOverhead() const {
  if (!ulpfec_) return 0;
  // RTPSender's max_padding_fec_packet_header_: the extensions of kFecOrPaddingExtensionSizes
  // that are registered, MID at its largest (RtpMid::kMaxValueSizeBytes, 16).
  RtpPacket p;
  if (cfg_.extensions) {
    const sync::ExtensionAccessor access(*cfg_.extensions);
    if (cfg_.extensions->IdOf(sync::uri::kTransportCc)) access.SetTransportSeq(p, 0);
    if (cfg_.extensions->IdOf(sync::uri::kAbsSendTime)) access.SetAbsSendTime(p, Timestamp::Micros(0));
    if (cfg_.extensions->IdOf(sync::uri::kMid)) access.SetString(p, sync::uri::kMid, std::string(16, 'm'));
  }
  return loss::ulpfec::ForwardErrorCorrection::MaxPacketOverhead() + loss::ulpfec::kRedForFecHeaderLength +
         (p.HeaderSize() - RtpPacket::kFixedHeaderSize);
}

std::vector<SenderPacket> RtpSender::TakePackets() {
  std::vector<SenderPacket> out;
  out.swap(out_);
  return out;
}

DataRate RtpSender::SendRate(Timestamp now) const {
  int64_t bps = 0;
  for (const RateStatistics& r : send_rates_) bps += r.Rate(now.ms()).value_or(0);
  return DataRate::BitsPerSec(bps);
}

RtcpSenderState RtpSender::SenderState(Timestamp now) const {
  RtcpSenderState s;
  s.send_bitrate = SendRate(now);
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
  w.Counter("payload_padding_packets", stats_.payload_padding_packets);
  w.Counter("nacks_without_rtx", stats_.nacks_without_rtx);
  w.Counter("mid_packets", stats_.mid_packets);
  w.Counter("audio_level_packets", stats_.audio_level_packets);
}

}  // namespace sm::rtp
