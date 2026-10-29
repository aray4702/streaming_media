// Unit tests: audio level.
#include <algorithm>
#include <cmath>

#include "sm/audio/audio_level.h"
#include "sm/core/dsp.h"
#include "sm/sim/signals.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::audio;

SM_TEST(AudioLevel, ExtensionAndLevel) {
  AudioLevel l{42, true};
  uint8_t b = EncodeAudioLevelExtension(l);
  SM_EXPECT_EQ(b, 0x80 | 42);
  auto d = DecodeAudioLevelExtension(b);
  SM_EXPECT_EQ(d.level, 42);
  SM_EXPECT_TRUE(d.voice);
  auto sine = sim::Sine(1000, 16000, 0.01, 0.1f);  // RMS 0.0707 -> 23 dBov
  SM_EXPECT_EQ(ComputeLevel(sine), 23);
  std::vector<float> silence(160, 0.0f);
  SM_EXPECT_EQ(ComputeLevel(silence), 127);
  std::vector<float> faint(4800, 0.0f);
  faint[3] = 1.0f / 32768;  // One LSB: inaudible, but not digital silence.
  SM_EXPECT_EQ(ComputeLevel(faint), RmsLevel::kInaudibleButNotMuted);
}

// modules/audio_processing/rms_level.cc (#567): the RFC 6464 level is the
// mean square over every chunk since the last Average(), rounded as libwebrtc.
SM_TEST(RmsLevel, AveragesTheChunksSinceTheLastCallAndResets) {
  RmsLevel r;
  SM_EXPECT_EQ(r.Average(), RmsLevel::kMinLevelDb);  // No samples.
  std::vector<int16_t> loud(160, 1000), zero(160, 0), full(160, 32767);
  r.Analyze(loud);
  SM_EXPECT_EQ(r.Average(), 30);  // -10 log10(1e6 / 2^30) = 30.3.
  SM_EXPECT_EQ(r.Average(), RmsLevel::kMinLevelDb);  // Average() reset it.
  r.Analyze(loud);
  r.Analyze(zero);
  SM_EXPECT_EQ(r.Average(), 33);  // Half the energy: 33.3.
  r.Analyze(loud);
  r.AnalyzeMuted(160);  // The same as a chunk of zeros.
  SM_EXPECT_EQ(r.Average(), 33);
  r.Analyze(full);
  SM_EXPECT_EQ(r.Average(), 0);
  std::vector<int16_t> one(160, 1);
  r.Analyze(one);
  SM_EXPECT_EQ(r.Average(), 90);
}

SM_TEST(RmsLevel, FaintButNotMutedReads126AndSilence127) {
  RmsLevel r;
  // One sample of 1 in 4800: a mean square below 10^(-127/10) of full scale.
  std::vector<int16_t> chunk(480, 0);
  chunk[7] = 1;
  r.Analyze(chunk);
  chunk[7] = 0;
  for (int i = 0; i < 9; ++i) r.Analyze(chunk);
  SM_EXPECT_EQ(r.Average(), RmsLevel::kInaudibleButNotMuted);
  for (int i = 0; i < 10; ++i) r.Analyze(chunk);
  SM_EXPECT_EQ(r.Average(), RmsLevel::kMinLevelDb);
  r.AnalyzeMuted(480);
  SM_EXPECT_EQ(r.Average(), RmsLevel::kMinLevelDb);
}

SM_TEST(RmsLevel, ANewChunkSizeRestartsAndPeakIsTheLoudestChunk) {
  RmsLevel r;
  std::vector<int16_t> loud(160, 1000), zero(160, 0), zero320(320, 0);
  r.Analyze(loud);
  r.Analyze(zero320);  // Another size: the loud chunk is dropped.
  SM_EXPECT_EQ(r.Average(), RmsLevel::kMinLevelDb);
  r.Analyze(loud);
  r.Analyze(zero);
  const auto levels = r.AverageAndPeak();
  SM_EXPECT_EQ(levels.average, 33);
  SM_EXPECT_EQ(levels.peak, 30);
  const auto empty = r.AverageAndPeak();
  SM_EXPECT_EQ(empty.average, RmsLevel::kMinLevelDb);
  SM_EXPECT_EQ(empty.peak, RmsLevel::kMinLevelDb);
  // Float samples are in int16 range and truncated to int16 first.
  std::vector<float> f(160, 1000.9f);
  r.Analyze(std::span<const float>(f));
  SM_EXPECT_EQ(r.Average(), 30);
  std::vector<float> clipped(160, 1e6f);
  r.Analyze(std::span<const float>(clipped));
  SM_EXPECT_EQ(r.Average(), 0);
}

// audio/audio_level.cc (voe::AudioLevel, #101): the level is the maximum
// absolute sample, refreshed every 11th frame, and decays to a quarter.
SM_TEST(AudioLevelMeter, RefreshesEvery11thFrameAndDecaysToAQuarter) {
  AudioLevelMeter m;
  for (int i = 0; i < 10; ++i) {
    m.ComputeLevel(static_cast<int16_t>(1000), 0.01);
    SM_EXPECT_EQ(m.LevelFullRange(), 0);  // Not refreshed yet.
  }
  m.ComputeLevel(static_cast<int16_t>(4000), 0.01);  // The 11th call.
  SM_EXPECT_EQ(m.LevelFullRange(), 4000);
  // The maximum decays by 4 after the refresh: a quiet next window reports 1000.
  for (int i = 0; i < 10; ++i) m.ComputeLevel(static_cast<int16_t>(0), 0.01);
  SM_EXPECT_EQ(m.LevelFullRange(), 4000);
  m.ComputeLevel(static_cast<int16_t>(0), 0.01);
  SM_EXPECT_EQ(m.LevelFullRange(), 1000);
}

SM_TEST(AudioLevelMeter, EnergyUsesTheLevelNotTheFrameAndMutedIsZero) {
  AudioLevelMeter m;
  for (int i = 0; i < 11; ++i) m.ComputeLevel(static_cast<int16_t>(32767), 0.01);
  // Ten frames at level 0, one at full scale: only the 11th adds energy.
  SM_EXPECT_NEAR(m.TotalEnergy(), 0.01, 1e-9);
  SM_EXPECT_NEAR(m.TotalDuration(), 0.11, 1e-9);
  m.Reset();
  SM_EXPECT_EQ(m.LevelFullRange(), 0);
  SM_EXPECT_EQ(m.TotalEnergy(), 0.0);
  std::vector<int16_t> pcm(160, 0);
  pcm[3] = -32768;  // |-32768| saturates to 32767.
  m.ComputeLevel(pcm, 0.01);
  for (int i = 0; i < 10; ++i) m.ComputeLevel(std::span<const int16_t>(pcm.data(), 0), 0.01);
  SM_EXPECT_EQ(m.LevelFullRange(), 32767);
}
