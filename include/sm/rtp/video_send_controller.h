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
// (OnBitrateUpdated, when MaybeEncodePendingFrame polls it after the session
// ran, so the overshoot detector leaks to the update's time; issue #894),
// on an encoder reconfiguration and every 1 s
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
// (ReconfigureEncoder's encoder reset). The start-bitrate reset of the initial
// frame drop (InitialFrameDropper::SetTargetBitrate) runs only with
// QualityScalerSettings' InitialBitrateIntervalMs and Factor, which are unset
// by default, as here (`initial_bitrate_interval`, `initial_bitrate_factor`).
// A frame dropped for its size or while the encoder is paused is held, as
// VideoStreamEncoder's `pending_frame_` (EncodeDecision::hold): the next
// captured frame drops it (kEncoderQueue), and when the encoder resumes
// (OnBitrateUpdated with a non-zero target after a zero one) it is encoded at
// once if it is no longer too large and was captured less than 1 s ago
// (kPendingFrameTimeoutMs). MaybeEncodePendingFrame returns that decision.
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
// through video::ResourceAdaptationProcessor (resource_adaptation_processor.cc):
// each keeps the restrictions it last asked for, an adapt-down is always applied,
// and an adapt-up only for the resource that set the most adaptations (shared
// with none), so CPU underuse cannot undo the quality scaler's steps or the
// reverse. The initial frame drop counts as the quality scaler's
// (MaybeInitializePixelLimitResource / OnFrameDroppedDueToSize). OnEncoded must
// be called for every frame the decision allowed, in order: the controller
// pairs them by count, where libwebrtc uses the RTP timestamp.
// Resources are removed and added again as libwebrtc's
// VideoStreamEncoderResourceManager does when the encoder's info changes
// (OnEncoderInfoChanged; ConfigureEncodeUsageResource, ConfigureQualityScaler):
// the quality scaler stops without trusted QP thresholds (and the initial frame
// drop with it), the CPU check without enable_cpu_overuse_detection.
// ResourceAdaptationProcessor::RemoveResource drops the resource's limit and
// its later signals; when it was the only adapted resource the restrictions
// are cleared, and when it was more limited than the most limited one left
// the stream goes back to that one's restrictions and counters
// (RemoveLimitationsImposedByResource, VideoStreamAdapter::GetAdaptationTo;
// issue #646). A started CPU check restarts with a fresh usage filter.
// video::VideoStreamAdapter has every degradation preference (#646); the
// controller runs MAINTAIN_FRAMERATE, the camera call's, and counts steps by
// VideoAdaptationCounters::Total() as the processor does. The frame dropper is
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
#include "sm/video/resource_adaptation_processor.h"
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
  // the target), and DistributeAllocationToTemporalLayers hands the one temporal
  // layer that rate in whole kbps, rounded down (issue #577). 0 = off (the v1
  // behaviour; `allocator v2` turns it on).
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
  // EncoderInfo::resolution_bitrate_limits, for BitrateConstraint (issue #819;
  // video::VideoStreamAdapter). Empty, as libwebrtc's software encoders report
  // by default: no step up is held back for the bitrate.
  std::vector<video::ResolutionBitrateLimits> resolution_bitrate_limits;
  // QualityScalerSettings' WebRTC-Video-QualityScalerSettings `initial_bitrate_interval_ms` and
  // `initial_bitrate_factor` (rtc_base/experiments/quality_scaler_settings.cc), unset by default as in
  // M157: with both, a target below `factor` times the start bitrate within `interval` of it restarts the
  // initial frame drop once (InitialFrameDropper::SetTargetBitrate). A negative interval or a factor below
  // 0.01 (kMinScaleFactor) is ignored, as there.
  std::optional<TimeDelta> initial_bitrate_interval;
  std::optional<double> initial_bitrate_factor;
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
  // A dropped frame the app keeps: MaybeEncodePendingFrame may ask for it on resume (VideoStreamEncoder's
  // pending_frame_, set for kSize and kPaused). The next OnCapturedFrame releases it.
  bool hold = false;
  // When the frame to encode was captured (OnCapturedFrame's `now`); a held frame's, from MaybeEncodePendingFrame.
  Timestamp capture_time = Timestamp::MinusInfinity();
};

struct EncodedFrameReport {
  DataSize size;
  bool keyframe = false;
  int qp = -1;  // EncodedImage::qp_; negative = not reported.
};

// The parts of VideoEncoder::EncoderInfo that decide which adaptation
// resources run (VideoStreamEncoderResourceManager).
struct EncoderAdaptationInfo {
  // ConfigureQualityScaler's quality_scaling_allowed under MAINTAIN_FRAMERATE:
  // scaling_settings.thresholds set and is_qp_trusted.
  bool quality_scaling = true;
  // enable_cpu_overuse_detection (ConfigureEncodeUsageResource).
  bool cpu_overuse_detection = true;
};

class VideoSendController : public MetricsSource {
 public:
  // `session` (not owned) supplies the rates and the pushback state.
  VideoSendController(VideoSendControllerConfig cfg, Session& session);

