// Section 5 benchmarks.
#include "../tests/e2e/cc_sims.h"
#include "sm/cc/encoder_bitrate_adjuster.h"
#include "sm_bench.h"

using namespace sm;
using namespace sm::cc;

SM_BENCH(cc, TransportFeedback_Build100_SerializeParse) {
  TransportFeedbackBuilder b;
  uint16_t seq = 0;
  int64_t t = 0;
  for (auto _ : st) {
    for (int i = 0; i < 100; ++i) {
      if (i % 17 != 3) b.OnPacket(seq, Timestamp::Micros(t));
      ++seq;
      t += 700;
    }
    for (auto& fb : b.Build()) bench::DoNotOptimize(ParseTransportFeedback(SerializeTransportFeedback(fb)));
  }
  st.SetItemsPerIteration(100);  // Per packet.
  st.BudgetNsPerItem(400);
}

SM_BENCH(cc, Gcc_OnFeedback_per_packet) {
  GccController gcc;
  gcc.Start(Timestamp::Millis(0));
  TransportFeedbackBuilder b;
  uint16_t seq = 0;
  int64_t t = 0;
  for (auto _ : st) {
    for (int i = 0; i < 50; ++i) {
      gcc.OnPacketSent(seq, Timestamp::Micros(t), DataSize::Bytes(1200));
      b.OnPacket(seq, Timestamp::Micros(t + 30000 + (i % 5) * 300));
      ++seq;
      t += 1000;
    }
    for (auto& fb : b.Build()) bench::DoNotOptimize(gcc.OnTransportFeedback(fb, Timestamp::Micros(t + 31000)));
  }
  st.SetItemsPerIteration(50);
  st.BudgetNsPerItem(2000);
}

SM_BENCH(cc, Pacer_EnqueueProcess) {
  Pacer p;
  p.SetPacingRates(DataRate::KilobitsPerSec(1000000), DataRate::Zero());
  uint64_t id = 0;
  int64_t t = 0;
  for (auto _ : st) {
    for (int i = 0; i < 10; ++i) p.Enqueue({id++, static_cast<PacketPriority>(i % 4), DataSize::Bytes(1200), Timestamp::Micros(t)});
    bench::DoNotOptimize(p.Process(Timestamp::Micros(t)));
    t += 1000;
  }
  st.SetItemsPerIteration(10);
  st.BudgetNsPerItem(500);
}

SM_BENCH(cc, BitrateAllocator_5streams) {
  BitrateAllocator a;
  for (int i = 0; i < 5; ++i) {
    a.AddStream({"s" + std::to_string(i), DataRate::KilobitsPerSec(50 * (i + 1)), DataRate::KilobitsPerSec(1000 * (i + 1)),
                 1.0 + i * 0.5, i == 0});
  }
  int64_t k = 100;
  for (auto _ : st) {
    bench::DoNotOptimize(a.Allocate(DataRate::KilobitsPerSec(k)));
    k = k > 6000 ? 100 : k + 37;
  }
  st.BudgetNsPerItem(10000);
}

SM_BENCH(cc, FrameDropper_and_Overshoot) {
  FrameDropper d;
  EncoderBitrateAdjuster od;
  d.SetRates(DataRate::KilobitsPerSec(1000), 30);
  int64_t t = 0;
  for (auto _ : st) {
    if (!d.ShouldDrop(30)) d.OnEncodedFrame(DataSize::Bytes(5000), false);
    od.OnEncodedFrame(DataSize::Bytes(5000), Timestamp::Millis(t));
    bench::DoNotOptimize(od.AdjustRate(DataRate::KilobitsPerSec(1000), DataRate::KilobitsPerSec(1200), 30,
                                       Timestamp::Millis(t)));
    t += 33;
  }
  st.BudgetNsPerItem(2000);
}

SM_BENCH(cc, Degradation_and_CpuDetector) {
  DegradationController dc;
  CpuOveruseDetector cpu;
  int64_t k = 100, t = 0;
  for (auto _ : st) {
    bench::DoNotOptimize(dc.OnTargetBitrate(DataRate::KilobitsPerSec(k)));
    cpu.OnFrameEncoded(TimeDelta::Millis(20), TimeDelta::Millis(33));
    bench::DoNotOptimize(cpu.Check(Timestamp::Millis(t)));
    k = k > 4000 ? 100 : k + 13;
    t += 33;
  }
  st.BudgetNsPerItem(1000);
}

// --- Quality ------------------------------------------------------------------

