// NetEq's Accelerate and PreemptiveExpand (issue #62): libwebrtc M157's
// time_stretch_unittest.cc (CreateAndDestroy; Accelerate and
// AccelerateFastMode read testfile32kHz.pcm, which isn't in the tree, so
// synthetic voiced signals and noise stand in), plus tests of the ported
// decisions: the pitch period, the 0.9 (0.5 fast) correlation threshold, the
// VAD against the background noise energy, the 15 ms split, preemptive
// expand's old data rule, the crossfade, and the jitter buffer's use of them.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "sm/timing/audio_jitter_buffer.h"
#include "sm/timing/neteq_expand.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::timing;

namespace {

constexpr double kPi = 3.14159265358979323846;
using Result = NetEqTimeStretch::Result;

// Harmonics of `f0` Hz, peak amplitude about `amplitude` (full scale 1).
std::vector<float> Voiced(int rate, double f0, size_t n, double amplitude) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) {
    double v = 0;
    for (int h = 1; h <= 4; ++h) v += std::sin(2 * kPi * f0 * h * static_cast<double>(i) / rate) / h;
    x[i] = static_cast<float>(amplitude * v / 2);
  }
  return x;
}

// White noise with an rms of `rms_int16` on the int16 scale (deterministic LCG).
std::vector<float> WhiteNoise(size_t n, double rms_int16, uint32_t seed = 1) {
  std::vector<float> x(n);
  uint32_t s = seed;
  const double amplitude = rms_int16 * std::sqrt(3.0) / 32768.0;  // Uniform in [-a, a].
  for (float& v : x) {
    s = s * 1664525u + 1013904223u;
    v = static_cast<float>(amplitude * (2.0 * (s >> 8) / 16777216.0 - 1.0));
  }
  return x;
}

std::vector<float> Add(std::vector<float> a, const std::vector<float>& b) {
  for (size_t i = 0; i < a.size(); ++i) a[i] += b[i];
  return a;
}

bool SamePrefix(const std::vector<float>& a, const std::vector<float>& b, size_t n) {
  return a.size() >= n && b.size() >= n && std::equal(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(n), b.begin());
}

}  // namespace

// time_stretch_unittest.cc CreateAndDestroy, at every NetEq rate.
SM_TEST(NetEqTimeStretch, CreateAndDestroy) {
  for (int rate : {8000, 16000, 32000, 48000}) {
    NetEqTimeStretch ts(rate);
    SM_EXPECT_EQ(ts.peak_index(), 0u);
  }
  SM_EXPECT_EQ(NetEqTimeStretch::kCorrelationThreshold, 14746);
  SM_EXPECT_EQ(NetEqTimeStretch::kFastCorrelationThreshold, 8192);
}

// A loud voiced block of 30 ms: the period (125 Hz, 8 ms, twice that is past
// the 15 ms longest lag) is found at every rate, the correlation is above
// 0.9, and accelerate removes one period after 15 ms unchanged.
SM_TEST(NetEqTimeStretch, AccelerateVoicedRemovesOnePeriod) {
  for (int rate : {8000, 16000, 32000, 48000}) {
    const size_t fs_mult = static_cast<size_t>(rate / 8000);
    const size_t period = static_cast<size_t>(rate / 125);
    const std::vector<float> in = Voiced(rate, 125, 240 * fs_mult, 0.4);
    NetEqTimeStretch ts(rate);
    NetEqBackgroundNoise bgn;
    std::vector<float> out;
    size_t change = 99;
    SM_EXPECT_TRUE(ts.Accelerate(in, false, bgn, out, &change) == Result::kSuccess);
    SM_EXPECT_TRUE(ts.active_speech());
    SM_EXPECT_GT(ts.best_correlation(), NetEqTimeStretch::kCorrelationThreshold);
    SM_EXPECT_LE(ts.best_correlation(), 16384);
    SM_EXPECT_NEAR(static_cast<double>(change), static_cast<double>(period), static_cast<double>(fs_mult));
    SM_EXPECT_EQ(change, ts.peak_index());
    SM_EXPECT_EQ(out.size() + change, in.size());
    SM_EXPECT_TRUE(SamePrefix(out, in, 120 * fs_mult - change));
    // After the crossfade the output is the input one period later.
    SM_EXPECT_EQ(out.back(), in.back());
    SM_EXPECT_EQ(out[120 * fs_mult], in[120 * fs_mult + change]);
  }
}

