// Section 4 benchmarks.
#include "../tests/e2e/timing_sims.h"
#include "sm_bench.h"

using namespace sm;
using namespace sm::timing;

SM_BENCH(timing, PacketBuffer_InsertAssemble) {
  PacketBuffer pb;
  RtpPacket p;
  p.payload.assign(1000, 1);
  uint16_t seq = 0;
  uint32_t ts = 0;
  for (auto _ : st) {
    int i = seq % 5;
    p.seq = seq++;
    p.timestamp = ts;
    p.marker = i == 4;
    pb.Insert(p, i == 0, Timestamp::Millis(seq));
    if (p.marker) {
      ts += 3000;
      bench::DoNotOptimize(pb.PopCompleteFrames());
    }
  }
  st.BudgetNsPerItem(1500);
}

SM_BENCH(timing, ReceiveStatistics_OnPacket) {
  ReceiveStatistics rs(1, 90000);
  uint16_t seq = 0;
  for (auto _ : st) {
    rs.OnPacket(seq, seq * 3000u, Timestamp::Millis(seq * 33));
    ++seq;
  }
  bench::DoNotOptimize(rs.Snapshot());
  st.BudgetNsPerItem(100);
}

SM_BENCH(timing, DelayManager_Update) {
  DelayManager dm;
  Random rng(1);
  int i = 0;
  for (auto _ : st) {
    dm.Update(static_cast<uint32_t>(i * 320), 16000, Timestamp::Micros(i * 20000 + static_cast<int64_t>(rng.Uniform(0, 30000))));
    ++i;
  }
  st.BudgetNsPerItem(2000);
}

SM_BENCH(timing, TimeStretch_Accelerate30ms_16k) {
  TimeStretcher ts;
  auto x = sim::SpeechLike(16000, 0.03, 3, false);
  std::vector<float> out;
  for (auto _ : st) bench::DoNotOptimize(ts.Accelerate(x, out));
  // Called at most once per 20 ms packet.
  st.RequireRealtimeFactor(0.020, 50);
}

SM_BENCH(timing, AudioJitterBuffer_10msPull_16k) {
  AudioJitterBuffer jb;
  auto x = sim::SpeechLike(16000, 2.0, 1, true);
  std::vector<float> out(160);
  int64_t t = 0;
  for (auto _ : st) {
    if (t % 20 == 0) {
      int i = static_cast<int>((t / 20) % 90);
      std::vector<float> pcm(x.begin() + i * 320, x.begin() + (i + 1) * 320);
      jb.InsertPacket(static_cast<uint32_t>(t / 20 * 320), std::move(pcm), Timestamp::Millis(t + (t % 60)));
    }
    jb.GetAudio(out, Timestamp::Millis(t));
    t += 10;
  }
  st.RequireRealtimeFactor(0.010, 100);
}

SM_BENCH(timing, VideoJitterEstimator_Update) {
  VideoJitterEstimator e;
  Random rng(1);
  for (auto _ : st) e.Update(TimeDelta::MillisF(rng.Gaussian(0, 5)), DataSize::Bytes(static_cast<int64_t>(rng.Uniform(3000, 6000))));
  st.BudgetNsPerItem(500);
}

SM_BENCH(timing, DriftCompensator_10ms_16k) {
  DriftCompensator c;
  c.SetRatio(1.0003);
  std::vector<float> in(160, 0.1f), out;
  out.reserve(400);
  for (auto _ : st) {
    out.clear();
    c.Process(in, out);
    bench::DoNotOptimize(out);
  }
  st.RequireRealtimeFactor(0.010, 200);
}

// --- Quality ------------------------------------------------------------------

SM_BENCH_QUALITY(timing, Q_AudioJitterBuffer_jitter25_loss2) {
  e2e::AudioJbSimParams p;
  p.jitter = TimeDelta::Millis(25);
  auto r = e2e::RunAudioJbSim(p);
  st.Counter("concealment_ratio", r.concealment_ratio);
  st.Counter("mean_buffer_delay_ms", r.mean_buffer_delay_ms);
  st.Counter("p95_buffer_delay_ms", r.p95_buffer_delay_ms);
  st.Require("concealment_ratio", r.concealment_ratio, "<=", 0.05);
  st.Require("mean_buffer_delay_ms", r.mean_buffer_delay_ms, "<=", 120);
}

SM_BENCH_QUALITY(timing, Q_TimeStretch_stall600ms) {
  e2e::AudioJbSimParams p;
  p.jitter = TimeDelta::Millis(3);
  p.loss = 0;
  p.seconds = 30;
  p.stall_at = TimeDelta::Seconds(10);
  p.stall_for = TimeDelta::Millis(600);
  auto r = e2e::RunAudioJbSim(p);
  st.Counter("recover_s", r.recover_s);
  st.Counter("removed_ms", r.removed_ms);
  st.Require("recover_s", r.recover_s, "<=", 8);
  st.Require("recover_s", r.recover_s, ">", 0);
}

SM_BENCH_QUALITY(timing, Q_VideoJitter_late_frames) {
  auto r = e2e::RunVideoJitterSim(true, TimeDelta::Millis(10), 4);
  st.Counter("late_ratio", r.late_ratio);
  st.Counter("mean_delay_ms", r.mean_delay_ms);
  st.Require("late_ratio", r.late_ratio, "<=", 0.03);
}

SM_BENCH_QUALITY(timing, Q_ClockDrift_300ppm) {
  auto r = e2e::RunDriftSim(300, true, 120, 7);
  st.Counter("estimated_ppm", r.estimated_ppm);
  st.Counter("max_level_error_ms", r.max_level_error_ms);
  st.Require("ppm_error", std::fabs(r.estimated_ppm - 300), "<=", 30);
  st.Require("max_level_error_ms", r.max_level_error_ms, "<=", 15);
}
