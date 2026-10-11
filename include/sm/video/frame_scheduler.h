// 7.7 Render timing and late-frame dropping.
//
// Decodable frames arrive with a target render time (from
// timing::VideoTiming: capture time mapped to the local clock plus the
// playout delay). The scheduler starts decoding each frame
// `decode_time` before its render time and then:
//   - renders it if it is on time (or at most `max_late` late);
//   - if it is later than that and a newer frame is already waiting, it
//     decodes it but skips rendering ("dropped late"), since showing a
//     stale frame only delays the newer one;
//   - if it is the newest frame it is rendered anyway (better late than a
//     freeze).
// If the queue grows beyond `max_queue` (a burst after a stall), it jumps
// ahead: everything up to the newest frame is decoded without rendering.
// Frames must be decoded in order (they reference each other), so skipped
// frames are still decoded.
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::video {

struct ScheduledFrame {
  int64_t id = 0;
  Timestamp render_time;
};

struct FrameAction {
  int64_t id = 0;
  bool render = false;
  Timestamp render_time;
  TimeDelta lateness;  // Positive = late.
};

struct FrameSchedulerConfig {
  TimeDelta decode_time = TimeDelta::Millis(5);
  TimeDelta max_late = TimeDelta::Millis(10);
  size_t max_queue = 30;
  EventSink* events = nullptr;
};

struct FrameSchedulerStats {
  uint64_t frames_in = 0, decoded = 0, rendered = 0, dropped_late = 0, fast_forwards = 0;
  Histogram lateness_ms;  // Rendered frames only (negative clamped to 0).
};

class FrameScheduler : public MetricsSource {
 public:
  explicit FrameScheduler(FrameSchedulerConfig cfg = {}) : cfg_(cfg) {}
  void Insert(const ScheduledFrame& f);
  std::vector<FrameAction> Process(Timestamp now);
  // Next time Process() has work to do.
  Timestamp NextWakeup() const;
  size_t queued() const { return queue_.size(); }
  const FrameSchedulerStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  FrameSchedulerConfig cfg_;
  std::deque<ScheduledFrame> queue_;
  FrameSchedulerStats stats_;
};

}  // namespace sm::video