// AudioVector::CrossFade: the period before 15 ms fades into the one after
// it with weights falling by 16384 / (L + 1) in Q14.
SM_TEST(NetEqTimeStretch, AccelerateCrossfadeIsLibwebrtcsRamp) {
  const std::vector<float> in = Voiced(16000, 125, 480, 0.4);
  NetEqTimeStretch ts(16000);
  NetEqBackgroundNoise bgn;
  std::vector<float> out;
  size_t l = 0;
  SM_ASSERT_TRUE(ts.Accelerate(in, false, bgn, out, &l) == Result::kSuccess);
  const int step = 16384 / (static_cast<int>(l) + 1);
  for (size_t i : {size_t{0}, l / 2, l - 1}) {
    const int alpha = 16384 - step * static_cast<int>(i + 1);
    const float expected = (static_cast<float>(alpha) * in[240 - l + i] + static_cast<float>(16384 - alpha) * in[240 + i]) / 16384.0f;
    SM_EXPECT_NEAR(out[240 - l + i], expected, 1e-6);
  }
}

// Loud white noise is active speech without a strong correlation: no stretch,
// and the input is copied.
SM_TEST(NetEqTimeStretch, AccelerateRejectsUncorrelatedSpeech) {
  const std::vector<float> in = WhiteNoise(480, 5000);
  NetEqTimeStretch ts(16000);
  NetEqBackgroundNoise bgn;
  std::vector<float> out;
  size_t change = 99;
  SM_EXPECT_TRUE(ts.Accelerate(in, false, bgn, out, &change) == Result::kNoStretch);
  SM_EXPECT_TRUE(ts.active_speech());
  SM_EXPECT_LT(ts.best_correlation(), NetEqTimeStretch::kCorrelationThreshold);
  SM_EXPECT_EQ(change, 0u);
  SM_EXPECT_TRUE(out == in);
}

// Fast accelerate (0.5 in Q14) takes a period whose correlation is between
// 0.5 and 0.9, and removes as many whole periods as fit in 15 ms.
SM_TEST(NetEqTimeStretch, FastModeThresholdAndWholePeriods) {
  const std::vector<float> in = Add(Voiced(16000, 125, 480, 0.4), WhiteNoise(480, 2200, 7));
  NetEqTimeStretch ts(16000);
  NetEqBackgroundNoise bgn;
  std::vector<float> out;
  size_t change = 0;
  SM_EXPECT_TRUE(ts.Accelerate(in, false, bgn, out, &change) == Result::kNoStretch);
  SM_EXPECT_GT(ts.best_correlation(), NetEqTimeStretch::kFastCorrelationThreshold);
  SM_EXPECT_LE(ts.best_correlation(), NetEqTimeStretch::kCorrelationThreshold);
  out.clear();
  SM_EXPECT_TRUE(ts.Accelerate(in, true, bgn, out, &change) == Result::kSuccess);
  SM_EXPECT_EQ(change, (240 / ts.peak_index()) * ts.peak_index());
  SM_EXPECT_EQ(out.size() + change, in.size());

  // A short period: fast mode removes three of them.
  const std::vector<float> high = Voiced(16000, 250, 480, 0.4);
  out.clear();
  SM_EXPECT_TRUE(ts.Accelerate(high, true, bgn, out, &change) == Result::kSuccess);
  SM_EXPECT_EQ(change, (240 / ts.peak_index()) * ts.peak_index());
  SM_EXPECT_GE(change, 2 * ts.peak_index());
}

