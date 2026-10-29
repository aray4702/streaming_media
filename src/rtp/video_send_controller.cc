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

// video_stream_encoder.cc's kPendingFrameTimeoutMs.
constexpr TimeDelta kPendingFrameTimeout = TimeDelta::Millis(1000);
// QualityScalerSettings' kMinScaleFactor.
constexpr double kMinScaleFactor = 0.01;
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
  // ConfigureQualityScaler and ConfigureEncodeUsageResource with the encoder's
  // default info (trusted QP thresholds, CPU overuse detection on).
  if (cfg.quality_scaling) {
    processor_.AddResource(kQualityResource);
    processor_.AddResource(kCpuResource);
  }
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
  using State = video::ResourceUsageState;
  if (signal == Signal::kAdaptDown) OnResourceUsageStateMeasured(kQualityResource, State::kOveruse, now);
  if (signal == Signal::kAdaptUp) OnResourceUsageStateMeasured(kQualityResource, State::kUnderuse, now);
}

void VideoSendController::OnCpuSignal(cc::CpuOveruseDetector::Signal signal, Timestamp now) {
  // EncodeUsageResource::AdaptDown / AdaptUp.
  using Signal = cc::CpuOveruseDetector::Signal;
  using State = video::ResourceUsageState;
  if (signal == Signal::kOveruse) OnResourceUsageStateMeasured(kCpuResource, State::kOveruse, now);
  if (signal == Signal::kUnderuse) OnResourceUsageStateMeasured(kCpuResource, State::kUnderuse, now);
}

void VideoSendController::OnResourceUsageStateMeasured(Resource r, video::ResourceUsageState state, Timestamp now) {
  using Result = video::ResourceAdaptationProcessor::MitigationResult;
  const video::VideoAdaptationCounters before = stream_adapter_.counters();
  const Result result = processor_.OnResourceUsageStateMeasured(r, state);
  if (result == Result::kNotMostLimitedResource) ++adapt_ups_not_most_limited_;
  if (result == Result::kAdaptationApplied) OnRestrictionsChanged(before, r, now);
}

void VideoSendController::OnRestrictionsChanged(const video::VideoAdaptationCounters& before, int reason,
                                                Timestamp now) {
  const bool down = stream_adapter_.counters().Total() > before.Total();
  ++(down ? adapt_downs_ : adapt_ups_);
  if (reason == kCpuResource) ++(down ? cpu_adapt_downs_ : cpu_adapt_ups_);
  video_adapter_.OnSinkWants(stream_adapter_.restrictions());
  SM_EVENT(cfg_.events, now, "video_send", down ? "adapt_down" : "adapt_up", static_cast<double>(reason),
           stream_adapter_.resolution_adaptations());
}

void VideoSendController::RemoveResource(Resource r, Timestamp now) {
  const video::VideoSourceRestrictions before = stream_adapter_.restrictions();
  processor_.RemoveResource(r);
  ++resources_removed_;
  SM_EVENT(cfg_.events, now, "video_send", "resource_removed", static_cast<double>(r),
           stream_adapter_.resolution_adaptations());
  // RemoveLimitationsImposedByResource cleared or restored the restrictions.
  if (!(stream_adapter_.restrictions() == before)) {
    ++restrictions_restored_;
    video_adapter_.OnSinkWants(stream_adapter_.restrictions());
  }
}

void VideoSendController::OnEncoderInfoChanged(const EncoderAdaptationInfo& info, Timestamp now) {
  if (!cfg_.quality_scaling) return;
  // ConfigureEncodeUsageResource: without CPU overuse detection the check stops
  // and the resource is removed; with it, a started check restarts and a
  // stopped one is added and started (StartCheckForOveruse with the target frame rate).
  if (!info.cpu_overuse_detection) {
    if (processor_.HasResource(kCpuResource)) RemoveResource(kCpuResource, now);
  } else {
    processor_.AddResource(kCpuResource);
    overuse_.RestartCheckForOveruse();
    overuse_.OnTargetFramerateUpdated(static_cast<int>(cfg_.fps));
    overuse_.Check(now);  // DelayedStart: the first run 100 ms from now.
  }
  // ConfigureQualityScaler: allowed and not started, a new QualityScaler (its
  // check task starts now) and the resource added; not allowed, the scaler is
  // destroyed and the resource removed. UpdateQualityScalerSettings then tells
  // the initial frame dropper (OnQualityScalerSettingsUpdated): restart the
  // drops for size if the scaler runs, else disable them.
  if (info.quality_scaling) {
    if (processor_.HasResource(kQualityResource)) return;
    processor_.AddResource(kQualityResource);
    scaler_ = video::QualityScaler(ScalerConfig(cfg_));
    scaler_.Check(now);
    initial_framedrop_ = 0;
  } else {
    if (processor_.HasResource(kQualityResource)) RemoveResource(kQualityResource, now);
    initial_framedrop_ = kMaxInitialFramedrop;
  }
}

