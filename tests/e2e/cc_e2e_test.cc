// Section 5 end-to-end tests (closed-loop congestion control simulations).
#include <cmath>
#include <cstdlib>
#include <set>

#include "cc_sims.h"
#include "sm/cc/layer_selector.h"
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

SM_TEST(E2E_TransportFeedback, EveryPacketReportedOnceWithinQuantization) {
  sim::LinkConfig f;
  f.base_delay = TimeDelta::Millis(25);
  f.jitter_stddev = TimeDelta::Millis(3);
  f.loss_rate = 0.05;
  sim::NetworkLink<uint16_t> fwd(f, 1);
  sim::NetworkLink<std::vector<uint8_t>> rev(sim::LinkConfig{}, 2);
  TransportFeedbackBuilder builder;
  SendHistory history;
  std::map<int64_t, Timestamp> true_arrival;
  std::map<int64_t, int> reported;
  int64_t max_err_us = 0;
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
      for (const auto& r : history.OnFeedback(*fb, now)) {
        ++reported[r.seq];
        if (r.received()) max_err_us = std::max<int64_t>(max_err_us, std::llabs(r.receive_time.us() - true_arrival[r.seq].us()));
      }
    });
  }
  int dup = 0;
  for (auto& [s, n] : reported) dup += n > 1;
  SM_RECORD("reported_ratio", static_cast<double>(reported.size()) / seq);
  SM_RECORD("max_arrival_error_us", max_err_us);
  SM_EXPECT_EQ(dup, 0);
  SM_EXPECT_GE(static_cast<double>(reported.size()) / seq, 0.99);
  SM_EXPECT_LE(max_err_us, 250);
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
    SM_EXPECT_LE(ph.queue_p95_ms, 200);
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

