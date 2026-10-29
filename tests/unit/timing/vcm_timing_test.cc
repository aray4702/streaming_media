// Timing v2 (issue #24): libwebrtc M157's receive-side video timing.
// Ported from libwebrtc's unit tests (compare/webrtc/reference/):
// video/timing/{jitter_estimator,rtt_filter,timestamp_extrapolator,
// inter_frame_delay_variation_calculator}_unittest.cc and
// modules/video_coding/timing/{timing,decode_time_percentile_filter}_unittest.cc.
// The FrameRelease tests check the release rules of
// video/video_stream_buffer_controller.cc and video/frame_decode_timing.cc.
#include <cmath>
#include <cstdint>
#include <limits>

#include "sm/timing/frame_release.h"
#include "sm/timing/vcm_timing.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::timing;

namespace {

// jitter_estimator_unittest.cc: a sawtooth delay and frame size.
class ValueGenerator {
 public:
  explicit ValueGenerator(int32_t amplitude) : amplitude_(amplitude) {}
  TimeDelta Delay() const { return TimeDelta::Millis(((counter_ % 11) - 5) * amplitude_); }
  int64_t FrameBytes() const { return 1000 + Delay().ms() / 5; }
  void Advance() { ++counter_; }

 private:
  const int32_t amplitude_;
  int64_t counter_ = 0;
};

struct JitterRun {
  JitterEstimator estimator;
  Timestamp now = Timestamp::Micros(0);
  void Run(int duration_s, int fps, ValueGenerator& gen) {
    const TimeDelta tick = TimeDelta::Micros(1'000'000 / fps);
    for (int i = 0; i < duration_s * fps; ++i) {
      estimator.UpdateEstimate(gen.Delay(), DataSize::Bytes(gen.FrameBytes()), now);
      now += tick;
      gen.Advance();
    }
  }
};

constexpr int64_t kTicksPer25Fps = 90000 / 25;

// libwebrtc's TimeDelta::ms() and Timestamp::ms() round to the nearest millisecond.
int64_t RoundMs(int64_t us) { return us >= 0 ? (us + 500) / 1000 : -((-us + 500) / 1000); }
constexpr TimeDelta k25FpsDelay = TimeDelta::Millis(40);

}  // namespace

// --- JitterEstimator ---

SM_TEST(VcmTiming, JitterSteadyStateConvergence) {
  JitterRun r;
  ValueGenerator gen(10);
  r.Run(60, 30, gen);
  SM_EXPECT_EQ(RoundMs(r.estimator.GetEstimate(r.now).us()), 54);  // libwebrtc's value.
}

SM_TEST(VcmTiming, JitterSizeOutlierIsNotRejectedAndIncreasesJitterEstimate) {
  JitterRun r;
  ValueGenerator gen(10);
  r.Run(60, 30, gen);
  const int64_t steady = r.estimator.GetEstimate(r.now).ms();
  r.estimator.UpdateEstimate(gen.Delay(), DataSize::Bytes(10 * gen.FrameBytes()), r.now);
  SM_EXPECT_GT(static_cast<double>(r.estimator.GetEstimate(r.now).ms()), 1.25 * static_cast<double>(steady));
}

SM_TEST(VcmTiming, JitterLowFramerateDisablesJitterEstimator) {
  JitterRun r;
  ValueGenerator gen(10);
  for (int i = 0; i < 60; ++i) {
    r.estimator.UpdateEstimate(gen.Delay(), DataSize::Bytes(gen.FrameBytes()), r.now);
    r.now += TimeDelta::Millis(200);
    if (i > 2) SM_EXPECT_EQ(r.estimator.GetEstimate(r.now).us(), 0);
    gen.Advance();
  }
}

SM_TEST(VcmTiming, JitterSingle2xFrameSizeImpactsJitterEstimate) {
  JitterRun r;
  ValueGenerator gen(10);
  r.Run(60, 30, gen);
  const int64_t steady = r.estimator.GetEstimate(r.now).ms();
  r.estimator.UpdateEstimate(gen.Delay(), DataSize::Bytes(2 * gen.FrameBytes()), r.now);
  SM_EXPECT_GT(r.estimator.GetEstimate(r.now).ms(), steady);
}

