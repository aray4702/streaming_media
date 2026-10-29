// Section 6 end-to-end tests: realistic capture scenarios through the APM
// chain (HPF -> AEC -> NS -> VAD -> TS -> AGC), plus
// conference level/active-speaker, mixing and rate conversion.
#include <cmath>

#include "audio_sims.h"
#include "sm/audio/audio_level.h"
#include "sm/audio/resampler.h"
#include "sm/sim/network_link.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::e2e;
using namespace sm::audio;

namespace {
// Amplitude of a sinusoid at `freq` in x (single-bin DFT over the span).
double ToneAmp(std::span<const float> x, double freq, int fs) {
  double re = 0, im = 0;
  for (size_t i = 0; i < x.size(); ++i) {
    double a = 2 * std::numbers::pi * freq * static_cast<double>(i) / fs;
    re += x[i] * std::cos(a);
    im += x[i] * std::sin(a);
  }
  return 2 * std::sqrt(re * re + im * im) / static_cast<double>(x.size());
}
// The v1 chain: these tests pin the v1 modules, which stay until they are
// retired (#565); the default chain is `apm v2`, libwebrtc's.
ChainConfig V1(bool aec, bool ns, bool ts, bool agc) {
  ChainConfig cc{true, aec, ns, ts, agc};
  cc.apm_v2 = false;
  return cc;
}
double Mean(std::span<const float> x) {
  double s = 0;
  for (float v : x) s += v;
  return s / static_cast<double>(x.size());
}
}  // namespace

SM_TEST(E2E_HighPassFilter, RemovesDcAndHumInFullChain) {
  CallScenario s;
  s.far_talk = false;
  s.dc = 0.1f;
  s.hum_amp = 0.05f;
  s.noise_amp = 0.002f;
  auto c = MakeCall(s);
  auto r = RunChain(c, V1(false, true, false, true));
  std::span<const float> tail(r.out.data() + 10 * 16000, 8 * 16000);
  double hum_in = ToneAmp(std::span<const float>(c.capture.data() + 10 * 16000, 8 * 16000), 50, 16000);
  double hum_out = ToneAmp(tail, 50, 16000);
  SM_RECORD("hum_attenuation_db", 20 * std::log10(hum_in / hum_out));
  SM_RECORD("dc_out", Mean(tail));
  SM_EXPECT_LT(std::fabs(Mean(tail)), 1e-3);
  SM_EXPECT_GT(20 * std::log10(hum_in / hum_out), 15.0);
}

// `apm v2` (#239): libwebrtc's high-pass filter on the case sm_apm_compare
// measures (the filter alone, so the hum left is the filter's). libwebrtc
// M157's APM (docs/results/apm-webrtc.md): hum attenuation 67.2 dB, DC left
// 0.0 (x1000), speech level change -0.2 dB (v1: 16.4, 0.0, -0.0).
SM_TEST(E2E_HighPassFilter, LibwebrtcHpfAsLibwebrtc) {
  CallScenario s;
  s.far_talk = false;
  s.dc = 0.1f;
  s.hum_amp = 0.05f;
  s.noise_amp = 0.002f;
  auto c = MakeCall(s);
  ChainConfig cc{true, false, false, false, false};
  cc.apm_v2 = true;
  auto r = RunChain(c, cc);
  std::span<const float> tail(r.out.data() + 10 * 16000, 8 * 16000);
  double hum_in = ToneAmp(std::span<const float>(c.capture.data() + 10 * 16000, 8 * 16000), 50, 16000);
  double hum_db = 20 * std::log10(hum_in / ToneAmp(tail, 50, 16000));
  // Pass band: the clean talker alone through the filter.
  CallScenario t;
  t.far_talk = false;
  auto ct = MakeCall(t);
  auto rt = RunChain(ct, cc);
  double speech_db = SegmentDb(rt.out, 3, 20) - SegmentDb(ct.capture, 3, 20);
  SM_RECORD("hum_attenuation_db", hum_db);
  SM_RECORD("dc_out", Mean(tail));
  SM_RECORD("speech_level_change_db", speech_db);
  SM_EXPECT_NEAR(hum_db, 67.2, 1.0);
  SM_EXPECT_LT(std::fabs(Mean(tail)), 1e-3);
  SM_EXPECT_NEAR(speech_db, -0.2, 0.1);
}

