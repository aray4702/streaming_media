#include "sm/rtp/rtp_receiver.h"

#include <algorithm>

#include "sm/loss/rtx.h"

namespace sm::rtp {
namespace {
// Frames that were assembled but are still waiting for a reference when this
// many newer ones have been seen are forgotten (the keyframe requester drops
// them the same way, at the next keyframe).
constexpr size_t kMaxFrameMeta = 1024;
// timing v2: the frame buffer's size, kMaxFramesBuffered (video/video_stream_buffer_controller.cc:46).
constexpr size_t kMaxFramesBuffered = 800;
// RtpVideoStreamReceiver2: "Assume frequency is the same one for all video frames."
constexpr int kVideoPayloadTypeFrequency = 90000;

loss::NackConfig NackCfg(const RtpReceiverConfig& c) {
  loss::NackConfig n = c.nack_config;
  if (!n.events) n.events = c.events;
  return n;
}
timing::PacketBufferConfig PacketBufferCfg(const RtpReceiverConfig& c) {
  timing::PacketBufferConfig p = c.packet_buffer;
  if (!p.events) p.events = c.events;
  return p;
}
loss::KeyframeRequestConfig KeyframeCfg(const RtpReceiverConfig& c) {
  loss::KeyframeRequestConfig k = c.keyframe;
  if (!k.events) k.events = c.events;
  return k;
}
video::FrameSchedulerConfig SchedulerCfg(const RtpReceiverConfig& c) {
  video::FrameSchedulerConfig f = c.scheduler;
  if (!f.events) f.events = c.events;
  return f;
}
timing::VcmTimingConfig VcmTimingCfg(const RtpReceiverConfig& c) {
  timing::VcmTimingConfig t;
  t.render_delay = c.timing.render_delay;
  t.min_playout_delay = c.timing.min_playout_delay;
  t.max_playout_delay = c.timing.max_playout_delay;
  return t;
}
timing::FrameReleaseConfig ReleaseCfg(const RtpReceiverConfig& c) {
  timing::FrameReleaseConfig r;
  r.events = c.events;
  return r;
}
// CallStats::kUpdateInterval: VideoReceiveStream2::OnRttUpdate runs once a second.
constexpr TimeDelta kRttUpdateInterval = TimeDelta::Seconds(1);
// Bound on the NACKed-packet set (timing v2); entries leave it when their frame assembles.
constexpr size_t kMaxNackedSeqs = 1024;
timing::AudioJitterBufferConfig JitterCfg(const RtpReceiverConfig& c) {
  timing::AudioJitterBufferConfig j = c.jitter_buffer;
  j.sample_rate = c.clock_rate;
  if (!j.events) j.events = c.events;
  return j;
}
bool PaddingOnly(const RtpPacket& p) { return p.payload.empty() && p.padding_size > 0; }
}  // namespace

RtpReceiver::RtpReceiver(RtpReceiverConfig cfg)
    : cfg_(std::move(cfg)),
      nack_(NackCfg(cfg_)),
      packet_buffer_(PacketBufferCfg(cfg_)),
      keyframes_(KeyframeCfg(cfg_)),
      video_timing_(cfg_.timing),
      scheduler_(SchedulerCfg(cfg_)),
      vcm_timing_(VcmTimingCfg(cfg_)),
      release_(&vcm_timing_, ReleaseCfg(cfg_)),
      render_frames_({cfg_.timing.render_delay, cfg_.events}),
      jitter_buffer_(JitterCfg(cfg_)),
      // receive_statistics_v2: RtpVideoStreamReceiver2::OnRtpPacket's statistician: 450 (kMaxPacketAgeToNack)
      // with NACK on (rtp_video_stream_receiver2.cc:365-367, :1293-1295); audio and RTX
      // keep kDefaultMaxReorderingThreshold (ChannelReceive::SetNACKStatus gives audio
      // its NACK history / 20 ms, 50 for the bench's 1000 ms, channel_receive.cc:931-943).
      stats_rtp_(cfg_.ssrc, cfg_.clock_rate, cfg_.receive_statistics_v2,
                 cfg_.video && cfg_.nack ? timing::kMaxPacketAgeToNack : timing::kDefaultMaxReorderingThreshold) {
  if (cfg_.video && cfg_.rtx_ssrc) stats_rtx_.emplace(*cfg_.rtx_ssrc, cfg_.clock_rate, cfg_.receive_statistics_v2);
  if (cfg_.video && cfg_.fec) {
    ulpfec_ = std::make_unique<loss::ulpfec::UlpfecReceiver>(cfg_.ssrc, cfg_.ulpfec.ulpfec_payload_type);
  }
  if (cfg_.video) packet_capture_times_.resize(std::max<size_t>(cfg_.packet_buffer.max_size, 1));
}

void RtpReceiver::OnRtt(TimeDelta rtt) {
  rtt_ = rtt;
  have_rtt_ = true;
  nack_.UpdateRtt(rtt);
}

void RtpReceiver::OnRtcpPacket(Timestamp now) {
  // ModuleRtpRtcpImpl2::LastRtt and GetSenderReportStats: wait for both.
  const std::optional<TimeDelta> rtt = LastRtt();
  if (!rtt || !last_sr_) return;
  // RtpVideoStreamReceiver2::DeliverRtcp: "Don't use old SRs to estimate time."
  if (cfg_.video && now - last_sr_arrival_ > TimeDelta::Millis(1)) return;
  ntp_estimator_.UpdateRtcpTimestamp(*rtt, last_sr_->ntp, last_sr_->rtp_timestamp, now);
  ++stats_.remote_ntp_updates;
  if (auto offset = ntp_estimator_.EstimateRemoteToLocalClockOffset()) {
    capture_clock_offset_updater_.SetRemoteToLocalClockOffset(*offset);
  }
}

timing::AudioOperation RtpReceiver::GetAudio(std::span<float> out, Timestamp now) {
  last_playout_time_ = now;
  const timing::AudioOperation op = jitter_buffer_.GetAudio(out, now);
  // ChannelReceive::GetAudioFrameWithInfo (channel_receive.cc:472-507): the frame's packet infos, each with
  // an absolute capture time given the local capture clock offset known now.
  audio_packet_infos_.clear();
  for (uint32_t ts : jitter_buffer_.last_decoded_timestamps()) {
    auto it = std::find_if(received_audio_packet_infos_.rbegin(), received_audio_packet_infos_.rend(),
                           [&](const AudioPacketInfo& i) { return i.rtp_timestamp == ts; });
    if (it == received_audio_packet_infos_.rend()) continue;
    AudioPacketInfo info = *it;
    if (info.absolute_capture_time) {
      info.local_capture_clock_offset = sync::CaptureClockOffsetUpdater::ConvertToTimeDelta(
          capture_clock_offset_updater_.AdjustEstimatedCaptureClockOffset(info.absolute_capture_time->clock_offset));
    }
    audio_packet_infos_.push_back(info);
  }
  return op;
}

int64_t RtpReceiver::EstimateNtpTimeMs(uint32_t rtp_timestamp) const {
  const timing::NtpTime ntp = ntp_estimator_.EstimateNtp(rtp_timestamp);
  if (ntp.value == 0) return -1;
  return sync::NtpToMs(ntp.value);
}

PacketCaptureTime RtpReceiver::OnPacketCaptureTime(const RtpPacket& p, int clock_rate, Timestamp now) {
  std::optional<sync::AbsoluteCaptureTime> received;
  if (cfg_.extensions) {
    if (auto raw = sync::ExtensionAccessor(*cfg_.extensions).GetRaw(p, sync::uri::kAbsCaptureTime)) {
      received = sync::ParseAbsoluteCaptureTime(*raw);
    }
  }
  PacketCaptureTime c;
  c.absolute_capture_time = abs_capture_interpolator_.OnPacket(
      sync::AbsoluteCaptureTimeInterpolator::GetSource(p.ssrc, p.csrcs), p.timestamp, clock_rate, received, now);
  if (received) {
    ++stats_.abs_capture_time_received;
  } else if (c.absolute_capture_time) {
    ++stats_.abs_capture_time_interpolated;
  }
  if (c.absolute_capture_time) {
    c.local_capture_clock_offset = sync::CaptureClockOffsetUpdater::ConvertToTimeDelta(
        capture_clock_offset_updater_.AdjustEstimatedCaptureClockOffset(c.absolute_capture_time->clock_offset));
  }
  return c;
}

ReceivedKind RtpReceiver::OnPacket(std::span<const uint8_t> datagram, Timestamp now) {
  auto parsed = RtpPacket::Parse(datagram);
  if (!parsed) {
    ++stats_.malformed;
    return ReceivedKind::kMalformed;
  }
  ++stats_.packets;
  CommitReceiveStatistics();
  const RtpPacket& p = *parsed;
  if (cfg_.extensions && cfg_.feedback) {
    if (auto tseq = sync::ExtensionAccessor(*cfg_.extensions).GetTransportSeq(p)) cfg_.feedback->OnPacket(*tseq, now);
  }

  if (cfg_.video) {
    if (cfg_.rtx_ssrc && p.ssrc == *cfg_.rtx_ssrc) {
      stats_rtx_->OnPacket(p.seq, p.timestamp, now);
      if (PaddingOnly(p)) {
        ++stats_.padding;
        return ReceivedKind::kPadding;
      }
      ++stats_.rtx;
      // RtxReceiveStream: the RTX payload type names the original's (RED when ULPFEC is on).
      const uint8_t original_pt =
          ulpfec_ && p.payload_type == cfg_.ulpfec.red_rtx_payload_type ? cfg_.ulpfec.red_payload_type : cfg_.payload_type;
      if (auto m = loss::RestoreFromRtx(p, cfg_.ssrc, original_pt)) {
        OnVideoRtp(*m, now, /*recovered=*/true);
      } else {
        ++stats_.rtx_unrestorable;
      }
      return ReceivedKind::kRtx;
    }
  }
  if (p.ssrc != cfg_.ssrc) {
    ++stats_.unknown_ssrc;
    SM_EVENT(cfg_.events, now, "rtp_receiver", "unknown_ssrc", p.ssrc, p.payload_type);
    return ReceivedKind::kUnknown;
  }
  if (PaddingOnly(p)) {
    // Padding on the media SSRC (no RTX configured) holds a media sequence
    // number: it is received, so it is no gap, no NACK and no loss in the RR.
    ++stats_.padding;
    CountReceived(p.seq, p.timestamp, now);
    if (cfg_.video) OnEmptyPacket(p.seq, now);
    return ReceivedKind::kPadding;
  }
  // FEC packets hold media sequence numbers: they count in the receive statistics too.
  CountReceived(p.seq, p.timestamp, now);
  if (cfg_.video) {
    const ReceivedKind kind = OnVideoRtp(p, now, /*recovered=*/false);
    if (kind == ReceivedKind::kMedia) ++stats_.media;
    return kind;
  }
  ++stats_.media;
  OnAudioMedia(p, now);
  return ReceivedKind::kMedia;
}

void RtpReceiver::CountReceived(uint16_t seq, uint32_t rtp_ts, Timestamp now) {
  if (defer_receive_statistics_ && cfg_.video) {
    pending_count_ = PendingCount{seq, rtp_ts, now};
    return;
  }
  stats_rtp_.OnPacket(seq, rtp_ts, now);
}

void RtpReceiver::CommitReceiveStatistics() {
  if (!pending_count_) return;
  const PendingCount c = *pending_count_;
  pending_count_.reset();
  stats_rtp_.OnPacket(c.seq, c.rtp_ts, c.at);
}

ReceivedKind RtpReceiver::OnVideoRtp(const RtpPacket& p, Timestamp now, bool recovered) {
  if (!ulpfec_ || p.payload_type != cfg_.ulpfec.red_payload_type) {
    OnVideoMedia(p, now, recovered);
    return ReceivedKind::kMedia;
  }
  // ParseAndHandleEncapsulatingHeader (rtp_video_stream_receiver2.cc:1482-1505).
  const bool is_fec = !p.payload.empty() && p.payload[0] == cfg_.ulpfec.ulpfec_payload_type;
  if (is_fec) {
    // So that the FEC packet's sequence number is not NACKed.
    ++stats_.fec;
    OnEmptyPacket(p.seq, now);
  }
  if (ulpfec_->AddReceivedRedPacket(p)) {
    // UlpfecReceiver::ProcessReceivedFec: the media packet out of RED, then what FEC recovered
    // (OnRecoveredPacket, which is not fed back).
    // The media packet out of RED keeps `p`'s sequence number; the rest were recovered by FEC.
    for (const RtpPacket& m : ulpfec_->ProcessReceivedFec()) OnVideoMedia(m, now, m.seq != p.seq || recovered);
  }
  return is_fec ? ReceivedKind::kFec : ReceivedKind::kMedia;
}

void RtpReceiver::OnEmptyPacket(uint16_t wire_seq, Timestamp now) {
  // ReceivePacket / ParseAndHandleEncapsulatingHeader: an empty packet goes through
  // UnwrapSequenceNumberOrRecover without a video header before NotifyReceiverOfEmptyPacket.
  if (!UnwrapSequenceNumberOrRecover(wire_seq, 0, nullptr, now)) return;
  const uint16_t seq = static_cast<uint16_t>(wire_seq + seq_shift_);
  TrackSequence(seq);
  if (cfg_.nack) {
    nack_.OnReceivedPacket(seq, false, now);
    SendSeqNumNacks(now);
  }
  packet_buffer_.NotePadding(seq);
}

// NackRequester::OnReceivedPacket sends the requests for a gap with the packet that revealed it
// (RtpVideoStreamReceiver2's RtcpFeedbackBuffer sends them once the packet is handled), not at the next
// 20 ms process tick.
void RtpReceiver::SendSeqNumNacks(Timestamp now) {
  AppendNacks(nack_.GetSeqNumNackBatch(now));
}

void RtpReceiver::AppendNacks(const std::vector<uint16_t>& batch) {
  for (uint16_t seq : batch) feedback_.nacks.push_back(static_cast<uint16_t>(seq - seq_shift_));
}

bool RtpReceiver::UnwrapSequenceNumberOrRecover(uint16_t seq, uint32_t timestamp, const VideoPayloadInfo* info,
                                                Timestamp now) {
  if (!cfg_.seq_num_discontinuity) return true;
  // kPacketBufferMaxSize (rtp_video_stream_receiver2.cc:103): PacketBuffer cannot recover dependencies
  // across more packets than it can retain.
  constexpr int64_t kPacketBufferMaxSize = 2048;
  const bool is_media = info != nullptr;
  const bool starts_frame = is_media && info->frame_start;
  const bool is_keyframe = is_media && info->keyframe;
  const bool large_backward_jump =
      newest_media_seq_.has_value() && *newest_media_seq_ - rtp_seq_unwrap_.PeekUnwrap(seq) > kPacketBufferMaxSize;
  // An RTP timestamp newer than any received media corroborates that this is resumed media, rather
  // than a severely reordered packet from an old frame.
  const bool timestamp_is_newer = newest_media_timestamp_.has_value() && AheadOf(timestamp, *newest_media_timestamp_);
  if (is_media && large_backward_jump && timestamp_is_newer && !waiting_for_keyframe_after_discontinuity_) {
    waiting_for_keyframe_after_discontinuity_ = true;
    ++stats_.seq_num_discontinuities;
    SM_EVENT(cfg_.events, now, "rtp_receiver", "seq_num_discontinuity", seq, *newest_media_seq_);
  }
  if (waiting_for_keyframe_after_discontinuity_) {
    // Re-anchor only at a frame boundary. Earlier reordered packets can be recovered by NACK. A
    // non-boundary packet that starts recovery also requests a keyframe.
    if (!(starts_frame && is_keyframe && timestamp_is_newer)) {
      if (next_discontinuity_request_ < now) {
        keyframes_.OnExternalRequest(now, "trigger_seq_num_discontinuity");
        next_discontinuity_request_ = now + cfg_.keyframe.max_wait_for_keyframe;
      }
      ++stats_.seq_num_discontinuity_dropped;
      return false;
    }
    // Drop state from the previous epoch and anchor at the recovery keyframe. The packet buffer and the
    // NACK list start again; the frame ids continue (see seq_shift_).
    packet_buffer_.Clear();
    nack_ = loss::NackTracker(NackCfg(cfg_));
    nacked_seqs_.clear();
    keyframes_.ResetPacketReceiveTimes();
    last_received_rtp_time_ = Timestamp::MinusInfinity();
    waiting_for_keyframe_after_discontinuity_ = false;
    next_discontinuity_request_ = Timestamp::MinusInfinity();
    rtp_seq_unwrap_.Reset();
    newest_media_seq_ = rtp_seq_unwrap_.Unwrap(seq);
    newest_media_timestamp_ = timestamp;
    if (highest_seq_ >= 0) seq_shift_ = static_cast<uint16_t>(highest_seq_ + 1 - seq);
    ++stats_.seq_num_reanchors;
    SM_EVENT(cfg_.events, now, "rtp_receiver", "seq_num_reanchor", seq, seq_shift_);
    return true;
  }
  // Do not let padding, FEC, or old media move the unwrapper to an ambiguous epoch. A later media
  // packet can corroborate the discontinuity.
  if (large_backward_jump) {
    ++stats_.seq_num_discontinuity_dropped;
    return false;
  }
  const int64_t unwrapped = rtp_seq_unwrap_.Unwrap(seq);
  if (is_media) {
    // UpdateNewestMediaSeqNumAndTimestamp.
    if (!newest_media_seq_ || unwrapped > *newest_media_seq_) newest_media_seq_ = unwrapped;
    if (!newest_media_timestamp_ || AheadOf(timestamp, *newest_media_timestamp_)) newest_media_timestamp_ = timestamp;
  }
  return true;
}

void RtpReceiver::TrackSequence(uint16_t seq) {
  const int64_t useq = unwrap_.Unwrap(seq);
  if (useq > highest_seq_) {
    if (highest_seq_ >= 0 && useq > highest_seq_ + 1) {
      ++stats_.gaps;
      stats_.missing += static_cast<uint64_t>(useq - highest_seq_ - 1);
    }
    highest_seq_ = useq;
  }
}

H264SpsPpsTracker::Action H264SpsPpsTracker::CopyAndFixBitstream(std::span<const VideoNaluInfo> nalus,
                                                                bool first_packet_in_frame) {
  constexpr int32_t kMaxSps = static_cast<int32_t>(kH264MaxSpsId);
  constexpr int32_t kMaxPps = static_cast<int32_t>(kH264MaxPpsId);
  for (const VideoNaluInfo& nalu : nalus) {
    switch (nalu.type) {
      case 7: {  // kSps
        if (nalu.sps_id < 0 || nalu.sps_id > kMaxSps) return Action::kDrop;  // "SPS id out of bounds"
        sps_[static_cast<size_t>(nalu.sps_id)] = true;
        break;
      }
      case 8: {  // kPps
        if (nalu.pps_id < 0 || nalu.pps_id > kMaxPps || nalu.sps_id < 0 || nalu.sps_id > kMaxSps) {
          return Action::kDrop;  // "PPS or SPS id out of bounds"
        }
        pps_sps_id_[static_cast<size_t>(nalu.pps_id)] = static_cast<int16_t>(nalu.sps_id);
        break;
      }
      case 5: {  // kIdr
        if (!first_packet_in_frame) break;
        if (nalu.pps_id < 0 || nalu.pps_id > kMaxPps) return Action::kRequestKeyframe;  // "No PPS id in IDR nalu."
        const int16_t sps_id = pps_sps_id_[static_cast<size_t>(nalu.pps_id)];
        if (sps_id < 0) return Action::kRequestKeyframe;                       // "No PPS with id .. received"
        if (!sps_[static_cast<size_t>(sps_id)]) return Action::kRequestKeyframe;  // "No SPS with id .. received"
        break;
      }
      default:
        break;
    }
  }
  return Action::kInsert;
}

void RtpReceiver::OnVideoMedia(const RtpPacket& wire, Timestamp now, bool recovered) {
  VideoPayloadInfo info = cfg_.parser ? cfg_.parser->Parse(wire) : VideoPayloadInfo{};
  if (!info.valid) {
    ++stats_.invalid_payload;
    SM_EVENT(cfg_.events, now, "rtp_receiver", "invalid_payload", wire.seq, wire.payload.size());
    return;
  }
  // ReceivePacket: UnwrapSequenceNumberOrRecover after the depacketizer, before NACK and the packet buffer.
  if (!UnwrapSequenceNumberOrRecover(wire.seq, wire.timestamp, &info, now)) return;
  if (cfg_.media_observer) cfg_.media_observer->OnVideoMediaPacket(wire.seq, now);
  // From here on, sequence numbers are the receiver's (seq_shift_ after a re-anchor).
  std::optional<RtpPacket> shifted;
  if (seq_shift_ != 0) {
    shifted = wire;
    shifted->seq = static_cast<uint16_t>(wire.seq + seq_shift_);
  }
  const RtpPacket& p = shifted ? *shifted : wire;
  // OnReceivedPayloadData: "Try to extrapolate absolute capture time if it is missing", at
  // kVideoPayloadTypeFrequency for every video packet (rtp_video_stream_receiver2.cc:780-795).
  packet_capture_times_[p.seq % packet_capture_times_.size()] =
      std::pair(p.seq, OnPacketCaptureTime(p, kVideoPayloadTypeFrequency, now));
  // RtpVideoStreamReceiver2::OnReceivedPayloadData: what GetSyncInfo reports,
  // from packets received as they were sent only. A packet restored from RTX
  // or recovered by FEC is `recovered()` there and doesn't move it
  // (UpdatePacketReceiveTimestamps), so a late retransmission of an old frame
  // doesn't look like a video delay to lip sync.
  if (!recovered) {
    last_received_rtp_timestamp_ = p.timestamp;
    last_received_rtp_time_ = now;
  }
  // UpdatePacketReceiveTimestamps runs for packets received as they were sent only.
  if (!recovered) keyframes_.OnMediaPacket(now, info.keyframe, p.timestamp);
  TrackSequence(p.seq);
  // The NACK tracker wants to know where keyframes start: the packet that
  // opens a keyframe access unit carries the parameter sets or the IDR slice.
  if (cfg_.nack) {
    // rtp_video_stream_receiver2.cc:1438: OnReceivedPacket(seq, rtp_packet.recovered()).
    const int times_nacked =
        nack_.OnReceivedPacket(p.seq, (info.idr || info.sps || info.pps) && info.frame_start, now, recovered);
    SendSeqNumNacks(now);
    // RtpFrameObject::delayed_by_retransmission: a packet of the frame had been NACKed.
    if (times_nacked > 0 && cfg_.timing.version >= 2) {
      if (nacked_seqs_.size() >= kMaxNackedSeqs) nacked_seqs_.clear();
      nacked_seqs_.insert(p.seq);
    }
  }
  // H264SpsPpsTracker::CopyAndFixBitstream: the SPS and PPS in the packet
  // are recorded by id first (a STAP-A [SPS, PPS, IDR] passes); the first
  // packet of an IDR slice whose PPS or SPS is unknown is dropped with a
  // keyframe request, and a parameter set without ids is dropped
  // (RtpVideoStreamReceiver2::OnReceivedPayloadData, kRequestKeyframe/kDrop).
  if (cfg_.sps_pps_tracker) {
    switch (sps_pps_tracker_.CopyAndFixBitstream(info.nalus, info.frame_start)) {
      case H264SpsPpsTracker::Action::kRequestKeyframe:
        ++stats_.idr_without_parameter_sets;
        SM_EVENT(cfg_.events, now, "rtp_receiver", "idr_without_parameter_sets", p.seq, 0);
        keyframes_.OnExternalRequest(now, "trigger_idr_without_parameter_sets");
        return;
      case H264SpsPpsTracker::Action::kDrop:
        ++stats_.parameter_sets_without_ids;
        SM_EVENT(cfg_.events, now, "rtp_receiver", "parameter_set_without_ids", p.seq, 0);
        return;
      case H264SpsPpsTracker::Action::kInsert:
        break;
    }
  }
  if (packet_buffer_.Insert(p, info.frame_start, now, info.idr) == timing::InsertResult::kBufferCleared) {
    // OnInsertedPacket's buffer_cleared (rtp_video_stream_receiver2.cc:1117-1123).
    keyframes_.ResetPacketReceiveTimes();
    last_received_rtp_time_ = Timestamp::MinusInfinity();
    keyframes_.OnExternalRequest(now, "trigger_packet_buffer_cleared");
  }
}

void RtpReceiver::OnAudioMedia(const RtpPacket& p, Timestamp now) {
  // ChannelReceive::OnRtpPacket: what GetSyncInfo reports.
  last_received_rtp_timestamp_ = p.timestamp;
  last_received_rtp_time_ = now;
  // ChannelReceive::OnRtpPacket interpolates at the payload type's frequency
  // (channel_receive.cc:747-754); GetAudioFrameWithInfo adds the local offset.
  const PacketCaptureTime c = OnPacketCaptureTime(p, cfg_.clock_rate, now);
  if (c.absolute_capture_time) {
    last_audio_capture_time_ = AudioCaptureTime{p.timestamp, *c.absolute_capture_time, c.local_capture_clock_offset};
  }
  // The packet's RtpPacketInfo, which NetEq keeps with its primary payload (neteq_impl.cc:316-318).
  if (received_audio_packet_infos_.size() >= kMaxAudioPacketInfos) received_audio_packet_infos_.pop_front();
  received_audio_packet_infos_.push_back(AudioPacketInfo{
      .ssrc = p.ssrc, .rtp_timestamp = p.timestamp, .receive_time = now, .absolute_capture_time = c.absolute_capture_time});
  if (!cfg_.decoder) return;
  // NetEqImpl::InsertPacketInternal: each RED block (red level 0 for the
  // primary, 1 for the redundant one) goes through the decoder's ParsePayload.
  // AudioDecoderOpusImpl::ParsePayload puts the frame's in-band FEC first,
  // at the previous frame's timestamp and codec level 1; the jitter buffer
  // keeps the higher priority of two at one timestamp. With a decoder that
  // gives a frame's duration (AudioDecoder::PacketDuration), the jitter
  // buffer gets the frame still encoded, and it is decoded when NetEq's
  // DecodeLoop would decode it: at playout, in timestamp order (#783).
  AudioDecoder* decoder = cfg_.decoder;
  auto decode = [&](std::span<const uint8_t> frame, uint32_t ts, int red_level) {
    if (const uint32_t d = decoder->FecDuration(frame); d > 0) {
      if (decoder->PacketDuration(frame) > 0) {
        ++stats_.audio_fec_frames;
        jitter_buffer_.InsertPacket(
            ts - d, d,
            [decoder, payload = std::vector<uint8_t>(frame.begin(), frame.end()), fec_ts = ts - d] {
              return decoder->DecodeFec(payload, fec_ts).value_or(std::vector<float>{});
            },
            now, {1, red_level});
      } else if (auto fec = decoder->DecodeFec(frame, ts - d)) {
        ++stats_.audio_fec_frames;
        jitter_buffer_.InsertPacket(ts - d, std::move(*fec), now, {1, red_level});
      }
    }
    if (const uint32_t samples = decoder->PacketDuration(frame); samples > 0) {
      ++stats_.audio_frames;
      jitter_buffer_.InsertPacket(
          ts, samples,
          [decoder, payload = std::vector<uint8_t>(frame.begin(), frame.end()), ts] {
            return decoder->Decode(payload, ts).value_or(std::vector<float>{});
          },
          now, {0, red_level});
      return;
    }
    auto pcm = decoder->Decode(frame, ts);
    if (!pcm) {
      ++stats_.audio_decode_failures;
      return;
    }
    ++stats_.audio_frames;
    jitter_buffer_.InsertPacket(ts, std::move(*pcm), now, {0, red_level});
  };
  if (cfg_.red) {
    // NetEqImpl::InsertPacketInternal: RedPayloadSplitter::SplitRed puts the
    // primary first, then the redundant blocks newest to oldest, and every
    // block goes on, copies included (the packet buffer and the arrival
    // history drop them). A redundant block that NetEq has not seen yet so
    // arrives after a newer timestamp, and PacketArrived counts it as
    // reordered (issue #722).
    for (const auto& b : red_.SplitForNetEq(p.payload, p.timestamp, p.payload_type))
      decode(b.payload, b.timestamp, b.red_level);
  } else {
    decode(p.payload, p.timestamp, 0);
  }
}

void RtpReceiver::AssembleFrames(Timestamp now) {
  for (auto& f : packet_buffer_.PopCompleteFrames()) {
    if (f.packets.empty()) continue;
    bool idr = false, sps = false, pps = false;
    for (const RtpPacket& pkt : f.packets) {
      VideoPayloadInfo info = cfg_.parser ? cfg_.parser->Parse(pkt) : VideoPayloadInfo{};
      idr = idr || info.idr;
      sps = sps || info.sps;
      pps = pps || info.pps;
    }
    const bool keyframe = idr && (!cfg_.keyframe_needs_parameter_sets || (sps && pps));
    ++stats_.frames_received;
    loss::FrameInfo fi;
    fi.frame_id = f.last_seq;
    fi.keyframe = keyframe;
    // Without a descriptor a delta frame depends on the one before it: the
    // frame whose last packet precedes this frame's first (design 6.4). If
    // that frame was lost, its id never arrives and the frame waits for the
    // repair or a keyframe.
    if (!keyframe) fi.references.push_back(packet_buffer_.PrecedingMediaSeq(f.first_seq));
    bool retransmitted = false;
    if (cfg_.timing.version >= 2) {
      for (const RtpPacket& pkt : f.packets) retransmitted = nacked_seqs_.erase(pkt.seq) > 0 || retransmitted;
      if (!FrameBufferAccepts(fi.frame_id, keyframe, f.timestamp, now)) continue;
    }
    keyframes_.OnFrame(fi, now);
    if (cfg_.timing.version < 2) {
      video_jitter_.OnFrame(f.timestamp, f.last_arrival, DataSize::Bytes(static_cast<int64_t>(f.size_bytes())));
      video_timing_.IncomingTimestamp(f.timestamp, f.last_arrival);
    }
    FrameMeta meta{f.timestamp, keyframe, f.size_bytes(), {}, f.last_arrival, retransmitted};
    // OnInsertedPacket: the frame's first packet's RtpPacketInfo, and RtpFrameObject's ntp_time_ms.
    const uint16_t first_seq = f.packets.front().seq;
    if (const auto& slot = packet_capture_times_[first_seq % packet_capture_times_.size()];
        slot && slot->first == first_seq) {
      meta.capture = slot->second;
    }
    meta.ntp_time_ms = EstimateNtpTimeMs(f.timestamp);
    if (cfg_.extensions && !f.packets.empty()) {
      // ConvertCVOByteToVideoRotation (rtp_cvo.h): the two rotation bits of a one-byte element.
      const auto cvo = sync::ExtensionAccessor(*cfg_.extensions).GetRaw(f.packets.back(), sync::uri::kVideoOrientation);
      if (cvo && cvo->size() == 1) meta.rotation = static_cast<video::Rotation>((*cvo)[0] & 0x3);
    }
    if (cfg_.deliver_bitstream && cfg_.parser && !cfg_.parser->Reassemble(f.packets, meta.bitstream)) {
      meta.bitstream.clear();
      ++stats_.reassembly_failures;
    }
    frames_[f.last_seq] = std::move(meta);
    while (frames_.size() > kMaxFrameMeta) {
      frames_.erase(frames_.begin());
      ++stats_.frame_meta_evicted;
    }
  }
}

bool RtpReceiver::FrameBufferAccepts(int64_t id, bool keyframe, uint32_t rtp_timestamp, Timestamp now) {
  // FrameBuffer::InsertFrame (api/video/frame_buffer.cc:84-113): a frame at or below the last decoded
  // id is refused, unless it is a keyframe with a newer RTP timestamp, which clears the buffer; in a
  // full buffer a keyframe clears it and a delta frame is refused. A refused frame never reaches the
  // decodability tracking, as VideoStreamBufferController::InsertFrame then reports nothing continuous.
  if (last_decoded_ && id <= last_decoded_->id) {
    if (!keyframe || !AheadOf(rtp_timestamp, last_decoded_->rtp_timestamp)) {
      ++stats_.frames_refused;
      SM_EVENT(cfg_.events, now, "frame_buffer", "refused_decoded", static_cast<double>(last_decoded_->id), id);
      return false;
    }
    ClearFrameBuffer(now, "clear_older_keyframe", id);
  }
  if (frames_.size() >= kMaxFramesBuffered) {
    if (!keyframe) {
      ++stats_.frames_refused;
      SM_EVENT(cfg_.events, now, "frame_buffer", "refused_full", static_cast<double>(frames_.size()), id);
      return false;
    }
    ClearFrameBuffer(now, "clear_full", id);
  }
  if (frames_.count(id) > 0) {  // Already inserted.
    ++stats_.frames_refused;
    return false;
  }
  return true;
}

void RtpReceiver::ClearFrameBuffer(Timestamp now, const char* reason, int64_t id) {
  // FrameBuffer::Clear: the buffered frames and the decoded-frame history go.
  SM_EVENT(cfg_.events, now, "frame_buffer", reason, static_cast<double>(frames_.size()), id);
  ++stats_.frame_buffer_clears;
  frames_.clear();
  last_decoded_.reset();
  keyframes_.ClearFrames();
  release_.Clear();
}

std::optional<sync::Syncable::Info> RtpReceiver::SyncInfo() const {
  if (!last_sr_ || !last_received_rtp_timestamp_ || !last_received_rtp_time_.IsFinite()) return std::nullopt;
  if (cfg_.video && cfg_.timing.version < 2) return std::nullopt;  // FrameScheduler has no VCMTiming.
  sync::Syncable::Info info;
  info.capture_time_ntp = last_sr_->ntp;
  info.capture_time_rtp = last_sr_->rtp_timestamp;
  info.latest_received_capture_rtp_timestamp = *last_received_rtp_timestamp_;
  info.latest_receive_time = last_received_rtp_time_;
  info.current_delay = cfg_.video ? vcm_timing_.TargetDelay() : TimeDelta::Millis(jitter_buffer_.FilteredCurrentDelayMs());
  return info;
}

bool RtpReceiver::SetSyncMinimumPlayoutDelay(TimeDelta delay) {
  if (cfg_.video) {
    if (cfg_.timing.version < 2) return false;
    // UpdatePlayoutDelays: the largest of the minimum delays set.
    vcm_timing_.set_min_playout_delay(std::max(delay, cfg_.timing.min_playout_delay));
    return true;
  }
  // ChannelReceive::SetMinimumPlayoutDelay clamps to kVoiceEngine{Min,Max}MinPlayoutDelay (0, 10 s).
  const int64_t ms = std::clamp<int64_t>(delay.ms(), 0, 10000);
  return jitter_buffer_.SetMinimumDelayMs(static_cast<int>(ms));
}

std::optional<sync::Syncable::PlayoutInfo> RtpReceiver::PlayoutRtpTimestamp() const {
  if (cfg_.video) return std::nullopt;
  std::optional<uint32_t> ts = jitter_buffer_.PlayoutTimestamp();
  if (!ts) return std::nullopt;
  return sync::Syncable::PlayoutInfo{last_playout_time_, *ts};
}

void RtpReceiver::Process(Timestamp now) {
  if (!cfg_.video) return;
  AssembleFrames(now);
  if (cfg_.nack && nack_.TakeKeyframeRequest()) {
    keyframes_.OnExternalRequest(now, "trigger_nack_list_full");
  }
  for (const auto& msg : keyframes_.Process(now)) feedback_.keyframe.push_back(msg);
  // IncomingVideoStream's delayed Dequeue task, if due, before this step's decoded frames.
  if (cfg_.render_smoothing) render_frames_.Process(now);
  if (cfg_.timing.version >= 2 && have_rtt_ &&
      (!last_rtt_update_.IsFinite() || now - last_rtt_update_ >= kRttUpdateInterval)) {
    vcm_timing_.OnNetworkUpdate(rtt_);
    last_rtt_update_ = now;
  }
  // Drops and releases made while frames are inserted (timing v2), emitted before Process's.
  std::vector<timing::ReleaseAction> inserted;
  for (int64_t id : keyframes_.TakeDecodableFrames()) {
    auto it = frames_.find(id);
    // RtpVideoStreamReceiver2::FrameContinuous: a continuous frame ends the repair of everything before its
    // last packet. Only a keyframe is continuous here whatever came before it; for a
    // delta frame our decodable test is looser than FrameBuffer's continuity, so it clears nothing.
    if (cfg_.nack && it != frames_.end() && it->second.keyframe) nack_.ClearUpTo(static_cast<uint16_t>(id));
    if (it == frames_.end()) continue;
    ++stats_.frames_scheduled;
    if (cfg_.timing.version >= 2) {
      const FrameMeta& m = it->second;
      release_.Insert({id, m.rtp_timestamp, m.keyframe, DataSize::Bytes(static_cast<int64_t>(m.size_bytes)),
                       m.receive_time, m.retransmitted});
      // VideoStreamBufferController::InsertFrame schedules as each frame goes in, so the first frame of a
      // batch gets its render time from the timestamps inserted before it (video_stream_buffer_controller.cc:172).
      for (const auto& a : release_.MaybeSchedule(now, cfg_.decode_time)) inserted.push_back(a);
      continue;
    }
    video_timing_.SetJitterDelay(video_jitter_.JitterDelay());
    video_timing_.AddDecodeTime(cfg_.decode_time);
    video_timing_.UpdateCurrentDelay(now);
    scheduler_.Insert({id, video_timing_.RenderTime(it->second.rtp_timestamp, now)});
  }
  auto emit = [&](int64_t id, bool render, Timestamp render_time, TimeDelta lateness) {
    // Timing v2 drops frames without decoding them; only a decoded frame re-arms the decode time-out.
    if (render || cfg_.timing.version < 2) keyframes_.OnFrameReleased(now);
    auto it = frames_.find(id);
    if (it == frames_.end()) return;
    ReceivedFrame r;
    r.frame_id = id;
    r.rtp_timestamp = it->second.rtp_timestamp;
    r.keyframe = it->second.keyframe;
    r.render = render;
    r.render_time = render_time;
    r.lateness = lateness;
    r.size_bytes = it->second.size_bytes;
    r.bitstream = std::move(it->second.bitstream);
    r.rotation = it->second.rotation;
    r.absolute_capture_time = it->second.capture.absolute_capture_time;
    r.local_capture_clock_offset = it->second.capture.local_capture_clock_offset;
    r.ntp_time_ms = it->second.ntp_time_ms;
    if (cfg_.render_smoothing && render) {
      render_pending_[id] = std::move(r);
      render_frames_.OnFrame(id, render_time, now);
    } else {
      out_frames_.push_back(std::move(r));
    }
    if (cfg_.timing.version >= 2) {
      // FrameBuffer::ExtractNextDecodableTemporalUnit / DropNextDecodableTemporalUnit: the frames up to
      // this one leave the buffer; only a decoded one enters the decoded-frame history.
      if (render) last_decoded_ = DecodedFrame{id, it->second.rtp_timestamp};
      frames_.erase(frames_.begin(), std::next(it));
    } else {
      frames_.erase(it);
    }
    // RtpVideoStreamReceiver2::FrameDecoded: the packet buffer drops what the decoded frame used.
    packet_buffer_.ClearTo(id);
  };
  if (cfg_.timing.version >= 2) {
    for (const auto& a : inserted) emit(a.id, a.decode, a.render_time, a.lateness);
    for (const auto& a : release_.Process(now, cfg_.decode_time)) emit(a.id, a.decode, a.render_time, a.lateness);
  } else {
    for (const auto& a : scheduler_.Process(now)) emit(a.id, a.render, a.render_time, a.lateness);
  }
  for (const auto& d : render_frames_.TakeDecisions()) {
    auto it = render_pending_.find(d.id);
    if (it == render_pending_.end()) continue;
    it->second.render = d.render;
    out_frames_.push_back(std::move(it->second));
    render_pending_.erase(it);
  }
  if (cfg_.nack) {
    AppendNacks(nack_.GetNackBatch(now));
  }
}

Timestamp RtpReceiver::NextProcessTime(Timestamp now) const {
  if (!cfg_.video) return Timestamp::PlusInfinity();
  Timestamp next = keyframes_.NextProcessTime();
  if (cfg_.nack) next = std::min(next, nack_.NextProcessTime());
  if (cfg_.render_smoothing) next = std::min(next, render_frames_.NextWakeup());
  next = std::min(next, cfg_.timing.version >= 2 ? release_.NextWakeup() : scheduler_.NextWakeup());
  return std::max(next, now);
}

std::vector<ReceivedFrame> RtpReceiver::TakeFrames() {
  std::vector<ReceivedFrame> out;
  out.swap(out_frames_);
  return out;
}

ReceiverFeedback RtpReceiver::TakeFeedback() {
  ReceiverFeedback out;
  std::swap(out, feedback_);
  return out;
}

void RtpReceiver::ExportMetrics(MetricsWriter& w) const {
  w.Counter("packets", stats_.packets);
  w.Counter("malformed", stats_.malformed);
  w.Counter("media", stats_.media);
  w.Counter("rtx", stats_.rtx);
  w.Counter("rtx_unrestorable", stats_.rtx_unrestorable);
  w.Counter("fec", stats_.fec);
  w.Counter("padding", stats_.padding);
  w.Counter("unknown_ssrc", stats_.unknown_ssrc);
  w.Counter("invalid_payload", stats_.invalid_payload);
  w.Counter("frames_received", stats_.frames_received);
  w.Counter("frames_scheduled", stats_.frames_scheduled);
  w.Counter("frame_meta_evicted", stats_.frame_meta_evicted);
  w.Counter("frames_refused", stats_.frames_refused);
  w.Counter("frame_buffer_clears", stats_.frame_buffer_clears);
  w.Counter("reassembly_failures", stats_.reassembly_failures);
  w.Counter("idr_without_parameter_sets", stats_.idr_without_parameter_sets);
  w.Counter("seq_num_discontinuities", stats_.seq_num_discontinuities);
  w.Counter("seq_num_discontinuity_dropped", stats_.seq_num_discontinuity_dropped);
  w.Counter("seq_num_reanchors", stats_.seq_num_reanchors);
  w.Counter("parameter_sets_without_ids", stats_.parameter_sets_without_ids);
  w.Counter("gaps", stats_.gaps);
  w.Counter("missing", stats_.missing);
  w.Counter("audio_frames", stats_.audio_frames);
  w.Counter("audio_decode_failures", stats_.audio_decode_failures);
  w.Counter("audio_fec_frames", stats_.audio_fec_frames);
  w.Counter("abs_capture_time_received", stats_.abs_capture_time_received);
  w.Counter("abs_capture_time_interpolated", stats_.abs_capture_time_interpolated);
  w.Counter("remote_ntp_updates", stats_.remote_ntp_updates);
}

}  // namespace sm::rtp