  // Per captured frame, before encoding.
  EncodeDecision OnCapturedFrame(const CapturedFrame& frame, Timestamp now);
  // After the session ran (Process, OnPacket): VideoStreamEncoder::OnBitrateUpdated's resume. When the
  // encoder was paused at the last call and is not any more, the held frame (EncodeDecision::hold) is
  // encoded if it fits and is less than kPendingFrameTimeoutMs (1 s) old; the decision names it by its
  // capture_time. Otherwise nothing. The decision is an encode like OnCapturedFrame's (OnEncoded follows).
  std::optional<EncodeDecision> MaybeEncodePendingFrame(Timestamp now);
  // After a frame the decision allowed was encoded. Not called for a drop.
  void OnEncoded(const EncodedFrameReport& report, Timestamp now);
  // Forward every Session event: the controller acts on kEncoderPause,
  // kEncoderResume and kKeyframeRequested (a peer PLI/FIR or the outage monitor).
  void OnSessionEvent(const SessionEvent& e);
  void RequestKeyframe() { keyframe_needed_ = true; }
  // VideoStreamEncoder's `encoder_info_ != info` branch: ConfigureEncodeUsageResource,
  // then ConfigureQualityScaler, which add, restart or remove the two
  // resources (see above). Only with `quality_scaling`; both start on.
  void OnEncoderInfoChanged(const EncoderAdaptationInfo& info, Timestamp now);
  bool quality_scaler_started() const { return processor_.HasResource(kQualityResource); }
  bool encode_usage_started() const { return processor_.HasResource(kCpuResource); }

  const cc::FrameDropper& frame_dropper() const { return dropper_; }
  const cc::EncoderBitrateAdjuster& bitrate_adjuster() const { return adjuster_; }
  const video::QualityScaler& quality_scaler() const { return scaler_; }
  const video::VideoStreamAdapter& stream_adapter() const { return stream_adapter_; }
  // The processor's resource ids.
  enum Resource { kQualityResource = 0, kCpuResource = 1, kNumResources = 2 };
  const video::ResourceAdaptationProcessor& adaptation_processor() const { return processor_; }
  const cc::CpuOveruseDetector& overuse_detector() const { return overuse_; }
  bool paused() const { return paused_; }
  // A frame is held (EncodeDecision::hold) and not yet released.
  bool has_pending_frame() const { return pending_frame_.has_value(); }
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
  void OnQualityScalerSignal(video::QualityScaler::Signal signal, Timestamp now);
  void OnCpuSignal(cc::CpuOveruseDetector::Signal signal, Timestamp now);
  // ResourceAdaptationProcessor::OnResourceUsageStateMeasured, then the sink wants.
  void OnResourceUsageStateMeasured(Resource r, video::ResourceUsageState state, Timestamp now);
  // After the processor changed the restrictions: the counts, the event and the
  // sink wants (VideoSourceSinkController), for a step with `reason`.
  void OnRestrictionsChanged(const video::VideoAdaptationCounters& before, int reason, Timestamp now);
  // ResourceAdaptationProcessor::RemoveResource (VideoStreamEncoderResourceManager::RemoveResource).
  void RemoveResource(Resource r, Timestamp now);
  bool QualityScalingDropsForSize(int width, int height, DataRate target, Timestamp now);
  // VideoStreamEncoder::UpdateBitrateAllocation's allocation and the bitrate adjuster (see the .cc).
  DataRate EncoderRate(const SendRates& rates, double fps, bool encode_path, bool new_adjuster, Timestamp now);
  // VideoStreamEncoderResourceManager::SetStartBitrate / SetTargetBitrate for the InitialFrameDropper.
  void OnTargetBitrate(DataRate target, Timestamp now);
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
  video::ResourceAdaptationProcessor processor_{stream_adapter_};
  video::VideoAdapter video_adapter_;
  int initial_framedrop_ = 0;  // InitialFrameDropper::initial_framedrop_.
  // InitialFrameDropper's set_start_bitrate_ and its time, has_seen_first_bwe_drop_.
  DataRate start_bitrate_ = DataRate::Zero();
  int64_t start_bitrate_time_ = 0;
  bool start_bitrate_set_ = false, has_seen_first_bwe_drop_ = false;
  uint64_t target_updates_seen_ = 0;
  // VideoStreamEncoder::pending_frame_ (its size as encoded) and pending_frame_post_time_us_.
  struct PendingFrame {
    int width = 0, height = 0;
    Timestamp posted;
  };
  std::optional<PendingFrame> pending_frame_;
  bool encoder_paused_seen_ = false;  // EncoderPaused() at the last look: a resume is a change from it.
  uint64_t pending_dropped_ = 0, pending_encoded_ = 0, initial_framedrop_resets_ = 0;
  int configured_width_ = 0, configured_height_ = 0;  // The encoder's send_codec_ size.
  uint64_t adapt_downs_ = 0, adapt_ups_ = 0;
  cc::CpuOveruseDetector overuse_;
  uint64_t resources_removed_ = 0, restrictions_restored_ = 0;
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
