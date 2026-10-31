#include "sm/sync/stream_sync.h"

#include <algorithm>
#include <cstdlib>

namespace sm::sync {

namespace {

// NtpTime::ToMs (system_wrappers/include/ntp_time.h).
int64_t NtpToMs(timing::NtpTime ntp) {
  constexpr double kNtpFracPerMs = 4.294967296E6;  // 2^32 / 1000.
  const double frac_ms = static_cast<double>(ntp.value & 0xFFFFFFFFu) / kNtpFracPerMs;
  return 1000 * static_cast<int64_t>(ntp.value >> 32) + static_cast<int64_t>(frac_ms + 0.5);
}

// rtp_streams_synchronizer2.cc UpdateMeasurements.
bool UpdateMeasurements(StreamSynchronization::Measurements* stream, const Syncable::Info& info) {
  stream->latest_timestamp = info.latest_received_capture_rtp_timestamp;
  stream->latest_receive_time_ms = info.latest_receive_time.ms();
  return stream->rtp_to_ntp.UpdateMeasurements(info.capture_time_ntp, info.capture_time_rtp) !=
         RtpToNtpEstimator::UpdateResult::kInvalidMeasurement;
}

}  // namespace

std::optional<int> StreamSynchronization::ComputeRelativeDelay(const Measurements& audio, const Measurements& video) {
  timing::NtpTime audio_last_capture = audio.rtp_to_ntp.EstimateNtp(audio.latest_timestamp);
  if (audio_last_capture.value == 0) return std::nullopt;
  timing::NtpTime video_last_capture = video.rtp_to_ntp.EstimateNtp(video.latest_timestamp);
  if (video_last_capture.value == 0) return std::nullopt;
  // Positive: video is behind audio.
  int64_t relative = video.latest_receive_time_ms - audio.latest_receive_time_ms -
                     (NtpToMs(video_last_capture) - NtpToMs(audio_last_capture));
  if (relative > kMaxDeltaDelayMs || relative < -kMaxDeltaDelayMs) return std::nullopt;
  return static_cast<int>(relative);
}

std::optional<StreamSynchronization::DelayTargets> StreamSynchronization::ComputeDelays(int relative_delay_ms,
                                                                                         int current_audio_delay_ms,
                                                                                         int current_video_delay_ms) {
  // Difference between the lowest possible video delay and the current
  // audio delay.
  int current_diff_ms = current_video_delay_ms - current_audio_delay_ms + relative_delay_ms;
  avg_diff_ms_ = ((kFilterLength - 1) * avg_diff_ms_ + current_diff_ms) / kFilterLength;
  if (std::abs(avg_diff_ms_) < kMinDeltaMs) return std::nullopt;  // Within the margin.
  // Don't move too fast.
  int diff_ms = std::clamp(avg_diff_ms_ / 2, -kMaxChangeMs, kMaxChangeMs);
  // Reset the average after a move to prevent an overshooting reaction.
  avg_diff_ms_ = 0;
  if (diff_ms > 0) {
    // Video is late: remove extra video delay before adding audio delay.
    if (video_delay_.extra_ms > base_target_delay_ms_) {
      video_delay_.extra_ms -= diff_ms;
      audio_delay_.extra_ms = base_target_delay_ms_;
    } else {
      audio_delay_.extra_ms += diff_ms;
      video_delay_.extra_ms = base_target_delay_ms_;
    }
  } else {
    // Video is early: remove extra audio delay before adding video delay.
    if (audio_delay_.extra_ms > base_target_delay_ms_) {
      audio_delay_.extra_ms += diff_ms;
      video_delay_.extra_ms = base_target_delay_ms_;
    } else {
      video_delay_.extra_ms -= diff_ms;
      audio_delay_.extra_ms = base_target_delay_ms_;
    }
  }
  // Video is never below the target.
  video_delay_.extra_ms = std::max(video_delay_.extra_ms, base_target_delay_ms_);
  // Only one stream changes at a time: the other keeps its last output.
  int new_video_delay_ms = video_delay_.extra_ms > base_target_delay_ms_ ? video_delay_.extra_ms : video_delay_.last_ms;
  new_video_delay_ms = std::max(new_video_delay_ms, video_delay_.extra_ms);
  new_video_delay_ms = std::min(new_video_delay_ms, base_target_delay_ms_ + kMaxDeltaDelayMs);
  int new_audio_delay_ms = audio_delay_.extra_ms > base_target_delay_ms_ ? audio_delay_.extra_ms : audio_delay_.last_ms;
  new_audio_delay_ms = std::max(new_audio_delay_ms, audio_delay_.extra_ms);
  new_audio_delay_ms = std::min(new_audio_delay_ms, base_target_delay_ms_ + kMaxDeltaDelayMs);
  video_delay_.last_ms = new_video_delay_ms;
  audio_delay_.last_ms = new_audio_delay_ms;
  return DelayTargets{new_audio_delay_ms, new_video_delay_ms};
}

void StreamSynchronization::SetTargetBufferingDelay(int target_delay_ms) {
  // Initial extra delay for audio (accounting for existing extra delay).
  audio_delay_.extra_ms += target_delay_ms - base_target_delay_ms_;
  audio_delay_.last_ms += target_delay_ms - base_target_delay_ms_;
  // The video delay is compared to the last value (and how much it can move
  // is limited by that as well).
  video_delay_.last_ms += target_delay_ms - base_target_delay_ms_;
  video_delay_.extra_ms += target_delay_ms - base_target_delay_ms_;
  base_target_delay_ms_ = target_delay_ms;
}

void StreamSynchronization::ReduceAudioDelay() {
  audio_delay_.extra_ms = static_cast<int>(static_cast<float>(audio_delay_.extra_ms) * 0.9f);
}
void StreamSynchronization::ReduceVideoDelay() {
  video_delay_.extra_ms = static_cast<int>(static_cast<float>(video_delay_.extra_ms) * 0.9f);
}

void RtpStreamsSynchronizer::ConfigureSync(Syncable* syncable_audio, Timestamp now) {
  if (syncable_audio == syncable_audio_) return;  // Prevent expensive no-ops.
  syncable_audio_ = syncable_audio;
  sync_.reset();
  if (!syncable_audio_) {
    next_run_.reset();
    return;
  }
  sync_.emplace(syncable_video_->id(), syncable_audio_->id());
  if (!next_run_) next_run_ = now + kSyncInterval;
}

void RtpStreamsSynchronizer::Process(Timestamp now) {
  if (!next_run_ || now < *next_run_) return;
  UpdateDelay(now);
  // RepeatingTask::operator() (repeating_task.cc:86-89): the next run is
  // kSyncInterval after the scheduled time, not after `now`; the lateness is
  // taken off the delay, at least zero, so a run more than an interval late
  // is due again at once and the schedule stays on its 1 s grid.
  *next_run_ += kSyncInterval;
}

void RtpStreamsSynchronizer::UpdateDelay(Timestamp now) {
  if (!syncable_audio_) return;
  ++runs_;
  int64_t last_audio_receive_time_ms = audio_measurement_.latest_receive_time_ms;
  std::optional<Syncable::Info> audio_info = syncable_audio_->GetInfo();
  if (!audio_info || !UpdateMeasurements(&audio_measurement_, *audio_info)) return;
  // No new audio packet since the last update.
  if (last_audio_receive_time_ms == audio_measurement_.latest_receive_time_ms) return;
  int64_t last_video_receive_ms = video_measurement_.latest_receive_time_ms;
  std::optional<Syncable::Info> video_info = syncable_video_->GetInfo();
  if (!video_info || !UpdateMeasurements(&video_measurement_, *video_info)) return;
  // No new video packet since the last update.
  if (last_video_receive_ms == video_measurement_.latest_receive_time_ms) return;
  std::optional<int> relative = StreamSynchronization::ComputeRelativeDelay(audio_measurement_, video_measurement_);
  if (!relative) return;
  last_relative_delay_ms_ = *relative;
  // The extra audio delay and the total video delay that put the streams
  // in sync (the video target starts at its current delay).
  auto targets = sync_->ComputeDelays(*relative, static_cast<int>(audio_info->current_delay.ms()),
                                      static_cast<int>(video_info->current_delay.ms()));
  if (!targets) return;
  ++updates_;
  last_targets_ = *targets;
  SM_EVENT(events_, now, "av_sync", "delays", targets->total_audio_delay_target_ms,
           targets->total_video_delay_target_ms);
  if (!syncable_audio_->SetMinimumPlayoutDelay(TimeDelta::Millis(targets->total_audio_delay_target_ms))) {
    ++failed_sets_;
    sync_->ReduceAudioDelay();
  }
  if (!syncable_video_->SetMinimumPlayoutDelay(TimeDelta::Millis(targets->total_video_delay_target_ms))) {
    ++failed_sets_;
    sync_->ReduceVideoDelay();
  }
}

std::optional<RtpStreamsSynchronizer::StreamSyncOffset> RtpStreamsSynchronizer::GetStreamSyncOffsetInMs(
    uint32_t rtp_timestamp, int64_t render_time_ms, Timestamp now) const {
  if (!syncable_audio_) return std::nullopt;
  std::optional<Syncable::PlayoutInfo> audio = syncable_audio_->GetPlayoutRtpTimestamp();
  if (!audio) return std::nullopt;
  timing::NtpTime latest_audio_ntp = audio_measurement_.rtp_to_ntp.EstimateNtp(audio->rtp_timestamp);
  if (latest_audio_ntp.value == 0) return std::nullopt;
  int64_t latest_audio_ntp_ms = NtpToMs(latest_audio_ntp);
  syncable_audio_->SetEstimatedPlayoutNtpTimestamp(latest_audio_ntp, audio->time);
  timing::NtpTime latest_video_ntp = video_measurement_.rtp_to_ntp.EstimateNtp(rtp_timestamp);
  if (latest_video_ntp.value == 0) return std::nullopt;
  int64_t latest_video_ntp_ms = NtpToMs(latest_video_ntp);
  // Current audio NTP.
  latest_audio_ntp_ms += (now - audio->time).ms();
  // Remove the video playout delay.
  int64_t time_to_render_ms = render_time_ms - now.ms();
  if (time_to_render_ms > 0) latest_video_ntp_ms -= time_to_render_ms;
  return StreamSyncOffset{latest_video_ntp_ms, latest_audio_ntp_ms - latest_video_ntp_ms,
                          video_measurement_.rtp_to_ntp.EstimatedFrequencyKhz()};
}

void RtpStreamsSynchronizer::ExportMetrics(MetricsWriter& w) const {
  w.Counter("runs", runs_);
  w.Counter("updates", updates_);
  w.Counter("failed_sets", failed_sets_);
  w.Gauge("relative_delay_ms", last_relative_delay_ms_);
  w.Gauge("audio_target_delay_ms", last_targets_.total_audio_delay_target_ms);
  w.Gauge("video_target_delay_ms", last_targets_.total_video_delay_target_ms);
}

void MediaSyncGroups::AddStream(uint32_t ssrc, const std::string& sync_group, Kind kind) {
  for (Stream& s : streams_) {
    if (s.ssrc == ssrc) {
      s.sync_group = sync_group;
      s.kind = kind;
      return;
    }
  }
  streams_.push_back({ssrc, sync_group, kind});
}

void MediaSyncGroups::RemoveStream(uint32_t ssrc) {
  std::erase_if(streams_, [ssrc](const Stream& s) { return s.ssrc == ssrc; });
}

std::vector<std::pair<uint32_t, uint32_t>> MediaSyncGroups::SyncPairs() const {
  std::vector<std::pair<uint32_t, uint32_t>> out;
  std::vector<const std::string*> configured;
  for (const Stream& video : streams_) {
    if (video.kind != Kind::kVideo || video.sync_group.empty()) continue;
    // Call::ConfigureSync: only the group's first video stream gets the audio.
    bool seen = false;
    for (const std::string* g : configured) seen = seen || *g == video.sync_group;
    if (seen) continue;
    configured.push_back(&video.sync_group);
    // FindAudioStreamForSyncGroup: the group's first audio stream, if any.
    for (const Stream& audio : streams_) {
      if (audio.kind == Kind::kAudio && audio.sync_group == video.sync_group) {
        out.emplace_back(audio.ssrc, video.ssrc);
        break;
      }
    }
  }
  return out;
}

}  // namespace sm::sync
