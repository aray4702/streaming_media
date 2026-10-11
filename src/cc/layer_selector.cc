#include "sm/cc/layer_selector.h"

namespace sm::cc {

LayerSelector::LayerSelector(std::vector<LayerOption> options, LayerSelectorConfig cfg)
    : options_(std::move(options)), cfg_(cfg) {}

void LayerSelector::OnBandwidth(DataRate available, Timestamp now) {
  size_t best = 0;
  for (size_t i = 0; i < options_.size(); ++i) {
    if (options_[i].bitrate <= available) best = i;
  }
  if (best < target_) {
    target_ = best;  // Down: immediately.
    up_candidate_since_ = Timestamp::PlusInfinity();
    ++stats_.switches_down;
    SM_EVENT(cfg_.events, now, "layer_selector", "down", options_[target_].bitrate.bps(), static_cast<int64_t>(target_));
  } else if (target_ + 1 < options_.size() && available >= options_[target_ + 1].bitrate * cfg_.up_margin) {
    if (!up_candidate_since_.IsFinite()) up_candidate_since_ = now;
    if (now - up_candidate_since_ >= cfg_.up_hold) {
      ++target_;
      up_candidate_since_ = Timestamp::PlusInfinity();
      ++stats_.switches_up;
      SM_EVENT(cfg_.events, now, "layer_selector", "up", options_[target_].bitrate.bps(), static_cast<int64_t>(target_));
    }
  } else {
    up_candidate_since_ = Timestamp::PlusInfinity();
  }
  if (options_[target_].spatial != cur_spatial_) keyframe_needed_ = true;
}

bool LayerSelector::TakeKeyframeRequest() {
  bool r = keyframe_needed_ && options_[target_].spatial != cur_spatial_;
  if (r) ++stats_.keyframe_requests;
  keyframe_needed_ = false;
  return r;
}

bool LayerSelector::ShouldForward(int spatial, int temporal, bool keyframe, bool layer_sync) {
  const LayerOption& t = options_[target_];
  // Spatial switch (simulcast: streams are independent) at a target keyframe.
  if (spatial == t.spatial && keyframe && cur_spatial_ != t.spatial) {
    cur_spatial_ = t.spatial;
    cur_temporal_ = t.temporal;
  }
  if (spatial != cur_spatial_) {
    ++stats_.dropped;
    return false;
  }
  if (t.temporal < cur_temporal_) cur_temporal_ = t.temporal;  // Down: any frame.
  if (t.temporal > cur_temporal_ && layer_sync && temporal <= t.temporal && temporal > cur_temporal_) {
    cur_temporal_ = temporal;  // Up: at a sync frame.
  }
  bool fwd = temporal <= cur_temporal_;
  (fwd ? stats_.forwarded : stats_.dropped)++;
  return fwd;
}

void LayerSelector::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("target_index", static_cast<double>(target_));
  w.Gauge("current_spatial", cur_spatial_);
  w.Gauge("current_temporal", cur_temporal_);
  w.Counter("switches_up", stats_.switches_up);
  w.Counter("switches_down", stats_.switches_down);
  w.Counter("keyframe_requests", stats_.keyframe_requests);
  w.Counter("forwarded", stats_.forwarded);
  w.Counter("dropped", stats_.dropped);
}

}  // namespace sm::cc
