// Unit tests: high-pass filter, VAD, noise suppressor, AGC/limiter,
// transient suppressor, resampler.
#include <algorithm>
#include <cmath>
#include <numbers>

#include "sm/audio/gain_control.h"
#include "sm/audio/high_pass_filter.h"
#include "sm/audio/noise_suppressor.h"
#include "sm/audio/resampler.h"
#include "sm/audio/transient_suppressor.h"
#include "sm/audio/vad.h"
#include "sm/core/dsp.h"
#include "sm/sim/signals.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::audio;

namespace {
double GainDb(double freq, int fs = 16000) {
  HighPassFilter hpf(fs);
  auto x = sim::Sine(freq, fs, 2.0, 0.5f);
  for (size_t i = 0; i < x.size(); i += 160) hpf.Process(std::span<float>(x.data() + i, 160));
  std::span<const float> tail(x.data() + fs, static_cast<size_t>(fs));  // Skip the transient.
  return dsp::RmsDbfs(tail) - dsp::LinearToDb(0.5f / std::sqrt(2.0f));
}
}  // namespace

SM_TEST(HighPassFilter, FrequencyResponse) {
  SM_RECORD("hpf_gain_20hz_db", GainDb(20));
  SM_RECORD("hpf_gain_50hz_db", GainDb(50));
  SM_RECORD("hpf_gain_300hz_db", GainDb(300));
  SM_EXPECT_LT(GainDb(20), -40.0);
  SM_EXPECT_LT(GainDb(50), -14.0);
  SM_EXPECT_NEAR(GainDb(300), 0.0, 0.2);
  SM_EXPECT_NEAR(GainDb(1000), 0.0, 0.1);
}

SM_TEST(HighPassFilter, RemovesDcOffset) {
  HighPassFilter hpf;
  std::vector<float> x(16000, 0.3f);
  for (size_t i = 0; i < x.size(); i += 160) hpf.Process(std::span<float>(x.data() + i, 160));
  SM_EXPECT_LT(dsp::RmsDbfs(std::span<const float>(x).last(1600)), -80.0f);
}

SM_TEST(Vad, SpeechVersusNoise) {
  const int fs = 16000;
  auto speech = sim::SpeechLike(fs, 10.0, 3, true);
  auto noise = sim::WhiteNoise(fs, 10.0, 0.01f, 4);  // -40 dBFS
  Vad vad;
  int tp = 0, fp = 0, fn = 0, tn = 0;
  int since_speech = 1000;  // Frames since the last true speech frame.
  for (size_t i = 0; i + 160 <= speech.size(); i += 160) {
    std::vector<float> f(160);
    bool truth = dsp::RmsDbfs(std::span<const float>(speech.data() + i, 160)) > -40.0f;
    since_speech = truth ? 0 : since_speech + 1;
    for (size_t k = 0; k < 160; ++k) f[k] = speech[i + k] + noise[i + k];
    bool d = vad.Process(f);
    if (i < 16000) continue;  // Let the noise floor settle.
    if (truth) {
      (d ? tp : fn)++;
    } else if (since_speech > 22) {  // Beyond the intended 200 ms hangover.
      (d ? fp : tn)++;
    }
  }
  double recall = static_cast<double>(tp) / (tp + fn);
  double fpr = static_cast<double>(fp) / std::max(1, fp + tn);
  SM_RECORD("vad_recall", recall);
  SM_RECORD("vad_false_positive_rate", fpr);
  SM_EXPECT_GT(recall, 0.95);
  SM_EXPECT_LT(fpr, 0.05);
}

SM_TEST(Vad, NoiseLevelStepIsNotSpeech) {
  Vad vad;
  auto quiet = sim::WhiteNoise(16000, 3.0, 0.005f, 1);
  auto loud = sim::WhiteNoise(16000, 3.0, 0.05f, 2);  // +20 dB, still noise.
  int speech_frames = 0;
  for (size_t i = 0; i + 160 <= quiet.size(); i += 160) vad.Process(std::span<const float>(quiet.data() + i, 160));
  for (size_t i = 0; i + 160 <= loud.size(); i += 160) speech_frames += vad.Process(std::span<const float>(loud.data() + i, 160));
  // White noise is spectrally flat; only the first moments after the step
  // (above the strong-SNR bypass) may fire until the floor catches up.
  SM_RECORD("vad_frames_on_noise_step", speech_frames);
  SM_EXPECT_LT(speech_frames, 300 * 0.25);
}

