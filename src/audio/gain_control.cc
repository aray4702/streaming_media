#include "sm/audio/gain_control.h"

#include <algorithm>
#include <cmath>

#include "sm/core/dsp.h"

namespace sm::audio {

Limiter::Limiter(LimiterConfig cfg) : cfg_(cfg), ceiling_(dsp::DbToLinear(cfg.ceiling_dbfs)) {
  const float block_s = 0.001f;
  release_coeff_ = std::exp(-block_s / (cfg_.release_ms / 1000.0f));
}

float Limiter::current_gain_db() const { return dsp::LinearToDb(gain_); }

void Limiter::Process(std::span<float> x) {
  const size_t sub = std::max<size_t>(1, static_cast<size_t>(cfg_.sample_rate / 1000));
  for (size_t start = 0; start < x.size(); start += sub) {
    size_t end = std::min(x.size(), start + sub);
    float peak = 0;
    for (size_t i = start; i < end; ++i) peak = std::max(peak, std::fabs(x[i]));
    float needed = peak > ceiling_ ? ceiling_ / peak : 1.0f;
    float released = 1.0f - (1.0f - gain_) * release_coeff_;
    float target = std::min(needed, released);
    ++blocks_;
    if (needed < 1.0f) ++limited_blocks_;
    // Attack instantly (no interpolation when reducing), interpolate on release.
    float g0 = target < gain_ ? target : gain_;
    for (size_t i = start; i < end; ++i) {
      float w = static_cast<float>(i - start + 1) / static_cast<float>(end - start);
      float g = g0 + (target - g0) * w;
      float y = x[i] * g;
      x[i] = std::clamp(y, -ceiling_, ceiling_);
    }
    gain_ = target;
  }
}

void Limiter::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("gain_db", current_gain_db());
  w.Counter("limited_blocks", limited_blocks_);
  w.Counter("blocks", blocks_);
}

AutomaticGainControl::AutomaticGainControl(AgcConfig cfg) : cfg_(cfg), limiter_(cfg.limiter) {}

void AutomaticGainControl::Process(std::span<float> frame, bool speech) {
  ++stats_.frames;
  ++recent_frames_;
  for (float v : frame) {
    if (std::fabs(v) >= 0.999f) {
      ++stats_.clipped_input_samples;
      ++recent_clips_;
    }
  }
  const float dt = static_cast<float>(frame.size()) / static_cast<float>(cfg_.sample_rate);
  float level = dsp::RmsDbfs(frame);
  if (speech) {
    ++stats_.speech_frames;
    float a = std::min(1.0f, dt / cfg_.level_time_constant_s);
    if (!level_init_) {
      speech_level_db_ = level;
      level_init_ = true;
    } else {
      // Rise faster than fall so loud onsets are caught quickly.
      float coeff = level > speech_level_db_ ? std::min(1.0f, 4 * a) : a;
      speech_level_db_ += coeff * (level - speech_level_db_);
    }
  } else {
    noise_db_ = noise_db_ < -89.0f ? level : noise_db_ + 0.05f * (level - noise_db_);
  }
  if (speech && level_init_) {
    float desired = std::clamp(cfg_.target_level_dbfs - speech_level_db_, 0.0f, cfg_.max_gain_db);
    // Do not lift the background noise above the allowed floor.
    desired = std::min(desired, std::max(0.0f, cfg_.max_output_noise_dbfs - noise_db_));
    float max_step = cfg_.max_gain_change_db_per_s * dt;
    gain_db_ += std::clamp(desired - gain_db_, -max_step, max_step);
  }
  float g = dsp::DbToLinear(gain_db_);
  for (float& v : frame) v *= g;
  limiter_.Process(frame);
  stats_.gain_db = gain_db_;
  stats_.speech_level_dbfs = speech_level_db_;
  stats_.noise_floor_dbfs = noise_db_;
  if (recent_frames_ > 500) {  // 5 s window for the mic recommendation.
    recent_frames_ /= 2;
    recent_clips_ /= 2;
  }
}

float AutomaticGainControl::recommended_mic_gain_change_db() const {
  if (recent_frames_ > 0 && static_cast<double>(recent_clips_) / static_cast<double>(recent_frames_) > 0.5) return -6.0f;
  if (level_init_ && gain_db_ >= cfg_.max_gain_db - 0.5f &&
      speech_level_db_ + gain_db_ < cfg_.target_level_dbfs - 6.0f) {
    return 6.0f;
  }
  return 0.0f;
}

void AutomaticGainControl::ExportMetrics(MetricsWriter& w) const {
  w.Counter("frames", stats_.frames);
  w.Counter("speech_frames", stats_.speech_frames);
  w.Counter("clipped_input_samples", stats_.clipped_input_samples);
  w.Gauge("speech_level_dbfs", speech_level_db_);
  w.Gauge("gain_db", gain_db_);
  w.Gauge("noise_floor_dbfs", noise_db_);
  w.Gauge("recommended_mic_gain_change_db", recommended_mic_gain_change_db());
  w.Gauge("limiter_gain_db", limiter_.current_gain_db());
}

}  // namespace sm::audio
