// Test bench scenarios: a call configuration plus a timeline of link
// impairments and the user-experience expectations the call must meet.
//
// Scenarios are plain text so they can be pasted into bug reports:
//
//   name     wifi-handover
//   describe Wi-Fi to LTE handover: 2 s outage, then a slower, lossier link
//   duration 40s
//   seed     7
//   # forward (sender -> receiver) link; every key is optional
//   at 0s    capacity=2500kbps delay=25ms jitter=4ms loss=0.5% queue=200ms
//   at 15s   down
//   at 17s   up capacity=800kbps delay=60ms loss=2%
//   # reverse (feedback) link
//   reverse at 0s delay=25ms
//   expect   audio.concealed_pct < 5
//   expect   video.longest_freeze_ms <= 2500
//
// See testbench/README.md for every directive and link key.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "sm/core/units.h"

namespace sm::testbench {

// One change to a link at `at`. Unset fields keep their previous value.
struct LinkChange {
  TimeDelta at = TimeDelta::Zero();
  std::optional<DataRate> capacity;     // PlusInfinity() = unlimited.
  std::optional<TimeDelta> delay;
  std::optional<TimeDelta> jitter;
  std::optional<double> loss;           // Random (Bernoulli) loss, 0..1.
  std::optional<double> burst_len;      // Mean loss burst length in packets (>1 = Gilbert-Elliott).
  std::optional<double> duplicate;
  std::optional<bool> reorder;
  std::optional<TimeDelta> queue;       // Bottleneck buffer, as time at capacity. Zero = unlimited.
  std::optional<DataRate> cross;        // Competing constant-rate traffic on the bottleneck.
  std::optional<bool> down;
};

enum class CompareOp { kLt, kLe, kGt, kGe, kEq };

struct Expectation {
  std::string metric;
  CompareOp op = CompareOp::kLt;
  double value = 0;
  std::string text;  // As written, for reports.
};

struct Scenario {
  std::string name = "unnamed";
  std::string description;
  TimeDelta duration = TimeDelta::Seconds(30);
  uint64_t seed = 1;
  int compare_seeds = 0;  // `compare_seeds N`: read only by sm_compare, overrides its --seeds for this scenario (0 = use --seeds).
  // `compare_clock_start S`: read only by sm_compare, the libwebrtc side's simulated clock at the call's start, in
  // seconds, for every seed (0 = derived from the seed). RTCPSender seeds its report-interval draws from that clock
  // (#390, #399).
  int64_t compare_clock_start_s = 0;

