#include "sm/audio/audio_level.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <vector>

namespace sm::audio {

uint8_t EncodeAudioLevelExtension(const AudioLevel& l) {
  return static_cast<uint8_t>((l.voice ? 0x80 : 0) | (std::min<int>(l.level, 127) & 0x7f));
}

AudioLevel DecodeAudioLevelExtension(uint8_t b) { return AudioLevel{static_cast<uint8_t>(b & 0x7f), (b & 0x80) != 0}; }

namespace {
// modules/audio_processing/rms_level.cc.
constexpr float kMaxSquaredLevel = 32768 * 32768;
// The level of kMinLevelDb, 10^(-127/10).
constexpr float kMinLevel = 1.995262314968883e-13f;

// -dBov of a mean square of int16 samples, rounded as libwebrtc (+0.5, truncate).
int ComputeRms(float mean_square) {
  if (mean_square <= kMinLevel * kMaxSquaredLevel) return RmsLevel::kMinLevelDb;
  const float mean_square_norm = mean_square / kMaxSquaredLevel;
  const float rms = 10.f * std::log10(mean_square_norm);  // 20 log10(x^0.5).
  return static_cast<int>(-rms + 0.5f);
}
}  // namespace

void RmsLevel::Reset() {
  sum_square_ = 0.f;
  sample_count_ = 0;
  max_sum_square_ = 0.f;
  block_size_ = std::nullopt;
}

void RmsLevel::Analyze(std::span<const int16_t> data) {
  if (data.empty()) return;
  CheckBlockSize(data.size());
  const float sum_square = std::accumulate(data.begin(), data.end(), 0.f, [](float a, int16_t b) { return a + b * b; });
  sum_square_ += sum_square;
  sample_count_ += data.size();
  max_sum_square_ = std::max(max_sum_square_, sum_square);
}

void RmsLevel::Analyze(std::span<const float> data) {
  if (data.empty()) return;
  CheckBlockSize(data.size());
  float sum_square = 0.f;
  for (float x : data) {
    const auto tmp = static_cast<int16_t>(std::min(std::max(x, -32768.f), 32767.f));
    sum_square += tmp * tmp;
  }
  sum_square_ += sum_square;
  sample_count_ += data.size();
  max_sum_square_ = std::max(max_sum_square_, sum_square);
}

void RmsLevel::AnalyzeMuted(size_t length) {
  CheckBlockSize(length);
  sample_count_ += length;
}

int RmsLevel::Average() {
  const bool have_samples = sample_count_ != 0;
  int rms = have_samples ? ComputeRms(sum_square_ / static_cast<float>(sample_count_)) : kMinLevelDb;
  // kMinLevelDb is kept for digital silence (RFC 6464): a faint non-zero input reads one above it.
  if (have_samples && rms == kMinLevelDb && sum_square_ != 0.0f) rms = kInaudibleButNotMuted;
  Reset();
  return rms;
}

RmsLevel::Levels RmsLevel::AverageAndPeak() {
  const Levels levels = sample_count_ == 0
                            ? Levels{kMinLevelDb, kMinLevelDb}
                            : Levels{ComputeRms(sum_square_ / static_cast<float>(sample_count_)),
                                     ComputeRms(max_sum_square_ / static_cast<float>(*block_size_))};
  Reset();
  return levels;
}

void RmsLevel::CheckBlockSize(size_t block_size) {
  if (block_size_ != block_size) {
    Reset();
    block_size_ = block_size;
  }
}

uint8_t ComputeLevel(std::span<const float> frame) {
  std::vector<float> scaled(frame.size());
  for (size_t i = 0; i < frame.size(); ++i) scaled[i] = frame[i] * 32768.f;
  RmsLevel level;
  level.Analyze(scaled);
  return static_cast<uint8_t>(level.Average());
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
