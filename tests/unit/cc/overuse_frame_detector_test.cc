// CpuOveruseDetector version 2: libwebrtc M157's OveruseFrameDetector with
// SendProcessingUsage1 (issue #84 step 3). Ported from
// compare/webrtc/reference/video/adaptation/overuse_frame_detector_unittest.cc
// (OveruseFrameDetectorTest), with the same fixture: 640x480 frames, a
// simulated clock starting at 12345 ms, min_process_count 0 and the helpers
// that capture a frame, send it `delay` later and advance to the next frame.
// Not ported: the two random-interval tests (they depend on libwebrtc's
// Random(17) sequence) and OveruseFrameDetectorTest2 (SendProcessingUsage2,
// off by default).
#include <initializer_list>

#include "sm/cc/video_adaptation.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::cc;

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 480;
// Corresponds to load of 15%.
constexpr TimeDelta kFrameInterval = TimeDelta::Millis(33);
constexpr TimeDelta kProcessTime = TimeDelta::Millis(5);

struct Fixture {
  CpuOveruseConfig options = [] {
    CpuOveruseConfig c;
    c.version = 2;
    c.min_process_count = 0;
    return c;
  }();
  Timestamp now = Timestamp::Millis(12345);
  CpuOveruseDetector detector{options};
  int overuse = 0, normaluse = 0;

  void SetOptions(const CpuOveruseConfig& o) {
    options = o;
    detector = CpuOveruseDetector(o);
  }
  void CheckForOveruse() {
    const auto s = detector.CheckForOveruse(now);
    if (s == CpuOveruseDetector::Signal::kOveruse) ++overuse;
    if (s == CpuOveruseDetector::Signal::kUnderuse) ++normaluse;
  }
  int UsagePercent() const { return detector.encode_usage_percent().value_or(-1); }
  int InitialUsage() const {
    return static_cast<int>(
        ((options.low_encode_usage_threshold_percent + options.high_encode_usage_threshold_percent) / 2.0f) + 0.5);
  }
  void InsertAndSendFramesWithInterval(int num_frames, TimeDelta interval, int width, int height, TimeDelta delay) {
    uint32_t timestamp = 0;
    while (num_frames-- > 0) {
      detector.OnFrameCaptured(width * height, timestamp, now);
      now += delay;
      detector.OnFrameSent(timestamp, now);
      now += interval - delay;
      timestamp += static_cast<uint32_t>(interval.us() * 90 / 1000);
    }
  }
  void InsertAndSendSimulcastFramesWithInterval(int num_frames, TimeDelta interval, int width, int height,
                                                std::initializer_list<TimeDelta> delays) {
    uint32_t timestamp = 0;
    while (num_frames-- > 0) {
      detector.OnFrameCaptured(width * height, timestamp, now);
      TimeDelta max_delay = TimeDelta::Zero();
      for (TimeDelta delay : delays) {
        if (delay > max_delay) {
          now += delay - max_delay;
          max_delay = delay;
        }
        detector.OnFrameSent(timestamp, now);
      }
      CheckForOveruse();
      now += interval - max_delay;
      timestamp += static_cast<uint32_t>(interval.us() * 90 / 1000);
    }
  }
  void ForceUpdate(int width, int height) {
    InsertAndSendFramesWithInterval(2, TimeDelta::Seconds(1), width, height, kFrameInterval);
  }
  void TriggerOveruse(int num_times) {
    const TimeDelta kDelay = TimeDelta::Millis(32);
    for (int i = 0; i < num_times; ++i) {
      InsertAndSendFramesWithInterval(1000, kFrameInterval, kWidth, kHeight, kDelay);
      CheckForOveruse();
    }
  }
  void TriggerUnderuse() {
    InsertAndSendFramesWithInterval(1300, kFrameInterval, kWidth, kHeight, TimeDelta::Micros(5000));
    InsertAndSendFramesWithInterval(1, kFrameInterval, kWidth, kHeight, TimeDelta::Micros(6000));
    CheckForOveruse();
  }
  TimeDelta OveruseProcessingTimeLimitForFramerate(int fps) const {
    return (TimeDelta::Seconds(1) / fps * options.high_encode_usage_threshold_percent) / 100;
  }
  TimeDelta UnderuseProcessingTimeLimitForFramerate(int fps) const {
    return (TimeDelta::Seconds(1) / fps * options.low_encode_usage_threshold_percent) / 100;
  }
};

}  // namespace

