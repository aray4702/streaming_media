// Runs one scenario: a complete one-way audio + video call through the
// library over the scenario's emulated links, measured the way a user
// experiences it (audio gaps, video freezes, delay, quality, recovery time).
#pragma once

#include <map>
#include <string>
#include <vector>

#include "scenario.h"
#include "sm/cc/send_side_bwe.h"
#include "sm/sim/network_link.h"

namespace sm::testbench {

// One 100 ms sample of the call, for plots.
struct Sample {
  double t_s = 0;
  double capacity_kbps = -1;   // -1 = unlimited, 0 = link down.
  double target_kbps = 0;      // GCC target.
  double send_kbps = 0;        // Everything the sender put on the wire.
  double goodput_kbps = 0;     // Media + repair delivered to the receiver.
  double queue_ms = 0;         // Bottleneck queue delay.
  double rtt_ms = 0;
  double audio_buffer_ms = 0;  // Jitter buffer level.
  double audio_concealed_pct = 0;
  double video_fps = 0;        // Rendered frames in the window, as fps.
  double video_delay_ms = 0;   // Capture -> render of the last rendered frame.
  double fec_pct = 0;          // FEC protection factor.
};

struct CheckResult {
  Expectation expectation;
  double actual = 0;
  bool found = false;
  bool passed = false;
};

// One second of the sender's video decisions (issue #131), the same record for
// this library and for libwebrtc. Counts are for the second ending at `t_ms`.
// Drops use libwebrtc's VideoSendStream::Stats reasons; see VideoTraceCsv.
struct VideoTraceRow {
  int64_t t_ms = 0;
  int64_t encoder_target_bps = 0;  // Rate the encoder is configured with.
  int64_t alloc_target_bps = 0;    // The allocator's video target.
  int captured = 0, encoded = 0;
  int dropped_capturer = 0, dropped_encoder_queue = 0, dropped_rate_limiter = 0, dropped_congestion_window = 0,
      dropped_encoder = 0;
  int paused = 0;  // 1 when the encoder is paused or suspended at the end of the second.
  int64_t encoded_bytes = 0;
  double pacer_queue_ms = 0;
  // Sums over the encoded frames of the second, from testbench/encoder_model.h (issue #84).
  int64_t qp_sum = 0, encode_us_sum = 0;
  // Resolution and keyframes, for lining up switches and keyframe bursts (issue #333).
  int encode_width = 0, encode_height = 0;  // Size of the last frame encoded in the second; 0 when none was.
  int keyframes = 0;                        // Keyframes encoded in the second (periodic and requested).
  int64_t keyframe_bytes = 0;               // Their total size.
  // Receive side (issue #333): frames rendered in the second, and the largest
  // gap from the previous rendered frame to one rendered in the second (0 when
  // none was, so a freeze shows as rendered=0 rows ended by one long gap).
  int rendered = 0;
  double max_render_gap_ms = 0;
  int64_t height_sum = 0;  // Encoded frame heights (quality scaling, issue #84).
  // Quality scaler (issue #347): the checks in the second that observed enough
  // frames (libwebrtc logs them in QualityScaler::CheckQp), and the last one's
  // smoothed QP as the decision reads it, its frame-drop percent when that
  // reported high QP, and its signal (-1 adapt down, +1 adapt up, 0 none).
  // -1 where there was no such check or value.
  int qs_checks = 0;
  int qs_qp_high = -1, qs_qp_low = -1, qs_drop_pct = -1;
  int qs_signal = 0;
};

// One 10 ms audio pull (issue #62), the same record for this library and for
// libwebrtc, read after the pull as AudioReceiveStream::GetStats reports it.
// Durations are in ms so the two stacks' sample rates compare; see AudioTraceCsv.
struct AudioTraceRow {
  int64_t t_ms = 0;
  int buffer_ms = 0;        // jitterBufferMs: NetEq's current_buffer_size_ms (packet buffer + sync buffer).
  int target_ms = 0;        // jitterBufferPreferredMs: DecisionLogic::TargetLevelMs.
  double jb_delay_ms = -1;  // jitterBufferDelay / jitterBufferEmittedCount over this pull; -1 when none was emitted.
  double received_ms = 0, concealed_ms = 0;  // totalSamplesReceived, concealedSamples over this pull.
  double accelerate_ms = 0, decelerate_ms = 0;  // removedSamplesForAcceleration, insertedSamplesForDeceleration.
};

// One event on the RTT path (issue #360), the same record for this library and
// for libwebrtc. `side` is 's' (sender) or 'r' (receiver). `event` is one of:
// sr, rr, xr: an outgoing RTCP packet of that type (one row per packet in the
//   compound datagram; value_ms is the datagram's size in bytes);
// rb_rtt: an RTT the sender computed from a report block (libwebrtc: the video
//   stream's ReportBlockData::last_rtt each time num_rtts grows);
// xr_rtt: an RTT the receiver computed from an XR DLRR (libwebrtc: the video
//   receive stream's round_trip_time each time round_trip_time_measurements grows);
// call_rtt: that side's CallStats RTT (Call::GetStats().rtt_ms) when it changed.
// call_rtt, xr_rtt and libwebrtc's rb_rtt are polled every 10 ms, so their times are on that grid; this
// library's rb_rtt row is at the millisecond the report arrived.
struct RttTraceRow {
  int64_t t_ms = 0;
  char side = 's';
  std::string event;
  double value_ms = -1;
};

struct Report {
  Scenario scenario;
  std::map<std::string, double> metrics;  // User-experience metrics (see README).
  std::vector<CheckResult> checks;
  bool passed = true;
  std::vector<Sample> timeseries;
  std::map<std::string, double> library;  // Every module's own metrics.
  std::map<std::string, int> events;      // Event counts by module/name.
  // Every packet offered to the forward link, when RunOptions::link_trace is set (issue #125).
  std::vector<sim::LinkTraceRecord> link_trace;
  // One row per simulated second of video send decisions, when RunOptions::video_trace is set (issue #131).
  std::vector<VideoTraceRow> video_trace;
  // The sender's GoogCC updates, when RunOptions::bwe_trace is set (issue #149).
  std::vector<cc::BweTraceRow> bwe_trace;
  // One row per 10 ms audio pull, when RunOptions::audio_trace is set (issue #62).
  std::vector<AudioTraceRow> audio_trace;
  // RTCP send times and RTT samples on both sides, when RunOptions::rtt_trace is set (issue #360).
  std::vector<RttTraceRow> rtt_trace;
};

struct RunOptions {
  bool timeseries = true;
  bool library_metrics = true;
  bool video_trace = false;  // Fill Report::video_trace.
  bool link_trace = false;  // Fill Report::link_trace (memory grows with the call).
  bool bwe_trace = false;   // Fill Report::bwe_trace.
  bool audio_trace = false;  // Fill Report::audio_trace.
  bool rtt_trace = false;    // Fill Report::rtt_trace.
};

Report RunCall(const Scenario& s, const RunOptions& opt = {});

// The rate the bench's audio codec, RTP clock, NetEq and playout run at for a
// capture rate: the capture rate at 8, 16, 32 and 48 kHz, else Opus's 48 kHz
// RTP clock, as libwebrtc resamples the capture to the encoder (issue #298).
int AudioCodecRate(int capture_rate);

// Steps shared with other call implementations (compare/), so every
// implementation reports the same metrics computed the same way.
// Adds cc.target_avg_kbps, cc.goodput_kbps, cc.utilization_pct,
// cc.max_settle_ms and cc.unsettled from rep.timeseries. `demand_kbps` is
// the media's maximum wire rate (what the call would send on an unlimited link).
void AddCcMetrics(const Scenario& s, double demand_kbps, Report& rep);
// Also adds cc.target_max_drop_pct and cc.send_over_target_p95_pct.
// Adds the forward link's per-kind counters (issue #117): net.<kind>_kbps
// (wire rate sent over the call) for audio, video, rtx, padding and fec, and
// net.<kind>_loss_pct (random, burst, outage and queue drops) for audio,
// video, rtx and padding. `stats` come from a link whose packets the caller
// tagged with sim::LinkKind; both implementations tag the same way.
void AddLinkKindMetrics(const sim::LinkStats& stats, const Scenario& s, Report& rep);
// The forward-link trace as CSV (issue #125), identical for both
// implementations: a header line, then one line per packet offered to the
// link: time_us,size,kind,seq,queue_bytes,outcome. `kind` is audio, video,
// rtx, padding, fec or other (RTCP, cross traffic); `seq` is the RTP sequence
// number or -1; `queue_bytes` is the bottleneck queue before the packet;
// `outcome` is delivered, queue_drop, lost or down.
// The per-second video send trace as CSV (issue #131), identical for both
// implementations: a header line, then one line per second:
// t_s,encoder_target_bps,alloc_target_bps,captured,encoded,dropped_capturer,
// dropped_encoder_queue,dropped_rate_limiter,dropped_congestion_window,
// dropped_encoder,paused,encoded_bytes,pacer_queue_ms,qp_mean,encode_ms_mean,
// encode_width,encode_height,keyframes,keyframe_bytes,rendered,max_render_gap_ms,
// height_mean. qp_mean, encode_ms_mean and height_mean are means over the second's encoded frames (0 when none), from
// the encoder model both implementations share (testbench/encoder_model.h,
// issue #84). The drop columns are
// libwebrtc's VideoSendStream::Stats reasons. This library maps its drop
// points onto them: the frame dropper is dropped_rate_limiter (libwebrtc's
// kMediaOptimization), congestion-window pushback is dropped_congestion_window
// and a paused encoder is dropped_encoder_queue (libwebrtc's EncoderPaused
// path). It has no counterpart for dropped_capturer, DropDueToSize or
// dropped_encoder, so those stay 0.
std::string VideoTraceCsv(const std::vector<VideoTraceRow>& trace);
// The rate the sender's allocator reserves for audio (zero without audio).
// `allocator v2` reserves what libwebrtc's AudioSendStream registers with
// BitrateAllocator (audio_send_stream.cc GetMinMaxBitrateConstraints): the codec
// rate plus one packet's overhead per 20 ms Opus frame, where the overhead is
// transport (UDP/IPv4, 28 B) + RTPSender::ExpectedPerPacketOverhead (12 B header
// + 8 B transport-cc extension block) = 48 B, so 51.2 kbps at 32 kbps (issue
// #196). v1 counts a 4 B extension, 49.6 kbps.
DataRate AudioAllocationRate(const Scenario& s);
std::string LinkTraceCsv(const std::vector<sim::LinkTraceRecord>& trace);
// time_us,event,bitrate_bps,detector,fraction_loss,packets,loss_state,loss_limit_bps,instant_ub_bps;
// event is delay_based, loss_based or target; a column the event lacks is empty (tools/bwe_trace.py).
std::string BweTraceCsv(const std::vector<cc::BweTraceRow>& trace);
// Report::audio_trace as CSV: t_ms,buffer_ms,target_ms,jb_delay_ms,received_ms,
// concealed_ms,accelerate_ms,decelerate_ms (jb_delay_ms empty when no packet
// was emitted in that pull).
std::string AudioTraceCsv(const std::vector<AudioTraceRow>& trace);
// Report::rtt_trace as CSV: t_ms,side,event,value_ms.
std::string RttTraceCsv(const std::vector<RttTraceRow>& trace);
// Appends one sr, rr or xr row per such packet in an outgoing RTCP datagram
// (other packet types and RTP datagrams add nothing).
void TraceRtcpOut(const std::vector<uint8_t>& datagram, int64_t t_ms, char side, std::vector<RttTraceRow>& rows);
// Checks rep.scenario's expectations against rep.metrics.
void EvaluateExpectations(Report& rep);

// Times of one video frame on its way from capture to render (issue #60).
// An unset time is infinite or minus infinity and the frame is not counted.
struct VideoFrameTimes {
  Timestamp capture = Timestamp::MinusInfinity();
  Timestamp submitted = Timestamp::MinusInfinity();   // Handed to the sender (after encoding).
  Timestamp last_sent = Timestamp::MinusInfinity();   // Last media packet left the pacer.
  Timestamp complete = Timestamp::MinusInfinity();    // Last media or RTX packet of the frame arrived.
  Timestamp rendered = Timestamp::MinusInfinity();
};

// Adds video.delay_encode_ms (mean), video.delay_pacer_p95_ms,
// video.delay_network_p95_ms and video.delay_jitter_buffer_p95_ms from the
// frames that were rendered. Adds nothing when no frame was.
void AddVideoDelayMetrics(const std::vector<VideoFrameTimes>& frames, Report& rep);

std::string ToJson(const Report& r, bool include_timeseries = true, bool include_library = false);
std::string ToText(const Report& r);

// Names and one-line meanings of every metric in Report::metrics.
const std::vector<std::pair<std::string, std::string>>& MetricCatalog();

}  // namespace sm::testbench
