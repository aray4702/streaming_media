#include "sm/audio/audio_level.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

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

void AudioLevelMeter::Reset() {
  ResetLevelFullRange();
  total_energy_ = 0.0;
  total_duration_ = 0.0;
}

void AudioLevelMeter::ResetLevelFullRange() {
  abs_max_ = 0;
  count_ = 0;
  current_level_full_range_ = 0;
}

void AudioLevelMeter::ComputeLevel(std::span<const int16_t> frame, double duration_s) {
  int abs_value = 0;
  for (int16_t v : frame) abs_value = std::max(abs_value, std::abs(static_cast<int>(v)));  // |-32768| saturates below.
  ComputeLevel(static_cast<int16_t>(std::min(abs_value, 32767)), duration_s);
}

void AudioLevelMeter::ComputeLevel(int16_t abs_value, double duration_s) {
  if (abs_value > abs_max_) abs_max_ = abs_value;
  if (count_++ == kUpdateFrequency) {
    current_level_full_range_ = abs_max_;
    count_ = 0;
    abs_max_ >>= 2;
  }
  double additional_energy = static_cast<double>(current_level_full_range_) / 32767;
  additional_energy *= additional_energy;
  total_energy_ += additional_energy * duration_s;
  total_duration_ += duration_s;
}

}  // namespace sm::audio
