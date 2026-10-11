// 6.8 Voice activity detection.
//
// Per 10 ms frame:
//  - frame energy (dBFS) against a tracked noise floor: the floor follows
//    the energy down immediately, rises slowly (3 dB/s) during non-flat
//    (speech-like) frames so speech cannot pull it up, and quickly
//    (30 dB/s) during flat, noise-like frames so a louder background is
//    learned in about a second;
//  - spectral flatness of the 300-4000 Hz band of a ~30 ms averaged power
//    spectrum: voiced speech has a peaky (harmonic) spectrum, noise a flat
//    one (averaging also flattens the random peaks of musical noise left
//    by noise suppression);
//  - decision: SNR above `snr_threshold_db` and a non-flat spectrum (or a
//    very high SNR), with a 2-frame onset requirement and a hangover so
//    word endings and short pauses are not clipped.
#pragma once

#include <span>
#include <vector>

#include "sm/core/fft.h"
#include "sm/core/metrics.h"

namespace sm::audio {

struct VadConfig {
  int sample_rate = 16000;
  float snr_threshold_db = 8.0f;
  float strong_snr_db = 18.0f;    // Above this, a looser flatness test applies
  float strong_flatness_threshold = 0.65f;  // (unvoiced fricatives are flat-ish).
  float flatness_threshold = 0.5f;
  float noise_rise_db_per_s = 3.0f;
  float noise_fast_rise_db_per_s = 30.0f;  // While frames look like noise.
  int onset_frames = 2;
  int hangover_frames = 20;       // 200 ms.
  float min_level_dbfs = -70.0f;  // Absolute floor: quieter is never speech.
};

struct VadStats {
  uint64_t frames = 0;
  uint64_t speech_frames = 0;
  float noise_floor_dbfs = 0;
  float last_snr_db = 0;
  float last_flatness = 0;
};

class Vad : public MetricsSource {
 public:
  explicit Vad(VadConfig cfg = {});
  // Returns true if the frame contains speech (after hangover).
  bool Process(std::span<const float> frame);
  bool raw_decision() const { return raw_; }
  // Current frame is voiced speech (harmonic spectrum, above the noise
  // floor), without hangover. Used by the transient suppressor: a keyboard
  // click is broadband, so it is never "voiced".
  // Uses this frame's own spectrum (not the averaged one the VAD decision
  // uses), so a click right after a word is not mistaken for voicing.
  bool voiced() const { return stats_.last_snr_db > cfg_.snr_threshold_db && inst_flatness_ < cfg_.flatness_threshold; }
  float noise_floor_dbfs() const { return noise_db_; }
  const VadStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  float Flatness(std::span<const float> frame);
  VadConfig cfg_;
  RealFft fft_;
  std::vector<float> buf_;
  std::vector<Complex> spec_;
  std::vector<float> window_;
  std::vector<double> avg_psd_;
  float inst_flatness_ = 1.0f;
  float noise_db_ = -60.0f;
  bool init_ = false;
  int onset_ = 0, hang_ = 0;
  bool raw_ = false, active_ = false;
  VadStats stats_;
};

}  // namespace sm::audio