SM_TEST(OveruseFrameDetector, DefaultsMatchLibwebrtc) {
  // CpuOveruseOptions in overuse_frame_detector.h, as GetCpuOveruseOptions
  // keeps them for a software encoder.
  CpuOveruseConfig c;
  SM_EXPECT_EQ(c.high_encode_usage_threshold_percent, 85);
  SM_EXPECT_EQ(c.low_encode_usage_threshold_percent, 42);
  SM_EXPECT_EQ(c.frame_timeout_interval.ms(), 1500);
  SM_EXPECT_EQ(c.min_frame_samples, 120);
  SM_EXPECT_EQ(c.min_process_count, 3);
  SM_EXPECT_EQ(c.consecutive_checks, 2);
}

SM_TEST(OveruseFrameDetector, TriggerOveruse) {
  Fixture f;
  f.TriggerOveruse(f.options.consecutive_checks);
  SM_EXPECT_EQ(f.overuse, 1);
}

SM_TEST(OveruseFrameDetector, OveruseAndRecover) {
  Fixture f;
  f.TriggerOveruse(f.options.consecutive_checks);
  SM_EXPECT_EQ(f.overuse, 1);
  f.TriggerUnderuse();
  SM_EXPECT_GE(f.normaluse, 1);
}

SM_TEST(OveruseFrameDetector, DoubleOveruseAndRecover) {
  Fixture f;
  f.TriggerOveruse(f.options.consecutive_checks);
  f.TriggerOveruse(f.options.consecutive_checks);
  SM_EXPECT_EQ(f.overuse, 2);
  f.TriggerUnderuse();
  SM_EXPECT_GE(f.normaluse, 1);
}

SM_TEST(OveruseFrameDetector, TriggerUnderuseWithMinProcessCount) {
  Fixture f;
  auto o = f.options;
  o.min_process_count = 1;
  f.SetOptions(o);
  f.InsertAndSendFramesWithInterval(1200, kFrameInterval, kWidth, kHeight, kProcessTime);
  f.CheckForOveruse();
  SM_EXPECT_EQ(f.normaluse, 0);
  f.now += TimeDelta::Seconds(5);
  f.CheckForOveruse();
  SM_EXPECT_EQ(f.normaluse, 1);
}

SM_TEST(OveruseFrameDetector, ConstantOveruseGivesNoNormalUsage) {
  Fixture f;
  for (int i = 0; i < 64; ++i) f.TriggerOveruse(f.options.consecutive_checks);
  SM_EXPECT_EQ(f.overuse, 64);
  SM_EXPECT_EQ(f.normaluse, 0);
}

SM_TEST(OveruseFrameDetector, ConsecutiveCountTriggersOveruse) {
  Fixture f;
  auto o = f.options;
  o.consecutive_checks = 2;
  f.SetOptions(o);
  f.TriggerOveruse(2);
  SM_EXPECT_EQ(f.overuse, 1);
}

SM_TEST(OveruseFrameDetector, IncorrectConsecutiveCountTriggersNoOveruse) {
  Fixture f;
  auto o = f.options;
  o.consecutive_checks = 2;
  f.SetOptions(o);
  f.TriggerOveruse(1);
  SM_EXPECT_EQ(f.overuse, 0);
}

SM_TEST(OveruseFrameDetector, ProcessingUsage) {
  Fixture f;
  f.InsertAndSendFramesWithInterval(1000, kFrameInterval, kWidth, kHeight, kProcessTime);
  SM_EXPECT_EQ(f.UsagePercent(), static_cast<int>(kProcessTime.us() * 100 / kFrameInterval.us()));
}

SM_TEST(OveruseFrameDetector, ResetAfterResolutionChange) {
  Fixture f;
  f.ForceUpdate(kWidth, kHeight);
  SM_EXPECT_EQ(f.UsagePercent(), f.InitialUsage());
  f.InsertAndSendFramesWithInterval(1000, kFrameInterval, kWidth, kHeight, kProcessTime);
  SM_EXPECT_NE(f.UsagePercent(), f.InitialUsage());
  // Verify reset (with new width/height).
  f.ForceUpdate(kWidth, kHeight + 1);
  SM_EXPECT_EQ(f.UsagePercent(), f.InitialUsage());
}

