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
// libwebrtc M157, also here; `bitrate_adjuster` switches it off) lowers the
// rate `EncodeDecision::target` by the encoder's measured overshoot (issue #5); the frame dropper keeps working on the
// unadjusted target, as in VideoStreamEncoder::OnBitrateUpdated. With
// `adjuster_on_update`, as in VideoStreamEncoder, the adjusted rate is
// recomputed only on an allocation update from the session's allocator
// (OnBitrateUpdated), on an encoder reconfiguration and every 1 s
// (MaybeEncodeVideoFrame's kParameterUpdateIntervalMs), and held in between
// (issue #569; on by default since #637, as libwebrtc); off, on every captured frame.
//
// `quality_scaling` (on by default since #84's flip, as libwebrtc for a camera
// whose encoder reports scaling settings; issue #84 step 4) adds libwebrtc's
// QualityScalerResource with the MAINTAIN_FRAMERATE degradation preference of
// a camera (video/adaptation/video_stream_encoder_resource_manager.cc,
// quality_scaler_resource.cc): video::QualityScaler version 2 reads the QP of
// each encoded frame (EncodedFrameReport::qp) and the frame dropper's drops
// (kMediaOptimization); its high-QP signal adapts the resolution down and its
// low-QP signal up, through video::VideoStreamAdapter, and video::VideoAdapter
// turns the restrictions into the frame size the decision returns. Also
// libwebrtc's initial frame drop (VideoStreamEncoder::DropDueToSize with the
// resource manager's InitialFrameDropper): while the quality scaler runs and
// no frame has passed it yet, up to 4 frames (kMaxInitialFramedrop) are
// dropped as too large for the encoder target (above 320x240 under 300 kbps,
// above 640x480 under 500 kbps; the encoder model reports no resolution
// bitrate limits), each one adapting the resolution down. A new frame size
// reconfigures the encoder, which makes the next frame a keyframe
// (ReconfigureEncoder's encoder reset). Not ported: the start-bitrate reset of
// the initial frame drop (QualityScalerSettings' InitialBitrateIntervalMs and
// Factor are unset by default) and the dropped frame kept as `pending_frame_`.
// The quality scaler's CheckQpTask due at a frame's capture time runs after
// that frame was encoded and its QP reported, and its restriction reaches the
// source through a worker-queue task (OnVideoSourceRestrictionsUpdated), so a
// step applies from the next frame (issue #348).
// With quality scaling the drop checks run in VideoStreamEncoder's order:
// congestion window (OnFrame), initial frame drop, outage pause, frame dropper.
// The same switch adds libwebrtc's EncodeUsageResource (issue #84 step 5): an
// encoder reports enable_cpu_overuse_detection by default, so with
// MAINTAIN_FRAMERATE VideoStreamEncoderResourceManager::ConfigureEncodeUsageResource
// starts it with the encoder (encode_usage_resource.cc). cc::CpuOveruseDetector
// version 2 (OveruseFrameDetector, a software encoder's CpuOveruseOptions,
// target frame rate = the codec's maximum) takes each encoded frame's start in
// OnCapturedFrame and its completion in OnEncoded, so its encode time is the
// time between the two calls (SendProcessingUsage1 measures on the clock); its
// overuse adapts the resolution down and its underuse up. The two resources go
// through a port of call/adaptation/resource_adaptation_processor.cc: each
// keeps the restrictions it last asked for, an adapt-down is always applied,
// and an adapt-up only for the resource that set the most adaptations (shared
// with none), so CPU underuse cannot undo the quality scaler's steps or the
// reverse. The initial frame drop counts as the quality scaler's
// (MaybeInitializePixelLimitResource / OnFrameDroppedDueToSize). OnEncoded must
// be called for every frame the decision allowed, in order: the controller
// pairs them by count, where libwebrtc uses the RTP timestamp.
// The other degradation preferences are not wired yet. The frame dropper is
// libwebrtc's, applied to keyframes too (issue #5).
#pragma once

#include <cstdint>
#include <deque>
#include <optional>

#include "sm/cc/encoder_bitrate_adjuster.h"
#include "sm/cc/frame_dropper.h"
#include "sm/cc/video_adaptation.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"
#include "sm/rtp/session.h"
#include "sm/video/quality_scaler.h"
#include "sm/video/video_stream_adapter.h"

