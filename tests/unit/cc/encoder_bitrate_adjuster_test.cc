#include "sm/cc/encoder_bitrate_adjuster.h"

#include "sm_test.h"

using namespace sm;
using namespace sm::cc;

namespace {
DataRate Kbps(int64_t k) { return DataRate::KilobitsPerSec(k); }

// Encodes `n` frames of `bytes` (sized for the target) at 30 fps from `start_ms`, re-adjusting before each one as
// VideoSendController does; returns the last adjusted rate.
DataRate Run(EncoderBitrateAdjuster& a, int n, int64_t bytes, DataRate target, DataRate link, int64_t start_ms) {
  DataRate r;
  for (int i = 0; i < n; ++i) {
    const Timestamp now = Timestamp::Micros((start_ms * 1000) + i * 1000000 / 30);
    r = a.AdjustRate(target, link, 30, now);
    // `bytes` is per frame at the target rate; an encoder follows the rate it is given.
    a.OnEncodedFrame(DataSize::Bytes(bytes * r.bps() / target.bps()), now);
  }
  return r;
}
}  // namespace

// EncoderOvershootDetector (video/encoder_overshoot_detector.cc).

SM_TEST(EncoderOvershootDetector, NoDataGivesNoFactor) {
  EncoderOvershootDetector d(3000);
  d.SetTargetRate(Kbps(1000), 30, 0);
  SM_EXPECT_FALSE(d.GetNetworkRateUtilizationFactor(0).has_value());
  SM_EXPECT_FALSE(d.GetMediaRateUtilizationFactor(0).has_value());
}

SM_TEST(EncoderOvershootDetector, FramesOnTargetGiveFactorOne) {
  EncoderOvershootDetector d(3000);
  d.SetTargetRate(Kbps(1000), 30, 0);
  // Ideal frame: (1e6 + 15) / 30 = 33333 bits = 4166.6 bytes; 4167 bytes at 33 ms spacing.
  for (int i = 0; i < 60; ++i) d.OnEncodedFrame(4167, i * 1000 / 30);
  SM_EXPECT_NEAR(*d.GetNetworkRateUtilizationFactor(1900), 1.0, 0.02);
  SM_EXPECT_NEAR(*d.GetMediaRateUtilizationFactor(1900), 1.0, 0.02);
}

SM_TEST(EncoderOvershootDetector, FirstFrameIsSizeOverIdealSize) {
  EncoderOvershootDetector d(3000);
  d.SetTargetRate(Kbps(1000), 30, 0);
  d.OnEncodedFrame(4167 * 3, 0);  // 3x the ideal size.
  SM_EXPECT_NEAR(*d.GetNetworkRateUtilizationFactor(0), 3.0, 0.01);
}

SM_TEST(EncoderOvershootDetector, SustainedOvershootIsPenalizedAndOldDataCulled) {
  EncoderOvershootDetector d(3000);
  d.SetTargetRate(Kbps(1000), 30, 0);
  for (int i = 0; i < 90; ++i) d.OnEncodedFrame(4167 * 3 / 2, i * 1000 / 30);  // 1.5x for 3 s.
  const double over = *d.GetNetworkRateUtilizationFactor(3000);
  SM_EXPECT_GT(over, 1.3);
  SM_EXPECT_LT(over, 1.6);
  // Everything is older than the 3 s window afterwards.
  SM_EXPECT_FALSE(d.GetNetworkRateUtilizationFactor(9000).has_value());
}

SM_TEST(EncoderOvershootDetector, DroppedFramesLeaveNoUpdate) {
  EncoderOvershootDetector d(3000);
  d.SetTargetRate(Kbps(1000), 30, 0);
  d.OnEncodedFrame(0, 33);
  SM_EXPECT_FALSE(d.GetNetworkRateUtilizationFactor(33).has_value());
}

// EncoderBitrateAdjuster (video/encoder_bitrate_adjuster.cc).

SM_TEST(EncoderBitrateAdjuster, StartsWithTwentyPercentOvershootAssumed) {
  EncoderBitrateAdjuster a;
  // kDefaultUtilizationFactor 1.2 until 30 frames: 1000 / 1.2 = 833 kbps.
  SM_EXPECT_NEAR(a.AdjustRate(Kbps(1000), Kbps(1000), 30, Timestamp::Millis(0)).kbps(), 833.3, 0.1);
  // Headroom does not help here: the media factor is the default 1.2 too.
  SM_EXPECT_NEAR(a.AdjustRate(Kbps(1000), Kbps(1500), 30, Timestamp::Millis(33)).kbps(), 833.3, 0.1);
}

SM_TEST(EncoderBitrateAdjuster, EncoderOnTargetIsLeftAlone) {
  EncoderBitrateAdjuster a;
  const DataRate r = Run(a, 120, 4167, Kbps(1000), Kbps(1000), 0);
  SM_EXPECT_GT(r.kbps(), 975.0);
  SM_EXPECT_LE(r.kbps(), 1000.0);
}