  // Media.
  bool audio = true;
  bool video = true;
  bool red = true;                  // Audio redundancy (RFC 2198).
  bool nack = true;                 // Video NACK/RTX.
  bool fec = true;                  // Adaptive video FEC (protection controller).
  int audio_sample_rate = 16000;
  DataRate audio_bitrate = DataRate::KilobitsPerSec(32);
  int video_fps = 30;
  int video_width = 1280, video_height = 720;  // `video_size <W>x<H>`: the captured resolution (issue #84).
  double video_complexity = 1;  // `video_complexity <x>`: content for the encoder's QP model (testbench/encoder_model.h).
  double encoder_speed = 1;     // `encoder_speed <x>`: CPU speed for the encode-time model (testbench/encoder_model.h).
  // `encode_delay on|off`: the encoder takes the encode-time model's time, so a frame reaches the
  // library and the sender that much after its capture, on both stacks (issue #84 step 5: what
  // libwebrtc's CPU overuse detector measures). Off: encoding is instant, as before.
  bool encode_delay = false;
  size_t video_max_payload = 1458 - 12;  // `video_max_payload <bytes>`: rtp::VideoStreamConfig::max_payload_size (issue #257).
  DataRate video_max = DataRate::KilobitsPerSec(2500);
  DataRate video_min = DataRate::KilobitsPerSec(50);
  TimeDelta keyframe_interval = TimeDelta::Zero();  // Zero = only on request.
  bool control_handler = true;  // `control_handler on|off`: libwebrtc's CongestionControlHandler (issue #204; default on since #224/#204).
  bool bitrate_adjuster = true;  // `bitrate_adjuster on|off`: libwebrtc's EncoderBitrateAdjuster (default on as in M157, issue #5).
  // `rate_utilization_tracker on|off`: the adjuster's media utilization factor from libwebrtc's
  // RateUtilizationTracker, its default "newfangled headroom adjustment" (issue #355;
  // cc::EncoderBitrateAdjusterConfig::newfangled_headroom_adjustment). On by default, as libwebrtc
  // (since #355's flip, after #362 turned periodic ALR probing off for video).
  bool rate_utilization_tracker = true;
  // `sps_pps_tracker on|off`: the receiver drops the first packet of an IDR slice that arrives
  // before an SPS and a PPS and requests a keyframe, as libwebrtc's H264SpsPpsTracker
  // (rtp::RtpReceiverConfig::sps_pps_tracker; issue #453). Off by default until it is judged.
  bool sps_pps_tracker = false;
  // `render_smoothing on|off`: decoded frames pass libwebrtc's pre-render queue (IncomingVideoStream,
  // VideoRenderFrames) before they render, as sm_compare's libwebrtc side does by default
  // (rtp::RtpReceiverConfig::render_smoothing; issue #437). Off by default until it is judged.
  bool render_smoothing = false;
  // `keyframe_flush_padding on|off`: a keyframe's pacer flush also removes queued padding, which
  // libwebrtc sends on the video stream's RTX SSRC and flushes with it
  // (cc::PacingControllerConfig::keyframe_flush_padding; issue #437). Off by default until it is judged.
  bool keyframe_flush_padding = false;
  // `h264_missing_packets on|off`: the receiver's packet buffer holds a complete H.264 delta frame
  // while a sequence number before it is missing, as libwebrtc's PacketBuffer `missing_packets_`
  // (timing::PacketBufferConfig::missing_packets; issue #453). Off by default until it is judged.
  bool h264_missing_packets = false;
  // `quality_scaling on|off`: libwebrtc's QualityScalerResource and EncodeUsageResource (CPU
  // overuse) with MAINTAIN_FRAMERATE and the initial frame drop, on the encoder model's QP and
  // encode time (issue #84 steps 4 and 5; on by default since #84's flip, on both stacks of
  // sm_compare, as libwebrtc runs a camera whose encoder reports QP).
  bool quality_scaling = true;
  bool timing_v2 = true;  // `timing v2`: libwebrtc's video receive timing (VCMTiming, JitterEstimator, frame release; issue #24; default v2 since #24; v1 is the library's FrameScheduler).
  bool av_sync = true;  // `av_sync on|off`: lip sync, audio and video in one sync group on both stacks (issue #77; rtp::SessionConfig::av_sync; default on since #77 step 3).
  bool send_rate_stats = true;  // `send_rate_stats on|off`: the repair share off the encoder rate from libwebrtc's per-type RateStatistics (issue #224; rtp::SendSideConfig::send_rate_stats; default on since #224/#204).
  bool link_headroom = true;  // `link_headroom on|off`: the encoder bitrate adjuster's bandwidth allocation before the encoder maximum, as VideoSendStreamImpl's link_allocation (issue #326; rtp::SendSideConfig::link_headroom).
  bool neteq_v2 = true;  // `neteq v2`: libwebrtc's NetEq (DelayManager, DecisionLogic, Expand, Merge, time stretch, Opus in-band FEC; issue #62; default v2 since #62).

  // `host_rtt_inf <time>`: at that time the host hands both sessions an infinite RTT
  // (Session::SetRtt(TimeDelta::PlusInfinity())), a host bug the library must absorb (issue #441).
  std::optional<TimeDelta> host_rtt_inf;

  // Congestion control.
  DataRate start_rate = DataRate::KilobitsPerSec(300);
  DataRate min_rate = DataRate::KilobitsPerSec(30);
  DataRate max_rate = DataRate::KilobitsPerSec(5000);

  std::vector<LinkChange> forward;
  std::vector<LinkChange> reverse;
  std::vector<Expectation> expectations;
};

struct ParseResult {
  std::optional<Scenario> scenario;
  std::string error;  // "line N: ..." when scenario is empty.
};

ParseResult ParseScenario(const std::string& text);

// Canonical text form (round-trips through ParseScenario).
std::string FormatScenario(const Scenario& s);

const char* ToString(CompareOp op);
bool Evaluate(CompareOp op, double lhs, double rhs);

}  // namespace sm::testbench