SM_TEST(NoiseSuppressor, AttenuatesStationaryNoise) {
  NoiseSuppressor ns;
  auto noise = sim::WhiteNoise(16000, 4.0, 0.05f, 5);
  for (size_t i = 0; i + 160 <= noise.size(); i += 160) ns.Process(std::span<float>(noise.data() + i, 160));
  float out_db = dsp::RmsDbfs(std::span<const float>(noise).last(16000));
  float in_db = dsp::LinearToDb(0.05f);
  SM_RECORD("ns_noise_reduction_db", in_db - out_db);
  SM_EXPECT_GT(in_db - out_db, 12.0);
}

SM_TEST(NoiseSuppressor, ImprovesSnr) {
  const int fs = 16000;
  auto clean = sim::SpeechLike(fs, 8.0, 6, true);
  auto noise = sim::WhiteNoise(fs, 8.0, 0.03f, 7);
  std::vector<float> noisy(clean.size());
  for (size_t i = 0; i < clean.size(); ++i) noisy[i] = clean[i] + noise[i];
  std::vector<float> out = noisy;
  NoiseSuppressor ns;
  for (size_t i = 0; i + 160 <= out.size(); i += 160) ns.Process(std::span<float>(out.data() + i, 160));
  // Align for the 10 ms latency; skip the first 2 s (noise estimate warm-up).
  size_t lat = ns.latency_samples(), start = 2 * fs, n = clean.size() - start - lat;
  double snr_in = dsp::SnrDb(std::span<const float>(clean.data() + start, n), std::span<const float>(noisy.data() + start, n));
  double snr_out = dsp::SnrDb(std::span<const float>(clean.data() + start, n), std::span<const float>(out.data() + start + lat, n));
  SM_RECORD("ns_snr_in_db", snr_in);
  SM_RECORD("ns_snr_out_db", snr_out);
  SM_EXPECT_GT(snr_out, snr_in + 4.0);
}

SM_TEST(GainControl, LimiterNeverExceedsCeiling) {
  Limiter lim;
  auto x = sim::Sine(440, 16000, 1.0, 2.0f);  // +6 dBFS peaks.
  for (size_t i = 0; i < x.size(); i += 160) lim.Process(std::span<float>(x.data() + i, 160));
  float peak = 0;
  for (float v : x) peak = std::max(peak, std::fabs(v));
  SM_EXPECT_LE(peak, dsp::DbToLinear(-1.0f) + 1e-6f);
  SM_EXPECT_GT(peak, dsp::DbToLinear(-2.0f));  // Not over-limited.
}

SM_TEST(GainControl, QuietSpeechReachesTarget) {
  AutomaticGainControl agc;
  auto x = sim::SpeechLike(16000, 20.0, 8, false, 0.3f);
  float scale = dsp::DbToLinear(-22.0f);  // Very quiet talker.
  std::vector<float> out;
  float max_step = 0, prev_gain = 0;
  for (size_t i = 0; i + 160 <= x.size(); i += 160) {
    std::vector<float> f(x.begin() + static_cast<std::ptrdiff_t>(i), x.begin() + static_cast<std::ptrdiff_t>(i + 160));
    for (float& v : f) v *= scale;
    agc.Process(f, true);
    max_step = std::max(max_step, std::fabs(agc.gain_db() - prev_gain));
    prev_gain = agc.gain_db();
    if (i >= 10 * 16000) out.insert(out.end(), f.begin(), f.end());
  }
  float level = dsp::RmsDbfs(out);
  SM_RECORD("agc_output_level_dbfs", level);
  SM_RECORD("agc_gain_db", agc.gain_db());
  SM_EXPECT_NEAR(level, -18.0, 3.0);
  SM_EXPECT_LE(max_step, 6.0f / 100.0f + 1e-4f);  // <= 6 dB/s.
}

SM_TEST(GainControl, NoGainOnSilenceAndClippingRecommendation) {
  AutomaticGainControl agc;
  std::vector<float> f(160, 0.0f);
  for (int i = 0; i < 300; ++i) agc.Process(f, false);
  SM_EXPECT_EQ(agc.gain_db(), 0.0f);
  for (int i = 0; i < 300; ++i) {
    std::vector<float> clip(160, 1.0f);  // Fresh each time: Process limits in place.
    agc.Process(clip, true);
  }
  SM_EXPECT_LT(agc.recommended_mic_gain_change_db(), 0.0f);
  SM_EXPECT_GT(agc.stats().clipped_input_samples, 0u);
}

