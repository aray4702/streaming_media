// 6.6 Transient (keyboard click) suppression.
//
// Keystrokes are short (2-10 ms), broadband, and start abruptly (< 1 ms).
// Each 1 ms sub-block's energy is compared with a fast (4 ms) envelope of
// the preceding signal, so even a click right after a word stands out; a
// jump of more than `onset_db` marks a transient (twice that if the frame
// is voiced speech, see Vad::voiced(), so vowels are left alone), and the
// following `hold_ms` are attenuated by `attenuation_db` (instant attack,
// smooth ramp back to unity).
#pragma once

#include <span>

#include "sm/core/metrics.h"

namespace sm::audio {

struct TransientSuppressorConfig {
  int sample_rate = 16000;
  float onset_db = 12.0f;
  float attenuation_db = 20.0f;
  float hold_ms = 12.0f;
  float release_ms = 10.0f;
  float envelope_ms = 4.0f;  // Fast: a keystroke rises in < 1 ms, speech does not.
};

class TransientSuppressor : public MetricsSource {
 public:
  explicit TransientSuppressor(TransientSuppressorConfig cfg = {});
  // `voice`: VAD decision; transients during voiced speech are left alone
  // unless they are very strong (2x onset_db).
  void Process(std::span<float> frame, bool voice);
  uint64_t detected() const { return detected_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  TransientSuppressorConfig cfg_;
  size_t sub_;
  float env_ = 0.0f;
  bool env_init_ = false;
  float env_coeff_;
  float gain_ = 1.0f;
  float floor_gain_;
  float release_step_;
  int hold_left_ = 0;
  int hold_blocks_;
  uint64_t detected_ = 0, attenuated_blocks_ = 0;
};

}  // namespace sm::audio
