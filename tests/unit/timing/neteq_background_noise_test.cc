// NetEq's BackgroundNoise (issue #62): libwebrtc M157's
// background_noise_unittest.cc (CreateAndDestroy, its only test), plus tests
// of the ported decisions: when the estimate updates (energy threshold,
// spectral flatness), the threshold's rise, the noise Expand mixes in (muted
// to zero, as in M157), and Normal's unmuting from the noise level.
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

std::vector<float> Tone(int rate, double f, size_t n, double rms_int16) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) {
    x[i] = static_cast<float>(rms_int16 * std::sqrt(2.0) / 32768.0 * std::sin(2 * kPi * f * static_cast<double>(i) / rate));
  }
  return x;
}

// A voiced signal: harmonics of `f0` Hz.
std::vector<float> Voiced(int rate, double f0, size_t n, double amplitude) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) {
    double v = 0;
    for (int h = 1; h <= 4; ++h) v += std::sin(2 * kPi * f0 * h * static_cast<double>(i) / rate) / h;
    x[i] = static_cast<float>(amplitude * v / 2);
  }
  return x;
}

double RmsInt16(const int16_t* x, size_t n) {
  double s = 0;
  for (size_t i = 0; i < n; ++i) s += static_cast<double>(x[i]) * x[i];
  return std::sqrt(s / static_cast<double>(n));
}

}  // namespace

// background_noise_unittest.cc CreateAndDestroy, and ChannelParameters::Reset.
SM_TEST(NetEqBackgroundNoise, CreateAndDefaults) {
  NetEqBackgroundNoise bgn;
  SM_EXPECT_FALSE(bgn.initialized());
  SM_EXPECT_EQ(bgn.Energy(), 2500);
  SM_EXPECT_EQ(bgn.energy_update_threshold(), 500000);
  SM_EXPECT_EQ(bgn.MuteFactor(), 0);
  SM_EXPECT_EQ(bgn.scale(), 20000);
  SM_EXPECT_EQ(bgn.scale_shift(), 24);
  SM_EXPECT_EQ(bgn.filter()[0], 4096);
  // Not initialized: zero noise, whatever the mute factor.
  bgn.SetMuteFactor(16384);
  int16_t random[100], out[100];
  for (int i = 0; i < 100; ++i) random[i] = NetEqRandomVector::kRandomTable[i];
  bgn.GenerateBackgroundNoise(random, 100, out);
  for (int16_t v : out) SM_EXPECT_EQ(v, 0);
}

// Quiet white noise is under the threshold and spectrally flat: the
// estimate is saved, with the noise's energy per sample.
SM_TEST(NetEqBackgroundNoise, QuietWhiteNoiseUpdates) {
  NetEqBackgroundNoise bgn;
  const std::vector<float> noise = WhiteNoise(256, 300);
  SM_EXPECT_TRUE(bgn.Update(noise));
  SM_EXPECT_TRUE(bgn.initialized());
  SM_EXPECT_NEAR(bgn.Energy(), 90000, 9000);
  SM_EXPECT_EQ(bgn.energy_update_threshold(), bgn.Energy());
  SM_EXPECT_EQ(bgn.filter()[0], 4096);
  // Unmuted, the generated noise has about the same level.
  bgn.SetMuteFactor(16384);
  int16_t random[240], out[240];
  for (int i = 0; i < 240; ++i) random[i] = NetEqRandomVector::kRandomTable[i];
  bgn.GenerateBackgroundNoise(random, 240, out);
  const double rms = RmsInt16(out, 240);
  SM_EXPECT_GT(rms, 150.0);
  SM_EXPECT_LT(rms, 600.0);
  // Muted (Expand sets 0 for every expand period): zero noise.
  bgn.SetMuteFactor(0);
  bgn.GenerateBackgroundNoise(random, 240, out);
  for (int16_t v : out) SM_EXPECT_EQ(v, 0);
}

// A quiet tone is under the threshold but not flat (5 * residual < 16 *
// energy): the threshold follows it down, the filter is not saved.
SM_TEST(NetEqBackgroundNoise, QuietToneIsNotFlat) {
  NetEqBackgroundNoise bgn;
  const std::vector<float> tone = Tone(16000, 500, 256, 300);
  SM_EXPECT_FALSE(bgn.Update(tone));
  SM_EXPECT_FALSE(bgn.initialized());
  SM_EXPECT_EQ(bgn.Energy(), 2500);
  SM_EXPECT_NEAR(bgn.energy_update_threshold(), 90000, 9000);
}

// Silence: auto_correlation[0] is 0, nothing changes.
SM_TEST(NetEqBackgroundNoise, SilenceDoesNotUpdate) {
  NetEqBackgroundNoise bgn;
  SM_EXPECT_FALSE(bgn.Update(std::vector<float>(256, 0.0f)));
  SM_EXPECT_FALSE(bgn.initialized());
  SM_EXPECT_EQ(bgn.energy_update_threshold(), 500000);
}

// Above the threshold, IncrementEnergyThreshold raises it by 229/65536 per
// update: about 4x in 400 updates (4 s of 10 ms GetAudio calls).
SM_TEST(NetEqBackgroundNoise, LoudSignalRaisesTheThreshold) {
  NetEqBackgroundNoise bgn;
  const std::vector<float> loud = WhiteNoise(256, 3000);  // Energy 9e6 per sample.
  for (int i = 0; i < 400; ++i) SM_EXPECT_FALSE(bgn.Update(loud));
  const double factor = bgn.energy_update_threshold() / 500000.0;
  SM_EXPECT_GT(factor, 3.8);
  SM_EXPECT_LT(factor, 4.3);
  SM_EXPECT_FALSE(bgn.initialized());
  // Once the threshold passes the signal's energy, a flat signal is saved.
  int updates = 400;
  while (!bgn.Update(loud) && updates < 2000) ++updates;
  SM_EXPECT_TRUE(bgn.initialized());
  SM_EXPECT_LT(updates, 1000);  // 9e6 / 5e5 = 18x: about 840 updates.
  SM_EXPECT_NEAR(bgn.Energy(), 9000000, 900000);
}

