// BitrateAdjuster (common_video/bitrate_adjuster.cc), as RTCVideoEncoderH264
// uses it: min 0.5, max 0.95 of the target (#1104).
#include "sm/video/bitrate_adjuster.h"

#include "sm_test.h"

using namespace sm;
using namespace sm::video;

namespace {

// Feeds `seconds` of 30 fps frames at `bps`, starting at `start_ms`.
int64_t Feed(BitrateAdjuster& a, double bps, int64_t start_ms, double seconds) {
  const size_t frame_bytes = static_cast<size_t>(bps / 8 / 30);
  const int frames = static_cast<int>(seconds * 30);
  int64_t t = start_ms;
  for (int i = 0; i < frames; ++i) {
    t = start_ms + i * 1000 / 30;
    a.Update(frame_bytes, Timestamp::Millis(t));
  }
  return t;
}

}  // namespace

SM_TEST(BitrateAdjuster, StartsAtTarget) {
  BitrateAdjuster a(0.5f, 0.95f);
  a.SetTargetBitrateBps(1'000'000);
  SM_EXPECT_EQ(a.GetAdjustedBitrateBps(), 1'000'000u);
  // A change within 10% keeps the adjusted rate until the next adjustment.
  a.SetTargetBitrateBps(1'050'000);
  SM_EXPECT_EQ(a.GetAdjustedBitrateBps(), 1'000'000u);
  // A larger one is taken at once.
  a.SetTargetBitrateBps(500'000);
  SM_EXPECT_EQ(a.GetAdjustedBitrateBps(), 500'000u);
}

SM_TEST(BitrateAdjuster, SmallStepsAddUpPastTolerance) {
  BitrateAdjuster a(0.5f, 0.95f);
  a.SetTargetBitrateBps(1'000'000);
  a.SetTargetBitrateBps(1'060'000);  // 6% from the last target and from the adjusted one.
  SM_EXPECT_EQ(a.GetAdjustedBitrateBps(), 1'000'000u);
  a.SetTargetBitrateBps(1'120'000);  // 5.7% from the last target, 12% from the target last adjusted against.
  SM_EXPECT_EQ(a.GetAdjustedBitrateBps(), 1'120'000u);
}

SM_TEST(BitrateAdjuster, OvershootLowersToMin) {
  BitrateAdjuster a(0.5f, 0.95f);
  a.SetTargetBitrateBps(1'000'000);
  Feed(a, 3'000'000, 1000, 1.1);
  // target + (target - 3 * target) / 2 = 0, clamped to 0.5 * target.
  SM_EXPECT_NEAR(a.GetAdjustedBitrateBps(), 500'000, 1);
}

SM_TEST(BitrateAdjuster, UndershootRaisesToMaxOnly) {
  BitrateAdjuster a(0.5f, 0.95f);
  a.SetTargetBitrateBps(1'000'000);
  Feed(a, 500'000, 1000, 1.1);
  // target + 0.25 * target, clamped to 0.95 * target.
  SM_EXPECT_NEAR(a.GetAdjustedBitrateBps(), 950'000, 1);
}

SM_TEST(BitrateAdjuster, SmallUndershootKeepsRate) {
  BitrateAdjuster a(0.5f, 0.95f);
  a.SetTargetBitrateBps(1'000'000);
  Feed(a, 950'000, 1000, 1.1);
  SM_EXPECT_EQ(a.GetAdjustedBitrateBps(), 1'000'000u);
}

SM_TEST(BitrateAdjuster, WaitsForSecondAndThirtyFrames) {
  BitrateAdjuster a(0.5f, 0.95f);
  a.SetTargetBitrateBps(1'000'000);
  // 29 frames over more than a second: no adjustment yet.
  for (int i = 0; i < 29; ++i) a.Update(20'000, Timestamp::Millis(1000 + i * 50));
  SM_EXPECT_EQ(a.GetAdjustedBitrateBps(), 1'000'000u);
  a.Update(20'000, Timestamp::Millis(1000 + 29 * 50));
  SM_EXPECT_LT(a.GetAdjustedBitrateBps(), 1'000'000u);
}
