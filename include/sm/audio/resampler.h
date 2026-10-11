// 6.11 Sample-rate conversion (device rate <-> processing rate <-> codec
// rate). Polyphase windowed-sinc (Kaiser) resampler for any rational ratio
// in_rate:out_rate (8k, 16k, 22.05k, 32k, 44.1k, 48k...). The anti-aliasing
// cutoff sits at 0.92 x the lower Nyquist frequency; `taps_per_phase`
// trades quality for CPU and is multiplied by the decimation factor when
// down-sampling so the transition band stays narrow relative to the
// output Nyquist. Streaming: state is
// kept across calls, any input block size is accepted.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "sm/core/metrics.h"

namespace sm::audio {

class Resampler : public MetricsSource {
 public:
  Resampler(int in_rate, int out_rate, int taps_per_phase = 32, double kaiser_beta = 8.0);
  // Appends resampled output to `out`.
  void Process(std::span<const float> in, std::vector<float>& out);
  int in_rate() const { return in_rate_; }
  int out_rate() const { return out_rate_; }
  // Group delay in output samples.
  double delay_out_samples() const;
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  int in_rate_, out_rate_;
  int up_, down_;  // L and M.
  int taps_;
  std::vector<float> coeffs_;   // Polyphase: coeffs_[phase * taps_ + k].
  std::vector<float> hist_;     // Last taps_ input samples, oldest first... ring.
  int64_t next_out_pos_ = 0;    // In units of input samples * up_.
  int64_t in_count_ = 0;        // Input samples consumed in total.
  uint64_t out_count_ = 0;
};

}  // namespace sm::audio
