#include "sm/audio/resampler.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace sm::audio {

namespace {
double BesselI0(double x) {
  double sum = 1, term = 1;
  for (int k = 1; k < 50; ++k) {
    term *= (x / (2 * k)) * (x / (2 * k));
    sum += term;
    if (term < 1e-12 * sum) break;
  }
  return sum;
}
}  // namespace

Resampler::Resampler(int in_rate, int out_rate, int taps_per_phase, double beta)
    : in_rate_(in_rate), out_rate_(out_rate) {
  int g = std::gcd(in_rate, out_rate);
  up_ = out_rate / g;
  down_ = in_rate / g;
  // When decimating, the transition band is a fraction of the *output*
  // Nyquist, so the filter must be longer by the decimation factor.
  taps_ = taps_per_phase * std::max(1, (down_ + up_ - 1) / up_);
  // Prototype low-pass at rate in*L, cutoff relative to that rate.
  const int n = taps_ * up_;
  const double cutoff = 0.92 * 0.5 * std::min(1.0, static_cast<double>(up_) / down_) / up_;  // cycles/sample
  const double center = (n - 1) / 2.0;
  std::vector<double> h(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    double t = i - center;
    double sinc = t == 0 ? 2 * cutoff : std::sin(2 * std::numbers::pi * cutoff * t) / (std::numbers::pi * t);
    double r = t / (center + 1);
    double w = BesselI0(beta * std::sqrt(std::max(0.0, 1 - r * r))) / BesselI0(beta);
    h[static_cast<size_t>(i)] = sinc * w * up_;  // Gain L restores level after zero-stuffing.
  }
  // Polyphase decomposition: phase p uses h[p + k*L].
  coeffs_.assign(static_cast<size_t>(n), 0.0f);
  for (int p = 0; p < up_; ++p) {
    for (int k = 0; k < taps_; ++k) coeffs_[static_cast<size_t>(p * taps_ + k)] = static_cast<float>(h[static_cast<size_t>(p + k * up_)]);
  }
  hist_.assign(static_cast<size_t>(taps_), 0.0f);
}

double Resampler::delay_out_samples() const {
  // Prototype delay (n-1)/2 at rate in*L, expressed at the output rate.
  return (taps_ * up_ - 1) / 2.0 / down_;
}

void Resampler::Process(std::span<const float> in, std::vector<float>& out) {
  for (float x : in) {
    // Shift in the new sample (hist_[0] = newest).
    for (int k = taps_ - 1; k > 0; --k) hist_[static_cast<size_t>(k)] = hist_[static_cast<size_t>(k - 1)];
    hist_[0] = x;
    ++in_count_;
    // Emit every output whose position (in input*L units) falls before the
    // next input sample.
    while (next_out_pos_ < in_count_ * up_) {
      int64_t base = next_out_pos_ / up_;  // Input index of the newest tap.
      int phase = static_cast<int>(next_out_pos_ % up_);
      int64_t lag = in_count_ - 1 - base;  // How far back the newest tap is.
      const float* c = &coeffs_[static_cast<size_t>(phase * taps_)];
      double acc = 0;
      for (int k = 0; k + lag < taps_; ++k) acc += static_cast<double>(c[k]) * hist_[static_cast<size_t>(k + lag)];
      out.push_back(static_cast<float>(acc));
      ++out_count_;
      next_out_pos_ += down_;
    }
  }
}

void Resampler::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("in_rate", in_rate_);
  w.Gauge("out_rate", out_rate_);
  w.Counter("in_samples", static_cast<uint64_t>(in_count_));
  w.Counter("out_samples", out_count_);
}

}  // namespace sm::audio
