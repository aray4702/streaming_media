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
// rtp_video_header.h / video_codec_constants: every video payload type runs at 90 kHz.
constexpr int kVideoPayloadTypeFrequency = 90000;
// RTPSender::BuildPaddingPacket (rtp_sender.cc:497-502): padding on the RTX SSRC takes
// rtx_payload_type_map_.begin()->second, the RTX payload type of the lowest associated one. The
// map holds the media type and, with RED configured, RED's (ConfigureRtx, call/rtp_video_sender.cc:
// 245-252).
uint8_t RtxPaddingPayloadType(const RtpSenderConfig& c, const loss::RtxConfig& r) {
  return r.red_payload_type && *r.red_payload_type < c.payload_type ? r.rtx_red_payload_type : c.rtx_payload_type;
}
}  // namespace

RtpSender::RtpSender(RtpSenderConfig cfg)
    : cfg_(ClampPacketSize(std::move(cfg))),
      rtx_(MakeRtxConfig(cfg_)),
      rtx_padding_payload_type_(RtxPaddingPayloadType(cfg_, MakeRtxConfig(cfg_))),
      red_(std::max(1, cfg_.red_distance)),
      next_seq_(cfg_.initial_seq),
      next_rtx_seq_(cfg_.rtx.initial_seq),
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

void RtpSender::AssignSequenceNumber(SenderPacket& p, Timestamp now) {
  // PacketSequencer::Sequence. A packet on the RTX SSRC (a resend or padding) takes the next RTX
  // number here, at release, as libwebrtc does (packet_sequencer.cc:71-75), so a resend or padding
  // packet the pacer never sends uses no number (issue #854).
  if (p.kind == PacketKind::kRtx) {
    p.packet.seq = next_rtx_seq_++;
    return;
  }
  if (p.kind == PacketKind::kPadding) {
    if (p.packet.ssrc == cfg_.ssrc) {
      // PopulatePaddingFields on the media SSRC: the last media packet's timestamp and payload type.
      p.packet.timestamp = sequencer_last_rtp_timestamp_;
      p.packet.payload_type = static_cast<uint8_t>(sequencer_last_payload_type_);
      p.packet.seq = next_seq_++;
      return;
    }
    p.packet.seq = next_rtx_seq_++;
    if (p.packet.payload.empty()) {
      // On the RTX SSRC, empty padding only (payload padding keeps the resent packet's timestamp).
      // libwebrtc advances the timestamp once the clock is past zero, which in a call means once a
      // media packet has gone out; here time can start at 0, so that is what is checked.
      p.packet.timestamp = sequencer_last_rtp_timestamp_;
      if (sequencer_last_timestamp_time_) {
        constexpr uint32_t kTimestampTicksPerMs = 90;
        const TimeDelta since_last_media = now - *sequencer_last_timestamp_time_;
        p.packet.timestamp += static_cast<uint32_t>(since_last_media.ms()) * kTimestampTicksPerMs;
      }
    }
    return;
  }
  if (p.kind != PacketKind::kAudio && p.kind != PacketKind::kVideo && p.kind != PacketKind::kFec) return;
  p.packet.seq = next_seq_++;
  media_has_been_sent_ = true;
  // UpdateLastPacketState. A RED-wrapped video or FEC packet (is_red) gives the payload type of
  // its RED block; audio RED is not is_red, so it gives RED's.
  sequencer_last_marker_ = p.packet.marker;
  const bool is_red = cfg_.video && ulpfec_ && p.packet.payload_type == cfg_.ulpfec.red_payload_type;
  sequencer_last_payload_type_ = is_red && !p.packet.payload.empty() ? p.packet.payload[0] : p.packet.payload_type;
  sequencer_last_rtp_timestamp_ = p.packet.timestamp;
  sequencer_last_timestamp_time_ = now;
}

bool RtpSender::CanSendPaddingOnMediaSsrc() const {
  if (sequencer_last_payload_type_ == -1) return false;
  // Without RTX, video can't pad in the middle of a frame; audio frames are one packet.
  if (cfg_.video && !sequencer_last_marker_) return false;
  return true;
}

bool RtpSender::SupportsPadding() const {
  return cfg_.extensions &&
         (cfg_.extensions->IdOf(sync::uri::kTransportCc) || cfg_.extensions->IdOf(sync::uri::kAbsSendTime));
}

