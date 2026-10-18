#include "sm/cc/delay_based_bwe.h"
#include "sm/cc/loss_based_bwe.h"
#include "sm/cc/send_side_bwe.h"
#include "sm/core/event_log.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::cc;

namespace {
// Results for packets sent every 10 ms; arrival delay grows by `slope_ms`
// per packet.
std::vector<PacketResult> Results(int64_t first, int n, double slope_ms, int64_t start_ms = 0, int64_t size = 1200) {
  std::vector<PacketResult> v;
  for (int i = 0; i < n; ++i) {
    PacketResult r;
    r.seq = first + i;
    r.send_time = Timestamp::Millis(start_ms + (first + i) * 10);
    r.receive_time = r.send_time + TimeDelta::MillisF(50 + slope_ms * static_cast<double>(first + i));
    r.size = DataSize::Bytes(size);
    v.push_back(r);
  }
  return v;
}
}  // namespace

SM_TEST(DelayBwe, InterArrivalGroupsBursts) {
  InterArrival ia;
  // Two packets in one 5 ms burst, then a new group.
  SM_EXPECT_FALSE(ia.OnPacket(Timestamp::Millis(0), Timestamp::Millis(50), DataSize::Bytes(100)).has_value());
  SM_EXPECT_FALSE(ia.OnPacket(Timestamp::Millis(2), Timestamp::Millis(52), DataSize::Bytes(100)).has_value());
  SM_EXPECT_FALSE(ia.OnPacket(Timestamp::Millis(20), Timestamp::Millis(75), DataSize::Bytes(100)).has_value());
  auto d = ia.OnPacket(Timestamp::Millis(40), Timestamp::Millis(95), DataSize::Bytes(100));
  SM_ASSERT_TRUE(d.has_value());
  SM_EXPECT_EQ(d->send_delta.ms(), 18);  // 20 - 2
  SM_EXPECT_EQ(d->recv_delta.ms(), 23);  // 75 - 52
  SM_EXPECT_EQ(d->size_delta, -100);
}

SM_TEST(DelayBwe, TrendlineDetectsOveruseAndUnderuse) {
  TrendlineEstimator flat, up, down;
  for (int i = 0; i < 100; ++i) {
    flat.Update(10, 10, i * 10.0);
    up.Update(12, 10, i * 10.0);    // Queue grows 2 ms per 10 ms.
    down.Update(8, 10, i * 10.0);
  }
  SM_EXPECT_TRUE(flat.State() == BandwidthUsage::kNormal);
  SM_EXPECT_TRUE(up.State() == BandwidthUsage::kOverusing);
  SM_EXPECT_TRUE(down.State() == BandwidthUsage::kUnderusing);
  SM_EXPECT_NEAR(up.trend(), 0.2, 0.05);
}

SM_TEST(DelayBwe, AdaptiveThresholdBounds) {
  TrendlineEstimator t;
  for (int i = 0; i < 2000; ++i) t.Update(10 + (i % 2 ? 0.3 : -0.3), 10, i * 10.0);
  SM_EXPECT_GE(t.threshold(), 6.0);
  SM_EXPECT_LE(t.threshold(), 600.0);
}

SM_TEST(DelayBwe, AimdIncreasesThenDecreasesToAcked) {
  AimdRateController a;
  a.SetEstimate(DataRate::KilobitsPerSec(500), Timestamp::Millis(0));
  DataRate r;
  for (int i = 1; i <= 10; ++i) r = a.Update(BandwidthUsage::kNormal, std::nullopt, Timestamp::Millis(i * 100));
  // ~8%/s multiplicative for 1 s.
  SM_EXPECT_NEAR(r.kbps(), 540, 10);
  r = a.Update(BandwidthUsage::kOverusing, DataRate::KilobitsPerSec(400), Timestamp::Millis(1100));
  SM_EXPECT_NEAR(r.kbps(), 340, 1);  // 0.85 x acked
  SM_EXPECT_TRUE(a.link_capacity_kbps().has_value());
  // A second overuse within one RTT does not reduce again.
  a.SetRtt(TimeDelta::Millis(100));
  r = a.Update(BandwidthUsage::kOverusing, DataRate::KilobitsPerSec(390), Timestamp::Millis(1150));
  SM_EXPECT_NEAR(r.kbps(), 340, 1);
  // Near the link capacity estimate: additive increase, about one packet
  // per response time (RTT + 100 ms): 1200 B * 8 / 0.2 s = 48 kbps per s.
  DataRate before = a.Update(BandwidthUsage::kNormal, DataRate::KilobitsPerSec(340), Timestamp::Millis(1200));
  DataRate after = a.Update(BandwidthUsage::kNormal, DataRate::KilobitsPerSec(340), Timestamp::Millis(2200));
  SM_EXPECT_NEAR((after - before).kbps(), 48, 2);
}

