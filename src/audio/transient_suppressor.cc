#include "sm/audio/transient_suppressor.h"

#include <algorithm>
#include <cmath>

#include "sm/core/dsp.h"

namespace sm::audio {

TransientSuppressor::TransientSuppressor(TransientSuppressorConfig cfg)
    : cfg_(cfg), sub_(static_cast<size_t>(std::max(1, cfg.sample_rate / 1000))) {
  env_coeff_ = std::exp(-1.0f / cfg_.envelope_ms);  // Per 1 ms block.
  floor_gain_ = dsp::DbToLinear(-cfg_.attenuation_db);
  release_step_ = (1.0f - floor_gain_) / std::max(1.0f, cfg_.release_ms);
  hold_blocks_ = static_cast<int>(cfg_.hold_ms);
}

void TransientSuppressor::Process(std::span<float> frame, bool voice) {
  const float onset = dsp::DbToLinear(cfg_.onset_db);
  const float strong = dsp::DbToLinear(2 * cfg_.onset_db);
  for (size_t start = 0; start < frame.size(); start += sub_) {
    size_t end = std::min(frame.size(), start + sub_);
    float e = 0;
    for (size_t i = start; i < end; ++i) e += frame[i] * frame[i];
    e /= static_cast<float>(end - start);
    if (!env_init_) {
      env_ = e;
      env_init_ = true;
    }
    float ratio = std::sqrt(e / (env_ + 1e-12f));
    bool transient = ratio > (voice ? strong : onset) && e > 1e-7f;
    if (transient) {
      if (hold_left_ == 0) ++detected_;
      hold_left_ = hold_blocks_;
    }
    float target;
    if (hold_left_ > 0) {
      --hold_left_;
      target = floor_gain_;
      ++attenuated_blocks_;
    } else {
      target = std::min(1.0f, gain_ + release_step_);
    }
    // Instant attack (a click carries most of its energy in its first
    // millisecond); smooth ramp on release.
    float g0 = target < gain_ ? target : gain_;
    for (size_t i = start; i < end; ++i) {
      float w = static_cast<float>(i - start + 1) / static_cast<float>(end - start);
      frame[i] *= g0 + (target - g0) * w;
    }
    gain_ = target;
    // The envelope tracks the signal but does not jump to transients
    // (during a transient it may rise at most 2x per ms, so a lasting level
    // change is learned instead of locking into "transient").
    float upd = transient ? std::min(e, 2.0f * env_) : e;
    env_ = env_coeff_ * env_ + (1 - env_coeff_) * upd;
  }
}

void TransientSuppressor::ExportMetrics(MetricsWriter& w) const {
  w.Counter("transients_detected", detected_);
  w.Counter("attenuated_ms", attenuated_blocks_);
  w.Gauge("gain", gain_);
}

}  // namespace sm::audio
