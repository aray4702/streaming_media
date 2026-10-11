#include "sm/loss/temporal_layers.h"

#include <algorithm>
#include <string>

namespace sm::loss {

TemporalLayerScheduler::TemporalLayerScheduler(int num_layers) : layers_(std::clamp(num_layers, 1, 3)) {}

LayerFrame TemporalLayerScheduler::NextFrame(bool force_keyframe) {
  LayerFrame f;
  f.info.frame_id = next_id_++;
  bool keyframe = force_keyframe || last_of_layer_[0] < 0;
  if (keyframe) {
    pattern_idx_ = 0;
    last_of_layer_ = {-1, -1, -1};
  }
  static constexpr int kL2[2] = {0, 1};
  static constexpr int kL3[4] = {0, 2, 1, 2};
  int tid = 0;
  if (layers_ == 2) tid = kL2[pattern_idx_ % 2];
  if (layers_ == 3) tid = kL3[pattern_idx_ % 4];
  ++pattern_idx_;

  f.temporal_id = tid;
  f.keyframe = keyframe;
  f.info.keyframe = keyframe;
  if (!keyframe) {
    if (tid == 0) {
      f.info.references = {last_of_layer_[0]};
      f.layer_sync = true;
    } else if (tid == 1) {
      f.info.references = {last_of_layer_[0]};
      f.layer_sync = true;  // T1 only depends on T0.
    } else {
      // T2 references the most recent lower-layer frame.
      int64_t ref = std::max(last_of_layer_[0], last_of_layer_[1]);
      f.info.references = {ref};
      f.layer_sync = ref == last_of_layer_[0] && last_of_layer_[1] < last_of_layer_[0];
    }
  } else {
    f.layer_sync = true;
  }
  last_of_layer_[static_cast<size_t>(tid)] = f.info.frame_id;
  return f;
}

std::vector<double> TemporalLayerScheduler::CumulativeBitrateShare(int n) {
  if (n <= 1) return {1.0};
  if (n == 2) return {0.6, 1.0};
  return {0.4, 0.6, 1.0};
}

double TemporalLayerScheduler::FramerateFraction(int n, int max_tid) {
  if (n <= 1) return 1.0;
  max_tid = std::clamp(max_tid, 0, n - 1);
  if (n == 2) return max_tid == 0 ? 0.5 : 1.0;
  static constexpr double kL3[3] = {0.25, 0.5, 1.0};
  return kL3[max_tid];
}

bool TemporalLayerFilter::ShouldForward(const LayerFrame& f) {
  int tid = std::clamp(f.temporal_id, 0, 2);
  if (target_ < current_) {
    current_ = target_;  // Switching down is always safe.
    ++stats_.switches;
  } else if (target_ > current_ && f.layer_sync && tid <= target_ && tid > current_) {
    current_ = tid;      // Switch up at a sync frame of the new layer.
    ++stats_.switches;
  }
  if (f.keyframe && target_ > current_) {
    current_ = target_;
    ++stats_.switches;
  }
  bool fwd = tid <= current_;
  (fwd ? stats_.forwarded : stats_.dropped)[static_cast<size_t>(tid)]++;
  return fwd;
}

void TemporalLayerFilter::ExportMetrics(MetricsWriter& w) const {
  for (int i = 0; i < 3; ++i) {
    w.Counter("forwarded_t" + std::to_string(i), stats_.forwarded[static_cast<size_t>(i)]);
    w.Counter("dropped_t" + std::to_string(i), stats_.dropped[static_cast<size_t>(i)]);
  }
  w.Counter("switches", stats_.switches);
  w.Gauge("current_layer", current_);
}

}  // namespace sm::loss