SM_TEST(VcmTiming, JitterCongestedFrameImpactsJitterEstimate) {
  JitterRun r;
  ValueGenerator gen(10);
  r.Run(10, 30, gen);
  const int64_t steady = r.estimator.GetEstimate(r.now).ms();
  r.estimator.UpdateEstimate(gen.Delay() * -10.0, DataSize::Bytes(gen.FrameBytes() / 10), r.now);
  SM_EXPECT_GT(r.estimator.GetEstimate(r.now).ms(), steady);
}

SM_TEST(VcmTiming, JitterRttTermAfterThreeNackedFrames) {
  // GetEstimate adds min(0.9 x RTT, 200 ms) once 3 frames were NACKed within 60 s.
  JitterRun r;
  ValueGenerator gen(10);
  r.Run(10, 30, gen);
  const TimeDelta base = r.estimator.GetEstimate(r.now);
  for (int i = 0; i < 5; ++i) r.estimator.UpdateRtt(TimeDelta::Millis(100));
  r.estimator.FrameNacked(r.now);
  r.estimator.FrameNacked(r.now);
  SM_EXPECT_EQ(r.estimator.GetEstimate(r.now).us(), base.us());
  r.estimator.FrameNacked(r.now);
  SM_EXPECT_EQ(r.estimator.GetEstimate(r.now).us(), (base + TimeDelta::Millis(90)).us());
  // The NACK count expires after 60 s without a NACK.
  SM_EXPECT_EQ(r.estimator.GetEstimate(r.now + TimeDelta::Seconds(61)).us(), base.us());
}

// --- RttFilter ---

SM_TEST(VcmTiming, RttIsCapped) {
  RttFilter f;
  f.Update(TimeDelta::Seconds(500));
  SM_EXPECT_EQ(f.Rtt().us(), TimeDelta::Seconds(3).us());
}

SM_TEST(VcmTiming, RttPositiveJumpDetection) {
  RttFilter f;
  for (int i = 0; i < 3; ++i) f.Update(TimeDelta::Millis(200));
  f.Update(TimeDelta::Millis(1400));
  f.Update(TimeDelta::Millis(1500));
  f.Update(TimeDelta::Millis(1600));
  f.Update(TimeDelta::Millis(1600));
  SM_EXPECT_EQ(f.Rtt().ms(), 1600);
  f.Update(TimeDelta::Millis(1600));
  SM_EXPECT_EQ(f.Rtt().ms(), 1600);
}

SM_TEST(VcmTiming, RttNegativeJumpDetection) {
  RttFilter f;
  for (int i = 0; i < 10; ++i) f.Update(TimeDelta::Millis(1500));
  for (int i = 0; i < 4; ++i) f.Update(TimeDelta::Millis(200));
  SM_EXPECT_EQ(f.Rtt().ms(), 1500);
  f.Update(TimeDelta::Millis(300));
  SM_EXPECT_EQ(f.Rtt().ms(), 300);
}

SM_TEST(VcmTiming, RttJumpsResetByDirectionShift) {
  RttFilter f;
  for (int i = 0; i < 10; ++i) f.Update(TimeDelta::Millis(1500));
  for (int i = 0; i < 4; ++i) f.Update(TimeDelta::Millis(200));
  f.Update(TimeDelta::Millis(2000));
  SM_EXPECT_EQ(f.Rtt().ms(), 2000);
  f.Update(TimeDelta::Millis(300));
  SM_EXPECT_EQ(f.Rtt().ms(), 2000);
}

SM_TEST(VcmTiming, RttDriftDetection) {
  RttFilter f;
  for (int64_t rtt = 1000; rtt >= 700; rtt -= 30) f.Update(TimeDelta::Millis(rtt));
  SM_EXPECT_EQ(f.Rtt().ms(), 1000);
  for (int i = 0; i < 50; ++i) f.Update(TimeDelta::Millis(700));
  SM_EXPECT_EQ(f.Rtt().ms(), 700);
}

// --- TimestampExtrapolator ---

