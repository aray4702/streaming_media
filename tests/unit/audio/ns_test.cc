// Unit tests: libwebrtc M157's noise suppressor (`ns::NoiseSuppressor`,
// `apm v2`, issue #241; modules/audio_processing/ns/).
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

#include "sm/audio/ns.h"
#include "sm/core/dsp.h"
#include "sm/sim/signals.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::audio::ns;

namespace {

// Runs `x` (one band, 16 kHz) through Analyze + Process, 10 ms at a time.
std::vector<float> Run(NoiseSuppressor& ns, const std::vector<float>& x) {
  std::vector<float> y = x;
  for (size_t i = 0; i + kNsFrameSize <= y.size(); i += kNsFrameSize) {
    std::span<float> f(y.data() + i, kNsFrameSize);
    ns.Analyze(f);
    ns.Process(f);
  }
  return y;
}

double Db(std::span<const float> x) { return dsp::RmsDbfs(x); }

}  // namespace

// suppression_params.cc: the four levels.
SM_TEST(Ns, SuppressionParamsAsLibwebrtc) {
  SuppressionParams p6(SuppressionLevel::k6dB), p12(SuppressionLevel::k12dB), p18(SuppressionLevel::k18dB),
      p21(SuppressionLevel::k21dB);
  SM_EXPECT_EQ(p6.over_subtraction_factor, 1.f);
  SM_EXPECT_EQ(p6.minimum_attenuating_gain, 0.5f);
  SM_EXPECT_FALSE(p6.use_attenuation_adjustment);
  SM_EXPECT_EQ(p12.over_subtraction_factor, 1.f);
  SM_EXPECT_EQ(p12.minimum_attenuating_gain, 0.25f);
  SM_EXPECT_TRUE(p12.use_attenuation_adjustment);
  SM_EXPECT_EQ(p18.over_subtraction_factor, 1.1f);
  SM_EXPECT_EQ(p18.minimum_attenuating_gain, 0.125f);
  SM_EXPECT_TRUE(p18.use_attenuation_adjustment);
  SM_EXPECT_EQ(p21.over_subtraction_factor, 1.25f);
  SM_EXPECT_EQ(p21.minimum_attenuating_gain, 0.09f);
  SM_EXPECT_TRUE(p21.use_attenuation_adjustment);
  // The call default is kHigh (WebRtcVoiceEngine::ApplyOptions).
  SM_EXPECT_TRUE(NoiseSuppressorConfig{}.level == SuppressionLevel::k18dB);
}

// fast_math.cc: the log2 is the float's bits read as an integer, scaled by
// 2^-23, minus 126.942695 (not 127), so it is 0.057 high at powers of two.
SM_TEST(Ns, FastMathApproximations) {
  for (float x : {1.f, 2.f, 1024.f, 0.5f}) SM_EXPECT_NEAR(LogApproximation(x), std::log(x), 0.05);
  for (float x : {1.5f, 3.f, 100.f, 12345.f}) SM_EXPECT_NEAR(LogApproximation(x), std::log(x), 0.07);
  for (float x : {0.f, 1.f, -2.f, 5.f}) SM_EXPECT_NEAR(ExpApproximation(x) / std::exp(x), 1.0, 0.07);
  SM_EXPECT_NEAR(PowApproximation(8.f, 2.f), std::exp2(2 * (130 - 126.942695)), 1e-3);
}

// quantile_noise_estimator.cc: on a stationary spectrum the estimate
// settles at it (the 0.25 quantile of a constant is the constant).
SM_TEST(Ns, QuantileEstimateSettlesOnStationarySpectrum) {
  QuantileNoiseEstimator q;
  Spectrum s, n;
  s.fill(300.f);
  for (int i = 0; i < 1000; ++i) q.Estimate(s, n);
  for (float v : n) SM_EXPECT_NEAR(v / 300.f, 1.0, 0.15);
}

// noise_suppressor.cc Analyze(): all-zero frames are not learnt from.
SM_TEST(Ns, ZeroFramesAreNotAnalyzed) {
  NoiseSuppressor ns;
  std::vector<float> zeros(160 * 50, 0.f);
  auto y = Run(ns, zeros);
  SM_EXPECT_EQ(ns.stats().analyzed_frames, 0u);
  SM_EXPECT_EQ(ns.stats().processed_frames, 50u);
  for (float v : y) SM_EXPECT_EQ(v, 0.f);
}

