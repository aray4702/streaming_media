// libwebrtc's `RmsLevel` (modules/audio_processing/rms_level.cc): the
// level behind the RFC 6464 audio-level extension. In core because both the
// audio section and the RTP send path (`RtpSender`, as `ChannelSend`) use it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace sm::dsp {

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

}  // namespace sm::dsp
