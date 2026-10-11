// 6.10 Multi-party audio mixer (loudest-N selection).
//
// Each 10 ms the mixer ranks sources by smoothed energy, mixes only the
// loudest `max_mixed` (others would only add noise), ramps a source's gain
// up or down across one frame when it enters or leaves the mix so there is
// no click, and runs the sum through a limiter so it never clips. A
// selected source keeps its slot until a challenger is clearly louder
// (hysteresis), which avoids rapid swapping between similar speakers.
#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <vector>

#include "sm/audio/gain_control.h"
#include "sm/core/metrics.h"

namespace sm::audio {

struct MixerConfig {
  int sample_rate = 16000;
  size_t max_mixed = 3;
  double energy_smoothing = 0.3;
  double hysteresis = 1.5;  // Energy ratio needed to take a mixed slot.
  LimiterConfig limiter;
};

class AudioMixer : public MetricsSource {
 public:
  explicit AudioMixer(MixerConfig cfg = {});
  // Frames for this 10 ms tick; all spans must have the same length.
  void Mix(const std::map<uint32_t, std::span<const float>>& sources, std::span<float> out);
  const std::vector<uint32_t>& mixed() const { return mixed_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  MixerConfig cfg_;
  Limiter limiter_;
  std::map<uint32_t, double> energy_;
  std::map<uint32_t, float> gain_;
  std::vector<uint32_t> mixed_;
  uint64_t frames_ = 0, swaps_ = 0;
};

}  // namespace sm::audio
