// Unit tests: libwebrtc M157's noise suppressor (`ns::NoiseSuppressor`,
// `apm v2`, issue #241; modules/audio_processing/ns/).
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <span>
#include <vector>

#include "sm/audio/fft4g.h"
#include "sm/audio/ns.h"
#include "sm/audio/splitting_filter.h"
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

// fft4g.cc (WebRtc_rdft), the FFT under NrFft. The expected values are the
// output of libwebrtc M157's prebuilt WebRtc_rdft on the same input (bit for
// bit). Several of them (a[3], a[6], ...) differ in the last bit if the
// twiddle tables are built with the double cos/sin instead of libc++'s float
// overloads. The twiddle tables come from the C library's cosf/sinf, so the
// bits are those of glibc (the prebuilt libwebrtc is a Linux build); other C
// libraries (Apple's) may round the tables differently, for libwebrtc as for
// us, and are held to a tolerance instead.
SM_TEST(Ns, Fft4gBitExactWithLibwebrtc) {
  std::vector<float> a(256);
  for (size_t i = 0; i < a.size(); ++i) a[i] = static_cast<float>((i * 37) % 101) - 50.f;
  std::vector<size_t> ip(128, 0);
  std::vector<float> w(128);
  audio::ooura::Rdft(256, 1, a.data(), ip.data(), w.data());
  const struct {
    size_t i;
    float v;
  } expected[] = {{0, -0x1.98p+5f},       {1, 0x1.6p+3f},          {2, -0x1.74cc38p+5f},  {3, 0x1.15d2fcp+4f},
                  {6, -0x1.2fb1d4p+7f},   {10, -0x1.d6e85ep+7f},   {11, -0x1.056ap+0f},   {100, -0x1.750808p+7f},
                  {101, 0x1.9af1acp+7f},  {254, 0x1.2e973cp+6f},   {255, -0x1.a310d2p+7f}};
#if defined(__GLIBC__)
  for (const auto& e : expected) SM_EXPECT_EQ(a[e.i], e.v);
#else
  for (const auto& e : expected) SM_EXPECT_LT(std::fabs(a[e.i] - e.v), 1e-3f);
#endif
}

// NrFft::Ifft: the inverse rdft scaled by 2/n gives the frame back.
SM_TEST(Ns, Fft4gInverseScaledByTwoOverN) {
  std::vector<float> x(256), a(256);
  for (size_t i = 0; i < x.size(); ++i) x[i] = 1000.f * std::sin(0.05f * static_cast<float>(i * i % 257));
  a = x;
  std::vector<size_t> ip(128, 0);
  std::vector<float> w(128);
  audio::ooura::Rdft(256, 1, a.data(), ip.data(), w.data());
  audio::ooura::Rdft(256, -1, a.data(), ip.data(), w.data());
  float max_err = 0.f;
  for (size_t i = 0; i < x.size(); ++i) max_err = std::max(max_err, std::fabs(a[i] * (2.f / 256.f) - x[i]));
  SM_EXPECT_LT(max_err, 1e-2f);
}

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

