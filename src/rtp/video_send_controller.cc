#include "sm/rtp/video_send_controller.h"

#include <algorithm>

namespace sm::rtp {

VideoSendController::VideoSendController(VideoSendControllerConfig cfg, Session& session)
    : cfg_(cfg), session_(session), adjuster_(cfg.adjuster) {}

namespace {
// FrameCadenceAdapterInterface::kFrameRateAveragingWindowSizeMs.
constexpr int64_t kFrameRateWindowMs = (1000 / 30) * 90;
}  // namespace

// RateStatistics::Rate then ::Update, as FrameCadenceAdapterImpl does per frame
// (rtc_base/rate_statistics.cc, not in reference/: written from the class).
void VideoSendController::UpdateFrameRate(Timestamp now) {
  const int64_t t = now.ms();
  auto erase_old = [&](int64_t at) {
    const int64_t oldest = at - kFrameRateWindowMs + 1;
    while (!frame_times_ms_.empty() && frame_times_ms_.front() < oldest) frame_times_ms_.pop_front();
  };
  erase_old(t);
  int64_t active = 0;
  if (first_frame_ms_ != -1) active = first_frame_ms_ <= t - kFrameRateWindowMs ? kFrameRateWindowMs : t - first_frame_ms_ + 1;
  const int64_t n = static_cast<int64_t>(frame_times_ms_.size());
  if (n == 0 || active <= 1 || (n <= 1 && active < kFrameRateWindowMs)) {
    input_fps_ = 0;
  } else {
    input_fps_ = static_cast<int>(static_cast<float>(n) * (1000.0f / static_cast<float>(active)) + 0.5f);
  }
  // RateStatistics::Update keeps the window start if the newest sample was
  // recent (younger than 1.5 windows); Rate() above has already erased the old
  // samples, as in FrameCadenceAdapter, so this matches it in that call order.
  const bool last_sample_is_recent = !frame_times_ms_.empty() && frame_times_ms_.back() > t - kFrameRateWindowMs * 3 / 2;
  if (first_frame_ms_ == -1 || (frame_times_ms_.empty() && !last_sample_is_recent)) first_frame_ms_ = t;
  frame_times_ms_.push_back(t);
}

EncodeDecision VideoSendController::OnCapturedFrame(const CapturedFrame& frame, Timestamp now) {
  UpdateFrameRate(now);
  const double fps = input_fps();
  const SendRates& rates = session_.rates();
  const DataRate target = rates.video_target;
  dropper_.SetRates(target, fps);
  // VideoSendStreamImpl::OnBitrateUpdated: link_allocation =
  // max(encoder_target, encoder_target - protection) = the encoder target, so
  // the adjuster's headroom is only what the encoder adds (bandwidth_allocation
  // - sum(allocation), zero here).
  // VideoStreamEncoder::UpdateBitrateAllocation: the allocator may raise the
  // allocation to the codec minimum, and the bandwidth allocation is at least
  // that (the dropper above has already been given the unraised target).
  DataRate allocation = target;
  if (cfg_.allocation_min_bitrate.bps() > 0 && target.bps() > 0) {
    allocation = std::max(cfg_.allocation_min_bitrate, std::min(target, cfg_.allocation_max_bitrate));
  }
  const DataRate link = std::max(target, allocation);
  const DataRate encoder_rate = cfg_.bitrate_adjuster ? adjuster_.AdjustRate(allocation, link, fps, now) : allocation;
  // No keyframe sent yet: the first one is already due through keyframe_needed_.
  const bool periodic = cfg_.keyframe_interval.us() > 0 && last_keyframe_.IsFinite() &&
                        now - last_keyframe_ >= cfg_.keyframe_interval;
  const bool key = keyframe_needed_ || periodic;

  EncodeDecision d;
  d.keyframe = key;
  d.target = encoder_rate;
  d.width = frame.width;
  d.height = frame.height;
  d.fps = fps;
  // As VideoStreamEncoder::OnFrame: the outage pause, then congestion-window
  // pushback (keyframes included), then the frame dropper (keyframes included).
  if (paused_) {
    d.reason = DropReason::kPaused;
  } else if (session_.send_side().DropCapturedFrame()) {
    d.reason = DropReason::kCongestionWindow;
  } else if (dropper_.ShouldDrop(fps)) {
    d.reason = DropReason::kFrameDropper;
  } else {
    d.encode = true;
    return d;
  }
  d.keyframe = false;
  if (d.reason == DropReason::kFrameDropper && cfg_.bitrate_adjuster) adjuster_.OnFrameDropped(now);
  ++dropped_[static_cast<int>(d.reason)];
  SM_EVENT(cfg_.events, now, "video_send", "frame_dropped", static_cast<double>(d.reason), 0);
  return d;
}

void VideoSendController::OnEncoded(const EncodedFrameReport& r, Timestamp now) {
  ++encoded_;
  dropper_.OnEncodedFrame(r.size, r.keyframe);
  if (cfg_.bitrate_adjuster) adjuster_.OnEncodedFrame(r.size, now);
  if (r.keyframe) {
    keyframe_needed_ = false;  // Sticky until a keyframe was actually produced.
    last_keyframe_ = now;
    ++keyframes_;
  }
}

void VideoSendController::OnSessionEvent(const SessionEvent& e) {
  switch (e.type) {
    case SessionEventType::kKeyframeRequested:
      // A peer PLI or FIR (named by its SSRC) goes through EncoderRtcpFeedback's
      // minimum interval; the outage monitor's own request (SSRC 0) does not.
      if (e.ssrc != 0 && cfg_.min_keyframe_request_interval > TimeDelta::Zero()) {
        if (last_keyframe_request_ + cfg_.min_keyframe_request_interval > e.time) {
          ++keyframe_requests_ignored_;
          SM_EVENT(cfg_.events, e.time, "video_send", "keyframe_request_ignored", 0, 0);
          break;
        }
        last_keyframe_request_ = e.time;
      }
      keyframe_needed_ = true;
      break;
    case SessionEventType::kEncoderPause: paused_ = true; break;
    case SessionEventType::kEncoderResume: paused_ = false; break;
    default: break;
  }
}

void VideoSendController::ExportMetrics(MetricsWriter& w) const {
  w.Counter("encoded", encoded_);
  w.Counter("keyframes", keyframes_);
  w.Counter("keyframe_requests_ignored", keyframe_requests_ignored_);
  w.Counter("dropped_paused", dropped_[static_cast<int>(DropReason::kPaused)]);
  w.Counter("dropped_cwnd", dropped_[static_cast<int>(DropReason::kCongestionWindow)]);
  w.Counter("dropped_dropper", dropped_[static_cast<int>(DropReason::kFrameDropper)]);
  w.Gauge("paused", paused_ ? 1 : 0);
}

}  // namespace sm::rtp