SM_TEST(VcmTiming, ExtrapolationOccursAfter2Packets) {
  Timestamp now = Timestamp::Millis(1337);
  TimestampExtrapolator ts(now);
  SM_EXPECT_FALSE(ts.ExtrapolateLocalTime(90000).has_value());
  uint32_t rtp = 90000;
  now += k25FpsDelay;
  ts.Update(now, rtp);
  SM_EXPECT_EQ(ts.ExtrapolateLocalTime(rtp)->us(), now.us());
  rtp += kTicksPer25Fps;
  now += k25FpsDelay;
  ts.Update(now, rtp);
  SM_EXPECT_EQ(ts.ExtrapolateLocalTime(rtp)->us(), now.us());
  SM_EXPECT_EQ(ts.ExtrapolateLocalTime(rtp + 90000)->us(), (now + TimeDelta::Seconds(1)).us());
}

SM_TEST(VcmTiming, ExtrapolatorResetsAfter10SecondPause) {
  Timestamp now = Timestamp::Millis(1337);
  TimestampExtrapolator ts(now);
  uint32_t rtp = 90000;
  ts.Update(now, rtp);
  SM_EXPECT_EQ(ts.ExtrapolateLocalTime(rtp)->us(), now.us());
  rtp += kTicksPer25Fps;
  now += k25FpsDelay;
  ts.Update(now, rtp);
  SM_EXPECT_EQ(ts.ExtrapolateLocalTime(rtp)->us(), now.us());
  rtp += 10 * 90000;
  now += TimeDelta::Seconds(10) + TimeDelta::Micros(1);
  ts.Update(now, rtp);
  SM_EXPECT_EQ(ts.ExtrapolateLocalTime(rtp)->us(), now.us());
}

SM_TEST(VcmTiming, ExtrapolatorMultipleRtpWrapArounds) {
  Timestamp now = Timestamp::Millis(1337);
  TimestampExtrapolator ts(now);
  uint32_t rtp = std::numeric_limits<uint32_t>::max();
  ts.Update(now, rtp);
  SM_EXPECT_EQ(ts.ExtrapolateLocalTime(rtp)->us(), now.us());
  rtp += static_cast<uint32_t>(kTicksPer25Fps);
  now += k25FpsDelay;
  ts.Update(now, rtp);
  SM_EXPECT_EQ(ts.ExtrapolateLocalTime(rtp)->us(), now.us());
  SM_EXPECT_EQ(ts.ExtrapolateLocalTime(rtp + 90000)->us(), (now + TimeDelta::Seconds(1)).us());
  SM_EXPECT_EQ(ts.ExtrapolateLocalTime(rtp - 90000)->us(), (now - TimeDelta::Millis(999)).us());
  // max uint32 / 90 kHz, twice.
  const Timestamp overflow_time = now + TimeDelta::Micros(std::numeric_limits<uint32_t>::max() / 90 * 1000) * 2.0;
  while (now < overflow_time) {
    now += TimeDelta::Seconds(10);
    rtp += static_cast<uint32_t>(900000);
    ts.Update(now, rtp);
    SM_EXPECT_EQ(ts.ExtrapolateLocalTime(rtp)->us(), now.us());
  }
}

SM_TEST(VcmTiming, ExtrapolatorSlowAndFast90KHzClock) {
  for (int fps : {24, 26}) {
    Timestamp now = Timestamp::Millis(1337);
    TimestampExtrapolator ts(now);
    const TimeDelta delay = TimeDelta::Micros(std::llround(1e6 / fps));  // 1 / Frequency rounds.
    uint32_t rtp = 90000;
    for (int i = 0; i < 25; ++i) {
      ts.Update(now, rtp);
      rtp += kTicksPer25Fps;
      now += delay;
    }
    // 90000 / (25 x delay): the camera's real tick rate, in Hz.
    const int64_t real_hz = std::llround(90000.0 / (25 * delay.seconds()));
    auto t = ts.ExtrapolateLocalTime(rtp + static_cast<uint32_t>(real_hz));
    SM_ASSERT_TRUE(t.has_value());
    SM_EXPECT_EQ(RoundMs(t->us()), RoundMs(now.us()) + 1000);
  }
}

SM_TEST(VcmTiming, ExtrapolatorTimestampJump) {
  Timestamp now = Timestamp::Millis(1337);
  TimestampExtrapolator ts(now);
  uint32_t rtp = 90000;
  for (int i = 0; i < 3; ++i) {
    now += k25FpsDelay;
    ts.Update(now, rtp);
    if (i < 2) rtp += kTicksPer25Fps;
  }
  SM_EXPECT_EQ(ts.ExtrapolateLocalTime(rtp)->us(), now.us());
  SM_EXPECT_EQ(ts.ExtrapolateLocalTime(rtp + 90000)->us(), (now + TimeDelta::Seconds(1)).us());
  uint32_t new_rtp = 1337u * 90000u;
  now += k25FpsDelay;
  ts.Update(now, new_rtp);
  new_rtp += kTicksPer25Fps;
  now += k25FpsDelay;
  ts.Update(now, new_rtp);
  SM_EXPECT_EQ(ts.ExtrapolateLocalTime(new_rtp)->us(), now.us());
}

