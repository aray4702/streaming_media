// 6.3 Noise suppression: STFT Wiener filter with decision-directed SNR.
//
// 10 ms hop, 20 ms sqrt-Hann analysis/synthesis windows (50% overlap,
// perfect reconstruction), zero-padded FFT. Per frequency bin:
//  - noise PSD: minimum-tracking of the smoothed power spectrum (follows
//    drops immediately, rises at most `noise_rise_db_per_s`), with a
//    speech-presence gate so speech does not leak into the estimate;
//  - a-priori SNR, decision-directed (Ephraim-Malah):
//        xi = a * G_prev^2 * |X_prev|^2 / N + (1 - a) * max(gamma - 1, 0)
//  - Wiener gain G = xi / (1 + xi), floored at -`max_attenuation_db`.
// Algorithmic latency: one hop (10 ms).
#pragma once

#include <span>
#include <vector>

#include "sm/core/fft.h"
#include "sm/core/metrics.h"

namespace sm::audio {

struct NoiseSuppressorConfig {
  int sample_rate = 16000;
  float max_attenuation_db = 18.0f;  // "moderate"; 12 low, 24 very high.
  float dd_alpha = 0.98f;
  float noise_rise_db_per_s = 4.0f;
  float psd_smoothing = 0.7f;
};

struct NoiseSuppressorStats {
  uint64_t frames = 0;
  float last_mean_gain_db = 0;
  float noise_level_dbfs = 0;
  Histogram gain_db;
};

class NoiseSuppressor : public MetricsSource {
 public:
  explicit NoiseSuppressor(NoiseSuppressorConfig cfg = {});
  // In-place on a 10 ms frame. Output is delayed by latency_samples().
  void Process(std::span<float> frame);
  size_t latency_samples() const { return hop_; }
  const NoiseSuppressorStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  NoiseSuppressorConfig cfg_;
  size_t hop_, win_;
  RealFft fft_;
  std::vector<float> window_;
  std::vector<float> in_hist_;   // Last win_ input samples.
  std::vector<float> ola_;       // Overlap-add tail (win_ - hop_).
  std::vector<float> buf_;
  std::vector<Complex> spec_;
  std::vector<float> psd_, noise_, prev_gain_, prev_power_;
  uint64_t frames_ = 0;
  NoiseSuppressorStats stats_;
};

}  // namespace sm::audio
