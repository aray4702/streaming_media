// Synthetic test signals. Deterministic and license-free replacements for
// speech corpora so tests run identically on every platform.
#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "sm/core/random.h"

namespace sm::sim {

inline std::vector<float> Sine(double freq_hz, int sample_rate, double seconds, float amp = 0.5f) {
  size_t n = static_cast<size_t>(seconds * sample_rate);
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) {
    x[i] = amp * static_cast<float>(std::sin(2 * std::numbers::pi * freq_hz * static_cast<double>(i) / sample_rate));
  }
  return x;
}

inline std::vector<float> WhiteNoise(int sample_rate, double seconds, float amp, uint64_t seed) {
  Random rng(seed);
  size_t n = static_cast<size_t>(seconds * sample_rate);
  std::vector<float> x(n);
  for (auto& v : x) v = amp * static_cast<float>(rng.Gaussian());
  return x;
}

// Voiced, speech-like signal: harmonic series with a gliding pitch
// (90-240 Hz), spectral tilt, a ~4 Hz syllable envelope and optional pauses.
inline std::vector<float> SpeechLike(int sample_rate, double seconds, uint64_t seed,
                                     bool with_pauses = true, float amp = 0.3f) {
  Random rng(seed);
  size_t n = static_cast<size_t>(seconds * sample_rate);
  std::vector<float> x(n);
  double phase = 0;
  double f0 = 140, f0_target = 160;
  double syl_phase = rng.Uniform(0, 6.28);
  double syl_rate = 4.0;
  bool paused = false;
  size_t seg_left = 0;
  for (size_t i = 0; i < n; ++i) {
    if (seg_left == 0) {
      f0_target = rng.Uniform(90, 240);
      syl_rate = rng.Uniform(3, 5.5);
      paused = with_pauses && rng.Bernoulli(0.25);
      seg_left = static_cast<size_t>(rng.Uniform(0.25, 0.6) * sample_rate);
    }
    --seg_left;
    f0 += (f0_target - f0) * (20.0 / sample_rate);
    phase += 2 * std::numbers::pi * f0 / sample_rate;
    if (phase > 2 * std::numbers::pi * 1000) phase -= 2 * std::numbers::pi * 1000;
    double s = 0;
    for (int h = 1; h * f0 < sample_rate * 0.45 && h <= 20; ++h) {
      // Formant-like emphasis near 500 Hz and 1500 Hz on top of 1/h tilt.
      double fh = h * f0;
      double formant = 1.0 + 2.0 * std::exp(-std::pow((fh - 500) / 200, 2)) +
                       1.2 * std::exp(-std::pow((fh - 1500) / 300, 2));
      s += formant / h * std::sin(h * phase);
    }
    syl_phase += 2 * std::numbers::pi * syl_rate / sample_rate;
    double env = 0.55 + 0.45 * std::sin(syl_phase);
    x[i] = paused ? 0.0f : static_cast<float>(amp * 0.35 * env * s);
  }
  return x;
}

// Exponentially decaying random room impulse response with a dominant
// direct path (reflections sigma 0.1 vs. direct 0.6, as in a real room).
inline std::vector<float> RoomImpulseResponse(int sample_rate, double length_ms, double rt60_ms,
                                              uint64_t seed, int direct_delay = 8) {
  Random rng(seed);
  size_t n = static_cast<size_t>(length_ms * sample_rate / 1000);
  std::vector<float> h(n, 0.0f);
  double decay = std::log(1000.0) / (rt60_ms * sample_rate / 1000);  // -60 dB at rt60.
  if (static_cast<size_t>(direct_delay) < n) h[static_cast<size_t>(direct_delay)] = 0.6f;
  for (size_t i = static_cast<size_t>(direct_delay) + 1; i < n; ++i) {
    h[i] = static_cast<float>(0.1 * rng.Gaussian() * std::exp(-decay * static_cast<double>(i)));
  }
  return h;
}

inline std::vector<float> Convolve(const std::vector<float>& x, const std::vector<float>& h) {
  std::vector<float> y(x.size(), 0.0f);
  for (size_t i = 0; i < x.size(); ++i) {
    double acc = 0;
    size_t kmax = std::min(h.size(), i + 1);
    for (size_t k = 0; k < kmax; ++k) acc += static_cast<double>(h[k]) * x[i - k];
    y[i] = static_cast<float>(acc);
  }
  return y;
}

}  // namespace sm::sim
