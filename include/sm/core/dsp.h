// Small DSP toolkit shared by audio algorithms: biquads, levels, pitch,
// correlation, LPC.
#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace sm::dsp {

// Second-order IIR section, transposed direct form II.
struct BiquadCoeffs {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  static BiquadCoeffs HighPass(double cutoff_hz, double sample_rate_hz, double q = 0.7071);
  static BiquadCoeffs LowPass(double cutoff_hz, double sample_rate_hz, double q = 0.7071);
};

class Biquad {
 public:
  Biquad() = default;
  explicit Biquad(const BiquadCoeffs& c) : c_(c) {}
  void SetCoeffs(const BiquadCoeffs& c) { c_ = c; }
  float Process(float x) {
    float y = c_.b0 * x + z1_;
    z1_ = c_.b1 * x - c_.a1 * y + z2_;
    z2_ = c_.b2 * x - c_.a2 * y;
    return y;
  }
  void Process(std::span<float> x) {
    for (float& v : x) v = Process(v);
  }
  void Reset() { z1_ = z2_ = 0; }

 private:
  BiquadCoeffs c_;
  float z1_ = 0, z2_ = 0;
};

float Energy(std::span<const float> x);
float Rms(std::span<const float> x);
// Level in dBFS for float samples in [-1, 1]; floor at -100 dB.
float RmsDbfs(std::span<const float> x);
float DbToLinear(float db);
float LinearToDb(float lin);  // 20*log10, floor at -100 dB.

// Normalized cross-correlation of a and b (length n). Returns [-1, 1].
float NormalizedCorrelation(const float* a, const float* b, size_t n);

struct PitchResult {
  int lag = 0;            // In samples.
  float correlation = 0;  // Normalized, at `lag`.
};
// Finds the lag in [min_lag, max_lag] maximizing the normalized correlation
// between the last `window` samples of x and the window `lag` samples
// earlier. Requires x.size() >= window + max_lag.
PitchResult EstimatePitch(std::span<const float> x, int min_lag, int max_lag, int window);

// Autocorrelation r[0..order].
void Autocorrelation(std::span<const float> x, int order, double* r);
// Levinson-Durbin recursion. a[0..order] (a[0]=1), k[0..order-1] reflection
// coefficients. Returns final prediction error.
double LevinsonDurbin(const double* r, int order, double* a, double* k);

std::vector<float> HannWindow(size_t n);
// sqrt(Hann) with 50% overlap gives perfect reconstruction for analysis +
// synthesis windowing (periodic form).
std::vector<float> SqrtHannWindow(size_t n);

// Signal-to-noise ratio of `test` against `ref` in dB.
double SnrDb(std::span<const float> ref, std::span<const float> test);

}  // namespace sm::dsp