// Until the background noise is estimated the VAD's threshold is 75000: a
// block at up to 8 * 75000 energy per sample (rms 775) is passive speech and
// is stretched whatever its correlation (kSuccessLowEnergy). Once the noise
// is estimated, the threshold is 8 times its energy.
SM_TEST(NetEqTimeStretch, VadAgainstTheBackgroundNoise) {
  const std::vector<float> in = WhiteNoise(480, 500, 3);
  NetEqTimeStretch ts(16000);
  NetEqBackgroundNoise bgn;
  std::vector<float> out;
  size_t change = 0;
  SM_EXPECT_TRUE(ts.Accelerate(in, false, bgn, out, &change) == Result::kSuccessLowEnergy);
  SM_EXPECT_FALSE(ts.active_speech());
  SM_EXPECT_EQ(ts.best_correlation(), 0);
  SM_EXPECT_GT(change, 0u);
  SM_EXPECT_EQ(out.size() + change, in.size());

  // Louder than 8 * 75000 per sample: active, and rejected.
  out.clear();
  SM_EXPECT_TRUE(ts.Accelerate(WhiteNoise(480, 1100, 3), false, bgn, out, &change) == Result::kNoStretch);

  // Noise estimated at rms 100 (energy about 10000): rms 500 is active now.
  SM_ASSERT_TRUE(bgn.Update(WhiteNoise(256, 100, 5)));
  SM_ASSERT_TRUE(bgn.initialized());
  SM_EXPECT_LT(bgn.Energy(), 20000);
  out.clear();
  SM_EXPECT_TRUE(ts.Accelerate(in, false, bgn, out, &change) == Result::kNoStretch);
  SM_EXPECT_TRUE(ts.active_speech());
  // And rms 200 (40000, under 8 * 10000) is passive.
  out.clear();
  SM_EXPECT_TRUE(ts.Accelerate(WhiteNoise(480, 200, 9), false, bgn, out, &change) == Result::kSuccessLowEnergy);
}

// Accelerate::Process and PreemptiveExpand::Process need (almost) 30 ms:
// (2 * 120 - 1) * fs_mult samples; shorter is copied with kError.
SM_TEST(NetEqTimeStretch, ShortInputIsAnError) {
  NetEqTimeStretch ts(16000);
  NetEqBackgroundNoise bgn;
  std::vector<float> out;
  size_t change = 99;
  const std::vector<float> in = Voiced(16000, 125, 477, 0.4);
  SM_EXPECT_TRUE(ts.Accelerate(in, false, bgn, out, &change) == Result::kError);
  SM_EXPECT_EQ(change, 0u);
  SM_EXPECT_TRUE(out == in);
  out.clear();
  SM_EXPECT_TRUE(ts.PreemptiveExpand(in, 0, 10, bgn, out, &change) == Result::kError);
  SM_EXPECT_TRUE(out == in);
  out.clear();
  SM_EXPECT_TRUE(ts.Accelerate(Voiced(16000, 125, 478, 0.4), false, bgn, out, &change) == Result::kSuccess);
}

// Preemptive expand repeats the period before 15 ms: one period longer, the
// first 15 ms unchanged, and the input continues one period later.
SM_TEST(NetEqTimeStretch, PreemptiveExpandVoicedAddsOnePeriod) {
  for (int rate : {8000, 16000, 32000, 48000}) {
    const size_t fs_mult = static_cast<size_t>(rate / 8000);
    const std::vector<float> in = Voiced(rate, 125, 240 * fs_mult, 0.4);
    NetEqTimeStretch ts(rate);
    NetEqBackgroundNoise bgn;
    std::vector<float> out;
    size_t change = 0;
    SM_EXPECT_TRUE(ts.PreemptiveExpand(in, 0, 5 * fs_mult, bgn, out, &change) == Result::kSuccess);
    SM_EXPECT_NEAR(static_cast<double>(change), static_cast<double>(rate / 125), static_cast<double>(fs_mult));
    SM_EXPECT_EQ(out.size(), in.size() + change);
    SM_EXPECT_TRUE(SamePrefix(out, in, 120 * fs_mult));
    SM_EXPECT_EQ(out[120 * fs_mult + change], in[120 * fs_mult]);
    SM_EXPECT_EQ(out.back(), in.back());
  }
}

