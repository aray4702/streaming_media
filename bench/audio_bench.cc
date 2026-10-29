// Section 6 benchmarks. Audio blocks are gated by a real-time factor: how
// many times faster than real time a 10 ms frame is processed (at 16 kHz
// unless noted). The budget is scaled down on slow targets.
#include "../tests/e2e/audio_sims.h"

#include <algorithm>
#include "sm/audio/audio_level.h"
#include "sm/audio/splitting_filter.h"
#include "sm_bench.h"

using namespace sm;
using namespace sm::audio;

namespace {
std::vector<float> Speech() { return sim::SpeechLike(16000, 2.0, 1, true); }
}  // namespace

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
