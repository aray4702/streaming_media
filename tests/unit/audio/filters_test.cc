// Unit tests: VAD (libwebrtc's `Vad`, #671), transient suppressor.
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

// libwebrtc's `webrtc::Vad` (common_audio/vad/vad.cc, #671): a decision per
// 10, 20 or 30 ms frame at 8, 16 or 32 kHz, kError otherwise (as
// vad_unittest.cc's ApiTest on `WebRtcVad_Process`, which `Vad` wraps).
SM_TEST(Vad, ApiAsLibwebrtc) {
  constexpr size_t kMaxFrameLength = 1440;
  int16_t zeros[kMaxFrameLength] = {0};
  int16_t speech[kMaxFrameLength];
  for (size_t i = 0; i < kMaxFrameLength; i++) speech[i] = static_cast<int16_t>(i * i);
  const int kRates[] = {8000, 12000, 16000, 24000, 32000};
  const size_t kFrameLengths[] = {80, 120, 160, 240, 320, 480, 640, 960, kMaxFrameLength};
  auto valid = [](int rate, size_t len) {
    return (rate == 8000 || rate == 16000 || rate == 32000) && (len * 100 == static_cast<size_t>(rate) ||
                                                                len * 50 == static_cast<size_t>(rate) ||
                                                                len * 100 == static_cast<size_t>(rate) * 3);
  };
  SM_EXPECT_EQ(Vad().aggressiveness(), Vad::kVadNormal);  // As apm_compare's CreateVad(kVadNormal).
  for (int mode = 0; mode < 4; ++mode) {
    Vad vad(static_cast<Vad::Aggressiveness>(mode));
    SM_EXPECT_EQ(vad.VoiceActivity(nullptr, 160, 16000), Vad::kError);
    SM_EXPECT_EQ(vad.VoiceActivity(speech, 160, 9999), Vad::kError);
    SM_EXPECT_EQ(vad.VoiceActivity(zeros, 160, 16000), Vad::kPassive);
    for (int rate : kRates) {
      for (size_t len : kFrameLengths) {
        SM_EXPECT_EQ(vad.VoiceActivity(speech, len, rate), valid(rate, len) ? Vad::kActive : Vad::kError);
      }
    }
    SM_EXPECT_EQ(vad.VoiceActivity(speech, 480, 48000), Vad::kError);  // 48 kHz not ported (see the header).
  }
}

// `Vad` is `WebRtcVad` in the chosen mode (VadImpl), and Reset() starts it
// again from the initial state (VadImpl::Reset: free, create, init, mode).
SM_TEST(Vad, IsWebRtcVadAndResets) {
  const int fs = 16000;
  auto speech = sim::SpeechLike(fs, 4.0, 3, true);
  auto noise = sim::WhiteNoise(fs, 4.0, 0.01f, 4);
  std::vector<int16_t> pcm(speech.size());
  for (size_t i = 0; i < pcm.size(); ++i) {
    pcm[i] = static_cast<int16_t>(std::clamp(std::lround((speech[i] + noise[i]) * 32768.0f), -32768L, 32767L));
  }
  for (int mode = 0; mode < 4; ++mode) {
    Vad vad(static_cast<Vad::Aggressiveness>(mode));
    agc1::detail::WebRtcVad core;
    SM_ASSERT_EQ(core.set_mode(mode), 0);
    std::vector<int> first;
    int mismatches = 0;
    for (size_t i = 0; i + 160 <= pcm.size(); i += 160) {
      const int d = vad.VoiceActivity(pcm.data() + i, 160, fs);
      mismatches += d != core.Process(fs, pcm.data() + i, 160);
      first.push_back(d);
    }
    SM_EXPECT_EQ(mismatches, 0);
    SM_EXPECT_GT(vad.stats().speech_frames, 0u);
    vad.Reset();
    for (size_t i = 0, k = 0; i + 160 <= pcm.size(); i += 160, ++k) {
      mismatches += vad.VoiceActivity(pcm.data() + i, 160, fs) != first[k];
    }
    SM_EXPECT_EQ(mismatches, 0);
  }
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
    bool d = vad.VoiceActivity(f, fs) == Vad::kActive;
    if (i < 16000) continue;  // Let the noise floor settle.
    if (truth) {
      (d ? tp : fn)++;
    } else if (since_speech > 22) {  // Beyond a 200 ms hangover.
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

// A +20 dB step to -26 dBFS white noise. libwebrtc's GMM caps its noise
// means (`kMaximumNoise`, vad_core.c:33 and 460), so in kVadNormal such loud
// stationary noise stays above the noise model and reads as speech for the
// whole 3 s, as libwebrtc (the port is bit-exact,
// `Agc1VoiceActivity.WebRtcVadMatchesLibwebrtc`). The retired v1 detector
// learned the new floor (< 75 frames); that was its own behaviour (#671).
// kVadVeryAggressive's higher thresholds pass a few frames.
SM_TEST(Vad, NoiseLevelStepAsLibwebrtc) {
  auto quiet = sim::WhiteNoise(16000, 3.0, 0.005f, 1);
  auto loud = sim::WhiteNoise(16000, 3.0, 0.05f, 2);  // +20 dB, still noise.
  int speech_frames[4] = {};
  for (int mode = 0; mode < 4; ++mode) {
    Vad vad(static_cast<Vad::Aggressiveness>(mode));
    for (size_t i = 0; i + 160 <= quiet.size(); i += 160) {
      vad.VoiceActivity(std::span<const float>(quiet.data() + i, 160), 16000);
    }
    for (size_t i = 0; i + 160 <= loud.size(); i += 160) {
      speech_frames[mode] += vad.VoiceActivity(std::span<const float>(loud.data() + i, 160), 16000) == Vad::kActive;
    }
  }
  SM_RECORD("vad_frames_on_noise_step", speech_frames[0]);
  SM_EXPECT_EQ(speech_frames[0], 300);
  SM_EXPECT_EQ(speech_frames[1], 300);
  SM_EXPECT_EQ(speech_frames[2], 282);
  SM_EXPECT_EQ(speech_frames[3], 281);
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
