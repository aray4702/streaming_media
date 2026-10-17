// Cases of libwebrtc's call/bitrate_allocator_unittest.cc that apply (issue #104),
// with the expected values worked out from call/bitrate_allocator.cc.
#include "sm/cc/bitrate_allocator_v2.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::cc;

namespace {
DataRate Kbps(int64_t k) { return DataRate::KilobitsPerSec(k); }
const Timestamp kNow = Timestamp::Millis(0);

StreamAllocationConfig Stream(int64_t min, int64_t max, bool enforce_min = false, double priority = 1.0) {
  StreamAllocationConfig c;
  c.min = Kbps(min);
  c.max = Kbps(max);
  c.enforce_min = enforce_min;
  c.priority = priority;
  return c;
}
}  // namespace

SM_TEST(BitrateAllocatorV2, SharesTheRestEvenlyAboveTheMinimums) {
  BitrateAllocatorV2 a;
  a.AddObserver("a", Stream(100, 400));
  a.AddObserver("b", Stream(100, 400));
  a.OnTargetChanged(Kbps(600), kNow);
  SM_EXPECT_EQ(a.allocation("a").kbps(), 300.0);
  SM_EXPECT_EQ(a.allocation("b").kbps(), 300.0);
}

SM_TEST(BitrateAllocatorV2, TheRestFollowsBitratePriority) {
  BitrateAllocatorV2 a;
  a.AddObserver("a", Stream(100, 1000, false, 2.0));
  a.AddObserver("b", Stream(100, 1000, false, 1.0));
  a.OnTargetChanged(Kbps(500), kNow);  // 300 above the minimums, shared 2:1.
  SM_EXPECT_EQ(a.allocation("a").kbps(), 300.0);
  SM_EXPECT_EQ(a.allocation("b").kbps(), 200.0);
}

SM_TEST(BitrateAllocatorV2, AStreamAtItsMaximumLeavesTheRestToTheOthers) {
  BitrateAllocatorV2 a;
  a.AddObserver("a", Stream(100, 150));
  a.AddObserver("b", Stream(100, 1000));
  a.OnTargetChanged(Kbps(600), kNow);
  SM_EXPECT_EQ(a.allocation("a").kbps(), 150.0);
  SM_EXPECT_EQ(a.allocation("b").kbps(), 450.0);
}

SM_TEST(BitrateAllocatorV2, PriorityRateIsAllocatedBeforeTheProportionalShare) {
  BitrateAllocatorV2 a;
  StreamAllocationConfig audio = Stream(20, 100, true);
  audio.priority_rate = Kbps(60);
  a.AddObserver("audio", audio);
  a.AddObserver("video", Stream(100, 1000));
  // 180 above the minimums: 40 take the audio to its priority rate, the other 140
  // are shared 1:1, and the audio is capped at its maximum (100); video gets 200.
  a.OnTargetChanged(Kbps(300), kNow);
  SM_EXPECT_EQ(a.allocation("audio").kbps(), 100.0);
  SM_EXPECT_EQ(a.allocation("video").kbps(), 200.0);
}

SM_TEST(BitrateAllocatorV2, AboveTheMaximumsEachStreamGetsUpToTwiceItsMaximum) {
  BitrateAllocatorV2 a;
  a.AddObserver("a", Stream(50, 100));
  a.AddObserver("b", Stream(50, 200));
  // Maximums first (300), then 700 shared evenly in ascending order of maximum:
  // a would get 350 more, capped at 2 x 100, and what it cannot take goes on to b,
  // which is capped at 2 x 200.
  a.OnTargetChanged(Kbps(1000), kNow);
  SM_EXPECT_EQ(a.allocation("a").kbps(), 200.0);
  SM_EXPECT_EQ(a.allocation("b").kbps(), 400.0);
}

SM_TEST(BitrateAllocatorV2, ZeroTargetAllocatesNothing) {
  BitrateAllocatorV2 a;
  a.AddObserver("a", Stream(50, 100, true));
  a.OnTargetChanged(Kbps(500), kNow);
  a.OnTargetChanged(DataRate::Zero(), kNow);
  SM_EXPECT_EQ(a.allocation("a").bps(), 0);
}