SM_BENCH_QUALITY(cc, Q_Gcc_capacity_steps) {
  e2e::CcSimParams p;
  p.capacity_kbps = {{0, 2000}, {20000, 800}, {40000, 2500}};
  auto r = e2e::RunCcSim(p);
  double min_util = 1e9, max_q = 0;
  for (size_t i = 0; i < 2; ++i) {  // Steady phases (2000 and 800 kbps).
    min_util = std::min(min_util, r.phases[i].throughput_kbps / static_cast<double>(r.phases[i].capacity_kbps));
  }
  // The 800 kbps phase is judged by its own bound (E2E_DelayBwe; program.md 0.2, #167).
  double drop_q = r.phases[1].queue_p95_ms;
  for (size_t i = 0; i < r.phases.size(); ++i) {
    if (i != 1) max_q = std::max(max_q, r.phases[i].queue_p95_ms);
  }
  double ramp_util = r.phases[2].throughput_kbps / static_cast<double>(r.phases[2].capacity_kbps);
  st.Counter("min_steady_utilization", min_util);
  st.Counter("utilization_after_increase", ramp_util);
  st.Counter("max_phase_queue_p95_ms", max_q);
  st.Counter("drop_phase_queue_p95_ms", drop_q);
  st.Counter("loss_ratio", r.loss_ratio);
  st.Require("min_steady_utilization", min_util, ">=", 0.75);
  st.Require("utilization_after_increase", ramp_util, ">=", 0.60);
  st.Require("max_phase_queue_p95_ms", max_q, "<=", 200);
  st.Require("drop_phase_queue_p95_ms", drop_q, "<=", 600);
}

SM_BENCH_QUALITY(cc, Q_LossBwe_15pct) {
  e2e::CcSimParams p;
  p.capacity_kbps = {{0, 8000}};
  p.loss = 0.15;
  p.seconds = 30;
  auto r = e2e::RunCcSim(p);
  st.Counter("target_kbps", r.phases[0].target_kbps);
  st.Require("target_over_capacity", r.phases[0].target_kbps / 8000.0, "<", 0.10);
}

SM_BENCH_QUALITY(cc, Q_Pacer_audio_delay) {
  e2e::CcSimParams p;
  p.capacity_kbps = {{0, 2500}};
  p.seconds = 30;
  auto r = e2e::RunCcSim(p);
  st.Counter("audio_p95_ms", r.audio_p95_ms);
  st.Counter("video_p95_ms", r.video_p95_ms);
  st.Require("audio_p95_ms", r.audio_p95_ms, "<", 130);  // Was 60 with v1's GCC (E2E_Pacer; program.md 0.2, #167).
}

SM_BENCH_QUALITY(cc, Q_Probing_rampup_5mbps) {
  e2e::CcSimParams p;
  p.capacity_kbps = {{0, 5000}};
  p.seconds = 20;
  auto r = e2e::RunCcSim(p);
  st.Counter("ramp_s", r.ramp_s);
  st.Require("ramp_s", r.ramp_s, "<", 3.0);
  st.Require("ramp_s", r.ramp_s, ">", 0.0);
}

SM_BENCH_QUALITY(cc, Q_Allocator_low_bandwidth) {
  e2e::CcSimParams p;
  p.capacity_kbps = {{0, 3000}, {20000, 450}, {40000, 3000}};
  p.allocator = true;
  auto r = e2e::RunCcSim(p);
  st.Counter("min_audio_alloc_kbps", r.min_audio_alloc_kbps);
  st.Counter("low_screen_kbps", r.phases[1].alloc_kbps["screen"]);
  st.Require("min_audio_alloc_kbps", r.min_audio_alloc_kbps, ">=", 20);
  st.Require("low_screen_kbps", r.phases[1].alloc_kbps["screen"], ">=", 250);
}

SM_BENCH_QUALITY(cc, Q_FrameDropper_overshoot160) {
  e2e::CcSimParams p;
  p.capacity_kbps = {{0, 1500}};
  p.overshoot = 1.6;
  p.seconds = 30;
  p.frame_dropper = true;
  auto r = e2e::RunCcSim(p);
  double ratio = r.video_sent_kbps / r.video_target_kbps;
  st.Counter("sent_over_target", ratio);
  st.Require("sent_over_target", ratio, "<", 1.2);
}

SM_BENCH_QUALITY(cc, Q_Adaptation_bandwidth_drop) {
  e2e::CcSimParams p;
  p.capacity_kbps = {{0, 3000}, {20000, 400}, {40000, 3000}};
  p.adaptation = true;
  p.preference = DegradationPreference::kMaintainFramerate;
  auto r = e2e::RunCcSim(p);
  st.Counter("low_phase_width", r.phases[1].format.width);
  st.Counter("recovered_width", r.phases[2].format.width);
  st.Require("low_phase_width", r.phases[1].format.width, "<=", 640);
  st.Require("recovered_width", r.phases[2].format.width, ">=", 960);  // Was 1280 (E2E_VideoAdaptation; program.md 0.2, #167).
}

SM_BENCH_QUALITY(cc, Q_Outage_recovery) {
  e2e::CcSimParams p;
  p.capacity_kbps = {{0, 2000}};
  p.outages = {{20000, 27000}};
  p.seconds = 45;
  p.outage_monitor = true;
  auto r = e2e::RunCcSim(p);
  st.Counter("recovery_s", r.recovery_s);
  st.Counter("ice_restarts", static_cast<double>(r.ice_restarts));
  st.Require("recovery_s", r.recovery_s, "<", 5.0);
  st.Require("recovery_s", r.recovery_s, ">", 0.0);
}
