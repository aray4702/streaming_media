// Section 5 end-to-end tests (closed-loop congestion control simulations).
#include <cmath>
#include <cstdlib>
#include <set>

#include "cc_sims.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::cc;
using namespace sm::e2e;

namespace {
// Self-contained L1T3 encoder pattern (T0 T2 T1 T2), so the congestion-
// control tests do not depend on the loss section.
struct L1T3 {
  int64_t next = 0, idx = 0, last[3] = {-1, -1, -1};
  struct Frame {
    int64_t id;
    int tid;
    bool key, sync;
    std::vector<int64_t> refs;
  };
  Frame Next(bool key) {
    static constexpr int kPattern[4] = {0, 2, 1, 2};
    if (key || last[0] < 0) {
      key = true;
      idx = 0;
      last[0] = last[1] = last[2] = -1;
    }
    Frame f{next++, kPattern[idx++ % 4], key, true, {}};
    if (!key) {
      int64_t ref = f.tid == 2 ? std::max(last[0], last[1]) : last[0];
      f.refs = {ref};
      f.sync = f.tid < 2 || ref == last[0];
    }
    last[f.tid] = f.id;
    return f;
  }
};
}  // namespace

// libwebrtc's generator reports a reordered packet again once it arrives, so
// a packet first reported lost can come back received; it is never reported
// received twice (SendHistory drops a second report, as TransportFeedbackAdapter).
// The wire carries arrival times in 250 us ticks; SendHistory then hands the
// estimators each time since the feedback's base time rounded down to 1 ms, as
// TransportFeedbackAdapter::ProcessTransportFeedback does (issue #365), on the
// adapter's local time base (the first feedback's receive time, #569).
SM_TEST(E2E_TransportFeedback, EveryPacketReportedReceivedOnceWithinQuantization) {
  sim::LinkConfig f;
  f.base_delay = TimeDelta::Millis(25);
  f.jitter_stddev = TimeDelta::Millis(3);
  f.loss_rate = 0.05;
  sim::NetworkLink<uint16_t> fwd(f, 1);
  sim::NetworkLink<std::vector<uint8_t>> rev(sim::LinkConfig{}, 2);
  TransportFeedbackBuilder builder;
  SendHistory history;
  std::map<int64_t, Timestamp> true_arrival;
  std::map<int64_t, int> reported, received;
  int64_t max_err_us = 0;
  int not_rounded = 0;
  int64_t first_base_us = -1, first_feedback_us = 0;
  uint16_t seq = 0;
  for (int64_t t = 0; t < 30000; ++t) {
    Timestamp now = Timestamp::Millis(t);
    if (t % 4 == 0) {
      history.OnPacketSent(seq, now, DataSize::Bytes(1000));
      fwd.Send(now, seq, DataSize::Bytes(1000));
      ++seq;  // Wraps after 65536: 7500 packets here, fine either way.
    }
    fwd.ReceiveEach(now, [&](uint16_t s, Timestamp at) {
      builder.OnPacket(s, at);
      true_arrival[s] = at;
    });
    if (t % 50 == 0) {
      for (auto& fb : builder.Build()) rev.Send(now, SerializeTransportFeedback(fb), DataSize::Bytes(100));
    }
    rev.ReceiveEach(now, [&](std::vector<uint8_t> b, Timestamp) {
      auto fb = ParseTransportFeedback(b);
      std::map<uint16_t, Timestamp> wire;
      for (const auto& [s, at] : fb->ArrivalTimes()) {
        wire[s] = at;
        max_err_us = std::max<int64_t>(max_err_us, std::llabs(at.us() - true_arrival[s].us()));
      }
      // The adapter's local time base: the first feedback's receive time, then each base-time delta.
      const int64_t base_us = static_cast<int64_t>(fb->reference_time) * 64000;
      if (first_base_us < 0) {
        first_base_us = base_us;
        first_feedback_us = now.us();
      }
      const int64_t offset_us = first_feedback_us + (base_us - first_base_us);
      for (const auto& r : history.OnFeedback(*fb, now)) {
        ++reported[r.seq];
        if (!r.received()) continue;
        ++received[r.seq];
        const int64_t since_base_us = wire.at(static_cast<uint16_t>(r.seq)).us() - base_us;
        not_rounded += r.receive_time.us() != offset_us + since_base_us / 1000 * 1000;
      }
    });
  }
  int dup = 0;
  for (auto& [s, n] : received) dup += n > 1;
  SM_RECORD("reported_ratio", static_cast<double>(reported.size()) / seq);
  SM_RECORD("max_arrival_error_us", max_err_us);
  SM_EXPECT_EQ(dup, 0);
  SM_EXPECT_GE(static_cast<double>(reported.size()) / seq, 0.99);
  SM_EXPECT_LE(max_err_us, 250);
  SM_EXPECT_EQ(not_rounded, 0);
}