EncodeDecision VideoSendController::OnCapturedFrame(const CapturedFrame& frame, Timestamp now) {
  UpdateFrameRate(now);
  int width = frame.width, height = frame.height;
  bool cwnd_drop = false;
  bool reconfigured = false;
  bool new_adjuster = false;
  if (cfg_.quality_scaling) {
    // The quality scaler's CheckQpTask, then the source's VideoAdapter with the
    // sink wants, then the frame size the encoder sees (OnFrameSizeObserved).
    // A task due at a frame's capture time runs on the encoder queue after that
    // frame was encoded and its QP reported, and the restriction it sets
    // reaches the source through a worker-queue task
    // (VideoStreamEncoder::OnVideoSourceRestrictionsUpdated), so it applies
    // from the next frame. Each task due before this frame runs here, at its
    // due time (issue #348).
    if (processor_.HasResource(kQualityResource)) {
      if (!scaler_.next_check().IsFinite()) scaler_.Check(now);
      while (scaler_.next_check() < now) {
        const Timestamp due = scaler_.next_check();
        OnQualityScalerSignal(scaler_.Check(due), due);
      }
    }
    // OveruseFrameDetector's CheckForOveruse task (stopped with the resource;
    // EncodeUsageResource still passes it every encoded frame).
    if (processor_.HasResource(kCpuResource)) OnCpuSignal(overuse_.Check(now), now);
    if (frame.width > 0 && frame.height > 0) video_adapter_.AdaptFrameResolution(frame.width, frame.height, &width, &height);
    // VideoStreamEncoder::OnFrame drops for the congestion window before
    // MaybeEncodeVideoFrame (video_stream_encoder.cc l.1635-1660), which
    // observes the frame size and reconfigures the encoder for a new one. A
    // dropped frame does neither, and the reallocation a reconfiguration starts
    // (OnEncoderConfigurationChanged -> BitrateAllocator::AddObserver, which
    // clears the reduce ratio) is posted, so it reaches the drop decision from
    // the next frame on (issue #411, D8).
    cwnd_drop = session_.send_side().DropCapturedFrame();
  }
  if (cfg_.quality_scaling && !cwnd_drop) {
    stream_adapter_.OnFrameSizeObserved(width * height);
    // ReconfigureEncoder for a new frame size: the encoder is reset and its
    // next frame is a keyframe (next_frame_types_). The frame dropper is
    // Reset() and its rates set again (SetRates(codec.startBitrate) below,
    // the encoder target), and bitrate_adjuster_ is a new
    // EncoderBitrateAdjuster, which uses kDefaultUtilizationFactor until
    // kMinFramesSinceLayoutChange frames have been encoded
    // (video/video_stream_encoder.cc l.1430-1442). Reset() is the same as a
    // new adjuster: its overshoot detector is created again on the next
    // AdjustRate, which restarts the frame count. It runs below, after the
    // rate update this frame may bring.
    if (width != configured_width_ || height != configured_height_) {
      const bool first_configuration = configured_width_ == 0;
      if (!first_configuration) keyframe_needed_ = true;
      dropper_.Reset();
      new_adjuster = true;
      configured_width_ = width;
      configured_height_ = height;
      // VideoSendStreamImpl::OnEncoderConfigurationChanged: the running stream
      // registers with the allocator again, also after an encoder time-out.
      // ReconfigureEncoder posts it to the worker queue and encodes this frame
      // with the last rate settings (SetEncoderRates(last_encoder_rate_settings_)),
      // so the reallocation, and the reduce ratio it clears, apply from the next
      // frame (issue #411, D8). The first frame's configuration comes before
      // VideoSendStream::Start below, as before.
      if (first_configuration) {
        session_.send_side().OnVideoEncoderReconfigured(now);
      } else {
        reconfigured = true;
      }
    }
  }
  // VideoSendStream::Start registers the stream with the BitrateAllocator, and
  // its OnBitrateUpdated reaches VideoStreamEncoder, before the camera delivers
  // a frame. The encoder's first SetRates (ReconfigureEncoder,
  // video_stream_encoder.cc:1446-1455) then carries that allocation, so the
  // first keyframe is sized from it rather than from the target less the
  // audio's nominal rate (issue #331).
  session_.Start(now);
  const double fps = input_fps();
  const SendRates& rates = session_.rates();
  const DataRate target = rates.video_target;
  // VideoSendStreamImpl's constructor gives the encoder the allocator's start bitrate (SetStartBitrate),
  // before the first allocation.
  if (!start_bitrate_set_) {
    start_bitrate_set_ = true;
    // VideoStreamEncoderResourceManager::SetStartBitrate passes the time in microseconds
    // (clock_->TimeInMicroseconds()), and InitialFrameDropper::SetTargetBitrate subtracts it from the
    // time in milliseconds, as here.
    start_bitrate_ = target;
    start_bitrate_time_ = now.us();
  }
  if (rates.video_allocation_updates == 0 || rates.video_allocation_updates != target_updates_seen_) {
    target_updates_seen_ = rates.video_allocation_updates;
    OnTargetBitrate(target, now);
  }
  dropper_.SetRates(target, fps);
  const DataRate encoder_rate = EncoderRate(rates, fps, !cwnd_drop, new_adjuster, now);
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
  d.capture_time = now;
  // The outage pause, then congestion-window pushback (keyframes included),
  // then the frame dropper (keyframes included). With quality scaling the
  // order is VideoStreamEncoder's: the congestion-window drop (OnFrame), then
  // in MaybeEncodeVideoFrame the initial frame drop (DropDueToSize and
  // OnMaybeEncodeFrame), the pause (EncoderPaused) and the frame dropper.
  const bool paused = paused_ || rates.encoder_paused;
  encoder_paused_seen_ = paused;
  if (paused && !cfg_.quality_scaling) {
    d.reason = DropReason::kPaused;
  } else if (cwnd_drop || (!cfg_.quality_scaling && session_.send_side().DropCapturedFrame())) {
    d.reason = DropReason::kCongestionWindow;
  } else if (cfg_.quality_scaling && QualityScalingDropsForSize(width, height, target, now)) {
    d.reason = DropReason::kSize;
  } else if (paused) {
    d.reason = DropReason::kPaused;
  } else {
    d.reason = DropReason::kNone;
  }
  // MaybeEncodeVideoFrame's pending_frame_: a frame held from before is dropped now (ProcessDroppedFrame,
  // kEncoderQueue), and a frame dropped for its size or the pause is held in its place. OnFrame's
  // congestion-window drop returns before MaybeEncodeVideoFrame and leaves it.
  if (d.reason != DropReason::kCongestionWindow) {
    if (pending_frame_) {
      pending_frame_.reset();
      ++pending_dropped_;
    }
    if (d.reason == DropReason::kSize || d.reason == DropReason::kPaused) {
      pending_frame_ = PendingFrame{width, height, now};
      d.hold = true;
    }
  }
  if (d.reason == DropReason::kNone && dropper_.ShouldDrop(fps)) d.reason = DropReason::kFrameDropper;
  if (d.reason == DropReason::kNone) {
    d.encode = true;
    // EncodeVideoFrame: OnEncodeStarted with the encoded frame and the time
    // the frame was first seen.
    if (cfg_.quality_scaling) overuse_.OnFrameCaptured(width * height, encodes_started_++, now);
    if (reconfigured) session_.send_side().OnVideoEncoderReconfigured(now);
    return d;
  }
  d.keyframe = false;
  if (d.reason == DropReason::kFrameDropper && cfg_.bitrate_adjuster) adjuster_.OnFrameDropped(now);
  // QualityScalerResource::OnFrameDropped counts kMediaOptimization (the frame
  // dropper) and kEncoder drops only.
  if (d.reason == DropReason::kFrameDropper && cfg_.quality_scaling && processor_.HasResource(kQualityResource)) {
    scaler_.OnFrameDropped();
  }
  ++dropped_[static_cast<int>(d.reason)];
  SM_EVENT(cfg_.events, now, "video_send", "frame_dropped", static_cast<double>(d.reason), 0);
  if (reconfigured) session_.send_side().OnVideoEncoderReconfigured(now);
  return d;
}