SM_TEST(BitrateAllocatorV2, EnforcedMinimumsAreFundedEvenWhenTheTargetIsBelowThem) {
  BitrateAllocatorV2 a;
  a.AddObserver("audio", Stream(50, 50, true));
  a.AddObserver("video", Stream(50, 1000, true));  // suspend_below_min_bitrate off: the default.
  a.OnTargetChanged(Kbps(40), kNow);
  SM_EXPECT_EQ(a.allocation("audio").kbps(), 50.0);
  SM_EXPECT_EQ(a.allocation("video").kbps(), 50.0);
  SM_EXPECT_EQ(a.limits().min_allocatable.kbps(), 100.0);  // The network controller's lower bound.
  SM_EXPECT_EQ(a.pause_events(), 0u);
}

SM_TEST(BitrateAllocatorV2, AStreamThatCannotBeFundedIsPausedAndResumesWithHysteresis) {
  BitrateAllocatorV2 a;
  a.AddObserver("audio", Stream(50, 200, true));
  a.AddObserver("video", Stream(100, 1000));  // May be suspended.
  a.OnTargetChanged(Kbps(120), kNow);         // Audio 50; the 70 left do not fund the video's 100.
  SM_EXPECT_TRUE(a.paused("video"));
  SM_EXPECT_EQ(a.allocation("audio").kbps(), 120.0);  // The remainder goes to the stream that has an allocation.
  // A paused stream needs min + max(10 %, 20 kbps) = 120 on top of the audio's 50.
  a.OnTargetChanged(Kbps(169), kNow);
  SM_EXPECT_TRUE(a.paused("video"));
  a.OnTargetChanged(Kbps(170), kNow);
  SM_EXPECT_EQ(a.allocation("video").kbps(), 120.0);
  SM_EXPECT_EQ(a.allocation("audio").kbps(), 50.0);
  SM_EXPECT_EQ(a.pause_events(), 1u);  // Only the resume counts: the first allocation is not a pause.
  // Once active the video needs only its plain minimum (no hysteresis): both get an even share of the 10 above the minimums.
  a.OnTargetChanged(Kbps(160), kNow);
  SM_EXPECT_EQ(a.allocation("video").kbps(), 105.0);
  SM_EXPECT_EQ(a.allocation("audio").kbps(), 55.0);
}

SM_TEST(BitrateAllocatorV2, ProtectionBitrateRaisesTheMinimumToResume) {
  BitrateAllocatorV2 a;
  a.AddObserver("video", Stream(100, 1000), [](DataRate alloc) { return alloc * 0.2; });
  a.OnTargetChanged(Kbps(200), kNow);
  SM_EXPECT_EQ(a.allocation("video").kbps(), 200.0);
  a.OnTargetChanged(Kbps(50), kNow);  // Below its minimum with 20 % protection: paused.
  SM_EXPECT_TRUE(a.paused("video"));
  // Minimum 100 + hysteresis 20 = 120, plus the protection share of last time: 120 x 1.2 = 144.
  a.OnTargetChanged(Kbps(143), kNow);
  SM_EXPECT_TRUE(a.paused("video"));
  a.OnTargetChanged(Kbps(145), kNow);
  SM_EXPECT_FALSE(a.paused("video"));
}

SM_TEST(BitrateAllocatorV2, LimitsAddUpMinimumsMaximumsAndPadding) {
  BitrateAllocatorV2 a;
  a.AddObserver("audio", Stream(50, 50, true));
  StreamAllocationConfig v = Stream(100, 1000);
  v.pad_up = Kbps(300);
  a.AddObserver("video", v);
  a.OnTargetChanged(Kbps(500), kNow);
  SM_EXPECT_EQ(a.limits().min_allocatable.kbps(), 50.0);   // Only the enforced minimum.
  SM_EXPECT_EQ(a.limits().max_allocatable.kbps(), 1050.0);
  SM_EXPECT_EQ(a.limits().max_padding.kbps(), 300.0);
  a.RemoveObserver("video");
  SM_EXPECT_EQ(a.limits().max_allocatable.kbps(), 50.0);
}
