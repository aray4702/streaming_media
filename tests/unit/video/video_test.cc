// Section 7 unit tests.
#include <cmath>
#include <functional>
#include <vector>

#include "sm/core/random.h"
#include "sm/video/codec_fallback.h"
#include "sm/video/codec_negotiation.h"
#include "sm/video/content_policy.h"
#include "sm/video/frame_scaler.h"
#include "sm/video/frame_scheduler.h"
#include "sm/video/freeze_detector.h"
#include "sm/video/orientation.h"
#include "sm/video/quality_scaler.h"
#include "sm/video/video_stream_adapter.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::video;

namespace {
Timestamp Ms(int64_t ms) { return Timestamp::Millis(ms); }
}

SM_TEST(FreezeDetector, DetectsFreezesAndPauses) {
  FreezeDetector fd;
  int64_t t = 0;
  for (int i = 0; i < 60; ++i, t += 33) fd.OnFrameRendered(Ms(t));
  t += 300;  // 333 ms gap: > max(99, 183) -> freeze.
  fd.OnFrameRendered(Ms(t));
  for (int i = 0; i < 30; ++i) fd.OnFrameRendered(Ms(t += 33));
  t += 120;  // 153 ms: below threshold.
  fd.OnFrameRendered(Ms(t));
  t += 6000;  // Pause.
  fd.OnFrameRendered(Ms(t));
  SM_EXPECT_EQ(fd.stats().freeze_count, 1u);
  SM_EXPECT_NEAR(fd.stats().total_freezes_ms, 333, 1);
  SM_EXPECT_EQ(fd.stats().pause_count, 1u);
  SM_EXPECT_LT(fd.HarmonicFps(), 30.0);
}

// Issue #283: the longest freeze must not come from the `freeze_ms` histogram,
// which records nothing when metrics are compiled out.
SM_TEST(FreezeDetector, TracksLongestFreeze) {
  FreezeDetector fd;
  int64_t t = 0;
  SM_EXPECT_EQ(fd.stats().longest_freeze_ms, 0.0);
  for (int i = 0; i < 30; ++i) fd.OnFrameRendered(Ms(t += 33));
  fd.OnFrameRendered(Ms(t += 1500));  // Freeze.
  for (int i = 0; i < 30; ++i) fd.OnFrameRendered(Ms(t += 33));
  fd.OnFrameRendered(Ms(t += 400));  // Shorter freeze.
  for (int i = 0; i < 30; ++i) fd.OnFrameRendered(Ms(t += 33));
  fd.OnFrameRendered(Ms(t += 7000));  // Pause: not a freeze.
  SM_EXPECT_EQ(fd.stats().freeze_count, 2u);
  SM_EXPECT_NEAR(fd.stats().longest_freeze_ms, 1500, 1e-6);
}

SM_TEST(FreezeDetector, SmoothPlaybackHasNoFreezes) {
  FreezeDetector fd;
  for (int i = 0; i < 300; ++i) fd.OnFrameRendered(Ms(i * 33 + (i % 3) * 5));
  SM_EXPECT_EQ(fd.stats().freeze_count, 0u);
  // Intervals of 38, 38, 23 ms: sum(d) / sum(d^2) = 99 / 3.417 = 28.97 fps,
  // below the arithmetic 30.3 fps because irregular timing looks worse.
  SM_EXPECT_NEAR(fd.HarmonicFps(), 28.97, 0.05);
}

// Issue #268: freezes as libwebrtc's VideoQualityObserver counts them. The
// render times are those of sm_video_compare's freeze block
// (compare/webrtc/video_compare.cc), and the expected values are libwebrtc's
// on them (docs/results/video-webrtc.md).
namespace {
int64_t RoundMs(double x) { return static_cast<int64_t>(std::llround(x)); }
void AddFrames(std::vector<int64_t>& v, double from, double to, double period) {
  for (double t = from; t < to; t += period) {
    int64_t ms = RoundMs(t);
    if (!v.empty()) ms = std::max(ms, v.back() + 1);
    v.push_back(ms);
  }
}
FreezeStats RunFreezes(const std::vector<int64_t>& render_ms) {
  FreezeDetector fd;
  for (int64_t t : render_ms) fd.OnFrameRendered(Ms(t));
  return fd.stats();
}
constexpr double k30Fps = 1000.0 / 30;
}  // namespace

