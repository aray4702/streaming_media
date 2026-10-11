#include "sm/sync/stream_sync.h"

#include <algorithm>
#include <cmath>

namespace sm::sync {

void StreamSynchronization::OnAudioPacket(uint32_t rtp, Timestamp arrival) { last_audio_ = {rtp, arrival}; }
void StreamSynchronization::OnVideoFrame(uint32_t rtp, Timestamp arrival) { last_video_ = {rtp, arrival}; }

std::optional<SyncDelays> StreamSynchronization::Update(TimeDelta audio_delay, TimeDelta video_delay, Timestamp now) {
  if (!last_audio_ || !last_video_) return std::nullopt;
  auto a_cap = audio_map_.Estimate(last_audio_->first);
  auto v_cap = video_map_.Estimate(last_video_->first);
  if (!a_cap || !v_cap) return std::nullopt;
  ++updates_;
  // Network/transport delay difference for the same capture instant.
  TimeDelta relative = (last_video_->second - last_audio_->second) - (*v_cap - *a_cap);
  // Total playout offset with the extra delays currently applied.
  TimeDelta offset = relative + (video_delay + delays_.video_extra) - (audio_delay + delays_.audio_extra);
  last_offset_ = offset;
  offset_ms_.Add(std::fabs(offset.ms_f()));
  avg_offset_ms_ = has_avg_ ? avg_offset_ms_ + cfg_.filter * (offset.ms_f() - avg_offset_ms_) : offset.ms_f();
  has_avg_ = true;
  if (std::fabs(avg_offset_ms_) < cfg_.tolerance.ms_f()) return delays_;
  // Correct half the remaining offset per update, bounded.
  TimeDelta step = Clamp(TimeDelta::MillisF(avg_offset_ms_ / 2), -cfg_.max_change, cfg_.max_change);
  if (step > TimeDelta::Zero()) {
    // Video is late: take extra delay off video first, then delay audio.
    TimeDelta from_video = std::min(step, delays_.video_extra);
    delays_.video_extra -= from_video;
    delays_.audio_extra += step - from_video;
  } else {
    TimeDelta s = -step;
    TimeDelta from_audio = std::min(s, delays_.audio_extra);
    delays_.audio_extra -= from_audio;
    delays_.video_extra += s - from_audio;
  }
  delays_.audio_extra = Clamp(delays_.audio_extra, TimeDelta::Zero(), cfg_.max_extra_delay);
  delays_.video_extra = Clamp(delays_.video_extra, TimeDelta::Zero(), cfg_.max_extra_delay);
  // The average now refers to the corrected state.
  avg_offset_ms_ -= step.ms_f();
  SM_EVENT(cfg_.events, now, "av_sync", "adjust", offset.ms_f(), delays_.audio_extra.ms() - delays_.video_extra.ms());
  return delays_;
}

void StreamSynchronization::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("av_offset_ms", last_offset_.ms_f());
  w.Gauge("audio_extra_delay_ms", delays_.audio_extra.ms_f());
  w.Gauge("video_extra_delay_ms", delays_.video_extra.ms_f());
  w.Counter("updates", updates_);
  w.Hist("abs_offset_ms", offset_ms_);
}

void MediaSyncGroups::AddStream(uint32_t ssrc, const std::string& cname, Kind kind) { streams_[ssrc] = {cname, kind}; }
void MediaSyncGroups::RemoveStream(uint32_t ssrc) { streams_.erase(ssrc); }

std::vector<std::pair<uint32_t, uint32_t>> MediaSyncGroups::SyncPairs() const {
  std::map<std::string, std::pair<std::optional<uint32_t>, std::optional<uint32_t>>> by;
  for (const auto& [ssrc, info] : streams_) {
    auto& slot = by[info.cname];
    // The lowest SSRC of each kind represents the participant (stable).
    auto& target = info.kind == Kind::kAudio ? slot.first : slot.second;
    if (!target || ssrc < *target) target = ssrc;
  }
  std::vector<std::pair<uint32_t, uint32_t>> out;
  for (const auto& [cname, p] : by) {
    if (p.first && p.second) out.emplace_back(*p.first, *p.second);
  }
  return out;
}

}  // namespace sm::sync