namespace {

// FNV-1a over the output's float bits.
uint64_t HashBits(const std::vector<float>& v) {
  uint64_t h = 1469598103934665603ull;
  for (float f : v) h = (h ^ std::bit_cast<uint32_t>(f)) * 1099511628211ull;
  return h;
}

// Runs `num_channels` channels of a ~7 dB SNR talker (SpeechLike seed 11 + c,
// amplitude 0.1, plus WhiteNoise 0.03 seed 13 + c) through NoiseSuppressor
// at `rate`, kHigh, 10 ms at a time. Above 16 kHz SplittingFilter splits and
// merges the bands, as AudioBuffer does around APM's NoiseSuppressor.
std::vector<std::vector<float>> RunChannels(int rate, size_t num_channels) {
  std::vector<std::vector<float>> y(num_channels);
  for (size_t c = 0; c < num_channels; ++c) {
    auto talk = sim::SpeechLike(rate, 4, 11 + c, true, 0.1f);
    auto noise = sim::WhiteNoise(rate, 4, 0.03f, 13 + c);
    y[c].resize(talk.size());
    for (size_t i = 0; i < talk.size(); ++i) y[c][i] = talk[i] + noise[i];
  }
  NoiseSuppressorConfig cfg;
  cfg.sample_rate = rate;
  cfg.num_channels = num_channels;
  NoiseSuppressor ns(cfg);
  std::vector<std::unique_ptr<audio::SplittingFilter>> split(num_channels);
  if (rate > 16000) {
    for (auto& s : split) s = std::make_unique<audio::SplittingFilter>(rate);
  }
  const size_t frame = static_cast<size_t>(rate / 100);
  std::vector<float> bands(frame * num_channels);
  for (size_t i = 0; i + frame <= y[0].size(); i += frame) {
    for (size_t c = 0; c < num_channels; ++c) {
      std::span<float> full(y[c].data() + i, frame), b(bands.data() + c * frame, frame);
      if (split[c]) {
        split[c]->Analysis(full, b);
      } else {
        std::copy(full.begin(), full.end(), b.begin());
      }
    }
    ns.Analyze(bands);
    ns.Process(bands);
    for (size_t c = 0; c < num_channels; ++c) {
      std::span<float> full(y[c].data() + i, frame), b(bands.data() + c * frame, frame);
      if (split[c]) {
        split[c]->Synthesis(b, full);
      } else {
        std::copy(b.begin(), b.end(), full.begin());
      }
    }
  }
  return y;
}

}  // namespace

// The whole suppressor against libwebrtc M157's: the expected values are the
// output of the prebuilt NoiseSuppressor (compare/webrtc/fetch.sh), kHigh,
// driven through its AudioBuffer (CopyFrom, SplitIntoFrequencyBands,
// Analyze, Process, MergeFrequencyBands, CopyTo) on the same input. With
// more than one channel this checks noise_suppressor.cc's aggregation: one
// Wiener filter, the minimum over the channels per bin, and the minimum
// overall scaling factor and upper-band gain. On x86-64 glibc (libwebrtc's
// build platform) every output sample is identical (the hashes); elsewhere
// the C library's tanh/exp/log may round differently, and the sampled values
// are held to a tolerance.
SM_TEST(Ns, BitExactWithLibwebrtcMultichannel) {
  const struct Case {
    int rate;
    size_t channels;
    uint64_t hash[2];
    struct {
      size_t c, i;
      float v;
    } samples[6];
  } cases[] = {
      {16000, 1, {0xb4ac47dbadf66991ull, 0},
       {{0, 8000, 0x1.39b6eep-8f}, {0, 32000, -0x1.fe2802p-9f}, {0, 63999, -0x1.34506ap-7f}}},
      {16000, 2, {0xdefe44cc13098040ull, 0x0803a277e0217336ull},
       {{0, 8000, 0x1.04980ap-9f}, {0, 32000, -0x1.9f5878p-9f}, {0, 63999, -0x1.59d46p-9f},
        {1, 8000, 0x1.81621ep-9f}, {1, 32000, 0x1.ed8e3ap-9f}, {1, 63999, 0x1.610bc8p-8f}}},
      {48000, 2, {0x57fc956be20834f3ull, 0x7e9727305d52d72full},
       {{0, 24000, 0x1.287d1cp-8f}, {0, 96000, -0x1.9939bp-10f}, {0, 191999, -0x1.13c98cp-9f},
        {1, 24000, -0x1.eee0dep-10f}, {1, 96000, -0x1.a26f96p-9f}, {1, 191999, 0x1.36064p-12f}}},
  };
  for (const Case& k : cases) {
    auto y = RunChannels(k.rate, k.channels);
    for (const auto& s : k.samples) {
      if (s.i == 0) continue;  // Unused slot.
      SM_EXPECT_NEAR(y[s.c][s.i], s.v, 1e-4);
    }
#if defined(__GLIBC__) && defined(__x86_64__)
    for (size_t c = 0; c < k.channels; ++c) SM_EXPECT_EQ(HashBits(y[c]), k.hash[c]);
#endif
  }
}