SM_TEST(OveruseFrameDetector, ResetAfterFrameTimeout) {
  Fixture f;
  f.ForceUpdate(kWidth, kHeight);
  SM_EXPECT_EQ(f.UsagePercent(), f.InitialUsage());
  f.InsertAndSendFramesWithInterval(1000, kFrameInterval, kWidth, kHeight, kProcessTime);
  SM_EXPECT_NE(f.UsagePercent(), f.InitialUsage());
  f.InsertAndSendFramesWithInterval(2, f.options.frame_timeout_interval, kWidth, kHeight, kProcessTime);
  SM_EXPECT_NE(f.UsagePercent(), f.InitialUsage());
  // Verify reset.
  f.InsertAndSendFramesWithInterval(2, f.options.frame_timeout_interval + TimeDelta::Millis(1), kWidth, kHeight,
                                    kProcessTime);
  f.ForceUpdate(kWidth, kHeight);
  SM_EXPECT_EQ(f.UsagePercent(), f.InitialUsage());
}

SM_TEST(OveruseFrameDetector, MinFrameSamplesBeforeUpdating) {
  Fixture f;
  auto o = f.options;
  o.min_frame_samples = 40;
  f.SetOptions(o);
  f.InsertAndSendFramesWithInterval(40, kFrameInterval, kWidth, kHeight, kProcessTime);
  SM_EXPECT_EQ(f.UsagePercent(), f.InitialUsage());
  // Pass time far enough to digest all previous samples.
  f.now += TimeDelta::Seconds(1);
  f.InsertAndSendFramesWithInterval(1, kFrameInterval, kWidth, kHeight, kProcessTime);
  // The last sample has not been processed here.
  SM_EXPECT_EQ(f.UsagePercent(), f.InitialUsage());
  // Pass time far enough to digest all previous samples, 41 in total.
  f.now += TimeDelta::Seconds(1);
  f.InsertAndSendFramesWithInterval(1, kFrameInterval, kWidth, kHeight, kProcessTime);
  SM_EXPECT_NE(f.UsagePercent(), f.InitialUsage());
}

SM_TEST(OveruseFrameDetector, InitialProcessingUsage) {
  Fixture f;
  f.ForceUpdate(kWidth, kHeight);
  SM_EXPECT_EQ(f.UsagePercent(), f.InitialUsage());
  SM_EXPECT_EQ(f.InitialUsage(), 64);  // (42 + 85) / 2, rounded.
}

SM_TEST(OveruseFrameDetector, MeasuresMultipleConcurrentSamples) {
  Fixture f;
  const TimeDelta kInterval = TimeDelta::Millis(33);
  const uint32_t kNumFramesEncodingDelay = 3;
  for (uint32_t i = 0; i < 1000; ++i) {
    // Unique timestamps.
    f.detector.OnFrameCaptured(kWidth * kHeight, i, f.now);
    f.now += kInterval;
    if (i > kNumFramesEncodingDelay) f.detector.OnFrameSent(i - kNumFramesEncodingDelay, f.now);
    f.CheckForOveruse();
  }
  SM_EXPECT_GE(f.overuse, 1);
}

SM_TEST(OveruseFrameDetector, UpdatesExistingSamples) {
  // >85% encoding time should trigger overuse.
  Fixture f;
  const TimeDelta kInterval = TimeDelta::Millis(33);
  const TimeDelta kDelay = TimeDelta::Millis(30);
  uint32_t timestamp = 0;
  for (int i = 0; i < 1000; ++i) {
    f.detector.OnFrameCaptured(kWidth * kHeight, timestamp, f.now);
    // Encode and send first parts almost instantly.
    f.now += TimeDelta::Millis(1);
    f.detector.OnFrameSent(timestamp, f.now);
    // Encode heavier part, resulting in >85% usage total.
    f.now += kDelay - TimeDelta::Millis(1);
    f.detector.OnFrameSent(timestamp, f.now);
    f.now += kInterval - kDelay;
    timestamp += static_cast<uint32_t>(kInterval.us() * 90 / 1000);
    f.CheckForOveruse();
  }
  SM_EXPECT_GE(f.overuse, 1);
}