SM_TEST(DelayBwe, EstimatorReactsToQueueBuildUp) {
  EventLog log;
  DelayBasedBwe::Config cfg;
  cfg.events = &log;
  DelayBasedBwe bwe(cfg);
  bwe.SetStartBitrate(DataRate::KilobitsPerSec(1000), Timestamp::Millis(0));
  DataRate r;
  for (int k = 0; k < 20; ++k) {
    auto res = Results(k * 10, 10, 0.0);
    r = bwe.OnPacketResults(res, DataRate::KilobitsPerSec(960), std::nullopt, res.back().receive_time);
  }
  SM_EXPECT_GE(r.kbps(), 1000);
  for (int k = 20; k < 40; ++k) {
    auto res = Results(k * 10, 10, 1.5);
    r = bwe.OnPacketResults(res, DataRate::KilobitsPerSec(960), std::nullopt, res.back().receive_time);
  }
  SM_EXPECT_LT(r.kbps(), 1000);
  SM_EXPECT_GE(bwe.stats().overuse_signals, 1u);
  SM_EXPECT_GE(log.Count("gcc", "decrease"), 1u);
}

SM_TEST(LossBwe, ThreeRegimes) {
  auto make = [](int lost_every, int n) {
    std::vector<PacketResult> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
      v[static_cast<size_t>(i)].seq = i;
      if (lost_every == 0 || i % lost_every != 0) v[static_cast<size_t>(i)].receive_time = Timestamp::Millis(i);
    }
    return v;
  };
  const DataRate cur = DataRate::KilobitsPerSec(1000);
  LossBasedBwe low, mid, high;
  for (auto* b : {&low, &mid, &high}) b->OnPacketResults({}, cur, Timestamp::Seconds(9));  // Open window.
  low.OnPacketResults(make(0, 100), cur, Timestamp::Seconds(10));
  mid.OnPacketResults(make(20, 100), cur, Timestamp::Seconds(10));   // 5%
  high.OnPacketResults(make(4, 100), cur, Timestamp::Seconds(10));   // 25%
  SM_EXPECT_FALSE(low.limiting());
  SM_EXPECT_EQ(mid.estimate().kbps(), 1000.0);
  SM_EXPECT_NEAR(high.estimate().kbps(), 875, 1);
  // Measurement windows are at least 1 s: a window that is too short does
  // not produce a new decision.
  high.OnPacketResults(make(2, 100), DataRate::KilobitsPerSec(875), Timestamp::Millis(10500));
  SM_EXPECT_NEAR(high.estimate().kbps(), 875, 1);
  high.OnPacketResults({}, DataRate::KilobitsPerSec(875), Timestamp::Millis(11100));
  SM_EXPECT_LT(high.estimate().kbps(), 875);
}

