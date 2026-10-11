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