SM_TEST(OveruseFrameDetector, RunOnTqNormalUsage) {
  // The repeating task (Check): normal usage eventually signals AdaptUp.
  Fixture f;
  f.detector.Check(f.now);  // StartCheckForOveruse.
  uint32_t timestamp = 0;
  bool up = false;
  for (int i = 0; i < 1301 && !up; ++i) {
    const TimeDelta delay = i < 1300 ? TimeDelta::Millis(5) : TimeDelta::Millis(6);
    f.detector.OnFrameCaptured(kWidth * kHeight, timestamp, f.now);
    f.now += delay;
    f.detector.OnFrameSent(timestamp, f.now);
    f.now += kFrameInterval - delay;
    timestamp += static_cast<uint32_t>(kFrameInterval.us() * 90 / 1000);
    up = f.detector.Check(f.now) == CpuOveruseDetector::Signal::kUnderuse;
  }
  SM_EXPECT_TRUE(up);
}

SM_TEST(OveruseFrameDetector, CheckScheduleIsDelayedStartThenEveryFiveSeconds) {
  // RepeatingTaskHandle::DelayedStart(kTimeToFirstCheckForOveruseMs = 100),
  // then kCheckForOveruseIntervalMs = 5000 after each run. With the default
  // min_process_count 3, the 4th run is the first that can act.
  CpuOveruseConfig c;
  c.version = 2;
  CpuOveruseDetector d(c);
  Timestamp t = Timestamp::Seconds(100);
  d.Check(t);
  // Heavy encoding (32 of 33 ms) from the start.
  uint32_t ts = 0;
  int overuse_at_ms = -1;
  for (int i = 0; i < 1000 && overuse_at_ms < 0; ++i) {
    d.OnFrameCaptured(kWidth * kHeight, ts, t);
    d.OnFrameSent(ts, t + TimeDelta::Millis(32));
    for (int ms = 0; ms < 33; ++ms) {
      if (d.Check(t + TimeDelta::Millis(ms)) == CpuOveruseDetector::Signal::kOveruse) {
        overuse_at_ms = static_cast<int>((t + TimeDelta::Millis(ms) - Timestamp::Seconds(100)).ms());
        break;
      }
    }
    t += TimeDelta::Millis(33);
    ts += 33 * 90;
  }
  // Runs at 0.1, 5.1 and 10.1 s are runs 1-3 (<= min_process_count, skipped);
  // 15.1 s is above the threshold once, 20.1 s twice in a row: overuse.
  SM_EXPECT_EQ(overuse_at_ms, 20100);
}

SM_TEST(OveruseFrameDetector, MaxIntervalScalesWithFramerate) {
  Fixture f;
  const int kCapturerMaxFrameRate = 30;
  const int kEncodeMaxFrameRate = 20;  // Maximum fps the encoder can sustain.
  // Trigger overuse.
  TimeDelta frame_interval = TimeDelta::Seconds(1) / kCapturerMaxFrameRate;
  // Processing time just below over use limit given kEncodeMaxFrameRate.
  TimeDelta processing_time = (f.OveruseProcessingTimeLimitForFramerate(kEncodeMaxFrameRate) * 98) / 100;
  for (int i = 0; i < f.options.consecutive_checks; ++i) {
    f.InsertAndSendFramesWithInterval(1200, frame_interval, kWidth, kHeight, processing_time);
    f.CheckForOveruse();
  }
  SM_EXPECT_EQ(f.overuse, 1);
  // Simulate frame rate reduction and normal usage.
  frame_interval = TimeDelta::Seconds(1) / kEncodeMaxFrameRate;
  f.detector.OnTargetFramerateUpdated(kEncodeMaxFrameRate);
  for (int i = 0; i < f.options.consecutive_checks; ++i) {
    f.InsertAndSendFramesWithInterval(1200, frame_interval, kWidth, kHeight, processing_time);
    f.CheckForOveruse();
  }
  SM_EXPECT_EQ(f.overuse, 1);
  // Reduce processing time to trigger underuse.
  processing_time = (f.UnderuseProcessingTimeLimitForFramerate(kEncodeMaxFrameRate) * 98) / 100;
  f.InsertAndSendFramesWithInterval(1200, frame_interval, kWidth, kHeight, processing_time);
  f.CheckForOveruse();
  SM_EXPECT_EQ(f.normaluse, 1);
}