bool RtpSender::SupportsRtxPayloadPadding() const {
  return SupportsPadding() && cfg_.video && cfg_.rtx_ssrc.has_value();
}

void RtpSender::OnPacketReleased(const SenderPacket& p, Timestamp now) {
  send_rates_[static_cast<size_t>(p.kind)].Update(static_cast<int64_t>(p.packet.size()), now.ms());
  // RtpSenderEgress::UpdateRtpStats (rtp_sender_egress.cc:332-336, 467-493): every packet sent on
  // the media or the RTX SSRC (media, retransmission, FEC, padding) adds one packet and its payload
  // octets (RtpPacketCounter: padding is counted apart) to `transmitted`, which the SR reports
  // (ModuleRtpRtcpImpl2::GetFeedbackState, rtp_rtcp_impl2.cc:251-254).
  sr_packets_ += 1;
  sr_octets_ += static_cast<uint32_t>(p.packet.payload.size());
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

void RtpSender::AnalyzeAudioCapture(std::span<const int16_t> frame, bool muted) {
  // channel_send.cc:909-924: only while the level indication is on (the extension is registered).
  if (cfg_.extensions && cfg_.extensions->IdOf(sync::uri::kAudioLevel)) {
    if (muted && previous_frame_muted_) {
      rms_level_.AnalyzeMuted(frame.size());
    } else {
      rms_level_.Analyze(frame);
    }
  }
  previous_frame_muted_ = muted;
}

void RtpSender::SendAudio(std::span<const uint8_t> frame, uint32_t rtp_timestamp, bool marker, Timestamp now,
                          std::optional<audio::AudioLevel> level, std::optional<Timestamp> capture_time) {
  ++stats_.frames;
  // rtp_sender_audio.cc:142-157: sent periodically; the receiver interpolates the rest.
  std::optional<sync::AbsoluteCaptureTime> abs_capture_time;
  if (capture_time) {
    abs_capture_time = abs_capture_time_sender_.OnPacket(
        sync::AbsoluteCaptureTimeSender::GetSource(cfg_.ssrc, {}), rtp_timestamp, cfg_.clock_rate,
        {timing::NtpTime::FromTimestamp(*capture_time).value, 0}, now);
  }
  // The payload type of the packet (RED's when RED is on, as ACM's RED encoder reports it), and ACM's
  // frame type: a DTX frame (at most 2 bytes) is kAudioFrameCN (audio_coding_module.cc:297-298).
  const int payload_type = cfg_.red_payload_type ? *cfg_.red_payload_type : cfg_.payload_type;
  marker = AudioMarkerBit(frame.size() > 2, payload_type) || marker;
  RtpPacket p;
  if (cfg_.red_payload_type) {
    p = NewPacket(*cfg_.red_payload_type, rtp_timestamp, marker);
    p.payload = red_.Encode(cfg_.payload_type, rtp_timestamp, frame);
  } else {
    p = NewPacket(cfg_.payload_type, rtp_timestamp, marker);
    p.payload.assign(frame.begin(), frame.end());
  }
  if (cfg_.extensions && cfg_.extensions->IdOf(sync::uri::kAudioLevel)) {
    // channel_send.cc:389-396: the average since the last packet, taken (and reset) on every packet,
    // then the override. rtp_sender_audio.cc:248-250: voice = a speech frame, not DTX.
    const int average = rms_level_.Average();
    if (!level) level = audio::AudioLevel{static_cast<uint8_t>(average), frame.size() > 2};
    // RFC 6464, as audio::EncodeAudioLevelExtension (sm_rtp does not link sm_audio, which is optional).
    const uint8_t byte = static_cast<uint8_t>((level->voice ? 0x80 : 0) | std::min<int>(level->level, 127));
    sync::ExtensionAccessor(*cfg_.extensions).SetRaw(p, sync::uri::kAudioLevel, {byte});
    ++stats_.audio_level_packets;
  }
  // rtp_sender_audio.cc:252-256: SetExtension writes nothing when it is not registered.
  if (SetsAbsoluteCaptureTime(abs_capture_time)) SetAbsoluteCaptureTime(p, *abs_capture_time);
  StoreAndQueueMedia(std::move(p), now);
  last_payload_type_ = payload_type;
}

bool RtpSender::AudioMarkerBit(bool speech, int payload_type) {
  // RTPSenderAudio::MarkerBit (rtp_sender_audio.cc:88-128). No CNG payload types are registered
  // (Opus's DTX is in band), so the CNG branch never applies.
  bool marker_bit = false;
  if (last_payload_type_ != payload_type) {
    if (last_payload_type_ == -1) {
      if (speech) return true;  // First packet and not CNG.
      inband_vad_active_ = true;
      return false;
    }
    marker_bit = true;  // The payload type changed.
  }
  if (!speech) {
    inband_vad_active_ = true;
  } else if (inband_vad_active_) {
    inband_vad_active_ = false;
    marker_bit = true;
  }
  return marker_bit;
}

bool RtpSender::SetsVideoRotation(bool keyframe, video::Rotation rotation) const {
  // rtp_sender_video.cc:388-408: on a keyframe or a change (TS 26.114 7.4.5), or when not 0
  // (for the current receivers). SetExtension writes nothing when the extension is not registered.
  return cfg_.extensions && cfg_.extensions->IdOf(sync::uri::kVideoOrientation) &&
         (keyframe || rotation != last_rotation_ || rotation != video::Rotation::k0);
}

bool RtpSender::SetsAbsoluteCaptureTime(const std::optional<sync::AbsoluteCaptureTime>& a) const {
  return a && cfg_.extensions && cfg_.extensions->IdOf(sync::uri::kAbsCaptureTime);
}

void RtpSender::SetAbsoluteCaptureTime(RtpPacket& p, const sync::AbsoluteCaptureTime& a) {
  sync::ExtensionAccessor(*cfg_.extensions).SetRaw(p, sync::uri::kAbsCaptureTime, sync::SerializeAbsoluteCaptureTime(a));
  ++stats_.abs_capture_time_packets;
}

std::optional<sync::AbsoluteCaptureTime> RtpSender::VideoAbsoluteCaptureTime(uint32_t rtp_timestamp,
                                                                             std::optional<Timestamp> capture_time,
                                                                             Timestamp now) {
  if (!capture_time) return std::nullopt;
  // rtp_sender_video.cc:690-707: Int64MsToUQ32x32(ConvertTimestampToNtpTime(capture_time).ToMs()),
  // estimated_capture_clock_offset 0; then AbsoluteCaptureTimeSender decides.
  const int64_t ntp_ms = sync::NtpToMs(timing::NtpTime::FromTimestamp(*capture_time).value);
  const sync::AbsoluteCaptureTime actual{sync::Int64MsToUQ32x32(ntp_ms), 0};
  return abs_capture_time_sender_.OnPacket(sync::AbsoluteCaptureTimeSender::GetSource(cfg_.ssrc, {}), rtp_timestamp,
                                           kVideoPayloadTypeFrequency, actual, now);
}

size_t RtpSender::HeaderSizeWith(std::optional<video::Rotation> rotation,
                                 const std::optional<sync::AbsoluteCaptureTime>& abs_capture_time) const {
  // Simplest way to estimate how much extensions would occupy is to set them (rtp_sender_video.cc:717),
  // in AddRtpHeaderExtensions' order: CVO, then the absolute capture time.
  RtpPacket p;
  if (!cfg_.extensions) return p.HeaderSize();
  const sync::ExtensionAccessor access(*cfg_.extensions);
  if (cfg_.extensions->IdOf(sync::uri::kTransportCc)) access.SetTransportSeq(p, 0);
  if (cfg_.extensions->IdOf(sync::uri::kAbsSendTime)) access.SetAbsSendTime(p, Timestamp::Micros(0));
  if (mid_remaining_ > 0 && !cfg_.mid.empty()) access.SetString(p, sync::uri::kMid, cfg_.mid);
  if (rotation) access.SetRaw(p, sync::uri::kVideoOrientation, {static_cast<uint8_t>(*rotation)});
  if (SetsAbsoluteCaptureTime(abs_capture_time)) {
    access.SetRaw(p, sync::uri::kAbsCaptureTime, sync::SerializeAbsoluteCaptureTime(*abs_capture_time));
  }
  return p.HeaderSize();
}

size_t RtpSender::LastPacketReduction(bool keyframe, video::Rotation rotation) const {
  if (!SetsVideoRotation(keyframe, rotation)) return 0;
  return HeaderSizeWith(rotation, std::nullopt) - HeaderSizeWith(std::nullopt, std::nullopt);
}

size_t RtpSender::FirstPacketReduction(const std::optional<sync::AbsoluteCaptureTime>& abs_capture_time) const {
  if (!SetsAbsoluteCaptureTime(abs_capture_time)) return 0;
  return HeaderSizeWith(std::nullopt, abs_capture_time) - HeaderSizeWith(std::nullopt, std::nullopt);
}

size_t RtpSender::SinglePacketReduction(bool keyframe, video::Rotation rotation,
                                        const std::optional<sync::AbsoluteCaptureTime>& abs_capture_time) const {
  const std::optional<video::Rotation> cvo =
      SetsVideoRotation(keyframe, rotation) ? std::optional(rotation) : std::nullopt;
  return HeaderSizeWith(cvo, abs_capture_time) - HeaderSizeWith(std::nullopt, std::nullopt);
}

size_t RtpSender::SendVideoFrame(std::span<const std::vector<uint8_t>> chunks, uint32_t rtp_timestamp, Timestamp now,
                                 bool keyframe, video::Rotation rotation,
                                 const std::optional<sync::AbsoluteCaptureTime>& abs_capture_time) {
  if (chunks.empty()) return 0;
  size_t packetized = 0;
  ++stats_.frames;
  const bool set_rotation = SetsVideoRotation(keyframe, rotation);
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
    if (last && set_rotation) {
      // ConvertVideoRotationToCVOByte (rtp_cvo.h): |0 0 0 0 C F R R| with C and F 0.
      sync::ExtensionAccessor(*cfg_.extensions).SetRaw(p, sync::uri::kVideoOrientation, {static_cast<uint8_t>(rotation)});
      ++stats_.video_orientation_packets;
    }
    packetized += p.payload.size();
    // rtp_sender_video.cc:425-428: on the frame's first packet.
    if (i == 0 && SetsAbsoluteCaptureTime(abs_capture_time)) SetAbsoluteCaptureTime(p, *abs_capture_time);
    StoreAndQueueMedia(std::move(p), now, keyframe, i == 0);
  }
  // Update details about the last sent frame (rtp_sender_video.cc:890).
  last_rotation_ = rotation;
  return packetized;
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

void RtpSender::OnEarlyLoss(std::span<const uint16_t> seqs, Timestamp now) {
  // Without RTX, libwebrtc would resend on the media SSRC; this sender only
  // retransmits over RTX, as OnNack.
  if (!cfg_.rtx_ssrc) return;
  for (uint16_t seq : seqs) {
    std::optional<RtpPacket> r = rtx_.ReSendEarlyLoss(seq, now);
    if (!r) continue;
    ++stats_.rtx_packets;
    Queue(std::move(*r), PacketKind::kRtx, cc::PacketPriority::kRetransmission);
  }
}

SenderPacket RtpSender::MakePadding(size_t total_bytes, uint32_t rtp_timestamp, Timestamp now) {
  RtpPacket p;
  p.timestamp = rtp_timestamp;
  if (cfg_.rtx_ssrc) {
    p.ssrc = *cfg_.rtx_ssrc;
    p.payload_type = rtx_padding_payload_type_;
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

std::vector<SenderPacket> RtpSender::GeneratePadding(size_t target_bytes, Timestamp now) {
  std::vector<SenderPacket> out;
  size_t bytes_left = target_bytes;
  if (SupportsRtxPayloadPadding()) {
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
  const bool rtx = cfg_.rtx_ssrc.has_value();
  if (rtx) {
    // Without abs-send-time or transport-cc a media packet must go first, so the timestamps
    // used for estimation are right.
    if (!media_has_been_sent_ && !SupportsPadding()) return out;
  } else if (!CanSendPaddingOnMediaSsrc()) {
    return out;
  }
  // PaddingBytesInPacket with max_packet_size_ - max_padding_fec_packet_header_ (rtp_sender.cc:150-165,
  // 434-435): the padding packet's header and extensions fit in the whole-packet limit (issue #513).
  const size_t max_packet_size = cfg_.max_payload_size + RtpPacket::kFixedHeaderSize;
  const size_t header = PaddingFecPacketHeaderSize();
  const size_t max_padding = std::min(max_packet_size > header ? max_packet_size - header : 1, kMaxPaddingLength);
  const size_t padding_bytes_in_packet =
      cfg_.video ? max_padding : std::clamp(bytes_left, std::min(kMinAudioPaddingLength, max_padding), max_padding);
  while (bytes_left > 0) {
    // BuildPaddingPacket: on the RTX SSRC, else on the media SSRC (its payload type, timestamp and
    // sequence number are set at release).
    RtpPacket p;
    if (rtx) {
      p.ssrc = *cfg_.rtx_ssrc;
      p.payload_type = rtx_padding_payload_type_;
    } else {
      p.ssrc = cfg_.ssrc;
    }
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

size_t RtpSender::PaddingFecPacketHeaderSize() const {
  // The extensions of kFecOrPaddingExtensionSizes that are registered, MID at its largest
  // (RtpMid::kMaxValueSizeBytes, 16).
  RtpPacket p;
  if (cfg_.extensions) {
    const sync::ExtensionAccessor access(*cfg_.extensions);
    if (cfg_.extensions->IdOf(sync::uri::kTransportCc)) access.SetTransportSeq(p, 0);
    if (cfg_.extensions->IdOf(sync::uri::kAbsSendTime)) access.SetAbsSendTime(p, Timestamp::Micros(0));
    if (cfg_.extensions->IdOf(sync::uri::kMid)) access.SetString(p, sync::uri::kMid, std::string(16, 'm'));
  }
  return p.HeaderSize();
}

size_t RtpSender::RtxPacketOverhead() const {
  if (!cfg_.rtx_ssrc) return 0;
  size_t overhead = 0;
  // always_send_mid_and_rid is false (RtpRtcpInterface::Configuration's default): the RTX packets
  // would still need the MID the media packets no longer carry.
  if (!rtx_ssrc_has_acked_ && ssrc_has_acked_) {
    constexpr size_t kRtpExtensionHeaderSize = 2;  // Two-byte header form, the larger.
    if (!cfg_.mid.empty()) overhead += kRtpExtensionHeaderSize + cfg_.mid.size();
    // Extensions are rounded up to 4 bytes: adding MID and RRID may add up to 3 bytes of padding.
    overhead += 3;
  }
  constexpr size_t kRtxHeaderSize = 2;  // The original sequence number.
  return overhead + kRtxHeaderSize;
}

size_t RtpSender::ExpectedPerPacketOverhead() const {
  // always_send_mid_and_rid is false (RtpRtcpInterface::Configuration's default), and this sender has no
  // RID, so of the BUNDLE extensions only MID counts, while it is still sent on either SSRC.
  const bool send_mid_rid_on_rtx = cfg_.rtx_ssrc.has_value() && !rtx_ssrc_has_acked_;
  const bool send_mid_rid = !ssrc_has_acked_;
  RtpPacket p;
  if (cfg_.extensions) {
    const sync::ExtensionAccessor access(*cfg_.extensions);
    if (cfg_.extensions->IdOf(sync::uri::kTransportCc)) access.SetTransportSeq(p, 0);
    if (cfg_.extensions->IdOf(sync::uri::kAbsSendTime)) access.SetAbsSendTime(p, Timestamp::Micros(0));
    if (!cfg_.video && cfg_.extensions->IdOf(sync::uri::kAudioLevel)) access.SetRaw(p, sync::uri::kAudioLevel, {0});
    if ((send_mid_rid || send_mid_rid_on_rtx) && !cfg_.mid.empty() && cfg_.extensions->IdOf(sync::uri::kMid)) {
      access.SetString(p, sync::uri::kMid, std::string(16, 'm'));
    }
  }
  constexpr size_t kRtxHeaderSize = 2;  // Reserved in case the packet is resent in an RTX packet.
  return p.HeaderSize() + (cfg_.rtx_ssrc ? kRtxHeaderSize : 0);
}

size_t RtpSender::FecPacketOverhead() const {
  if (!ulpfec_) return 0;
  return loss::ulpfec::ForwardErrorCorrection::MaxPacketOverhead() + loss::ulpfec::kRedForFecHeaderLength +
         (PaddingFecPacketHeaderSize() - RtpPacket::kFixedHeaderSize);
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
  w.Counter("video_orientation_packets", stats_.video_orientation_packets);
  w.Counter("abs_capture_time_packets", stats_.abs_capture_time_packets);
}

}  // namespace sm::rtp
