#include "sm/audio/vad.h"

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

Vad::Vad(VadConfig cfg)
    : cfg_(cfg), fft_(NextPow2(static_cast<size_t>(cfg.sample_rate / 100))),
      buf_(fft_.size(), 0.0f), spec_(fft_.bins()), window_(dsp::HannWindow(static_cast<size_t>(cfg.sample_rate / 100))) {}

float Vad::Flatness(std::span<const float> frame) {
  std::fill(buf_.begin(), buf_.end(), 0.0f);
  size_t n = std::min(frame.size(), window_.size());
  for (size_t i = 0; i < n; ++i) buf_[i] = frame[i] * window_[i];
  fft_.Forward(buf_.data(), spec_.data());
  // Flatness of a short time-averaged spectrum (~30 ms): residual "musical"
  // noise after noise suppression is peaky in one frame but random across
  // frames, while speech harmonics persist.
  if (avg_psd_.size() != spec_.size()) avg_psd_.assign(spec_.size(), 0.0);
  for (size_t k = 0; k < spec_.size(); ++k) avg_psd_[k] = 0.6 * avg_psd_[k] + 0.4 * std::norm(spec_[k]);
  const double bin_hz = static_cast<double>(cfg_.sample_rate) / static_cast<double>(fft_.size());
  size_t lo = static_cast<size_t>(300 / bin_hz), hi = std::min(spec_.size() - 1, static_cast<size_t>(4000 / bin_hz));
  double log_sum = 0, sum = 0, ilog_sum = 0, isum = 0;
  size_t cnt = 0;
  for (size_t k = lo; k <= hi; ++k) {
    double p = avg_psd_[k] + 1e-12;
    double ip = std::norm(spec_[k]) + 1e-12;
    log_sum += std::log(p);
    sum += p;
    ilog_sum += std::log(ip);
    isum += ip;
    ++cnt;
  }
  if (cnt == 0 || sum <= 0) return 1.0f;
  const double c = static_cast<double>(cnt);
  inst_flatness_ = static_cast<float>(std::exp(ilog_sum / c) / (isum / c));
  return static_cast<float>(std::exp(log_sum / c) / (sum / c));
}

bool Vad::Process(std::span<const float> frame) {
  ++stats_.frames;
  float e = dsp::RmsDbfs(frame);
  const float dt = static_cast<float>(frame.size()) / static_cast<float>(cfg_.sample_rate);
  if (!init_) {
    noise_db_ = e;
    init_ = true;
  }
  float flat = Flatness(frame);
  // Noise floor: fast down, slow up -- but fast up when the frame looks
  // like noise (flat spectrum), so a louder background is learned in ~1 s.
  if (e < noise_db_) {
    noise_db_ = e;
  } else {
    float rise = flat >= cfg_.flatness_threshold ? cfg_.noise_fast_rise_db_per_s : cfg_.noise_rise_db_per_s;
    noise_db_ = std::min(e, noise_db_ + rise * dt);
  }
  noise_db_ = std::max(noise_db_, -100.0f);
  float snr = e - noise_db_;
  stats_.last_snr_db = snr;
  stats_.last_flatness = flat;
  stats_.noise_floor_dbfs = noise_db_;
  raw_ = e > cfg_.min_level_dbfs &&
         ((snr > cfg_.snr_threshold_db && flat < cfg_.flatness_threshold) ||
          (snr > cfg_.strong_snr_db && flat < cfg_.strong_flatness_threshold));
  if (raw_) {
    if (++onset_ >= cfg_.onset_frames) {
      active_ = true;
      hang_ = cfg_.hangover_frames;
    }
  } else {
    onset_ = 0;
    if (hang_ > 0) {
      --hang_;
    } else {
      active_ = false;
    }
  }
  if (active_) ++stats_.speech_frames;
  return active_;
}

void Vad::ExportMetrics(MetricsWriter& w) const {
  w.Counter("frames", stats_.frames);
  w.Counter("speech_frames", stats_.speech_frames);
  w.Gauge("noise_floor_dbfs", stats_.noise_floor_dbfs);
  w.Gauge("snr_db", stats_.last_snr_db);
  w.Gauge("flatness", stats_.last_flatness);
}

}  // namespace sm::audio
