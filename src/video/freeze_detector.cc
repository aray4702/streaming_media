#include "sm/video/freeze_detector.h"

#include <algorithm>

namespace sm::video {

// --- FreezeDetector ----------------------------------------------------------

void FreezeDetector::OnFrameRendered(Timestamp now) {
  ++stats_.frames_rendered;
  if (!last_.IsFinite()) {
    last_ = now;
    return;
  }
  double d = (now - last_).ms_f();
  last_ = now;
  stats_.total_frames_duration_ms += d;
  stats_.sum_squared_frame_durations_s2 += (d / 1000.0) * (d / 1000.0);
  if (d >= 5000) {
    ++stats_.pause_count;
    stats_.total_pauses_ms += d;
    SM_EVENT(events_, now, "freeze", "pause", d, 0);
    return;
  }
  if (recent_ms_.size() >= 5) {
    double avg = recent_sum_ / static_cast<double>(recent_ms_.size());
    if (d >= std::max(3 * avg, avg + 150)) {
      ++stats_.freeze_count;
      stats_.total_freezes_ms += d;
      stats_.freeze_ms.Add(d);
      SM_EVENT(events_, now, "freeze", "freeze", d, 0);
      return;  // Freezes do not enter the average.
    }
  }
  recent_ms_.push_back(d);
  recent_sum_ += d;
  if (recent_ms_.size() > 30) {
    recent_sum_ -= recent_ms_.front();
    recent_ms_.pop_front();
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
