// Send-side controller for a simulcast video track (C17, issue #1162): the
// library's equivalent of libwebrtc M157's VideoStreamEncoder driving a
// SimulcastEncoderAdapter, with VideoSendStreamImpl's allocation limits. Per
// captured frame it decides, for each simulcast layer, whether to encode it, as
// a keyframe or not, at what rate and resolution; the app runs one encoder per
// layer (the adapter's one encoder per stream) and hands each encoded frame to
// the session on that layer's send stream (Session::AddVideoSend per layer, each
// with its SSRC, RTX SSRC and RID).
//
// What it ports:
//  - the layers: EncoderStreamFactory::CreateEncoderStreams for the input size
//    (video::CreateEncoderStreams: the bitrate table by resolution, the layer
//    count limited by resolution, 1/2 resolution steps) and
//    VideoCodecInitializer::SetupCodec (video::SetupCodec), on the first frame
//    and whenever the input size changes (ReconfigureEncoder: every layer's next
//    frame is a keyframe, the frame dropper is reset, a new bitrate adjuster);
//  - the allocation limits VideoSendStreamImpl::OnEncoderConfigurationChanged
//    hands the BitrateAllocator (SimulcastAllocationLimitsFor): min the lowest
//    layer's minimum, max the sum of the active layers' maximums, and the pad-up
//    rate CalculateMaxPadBitrateBps (the lower active layers' targets plus
//    min(1.2 x the top layer's minimum, its target), as no ALR probing is set);
//  - VideoStreamEncoder::UpdateBitrateAllocation with SimulcastRateAllocator
//    (video::SimulcastRateAllocator: layers on lowest first while the target
//    covers their minimums with the 1.2 hysteresis, the rest to the top layer),
//    then EncoderBitrateAdjuster::AdjustRateAllocation per layer
//    (cc::SimulcastEncoderBitrateAdjuster), at the same times as
//    rtp::VideoSendController: an allocation update, a reconfiguration, and every
//    1 s (kParameterUpdateIntervalMs) for a frame past the congestion-window drop;
//  - SimulcastEncoderAdapter::SetRates: a layer whose allocation is below 1 kbps
//    is paused (not encoded); a paused layer that gets a rate needs a keyframe,
//    and SimulcastEncoderAdapter::Encode then makes every active layer's frame a
//    keyframe (its legacy behaviour; is_keyframe_needed);
//  - keyframe requests through EncoderRtcpFeedback: a PLI or FIR on any layer's
//    SSRC asks for a keyframe on every layer (VideoStreamEncoder::SendKeyFrame()),
//    at most one per 300 ms; with `per_layer_keyframes` (the codec's
//    x-google-per-layer-pli=1 or WebRTC-Video-PerSsrcKeyframes) only on the layer
//    named, with an interval per layer;
//  - VideoStreamEncoder's drops for the whole frame, in its order: the
//    congestion-window drop (OnFrame), the paused encoder (the frame is held as
//    pending_frame_ and may be encoded on resume, MaybeEncodePendingFrame), and
//    the frame dropper on the stream's total target, filled with every layer's
//    encoded size (RunPostEncode per encoded image).
// Quality scaling is off for simulcast, as libwebrtc's
// WebRtcVideoSendStream::CreateVideoEncoderConfig allows it only for one active
// layer (is_quality_scaling_allowed), and so is the initial frame drop.
// Not ported (C17 remaining work in docs/design/libwebrtc-parity.md 10.x): CPU
// adaptation (EncodeUsageResource), per-layer frame-rate limits
// (FramerateController for a layer whose max_framerate is below the input),
// temporal layers (the encoders here have one), scale_resolution_down_to.
// Takes `now`, reads no clock, starts no thread, has no callbacks.
#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

#include "sm/cc/encoder_bitrate_adjuster.h"
#include "sm/cc/frame_dropper.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"
#include "sm/rtp/session.h"
#include "sm/rtp/video_send_controller.h"
#include "sm/video/simulcast.h"
#include "sm/video/simulcast_rate_allocator.h"

