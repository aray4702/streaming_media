// Section 6 end-to-end tests: realistic capture scenarios through the APM
// chain (HPF -> AEC -> NS -> VAD -> TS -> AGC), plus DTX/CNG over a lossy
// link, conference level/active-speaker, mixing and rate conversion.
#include <cmath>
#include <map>

#include "audio_sims.h"
#include "sm/audio/audio_level.h"
#include "sm/audio/mixer.h"
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
  auto r = RunChain(c, {true, false, true, false, true});
  std::span<const float> tail(r.out.data() + 10 * 16000, 8 * 16000);
  double hum_in = ToneAmp(std::span<const float>(c.capture.data() + 10 * 16000, 8 * 16000), 50, 16000);
  double hum_out = ToneAmp(tail, 50, 16000);
  SM_RECORD("hum_attenuation_db", 20 * std::log10(hum_in / hum_out));
  SM_RECORD("dc_out", Mean(tail));
  SM_EXPECT_LT(std::fabs(Mean(tail)), 1e-3);
  SM_EXPECT_GT(20 * std::log10(hum_in / hum_out), 15.0);
}

SM_TEST(E2E_Vad, DetectsSpeechAfterNoiseSuppression) {
  CallScenario s;
  s.far_talk = false;
  s.noise_amp = 0.015f;  // ~10 dB SNR.
  auto c = MakeCall(s);
  auto r = RunChain(c, {true, false, true, false, false});
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
  auto r = RunChain(c, {true, false, true, false, false});
  auto base = RunChain(c, {true, false, false, false, false});
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
    r = RunChain(c, {true, false, false, false, true});
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

SM_TEST(E2E_EchoDelay, AecFollowsAudioRouteChange) {
  CallScenario s;
  s.seconds = 24;
  s.echo_delay_ms = 60;
  s.echo_delay2_ms = 180;  // Speaker -> Bluetooth at 12 s.
  s.near_start_s = 1e9;    // Far end only.
  s.noise_amp = 0.0005f;
  auto c = MakeCall(s);
  auto r = RunChain(c, {true, true, false, false, false});
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
  auto r = RunChain(c, {true, true, true, false, false});
  double erle = SegmentDb(c.echo, 6, 15) - SegmentDb(r.out, 6, 15, 16000, r.latency);
  double near_change = SegmentDb(r.out, 18, 30, 16000, r.latency) - SegmentDb(c.talker, 18, 30);
  SM_RECORD("call_erle_db", erle);
  SM_RECORD("call_double_talk_near_level_change_db", near_change);
  SM_EXPECT_GT(erle, 25.0);
  SM_EXPECT_GT(near_change, -6.0);
  SM_EXPECT_LT(near_change, 3.0);
}

SM_TEST(E2E_TransientSuppressor, KeyboardDuringCall) {
  CallScenario s;
  s.far_talk = false;
  s.click_interval_s = 0.25;
  s.noise_amp = 0.001f;
  auto c = MakeCall(s);
  auto with = RunChain(c, {true, false, false, true, false});
  auto without = RunChain(c, {true, false, false, false, false});
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

SM_TEST(E2E_ComfortNoise, DtxSavesPacketsCngFillsSilence) {
  CallScenario s;
  s.far_talk = false;
  s.noise_amp = 0.003f;
  s.seconds = 30;
  s.near_end_s = 15;  // Talks, then listens: a one-sided stretch of a call.
  auto c = MakeCall(s);
  // Sender: VAD + DTX. Receiver: plays speech frames, CNG otherwise.
  Vad vad;
  DtxController dtx;
  ComfortNoiseGenerator cng;
  sim::LinkConfig lc;
  lc.loss_rate = 0.02;
  sim::NetworkLink<std::pair<size_t, std::vector<uint8_t>>> link(lc, 3);
  size_t frames = c.capture.size() / 160, sent = 0;
  for (size_t f = 0; f < frames; ++f) {
    std::span<const float> fr(c.capture.data() + f * 160, 160);
    SidFrame sid;
    auto d = dtx.Process(fr, vad.Process(fr), &sid);
    if (d == DtxDecision::kSendSpeech) {
      link.Send(Timestamp::Millis(static_cast<int64_t>(f * 10)), {f, {}}, DataSize::Bytes(40));
      ++sent;
    } else if (d == DtxDecision::kSendSid) {
      link.Send(Timestamp::Millis(static_cast<int64_t>(f * 10)), {f, EncodeSid(sid)}, DataSize::Bytes(12));
      ++sent;
    }
  }
  std::vector<float> out(c.capture.size(), 0.0f);
  std::vector<int> kind(frames, 0);  // 0 nothing, 1 speech, 2 SID
  std::map<size_t, std::vector<uint8_t>> sids;
  for (auto& [f, payload] : link.Receive(Timestamp::Seconds(3600))) {
    kind[f] = payload.empty() ? 1 : 2;
    if (!payload.empty()) sids[f] = payload;
  }
  for (size_t f = 0; f < frames; ++f) {
    std::span<float> o(out.data() + f * 160, 160);
    if (kind[f] == 1) {
      std::copy(c.capture.begin() + static_cast<std::ptrdiff_t>(f * 160), c.capture.begin() + static_cast<std::ptrdiff_t>(f * 160 + 160), o.begin());
    } else {
      if (kind[f] == 2) cng.OnSid(*DecodeSid(sids[f]));
      cng.Generate(o);
    }
  }
  // Level during non-speech frames vs. the real background noise.
  double eo = 0, en = 0;
  size_t n = 0;
  int since_speech = 1000;
  for (size_t f = 0; f < frames; ++f) {
    bool talking = dsp::RmsDbfs(std::span<const float>(c.talker.data() + f * 160, 160)) > -60.0f;
    since_speech = talking ? 0 : since_speech + 1;
    // Judge comfort noise only in real pauses (beyond VAD hangover).
    if (f < 200 || kind[f] == 1 || since_speech < 30) continue;
    for (size_t i = f * 160; i < f * 160 + 160; ++i) {
      eo += static_cast<double>(out[i]) * out[i];
      en += static_cast<double>(c.noise[i]) * c.noise[i];
    }
    n += 160;
  }
  double level_err = 10 * std::log10(eo / en);
  double tx_ratio = static_cast<double>(sent) / static_cast<double>(frames);
  size_t speech_frames = 0;
  for (size_t f = 0; f < frames; ++f) {
    speech_frames += dsp::RmsDbfs(std::span<const float>(c.talker.data() + f * 160, 160)) > -60.0f;
  }
  double speech_ratio = static_cast<double>(speech_frames) / static_cast<double>(frames);
  SM_RECORD("speech_ratio", speech_ratio);
  SM_RECORD("dtx_tx_ratio", tx_ratio);
  SM_RECORD("cng_level_error_db", level_err);
  // Transmitted = speech + hangover + SIDs (one per 400 ms at most).
  SM_EXPECT_LT(tx_ratio, speech_ratio + 0.12);
  SM_EXPECT_NEAR(level_err, 0.0, 3.0);
  SM_EXPECT_GT(n, 16000u);
}

SM_TEST(E2E_AudioLevel, ActiveSpeakerFollowsTurns) {
  // Four participants take 5 s turns; everyone has some background noise.
  const int fs = 16000, people = 4;
  const double turn = 5.0;
  std::vector<std::vector<float>> voice;
  for (int p = 0; p < people; ++p) {
    auto v = sim::SpeechLike(fs, turn * people, 10 + p, false, 0.25f);
    auto noise = sim::WhiteNoise(fs, turn * people, 0.002f, 20 + p);
    for (size_t i = 0; i < v.size(); ++i) {
      bool talking = static_cast<int>(static_cast<double>(i) / fs / turn) == p;
      v[i] = (talking ? v[i] : 0.0f) + noise[i];
    }
    voice.push_back(std::move(v));
  }
  std::vector<Vad> vads(people);
  ActiveSpeakerDetector asd;
  int correct = 0, judged = 0;
  for (size_t f = 0; f + 1 < voice[0].size() / 320; ++f) {  // 20 ms packets.
    Timestamp now = Timestamp::Millis(static_cast<int64_t>(f * 20));
    for (int p = 0; p < people; ++p) {
      std::span<const float> fr(voice[static_cast<size_t>(p)].data() + f * 320, 320);
      bool v1 = vads[static_cast<size_t>(p)].Process(fr.first(160));
      bool v2 = vads[static_cast<size_t>(p)].Process(fr.last(160));
      bool v = v1 || v2;
      // Sender writes the RFC 6464 byte; the SFU only reads that byte.
      uint8_t ext = EncodeAudioLevelExtension({ComputeLevel(fr), v});
      asd.OnLevel(static_cast<uint32_t>(p), DecodeAudioLevelExtension(ext), now);
    }
    double t = f * 0.02;
    if (std::fmod(t, turn) > 1.5 && asd.dominant()) {
      ++judged;
      correct += static_cast<int>(*asd.dominant()) == static_cast<int>(t / turn);
    }
  }
  SM_RECORD("active_speaker_accuracy", static_cast<double>(correct) / judged);
  SM_RECORD("active_speaker_switches", asd.switches());
  SM_EXPECT_GT(static_cast<double>(correct) / judged, 0.95);
  SM_EXPECT_LE(asd.switches(), static_cast<uint64_t>(people + 2));
}

SM_TEST(E2E_Mixer, ConferenceMixOfLoudestThree) {
  const int fs = 16000;
  std::vector<std::vector<float>> src;
  for (int p = 0; p < 6; ++p) {
    if (p < 3) {
      src.push_back(sim::SpeechLike(fs, 10.0, 30 + p, false, 0.6f));  // Talkers.
    } else {
      src.push_back(sim::WhiteNoise(fs, 10.0, 0.003f, 40 + p));       // Muted-ish rooms.
    }
  }
  AudioMixer mixer;
  std::vector<float> out(160);
  int frames = 0, ok = 0;
  float peak = 0;
  for (size_t off = 0; off + 160 <= src[0].size(); off += 160) {
    std::map<uint32_t, std::span<const float>> m;
    for (uint32_t p = 0; p < 6; ++p) m[p] = std::span<const float>(src[p].data() + off, 160);
    mixer.Mix(m, out);
    for (float v : out) peak = std::max(peak, std::fabs(v));
    auto mixed = mixer.mixed();
    ++frames;
    int talkers = 0;
    for (uint32_t id : mixed) talkers += id < 3;
    ok += talkers == 3;
  }
  SM_RECORD("mixer_talkers_selected_ratio", static_cast<double>(ok) / frames);
  SM_RECORD("mixer_peak", peak);
  SM_EXPECT_GT(static_cast<double>(ok) / frames, 0.95);
  SM_EXPECT_LE(peak, dsp::DbToLinear(-1.0f) + 1e-6f);
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
