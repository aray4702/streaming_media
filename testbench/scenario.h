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
#include <utility>
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
  // `compare off`: read only by sm_compare, which skips the scenario (#921). For a scenario that tests only this
  // library and has no defined libwebrtc counterpart (issue_451_host_rtt_near_limit: libwebrtc overflows, #904).
  bool compare = true;
  // `compare_clock_start S`: the libwebrtc side's simulated clock at the call's start in sm_compare, in seconds, for
  // every seed (0 = derived from the seed). RTCPSender seeds its report-interval draws from that clock (#390, #399),
  // and the test bench seeds its RTCP engines with the same time (#517).
  int64_t compare_clock_start_s = 0;
  // `call one-way|two-way`: one-way (the default) is the sender -> receiver call every scenario runs today; two-way
  // has both endpoints send and receive (#2). It parses, but sm_testbench rejects it until #746 runs it, and
  // sm_compare skips it: libwebrtc's side has no two-way call (issue #743).
  bool two_way = false;
  // `two_way_video both|up|down` (issue #747): in a two-way call with video, which directions send it: both (the
  // default), up (user to AI) or down (AI to user). Only for `call two-way`.
  bool video_up = true, video_down = true;

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
  // `encoder_overshoot <x>`: delta frames are x times the size model's (issue #466; testbench/encoder_model.h).
  double encoder_overshoot = 1;
  // `scene_change <interval> <frames> <factor>`: every interval (after the first), the delta frames of the first
  // `frames` frame intervals are `factor` times larger (issue #466; testbench/encoder_model.h). Zero interval = off.
  TimeDelta scene_change_interval = TimeDelta::Zero();
  int scene_change_frames = 0;
  double scene_change_factor = 1;
  // `encode_delay on|off`: the encoder takes the encode-time model's time, so a frame reaches the
  // library and the sender that much after its capture, on both stacks (issue #84 step 5: what
  // libwebrtc's CPU overuse detector measures). Off: encoding is instant, as before.
  bool encode_delay = false;
  // `video_max_payload <bytes>`: rtp::VideoStreamConfig::max_payload_size (issue #257). The default is
  // libwebrtc's whole packet, kDefaultMaxPacketSize 1460, less the 12-byte fixed header: both stacks get
  // the same limit and reserve RTX and FEC room from it themselves (issue #513).
  size_t video_max_payload = 1460 - 12;
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
  // `adjuster_on_update on|off`: the bitrate adjuster re-runs only when VideoStreamEncoder's
  // UpdateBitrateAllocation does (an allocation update, a reconfiguration, every 1 s; issue #569;
  // rtp::VideoSendControllerConfig::adjuster_on_update). On by default, as libwebrtc (issue #637); off:
  // every captured frame.
  bool adjuster_on_update = true;
  // `per_stream_rtt on|off`: both sessions aggregate the RTT per send stream, as libwebrtc's
  // RTCPReceiver::RttStats and ModuleRtpRtcpImpl2::PeriodicUpdate (rtp::SessionConfig::per_stream_rtt;
  // issue #568). On by default, as libwebrtc; `off` keeps one session-wide RTT.
  bool per_stream_rtt = true;
  // `receive_statistics_v2 on|off`: both sessions keep receive statistics as libwebrtc's
  // StreamStatisticianImpl (max reordering threshold, stream restart, 255-scaled fraction lost;
  // rtp::SessionConfig::receive_statistics_v2; issue #575). On by default, as libwebrtc; `off` keeps
  // the RFC 3550 counts with no reordering threshold.
  bool receive_statistics_v2 = true;
  // `expected_packet_overhead on|off`: the sender's video overhead rate counts the RTP header it
  // builds (RtpSender::ExpectedPerPacketOverhead: 12 + the registered non-volatile extensions + 2 with
  // RTX, 22 B here with NACK, 20 without), as libwebrtc's RtpVideoSender; off: a fixed 24 B
  // (rtp::SendSideConfig::expected_packet_overhead; issue #577). On by default, as libwebrtc.
  bool expected_packet_overhead = true;
  // `sps_pps_tracker on|off`: the receiver drops the first packet of an IDR slice that arrives
  // before an SPS and a PPS and requests a keyframe, as libwebrtc's H264SpsPpsTracker
  // (rtp::RtpReceiverConfig::sps_pps_tracker; issue #453). On by default, as libwebrtc.
  bool sps_pps_tracker = true;
  // `render_smoothing on|off`: decoded frames pass libwebrtc's pre-render queue (IncomingVideoStream,
  // VideoRenderFrames) before they render, as sm_compare's libwebrtc side does by default
  // (rtp::RtpReceiverConfig::render_smoothing; issue #437). On by default, as libwebrtc.
  bool render_smoothing = true;
  // `keyframe_flush_padding on|off`: a keyframe's pacer flush also removes queued padding, which
  // libwebrtc sends on the video stream's RTX SSRC and flushes with it
  // (cc::PacingControllerConfig::keyframe_flush_padding; issue #437). On by default, as libwebrtc.
  bool keyframe_flush_padding = true;
  // `h264_missing_packets on|off`: the receiver's packet buffer holds a complete H.264 delta frame
  // while a sequence number before it is missing, as libwebrtc's PacketBuffer `missing_packets_`
  // (timing::PacketBufferConfig::missing_packets; issue #453). On by default, as libwebrtc.
  bool h264_missing_packets = true;
  // `quality_scaling on|off`: libwebrtc's QualityScalerResource and EncodeUsageResource (CPU
  // overuse) with MAINTAIN_FRAMERATE and the initial frame drop, on the encoder model's QP and
  // encode time (issue #84 steps 4 and 5; on by default since #84's flip, on both stacks of
  // sm_compare, as libwebrtc runs a camera whose encoder reports QP).
  bool quality_scaling = true;
  bool timing_v2 = true;  // `timing v2`: libwebrtc's video receive timing (VCMTiming, JitterEstimator, frame release; issue #24; default v2 since #24; v1 is the library's FrameScheduler).
  bool av_sync = true;  // `av_sync on|off`: lip sync, audio and video in one sync group on both stacks (issue #77; rtp::SessionConfig::av_sync; default on since #77 step 3). Off only empties the receiver's sync group; the sender's audio RTCP stays on, as libwebrtc's (issue #524).
  bool audio_receive_rtcp = true;  // `audio_receive_rtcp on|off` (default on, as libwebrtc): the receiver's audio stream reports in its own RTCP (an RR with the audio block alone every 5 s), as libwebrtc's ChannelReceive (issue #484; rtp::SessionConfig::audio_receive_rtcp). Off: the audio block rides in every compound the receiver sends. Our side only: libwebrtc's side of sm_compare always reports per stream.
  bool send_rate_stats = true;  // `send_rate_stats on|off`: the repair share off the encoder rate from libwebrtc's per-type RateStatistics (issue #224; rtp::SendSideConfig::send_rate_stats; default on since #224/#204).
  bool link_headroom = true;  // `link_headroom on|off`: the encoder bitrate adjuster's bandwidth allocation before the encoder maximum, as VideoSendStreamImpl's link_allocation (issue #326; rtp::SendSideConfig::link_headroom).

  // `host_rtt_inf <time>`: at that time the host hands both sessions an infinite RTT
  // (Session::SetRtt(TimeDelta::PlusInfinity())), a host bug the library must absorb (issue #441).
  std::optional<TimeDelta> host_rtt_inf;
  // `host_rtt <time> <rtt>`: from that time on the host hands both sessions a finite RTT it
  // measured itself (Session::SetRtt at every step), for RTTs the call cannot produce, such as one
  // near the int64 limit (issue #451).
  std::optional<std::pair<TimeDelta, TimeDelta>> host_rtt;
  // `apm_rate <hz>`: before the call the host builds the ported APM modules (AEC3, NS, AGC2)
  // at that rate and feeds each 50 frames of 10 ms, as a host that configured them would.
  // Reports apm.{aec3,ns,agc2}_valid (1 runs, 0 rejected) and apm.untouched (1 when every
  // rejected module left its audio as it was). A rate libwebrtc doesn't run them at corrupted
  // memory before #594; the bench call itself runs no APM.
  std::optional<int> apm_rate;

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

// "" when RunCall can run the scenario, else why not, for the caller to report (issue #743: `call two-way`).
std::string UnsupportedReason(const Scenario& s);

const char* ToString(CompareOp op);
bool Evaluate(CompareOp op, double lhs, double rhs);

}  // namespace sm::testbench
