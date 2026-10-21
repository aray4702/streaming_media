// Ports of libwebrtc's aimd_rate_control_unittest.cc, bitrate_estimator and
// delay_based_bwe decision logic (`cc v2`, #15).
#include "sm/cc/aimd_rate_control.h"

#include <algorithm>

#include "sm/cc/delay_based_bwe.h"
#include "sm/cc/send_side_bwe.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::cc;

namespace {
constexpr Timestamp kInitialTime = Timestamp::Millis(123'456);
// After an overuse, we back off to 85% of the received bitrate.
constexpr double kFractionAfterOveruse = 0.85;
DataRate Bps(int64_t v) { return DataRate::BitsPerSec(v); }
}  // namespace

SM_TEST(AimdRateControl, MinNearMaxIncreaseRateOnLowBandwidth) {
  AimdRateControl c;
  c.SetEstimate(Bps(30'000), kInitialTime);
  SM_EXPECT_EQ(c.GetNearMaxIncreaseRateBpsPerSecond(), 4'000);
}

SM_TEST(AimdRateControl, NearMaxIncreaseRateIs5kbpsOn90kbpsAnd200msRtt) {
  AimdRateControl c;
  c.SetEstimate(Bps(90'000), kInitialTime);
  SM_EXPECT_EQ(c.GetNearMaxIncreaseRateBpsPerSecond(), 5'000);
}

SM_TEST(AimdRateControl, NearMaxIncreaseRateIs5kbpsOn60kbpsAnd100msRtt) {
  AimdRateControl c;
  c.SetEstimate(Bps(60'000), kInitialTime);
  c.SetRtt(TimeDelta::Millis(100));
  SM_EXPECT_EQ(c.GetNearMaxIncreaseRateBpsPerSecond(), 5'000);
}

SM_TEST(AimdRateControl, GetIncreaseRate) {
  AimdRateControl c;
  c.SetEstimate(Bps(300'000), kInitialTime);
  c.Update(BandwidthUsage::kOverusing, Bps(300'000), kInitialTime);
  SM_EXPECT_NEAR(c.GetNearMaxIncreaseRateBpsPerSecond(), 14'000, 1'000);
}

SM_TEST(AimdRateControl, BweLimitedByAckedBitrate) {
  AimdRateControl c;
  const DataRate acked = Bps(10'000);
  Timestamp now = kInitialTime;
  c.SetEstimate(acked, now);
  while (now - kInitialTime < TimeDelta::Seconds(20)) {
    c.Update(BandwidthUsage::kNormal, acked, now);
    now += TimeDelta::Millis(100);
  }
  SM_ASSERT_TRUE(c.ValidEstimate());
  SM_EXPECT_EQ(c.LatestEstimate().bps(), (acked * 1.5 + Bps(10'000)).bps());
}

SM_TEST(AimdRateControl, BweNotLimitedByDecreasingAckedBitrate) {
  AimdRateControl c;
  const DataRate acked = Bps(10'000);
  Timestamp now = kInitialTime;
  c.SetEstimate(acked, now);
  while (now - kInitialTime < TimeDelta::Seconds(20)) {
    c.Update(BandwidthUsage::kNormal, acked, now);
    now += TimeDelta::Millis(100);
  }
  SM_ASSERT_TRUE(c.ValidEstimate());
  // A decreasing acked rate neither lowers the estimate to 1.5x of it nor lets it grow.
  DataRate prev = c.LatestEstimate();
  c.Update(BandwidthUsage::kNormal, acked * 0.5, now);
  SM_EXPECT_EQ(c.LatestEstimate().bps(), prev.bps());
  SM_EXPECT_NEAR(static_cast<double>(c.LatestEstimate().bps()), static_cast<double>((acked * 1.5 + Bps(10'000)).bps()), 2'000);
}

SM_TEST(AimdRateControl, TypicalDrop) {
  AimdRateControl c;
  // The rate increase at 216 kbps should be 12 kbps.
  const DataRate initial = Bps(264'000), updated = Bps(216'000);
  const DataRate acked = Bps(static_cast<int64_t>((updated + Bps(5'000)).bps() / kFractionAfterOveruse));
  Timestamp now = kInitialTime;
  c.SetEstimate(initial, now);
  now += TimeDelta::Millis(100);
  c.Update(BandwidthUsage::kOverusing, acked, now);
  SM_EXPECT_EQ(c.LatestEstimate().bps(), updated.bps());
  SM_EXPECT_EQ(c.GetNearMaxIncreaseRateBpsPerSecond(), 12'000);
}

SM_TEST(AimdRateControl, SendingRateBoundedWhenThroughputNotEstimated) {
  AimdRateControl c;
  const DataRate initial = Bps(123'000);
  Timestamp now = kInitialTime;
  c.Update(BandwidthUsage::kNormal, initial, now);
  // The initial rate is what the controller receives after five seconds.
  now += TimeDelta::Seconds(5) + TimeDelta::Millis(1);
  c.Update(BandwidthUsage::kNormal, initial, now);
  for (int i = 0; i < 100; ++i) {
    c.Update(BandwidthUsage::kNormal, std::nullopt, now);
    now += TimeDelta::Millis(100);
  }
  SM_EXPECT_LE(c.LatestEstimate().bps(), (initial * 1.5 + Bps(10'000)).bps());
}

SM_TEST(AimdRateControl, NoIncreaseBeforeInitialized) {
  AimdRateControl c;
  Timestamp now = kInitialTime;
  DataRate before = c.LatestEstimate();
  for (int i = 0; i < 10; ++i) {
    c.Update(BandwidthUsage::kNormal, Bps(500'000), now);
    now += TimeDelta::Millis(100);
  }
  SM_EXPECT_FALSE(c.ValidEstimate());
  SM_EXPECT_EQ(c.LatestEstimate().bps(), before.bps());
}

SM_TEST(AimdRateControl, OveruseWithoutEstimateInitializesIt) {
  AimdRateControl c;
  c.Update(BandwidthUsage::kOverusing, Bps(400'000), kInitialTime);
  SM_EXPECT_TRUE(c.ValidEstimate());
  SM_EXPECT_EQ(c.LatestEstimate().bps(), Bps(400'000).bps() * 85 / 100 - 5'000);
}

SM_TEST(AimdRateControl, ReduceFurtherNeedsAnRttOrHalvedThroughput) {
  AimdRateControl c;
  c.SetRtt(TimeDelta::Millis(100));
  c.SetEstimate(Bps(1'000'000), kInitialTime);
  SM_EXPECT_FALSE(c.TimeToReduceFurther(kInitialTime + TimeDelta::Millis(50), Bps(900'000)));
  SM_EXPECT_TRUE(c.TimeToReduceFurther(kInitialTime + TimeDelta::Millis(50), Bps(400'000)));
  SM_EXPECT_TRUE(c.TimeToReduceFurther(kInitialTime + TimeDelta::Millis(100), Bps(900'000)));
  // The interval is clamped to [10, 200] ms.
  c.SetRtt(TimeDelta::Seconds(2));
  SM_EXPECT_FALSE(c.TimeToReduceFurther(kInitialTime + TimeDelta::Millis(199), Bps(900'000)));
  SM_EXPECT_TRUE(c.TimeToReduceFurther(kInitialTime + TimeDelta::Millis(200), Bps(900'000)));
}

SM_TEST(AimdRateControl, LinkCapacityStopsMultiplicativeIncreaseAndResetsFarAbove) {
  AimdRateControl c;
  Timestamp now = kInitialTime;
  c.SetEstimate(Bps(1'000'000), now);
  now += TimeDelta::Millis(200);
  c.Update(BandwidthUsage::kOverusing, Bps(800'000), now);  // seeds the link capacity: 800 kbps
  SM_ASSERT_TRUE(c.link_capacity().has_estimate());
  SM_EXPECT_NEAR(c.link_capacity().estimate().kbps(), 800.0, 0.1);
  // Near the capacity the increase is additive: well below 8% per second.
  now += TimeDelta::Millis(100);
  c.Update(BandwidthUsage::kNormal, Bps(800'000), now);
  DataRate r0 = c.LatestEstimate();
  now += TimeDelta::Seconds(1);
  c.Update(BandwidthUsage::kNormal, Bps(800'000), now);
  SM_EXPECT_LT(static_cast<double>((c.LatestEstimate() - r0).bps()), 0.08 * static_cast<double>(r0.bps()));
  // A throughput far above the capacity estimate drops the estimate.
  now += TimeDelta::Millis(100);
  c.Update(BandwidthUsage::kNormal, Bps(3'000'000), now);
  SM_EXPECT_FALSE(c.link_capacity().has_estimate());
}

SM_TEST(BitrateEstimatorV2, NoEstimateBeforeTheFirstFullWindow) {
  // The first sample needs a full 500 ms window, so an overuse within that time (a start-up probe
  // burst) has no acknowledged rate to seed the link-capacity estimate from (#15).
  BitrateEstimator e;
  for (int ms = 0; ms < 500; ms += 5) e.Update(Timestamp::Millis(1000 + ms), DataSize::Bytes(6'000), false);
  SM_EXPECT_FALSE(e.bitrate().has_value());
  e.Update(Timestamp::Millis(1500), DataSize::Bytes(6'000), false);
  SM_ASSERT_TRUE(e.bitrate().has_value());
  SM_EXPECT_NEAR(e.bitrate()->kbps(), 9'600.0, 100.0);  // 6000 B / 5 ms
}

SM_TEST(BitrateEstimatorV2, FollowsASlowDropWithinFewWindows) {
  BitrateEstimator e;
  int64_t t = 0;
  auto feed = [&](int64_t until, int bytes_per_5ms) {
    for (; t < until; t += 5) e.Update(Timestamp::Millis(t), DataSize::Bytes(bytes_per_5ms), false);
  };
  feed(2000, 1250);  // 2 Mbps
  SM_ASSERT_TRUE(e.bitrate().has_value());
  SM_EXPECT_NEAR(e.bitrate()->kbps(), 2000.0, 60.0);
  feed(4000, 250);  // 400 kbps
  SM_EXPECT_NEAR(e.bitrate()->kbps(), 400.0, 40.0);
}

SM_TEST(BitrateEstimatorV2, ExpectFastRateChangeSpeedsUpTheNextSamples) {
  BitrateEstimator slow, fast;
  int64_t t = 0;
  for (; t < 3000; t += 5) {
    slow.Update(Timestamp::Millis(t), DataSize::Bytes(1250), false);
    fast.Update(Timestamp::Millis(t), DataSize::Bytes(1250), false);
  }
  fast.ExpectFastRateChange();
  for (int64_t end = t + 300; t < end; t += 5) {
    slow.Update(Timestamp::Millis(t), DataSize::Bytes(250), false);
    fast.Update(Timestamp::Millis(t), DataSize::Bytes(250), false);
  }
  SM_EXPECT_LT(fast.bitrate()->kbps(), slow.bitrate()->kbps());
}

namespace {
// One feedback of `n` packets of `size` bytes sent every `gap_ms`, received `delay_ms` later.
std::vector<PacketResult> Packets(int64_t& seq, int64_t& send_ms, int n, int gap_ms, int delay_ms, int size = 1200) {
  std::vector<PacketResult> v;
  for (int i = 0; i < n; ++i, ++seq, send_ms += gap_ms) {
    PacketResult r;
    r.seq = seq;
    r.send_time = Timestamp::Millis(send_ms);
    r.receive_time = Timestamp::Millis(send_ms + delay_ms);
    r.size = DataSize::Bytes(size);
    v.push_back(r);
  }
  return v;
}
}  // namespace

SM_TEST(DelayBweV2, OveruseWithoutAckedRateHalvesAtMostEveryRtt) {
  DelayBasedBwe::Config cfg;
  DelayBasedBwe bwe(cfg);
  Timestamp now = Timestamp::Millis(1000);
  bwe.SetStartBitrate(Bps(2'000'000));
  bwe.SetRtt(TimeDelta::Millis(200));
  // A growing queue (4 ms more delay per packet) drives the detector to overuse; no acked rate is given.
  int64_t seq = 0, send = 1000;
  std::vector<int64_t> estimates;
  int64_t extra = 50;
  for (int fb = 0; fb < 40; ++fb) {
    std::vector<PacketResult> v;
    for (int i = 0; i < 5; ++i, ++seq, send += 10, extra += 4) {
      PacketResult r;
      r.seq = seq;
      r.send_time = Timestamp::Millis(send);
      r.receive_time = Timestamp::Millis(send + extra);
      r.size = DataSize::Bytes(1200);
      v.push_back(r);
    }
    now = Timestamp::Millis(send + extra);
    estimates.push_back(bwe.OnPacketResults(v, std::nullopt, std::nullopt, now).bps());
  }
  // Only halvings (or holds): every drop is exactly half of the previous value.
  int64_t prev = 2'000'000;
  int halvings = 0;
  for (int64_t e : estimates) {
    if (e < prev) {
      SM_EXPECT_EQ(e, std::max<int64_t>(prev / 2, 30'000));  // halves, down to the 30 kbps minimum
      ++halvings;
    }
    prev = e;
  }
  SM_EXPECT_GE(halvings, 1);
  // 40 feedbacks span 40 x 50 ms = 2 s; at most one halving per 200 ms.
  SM_EXPECT_LE(halvings, 11);
}

SM_TEST(DelayBweV2, ProbeEstimateSetsTheEstimateEvenBelowIt) {
  // libwebrtc: a probe result is taken as is (SetEstimate), with no check against the current estimate.
  DelayBasedBwe::Config cfg;
  DelayBasedBwe bwe(cfg);
  Timestamp now = Timestamp::Millis(1000);
  bwe.SetStartBitrate(Bps(1'000'000));
  int64_t seq = 0, send = 1000;
  auto v = Packets(seq, send, 5, 10, 50);
  now = Timestamp::Millis(send + 50);
  DataRate r = bwe.OnPacketResults(v, std::nullopt, Bps(600'000), now);
  SM_EXPECT_EQ(r.bps(), 600'000);
}

SM_TEST(DelayBweV2, SteadyFeedbackRampsMultiplicativelyUntilTheAckedLimit) {
  DelayBasedBwe::Config cfg;
  DelayBasedBwe bwe(cfg);
  Timestamp now = Timestamp::Millis(1000);
  bwe.SetStartBitrate(Bps(300'000));
  int64_t seq = 0, send = 1000;
  DataRate r = Bps(300'000);
  for (int i = 0; i < 100; ++i) {
    auto v = Packets(seq, send, 5, 10, 50);
    now = Timestamp::Millis(send + 50);
    r = bwe.OnPacketResults(v, Bps(2'000'000), std::nullopt, now);
  }
  // 5 s of normal feedback at 8%/s: about 300 kbps x 1.08^5 = 440 kbps (no link-capacity estimate yet).
  SM_EXPECT_GT(r.kbps(), 400.0);
  SM_EXPECT_LT(r.kbps(), 500.0);
}

SM_TEST(SendSideBwe, V2UsesTheWindowedAckedRate) {
  // `cc v2` takes the acknowledged rate from the Bayesian BitrateEstimator: none during its first 500 ms.
  AckedBitrateEstimatorV2 a;
  int64_t seq = 0, send = 0;
  a.OnPacketResults(Packets(seq, send, 40, 5, 20));  // 200 ms of packets
  SM_EXPECT_FALSE(a.bitrate().has_value());
  a.OnPacketResults(Packets(seq, send, 100, 5, 20));  // 700 ms in total
  SM_ASSERT_TRUE(a.bitrate().has_value());
  SM_EXPECT_NEAR(a.bitrate()->kbps(), 1920.0, 100.0);  // 1200 B / 5 ms
}
