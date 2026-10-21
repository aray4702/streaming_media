// 7.1 Freeze detection (the WebRTC getStats freeze definition).
//
// A freeze is an inter-frame interval of at least
//     max(3 * avg_frame_duration, avg_frame_duration + 150 ms)
// as libwebrtc's VideoQualityObserver::OnRenderedFrame counts it
// (video/video_quality_observer2.cc): intervals are in whole ms, the average
// is over the last 30 intervals including the current one and earlier
// freezes, rounded down to whole ms, and the test starts once 5 intervals
// are in it. Gaps of 5 s or more are pauses (libwebrtc's OnStreamInactive,
// e.g. the sender stopped the track), counted separately and kept out of
// the average. Also tracks the sum of squared frame
// durations, which gives the harmonic frame rate that weights long frames
// (what a viewer perceives) more than the arithmetic fps does.
//
// When decoding is impossible the receiver keeps showing the last frame;
// this detector quantifies how often and how long that happens.
#pragma once

#include <deque>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::video {

struct FreezeStats {
  uint64_t frames_rendered = 0;
  uint64_t freeze_count = 0;
  double total_freezes_ms = 0;
  uint64_t pause_count = 0;
  double total_pauses_ms = 0;
  double total_frames_duration_ms = 0;
  double sum_squared_frame_durations_s2 = 0;
  // Longest freeze, kept as a plain value because `freeze_ms` records nothing
  // when metrics are compiled out (SM_ENABLE_METRICS=0).
  double longest_freeze_ms = 0;
  Histogram freeze_ms;
};

class FreezeDetector : public MetricsSource {
 public:
  explicit FreezeDetector(EventSink* events = nullptr) : events_(events) {}
  void OnFrameRendered(Timestamp now);
  // Frames per second weighted by duration (WebRTC "harmonic" fps).
  double HarmonicFps() const;
  const FreezeStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  [[maybe_unused]] EventSink* events_;  // Unused when SM_ENABLE_EVENTS=0.
  Timestamp last_ = Timestamp::MinusInfinity();
  // VideoQualityObserver's kAvgInterframeDelaysWindowSizeFrames,
  // kMinFrameSamplesToDetectFreeze and kMinIncreaseForFreezeMs.
  static constexpr size_t kAvgWindowFrames = 30;
  static constexpr size_t kMinSamplesToDetectFreeze = 5;
  static constexpr int64_t kMinIncreaseForFreezeMs = 150;
  std::deque<int64_t> recent_ms_;
  int64_t recent_sum_ = 0;
  FreezeStats stats_;
};

}  // namespace sm::video
