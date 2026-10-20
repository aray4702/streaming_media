// libwebrtc M157's NetEq unit tests for the target-delay classes (issue #62):
// histogram_unittest.cc, underrun_optimizer_unittest.cc,
// reorder_optimizer_unittest.cc, packet_arrival_history_unittest.cc,
// delay_manager_unittest.cc and delay_constraints_unittest.cc, with the
// TickTimer replaced by the tick time in ms.
#include <cmath>
#include <limits>
#include <vector>

#include "sm/timing/audio_jitter_buffer.h"
#include "sm/timing/neteq_delay.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::timing;

SM_TEST(NetEqHistogram, Initialization) {
  NetEqHistogram histogram(65, 32440);
  histogram.Reset();
  double sum = 0.0;
  for (size_t i = 0; i < histogram.buckets().size(); ++i) {
    SM_EXPECT_NEAR(std::ldexp(std::pow(0.5, static_cast<int>(i + 1)), 30), histogram.buckets()[i], 65537);
    sum += histogram.buckets()[i];
  }
  SM_EXPECT_EQ(1 << 30, static_cast<int>(sum));
}

SM_TEST(NetEqHistogram, Add) {
  NetEqHistogram histogram(10, 32440);
  histogram.Reset();
  const std::vector<int> before = histogram.buckets();
  histogram.Add(5);
  const std::vector<int> after = histogram.buckets();
  SM_EXPECT_GT(after[5], before[5]);
  int sum = 0;
  for (int b : after) sum += b;
  SM_EXPECT_EQ(1 << 30, sum);
}

SM_TEST(NetEqHistogram, ForgetFactor) {
  NetEqHistogram histogram(10, 32440);
  histogram.Reset();
  const std::vector<int> before = histogram.buckets();
  histogram.Add(4);
  const std::vector<int> after = histogram.buckets();
  for (int i = 0; i < histogram.NumBuckets(); ++i) {
    if (i != 4) SM_EXPECT_LT(after[static_cast<size_t>(i)], before[static_cast<size_t>(i)]);
  }
}

SM_TEST(NetEqHistogram, ReachSteadyStateForgetFactor) {
  static constexpr int kSteadyStateForgetFactor = static_cast<int>((1 << 15) * 0.9993);
  NetEqHistogram histogram(100, kSteadyStateForgetFactor, 1.0);
  histogram.Reset();
  int n = (1 << 15) / ((1 << 15) - kSteadyStateForgetFactor);
  for (int i = 0; i < n; ++i) histogram.Add(0);
  SM_EXPECT_EQ(histogram.forget_factor(), kSteadyStateForgetFactor);
}

SM_TEST(UnderrunOptimizer, ResamplePacketDelays) {
  constexpr int kDefaultHistogramQuantile = 1020054733;  // 0.95 in Q30.
  constexpr int kForgetFactor = 32745;                   // 0.9993 in Q15.
  constexpr int kResampleIntervalMs = 500;
  UnderrunOptimizer underrun(kDefaultHistogramQuantile, kForgetFactor, std::nullopt, kResampleIntervalMs);
  // The histogram is updated once, with the largest delay of the interval.
  for (int i = 0; i < 500; i += 20) {
    underrun.Update(i, 0);
    SM_EXPECT_FALSE(underrun.GetOptimalDelayMs().has_value());
  }
  underrun.Update(0, (kResampleIntervalMs / 10 + 1) * 10);
  SM_EXPECT_EQ(underrun.GetOptimalDelayMs().value_or(-1), 500);
}

namespace {
constexpr int kReorderForgetFactor = 32745;  // 0.9993 in Q15.
constexpr int kMsPerLossPercent = 20;
constexpr int kStartForgetWeight = 1;
}  // namespace

