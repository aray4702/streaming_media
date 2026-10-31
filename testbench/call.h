// Runs one scenario: a complete one-way audio + video call through the
// library over the scenario's emulated links, measured the way a user
// experiences it (audio gaps, video freezes, delay, quality, recovery time).
#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "call_common.h"
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
  // Receive-side frame path in the second (issue #437): frames assembled and
  // handed to the frame buffer (libwebrtc's received_frame_counts, counted on insertion),
  // frames decoded (frames_decoded) and frames dropped (frames_dropped:
  // fast-forwards and frames cleared for a keyframe).
  int rx_complete = 0, rx_decoded = 0, rx_dropped = 0;
  // A/V sync at the frames rendered in the second (issue #522), from the
  // shared AvSyncMeter: the mean signed offset (positive: video later than
  // audio), the largest absolute one, and the mean capture-to-render delay.
  // 0 when no frame was measured.
  double av_offset_ms = 0, av_offset_abs_max_ms = 0, video_delay_ms = 0;
};

// One video frame leaving the receive side, for lining up the two stacks'
// render decisions frame by frame (#931). capture_ms names the frame (its
// capture time, the same on both stacks), render_time_ms is the render time the
// receiver gave it (VCMTiming::RenderTime), out_ms is when it was rendered, or
// when it left the receiver without being rendered, and rendered is 1 or 0.
// Times are call-relative ms. This library's rows are every frame the receiver
// hands out (decoded or dropped, before or after the decoder); libwebrtc's are
// the frames its decoder decoded (ModelDecoder::Decode), with out_ms the decode
// time for those its render queue dropped (IncomingVideoStream keeps no record).
struct FrameRenderRow {
  int64_t capture_ms = -1;
  int64_t render_time_ms = -1;
  int64_t out_ms = -1;
  int rendered = 0;
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

// The audio send path (issue #477), the same record for this library and for
// libwebrtc, read from the packet bytes on the forward and reverse links: one
// row per audio packet offered to the forward link (its RTP timestamp and a hash
// of its payload, so equal rows mean both encoders took the same samples with the
// same settings) and one per audio report block delivered to the sender (the
// fraction lost the encoder's packet loss smoother takes). tools/audio_send.py
// compares the two stacks frame by frame. One more row per packet the Opus encoder
// completes records the encoder's own inputs (issue #547): a hash of the PCM it took,
// the bitrate it had, the last allocation and overhead it was given and the last
// fraction lost it was given, so a payload that differs can be traced to the input
// that did.
struct AudioSendRow {
  enum class Kind { kFrame, kReport, kEncode };
  Kind kind = Kind::kFrame;
  int64_t time_us = 0;        // Frame: offered to the forward link. Report: delivered to the sender.
  int32_t seq = -1;           // Frame: RTP sequence number.
  uint32_t rtp_ts = 0;        // Frame: RTP timestamp (the CSV makes it relative to the first frame's).
  int32_t pt = -1;            // Frame: RTP payload type (Opus or RED).
  int32_t payload_bytes = 0;  // Frame: RTP payload, RED header included.
  uint32_t payload_hash = 0;  // Frame: FNV-1a 32 over the same bytes.
  int32_t fraction_lost = -1; // Report: the block's 8-bit fraction lost (RFC 3550 6.4.1). Encode: the last one the
                              // encoder was given (OnReceivedUplinkPacketLossFraction, times 256), -1 before any.
  // Encode rows (seq is the packet's index, payload_hash the FNV-1a 32 of its PCM, PcmHash):
  int32_t bitrate_bps = -1;     // The bitrate the encoder had (AudioEncoder::GetTargetBitrate).
  int32_t allocation_bps = -1;  // The last OnReceivedUplinkAllocation target, -1 if it never had one.
  int32_t overhead_bytes = -1;  // The last OnReceivedOverhead, -1 if it never had one.
};

// FNV-1a 32 over the bytes of 16-bit PCM, continuing from `h`.
inline uint32_t PcmHash(std::span<const int16_t> pcm, uint32_t h = 2166136261u) {
  for (int16_t v : pcm) {
    const auto u = static_cast<uint16_t>(v);
    h = (h ^ (u & 0xff)) * 16777619u;
    h = (h ^ (u >> 8)) * 16777619u;
  }
  return h;
}

// The forward trace's `header` column (#518's drift check): the bytes of RTP packet `packet` on the wire that are not
// its codec payload. That is the UDP/IP overhead (`wire_bytes` minus the packet's size), the fixed header, CSRCs, header
// extensions and RTP padding, plus the RED header when the payload type is `red_pt` (RFC 2198: 4 bytes per redundant
// block and 1 for the primary). -1 when `packet` is not RTP. Both implementations call it on the bytes they send, so
// a packet's codec payload is `size - header` on either side.
int32_t TraceHeaderBytes(std::span<const uint8_t> packet, int64_t wire_bytes, uint8_t red_pt);
// The forward trace's `rtx_room` column (#555's drift check): whether the sender's RTPSender::RtxPacketOverhead
// (rtp_sender.cc:564-595) reserves its MID/RID room for a frame. That is while a report block for the video's media
// SSRC has reached the sender and none yet for its RTX SSRC (ModuleRtpRtcpImpl2::OnReceivedRtcpReportBlocks,
// rtp_rtcp_impl2.cc:694-713), and only when the frame may be retransmitted, which RTPSenderVideo::SendVideo requires
// for any RTX room (rtp_sender_video.cc:619-686): with the scenario's NACK on. Both implementations hand it every RTCP
// packet their sender takes and read Room() when a frame is handed to the sender, so the drift check compares frame
// splits within one reservation phase.
class RtxAckPhase {
 public:
  RtxAckPhase(uint32_t media_ssrc, uint32_t rtx_ssrc, bool retransmission)
      : media_ssrc_(media_ssrc), rtx_ssrc_(rtx_ssrc), retransmission_(retransmission) {}
  // A compound RTCP packet the sender takes: the report blocks of its SRs and RRs.
  void OnRtcp(std::span<const uint8_t> compound);
  // 1 while the media SSRC is acked and the RTX SSRC is not, with retransmission; else 0.
  int32_t Room() const { return retransmission_ && media_acked_ && !rtx_acked_ ? 1 : 0; }

