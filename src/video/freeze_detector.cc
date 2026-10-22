#include "sm/video/freeze_detector.h"

#include <algorithm>

namespace sm::video {

// --- FreezeDetector ----------------------------------------------------------

namespace {
// libwebrtc's Timestamp::ms() rounds to the nearest millisecond.
int64_t RoundedMs(Timestamp t) { return (t.us() + 500) / 1000; }
}  // namespace

// The freeze rule of libwebrtc's VideoQualityObserver::OnRenderedFrame
// (video/video_quality_observer2.cc): the current interval enters the 30-frame
// average before the test, freezes stay in it, the average is rounded down to
// whole ms, and the test starts once the window holds 5 samples.
void FreezeDetector::OnFrameRendered(Timestamp now) {
  ++stats_.frames_rendered;
  if (!last_.IsFinite()) {
    last_ = now;
    return;
  }
  double d = (now - last_).ms_f();
  const int64_t delay_ms = RoundedMs(now) - RoundedMs(last_);
  last_ = now;
  stats_.total_frames_duration_ms += d;
  stats_.sum_squared_frame_durations_s2 += (d / 1000.0) * (d / 1000.0);
  if (d >= 5000) {
    // libwebrtc's OnStreamInactive: the pause stays out of the average.
    ++stats_.pause_count;
    stats_.total_pauses_ms += d;
    SM_EVENT(events_, now, "freeze", "pause", d, 0);
    return;
  }
  recent_ms_.push_back(delay_ms);
  recent_sum_ += delay_ms;
  if (recent_ms_.size() > kAvgWindowFrames) {
    recent_sum_ -= recent_ms_.front();
    recent_ms_.pop_front();
  }
  if (recent_ms_.size() < kMinSamplesToDetectFreeze) return;
  const int64_t avg = recent_sum_ / static_cast<int64_t>(recent_ms_.size());  // GetAverageRoundedDown.
  if (delay_ms >= std::max(3 * avg, avg + kMinIncreaseForFreezeMs)) {
    const double freeze = static_cast<double>(delay_ms);
    ++stats_.freeze_count;
    stats_.total_freezes_ms += freeze;
    stats_.longest_freeze_ms = std::max(stats_.longest_freeze_ms, freeze);
    stats_.freeze_ms.Add(freeze);
    SM_EVENT(events_, now, "freeze", "freeze", freeze, 0);
  }
}

double FreezeDetector::HarmonicFps() const {
  if (stats_.sum_squared_frame_durations_s2 <= 0) return 0;
  return stats_.total_frames_duration_ms / 1000.0 / stats_.sum_squared_frame_durations_s2;
}

void FreezeDetector::ExportMetrics(MetricsWriter& w) const {
  w.Counter("frames_rendered", stats_.frames_rendered);
  w.Counter("freeze_count", stats_.freeze_count);
  w.Gauge("total_freezes_duration_ms", stats_.total_freezes_ms);
  w.Counter("pause_count", stats_.pause_count);
  w.Gauge("total_pauses_duration_ms", stats_.total_pauses_ms);
  w.Gauge("harmonic_fps", HarmonicFps());
  w.Hist("freeze_ms", stats_.freeze_ms);
}

}  // namespace sm::video