// PreemptiveExpand's old data: active speech is stretched only with at most
// 15 ms already played; passive speech is stretched after the old data,
// with a period of at most the new samples; old data within the overlap of
// the end is an error.
SM_TEST(NetEqTimeStretch, PreemptiveExpandOldData) {
  NetEqTimeStretch ts(16000);
  NetEqBackgroundNoise bgn;
  std::vector<float> out;
  size_t change = 0;
  const std::vector<float> loud = Voiced(16000, 125, 480, 0.4);
  SM_EXPECT_TRUE(ts.PreemptiveExpand(loud, 240, 10, bgn, out, &change) == Result::kSuccess);
  out.clear();
  SM_EXPECT_TRUE(ts.PreemptiveExpand(loud, 241, 10, bgn, out, &change) == Result::kNoStretch);
  SM_EXPECT_EQ(change, 0u);
  SM_EXPECT_TRUE(out == loud);

  const std::vector<float> quiet = Voiced(16000, 125, 480, 0.01);
  out.clear();
  SM_EXPECT_TRUE(ts.PreemptiveExpand(quiet, 400, 10, bgn, out, &change) == Result::kSuccessLowEnergy);
  SM_EXPECT_LE(change, 80u);
  SM_EXPECT_EQ(out.size(), quiet.size() + change);
  SM_EXPECT_TRUE(SamePrefix(out, quiet, 400));

  out.clear();
  SM_EXPECT_TRUE(ts.PreemptiveExpand(quiet, 470, 10, bgn, out, &change) == Result::kError);
  SM_EXPECT_TRUE(out == quiet);
}

// The jitter buffer under version 2: a 200 ms minimum delay is reached by
// preemptive expand and, once it is lifted, left by accelerate. Each one
// adds or removes one 8 ms period of the 125 Hz signal (libwebrtc's DSP),
// not an arbitrary length, and nothing is concealed.
SM_TEST(AudioJitterBufferNetEqV2, TimeStretchMovesWholePitchPeriods) {
  AudioJitterBufferConfig cfg;
  cfg.version = 2;
  AudioJitterBuffer jb(cfg);
  std::vector<float> out(160);
  const std::vector<float> voice = Voiced(16000, 125, 320 * 300, 0.4);
  jb.SetMinimumDelayMs(200);
  uint64_t accelerations = 0, expansions = 0;
  for (int i = 0; i < 300; ++i) {
    if (i == 150) jb.SetMinimumDelayMs(0);
    jb.InsertPacket(static_cast<uint32_t>(i * 320), std::vector<float>(voice.begin() + i * 320, voice.begin() + (i + 1) * 320),
                    Timestamp::Millis(i * 20));
    for (int k = 0; k < 2; ++k) {
      const AudioOperation op = jb.GetAudio(out, Timestamp::Millis(i * 20 + k * 10));
      if (op == AudioOperation::kAccelerate) ++accelerations;
      if (op == AudioOperation::kPreemptiveExpand) ++expansions;
    }
    if (i == 149) {
      SM_EXPECT_EQ(accelerations, 0u);
      SM_EXPECT_GT(jb.CurrentLevelMs(), 180.0);
    }
  }
  SM_ASSERT_GT(expansions, 0u);
  SM_ASSERT_GT(accelerations, 0u);
  const auto& st = jb.stats();
  SM_EXPECT_NEAR(static_cast<double>(st.inserted_samples_for_deceleration) / static_cast<double>(expansions), 128.0, 2.0);
  SM_EXPECT_NEAR(static_cast<double>(st.removed_samples_for_acceleration) / static_cast<double>(accelerations), 128.0, 2.0);
  SM_EXPECT_LT(jb.CurrentLevelMs(), 60.0);
  SM_EXPECT_EQ(st.concealed_samples, 0u);
}