// The default capture chain is libwebrtc's (#565): the full chain with
// every module on runs the ported modules, bit for bit the `apm v2` chain.
SM_TEST(E2E_Apm, DefaultChainIsLibwebrtcs) {
  SM_EXPECT_TRUE(ChainConfig{}.apm_v2);
  CallScenario s;
  s.seconds = 6;
  s.near_start_s = 3;
  s.noise_amp = 0.003f;
  s.hum_amp = 0.02f;
  auto c = MakeCall(s);
  auto def = RunChain(c, {true, true, true, false, true});
  ChainConfig v2{true, true, true, false, true};
  v2.apm_v2 = true;
  auto ported = RunChain(c, v2);
  SM_EXPECT_TRUE(def.out == ported.out);
  SM_EXPECT_EQ(def.latency, ported.latency);
  SM_EXPECT_EQ(def.latency, 64u + 96u);  // AEC3's block plus NS's filter bank, as libwebrtc.
  SM_EXPECT_TRUE(def.out != RunChain(c, V1(true, true, false, true)).out);
}

SM_TEST(E2E_Vad, DetectsSpeechAfterNoiseSuppression) {
  CallScenario s;
  s.far_talk = false;
  s.noise_amp = 0.015f;  // ~10 dB SNR.
  auto c = MakeCall(s);
  auto r = RunChain(c, V1(false, true, false, false));
  int tp = 0, fn = 0, fp = 0, tn = 0, since = 1000;
  for (size_t f = 100; f < r.vad.size(); ++f) {
    bool truth = dsp::RmsDbfs(std::span<const float>(c.talker.data() + f * 160, 160)) > -45.0f;
    since = truth ? 0 : since + 1;
    if (truth) {
      (r.vad[f] ? tp : fn)++;
    } else if (since > 25) {
      (r.vad[f] ? fp : tn)++;
    }
  }
  double recall = static_cast<double>(tp) / (tp + fn), fpr = static_cast<double>(fp) / std::max(1, fp + tn);
  SM_RECORD("vad_recall", recall);
  SM_RECORD("vad_fpr", fpr);
  SM_EXPECT_GT(recall, 0.93);
  SM_EXPECT_LT(fpr, 0.10);
}

SM_TEST(E2E_NoiseSuppressor, ImprovesSnrInChain) {
  CallScenario s;
  s.far_talk = false;
  s.noise_amp = 0.03f;  // ~7 dB SNR: a noisy office or car.
  auto c = MakeCall(s);
  auto r = RunChain(c, V1(false, true, false, false));
  auto base = RunChain(c, V1(false, false, false, false));
  // Reference: the clean talker through the same high-pass filter, so the
  // HPF's phase response is not counted as noise.
  std::vector<float> talker_hp = c.talker;
  HighPassFilter hp_ref;
  for (size_t i = 0; i + 160 <= talker_hp.size(); i += 160) hp_ref.Process(std::span<float>(talker_hp.data() + i, 160));
  size_t lat = r.latency, start = 3 * 16000, n = c.talker.size() - start - lat;
  std::span<const float> ref(talker_hp.data() + start, n);
  double snr_in = dsp::SnrDb(ref, std::span<const float>(base.out.data() + start, n));
  double snr_out = dsp::SnrDb(ref, std::span<const float>(r.out.data() + start + lat, n));
  SM_RECORD("chain_snr_in_db", snr_in);
  SM_RECORD("chain_snr_out_db", snr_out);
  SM_EXPECT_GT(snr_out, snr_in + 4.0);
}