SM_TEST(ReorderOptimizer, OnlyIncreaseDelayForReorderedPackets) {
  ReorderOptimizer r(kReorderForgetFactor, kMsPerLossPercent, kStartForgetWeight);
  SM_EXPECT_FALSE(r.GetOptimalDelayMs().has_value());
  r.Update(60, false, 0);
  SM_EXPECT_EQ(r.GetOptimalDelayMs().value_or(-1), 20);
  r.Update(100, false, 0);
  SM_EXPECT_EQ(r.GetOptimalDelayMs().value_or(-1), 20);
  r.Update(80, true, 0);
  SM_EXPECT_EQ(r.GetOptimalDelayMs().value_or(-1), 100);
}

SM_TEST(ReorderOptimizer, AvoidIncreasingDelayWhenProbabilityIsLow) {
  ReorderOptimizer r(kReorderForgetFactor, kMsPerLossPercent, kStartForgetWeight);
  r.Update(40, true, 0);
  r.Update(40, true, 0);
  r.Update(40, true, 0);
  SM_EXPECT_EQ(r.GetOptimalDelayMs().value_or(-1), 60);
  // The cost of the delay is too high relative to the probability.
  r.Update(600, true, 0);
  SM_EXPECT_EQ(r.GetOptimalDelayMs().value_or(-1), 60);
}

SM_TEST(ReorderOptimizer, BaseDelayIsSubtractedFromCost) {
  constexpr int kBaseDelayMs = 200;
  ReorderOptimizer r(kReorderForgetFactor, kMsPerLossPercent, kStartForgetWeight);
  r.Update(40, true, kBaseDelayMs);
  r.Update(40, true, kBaseDelayMs);
  r.Update(40, true, kBaseDelayMs);
  SM_EXPECT_EQ(r.GetOptimalDelayMs().value_or(-1), 60);
  r.Update(600, true, kBaseDelayMs);
  SM_EXPECT_EQ(r.GetOptimalDelayMs().value_or(-1), 620);
}

namespace {
// packet_arrival_history_unittest.cc's fixture.
struct ArrivalFixture {
  static constexpr int kFs = 8000;
  static constexpr int kFsKhz = kFs / 1000;
  static constexpr int kFrameSizeMs = 20;
  static constexpr int kFrameSizeSamples = kFrameSizeMs * kFsKhz;
  static constexpr int kWindowSizeMs = 1000;
  PacketArrivalHistory history{kWindowSizeMs};
  int64_t now_ms = 0;
  uint32_t timestamp = 0x12345678;
  bool ok = true;
  ArrivalFixture() { history.set_sample_rate(kFs); }
  void IncrementTime(int delta_ms) { now_ms += delta_ms; }
  int InsertPacketAndGetDelay(int timestamp_delta_ms) {
    uint32_t ts = timestamp + static_cast<uint32_t>(timestamp_delta_ms * kFsKhz);
    if (timestamp_delta_ms > 0) timestamp = ts;
    ok = ok && history.Insert(ts, kFrameSizeSamples, now_ms);
    ok = ok && history.IsNewestRtpTimestamp(ts) == (timestamp_delta_ms >= 0);
    return history.GetDelayMs(ts, now_ms);
  }
};
}  // namespace

SM_TEST(PacketArrivalHistory, RelativeArrivalDelay) {
  ArrivalFixture f;
  SM_EXPECT_EQ(f.InsertPacketAndGetDelay(0), 0);
  f.IncrementTime(20);
  SM_EXPECT_EQ(f.InsertPacketAndGetDelay(20), 0);
  f.IncrementTime(40);
  SM_EXPECT_EQ(f.InsertPacketAndGetDelay(20), 20);
  SM_EXPECT_EQ(f.InsertPacketAndGetDelay(-60), 80);  // Reordered.
  f.IncrementTime(40);
  SM_EXPECT_EQ(f.InsertPacketAndGetDelay(20), 40);
  SM_EXPECT_EQ(f.InsertPacketAndGetDelay(80), 0);  // Moves the reference forward.
  f.IncrementTime(40);
  SM_EXPECT_EQ(f.InsertPacketAndGetDelay(20), 20);
  SM_EXPECT_EQ(f.history.GetMaxDelayMs(), 80);
  SM_EXPECT_TRUE(f.ok);
}

