// 6.4 Automatic gain control (AGC2-style adaptive digital gain), limiter
// and clipping detection.
//
// Limiter: splits each frame into 1 ms sub-blocks, computes the gain that
// keeps every sub-block's peak under `ceiling_dbfs`, applies it instantly
// (attack) and recovers with an exponential release, interpolating the gain
// across each sub-block so there are no zipper artifacts. A final hard clip
// guarantees the ceiling.
//
// AutomaticGainControl: estimates the speech level (RMS during VAD-active
// frames, slow EMA) and moves the digital gain toward
// target_level - speech_level, limited to `max_gain_db` and to
// `max_gain_change_db_per_s` so the gain never pumps. Gain is frozen in
// non-speech frames, and capped so the noise floor is not amplified above
// `max_output_noise_dbfs`. Input clipping is counted and turned into a
// microphone gain recommendation (the analog part of AGC1/AGC2).
#pragma once

#include <span>

#include "sm/core/metrics.h"

namespace sm::audio {

struct LimiterConfig {
  int sample_rate = 16000;
  float ceiling_dbfs = -1.0f;
  float release_ms = 60.0f;
};

class Limiter : public MetricsSource {
 public:
  explicit Limiter(LimiterConfig cfg = {});
  void Process(std::span<float> x);
  float current_gain_db() const;
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  LimiterConfig cfg_;
  float ceiling_;
  float gain_ = 1.0f;
  float release_coeff_;
  uint64_t limited_blocks_ = 0, blocks_ = 0;
};

struct AgcConfig {
  int sample_rate = 16000;
  float target_level_dbfs = -18.0f;  // Speech RMS target.
  float max_gain_db = 30.0f;
  float max_gain_change_db_per_s = 6.0f;
  float max_output_noise_dbfs = -50.0f;
  float level_time_constant_s = 1.0f;
  LimiterConfig limiter;
};

struct AgcStats {
  uint64_t frames = 0;
  uint64_t speech_frames = 0;
  uint64_t clipped_input_samples = 0;
  float speech_level_dbfs = -90.0f;
  float gain_db = 0;
  float noise_floor_dbfs = -90.0f;
};

class AutomaticGainControl : public MetricsSource {
 public:
  explicit AutomaticGainControl(AgcConfig cfg = {});
  // `speech`: VAD decision for this frame.
  void Process(std::span<float> frame, bool speech);
  float gain_db() const { return gain_db_; }
  // Suggested microphone (analog) gain change in dB; negative if the input
  // is clipping, positive if speech stays very quiet even at max gain.
  float recommended_mic_gain_change_db() const;
  const AgcStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  AgcConfig cfg_;
  Limiter limiter_;
  float speech_level_db_ = -90.0f;
  bool level_init_ = false;
  float noise_db_ = -90.0f;
  float gain_db_ = 0.0f;
  uint64_t recent_clips_ = 0, recent_frames_ = 0;
  AgcStats stats_;
};

}  // namespace sm::audio
