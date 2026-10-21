// sm_apm_compare: this library's audio processing modules and libwebrtc's
// APM on the same test signals (#63, section 6).
//
// Every case is one of the section 6 end-to-end scenarios in
// tests/e2e/audio_e2e_test.cc, built by tests/e2e/audio_sims.h from
// include/sm/sim/signals.h. Each side runs the same chain of modules on the
// same capture and far-end signals, 16 kHz mono in 10 ms frames, and the
// same metric code reads both outputs.
//
// libwebrtc's side is `BuiltinAudioProcessingBuilder` with the modules of
// the chain switched on, at the settings `WebRtcVoiceEngine` applies to a
// call (media/engine/webrtc_voice_engine.cc, `ApplyOptions`): AEC3, the
// high-pass filter, noise suppression at kHigh, and AGC1 in adaptive analog
// mode on desktop. Our AGC follows AGC2's adaptive digital controller, so it
// is also measured against that. The VAD is `webrtc::CreateVad` (the
// common_audio GMM VAD), which APM no longer runs itself.
//
//   out/compare/sm_apm_compare            # Markdown table on stdout
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

#include "api/audio/audio_processing.h"
#include "api/audio/builtin_audio_processing_builder.h"
#include "api/environment/environment_factory.h"
#include "audio_sims.h"
#include "common_audio/vad/include/vad.h"

