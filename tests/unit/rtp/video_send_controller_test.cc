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
  off.bitrate_adjuster = false;
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

SM_TEST(VideoSendController, BitrateAdjusterIsOnByDefaultAsInM157) {
  SM_EXPECT_TRUE(VideoSendControllerConfig{}.bitrate_adjuster);
}

// VideoStreamEncoder::GetInputFramerateFps: the measured rate of captured
// frames (FrameCadenceAdapter's RateStatistics, 2970 ms window), the configured
// maximum while unavailable.
SM_TEST(VideoSendController, InputFrameRateIsMeasuredNotConfigured) {
  Session s(Cfg());
  VideoSendControllerConfig cfg;
  cfg.fps = 30;
  VideoSendController c(cfg, s);
  // Unavailable for the first frames: the configured maximum.
  SM_EXPECT_NEAR(c.OnCapturedFrame({}, At(0)).fps, 30.0, 0.0);
  SM_EXPECT_NEAR(c.OnCapturedFrame({}, At(50)).fps, 30.0, 0.0);
  EncodeDecision d;
  for (int t = 100; t <= 4000; t += 50) d = c.OnCapturedFrame({}, At(t));
  SM_EXPECT_NEAR(d.fps, 20.0, 1.0);
  // The frame dropper is fed the same rate.
  SM_EXPECT_NEAR(c.frame_dropper().incoming_frame_rate(), 20.0, 1.0);
  // Frames dropped by the controller still count as captured: the rate follows
  // the camera, not what was encoded.
  for (int t = 4050; t <= 8000; t += 100) d = c.OnCapturedFrame({}, At(t));
  SM_EXPECT_NEAR(d.fps, 10.0, 1.0);
}

// VideoSendStreamImpl::OnBitrateUpdated: link_allocation equals the encoder
// target, so the adjuster sees no headroom from the session's audio share.
SM_TEST(VideoSendController, LinkAllocationEqualsTheEncoderTarget) {
  Session s(Cfg());
  VideoSendController c({}, s);
  const DataRate target = s.rates().video_target;
  for (int t = 0; t < 3000; t += 33) {
    const auto d = c.OnCapturedFrame({}, At(t));
    SM_EXPECT_TRUE(d.encode);
    // Encoder output exactly on target: no overshoot, so the adjusted rate
    // never exceeds the target (no headroom to grow into).
    c.OnEncoded({DataSize::Bytes(target.bps() / 8 / 30), false}, At(t));
    SM_EXPECT_TRUE(d.target <= target);
  }
}

// SimulcastRateAllocator::DistributeAllocationToSimulcastLayers (single
// stream): the encoder is configured with max(minBitrate, min(target, max)),
//; the frame dropper still gets the target (VideoStreamEncoder::OnBitrateUpdated).
SM_TEST(VideoSendController, AllocationIsRaisedToTheCodecMinimum) {
  Session s(Cfg());
  const DataRate target = s.rates().video_target;
  SM_EXPECT_TRUE(target.bps() > 0);
  VideoSendControllerConfig cfg;
  cfg.bitrate_adjuster = false;
  cfg.allocation_min_bitrate = target * 2;
  VideoSendController raised(cfg, s);
  SM_EXPECT_TRUE(raised.OnCapturedFrame({}, At(0)).target == target * 2);
  // A maximum below the target caps it; the minimum still wins over the maximum.
  cfg.allocation_min_bitrate = DataRate::BitsPerSec(1);
  cfg.allocation_max_bitrate = target * 0.5;
  VideoSendController capped(cfg, s);
  SM_EXPECT_TRUE(capped.OnCapturedFrame({}, At(0)).target == target * 0.5);
  // Off (the default): the encoder gets the target as before.
  cfg = {};
  cfg.bitrate_adjuster = false;
  VideoSendController off(cfg, s);
  SM_EXPECT_TRUE(off.OnCapturedFrame({}, At(0)).target == target);
}