// `apm v2` (#241): libwebrtc's NS (kHigh) on the same signals, measured as
// sm_apm_compare measures both stacks. libwebrtc M157's APM
// (docs/results/apm-webrtc.md): SNR improvement -0.1 dB, noise attenuation
// in pauses 17.9 dB, level change in speech -2.9 dB, delay 6.0 ms (v1:
// +5.3, 16.9, -1.3, 10.0).
SM_TEST(E2E_NoiseSuppressor, LibwebrtcNsAsLibwebrtc) {
  CallScenario s;
  s.far_talk = false;
  s.noise_amp = 0.03f;
  auto c = MakeCall(s);
  ChainConfig cc{true, false, true, false, false};
  cc.apm_v2 = true;
  auto r = RunChain(c, cc);
  // The input and the reference go through the same (`apm v2`, #239)
  // high-pass filter as the output, as sm_apm_compare runs each stack.
  ChainConfig hpf_only{true, false, false, false, false};
  hpf_only.apm_v2 = true;
  auto base = RunChain(c, hpf_only);
  CallScenario t = s;
  t.noise_amp = 0;
  auto ref = RunChain(MakeCall(t), hpf_only).out;
  // SNR against the clean talker through the same HPF, at the output delay
  // (0..40 ms) that gives the best SNR.
  const size_t start = 3 * 16000, n = ref.size() - start - 640;
  auto best_lag_snr = [&](const std::vector<float>& out, size_t* lag) {
    double best = -1e9;
    for (size_t l = 0; l <= 640; ++l) {
      double snr = dsp::SnrDb({ref.data() + start, n}, {out.data() + start + l, n});
      if (snr > best) {
        best = snr;
        *lag = l;
      }
    }
    return best;
  };
  size_t lag_in = 0, lag = 0;
  const double snr_gain = best_lag_snr(r.out, &lag) - best_lag_snr(base.out, &lag_in);
  // Speech frames by the clean talker's level; pauses are frames more than
  // 250 ms after it stopped.
  std::vector<int> truth;  // 1 speech, 0 pause, -1 not scored.
  for (size_t f = 0, since = 1000; (f + 1) * 160 <= c.talker.size(); ++f) {
    bool speech = dsp::RmsDbfs(std::span<const float>(c.talker.data() + f * 160, 160)) > -45.0f;
    since = speech ? 0 : since + 1;
    truth.push_back(speech ? 1 : since > 25 ? 0 : -1);
  }
  auto frames_db = [&](const std::vector<float>& x, int want, size_t shift) {
    double e = 0;
    size_t cnt = 0;
    for (size_t f = 300; f < 2000 && f < truth.size(); ++f) {
      if (truth[f] != want) continue;
      for (size_t i = f * 160 + shift; i < f * 160 + 160 + shift && i < x.size(); ++i, ++cnt) {
        e += static_cast<double>(x[i]) * x[i];
      }
    }
    return 10 * std::log10(e / static_cast<double>(std::max<size_t>(1, cnt)) + 1e-20);
  };
  const double atten = frames_db(base.out, 0, 0) - frames_db(r.out, 0, lag);
  const double speech_change = frames_db(r.out, 1, lag) - frames_db(base.out, 1, 0);
  SM_RECORD("snr_improvement_db", snr_gain);
  SM_RECORD("noise_attenuation_db", atten);
  SM_RECORD("speech_level_change_db", speech_change);
  SM_RECORD("delay_ms", lag / 16.0);
  SM_EXPECT_EQ(r.latency, 96u);
  SM_EXPECT_EQ(lag, 96u);
  SM_EXPECT_NEAR(snr_gain, -0.1, 0.5);
  SM_EXPECT_NEAR(atten, 17.9, 0.5);
  SM_EXPECT_NEAR(speech_change, -2.9, 0.5);
}