SM_TEST(FreezeDetector, CurrentIntervalEntersAverageBeforeTest) {
  // libwebrtc: a gap enters the 30-frame average before it is tested, so at
  // 30 fps a 185 ms gap is not a freeze (threshold about 188 ms) and 190 ms is.
  std::vector<int64_t> v;
  double t = 0;
  for (int gap = 180; gap <= 230; gap += 5) {
    AddFrames(v, t, t + 5000, k30Fps);
    t = static_cast<double>(v.back()) + gap;
  }
  AddFrames(v, t, t + 2000, k30Fps);
  FreezeStats s = RunFreezes(v);
  SM_EXPECT_EQ(s.freeze_count, 9u);  // libwebrtc 9 (was 10).
  SM_EXPECT_NEAR(s.total_freezes_ms, 1890, 1e-6);  // libwebrtc 1890 (was 2075).
}

SM_TEST(FreezeDetector, FreezesStayInAverage) {
  // 3 s at 3 fps: libwebrtc keeps the long intervals in the average, which
  // rises within a few frames and stops counting them as freezes.
  std::vector<int64_t> v;
  AddFrames(v, 0, 3000, k30Fps);
  AddFrames(v, 3000, 6000, 333);
  AddFrames(v, 6000, 9000, k30Fps);
  FreezeStats s = RunFreezes(v);
  SM_EXPECT_EQ(s.freeze_count, 7u);  // libwebrtc 7 (was 9).
  SM_EXPECT_NEAR(s.total_freezes_ms, 2331, 1e-6);  // libwebrtc 2331 (was 2997).
}

SM_TEST(FreezeDetector, RandomGapsAsLibwebrtc) {
  Random rng(7);
  std::vector<int64_t> v;
  double t = 0;
  while (t < 120000) {
    v.push_back(RoundMs(t));
    t += rng.Bernoulli(0.02) ? rng.Uniform(100, 800) : k30Fps;
  }
  FreezeStats s = RunFreezes(v);
  SM_EXPECT_EQ(s.freeze_count, 57u);  // libwebrtc 57 (was 59).
  SM_EXPECT_NEAR(s.total_freezes_ms, 26580, 1e-6);  // libwebrtc 26580 (was 26974).
}

SM_TEST(FreezeDetector, TestStartsAtFiveSamplesIncludingCurrent) {
  // Four 33 ms intervals, then a 300 ms gap: the window holds 5 samples
  // counting the gap (average 86 ms, threshold 258 ms), so it is a freeze.
  // Three intervals and the gap make only 4 samples: not tested.
  FreezeDetector a;
  int64_t t = 0;
  a.OnFrameRendered(Ms(t));
  for (int i = 0; i < 4; ++i) a.OnFrameRendered(Ms(t += 33));
  a.OnFrameRendered(Ms(t += 300));
  SM_EXPECT_EQ(a.stats().freeze_count, 1u);
  FreezeDetector b;
  t = 0;
  b.OnFrameRendered(Ms(t));
  for (int i = 0; i < 3; ++i) b.OnFrameRendered(Ms(t += 33));
  b.OnFrameRendered(Ms(t += 300));
  SM_EXPECT_EQ(b.stats().freeze_count, 0u);
}

SM_TEST(FreezeDetector, AverageRoundedDownToWholeMs) {
  // 29 intervals of 50 ms and a gap: with 205 ms the average is 1655 / 30 =
  // 55.17 ms, rounded down to 55, so the threshold is 205 ms and the gap is a
  // freeze (unrounded it would need 205.17 ms). 204 ms is not.
  for (int gap : {204, 205}) {
    FreezeDetector fd;
    int64_t t = 0;
    fd.OnFrameRendered(Ms(t));
    for (int i = 0; i < 29; ++i) fd.OnFrameRendered(Ms(t += 50));
    fd.OnFrameRendered(Ms(t += gap));
    SM_EXPECT_EQ(fd.stats().freeze_count, gap == 204 ? 0u : 1u);
  }
}

SM_TEST(CodecFallback, InitFailureAndConsecutiveErrors) {
  CodecFallbackController a;
  a.OnInitResult(false, Ms(0));
  SM_EXPECT_TRUE(a.active() == CodecImpl::kSoftware);
  SM_EXPECT_TRUE(a.TakeKeyframeRequest());
  SM_EXPECT_FALSE(a.TakeKeyframeRequest());
  CodecFallbackController b;
  b.OnFrameResult(false, Ms(0));
  b.OnFrameResult(false, Ms(33));
  SM_EXPECT_TRUE(b.active() == CodecImpl::kHardware);
  b.OnFrameResult(false, Ms(66));
  SM_EXPECT_TRUE(b.active() == CodecImpl::kSoftware);
}