SM_TEST(PacketArrivalHistory, ReorderedPackets) {
  ArrivalFixture f;
  SM_EXPECT_EQ(f.InsertPacketAndGetDelay(0), 0);
  SM_EXPECT_EQ(f.InsertPacketAndGetDelay(-80), 80);
  SM_EXPECT_EQ(f.InsertPacketAndGetDelay(-20), 20);
  f.IncrementTime(80);
  SM_EXPECT_EQ(f.InsertPacketAndGetDelay(20), 60);
  SM_EXPECT_EQ(f.history.GetMaxDelayMs(), 60);
  SM_EXPECT_TRUE(f.ok);
}

SM_TEST(PacketArrivalHistory, MaxHistorySize) {
  ArrivalFixture f;
  SM_EXPECT_EQ(f.InsertPacketAndGetDelay(0), 0);
  f.IncrementTime(40);
  SM_EXPECT_EQ(f.InsertPacketAndGetDelay(20), 20);
  SM_EXPECT_EQ(f.history.GetMaxDelayMs(), 20);
  f.IncrementTime(ArrivalFixture::kWindowSizeMs + 20);
  SM_EXPECT_EQ(f.InsertPacketAndGetDelay(20 + ArrivalFixture::kWindowSizeMs), 0);
  SM_EXPECT_EQ(f.history.GetMaxDelayMs(), 0);
  SM_EXPECT_TRUE(f.ok);
}

SM_TEST(PacketArrivalHistory, TimestampWraparound) {
  for (uint32_t start : {std::numeric_limits<uint32_t>::max(), 0u}) {
    ArrivalFixture f;
    f.timestamp = start;
    SM_EXPECT_EQ(f.InsertPacketAndGetDelay(0), 0);
    f.IncrementTime(40);
    SM_EXPECT_EQ(f.InsertPacketAndGetDelay(20), 20);
    SM_EXPECT_EQ(f.InsertPacketAndGetDelay(-40), 60);  // Reordered, before the wrap.
    SM_EXPECT_EQ(f.InsertPacketAndGetDelay(20), 0);
    SM_EXPECT_EQ(f.history.GetMaxDelayMs(), 20);
    SM_EXPECT_TRUE(f.ok);
  }
}

SM_TEST(PacketArrivalHistory, OldDuplicateAndOverlappingPacketsAreNotInserted) {
  ArrivalFixture f;
  const uint32_t first = f.timestamp;
  SM_EXPECT_EQ(f.InsertPacketAndGetDelay(0), 0);
  SM_EXPECT_FALSE(f.history.Insert(f.timestamp - ArrivalFixture::kWindowSizeMs * ArrivalFixture::kFsKhz - 1,
                                   ArrivalFixture::kFrameSizeSamples, f.now_ms));
  SM_EXPECT_FALSE(f.history.Insert(f.timestamp + ArrivalFixture::kFrameSizeSamples / 2,
                                   ArrivalFixture::kFrameSizeSamples / 2, f.now_ms));
  SM_EXPECT_EQ(f.InsertPacketAndGetDelay(20), 0);
  SM_EXPECT_FALSE(f.history.Insert(first, ArrivalFixture::kFrameSizeSamples, f.now_ms));
  SM_EXPECT_TRUE(f.ok);
}

// delay_manager_unittest.cc UpdateNormal, through DecisionLogic::PacketArrived:
// packets every 20 ms with no jitter target one bucket, 20 ms. The first
// estimate comes after the first 500 ms resample interval; until then the
// target is libwebrtc's kStartDelayMs, 80 ms.
SM_TEST(NetEqDelayManager, UpdateNormal) {
  NetEqDelayManager dm;
  dm.SetSampleRate(16000);
  SM_EXPECT_EQ(dm.TargetLevelMs(), 80);
  for (int i = 0; i < 50; ++i) dm.PacketArrived(static_cast<uint32_t>(i * 320), 320, 16000, i * 20);
  SM_EXPECT_EQ(dm.TargetLevelMs(), 20);
}

