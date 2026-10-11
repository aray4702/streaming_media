// 6.1 Acoustic echo cancellation.
//
// Linear stage: partitioned-block frequency-domain NLMS (PBFDAF / MDF).
// Blocks of B = 5 ms, FFT N = next_pow2(2B), P partitions of B taps cover
// `filter_length_ms` of echo tail after the bulk delay found by
// EchoDelayEstimator. Each partition's gradient is constrained to B taps
// (overlap-save), and the step is normalized per frequency bin by the
// far-end power summed over partitions.
//
// Step size: after initial convergence the NLMS step follows the
// residual-echo-to-error ratio (Valin 2007), mu = mu0 * min(1, leak * |y|^2
// / |e|^2); near-end speech inflates |e|^2, so adaptation nearly stops
// during double talk without an explicit detector.
//
// Double talk: two filters (Speex MDF style). The background filter always
// adapts; the foreground filter (which produces the output) receives the
// background's coefficients only when the background's error-energy
// improvement is statistically significant: its mean, at two time
// constants, must exceed a multiple of its variance (estimated from the
// foreground error energy times the energy of the difference between the
// two errors). Near-end speech makes that difference noisy, so a
// background filter disturbed by double talk is not copied; a
// significantly worse background is reset from the foreground.
//
// Residual echo suppression: the linear filter leaves residual echo (non-
// linearities, model mismatch). A second STFT stage estimates the residual
// as leak * |Y(k)|^2, where the leak is the correlation between error and
// echo-estimate power spectra over time (near speech is uncorrelated with
// the far end, so double talk does not inflate it), and applies a
// suppression gain per bin.
#pragma once

#include <memory>
#include <span>
#include <vector>

#include "sm/audio/echo_delay_estimator.h"
#include "sm/core/event_log.h"
#include "sm/core/fft.h"
#include "sm/core/metrics.h"

namespace sm::audio {

struct EchoCancellerConfig {
  int sample_rate = 16000;
  int filter_length_ms = 80;
  bool delay_estimation = true;
  int max_delay_ms = 500;
  int fixed_delay_ms = 0;          // Used when delay estimation is off.
  float mu = 0.4f;
  bool residual_suppression = true;
  float over_suppression = 2.0f;
  float min_gain_db = -40.0f;
  EventSink* events = nullptr;
};

struct EchoCancellerStats {
  uint64_t blocks = 0;
  uint64_t foreground_updates = 0;
  uint64_t background_resets = 0;
  uint64_t delay_changes = 0;
  float erle_linear_db = 0;
  float erle_db = 0;
  float leak = 0;
  float step_size = 0;
  int delay_samples = 0;
};

class EchoCanceller : public MetricsSource {
 public:
  explicit EchoCanceller(EchoCancellerConfig cfg = {});
  ~EchoCanceller() override;
  // Call once per 10 ms with the far-end signal sent to the speaker, then
  // ProcessCapture with the microphone signal of the same 10 ms.
  void AnalyzeRender(std::span<const float> far);
  void ProcessCapture(std::span<float> near);
  size_t latency_samples() const;
  const EchoCancellerStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  EchoCancellerStats stats_;
};

}  // namespace sm::audio
