#include "sm/rtp/rtp_receiver.h"

#include <algorithm>

#include "sm/loss/rtx.h"

namespace sm::rtp {
namespace {
// Frames that were assembled but are still waiting for a reference when this
// many newer ones have been seen are forgotten (the keyframe requester drops
// them the same way, at the next keyframe).
constexpr size_t kMaxFrameMeta = 1024;

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
      stats_rtp_(cfg_.ssrc, cfg_.clock_rate) {
  if (cfg_.video && cfg_.rtx_ssrc) stats_rtx_.emplace(*cfg_.rtx_ssrc, cfg_.clock_rate);
  if (cfg_.video && cfg_.fec) {
    ulpfec_ = std::make_unique<loss::ulpfec::UlpfecReceiver>(cfg_.ssrc, cfg_.ulpfec.ulpfec_payload_type);
  }
}

void RtpReceiver::OnRtt(TimeDelta rtt) {
  rtt_ = rtt;
  have_rtt_ = true;
  nack_.UpdateRtt(rtt);
}

ReceivedKind RtpReceiver::OnPacket(std::span<const uint8_t> datagram, Timestamp now) {
  auto parsed = RtpPacket::Parse(datagram);
  if (!parsed) {
    ++stats_.malformed;
    return ReceivedKind::kMalformed;
  }
  ++stats_.packets;
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
    stats_rtp_.OnPacket(p.seq, p.timestamp, now);
    if (cfg_.video) OnEmptyPacket(p.seq, now);
    return ReceivedKind::kPadding;
  }
  // FEC packets hold media sequence numbers: they count in the receive statistics too.
  stats_rtp_.OnPacket(p.seq, p.timestamp, now);
  if (cfg_.video) {
    const ReceivedKind kind = OnVideoRtp(p, now, /*recovered=*/false);
    if (kind == ReceivedKind::kMedia) ++stats_.media;
    return kind;
  }
  ++stats_.media;
  OnAudioMedia(p, now);
  return ReceivedKind::kMedia;
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

void RtpReceiver::OnEmptyPacket(uint16_t seq, Timestamp now) {
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
  auto batch = nack_.GetSeqNumNackBatch(now);
  feedback_.nacks.insert(feedback_.nacks.end(), batch.begin(), batch.end());
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

void RtpReceiver::OnVideoMedia(const RtpPacket& p, Timestamp now, bool recovered) {
  VideoPayloadInfo info = cfg_.parser ? cfg_.parser->Parse(p) : VideoPayloadInfo{};
  if (!info.valid) {
    ++stats_.invalid_payload;
    SM_EVENT(cfg_.events, now, "rtp_receiver", "invalid_payload", p.seq, p.payload.size());
    return;
  }
  if (cfg_.media_observer) cfg_.media_observer->OnVideoMediaPacket(p.seq, now);
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
    const int times_nacked = nack_.OnReceivedPacket(p.seq, (info.idr || info.sps || info.pps) && info.frame_start, now);
    SendSeqNumNacks(now);
    // RtpFrameObject::delayed_by_retransmission: a packet of the frame had been NACKed.
    if (times_nacked > 0 && cfg_.timing.version >= 2) {
      if (nacked_seqs_.size() >= kMaxNackedSeqs) nacked_seqs_.clear();
      nacked_seqs_.insert(p.seq);
    }
  }
  // H264SpsPpsTracker::CopyAndFixBitstream: an SPS or PPS in the packet is
  // recorded first (a STAP-A [SPS, PPS, IDR] passes); the first packet of an
  // IDR slice without both is dropped with a keyframe request
  // (RtpVideoStreamReceiver2::OnReceivedPayloadData, kRequestKeyframe).
  if (cfg_.sps_pps_tracker) {
    have_sps_ = have_sps_ || info.sps;
    have_pps_ = have_pps_ || info.pps;
    if (info.idr && info.frame_start && !(have_sps_ && have_pps_)) {
      ++stats_.idr_without_parameter_sets;
      SM_EVENT(cfg_.events, now, "rtp_receiver", "idr_without_parameter_sets", p.seq, 0);
      keyframes_.OnExternalRequest(now, "trigger_idr_without_parameter_sets");
      return;
    }
  }
  // An IDR slice that starts a packet (no parameter sets in it) may follow
  // the parameter-set packet of the same access unit.
  if (packet_buffer_.Insert(p, info.frame_start, now, info.frame_start && info.idr && !info.sps && !info.pps,
                            info.idr) == timing::InsertResult::kBufferCleared) {
    keyframes_.OnExternalRequest(now, "trigger_packet_buffer_cleared");
  }
}

void RtpReceiver::OnAudioMedia(const RtpPacket& p, Timestamp now) {
  // ChannelReceive::OnRtpPacket: what GetSyncInfo reports.
  last_received_rtp_timestamp_ = p.timestamp;
  last_received_rtp_time_ = now;
  if (!cfg_.decoder) return;
  // NetEqImpl::InsertPacketInternal: each RED block (red level 0 for the
  // primary, 1 for the redundant one) goes through the decoder's ParsePayload.
  // With version 2, AudioDecoderOpusImpl::ParsePayload puts the frame's
  // in-band FEC first, at the previous frame's timestamp and codec level 1;
  // the jitter buffer keeps the higher priority of two at one timestamp.
  const bool neteq_v2 = cfg_.jitter_buffer.version >= 2;
  auto decode = [&](std::span<const uint8_t> frame, uint32_t ts, int red_level) {
    if (neteq_v2) {
      if (const uint32_t d = cfg_.decoder->FecDuration(frame); d > 0) {
        if (auto fec = cfg_.decoder->DecodeFec(frame, ts - d)) {
          ++stats_.audio_fec_frames;
          jitter_buffer_.InsertPacket(ts - d, std::move(*fec), now, {1, red_level});
        }
      }
    }
    auto pcm = cfg_.decoder->Decode(frame, ts);
    if (!pcm) {
      ++stats_.audio_decode_failures;
      return;
    }
    ++stats_.audio_frames;
    jitter_buffer_.InsertPacket(ts, std::move(*pcm), now, {0, red_level});
  };
  if (cfg_.red) {
    for (const auto& b : red_.OnPacket(p.payload, p.timestamp)) decode(b.payload, b.timestamp, b.primary ? 0 : 1);
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
    keyframes_.OnFrame(fi, now);
    bool retransmitted = false;
    if (cfg_.timing.version >= 2) {
      for (const RtpPacket& pkt : f.packets) retransmitted = nacked_seqs_.erase(pkt.seq) > 0 || retransmitted;
    } else {
      video_jitter_.OnFrame(f.timestamp, f.last_arrival, DataSize::Bytes(static_cast<int64_t>(f.size_bytes())));
      video_timing_.IncomingTimestamp(f.timestamp, f.last_arrival);
    }
    FrameMeta meta{f.timestamp, keyframe, f.size_bytes(), {}, f.last_arrival, retransmitted};
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

std::optional<sync::Syncable::Info> RtpReceiver::SyncInfo() const {
  if (!last_sr_ || !last_received_rtp_timestamp_) return std::nullopt;
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
    if (cfg_.render_smoothing && render) {
      render_pending_[id] = std::move(r);
      render_frames_.OnFrame(id, render_time, now);
    } else {
      out_frames_.push_back(std::move(r));
    }
    frames_.erase(it);
    // RtpVideoStreamReceiver2::FrameDecoded: the packet buffer drops what the decoded frame used.
    packet_buffer_.ClearTo(id);
  };
  if (cfg_.timing.version >= 2) {
    if (have_rtt_ && (!last_rtt_update_.IsFinite() || now - last_rtt_update_ >= kRttUpdateInterval)) {
      vcm_timing_.OnNetworkUpdate(rtt_);
      last_rtt_update_ = now;
    }
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
    auto batch = nack_.GetNackBatch(now);
    if (!batch.empty()) feedback_.nacks.insert(feedback_.nacks.end(), batch.begin(), batch.end());
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
  w.Counter("reassembly_failures", stats_.reassembly_failures);
  w.Counter("idr_without_parameter_sets", stats_.idr_without_parameter_sets);
  w.Counter("gaps", stats_.gaps);
  w.Counter("missing", stats_.missing);
  w.Counter("audio_frames", stats_.audio_frames);
  w.Counter("audio_decode_failures", stats_.audio_decode_failures);
  w.Counter("audio_fec_frames", stats_.audio_fec_frames);
}

}  // namespace sm::rtp