SM_TEST(SendSideBwe, StartProbesAndRembCap) {
  GccController gcc;
  auto u = gcc.Start(Timestamp::Millis(0));
  SM_EXPECT_EQ(u.target_rate.kbps(), 300.0);
  SM_EXPECT_EQ(u.pacing_rate.kbps(), 750.0);
  SM_ASSERT_EQ(u.probes.size(), 2u);
  SM_EXPECT_EQ(u.probes[0].rate.kbps(), 900.0);
  SM_EXPECT_EQ(u.probes[1].rate.kbps(), 1800.0);
  // Feedback for an un-congested stream raises the target; REMB caps it.
  TransportFeedbackBuilder fb;
  for (int i = 0; i < 400; ++i) {
    gcc.OnPacketSent(static_cast<uint16_t>(i), Timestamp::Millis(i * 5), DataSize::Bytes(1000));
    fb.OnPacket(static_cast<uint16_t>(i), Timestamp::Millis(i * 5 + 30));
    if (i % 10 == 9) {
      for (auto& f : fb.Build()) u = gcc.OnTransportFeedback(f, Timestamp::Millis(i * 5 + 60));
    }
  }
  SM_EXPECT_GT(u.target_rate.kbps(), 300.0);
  gcc.OnRemb(DataRate::KilobitsPerSec(200));
  for (int i = 400; i < 420; ++i) {
    gcc.OnPacketSent(static_cast<uint16_t>(i), Timestamp::Millis(i * 5), DataSize::Bytes(1000));
    fb.OnPacket(static_cast<uint16_t>(i), Timestamp::Millis(i * 5 + 30));
  }
  for (auto& f : fb.Build()) u = gcc.OnTransportFeedback(f, Timestamp::Millis(2200));
  SM_EXPECT_EQ(u.target_rate.kbps(), 200.0);
  auto m = CollectMetrics(gcc);
  SM_EXPECT_EQ(m["target_kbps"], 200.0);
}

// --- cc v2: libwebrtc InterArrivalDelta (inter_arrival_delta_unittest-style cases) ---

namespace {
struct Pkt { int64_t send, arrival; };
// Feeds packets, returns the deltas produced at each call (nullopt if none).
std::optional<InterArrivalDelta::Deltas> Feed(InterArrivalDelta& ia, Pkt p, size_t size = 100) {
  return ia.ComputeDeltas(Timestamp::Millis(p.send), Timestamp::Millis(p.arrival), Timestamp::Millis(p.arrival), size);
}
}  // namespace

SM_TEST(DelayBweV2, InterArrivalDeltaGroupsAndDeltas) {
  InterArrivalDelta ia(TimeDelta::Millis(5));
  SM_EXPECT_FALSE(Feed(ia, {0, 17}).has_value());
  SM_EXPECT_FALSE(Feed(ia, {2, 19}).has_value());   // Same group.
  SM_EXPECT_FALSE(Feed(ia, {20, 40}).has_value());  // Second group starts: one complete group only.
  auto d = Feed(ia, {40, 62});                      // Third group: deltas of group 2 vs 1.
  SM_ASSERT_TRUE(d.has_value());
  SM_EXPECT_EQ(d->send_delta.ms(), 18);     // 20 - 2
  SM_EXPECT_EQ(d->arrival_delta.ms(), 21);  // 40 - 19
  SM_EXPECT_EQ(d->size_delta, -100);
}

SM_TEST(DelayBweV2, InterArrivalDeltaBurstStaysInGroup) {
  // Packets sent 10 ms apart that arrive back to back (a burst the link
  // released at once; propagation delta < 0, arrival delta <= 5 ms) belong
  // to the group, even though their send times are more than 5 ms apart.
  InterArrivalDelta ia(TimeDelta::Millis(5));
  SM_EXPECT_FALSE(Feed(ia, {0, 100}).has_value());
  SM_EXPECT_FALSE(Feed(ia, {10, 102}).has_value());  // arrival delta 2 < send delta 10: burst.
  SM_EXPECT_FALSE(Feed(ia, {20, 104}).has_value());
  SM_EXPECT_FALSE(Feed(ia, {30, 140}).has_value());  // arrival delta 36 > 5 ms: new group.
  auto d = Feed(ia, {60, 170});
  SM_ASSERT_TRUE(d.has_value());
  SM_EXPECT_EQ(d->send_delta.ms(), 10);      // Group {30} send 30 vs. group {0,10,20} send 20.
  SM_EXPECT_EQ(d->arrival_delta.ms(), 36);   // 140 - 104
}

SM_TEST(DelayBweV2, InterArrivalDeltaIgnoresReorderedPacket) {
  InterArrivalDelta ia(TimeDelta::Millis(5));
  Feed(ia, {100, 150});
  SM_EXPECT_FALSE(Feed(ia, {90, 152}).has_value());  // Send time before the group's first: dropped.
  Feed(ia, {120, 170});
  auto d = Feed(ia, {140, 190});
  SM_ASSERT_TRUE(d.has_value());
  SM_EXPECT_EQ(d->send_delta.ms(), 20);
  SM_EXPECT_EQ(d->arrival_delta.ms(), 20);
}

