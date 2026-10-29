// Unit tests: VAD, transient suppressor.
#include <algorithm>
#include <cmath>
#include <numbers>

#include "sm/audio/transient_suppressor.h"
#include "sm/audio/vad.h"
#include "sm/core/dsp.h"
#include "sm/sim/signals.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::audio;

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

SM_TEST(TransientSuppressor, AttenuatesClicksKeepsTone) {
  TransientSuppressorConfig tc;
  tc.enabled = true;
  TransientSuppressor ts(tc);
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

SM_TEST(TransientSuppressor, OffByDefaultAsLibwebrtc) {
  SM_EXPECT_FALSE(TransientSuppressorConfig{}.enabled);
  TransientSuppressor ts;
  auto y = sim::Sine(200, 16000, 1.0, 0.01f);
  for (size_t c = 4800; c + 48 < y.size(); c += 4800) {
    for (size_t i = 0; i < 48; ++i) y[c + i] += 0.4f * static_cast<float>(std::exp(-static_cast<double>(i) / 12));
  }
  const std::vector<float> orig = y;
  for (size_t i = 0; i + 160 <= y.size(); i += 160) ts.Process(std::span<float>(y.data() + i, 160), false);
  SM_EXPECT_TRUE(y == orig);
  SM_EXPECT_EQ(ts.detected(), 0u);
}