SM_TEST(NetEqDelayManager, StartsAt80MsUntilFirstResample) {
  NetEqDelayManager dm;
  dm.SetSampleRate(16000);
  for (int i = 0; i < 25; ++i) dm.PacketArrived(static_cast<uint32_t>(i * 320), 320, 16000, i * 20);
  SM_EXPECT_EQ(dm.TargetLevelMs(), 80);  // 480 ms: no interval has closed yet.
}

SM_TEST(NetEqDelayManager, TargetIsTheQuantileOfIntervalMaxima) {
  // Every 500 ms one packet is 100 ms late: each interval's maximum is 100,
  // so the 95th percentile target becomes (1 + 100 / 20) * 20 = 120 ms.
  NetEqDelayManager dm;
  dm.SetSampleRate(16000);
  for (int i = 0; i < 500; ++i) {
    const int late = i % 25 == 10 ? 100 : 0;
    dm.PacketArrived(static_cast<uint32_t>(i * 320), 320, 16000, (i * 20 + late) / 10 * 10);
  }
  SM_EXPECT_EQ(dm.TargetLevelMs(), 120);
}

SM_TEST(NetEqDelayManager, ReorderedPacketsRaiseTargetThroughReorderOptimizer) {
  // Every 5th packet is swapped with the next one. The early packet becomes
  // the reference, so in-order packets are 20 ms late (underrun target
  // (1 + 1) x 20 = 40 ms) and the reordered ones 40 ms. The underrun
  // optimizer skips the reordered packets; the reorder optimizer puts 20%
  // of packets in bucket 2, which costs less than losing them, so the target
  // is (1 + 2) x 20 = 60 ms, as libwebrtc's DelayManager::Update.
  NetEqDelayManager dm;
  dm.SetSampleRate(16000);
  for (int i = 0; i < 1000; ++i) {
    int seq = i;
    if (i % 5 == 0) seq = i + 1;
    if (i % 5 == 1) seq = i - 1;
    dm.PacketArrived(static_cast<uint32_t>(seq * 320), 320, 16000, i * 20);
  }
  SM_EXPECT_EQ(dm.TargetLevelMs(), 60);
}

// delay_constraints_unittest.cc: the minimum delay raises the target; the
// target is capped at 75% of the packet buffer (200 packets of 20 ms).
SM_TEST(NetEqDelayManager, MinimumDelayAndBufferCap) {
  NetEqDelayManager dm;
  dm.SetSampleRate(16000);
  for (int i = 0; i < 50; ++i) dm.PacketArrived(static_cast<uint32_t>(i * 320), 320, 16000, i * 20);
  SM_EXPECT_TRUE(dm.SetMinimumDelay(200));
  SM_EXPECT_EQ(dm.TargetLevelMs(), 200);
  SM_EXPECT_FALSE(dm.SetMinimumDelay(3001));  // Above 75% of 200 x 20 ms.
  SM_EXPECT_TRUE(dm.SetMinimumDelay(0));
  SM_EXPECT_EQ(dm.TargetLevelMs(), 20);
}

// The jitter buffer under version 2: packets before the first GetAudio are
// dropped from the statistics (NetEqImpl's SoftReset on the new codec), and
// arrival times are on the 10 ms tick of GetAudio.
SM_TEST(AudioJitterBufferNetEqV2, TargetFromNetEqDelayManager) {
  AudioJitterBufferConfig cfg;
  cfg.version = 2;
  AudioJitterBuffer jb(cfg);
  SM_EXPECT_EQ(jb.TargetDelayMs(), 80);
  std::vector<float> out(160);
  for (int i = 0; i < 300; ++i) {
    jb.InsertPacket(static_cast<uint32_t>(i * 320), std::vector<float>(320, 0.0f), Timestamp::Millis(i * 20));
    jb.GetAudio(out, Timestamp::Millis(i * 20));
    jb.GetAudio(out, Timestamp::Millis(i * 20 + 10));
  }
  SM_EXPECT_EQ(jb.TargetDelayMs(), 20);
  jb.SetMinimumDelayMs(100);
  SM_EXPECT_EQ(jb.TargetDelayMs(), 100);
}
