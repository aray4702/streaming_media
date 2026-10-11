// 3.6 Temporal scalability (L1T1..L1T3, VP8/VP9/AV1 style).
//
// The scheduler assigns each frame a temporal layer id (tid) and the frames
// it may reference, so frames of a higher layer are never referenced by a
// lower or same layer. Losing or dropping a T1/T2 frame therefore never
// breaks decoding of later T0 frames; an SFU can also drop upper layers to
// halve or quarter the frame rate for a slow receiver.
//
//   L1T2 (period 2): T0 T1 T0 T1 ...        T1 refs the last T0
//   L1T3 (period 4): T0 T2 T1 T2 T0 ...     T1 refs T0, T2 refs T0 or T1
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "sm/core/metrics.h"
#include "sm/loss/keyframe_request.h"

namespace sm::loss {

struct LayerFrame {
  int temporal_id = 0;
  bool keyframe = false;
  bool layer_sync = false;  // Depends only on T0: safe point to switch up.
  FrameInfo info;           // frame_id + references.
};

class TemporalLayerScheduler {
 public:
  explicit TemporalLayerScheduler(int num_layers);
  int num_layers() const { return layers_; }
  LayerFrame NextFrame(bool force_keyframe = false);
  // Share of total bitrate per layer, cumulative (e.g. {0.6, 1.0} for L1T2,
  // {0.4, 0.6, 1.0} for L1T3, as in libvpx defaults).
  static std::vector<double> CumulativeBitrateShare(int num_layers);
  // Frame-rate fraction available when forwarding layers <= tid.
  static double FramerateFraction(int num_layers, int max_tid);

 private:
  int layers_;
  int64_t next_id_ = 0;
  int64_t pattern_idx_ = 0;
  std::array<int64_t, 3> last_of_layer_{-1, -1, -1};
};

struct LayerFilterStats {
  std::array<uint64_t, 3> forwarded{};
  std::array<uint64_t, 3> dropped{};
  uint64_t switches = 0;
};

// Selective forwarding: forwards frames with tid <= the target, switching up
// only at layer-sync frames so the receiver never sees a frame whose
// references it did not get.
class TemporalLayerFilter : public MetricsSource {
 public:
  void SetTargetLayer(int tid) { target_ = tid; }
  bool ShouldForward(const LayerFrame& f);
  int current_layer() const { return current_; }
  const LayerFilterStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  int target_ = 2;
  int current_ = 0;
  LayerFilterStats stats_;
};

}  // namespace sm::loss
