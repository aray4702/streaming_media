#include "sm/core/dsp.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sm::dsp {

BiquadCoeffs BiquadCoeffs::HighPass(double fc, double fs, double q) {
  // RBJ audio EQ cookbook.
  double w0 = 2 * std::numbers::pi * fc / fs;
  double alpha = std::sin(w0) / (2 * q);
  double cw = std::cos(w0);
  double a0 = 1 + alpha;
  BiquadCoeffs c;
  c.b0 = static_cast<float>((1 + cw) / 2 / a0);
  c.b1 = static_cast<float>(-(1 + cw) / a0);
  c.b2 = c.b0;
  c.a1 = static_cast<float>(-2 * cw / a0);
  c.a2 = static_cast<float>((1 - alpha) / a0);
  return c;
}

BiquadCoeffs BiquadCoeffs::LowPass(double fc, double fs, double q) {
  double w0 = 2 * std::numbers::pi * fc / fs;
  double alpha = std::sin(w0) / (2 * q);
  double cw = std::cos(w0);
  double a0 = 1 + alpha;
  BiquadCoeffs c;
  c.b0 = static_cast<float>((1 - cw) / 2 / a0);
  c.b1 = static_cast<float>((1 - cw) / a0);
  c.b2 = c.b0;
  c.a1 = static_cast<float>(-2 * cw / a0);
  c.a2 = static_cast<float>((1 - alpha) / a0);
  return c;
}

float Energy(std::span<const float> x) {
  double e = 0;
  for (float v : x) e += static_cast<double>(v) * v;
  return static_cast<float>(e);
}

float Rms(std::span<const float> x) {
  if (x.empty()) return 0;
  return std::sqrt(Energy(x) / static_cast<float>(x.size()));
}

float LinearToDb(float lin) { return lin <= 1e-5f ? -100.0f : 20.0f * std::log10(lin); }
float DbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }
float RmsDbfs(std::span<const float> x) { return LinearToDb(Rms(x)); }

float NormalizedCorrelation(const float* a, const float* b, size_t n) {
  double ab = 0, aa = 0, bb = 0;
  for (size_t i = 0; i < n; ++i) {
    ab += static_cast<double>(a[i]) * b[i];
    aa += static_cast<double>(a[i]) * a[i];
    bb += static_cast<double>(b[i]) * b[i];
  }
  double d = std::sqrt(aa * bb);
  return d < 1e-12 ? 0.0f : static_cast<float>(ab / d);
}

PitchResult EstimatePitch(std::span<const float> x, int min_lag, int max_lag, int window) {
  PitchResult best;
  best.correlation = -2;
  const int n = static_cast<int>(x.size());
  if (n < window + max_lag) max_lag = n - window;
  const float* tail = x.data() + n - window;
  for (int lag = min_lag; lag <= max_lag; ++lag) {
    float c = NormalizedCorrelation(tail, tail - lag, static_cast<size_t>(window));
    if (c > best.correlation) {
      best.correlation = c;
      best.lag = lag;
    }
  }
  // Prefer the shortest lag that is nearly as good, to avoid picking a pitch
  // multiple (octave error).
  if (best.lag > 0) {
    for (int div = 4; div >= 2; --div) {
      int cand = best.lag / div;
      if (cand < min_lag) continue;
      for (int l = std::max(min_lag, cand - 1); l <= cand + 1; ++l) {
        float c = NormalizedCorrelation(tail, tail - l, static_cast<size_t>(window));
        if (c > 0.9f * best.correlation) return PitchResult{l, c};
      }
    }
  }
  if (best.correlation < -1) best = PitchResult{min_lag, 0};
  return best;
}

void Autocorrelation(std::span<const float> x, int order, double* r) {
  const size_t n = x.size();
  for (int k = 0; k <= order; ++k) {
    double s = 0;
    for (size_t i = static_cast<size_t>(k); i < n; ++i) s += static_cast<double>(x[i]) * x[i - k];
    r[k] = s;
  }
}

double LevinsonDurbin(const double* r, int order, double* a, double* k) {
  std::vector<double> tmp(static_cast<size_t>(order) + 1);
  a[0] = 1;
  for (int i = 1; i <= order; ++i) a[i] = 0;
  double err = r[0];
  if (err <= 0) {
    for (int i = 0; i < order; ++i) k[i] = 0;
    return 0;
  }
  for (int i = 1; i <= order; ++i) {
    double acc = r[i];
    for (int j = 1; j < i; ++j) acc += a[j] * r[i - j];
    double ki = -acc / err;
    k[i - 1] = ki;
    for (int j = 0; j <= order; ++j) tmp[j] = a[j];
    for (int j = 1; j < i; ++j) a[j] = tmp[j] + ki * tmp[i - j];
    a[i] = ki;
    err *= (1 - ki * ki);
    if (err <= 0) break;
  }
  return err;
}

std::vector<float> HannWindow(size_t n) {
  std::vector<float> w(n);
  for (size_t i = 0; i < n; ++i) {
    w[i] = static_cast<float>(0.5 - 0.5 * std::cos(2 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n)));
  }
  return w;
}

std::vector<float> SqrtHannWindow(size_t n) {
  std::vector<float> w = HannWindow(n);
  for (float& v : w) v = std::sqrt(v);
  return w;
}

double SnrDb(std::span<const float> ref, std::span<const float> test) {
  size_t n = std::min(ref.size(), test.size());
  double s = 0, e = 0;
  for (size_t i = 0; i < n; ++i) {
    s += static_cast<double>(ref[i]) * ref[i];
    double d = static_cast<double>(ref[i]) - test[i];
    e += d * d;
  }
  if (e < 1e-20) return 200.0;
  if (s < 1e-20) return -200.0;
  return 10.0 * std::log10(s / e);
}

}  // namespace sm::dsp
