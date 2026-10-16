#include "sm/rtp/video_send_controller.h"

namespace sm::rtp {

VideoSendController::VideoSendController(VideoSendControllerConfig cfg, Session& session)
    : cfg_(cfg), session_(session), dropper_(cfg.dropper) {}

EncodeDecision VideoSendController::OnCapturedFrame(const CapturedFrame& frame, Timestamp now) {
  const DataRate target = session_.rates().video_target;
  dropper_.SetRates(target, cfg_.fps);
  // No keyframe sent yet: the first one is already due through keyframe_needed_.
  const bool periodic = cfg_.keyframe_interval.us() > 0 && last_keyframe_.IsFinite() &&
                        now - last_keyframe_ >= cfg_.keyframe_interval;
  const bool key = keyframe_needed_ || periodic;

  EncodeDecision d;
  d.keyframe = key;
  d.target = target;
  d.width = frame.width;
  d.height = frame.height;
  d.fps = cfg_.fps;
  // As VideoStreamEncoder::OnFrame: the outage pause, then congestion-window
  // pushback (keyframes included), then the frame dropper (keyframes exempt).
  if (paused_) {
    d.reason = DropReason::kPaused;
  } else if (session_.send_side().DropCapturedFrame()) {
    d.reason = DropReason::kCongestionWindow;
  } else if (!key && dropper_.ShouldDrop()) {
    d.reason = DropReason::kFrameDropper;
  } else {
    d.encode = true;
    return d;
  }
  d.keyframe = false;
  ++dropped_[static_cast<int>(d.reason)];
  SM_EVENT(cfg_.events, now, "video_send", "frame_dropped", static_cast<double>(d.reason), 0);
  return d;
}

void VideoSendController::OnEncoded(const EncodedFrameReport& r, Timestamp now) {
  ++encoded_;
  dropper_.OnEncodedFrame(r.size, r.keyframe);
  if (r.keyframe) {
    keyframe_needed_ = false;  // Sticky until a keyframe was actually produced.
    last_keyframe_ = now;
    ++keyframes_;
  }
}

void VideoSendController::OnSessionEvent(const SessionEvent& e) {
  switch (e.type) {
    case SessionEventType::kKeyframeRequested: keyframe_needed_ = true; break;
    case SessionEventType::kEncoderPause: paused_ = true; break;
    case SessionEventType::kEncoderResume: paused_ = false; break;
    default: break;
  }
}

void VideoSendController::ExportMetrics(MetricsWriter& w) const {
  w.Counter("encoded", encoded_);
  w.Counter("keyframes", keyframes_);
  w.Counter("dropped_paused", dropped_[static_cast<int>(DropReason::kPaused)]);
  w.Counter("dropped_cwnd", dropped_[static_cast<int>(DropReason::kCongestionWindow)]);
  w.Counter("dropped_dropper", dropped_[static_cast<int>(DropReason::kFrameDropper)]);
  w.Gauge("paused", paused_ ? 1 : 0);
}

}  // namespace sm::rtp
