// 6.9 Audio level indication (RFC 6464) and active speaker detection.
//
// ComputeLevel + extension codec: per-packet level in -dBov (0 = full
// scale, 127 = silence) plus the voice-activity flag, encoded as the
// one-byte RFC 6464 header extension value  V(1) | level(7). An SFU can
// then pick speakers without decoding any audio.
//
// ActiveSpeakerDetector: consumes (stream, level, voice) observations,
// keeps a speech-activity score per stream (exponentially smoothed, voiced
// loud frames score high), and switches the dominant speaker only when a
// challenger beats the current speaker by `switch_margin` for
// `switch_hold`, so short interjections or coughs do not flip the layout.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>

#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::audio {

struct AudioLevel {
  uint8_t level = 127;  // -dBov.
  bool voice = false;
};

uint8_t EncodeAudioLevelExtension(const AudioLevel& l);
AudioLevel DecodeAudioLevelExtension(uint8_t byte);
// RMS level of a frame in -dBov (full-scale square wave = 0).
uint8_t ComputeLevel(std::span<const float> frame);

struct ActiveSpeakerConfig {
  double smoothing = 0.15;      // Per observation (10-20 ms).
  double switch_margin = 1.3;   // Challenger score / current score.
  TimeDelta switch_hold = TimeDelta::Millis(600);
  int silence_level = 70;       // -dBov at or below which a frame is ignored.
};

class ActiveSpeakerDetector : public MetricsSource {
 public:
  explicit ActiveSpeakerDetector(ActiveSpeakerConfig cfg = {}) : cfg_(cfg) {}
  void OnLevel(uint32_t stream, const AudioLevel& l, Timestamp now);
  std::optional<uint32_t> dominant() const { return dominant_; }
  uint64_t switches() const { return switches_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  void Decide(Timestamp now);
  ActiveSpeakerConfig cfg_;
  std::map<uint32_t, double> score_;
  std::optional<uint32_t> dominant_;
  std::optional<uint32_t> challenger_;
  Timestamp challenger_since_ = Timestamp::PlusInfinity();
  uint64_t switches_ = 0;
};

}  // namespace sm::audio
