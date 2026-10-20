// Runs one scenario: a complete one-way audio + video call through the
// library over the scenario's emulated links, measured the way a user
// experiences it (audio gaps, video freezes, delay, quality, recovery time).
#pragma once

#include <cstdint>
#include <map>
#include <span>
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
  // The sender's GoogCC updates (`cc v2` only), when RunOptions::bwe_trace is set (issue #149).
  std::vector<cc::BweTraceRow> bwe_trace;
};

struct RunOptions {
  bool timeseries = true;
  bool library_metrics = true;
  bool video_trace = false;  // Fill Report::video_trace.
  bool link_trace = false;  // Fill Report::link_trace (memory grows with the call).
  bool bwe_trace = false;   // Fill Report::bwe_trace.
};

Report RunCall(const Scenario& s, const RunOptions& opt = {});

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
// dropped_encoder,paused,encoded_bytes,pacer_queue_ms. The drop columns are
// libwebrtc's VideoSendStream::Stats reasons. This library maps its drop
// points onto them: the frame dropper is dropped_rate_limiter (libwebrtc's
// kMediaOptimization), congestion-window pushback is dropped_congestion_window
// and a paused encoder is dropped_encoder_queue (libwebrtc's EncoderPaused
// path). It has no counterpart for dropped_capturer, DropDueToSize or
// dropped_encoder, so those stay 0.
std::string VideoTraceCsv(const std::vector<VideoTraceRow>& trace);
std::string LinkTraceCsv(const std::vector<sim::LinkTraceRecord>& trace);
// time_us,event,bitrate_bps,detector,fraction_loss,packets,loss_state,loss_limit_bps,instant_ub_bps;
// event is delay_based, loss_based or target; a column the event lacks is empty (tools/bwe_trace.py).
std::string BweTraceCsv(const std::vector<cc::BweTraceRow>& trace);
// Appends a nack_sent / pli_sent row (or *_recv when `received`) for each generic
// NACK and each PLI or FIR in the compound RTCP packet `rtcp` (issue #151).
void AddRtcpTraceRows(std::span<const uint8_t> rtcp, int64_t time_us, bool received, std::vector<cc::BweTraceRow>& out);
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