 private:
  uint32_t media_ssrc_, rtx_ssrc_;
  bool retransmission_;
  bool media_acked_ = false, rtx_acked_ = false;
};
// Appends the row of an RTP packet from `audio_ssrc` with a payload.
void TraceAudioRtp(std::span<const uint8_t> packet, int64_t time_us, uint32_t audio_ssrc,
                   std::vector<AudioSendRow>& rows);
// Appends a row per report block about `audio_ssrc` in an RTCP compound packet's SRs and RRs.
void TraceAudioRtcp(std::span<const uint8_t> packet, int64_t time_us, uint32_t audio_ssrc,
                    std::vector<AudioSendRow>& rows);

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
  // Every packet offered to the reverse (feedback) link, when RunOptions::link_trace is set (issue #507).
  std::vector<sim::LinkTraceRecord> reverse_trace;
  // One row per simulated second of video send decisions, when RunOptions::video_trace is set (issue #131).
  std::vector<VideoTraceRow> video_trace;
  // The sender's GoogCC updates, when RunOptions::bwe_trace is set (issue #149).
  std::vector<cc::BweTraceRow> bwe_trace;
  // One row per 10 ms audio pull, when RunOptions::audio_trace is set (issue #62).
  std::vector<AudioTraceRow> audio_trace;
  // The audio send path, when RunOptions::link_trace is set (issue #477).
  std::vector<AudioSendRow> audio_send_trace;
  // The receive side's frames, when RunOptions::video_trace is set (#931).
  std::vector<FrameRenderRow> frame_trace;
};

struct RunOptions {
  bool timeseries = true;
  bool library_metrics = true;
  bool video_trace = false;  // Fill Report::video_trace.
  bool link_trace = false;  // Fill Report::link_trace, Report::reverse_trace and Report::audio_send_trace (memory grows with the call).
  bool bwe_trace = false;   // Fill Report::bwe_trace.
  bool audio_trace = false;  // Fill Report::audio_trace.
};

Report RunCall(const Scenario& s, const RunOptions& opt = {});

