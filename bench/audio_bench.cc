// Section 6 benchmarks. Audio blocks are gated by a real-time factor: how
// many times faster than real time a 10 ms frame is processed (at 16 kHz
// unless noted). The budget is scaled down on slow targets.
#include "../tests/e2e/audio_sims.h"

#include <algorithm>
#include "sm/audio/audio_level.h"
#include "sm/audio/resampler.h"
#include "sm/audio/splitting_filter.h"
#include "sm_bench.h"

using namespace sm;
using namespace sm::audio;

namespace {
std::vector<float> Speech() { return sim::SpeechLike(16000, 2.0, 1, true); }
}  // namespace

SM_BENCH(audio, HighPassFilter_10ms) {
  HighPassFilter f;
  auto x = Speech();
  size_t off = 0;
  for (auto _ : st) {
    f.Process(std::span<float>(x.data() + off, 160));
    off = (off + 160) % (x.size() - 160);
  }
  st.RequireRealtimeFactor(0.010, 2000);
}

SM_BENCH(audio, Vad_10ms) {
  Vad v;
  auto x = Speech();
  size_t off = 0;
  for (auto _ : st) {
    bench::DoNotOptimize(v.Process(std::span<const float>(x.data() + off, 160)));
    off = (off + 160) % (x.size() - 160);
  }
  st.RequireRealtimeFactor(0.010, 500);
}

SM_BENCH(audio, SplittingFilter_48k_10ms) {
  // Three-band analysis plus synthesis of one 10 ms frame at 48 kHz.
  SplittingFilter f(48000);
  auto x = sim::SpeechLike(48000, 2.0, 1, true);
  std::vector<float> bands(480), out(480);
  size_t off = 0;
  for (auto _ : st) {
    f.Analysis(std::span<const float>(x.data() + off, 480), bands);
    f.Synthesis(bands, out);
    bench::DoNotOptimize(out[0]);
    off = (off + 480) % (x.size() - 480);
  }
  st.RequireRealtimeFactor(0.010, 500);
}

SM_BENCH(audio, NoiseSuppressor_10ms) {
  NoiseSuppressor ns;
  auto x = Speech();
  size_t off = 0;
  for (auto _ : st) {
    ns.Process(std::span<float>(x.data() + off, 160));
    off = (off + 160) % (x.size() - 160);
  }
  st.RequireRealtimeFactor(0.010, 300);
}

SM_BENCH(audio, Agc_10ms) {
  AutomaticGainControl agc;
  auto x = Speech();
  size_t off = 0;
  for (auto _ : st) {
    agc.Process(std::span<float>(x.data() + off, 160), true);
    off = (off + 160) % (x.size() - 160);
  }
  st.RequireRealtimeFactor(0.010, 1000);
}

SM_BENCH(audio, EchoDelayEstimator_10ms) {
  EchoDelayEstimator e;
  auto x = Speech();
  size_t off = 0;
  for (auto _ : st) {
    e.AnalyzeRender(std::span<const float>(x.data() + off, 160));
    e.AnalyzeCapture(std::span<const float>(x.data() + (off + 1600) % (x.size() - 160), 160));
    off = (off + 160) % (x.size() - 160);
  }
  st.RequireRealtimeFactor(0.010, 50);
}

SM_BENCH(audio, EchoCanceller_10ms_80ms_tail) {
  EchoCanceller aec;
  auto far = Speech();
  auto near = far;
  size_t off = 0;
  for (auto _ : st) {
    aec.AnalyzeRender(std::span<const float>(far.data() + off, 160));
    aec.ProcessCapture(std::span<float>(near.data() + off, 160));
    off = (off + 160) % (far.size() - 160);
  }
  st.RequireRealtimeFactor(0.010, 20);
}

SM_BENCH(audio, TransientSuppressor_10ms) {
  TransientSuppressorConfig tc;
  tc.enabled = true;
  TransientSuppressor ts(tc);
  auto x = Speech();
  size_t off = 0;
  for (auto _ : st) {
    ts.Process(std::span<float>(x.data() + off, 160), false);
    off = (off + 160) % (x.size() - 160);
  }
  st.RequireRealtimeFactor(0.010, 2000);
}

SM_BENCH(audio, DtxController_10ms) {
  DtxController d;
  auto x = sim::WhiteNoise(16000, 2.0, 0.01f, 1);
  size_t off = 0;
  SidFrame sid;
  for (auto _ : st) {
    bench::DoNotOptimize(d.Process(std::span<const float>(x.data() + off, 160), false, &sid));
    off = (off + 160) % (x.size() - 160);
  }
  st.RequireRealtimeFactor(0.010, 500);
}

SM_BENCH(audio, ComfortNoise_10ms) {
  ComfortNoiseGenerator g;
  auto x = sim::WhiteNoise(16000, 0.02, 0.01f, 1);
  g.OnSid(AnalyzeNoise(std::span<const float>(x.data(), 160)));
  std::vector<float> out(160);
  for (auto _ : st) g.Generate(out);
  st.RequireRealtimeFactor(0.010, 500);
}

