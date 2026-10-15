#include "sm/audio/audio_level.h"

#include <algorithm>
#include <cmath>

#include "sm/core/dsp.h"

namespace sm::audio {

uint8_t EncodeAudioLevelExtension(const AudioLevel& l) {
  return static_cast<uint8_t>((l.voice ? 0x80 : 0) | (std::min<int>(l.level, 127) & 0x7f));
}

AudioLevel DecodeAudioLevelExtension(uint8_t b) { return AudioLevel{static_cast<uint8_t>(b & 0x7f), (b & 0x80) != 0}; }

uint8_t ComputeLevel(std::span<const float> frame) {
  float rms = dsp::Rms(frame);
  if (rms <= 0) return 127;
  return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(-20.0f * std::log10(rms))), 0, 127));
}

}  // namespace sm::audio