DataRate VideoSendController::EncoderRate(const SendRates& rates, double fps, bool encode_path, bool new_adjuster,
                                          Timestamp now) {
  // VideoSendStreamImpl::OnBitrateUpdated: link_allocation is the encoder
  // target less the protection before the target is capped at the encoder
  // maximum (SendRates::video_link_allocation), and at least the target, so
  // the adjuster has headroom when the allocation is above the maximum.
  // Congestion-window pushback lowers only the target
  // (VideoStreamEncoder::UpdateTargetBitrate).
  // VideoStreamEncoder::UpdateBitrateAllocation: the allocator may raise the
  // allocation to the codec minimum, and the bandwidth allocation is at least
  // that (the frame dropper is given the unraised target).
  const DataRate target = rates.video_target;
  DataRate allocation = target;
  if (cfg_.allocation_min_bitrate.bps() > 0 && target.bps() > 0) {
    allocation = std::max(cfg_.allocation_min_bitrate, std::min(target, cfg_.allocation_max_bitrate));
  }
  const DataRate link = std::max({target, allocation, rates.video_link_allocation});
  return cfg_.bitrate_adjuster ? AdjustedRate(allocation, link, fps, encode_path, new_adjuster, now) : allocation;
}

void VideoSendController::OnTargetBitrate(DataRate target, Timestamp now) {
  // InitialFrameDropper::SetTargetBitrate: once, within the interval after the start bitrate was set, a
  // target below `factor` times it restarts the initial frame drop (the estimate fell far below the start
  // rate, so the resolution may be too large again).
  // quality_scaler_resource_->is_started().
  if (start_bitrate_.bps() <= 0 || has_seen_first_bwe_drop_ || !processor_.HasResource(kQualityResource)) return;
  const int64_t interval_ms = cfg_.initial_bitrate_interval.value_or(TimeDelta::Millis(-1)).ms();
  const double factor = cfg_.initial_bitrate_factor.value_or(0);
  if (interval_ms < 0 || factor < kMinScaleFactor) return;  // Unset or ignored.
  const int64_t diff_ms = now.ms() - start_bitrate_time_;
  if (diff_ms < interval_ms && target < start_bitrate_ * factor) {
    initial_framedrop_ = 0;
    has_seen_first_bwe_drop_ = true;
    ++initial_framedrop_resets_;
    SM_EVENT(cfg_.events, now, "video_send", "initial_frame_drop_reset", static_cast<double>(start_bitrate_.bps()),
             static_cast<double>(target.bps()));
  }
}

