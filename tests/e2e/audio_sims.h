// End-to-end audio scenarios for section 6: a synthetic two-party call
// (far-end speech played into a room, near-end talker, background noise,
// DC/hum, keyboard clicks) and helpers to run capture chains over it.
#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "sm/audio/agc2.h"
#include "sm/audio/comfort_noise.h"
#include "sm/audio/echo_canceller.h"
#include "sm/audio/gain_control.h"
#include "sm/audio/high_pass_filter.h"
#include "sm/audio/noise_suppressor.h"
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
  // `apm v2`: libwebrtc's modules where ported (#240-#242). So far the AGC
  // is AGC2 (`agc2::GainController2`) fed our VAD's per-frame decision.
  bool apm_v2 = false;
};

struct ChainResult {
  std::vector<float> out;
  std::vector<bool> vad;  // Per 10 ms frame.
  size_t latency = 0;     // Samples of algorithmic delay.
  audio::EchoCancellerStats aec;
};

inline ChainResult RunChain(const CallSignals& c, const ChainConfig& cc, int fs = 16000) {
  audio::HighPassFilter hpf(fs);
  audio::EchoCancellerConfig ac;
  ac.sample_rate = fs;
  audio::EchoCanceller aec(ac);
  audio::NoiseSuppressorConfig nc;
  nc.sample_rate = fs;
  audio::NoiseSuppressor ns(nc);
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
  ChainResult r;
  r.out = c.capture;
  const size_t frame = static_cast<size_t>(fs / 100);
  for (size_t i = 0; i + frame <= r.out.size(); i += frame) {
    std::span<float> f(r.out.data() + i, frame);
    if (cc.hpf) hpf.Process(f);
    if (cc.aec) {
      aec.AnalyzeRender(std::span<const float>(c.far.data() + i, frame));
      aec.ProcessCapture(f);
    }
    if (cc.ns) ns.Process(f);
    bool v = vad.Process(f);
    r.vad.push_back(v);
    if (cc.ts) ts.Process(f, vad.voiced());
    if (cc.agc && cc.apm_v2) {
      // AGC2 reads a per-frame speech probability (its RNN VAD has no
      // hangover), so it gets our VAD's decision before the hangover.
      agc2.Process(f, vad.raw_decision() ? 1.0f : 0.0f);
    } else if (cc.agc) {
      agc.Process(f, v);
    }
  }
  if (cc.aec) r.latency += aec.latency_samples();
  if (cc.ns) r.latency += ns.latency_samples();
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
