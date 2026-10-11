// Unit tests: DTX/CNG, audio level / active speaker, mixer.
#include <algorithm>
#include <cmath>

#include "sm/audio/audio_level.h"
#include "sm/audio/comfort_noise.h"
#include "sm/audio/mixer.h"
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

SM_TEST(AudioLevel, ActiveSpeakerHysteresis) {
  ActiveSpeakerDetector asd;
  int64_t t = 0;
  auto feed = [&](int frames, uint8_t a, uint8_t b) {
    for (int i = 0; i < frames; ++i, t += 20) {
      asd.OnLevel(1, {a, a < 60}, Timestamp::Millis(t));
      asd.OnLevel(2, {b, b < 60}, Timestamp::Millis(t));
    }
  };
  feed(100, 25, 127);  // Speaker 1 talks for 2 s.
  SM_EXPECT_EQ(*asd.dominant(), 1u);
  feed(10, 127, 25);   // 200 ms interjection: no switch.
  SM_EXPECT_EQ(*asd.dominant(), 1u);
  feed(100, 127, 25);  // Speaker 2 keeps talking: switch.
  SM_EXPECT_EQ(*asd.dominant(), 2u);
  SM_EXPECT_EQ(asd.switches(), 2u);
}

SM_TEST(Mixer, LoudestNWithoutClipping) {
  MixerConfig cfg;
  cfg.max_mixed = 3;
  AudioMixer mixer(cfg);
  std::vector<std::vector<float>> src;
  const float amps[5] = {0.9f, 0.8f, 0.7f, 0.05f, 0.04f};
  for (int i = 0; i < 5; ++i) src.push_back(sim::Sine(200 + 100 * i, 16000, 1.0, amps[i]));
  std::vector<float> out(160);
  float peak = 0;
  for (size_t off = 0; off + 160 <= src[0].size(); off += 160) {
    std::map<uint32_t, std::span<const float>> m;
    for (uint32_t i = 0; i < 5; ++i) m[i] = std::span<const float>(src[i].data() + off, 160);
    mixer.Mix(m, out);
    for (float v : out) peak = std::max(peak, std::fabs(v));
  }
  auto mixed = mixer.mixed();
  std::sort(mixed.begin(), mixed.end());
  SM_ASSERT_EQ(mixed.size(), 3u);
  SM_EXPECT_EQ(mixed[0], 0u);
  SM_EXPECT_EQ(mixed[2], 2u);
  SM_EXPECT_LE(peak, dsp::DbToLinear(-1.0f) + 1e-6f);
}

SM_TEST(Mixer, RampAvoidsClickOnJoin) {
  AudioMixer mixer;
  auto a = sim::Sine(300, 16000, 1.0, 0.3f);
  std::vector<float> out(160);
  float max_jump = 0, prev = 0;
  for (size_t off = 0; off + 160 <= a.size(); off += 160) {
    std::map<uint32_t, std::span<const float>> m;
    if (off >= 8000) m[7] = std::span<const float>(a.data() + off, 160);  // Joins mid-way.
    mixer.Mix(m, out);
    for (float v : out) {
      max_jump = std::max(max_jump, std::fabs(v - prev));
      prev = v;
    }
  }
  // Max per-sample slope of a 300 Hz, 0.3 amplitude sine is ~0.035.
  SM_EXPECT_LT(max_jump, 0.05f);
}