// The filter bank delays the lowest band by kOverlapSize (96 samples, 6 ms):
// the output lines up best with the input at that lag. (Without noise the
// quantile estimate learns from the speech itself, so weak parts of it are
// suppressed too; the SNR is recorded, not bounded.)
SM_TEST(Ns, PassesCleanSpeechWith96SamplesDelay) {
  NoiseSuppressor ns;
  SM_EXPECT_EQ(NoiseSuppressor::latency_samples(), 96u);
  auto x = sim::SpeechLike(16000, 6, 3);
  const size_t n = x.size();
  auto y = Run(ns, x);
  // Best lag over 0..200 samples, after 3 s.
  const size_t start = 3 * 16000, len = n - start - 200;
  size_t best_lag = 0;
  double best = -1e9;
  for (size_t lag = 0; lag <= 200; ++lag) {
    double snr = dsp::SnrDb({x.data() + start, len}, {y.data() + start + lag, len});
    if (snr > best) {
      best = snr;
      best_lag = lag;
    }
  }
  SM_RECORD("speech_snr_db", best);
  SM_EXPECT_EQ(best_lag, 96u);
}

// Stationary white noise alone is taken down to the level's floor: about
// 18 dB at kHigh, about 6 dB at kLow.
SM_TEST(Ns, AttenuatesNoiseToTheLevelFloor) {
  auto noise = sim::WhiteNoise(16000, 8, 0.03f, 7);
  for (auto [level, expect] : {std::pair{SuppressionLevel::k18dB, 18.0}, std::pair{SuppressionLevel::k6dB, 6.0}}) {
    NoiseSuppressorConfig cfg;
    cfg.level = level;
    NoiseSuppressor ns(cfg);
    auto y = Run(ns, noise);
    const size_t a = 4 * 16000, len = 4 * 16000;
    double atten = Db({noise.data() + a, len}) - Db({y.data() + a + 96, len - 96});
    SM_RECORD(level == SuppressionLevel::k18dB ? "attenuation_k18db" : "attenuation_k6db", atten);
    SM_EXPECT_NEAR(atten, expect, 1.5);
  }
}

// noise_suppressor_unittest.cc's input (a ramp, plus 5000 * band in the
// upper bands, in FloatS16) at every rate and level for 1000 frames: the
// output stays finite and inside the S16 range, and the upper bands are
// delayed by 96 samples and scaled by one gain in [floor, 1].
SM_TEST(Ns, AllRatesAndLevelsAsLibwebrtcUnitTest) {
  for (int rate : {16000, 32000, 48000}) {
    for (auto level : {SuppressionLevel::k6dB, SuppressionLevel::k12dB, SuppressionLevel::k18dB,
                       SuppressionLevel::k21dB}) {
      NoiseSuppressorConfig cfg;
      cfg.sample_rate = rate;
      cfg.level = level;
      NoiseSuppressor ns(cfg);
      const size_t bands = static_cast<size_t>(rate / 16000);
      SM_ASSERT_EQ(ns.num_bands(), bands);
      std::vector<float> frame(bands * 160), prev_upper(160, 0.f);
      bool ok = true;
      for (size_t f = 0; f < 1000; ++f) {
        for (size_t b = 0; b < bands; ++b) {
          for (size_t i = 0; i < 160; ++i) {
            float value = static_cast<float>(f * 160 + i);
            frame[b * 160 + i] = std::fmod(value > 0 ? 5000.f * b + value : 0.f, 32768.f) / 32768.f;
          }
        }
        std::vector<float> in = frame;
        ns.Analyze(frame);
        ns.Process(frame);
        for (float v : frame) ok = ok && std::isfinite(v) && v >= -1.f && v <= 32767.f / 32768.f;
        if (bands > 1 && f > 0) {
          // Sample 96 of the output is sample 0 of this frame's input.
          const float g = ns.stats().last_upper_band_gain;
          ok = ok && g >= SuppressionParams(level).minimum_attenuating_gain && g <= 1.f;
          ok = ok && std::fabs(frame[160 + 96] - g * in[160]) < 1e-6f;
          ok = ok && std::fabs(frame[160] - g * prev_upper[64]) < 1e-6f;
        }
        if (bands > 1) std::copy(in.begin() + 160, in.begin() + 320, prev_upper.begin());
      }
      SM_EXPECT_TRUE(ok);
    }
  }
}