namespace sm::rtp {

struct VideoSendControllerConfig {
  TimeDelta keyframe_interval = TimeDelta::Zero();  // Periodic keyframe; zero = off.
  // The configured maximum frame rate (VideoStreamEncoder::max_framerate_). The
  // dropper and the bitrate adjuster get the measured input rate instead; this
  // is the fallback while it is unavailable (GetInputFramerateFps).
  double fps = 30;
  // RateControlSettings bitrate_adjuster (true in M157, issue #5);
  // `adjuster_use_headroom` and the codec minimum bitrate are in `adjuster`.
  bool bitrate_adjuster = true;
  cc::EncoderBitrateAdjusterConfig adjuster;
  // VideoStreamEncoder's timing (issue #569): the adjuster re-runs only in UpdateBitrateAllocation,
  // on an allocation update from the session's allocator, on a reconfiguration and every 1 s
  // (kParameterUpdateIntervalMs), and the encoder keeps its rate in between. On by default, as libwebrtc
  // has no other timing (issue #637); off re-runs it on every captured frame (the behaviour before #637).
  bool adjuster_on_update = true;
  // SimulcastRateAllocator::DistributeAllocationToSimulcastLayers for a single
  // stream (modules/video_coding/utility/simulcast_rate_allocator.cc): the
  // encoder is configured with max(minBitrate, min(target, maxBitrate)), so a
  // target below the codec minimum is raised to it (the frame dropper still gets
  // the target). 0 = off (the v1 behaviour; `allocator v2` turns it on).
  DataRate allocation_min_bitrate;
  // The codec maximum is the stream's max_bitrate_bps, which the app sets;
  // libwebrtc's allocator has no cap of its own, so neither does the default
  // (issue #580): the session's allocation (SendSideConfig::video_max) bounds it.
  DataRate allocation_max_bitrate = DataRate::PlusInfinity();
  // EncoderRtcpFeedback::OnReceivedIntraFrameRequest (video/encoder_rtcp_feedback.cc):
  // a PLI or FIR within kMinKeyframeSendIntervalMs (300 ms) of the last one it
  // passed on is ignored. 300 ms by default, as libwebrtc (issue #580); 0 = off.
  TimeDelta min_keyframe_request_interval = TimeDelta::Millis(300);
  // QualityScalerResource and EncodeUsageResource with MAINTAIN_FRAMERATE and
  // the initial frame drop (see above). On by default, as libwebrtc (issue #84's flip).
  // Off = the v1 behaviour: the captured
  // size is encoded as is.
  bool quality_scaling = true;
  // EncoderInfo::ScalingSettings: H.264's QP thresholds (QualityScalingExperiment's
  // default 24/37) and kDefaultMinPixelsPerFrame.
  video::QpThresholds qp_thresholds;
  int min_pixels_per_frame = 320 * 180;
  EventSink* events = nullptr;
};

struct CapturedFrame {
  int width = 0, height = 0;
};

// kSize: VideoStreamEncoder::DropDueToSize (the initial frame drop).
enum class DropReason { kNone, kPaused, kCongestionWindow, kFrameDropper, kSize };

struct EncodeDecision {
  bool encode = false;
  DropReason reason = DropReason::kNone;  // Why the frame is dropped.
  bool keyframe = false;
  DataRate target;  // Rate to configure the encoder with (after the bitrate adjuster).
  int width = 0, height = 0;
  double fps = 0;  // Measured input frame rate (GetInputFramerateFps).
};

struct EncodedFrameReport {
  DataSize size;
  bool keyframe = false;
  int qp = -1;  // EncodedImage::qp_; negative = not reported.
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
  const video::QualityScaler& quality_scaler() const { return scaler_; }
  const video::VideoStreamAdapter& stream_adapter() const { return stream_adapter_; }
  const cc::CpuOveruseDetector& overuse_detector() const { return overuse_; }
  bool paused() const { return paused_; }
  // VideoStreamEncoder::GetInputFramerateFps: the rate of captured frames over
  // the last 2970 ms (FrameCadenceAdapter's RateStatistics), or the configured
  // maximum while it is unavailable or zero.
  double input_fps() const { return input_fps_ > 0 ? input_fps_ : cfg_.fps; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  VideoSendControllerConfig cfg_;
  Session& session_;
  cc::FrameDropper dropper_;
  cc::EncoderBitrateAdjuster adjuster_;
  // Quality scaling and CPU adaptation (cfg_.quality_scaling).
  enum Resource { kQualityResource = 0, kCpuResource = 1, kNumResources = 2 };
  // ResourceAdaptationProcessor's adaptation_limits_by_resources_ entry.
  struct ResourceLimit {
    video::VideoSourceRestrictions restrictions;
    int adaptations = 0;  // VideoAdaptationCounters::Total().
  };
  void OnQualityScalerSignal(video::QualityScaler::Signal signal, Timestamp now);
  void OnCpuSignal(cc::CpuOveruseDetector::Signal signal, Timestamp now);
  // ResourceAdaptationProcessor::OnResourceOveruse / OnResourceUnderuse.
  void OnResourceOveruse(Resource r, Timestamp now);
  void OnResourceUnderuse(Resource r, Timestamp now);
  // VideoStreamAdapter::ApplyAdaptation with `r` as the reason, then the
  // processor's OnVideoSourceRestrictionsUpdated and the sink wants.
  void ApplyAdaptation(const video::VideoStreamAdapter::Adaptation& a, Resource r, Timestamp now);
  bool QualityScalingDropsForSize(int width, int height, DataRate target, Timestamp now);
  // The rate the encoder is set to, re-adjusted only when libwebrtc's
  // VideoStreamEncoder calls UpdateBitrateAllocation (see the .cc).
  // `encode_path`: the frame got past OnFrame's congestion-window drop.
  DataRate AdjustedRate(DataRate allocation, DataRate link, double fps, bool encode_path, bool new_adjuster,
                        Timestamp now);
  // VideoStreamEncoder's kParameterUpdateIntervalMs.
  static constexpr TimeDelta kParameterUpdateInterval = TimeDelta::Millis(1000);
  DataRate encoder_rate_;  // last_encoder_rate_settings_' adjusted bitrate.
  bool adjusted_once_ = false;
  uint64_t allocation_updates_seen_ = 0;
  Timestamp last_parameters_update_ = Timestamp::MinusInfinity();  // last_parameters_update_ms_.
  video::QualityScaler scaler_;
  video::VideoStreamAdapter stream_adapter_;
  video::VideoAdapter video_adapter_;
  int initial_framedrop_ = 0;  // InitialFrameDropper::initial_framedrop_.
  int configured_width_ = 0, configured_height_ = 0;  // The encoder's send_codec_ size.
  uint64_t adapt_downs_ = 0, adapt_ups_ = 0;
  cc::CpuOveruseDetector overuse_;
  std::optional<ResourceLimit> limits_[kNumResources];
  uint32_t encodes_started_ = 0, encodes_completed_ = 0;  // Pair OnFrameCaptured with OnFrameSent.
  uint64_t cpu_adapt_downs_ = 0, cpu_adapt_ups_ = 0, adapt_ups_not_most_limited_ = 0;
  // RateStatistics (window 2970 ms, scale 1000) over captured frames.
  void UpdateFrameRate(Timestamp now);
  std::deque<int64_t> frame_times_ms_;
  int64_t first_frame_ms_ = -1;
  int input_fps_ = 0;  // Rate before the current frame was counted; 0 = unavailable.
  bool paused_ = false;
  bool keyframe_needed_ = true;  // The first frame is a keyframe.
  Timestamp last_keyframe_ = Timestamp::MinusInfinity();
  // When the last peer keyframe request was passed on (EncoderRtcpFeedback's
  // time_last_packet_delivery_queue_; libwebrtc starts it at clock zero, which
  // a real or simulated clock is far past).
  Timestamp last_keyframe_request_ = Timestamp::MinusInfinity();
  uint64_t dropped_[5] = {0, 0, 0, 0, 0};
  uint64_t encoded_ = 0, keyframes_ = 0, keyframe_requests_ignored_ = 0;
};

}  // namespace sm::rtp