// EncoderRtcpFeedback::OnReceivedIntraFrameRequest (video/encoder_rtcp_feedback.cc):
// a PLI/FIR less than kMinKeyframeSendIntervalMs (300 ms) after the last one it
// passed on is ignored; one exactly 300 ms later goes through.
SM_TEST(VideoSendController, PeerKeyframeRequestsWithin300msAreIgnored) {
  Session s(Cfg());
  VideoSendControllerConfig cfg;
  cfg.min_keyframe_request_interval = TimeDelta::Millis(300);
  VideoSendController c(cfg, s);
  c.OnCapturedFrame({}, At(0));
  c.OnEncoded({DataSize::Bytes(4000), true}, At(0));
  auto pli = [](int ms) {
    SessionEvent e = Ev(SessionEventType::kKeyframeRequested);
    e.time = At(ms);
    e.ssrc = 2;
    return e;
  };
  c.OnSessionEvent(pli(1000));
  SM_EXPECT_TRUE(c.OnCapturedFrame({}, At(1033)).keyframe);
  c.OnEncoded({DataSize::Bytes(4000), true}, At(1033));
  c.OnSessionEvent(pli(1299));  // 299 ms after the last one passed on.
  SM_EXPECT_FALSE(c.OnCapturedFrame({}, At(1300)).keyframe);
  c.OnSessionEvent(pli(1300));  // 300 ms: passed on.
  SM_EXPECT_TRUE(c.OnCapturedFrame({}, At(1333)).keyframe);
  c.OnEncoded({DataSize::Bytes(4000), true}, At(1333));
  // The outage monitor's own request (SSRC 0) is not a peer request: not filtered.
  SessionEvent own = Ev(SessionEventType::kKeyframeRequested);
  own.time = At(1400);
  c.OnSessionEvent(own);
  SM_EXPECT_TRUE(c.OnCapturedFrame({}, At(1433)).keyframe);
}

// Off by default (keyframe v1): every request is passed on.
SM_TEST(VideoSendController, PeerKeyframeRequestsAreNotFilteredByDefault) {
  Session s(Cfg());
  VideoSendController c({}, s);
  c.OnCapturedFrame({}, At(0));
  c.OnEncoded({DataSize::Bytes(4000), true}, At(0));
  SessionEvent e = Ev(SessionEventType::kKeyframeRequested);
  e.ssrc = 2;
  e.time = At(100);
  c.OnSessionEvent(e);
  SM_EXPECT_TRUE(c.OnCapturedFrame({}, At(133)).keyframe);
  c.OnEncoded({DataSize::Bytes(4000), true}, At(133));
  e.time = At(150);
  c.OnSessionEvent(e);
  SM_EXPECT_TRUE(c.OnCapturedFrame({}, At(166)).keyframe);
}

// Issue #84 step 4: QualityScalerResource with MAINTAIN_FRAMERATE and the
// initial frame drop (VideoStreamEncoder::DropDueToSize).
SM_TEST(VideoSendController, QualityScalingDropsInitialFramesAndScalesDown) {
  Session s(Cfg());
  VideoSendControllerConfig cfg;
  cfg.quality_scaling = true;
  VideoSendController c(cfg, s);
  const int64_t target_bps = s.rates().video_target.bps();
  SM_EXPECT_TRUE(target_bps >= 300000 && target_bps < 500000);  // The start rate: 640x480 at most.
  // 720p is too large: frames are dropped, each one adapting down (960x540,
  // then 640x360 through VideoAdapter's series), until one fits.
  const int sizes[][2] = {{1280, 720}, {960, 540}};
  int t = 0;
  for (const auto& sz : sizes) {
    auto d = c.OnCapturedFrame({1280, 720}, At(t));
    SM_EXPECT_FALSE(d.encode);
    SM_EXPECT_TRUE(d.reason == DropReason::kSize);
    SM_EXPECT_EQ(d.width, sz[0]);
    SM_EXPECT_EQ(d.height, sz[1]);
    t += 33;
  }
  auto d = c.OnCapturedFrame({1280, 720}, At(t));
  SM_EXPECT_TRUE(d.encode);
  SM_EXPECT_TRUE(d.keyframe);
  SM_EXPECT_EQ(d.width, 640);
  SM_EXPECT_EQ(d.height, 360);
  SM_EXPECT_EQ(c.stream_adapter().resolution_adaptations(), 2);
  // The initial frame drop is over once a frame passed it.
  c.OnEncoded({DataSize::Bytes(4000), true, 30}, At(t));
  SM_EXPECT_TRUE(c.OnCapturedFrame({1280, 720}, At(t += 33)).width == 640);
}

SM_TEST(VideoSendController, QualityScalingAdaptsUpOnLowQpWithAKeyframe) {
  Session s(Cfg());
  VideoSendControllerConfig cfg;
  cfg.quality_scaling = true;
  VideoSendController c(cfg, s);
  int t = 0;
  EncodeDecision d;
  do {
    d = c.OnCapturedFrame({1280, 720}, At(t));
    t += 33;
  } while (!d.encode);
  SM_EXPECT_EQ(d.height, 360);
  c.OnEncoded({DataSize::Bytes(4000), true, 10}, At(t - 33));
  // QP 10 (below H.264's low threshold 24): the scaler's check (2 s, 60
  // frames) asks to adapt up: target 5/3 of 640x360, and 960x540 is the
  // closest step under 12/5 of it.
  int first_up_height = 0;
  for (int i = 0; i < 300 && first_up_height == 0; ++i, t += 33) {
    d = c.OnCapturedFrame({1280, 720}, At(t));
    if (d.height > 360) {
      first_up_height = d.height;
      SM_EXPECT_TRUE(d.keyframe);  // A new size resets the encoder.
    }
    if (d.encode) c.OnEncoded({DataSize::Bytes(1000), d.keyframe, 10}, At(t));
  }
  SM_EXPECT_EQ(first_up_height, 540);
  SM_EXPECT_GE(t, 2000);
}

