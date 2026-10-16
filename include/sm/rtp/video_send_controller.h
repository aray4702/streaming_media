// Send-side media controller for one video stream (issue #37, design note
// docs/design/send-controller.md). It is the library's equivalent of the
// frame-dropping and keyframe parts of libwebrtc's VideoStreamEncoder
// (video/video_stream_encoder.cc: OnFrame, MaybeEncodeVideoFrame,
// UpdateTargetBitrate, RunPostEncode): per captured frame it decides encode
// or drop, keyframe or not, and the rate to configure the encoder with. The
// app runs the codec and nothing else.
//
// It composes existing modules and adds no new algorithm: the target and the
// congestion-window pushback come from the session's SendRates
// (SendSideController, issue #21/#53), the drop decision from cc::FrameDropper.
// Takes `now`, reads no clock, starts no thread, has no callbacks.
//
// The encoder bitrate adjuster (cc::EncoderBitrateAdjuster, on by default in
// libwebrtc M157, behind `bitrate_adjuster` here) lowers the rate `EncodeDecision::target` by the encoder's
// measured overshoot (issue #5); the frame dropper keeps working on the
// unadjusted target, as in VideoStreamEncoder::OnBitrateUpdated.
//
// Not wired yet (note section 6): the quality scaler, CPU overuse detection and
// the degradation ladder (the test bench's fake encoder reports no QP). The
// frame dropper is libwebrtc's, applied to keyframes too (issue #5).
#pragma once

#include <cstdint>

#include "sm/cc/encoder_bitrate_adjuster.h"
#include "sm/cc/frame_dropper.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"
#include "sm/rtp/session.h"

namespace sm::rtp {

struct VideoSendControllerConfig {
  TimeDelta keyframe_interval = TimeDelta::Zero();  // Periodic keyframe; zero = off.
  double fps = 30;                                  // Input frame rate fed to the dropper.
  // RateControlSettings bitrate_adjuster (true in M157; off here until it is
  // judged against libwebrtc as a whole, issue #5); `adjuster_use_headroom` and
  // the codec minimum bitrate are in `adjuster`.
  bool bitrate_adjuster = false;
  cc::EncoderBitrateAdjusterConfig adjuster;
  EventSink* events = nullptr;
};

struct CapturedFrame {
  int width = 0, height = 0;
};

enum class DropReason { kNone, kPaused, kCongestionWindow, kFrameDropper };

struct EncodeDecision {
  bool encode = false;
  DropReason reason = DropReason::kNone;  // Why the frame is dropped.
  bool keyframe = false;
  DataRate target;  // Rate to configure the encoder with (after the bitrate adjuster).
  int width = 0, height = 0;
  double fps = 0;
};

struct EncodedFrameReport {
  DataSize size;
  bool keyframe = false;
};

class VideoSendController : public MetricsSource {
 public:
  // `session` (not owned) supplies the rates and the pushback state.
  VideoSendController(VideoSendControllerConfig cfg, Session& session);

  // Per captured frame, before encoding.
  EncodeDecision OnCapturedFrame(const CapturedFrame& frame, Timestamp now);
  // After a frame the decision allowed was encoded. Not called for a drop.
  void OnEncoded(const EncodedFrameReport& report, Timestamp now);
  // Forward every Session event: the controller acts on kEncoderPause,
  // kEncoderResume and kKeyframeRequested (a peer PLI/FIR or the outage monitor).
  void OnSessionEvent(const SessionEvent& e);
  void RequestKeyframe() { keyframe_needed_ = true; }

  const cc::FrameDropper& frame_dropper() const { return dropper_; }
  const cc::EncoderBitrateAdjuster& bitrate_adjuster() const { return adjuster_; }
  bool paused() const { return paused_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  VideoSendControllerConfig cfg_;
  Session& session_;
  cc::FrameDropper dropper_;
  cc::EncoderBitrateAdjuster adjuster_;
  bool paused_ = false;
  bool keyframe_needed_ = true;  // The first frame is a keyframe.
  Timestamp last_keyframe_ = Timestamp::MinusInfinity();
  uint64_t dropped_[4] = {0, 0, 0, 0};
  uint64_t encoded_ = 0, keyframes_ = 0;
};

}  // namespace sm::rtp
