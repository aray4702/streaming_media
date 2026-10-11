#include "sm/audio/echo_delay_estimator.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "sm/core/dsp.h"

namespace sm::audio {

namespace {
size_t NextPow2(size_t n) {
  size_t p = 1;
  while (p < n) p <<= 1;
  return p;
}
void Append(std::vector<float>& buf, std::span<const float> x) {
  size_t n = std::min(x.size(), buf.size());
  std::copy(buf.begin() + static_cast<std::ptrdiff_t>(n), buf.end(), buf.begin());
  std::copy(x.end() - static_cast<std::ptrdiff_t>(n), x.end(), buf.end() - static_cast<std::ptrdiff_t>(n));
}
}  // namespace

EchoDelayEstimator::EchoDelayEstimator(EchoDelayConfig cfg)
    : cfg_(cfg), max_delay_(static_cast<size_t>(cfg.max_delay_ms * cfg.sample_rate / 1000)),
      window_(static_cast<size_t>(cfg.window_ms * cfg.sample_rate / 1000)),
      interval_(static_cast<size_t>(cfg.update_interval_ms * cfg.sample_rate / 1000)),
      far_(max_delay_ + window_, 0.0f), near_(window_, 0.0f), fft_(NextPow2(max_delay_ + 2 * window_)),
      x_(fft_.size()), y_(fft_.size()), cross_(fft_.size()), near_win_(dsp::HannWindow(window_)) {}

void EchoDelayEstimator::AnalyzeRender(std::span<const float> far) { Append(far_, far); }

void EchoDelayEstimator::AnalyzeCapture(std::span<const float> near) {
  Append(near_, near);
  since_update_ += near.size();
  if (since_update_ >= interval_) {
    since_update_ = 0;
    if (dsp::RmsDbfs(std::span<const float>(far_).last(window_)) > cfg_.min_render_dbfs) Update();
  }
}

void EchoDelayEstimator::Update() {
  const size_t n = fft_.size();
  std::fill(x_.begin(), x_.end(), Complex(0, 0));
  std::fill(y_.begin(), y_.end(), Complex(0, 0));
  // Taper both segments: hard window edges would otherwise correlate with
  // each other and produce false peaks at the ends of the lag range.
  const size_t taper = window_ / 4;
  for (size_t i = 0; i < far_.size(); ++i) {
    float w = 1.0f;
    if (i < taper) w = near_win_[i * near_win_.size() / 2 / taper];
    if (far_.size() - 1 - i < taper) w = near_win_[(far_.size() - 1 - i) * near_win_.size() / 2 / taper];
    x_[i] = Complex(far_[i] * w, 0);
  }
  for (size_t i = 0; i < near_.size(); ++i) y_[i] = Complex(near_[i] * near_win_[i], 0);
  fft_.Forward(x_.data());
  fft_.Forward(y_.data());
  // c[m] = sum_i near[i] far[i + m]  <->  conj(Y) X. PHAT-weighted.
  for (size_t k = 0; k < n; ++k) {
    Complex c = std::conj(y_[k]) * x_[k];
    float mag = std::abs(c);
    // Partial phase transform (|C|^0.8): sharp peaks, but bins with almost
    // no energy are not blown up to full weight.
    c = mag > 1e-20f ? c / std::pow(mag, 0.8f) : Complex(0, 0);
    cross_[k] = have_cross_ ? cfg_.smoothing * cross_[k] + (1 - cfg_.smoothing) * c : c;
  }
  have_cross_ = true;
  std::vector<Complex> corr(cross_);
  fft_.Inverse(corr.data());
  // Lag m = max_delay - d, m in [0, max_delay].
  float best = -1, sum = 0;
  size_t best_m = 0;
  for (size_t m = 0; m <= max_delay_; ++m) {
    float v = std::fabs(corr[m].real());
    sum += v;
    if (v > best) {
      best = v;
      best_m = m;
    }
  }
  float mean = sum / static_cast<float>(max_delay_ + 1);
  confidence_ = mean > 0 ? best / mean : 0;
  ++updates_;
  int d = static_cast<int>(max_delay_ - best_m);
  if (confidence_ < cfg_.min_confidence) return;
  if (candidate_ >= 0 && std::abs(d - candidate_) <= 2) {
    ++candidate_count_;
  } else {
    candidate_ = d;
    candidate_count_ = 1;
  }
  if (candidate_count_ >= cfg_.stable_updates && (!delay_ || std::abs(*delay_ - candidate_) > 2)) {
    delay_ = candidate_;
    ++changes_;
    SM_EVENT(cfg_.events, Timestamp::Micros(0), "echo_delay", "estimate", *delay_, static_cast<int64_t>(confidence_));
  }
}

void EchoDelayEstimator::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("delay_ms", delay_ ? *delay_ * 1000.0 / cfg_.sample_rate : -1.0);
  w.Gauge("confidence", confidence_);
  w.Counter("updates", updates_);
  w.Counter("changes", changes_);
}

}  // namespace sm::audio
