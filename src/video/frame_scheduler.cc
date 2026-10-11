#include "sm/video/frame_scheduler.h"

#include <algorithm>

namespace sm::video {

void FrameScheduler::Insert(const ScheduledFrame& f) {
  ++stats_.frames_in;
  // Keep decode order (ids increase); ignore stale duplicates.
  if (!queue_.empty() && f.id <= queue_.back().id) return;
  queue_.push_back(f);
}

std::vector<FrameAction> FrameScheduler::Process(Timestamp now) {
  std::vector<FrameAction> out;
  if (queue_.size() > cfg_.max_queue) {
    // Jump ahead: decode (but do not show) all but the newest frame.
    ++stats_.fast_forwards;
    SM_EVENT(cfg_.events, now, "frame_scheduler", "fast_forward", static_cast<double>(queue_.size()), queue_.back().id);
    while (queue_.size() > 1) {
      out.push_back({queue_.front().id, false, queue_.front().render_time, now - queue_.front().render_time});
      ++stats_.decoded;
      ++stats_.dropped_late;
      queue_.pop_front();
    }
  }
  while (!queue_.empty() && now >= queue_.front().render_time - cfg_.decode_time) {
    ScheduledFrame f = queue_.front();
    queue_.pop_front();
    ++stats_.decoded;
    TimeDelta late = now + cfg_.decode_time - f.render_time;
    bool newer_waiting = !queue_.empty() && now >= queue_.front().render_time - cfg_.decode_time;
    bool render = late <= cfg_.max_late || !newer_waiting;
    if (render) {
      ++stats_.rendered;
      stats_.lateness_ms.Add(std::max(0.0, late.ms_f()));
    } else {
      ++stats_.dropped_late;
      SM_EVENT(cfg_.events, now, "frame_scheduler", "drop_late", late.ms_f(), f.id);
    }
    out.push_back({f.id, render, f.render_time, late});
  }
  return out;
}

Timestamp FrameScheduler::NextWakeup() const {
  return queue_.empty() ? Timestamp::PlusInfinity() : queue_.front().render_time - cfg_.decode_time;
}

void FrameScheduler::ExportMetrics(MetricsWriter& w) const {
  w.Counter("frames_in", stats_.frames_in);
  w.Counter("decoded", stats_.decoded);
  w.Counter("rendered", stats_.rendered);
  w.Counter("dropped_late", stats_.dropped_late);
  w.Counter("fast_forwards", stats_.fast_forwards);
  w.Gauge("queued", static_cast<double>(queue_.size()));
  w.Hist("lateness_ms", stats_.lateness_ms);
}

}  // namespace sm::video
