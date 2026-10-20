// 4.4 Pitch-synchronous time stretching (WSOLA / NetEq accelerate and
// preemptive expand).
//
// Accelerate removes one pitch period L from a block of at least 30 ms by
// crossfading segment A = x[s, s+L) into B = x[s+L, s+2L); preemptive expand
// inserts one period by crossfading B back into A. Because voiced speech is
// nearly periodic with period L, the edit is inaudible. Stretching is only
// applied when the two segments are well correlated, or when the block is
// near-silent (any lag works then).
#pragma once

#include <span>
#include <vector>

#include "sm/core/metrics.h"

namespace sm::timing {

struct TimeStretchConfig {
  int sample_rate = 16000;
  float correlation_threshold = 0.75f;
  float silence_dbfs = -55.0f;
  int min_period_hz = 400;  // Shortest period (highest pitch) considered.
  int max_period_hz = 70;   // Longest period considered (must fit in 30 ms / 2).
};

struct TimeStretchStats {
  uint64_t accelerate_ok = 0;
  uint64_t accelerate_rejected = 0;
  uint64_t expand_ok = 0;
  uint64_t expand_rejected = 0;
  uint64_t samples_removed = 0;
  uint64_t samples_inserted = 0;
};

class TimeStretcher : public MetricsSource {
 public:
  explicit TimeStretcher(TimeStretchConfig cfg = {});
  // Minimum input length in samples (30 ms).
  size_t min_input() const { return static_cast<size_t>(cfg_.sample_rate * 3 / 100); }
  // Writes the stretched signal to `out` and returns the number of samples
  // removed (accelerate) or inserted (expand); 0 means the input was copied
  // unchanged.
  size_t Accelerate(std::span<const float> in, std::vector<float>& out);
  // `old_samples` at the start of `in` were played already and stay as they
  // are: the period is inserted after them (PreemptiveExpand's
  // old_data_length in NetEq).
  size_t PreemptiveExpand(std::span<const float> in, std::vector<float>& out, size_t old_samples = 0);
  const TimeStretchStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  // Best period length for the segment pair starting at `start`.
  int FindPeriod(std::span<const float> in, size_t start, float* corr) const;
  TimeStretchConfig cfg_;
  TimeStretchStats stats_;
};

}  // namespace sm::timing