SM_TEST(E2E_Pacer, SmoothsBurstsAndProtectsAudio) {
  CcSimParams p;
  p.capacity_kbps = {{0, 2500}};
  p.seconds = 30;
  auto paced = RunCcSim(p);
  p.use_pacer = false;
  auto burst = RunCcSim(p);
  SM_RECORD("paced_audio_p95_ms", paced.audio_p95_ms);
  SM_RECORD("unpaced_audio_p95_ms", burst.audio_p95_ms);
  SM_RECORD("paced_queue_p95_ms", paced.queue_p95_ms);
  SM_RECORD("unpaced_queue_p95_ms", burst.queue_p95_ms);
  SM_EXPECT_LT(paced.audio_p95_ms, burst.audio_p95_ms);
  SM_EXPECT_LT(paced.audio_p95_ms, 60);
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

SM_TEST(E2E_VideoAdaptation, ResolutionFollowsBandwidth) {
  CcSimParams p;
  p.capacity_kbps = {{0, 3000}, {20000, 400}, {40000, 3000}};
  p.adaptation = true;
  p.preference = DegradationPreference::kMaintainFramerate;
  auto r = RunCcSim(p);
  SM_RECORD("phase0_width", r.phases[0].format.width);
  SM_RECORD("phase1_width", r.phases[1].format.width);
  SM_RECORD("phase2_width", r.phases[2].format.width);
  SM_EXPECT_EQ(r.phases[0].format.width, 1280);
  SM_EXPECT_LE(r.phases[1].format.width, 640);
  SM_EXPECT_EQ(r.phases[1].format.fps, 30.0);
  SM_EXPECT_EQ(r.phases[2].format.width, 1280);
}

SM_TEST(E2E_VideoAdaptation, CpuOveruseConvergesToSustainableResolution) {
  // A slow device: 40 ms of encode per megapixel per frame.
  DegradationConfig dc;
  dc.preference = DegradationPreference::kMaintainFramerate;
  DegradationController deg(dc);
  CpuOveruseDetector cpu;
  deg.OnTargetBitrate(DataRate::KilobitsPerSec(5000));
  int changes = 0;
  VideoFormat prev = deg.current();
  for (int64_t t = 0; t < 120000; t += 33) {
    VideoFormat f = deg.current();
    double enc_ms = static_cast<double>(f.pixels()) / 1e6 * 40.0;
    cpu.OnFrameEncoded(TimeDelta::MillisF(enc_ms), TimeDelta::Millis(33));
    auto sig = cpu.Check(Timestamp::Millis(t));
    if (sig == CpuOveruseDetector::Signal::kOveruse) deg.OnResourceOveruse();
    if (sig == CpuOveruseDetector::Signal::kUnderuse) deg.OnResourceUnderuse();
    if (!(deg.current() == prev)) ++changes;
    prev = deg.current();
  }
  SM_RECORD("cpu_final_width", deg.current().width);
  SM_RECORD("cpu_final_usage", cpu.usage());
  SM_RECORD("cpu_format_changes", changes);
  SM_EXPECT_LT(deg.current().width, 1280);
  SM_EXPECT_LT(cpu.usage(), 0.85);
  SM_EXPECT_LE(changes, 6);
}

SM_TEST(E2E_LayerSelector, SfuForwardsDecodableStreamWithinBandwidth) {
  // Three simulcast encodings, each L1T3.
  std::vector<LayerOption> opts;
  const double rates[3] = {150, 500, 1500};
  const double tl_share[3] = {0.4, 0.6, 1.0};
  for (int s = 0; s < 3; ++s) {
    for (int tl = 0; tl < 3; ++tl) {
      opts.push_back({s, tl, DataRate::KilobitsPerSec(static_cast<int64_t>(rates[s] * tl_share[tl]))});
    }
  }
  std::sort(opts.begin(), opts.end(), [](auto& a, auto& b) { return a.bitrate < b.bitrate; });
  LayerSelector sel(opts);
  L1T3 sched[3];
  std::set<int64_t> decoded;  // Receiver-side frame ids that decoded.
  size_t decodable = 0;
  Random rng(3);
  bool pending_key[3] = {false, false, false};
  int64_t forwarded_bits_window = 0, over_budget_seconds = 0, seconds = 0;
  int64_t rx_id = 0;
  std::map<std::pair<int, int64_t>, int64_t> id_map;  // (spatial, sender id) -> receiver id
  uint64_t undecodable = 0;
  for (int64_t t = 0; t < 120000; t += 33) {
    Timestamp now = Timestamp::Millis(t);
    // Receiver bandwidth: 2 Mbps, drops to 700 kbps for 40 s, then 300 kbps.
    double bw = t < 40000 ? 2000 : (t < 80000 ? 700 : 300);
    bw *= rng.Uniform(0.9, 1.1);
    sel.OnBandwidth(DataRate::KilobitsPerSec(static_cast<int64_t>(bw)), now);
    if (sel.TakeKeyframeRequest()) pending_key[sel.target().spatial] = true;
    for (int s = 0; s < 3; ++s) {
      auto f = sched[s].Next(pending_key[s]);
      pending_key[s] = false;
      double bits = rates[s] * 1000 / 30.0 * (f.key ? 3 : 1);
      if (sel.ShouldForward(s, f.tid, f.key, f.sync)) {
        forwarded_bits_window += static_cast<int64_t>(bits);
        // Remap ids into one receiver-side stream; every reference must
        // have been forwarded and decoded.
        bool ok = true;
        for (int64_t ref : f.refs) {
          auto it = id_map.find({s, ref});
          if (it == id_map.end() || !decoded.count(it->second)) {
            ++undecodable;
            ok = false;
          }
        }
        if (ok) {
          decoded.insert(rx_id);
          ++decodable;
        }
        id_map[{s, f.id}] = rx_id++;
      }
    }
    if (t % 1000 < 33) {
      ++seconds;
      if (forwarded_bits_window > bw * 1000 * 1.3) ++over_budget_seconds;
      forwarded_bits_window = 0;
    }
  }

  SM_RECORD("sfu_switches_up", sel.stats().switches_up);
  SM_RECORD("sfu_switches_down", sel.stats().switches_down);
  SM_RECORD("sfu_over_budget_seconds", over_budget_seconds);
  SM_RECORD("sfu_decodable_ratio", static_cast<double>(decodable) / static_cast<double>(rx_id));
  SM_EXPECT_EQ(undecodable, 0u);
  SM_EXPECT_EQ(decodable, static_cast<size_t>(rx_id));
  SM_EXPECT_LE(over_budget_seconds, seconds / 20);
  SM_EXPECT_LE(sel.stats().switches_up + sel.stats().switches_down, 20u);
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
