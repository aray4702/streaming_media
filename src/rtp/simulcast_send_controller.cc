#include "sm/rtp/simulcast_send_controller.h"

#include <algorithm>

namespace sm::rtp {

namespace {
// video_send_stream_impl.cc kVideoHysteresis / kScreenshareHysteresis (CalculateMaxPadBitrateBps).
constexpr double kVideoHysteresis = 1.2;
constexpr double kScreenshareHysteresis = 1.35;
// video_stream_encoder.cc kPendingFrameTimeoutMs.
constexpr TimeDelta kPendingFrameTimeout = TimeDelta::Millis(1000);
// FrameCadenceAdapterInterface::kFrameRateAveragingWindowSizeMs.
constexpr int64_t kFrameRateWindowMs = (1000 / 30) * 90;
}  // namespace

SimulcastAllocationLimits SimulcastAllocationLimitsFor(const std::vector<video::VideoStream>& streams,
                                                       DataRate configured_max, bool alr_probing, bool screen) {
  SimulcastAllocationLimits l;
  if (streams.empty()) return l;
  // encoder_min_bitrate_: the first stream's minimum, at least GetDefaultMinVideoBitrate (30 kbps but AV1).
  const DataRate first_min =
      streams[0].min_bitrate_bps > 0 ? DataRate::BitsPerSec(streams[0].min_bitrate_bps) : DataRate::Zero();
  l.min = std::max(first_min, DataRate::BitsPerSec(video::kDefaultMinVideoBitrateBps));
  // encoder_max_bitrate_ (CalculateMaxBitrate): the active streams' maximums, or the configured one if none
  // has a maximum; at least the minimum.
  bool has_active = false;
  std::optional<DataRate> sum;
  for (const auto& s : streams) {
    if (!s.active) continue;
    has_active = true;
    if (s.max_bitrate_bps > 0) sum = sum.value_or(DataRate::Zero()) + DataRate::BitsPerSec(s.max_bitrate_bps);
  }
  if (has_active && !sum && !configured_max.IsZero()) sum = configured_max;
  l.max = has_active && sum ? std::max(l.min, *sum) : DataRate::Zero();
  // CalculateMaxPadBitrateBps for simulcast (is_svc false, min_transmit_bitrate 0, pad_to_min_bitrate false).
  std::vector<video::VideoStream> active;
  for (const auto& s : streams) {
    if (s.active) active.push_back(s);
  }
  int64_t pad_bps = 0;
  if (active.size() > 1) {
    if (alr_probing) {
      // With ALR probing, pad to the lowest stream's minimum; probing does the rest of the ramp-up.
      pad_bps = active[0].min_bitrate_bps;
    } else {
      // Without, pad up to the start rate of the highest active stream.
      const double hysteresis = screen ? kScreenshareHysteresis : kVideoHysteresis;
      const size_t top = active.size() - 1;
      pad_bps = std::min(static_cast<int64_t>(hysteresis * active[top].min_bitrate_bps + 0.5),
                         static_cast<int64_t>(active[top].target_bitrate_bps));
      for (size_t i = 0; i < top; ++i) pad_bps += active[i].target_bitrate_bps;
    }
  }
  l.pad_up = DataRate::BitsPerSec(std::max<int64_t>(pad_bps, 0));
  return l;
}

SimulcastSendController::SimulcastSendController(SimulcastSendControllerConfig cfg, Session& session,
                                                 std::vector<VideoSendId> layers)
    : cfg_(std::move(cfg)), session_(session) {
  for (VideoSendId id : layers) {
    Layer l;
    l.id = id;
    layers_.push_back(l);
  }
}

// RateStatistics::Rate then ::Update over captured frames, as VideoSendController::UpdateFrameRate.
void SimulcastSendController::UpdateFrameRate(Timestamp now) {
  const int64_t t = now.ms();
  const int64_t oldest = t - kFrameRateWindowMs + 1;
  while (!frame_times_ms_.empty() && frame_times_ms_.front() < oldest) frame_times_ms_.pop_front();
  int64_t active = 0;
  if (first_frame_ms_ != -1) active = first_frame_ms_ <= t - kFrameRateWindowMs ? kFrameRateWindowMs : t - first_frame_ms_ + 1;
  const int64_t n = static_cast<int64_t>(frame_times_ms_.size());
  if (n == 0 || active <= 1 || (n <= 1 && active < kFrameRateWindowMs)) {
    input_fps_ = 0;
  } else {
    input_fps_ = static_cast<int>(static_cast<float>(n) * (1000.0f / static_cast<float>(active)) + 0.5f);
  }
  const bool last_sample_is_recent = !frame_times_ms_.empty() && frame_times_ms_.back() > t - kFrameRateWindowMs * 3 / 2;
  if (first_frame_ms_ == -1 || (frame_times_ms_.empty() && !last_sample_is_recent)) first_frame_ms_ = t;
  frame_times_ms_.push_back(t);
}

void SimulcastSendController::Reconfigure(int width, int height, Timestamp now) {
  // VideoStreamEncoder::ReconfigureEncoder: the streams for the new size, the codec, a new rate allocator and
  // bitrate adjuster, the frame dropper reset, and every layer's next frame a keyframe.
  const bool first = configured_width_ == 0;
  configured_width_ = width;
  configured_height_ = height;
  ++reconfigurations_;
  streams_ = video::CreateEncoderStreams(cfg_.trials, width, height, cfg_.encoder);
  codec_ = video::SetupCodec(cfg_.encoder, streams_, session_.rates().video_target);
  allocator_ = std::make_unique<video::SimulcastRateAllocator>(codec_, cfg_.trials.base_heavy_tl3_rate_allocation);
  std::vector<DataRate> mins(layers_.size(), DataRate::Zero());
  for (size_t i = 0; i < layers_.size() && i < codec_.number_of_simulcast_streams; ++i) {
    // EncoderBitrateAdjuster's min_bitrates_bps_: max(codec minBitrate, the stream's), for an active stream.
    const auto& ss = codec_.simulcast_stream[i];
    if (ss.active) mins[i] = DataRate::KilobitsPerSec(std::max(codec_.min_bitrate_kbps, ss.min_bitrate_kbps));
  }
  adjuster_ = std::make_unique<cc::SimulcastEncoderBitrateAdjuster>(cfg_.adjuster, mins);
  dropper_.Reset();
  for (size_t i = 0; i < layers_.size(); ++i) {
    Layer& l = layers_[i];
    l.next_key = true;
    // SimulcastEncoderAdapter::InitEncode: a stream the codec has no room for (the layer count is limited by
    // the resolution) or that is inactive is not sent.
    if (i >= streams_.size() || !streams_[i].active) l.paused = true;
  }
  // VideoSendStreamImpl::OnEncoderConfigurationChanged: the new limits, and the FEC controller's encoding
  // data (RtpVideoSender::SetEncodingData with the first stream's size).
  const SimulcastAllocationLimits lim = SimulcastAllocationLimitsFor(
      streams_, DataRate::BitsPerSec(std::max(cfg_.encoder.max_bitrate_bps, 0)), false,
      cfg_.encoder.content_type == video::VideoEncoderConfig::ContentType::kScreen);
  session_.send_side().SetVideoAllocationLimits(lim.min, lim.max, lim.pad_up);
  if (!streams_.empty()) {
    for (const Layer& l : layers_) session_.SetVideoEncodingData(l.id, streams_[0].width, streams_[0].height);
  }
  session_.send_side().OnVideoEncoderReconfigured(now);
  SM_EVENT(cfg_.events, now, "simulcast_send", first ? "configured" : "reconfigured", static_cast<double>(streams_.size()),
           lim.max.kbps());
}

void SimulcastSendController::UpdateRates(const SendRates& rates, double fps, Timestamp now) {
  if (!allocator_) return;
  // VideoStreamEncoder::UpdateBitrateAllocation: the allocator splits the encoder target over the layers, and
  // the bandwidth allocation is at least what it allocated.
  const DataRate target = rates.video_target;
  allocation_ = allocator_->Allocate(target, fps);
  std::vector<DataRate> targets(layers_.size(), DataRate::Zero());
  DataRate sum = DataRate::Zero();
  for (size_t i = 0; i < layers_.size(); ++i) {
    targets[i] = DataRate::BitsPerSec(allocation_.GetSpatialLayerSum(i));
    sum += targets[i];
  }
  const DataRate bandwidth = std::max({target, sum, rates.video_link_allocation});
  // EncoderBitrateAdjuster::AdjustRateAllocation, then SimulcastEncoderAdapter::SetRates per layer.
  const std::vector<DataRate> rates_out =
      cfg_.bitrate_adjuster ? adjuster_->AdjustRates(targets, bandwidth, fps, now) : targets;
  for (size_t i = 0; i < layers_.size(); ++i) {
    Layer& l = layers_[i];
    const bool exists = i < streams_.size();
    const int64_t kbps = exists ? rates_out[i].bps() / 1000 : 0;
    // A stream not sent before needs a key frame.
    if (kbps > 0 && l.paused) {
      l.keyframe_needed = true;
      ++l.enables;
      ++layer_switches_;
      SM_EVENT(cfg_.events, now, "simulcast_send", "layer_on", static_cast<double>(i), rates_out[i].kbps());
    } else if (kbps == 0 && !l.paused) {
      ++l.disables;
      ++layer_switches_;
      SM_EVENT(cfg_.events, now, "simulcast_send", "layer_off", static_cast<double>(i), target.kbps());
    }
    l.paused = kbps == 0;
    l.rate = exists ? rates_out[i] : DataRate::Zero();
  }
}

void SimulcastSendController::MaybeUpdateRates(const SendRates& rates, double fps, bool encode_path, bool reconfigured,
                                               Timestamp now) {
  // As VideoSendController::AdjustedRate: on each allocation update, and for a frame past the
  // congestion-window drop on a reconfiguration and every kParameterUpdateIntervalMs.
  if (!cfg_.adjuster_on_update) {
    UpdateRates(rates, fps, now);
    return;
  }
  const uint64_t updates = rates.video_allocation_updates;
  if (!rates_set_ || updates == 0 || updates != allocation_updates_seen_) {
    UpdateRates(rates, fps, now);
    allocation_updates_seen_ = updates;
    rates_set_ = true;
  }
  if (!encode_path) return;
  if (reconfigured) {
    UpdateRates(rates, fps, now);
    last_parameters_update_ = now;
  } else if (!last_parameters_update_.IsFinite()) {
    last_parameters_update_ = now;
  } else if (now - last_parameters_update_ >= kParameterUpdateInterval) {
    UpdateRates(rates, fps, now);
    last_parameters_update_ = now;
  }
}

SimulcastEncodeDecision SimulcastSendController::Encode(int width, int height, double fps, Timestamp capture,
                                                        Timestamp now) {
  (void)width;
  (void)height;
  SimulcastEncodeDecision d;
  d.fps = fps;
  d.capture_time = capture;
  d.target = session_.rates().video_target;
  d.layers.resize(layers_.size());
  // SimulcastEncoderAdapter::Encode: a layer that needs a keyframe (newly active) makes every layer's frame a
  // keyframe (legacy behaviour); otherwise the frame types VideoStreamEncoder asked for.
  bool any_keyframe_needed = false;
  for (const Layer& l : layers_) any_keyframe_needed = any_keyframe_needed || (!l.paused && l.keyframe_needed);
  for (size_t i = 0; i < layers_.size(); ++i) {
    Layer& l = layers_[i];
    SimulcastLayerDecision& ld = d.layers[i];
    if (i < streams_.size()) {
      ld.width = streams_[i].width;
      ld.height = streams_[i].height;
    }
    ld.target = l.rate;
    if (l.paused) continue;  // Not encoded in a resolution that is not sent.
    ld.encode = true;
    ld.keyframe = any_keyframe_needed || l.next_key;
    if (ld.keyframe) l.keyframe_needed = false;  // StreamContext::OnKeyframe.
    d.encode = true;
  }
  // The encoder took the frame: every next frame type is delta again.
  for (Layer& l : layers_) l.next_key = false;
  ++encoded_frames_;
  (void)now;
  return d;
}

SimulcastEncodeDecision SimulcastSendController::OnCapturedFrame(const CapturedFrame& frame, Timestamp now) {
  UpdateFrameRate(now);
  // VideoStreamEncoder::OnFrame -> SendStatisticsProxy::OnIncomingFrame.
  session_.OnIncomingVideoFrame(now);
  // OnFrame's congestion-window drop comes before MaybeEncodeVideoFrame, which observes the size and
  // reconfigures the encoder.
  const bool cwnd_drop = session_.send_side().DropCapturedFrame();
  bool reconfigured = false;
  if (!cwnd_drop && frame.width > 0 && frame.height > 0 &&
      (frame.width != configured_width_ || frame.height != configured_height_)) {
    Reconfigure(frame.width, frame.height, now);
    reconfigured = true;
  }
  session_.Start(now);
  const double fps = input_fps();
  const SendRates rates = session_.rates();
  dropper_.SetRates(rates.video_target, fps);
  MaybeUpdateRates(rates, fps, !cwnd_drop, reconfigured, now);
  if (cfg_.keyframe_interval.us() > 0 && last_keyframe_.IsFinite() && now - last_keyframe_ >= cfg_.keyframe_interval) {
    for (Layer& l : layers_) l.next_key = true;
  }
  SimulcastEncodeDecision d;
  d.target = rates.video_target;
  d.fps = fps;
  d.capture_time = now;
  d.layers.resize(layers_.size());
  const bool paused = paused_ || rates.encoder_paused;
  encoder_paused_seen_ = paused;
  if (cwnd_drop) {
    d.reason = DropReason::kCongestionWindow;
  } else if (paused) {
    d.reason = DropReason::kPaused;
  }
  if (d.reason != DropReason::kCongestionWindow) {
    // MaybeEncodeVideoFrame's pending_frame_: a frame held from before is dropped now; a frame dropped for
    // the paused encoder is held in its place.
    if (pending_frame_) {
      pending_frame_.reset();
      ++pending_dropped_;
    }
    if (d.reason == DropReason::kPaused) {
      pending_frame_ = PendingFrame{frame.width, frame.height, now};
      d.hold = true;
    }
  }
  if (d.reason == DropReason::kNone && dropper_.ShouldDrop(fps)) d.reason = DropReason::kFrameDropper;
  if (d.reason == DropReason::kNone) return Encode(frame.width, frame.height, fps, now, now);
  if (d.reason == DropReason::kFrameDropper && cfg_.bitrate_adjuster && adjuster_) adjuster_->OnFrameDropped(now);
  ++dropped_[static_cast<int>(d.reason)];
  SM_EVENT(cfg_.events, now, "simulcast_send", "frame_dropped", static_cast<double>(d.reason), 0);
  return d;
}

std::optional<SimulcastEncodeDecision> SimulcastSendController::MaybeEncodePendingFrame(Timestamp now) {
  const SendRates rates = session_.rates();
  // OnBitrateUpdated: the allocation arrives (UpdateBitrateAllocation and SetEncoderRates at its time).
  if (allocator_ && rates.video_allocation_updates != 0 && rates.video_allocation_updates != allocation_updates_seen_ &&
      cfg_.adjuster_on_update) {
    UpdateRates(rates, input_fps(), now);
    allocation_updates_seen_ = rates.video_allocation_updates;
    rates_set_ = true;
  }
  const bool paused = paused_ || rates.encoder_paused;
  const bool resumed = encoder_paused_seen_ && !paused;
  encoder_paused_seen_ = paused;
  if (!resumed || !pending_frame_) return std::nullopt;
  const PendingFrame p = *pending_frame_;
  pending_frame_.reset();
  if (now - p.posted >= kPendingFrameTimeout) {
    ++pending_dropped_;
    return std::nullopt;
  }
  const double fps = input_fps();
  dropper_.SetRates(rates.video_target, fps);
  ++pending_encoded_;
  return Encode(p.width, p.height, fps, p.posted, now);
}

void SimulcastSendController::OnEncoded(size_t layer, const EncodedFrameReport& r, Timestamp now) {
  // RunPostEncode for each encoded image: the frame dropper and the bitrate adjuster's layer.
  dropper_.OnEncodedFrame(r.size, r.keyframe);
  if (cfg_.bitrate_adjuster && adjuster_) adjuster_->OnEncodedFrame(layer, r.size, now);
  if (layer < layers_.size()) {
    ++layers_[layer].encoded;
    if (r.keyframe) ++layers_[layer].keyframes;
  }
  if (r.keyframe) last_keyframe_ = now;
}

void SimulcastSendController::RequestKeyframe() {
  for (Layer& l : layers_) l.next_key = true;
}

void SimulcastSendController::OnSessionEvent(const SessionEvent& e) {
  switch (e.type) {
    case SessionEventType::kKeyframeRequested: {
      if (e.ssrc == 0) {  // The outage monitor's own request: every layer, no minimum interval.
        RequestKeyframe();
        break;
      }
      // EncoderRtcpFeedback::OnReceivedIntraFrameRequest: the SSRC's layer (index 0 without per-layer
      // keyframes) and its minimum interval.
      size_t index = layers_.size();
      for (size_t i = 0; i < layers_.size(); ++i) {
        if (session_.video_sender(layers_[i].id).config().ssrc == e.ssrc) index = i;
      }
      if (index == layers_.size()) break;  // Not one of this stream's SSRCs.
      Timestamp& last = cfg_.per_layer_keyframes ? layers_[index].last_keyframe_request : last_keyframe_request_;
      if (cfg_.min_keyframe_request_interval > TimeDelta::Zero() && last + cfg_.min_keyframe_request_interval > e.time) {
        ++keyframe_requests_ignored_;
        SM_EVENT(cfg_.events, e.time, "simulcast_send", "keyframe_request_ignored", static_cast<double>(index), 0);
        break;
      }
      last = e.time;
      if (cfg_.per_layer_keyframes) {
        layers_[index].next_key = true;
      } else {
        RequestKeyframe();
      }
      break;
    }
    case SessionEventType::kEncoderPause: paused_ = true; break;
    case SessionEventType::kEncoderResume: paused_ = false; break;
    default: break;
  }
}

size_t SimulcastSendController::active_layers() const {
  size_t n = 0;
  for (const Layer& l : layers_) n += l.paused ? 0 : 1;
  return n;
}

void SimulcastSendController::ExportMetrics(MetricsWriter& w) const {
  w.Counter("encoded", encoded_frames_);
  w.Counter("dropped_paused", dropped_[static_cast<int>(DropReason::kPaused)]);
  w.Counter("dropped_cwnd", dropped_[static_cast<int>(DropReason::kCongestionWindow)]);
  w.Counter("dropped_dropper", dropped_[static_cast<int>(DropReason::kFrameDropper)]);
  w.Counter("keyframe_requests_ignored", keyframe_requests_ignored_);
  w.Counter("layer_switches", layer_switches_);
  w.Counter("reconfigurations", reconfigurations_);
  w.Counter("pending_dropped", pending_dropped_);
  w.Counter("pending_encoded", pending_encoded_);
  w.Gauge("streams", static_cast<double>(streams_.size()));
  w.Gauge("active_layers", static_cast<double>(active_layers()));
  for (size_t i = 0; i < layers_.size(); ++i) {
    const std::string p = "layer" + std::to_string(i) + "_";
    w.Counter(p + "encoded", layers_[i].encoded);
    w.Counter(p + "keyframes", layers_[i].keyframes);
    w.Counter(p + "enables", layers_[i].enables);
    w.Counter(p + "disables", layers_[i].disables);
    w.Gauge(p + "rate_kbps", layers_[i].rate.kbps());
    w.Gauge(p + "allocation_kbps", static_cast<double>(allocation_.GetSpatialLayerSum(i)) / 1000.0);
  }
}

}  // namespace sm::rtp