SM_TEST(E2E_GainControl, NormalizesQuietTalkerLeavesLoudToLimiter) {
  // Quiet talker (~-33 dBFS) for 20 s, then a loud one (+20 dB) for 20 s.
  CallScenario q;
  q.far_talk = false;
  q.seconds = 40;
  q.near_gain_db = -10;
  q.noise_amp = 0.0002f;  // -74 dBFS room noise.
  auto c = MakeCall(q);
  for (size_t i = c.capture.size() / 2; i < c.capture.size(); ++i) c.capture[i] *= dsp::DbToLinear(20);
  auto in_level = [&](double a, double b) { return SegmentDb(c.capture, a, b); };
  ChainResult r;
  {
    // Run the chain manually to read the AGC gain at the end of each half.
    r = RunChain(c, V1(false, false, false, true));
  }
  auto level = [&](double a, double b) {
    double e = 0;
    size_t n = 0;
    for (size_t f = static_cast<size_t>(a * 100); f < static_cast<size_t>(b * 100) && f < r.vad.size(); ++f) {
      if (!r.vad[f]) continue;
      for (size_t i = f * 160; i < f * 160 + 160; ++i) e += static_cast<double>(r.out[i]) * r.out[i];
      n += 160;
    }
    return 10 * std::log10(e / std::max<size_t>(1, n) + 1e-20);
  };
  float peak = 0;
  for (float v : r.out) peak = std::max(peak, std::fabs(v));
  SM_RECORD("quiet_talker_in_dbfs", in_level(12, 20));
  SM_RECORD("quiet_talker_out_dbfs", level(12, 20));
  SM_RECORD("loud_talker_in_dbfs", in_level(32, 40));
  SM_RECORD("loud_talker_out_dbfs", level(32, 40));
  SM_RECORD("peak", peak);
  // Quiet speech is lifted to the -18 dBFS target.
  SM_EXPECT_NEAR(level(12, 20), -18.0, 4.0);
  // The digital AGC never attenuates: after the jump its gain returns to
  // ~0 dB (6 dB/s), so loud speech is not amplified; only the limiter
  // trims its peaks (a few dB at most) to keep them under -1 dBFS.
  double loud_change = level(32, 40) - in_level(32, 40);
  SM_EXPECT_LE(loud_change, 1.0);
  SM_EXPECT_GE(loud_change, -4.0);
  SM_EXPECT_LE(peak, dsp::DbToLinear(-1.0f) + 1e-6f);
}

namespace {

struct QuietThenLoud {
  double quiet_in, quiet_out, loud_change, noise_out, peak_dbfs;
};

// The quiet-then-loud talker through the `apm v2` chain with AGC1 (the call
// default) or AGC2, measured as sm_apm_compare measures both stacks.
QuietThenLoud RunQuietThenLoud(bool agc2) {
  CallScenario q;
  q.far_talk = false;
  q.seconds = 40;
  q.near_gain_db = -10;
  q.noise_amp = 0.0002f;
  auto c = MakeCall(q);
  for (size_t i = c.capture.size() / 2; i < c.capture.size(); ++i) c.capture[i] *= dsp::DbToLinear(20);
  ChainConfig cc{true, false, false, false, true};
  cc.apm_v2 = true;
  cc.agc2 = agc2;
  auto r = RunChain(c, cc);
  // Speech frames by the clean talker's level (as sm_apm_compare's truth),
  // so both stacks are measured on the same frames.
  auto talker_level = [&](size_t f) {
    return dsp::RmsDbfs(std::span<const float>(c.talker.data() + f * 160, 160));
  };
  auto level = [&](const std::vector<float>& x, double a, double b, bool speech) {
    double e = 0;
    size_t n = 0;
    for (size_t f = static_cast<size_t>(a * 100); f < static_cast<size_t>(b * 100); ++f) {
      const bool is_speech = talker_level(f) > -60.0f;
      if (is_speech != speech) continue;
      if (!speech && (f < 25 || talker_level(f - 25) > -60.0f)) continue;  // Pauses only, not word tails.
      for (size_t i = f * 160; i < f * 160 + 160; ++i) e += static_cast<double>(x[i]) * x[i];
      n += 160;
    }
    return 10 * std::log10(e / static_cast<double>(std::max<size_t>(1, n)) + 1e-20);
  };
  float peak = 0;
  for (float v : r.out) peak = std::max(peak, std::fabs(v));
  return {level(c.capture, 12, 20, true), level(r.out, 12, 20, true),
          level(r.out, 32, 40, true) - level(c.capture, 32, 40, true), level(r.out, 12, 20, false),
          dsp::LinearToDb(peak)};
}

}  // namespace