std::optional<EncodeDecision> VideoSendController::MaybeEncodePendingFrame(Timestamp now) {
  // VideoStreamEncoder::OnBitrateUpdated: video_suspension_changed to not suspended, with a stored frame
  // that DropDueToSize lets through, encodes it (EncodeVideoFrame, past the frame dropper) if it was posted
  // less than kPendingFrameTimeoutMs ago; the stored frame is released either way.
  // Each allocation update is an OnBitrateUpdated: SetTargetBitrate runs first.
  const SendRates& rates = session_.rates();
  const DataRate target = rates.video_target;
  if (start_bitrate_set_ && rates.video_allocation_updates != 0 && rates.video_allocation_updates != target_updates_seen_) {
    target_updates_seen_ = rates.video_allocation_updates;
    OnTargetBitrate(target, now);
  }
  const bool paused = paused_ || rates.encoder_paused;
  const bool resumed = encoder_paused_seen_ && !paused;
  encoder_paused_seen_ = paused;
  if (!resumed || !pending_frame_) return std::nullopt;
  const PendingFrame p = *pending_frame_;
  if (cfg_.quality_scaling && initial_framedrop_ < kMaxInitialFramedrop && target.bps() > 0 &&
      DropDueToSize(p.width * p.height, target.bps())) {
    return std::nullopt;  // Still held: the next captured frame drops it.
  }
  pending_frame_.reset();
  if (now - p.posted >= kPendingFrameTimeout) {
    ++pending_dropped_;
    return std::nullopt;
  }
  const double fps = input_fps();
  dropper_.SetRates(target, fps);
  EncodeDecision d;
  d.encode = true;
  d.target = EncoderRate(rates, fps, false, false, now);
  const bool periodic = cfg_.keyframe_interval.us() > 0 && last_keyframe_.IsFinite() &&
                        now - last_keyframe_ >= cfg_.keyframe_interval;
  d.keyframe = keyframe_needed_ || periodic;
  d.width = p.width;
  d.height = p.height;
  d.fps = fps;
  d.capture_time = p.posted;
  if (cfg_.quality_scaling) overuse_.OnFrameCaptured(p.width * p.height, encodes_started_++, now);
  ++pending_encoded_;
  SM_EVENT(cfg_.events, now, "video_send", "pending_frame_encoded", static_cast<double>((now - p.posted).us()) / 1000.0, 0);
  return d;
}

