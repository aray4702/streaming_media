#include <cmath>

#include "sm/core/random.h"
#include "sm/timing/video_jitter_estimator.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::timing;

SM_TEST(VideoJitter, SteadyArrivalsGiveSmallJitter) {
  VideoJitterEstimator e;
  for (int i = 0; i < 300; ++i) e.Update(TimeDelta::Zero(), DataSize::Bytes(5000));
  // Noise variance is floored at 1 ms^2: 3 sigma = 3 ms.
  SM_EXPECT_LE(e.JitterDelay().ms_f(), 3.5);
}

SM_TEST(VideoJitter, NoiseRaisesJitter) {
  VideoJitterEstimator quiet, noisy;
  Random rng(1);
  for (int i = 0; i < 600; ++i) {
    quiet.Update(TimeDelta::MillisF(rng.Gaussian(0, 2)), DataSize::Bytes(5000));
    noisy.Update(TimeDelta::MillisF(rng.Gaussian(0, 25)), DataSize::Bytes(5000));
  }
  SM_RECORD("jitter_quiet_ms", quiet.JitterDelay().ms_f());
  SM_RECORD("jitter_noisy_ms", noisy.JitterDelay().ms_f());
  SM_EXPECT_GT(noisy.JitterDelay().ms_f(), quiet.JitterDelay().ms_f() + 10);
  SM_EXPECT_NEAR(noisy.noise_std_ms(), 25, 8);
}

SM_TEST(VideoJitter, LearnsCapacityFromFrameSizeDelay) {
  // 1 Mbps link: each extra byte costs 8 us.
  VideoJitterEstimator e;
  Random rng(2);
  double prev = 5000;
  for (int i = 0; i < 2000; ++i) {
    double size = rng.Uniform(2000, 12000);
    double d_ms = (size - prev) * 8.0 / 1000.0 + rng.Gaussian(0, 0.5);
    e.Update(TimeDelta::MillisF(d_ms), DataSize::Bytes(static_cast<int64_t>(size)));
    prev = size;
  }
  double capacity_kbps = 8.0 / e.slope_ms_per_byte();
  SM_RECORD("estimated_capacity_kbps", capacity_kbps);
  SM_EXPECT_NEAR(capacity_kbps, 1000, 150);
  // Jitter covers the worst-case frame: (max - avg) / capacity ~ 50 ms.
  SM_EXPECT_GT(e.JitterDelay().ms_f(), 30.0);
}

SM_TEST(VideoJitter, TimingRenderTimeAndSmoothing) {
  VideoTiming t;
  for (int i = 0; i < 30; ++i) {
    t.IncomingTimestamp(static_cast<uint32_t>(i * 3000), Timestamp::Micros(1000000 + i * 33333 + (i % 3) * 1000));
  }
  t.SetJitterDelay(TimeDelta::Millis(50));
  t.AddDecodeTime(TimeDelta::Millis(8));
  SM_EXPECT_EQ(t.TargetDelay().ms(), 50 + 8 + 10);
  t.UpdateCurrentDelay(Timestamp::Millis(2000));
  SM_EXPECT_EQ(t.CurrentDelay().ms(), 68);
  // Capture of ts=0 is at the 1000 ms envelope; render = capture + delay.
  SM_EXPECT_NEAR(static_cast<double>(t.RenderTime(0, Timestamp::Millis(2000)).ms()), 1068, 2);
  // Target jumps to 500 ms; current delay moves at most 10% of wall time.
  t.SetJitterDelay(TimeDelta::Millis(482));
  t.UpdateCurrentDelay(Timestamp::Millis(3000));
  SM_EXPECT_NEAR(t.CurrentDelay().ms_f(), 168, 1);
  t.SetMinPlayoutDelay(TimeDelta::Millis(900));
  SM_EXPECT_EQ(t.TargetDelay().ms(), 900);
}
