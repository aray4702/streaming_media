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
loss::FecConfig FecCfg(const RtpReceiverConfig& c) {
  loss::FecConfig f = c.fec_config;
  if (!f.events) f.events = c.events;
  return f;
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
      fec_(FecCfg(cfg_)),
      packet_buffer_(PacketBufferCfg(cfg_)),
      keyframes_(KeyframeCfg(cfg_)),
      video_timing_(cfg_.timing),
      scheduler_(SchedulerCfg(cfg_)),
      jitter_buffer_(JitterCfg(cfg_)),
      stats_rtp_(cfg_.ssrc, cfg_.clock_rate) {}

void RtpReceiver::OnRtt(TimeDelta rtt) {
  rtt_ = rtt;
  nack_.UpdateRtt(rtt);
  keyframes_.UpdateRtt(rtt);
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
      if (PaddingOnly(p)) {
        ++stats_.padding;
        return ReceivedKind::kPadding;
      }
      ++stats_.rtx;
      if (auto m = loss::RestoreFromRtx(p, cfg_.ssrc, cfg_.payload_type)) {
        OnVideoMedia(*m, now, true);
      } else {
        ++stats_.rtx_unrestorable;
      }
      return ReceivedKind::kRtx;
    }
    if (cfg_.fec && p.ssrc == cfg_.fec_config.fec_ssrc) {
      ++stats_.fec;
      for (auto& r : fec_.OnFecPacket(p)) OnVideoMedia(r, now, false);
      return ReceivedKind::kFec;
    }
  }
  if (p.ssrc != cfg_.ssrc) {
    ++stats_.unknown_ssrc;
    SM_EVENT(cfg_.events, now, "rtp_receiver", "unknown_ssrc", p.ssrc, p.payload_type);
    return ReceivedKind::kUnknown;
  }
  if (PaddingOnly(p)) {
    ++stats_.padding;
    return ReceivedKind::kPadding;
  }
  ++stats_.media;
  stats_rtp_.OnPacket(p.seq, p.timestamp, now);
  if (cfg_.video) {
    OnVideoMedia(p, now, true);
  } else {
    OnAudioMedia(p, now);
  }
  return ReceivedKind::kMedia;
}

void RtpReceiver::OnVideoMedia(const RtpPacket& p, Timestamp now, bool feed_fec) {
  VideoPayloadInfo info = cfg_.parser ? cfg_.parser->Parse(p) : VideoPayloadInfo{};
  if (!info.valid) {
    ++stats_.invalid_payload;
    SM_EVENT(cfg_.events, now, "rtp_receiver", "invalid_payload", p.seq, p.payload.size());
    return;
  }
  const int64_t useq = unwrap_.Unwrap(p.seq);
  if (useq > highest_seq_) {
    if (highest_seq_ >= 0 && useq > highest_seq_ + 1) {
      ++stats_.gaps;
      stats_.missing += static_cast<uint64_t>(useq - highest_seq_ - 1);
    }
    highest_seq_ = useq;
  }
  if (cfg_.nack) nack_.OnReceivedPacket(p.seq, info.keyframe && info.frame_start, now);
  packet_buffer_.Insert(p, info.frame_start, now);
  if (feed_fec && cfg_.fec) {
    for (auto& r : fec_.OnMediaPacket(p)) OnVideoMedia(r, now, false);
  }
}

void RtpReceiver::OnAudioMedia(const RtpPacket& p, Timestamp now) {
  if (!cfg_.decoder) return;
  auto decode = [&](std::span<const uint8_t> frame, uint32_t ts) {
    auto pcm = cfg_.decoder->Decode(frame, ts);
    if (!pcm) {
      ++stats_.audio_decode_failures;
      return;
    }
    ++stats_.audio_frames;
    jitter_buffer_.InsertPacket(ts, std::move(*pcm), now);
  };
  if (cfg_.red) {
    for (const auto& b : red_.OnPacket(p.payload, p.timestamp)) decode(b.payload, b.timestamp);
  } else {
    decode(p.payload, p.timestamp);
  }
}

void RtpReceiver::AssembleFrames(Timestamp now) {
  for (auto& f : packet_buffer_.PopCompleteFrames()) {
    if (f.packets.empty()) continue;
    VideoPayloadInfo info = cfg_.parser ? cfg_.parser->Parse(f.packets.front()) : VideoPayloadInfo{};
    if (!info.valid || info.frame_id < 0) continue;
    ++stats_.frames_received;
    loss::FrameInfo fi;
    fi.frame_id = info.frame_id;
    fi.keyframe = info.keyframe;
    // Without a descriptor a delta frame depends on the one before it
    // (design 6.4; plan PR 7 derives the ids from sequence numbers).
    if (!info.keyframe && info.frame_id > 0) fi.references.push_back(info.frame_id - 1);
    keyframes_.OnFrame(fi, now);
    video_jitter_.OnFrame(f.timestamp, f.last_arrival, DataSize::Bytes(static_cast<int64_t>(f.size_bytes())));
    video_timing_.IncomingTimestamp(f.timestamp, f.last_arrival);
    frames_[info.frame_id] = {f.timestamp, info.keyframe, f.size_bytes()};
    while (frames_.size() > kMaxFrameMeta) {
      frames_.erase(frames_.begin());
      ++stats_.frame_meta_evicted;
    }
  }
}

void RtpReceiver::Process(Timestamp now) {
  if (!cfg_.video) return;
  AssembleFrames(now);
  for (const auto& msg : keyframes_.Process(now)) feedback_.keyframe.push_back(msg);
  if (cfg_.nack && nack_.TakeKeyframeRequest()) feedback_.pli = true;
  for (int64_t id : keyframes_.TakeDecodableFrames()) {
    auto it = frames_.find(id);
    if (it == frames_.end()) continue;
    video_timing_.SetJitterDelay(video_jitter_.JitterDelay());
    video_timing_.AddDecodeTime(cfg_.decode_time);
    video_timing_.UpdateCurrentDelay(now);
    scheduler_.Insert({id, video_timing_.RenderTime(it->second.rtp_timestamp, now)});
    ++stats_.frames_scheduled;
  }
  for (const auto& a : scheduler_.Process(now)) {
    auto it = frames_.find(a.id);
    if (it == frames_.end()) continue;
    ReceivedFrame r;
    r.frame_id = a.id;
    r.rtp_timestamp = it->second.rtp_timestamp;
    r.keyframe = it->second.keyframe;
    r.render = a.render;
    r.render_time = a.render_time;
    r.lateness = a.lateness;
    r.size_bytes = it->second.size_bytes;
    out_frames_.push_back(r);
    frames_.erase(it);
  }
  if (cfg_.nack) {
    auto batch = nack_.GetNackBatch(now);
    if (!batch.empty()) feedback_.nacks.insert(feedback_.nacks.end(), batch.begin(), batch.end());
  }
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
  w.Counter("gaps", stats_.gaps);
  w.Counter("missing", stats_.missing);
  w.Counter("audio_frames", stats_.audio_frames);
  w.Counter("audio_decode_failures", stats_.audio_decode_failures);
}

}  // namespace sm::rtp