SM_TEST(TransientSuppressor, AttenuatesClicksKeepsTone) {
  TransientSuppressor ts;
  // Background tone at -40 dBFS with a 3 ms click every 300 ms.
  auto x = sim::Sine(200, 16000, 3.0, 0.01f);
  Random rng(9);
  std::vector<float> clicks(x.size(), 0.0f);
  for (size_t c = 4800; c + 48 < x.size(); c += 4800) {
    for (size_t i = 0; i < 48; ++i) clicks[c + i] = static_cast<float>(0.4 * rng.Gaussian() * std::exp(-static_cast<double>(i) / 12));
  }
  std::vector<float> y(x.size());
  for (size_t i = 0; i < x.size(); ++i) y[i] = x[i] + clicks[i];
  double click_in = 0, click_out = 0;
  std::vector<float> orig = y;
  for (size_t i = 0; i + 160 <= y.size(); i += 160) ts.Process(std::span<float>(y.data() + i, 160), false);
  for (size_t i = 0; i < y.size(); ++i) {
    if (clicks[i] != 0) {
      click_in += static_cast<double>(orig[i]) * orig[i];
      click_out += static_cast<double>(y[i]) * y[i];
    }
  }
  double atten = 10 * std::log10(click_in / click_out);
  SM_RECORD("click_attenuation_db", atten);
  SM_EXPECT_GT(atten, 10.0);
  SM_EXPECT_GE(ts.detected(), 8u);
  // Away from clicks the tone is untouched.
  std::span<const float> a(x.data() + 2400, 1600), b(y.data() + 2400, 1600);
  SM_EXPECT_GT(dsp::SnrDb(a, b), 40.0);
}

SM_TEST(Resampler, DownsampleQualityAndAliasRejection) {
  Resampler rs(48000, 16000);
  auto x = sim::Sine(1000, 48000, 1.0, 0.5f);
  std::vector<float> y;
  for (size_t i = 0; i < x.size(); i += 480) rs.Process(std::span<const float>(x.data() + i, 480), y);
  SM_EXPECT_EQ(y.size(), 16000u);
  double d = rs.delay_out_samples();
  std::vector<float> ref(y.size());
  for (size_t n = 0; n < ref.size(); ++n) {
    ref[n] = 0.5f * static_cast<float>(std::sin(2 * std::numbers::pi * 1000 * (static_cast<double>(n) - d) / 16000));
  }
  double snr = dsp::SnrDb(std::span<const float>(ref).subspan(200), std::span<const float>(y).subspan(200));
  SM_RECORD("resampler_48_16_snr_db", snr);
  SM_EXPECT_GT(snr, 60.0);
  // 10 kHz is above the 8 kHz output Nyquist: must be removed.
  Resampler rs2(48000, 16000);
  auto hi = sim::Sine(10000, 48000, 1.0, 0.5f);
  std::vector<float> y2;
  rs2.Process(hi, y2);
  double rej = dsp::LinearToDb(0.5f / std::sqrt(2.0f)) - dsp::RmsDbfs(std::span<const float>(y2).subspan(200));
  SM_RECORD("resampler_alias_rejection_db", rej);
  SM_EXPECT_GT(rej, 60.0);
}

SM_TEST(Resampler, ArbitraryRatiosKeepRate) {
  for (auto [in, out] : {std::pair{44100, 48000}, {48000, 44100}, {16000, 48000}, {8000, 16000}, {32000, 22050}}) {
    Resampler rs(in, out);
    std::vector<float> x(static_cast<size_t>(in), 0.1f), y;
    for (size_t i = 0; i < x.size(); i += static_cast<size_t>(in / 100)) {
      rs.Process(std::span<const float>(x.data() + i, static_cast<size_t>(in / 100)), y);
    }
    SM_EXPECT_NEAR(static_cast<double>(y.size()), out, 1);
    // DC gain is 1 after the filter settles.
    SM_EXPECT_NEAR(y.back(), 0.1, 0.002);
  }
}