// `apm v2` (#242): AGC2 as libwebrtc M157 on the same signals. libwebrtc's
// AGC2 adaptive digital at its defaults (docs/results/apm-webrtc.md, from
// sm_apm_compare): quiet talker -21.0 dBFS out, loud talker -1.0 dB,
// noise in pauses -62.9 dBFS, peak 0.0 dBFS.
SM_TEST(E2E_GainControl, Agc2LiftsQuietTalkerAsLibwebrtc) {
  const QuietThenLoud m = RunQuietThenLoud(/*agc2=*/true);
  SM_RECORD("quiet_talker_in_dbfs", m.quiet_in);
  SM_RECORD("quiet_talker_out_dbfs", m.quiet_out);
  SM_RECORD("loud_talker_change_db", m.loud_change);
  SM_RECORD("noise_in_pauses_dbfs", m.noise_out);
  SM_RECORD("peak_dbfs", m.peak_dbfs);
  // libwebrtc AGC2: -21.0 dBFS, -1.0 dB, -62.9 dBFS (v1 lifts the quiet
  // talker to -16.8 dBFS and the noise to -58.2 dBFS, #242).
  SM_EXPECT_NEAR(m.quiet_out, -21.0, 1.0);
  SM_EXPECT_NEAR(m.loud_change, -1.0, 1.0);
  SM_EXPECT_NEAR(m.noise_out, -62.9, 1.5);
  SM_EXPECT_LE(m.peak_dbfs, 0.0);
}

// `apm v2` (#242): AGC1 as a call runs it (AgcManagerDirect in
// kAdaptiveAnalog, GainControl in kFixedDigital, the mic volume emulated
// from 255). libwebrtc M157's APM with the same configuration
// (docs/results/apm-webrtc.md, sm_apm_compare): quiet talker -20.6 dBFS out,
// loud talker -3.2 dB, noise in pauses -63.0 dBFS, peak 0.0 dBFS.
SM_TEST(E2E_GainControl, Agc1LiftsQuietTalkerAsLibwebrtc) {
  const QuietThenLoud m = RunQuietThenLoud(/*agc2=*/false);
  SM_RECORD("quiet_talker_in_dbfs", m.quiet_in);
  SM_RECORD("quiet_talker_out_dbfs", m.quiet_out);
  SM_RECORD("loud_talker_change_db", m.loud_change);
  SM_RECORD("noise_in_pauses_dbfs", m.noise_out);
  SM_RECORD("peak_dbfs", m.peak_dbfs);
  SM_EXPECT_NEAR(m.quiet_out, -20.6, 1.0);
  SM_EXPECT_NEAR(m.loud_change, -3.2, 1.0);
  SM_EXPECT_NEAR(m.noise_out, -63.0, 1.5);
  SM_EXPECT_LE(m.peak_dbfs, 0.0);
}

SM_TEST(E2E_EchoDelay, AecFollowsAudioRouteChange) {
  CallScenario s;
  s.seconds = 24;
  s.echo_delay_ms = 60;
  s.echo_delay2_ms = 180;  // Speaker -> Bluetooth at 12 s.
  s.near_start_s = 1e9;    // Far end only.
  s.noise_amp = 0.0005f;
  auto c = MakeCall(s);
  auto r = RunChain(c, V1(true, false, false, false));
  double erle_before = SegmentDb(c.echo, 6, 12) - SegmentDb(r.out, 6, 12, 16000, r.latency);
  double erle_after = SegmentDb(c.echo, 18, 24) - SegmentDb(r.out, 18, 24, 16000, r.latency);
  SM_RECORD("erle_before_change_db", erle_before);
  SM_RECORD("erle_after_change_db", erle_after);
  SM_RECORD("final_bulk_delay_ms", r.aec.delay_samples / 16.0);
  SM_EXPECT_GT(erle_before, 20.0);
  SM_EXPECT_GT(erle_after, 20.0);
  SM_EXPECT_GE(r.aec.delay_changes, 2u);
  SM_EXPECT_NEAR(r.aec.delay_samples / 16.0, 180 - 10, 3);  // Estimate minus 2-block margin.
}

SM_TEST(E2E_EchoCanceller, FullDuplexCall) {
  CallScenario s;
  s.seconds = 30;
  s.near_start_s = 15;  // Far end alone first, then double talk.
  s.noise_amp = 0.0005f;
  auto c = MakeCall(s);
  auto r = RunChain(c, V1(true, true, false, false));
  double erle = SegmentDb(c.echo, 6, 15) - SegmentDb(r.out, 6, 15, 16000, r.latency);
  double near_change = SegmentDb(r.out, 18, 30, 16000, r.latency) - SegmentDb(c.talker, 18, 30);
  SM_RECORD("call_erle_db", erle);
  SM_RECORD("call_double_talk_near_level_change_db", near_change);
  SM_EXPECT_GT(erle, 25.0);
  SM_EXPECT_GT(near_change, -6.0);
  SM_EXPECT_LT(near_change, 3.0);
}