SM_TEST(CodecFallback, ErrorRateAndRetryBackoff) {
  CodecFallbackConfig cfg;
  cfg.retry_hardware_after = TimeDelta::Seconds(10);
  CodecFallbackController c(cfg);
  for (int i = 0; i < 100; ++i) c.OnFrameResult(i % 6 != 0, Ms(i * 33));  // ~17% errors.
  SM_EXPECT_TRUE(c.active() == CodecImpl::kSoftware);
  int64_t fell = 3300;
  c.Process(Ms(fell + 5000));
  SM_EXPECT_TRUE(c.active() == CodecImpl::kSoftware);
  c.Process(Ms(fell + 10500));
  SM_EXPECT_TRUE(c.active() == CodecImpl::kHardware);
  // Retry fails again: back-off doubles to 20 s.
  for (int i = 0; i < 3; ++i) c.OnFrameResult(false, Ms(fell + 11000 + i));
  SM_EXPECT_TRUE(c.active() == CodecImpl::kSoftware);
  c.Process(Ms(fell + 11000 + 15000));
  SM_EXPECT_TRUE(c.active() == CodecImpl::kSoftware);
  c.Process(Ms(fell + 11000 + 21000));
  SM_EXPECT_TRUE(c.active() == CodecImpl::kHardware);
  SM_EXPECT_EQ(c.stats().fallbacks, 2u);
}

