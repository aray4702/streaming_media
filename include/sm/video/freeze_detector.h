// 7.1 Freeze detection (the WebRTC getStats freeze definition).
//
// A freeze is an inter-frame interval longer than
//     max(3 * avg_frame_duration, avg_frame_duration + 150 ms)
// where the average is over the last 30 rendered frames (excluding
// freezes). Gaps of 5 s or more are pauses (e.g. the sender stopped the
// track), counted separately. Also tracks the sum of squared frame
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
  std::deque<double> recent_ms_;
  double recent_sum_ = 0;
  FreezeStats stats_;
};

}  // namespace sm::video