SM_TEST(E2E_DelayBwe, TracksCapacityStepsWithLowQueueing) {
  CcSimParams p;
  p.capacity_kbps = {{0, 2000}, {20000, 800}, {40000, 2500}};
  auto r = RunCcSim(p);
  for (size_t i = 0; i < r.phases.size(); ++i) {
    const auto& ph = r.phases[i];
    std::string k = "phase" + std::to_string(i) + "_";
    SM_RECORD((k + "capacity_kbps").c_str(), ph.capacity_kbps);
    SM_RECORD((k + "throughput_kbps").c_str(), ph.throughput_kbps);
    SM_RECORD((k + "target_kbps").c_str(), ph.target_kbps);
    SM_RECORD((k + "queue_p95_ms").c_str(), ph.queue_p95_ms);
    // After a capacity increase GCC climbs multiplicatively (8%/s), so the
    // last phase (800 -> 2500 kbps) gets a lower bar than steady phases.
    SM_EXPECT_GE(ph.throughput_kbps, (i == 2 ? 0.60 : 0.75) * ph.capacity_kbps);
    SM_EXPECT_LE(ph.target_kbps, 1.10 * ph.capacity_kbps);
    // GoogCC drains the queue a 2.5x drop leaves slowly: after the decrease to
    // 0.85 x the acked rate it sends close to the new capacity (program.md 0.2,
    // #167: was 200 ms for every phase, v1's AIMD; GoogCC here 557 ms in the
    // 800 kbps phase). libwebrtc on the bench's capacity_drop (2.5 Mbps -> 500
    // kbps): queue p95 279 ms over the call and 14.3 s to settle (parity-data,
    // mean of 10 seeds; ours 282 ms, 14.3 s).
    // PR #381 (#365, 1 ms feedback receive times, as libwebrtc; program.md 0.2, Owner sign-off
    // 2026-10-25): was 600. Ours 636 here; on the bench's capacity_drop, 10 seeds, ours 302 ms
    // against libwebrtc's 277.
    SM_EXPECT_LE(ph.queue_p95_ms, i == 1 ? 700 : 200);
  }
  SM_RECORD("loss_ratio", r.loss_ratio);
  SM_EXPECT_LE(r.loss_ratio, 0.02);
}

SM_TEST(E2E_LossBwe, BacksOffOnHeavyLossGrowsOnLight) {
  CcSimParams heavy;
  heavy.capacity_kbps = {{0, 8000}};
  heavy.loss = 0.15;
  heavy.seconds = 30;
  auto h = RunCcSim(heavy);
  CcSimParams light = heavy;
  light.loss = 0.01;
  auto l = RunCcSim(light);
  SM_RECORD("target_kbps_loss15", h.phases[0].target_kbps);
  SM_RECORD("target_kbps_loss1", l.phases[0].target_kbps);
  // Heavy random loss drives the rate far below the (ample) capacity, while
  // light loss leaves the delay-based controller in charge.
  SM_EXPECT_LT(h.phases[0].target_kbps, 0.10 * 8000);
  SM_EXPECT_LT(h.phases[0].target_kbps, 0.15 * l.phases[0].target_kbps);
  SM_EXPECT_GT(l.phases[0].target_kbps, 2000);
}

SM_TEST(E2E_SendSideBwe, CombinesDelayAndLossControllers) {
  // Phases of 20 s on a 1.5 Mbps link: clean, 5% loss (GCC "hold" band),
  // 20% loss (decrease band). The phase boundaries are capacity entries
  // with the same capacity so per-phase stats are reported.
  CcSimParams p;
  p.capacity_kbps = {{0, 1500}, {20000, 1500}, {40000, 1500}};
  p.loss_schedule = {{20000, 0.05}, {40000, 0.20}};
  auto r = RunCcSim(p);
  SM_RECORD("clean_target_kbps", r.phases[0].target_kbps);
  SM_RECORD("loss5_target_kbps", r.phases[1].target_kbps);
  SM_RECORD("loss20_target_kbps", r.phases[2].target_kbps);
  SM_EXPECT_GE(r.phases[0].target_kbps, 0.75 * 1500);
  SM_EXPECT_LE(r.phases[0].target_kbps, 1.1 * 1500);
  // Moderate loss holds the rate (no collapse, no growth).
  SM_EXPECT_GE(r.phases[1].target_kbps, 0.85 * r.phases[0].target_kbps);
  SM_EXPECT_LE(r.phases[1].target_kbps, 1.05 * r.phases[0].target_kbps);
  // Heavy loss backs off.
  SM_EXPECT_LT(r.phases[2].target_kbps, 0.5 * r.phases[1].target_kbps);
}