// With identical channels the aggregation changes nothing: each channel's
// output is the mono output.
SM_TEST(Ns, IdenticalChannelsGiveTheMonoOutput) {
  auto x = sim::SpeechLike(16000, 3, 5, true, 0.1f);
  auto noise = sim::WhiteNoise(16000, 3, 0.03f, 6);
  for (size_t i = 0; i < x.size(); ++i) x[i] += noise[i];
  NoiseSuppressor mono;
  auto ym = Run(mono, x);
  NoiseSuppressorConfig cfg;
  cfg.num_channels = 2;
  NoiseSuppressor stereo(cfg);
  SM_ASSERT_EQ(stereo.num_channels(), 2u);
  std::vector<float> bands(320);
  bool same = true;
  for (size_t i = 0; i + 160 <= x.size(); i += 160) {
    std::copy(x.begin() + i, x.begin() + i + 160, bands.begin());
    std::copy(x.begin() + i, x.begin() + i + 160, bands.begin() + 160);
    stereo.Analyze(bands);
    stereo.Process(bands);
    for (size_t j = 0; j < 160; ++j) same = same && bands[j] == ym[i + j] && bands[160 + j] == ym[i + j];
  }
  SM_EXPECT_TRUE(same);
}

// #594: libwebrtc's NoiseSuppressor runs only at 16, 32 or 48 kHz
// (`NumBandsForRate`'s RTC_DCHECK, noise_suppressor.cc). Any other rate is
// rejected: valid() is false and Analyze/Process leave the audio untouched,
// where 8 and 11.025 kHz used to read past the caller's frame.
SM_TEST(Ns, RejectsUnsupportedRates) {
  for (int rate : {0, 8000, 11025, 12000, 24000, 44100, -16000}) {
    NoiseSuppressorConfig c;
    c.sample_rate = rate;
    NoiseSuppressor ns(c);
    SM_EXPECT_FALSE(ns.valid());
    std::vector<float> f(rate / 100 > 0 ? static_cast<size_t>(rate / 100) : 160, 0.03f);
    for (int i = 0; i < 20; ++i) {
      ns.Analyze(f);
      ns.Process(f);
    }
    SM_EXPECT_TRUE(std::all_of(f.begin(), f.end(), [](float x) { return x == 0.03f; }));
    SM_EXPECT_EQ(ns.stats().processed_frames, 0u);
  }
}

// At a supported rate, a frame shorter than num_channels() * frame_samples()
// is rejected the same way.
SM_TEST(Ns, RejectsShortFrames) {
  NoiseSuppressorConfig c;
  c.sample_rate = 32000;
  NoiseSuppressor ns(c);
  SM_EXPECT_TRUE(ns.valid());
  std::vector<float> f(kNsFrameSize, 0.03f);  // One band; 32 kHz needs two.
  for (int i = 0; i < 20; ++i) {
    ns.Analyze(f);
    ns.Process(f);
  }
  SM_EXPECT_TRUE(std::all_of(f.begin(), f.end(), [](float x) { return x == 0.03f; }));
  SM_EXPECT_EQ(ns.stats().processed_frames, 0u);
}

SM_TEST(Ns, RunsAtFullBandRates) {
  for (int rate : {16000, 32000, 48000}) {
    NoiseSuppressorConfig c;
    c.sample_rate = rate;
    NoiseSuppressor ns(c);
    SM_EXPECT_TRUE(ns.valid());
    std::vector<float> f(ns.frame_samples(), 0.03f);
    for (int i = 0; i < 20; ++i) {
      ns.Analyze(f);
      ns.Process(f);
    }
    SM_EXPECT_EQ(ns.stats().processed_frames, 20u);
  }
}