// Expand mixes the background noise in, muted to zero at the start of every
// expand period (M157 never raises it): the expansion is the same as without
// a noise estimate.
SM_TEST(NetEqBackgroundNoise, ExpandAddsNoNoise) {
  const std::vector<float> history = Voiced(16000, 180, 1600, 0.3);
  NetEqExpand with(16000), without(16000);
  SM_ASSERT_TRUE(with.background_noise().Update(WhiteNoise(256, 300)));
  std::vector<float> a = history, b = history, out_a, out_b;
  for (int i = 0; i < 6; ++i) {
    out_a.clear();
    out_b.clear();
    with.Process(a, out_a);
    without.Process(b, out_b);
    a.insert(a.end(), out_a.begin(), out_a.end());
    b.insert(b.end(), out_b.begin(), out_b.end());
  }
  SM_ASSERT_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) SM_EXPECT_EQ(a[i], b[i]);
  SM_EXPECT_EQ(with.background_noise().MuteFactor(), 0);
  SM_EXPECT_TRUE(with.background_noise().initialized());  // Expand::Reset keeps it.
  with.Reset();
  SM_EXPECT_TRUE(with.background_noise().initialized());
}

// Normal::Process after an expand: audio quieter than the noise starts at
// full gain; louder audio starts at sqrt(noise energy / energy), or at
// Expand's mute factor if that is higher.
SM_TEST(NetEqBackgroundNoise, NormalMuteFactor) {
  const std::vector<float> loud = WhiteNoise(320, 1000);  // Energy about 1e6.
  SM_EXPECT_EQ(NetEqNormalMuteFactor(std::vector<float>(320, 0.0f), 16000, 0, 2500), 16384);
  SM_EXPECT_EQ(NetEqNormalMuteFactor(WhiteNoise(320, 40), 16000, 0, 2500), 16384);
  // 2500 / 1e6: sqrt is 0.05, 819 in Q14.
  const int16_t from_noise = NetEqNormalMuteFactor(loud, 16000, 0, 2500);
  SM_EXPECT_NEAR(from_noise, 819, 120);
  // A noise estimate of 250000 (rms 500): sqrt(0.25) = 0.5.
  SM_EXPECT_NEAR(NetEqNormalMuteFactor(loud, 16000, 0, 250000), 8192, 900);
  // Expand's mute factor wins when higher.
  SM_EXPECT_EQ(NetEqNormalMuteFactor(loud, 16000, 12000, 2500), 12000);
  // Before BackgroundNoise was ported (energy 0), only Expand's applied.
  SM_EXPECT_EQ(NetEqNormalMuteFactor(loud, 16000, 0, 0), 0);
}

// The jitter buffer updates the estimate from decoded audio and unmutes the
// first decode after a delayed packet from it: after quiet noise, voiced
// audio returns louder than after a quiet tone (which leaves no estimate).
SM_TEST(AudioJitterBufferNetEqV2, NormalAfterExpandUnmutesFromTheNoiseLevel) {
  auto first_output_energy = [](const std::vector<float>& preface) {
    AudioJitterBufferConfig cfg;
    cfg.version = 2;
    AudioJitterBuffer jb(cfg);
    std::vector<float> out(160);
    const std::vector<float> voice = Voiced(16000, 200, 320 * 80, 0.3);
    auto packet = [&](int i) {
      const std::vector<float>& src = i < 50 ? preface : voice;
      const size_t at = static_cast<size_t>(i < 50 ? i : i - 50) * 320;
      return std::vector<float>(src.begin() + static_cast<std::ptrdiff_t>(at), src.begin() + static_cast<std::ptrdiff_t>(at + 320));
    };
    for (int i = 0; i < 50; ++i) {
      jb.InsertPacket(static_cast<uint32_t>(i * 320), packet(i), Timestamp::Millis(i * 20));
      jb.GetAudio(out, Timestamp::Millis(i * 20));
      jb.GetAudio(out, Timestamp::Millis(i * 20 + 10));
    }
    // 500 ms without packets: Expand mutes to zero.
    for (int t = 1000; t < 1500; t += 10) jb.GetAudio(out, Timestamp::Millis(t));
    // Packet 50 arrives late (no timestamp gap): a normal decode after the
    // expand. Expand has muted to zero, so the decoded audio starts at the
    // first nonzero output sample.
    std::vector<float> played;
    for (int i = 50; i < 60; ++i) {
      jb.InsertPacket(static_cast<uint32_t>(i * 320), packet(i), Timestamp::Millis(1500 + (i - 50) * 20));
      for (int k = 0; k < 2; ++k) {
        jb.GetAudio(out, Timestamp::Millis(1500 + (i - 50) * 20 + k * 10));
        played.insert(played.end(), out.begin(), out.end());
      }
    }
    size_t start = 0;
    while (start < played.size() && played[start] == 0.0f) ++start;
    double energy = 0;
    for (size_t n = start; n < std::min(played.size(), start + 48); ++n) energy += static_cast<double>(played[n]) * played[n];
    return energy;
  };
  const double after_noise = first_output_energy(WhiteNoise(320 * 50, 300));
  const double after_tone = first_output_energy(Tone(16000, 500, 320 * 50, 300));
  SM_ASSERT_GT(after_tone, 0.0);
  SM_EXPECT_GT(after_noise, 1.5 * after_tone);
}
