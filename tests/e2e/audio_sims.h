// End-to-end audio scenarios for section 6: a synthetic two-party call
// (far-end speech played into a room, near-end talker, background noise,
// DC/hum, keyboard clicks) and helpers to run capture chains over it.
#pragma once

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

#include "sm/audio/agc1.h"
#include "sm/audio/agc2.h"
#include "sm/audio/agc_manager.h"
#include "sm/audio/comfort_noise.h"
#include "sm/audio/echo_canceller.h"
#include "sm/audio/gain_control.h"
#include "sm/audio/high_pass_filter.h"
#include "sm/audio/hpf.h"
#include "sm/audio/noise_suppressor.h"
#include "sm/audio/ns.h"
#include "sm/audio/transient_suppressor.h"
#include "sm/audio/vad.h"
#include "sm/core/dsp.h"
#include "sm/sim/signals.h"

namespace sm::e2e {

struct CallScenario {
  int fs = 16000;
  double seconds = 20;
  bool far_talk = true;
  int echo_delay_ms = 60;
  int echo_delay2_ms = -1;         // Route change at half time when >= 0.
  float near_gain_db = 0;           // Talker gain.
  double near_start_s = 0, near_end_s = 1e9;
  float noise_amp = 0.0f;
  float dc = 0.0f;
  float hum_amp = 0.0f;             // 50 Hz.
  double click_interval_s = 0;      // Keyboard clicks (0 = none).
  uint64_t seed = 1;
};

struct CallSignals {
  std::vector<float> far, echo, talker, noise, clicks, capture;
};

inline CallSignals MakeCall(const CallScenario& s) {
  CallSignals c;
  const size_t n = static_cast<size_t>(s.seconds * s.fs);
  c.far = s.far_talk ? sim::SpeechLike(s.fs, s.seconds, s.seed, true) : std::vector<float>(n, 0.0f);
  auto rir = sim::RoomImpulseResponse(s.fs, 40, 100, s.seed + 1);
  auto conv = sim::Convolve(c.far, rir);
  c.echo.assign(n, 0.0f);
  for (size_t i = 0; i < n; ++i) {
    int d = (s.echo_delay2_ms >= 0 && i >= n / 2) ? s.echo_delay2_ms : s.echo_delay_ms;
    size_t ds = static_cast<size_t>(d * s.fs / 1000);
    if (i >= ds) c.echo[i] = conv[i - ds];
  }
  auto talk = sim::SpeechLike(s.fs, s.seconds, s.seed + 2, true, 0.25f);
  float g = dsp::DbToLinear(s.near_gain_db);
  c.talker.assign(n, 0.0f);
  for (size_t i = 0; i < n; ++i) {
    double t = static_cast<double>(i) / s.fs;
    if (t >= s.near_start_s && t < s.near_end_s) c.talker[i] = g * talk[i];
  }
  c.noise = s.noise_amp > 0 ? sim::WhiteNoise(s.fs, s.seconds, s.noise_amp, s.seed + 3) : std::vector<float>(n, 0.0f);
  c.clicks.assign(n, 0.0f);
  if (s.click_interval_s > 0) {
    Random rng(s.seed + 4);
    size_t step = static_cast<size_t>(s.click_interval_s * s.fs);
    for (size_t at = step; at + 64 < n; at += step) {
      for (size_t i = 0; i < 64; ++i) c.clicks[at + i] = static_cast<float>(0.3 * rng.Gaussian() * std::exp(-static_cast<double>(i) / 12));
    }
  }
  c.capture.assign(n, 0.0f);
  for (size_t i = 0; i < n; ++i) {
    double t = static_cast<double>(i) / s.fs;
    c.capture[i] = c.echo[i] + c.talker[i] + c.noise[i] + c.clicks[i] + s.dc +
                   s.hum_amp * static_cast<float>(std::sin(2 * std::numbers::pi * 50 * t));
  }
  return c;
}

// The capture processing chain, in WebRTC APM order.
struct ChainConfig {
  bool hpf = true, aec = false, ns = false, ts = false, agc = false;
  // `apm v2`: libwebrtc's modules where ported (#239-#242). The high-pass
  // filter is libwebrtc's (`hpf::HighPassFilter`, #239). The AGC is AGC1
  // as a call runs it (`AgcManagerDirect` in kAdaptiveAnalog with
  // `GainControl` in kFixedDigital, the volume emulated by `CaptureLevels`
  // from 255, #242), and the NS is libwebrtc's (`ns::NoiseSuppressor`,
  // kHigh), which analyzes the capture before the AEC and suppresses after
  // it, as APM does.
  bool apm_v2 = false;
  // With `apm_v2`: AGC2 (`agc2::GainController2` with its RNN VAD) instead
  // of AGC1.
  bool agc2 = false;
};

struct ChainResult {
  std::vector<float> out;
  std::vector<bool> vad;  // Per 10 ms frame.
  size_t latency = 0;     // Samples of algorithmic delay.
  audio::EchoCancellerStats aec;
};

inline ChainResult RunChain(const CallSignals& c, const ChainConfig& cc, int fs = 16000) {
  audio::HighPassFilter hpf(fs);
  audio::hpf::HighPassFilter hpf2(fs);
  audio::EchoCancellerConfig ac;
  ac.sample_rate = fs;
  audio::EchoCanceller aec(ac);
  audio::NoiseSuppressorConfig nc;
  nc.sample_rate = fs;
  audio::NoiseSuppressor ns(nc);
  audio::ns::NoiseSuppressorConfig nc2;
  nc2.sample_rate = fs;
  // On the heap, as the AGC1 objects below: AGC1's pitch-based VAD needs
  // ~40 KB of stack (iSAC's PitchAnalysis, as in libwebrtc) and wasm's
  // stack is 64 KB.
  auto ns2_ptr = std::make_unique<audio::ns::NoiseSuppressor>(nc2);
  audio::ns::NoiseSuppressor& ns2 = *ns2_ptr;
  audio::VadConfig vc;
  vc.sample_rate = fs;
  audio::Vad vad(vc);
  audio::TransientSuppressor ts;
  audio::AgcConfig gc;
  gc.sample_rate = fs;
  gc.limiter.sample_rate = fs;
  audio::AutomaticGainControl agc(gc);
  audio::agc2::GainController2Config gc2;
  gc2.sample_rate = fs;
  audio::agc2::GainController2 agc2(gc2);
  // AGC1 (#242): APM's order with the analog mic gain emulation.
  const bool agc1_on = cc.agc && cc.apm_v2 && !cc.agc2;
  auto agc_manager_ptr = std::make_unique<audio::agc1::AgcManagerDirect>(1);
  audio::agc1::AgcManagerDirect& agc_manager = *agc_manager_ptr;
  agc_manager.Initialize();
  audio::agc1::GainControlConfig g1;
  g1.sample_rate = fs;
  g1.target_level_dbfs = agc_manager.digital_target_level_dbfs();
  g1.compression_gain_db = agc_manager.digital_compression_gain_db();
  g1.enable_limiter = agc_manager.digital_limiter_enabled();
  auto agc1_ptr = std::make_unique<audio::agc1::GainControl>(g1);
  audio::agc1::GainControl& agc1 = *agc1_ptr;
  audio::agc1::CaptureLevelsConfig lc;
  lc.analog_mic_gain_emulation = true;
  audio::agc1::CaptureLevels levels(lc);
  std::vector<float> s16;
  auto to_s16 = [&](std::span<const float> f) {
    s16.assign(f.begin(), f.end());
    for (float& v : s16) v *= 32768.0f;
  };
  auto from_s16 = [&](std::span<float> f) {
    for (size_t k = 0; k < f.size(); ++k) f[k] = s16[k] / 32768.0f;
  };
  ChainResult r;
  r.out = c.capture;
  const size_t frame = static_cast<size_t>(fs / 100);
  for (size_t i = 0; i + frame <= r.out.size(); i += frame) {
    std::span<float> f(r.out.data() + i, frame);
    if (cc.hpf && cc.apm_v2) {
      hpf2.Process(f);
    } else if (cc.hpf) {
      hpf.Process(f);
    }
    if (agc1_on) {
      // The emulated volume is the applied input volume.
      agc_manager.set_stream_analog_level(levels.GetAnalogMicGainLevel());
      to_s16(f);
      std::span<float> ch[1] = {s16};
      levels.ApplyPreLevelAdjustment(ch);
      const std::span<const float> cch[1] = {s16};
      agc_manager.AnalyzePreProcess(cch);
      from_s16(f);
    }
    if (cc.ns && cc.apm_v2) ns2.Analyze(f);
    if (cc.aec) {
      aec.AnalyzeRender(std::span<const float>(c.far.data() + i, frame));
      aec.ProcessCapture(f);
    }
    if (cc.ns && cc.apm_v2) {
      ns2.Process(f);
    } else if (cc.ns) {
      ns.Process(f);
    }
    bool v = vad.Process(f);
    r.vad.push_back(v);
    if (cc.ts) ts.Process(f, vad.voiced());
    if (agc1_on) {
      to_s16(f);
      const std::span<const float> band0[1] = {s16};
      agc_manager.Process(band0);
      if (auto g = agc_manager.GetDigitalCompressionGain()) agc1.set_compression_gain_db(*g);
      agc1.Process(f);
      to_s16(f);
      std::span<float> ch[1] = {s16};
      levels.ApplyPostLevelAdjustment(ch);
      from_s16(f);
      levels.SetAnalogMicGainLevel(agc_manager.recommended_analog_level());
    } else if (cc.agc && cc.apm_v2) {
      agc2.Process(f);
    } else if (cc.agc) {
      agc.Process(f, v);
    }
  }
  if (cc.aec) r.latency += aec.latency_samples();
  if (cc.ns) r.latency += cc.apm_v2 ? ns2.latency_samples() : ns.latency_samples();
  r.aec = aec.stats();
  return r;
}

// Energy (dB) of x over [a, b) seconds.
inline double SegmentDb(const std::vector<float>& x, double a, double b, int fs = 16000, size_t shift = 0) {
  size_t i0 = static_cast<size_t>(a * fs) + shift, i1 = std::min(x.size(), static_cast<size_t>(b * fs) + shift);
  if (i1 <= i0) return -100;
  return dsp::RmsDbfs(std::span<const float>(x.data() + i0, i1 - i0));
}

}  // namespace sm::e2e