namespace sm::rtp {

// VideoSendStreamImpl::GetAllocationConfig for a set of encoder streams (OnEncoderConfigurationChanged).
struct SimulcastAllocationLimits {
  DataRate min;     // encoder_min_bitrate_: the first stream's minimum, at least kDefaultMinVideoBitrateBps.
  DataRate max;     // encoder_max_bitrate_: the sum of the active streams' maximums.
  DataRate pad_up;  // max_padding_bitrate_ (CalculateMaxPadBitrateBps, without ALR probing).
};
// `configured_max`: the stream's configured maximum (VideoEncoderConfig::max_bitrate_bps), used when no active
// stream has one; `alr_probing`: has_alr_probing_ (off by default).
SimulcastAllocationLimits SimulcastAllocationLimitsFor(const std::vector<video::VideoStream>& streams,
                                                       DataRate configured_max = DataRate::Zero(),
                                                       bool alr_probing = false,
                                                       bool screen = false);

struct SimulcastSendControllerConfig {
  // The encodings (number_of_streams, per-layer overrides from RtpEncodingParameters), codec and content.
  video::VideoEncoderConfig encoder = [] {
    video::VideoEncoderConfig c;
    c.codec_type = video::VideoCodecType::kH264;
    c.number_of_streams = 3;
    c.simulcast_layers.resize(3);
    return c;
  }();
  video::SimulcastTrials trials;
  TimeDelta keyframe_interval = TimeDelta::Zero();  // Periodic keyframe on every layer; zero = off.
  double fps = 30;  // The configured maximum frame rate, until the input rate is measured.
  bool bitrate_adjuster = true;  // RateControlSettings bitrate_adjuster (true in M157).
  cc::EncoderBitrateAdjusterConfig adjuster;
  bool adjuster_on_update = true;  // As VideoSendControllerConfig::adjuster_on_update.
  // EncoderRtcpFeedback's kMinKeyframeSendIntervalMs.
  TimeDelta min_keyframe_request_interval = TimeDelta::Millis(300);
  // EncoderRtcpFeedback's per_layer_keyframes_: SupportsPerLayerPictureLossIndication (the codec's
  // x-google-per-layer-pli=1) or WebRTC-Video-PerSsrcKeyframes. Off by default, as Chrome's H.264.
  bool per_layer_keyframes = false;
  EventSink* events = nullptr;
};

struct SimulcastLayerDecision {
  bool encode = false;  // The layer is not paused and the frame is encoded.
  bool keyframe = false;
  DataRate target;      // The layer encoder's rate (after the bitrate adjuster).
  int width = 0, height = 0;
};

struct SimulcastEncodeDecision {
  bool encode = false;  // Any layer encodes this frame.
  DropReason reason = DropReason::kNone;  // Why the whole frame is dropped.
  std::vector<SimulcastLayerDecision> layers;  // One per configured layer (the session's send streams).
  DataRate target;  // The stream's encoder target before the allocator.
  double fps = 0;
  bool hold = false;  // As EncodeDecision::hold (a paused encoder holds the frame).
  Timestamp capture_time = Timestamp::MinusInfinity();
};

class SimulcastSendController : public MetricsSource {
 public:
  // `layers`: the session's send stream of each layer, lowest first (as many as encoder.number_of_streams).
  SimulcastSendController(SimulcastSendControllerConfig cfg, Session& session, std::vector<VideoSendId> layers);

  SimulcastEncodeDecision OnCapturedFrame(const CapturedFrame& frame, Timestamp now);
  // As VideoSendController::MaybeEncodePendingFrame: call after the session ran.
  std::optional<SimulcastEncodeDecision> MaybeEncodePendingFrame(Timestamp now);
  // After layer `layer`'s encoder produced the frame the decision allowed.
  void OnEncoded(size_t layer, const EncodedFrameReport& report, Timestamp now);
  // Forward every Session event: keyframe requests (by SSRC) and the encoder pause.
  void OnSessionEvent(const SessionEvent& e);
  // A keyframe on every layer (VideoStreamEncoder::SendKeyFrame()).
  void RequestKeyframe();