SM_TEST(VcmTiming, ExtrapolatorGapInReceivedFrames) {
  Timestamp now = Timestamp::Seconds(std::numeric_limits<uint32_t>::max() / 90000 - 31);
  TimestampExtrapolator ts(now);
  uint32_t rtp = std::numeric_limits<uint32_t>::max();
  now += k25FpsDelay;
  ts.Update(now, rtp);
  rtp += 30 * 90000;
  now += TimeDelta::Seconds(30);
  ts.Update(now, rtp);
  SM_EXPECT_EQ(ts.ExtrapolateLocalTime(rtp)->us(), now.us());
}

// --- InterFrameDelayVariationCalculator ---

SM_TEST(VcmTiming, IfdvFirstFrameIsZeroAndOldTimestampIsRejected) {
  InterFrameDelayVariationCalculator c;
  Timestamp now = Timestamp::Millis(1000);
  SM_EXPECT_EQ(c.Calculate(180000, now)->us(), 0);
  SM_EXPECT_FALSE(c.Calculate(180000 - 3000, now + TimeDelta::Millis(10)).has_value());
  // 3000 ticks = 33.333 ms of RTP time arriving after 40 ms: +6.667 ms.
  SM_EXPECT_EQ(c.Calculate(183000, now + TimeDelta::Millis(40))->us(), 6667);
}

SM_TEST(VcmTiming, IfdvRtpDeltaTruncatesToTheMicrosecond) {
  // `d_rtp_ticks / k90kHz` is int64 / Frequency (api/units/frequency.h): 3005 ticks are 33388.9 us,
  // truncated to 33388, not rounded to 33389.
  InterFrameDelayVariationCalculator c;
  const Timestamp now = Timestamp::Millis(1000);
  SM_EXPECT_EQ(c.Calculate(180000, now)->us(), 0);
  SM_EXPECT_EQ(c.Calculate(183005, now + TimeDelta::Micros(33388))->us(), 0);
}

SM_TEST(VcmTiming, JitterFrameRateIsWholeMillihertz) {
  // JitterEstimator::GetFrameRate is `1 / mean_frame_period`, a Frequency in whole millihertz: a 30001 us
  // period is 33.332 Hz, not 33.3322.
  JitterEstimator e;
  Timestamp now = Timestamp::Seconds(1);
  e.UpdateEstimate(TimeDelta::Zero(), DataSize::Bytes(1000), now);  // Sets the previous frame size only.
  now += TimeDelta::Micros(30001);
  e.UpdateEstimate(TimeDelta::Zero(), DataSize::Bytes(1000), now);
  now += TimeDelta::Micros(30001);
  e.UpdateEstimate(TimeDelta::Zero(), DataSize::Bytes(1000), now);
  SM_EXPECT_TRUE(e.frame_rate_hz() == 33332 * (1 / 1000.0));
}

SM_TEST(VcmTiming, JitterFrameRateMeanIsWelfordsAsRollingAccumulator) {
  // fps_counter_ is RollingAccumulator<uint64_t>::ComputeMean, RunningStatistics' Welford mean
  // (#674). On these 7 periods it is 33345.999999999993 us, not the exact 33346, so
  // TimeDelta::Micros truncates it to 33345 us: 1e9 / 33345 = 29989 mHz (sum / count gives 29988).
  JitterEstimator e;
  Timestamp now = Timestamp::Seconds(1);
  e.UpdateEstimate(TimeDelta::Zero(), DataSize::Bytes(1000), now);  // Sets the previous frame size only.
  now += TimeDelta::Micros(33333);
  e.UpdateEstimate(TimeDelta::Zero(), DataSize::Bytes(1000), now);  // First update time, no period yet.
  for (int64_t period_us : {33314, 33340, 33366, 33354, 33324, 33329, 33395}) {
    now += TimeDelta::Micros(period_us);
    e.UpdateEstimate(TimeDelta::Zero(), DataSize::Bytes(1000), now);
  }
  SM_EXPECT_TRUE(e.frame_rate_hz() == 29989 * (1 / 1000.0));
}

