// Unit tests: DTX/CNG, audio level.
#include <algorithm>
#include <cmath>

#include "sm/audio/audio_level.h"
#include "sm/audio/comfort_noise.h"
#include "sm/core/dsp.h"
#include "sm/sim/signals.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::audio;

namespace {
// Energy ratio (dB) of the band below 2 kHz to the band above.
double TiltDb(const std::vector<float>& x) {
  dsp::Biquad lp(dsp::BiquadCoeffs::LowPass(2000, 16000));
  dsp::Biquad hp(dsp::BiquadCoeffs::HighPass(2000, 16000));
  double lo = 0, hi = 0;
  for (float v : x) {
    float l = lp.Process(v), h = hp.Process(v);
    lo += l * l;
    hi += h * h;
  }
  return 10 * std::log10(lo / hi);
}
// Low-pass "room" noise.
std::vector<float> ColoredNoise(double seconds, float amp, uint64_t seed) {
  auto x = sim::WhiteNoise(16000, seconds, amp, seed);
  dsp::Biquad lp(dsp::BiquadCoeffs::LowPass(800, 16000));
  for (float& v : x) v = lp.Process(v);
  return x;
}
}  // namespace

SM_TEST(ComfortNoise, SidRoundTripAndLevel) {
  auto noise = ColoredNoise(0.02, 0.05f, 1);
  SidFrame s = AnalyzeNoise(std::span<const float>(noise).last(160));
  SM_EXPECT_EQ(s.reflection.size(), 10u);
  auto bytes = EncodeSid(s);
  SM_EXPECT_EQ(bytes.size(), 11u);
  auto back = DecodeSid(bytes);
  SM_ASSERT_TRUE(back.has_value());
  SM_EXPECT_EQ(back->level, s.level);
  for (size_t i = 0; i < s.reflection.size(); ++i) SM_EXPECT_NEAR(back->reflection[i], s.reflection[i], 1.0 / 127);
  SM_EXPECT_NEAR(-static_cast<double>(s.level), dsp::RmsDbfs(std::span<const float>(noise).last(160)), 1.0);
  SM_EXPECT_FALSE(DecodeSid(std::vector<uint8_t>{200}).has_value());
}

SM_TEST(ComfortNoise, GeneratedNoiseMatchesLevelAndSpectrum) {
  auto noise = ColoredNoise(2.0, 0.08f, 2);
  ComfortNoiseGenerator cng;
  std::vector<float> out;
  for (size_t i = 0; i + 160 <= noise.size(); i += 160) {
    if (i % 6400 == 0) cng.OnSid(AnalyzeNoise(std::span<const float>(noise.data() + i, 160)));
    std::vector<float> f(160);
    cng.Generate(f);
    out.insert(out.end(), f.begin(), f.end());
  }
  std::span<const float> o = std::span<const float>(out).subspan(1600);
  double level_err = dsp::RmsDbfs(o) - dsp::RmsDbfs(noise);
  double tilt_err = TiltDb(std::vector<float>(o.begin(), o.end())) - TiltDb(noise);
  SM_RECORD("cng_level_error_db", level_err);
  SM_RECORD("cng_tilt_error_db", tilt_err);
  SM_EXPECT_NEAR(level_err, 0.0, 2.0);
  SM_EXPECT_NEAR(tilt_err, 0.0, 4.0);
}

SM_TEST(ComfortNoise, DtxDecisions) {
  DtxController dtx;
  auto noise = ColoredNoise(2.0, 0.02f, 3);
  std::span<const float> f(noise.data(), 160);
  SidFrame sid;
  SM_EXPECT_TRUE(dtx.Process(f, true, &sid) == DtxDecision::kSendSpeech);
  SM_EXPECT_TRUE(dtx.Process(f, false, &sid) == DtxDecision::kSendSid);
  int sids = 0, suppressed = 0;
  for (int i = 0; i < 100; ++i) {  // 1 s of silence.
    auto d = dtx.Process(std::span<const float>(noise.data() + 160 * (i % 100), 160), false, &sid);
    sids += d == DtxDecision::kSendSid;
    suppressed += d == DtxDecision::kSuppress;
  }
  SM_EXPECT_GE(sids, 2);   // Keep-alive every 400 ms.
  SM_EXPECT_LE(sids, 6);
  SM_EXPECT_GE(suppressed, 90);
  // A 10 dB louder background triggers an update immediately.
  std::vector<float> loud(160);
  for (size_t i = 0; i < 160; ++i) loud[i] = noise[i] * 3.2f;
  SM_EXPECT_TRUE(dtx.Process(loud, false, &sid) == DtxDecision::kSendSid);
}

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
}
