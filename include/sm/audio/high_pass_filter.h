// 6.5 High-pass filter: removes DC offset and low-frequency rumble (mains
// hum, handling noise, wind) before echo cancellation and noise
// suppression. 4th-order Butterworth (two cascaded biquads) at 80 Hz: flat
// above ~150 Hz, -24 dB/octave below the cutoff.
#pragma once

#include <span>

#include "sm/core/dsp.h"
#include "sm/core/metrics.h"

namespace sm::audio {

class HighPassFilter : public MetricsSource {
 public:
  explicit HighPassFilter(int sample_rate = 16000, double cutoff_hz = 80.0);
  void Process(std::span<float> x);
  void Reset();
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  dsp::Biquad a_, b_;
  uint64_t samples_ = 0;
  double dc_in_ = 0;  // Smoothed input mean (diagnostic).
};

}  // namespace sm::audio