  // The configured encoder streams (empty before the first frame).
  const std::vector<video::VideoStream>& streams() const { return streams_; }
  const video::VideoCodec& codec() const { return codec_; }
  // The last allocation per layer, before the bitrate adjuster.
  const video::VideoBitrateAllocation& allocation() const { return allocation_; }
  bool layer_paused(size_t layer) const { return layer < layers_.size() ? layers_[layer].paused : true; }
  size_t active_layers() const;
  const cc::FrameDropper& frame_dropper() const { return dropper_; }
  bool paused() const { return paused_; }
  bool has_pending_frame() const { return pending_frame_.has_value(); }
  double input_fps() const { return input_fps_ > 0 ? input_fps_ : cfg_.fps; }
  uint64_t layer_switches() const { return layer_switches_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  struct Layer {
    VideoSendId id;
    bool paused = true;            // StreamContext::is_paused_.
    bool keyframe_needed = false;  // StreamContext::is_keyframe_needed_.
    bool next_key = true;          // VideoStreamEncoder::next_frame_types_[i].
    DataRate rate;                 // Last SetRates (adjusted).
    Timestamp last_keyframe_request = Timestamp::MinusInfinity();
    uint64_t encoded = 0, keyframes = 0, enables = 0, disables = 0;
  };
  void Reconfigure(int width, int height, Timestamp now);
  // UpdateBitrateAllocation and SetEncoderRates (the allocator, the adjuster, SimulcastEncoderAdapter::SetRates).
  void UpdateRates(const SendRates& rates, double fps, Timestamp now);
  void MaybeUpdateRates(const SendRates& rates, double fps, bool encode_path, bool reconfigured, Timestamp now);
  SimulcastEncodeDecision Encode(int width, int height, double fps, Timestamp capture, Timestamp now);
  void UpdateFrameRate(Timestamp now);

  static constexpr TimeDelta kParameterUpdateInterval = TimeDelta::Millis(1000);
  SimulcastSendControllerConfig cfg_;
  Session& session_;
  std::vector<Layer> layers_;
  std::vector<video::VideoStream> streams_;
  video::VideoCodec codec_;
  std::unique_ptr<video::SimulcastRateAllocator> allocator_;
  std::unique_ptr<cc::SimulcastEncoderBitrateAdjuster> adjuster_;
  video::VideoBitrateAllocation allocation_;
  cc::FrameDropper dropper_;
  int configured_width_ = 0, configured_height_ = 0;
  uint64_t allocation_updates_seen_ = 0;
  bool rates_set_ = false;
  Timestamp last_parameters_update_ = Timestamp::MinusInfinity();
  // VideoStreamEncoder::pending_frame_.
  struct PendingFrame {
    int width = 0, height = 0;
    Timestamp posted;
  };
  std::optional<PendingFrame> pending_frame_;
  bool encoder_paused_seen_ = false;
  bool paused_ = false;
  Timestamp last_keyframe_ = Timestamp::MinusInfinity();
  Timestamp last_keyframe_request_ = Timestamp::MinusInfinity();  // Without per-layer keyframes.
  std::deque<int64_t> frame_times_ms_;
  int64_t first_frame_ms_ = -1;
  int input_fps_ = 0;
  uint64_t dropped_[5] = {0, 0, 0, 0, 0};
  uint64_t encoded_frames_ = 0, keyframe_requests_ignored_ = 0, layer_switches_ = 0, reconfigurations_ = 0;
  uint64_t pending_dropped_ = 0, pending_encoded_ = 0;
};

}  // namespace sm::rtp