DataRate VideoSendController::AdjustedRate(DataRate allocation, DataRate link, double fps, bool encode_path,
                                           bool new_adjuster, Timestamp now) {
  // VideoStreamEncoder runs EncoderBitrateAdjuster::AdjustRateAllocation in
  // UpdateBitrateAllocation, and the encoder keeps the rate it was last set to
  // (last_encoder_rate_settings_) in between. It runs on each allocation the
  // allocator hands the stream (OnBitrateUpdated, l.2459-2507). Without an
  // update count the rates are recomputed per call, and so is the adjustment.
  if (!cfg_.adjuster_on_update) {
    if (new_adjuster) adjuster_.Reset(now);
    return adjuster_.AdjustRate(allocation, link, fps, now);
  }
  const uint64_t updates = session_.rates().video_allocation_updates;
  if (!adjusted_once_ || updates == 0 || updates != allocation_updates_seen_) {
    encoder_rate_ = adjuster_.AdjustRate(allocation, link, fps, now);
    allocation_updates_seen_ = updates;
    adjusted_once_ = true;
  }
  // MaybeEncodeVideoFrame (l.1921-1940), for a frame that got past OnFrame's
  // congestion-window drop: ReconfigureEncoder's new adjuster gets the last
  // rate settings with the input frame rate; otherwise they are applied again
  // with the input frame rate every kParameterUpdateIntervalMs (1000 ms).
  if (!encode_path) return encoder_rate_;
  if (new_adjuster) {
    adjuster_.Reset(now);
    encoder_rate_ = adjuster_.AdjustRate(allocation, link, fps, now);
    last_parameters_update_ = now;
  } else if (!last_parameters_update_.IsFinite()) {
    last_parameters_update_ = now;
  } else if (now - last_parameters_update_ >= kParameterUpdateInterval) {
    encoder_rate_ = adjuster_.AdjustRate(allocation, link, fps, now);
    last_parameters_update_ = now;
  }
  return encoder_rate_;
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
  if (a.status == video::VideoStreamAdapter::Status::kValid) {
    const video::VideoAdaptationCounters before = stream_adapter_.counters();
    processor_.ApplyAdaptation(a, kQualityResource);
    OnRestrictionsChanged(before, kQualityResource, now);
  }
  SM_EVENT(cfg_.events, now, "video_send", "initial_frame_drop", width * height, static_cast<double>(target.bps()));
  return true;
}

void VideoSendController::OnEncoded(const EncodedFrameReport& r, Timestamp now) {
  ++encoded_;
  // QualityScalerResource::OnEncodeCompleted: QP with the send time.
  if (cfg_.quality_scaling && processor_.HasResource(kQualityResource) && r.qp >= 0) scaler_.OnEncodedFrame(r.qp, now);
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
    w.Counter("resources_removed", resources_removed_);
    w.Counter("restrictions_restored", restrictions_restored_);
  }
  w.Counter("pending_dropped", pending_dropped_);
  w.Counter("pending_encoded", pending_encoded_);
  if (cfg_.quality_scaling) w.Counter("initial_framedrop_resets", initial_framedrop_resets_);
  w.Gauge("paused", paused_ ? 1 : 0);
}

}  // namespace sm::rtp