// The pacer is libwebrtc's PacingController: it lets 40 ms of the pacing rate
// out in one burst and does not pace audio (pacing_controller_unittest.cc
// SendsPacketsInBurstImmediately, HighPrioDoesntAffectBudget), so frames of
// this size leave in one burst either way: the paced run is no better than the
// unpaced one, but never worse (program.md 0.2, signed off by the Owner on #80).
// With GoogCC (#167) the target sits close to the 2.5 Mbps capacity, so audio waits
// behind a standing link queue: 122 ms p95 here, 6.5 ms above the unpaced run (the
// bounds were < 60 ms and +1 ms with v1's GCC). libwebrtc
// on the bench's clean_link (2.5 Mbps) keeps a queue p95 of 74 ms (ours 81 ms).
SM_TEST(E2E_Pacer, LibwebrtcPacerDoesNotPaceAudio) {
  CcSimParams p;
  p.capacity_kbps = {{0, 2500}};
  p.seconds = 30;
  auto paced = RunCcSim(p);
  p.use_pacer = false;
  auto burst = RunCcSim(p);
  SM_RECORD("v2_paced_audio_p95_ms", paced.audio_p95_ms);
  SM_RECORD("v2_unpaced_audio_p95_ms", burst.audio_p95_ms);
  SM_EXPECT_LE(paced.audio_p95_ms, burst.audio_p95_ms + 10);
  SM_EXPECT_LT(paced.audio_p95_ms, 130);
}

SM_TEST(E2E_Probing, FastRampUp) {
  CcSimParams p;
  p.capacity_kbps = {{0, 5000}};
  p.seconds = 30;
  auto with = RunCcSim(p);
  p.probing = false;
  auto without = RunCcSim(p);
  SM_RECORD("ramp_s_probing", with.ramp_s);
  SM_RECORD("ramp_s_no_probing", without.ramp_s);
  SM_EXPECT_GT(with.ramp_s, 0.0);
  SM_EXPECT_LT(with.ramp_s, 3.0);
  SM_EXPECT_TRUE(without.ramp_s < 0 || without.ramp_s > 2 * with.ramp_s);
}

SM_TEST(E2E_BitrateAllocator, AudioProtectedVideoDegradesFirst) {
  CcSimParams p;
  p.capacity_kbps = {{0, 3000}, {20000, 450}, {40000, 3000}};
  p.allocator = true;
  auto r = RunCcSim(p);
  const auto& low = r.phases[1].alloc_kbps;
  SM_RECORD("min_audio_alloc_kbps", r.min_audio_alloc_kbps);
  SM_RECORD("low_phase_camera_kbps", low.at("camera"));
  SM_RECORD("low_phase_screen_kbps", low.at("screen"));
  SM_RECORD("high_phase_camera_kbps", r.phases[2].alloc_kbps.at("camera"));
  SM_EXPECT_GE(r.min_audio_alloc_kbps, 20.0);
  SM_EXPECT_GE(low.at("screen"), 250.0);         // Higher priority keeps running.
  SM_EXPECT_LT(low.at("camera"), low.at("screen"));
  SM_EXPECT_GT(r.phases[2].alloc_kbps.at("camera"), 300.0);  // Resumed.
}

SM_TEST(E2E_FrameDropper, ContainsEncoderOvershoot) {
  CcSimParams p;
  p.capacity_kbps = {{0, 1500}};
  p.overshoot = 1.6;
  p.seconds = 30;
  p.frame_dropper = true;
  auto with = RunCcSim(p);
  p.frame_dropper = false;
  auto without = RunCcSim(p);
  double ratio_with = with.video_sent_kbps / with.video_target_kbps;
  double ratio_without = without.video_sent_kbps / without.video_target_kbps;
  SM_RECORD("sent_over_target_with_dropper", ratio_with);
  SM_RECORD("sent_over_target_without", ratio_without);
  SM_RECORD("drop_ratio", static_cast<double>(with.frames_dropped) / with.frames);
  SM_EXPECT_LT(ratio_with, 1.2);
  SM_EXPECT_GT(ratio_without, 1.4);
}

SM_TEST(E2E_Outage, PausesDuringOutageAndRecovers) {
  CcSimParams p;
  p.capacity_kbps = {{0, 2000}};
  p.outages = {{20000, 27000}};
  p.seconds = 45;
  p.outage_monitor = true;
  auto with = RunCcSim(p);
  p.outage_monitor = false;
  auto without = RunCcSim(p);
  SM_RECORD("video_packets_while_down_monitor", with.video_packets_while_down);
  SM_RECORD("video_packets_while_down_no_monitor", without.video_packets_while_down);
  SM_RECORD("recovery_s", with.recovery_s);
  SM_RECORD("ice_restarts", with.ice_restarts);
  SM_EXPECT_LT(with.video_packets_while_down, without.video_packets_while_down / 2);
  SM_EXPECT_EQ(with.ice_restarts, 1u);
  SM_EXPECT_GT(with.recovery_s, 0.0);
  SM_EXPECT_LT(with.recovery_s, 5.0);
}