SM_TEST(OveruseFrameDetector, RespectsMinFramerate) {
  Fixture f;
  const int kMinFrameRate = 7;  // Minimum fps allowed by current detector impl.
  f.detector.OnTargetFramerateUpdated(kMinFrameRate);
  // Normal usage just at the limit.
  const TimeDelta frame_interval = TimeDelta::Seconds(1) / kMinFrameRate;
  TimeDelta processing_time = (f.OveruseProcessingTimeLimitForFramerate(kMinFrameRate) * 98) / 100;
  for (int i = 0; i < f.options.consecutive_checks; ++i) {
    f.InsertAndSendFramesWithInterval(1200, frame_interval, kWidth, kHeight, processing_time);
    f.CheckForOveruse();
  }
  SM_EXPECT_EQ(f.overuse, 0);
  // Over the limit to overuse.
  processing_time = (f.OveruseProcessingTimeLimitForFramerate(kMinFrameRate) * 102) / 100;
  for (int i = 0; i < f.options.consecutive_checks; ++i) {
    f.InsertAndSendFramesWithInterval(1200, frame_interval, kWidth, kHeight, processing_time);
    f.CheckForOveruse();
  }
  SM_EXPECT_EQ(f.overuse, 1);
  // Reduce input frame rate. Should still trigger overuse.
  f.detector.OnTargetFramerateUpdated(kMinFrameRate - 1);
  for (int i = 0; i < f.options.consecutive_checks; ++i) {
    f.InsertAndSendFramesWithInterval(1200, frame_interval, kWidth, kHeight, processing_time);
    f.CheckForOveruse();
  }
  SM_EXPECT_EQ(f.overuse, 2);
}

SM_TEST(OveruseFrameDetector, LimitsMaxFrameInterval) {
  Fixture f;
  const int kMaxFrameRate = 20;
  f.detector.OnTargetFramerateUpdated(kMaxFrameRate);
  const TimeDelta frame_interval = TimeDelta::Seconds(1) / kMaxFrameRate;
  // Maximum frame interval allowed is 35% above ideal.
  TimeDelta max_frame_interval = (frame_interval * 135) / 100;
  // Maximum processing time, without triggering overuse, allowed with the
  // above frame interval.
  const TimeDelta max_processing_time = (max_frame_interval * f.options.high_encode_usage_threshold_percent) / 100;
  // Processing time just below overuse limit given kMaxFrameRate.
  TimeDelta processing_time = (max_processing_time * 98) / 100;
  for (int i = 0; i < f.options.consecutive_checks; ++i) {
    f.InsertAndSendFramesWithInterval(1200, max_frame_interval, kWidth, kHeight, processing_time);
    f.CheckForOveruse();
  }
  SM_EXPECT_EQ(f.overuse, 0);
  // Go above limit, trigger overuse.
  processing_time = (max_processing_time * 102) / 100;
  for (int i = 0; i < f.options.consecutive_checks; ++i) {
    f.InsertAndSendFramesWithInterval(1200, max_frame_interval, kWidth, kHeight, processing_time);
    f.CheckForOveruse();
  }
  SM_EXPECT_EQ(f.overuse, 1);
  // Increase frame interval, should still trigger overuse.
  max_frame_interval = max_frame_interval * 2;
  for (int i = 0; i < f.options.consecutive_checks; ++i) {
    f.InsertAndSendFramesWithInterval(1200, max_frame_interval, kWidth, kHeight, processing_time);
    f.CheckForOveruse();
  }
  SM_EXPECT_EQ(f.overuse, 2);
}

SM_TEST(OveruseFrameDetector, NoOveruseForSimulcast) {
  // Models simulcast, with multiple encoded frames for each input frame.
  Fixture f;
  f.InsertAndSendSimulcastFramesWithInterval(500, TimeDelta::Millis(30), kWidth, kHeight,
                                             {TimeDelta::Millis(10), TimeDelta::Millis(8), TimeDelta::Millis(12)});
  SM_EXPECT_EQ(f.overuse, 0);
  // Average usage 40%. 12 ms / 30 ms.
  SM_EXPECT_GE(f.UsagePercent(), 35);
  SM_EXPECT_LE(f.UsagePercent(), 45);
}