// The rate the bench's audio codec, RTP clock, NetEq and playout run at for a
// capture rate: Opus's 48 kHz at every capture rate, as libwebrtc resamples the
// capture to the encoder and stamps Opus packets with a 48 kHz RTP clock
// (audio_encoder_opus.cc kRtpTimestampRateHz; RFC 7587), so its NetEq decodes
// at 48 kHz (issues #298, #477).
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
// `outcome` is delivered, queue_drop, lost or down. Then arrival_us, rtp_ts,
// marker, osn, key (issue #437), header (TraceHeaderBytes, #518) and rtx_room
// (RtxAckPhase, #555).
// The per-second video send trace as CSV (issue #131), identical for both
// implementations: a header line, then one line per second:
// t_s,encoder_target_bps,alloc_target_bps,captured,encoded,dropped_capturer,
// dropped_encoder_queue,dropped_rate_limiter,dropped_congestion_window,
// dropped_encoder,paused,encoded_bytes,pacer_queue_ms,qp_mean,encode_ms_mean,
// encode_width,encode_height,keyframes,keyframe_bytes,rendered,max_render_gap_ms,
// height_mean,qs_checks,qs_qp_high,qs_qp_low,qs_drop_pct,qs_signal,rx_complete,
// rx_decoded,rx_dropped,av_offset_ms,av_offset_abs_max_ms,video_delay_ms. qp_mean, encode_ms_mean and height_mean are means over the second's encoded frames (0 when none), from
// the encoder model both implementations share (testbench/encoder_model.h,
// issue #84). The drop columns are
// libwebrtc's VideoSendStream::Stats reasons. This library maps its drop
// points onto them: the frame dropper is dropped_rate_limiter (libwebrtc's
// kMediaOptimization), congestion-window pushback is dropped_congestion_window
// and a frame dropped for its size or a paused encoder is dropped_encoder_queue
// (libwebrtc's DropDueToSize and EncoderPaused paths): as there, the frame is
// held (pending_frame_) and counted when the next frame releases it, not when
// a resume encodes it. It has no counterpart for dropped_capturer or
// dropped_encoder, so those stay 0.
std::string VideoTraceCsv(const std::vector<VideoTraceRow>& trace);
// Fills each row's av_offset_ms, av_offset_abs_max_ms and video_delay_ms from
// the frames `av_sync` measured in the row's second, [t_ms - 1000, t_ms)
// (issue #522). Both implementations call it, so the columns read the same.
void AddAvSyncToVideoTrace(const AvSyncMeter& av_sync, std::vector<VideoTraceRow>& trace);
std::string LinkTraceCsv(const std::vector<sim::LinkTraceRecord>& trace);
// time_us,event,bitrate_bps,detector,fraction_loss,packets,loss_state,loss_limit_bps,instant_ub_bps;
// event is delay_based, loss_based or target; a column the event lacks is empty (tools/bwe_trace.py).
std::string BweTraceCsv(const std::vector<cc::BweTraceRow>& trace);
// Report::audio_trace as CSV: t_ms,buffer_ms,target_ms,jb_delay_ms,received_ms,
// concealed_ms,accelerate_ms,decelerate_ms (jb_delay_ms empty when no packet
// was emitted in that pull).
std::string AudioTraceCsv(const std::vector<AudioTraceRow>& trace);
std::string AudioSendCsv(const std::vector<AudioSendRow>& rows);
// Report::frame_trace as CSV: capture_ms,render_time_ms,out_ms,rendered (#931).
std::string FrameTraceCsv(const std::vector<FrameRenderRow>& rows);
// Checks rep.scenario's expectations against rep.metrics.
void EvaluateExpectations(Report& rep);

// The video delay breakdown (call_common.h) into rep.metrics.
void AddVideoDelayMetrics(const std::vector<VideoFrameTimes>& frames, Report& rep);

std::string ToJson(const Report& r, bool include_timeseries = true, bool include_library = false);
std::string ToText(const Report& r);

// Names and one-line meanings of every metric in Report::metrics.
const std::vector<std::pair<std::string, std::string>>& MetricCatalog();

}  // namespace sm::testbench