namespace {

using sm::e2e::CallScenario;
using sm::e2e::CallSignals;
using sm::e2e::ChainConfig;
using sm::e2e::MakeCall;
using sm::e2e::RunChain;
using sm::e2e::SegmentDb;
using ApmConfig = webrtc::AudioProcessing::Config;

constexpr int kFs = 16000;
constexpr size_t kFrame = kFs / 100;

struct Output {
  std::vector<float> out;
  size_t latency = 0;  // Samples of algorithmic delay.
};

Output RunOurs(const CallSignals& c, const ChainConfig& cc) {
  auto r = RunChain(c, cc, kFs);
  return {std::move(r.out), r.latency};
}

// libwebrtc's APM over the call, frame by frame as `AudioTransportImpl`
// drives it: render first, then capture with the stream delay (0: the test
// signals carry the echo path delay themselves) and, for AGC1, the analog
// level fed back from the previous frame.
Output RunWebrtc(const CallSignals& c, const ApmConfig& cfg) {
  auto apm = webrtc::BuiltinAudioProcessingBuilder(cfg).Build(webrtc::CreateEnvironment());
  const webrtc::StreamConfig sc(kFs, 1);
  Output o;
  o.out = c.capture;
  std::vector<float> render(kFrame), render_out(kFrame), cap(kFrame);
  int analog_level = 255;
  for (size_t i = 0; i + kFrame <= o.out.size(); i += kFrame) {
    std::copy_n(c.far.data() + i, kFrame, render.data());
    const float* rin = render.data();
    float* rout = render_out.data();
    apm->ProcessReverseStream(&rin, sc, sc, &rout);
    apm->set_stream_delay_ms(0);
    if (cfg.gain_controller1.enabled) apm->set_stream_analog_level(analog_level);
    std::copy_n(o.out.data() + i, kFrame, cap.data());
    const float* cin = cap.data();
    float* cout = o.out.data() + i;
    apm->ProcessStream(&cin, sc, sc, &cout);
    if (cfg.gain_controller1.enabled) analog_level = apm->recommended_stream_analog_level();
  }
  return o;
}

ApmConfig WebrtcConfig(const ChainConfig& cc, bool agc2 = false) {
  ApmConfig cfg;
  cfg.high_pass_filter.enabled = cc.hpf;
  cfg.echo_canceller.enabled = cc.aec;
  cfg.noise_suppression.enabled = cc.ns;
  cfg.noise_suppression.level = ApmConfig::NoiseSuppression::kHigh;
  if (cc.agc && !agc2) {
    cfg.gain_controller1.enabled = true;
    cfg.gain_controller1.mode = ApmConfig::GainController1::kAdaptiveAnalog;
    // No device volume in the simulation: APM emulates the analog gain.
    cfg.capture_level_adjustment.enabled = true;
    cfg.capture_level_adjustment.analog_mic_gain_emulation.enabled = true;
  }
  if (cc.agc && agc2) {
    cfg.gain_controller2.enabled = true;
    cfg.gain_controller2.adaptive_digital.enabled = true;
  }
  return cfg;
}

// Amplitude of a sinusoid at `freq` in x (single-bin DFT), as the e2e test.
double ToneAmp(std::span<const float> x, double freq) {
  double re = 0, im = 0;
  for (size_t i = 0; i < x.size(); ++i) {
    double a = 2 * std::numbers::pi * freq * static_cast<double>(i) / kFs;
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

std::span<const float> Seg(const std::vector<float>& x, double a_s, double len_s, size_t shift = 0) {
  size_t i0 = std::min(x.size(), static_cast<size_t>(a_s * kFs) + shift);
  size_t n = std::min(x.size() - i0, static_cast<size_t>(len_s * kFs));
  return {x.data() + i0, n};
}

// SNR of `out` against `ref` from `start` to the end, at the output delay
// (0..40 ms) that gives the best SNR, so each side is judged at its own
// algorithmic delay. Returns the SNR and sets `lag`.
double BestLagSnr(const std::vector<float>& ref, const std::vector<float>& out, size_t start, size_t* lag) {
  double best = -1e9;
  for (size_t l = 0; l <= 640; ++l) {
    size_t n = ref.size() - start - 640;
    double snr = sm::dsp::SnrDb({ref.data() + start, n}, {out.data() + start + l, n});
    if (snr > best) {
      best = snr;
      *lag = l;
    }
  }
  return best;
}

// Ground truth for frame f: the talker is active (as E2E_Vad), or has been
// silent for more than 250 ms (frames between are not scored).
enum class Truth { kSpeech, kSilence, kSkip };
std::vector<Truth> SpeechTruth(const CallSignals& c, float active_dbfs) {
  std::vector<Truth> t;
  int since = 1000;
  for (size_t f = 0; (f + 1) * kFrame <= c.talker.size(); ++f) {
    bool speech = sm::dsp::RmsDbfs({c.talker.data() + f * kFrame, kFrame}) > active_dbfs;
    since = speech ? 0 : since + 1;
    t.push_back(speech ? Truth::kSpeech : since > 25 ? Truth::kSilence : Truth::kSkip);
  }
  return t;
}

// Level (dBFS) of `out` over the frames of [a, b) s marked `want`.
double FramesDb(const std::vector<float>& out, const std::vector<Truth>& truth, Truth want, double a, double b,
                size_t shift) {
  double e = 0;
  size_t n = 0;
  for (size_t f = static_cast<size_t>(a * 100); f < static_cast<size_t>(b * 100) && f < truth.size(); ++f) {
    if (truth[f] != want) continue;
    for (size_t i = f * kFrame + shift; i < f * kFrame + kFrame + shift && i < out.size(); ++i) {
      e += static_cast<double>(out[i]) * out[i];
      ++n;
    }
  }
  return 10 * std::log10(e / static_cast<double>(std::max<size_t>(1, n)) + 1e-20);
}

struct Row {
  std::string module, scenario, metric;
  double ours, webrtc;
  std::string note;
};
std::vector<Row> rows;

void Add(std::string module, std::string scenario, std::string metric, double ours, double webrtc,
         std::string note = "") {
  rows.push_back({std::move(module), std::move(scenario), std::move(metric), ours, webrtc, std::move(note)});
}

// E2E_HighPassFilter: DC 0.1 and 50 Hz hum 0.05 under a near-end talker.
void HighPass() {
  CallScenario s;
  s.far_talk = false;
  s.dc = 0.1f;
  s.hum_amp = 0.05f;
  s.noise_amp = 0.002f;
  auto c = MakeCall(s);
  ChainConfig cc{true, false, false, false, false};
  auto ours = RunOurs(c, cc);
  auto webrtc = RunWebrtc(c, WebrtcConfig(cc));
  double hum_in = ToneAmp(Seg(c.capture, 10, 8), 50);
  auto hum_db = [&](const Output& o) { return 20 * std::log10(hum_in / ToneAmp(Seg(o.out, 10, 8), 50)); };
  Add("high-pass filter", "dc + 50 Hz hum", "hum attenuation (dB)", hum_db(ours), hum_db(webrtc));
  Add("high-pass filter", "dc + 50 Hz hum", "DC left (x1000)", 1000 * Mean(Seg(ours.out, 10, 8)),
      1000 * Mean(Seg(webrtc.out, 10, 8)));
  // Pass band: the clean talker alone through the filter.
  CallScenario t;
  t.far_talk = false;
  auto ct = MakeCall(t);
  auto ours_t = RunOurs(ct, cc);
  auto webrtc_t = RunWebrtc(ct, WebrtcConfig(cc));
  double in = SegmentDb(ct.capture, 3, 20);
  Add("high-pass filter", "talker only", "speech level change (dB)", SegmentDb(ours_t.out, 3, 20) - in,
      SegmentDb(webrtc_t.out, 3, 20) - in);
}

// E2E_EchoDelay: far end only, echo path 60 ms, then 180 ms at 12 s.
void EchoRouteChange() {
  CallScenario s;
  s.seconds = 24;
  s.echo_delay_ms = 60;
  s.echo_delay2_ms = 180;
  s.near_start_s = 1e9;
  s.noise_amp = 0.0005f;
  auto c = MakeCall(s);
  ChainConfig cc{true, true, false, false, false};
  auto ours = RunOurs(c, cc);
  auto webrtc = RunWebrtc(c, WebrtcConfig(cc));
  auto erle = [&](const Output& o, double a, double b) {
    return SegmentDb(c.echo, a, b) - SegmentDb(o.out, a, b, kFs, o.latency);
  };
  Add("echo canceller", "far end only, route change", "ERLE 1-3 s (dB)", erle(ours, 1, 3), erle(webrtc, 1, 3),
      "convergence");
  Add("echo canceller", "far end only, route change", "ERLE 6-12 s (dB)", erle(ours, 6, 12), erle(webrtc, 6, 12));
  Add("echo canceller", "far end only, route change", "ERLE 12-14 s (dB)", erle(ours, 12, 14),
      erle(webrtc, 12, 14), "just after the delay change");
  Add("echo canceller", "far end only, route change", "ERLE 18-24 s (dB)", erle(ours, 18, 24),
      erle(webrtc, 18, 24));
}

// E2E_EchoCanceller.FullDuplexCall: far end alone, then double talk at 15 s.
void EchoDoubleTalk() {
  CallScenario s;
  s.seconds = 30;
  s.near_start_s = 15;
  s.noise_amp = 0.0005f;
  auto c = MakeCall(s);
  ChainConfig cc{true, true, true, false, false};
  auto ours = RunOurs(c, cc);
  auto webrtc = RunWebrtc(c, WebrtcConfig(cc));
  auto erle = [&](const Output& o) { return SegmentDb(c.echo, 6, 15) - SegmentDb(o.out, 6, 15, kFs, o.latency); };
  auto near = [&](const Output& o) { return SegmentDb(o.out, 18, 30, kFs, o.latency) - SegmentDb(c.talker, 18, 30); };
  Add("echo canceller + NS", "full duplex", "ERLE far end alone 6-15 s (dB)", erle(ours), erle(webrtc));
  Add("echo canceller + NS", "full duplex", "near-end level change in double talk (dB)", near(ours), near(webrtc));
  // How much of the output in double talk is the near-end talker: the
  // talker against everything else (residual echo, distortion, noise).
  size_t lag_o = 0, lag_w = 0;
  std::vector<float> ref(c.talker.begin(), c.talker.begin() + 30 * kFs);
  auto snr = [&](const Output& o, size_t* lag) {
    std::vector<float> tail(o.out.begin() + 18 * kFs, o.out.end());
    std::vector<float> rtail(ref.begin() + 18 * kFs, ref.end());
    return BestLagSnr(rtail, tail, 0, lag);
  };
  double so = snr(ours, &lag_o), sw = snr(webrtc, &lag_w);
  Add("echo canceller + NS", "full duplex", "near-end SNR in double talk (dB)", so, sw);
}

// E2E_NoiseSuppressor: talker in white noise at about 7 dB SNR. With
// `apm_v2`, our side is the port of libwebrtc's NS (#241).
void Noise(bool apm_v2) {
  CallScenario s;
  s.far_talk = false;
  s.noise_amp = 0.03f;
  auto c = MakeCall(s);
  ChainConfig base{true, false, false, false, false};
  ChainConfig cc{true, false, true, false, false};
  const std::string module = apm_v2 ? "noise suppressor (apm v2)" : "noise suppressor";
  ChainConfig ours_cc = cc;
  ours_cc.apm_v2 = apm_v2;
  auto ours = RunOurs(c, ours_cc);
  auto webrtc = RunWebrtc(c, WebrtcConfig(cc));
  auto ours_base = RunOurs(c, base);
  auto webrtc_base = RunWebrtc(c, WebrtcConfig(base));
  // Reference: the clean talker through each side's own high-pass filter.
  CallScenario t = s;
  t.noise_amp = 0;
  auto ct = MakeCall(t);
  auto ref_o = RunOurs(ct, base).out, ref_w = RunWebrtc(ct, WebrtcConfig(base)).out;
  const size_t start = 3 * kFs;
  size_t lo = 0, lw = 0, l0 = 0;
  double in_o = BestLagSnr(ref_o, ours_base.out, start, &l0);
  double in_w = BestLagSnr(ref_w, webrtc_base.out, start, &l0);
  double out_o = BestLagSnr(ref_o, ours.out, start, &lo);
  double out_w = BestLagSnr(ref_w, webrtc.out, start, &lw);
  Add(module, "white noise, ~7 dB SNR", "SNR improvement (dB)", out_o - in_o, out_w - in_w,
      apm_v2 ? "both kHigh (18 dB)" : "libwebrtc kHigh (18 dB), ours 18 dB");
  auto truth = SpeechTruth(c, -45.0f);
  auto atten = [&](const Output& o, const Output& b) {
    return FramesDb(b.out, truth, Truth::kSilence, 3, 20, 0) - FramesDb(o.out, truth, Truth::kSilence, 3, 20, o.latency);
  };
  ours.latency = lo;
  webrtc.latency = lw;
  Add(module, "white noise, ~7 dB SNR", "noise attenuation in pauses (dB)", atten(ours, ours_base),
      atten(webrtc, webrtc_base));
  auto speech = [&](const Output& o, const Output& b) {
    return FramesDb(o.out, truth, Truth::kSpeech, 3, 20, o.latency) - FramesDb(b.out, truth, Truth::kSpeech, 3, 20, 0);
  };
  Add(module, "white noise, ~7 dB SNR", "level change in speech (dB)", speech(ours, ours_base),
      speech(webrtc, webrtc_base));
  Add(module, "white noise, ~7 dB SNR", "algorithmic delay (ms)", lo / 16.0, lw / 16.0);
}

// E2E_GainControl: a quiet talker (~-33 dBFS) for 20 s, then +20 dB. With
// `apm_v2`, our side is the port of the same controller (#242): AGC1 with the
// emulated mic volume against libwebrtc's AGC1, AGC2 against its AGC2.
void Gain(bool apm_v2) {
  CallScenario q;
  q.far_talk = false;
  q.seconds = 40;
  q.near_gain_db = -10;
  q.noise_amp = 0.0002f;
  auto c = MakeCall(q);
  for (size_t i = c.capture.size() / 2; i < c.capture.size(); ++i) c.capture[i] *= sm::dsp::DbToLinear(20);
  ChainConfig cc{true, false, false, false, true};
  auto ours = RunOurs(c, cc);
  ChainConfig v2 = cc;
  v2.apm_v2 = true;
  auto ours_agc1 = RunOurs(c, v2);
  v2.agc2 = true;
  auto ours_agc2 = RunOurs(c, v2);
  auto agc1 = RunWebrtc(c, WebrtcConfig(cc));
  auto agc2 = RunWebrtc(c, WebrtcConfig(cc, true));
  auto truth = SpeechTruth(c, -60.0f);
  auto lvl = [&](const Output& o, double a, double b) { return FramesDb(o.out, truth, Truth::kSpeech, a, b, 0); };
  auto in = [&](double a, double b) { return FramesDb(c.capture, truth, Truth::kSpeech, a, b, 0); };
  auto peak = [](const Output& o) {
    float p = 0;
    for (float v : o.out) p = std::max(p, std::fabs(v));
    return 20 * std::log10(p);
  };
  const std::string module = apm_v2 ? "gain control (apm v2)" : "gain control";
  for (int i = 0; i < 2; ++i) {
    const Output& w = i == 0 ? agc1 : agc2;
    if (apm_v2) ours = i == 0 ? ours_agc1 : ours_agc2;
    std::string sc = i == 0 ? "quiet then loud talker, AGC1 (default)" : "quiet then loud talker, AGC2 digital";
    Add(module, sc, "quiet talker in -> out, 12-20 s (dBFS)", lvl(ours, 12, 20), lvl(w, 12, 20),
        "input " + std::to_string(static_cast<int>(std::lround(in(12, 20)))) + " dBFS");
    Add(module, sc, "loud talker level change, 32-40 s (dB)", lvl(ours, 32, 40) - in(32, 40),
        lvl(w, 32, 40) - in(32, 40));
    Add(module, sc, "noise level in pauses, 12-20 s (dBFS)",
        FramesDb(ours.out, truth, Truth::kSilence, 12, 20, 0), FramesDb(w.out, truth, Truth::kSilence, 12, 20, 0));
    Add(module, sc, "peak (dBFS)", peak(ours), peak(w));
  }
}

// E2E_Vad: talker in white noise at about 10 dB SNR, after the high-pass
// filter (both detectors read the same signal).
void VoiceActivity() {
  CallScenario s;
  s.far_talk = false;
  s.noise_amp = 0.015f;
  auto c = MakeCall(s);
  auto in = RunOurs(c, {true, false, false, false, false}).out;
  sm::audio::VadConfig vc;
  sm::audio::Vad ours(vc);
  auto webrtc = webrtc::CreateVad(webrtc::Vad::kVadNormal);
  auto truth = SpeechTruth(c, -45.0f);
  int tp[2] = {}, fn[2] = {}, fp[2] = {}, tn[2] = {};
  std::vector<int16_t> pcm(kFrame);
  for (size_t f = 0; f < truth.size(); ++f) {
    std::span<const float> fr(in.data() + f * kFrame, kFrame);
    for (size_t i = 0; i < kFrame; ++i) {
      pcm[i] = static_cast<int16_t>(std::clamp(std::lround(fr[i] * 32768.0f), -32768L, 32767L));
    }
    bool v[2] = {ours.Process(fr), webrtc->VoiceActivity(pcm.data(), kFrame, kFs) == webrtc::Vad::kActive};
    if (f < 100) continue;  // As E2E_Vad: the first second is warm-up.
    for (int k = 0; k < 2; ++k) {
      if (truth[f] == Truth::kSpeech) (v[k] ? tp[k] : fn[k])++;
      if (truth[f] == Truth::kSilence) (v[k] ? fp[k] : tn[k])++;
    }
  }
  auto recall = [&](int k) { return 100.0 * tp[k] / std::max(1, tp[k] + fn[k]); };
  auto fpr = [&](int k) { return 100.0 * fp[k] / std::max(1, fp[k] + tn[k]); };
  Add("VAD", "white noise, ~10 dB SNR", "speech frames detected (%)", recall(0), recall(1), "libwebrtc kVadNormal");
  Add("VAD", "white noise, ~10 dB SNR", "noise frames flagged as speech (%)", fpr(0), fpr(1));
}

}  // namespace

int main() {
  HighPass();
  EchoRouteChange();
  EchoDoubleTalk();
  Noise(false);
  Noise(true);
  Gain(false);
  Gain(true);
  VoiceActivity();
  std::printf("| module | scenario | metric | ours | libwebrtc | note |\n|---|---|---|---|---|---|\n");
  for (const auto& r : rows) {
    std::printf("| %s | %s | %s | %.1f | %.1f | %s |\n", r.module.c_str(), r.scenario.c_str(), r.metric.c_str(), r.ours,
                r.webrtc, r.note.c_str());
  }
  return 0;
}
