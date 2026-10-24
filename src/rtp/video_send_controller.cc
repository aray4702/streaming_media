#include "sm/rtp/video_send_controller.h"

#include <algorithm>

namespace sm::rtp {

namespace {
video::QualityScalerConfig ScalerConfig(const VideoSendControllerConfig& cfg) {
  video::QualityScalerConfig c;
  c.thresholds = cfg.qp_thresholds;
  c.version = 2;
  c.events = cfg.events;
  return c;
}

// VideoStreamEncoderResourceManager::GetCpuOveruseOptions for a software
// encoder: CpuOveruseOptions' defaults.
cc::CpuOveruseConfig OveruseConfig(const VideoSendControllerConfig& cfg) {
  cc::CpuOveruseConfig c;
  c.version = 2;
  c.events = cfg.events;
  return c;
}

// InitialFrameDropper's kMaxInitialFramedrop.
constexpr int kMaxInitialFramedrop = 4;

// VideoStreamEncoder::DropDueToSize without encoder bitrate limits.
bool DropDueToSize(int pixel_count, int64_t bitrate_bps) {
  if (bitrate_bps < 300000) return pixel_count > 320 * 240;  // qvga
  if (bitrate_bps < 500000) return pixel_count > 640 * 480;  // vga
  return false;
}
}  // namespace

VideoSendController::VideoSendController(VideoSendControllerConfig cfg, Session& session)
    : cfg_(cfg),
      session_(session),
      adjuster_(cfg.adjuster),
      scaler_(ScalerConfig(cfg)),
      stream_adapter_(cfg.min_pixels_per_frame),
      overuse_(OveruseConfig(cfg)) {
  // EncodeUsageResource::SetTargetFrameRate: the codec's maximum frame rate
  // (MaybeUpdateTargetFrameRate; MAINTAIN_FRAMERATE sets no frame-rate limit).
  overuse_.OnTargetFramerateUpdated(static_cast<int>(cfg.fps));
}

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

void VideoSendController::OnQualityScalerSignal(video::QualityScaler::Signal signal, Timestamp now) {
  // QualityScalerResource::OnReportQpUsageHigh / Low.
  using Signal = video::QualityScaler::Signal;
  if (signal == Signal::kAdaptDown) OnResourceOveruse(kQualityResource, now);
  if (signal == Signal::kAdaptUp) OnResourceUnderuse(kQualityResource, now);
}

void VideoSendController::OnCpuSignal(cc::CpuOveruseDetector::Signal signal, Timestamp now) {
  // EncodeUsageResource::AdaptDown / AdaptUp.
  using Signal = cc::CpuOveruseDetector::Signal;
  if (signal == Signal::kOveruse) OnResourceOveruse(kCpuResource, now);
  if (signal == Signal::kUnderuse) OnResourceUnderuse(kCpuResource, now);
}

void VideoSendController::ApplyAdaptation(const video::VideoStreamAdapter::Adaptation& a, Resource r, Timestamp now) {
  const bool down = a.resolution_adaptations > stream_adapter_.resolution_adaptations();
  stream_adapter_.ApplyAdaptation(a);
  // OnVideoSourceRestrictionsUpdated with a reason: UpdateResourceLimitations.
  limits_[r] = ResourceLimit{a.restrictions, a.resolution_adaptations};
  ++(down ? adapt_downs_ : adapt_ups_);
  if (r == kCpuResource) ++(down ? cpu_adapt_downs_ : cpu_adapt_ups_);
  video_adapter_.OnSinkWants(stream_adapter_.restrictions());
  SM_EVENT(cfg_.events, now, "video_send", down ? "adapt_down" : "adapt_up", static_cast<double>(r),
           a.resolution_adaptations);
}

void VideoSendController::OnResourceOveruse(Resource r, Timestamp now) {
  using Status = video::VideoStreamAdapter::Status;
  const auto a = stream_adapter_.GetAdaptationDown();
  if (a.status == Status::kLimitReached) {
    // The resource is held at the most limited restrictions so far
    // (FindMostLimitedResources; none = unrestricted).
    ResourceLimit most;
    for (const auto& l : limits_) {
      if (l && l->adaptations > most.adaptations) most = *l;
    }
    limits_[r] = most;
  }
  if (a.status != Status::kValid) return;
  ApplyAdaptation(a, r, now);
}

void VideoSendController::OnResourceUnderuse(Resource r, Timestamp now) {
  const auto a = stream_adapter_.GetAdaptationUp();
  if (a.status != video::VideoStreamAdapter::Status::kValid) return;
  // FindMostLimitedResources: the resources with the most adaptations (ties
  // compare equal counters, so a resource at zero ties with the start state).
  int most = 0;
  bool r_most = false;
  int n_most = 0;
  for (int i = 0; i < kNumResources; ++i) {
    if (!limits_[i]) continue;
    if (limits_[i]->adaptations > most) {
      most = limits_[i]->adaptations;
      n_most = 1;
      r_most = i == r;
    } else if (limits_[i]->adaptations == most) {
      ++n_most;
      r_most = r_most || i == r;
    }
  }
  if (n_most > 0 && most >= stream_adapter_.resolution_adaptations()) {
    if (!r_most) {
      ++adapt_ups_not_most_limited_;  // kNotMostLimitedResource.
      return;
    }
    if (n_most > 1) {
      // kSharedMostLimitedResource: the resource's limit is relaxed, the
      // stream is not.
      limits_[r] = ResourceLimit{a.restrictions, a.resolution_adaptations};
      return;
    }
  }
  ApplyAdaptation(a, r, now);
}

EncodeDecision VideoSendController::OnCapturedFrame(const CapturedFrame& frame, Timestamp now) {
  UpdateFrameRate(now);
  int width = frame.width, height = frame.height;
  if (cfg_.quality_scaling) {
    // The quality scaler's CheckQpTask, then the source's VideoAdapter with the
    // sink wants, then the frame size the encoder sees (OnFrameSizeObserved).
    OnQualityScalerSignal(scaler_.Check(now), now);
    // OveruseFrameDetector's CheckForOveruse task.
    OnCpuSignal(overuse_.Check(now), now);
    if (frame.width > 0 && frame.height > 0) video_adapter_.AdaptFrameResolution(frame.width, frame.height, &width, &height);
    stream_adapter_.OnFrameSizeObserved(width * height);
    // ReconfigureEncoder for a new frame size: the encoder is reset and its
    // next frame is a keyframe (next_frame_types_). The frame dropper is
    // Reset() and its rates set again (SetRates(codec.startBitrate) below,
    // the encoder target), and bitrate_adjuster_ is a new
    // EncoderBitrateAdjuster, which uses kDefaultUtilizationFactor until
    // kMinFramesSinceLayoutChange frames have been encoded
    // (video/video_stream_encoder.cc l.1430-1442). Reset() is the same as a
    // new adjuster: its overshoot detector is created again on the next
    // AdjustRate, which restarts the frame count.
    if (width != configured_width_ || height != configured_height_) {
      if (configured_width_ > 0) keyframe_needed_ = true;
      dropper_.Reset();
      adjuster_.Reset(now);
      configured_width_ = width;
      configured_height_ = height;
    }
  }
  const double fps = input_fps();
  const SendRates& rates = session_.rates();
  const DataRate target = rates.video_target;
  dropper_.SetRates(target, fps);
  // VideoSendStreamImpl::OnBitrateUpdated: link_allocation is the encoder
  // target less the protection before the target is capped at the encoder
  // maximum (SendRates::video_link_allocation), and at least the target, so
  // the adjuster has headroom when the allocation is above the maximum.
  // Congestion-window pushback lowers only the target
  // (VideoStreamEncoder::UpdateTargetBitrate).
  // VideoStreamEncoder::UpdateBitrateAllocation: the allocator may raise the
  // allocation to the codec minimum, and the bandwidth allocation is at least
  // that (the dropper above has already been given the unraised target).
  DataRate allocation = target;
  if (cfg_.allocation_min_bitrate.bps() > 0 && target.bps() > 0) {
    allocation = std::max(cfg_.allocation_min_bitrate, std::min(target, cfg_.allocation_max_bitrate));
  }
  const DataRate link = std::max({target, allocation, rates.video_link_allocation});
  const DataRate encoder_rate = cfg_.bitrate_adjuster ? adjuster_.AdjustRate(allocation, link, fps, now) : allocation;
  // No keyframe sent yet: the first one is already due through keyframe_needed_.
  const bool periodic = cfg_.keyframe_interval.us() > 0 && last_keyframe_.IsFinite() &&
                        now - last_keyframe_ >= cfg_.keyframe_interval;
  const bool key = keyframe_needed_ || periodic;

  EncodeDecision d;
  d.keyframe = key;
  d.target = encoder_rate;
  d.width = width;
  d.height = height;
  d.fps = fps;
  // The outage pause, then congestion-window pushback (keyframes included),
  // then the frame dropper (keyframes included). With quality scaling the
  // order is VideoStreamEncoder's: the congestion-window drop (OnFrame), then
  // in MaybeEncodeVideoFrame the initial frame drop (DropDueToSize and
  // OnMaybeEncodeFrame), the pause (EncoderPaused) and the frame dropper.
  const bool paused = paused_ || rates.encoder_paused;
  if (paused && !cfg_.quality_scaling) {
    d.reason = DropReason::kPaused;
  } else if (session_.send_side().DropCapturedFrame()) {
    d.reason = DropReason::kCongestionWindow;
  } else if (cfg_.quality_scaling && QualityScalingDropsForSize(width, height, target, now)) {
    d.reason = DropReason::kSize;
  } else if (paused) {
    d.reason = DropReason::kPaused;
  } else if (dropper_.ShouldDrop(fps)) {
    d.reason = DropReason::kFrameDropper;
  } else {
    d.encode = true;
    // EncodeVideoFrame: OnEncodeStarted with the encoded frame and the time
    // the frame was first seen.
    if (cfg_.quality_scaling) overuse_.OnFrameCaptured(width * height, encodes_started_++, now);
    return d;
  }
  d.keyframe = false;
  if (d.reason == DropReason::kFrameDropper && cfg_.bitrate_adjuster) adjuster_.OnFrameDropped(now);
  // QualityScalerResource::OnFrameDropped counts kMediaOptimization (the frame
  // dropper) and kEncoder drops only.
  if (d.reason == DropReason::kFrameDropper && cfg_.quality_scaling) scaler_.OnFrameDropped();
  ++dropped_[static_cast<int>(d.reason)];
  SM_EVENT(cfg_.events, now, "video_send", "frame_dropped", static_cast<double>(d.reason), 0);
  return d;
}

bool VideoSendController::QualityScalingDropsForSize(int width, int height, DataRate target, Timestamp now) {
  // DropDueToSize, then OnMaybeEncodeFrame disables the initial frame drop.
  if (initial_framedrop_ >= kMaxInitialFramedrop || target.bps() <= 0) return false;
  if (!DropDueToSize(width * height, target.bps())) {
    initial_framedrop_ = kMaxInitialFramedrop;
    return false;
  }
  // VideoStreamEncoderResourceManager::OnFrameDroppedDueToSize.
  ++initial_framedrop_;
  // MaybeInitializePixelLimitResource: the step counts as the quality scaler's.
  const auto a = stream_adapter_.GetAdaptationDown();
  if (a.status == video::VideoStreamAdapter::Status::kValid) ApplyAdaptation(a, kQualityResource, now);
  SM_EVENT(cfg_.events, now, "video_send", "initial_frame_drop", width * height, static_cast<double>(target.bps()));
  return true;
}

void VideoSendController::OnEncoded(const EncodedFrameReport& r, Timestamp now) {
  ++encoded_;
  // QualityScalerResource::OnEncodeCompleted: QP with the send time.
  if (cfg_.quality_scaling && r.qp >= 0) scaler_.OnEncodedFrame(r.qp, now);
  // EncodeUsageResource::OnEncodeCompleted, with the time the frame came out.
  if (cfg_.quality_scaling) overuse_.OnFrameSent(encodes_completed_++, now);
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
  if (cfg_.quality_scaling) {
    w.Counter("dropped_size", dropped_[static_cast<int>(DropReason::kSize)]);
    w.Counter("resolution_adapt_downs", adapt_downs_);
    w.Counter("resolution_adapt_ups", adapt_ups_);
    w.Gauge("resolution_adaptations", stream_adapter_.resolution_adaptations());
    w.Gauge("encoded_pixels", configured_width_ * configured_height_);
    w.Counter("cpu_adapt_downs", cpu_adapt_downs_);
    w.Counter("cpu_adapt_ups", cpu_adapt_ups_);
    w.Counter("adapt_ups_not_most_limited", adapt_ups_not_most_limited_);
  }
  w.Gauge("paused", paused_ ? 1 : 0);
}

}  // namespace sm::rtp
