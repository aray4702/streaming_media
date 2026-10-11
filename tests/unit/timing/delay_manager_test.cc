#include <cmath>

#include "sm/core/random.h"
#include "sm/timing/delay_manager.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::timing;

SM_TEST(DelayManager, SteadyNetworkTargetsOnePacket) {
  DelayManager dm;
  for (int i = 0; i < 500; ++i) dm.Update(static_cast<uint32_t>(i * 320), 16000, Timestamp::Millis(i * 20 + 50));
  SM_EXPECT_EQ(dm.TargetDelayMs(), 20);
}

SM_TEST(DelayManager, TracksJitterQuantile) {
  DelayManager dm;
  Random rng(1);
  for (int i = 0; i < 3000; ++i) {
    double j = std::fabs(rng.Gaussian(0, 20));
    dm.Update(static_cast<uint32_t>(i * 320), 16000, Timestamp::Micros(static_cast<int64_t>((i * 20 + 50 + j) * 1000)));
  }
  // 95th percentile of |N(0, 20)| is ~39 ms -> 40 or 60 after packet rounding.
  SM_RECORD("target_ms_sigma20", dm.TargetDelayMs());
  SM_EXPECT_GE(dm.TargetDelayMs(), 40);
  SM_EXPECT_LE(dm.TargetDelayMs(), 60);
}

SM_TEST(DelayManager, IncreasesFastDecreasesSlowly) {
  DelayManager dm;
  int i = 0;
  auto feed = [&](int n, double jitter_ms) {
    Random rng(static_cast<uint64_t>(i + 1));
    for (int k = 0; k < n; ++k, ++i) {
      double j = rng.Uniform(0, jitter_ms);
      dm.Update(static_cast<uint32_t>(i * 320), 16000, Timestamp::Micros(static_cast<int64_t>((i * 20 + j) * 1000)));
    }
  };
  feed(500, 0);
  SM_EXPECT_EQ(dm.TargetDelayMs(), 20);
  feed(100, 150);  // 2 s of heavy jitter.
  int high = dm.TargetDelayMs();
  SM_EXPECT_GE(high, 100);
  feed(50, 0);     // 1 s calm: still elevated.
  SM_EXPECT_GE(dm.TargetDelayMs(), high / 2);
  feed(1000, 0);   // 20 s calm: back down.
  SM_EXPECT_LE(dm.TargetDelayMs(), 40);
}

SM_TEST(DelayManager, MinimumDelayAndMetrics) {
  DelayManager dm;
  dm.SetMinimumDelayMs(100);
  for (int i = 0; i < 100; ++i) dm.Update(static_cast<uint32_t>(i * 320), 16000, Timestamp::Millis(i * 20));
  SM_EXPECT_EQ(dm.TargetDelayMs(), 100);
  auto m = CollectMetrics(dm);
  SM_EXPECT_EQ(m["target_delay_ms"], 100.0);
  SM_EXPECT_EQ(m["updates"], 100.0);
}
