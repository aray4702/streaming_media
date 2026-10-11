// Section 7 unit tests.
#include "sm/video/codec_fallback.h"
#include "sm/video/codec_negotiation.h"
#include "sm/video/content_policy.h"
#include "sm/video/frame_scaler.h"
#include "sm/video/frame_scheduler.h"
#include "sm/video/freeze_detector.h"
#include "sm/video/orientation.h"
#include "sm/video/quality_scaler.h"
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

SM_TEST(FreezeDetector, SmoothPlaybackHasNoFreezes) {
  FreezeDetector fd;
  for (int i = 0; i < 300; ++i) fd.OnFrameRendered(Ms(i * 33 + (i % 3) * 5));
  SM_EXPECT_EQ(fd.stats().freeze_count, 0u);
  // Intervals of 38, 38, 23 ms: sum(d) / sum(d^2) = 99 / 3.417 = 28.97 fps,
  // below the arithmetic 30.3 fps because irregular timing looks worse.
  SM_EXPECT_NEAR(fd.HarmonicFps(), 28.97, 0.05);
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