SM_TEST(VideoSendController, QualityScalingInitialDropRunsBeforeThePause) {
  // VideoStreamEncoder::MaybeEncodeVideoFrame: DropDueToSize (and
  // OnMaybeEncodeFrame) come before EncoderPaused, so a paused encoder still
  // adapts down for an oversized frame and a fitting frame ends the initial drop.
  Session s(Cfg());
  VideoSendControllerConfig cfg;
  cfg.quality_scaling = true;
  VideoSendController c(cfg, s);
  c.OnSessionEvent(Ev(SessionEventType::kEncoderPause));
  auto d = c.OnCapturedFrame({1280, 720}, At(0));
  SM_EXPECT_TRUE(d.reason == DropReason::kSize);
  SM_EXPECT_TRUE(c.OnCapturedFrame({1280, 720}, At(33)).reason == DropReason::kSize);
  d = c.OnCapturedFrame({1280, 720}, At(66));
  SM_EXPECT_TRUE(d.reason == DropReason::kPaused);
  SM_EXPECT_EQ(d.height, 360);
  c.OnSessionEvent(Ev(SessionEventType::kEncoderResume));
  d = c.OnCapturedFrame({1280, 720}, At(99));
  SM_EXPECT_TRUE(d.encode);
  SM_EXPECT_EQ(c.stream_adapter().resolution_adaptations(), 2);
}

SM_TEST(VideoSendController, CpuOveruseAdaptsASlowEncoderDown) {
  // EncodeUsageResource on OveruseFrameDetector (issue #84 step 5): every frame
  // takes a whole frame interval from OnCapturedFrame to OnEncoded (100% usage,
  // above the 85% threshold). After 120 samples and two overusing checks in a
  // row it adapts down one step: 3/5 of 640x360, which VideoAdapter makes
  // 480x270. QP 30 sits between the quality scaler's thresholds.
  Session s(Cfg());
  VideoSendControllerConfig cfg;
  cfg.quality_scaling = true;
  VideoSendController c(cfg, s);
  int t = 0, down_at = -1;
  for (; t < 30000 && down_at < 0; t += 33) {
    const auto d = c.OnCapturedFrame({640, 360}, At(t));
    if (d.height < 360) down_at = t;
    // 1000 bytes a frame (240 kbps) stays under the start rate: no frame dropper drops.
    if (d.encode) c.OnEncoded({DataSize::Bytes(1000), d.keyframe, 30}, At(t + 33));
  }
  SM_EXPECT_GE(down_at, 5000);  // Not before 120 samples and two checks.
  SM_EXPECT_EQ(c.OnCapturedFrame({640, 360}, At(t)).height, 270);
  SM_EXPECT_EQ(c.stream_adapter().resolution_adaptations(), 1);
  // The new resolution resets the estimate (OveruseFrameDetector::ResetAll).
  SM_EXPECT_FALSE(c.overuse_detector().encode_usage_percent().has_value());
}

SM_TEST(VideoSendController, CpuUnderuseDoesNotUndoTheQualityScalersSteps) {
  // ResourceAdaptationProcessor::OnResourceUnderuse: the initial frame drop's
  // two steps belong to the quality scaler, so an instant encoder's CPU
  // underuse (usage 0%, after the 40 s ramp-up delay) is not the most limited
  // resource and may not adapt up (kNotMostLimitedResource).
  Session s(Cfg());
  VideoSendControllerConfig cfg;
  cfg.quality_scaling = true;
  VideoSendController c(cfg, s);
  int height = 0;
  for (int t = 0; t < 90000; t += 33) {
    const auto d = c.OnCapturedFrame({1280, 720}, At(t));
    height = d.height;
    if (d.encode) c.OnEncoded({DataSize::Bytes(1500), d.keyframe, 30}, At(t));
  }
  SM_EXPECT_EQ(height, 360);
  SM_EXPECT_EQ(c.stream_adapter().resolution_adaptations(), 2);
  SM_EXPECT_EQ(c.overuse_detector().encode_usage_percent().value_or(-1), 0);
}
