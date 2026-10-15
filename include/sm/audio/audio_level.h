// 6.9 Audio level indication (RFC 6464).
//
// ComputeLevel + extension codec: per-packet level in -dBov (0 = full
// scale, 127 = silence) plus the voice-activity flag, encoded as the
// one-byte RFC 6464 header extension value  V(1) | level(7). The receiver
// can show a level meter or see voice activity without decoding any audio.
#pragma once

#include <cstdint>
#include <span>

namespace sm::audio {

struct AudioLevel {
  uint8_t level = 127;  // -dBov.
  bool voice = false;
};

uint8_t EncodeAudioLevelExtension(const AudioLevel& l);
AudioLevel DecodeAudioLevelExtension(uint8_t byte);
// RMS level of a frame in -dBov (full-scale square wave = 0).
uint8_t ComputeLevel(std::span<const float> frame);

}  // namespace sm::audio
