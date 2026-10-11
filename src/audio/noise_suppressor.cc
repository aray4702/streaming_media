#include "sm/audio/noise_suppressor.h"

#include <algorithm>
#include <cmath>

#include "sm/core/dsp.h"

namespace sm::audio {

namespace {
size_t NextPow2(size_t n) {
  size_t p = 1;
  while (p < n) p <<= 1;
  return p;
}
}  // namespace

NoiseSuppressor::NoiseSuppressor(NoiseSuppressorConfig cfg)
    : cfg_(cfg), hop_(static_cast<size_t>(cfg.sample_rate / 100)), win_(2 * hop_), fft_(NextPow2(win_)),
      window_(dsp::SqrtHannWindow(win_)), in_hist_(win_, 0.0f), ola_(win_ - hop_, 0.0f), buf_(fft_.size()),
      spec_(fft_.bins()), psd_(fft_.bins(), 0.0f), noise_(fft_.bins(), 0.0f), prev_gain_(fft_.bins(), 1.0f),
      prev_power_(fft_.bins(), 0.0f) {}

void NoiseSuppressor::Process(std::span<float> frame) {
  const size_t n = std::min(frame.size(), hop_);
  // Slide the input history.
  std::copy(in_hist_.begin() + static_cast<std::ptrdiff_t>(hop_), in_hist_.end(), in_hist_.begin());
  std::copy(frame.begin(), frame.begin() + static_cast<std::ptrdiff_t>(n), in_hist_.end() - static_cast<std::ptrdiff_t>(hop_));
  std::fill(buf_.begin(), buf_.end(), 0.0f);
  for (size_t i = 0; i < win_; ++i) buf_[i] = in_hist_[i] * window_[i];
  fft_.Forward(buf_.data(), spec_.data());

  const float dt = static_cast<float>(hop_) / static_cast<float>(cfg_.sample_rate);
  const float rise = std::pow(10.0f, cfg_.noise_rise_db_per_s * dt / 10.0f);
  const float floor_gain = dsp::DbToLinear(-cfg_.max_attenuation_db);
  const bool warmup = frames_ < 10;
  double gain_sum = 0, noise_sum = 0;
  for (size_t k = 0; k < spec_.size(); ++k) {
    float p = std::norm(spec_[k]);
    psd_[k] = cfg_.psd_smoothing * psd_[k] + (1 - cfg_.psd_smoothing) * p;
    if (warmup) {
      // Bootstrap: assume the first 100 ms are mostly noise.
      noise_[k] = frames_ == 0 ? psd_[k] : 0.7f * noise_[k] + 0.3f * psd_[k];
    } else if (psd_[k] < noise_[k]) {
      noise_[k] = 0.9f * noise_[k] + 0.1f * psd_[k];
      if (psd_[k] < 0.5f * noise_[k]) noise_[k] = psd_[k];
    } else {
      noise_[k] = std::min(noise_[k] * rise, psd_[k]);
    }
    float nk = std::max(noise_[k], 1e-12f);
    float gamma = p / nk;
    float xi = cfg_.dd_alpha * prev_gain_[k] * prev_gain_[k] * prev_power_[k] / nk +
               (1 - cfg_.dd_alpha) * std::max(gamma - 1.0f, 0.0f);
    float g = std::max(xi / (1.0f + xi), floor_gain);
    prev_gain_[k] = g;
    prev_power_[k] = p;
    spec_[k] *= g;
    gain_sum += g;
    noise_sum += nk;
  }
  fft_.Inverse(spec_.data(), buf_.data());
  // Overlap-add: output the first hop, keep the rest.
  for (size_t i = 0; i < win_; ++i) buf_[i] *= window_[i];
  for (size_t i = 0; i < n; ++i) frame[i] = ola_[i] + buf_[i];
  for (size_t i = 0; i < win_ - hop_; ++i) ola_[i] = buf_[hop_ + i];
  ++frames_;
  ++stats_.frames;
  float mean_gain = static_cast<float>(gain_sum / static_cast<double>(spec_.size()));
  stats_.last_mean_gain_db = dsp::LinearToDb(mean_gain);
  stats_.gain_db.Add(-stats_.last_mean_gain_db);
  // Noise level relative to a full-scale sine in the same window.
  double ref = static_cast<double>(win_) * static_cast<double>(win_) / 8.0;
  stats_.noise_level_dbfs = static_cast<float>(10 * std::log10(noise_sum / static_cast<double>(spec_.size()) / ref + 1e-20));
}

void NoiseSuppressor::ExportMetrics(MetricsWriter& w) const {
  w.Counter("frames", stats_.frames);
  w.Gauge("mean_gain_db", stats_.last_mean_gain_db);
  w.Gauge("noise_level_db", stats_.noise_level_dbfs);
  w.Hist("attenuation_db", stats_.gain_db);
}

}  // namespace sm::audio