SM_BENCH(audio, AudioLevel_20ms) {
  auto x = Speech();
  size_t off = 0;
  int64_t t = 0;
  for (auto _ : st) {
    AudioLevel l{ComputeLevel(std::span<const float>(x.data() + off, 320)), true};
    bench::DoNotOptimize(DecodeAudioLevelExtension(EncodeAudioLevelExtension(l)));
    off = (off + 320) % (x.size() - 320);
    ++t;
  }
  st.RequireRealtimeFactor(0.020, 5000);
}

SM_BENCH(audio, Resampler_48k_to_16k_10ms) {
  Resampler r(48000, 16000);
  std::vector<float> in(480, 0.1f), out;
  out.reserve(200);
  for (auto _ : st) {
    out.clear();
    r.Process(in, out);
  }
  st.RequireRealtimeFactor(0.010, 200);
}

SM_BENCH(audio, Resampler_44k1_to_48k_10ms) {
  Resampler r(44100, 48000);
  std::vector<float> in(441, 0.1f), out;
  out.reserve(600);
  for (auto _ : st) {
    out.clear();
    r.Process(in, out);
  }
  st.RequireRealtimeFactor(0.010, 200);
}

SM_BENCH(audio, FullCaptureChain_10ms) {
  // HPF + AEC + NS + VAD + AGC: the whole capture path per 10 ms. The
  // transient suppressor is off by default (#243), as in libwebrtc M157's APM.
  e2e::CallScenario s;
  s.seconds = 4;
  s.noise_amp = 0.003f;
  auto c = e2e::MakeCall(s);
  HighPassFilter hpf;
  EchoCanceller aec;
  NoiseSuppressor ns;
  Vad vad;
  AutomaticGainControl agc;
  size_t off = 0;
  std::vector<float> f(160);
  for (auto _ : st) {
    std::copy(c.capture.begin() + static_cast<std::ptrdiff_t>(off), c.capture.begin() + static_cast<std::ptrdiff_t>(off + 160), f.begin());
    hpf.Process(f);
    aec.AnalyzeRender(std::span<const float>(c.far.data() + off, 160));
    aec.ProcessCapture(f);
    ns.Process(f);
    bool v = vad.Process(f);
    agc.Process(f, v);
    off = (off + 160) % (c.capture.size() - 160);
  }
  st.RequireRealtimeFactor(0.010, 15);
}

// --- Quality ------------------------------------------------------------------

SM_BENCH_QUALITY(audio, Q_EchoCanceller_full_duplex_call) {
  e2e::CallScenario s;
  s.seconds = 30;
  s.near_start_s = 15;
  s.noise_amp = 0.0005f;
  auto c = e2e::MakeCall(s);
  auto r = e2e::RunChain(c, {true, true, true, false, false});
  double erle = e2e::SegmentDb(c.echo, 6, 15) - e2e::SegmentDb(r.out, 6, 15, 16000, r.latency);
  double near = e2e::SegmentDb(r.out, 18, 30, 16000, r.latency) - e2e::SegmentDb(c.talker, 18, 30);
  st.Counter("erle_db", erle);
  st.Counter("double_talk_near_level_change_db", near);
  st.Require("erle_db", erle, ">", 25);
  st.Require("double_talk_near_level_change_db", near, ">", -6);
}

SM_BENCH_QUALITY(audio, Q_EchoDelay_route_change) {
  e2e::CallScenario s;
  s.seconds = 24;
  s.echo_delay2_ms = 180;
  s.near_start_s = 1e9;
  s.noise_amp = 0.0005f;
  auto c = e2e::MakeCall(s);
  auto r = e2e::RunChain(c, {true, true, false, false, false});
  double erle_after = e2e::SegmentDb(c.echo, 18, 24) - e2e::SegmentDb(r.out, 18, 24, 16000, r.latency);
  st.Counter("erle_after_change_db", erle_after);
  st.Require("erle_after_change_db", erle_after, ">", 20);
}

SM_BENCH_QUALITY(audio, Q_NoiseSuppressor_7dB_snr) {
  e2e::CallScenario s;
  s.far_talk = false;
  s.noise_amp = 0.03f;
  auto c = e2e::MakeCall(s);
  auto r = e2e::RunChain(c, {true, false, true, false, false});
  auto base = e2e::RunChain(c, {true, false, false, false, false});
  std::vector<float> ref = c.talker;
  HighPassFilter hp;
  for (size_t i = 0; i + 160 <= ref.size(); i += 160) hp.Process(std::span<float>(ref.data() + i, 160));
  size_t lat = r.latency, start = 3 * 16000, n = ref.size() - start - lat;
  double in = dsp::SnrDb(std::span<const float>(ref.data() + start, n), std::span<const float>(base.out.data() + start, n));
  double out = dsp::SnrDb(std::span<const float>(ref.data() + start, n), std::span<const float>(r.out.data() + start + lat, n));
  st.Counter("snr_in_db", in);
  st.Counter("snr_out_db", out);
  st.Require("snr_gain_db", out - in, ">", 4);
}