SM_TEST(DelayBweV2, InterArrivalDeltaBurstIsCappedAt100ms) {
  // Back-to-back arrivals (arrival delta <= 5 ms, propagation delta < 0) only
  // extend a group for kMaxBurstDuration (100 ms) after its first arrival.
  InterArrivalDelta ia(TimeDelta::Millis(5));
  auto f = [&](int64_t send, int64_t arrival) {
    return ia.ComputeDeltas(Timestamp::Millis(send), Timestamp::Millis(arrival), Timestamp::Millis(arrival), 100);
  };
  f(0, 1000);
  for (int i = 1; i <= 24; ++i) f(i * 10, 1000 + i * 4);  // Arrives 4 ms apart, sent 10 ms apart: one burst.
  // Packet 25 arrives 100 ms after the group's first arrival: no longer a burst member,
  // so it starts the second group (still no delta); packet 26 starts the third.
  SM_EXPECT_FALSE(f(250, 1100).has_value());
  auto d = f(500, 1300);
  SM_ASSERT_TRUE(d.has_value());
  SM_EXPECT_EQ(d->send_delta.ms(), 10);      // {250} vs {240}.
  SM_EXPECT_EQ(d->arrival_delta.ms(), 4);    // 1100 - 1096
}

SM_TEST(DelayBweV2, InterArrivalDeltaResetsOnClockOffsetJump) {
  InterArrivalDelta ia(TimeDelta::Millis(5));
  auto f = [&](int64_t send, int64_t arrival, int64_t sys) {
    return ia.ComputeDeltas(Timestamp::Millis(send), Timestamp::Millis(arrival), Timestamp::Millis(sys), 100);
  };
  f(0, 100, 100);
  f(20, 120, 120);
  SM_EXPECT_TRUE(f(40, 140, 140).has_value());
  f(60, 160 + 5000, 160);                         // Arrival clock jumped 5 s against the system clock.
  SM_EXPECT_FALSE(f(80, 180 + 5000, 180).has_value());  // Reset: nothing yet.
}

SM_TEST(DelayBweV2, JitterAndReorderingReadAsOveruseLessOften) {
  // 40 ms of uniform jitter on a link with no queue: packets sent every 5 ms
  // arrive out of order. A feedback every 50 ms reports the packets that have
  // arrived by then (as the transport-cc generator does), in sequence order.
  // v1 grouping reads the shuffle as delay growth; libwebrtc's InterArrivalDelta
  // fed in receive-time order rarely does (testbench high_jitter, seeds 1-5:
  // 17-28 overuse signals with v1, 2-5 with v2).
  auto run = [](int version) {
    DelayBasedBwe::Config cfg;
    cfg.version = version;
    DelayBasedBwe bwe(cfg);
    bwe.SetStartBitrate(DataRate::KilobitsPerSec(1000), Timestamp::Millis(0));
    uint32_t rng = 12345;
    auto rnd = [&] { rng = rng * 1664525u + 1013904223u; return static_cast<double>(rng >> 8) / 16777216.0; };
    std::vector<PacketResult> all;
    for (int64_t seq = 0; seq < 4000; ++seq) {
      PacketResult r;
      r.seq = seq;
      r.send_time = Timestamp::Millis(seq * 5);
      r.receive_time = r.send_time + TimeDelta::MillisF(30 + 40 * rnd());
      r.size = DataSize::Bytes(1200);
      all.push_back(r);
    }
    size_t reported_upto = 0;
    std::vector<bool> reported(all.size(), false);
    for (int64_t t = 100; t < 20000; t += 50) {
      std::vector<PacketResult> v;
      for (size_t i = reported_upto; i < all.size() && all[i].send_time.ms() <= t; ++i) {
        if (!reported[i] && all[i].receive_time.ms() <= t) {
          v.push_back(all[i]);
          reported[i] = true;
        }
      }
      while (reported_upto < all.size() && reported[reported_upto]) ++reported_upto;
      if (!v.empty()) bwe.OnPacketResults(v, DataRate::KilobitsPerSec(1000), std::nullopt, Timestamp::Millis(t));
    }
    return bwe.stats().overuse_signals;
  };
  uint64_t v1 = run(1), v2 = run(2);
  SM_EXPECT_GT(v1, v2);
  SM_EXPECT_LE(v2, 5u);
}