// `apm v2` (#240): libwebrtc's AEC3. libwebrtc M157's APM on the same call
// (docs/results/apm-webrtc.md, sm_apm_compare): ERLE 33.2 dB at 1-3 s, 50.1
// at 6-12 s, 8.0 just after the delay change, 49.0 at 18-24 s.
SM_TEST(E2E_EchoDelay, Aec3FollowsAudioRouteChangeAsLibwebrtc) {
  CallScenario s;
  s.seconds = 24;
  s.echo_delay_ms = 60;
  s.echo_delay2_ms = 180;
  s.near_start_s = 1e9;
  s.noise_amp = 0.0005f;
  auto c = MakeCall(s);
  ChainConfig cc{true, true, false, false, false};
  cc.apm_v2 = true;
  auto r = RunChain(c, cc);
  auto erle = [&](double a, double b) { return SegmentDb(c.echo, a, b) - SegmentDb(r.out, a, b, 16000, r.latency); };
  SM_RECORD("erle_1_3_db", erle(1, 3));
  SM_RECORD("erle_6_12_db", erle(6, 12));
  SM_RECORD("erle_12_14_db", erle(12, 14));
  SM_RECORD("erle_18_24_db", erle(18, 24));
  SM_EXPECT_EQ(r.latency, 64u);
  SM_EXPECT_NEAR(erle(1, 3), 33.2, 1.0);
  SM_EXPECT_NEAR(erle(6, 12), 50.1, 1.0);
  SM_EXPECT_NEAR(erle(12, 14), 8.0, 1.0);
  SM_EXPECT_NEAR(erle(18, 24), 49.0, 1.0);
}

// `apm v2` (#240, #241): AEC3 then libwebrtc's NS. libwebrtc M157's APM:
// ERLE 67.0 dB with the far end alone, the near end 5.3 dB quieter in
// double talk, near-end SNR -0.2 dB there.
SM_TEST(E2E_EchoCanceller, Aec3FullDuplexCallAsLibwebrtc) {
  CallScenario s;
  s.seconds = 30;
  s.near_start_s = 15;
  s.noise_amp = 0.0005f;
  auto c = MakeCall(s);
  ChainConfig cc{true, true, true, false, false};
  cc.apm_v2 = true;
  auto r = RunChain(c, cc);
  double erle = SegmentDb(c.echo, 6, 15) - SegmentDb(r.out, 6, 15, 16000, r.latency);
  double near_change = SegmentDb(r.out, 18, 30, 16000, r.latency) - SegmentDb(c.talker, 18, 30);
  // The talker against everything else in the output from 18 s, at the
  // output delay (0..40 ms) with the best SNR, as sm_apm_compare.
  const size_t start = 18 * 16000, n = 30 * 16000 - start - 640;
  double snr = -1e9;
  for (size_t l = 0; l <= 640; ++l) {
    snr = std::max(snr, dsp::SnrDb({c.talker.data() + start, n}, {r.out.data() + start + l, n}));
  }
  SM_RECORD("call_erle_db", erle);
  SM_RECORD("call_double_talk_near_level_change_db", near_change);
  SM_RECORD("call_double_talk_near_snr_db", snr);
  SM_EXPECT_NEAR(erle, 67.0, 1.0);
  SM_EXPECT_NEAR(near_change, -5.3, 1.0);
  SM_EXPECT_NEAR(snr, -0.2, 1.0);
}

SM_TEST(E2E_TransientSuppressor, KeyboardDuringCall) {
  CallScenario s;
  s.far_talk = false;
  s.click_interval_s = 0.25;
  s.noise_amp = 0.001f;
  auto c = MakeCall(s);
  auto with = RunChain(c, V1(false, false, true, false));
  auto without = RunChain(c, V1(false, false, false, false));
  double ein = 0, eout = 0;
  for (size_t i = 16000; i < c.clicks.size(); ++i) {
    if (c.clicks[i] == 0 || std::fabs(c.talker[i]) > 1e-4f) continue;  // Clicks in pauses.
    ein += static_cast<double>(without.out[i]) * without.out[i];
    eout += static_cast<double>(with.out[i]) * with.out[i];
  }
  double atten = 10 * std::log10(ein / eout);
  SM_RECORD("click_attenuation_in_pauses_db", atten);
  SM_EXPECT_GT(atten, 10.0);
}