SM_TEST(VcmTiming, RollingMeanKeepsTheLast30SamplesWithWelfordsUpdate) {
  RollingMean m;
  SM_EXPECT_TRUE(m.ComputeMean() == 0.0);  // GetMean().value_or(0) with no samples.
  // RunningStatistics' update, written out: add while the window fills, then remove the oldest.
  uint64_t samples[45];
  for (int i = 0; i < 45; ++i) samples[i] = 33000 + static_cast<uint64_t>((i * 7919) % 701);
  double mean = 0;
  int64_t size = 0;
  for (int i = 0; i < 45; ++i) {
    if (size == 30) {
      --size;
      mean -= (static_cast<double>(samples[i - 30]) - mean) / static_cast<double>(size);
    }
    ++size;
    mean += (static_cast<double>(samples[i]) - mean) / static_cast<double>(size);
    m.AddSample(samples[i]);
    SM_EXPECT_TRUE(m.ComputeMean() == mean);
  }
  // Only the last 30 samples count.
  double sum = 0;
  for (int i = 15; i < 45; ++i) sum += static_cast<double>(samples[i]);
  SM_EXPECT_TRUE(std::fabs(m.ComputeMean() - sum / 30) < 1e-6);
  m.Reset();
  SM_EXPECT_TRUE(m.ComputeMean() == 0.0);
  m.AddSample(40000);
  SM_EXPECT_TRUE(m.ComputeMean() == 40000.0);
}

// --- DecodeTimePercentileFilter ---

SM_TEST(VcmTiming, DecodeTimeFilterIgnoresFirstFiveSamples) {
  DecodeTimePercentileFilter f;
  SM_EXPECT_EQ(f.GetPercentileMs(), 0);
  for (int i = 0; i < 5; ++i) f.AddSample(10, i);
  SM_EXPECT_EQ(f.GetPercentileMs(), 0);
  f.AddSample(10, 5);
  SM_EXPECT_EQ(f.GetPercentileMs(), 10);
  f.AddSample(-5, 6);  // Negative samples count as 0.
  SM_EXPECT_EQ(f.GetPercentileMs(), 0);
}

