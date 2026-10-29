#include "sm/rtp/video_send_controller.h"

#include <vector>

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

// VideoSendStreamImpl::OnBitrateUpdated: below the encoder maximum,
// link_allocation equals the encoder target, so the adjuster sees no headroom
// from the session's audio share (above the maximum it does, issue #326).
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

// 0 turns the filter off: every request is passed on (300 ms by default since #580, as libwebrtc).
SM_TEST(VideoSendController, PeerKeyframeRequestsAreNotFilteredWithAZeroInterval) {
  Session s(Cfg());
  VideoSendControllerConfig cfg;
  cfg.min_keyframe_request_interval = TimeDelta::Zero();
  VideoSendController c(cfg, s);
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

// libwebrtc's CheckQpTask due at a frame's capture time runs after that frame
// was encoded, and the restriction reaches the source through a worker-queue
// task (VideoStreamEncoder::OnVideoSourceRestrictionsUpdated), so the frame
// captured at the check keeps the old size and the next one gets the new size
// (issue #348).
SM_TEST(VideoSendController, QualityScalerStepAppliesFromTheFrameAfterTheCheck) {
  Session s(Cfg());
  VideoSendControllerConfig cfg;
  cfg.quality_scaling = true;
  VideoSendController c(cfg, s);
  // 25 fps, so the checks (every 2 s from the first frame) are due exactly at
  // capture times. The one at 4 s sees enough low-QP frames to adapt up.
  int t = 0;
  EncodeDecision d;
  int first_up = -1, last_360 = -1;
  for (; t <= 10000 && first_up < 0; t += 40) {
    d = c.OnCapturedFrame({1280, 720}, At(t));
    if (last_360 >= 0 && d.height > 360) {
      first_up = t;
      SM_EXPECT_TRUE(d.keyframe);
    } else if (d.encode && d.height == 360) {
      last_360 = t;  // The initial frame drop has scaled 720p down to 360p.
    }
    if (d.encode) c.OnEncoded({DataSize::Bytes(1000), d.keyframe, 10}, At(t));
  }
  SM_EXPECT_EQ(last_360, 4000);  // Encoded at 360p although the check is due now.
  SM_EXPECT_EQ(first_up, 4040);
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

SM_TEST(VideoSendController, NewFrameSizeStartsAFreshBitrateAdjuster) {
  // Issue #346: VideoStreamEncoder::ReconfigureEncoder makes a new
  // EncoderBitrateAdjuster (and resets the frame dropper) on a new frame size,
  // so the encoder target is the allocation / kDefaultUtilizationFactor (1.2)
  // for kMinFramesSinceLayoutChange (30) frames after every resolution switch.
  Session s(Cfg());
  VideoSendControllerConfig cfg;
  cfg.quality_scaling = true;
  VideoSendController c(cfg, s);
  const double allocation_kbps = s.rates().video_target.kbps();
  int t = 0;
  EncodeDecision d;
  do {
    d = c.OnCapturedFrame({1280, 720}, At(t));
    t += 33;
  } while (!d.encode);
  // Frames exactly on the encoder target: once the first 30 frames are past,
  // the overshoot detector's factor is about 1.0. QP 10 makes the scaler adapt
  // up after its 2 s check.
  int switch_frame = -1, frames_after = 0;
  double before_switch_kbps = 0;
  for (int i = 0; i < 300; ++i, t += 33) {
    const int height = d.height;
    if (d.encode) c.OnEncoded({DataSize::Bytes(d.target.bps() / 8 / 30), d.keyframe, 10}, At(t - 33));
    d = c.OnCapturedFrame({1280, 720}, At(t));
    if (switch_frame < 0 && d.height != height) {
      switch_frame = i;
      SM_EXPECT_TRUE(d.keyframe);
    }
    if (switch_frame < 0) {
      before_switch_kbps = d.target.kbps();
    } else if (d.encode && frames_after++ < 30) {
      SM_EXPECT_NEAR(d.target.kbps(), allocation_kbps / 1.2, 0.5);
    }
  }
  SM_EXPECT_GE(switch_frame, 0);
  // Before the switch the adjuster had learned a factor near 1.0.
  SM_EXPECT_GT(before_switch_kbps, allocation_kbps / 1.1);
  SM_EXPECT_GE(frames_after, 30);
}

// Issue #569: VideoStreamEncoder runs EncoderBitrateAdjuster::AdjustRateAllocation only in
// UpdateBitrateAllocation: on an allocation update (OnBitrateUpdated), on ReconfigureEncoder and every
// kParameterUpdateIntervalMs (1 s) in MaybeEncodeVideoFrame. The encoder keeps its last rate in between.
SM_TEST(VideoSendController, BitrateAdjusterRunsOnAllocationUpdatesAndEverySecond) {
  // Written for 1200-byte packets, the default before #639 (now 1460): at 1460 this rate pattern gives
  // the re-run after the update the same rate as the last one, so the change below would not show.
  SessionConfig sc = Cfg();
  sc.send_side.max_packet_size = 1200;
  Session s(sc);
  s.AddVideoSend(VideoStreamConfig{});
  VideoSendControllerConfig cfg;
  cfg.quality_scaling = false;
  cfg.adjuster_on_update = true;
  VideoSendController c(cfg, s);
  // The encoder produces 1.5 times the rate it is given, so the adjuster's factor keeps moving once the
  // first 30 frames are past.
  std::vector<int> change_ms;
  DataRate last;
  int t = 0;
  for (; t < 3500; t += 33) {
    const EncodeDecision d = c.OnCapturedFrame({640, 360}, At(t));
    if (t > 0 && d.target != last) change_ms.push_back(t);
    last = d.target;
    if (d.encode) c.OnEncoded({DataSize::Bytes(d.target.bps() * 3 / 2 / 8 / 30), false}, At(t));
  }
  // Held between the 1 s parameter updates (per frame it would move on every frame after the 30th).
  SM_EXPECT_GE(change_ms.size(), size_t{2});
  for (size_t i = 1; i < change_ms.size(); ++i) SM_EXPECT_GE(change_ms[i] - change_ms[i - 1], 1000);
  // An allocation update re-runs the adjuster at the next frame, without waiting for the second.
  const uint64_t updates = s.rates().video_allocation_updates;
  SM_EXPECT_GT(updates, uint64_t{0});
  s.send_side().OnVideoEncoderReconfigured(At(t));
  SM_EXPECT_EQ(s.rates().video_allocation_updates, updates + 1);
  const EncodeDecision d = c.OnCapturedFrame({640, 360}, At(t));
  SM_EXPECT_TRUE(t - change_ms.back() < 1000);
  SM_EXPECT_TRUE(d.target != last);
  SM_EXPECT_TRUE(d.target == c.bitrate_adjuster().adjusted());
}

// VideoStreamEncoder's pending_frame_ (video_stream_encoder.cc MaybeEncodeVideoFrame and
// OnBitrateUpdated): a frame dropped while the encoder is paused is held, and the resume encodes it at
// once, before the next frame is captured, with its own capture time.
SM_TEST(VideoSendController, PausedFrameIsHeldAndEncodedOnResume) {
  Session s(Cfg());
  VideoSendController c({}, s);
  SM_EXPECT_TRUE(c.OnCapturedFrame({640, 360}, At(0)).encode);
  c.OnEncoded({DataSize::Bytes(4000), true, 30}, At(0));
  SM_EXPECT_FALSE(c.MaybeEncodePendingFrame(At(10)).has_value());  // Never paused.
  c.OnSessionEvent(Ev(SessionEventType::kEncoderPause));
  auto d = c.OnCapturedFrame({640, 360}, At(33));
  SM_EXPECT_TRUE(d.reason == DropReason::kPaused);
  SM_EXPECT_TRUE(d.hold);
  SM_EXPECT_FALSE(c.MaybeEncodePendingFrame(At(40)).has_value());  // Still paused.
  c.OnSessionEvent(Ev(SessionEventType::kEncoderResume));
  const std::optional<EncodeDecision> p = c.MaybeEncodePendingFrame(At(50));
  SM_EXPECT_TRUE(p.has_value());
  SM_EXPECT_TRUE(p->encode);
  SM_EXPECT_TRUE(p->capture_time == At(33));
  SM_EXPECT_EQ(p->width, 640);
  SM_EXPECT_EQ(p->height, 360);
  SM_EXPECT_FALSE(p->keyframe);
  SM_EXPECT_TRUE(p->target.bps() > 0);
  c.OnEncoded({DataSize::Bytes(1000), false, 30}, At(50));
  SM_EXPECT_FALSE(c.MaybeEncodePendingFrame(At(55)).has_value());  // Released.
  d = c.OnCapturedFrame({640, 360}, At(66));
  SM_EXPECT_TRUE(d.encode);
  SM_EXPECT_FALSE(d.hold);
  SM_EXPECT_TRUE(d.capture_time == At(66));
}

// Each MaybeEncodeVideoFrame drops the frame held before it (kEncoderQueue): only the last frame
// captured during the pause is left to encode.
SM_TEST(VideoSendController, NextCapturedFrameReplacesTheHeldFrame) {
  Session s(Cfg());
  VideoSendController c({}, s);
  c.OnCapturedFrame({640, 360}, At(0));
  c.OnEncoded({DataSize::Bytes(4000), true, 30}, At(0));
  c.OnSessionEvent(Ev(SessionEventType::kEncoderPause));
  for (int t = 33; t <= 99; t += 33) SM_EXPECT_TRUE(c.OnCapturedFrame({640, 360}, At(t)).hold);
  c.OnSessionEvent(Ev(SessionEventType::kEncoderResume));
  const auto p = c.MaybeEncodePendingFrame(At(110));
  SM_EXPECT_TRUE(p.has_value() && p->capture_time == At(99));
}

// kPendingFrameTimeoutMs: a frame held for 1 s or more is released without an encode.
SM_TEST(VideoSendController, HeldFrameOlderThanOneSecondIsNotEncoded) {
  Session s(Cfg());
  VideoSendController c({}, s);
  c.OnCapturedFrame({640, 360}, At(0));
  c.OnEncoded({DataSize::Bytes(4000), true, 30}, At(0));
  c.OnSessionEvent(Ev(SessionEventType::kEncoderPause));
  c.OnCapturedFrame({640, 360}, At(33));
  c.OnSessionEvent(Ev(SessionEventType::kEncoderResume));
  SM_EXPECT_FALSE(c.MaybeEncodePendingFrame(At(1033)).has_value());
  // 999 ms is still in time.
  c.OnSessionEvent(Ev(SessionEventType::kEncoderPause));
  c.OnCapturedFrame({640, 360}, At(1066));
  c.OnSessionEvent(Ev(SessionEventType::kEncoderResume));
  SM_EXPECT_TRUE(c.MaybeEncodePendingFrame(At(2065)).has_value());
}

// A frame dropped for its size is held too, but without a resume the next frame drops it; on a resume
// a frame still too large (DropDueToSize) stays held and is not encoded.
SM_TEST(VideoSendController, SizeDroppedFrameIsHeldButNotEncodedWhileTooLarge) {
  Session s(Cfg());
  VideoSendController c({}, s);  // Quality scaling on: the start rate allows 640x480 at most.
  c.OnSessionEvent(Ev(SessionEventType::kEncoderPause));
  auto d = c.OnCapturedFrame({1280, 720}, At(0));
  SM_EXPECT_TRUE(d.reason == DropReason::kSize);
  SM_EXPECT_TRUE(d.hold);
  c.OnSessionEvent(Ev(SessionEventType::kEncoderResume));
  SM_EXPECT_FALSE(c.MaybeEncodePendingFrame(At(10)).has_value());  // 1280x720 is still too large.
  d = c.OnCapturedFrame({1280, 720}, At(33));  // Adapted to 960x540: still too large.
  SM_EXPECT_TRUE(d.reason == DropReason::kSize);
  SM_EXPECT_FALSE(c.MaybeEncodePendingFrame(At(40)).has_value());  // Not paused since the last look.
  SM_EXPECT_TRUE(c.OnCapturedFrame({1280, 720}, At(66)).encode);
}

// InitialFrameDropper::SetTargetBitrate with QualityScalerSettings' initial_bitrate_interval_ms and
// initial_bitrate_factor: the first target below factor x start bitrate within the interval restarts the
// initial frame drop, once. Unset (the default, as M157) it never does.
SM_TEST(VideoSendController, StartBitrateDropRestartsTheInitialFrameDrop) {
  for (const bool configured : {false, true}) {
    Session s(Cfg());
    s.AddVideoSend(VideoStreamConfig{});
    VideoSendControllerConfig cfg;
    if (configured) {
      cfg.initial_bitrate_interval = TimeDelta::Seconds(60);
      cfg.initial_bitrate_factor = 0.5;
    }
    VideoSendController c(cfg, s);
    // 320x180 fits any rate: the first frame ends the initial frame drop.
    int t = 0;
    const EncodeDecision first = c.OnCapturedFrame({320, 180}, At(t));
    SM_EXPECT_TRUE(first.encode);
    c.OnEncoded({DataSize::Bytes(4000), true, 30}, At(t));
    // The receiver caps the estimate at 100 kbps, under half the start rate (about 290 kbps for video).
    s.Process(At(t));
    s.send_side().OnRemb(DataRate::KilobitsPerSec(100));
    t = 33;
    s.Process(At(t));
    SM_EXPECT_TRUE(s.rates().video_target < first.target * 0.5);
    // The camera now delivers 640x360: above 320x240, too large for under 300 kbps.
    const EncodeDecision d = c.OnCapturedFrame({640, 360}, At(t));
    if (configured) {
      // The initial frame drop runs again: dropped and adapted down, as at the start.
      SM_EXPECT_TRUE(d.reason == DropReason::kSize);
      SM_EXPECT_EQ(c.stream_adapter().resolution_adaptations(), 1);
    } else {
      SM_EXPECT_TRUE(d.encode);
      SM_EXPECT_EQ(c.stream_adapter().resolution_adaptations(), 0);
    }
    // As at the start it drops until a frame fits (480x270 too, then 320x180), and then no more.
    int size_drops = d.reason == DropReason::kSize ? 1 : 0;
    for (t = 66; t < 3000; t += 33) {
      s.Process(At(t));
      if (t == 1000) s.send_side().OnRemb(DataRate::KilobitsPerSec(60));
      const EncodeDecision e = c.OnCapturedFrame({640, 360}, At(t));
      if (e.reason == DropReason::kSize) ++size_drops;
      if (e.encode) c.OnEncoded({DataSize::Bytes(300), e.keyframe, 30}, At(t));
    }
    SM_EXPECT_EQ(size_drops, configured ? 2 : 0);
    SM_EXPECT_EQ(c.stream_adapter().resolution_adaptations(), configured ? 2 : 0);
  }
}