SM_TEST(EncoderBitrateAdjuster, OvershootLowersTheRateAndNeverBelowHalf) {
  EncoderBitrateAdjuster a;
  // The encoder produces 1.5x whatever the ideal frame is, with no link headroom.
  DataRate r;
  for (int i = 0; i < 150; ++i) {
    const Timestamp now = Timestamp::Micros(i * 1000000 / 30);
    r = a.AdjustRate(Kbps(1000), Kbps(1000), 30, now);
    a.OnEncodedFrame(DataSize::Bytes(4167 * 3 / 2 * r.bps() / 1000000), now);
  }
  SM_EXPECT_LT(r.kbps(), 800.0);
  SM_EXPECT_GE(r.kbps(), 500.0);  // Clamped at factor 2.0.
  // A 4x encoder is still clamped at half the target.
  EncoderBitrateAdjuster b;
  const DataRate r4 = Run(b, 150, 4167 * 4, Kbps(1000), Kbps(1000), 0);
  SM_EXPECT_NEAR(r4.kbps(), 500.0, 1.0);
}

SM_TEST(EncoderBitrateAdjuster, LinkHeadroomPaysForOvershoot) {
  EncoderBitrateAdjuster no_headroom;
  EncoderBitrateAdjuster with_headroom;
  // Alternating a large and a small frame: the media rate is on target (media
  // factor 1.0) while the link sees bursts (link factor above 1.0).
  DataRate a, b;
  for (int i = 0; i < 180; ++i) {
    const Timestamp now = Timestamp::Micros(i * 1000000 / 30);
    a = no_headroom.AdjustRate(Kbps(1000), Kbps(1000), 30, now);
    b = with_headroom.AdjustRate(Kbps(1000), Kbps(1400), 30, now);
    const int64_t bytes = (i % 10 == 0) ? 4167 * 4 : 4167 * 7 / 10;
    no_headroom.OnEncodedFrame(DataSize::Bytes(bytes), now);
    with_headroom.OnEncodedFrame(DataSize::Bytes(bytes), now);
  }
  SM_EXPECT_GT(no_headroom.link_utilization_factor(), 1.05);
  SM_EXPECT_LT(a.kbps(), 950.0);
  SM_EXPECT_GT(b.kbps(), a.kbps());  // Spare headroom raises the encoder rate.
  SM_EXPECT_LE(b.kbps(), 1000.0);
}

SM_TEST(EncoderBitrateAdjuster, DisablingHeadroomIgnoresTheLinkAllocation) {
  EncoderBitrateAdjusterConfig cfg;
  cfg.utilize_bandwidth_headroom = false;
  EncoderBitrateAdjuster a(cfg);
  EncoderBitrateAdjuster b;
  DataRate ra, rb;
  for (int i = 0; i < 180; ++i) {
    const Timestamp now = Timestamp::Micros(i * 1000000 / 30);
    ra = a.AdjustRate(Kbps(1000), Kbps(1400), 30, now);
    rb = b.AdjustRate(Kbps(1000), Kbps(1000), 30, now);
    const int64_t bytes = (i % 10 == 0) ? 4167 * 4 : 4167 * 7 / 10;
    a.OnEncodedFrame(DataSize::Bytes(bytes), now);
    b.OnEncodedFrame(DataSize::Bytes(bytes), now);
  }
  SM_EXPECT_EQ(ra.bps(), rb.bps());
}

SM_TEST(EncoderBitrateAdjuster, MinimumBitrateIsKept) {
  EncoderBitrateAdjusterConfig cfg;
  cfg.min_bitrate = Kbps(900);
  EncoderBitrateAdjuster a(cfg);
  const DataRate r = Run(a, 150, 4167 * 4, Kbps(1000), Kbps(1000), 0);
  SM_EXPECT_GE(r.kbps(), 899.0);
}

SM_TEST(EncoderBitrateAdjuster, ZeroTargetPassesThroughAndRestartsTheWarmUp) {
  EncoderBitrateAdjuster a;
  Run(a, 60, 4167, Kbps(1000), Kbps(1000), 0);
  SM_EXPECT_EQ(a.AdjustRate(DataRate::BitsPerSec(0), DataRate::BitsPerSec(0), 30, Timestamp::Millis(3000)).bps(), 0);
  // Back on: a fresh detector, so the 1.2 default again for 30 frames.
  SM_EXPECT_NEAR(a.AdjustRate(Kbps(1000), Kbps(1000), 30, Timestamp::Millis(4000)).kbps(), 833.3, 0.1);
}

SM_TEST(EncoderBitrateAdjuster, DroppedFramesCountNoBytesAndResetRestarts) {
  EncoderBitrateAdjuster a;
  Run(a, 60, 4167, Kbps(1000), Kbps(1000), 0);
  a.OnFrameDropped(Timestamp::Millis(2100));
  a.Reset(Timestamp::Millis(2200));
  SM_EXPECT_NEAR(a.adjusted().kbps(), 833.3, 0.1);  // Warm-up again after Reset.
}