SM_TEST(Orientation, CvoByteAndRotation) {
  VideoOrientation o{Rotation::k90, true, true};
  uint8_t b = EncodeCvo(o);
  SM_EXPECT_EQ(b, 0x0D);
  auto d = DecodeCvo(b);
  SM_EXPECT_TRUE(d.rotation == Rotation::k90);
  SM_EXPECT_TRUE(d.flip && d.back_camera);
  I420Frame f(4, 2);
  for (int i = 0; i < 8; ++i) f.y[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
  auto r = ApplyOrientation(f, {Rotation::k90, false, false});
  SM_EXPECT_EQ(r.width, 2);
  SM_EXPECT_EQ(r.height, 4);
  // Clockwise: the bottom-left source pixel (4) becomes the top-left.
  SM_EXPECT_EQ(r.Y(0, 0), 4);
  SM_EXPECT_EQ(r.Y(1, 0), 0);
}

SM_TEST(Orientation, InverseRestoresAllEightCases) {
  I420Frame f = MakeTestFrame(64, 48, 3);
  for (int rot = 0; rot < 4; ++rot) {
    for (bool flip : {false, true}) {
      VideoOrientation o{static_cast<Rotation>(rot), flip, false};
      I420Frame back = ApplyOrientation(ApplyOrientation(f, o), Inverse(o));
      SM_EXPECT_TRUE(back == f);
    }
  }
}

SM_TEST(ContentPolicy, HintsMapToPolicies) {
  auto text = SettingsFor(ContentHint::kText, true);
  SM_EXPECT_TRUE(text.degradation == Degradation::kMaintainResolution);
  SM_EXPECT_TRUE(text.variable_framerate);
  SM_EXPECT_LE(text.max_qp_h264, 32);
  auto motion = SettingsFor(ContentHint::kMotion, false);
  SM_EXPECT_TRUE(motion.degradation == Degradation::kMaintainFramerate);
  SM_EXPECT_FALSE(motion.variable_framerate);
}

SM_TEST(ContentPolicy, ChangeDetectorSkipsStaticFrames) {
  FrameChangeDetector cd(TimeDelta::Seconds(1));
  I420Frame slide = MakeTestFrame(320, 240, 0);
  SM_EXPECT_TRUE(cd.ShouldEncode(slide, Ms(0)));
  SM_EXPECT_FALSE(cd.ShouldEncode(slide, Ms(100)));
  slide.Y(100, 100) ^= 0xff;  // One pixel changes (cursor blink).
  auto r = cd.Analyze(slide);
  SM_EXPECT_TRUE(r.changed);
  SM_EXPECT_EQ(r.dirty_blocks, 1);
  SM_EXPECT_FALSE(cd.ShouldEncode(slide, Ms(200)));  // Already absorbed by Analyze.
  SM_EXPECT_TRUE(cd.ShouldEncode(slide, Ms(1300)));  // Keep-alive.
}

SM_TEST(CodecNegotiation, H264ProfileLevelParsing) {
  auto cb = ParseH264ProfileLevelId("42e01f");
  SM_ASSERT_TRUE(cb.has_value());
  SM_EXPECT_TRUE(cb->profile == H264Profile::kConstrainedBaseline);
  SM_EXPECT_EQ(cb->level_idc, 31);
  auto high = ParseH264ProfileLevelId("640c34");
  SM_ASSERT_TRUE(high.has_value());
  SM_EXPECT_TRUE(high->profile == H264Profile::kConstrainedHigh);
  SM_EXPECT_EQ(ParseH264ProfileLevelId("4d001f")->profile, H264Profile::kMain);
  SM_EXPECT_FALSE(ParseH264ProfileLevelId("zz0000").has_value());
  SM_EXPECT_EQ(H264ProfileLevelIdString(*cb), "42e01f");
}

SM_TEST(CodecNegotiation, AnswerKeepsSupportedInLocalOrder) {
  std::vector<CodecCapability> offer = {
      {"VP8", 96, {}, {"nack", "nack pli", "transport-cc"}},
      {"rtx", 97, {{"apt", "96"}}, {}},
      {"VP9", 98, {{"profile-id", "2"}}, {"nack"}},
      {"H264", 102, {{"profile-level-id", "42e034"}, {"packetization-mode", "1"}}, {"nack", "ccm fir", "goog-remb"}},
      {"rtx", 103, {{"apt", "102"}}, {}},
      {"H264", 104, {{"profile-level-id", "640034"}, {"packetization-mode", "1"}}, {"nack"}},
  };
  std::vector<CodecCapability> local = {
      {"VP8", 0, {}, {"nack", "nack pli", "transport-cc"}, false},
      {"VP9", 0, {{"profile-id", "0"}}, {"nack"}, false},
      {"H264", 0, {{"profile-level-id", "42e01f"}, {"packetization-mode", "1"}}, {"nack", "ccm fir"}, true},
  };
  auto ans = NegotiateCodecs(offer, local);
  SM_ASSERT_EQ(ans.size(), 4u);  // H264 CB, VP8, rtx(96), rtx(102). VP9 p2 != p0.
  SM_EXPECT_EQ(ans[0].name, "H264");  // Hardware first.
  SM_EXPECT_EQ(ans[0].payload_type, 102);
  SM_EXPECT_EQ(ans[0].params["profile-level-id"], "42e01f");  // min(5.2, 3.1)
  SM_EXPECT_EQ(ans[0].feedback.size(), 2u);
  SM_EXPECT_EQ(ans[1].name, "VP8");
  SM_EXPECT_EQ(ans[1].payload_type, 96);
  SM_EXPECT_EQ(ans[2].payload_type, 97);
  SM_EXPECT_EQ(ans[3].payload_type, 103);
}

SM_TEST(FrameScaler, DownUpRoundTripQuality) {
  I420Frame f = MakeTestFrame(640, 360, 1);
  for (auto [w, h] : {std::pair{480, 270}, {320, 180}, {160, 90}}) {
    I420Frame back = Scale(Scale(f, w, h), 640, 360);
    double psnr = PsnrY(f, back);
    SM_EXPECT_GT(psnr, w >= 320 ? 26.0 : 20.0);
  }
  I420Frame same = Scale(f, 640, 360);
  SM_EXPECT_TRUE(same == f);
}

SM_TEST(FrameScaler, BoxFilterPreservesMeanAndAspectModes) {
  I420Frame f(64, 64);
  for (size_t i = 0; i < f.y.size(); ++i) f.y[i] = (i % 2) ? 200 : 100;  // Fine detail.
  I420Frame s = Scale(f, 16, 16);
  for (uint8_t v : s.y) SM_EXPECT_NEAR(v, 150, 1);  // Averaged, no aliasing.
  I420Frame wide = MakeTestFrame(640, 360, 0);
  I420Frame crop = ScaleToFit(wide, 320, 320, AspectMode::kCrop);
  SM_EXPECT_EQ(crop.width, 320);
  I420Frame box = ScaleToFit(wide, 320, 320, AspectMode::kLetterbox);
  SM_EXPECT_EQ(box.Y(160, 0), 16);    // Black bar on top.
  SM_EXPECT_NE(box.Y(160, 160), 16);  // Picture in the middle.
}

SM_TEST(FrameScheduler, RendersOnTimeDropsLateWhenNewerWaiting) {
  FrameScheduler s;
  s.Insert({1, Ms(100)});
  s.Insert({2, Ms(133)});
  s.Insert({3, Ms(166)});
  SM_EXPECT_TRUE(s.Process(Ms(50)).empty());
  auto a = s.Process(Ms(96));
  SM_ASSERT_EQ(a.size(), 1u);
  SM_EXPECT_TRUE(a[0].render);
  // Stall until 200: frame 2 is 72 ms late with frame 3 waiting -> skipped.
  auto b = s.Process(Ms(200));
  SM_ASSERT_EQ(b.size(), 2u);
  SM_EXPECT_FALSE(b[0].render);
  SM_EXPECT_TRUE(b[1].render);  // Newest is shown even though late.
  SM_EXPECT_EQ(s.stats().dropped_late, 1u);
  SM_EXPECT_EQ(s.stats().decoded, 3u);
}

SM_TEST(FrameScheduler, FastForwardOnHugeBacklog) {
  FrameSchedulerConfig cfg;
  cfg.max_queue = 10;
  FrameScheduler s(cfg);
  for (int i = 0; i < 20; ++i) s.Insert({i, Ms(1000 + i * 33)});
  auto a = s.Process(Ms(0));
  SM_EXPECT_EQ(a.size(), 19u);
  SM_EXPECT_EQ(s.queued(), 1u);
  SM_EXPECT_EQ(s.stats().fast_forwards, 1u);
}

SM_TEST(QualityScaler, ThresholdsAndSignals) {
  SM_EXPECT_EQ(QpThresholds::ForCodec("vp8").high, 95);
  SM_EXPECT_EQ(QpThresholds::ForCodec("H264").low, 24);
  QualityScaler q;  // H.264 thresholds.
  for (int i = 0; i < 30; ++i) q.OnEncodedFrame(45);
  SM_EXPECT_TRUE(q.Check(Ms(0)) == QualityScaler::Signal::kAdaptDown);
  q.OnResolutionChanged();
  for (int i = 0; i < 30; ++i) q.OnEncodedFrame(30);
  SM_EXPECT_TRUE(q.Check(Ms(2000)) == QualityScaler::Signal::kNone);
  for (int i = 0; i < 30; ++i) q.OnEncodedFrame(18);
  SM_EXPECT_TRUE(q.Check(Ms(2500)) == QualityScaler::Signal::kNone);  // Rate limited.
  SM_EXPECT_TRUE(q.Check(Ms(4000)) == QualityScaler::Signal::kAdaptUp);
  QualityScaler d;
  for (int i = 0; i < 20; ++i) d.OnFrameDropped();
  for (int i = 0; i < 5; ++i) d.OnEncodedFrame(20);
  SM_EXPECT_TRUE(d.Check(Ms(0)) == QualityScaler::Signal::kAdaptDown);  // 80% dropped.
}

// Version 2: libwebrtc M157's QualityScaler. These are the cases of
// modules/video_coding/utility/quality_scaler_unittest.cc (thresholds 15/40,
// 150 frames per TriggerScale, every sample at time 0), run on M157's real
// cadence instead of the test's 5 ms period: the first check comes 2 s after
// the first Check() call.
namespace {

QualityScaler V2Scaler() {
  QualityScalerConfig cfg;
  cfg.thresholds = {15, 40};
  cfg.version = 2;
  return QualityScaler(cfg);
}

// Starts the check timer at 0 and runs `feed`, then the first check at 2 s.
QualityScaler::Signal FirstCheck(QualityScaler& q, const std::function<void()>& feed) {
  SM_EXPECT_TRUE(q.Check(Ms(0)) == QualityScaler::Signal::kNone);
  feed();
  SM_EXPECT_TRUE(q.Check(Ms(1999)) == QualityScaler::Signal::kNone);
  return q.Check(Ms(2000));
}

}  // namespace

SM_TEST(QualityScaler, V2DownscalesAfterContinuousFramedrop) {
  auto q = V2Scaler();
  SM_EXPECT_TRUE(FirstCheck(q, [&] { for (int i = 0; i < 150; ++i) q.OnFrameDropped(); }) ==
                 QualityScaler::Signal::kAdaptDown);
}

SM_TEST(QualityScaler, V2KeepsScaleAtHighQp) {
  auto q = V2Scaler();
  SM_EXPECT_TRUE(FirstCheck(q, [&] { for (int i = 0; i < 150; ++i) q.OnEncodedFrame(40, Ms(0)); }) ==
                 QualityScaler::Signal::kNone);
}

SM_TEST(QualityScaler, V2DownscalesAboveHighQp) {
  auto q = V2Scaler();
  SM_EXPECT_TRUE(FirstCheck(q, [&] { for (int i = 0; i < 150; ++i) q.OnEncodedFrame(41, Ms(0)); }) ==
                 QualityScaler::Signal::kAdaptDown);
}

SM_TEST(QualityScaler, V2DownscalesAfterTwoThirdsFramedrop) {
  // Also DownscalesAfterTwoThirdsIfFieldTrialEnabled: QualityScalingExperiment
  // is on by default and counts drops by the encoder too.
  auto q = V2Scaler();
  SM_EXPECT_TRUE(FirstCheck(q, [&] {
                   for (int i = 0; i < 150; ++i) {
                     q.OnFrameDropped();
                     q.OnFrameDropped();
                     q.OnEncodedFrame(40, Ms(0));
                   }
                 }) == QualityScaler::Signal::kAdaptDown);
}

SM_TEST(QualityScaler, V2DoesNotDownscaleAfterHalfFramedrop) {
  auto q = V2Scaler();
  SM_EXPECT_TRUE(FirstCheck(q, [&] {
                   for (int i = 0; i < 150; ++i) {
                     q.OnFrameDropped();
                     q.OnEncodedFrame(40, Ms(0));
                   }
                 }) == QualityScaler::Signal::kNone);
}

SM_TEST(QualityScaler, V2KeepsScaleOnNormalQp) {
  auto q = V2Scaler();
  SM_EXPECT_TRUE(FirstCheck(q, [&] { for (int i = 0; i < 150; ++i) q.OnEncodedFrame(16, Ms(0)); }) ==
                 QualityScaler::Signal::kNone);
}

SM_TEST(QualityScaler, V2ScalesDownAndBackUp) {
  auto q = V2Scaler();
  SM_EXPECT_TRUE(FirstCheck(q, [&] { for (int i = 0; i < 150; ++i) q.OnFrameDropped(); }) ==
                 QualityScaler::Signal::kAdaptDown);
  for (int i = 0; i < 150; ++i) q.OnEncodedFrame(15, Ms(2000));
  // After the first adapt-down the period is 2 s x 2.5.
  SM_EXPECT_TRUE(q.Check(Ms(6999)) == QualityScaler::Signal::kNone);
  SM_EXPECT_TRUE(q.Check(Ms(7000)) == QualityScaler::Signal::kAdaptUp);
}

SM_TEST(QualityScaler, V2DoesNotScaleUntilEnoughFramesObserved) {
  auto q = V2Scaler();
  SM_EXPECT_TRUE(FirstCheck(q, [&] { for (int i = 0; i < 59; ++i) q.OnEncodedFrame(15, Ms(0)); }) ==
                 QualityScaler::Signal::kNone);
  q.OnEncodedFrame(15, Ms(0));  // The 60th frame.
  // Still in fast rampup: the next check is 2 s later.
  SM_EXPECT_TRUE(q.Check(Ms(3999)) == QualityScaler::Signal::kNone);
  SM_EXPECT_TRUE(q.Check(Ms(4000)) == QualityScaler::Signal::kAdaptUp);
  // Samples are cleared after a report.
  q.OnEncodedFrame(15, Ms(4000));
  SM_EXPECT_TRUE(q.Check(Ms(6000)) == QualityScaler::Signal::kNone);
  SM_EXPECT_EQ(q.average_qp(), 15.0);
}

SM_TEST(QualityScaler, V2ScalesDownAndBackUpWithMinFramesNeeded) {
  auto q = V2Scaler();
  SM_EXPECT_TRUE(FirstCheck(q, [&] { for (int i = 0; i < 60; ++i) q.OnEncodedFrame(41, Ms(0)); }) ==
                 QualityScaler::Signal::kAdaptDown);
  for (int i = 0; i < 60; ++i) q.OnEncodedFrame(15, Ms(2000));
  SM_EXPECT_TRUE(q.Check(Ms(7000)) == QualityScaler::Signal::kAdaptUp);
}

SM_TEST(QualityScaler, V2HalfPeriodWhileTooFewFramesAfterRampup) {
  // CheckQpTask::GetCheckingQpDelayMs: with QualityScalingExperiment on, a
  // check that saw too few frames is followed by one after 1 s, not 5 s.
  auto q = V2Scaler();
  SM_EXPECT_TRUE(FirstCheck(q, [&] { for (int i = 0; i < 60; ++i) q.OnEncodedFrame(41, Ms(0)); }) ==
                 QualityScaler::Signal::kAdaptDown);
  SM_EXPECT_TRUE(q.Check(Ms(7000)) == QualityScaler::Signal::kNone);  // No frames since.
  for (int i = 0; i < 60; ++i) q.OnEncodedFrame(41, Ms(7000));
  SM_EXPECT_TRUE(q.Check(Ms(7999)) == QualityScaler::Signal::kNone);
  SM_EXPECT_TRUE(q.Check(Ms(8000)) == QualityScaler::Signal::kAdaptDown);
}

SM_TEST(QualityScaler, V2SmoothsQpOverTime) {
  // QpSmoother: alpha 0.9995 per ms for the high check. 40 for 10 s at 30 fps,
  // then 18: the high filter is still above 37 a few frames into the 18s.
  QualityScalerConfig cfg;
  cfg.version = 2;  // H.264 24/37.
  QualityScaler q(cfg);
  q.Check(Ms(0));
  int64_t t = 0;
  for (; t < 10000; t += 33) q.OnEncodedFrame(40, Ms(t));
  SM_EXPECT_NEAR(q.average_qp(), 40.0, 1e-3);
  q.OnEncodedFrame(18, Ms(t));
  // 0.9995^33 = 0.98363: 40 - 22 x (1 - 0.98363) = 39.64.
  SM_EXPECT_NEAR(q.average_qp(), 39.64, 0.01);
}

SM_TEST(QualityScaler, V2RecordsTheChecksLibwebrtcLogs) {
  // Issue #347: the checks QualityScaler::CheckQp logs, with the values it
  // logs ("Checking average QP H (L)." or "Reporting high QP, framedrop
  // percent N"); a check with too few frames logs nothing.
  auto q = V2Scaler();
  SM_EXPECT_EQ(q.checks(), static_cast<uint64_t>(0));
  SM_EXPECT_TRUE(FirstCheck(q, [&] { for (int i = 0; i < 59; ++i) q.OnEncodedFrame(20, Ms(0)); }) ==
                 QualityScaler::Signal::kNone);
  SM_EXPECT_EQ(q.checks(), static_cast<uint64_t>(0));
  SM_EXPECT_EQ(q.last_check().qp_high, -1);
  q.OnEncodedFrame(20, Ms(2000));
  SM_EXPECT_TRUE(q.Check(Ms(4000)) == QualityScaler::Signal::kNone);  // 20 is between 15 and 40.
  SM_EXPECT_EQ(q.checks(), static_cast<uint64_t>(1));
  SM_EXPECT_EQ(q.last_check().qp_high, 20);
  SM_EXPECT_EQ(q.last_check().qp_low, 20);
  SM_EXPECT_EQ(q.last_check().drop_pct, -1);
  SM_EXPECT_TRUE(q.last_check().signal == QualityScaler::Signal::kNone);
  for (int i = 0; i < 150; ++i) q.OnFrameDropped();
  SM_EXPECT_TRUE(q.Check(Ms(6000)) == QualityScaler::Signal::kAdaptDown);
  SM_EXPECT_EQ(q.checks(), static_cast<uint64_t>(2));
  SM_EXPECT_EQ(q.last_check().qp_high, -1);
  SM_EXPECT_EQ(q.last_check().drop_pct, 100);
  SM_EXPECT_TRUE(q.last_check().signal == QualityScaler::Signal::kAdaptDown);
  for (int i = 0; i < 60; ++i) q.OnEncodedFrame(15, Ms(6000));
  SM_EXPECT_TRUE(q.Check(Ms(11000)) == QualityScaler::Signal::kAdaptUp);
  SM_EXPECT_EQ(q.last_check().qp_low, 15);
  SM_EXPECT_TRUE(q.last_check().signal == QualityScaler::Signal::kAdaptUp);
}

// Ports of call/adaptation/video_stream_adapter_unittest.cc (MAINTAIN_FRAMERATE)
// and the VideoAdapter scale series (api/video/video_adapter.cc), issue #84.
namespace {
// FakeVideoStream::ApplyAdaptation: the input follows the target, else the maximum.
void FollowRestrictions(VideoStreamAdapter& a, int& input_pixels) {
  const auto& r = a.restrictions();
  if (r.target_pixels_per_frame) {
    input_pixels = *r.target_pixels_per_frame;
  } else if (r.max_pixels_per_frame) {
    input_pixels = *r.max_pixels_per_frame;
  }
  a.OnFrameSizeObserved(input_pixels);
}
}  // namespace

SM_TEST(VideoStreamAdapter, DecreasesPixelsToThreeFifthsAndStopsAtTheMinimum) {
  VideoStreamAdapter a;
  SM_EXPECT_TRUE(a.restrictions() == VideoSourceRestrictions{});
  SM_EXPECT_TRUE(a.AdaptDown() == VideoStreamAdapter::Status::kInsufficientInput);
  a.OnFrameSizeObserved(1280 * 720);
  SM_EXPECT_TRUE(a.AdaptDown() == VideoStreamAdapter::Status::kValid);
  SM_EXPECT_EQ(a.restrictions().max_pixels_per_frame.value_or(0), 1280 * 720 * 3 / 5);
  SM_EXPECT_FALSE(a.restrictions().target_pixels_per_frame.has_value());
  SM_EXPECT_EQ(a.resolution_adaptations(), 1);
  // MaintainFramerate_DecreasesPixelsToLimitReached: 3/5 would go below the minimum.
  VideoStreamAdapter limited(640 * 480);
  limited.OnFrameSizeObserved(640 * 480 + 1);
  SM_EXPECT_TRUE(limited.AdaptDown() == VideoStreamAdapter::Status::kLimitReached);
}

SM_TEST(VideoStreamAdapter, IncreasesPixelsToFiveThirdsThenUnrestricted) {
  VideoStreamAdapter a;
  int input = 1280 * 720;
  a.OnFrameSizeObserved(input);
  // Unrestricted: nothing to undo.
  SM_EXPECT_TRUE(a.AdaptUp() == VideoStreamAdapter::Status::kLimitReached);
  a.AdaptDown();
  FollowRestrictions(a, input);
  a.AdaptDown();
  FollowRestrictions(a, input);
  SM_EXPECT_EQ(a.resolution_adaptations(), 2);
  const int target = input * 5 / 3;
  SM_EXPECT_TRUE(a.AdaptUp() == VideoStreamAdapter::Status::kValid);
  SM_EXPECT_EQ(a.restrictions().max_pixels_per_frame.value_or(0), target * 12 / 5);
  SM_EXPECT_EQ(a.restrictions().target_pixels_per_frame.value_or(0), target);
  SM_EXPECT_EQ(a.resolution_adaptations(), 1);
  FollowRestrictions(a, input);
  // Undoing the last step removes the restrictions.
  SM_EXPECT_TRUE(a.AdaptUp() == VideoStreamAdapter::Status::kValid);
  SM_EXPECT_TRUE(a.restrictions() == VideoSourceRestrictions{});
  SM_EXPECT_EQ(a.resolution_adaptations(), 0);
}

SM_TEST(VideoStreamAdapter, AwaitsThePreviousAdaptation) {
  VideoStreamAdapter a;
  a.OnFrameSizeObserved(1280 * 720);
  a.AdaptDown();
  // The input has not changed size yet: no second step down.
  SM_EXPECT_TRUE(a.AdaptDown() == VideoStreamAdapter::Status::kAwaitingPreviousAdaptation);
  a.OnFrameSizeObserved(960 * 540);
  SM_EXPECT_TRUE(a.AdaptDown() == VideoStreamAdapter::Status::kValid);
  a.OnFrameSizeObserved(640 * 360);
  SM_EXPECT_TRUE(a.AdaptUp() == VideoStreamAdapter::Status::kValid);
  SM_EXPECT_TRUE(a.AdaptUp() == VideoStreamAdapter::Status::kAwaitingPreviousAdaptation);
}

SM_TEST(VideoAdapter, ScalesThroughLibwebrtcsSeries) {
  // 1280x720: 3/4 -> 960x540, 1/2 -> 640x360, 3/8 -> 480x270, 1/4 -> 320x180.
  VideoAdapter v;
  int w = 0, h = 0;
  SM_EXPECT_TRUE(v.AdaptFrameResolution(1280, 720, &w, &h));
  SM_EXPECT_EQ(w, 1280);
  SM_EXPECT_EQ(h, 720);
  const int expected[][2] = {{960, 540}, {640, 360}, {480, 270}, {320, 180}};
  int pixels = 1280 * 720;
  for (const auto& e : expected) {
    pixels = GetLowerResolutionThan(pixels);
    v.OnSinkWants({pixels, std::nullopt});
    v.AdaptFrameResolution(1280, 720, &w, &h);
    SM_EXPECT_EQ(w, e[0]);
    SM_EXPECT_EQ(h, e[1]);
    pixels = w * h;
  }
  // Going up: target 5/3 of 320x180, maximum 12/5 of that -> 480x270 is closest.
  const int target = GetHigherResolutionThan(320 * 180);
  v.OnSinkWants({target * 12 / 5, target});
  v.AdaptFrameResolution(1280, 720, &w, &h);
  SM_EXPECT_EQ(w, 480);
  SM_EXPECT_EQ(h, 270);
  // 960x540 divides by 3, so the series starts with 2/3: 3/5 of it -> 640x360.
  v.OnSinkWants({GetLowerResolutionThan(960 * 540), std::nullopt});
  v.AdaptFrameResolution(960, 540, &w, &h);
  SM_EXPECT_EQ(w, 640);
  SM_EXPECT_EQ(h, 360);
}