SM_TEST(E2E_AudioLevel, ExtensionByteTracksSpeechAndSilence) {
  // The sender writes the RFC 6464 byte per 20 ms packet; the receiver reads
  // only that byte. Speech must read louder than the room, and the voice flag
  // must survive the round trip.
  const int fs = 16000;
  auto speech = sim::SpeechLike(fs, 6.0, 10, false, 0.25f);
  auto noise = sim::WhiteNoise(fs, 6.0, 0.002f, 20);
  Vad vad;
  double speech_level = 0, noise_level = 0;
  int speech_n = 0, noise_n = 0, voiced_speech = 0, voiced_noise = 0;
  for (size_t f = 0; f + 1 < speech.size() / 320; ++f) {
    std::span<const float> fr(speech.data() + f * 320, 320);
    std::span<const float> nz(noise.data() + f * 320, 320);
    bool v = vad.Process(fr.first(160)) | vad.Process(fr.last(160));
    auto rx = DecodeAudioLevelExtension(EncodeAudioLevelExtension({ComputeLevel(fr), v}));
    if (f * 20 >= 1500 && rx.level < 60) {
      speech_level += rx.level;
      ++speech_n;
      voiced_speech += rx.voice;
    }
    auto rn = DecodeAudioLevelExtension(EncodeAudioLevelExtension({ComputeLevel(nz), false}));
    noise_level += rn.level;
    ++noise_n;
    voiced_noise += rn.voice;
  }
  SM_RECORD("speech_level_dbov", speech_level / std::max(speech_n, 1));
  SM_RECORD("noise_level_dbov", noise_level / noise_n);
  SM_ASSERT_GT(speech_n, 0);
  SM_EXPECT_LT(speech_level / speech_n, noise_level / noise_n - 10.0);
  SM_EXPECT_GT(voiced_speech, speech_n / 2);
  SM_EXPECT_EQ(voiced_noise, 0);
}

SM_TEST(E2E_Resampler, DeviceToProcessingToPlayout) {
  // 44.1 kHz microphone -> 16 kHz processing (HPF) -> 48 kHz speaker.
  auto mic = sim::Sine(1000, 44100, 3.0, 0.4f);
  Resampler down(44100, 16000), up(16000, 48000);
  HighPassFilter hpf(16000);
  std::vector<float> mid, out;
  for (size_t i = 0; i + 441 <= mic.size(); i += 441) {
    std::vector<float> m;
    down.Process(std::span<const float>(mic.data() + i, 441), m);
    hpf.Process(m);
    mid.insert(mid.end(), m.begin(), m.end());
    up.Process(m, out);
  }
  SM_EXPECT_NEAR(static_cast<double>(out.size()), 3.0 * 48000, 2);
  // Compare with an ideal 1 kHz tone at 48 kHz, fitting amplitude/phase
  // (the chain adds delay and a tiny HPF phase shift).
  std::span<const float> o(out.data() + 48000, 96000);
  double re = 0, im = 0;
  for (size_t i = 0; i < o.size(); ++i) {
    double a = 2 * std::numbers::pi * 1000 * static_cast<double>(i) / 48000;
    re += o[i] * std::cos(a);
    im += o[i] * std::sin(a);
  }
  re *= 2.0 / static_cast<double>(o.size());
  im *= 2.0 / static_cast<double>(o.size());
  std::vector<float> fit(o.size());
  for (size_t i = 0; i < o.size(); ++i) {
    double a = 2 * std::numbers::pi * 1000 * static_cast<double>(i) / 48000;
    fit[i] = static_cast<float>(re * std::cos(a) + im * std::sin(a));
  }
  double snr = dsp::SnrDb(fit, o);
  SM_RECORD("chain_44k_16k_48k_snr_db", snr);
  SM_EXPECT_GT(snr, 50.0);
  SM_EXPECT_NEAR(std::sqrt(re * re + im * im), 0.4, 0.01);
}
