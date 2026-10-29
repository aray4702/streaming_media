// 6.9 Audio level indication (RFC 6464).
//
// ComputeLevel + extension codec: per-packet level in -dBov (0 = full
// scale, 127 = silence) plus the voice-activity flag, encoded as the
// one-byte RFC 6464 header extension value  V(1) | level(7). The receiver
// can show a level meter or see voice activity without decoding any audio.
//
// The level is libwebrtc's `RmsLevel` (modules/audio_processing/rms_level.cc,
// #567): `ChannelSend` feeds it every 10 ms capture frame (`AnalyzeMuted` for
// a frame muted after a muted one) and writes `Average()` into each packet
// (audio/channel_send.cc:390-393, 913-921), so a 20 ms packet carries the
// level of both of its frames.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace sm::audio {

struct AudioLevel {
  uint8_t level = 127;  // -dBov.
  bool voice = false;
};

uint8_t EncodeAudioLevelExtension(const AudioLevel& l);
AudioLevel DecodeAudioLevelExtension(uint8_t byte);

// libwebrtc's `RmsLevel` (RFC 6465): accumulates the mean square of int16
// samples over the chunks passed since the last `Average()`, as -dBov in
// [0, 127]. 127 is digital silence; a faint but non-zero input reads 126.
// A chunk of a different size than the last one restarts the measurement.
class RmsLevel {
 public:
  struct Levels {
    int average;
    int peak;
  };
  static constexpr int kMinLevelDb = 127;
  static constexpr int kInaudibleButNotMuted = 126;

  RmsLevel() { Reset(); }
  void Reset();
  void Analyze(std::span<const int16_t> data);
  // Samples in int16 range (not normalized), truncated to int16 as libwebrtc.
  void Analyze(std::span<const float> data);
  // A chunk of `length` zero samples.
  void AnalyzeMuted(size_t length);
  // The level since the last call; resets the measurement.
  int Average();
  // Also the peak: the loudest chunk's level. Resets the measurement.
  Levels AverageAndPeak();

 private:
  void CheckBlockSize(size_t block_size);

  float sum_square_ = 0.f;
  size_t sample_count_ = 0;
  float max_sum_square_ = 0.f;
  std::optional<size_t> block_size_;
};

// Level of one frame of normalized samples ([-1, 1]) in -dBov: `RmsLevel` of
// the frame scaled to int16 (full-scale square wave = 0).
uint8_t ComputeLevel(std::span<const float> frame);

// libwebrtc's `voe::AudioLevel` (audio/audio_level.cc): the stats level of a
// send stream. The full-range level is the maximum absolute sample, refreshed
// every 11th frame and decayed to a quarter after each refresh; the total
// energy grows by the energy of that level (not of the frame, crbug 10784)
// times the frame duration. The RFC 6464 header-extension level above is a
// different quantity (libwebrtc takes it from the audio processing RMS).
class AudioLevelMeter {
 public:
  static constexpr int kUpdateFrequency = 10;
  void Reset();
  // `abs_max` is the largest |sample| of the frame in int16 units (0 if muted).
  void ComputeLevel(int16_t abs_max, double duration_s);
  void ComputeLevel(std::span<const int16_t> frame, double duration_s);
  int16_t LevelFullRange() const { return current_level_full_range_; }
  void ResetLevelFullRange();
  double TotalEnergy() const { return total_energy_; }
  double TotalDuration() const { return total_duration_; }

 private:
  int16_t abs_max_ = 0;
  int16_t count_ = 0;
  int16_t current_level_full_range_ = 0;
  double total_energy_ = 0.0;
  double total_duration_ = 0.0;
};

}  // namespace sm::audio