SM_TEST(VcmTiming, DecodeTimeFilterReturns95thPercentileOver10s) {
  DecodeTimePercentileFilter f;
  for (int i = 0; i < 5; ++i) f.AddSample(0, 0);
  for (int i = 1; i <= 100; ++i) f.AddSample(i, i);
  SM_EXPECT_EQ(f.GetPercentileMs(), 95);  // Index floor(0.95 x 99) = 94.
  f.AddSample(1, 10'101);                 // Everything before t = 101 ms falls out.
  SM_EXPECT_EQ(f.GetPercentileMs(), 1);
}

// --- VcmTiming ---

SM_TEST(VcmTiming, UseLowLatencyRenderer) {
  VcmTiming t;
  SM_EXPECT_FALSE(t.UseLowLatencyRendering());
  t.set_playout_delay(TimeDelta::Millis(10), TimeDelta::Millis(20));
  SM_EXPECT_FALSE(t.UseLowLatencyRendering());
  t.set_playout_delay(TimeDelta::Zero(), TimeDelta::Millis(20));
  SM_EXPECT_TRUE(t.UseLowLatencyRendering());
  t.set_playout_delay(TimeDelta::Zero(), TimeDelta::Millis(500));
  SM_EXPECT_TRUE(t.UseLowLatencyRendering());
  t.set_playout_delay(TimeDelta::Zero(), TimeDelta::Millis(501));
  SM_EXPECT_FALSE(t.UseLowLatencyRendering());
}

SM_TEST(VcmTiming, UpdateCurrentDelayCapsWhenOffByMicroseconds) {
  VcmTimingConfig cfg;
  cfg.render_delay = TimeDelta::Millis(15);
  VcmTiming t(cfg);
  t.set_min_playout_delay(TimeDelta::Millis(200));
  t.UpdateCurrentDelay(Timestamp::Millis(900), Timestamp::Millis(1000));
  t.set_min_playout_delay(TimeDelta::Millis(50));
  const Timestamp decode_time = Timestamp::Millis(1337);
  t.UpdateCurrentDelay(decode_time + TimeDelta::Millis(15) + TimeDelta::Micros(37), decode_time);
  SM_EXPECT_EQ(t.current_delay().us(), t.TargetDelay().us());
}

SM_TEST(VcmTiming, CurrentDelayGrowsOnlyForLateFramesAndCapsAtTarget) {
  VcmTimingConfig cfg;
  cfg.render_delay = TimeDelta::Millis(15);
  const Timestamp now = Timestamp::Millis(0);
  {  // IncreasesCurrentDelayWhenFrameIsLate.
    VcmTiming t(cfg);
    t.SetMinimumDelay(TimeDelta::Millis(100));
    SM_EXPECT_EQ(t.current_delay().ms(), 100);
    SM_EXPECT_EQ(t.TargetDelay().ms(), 115);
    t.UpdateCurrentDelay(now + TimeDelta::Millis(15), now + TimeDelta::Millis(4));
    SM_EXPECT_EQ(t.current_delay().ms(), 104);
  }
  {  // CapsCurrentDelayIncreaseToTarget.
    VcmTiming t(cfg);
    t.SetMinimumDelay(TimeDelta::Millis(100));
    t.UpdateCurrentDelay(now + TimeDelta::Millis(15), now + TimeDelta::Millis(588));
    SM_EXPECT_EQ(t.current_delay().ms(), 115);
  }
  {  // KeepsCurrentDelayWhenFrameIsEarly.
    VcmTiming t(cfg);
    t.SetMinimumDelay(TimeDelta::Millis(100));
    t.UpdateCurrentDelay(now + TimeDelta::Millis(30), now);
    SM_EXPECT_EQ(t.current_delay().ms(), 100);
  }
  {  // DecreasesCurrentDelayToTarget.
    VcmTiming t(cfg);
    t.SetMinimumDelay(TimeDelta::Millis(100));
    t.UpdateCurrentDelay(now, now + TimeDelta::Millis(588));
    SM_EXPECT_EQ(t.current_delay().us(), t.TargetDelay().us());
    t.SetMinimumDelay(TimeDelta::Millis(50));
    SM_EXPECT_EQ(t.TargetDelay().ms(), 65);
    t.UpdateCurrentDelay(now + TimeDelta::Millis(15), now);
    SM_EXPECT_EQ(t.current_delay().ms(), 65);
  }
}

SM_TEST(VcmTiming, RenderTimeIsExtrapolatedLocalTimePlusClampedCurrentDelay) {
  VcmTiming t;
  Timestamp now = Timestamp::Millis(1000);
  SM_EXPECT_EQ(t.RenderTime(90000, now).us(), now.us());  // No timestamps yet: now.
  for (int i = 0; i < 3; ++i) {
    t.OnCompleteFrame(90000 + static_cast<uint32_t>(i * 3000), now, false);
    if (i < 2) now += TimeDelta::Micros(33'333);
  }
  t.SetMinimumDelay(TimeDelta::Millis(40));
  SM_EXPECT_NEAR(static_cast<double>((t.RenderTime(96000, now) - now).us()) / 1000.0, 40.0, 1.0);
  t.set_min_playout_delay(TimeDelta::Millis(300));
  SM_EXPECT_NEAR(static_cast<double>((t.RenderTime(96000, now) - now).us()) / 1000.0, 300.0, 1.0);
  // A retransmitted frame does not update the extrapolator.
  t.OnCompleteFrame(200000, now + TimeDelta::Seconds(5), true);
  SM_EXPECT_NEAR(static_cast<double>((t.RenderTime(96000, now) - now).us()) / 1000.0, 300.0, 1.0);
}

// --- FrameReleaseController ---

namespace {
struct ReleaseHarness {
  VcmTiming timing;
  FrameReleaseController release{&timing};
  Timestamp now = Timestamp::Millis(1000);
  int64_t next_id = 1;
  uint32_t next_rtp = 90000;
  ReleaseFrame Make(bool keyframe, Timestamp receive) {
    ReleaseFrame f;
    f.id = next_id++;
    f.rtp_timestamp = next_rtp;
    next_rtp += 3000;
    f.keyframe = keyframe;
    f.size = DataSize::Bytes(keyframe ? 20000 : 4000);
    f.receive_time = receive;
    return f;
  }
};
}  // namespace

SM_TEST(VcmTiming, ReleaseFirstKeyframeAtOnceDroppingFramesBeforeIt) {
  ReleaseHarness h;
  h.release.Insert(h.Make(false, h.now));
  h.release.Insert(h.Make(true, h.now));
  auto out = h.release.Process(h.now, TimeDelta::Zero());
  SM_ASSERT_EQ(out.size(), 2u);
  SM_EXPECT_FALSE(out[0].decode);
  SM_EXPECT_TRUE(out[1].decode);
  SM_EXPECT_EQ(h.release.stats().dropped_for_keyframe, 1u);
}

namespace {
// 60 frames at 30 fps, each inserted and released when it arrives on time.
void WarmUp(ReleaseHarness& h) {
  h.release.Insert(h.Make(true, h.now));
  h.release.Process(h.now, TimeDelta::Zero());
  for (int i = 0; i < 60; ++i) {
    h.now += TimeDelta::Micros(33'333);
    h.release.Insert(h.Make(false, h.now));
    for (Timestamp t = h.now; t < h.now + TimeDelta::Micros(33'333); t += TimeDelta::Millis(1)) {
      h.release.Process(t, TimeDelta::Zero());
    }
  }
  h.now += TimeDelta::Micros(33'333);
}
}  // namespace

SM_TEST(VcmTiming, ReleaseLateDeltaFrameIsDecodedWithoutKeyframeBehindIt) {
  // libwebrtc has no 10 ms late-drop rule: with only delta frames queued,
  // the next one is the last decodable unit and is released however late.
  ReleaseHarness h;
  WarmUp(h);
  SM_EXPECT_EQ(h.release.queued(), 0u);
  // Three frames held up by 500 ms arrive together.
  h.now += TimeDelta::Millis(500);
  for (int i = 0; i < 3; ++i) h.release.Insert(h.Make(false, h.now));
  auto out = h.release.Process(h.now, TimeDelta::Zero());
  SM_ASSERT_EQ(out.size(), 3u);
  for (const auto& a : out) SM_EXPECT_TRUE(a.decode);
  SM_EXPECT_GT(out[0].lateness.ms(), 300);  // The extrapolator absorbs part of the 500 ms.
  SM_EXPECT_EQ(h.release.stats().fast_forwarded, 0u);
}

SM_TEST(VcmTiming, ReleaseFastForwardsToALaterKeyframe) {
  ReleaseHarness h;
  WarmUp(h);
  h.now += TimeDelta::Millis(500);
  h.release.Insert(h.Make(false, h.now));
  h.release.Insert(h.Make(false, h.now));
  h.release.Insert(h.Make(true, h.now));
  auto out = h.release.Process(h.now, TimeDelta::Zero());
  SM_ASSERT_EQ(out.size(), 3u);
  SM_EXPECT_FALSE(out[0].decode);
  SM_EXPECT_FALSE(out[1].decode);
  SM_EXPECT_TRUE(out[2].decode);
  SM_EXPECT_EQ(h.release.stats().fast_forwarded, 2u);
}

SM_TEST(VcmTiming, ReleaseWaitsUntilRenderTimeMinusDecodeAndRenderDelay) {
  ReleaseHarness h;
  h.release.Insert(h.Make(true, h.now));
  h.release.Process(h.now, TimeDelta::Zero());
  h.timing.SetMinimumDelay(TimeDelta::Millis(100));
  // On time: arrives with its capture time; due at local + 100 ms - 10 ms.
  const Timestamp arrival = h.now + TimeDelta::Micros(33'333);
  h.release.Insert(h.Make(false, arrival));
  auto out = h.release.Process(arrival, TimeDelta::Zero());
  SM_EXPECT_EQ(out.size(), 0u);
  const Timestamp due = h.release.NextWakeup();
  SM_EXPECT_GT(due.ms(), arrival.ms());
  SM_EXPECT_EQ(h.release.Process(due - TimeDelta::Millis(1), TimeDelta::Zero()).size(), 0u);
  out = h.release.Process(due, TimeDelta::Zero());
  SM_ASSERT_EQ(out.size(), 1u);
  SM_EXPECT_TRUE(out[0].decode);
  SM_EXPECT_EQ((out[0].render_time - due).ms(), 10);
}