SM_TEST(DelayBweV2, StreamTimeoutResetsDetector) {
  DelayBasedBwe::Config cfg;
  cfg.version = 2;
  DelayBasedBwe bwe(cfg);
  bwe.SetStartBitrate(DataRate::KilobitsPerSec(1000), Timestamp::Millis(0));
  for (int k = 0; k < 10; ++k) {
    auto res = Results(k * 10, 10, 0.0);
    bwe.OnPacketResults(res, DataRate::KilobitsPerSec(960), std::nullopt, res.back().receive_time);
  }
  SM_EXPECT_EQ(bwe.stats().stream_resets, 0u);
  auto res = Results(500, 10, 0.0);
  bwe.OnPacketResults(res, DataRate::KilobitsPerSec(960), std::nullopt, res.back().receive_time);  // > 2 s later.
  SM_EXPECT_EQ(bwe.stats().stream_resets, 1u);
}

SM_TEST(DelayBweV2, FeedbackWithOnlyLostPacketsLeavesTheEstimate) {
  // libwebrtc IncomingPacketFeedbackVector: no received packet -> empty Result, no rate update.
  DelayBasedBwe::Config cfg;
  cfg.version = 2;
  DelayBasedBwe bwe(cfg);
  bwe.SetStartBitrate(DataRate::KilobitsPerSec(1000), Timestamp::Millis(0));
  std::vector<PacketResult> lost(5);
  for (size_t i = 0; i < lost.size(); ++i) {
    lost[i].seq = static_cast<int64_t>(i);
    lost[i].send_time = Timestamp::Millis(static_cast<int64_t>(i) * 10);
    lost[i].size = DataSize::Bytes(1200);
  }
  DataRate r = bwe.OnPacketResults(lost, DataRate::KilobitsPerSec(500), std::nullopt, Timestamp::Millis(1000));
  SM_EXPECT_EQ(r.bps(), 1000000);
}

// RtpTransportControllerSend::OnRttUpdate (#101): `cc v2` gives the controller
// the RTT rounded to whole milliseconds and drops a zero one.
SM_TEST(GccController, V2RoundsTheRttToWholeMillisecondsAndDropsZero) {
  GccConfig c2;
  c2.version = 2;
  GccController v2(c2);
  v2.OnRttUpdate(TimeDelta::Micros(49600));
  SM_EXPECT_EQ(v2.last_rtt().us(), 50000);
  v2.OnRttUpdate(TimeDelta::Micros(50400));
  SM_EXPECT_EQ(v2.last_rtt().us(), 50000);
  v2.OnRttUpdate(TimeDelta::Micros(300));  // Rounds to zero: not passed on.
  SM_EXPECT_EQ(v2.last_rtt().us(), 50000);
  GccController v1;
  v1.OnRttUpdate(TimeDelta::Micros(49600));
  SM_EXPECT_EQ(v1.last_rtt().us(), 49600);
}

// GoogCcNetworkController::GetPacingRates (#61 glue): 2.5 x the target until the
// first transport feedback, 1.1 x after it, never below the allocator's minimum.
SM_TEST(GccController, V2PacingRateFollowsGetPacingRates) {
  GccConfig c;
  c.version = 2;
  c.start_rate = DataRate::KilobitsPerSec(300);
  GccController g(c);
  g.Start(Timestamp::Millis(0));
  SM_EXPECT_EQ(g.PacingRate().bps(), 750000);
  g.SetPacingLimits(DataRate::KilobitsPerSec(400), DataRate::KilobitsPerSec(120));
  SM_EXPECT_EQ(g.PacingRate().bps(), 1000000);  // max(min allocatable, target) x 2.5.
  SM_EXPECT_EQ(g.PaddingRate().bps(), 120000);
  GccController v1;
  v1.Start(Timestamp::Millis(0));
  SM_EXPECT_EQ(v1.Start(Timestamp::Millis(0)).pacing_rate.bps(), 750000);  // cc v1 and the update keep 2.5 x.
}
