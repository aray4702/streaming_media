#include "sm/audio/high_pass_filter.h"

namespace sm::audio {

HighPassFilter::HighPassFilter(int sample_rate, double cutoff_hz) {
  // 4th-order Butterworth = two biquads with Q = 0.5412 and 1.3066.
  a_.SetCoeffs(dsp::BiquadCoeffs::HighPass(cutoff_hz, sample_rate, 0.54119610));
  b_.SetCoeffs(dsp::BiquadCoeffs::HighPass(cutoff_hz, sample_rate, 1.30656296));
}

void HighPassFilter::Process(std::span<float> x) {
  double sum = 0;
  for (float& v : x) {
    sum += v;
    v = b_.Process(a_.Process(v));
  }
  if (!x.empty()) dc_in_ = 0.95 * dc_in_ + 0.05 * (sum / static_cast<double>(x.size()));
  samples_ += x.size();
}

void HighPassFilter::Reset() {
  a_.Reset();
  b_.Reset();
}

void HighPassFilter::ExportMetrics(MetricsWriter& w) const {
  w.Counter("samples", samples_);
  w.Gauge("input_dc", dc_in_);
}

}  // namespace sm::audio
