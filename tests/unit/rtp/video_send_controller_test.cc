#include "sm/rtp/video_send_controller.h"

#include "sm_test.h"

using namespace sm;
using namespace sm::rtp;

namespace {
SessionConfig Cfg() {
  SessionConfig c;
  c.local_ssrc = 1;
  c.extensions.Register(1, sync::uri::kTransportCc);
  return c;
}

SessionEvent Ev(SessionEventType t) {
  SessionEvent e;
  e.type = t;
  return e;
}

Timestamp At(int ms) { return Timestamp::Millis(ms); }
}  // namespace

SM_TEST(VideoSendController, FirstFrameIsAKeyframeAndStaysSticky) {
  Session s(Cfg());
  VideoSendController c({}, s);
  auto d = c.OnCapturedFrame({640, 360}, At(0));
  SM_EXPECT_TRUE(d.encode);
  SM_EXPECT_TRUE(d.keyframe);
  SM_EXPECT_EQ(d.width, 640);
  // The encoder failed to produce it (no OnEncoded): the next frame asks again.
  d = c.OnCapturedFrame({640, 360}, At(33));
  SM_EXPECT_TRUE(d.keyframe);
  c.OnEncoded({DataSize::Bytes(4000), true}, At(33));
  d = c.OnCapturedFrame({640, 360}, At(66));
  SM_EXPECT_FALSE(d.keyframe);
}

SM_TEST(VideoSendController, KeyframeRequestAndPeriodicKeyframe) {
  Session s(Cfg());
  VideoSendControllerConfig cfg;
  cfg.keyframe_interval = TimeDelta::Seconds(2);
  VideoSendController c(cfg, s);
  c.OnCapturedFrame({}, At(0));
  c.OnEncoded({DataSize::Bytes(4000), true}, At(0));
  SM_EXPECT_FALSE(c.OnCapturedFrame({}, At(1000)).keyframe);
  c.OnSessionEvent(Ev(SessionEventType::kKeyframeRequested));
  SM_EXPECT_TRUE(c.OnCapturedFrame({}, At(1033)).keyframe);
  c.OnEncoded({DataSize::Bytes(4000), true}, At(1033));
  SM_EXPECT_FALSE(c.OnCapturedFrame({}, At(2000)).keyframe);
  SM_EXPECT_TRUE(c.OnCapturedFrame({}, At(3100)).keyframe);  // 2 s since the last keyframe.
}

SM_TEST(VideoSendController, OutagePauseDropsFramesAndResumeWithKeyframe) {
  Session s(Cfg());
  VideoSendController c({}, s);
  c.OnSessionEvent(Ev(SessionEventType::kEncoderPause));
  SM_EXPECT_TRUE(c.paused());
  auto d = c.OnCapturedFrame({}, At(0));
  SM_EXPECT_FALSE(d.encode);
  SM_EXPECT_TRUE(d.reason == DropReason::kPaused);
  SM_EXPECT_FALSE(d.keyframe);
  c.OnSessionEvent(Ev(SessionEventType::kEncoderResume));
  c.OnSessionEvent(Ev(SessionEventType::kKeyframeRequested));
  d = c.OnCapturedFrame({}, At(33));
  SM_EXPECT_TRUE(d.encode);
  SM_EXPECT_TRUE(d.keyframe);
}

SM_TEST(VideoSendController, FrameDropperDropsFramesKeyframesIncluded) {
  Session s(Cfg());
  VideoSendController c({}, s);
  const DataRate target = c.OnCapturedFrame({}, At(0)).target;
  SM_EXPECT_TRUE(target.bps() > 0);
  c.OnEncoded({DataSize::Bytes(4000), true}, At(0));
  // A delta frame ten times the budget: the bucket overflows and the next
  // delta frame is dropped before encoding.
  const int64_t per_frame = target.bps() / 8 / 30;
  c.OnEncoded({DataSize::Bytes(per_frame * 30), false}, At(33));
  auto d = c.OnCapturedFrame({}, At(66));
  SM_EXPECT_FALSE(d.encode);
  SM_EXPECT_TRUE(d.reason == DropReason::kFrameDropper);
  // As VideoStreamEncoder::OnFrame, a keyframe request does not bypass the
  // dropper (issue #5): while the bucket is over its limit a requested
  // keyframe is dropped too, and the request stays pending.
  int dropped = 0;
  for (int i = 0; i < 5; ++i) {
    c.RequestKeyframe();
    d = c.OnCapturedFrame({}, At(99 + 33 * i));
    if (!d.encode) {
      ++dropped;
      SM_EXPECT_TRUE(d.reason == DropReason::kFrameDropper);
      SM_EXPECT_FALSE(d.keyframe);
    }
  }
  SM_EXPECT_GE(dropped, 1);
}

SM_TEST(VideoSendController, DroppedFramesAreCounted) {
  Session s(Cfg());
  VideoSendController c({}, s);
  c.OnSessionEvent(Ev(SessionEventType::kEncoderPause));
  c.OnCapturedFrame({}, At(0));
  c.OnCapturedFrame({}, At(33));
  SM_EXPECT_EQ(CollectMetrics(c).at("dropped_paused"), 2.0);
}

SM_TEST(VideoSendController, BitrateAdjusterLowersTheEncoderRateNotTheDropperTarget) {
  Session s(Cfg());
  VideoSendControllerConfig on;
  on.bitrate_adjuster = true;
  VideoSendControllerConfig off;
  VideoSendController a(on, s);
  VideoSendController b(off, s);
  const DataRate target = s.rates().video_target;
  SM_EXPECT_TRUE(target.bps() > 0);
  const auto da = a.OnCapturedFrame({}, At(0));
  const auto db = b.OnCapturedFrame({}, At(0));
  SM_EXPECT_EQ(db.target.bps(), target.bps());
  // libwebrtc assumes a 20% overshoot until 30 frames were encoded.
  SM_EXPECT_NEAR(da.target.kbps(), target.kbps() / 1.2, 0.1);
  SM_EXPECT_NEAR(a.bitrate_adjuster().adjusted().kbps(), target.kbps() / 1.2, 0.1);
}
