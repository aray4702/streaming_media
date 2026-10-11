// 5.10 Simulcast / SVC layer selection for a selective forwarding unit.
//
// Each receiver gets the highest (spatial, temporal) layer whose bitrate
// fits its bandwidth estimate. Rules that keep the forwarded stream
// decodable and stable:
//  - switch down immediately when the estimate drops below the layer rate;
//  - switch up only after the estimate has exceeded the next layer's rate
//    by `up_margin` for `up_hold` (hysteresis);
//  - a spatial switch takes effect only at a keyframe of the target spatial
//    layer (a keyframe request is raised); temporal up-switches happen at
//    layer-sync frames, temporal down-switches at any frame.
#pragma once

#include <vector>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::cc {

struct LayerOption {
  int spatial = 0;
  int temporal = 0;
  DataRate bitrate;  // Total rate needed to forward this selection.
};

struct LayerSelectorConfig {
  double up_margin = 1.15;
  TimeDelta up_hold = TimeDelta::Millis(1000);
  EventSink* events = nullptr;
};

struct LayerSelectorStats {
  uint64_t switches_up = 0;
  uint64_t switches_down = 0;
  uint64_t keyframe_requests = 0;
  uint64_t forwarded = 0;
  uint64_t dropped = 0;
};

class LayerSelector : public MetricsSource {
 public:
  // Options sorted from lowest to highest quality.
  LayerSelector(std::vector<LayerOption> options, LayerSelectorConfig cfg = {});
  // Updates the target from the receiver's available bandwidth.
  void OnBandwidth(DataRate available, Timestamp now);
  // Per-frame forwarding decision.
  bool ShouldForward(int spatial, int temporal, bool keyframe, bool layer_sync);
  // True once per pending spatial switch: ask the sender for a keyframe.
  bool TakeKeyframeRequest();
  LayerOption target() const { return options_[target_]; }
  int current_spatial() const { return cur_spatial_; }
  int current_temporal() const { return cur_temporal_; }
  const LayerSelectorStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  std::vector<LayerOption> options_;
  LayerSelectorConfig cfg_;
  size_t target_ = 0;
  Timestamp up_candidate_since_ = Timestamp::PlusInfinity();
  int cur_spatial_ = -1;  // -1: nothing forwarded yet (wait for keyframe).
  int cur_temporal_ = 0;
  bool keyframe_needed_ = false;
  LayerSelectorStats stats_;
};

}  // namespace sm::cc
