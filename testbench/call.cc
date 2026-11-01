#include "call.h"

#include "call_common.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <deque>
#include <functional>
#include <optional>
#include <set>
#include <string_view>
#include <unordered_map>

#include "ai_source.h"
#include "encoder_model.h"
#include "opus_decoder.h"
#include "turn_detector.h"
#include "opus_encoder.h"
#include "sm/audio/aec3.h"
#include "sm/audio/agc2.h"
#include "sm/audio/ns.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/random.h"
#include "sm/core/rtp_packet.h"
#include "sm/core/seq_num.h"
#include "sm/rtp/fake_h264.h"
#include "sm/rtp/h264.h"
#include "sm/rtp/session.h"
#include "sm/rtp/video_send_controller.h"
#include "sm/sim/network_link.h"
#include "sm/sim/signals.h"
#include "sm/video/freeze_detector.h"

namespace sm::testbench {
namespace {

constexpr uint32_t kAudioSsrc = 1, kVideoSsrc = 2, kRtxSsrc = 3;
constexpr uint32_t kSenderRtcpSsrc = 100, kReceiverRtcpSsrc = 200;
constexpr uint8_t kAudioPt = 111, kAudioRedPt = 63, kVideoPt = 96, kRtxPt = 97;
constexpr int64_t kUdpIpOverhead = 28;

// What a datagram is on the link, for the accounting only: the receiver
// session works from the bytes.
enum class Kind : uint8_t { kAudio, kVideo, kRtx, kFec, kPadding, kRtcp, kCross };
struct Fwd {
  Kind kind = Kind::kCross;
  std::vector<uint8_t> bytes;
};
struct Rev {
  std::vector<uint8_t> bytes;
};

// One gauge of a session's exported metrics (-1 if it has none), so the bench reads library state
// without a test-only accessor.
double SessionGauge(const rtp::Session& session, std::string_view name) {
  struct OneGauge : MetricsWriter {
    std::string_view name;
    double value = -1;
    void Counter(std::string_view, uint64_t) override {}
    void Gauge(std::string_view n, double v) override {
      if (n == name) value = v;
    }
    void Hist(std::string_view, const Histogram&) override {}
  } w;
  w.name = name;
  session.ExportMetrics(w);
  return w.value;
}

sim::LinkKind LinkKindOf(Kind k) {
  switch (k) {
    case Kind::kAudio: return sim::LinkKind::kAudio;
    case Kind::kVideo: return sim::LinkKind::kVideo;
    case Kind::kRtx: return sim::LinkKind::kRtx;
    case Kind::kFec: return sim::LinkKind::kFec;
    case Kind::kPadding: return sim::LinkKind::kPadding;
    case Kind::kRtcp:
    case Kind::kCross: break;
  }
  return sim::LinkKind::kOther;
}

Kind ToKind(rtp::OutgoingKind k) {
  switch (k) {
    case rtp::OutgoingKind::kAudio: return Kind::kAudio;
    case rtp::OutgoingKind::kVideo: return Kind::kVideo;
    case rtp::OutgoingKind::kRtx: return Kind::kRtx;
    case rtp::OutgoingKind::kFec: return Kind::kFec;
    case rtp::OutgoingKind::kPadding: return Kind::kPadding;
    case rtp::OutgoingKind::kRtcp: return Kind::kRtcp;
  }
  return Kind::kVideo;
}

// RTP fixed header fields (RFC 3550 section 5.1), read from the bytes of a
// datagram that is known to be RTP.
uint16_t RtpSeq(const std::vector<uint8_t>& b) { return static_cast<uint16_t>((b[2] << 8) | b[3]); }
uint32_t RtpTimestamp(const std::vector<uint8_t>& b) {
  return (static_cast<uint32_t>(b[4]) << 24) | (static_cast<uint32_t>(b[5]) << 16) | (static_cast<uint32_t>(b[6]) << 8) | b[7];
}

// The video media packets the receiver hands on: those that arrived from the link (the RepairMeter has them
// already, since each arrival is recorded before the receiver takes it) and those its ULPFEC receiver recovered.
class FecRecoveredSeqs : public rtp::VideoMediaObserver {
 public:
  explicit FecRecoveredSeqs(RepairMeter& meter) : meter_(meter) {}
  void OnVideoMediaPacket(uint16_t seq, Timestamp) override { meter_.OnFecRecovered(seq); }

 private:
  RepairMeter& meter_;
};

}  // namespace

namespace {
const std::vector<std::pair<std::string, std::string>>& OneWayMetricCatalog() {
  static const std::vector<std::pair<std::string, std::string>> k = {
      {"audio.concealed_pct", "Concealed samples in percent of the samples played (concealedSamples / "
                              "totalSamplesReceived, after the first packet)"},
      {"audio.interruptions", "Concealment events of 150 ms or more (WebRTC interruptionCount)"},
      {"audio.interruption_ms", "Total duration of those interruptions"},
      {"audio.longest_interruption_ms", "Longest concealment event (its concealed samples)"},
      {"audio.jb_delay_p50_ms", "Median jitter buffer delay per 10 ms of playout (jitterBufferDelay / "
                                 "jitterBufferEmittedCount: packet buffer waiting time, as sm_compare reads libwebrtc's)"},
      {"audio.jb_delay_p95_ms", "95th percentile of the same"},
      {"audio.net_delay_p95_ms", "95th percentile capture -> arrival delay of audio packets (pacer + network)"},
      {"audio.mouth_to_ear_p95_ms", "Upper estimate of mouth-to-ear delay: net_delay_p95 + jb_delay_p95"},
      {"audio.stretch_pct", "Share of playout spent accelerating or decelerating (audible as tempo change)"},
      {"audio.late_discards", "Packets that arrived after their playout time"},
      {"audio.time_to_first_ms", "Call start -> first audio packet in the jitter buffer"},
      {"video.frames_rendered", "Frames shown to the user"},
      {"video.render_fps", "Average rendered frame rate over the call"},
      {"video.render_ratio_pct", "Rendered frames / encoded frames"},
      {"video.freeze_count", "Freezes: render gap > max(3 x avg frame duration, avg + 150 ms) (WebRTC freezeCount)"},
      {"video.freeze_total_ms", "Total frozen time, including a freeze still in progress at the end"},
      {"video.longest_freeze_ms", "Longest freeze or tail gap (or call length if nothing rendered); gaps of 5 s or more are pauses and are not included"},
      {"video.pause_count", "Pauses: render gaps of 5 s or more (libwebrtc VideoQualityObserver pauses), including one still in progress at the end"},
      {"video.pause_total_ms", "Total time in pauses: the video blackout that freeze_total_ms does not count"},
      {"video.e2e_delay_p50_ms", "Median capture -> render delay"},
      {"video.e2e_delay_p95_ms", "95th percentile capture -> render delay"},
      {"video.delay_encode_ms", "Mean capture -> frame handed to the sender (the bench's encoder is instantaneous, so 0)"},
      {"video.delay_pacer_p95_ms", "95th percentile frame handed to the sender -> its last media packet leaves the pacer"},
      {"video.delay_network_p95_ms", "95th percentile last media packet sent -> frame complete at the receiver (propagation, queue, jitter and waits for RTX)"},
      {"video.delay_jitter_buffer_p95_ms", "95th percentile frame complete -> rendered (video jitter buffer and render timing)"},
      {"video.time_to_first_frame_ms", "Call start -> first rendered frame"},
      {"video.avg_kbps", "Average encoded video bitrate (quality proxy)"},
      {"video.qp_mean", "Mean QP of encoded frames, from the encoder model both implementations share (testbench/encoder_model.h; H.264 0..51)"},
      {"video.encode_height_mean", "Mean height of encoded frames (the captured height unless quality scaling adapts it, issue #84)"},
      {"video.keyframes", "Keyframes encoded"},
      {"video.keyframe_requests", "PLI/FIR received by the sender"},
      {"video.encoder_dropped", "Frames skipped by the frame dropper or a paused encoder"},
      {"video.encoder_target_avg_kbps", "Mean rate the encoder is configured with (after the bitrate adjuster), sampled at each captured frame (issue #466)"},
      {"video.undecodable_frames", "Frames received but never decoded: discarded for a missing reference, fast-forwarded or cleared (libwebrtc frames_dropped)"},
      {"video.residual_loss_pct", "Video media packets sent that never arrived, as media or RTX, and were not "
                                  "recovered by FEC (RepairMeter, as sm_compare)"},
      {"sync.av_offset_p50_ms", "Median A/V offset at each rendered frame: capture time of the audio playing minus the frame's (positive: video later than audio)"},
      {"sync.av_offset_p95_ms", "95th percentile of the absolute A/V offset at each rendered frame (lip sync; audio and video calls only)"},
      {"sync.av_offset_max_ms", "Largest absolute A/V offset at a rendered frame"},
      {"cc.target_avg_kbps", "Average GCC target bitrate"},
      {"cc.goodput_kbps", "Average bitrate of media delivered, each RTP sequence number counted once (original or RTX)"},
      {"cc.utilization_pct", "Delivered bitrate / usable capacity, over samples where the link is up and finite"},
      {"cc.queue_p95_ms", "95th percentile bottleneck queue delay (bufferbloat)"},
      {"cc.queue_max_ms", "Maximum bottleneck queue delay"},
      {"cc.queue_over_200ms_pct", "Share of time the bottleneck queue exceeded 200 ms"},
      {"cc.send_over_target_ms", "Time after the first 2 s that the send rate (100 ms windows, all traffic) exceeded 1.5 x the GCC target"},
      {"cc.send_over_target_p95_pct", "95th percentile of how far the send rate (100 ms windows after the first 2 s) is above the GCC target, in % of the target (0 when at or below)"},
      {"cc.target_max_drop_pct", "Largest fall of the target below its peak of the preceding 5 s, outside the first 5 s and the 5 s after each link change"},
      {"cc.max_settle_ms", "Longest time after a link change until the target settles in [0.5, 1.0] x usable capacity"},
      {"cc.unsettled", "Link changes after which the target never settled before the next change"},
      {"cc.rtt_p50_ms", "Median of the sender's CallStats RTT (the smoothed average of the 1 s RTCP RTT reports, as libwebrtc's Call::GetStats().rtt_ms), sampled every 100 ms once it has a value"},
      {"net.loss_pct", "Forward link loss (random, burst, outage and queue drops)"},
      {"net.audio_loss_pct", "Forward link loss of audio packets (random, burst, outage and queue drops)"},
      {"net.video_loss_pct", "Forward link loss of video media packets"},
      {"net.rtx_loss_pct", "Forward link loss of RTX packets that carry a retransmission"},
      {"net.padding_loss_pct", "Forward link loss of padding-only packets (RTP padding, or RTX with no payload)"},
      {"net.<kind>_queue_drops", "Packets of a kind (audio, video, rtx, padding) dropped by the forward link's drop-tail queue"},
      {"net.<kind>_size_p50_b / _p90_b / _max_b", "Wire size (payload, RTP/RTCP headers and the modelled UDP/IP overhead) "
                                                  "of the packets of a kind offered to the forward link: median, 90th percentile "
                                                  "(nearest rank) and maximum"},
      {"net.audio_kbps", "Audio sent on the forward link, wire rate averaged over the call"},
      {"net.audio_rtp_clock_hz", "RTP clock of the audio packets on the forward link: timestamp advance per second "
                                 "sent, to the nearest kHz (Opus: 48000, RFC 7587)"},
      {"net.audio_start_seq", "RTP sequence number of the first audio packet on the forward link (libwebrtc starts "
                              "each stream at a random 1-32767; #514)"},
      {"net.video_start_seq", "RTP sequence number of the first video media packet on the forward link (#514)"},
      {"net.audio_first_marker", "1 if the first audio packet on the forward link has the marker bit "
                                 "(RTPSenderAudio::MarkerBit; #514)"},
      {"net.padding_media_ssrc_packets", "Padding-only packets on the audio or video media SSRC, on the forward "
                                         "link (RTPSender::GeneratePadding without RTX; #514)"},
      {"net.padding_rtx_ssrc_packets", "Padding-only packets on the RTX SSRC, on the forward link (#514)"},
      {"net.padding_distinct_sizes", "Number of distinct wire sizes of the padding-only packets on the forward link "
                                     "(audio padding is sized by the bytes the pacer asks for; #514)"},
      {"net.video_kbps", "Video media sent on the forward link, wire rate averaged over the call"},
      {"net.rtx_kbps", "RTX retransmissions sent on the forward link, wire rate averaged over the call"},
      {"net.padding_kbps", "Padding-only packets sent on the forward link, wire rate averaged over the call"},
      {"net.fec_kbps", "FEC packets sent on the forward link, wire rate averaged over the call"},
      {"repair.overhead_pct", "(RTX + FEC packet bytes) / (audio + video media packet bytes), whole RTP packets"},
      {"repair.nack_recovered", "RTX packets that arrived before their original (from the link, as sm_compare)"},
      {"repair.fec_recovered", "Packets recovered by FEC (the ULPFEC receiver's counter)"},
      {"rtcp.audio_report_blocks", "Report blocks about the audio SSRC in the RTCP the sender received (SRs and RRs; "
                                   "libwebrtc's audio channel reports every 5 s, about 6 in 30 s; issue #484)"},
      {"conv.responses", "Two-way call with turns: AI replies heard at the user (#753)"},
      {"conv.response_delay_p50_ms", "Two-way call with turns: median of what the transport added to the AI's response: the user's last "
                                     "speech sample captured (the end of the scripted turn) to the AI's first sample played at the user, less the "
                                     "AI's own decision time: the thinking time (`after`) and the 600 ms of silence its endpointer waits for. "
                                     "With `ai_source` it includes the source's first-frame delay (#753)"},
      {"conv.response_delay_p95_ms", "95th percentile of the same (#753)"},
      {"conv.barge_ins", "Two-way call with turns: barge-ins the AI detected while speaking and stopped for (#754)"},
      {"conv.barge_in_stop_ms", "Two-way call with turns: longest time from the user starting to talk over the AI (the scripted barge-in) to "
                                "the AI's audio stopping at the user's ear (#754); audio already in the AI-to-user path keeps playing"},
      {"ai.turns", "Two-way call with `ai_source`: AI replies the source generated (#758)"},
      {"ai.queue_ahead_max_ms", "Two-way call with `ai_source`: most media generated but not yet sent, i.e. how far the AI's app-side queue got ahead of real time (#758)"},
      {"ai.first_frame_delay_max_ms", "Two-way call with `ai_source`: longest time from an AI turn's start to its first frame handed to the sender (#758)"},
      {"ai.underruns", "Two-way call with `ai_source`: frames sent late because generation fell behind real time (#758)"},
  };
  return k;
}
}  // namespace

const std::vector<std::pair<std::string, std::string>>& MetricCatalog() {
  // A two-way call (#746, #747) reports each direction's metrics as up.* (user to AI) and
  // down.* (AI to user); its forward link is the up direction's, its reverse link the down direction's.
  static const std::vector<std::pair<std::string, std::string>> k = [] {
    std::vector<std::pair<std::string, std::string>> all = OneWayMetricCatalog();
    for (const char* dir : {"up.", "down."}) {
      for (const auto& [name, meaning] : OneWayMetricCatalog()) {
        for (const char* group : {"audio.", "video.", "sync.", "cc.", "net.", "rtcp.", "repair."}) {
          if (name.rfind(group, 0) == 0) {
            all.emplace_back(dir + name, std::string(dir[0] == 'u' ? "Two-way call, user to AI: " : "Two-way call, AI to user: ") + meaning);
          }
        }
      }
    }
    return all;
  }();
  return k;
}

void AddAvSyncToVideoTrace(const AvSyncMeter& av_sync, std::vector<VideoTraceRow>& trace) {
  for (VideoTraceRow& r : trace) {
    const AvSyncMeter::Second* sec = av_sync.InSecond(r.t_ms / 1000 - 1);
    if (sec == nullptr || sec->frames == 0) continue;
    r.av_offset_ms = sec->offset_sum_ms / sec->frames;
    r.av_offset_abs_max_ms = sec->offset_abs_max_ms;
    r.video_delay_ms = sec->video_delay_sum_ms / sec->frames;
  }
}

std::string VideoTraceCsv(const std::vector<VideoTraceRow>& trace) {
  std::string out =
      "t_s,encoder_target_bps,alloc_target_bps,captured,encoded,dropped_capturer,dropped_encoder_queue,"
      "dropped_rate_limiter,dropped_congestion_window,dropped_encoder,paused,encoded_bytes,pacer_queue_ms,qp_mean,"
      "encode_ms_mean,encode_width,encode_height,keyframes,keyframe_bytes,rendered,max_render_gap_ms,height_mean,qs_checks,"
      "qs_qp_high,qs_qp_low,qs_drop_pct,qs_signal,rx_complete,rx_decoded,rx_dropped,av_offset_ms,av_offset_abs_max_ms,"
      "video_delay_ms\n";
  char ms[96], gap[64], av[96];
  for (const auto& r : trace) {
    const double n = r.encoded > 0 ? r.encoded : 1;
    std::snprintf(ms, sizeof(ms), "%.1f,%.1f,%.2f", r.pacer_queue_ms, static_cast<double>(r.qp_sum) / n,
                  static_cast<double>(r.encode_us_sum) / 1000.0 / n);
    std::snprintf(gap, sizeof(gap), "%.1f,%.1f", r.max_render_gap_ms, static_cast<double>(r.height_sum) / n);
    std::snprintf(av, sizeof(av), "%.1f,%.1f,%.1f", r.av_offset_ms, r.av_offset_abs_max_ms, r.video_delay_ms);
    out += std::to_string(r.t_ms / 1000) + "," + std::to_string(r.encoder_target_bps) + "," +
           std::to_string(r.alloc_target_bps) + "," + std::to_string(r.captured) + "," + std::to_string(r.encoded) + "," +
           std::to_string(r.dropped_capturer) + "," + std::to_string(r.dropped_encoder_queue) + "," +
           std::to_string(r.dropped_rate_limiter) + "," + std::to_string(r.dropped_congestion_window) + "," +
           std::to_string(r.dropped_encoder) + "," + std::to_string(r.paused) + "," + std::to_string(r.encoded_bytes) +
           "," + ms + "," + std::to_string(r.encode_width) + "," + std::to_string(r.encode_height) + "," +
           std::to_string(r.keyframes) + "," + std::to_string(r.keyframe_bytes) + "," + std::to_string(r.rendered) + "," +
           gap + "," + std::to_string(r.qs_checks) + "," + std::to_string(r.qs_qp_high) + "," +
           std::to_string(r.qs_qp_low) + "," + std::to_string(r.qs_drop_pct) + "," + std::to_string(r.qs_signal) + "," +
           std::to_string(r.rx_complete) + "," + std::to_string(r.rx_decoded) + "," + std::to_string(r.rx_dropped) + "," + av + "\n";
  }
  return out;
}

// One kSync row (issue #895): both receive streams' Syncable::GetInfo, as RtpStreamsSynchronizer::UpdateDelay
// reads them, and the synchronizer's last relative delay and filtered diff. `ts0` keeps the first RTP timestamp
// of each stream, so the rows show RTP time from the stream's start.
void AppendSyncTraceRow(const rtp::Session& receiver, Timestamp now, TimeDelta rx0, SyncTs0& ts0,
                        std::vector<cc::BweTraceRow>& out) {
  const sync::Syncable* a = receiver.sync_audio();
  const sync::Syncable* v = receiver.sync_video();
  if (!a || !v) return;
  const std::optional<sync::Syncable::Info> ai = a->GetInfo();
  const std::optional<sync::Syncable::Info> vi = v->GetInfo();
  if (!ai || !vi) return;
  if (!ts0.audio) ts0.audio = ai->latest_received_capture_rtp_timestamp;
  if (!ts0.video) ts0.video = vi->latest_received_capture_rtp_timestamp;
  cc::BweTraceRow r;
  r.time_us = now.us();
  r.kind = cc::BweTraceRow::Kind::kSync;
  r.bitrate_bps = ai->current_delay.ms();
  r.packets = static_cast<int>(vi->current_delay.ms());
  r.loss_limit_bps = (ai->latest_receive_time - rx0).ms();  // In call time (issue #897).
  r.instant_ub_bps = (vi->latest_receive_time - rx0).ms();
  r.send_ms = static_cast<uint32_t>(ai->latest_received_capture_rtp_timestamp - *ts0.audio) / 48.0;  // Opus, 48 kHz.
  r.arrival_ms = static_cast<uint32_t>(vi->latest_received_capture_rtp_timestamp - *ts0.video) / 90.0;
  if (const sync::RtpStreamsSynchronizer* sy = receiver.av_sync()) {
    r.modified_trend = sy->last_relative_delay_ms();
    r.threshold = sy->last_avg_diff_ms();
  }
  out.push_back(r);
}

std::string BweTraceCsv(const std::vector<cc::BweTraceRow>& trace) {
  static const char* const kKinds[] = {"delay_based", "loss_based", "target", "detector", "rxtiming", "pacer", "sync"};
  static const char* const kDetector[] = {"normal", "underusing", "overusing"};
  static const char* const kLossState[] = {"increasing", "increase_using_padding", "decreasing", "delay_based_estimate"};
  std::string out =
      "time_us,event,bitrate_bps,detector,fraction_loss,packets,loss_state,loss_limit_bps,instant_ub_bps,"
      "modified_trend,threshold,send_delta_ms,recv_delta_ms,send_ms,arrival_ms\n";
  auto num = [](int64_t v) { return v < 0 ? std::string() : std::to_string(v); };
  auto real = [](double v) {
    if (std::isnan(v)) return std::string();
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return std::string(buf);
  };
  for (const auto& r : trace) {
    out += std::to_string(r.time_us) + "," + kKinds[static_cast<size_t>(r.kind)] + "," + std::to_string(r.bitrate_bps) + "," +
           (r.detector < 0 ? "" : kDetector[r.detector]) + "," + num(r.fraction_loss) + "," + num(r.packets) + "," +
           (r.loss_state < 0 ? "" : kLossState[r.loss_state]) + "," + num(r.loss_limit_bps) + "," + num(r.instant_ub_bps) + "," +
           real(r.modified_trend) + "," + real(r.threshold) + "," + real(r.send_delta_ms) + "," + real(r.recv_delta_ms) + "," +
           real(r.send_ms) + "," + real(r.arrival_ms) + "\n";
  }
  return out;
}

std::string AudioTraceCsv(const std::vector<AudioTraceRow>& trace) {
  std::string out = "t_ms,buffer_ms,target_ms,jb_delay_ms,received_ms,concealed_ms,accelerate_ms,decelerate_ms\n";
  auto real = [](double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4g", v);
    return std::string(buf);
  };
  for (const auto& r : trace) {
    out += std::to_string(r.t_ms) + "," + std::to_string(r.buffer_ms) + "," + std::to_string(r.target_ms) + "," +
           (r.jb_delay_ms < 0 ? std::string() : real(r.jb_delay_ms)) + "," + real(r.received_ms) + "," +
           real(r.concealed_ms) + "," + real(r.accelerate_ms) + "," + real(r.decelerate_ms) + "\n";
  }
  return out;
}

int32_t TraceHeaderBytes(std::span<const uint8_t> p, int64_t wire_bytes, uint8_t red_pt) {
  if (p.size() < 12 || (p[0] >> 6) != 2) return -1;
  size_t off = 12 + 4 * static_cast<size_t>(p[0] & 0x0f);
  if (p[0] & 0x10) {  // Header extensions.
    if (off + 4 > p.size()) return -1;
    off += 4 + 4 * (static_cast<size_t>(p[off + 2]) << 8 | p[off + 3]);
  }
  size_t end = p.size();
  if (p[0] & 0x20) end -= std::min<size_t>(end, p[p.size() - 1]);  // RTP padding.
  if (off > end) return -1;
  if ((p[1] & 0x7f) == red_pt) {
    while (off < end && (p[off] & 0x80)) off += 4;  // Redundant block headers.
    off = std::min(off + 1, end);                    // The primary block's header.
  }
  return static_cast<int32_t>(wire_bytes - static_cast<int64_t>(end - off));
}

void RtxAckPhase::OnRtcp(std::span<const uint8_t> p) {
  for (size_t off = 0; off + 4 <= p.size();) {
    if ((p[off] >> 6) != 2) return;
    const size_t len = 4 * ((static_cast<size_t>(p[off + 2]) << 8 | p[off + 3]) + 1);
    if (off + len > p.size()) return;
    const uint8_t pt = p[off + 1];
    // SR (200): the blocks follow the sender SSRC and the 20-byte sender info; RR (201): the sender SSRC.
    if (pt == 200 || pt == 201) {
      const size_t count = p[off] & 0x1f;
      size_t b = off + (pt == 200 ? 28 : 8);
      for (size_t i = 0; i < count && b + 24 <= off + len; ++i, b += 24) {
        const uint32_t ssrc = static_cast<uint32_t>(p[b]) << 24 | p[b + 1] << 16 | p[b + 2] << 8 | p[b + 3];
        media_acked_ |= ssrc == media_ssrc_;
        rtx_acked_ |= ssrc == rtx_ssrc_;
      }
    }
    off += len;
  }
}

void TraceAudioRtp(std::span<const uint8_t> p, int64_t time_us, uint32_t audio_ssrc, std::vector<AudioSendRow>& rows) {
  if (p.size() < 12 || (p[0] >> 6) != 2) return;
  auto u32 = [&](size_t i) {
    return static_cast<uint32_t>(p[i]) << 24 | static_cast<uint32_t>(p[i + 1]) << 16 |
           static_cast<uint32_t>(p[i + 2]) << 8 | p[i + 3];
  };
  if (u32(8) != audio_ssrc) return;
  size_t off = 12 + 4 * static_cast<size_t>(p[0] & 0x0f);
  if (p[0] & 0x10) {  // Header extensions.
    if (off + 4 > p.size()) return;
    off += 4 + 4 * (static_cast<size_t>(p[off + 2]) << 8 | p[off + 3]);
  }
  size_t end = p.size();
  if (p[0] & 0x20) end -= std::min<size_t>(end, p.back());  // RTP padding.
  if (off >= end) return;  // Padding only.
  AudioSendRow r;
  r.time_us = time_us;
  r.seq = static_cast<int32_t>(p[2]) << 8 | p[3];
  r.rtp_ts = u32(4);
  r.pt = p[1] & 0x7f;
  r.payload_bytes = static_cast<int32_t>(end - off);
  uint32_t h = 2166136261u;
  for (size_t i = off; i < end; ++i) h = (h ^ p[i]) * 16777619u;
  r.payload_hash = h;
  rows.push_back(r);
}

void TraceAudioRtcp(std::span<const uint8_t> p, int64_t time_us, uint32_t audio_ssrc, std::vector<AudioSendRow>& rows) {
  auto u32 = [&](size_t i) {
    return static_cast<uint32_t>(p[i]) << 24 | static_cast<uint32_t>(p[i + 1]) << 16 |
           static_cast<uint32_t>(p[i + 2]) << 8 | p[i + 3];
  };
  for (size_t off = 0; off + 4 <= p.size();) {
    if ((p[off] >> 6) != 2) return;
    const size_t len = 4 * ((static_cast<size_t>(p[off + 2]) << 8 | p[off + 3]) + 1);
    if (off + len > p.size()) return;
    const int type = p[off + 1];
    if (type == 200 || type == 201) {  // SR, RR: the report blocks follow the sender info.
      const size_t first = off + (type == 200 ? 28 : 8);
      for (size_t b = 0; b < static_cast<size_t>(p[off] & 0x1f); ++b) {
        const size_t at = first + 24 * b;
        if (at + 24 > off + len) break;
        if (u32(at) != audio_ssrc) continue;
        AudioSendRow r;
        r.kind = AudioSendRow::Kind::kReport;
        r.time_us = time_us;
        r.fraction_lost = p[at + 4];
        rows.push_back(r);
      }
    }
    off += len;
  }
}

std::string FrameTraceCsv(const std::vector<FrameRenderRow>& rows) {
  std::string out = "capture_ms,render_time_ms,out_ms,rendered\n";
  for (const auto& r : rows)
    out += std::to_string(r.capture_ms) + "," + std::to_string(r.render_time_ms) + "," + std::to_string(r.out_ms) +
           "," + std::to_string(r.rendered) + "\n";
  return out;
}

std::string AudioSendCsv(const std::vector<AudioSendRow>& rows) {
  std::string out = "kind,time_us,seq,rtp_ts,pt,payload_bytes,payload_hash,fraction_lost,bitrate_bps,allocation_bps,"
                    "overhead_bytes\n";
  std::optional<uint32_t> ts0;
  for (const auto& r : rows) {
    if (r.kind == AudioSendRow::Kind::kReport) {
      out += "report," + std::to_string(r.time_us) + ",-1,-1,-1,-1,-1," + std::to_string(r.fraction_lost) + ",-1,-1,-1\n";
      continue;
    }
    if (r.kind == AudioSendRow::Kind::kEncode) {
      char hash[16];
      std::snprintf(hash, sizeof(hash), "%08x", r.payload_hash);
      out += "encode,-1," + std::to_string(r.seq) + ",-1,-1,-1," + hash + "," + std::to_string(r.fraction_lost) + "," +
             std::to_string(r.bitrate_bps) + "," + std::to_string(r.allocation_bps) + "," +
             std::to_string(r.overhead_bytes) + "\n";
      continue;
    }
    if (!ts0) ts0 = r.rtp_ts;
    char hash[16];
    std::snprintf(hash, sizeof(hash), "%08x", r.payload_hash);
    out += "frame," + std::to_string(r.time_us) + "," + std::to_string(r.seq) + "," +
           std::to_string(static_cast<uint32_t>(r.rtp_ts - *ts0)) + "," + std::to_string(r.pt) + "," +
           std::to_string(r.payload_bytes) + "," + hash + ",-1,-1,-1,-1\n";
  }
  return out;
}

std::string LinkTraceCsv(const std::vector<sim::LinkTraceRecord>& trace) {
  static const char* const kKinds[] = {"other", "audio", "video", "rtx", "padding", "fec"};
  static const char* const kOutcomes[] = {"delivered", "queue_drop", "lost", "down"};
  std::string out = "time_us,size,kind,seq,queue_bytes,outcome,arrival_us,rtp_ts,marker,osn,key,header,rtx_room\n";
  for (const auto& r : trace) {
    out += std::to_string(r.time_us) + "," + std::to_string(r.size) + "," + kKinds[static_cast<size_t>(r.kind)] + "," +
           std::to_string(r.seq) + "," + std::to_string(r.queue_bytes) + "," +
           kOutcomes[static_cast<size_t>(r.outcome)] + "," + std::to_string(r.arrival_us) + "," +
           std::to_string(r.rtp_ts) + "," + std::to_string(r.marker) + "," + std::to_string(r.osn) + "," +
           std::to_string(r.key) + "," + std::to_string(r.header) + "," + std::to_string(r.rtx_room) + "\n";
  }
  return out;
}

void AddLinkKindMetrics(const sim::LinkStats& stats, const Scenario& s, Report& rep) {
  struct Row {
    const char* name;
    sim::LinkKind kind;
    bool loss;
  };
  static const Row rows[] = {{"audio", sim::LinkKind::kAudio, true}, {"video", sim::LinkKind::kVideo, true},
                             {"rtx", sim::LinkKind::kRtx, true},     {"padding", sim::LinkKind::kPadding, true},
                             {"fec", sim::LinkKind::kFec, false}};
  const double seconds = std::max(1e-3, static_cast<double>(s.duration.ms()) / 1000.0);
  for (const Row& r : rows) {
    const sim::LinkKindStats& k = stats.of(r.kind);
    rep.metrics[std::string("net.") + r.name + "_kbps"] = static_cast<double>(k.bytes_sent) * 8 / 1000.0 / seconds;
    if (r.loss) {
      rep.metrics[std::string("net.") + r.name + "_loss_pct"] =
          k.sent ? 100.0 * static_cast<double>(k.lost + k.queue_dropped) / static_cast<double>(k.sent) : 0;
    }
    if (r.loss) {
      const std::string base = std::string("net.") + r.name;
      rep.metrics[base + "_queue_drops"] = static_cast<double>(k.queue_dropped);
      rep.metrics[base + "_size_p50_b"] = static_cast<double>(k.SizePercentile(50));
      rep.metrics[base + "_size_p90_b"] = static_cast<double>(k.SizePercentile(90));
      rep.metrics[base + "_size_max_b"] = static_cast<double>(k.max_size);
    }
  }
}

void AddCcMetrics(const Scenario& s, double demand_kbps, Report& rep) { AddCcMetrics(s, s.forward, demand_kbps, rep); }

void AddCcMetrics(const Scenario& s, const std::vector<LinkChange>& changes, double demand_kbps, Report& rep) {
  auto& m = rep.metrics;
  std::vector<double> targets;
  double good_sum = 0, usable_sum = 0;
  for (const auto& smp : rep.timeseries) {
    targets.push_back(smp.target_kbps);
    if (smp.capacity_kbps > 0) {
      good_sum += smp.goodput_kbps;
      usable_sum += std::min(smp.capacity_kbps, demand_kbps);
    }
  }
  m["cc.target_avg_kbps"] = Mean(targets);
  {
    double over_ms = 0;
    for (const auto& smp : rep.timeseries) {
      if (smp.t_s > 2.0 && smp.send_kbps > 1.5 * smp.target_kbps) over_ms += 100;
    }
    m["cc.send_over_target_ms"] = over_ms;
  }
  {
    std::vector<double> over;
    for (const auto& smp : rep.timeseries) {
      if (smp.t_s > 2.0 && smp.target_kbps > 0) over.push_back(std::max(0.0, 100.0 * (smp.send_kbps / smp.target_kbps - 1.0)));
    }
    m["cc.send_over_target_p95_pct"] = Percentile(over, 95);
  }
  {
    // A sharp cut of the target on a link that did not change (for example a
    // loss burst read as congestion). Start-up and link changes are excluded
    // because the target is expected to move there.
    double max_drop = 0;
    for (size_t i = 0; i < rep.timeseries.size(); ++i) {
      const double t = rep.timeseries[i].t_s;
      bool excluded = t < 5.0;
      for (const auto& c : changes) excluded = excluded || (t >= c.at.seconds() && t < c.at.seconds() + 5.0);
      if (excluded) continue;
      double peak = 0;
      for (size_t j = i + 1; j-- > 0 && rep.timeseries[j].t_s > t - 5.0;) peak = std::max(peak, rep.timeseries[j].target_kbps);
      if (peak > 0) max_drop = std::max(max_drop, 100.0 * (1.0 - rep.timeseries[i].target_kbps / peak));
    }
    m["cc.target_max_drop_pct"] = max_drop;
  }
  double good_total = 0;
  for (const auto& smp : rep.timeseries) good_total += smp.goodput_kbps;
  m["cc.goodput_kbps"] = rep.timeseries.empty() ? 0 : good_total / static_cast<double>(rep.timeseries.size());
  m["cc.utilization_pct"] = usable_sum > 0 ? 100.0 * good_sum / usable_sum : 0;
  // Every forward change (including the start) opens a segment that lasts
  // until the next change; the target must settle within it.
  LinkState st = DefaultForward();
  double max_settle = 0;
  int unsettled = 0;
  std::vector<std::pair<double, LinkState>> segs;
  segs.emplace_back(0.0, st);
  for (const auto& c : changes) {
    st.Apply(c);
    double at = c.at.seconds();
    if (!segs.empty() && segs.back().first == at) segs.back().second = st;
    else segs.emplace_back(at, st);
  }
  for (size_t i = 0; i < segs.size(); ++i) {
    const auto& cfg = segs[i].second;
    double seg_end = i + 1 < segs.size() ? segs[i + 1].first : s.duration.seconds();
    if (cfg.cfg.down || !cfg.cfg.capacity.IsFinite() || seg_end <= segs[i].first) continue;
    double usable = cfg.cfg.capacity.kbps() - cfg.cross.kbps();
    if (usable <= 0) continue;
    double lo = 0.5 * std::min(usable, demand_kbps), hi = usable;
    std::optional<double> settled;
    for (const auto& smp : rep.timeseries) {
      if (smp.t_s <= segs[i].first || smp.t_s > seg_end) continue;
      if (smp.target_kbps >= lo && smp.target_kbps <= hi) {
        settled = smp.t_s - segs[i].first;
        break;
      }
    }
    if (!settled) {
      ++unsettled;
      settled = seg_end - segs[i].first;
    }
    max_settle = std::max(max_settle, *settled * 1000);
  }
  m["cc.max_settle_ms"] = max_settle;
  m["cc.unsettled"] = unsettled;
}

void AddVideoDelayMetrics(const std::vector<VideoFrameTimes>& frames, Report& rep) {
  AddVideoDelayMetrics(frames, rep.metrics);
}

void EvaluateExpectations(Report& rep) {
  for (const auto& e : rep.scenario.expectations) {
    CheckResult c;
    c.expectation = e;
    auto it = rep.metrics.find(e.metric);
    c.found = it != rep.metrics.end();
    c.actual = c.found ? it->second : 0;
    c.passed = c.found && Evaluate(e.op, c.actual, e.value);
    rep.passed = rep.passed && c.passed;
    rep.checks.push_back(c);
  }
}

namespace {

// `apm_rate`: AEC3, NS and AGC2 built at one rate, each fed 50 frames of 10 ms
// as a host would (issue #594). AEC3 takes split-band frames with room for every
// band; NS and AGC2 take rate / 100 samples.
void ProbeApm(int rate, std::map<std::string, double>& m) {
  constexpr float kAec3Level = 50.f, kNsLevel = 0.03f, kAgc2Level = 0.1f;
  const size_t samples = rate / 100 > 0 ? static_cast<size_t>(rate / 100) : 1;
  bool untouched = true;
  auto same = [](const std::vector<float>& v, float x) {
    return std::all_of(v.begin(), v.end(), [x](float y) { return y == x; });
  };

  audio::aec3::EchoCanceller3 aec3(audio::aec3::Aec3Config{}, rate, 1, 1);
  audio::aec3::Frame frame(audio::aec3::kMaxNumBands,
                           std::vector<std::vector<float>>(1, std::vector<float>(audio::aec3::kFrameSize, kAec3Level)));
  for (int i = 0; i < 50; ++i) {
    aec3.AnalyzeRender(frame);
    aec3.ProcessCapture(&frame, false);
  }
  if (!aec3.valid()) untouched = untouched && same(frame[0][0], kAec3Level);

  audio::ns::NoiseSuppressorConfig nc;
  nc.sample_rate = rate;
  audio::ns::NoiseSuppressor ns(nc);
  std::vector<float> nf(samples, kNsLevel);
  for (int i = 0; i < 50; ++i) {
    ns.Analyze(nf);
    ns.Process(nf);
  }
  if (!ns.valid()) untouched = untouched && same(nf, kNsLevel);

  audio::agc2::GainController2Config gc;
  gc.sample_rate = rate;
  audio::agc2::GainController2 agc2(gc);
  std::vector<float> gf(samples, kAgc2Level);
  for (int i = 0; i < 50; ++i) agc2.Process(gf);
  if (!agc2.valid()) untouched = untouched && same(gf, kAgc2Level);

  m["apm.aec3_valid"] = aec3.valid() ? 1 : 0;
  m["apm.ns_valid"] = ns.valid() ? 1 : 0;
  m["apm.agc2_valid"] = agc2.valid() ? 1 : 0;
  m["apm.untouched"] = untouched ? 1 : 0;
}

}  // namespace

int AudioCodecRate(int /*capture_rate*/) { return OpusDefaults::kSampleRateHz; }

namespace {

// One direction of the call, sender to receiver, as two objects (issues #744, #745): the send side drives one
// rtp::Session's send streams (media sources, the encoder model, the frame dropper and the FEC inputs), the receive
// side another session's receive streams (audio playout, video render, the freeze detector and the meters that feed
// the report). RunCall owns the sessions, the links and the event loop, and calls each side at the same points it
// always ran them, so a one-way call runs exactly as before. A two-way call (#746) builds a second pair.

// What both ends of one direction write: the frames on their way from capture to render, the forward link's
// accounting, the repair meter and the per-sample window counters.
struct DirectionMeters {
  DirectionMeters() : fec_recovered_seqs(repair) {}
  DirectionMeters(const DirectionMeters&) = delete;
  DirectionMeters& operator=(const DirectionMeters&) = delete;

  RepairMeter repair;  // Issue #509: the repair metrics by the rule sm_compare's libwebrtc side uses.
  FecRecoveredSeqs fec_recovered_seqs;
  rtp::H264PayloadParser parser;
  std::unordered_map<uint32_t, Timestamp> frame_capture;      // RTP timestamp -> capture time.
  std::unordered_map<uint32_t, VideoFrameTimes> frame_times;  // RTP timestamp -> its path (issue #60).
  std::unordered_map<uint32_t, int32_t> frame_rtx_room;       // RTP timestamp -> rtx_room (#555).
  VideoTraceRow vt_row;                                       // The second being accumulated (issue #131).
  // Per-sample window counters.
  int64_t w_send = 0, w_good = 0, w_frames = 0;
  uint64_t w_conceal = 0, w_played = 0;  // Audio samples.
  // The first and last audio packet on the forward link: RTP timestamp and send time (net.audio_rtp_clock_hz).
  std::optional<std::pair<uint32_t, Timestamp>> audio_ts_first, audio_ts_last;
  // The first audio and video media packets on the forward link (issue #514).
  std::optional<uint16_t> audio_start_seq, video_start_seq;
  bool audio_first_marker = false;
  // Padding-only packets by SSRC (issue #514).
  int64_t padding_media_ssrc = 0, padding_rtx_ssrc = 0;
  std::set<int64_t> padding_sizes;
};

// The streams of one direction, as both its ends configure them.
struct StreamConfigs {
  rtp::AudioStreamConfig audio;
  rtp::VideoStreamConfig video;
  // The direction's SSRCs: 1, 2 and 3 for a one-way call and a two-way call's up direction, 11, 12 and 13 for its down
  // direction (each endpoint's session sends on one set and receives the other).
  uint32_t audio_ssrc = kAudioSsrc, video_ssrc = kVideoSsrc, rtx_ssrc = kRtxSsrc;
};

StreamConfigs MakeStreamConfigs(const Scenario& s, int sr, DataRate audio_wire_rate, DirectionMeters& d,
                                uint32_t ssrc_base = 0) {
  StreamConfigs c;
  c.audio_ssrc = kAudioSsrc + ssrc_base;
  c.video_ssrc = kVideoSsrc + ssrc_base;
  c.rtx_ssrc = kRtxSsrc + ssrc_base;
  rtp::AudioStreamConfig& audio_cfg = c.audio;
  audio_cfg.ssrc = c.audio_ssrc;
  audio_cfg.payload_type = kAudioPt;
  if (s.red) audio_cfg.red_payload_type = kAudioRedPt;
  audio_cfg.clock_rate = sr;
  audio_cfg.nominal_rate = audio_wire_rate;
  // sm_compare's AudioSendStream::Config: min = max = the codec rate, 20 ms Opus frames. The session
  // adds the headers of one packet per frame, not the redundancy (GetMinMaxBitrateConstraints, #569).
  audio_cfg.min_bitrate = audio_cfg.max_bitrate = s.audio ? s.audio_bitrate : DataRate::Zero();
  rtp::VideoStreamConfig& video_cfg = c.video;
  video_cfg.ssrc = c.video_ssrc;
  video_cfg.payload_type = kVideoPt;
  // RTX only with NACK, as sm_compare configures libwebrtc's send and receive streams
  // (compare/webrtc/webrtc_call.cc, `if (s.nack)`; issue #514).
  if (s.nack) {
    video_cfg.rtx_ssrc = c.rtx_ssrc;
    video_cfg.rtx_payload_type = kRtxPt;
  }
  video_cfg.fec = s.fec;
  // libwebrtc's whole-packet limit less the fixed header; the session subtracts the real header and
  // the FEC and RTX reservations (issue #513).
  video_cfg.max_payload_size = s.video_max_payload;
  video_cfg.parser = &d.parser;
  video_cfg.sps_pps_tracker = s.sps_pps_tracker;
  video_cfg.render_smoothing = s.render_smoothing;
  video_cfg.packet_buffer.missing_packets = s.h264_missing_packets;
  video_cfg.media_observer = &d.fec_recovered_seqs;
  video_cfg.nack = s.nack;
  // A loss notification needs frame ids and dependencies from a generic
  // descriptor, which H.264 has none of (design 6.4): libwebrtc sends PLI and
  // NACK only, and so does the bench.
  video_cfg.keyframe.send_lntf = false;
  video_cfg.timing.version = s.timing_v2 ? 2 : 1;
  // The bench has no decoder, so it reports decoding in no time, as sm_compare's ModelDecoder does: it decodes
  // inside Decode() in simulated time, so libwebrtc's decode-time filter (decode_time_percentile_filter.cc) sees
  // 0 ms and VCMTiming's TargetDelay floor is the 10 ms render delay alone (timing.cc). Issue #379.
  video_cfg.decode_time = TimeDelta::Zero();
  // The bench's H.264 stream has no picture id, so with NACK on libwebrtc sends
  // no video FEC (ShouldDisableRedAndUlpfec) and neither does the session.
  video_cfg.deliver_bitstream = false;  // The bench has no decoder.
  return c;
}

// The encoder side's controller: the frame dropper. The session decides the FEC protection (#29).
rtp::VideoSendControllerConfig MakeVideoSendControllerConfig(const Scenario& s) {
  rtp::VideoSendControllerConfig vsc_cfg;
  vsc_cfg.keyframe_interval = s.keyframe_interval;
  vsc_cfg.fps = s.video_fps;
  vsc_cfg.bitrate_adjuster = s.bitrate_adjuster;
  vsc_cfg.adjuster.newfangled_headroom_adjustment = s.rate_utilization_tracker;
  vsc_cfg.adjuster_on_update = s.adjuster_on_update;
  // EncoderRtcpFeedback's kMinKeyframeSendIntervalMs (video/encoder_rtcp_feedback.cc).
  vsc_cfg.min_keyframe_request_interval = TimeDelta::Millis(300);
  // libwebrtc's SimulcastRateAllocator keeps the encoder at the codec minimum (VideoCodec::minBitrate =
  // the stream's min_bitrate_bps), and the bitrate adjuster never pushes the target below it.
  vsc_cfg.allocation_min_bitrate = s.video_min;
  vsc_cfg.allocation_max_bitrate = s.video_max;
  vsc_cfg.adjuster.min_bitrate = s.video_min;
  vsc_cfg.quality_scaling = s.quality_scaling;
  return vsc_cfg;
}

// The session configuration both ends of a call share (each with its own RTCP SSRC and CNAME).
rtp::SessionConfig MakeSessionConfig(const Scenario& s, EventLog& events, uint32_t rtcp_ssrc, const char* cname) {
  rtp::SessionConfig c;
  c.local_ssrc = rtcp_ssrc;
  c.cname = cname;
  c.extensions.Register(1, sync::uri::kTransportCc);
  c.rtcp.has_video = s.video;
  c.send_side.gcc.min_rate = s.min_rate;
  c.send_side.gcc.start_rate = s.start_rate;
  c.send_side.gcc.max_rate = s.max_rate;
  c.send_side.transport_overhead_bytes = kUdpIpOverhead;
  c.send_side.video_min = s.video_min;
  c.send_side.video_max = s.video_max;
  c.send_side.send_rate_stats = s.send_rate_stats;
  c.send_side.link_headroom = s.link_headroom;
  c.send_side.control_handler = s.control_handler;
  c.send_side.expected_packet_overhead = s.expected_packet_overhead;  // Issue #577.
  c.send_side.pacer.controller.keyframe_flush_padding = s.keyframe_flush_padding;
  // RtpVideoSender::OnBitrateUpdated computes the overhead rate for rtp_config_.max_packet_size, the same
  // limit its packetizer uses (libwebrtc's default 1460, rtp_rtcp_impl2.cc:149):
  // the bench's packets are that size, so the overhead is too (issue #151): the packetizer's
  // whole-packet limit, `video_max_payload` plus the 12-byte fixed header clamped to 100-1500 as
  // rtp::RtpSender does, as in sm_compare's libwebrtc call (#379); 1460 by default (#513).
  c.send_side.max_packet_size = static_cast<int64_t>(std::clamp<size_t>(s.video_max_payload + 12, 100, 1500));
  // libwebrtc's RTCPSender seeds its report-interval draws with the clock in microseconds when it is built
  // (rtcp_sender.cc:130), and sm_compare builds every one at the call's start: every engine here takes that same
  // time, so both stacks draw the same RTCP schedule (#517; per seed since #399).
  c.seed = RtcpRandomSeed(s);
  c.per_stream_rtt = s.per_stream_rtt;  // Issue #568.
  c.receive_statistics_v2 = s.receive_statistics_v2;  // Issue #575.
  c.events = &events;
  return c;
}

// The send side of one direction (issue #744): the audio and video sources, the Opus encoder and the video encoder
// model, the frame dropper (rtp::VideoSendController) and the frames still in the encoder, on one session's send
// streams.
class SendSide {
 public:
  // `encode_delay on`: frames still in the encoder, oldest first (issue #84).
  struct PendingFrame {
    Timestamp capture, done;
    rtp::EncodedFrameReport report;
    uint32_t rtp_ts;
  };

  SendSide(const Scenario& s, const RunOptions& opt, rtp::Session& session, const StreamConfigs& cfg,
           DirectionMeters& d, int sr, TimeDelta clock0)
      : s_(s),
        opt_(opt),
        session_(session),
        d_(d),
        sr_(sr),
        clock0_(clock0),
        audio_ssrc_(cfg.audio_ssrc),
        enc_rng_(s.seed + 13),
        opus_encoder_(OpusBitrateBps(static_cast<int>(s.audio_bitrate.bps()), 0), /*fec_enabled=*/true),
        opus_loss_(Timestamp::Millis(0)),
        rtx_ack_(cfg.video_ssrc, cfg.rtx_ssrc, s.nack),
        enc_behaviour_{s.encoder_overshoot, s.scene_change_interval.ms(), s.scene_change_frames, s.scene_change_factor} {
    // libopus encodes what sm_compare gives libwebrtc: the 48 kHz signal as 16-bit PCM (testbench/opus_encoder.h).
    if (s.audio) {
      // In a scripted conversation (#753) the turns are the pauses: speech within a turn has none, so an endpointer's
      // end of turn is the turn's end, not a pause in the signal (SpeechLike's run up to 1.2 s).
      const bool pauses = !(s.two_way && !s.turns.empty());
      for (float v : sim::SpeechLike(OpusDefaults::kSampleRateHz, s.duration.seconds() + 1, s.seed + 7, pauses)) {
        opus_pcm_.push_back(OpusSendEncoder::ToPcm(v));
      }
    }
    if (s.audio) audio_send_ = session.AddAudioSend(cfg.audio);
    if (s.video) video_send_ = session.AddVideoSend(cfg.video);
    // Each stream starts its RTP timestamps at a random offset, as libwebrtc (issue #514). The
    // measurement maps wire timestamps back to the capture clock by taking the offset off again, as
    // webrtc_call.cc takes off the first audio timestamp and the video frames' offset.
    audio_ts_offset_ = s.audio ? session.audio_sender(audio_send_).StartTimestamp() : 0;
    video_ts_offset_ = s.video ? session.video_sender(video_send_).StartTimestamp() : 0;
    video_ctl_.emplace(MakeVideoSendControllerConfig(s), session);
  }

  rtp::Session& session() { return session_; }
  const rtp::Session& session() const { return session_; }
  rtp::VideoSendController& video_ctl() { return *video_ctl_; }
  const rtp::VideoSendController& video_ctl() const { return *video_ctl_; }
  rtp::AudioSendId audio_send() const { return audio_send_; }
  rtp::VideoSendId video_send() const { return video_send_; }
  uint32_t audio_ts_offset() const { return audio_ts_offset_; }
  RtxAckPhase& rtx_ack() { return rtx_ack_; }
  std::vector<AudioSendRow>& audio_reports() { return audio_reports_; }
  const std::vector<AudioSendRow>& opus_encode_rows() const { return opus_encode_rows_; }

  // The RTT sample the session measured during this tick, for the receiver; cleared at each tick's start.
  std::optional<TimeDelta>& new_rtt() { return new_rtt_; }

  // Whether the endpoint speaks in the audio frame captured at a time (#751): it sends silence when not. Unset (a
  // one-way call, or a two-way call without turns): it speaks the whole call.
  void SetSpeech(std::function<bool(Timestamp)> speaking) { speaking_ = std::move(speaking); }

  // The session's events since the last call.
  void OnEvents(Timestamp at) {
    for (const auto& e : session_.TakeEvents()) {
      video_ctl().OnSessionEvent(e);
      switch (e.type) {
        case rtp::SessionEventType::kKeyframeRequested:
          // A PLI or FIR names the media SSRC; the outage monitor's own request does not.
          if (e.ssrc != 0) ++keyframe_requests_;
          break;
        case rtp::SessionEventType::kEncoderPause:
        case rtp::SessionEventType::kEncoderResume: break;  // Handled by the video controller.
        case rtp::SessionEventType::kRttUpdated:
          new_rtt_ = e.rtt;
          break;
        case rtp::SessionEventType::kReportBlock:
          // ChannelSend::OnReportBlockDataUpdated: the audio stream's fraction lost goes to the encoder.
          if (e.ssrc == audio_ssrc_) {
            opus_loss_.OnReceivedUplinkPacketLossFraction(static_cast<float>(e.loss_fraction), at);
            opus_loss_given_ = static_cast<int32_t>(std::lround(static_cast<float>(e.loss_fraction) * 256));
          }
          break;
        case rtp::SessionEventType::kTargetRateChanged: break;
      }
    }
  }

  // RTCP from the other end, before the session takes it.
  void OnFeedback(const std::vector<uint8_t>& bytes, Timestamp at, Report& rep) {
    if (opt_.link_trace) TraceAudioRtcp(bytes, at.us(), audio_ssrc_, rep.audio_send_trace);
    TraceAudioRtcp(bytes, at.us(), audio_ssrc_, audio_reports_);  // rtcp.audio_report_blocks (issue #484).
    if (opt_.link_trace) rtx_ack_.OnRtcp(bytes);
    session_.OnPacket(bytes, at + clock0_);
  }

  // The encoder hands a frame to the library and the sender when it is done:
  // at once, or after the encode-time model with `encode_delay on`.
  // As VideoStreamEncoder::OnEncodedImage, the frame goes to the sender first and the library's
  // post-encode step (RunPostEncode) then gets the size of the frame as sent, after the keyframe SPS
  // rewrite (issue #655).
  void FrameEncoded(const PendingFrame& f) {
    // Keyed by the timestamp on the wire.
    const uint32_t wire_ts = f.rtp_ts + video_ts_offset_;
    d_.frame_capture[wire_ts] = f.capture;
    d_.frame_times[wire_ts].capture = f.capture;
    d_.frame_times[wire_ts].submitted = f.done;
    if (opt_.link_trace) d_.frame_rtx_room[wire_ts] = rtx_ack_.Room();
    const auto frame = rtp::MakeFakeH264Frame(f.report.keyframe, static_cast<size_t>(f.report.size.bytes()));
    rtp::EncodedFrameReport sent = f.report;
    sent.size = session_.SendVideo(video_send_, {f.rtp_ts, frame, f.report.keyframe}, f.done + clock0_);
    video_ctl().OnEncoded(sent, f.done + clock0_);
  }

  // The time the oldest frame in the encoder is done, if by `now`.
  Timestamp EncodedDue(Timestamp now) const {
    return !encoding_.empty() && encoding_.front().done <= now ? encoding_.front().done : Timestamp::PlusInfinity();
  }
  void FinishOldestEncode() {
    FrameEncoded(encoding_.front());
    encoding_.pop_front();
  }

  // VideoStreamEncoder::OnBitrateUpdated after the session ran: a resume encodes the frame held during the
  // pause (pending_frame_), with its capture time, as libwebrtc's encoder does in sm_compare.
  void PollPendingFrame(Timestamp at) {
    if (const std::optional<rtp::EncodeDecision> p = video_ctl().MaybeEncodePendingFrame(at + clock0_)) {
      held_frame_ = false;
      --frames_skipped_;  // Counted when it was held.
      EncodeVideo(*p, p->capture_time - clock0_, at);
    } else if (held_frame_ && !video_ctl().has_pending_frame()) {
      held_frame_ = false;  // Released after kPendingFrameTimeoutMs without ProcessDroppedFrame: not a drop there.
    }
  }

  // Frame k (20 ms from 20k ms) leaves the encoder at 20k + 10 ms. That is the
  // phase libwebrtc's audio send path has in sm_compare: the ADM delivers 10 ms
  // blocks from t = 0 and a 20 ms Opus packet completes on every second block
  // (10, 30, 50 ms). With 30 fps video captured from t = 0 it decides which
  // packets share a 5 ms send group in the delay-based detector (issue #124).
  void CaptureAudio(int64_t t, Timestamp now) {
    if (!(s_.audio && t % 20 == 10)) return;
    const int64_t k = t / 20;
    opus_encoder_.SetPacketLossPercent(opus_loss_.percent());
    const size_t opus_off = static_cast<size_t>(k) * OpusSendEncoder::kFrameSamples;
    static const std::vector<int16_t> kSilence(OpusSendEncoder::kFrameSamples, 0);
    const std::span<const int16_t> opus_in = !speaking_ || speaking_(now)
                                                 ? std::span<const int16_t>(opus_pcm_).subspan(std::min(opus_off, opus_pcm_.size()))
                                                 : std::span<const int16_t>(kSilence);
    if (opt_.link_trace) {
      // What libopus encodes: the frame, zero-padded past the end of the signal (OpusSendEncoder::Encode).
      std::vector<int16_t> in(OpusSendEncoder::kFrameSamples, 0);
      std::copy_n(opus_in.begin(), std::min(opus_in.size(), in.size()), in.begin());
      AudioSendRow r;
      r.kind = AudioSendRow::Kind::kEncode;
      r.seq = static_cast<int32_t>(k);
      r.payload_hash = PcmHash(in);
      r.fraction_lost = opus_loss_given_;
      r.bitrate_bps = opus_encoder_.bitrate_bps();  // Built once from the configured rate: no allocation, no overhead.
      opus_encode_rows_.push_back(r);
    }
    const std::vector<uint8_t> frame = opus_encoder_.Encode(opus_in);
    session_.SendAudio(audio_send_, {static_cast<uint32_t>(k * 20 * sr_ / 1000), frame, false}, now + clock0_);
  }

  void CaptureVideo(int64_t t, Timestamp now) {
    if (!(s_.video && t >= next_frame_ms_)) return;
    VideoTraceRow& vt_row = d_.vt_row;
    int64_t k = frames_encoded_ + frames_skipped_ + 1;
    next_frame_ms_ = k * 1000 / s_.video_fps;
    // The library decides: encode or drop, keyframe or not, and the rate.
    const rtp::EncodeDecision dec = video_ctl().OnCapturedFrame({s_.video_width, s_.video_height}, now + clock0_);
    // The size the library asks for (the captured size unless quality scaling adapts it) is dec.width x
    // dec.height; EncodeVideo gives it to the FEC controller.
    const DataRate vr = dec.target;
    ++vt_row.captured;
    vt_row.encoder_target_bps = vr.bps();
    encoder_target_bps_sum_ += vr.bps();
    // VideoStreamEncoder counts a frame it holds (pending_frame_, for its size or a pause) as dropped
    // (kEncoderQueue) only when the next frame releases it, and not when a resume encodes it.
    if (held_frame_ && dec.reason != rtp::DropReason::kCongestionWindow) ++vt_row.dropped_encoder_queue;
    if (dec.reason != rtp::DropReason::kCongestionWindow) held_frame_ = dec.hold;
    if (!dec.encode) {
      ++frames_skipped_;
      switch (dec.reason) {
        case rtp::DropReason::kFrameDropper: ++vt_row.dropped_rate_limiter; break;
        case rtp::DropReason::kCongestionWindow: ++vt_row.dropped_congestion_window; break;
        default:
          if (!dec.hold) ++vt_row.dropped_encoder_queue;  // Paused: libwebrtc's EncoderPaused path.
          break;
      }
    } else {
      EncodeVideo(dec, now, now);
    }
  }

  // The send side's columns of the video trace's row for the second ending at `now`.
  void FillTraceRow(Timestamp now) {
    VideoTraceRow& vt_row = d_.vt_row;
    vt_row.alloc_target_bps = session_.rates().video_target.bps();
    vt_row.paused = video_ctl().paused() ? 1 : 0;
    vt_row.pacer_queue_ms = static_cast<double>(session_.send_side().pacer().OldestQueueTime(now + clock0_).us()) / 1000.0;
    const video::QualityScaler& qs = video_ctl().quality_scaler();
    if (qs.checks() > vt_qs_checks_) {
      using Signal = video::QualityScaler::Signal;
      const auto& c = qs.last_check();
      vt_row.qs_checks = static_cast<int>(qs.checks() - vt_qs_checks_);
      vt_row.qs_qp_high = c.qp_high;
      vt_row.qs_qp_low = c.qp_low;
      vt_row.qs_drop_pct = c.drop_pct;
      vt_row.qs_signal = c.signal == Signal::kAdaptDown ? -1 : c.signal == Signal::kAdaptUp ? 1 : 0;
      vt_qs_checks_ = qs.checks();
    }
  }

  int64_t frames_encoded() const { return frames_encoded_; }
  int64_t frames_skipped() const { return frames_skipped_; }
  int64_t keyframes_sent() const { return keyframes_sent_; }
  int64_t keyframe_requests() const { return keyframe_requests_; }
  int64_t encoder_target_bps_sum() const { return encoder_target_bps_sum_; }
  int64_t video_bytes_encoded() const { return video_bytes_encoded_; }
  int64_t qp_sum() const { return qp_sum_; }
  int64_t height_sum() const { return height_sum_; }

 private:
  // Encode a frame the library allowed, captured at `capture`, at `at` (the capture time, or the resume
  // that encodes a held frame).
  void EncodeVideo(const rtp::EncodeDecision& dec, Timestamp capture, Timestamp at) {
    VideoTraceRow& vt_row = d_.vt_row;
    const int enc_w = dec.width, enc_h = dec.height;
    const DataRate vr = dec.target;
    const bool key = dec.keyframe;
    // VideoSendStreamImpl::OnEncoderConfigurationChanged: the FEC controller takes the encoded size.
    if (enc_w != fec_width_ || enc_h != fec_height_) {
      session_.SetVideoEncodingData(video_send_, enc_w, enc_h);
      fec_width_ = enc_w;
      fec_height_ = enc_h;
    }
    ++vt_row.encoded;
    ++frames_encoded_;
    double per_frame = static_cast<double>(vr.bps()) / 8 / s_.video_fps;
    int64_t bytes = static_cast<int64_t>(
        key ? per_frame * 4 : per_frame * enc_rng_.Uniform(0.8, 1.2) * EncoderModel::DeltaSizeFactor(enc_behaviour_, capture.ms(), s_.video_fps));
    bytes = std::max<int64_t>(bytes, 200);
    vt_row.encoded_bytes += bytes;
    const int qp = EncoderModel::Qp(bytes, key, enc_w, enc_h, s_.video_complexity);
    vt_row.qp_sum += qp;
    vt_row.encode_us_sum += EncoderModel::EncodeTimeUs(enc_w, enc_h, s_.encoder_speed);
    qp_sum_ += qp;
    height_sum_ += enc_h;
    vt_row.height_sum += enc_h;
    if (key) ++keyframes_sent_;
    vt_row.encode_width = enc_w;
    vt_row.encode_height = enc_h;
    if (key) {
      ++vt_row.keyframes;
      vt_row.keyframe_bytes += bytes;
    }
    video_bytes_encoded_ += bytes;
    // VideoStreamEncoder gives a frame the RTP time 90 x its capture NTP time in ms; on libwebrtc's
    // simulated clock that is the clock (which starts at CallClockStartSeconds) plus the NTP epoch
    // offset, 2208988800 s (issue #514). The sender adds the stream's start offset.
    const int64_t capture_ntp_ms = (CallClockStartSeconds(s_) + 2208988800LL) * 1000 + capture.ms();
    const uint32_t rtp_ts = static_cast<uint32_t>(capture_ntp_ms * 90);
    const TimeDelta encode_time =
        s_.encode_delay ? TimeDelta::Micros(EncoderModel::EncodeTimeUs(enc_w, enc_h, s_.encoder_speed)) : TimeDelta::Zero();
    PendingFrame f{capture, at + encode_time, {DataSize::Bytes(bytes), key, qp}, rtp_ts};
    if (encode_time > TimeDelta::Zero()) {
      encoding_.push_back(f);
    } else {
      FrameEncoded(f);
    }
  }

  const Scenario& s_;
  const RunOptions& opt_;
  rtp::Session& session_;
  DirectionMeters& d_;
  const int sr_;
  const TimeDelta clock0_;
  const uint32_t audio_ssrc_;

  Random enc_rng_;
  std::vector<int16_t> opus_pcm_;
  OpusSendEncoder opus_encoder_;
  OpusPacketLossRate opus_loss_;  // The in-band FEC input (issue #62).
  int32_t opus_loss_given_ = -1;  // The last fraction it was given, times 256 (issue #547).
  std::vector<AudioSendRow> opus_encode_rows_;  // The encoder's inputs per packet (issue #547).
  // The trace's rtx_room column (#555): the sender's RTX reservation phase when each frame was handed to it.
  RtxAckPhase rtx_ack_;
  std::vector<AudioSendRow> audio_reports_;  // Each audio report block the sender received (issue #484).
  rtp::AudioSendId audio_send_;
  rtp::VideoSendId video_send_;
  uint32_t audio_ts_offset_ = 0, video_ts_offset_ = 0;
  std::optional<rtp::VideoSendController> video_ctl_;

  const EncoderModel::Behaviour enc_behaviour_;
  int64_t frames_encoded_ = 0, frames_skipped_ = 0, keyframes_sent_ = 0, keyframe_requests_ = 0;
  int64_t encoder_target_bps_sum_ = 0;  // The encoder's configured rate at each captured frame (issue #466).
  int64_t video_bytes_encoded_ = 0, qp_sum_ = 0, height_sum_ = 0;
  int fec_width_ = 0, fec_height_ = 0;  // The resolution the session's FEC controller last took.
  bool held_frame_ = false;  // The video controller holds the last dropped frame (EncodeDecision::hold).
  uint64_t vt_qs_checks_ = 0;  // Quality scaler checks already in the trace (issue #347).
  int64_t next_frame_ms_ = 0;
  std::deque<PendingFrame> encoding_;
  std::optional<TimeDelta> new_rtt_;
  std::function<bool(Timestamp)> speaking_;
};

// The receive side of one direction (issue #745): the 10 ms audio device pull and playout, video frame handling,
// render and the freeze detector, and the per-stream meters that feed the report, on one session's receive streams.
class ReceiveSide {
 public:
  ReceiveSide(const Scenario& s, const RunOptions& opt, rtp::Session& session, const StreamConfigs& cfg,
              DirectionMeters& d, EventLog& events, int sr, TimeDelta rx0, uint32_t audio_ts_offset)
      : s_(s),
        opt_(opt),
        session_(session),
        d_(d),
        sr_(sr),
        rx0_(rx0),
        audio_ts_offset_(audio_ts_offset),
        decoder_(sr),
        freezes_(&events),
        concealment_(sr),
        audio_out_(static_cast<size_t>(sr / 100)) {
    if (s.audio) audio_recv_ = session.AddAudioReceive(cfg.audio, &decoder_);
    if (s.video) video_recv_ = session.AddVideoReceive(cfg.video);
    audio_net_delay_.Start(audio_ts_offset, sr);
  }

  rtp::Session& session() { return session_; }
  const rtp::Session& session() const { return session_; }
  rtp::AudioRecvId audio_recv() const { return audio_recv_; }
  rtp::VideoRecvId video_recv() const { return video_recv_; }
  const video::FreezeDetector& freezes() const { return freezes_; }

  // A packet that arrived on the forward link, before the receiver takes it.
  void OnPacket(const Fwd& w, Timestamp at) {
    int64_t wire = static_cast<int64_t>(w.bytes.size()) + kUdpIpOverhead;
    switch (w.kind) {
      case Kind::kAudio: {
        if (first_delivery_.Deliver(FirstDelivery::kAudio, RtpSeq(w.bytes))) d_.w_good += wire;
        // Kind::kAudio is a media packet: padding on the audio SSRC is Kind::kPadding.
        audio_net_delay_.OnArrival(RtpTimestamp(w.bytes), /*carries_audio=*/true, at);
        audio_started_ = true;
        break;
      }
      case Kind::kVideo:
      case Kind::kRtx:
      case Kind::kFec: {
        if (w.kind != Kind::kRtx) {
          if (first_delivery_.Deliver(FirstDelivery::kVideo, RtpSeq(w.bytes))) d_.w_good += wire;
          if (w.kind == Kind::kVideo) d_.repair.OnVideoArrived(RtpSeq(w.bytes));
        } else if (auto q = RtpPacket::Parse(w.bytes); q && q->payload.size() > 2) {
          const auto osn = static_cast<uint16_t>((q->payload[0] << 8) | q->payload[1]);
          if (first_delivery_.Deliver(FirstDelivery::kVideo, osn)) d_.w_good += wire;
          d_.repair.OnRtxArrived(osn);
        }
        // The last media or RTX packet of a frame (RFC 4588 keeps the
        // original RTP timestamp) before it renders marks it complete.
        if (w.kind != Kind::kFec) {
          auto ft = d_.frame_times.find(RtpTimestamp(w.bytes));
          if (ft != d_.frame_times.end() && !ft->second.rendered.IsFinite()) ft->second.complete = at;
        }
        break;
      }
      default:
        break;
    }
    session_.OnPacket(w.bytes, at + rx0_);
    // The first packet the jitter buffer takes: none when it refuses the stream (#256).
    if (w.kind == Kind::kAudio) {
      audio_first_.OnAudioPacket(at, session_.audio_receiver(audio_recv_).jitter_buffer().stats().packets_received > 0);
    }
  }

  // Renders the frames the receiver released by `at`, at that time.
  void Render(Timestamp at, Report& rep) {
    if (!s_.video) return;
    VideoTraceRow& vt_row = d_.vt_row;
    for (const auto& f : session_.TakeVideoFrames(video_recv_)) {
      if (opt_.video_trace) {
        auto cap = d_.frame_capture.find(f.rtp_timestamp);
        rep.frame_trace.push_back({cap != d_.frame_capture.end() ? cap->second.ms() : -1,
                                   f.render_time.IsFinite() ? (f.render_time - rx0_).ms() : -1, at.ms(), f.render ? 1 : 0});
      }
      if (!f.render) continue;
      freezes_.OnFrameRendered(at);
      ++vt_row.rendered;
      if (last_render_.IsFinite())
        vt_row.max_render_gap_ms = std::max(vt_row.max_render_gap_ms, static_cast<double>((at - last_render_).us()) / 1000.0);
      ++frames_rendered_;
      ++d_.w_frames;
      if (!video_first_.IsFinite()) video_first_ = at;
      last_render_ = at;
      if (auto ft = d_.frame_times.find(f.rtp_timestamp); ft != d_.frame_times.end()) ft->second.rendered = at;
      auto cap = d_.frame_capture.find(f.rtp_timestamp);
      if (cap != d_.frame_capture.end()) {
        av_sync_.OnVideoRendered(at, cap->second);
        last_video_delay_ = static_cast<double>((at - cap->second).us()) / 1000.0;
        video_e2e_.push_back(last_video_delay_);
        d_.frame_capture.erase(cap);
      }
    }
  }

  // The A/V sync inputs at each whole second, after the synchronizer's run at this time and before this time's
  // audio pull, as sm_compare reads libwebrtc's (issue #895).
  void TraceSync(int64_t t, Timestamp now, Report& rep) {
    if (opt_.bwe_trace && t % 1000 == 0) AppendSyncTraceRow(session_, now, rx0_, sync_ts0_, rep.bwe_trace);
  }

  // Audio playout: the device pulls 10 ms.
  void PullAudio(int64_t t, Timestamp now, Report& rep) {
    if (!(s_.audio && t % 10 == 0 && audio_started_)) return;
    session_.GetAudio(audio_recv_, audio_out_, now + rx0_);
    last_pull_time_ = now;
    last_pull_capture_.reset();
    // Audio RTP timestamps, less the stream's offset, count samples from the call start (capture at t = 0).
    if (auto ts = session_.audio_receiver(audio_recv_).jitter_buffer().PlayoutTimestamp()) {
      const uint32_t samples = static_cast<uint32_t>(*ts - audio_ts_offset_);
      av_sync_.OnAudioPlayout(now, Timestamp::Micros(static_cast<int64_t>(samples) * 1000000 / sr_));
      last_pull_capture_ = Timestamp::Micros(static_cast<int64_t>(samples) * 1000000 / sr_);
    }
    const auto& jb = session_.audio_receiver(audio_recv_).jitter_buffer();
    const auto& jst = jb.stats();
    // Playout statistics count from the first pull that output samples, as sm_compare's libwebrtc side (#510).
    if (stretch_.OnPull(jst.total_samples_received, jst.inserted_samples_for_deceleration,
                        jst.removed_samples_for_acceleration)) {
      if (const uint64_t emitted = jst.jitter_buffer_emitted_count - prev_jb_emitted_; emitted > 0) {
        jb_delay_.push_back(static_cast<double>(jst.jitter_buffer_delay_ms_samples - prev_jb_delay_) /
                            static_cast<double>(emitted));
      }
      if (opt_.audio_trace) {
        // Issue #62: the jitter buffer after each pull, as sm_compare reads libwebrtc's GetStats.
        const double ms_per_sample = 1000.0 / sr_;
        AudioTraceRow r;
        r.t_ms = t;
        r.buffer_ms = static_cast<int>(jb.CurrentLevelMs());
        r.target_ms = jb.TargetDelayMs();
        if (jst.jitter_buffer_emitted_count > prev_jb_emitted_) r.jb_delay_ms = jb_delay_.back();
        r.received_ms = static_cast<double>(jst.total_samples_received - prev_jb_trace_.total_samples_received) * ms_per_sample;
        r.concealed_ms = static_cast<double>(jst.concealed_samples - prev_jb_trace_.concealed_samples) * ms_per_sample;
        r.accelerate_ms = static_cast<double>(jst.removed_samples_for_acceleration -
                                              prev_jb_trace_.removed_samples_for_acceleration) * ms_per_sample;
        r.decelerate_ms = static_cast<double>(jst.inserted_samples_for_deceleration -
                                              prev_jb_trace_.inserted_samples_for_deceleration) * ms_per_sample;
        rep.audio_trace.push_back(r);
        prev_jb_trace_.total_samples_received = jst.total_samples_received;
        prev_jb_trace_.concealed_samples = jst.concealed_samples;
        prev_jb_trace_.removed_samples_for_acceleration = jst.removed_samples_for_acceleration;
        prev_jb_trace_.inserted_samples_for_deceleration = jst.inserted_samples_for_deceleration;
      }
      prev_jb_delay_ = jst.jitter_buffer_delay_ms_samples;
      prev_jb_emitted_ = jst.jitter_buffer_emitted_count;
      concealment_.OnPull(jst.total_samples_received, jst.concealed_samples, jst.concealment_events);
      d_.w_played += concealment_.pull_total();
      d_.w_conceal += concealment_.pull_concealed();
    }
  }

  // The receiver's VCMTiming every 5 ms when it changed, as sm_compare samples libwebrtc's
  // VideoReceiveStreamInterface::Stats (issue #893).
  void TraceRxTiming(int64_t t, Report& rep) {
    if (!(opt_.bwe_trace && s_.video && s_.timing_v2 && t % 5 == 4)) return;
    const timing::VcmTiming& vt = session_.video_receiver(video_recv_).vcm_timing();
    cc::BweTraceRow r;
    r.time_us = static_cast<int64_t>(t + 1) * 1000;
    r.kind = cc::BweTraceRow::Kind::kRxTiming;
    r.bitrate_bps = vt.TargetDelay().ms();
    r.packets = static_cast<int>(vt.minimum_delay().ms());
    r.fraction_loss = static_cast<int>(vt.current_delay().ms());
    r.loss_limit_bps = vt.min_playout_delay().ms();
    r.instant_ub_bps = vt.render_delay().ms();
    if (!last_rx_timing_ || r.bitrate_bps != last_rx_timing_->bitrate_bps || r.packets != last_rx_timing_->packets ||
        r.fraction_loss != last_rx_timing_->fraction_loss || r.loss_limit_bps != last_rx_timing_->loss_limit_bps ||
        r.instant_ub_bps != last_rx_timing_->instant_ub_bps) {
      rep.bwe_trace.push_back(r);
      last_rx_timing_ = r;
    }
  }

  // The receive side's columns of the video trace's row for the second just ended.
  void FillTraceRow() {
    if (!s_.video) return;
    VideoTraceRow& vt_row = d_.vt_row;
    // Assembled frames (decodable or not) as libwebrtc's received_frame_counts; dropped are the frames that waited for a
    // missing reference until a keyframe cleared them, plus the frame release's fast-forwards and clears.
    const rtp::RtpReceiver& vr = session_.video_receiver(video_recv_);
    const timing::FrameReleaseStats& rs = vr.frame_release().stats();
    const uint64_t complete = vr.keyframes().stats().frames_received;
    const uint64_t dropped = vr.keyframes().stats().frames_dropped + rs.fast_forwarded + rs.dropped_for_keyframe;
    vt_row.rx_complete = static_cast<int>(complete - vt_rx_prev_[0]);
    vt_row.rx_decoded = static_cast<int>(rs.released - vt_rx_prev_[1]);
    vt_row.rx_dropped = static_cast<int>(dropped - vt_rx_prev_[2]);
    vt_rx_prev_ = {complete, rs.released, dropped};
  }

  double audio_buffer_ms() const {
    return s_.audio ? session_.audio_receiver(audio_recv_).jitter_buffer().CurrentLevelMs() : 0;
  }
  // The last 10 ms the device pulled, when (call time), and the capture time (call time) of the sample at its start,
  // if the jitter buffer had one (#753): what the listener hears, and from when the speaker said it.
  std::span<const float> last_pull() const { return audio_out_; }
  Timestamp last_pull_time() const { return last_pull_time_; }
  std::optional<Timestamp> last_pull_capture() const { return last_pull_capture_; }
  double last_video_delay() const { return last_video_delay_; }

  // The receive side's audio and video metrics at the end of a call of `end_ms`; `frames_encoded` is the send side's
  // count, for the render ratio.
  void AddMetrics(int64_t end_ms, int64_t frames_encoded, Report& rep) {
    const Timestamp end = Timestamp::Millis(end_ms);
    auto& m = rep.metrics;
    concealment_.Finish();
    if (s_.audio) {
      const auto& jbs = session_.audio_receiver(audio_recv_).jitter_buffer().stats();
      m["audio.concealed_pct"] = concealment_.concealed_pct();
      m["audio.interruptions"] = static_cast<double>(concealment_.interruptions());
      m["audio.interruption_ms"] = static_cast<double>(concealment_.interruption_ms());
      m["audio.longest_interruption_ms"] = static_cast<double>(concealment_.longest_event_ms());
      m["audio.jb_delay_p50_ms"] = Percentile(jb_delay_, 50);
      m["audio.jb_delay_p95_ms"] = Percentile(jb_delay_, 95);
      m["audio.net_delay_p95_ms"] = Percentile(audio_net_delay_.samples(), 95);
      m["audio.mouth_to_ear_p95_ms"] = m["audio.net_delay_p95_ms"] + m["audio.jb_delay_p95_ms"];
      m["audio.stretch_pct"] = stretch_.stretch_pct();
      m["audio.late_discards"] = static_cast<double>(jbs.packets_discarded_late);
      m["audio.time_to_first_ms"] = audio_first_.ms(end_ms);
    }
    if (s_.video) {
      const rtp::RtpReceiver& vrx = session_.video_receiver(video_recv_);
      const auto& fz = freezes_.stats();
      double freeze_count = static_cast<double>(fz.freeze_count);
      double freeze_total = fz.total_freezes_ms;
      double longest = fz.longest_freeze_ms;
      double frame_ms = 1000.0 / s_.video_fps;
      // Pauses as libwebrtc's VideoQualityObserver counts them: a gap of 5 s or more is
      // a pause, not a freeze. One still in progress at the end of the call is counted
      // too, so a blackout that lasts to the end is visible.
      double pause_count = static_cast<double>(fz.pause_count);
      double pause_total = fz.total_pauses_ms;
      if (!last_render_.IsFinite()) {
        freeze_count = 1;
        freeze_total = longest = static_cast<double>(end_ms);
      } else {
        // A freeze still in progress when the call ends.
        double tail = static_cast<double>((end - last_render_).us()) / 1000.0;
        if (tail > std::max(3 * frame_ms, frame_ms + 150)) {
          freeze_count += 1;
          freeze_total += tail;
        }
        longest = std::max(longest, tail);
        if (tail >= 5000) {
          pause_count += 1;
          pause_total += tail;
        }
      }
      // Longest render gap also counts gaps below the freeze threshold.
      m["video.frames_rendered"] = static_cast<double>(frames_rendered_);
      m["video.render_fps"] = static_cast<double>(frames_rendered_) / s_.duration.seconds();
      m["video.render_ratio_pct"] = frames_encoded ? 100.0 * static_cast<double>(frames_rendered_) / static_cast<double>(frames_encoded) : 0;
      m["video.freeze_count"] = freeze_count;
      m["video.freeze_total_ms"] = freeze_total;
      m["video.longest_freeze_ms"] = longest;
      m["video.pause_count"] = pause_count;
      m["video.pause_total_ms"] = pause_total;
      m["video.e2e_delay_p50_ms"] = Percentile(video_e2e_, 50);
      m["video.e2e_delay_p95_ms"] = Percentile(video_e2e_, 95);
      {
        std::vector<VideoFrameTimes> all;
        all.reserve(d_.frame_times.size());
        for (const auto& [ts, ft] : d_.frame_times) all.push_back(ft);
        AddVideoDelayMetrics(all, rep);
      }
      m["video.time_to_first_frame_ms"] = video_first_.IsFinite() ? static_cast<double>(video_first_.ms()) : static_cast<double>(end_ms);
      av_sync_.AddMetrics(m);
      AddAvSyncToVideoTrace(av_sync_, rep.video_trace);
      // As libwebrtc's VideoReceiveStream frames_dropped (VideoStreamBufferController::UpdateDroppedFrames and Clear):
      // the frames that waited for a missing reference until a keyframe cleared them, plus the frame release's
      // fast-forwards and clears, the sum the video trace counts (issue #510).
      const timing::FrameReleaseStats& vrs = vrx.frame_release().stats();
      m["video.undecodable_frames"] =
          static_cast<double>(vrx.keyframes().stats().frames_dropped + vrs.fast_forwarded + vrs.dropped_for_keyframe);
      m["repair.fec_recovered"] =
          vrx.ulpfec() ? static_cast<double>(vrx.ulpfec()->GetPacketCounter().num_recovered_packets) : 0;
    }
  }

 private:
  const Scenario& s_;
  const RunOptions& opt_;
  rtp::Session& session_;
  DirectionMeters& d_;
  const int sr_;
  const TimeDelta rx0_;
  const uint32_t audio_ts_offset_;

  OpusAudioDecoder decoder_;
  rtp::AudioRecvId audio_recv_;
  rtp::VideoRecvId video_recv_;
  video::FreezeDetector freezes_;
  std::optional<cc::BweTraceRow> last_rx_timing_;  // Last receiver timing written to rep.bwe_trace (issue #893).
  std::array<uint64_t, 3> vt_rx_prev_{};           // Frame release counters at the last row (issue #437).
  std::vector<double> video_e2e_;
  AudioNetDelayMeter audio_net_delay_;
  bool audio_started_ = false;
  FirstAudioMeter audio_first_;  // Issue #510: as sm_compare's libwebrtc side.
  Timestamp video_first_ = Timestamp::MinusInfinity();
  Timestamp last_render_ = Timestamp::MinusInfinity();
  AvSyncMeter av_sync_;  // Issue #63.
  SyncTs0 sync_ts0_;     // Issue #895.
  StretchMeter stretch_;           // Issue #510: from the jitter buffer's counters, as sm_compare's libwebrtc side.
  ConcealmentMeter concealment_;   // Issue #372: from the jitter buffer's counters, as sm_compare's libwebrtc side.
  // Jitter buffer delay per 10 ms of playout, as sm_compare reads libwebrtc's
  // (jitterBufferDelay / jitterBufferEmittedCount over each pull).
  std::vector<double> jb_delay_;
  uint64_t prev_jb_delay_ = 0, prev_jb_emitted_ = 0;
  timing::AudioJitterBufferStats prev_jb_trace_;  // The counters at the last pull, for rep.audio_trace.
  int64_t frames_rendered_ = 0;
  std::vector<float> audio_out_;
  Timestamp last_pull_time_ = Timestamp::MinusInfinity();
  std::optional<Timestamp> last_pull_capture_;
  FirstDelivery first_delivery_;  // goodput counts each media sequence number once (#329)
  double last_video_delay_ = 0;
};

// A two-way call (issue #746): the user's endpoint and the AI's, each one rtp::Session that sends its own streams and
// receives the other's, bundled on one transport with RTCP multiplexed (program.md 0.1). The forward link carries the
// user's media and RTCP to the AI (the up direction), the reverse link the AI's media and RTCP to the user (down), so
// each direction's feedback shares a link with, and competes with, the other direction's media. Each direction is a
// SendSide and a ReceiveSide (#744, #745), measured as in a one-way call and reported as up.* and down.*.
Report RunTwoWayCall(const Scenario& s, const RunOptions& opt) {
  Report rep;
  rep.scenario = s;
  // The down direction's time series and traces (#749); the up direction's go in `rep`.
  rep.down = std::make_shared<Report>();
  Report& down = *rep.down;
  down.scenario = s;
  MetricsRegistry registry;

  EventLog events(64);
  events.SetCallback([&](const Event& e) { ++rep.events[std::string(e.module) + "/" + e.name]; });

  // --- Links ----------------------------------------------------------------
  LinkTimeline links(s);
  const LinkState& fwd_state = links.forward();
  const LinkState& rev_state = links.reverse();
  sim::NetworkLink<Fwd> forward(fwd_state.cfg, s.seed);
  sim::NetworkLink<Fwd> reverse(rev_state.cfg, s.seed + 1000003);
  if (opt.link_trace) {
    forward.EnableTrace();
    reverse.EnableTrace();
  }

  // --- Media ----------------------------------------------------------------
  const int sr = AudioCodecRate(s.audio_sample_rate);
  const size_t audio_frame_bytes = static_cast<size_t>(std::max<int64_t>(10, s.audio_bitrate.bps() / 8 / 50));
  const DataRate audio_wire_rate =
      s.audio ? DataRate::BitsPerSec((static_cast<int64_t>(audio_frame_bytes) * (s.red ? 2 : 1) + 12 + 4 + kUdpIpOverhead) * 8 * 50)
              : DataRate::Zero();

  // --- The two endpoints ----------------------------------------------------
  // Each endpoint both sends and receives: the one-way sender's settings and the one-way receiver's.
  auto endpoint_config = [&](uint32_t rtcp_ssrc, const char* cname) {
    rtp::SessionConfig c = MakeSessionConfig(s, events, rtcp_ssrc, cname);
    c.audio_rtcp = true;
    c.av_sync = s.av_sync;
    c.audio_receive_rtcp = s.audio_receive_rtcp;
    return c;
  };
  rtp::SessionConfig user_cfg = endpoint_config(kSenderRtcpSsrc, "user@testbench");
  if (opt.bwe_trace) user_cfg.send_side.gcc.bwe_trace = &rep.bwe_trace;
  rtp::SessionConfig ai_cfg = endpoint_config(kReceiverRtcpSsrc, "ai@testbench");
  if (opt.bwe_trace) ai_cfg.send_side.gcc.bwe_trace = &down.bwe_trace;
  rtp::Session user(user_cfg), ai(ai_cfg);
  const TimeDelta clock0 = TimeDelta::Seconds(CallClockStartSeconds(s));

  // Each direction's view of the scenario: video only in the directions `two_way_video` sends it (#747).
  Scenario up_s = s;
  up_s.video = s.video && s.video_up;
  Scenario down_s = s;
  down_s.video = s.video && s.video_down;
  // The AI speaks its own signal and draws its own encoder sizes: its sources take another seed, on the same clock.
  Scenario ai_s = down_s;
  ai_s.compare_clock_start_s = CallClockStartSeconds(s);
  ai_s.seed = s.seed + 50021;
  const Scenario* dir_s[2] = {&up_s, &down_s};

  // Up: the user's send side and the AI's receive side. Down: the AI's send side and the user's receive side, on
  // SSRCs 11-13, so each session sends on one set and receives the other.
  DirectionMeters up_m, down_m;
  const StreamConfigs up_streams = MakeStreamConfigs(up_s, sr, audio_wire_rate, up_m);
  const StreamConfigs down_streams = MakeStreamConfigs(down_s, sr, audio_wire_rate, down_m, 10);
  SendSide up_tx(up_s, opt, user, up_streams, up_m, sr, clock0);
  ReceiveSide up_rx(up_s, opt, ai, up_streams, up_m, events, sr, clock0, up_tx.audio_ts_offset());
  SendSide down_tx(ai_s, opt, ai, down_streams, down_m, sr, clock0);
  ReceiveSide down_rx(down_s, opt, user, down_streams, down_m, events, sr, clock0, down_tx.audio_ts_offset());

  // --- Turns (#751, #753, #754) and the AI's source (#758) --------------------
  // The user speaks in its scripted windows (turns and barge-ins). The AI listens: a TurnDetector (#752) runs on the
  // audio the AI endpoint plays out, so loss and delay move its decisions as in a deployment. When it detects the end
  // of the user's turn, the AI starts its next scripted reply `after` (its thinking time) later, for `len` (#753);
  // when it detects the user speaking during its reply (a barge-in), it stops sending its speech (#754). With
  // `ai_source` a reply is generated and drained by the source, and the AI speaks while frames come out of it.
  // Without any turn both speak the whole call (#746).
  struct Window {
    Timestamp start, end;
  };
  struct AiReply {
    TimeDelta after, len;
  };
  std::vector<Window> user_speech, ai_turns;
  std::vector<AiReply> ai_script;  // In script order: each end of turn the AI detects starts the next one.
  {
    for (const Scenario::Turn& tr : s.turns) {
      if (tr.who == Scenario::Turn::Who::kAi) {
        ai_script.push_back({tr.after, tr.len});
      } else {
        user_speech.push_back({Timestamp::Micros(0) + tr.start, Timestamp::Micros(0) + tr.end});
      }
    }
  }
  TurnDetectorConfig det_cfg;
  det_cfg.sample_rate_hz = sr;
  TurnDetector ai_detector(det_cfg);
  size_t next_reply = 0;  // The next scripted AI reply to start.
  // The user's speech window (turn or barge-in) an utterance the AI heard belongs to: the last that started by the
  // time the AI heard it (it hears it a network delay later).
  auto user_window_of = [&](Timestamp heard) -> const Window* {
    const Window* w = nullptr;
    for (const Window& u : user_speech) {
      if (u.start <= heard) w = &u;
    }
    return w;
  };
  // A reply the user hears (#753): the end of the user speech it answers, its thinking time, and its first speech
  // frame's capture time to the time that plays at the user.
  struct Response {
    Timestamp user_end;
    TimeDelta after;
    std::optional<Timestamp> first_capture, played;
  };
  std::vector<Response> responses;
  // A barge-in the AI acted on (#754): the AI's audio stops at the user's ear when the playout passes the end of the
  // last speech frame it sent.
  struct Stop {
    Timestamp user_start, cut;
    std::optional<Timestamp> last_capture, at_ear;
  };
  std::vector<Stop> stops;
  std::optional<Timestamp> ai_last_speech_capture;
  auto in_any = [](const std::vector<Window>& ws, Timestamp t) {
    for (const Window& w : ws) {
      if (t >= w.start && t < w.end) return true;
    }
    return false;
  };
  std::optional<AiMediaSource> ai_source;
  if (s.ai_source) ai_source.emplace(*s.ai_source);
  size_t next_ai_turn = 0;
  std::vector<double> ai_first_frame_ms;
  if (!s.turns.empty()) {
    up_tx.SetSpeech([&](Timestamp t) { return in_any(user_speech, t); });
    down_tx.SetSpeech([&](Timestamp t) {
      bool speaking = in_any(ai_turns, t);
      if (speaking && ai_source) speaking = !ai_source->Poll(t).empty();  // A frame the real-time drain hands over now.
      if (speaking) {
        // The frame captured at t starts at its 20 ms boundary (CaptureAudio: frame k is [20k, 20k + 20) ms).
        const Timestamp capture = Timestamp::Millis(t.ms() / 20 * 20);
        ai_last_speech_capture = capture;
        if (!responses.empty() && !responses.back().first_capture) responses.back().first_capture = capture;
      }
      return speaking;
    });
  }

  struct Endpoint {
    rtp::Session& session;
    SendSide& tx;                       // The direction it sends.
    ReceiveSide& rx;                    // The direction it receives.
    sim::NetworkLink<Fwd>& out;         // The link it sends on.
    DirectionMeters& out_m;             // Its send direction's meters.
    const StreamConfigs& out_streams;   // Its send direction's streams.
    Report& tx_trace;                   // Where its send direction's traces go.
    Report& rx_trace;                   // Where its receive direction's traces go.
    Timestamp ran = Timestamp::MinusInfinity();
  };
  Endpoint ep[2] = {{user, up_tx, down_rx, forward, up_m, up_streams, rep, down},
                    {ai, down_tx, up_rx, reverse, down_m, down_streams, down, rep}};

  // Each direction's modules (#749): up.sender.* is the user's session and send side, up.receiver.* the AI's receive
  // streams; down.sender.* the AI's session and send side, down.receiver.* the user's receive streams.
  for (int i = 0; i < 2; ++i) {
    const std::string d = i == 0 ? "up." : "down.";
    const Endpoint& from = ep[i];
    rtp::Session& to = ep[1 - i].session;
    const ReceiveSide& rx = ep[1 - i].rx;
    const Scenario& ds = *dir_s[i];
    registry.Register(d + "sender.session", &from.session);
    registry.Register(d + "sender.send_side", &from.session.send_side());
    registry.Register(d + "sender.gcc", &from.session.send_side().gcc());
    registry.Register(d + "sender.pacer", &from.session.send_side().pacer());
    registry.Register(d + "sender.outage", &from.session.send_side().outage());
    registry.Register(d + "sender.rtcp", &from.session.rtcp());
    registry.Register(d + "receiver.freezes", &rx.freezes());
    if (to.av_sync()) registry.Register(d + "receiver.av_sync", to.av_sync());
    if (s.audio) {
      registry.Register(d + "sender.audio", &from.session.audio_sender(from.tx.audio_send()));
      registry.Register(d + "receiver.audio", &to.audio_receiver(rx.audio_recv()));
      registry.Register(d + "receiver.neteq", &to.audio_receiver(rx.audio_recv()).jitter_buffer());
    }
    if (ds.video) {
      registry.Register(d + "sender.frame_dropper", &from.tx.video_ctl().frame_dropper());
      registry.Register(d + "sender.bitrate_adjuster", &from.tx.video_ctl().bitrate_adjuster());
      registry.Register(d + "sender.video", &from.session.video_sender(from.tx.video_send()));
      const rtp::RtpReceiver& vr = to.video_receiver(rx.video_recv());
      registry.Register(d + "receiver.video", &vr);
      registry.Register(d + "receiver.nack", &vr.nack());
      registry.Register(d + "receiver.packet_buffer", &vr.packet_buffer());
      registry.Register(d + "receiver.keyframes", &vr.keyframes());
    }
  }

  // Sends what an endpoint has ready onto its link, with the send direction's accounting.
  auto put_on_link = [&](Endpoint& e, Timestamp now) {
    DirectionMeters& dm = e.out_m;
    const StreamConfigs& st = e.out_streams;
    for (auto& p : e.session.TakeOutgoing()) {
      Kind kind = ToKind(p.kind);
      if (kind == Kind::kPadding) {
        if (auto q = RtpPacket::Parse(p.bytes); q && q->ssrc == st.rtx_ssrc && q->payload.size() > 2) kind = Kind::kRtx;
      }
      dm.repair.OnSent(LinkKindOf(kind), p.bytes.size(), kind == Kind::kVideo ? RtpSeq(p.bytes) : 0, now.ms());
      if (kind != Kind::kRtcp) dm.w_send += p.wire_size.bytes();
      if (kind == Kind::kAudio) {
        if (!dm.audio_ts_first) {
          dm.audio_ts_first = std::make_pair(RtpTimestamp(p.bytes), now);
          dm.audio_start_seq = RtpSeq(p.bytes);
          dm.audio_first_marker = (p.bytes[1] & 0x80) != 0;
        }
        dm.audio_ts_last = std::make_pair(RtpTimestamp(p.bytes), now);
      }
      if (kind == Kind::kPadding) {
        if (auto q = RtpPacket::Parse(p.bytes)) (q->ssrc == st.rtx_ssrc ? dm.padding_rtx_ssrc : dm.padding_media_ssrc)++;
        dm.padding_sizes.insert(p.wire_size.bytes());
      }
      if (kind == Kind::kVideo) {
        if (!dm.video_start_seq) dm.video_start_seq = RtpSeq(p.bytes);
        auto ft = dm.frame_times.find(RtpTimestamp(p.bytes));
        if (ft != dm.frame_times.end()) ft->second.last_sent = now;
      }
      const int32_t seq = kind == Kind::kRtcp || p.bytes.size() < 4 ? -1 : RtpSeq(p.bytes);
      if (opt.link_trace && kind == Kind::kAudio) TraceAudioRtp(p.bytes, now.us(), st.audio_ssrc, e.tx_trace.audio_send_trace);
      e.out.Send(now, Fwd{kind, std::move(p.bytes)}, p.wire_size, LinkKindOf(kind), seq);
    }
  };

  auto advance_links = [&](Timestamp at) {
    auto [fwd_changed, rev_changed] = links.Advance(at - Timestamp::Micros(0));
    if (fwd_changed) forward.SetConfig(fwd_state.cfg);
    if (rev_changed) reverse.SetConfig(rev_state.cfg);
  };

  // --- State ----------------------------------------------------------------
  const int64_t end_ms = s.duration.ms();
  double cross_debt[2] = {0, 0};
  std::vector<double> queue_samples[2], rtt_samples[2];
  std::vector<Sample> samples[2];

  for (int64_t t = 0; t < end_ms; ++t) {
    const Timestamp now = Timestamp::Millis(t);
    const Timestamp prev = now - TimeDelta::Millis(1);
    for (Endpoint& e : ep) e.tx.new_rtt().reset();

    // --- The last millisecond's events, each at its own time (as RunCall) ----
    // Packets for the user arrive on the reverse link, for the AI on the forward link. At one time: the user's
    // timers, arrivals and finished frames, then the AI's, as a one-way call runs the sender's before the receiver's.
    std::vector<std::pair<Timestamp, Fwd>> in[2];
    reverse.ReceiveEach(now, [&](Fwd w, Timestamp at) {
      if (w.kind != Kind::kCross) in[0].emplace_back(at, std::move(w));
    });
    forward.ReceiveEach(now, [&](Fwd w, Timestamp at) {
      if (w.kind != Kind::kCross) in[1].emplace_back(at, std::move(w));
    });
    auto timer_due = [&](const Endpoint& e) {
      const Timestamp from = std::max(e.ran, prev);
      const Timestamp due = e.session.NextProcessTime(from + clock0) - clock0;
      return due > from && due <= now ? due : Timestamp::PlusInfinity();
    };
    auto run = [&](Endpoint& e, Timestamp at) {
      e.session.Process(at + clock0);
      e.ran = at;
      put_on_link(e, at);
      e.tx.OnEvents(at);
      e.tx.PollPendingFrame(at);
      e.rx.Render(at, e.rx_trace);
    };
    size_t next[2] = {0, 0};
    for (;;) {
      Timestamp due[2][3];
      Timestamp at = Timestamp::PlusInfinity();
      for (int i = 0; i < 2; ++i) {
        due[i][0] = timer_due(ep[i]);
        due[i][1] = next[i] < in[i].size() ? in[i][next[i]].first : Timestamp::PlusInfinity();
        due[i][2] = ep[i].tx.EncodedDue(now);
        for (const Timestamp d : due[i]) at = std::min(at, d);
      }
      if (!at.IsFinite()) break;
      advance_links(at);
      for (int i = 0; i < 2; ++i) {
        Endpoint& e = ep[i];
        if (due[i][0] == at) {
          run(e, at);
        } else if (due[i][1] == at) {
          const Fwd& w = in[i][next[i]++].second;
          // RTCP goes to the send side (report blocks, the RTX phase), media to the receive side.
          if (w.kind == Kind::kRtcp) {
            e.tx.OnFeedback(w.bytes, at, e.tx_trace);
          } else {
            e.rx.OnPacket(w, at);
          }
          run(e, at);
        } else if (due[i][2] == at) {
          e.tx.FinishOldestEncode();
          run(e, at);
        } else {
          continue;
        }
        break;
      }
    }

    advance_links(now);
    for (Endpoint& e : ep) e.tx.OnEvents(now);

    // An AI turn starts: its source begins generating the reply (#758).
    while (next_ai_turn < ai_turns.size() && ai_turns[next_ai_turn].start <= now) {
      if (ai_source) {
        const AiSourceStats before = ai_source->stats();
        if (before.turns > 0 && before.first_frame_delay) ai_first_frame_ms.push_back(before.first_frame_delay->us() / 1000.0);
        ai_source->StartTurn(ai_turns[next_ai_turn].start, ai_turns[next_ai_turn].end - ai_turns[next_ai_turn].start);
      }
      ++next_ai_turn;
    }

    // --- Capture: each endpoint's audio, then video ---------------------------
    for (Endpoint& e : ep) {
      e.tx.CaptureAudio(t, now);
      e.tx.CaptureVideo(t, now);
    }

    // --- Cross traffic on both links, then the queues are sampled (as RunCall) ---
    const LinkState* states[2] = {&fwd_state, &rev_state};
    for (int i = 0; i < 2; ++i) {
      if (states[i]->cross.bps() <= 0) continue;
      cross_debt[i] += static_cast<double>(states[i]->cross.bps()) / 8 / 1000;
      while (cross_debt[i] >= 1200) {
        cross_debt[i] -= 1200;
        ep[i].out.Send(now, Fwd{}, DataSize::Bytes(1200));
      }
    }
    const double queue_ms[2] = {QueueDelayMs(forward, fwd_state, now), QueueDelayMs(reverse, rev_state, now)};

    // A host bug or a host-measured RTT, handed to both sessions (issues #441, #451).
    if (s.host_rtt_inf && t == s.host_rtt_inf->ms()) {
      user.SetRtt(TimeDelta::PlusInfinity());
      ai.SetRtt(TimeDelta::PlusInfinity());
    }
    if (s.host_rtt && t >= s.host_rtt->first.ms()) {
      user.SetRtt(s.host_rtt->second);
      ai.SetRtt(s.host_rtt->second);
    }

    // --- Each endpoint: its timers, render, the audio device's 10 ms pull, then its packets go out ---
    for (Endpoint& e : ep) {
      e.session.Process(now + clock0);
      e.ran = now;
      e.rx.Render(now, e.rx_trace);
      e.rx.TraceSync(t, now, e.rx_trace);
      e.rx.PullAudio(t, now, e.rx_trace);
      put_on_link(e, now);
      e.tx.OnEvents(now);
      e.tx.PollPendingFrame(now);
    }

    // --- The AI listens (#753, #754), and what the user hears of it ----------
    if (!s.turns.empty() && s.audio) {
      if (up_rx.last_pull_time() == now) {
        const bool ai_speaking = in_any(ai_turns, now);
        if (const std::optional<TurnEvent> ev = ai_detector.Process(up_rx.last_pull(), now, ai_speaking)) {
          if (ev->type == TurnEventType::kEndOfTurn && !ai_speaking && next_reply < ai_script.size()) {
            const AiReply& r = ai_script[next_reply++];
            ai_turns.push_back({ev->at + r.after, ev->at + r.after + r.len});
            const Window* u = user_window_of(ev->last_speech);
            responses.push_back({u ? u->end : ev->last_speech, r.after, std::nullopt, std::nullopt});
          } else if (ev->type == TurnEventType::kBargeIn && ai_speaking) {
            // The AI stops its speech at once: the rest of the reply is not sent.
            ai_turns.back().end = ev->at;
            const Window* u = user_window_of(ev->speech_start);
            stops.push_back({u ? u->start : ev->speech_start, ev->at, ai_last_speech_capture, std::nullopt});
          }
        }
      }
      if (down_rx.last_pull_time() == now) {
        if (const std::optional<Timestamp> heard = down_rx.last_pull_capture()) {
          for (Response& r : responses) {
            if (r.first_capture && !r.played && *heard >= *r.first_capture) r.played = now;
          }
          for (Stop& st : stops) {
            if (!st.at_ear && (!st.last_capture || *heard >= *st.last_capture + TimeDelta::Millis(20))) st.at_ear = now;
          }
        }
      }
    }

    // --- Measurement, per direction (0 up, 1 down) ----------------------------
    for (int i = 0; i < 2; ++i) {
      if (t % 10 == 0) queue_samples[i].push_back(queue_ms[i]);
      ep[1 - i].rx.TraceRxTiming(t, ep[i].tx_trace);
      if (opt.video_trace && t % 1000 == 999) {
        DirectionMeters& vm = ep[i].out_m;
        vm.vt_row.t_ms = t + 1;
        ep[i].tx.FillTraceRow(now);
        ep[1 - i].rx.FillTraceRow();
        ep[i].tx_trace.video_trace.push_back(vm.vt_row);
        vm.vt_row = VideoTraceRow{};
      }
      if (t % 100 != 99) continue;
      const Endpoint& from = ep[i];
      const ReceiveSide& to = ep[1 - i].rx;
      DirectionMeters& dm = from.out_m;
      const LinkState& st = *states[i];
      Sample smp;
      smp.t_s = static_cast<double>(t + 1) / 1000.0;
      smp.capacity_kbps = st.cfg.down ? 0 : st.cfg.capacity.IsFinite() ? st.cfg.capacity.kbps() : -1;
      smp.target_kbps = from.session.rates().target.kbps();
      smp.send_kbps = static_cast<double>(dm.w_send) * 8 / 100;
      smp.goodput_kbps = static_cast<double>(dm.w_good) * 8 / 100;
      smp.queue_ms = queue_ms[i];
      // The sending session's CallStats RTT, as the one-way call samples its sender's (issue #370).
      const double call_rtt_ms = SessionGauge(from.session, "call_stats_rtt_ms");
      smp.rtt_ms = call_rtt_ms > 0 ? call_rtt_ms : 0;
      if (call_rtt_ms > 0) rtt_samples[i].push_back(call_rtt_ms);
      smp.audio_buffer_ms = to.audio_buffer_ms();
      smp.audio_concealed_pct = dm.w_played ? 100.0 * static_cast<double>(dm.w_conceal) / static_cast<double>(dm.w_played) : 0;
      smp.video_fps = static_cast<double>(dm.w_frames) * 10;
      smp.video_delay_ms = to.last_video_delay();
      smp.fec_pct = dir_s[i]->video ? from.session.video_sender(from.tx.video_send()).fec_params().fec_rate * 100.0 / 256 : 0;
      samples[i].push_back(smp);
      dm.w_send = dm.w_good = dm.w_frames = dm.w_conceal = dm.w_played = 0;
    }
  }

  for (Report* dr : {&rep, &down}) {
    for (cc::BweTraceRow& r : dr->bwe_trace) {
      using K = cc::BweTraceRow::Kind;
      if (r.kind == K::kRxTiming || r.kind == K::kSync) continue;
      r.time_us -= clock0.us();
      if ((r.kind == K::kDetector || r.kind == K::kPacer) && r.send_ms >= 0) r.send_ms -= static_cast<double>(clock0.ms());
    }
  }

  // --- Summary, per direction -------------------------------------------------
  // The one-way call's metrics for each direction, prefixed: up.* (user to AI) and down.* (AI to user).
  const sim::NetworkLink<Fwd>* link_of[2] = {&forward, &reverse};
  const std::vector<LinkChange>* changes_of[2] = {&s.forward, &s.reverse};
  const char* prefix[2] = {"up.", "down."};
  for (int i = 0; i < 2; ++i) {
    SendSide& tx = ep[i].tx;
    ReceiveSide& rx = ep[1 - i].rx;
    DirectionMeters& dm = ep[i].out_m;
    Report d;
    d.scenario = s;
    d.timeseries = samples[i];
    auto& m = d.metrics;
    const bool video = dir_s[i]->video;
    // The receive side adds the A/V offsets to the direction's video trace, as in a one-way call (AddAvSyncToVideoTrace).
    d.video_trace = std::move(ep[i].tx_trace.video_trace);
    rx.AddMetrics(end_ms, tx.frames_encoded(), d);
    ep[i].tx_trace.video_trace = std::move(d.video_trace);
    if (video) {
      m["video.avg_kbps"] = static_cast<double>(tx.video_bytes_encoded()) * 8 / s.duration.seconds() / 1000;
      m["video.qp_mean"] = tx.frames_encoded() ? static_cast<double>(tx.qp_sum()) / static_cast<double>(tx.frames_encoded()) : 0;
      m["video.encode_height_mean"] =
          tx.frames_encoded() ? static_cast<double>(tx.height_sum()) / static_cast<double>(tx.frames_encoded()) : 0;
      m["video.keyframes"] = static_cast<double>(tx.keyframes_sent());
      m["video.keyframe_requests"] = static_cast<double>(tx.keyframe_requests());
      m["video.encoder_dropped"] = static_cast<double>(tx.frames_skipped());
      m["video.encoder_target_avg_kbps"] =
          tx.frames_encoded() + tx.frames_skipped()
              ? static_cast<double>(tx.encoder_target_bps_sum()) / static_cast<double>(tx.frames_encoded() + tx.frames_skipped()) / 1000
              : 0;
    }
    const double demand_kbps = (video ? s.video_max.kbps() : 0) + audio_wire_rate.kbps();
    AddCcMetrics(s, *changes_of[i], demand_kbps, d);
    m["cc.queue_p95_ms"] = Percentile(queue_samples[i], 95);
    m["cc.queue_max_ms"] = queue_samples[i].empty() ? 0 : *std::max_element(queue_samples[i].begin(), queue_samples[i].end());
    {
      size_t over = 0;
      for (double q : queue_samples[i]) over += q > 200;
      m["cc.queue_over_200ms_pct"] =
          queue_samples[i].empty() ? 0 : 100.0 * static_cast<double>(over) / static_cast<double>(queue_samples[i].size());
    }
    m["cc.rtt_p50_ms"] = Percentile(rtt_samples[i], 50);
    m["rtcp.audio_report_blocks"] = static_cast<double>(tx.audio_reports().size());
    const auto& ls = link_of[i]->stats();
    m["net.loss_pct"] = ls.sent ? 100.0 * static_cast<double>(ls.lost + ls.queue_dropped) / static_cast<double>(ls.sent) : 0;
    AddLinkKindMetrics(ls, s, d);
    if (dm.audio_ts_first && dm.audio_ts_last->second - dm.audio_ts_first->second >= TimeDelta::Seconds(1)) {
      const double hz = static_cast<double>(static_cast<uint32_t>(dm.audio_ts_last->first - dm.audio_ts_first->first)) /
                        (static_cast<double>((dm.audio_ts_last->second - dm.audio_ts_first->second).us()) / 1e6);
      m["net.audio_rtp_clock_hz"] = std::round(hz / 1000) * 1000;
    }
    if (dm.video_start_seq) m["net.video_start_seq"] = *dm.video_start_seq;
    dm.repair.AddMetrics(m, video, end_ms);
    for (const auto& [name, value] : m) rep.metrics[prefix[i] + name] = value;
  }
  if (opt.link_trace) {
    rep.link_trace = forward.trace();
    down.link_trace = reverse.trace();
  }
  rep.audio_send_trace.insert(rep.audio_send_trace.end(), up_tx.opus_encode_rows().begin(), up_tx.opus_encode_rows().end());
  down.audio_send_trace.insert(down.audio_send_trace.end(), down_tx.opus_encode_rows().begin(), down_tx.opus_encode_rows().end());

  // The conversation (#753, #754): what the transport adds to the AI's response, from the user's last speech sample
  // captured to the AI's first sample played at the user, less the thinking time; and from the user starting to talk
  // over the AI to the AI's audio stopping at the user's ear.
  if (!s.turns.empty()) {
    std::vector<double> delays, stop_ms;
    for (const Response& r : responses) {
      // The AI's own decision time is not the transport's: its thinking time and the silence its endpointer waits for
      // before it calls the turn over (TurnDetectorConfig::end_of_turn_silence, 600 ms). A detection the transport
      // delays (loss, jitter, an outage) still counts.
      if (r.played) delays.push_back((*r.played - r.user_end - r.after - det_cfg.end_of_turn_silence).us() / 1000.0);
    }
    for (const Stop& st : stops) {
      if (st.at_ear) stop_ms.push_back((*st.at_ear - st.user_start).us() / 1000.0);
    }
    rep.metrics["conv.responses"] = static_cast<double>(delays.size());
    rep.metrics["conv.response_delay_p50_ms"] = Percentile(delays, 50);
    rep.metrics["conv.response_delay_p95_ms"] = Percentile(delays, 95);
    rep.metrics["conv.barge_ins"] = static_cast<double>(stop_ms.size());
    if (!stop_ms.empty()) rep.metrics["conv.barge_in_stop_ms"] = *std::max_element(stop_ms.begin(), stop_ms.end());
  }

  // The AI's source (#758): how far ahead of real time its queue got, the time from a turn's start to its first frame
  // sent, and the frames sent late because generation fell behind.
  if (ai_source) {
    const AiSourceStats& st = ai_source->stats();
    if (st.turns > 0 && st.first_frame_delay) ai_first_frame_ms.push_back(st.first_frame_delay->us() / 1000.0);
    rep.metrics["ai.turns"] = static_cast<double>(st.turns);
    rep.metrics["ai.queue_ahead_max_ms"] = st.max_queue_ahead.us() / 1000.0;
    rep.metrics["ai.first_frame_delay_max_ms"] =
        ai_first_frame_ms.empty() ? 0 : *std::max_element(ai_first_frame_ms.begin(), ai_first_frame_ms.end());
    rep.metrics["ai.underruns"] = static_cast<double>(st.underruns);
  }

  EvaluateExpectations(rep);
  if (opt.library_metrics) {
    MapMetricsWriter w;
    registry.Export(w);
    rep.library = w.values();
  }
  if (opt.timeseries) {
    rep.timeseries = samples[0];
    down.timeseries = samples[1];
  }
  return rep;
}

}  // namespace

Report RunCall(const Scenario& s, const RunOptions& opt) {
  if (s.two_way) return RunTwoWayCall(s, opt);
  Report rep;
  rep.scenario = s;

  EventLog events(64);
  events.SetCallback([&](const Event& e) { ++rep.events[std::string(e.module) + "/" + e.name]; });
  MetricsRegistry registry;

  // --- Links ----------------------------------------------------------------
  LinkTimeline links(s);
  const LinkState& fwd_state = links.forward();
  sim::NetworkLink<Fwd> forward(fwd_state.cfg, s.seed);
  if (opt.link_trace) forward.EnableTrace();
  sim::NetworkLink<Rev> reverse(links.reverse().cfg, s.seed + 1000003);
  if (opt.link_trace) reverse.EnableTrace();

  // --- Media sources --------------------------------------------------------
  // The codec's rate, which the RTP clock, NetEq and the playout run at. libwebrtc resamples the capture to
  // the encoder's rate before encoding (AudioState::UpdateAudioTransportWithSendingStreams), and Opus stamps
  // its packets with a 48 kHz clock whatever it encodes at (audio_encoder_opus.cc kRtpTimestampRateHz), so a
  // capture rate NetEq doesn't decode at (12, 24, 44.1 kHz) still plays: the bench runs those at 48 kHz
  // (issue #298). It keeps the capture rate at 8, 16, 32 and 48 kHz.
  const int sr = AudioCodecRate(s.audio_sample_rate);
  const size_t audio_frame_bytes = static_cast<size_t>(std::max<int64_t>(10, s.audio_bitrate.bps() / 8 / 50));
  const DataRate audio_wire_rate =
      s.audio ? DataRate::BitsPerSec((static_cast<int64_t>(audio_frame_bytes) * (s.red ? 2 : 1) + 12 + 4 + kUdpIpOverhead) * 8 * 50)
              : DataRate::Zero();

  // --- The two ends of the call ---------------------------------------------
  auto session_config = [&](uint32_t rtcp_ssrc, const char* cname) { return MakeSessionConfig(s, events, rtcp_ssrc, cname); };
  rtp::SessionConfig sender_cfg = session_config(kSenderRtcpSsrc, "sender@testbench");
  if (opt.bwe_trace) sender_cfg.send_side.gcc.bwe_trace = &rep.bwe_trace;
  // The audio send stream always sends its own RTCP, as libwebrtc's audio channel runs RtcpMode::kCompound whatever
  // the sync group (compare/webrtc/reference/audio/channel_send.cc:551); `av_sync off` only leaves the receiver's
  // sync group empty, as sm_compare does (issue #524; it used to turn the audio SR off too).
  sender_cfg.audio_rtcp = true;
  rtp::SessionConfig receiver_cfg = session_config(kReceiverRtcpSsrc, "receiver@testbench");
  // The receiver's first report goes out at half its nominal interval, as the
  // sender's does: libwebrtc's receive streams have their own RTCPSender, whose
  // constructor schedules report_interval_ / 2 (rtcp_sender.cc:171), so 0.5 s
  // with video (issue #360; the receiver used to wait 1 s).
  receiver_cfg.av_sync = s.av_sync;  // Issue #77.
  receiver_cfg.audio_receive_rtcp = s.audio_receive_rtcp;  // Issue #484.
  // The receiver measures its RTT as sm_compare's libwebrtc receiver does:
  // RRTR in its reports, the sender's DLRR echo (rtcp_xr.receiver_reference_time_report, issue #356).
  receiver_cfg.rtcp.xr_rrtr = s.video;
  rtp::Session sender(sender_cfg), receiver(receiver_cfg);

  // Both sessions (and the send-side video controller) run on the clock libwebrtc runs on in sm_compare: its
  // simulated clock starts at CallClockStartSeconds, not 0 (issue #897). The receive side's transport-cc feedback
  // puts its reference time on a 64 ms grid of that clock (rtcp::TransportFeedback::SetBase,
  // transport_feedback.cc:305-311) and culls old arrivals only once the clock passes 500 ms (MaybeCullOldPackets,
  // transport_sequence_number_feedback_generator.cc:59-69), so a clock that starts at 0 rounds receive times on
  // another grid than libwebrtc's; and the sender's NTP time (sender reports, capture times) is that clock's.
  // Every time handed to the library is in its clock (call time + clock0); every time read back from it is taken
  // back to call time, which the test bench measures in.
  const TimeDelta clock0 = TimeDelta::Seconds(CallClockStartSeconds(s));
  const TimeDelta rx0 = clock0;

  // One direction: the sender's send side and the receiver's receive side (#744, #745).
  DirectionMeters dm;
  const StreamConfigs streams = MakeStreamConfigs(s, sr, audio_wire_rate, dm);
  SendSide tx(s, opt, sender, streams, dm, sr, clock0);
  ReceiveSide rx(s, opt, receiver, streams, dm, events, sr, rx0, tx.audio_ts_offset());
  const rtp::AudioSendId audio_send = tx.audio_send();
  const rtp::VideoSendId video_send = tx.video_send();
  const rtp::AudioRecvId audio_recv = rx.audio_recv();
  const rtp::VideoRecvId video_recv = rx.video_recv();

  registry.Register("sender.session", &sender);
  registry.Register("sender.send_side", &sender.send_side());
  registry.Register("sender.gcc", &sender.send_side().gcc());
  registry.Register("sender.pacer", &sender.send_side().pacer());
  registry.Register("sender.outage", &sender.send_side().outage());
  registry.Register("sender.rtcp", &sender.rtcp());
  registry.Register("sender.frame_dropper", &tx.video_ctl().frame_dropper());
  registry.Register("sender.bitrate_adjuster", &tx.video_ctl().bitrate_adjuster());
  if (s.quality_scaling) {
    registry.Register("sender.video_send", &tx.video_ctl());
    registry.Register("sender.quality_scaler", &tx.video_ctl().quality_scaler());
  }
  if (s.video) registry.Register("sender.protection", &sender.fec_controller(video_send));
  registry.Register("receiver.session", &receiver);
  registry.Register("receiver.rtcp", &receiver.rtcp());
  registry.Register("receiver.freezes", &rx.freezes());
  if (receiver.av_sync()) registry.Register("receiver.av_sync", receiver.av_sync());
  if (s.video) {
    registry.Register("sender.video", &sender.video_sender(video_send));
    const rtp::RtpReceiver& vr = receiver.video_receiver(video_recv);
    registry.Register("receiver.video", &vr);
    registry.Register("receiver.nack", &vr.nack());
    if (vr.ulpfec()) registry.Register("receiver.ulpfec", vr.ulpfec());
    if (const auto* gen = sender.video_sender(video_send).ulpfec()) registry.Register("sender.ulpfec", gen);
    registry.Register("receiver.packet_buffer", &vr.packet_buffer());
    registry.Register("receiver.keyframes", &vr.keyframes());
    if (s.timing_v2) {
      registry.Register("receiver.vcm_timing", &vr.vcm_timing());
      registry.Register("receiver.frame_release", &vr.frame_release());
    } else {
      registry.Register("receiver.video_jitter", &vr.video_jitter());
      registry.Register("receiver.video_timing", &vr.video_timing());
      registry.Register("receiver.frame_scheduler", &vr.scheduler());
    }
  }
  if (s.audio) {
    registry.Register("sender.audio", &sender.audio_sender(audio_send));
    registry.Register("receiver.audio", &receiver.audio_receiver(audio_recv));
    registry.Register("receiver.red", &receiver.audio_receiver(audio_recv).red());
    registry.Register("receiver.neteq", &receiver.audio_receiver(audio_recv).jitter_buffer());
  }

  // --- State ----------------------------------------------------------------
  double cross_debt = 0;
  std::vector<double> rtt_samples, queue_samples;
  const int64_t end_ms = s.duration.ms();

  // Sends what a session has ready onto its link; returns nothing to count
  // beyond the pacer-released bytes the caller tallies.
  auto put_on_link = [&](rtp::Session& from, auto& link, auto make, Timestamp now, bool is_forward) {
    for (auto& p : from.TakeOutgoing()) {
      Kind kind = ToKind(p.kind);
      // Counted by what is on the wire, as sm_compare counts libwebrtc: an RTX packet that carries
      // a payload is a retransmission, also when it was sent as payload padding (#120).
      if (is_forward && kind == Kind::kPadding) {
        if (auto q = RtpPacket::Parse(p.bytes); q && q->ssrc == kRtxSsrc && q->payload.size() > 2) kind = Kind::kRtx;
      }
      if (is_forward) {
        dm.repair.OnSent(LinkKindOf(kind), p.bytes.size(), kind == Kind::kVideo ? RtpSeq(p.bytes) : 0, now.ms());
        if (kind != Kind::kRtcp) dm.w_send += p.wire_size.bytes();
        if (kind == Kind::kAudio) {
          if (!dm.audio_ts_first) {
            dm.audio_ts_first = std::make_pair(RtpTimestamp(p.bytes), now);
            dm.audio_start_seq = RtpSeq(p.bytes);
            dm.audio_first_marker = (p.bytes[1] & 0x80) != 0;
          }
          dm.audio_ts_last = std::make_pair(RtpTimestamp(p.bytes), now);
        }
        if (kind == Kind::kPadding) {
          if (auto q = RtpPacket::Parse(p.bytes)) (q->ssrc == kRtxSsrc ? dm.padding_rtx_ssrc : dm.padding_media_ssrc)++;
          dm.padding_sizes.insert(p.wire_size.bytes());
        }
        if (kind == Kind::kVideo) {
          if (!dm.video_start_seq) dm.video_start_seq = RtpSeq(p.bytes);
          auto ft = dm.frame_times.find(RtpTimestamp(p.bytes));
          if (ft != dm.frame_times.end()) ft->second.last_sent = now;
        }
      }
      const int32_t seq = kind == Kind::kRtcp || p.bytes.size() < 4 ? -1 : RtpSeq(p.bytes);
      if (opt.link_trace && is_forward && kind == Kind::kAudio) {
        TraceAudioRtp(p.bytes, now.us(), kAudioSsrc, rep.audio_send_trace);
      }
      // The frame trace's RTP fields (issue #437): timestamp, marker, an RTX packet's original sequence number
      // and whether a video packet belongs to a keyframe.
      int64_t rtp_ts = -1;
      int32_t marker = -1, osn = -1, key = -1, header = -1, rtx_room = -1;
      if (opt.link_trace && seq >= 0) {
        if (kind == Kind::kVideo) {
          if (auto it = dm.frame_rtx_room.find(RtpTimestamp(p.bytes)); it != dm.frame_rtx_room.end()) rtx_room = it->second;
        }
        header = TraceHeaderBytes(p.bytes, p.wire_size.bytes(), streams.video.ulpfec.red_payload_type);
        if (auto q = RtpPacket::Parse(p.bytes)) {
          rtp_ts = q->timestamp;
          marker = q->marker ? 1 : 0;
          if (q->ssrc == kRtxSsrc && q->payload.size() >= 2) osn = (q->payload[0] << 8) | q->payload[1];
          if (kind == Kind::kVideo) {
            // Behind RED, the primary block follows a one-byte header, as sm_compare's side skips it (#518).
            const size_t skip = q->payload_type == streams.video.ulpfec.red_payload_type && !q->payload.empty() ? 1 : 0;
            key = rtp::H264Depacketizer::Parse(std::span<const uint8_t>(q->payload).subspan(skip)).keyframe ? 1 : 0;
          }
        }
      }
      link.Send(now, make(kind, std::move(p.bytes)), p.wire_size, LinkKindOf(kind), seq);
      link.AnnotateLastTrace(rtp_ts, marker, osn, key, header, rtx_room);
    }
  };

  // Applies the scenario's link changes due by `at`, before a packet goes on a link then (as sm_compare's).
  auto advance_links = [&](Timestamp at) {
    auto [fwd_changed, rev_changed] = links.Advance(at - Timestamp::Micros(0));
    if (fwd_changed) forward.SetConfig(fwd_state.cfg);
    if (rev_changed) reverse.SetConfig(links.reverse().cfg);
  };

  // The last time each session ran, so the event loop runs a timer once, at its due time.
  Timestamp sender_ran = Timestamp::MinusInfinity(), receiver_ran = Timestamp::MinusInfinity();

  for (int64_t t = 0; t < end_ms; ++t) {
    const Timestamp now = Timestamp::Millis(t);

    // --- The last millisecond's events, each at its own time (issue #507) ----
    // libwebrtc's harness hands each packet that arrives on either link to its stack at the arrival time, and an
    // `encode_delay` frame to the sender when its encode is done; whatever the stack sends in response (a NACK, an
    // RTX packet, the frame's packets) goes on the link at that time (compare/webrtc/webrtc_call.cc post_at and
    // LinkTransport). So here each packet that arrived in (t - 1 ms, t] and each frame finished then goes to its
    // session at that time, in time order, followed by the session's Process and its packets on the link, also at
    // that time. Feedback that arrived during the last millisecond still comes before this tick's capture: after
    // the outage in issue #151 a NACK and the recovery keyframe meet in the same tick.
    tx.new_rtt().reset();  // The sender's RTT sample this tick, for the receiver.
    std::vector<std::pair<Timestamp, Rev>> rev_in;
    reverse.ReceiveEach(now, [&](Rev r, Timestamp at) { rev_in.emplace_back(at, std::move(r)); });
    std::vector<std::pair<Timestamp, Fwd>> fwd_in;
    forward.ReceiveEach(now, [&](Fwd w, Timestamp at) {
      if (w.kind != Kind::kCross) fwd_in.emplace_back(at, std::move(w));
    });
    // Each session's own timers (pacer, RTCP, transport feedback, frame release and render, decode time-out, NACK
    // tick) run at their due time too, as libwebrtc's delayed tasks do on SimTime (issue #518), not on the next
    // 1 ms tick. SimTime runs the due tasks of its queues in creation order (sender, then receiver, then the
    // network's; compare/webrtc/sim_time.cc RunReady), and a timer posted before the tick that posts a packet
    // arriving at its due time runs first (PostDelayedTask keeps posting order). So at one time: the sender's
    // timers, its arriving feedback and finished frames, then the receiver's timers, then its arriving packets.
    const Timestamp prev = now - TimeDelta::Millis(1);
    auto timer_due = [&](const rtp::Session& session, Timestamp ran, TimeDelta clock0) {
      const Timestamp from = std::max(ran, prev);
      const Timestamp due = session.NextProcessTime(from + clock0) - clock0;
      return due > from && due <= now ? due : Timestamp::PlusInfinity();
    };
    auto run_sender = [&](Timestamp at) {
      sender.Process(at + clock0);
      sender_ran = at;
      put_on_link(sender, forward, [](Kind k, std::vector<uint8_t> b) { return Fwd{k, std::move(b)}; }, at, true);
      tx.OnEvents(at);
      tx.PollPendingFrame(at);
    };
    auto run_receiver = [&](Timestamp at) {
      receiver.Process(at + rx0);
      receiver_ran = at;
      put_on_link(receiver, reverse, [](Kind, std::vector<uint8_t> b) { return Rev{std::move(b)}; }, at, false);
      rx.Render(at, rep);
    };
    for (size_t ri = 0, fi = 0;;) {
      const Timestamp ts = timer_due(sender, sender_ran, clock0);
      const Timestamp tr = ri < rev_in.size() ? rev_in[ri].first : Timestamp::PlusInfinity();
      const Timestamp te = tx.EncodedDue(now);
      const Timestamp tx_due = timer_due(receiver, receiver_ran, rx0);
      const Timestamp tf = fi < fwd_in.size() ? fwd_in[fi].first : Timestamp::PlusInfinity();
      const Timestamp at = std::min({ts, tr, te, tx_due, tf});
      if (!at.IsFinite()) break;
      advance_links(at);
      if (ts == at) {
        run_sender(at);
      } else if (tr == at) {
        tx.OnFeedback(rev_in[ri++].second.bytes, at, rep);
        run_sender(at);
      } else if (te == at) {
        tx.FinishOldestEncode();
        run_sender(at);
      } else if (tx_due == at) {
        run_receiver(at);
      } else {
        rx.OnPacket(fwd_in[fi++].second, at);
        run_receiver(at);
      }
    }

    // --- Link timeline ------------------------------------------------------
    advance_links(now);
    tx.OnEvents(now);

    // --- Sender: audio, then video --------------------------------------------
    tx.CaptureAudio(t, now);
    tx.CaptureVideo(t, now);

    // --- Cross traffic, then the sender: pacer and RTCP --------------------------
    // The cross traffic of a tick reaches the bottleneck before the call's packets of the same millisecond, as in
    // sm_compare, whose tick sends it before libwebrtc's pacer runs (issue #390). When the drop-tail queue is full,
    // the packet that arrives second is the one dropped; with the call's packets first, the cross traffic took
    // those drops, and the call kept more throughput than libwebrtc's on the same link.
    if (fwd_state.cross.bps() > 0) {
      cross_debt += static_cast<double>(fwd_state.cross.bps()) / 8 / 1000;
      while (cross_debt >= 1200) {
        cross_debt -= 1200;
        forward.Send(now, Fwd{}, DataSize::Bytes(1200));
      }
    }
    // The bottleneck queue is sampled here, after the tick's cross traffic and before the call's own packets of
    // this millisecond, as in sm_compare: its tick measures the link before libwebrtc's encoder and pacer tasks of
    // the same millisecond run (issue #410). Audio leaves on multiples of 10 ms and every third video frame on a
    // multiple of 100 ms, so sampling after them counted their own bytes in up to a tenth of the samples.
    const double queue_ms = QueueDelayMs(forward, fwd_state, now);
    sender.Process(now + clock0);
    sender_ran = now;
    put_on_link(sender, forward, [](Kind k, std::vector<uint8_t> b) { return Fwd{k, std::move(b)}; }, now, true);
    tx.PollPendingFrame(now);

    // --- Receiver -----------------------------------------------------------
    // The receiver learns each RTT sample the sender measures, for its
    // congestion controller and audio receivers (a one-way receiver has no
    // report blocks of its own). With video its CallStats takes the RTT it
    // measures over XR instead (issue #356); without, this one at the next 1 s
    // update.
    if (tx.new_rtt()) receiver.SetRtt(*tx.new_rtt());
    // A host bug: an infinite RTT handed to both sessions (issue #441).
    if (s.host_rtt_inf && t == s.host_rtt_inf->ms()) {
      sender.SetRtt(TimeDelta::PlusInfinity());
      receiver.SetRtt(TimeDelta::PlusInfinity());
    }
    // A host that measures the RTT itself hands it to both sessions at every step from then on,
    // over what the call measures (issue #451).
    if (s.host_rtt && t >= s.host_rtt->first.ms()) {
      sender.SetRtt(s.host_rtt->second);
      receiver.SetRtt(s.host_rtt->second);
    }
    receiver.Process(now + rx0);
    receiver_ran = now;
    rx.Render(now, rep);
    receiver.TakeEvents();

    rx.TraceSync(t, now, rep);
    rx.PullAudio(t, now, rep);

    // Receiver feedback (NACK, keyframe requests, transport-cc, reports).
    put_on_link(receiver, reverse, [](Kind, std::vector<uint8_t> b) { return Rev{std::move(b)}; }, now, false);

    // --- Measurement ----------------------------------------------------------
    if (t % 10 == 0) queue_samples.push_back(queue_ms);
    rx.TraceRxTiming(t, rep);
    if (opt.video_trace && t % 1000 == 999) {
      dm.vt_row.t_ms = t + 1;
      tx.FillTraceRow(now);
      rx.FillTraceRow();
      rep.video_trace.push_back(dm.vt_row);
      dm.vt_row = VideoTraceRow{};
    }
    if (t % 100 == 99) {
      Sample smp;
      smp.t_s = static_cast<double>(t + 1) / 1000.0;
      smp.capacity_kbps = fwd_state.cfg.down ? 0 : fwd_state.cfg.capacity.IsFinite() ? fwd_state.cfg.capacity.kbps() : -1;
      smp.target_kbps = sender.rates().target.kbps();
      smp.send_kbps = static_cast<double>(dm.w_send) * 8 / 100;
      smp.goodput_kbps = static_cast<double>(dm.w_good) * 8 / 100;
      smp.queue_ms = queue_ms;
      // The sender's CallStats::LastProcessedRtt() (the 70/30 smoothed average of the 1 s reports), as
      // sm_compare's libwebrtc side samples Call::GetStats().rtt_ms (compare/webrtc/webrtc_call.cc,
      // reference/call/call.cc), not the last raw report-block RTT (issue #370). Sampled every 100 ms, not
      // once per RTCP sample: those are not evenly spaced in time (cross_traffic seed 1: median of the
      // RTCP samples 316 ms, of 100 ms samples 67 ms).
      const double call_rtt_ms = SessionGauge(sender, "call_stats_rtt_ms");
      smp.rtt_ms = call_rtt_ms > 0 ? call_rtt_ms : 0;
      if (call_rtt_ms > 0) rtt_samples.push_back(call_rtt_ms);
      smp.audio_buffer_ms = rx.audio_buffer_ms();
      smp.audio_concealed_pct = dm.w_played ? 100.0 * static_cast<double>(dm.w_conceal) / static_cast<double>(dm.w_played) : 0;
      smp.video_fps = static_cast<double>(dm.w_frames) * 10;
      smp.video_delay_ms = rx.last_video_delay();
      smp.fec_pct = s.video ? sender.video_sender(video_send).fec_params().fec_rate * 100.0 / 256 : 0;
      rep.timeseries.push_back(smp);
      dm.w_send = dm.w_good = dm.w_frames = dm.w_conceal = dm.w_played = 0;
    }
  }

  // The rows the library wrote are in its clock: back to call time, as sm_compare writes libwebrtc's (time and send
  // time call-relative; a detector row's arrival time is the feedback's receive time on the sender's clock, as
  // libwebrtc's TransportFeedbackAdapter gives it, so it stays) (issue #897).
  for (cc::BweTraceRow& r : rep.bwe_trace) {
    using K = cc::BweTraceRow::Kind;
    if (r.kind == K::kRxTiming || r.kind == K::kSync) continue;  // Written by the test bench, in call time.
    r.time_us -= clock0.us();
    if ((r.kind == K::kDetector || r.kind == K::kPacer) && r.send_ms >= 0) r.send_ms -= static_cast<double>(clock0.ms());
  }

  // --- Summary ----------------------------------------------------------------
  auto& m = rep.metrics;
  rx.AddMetrics(end_ms, tx.frames_encoded(), rep);
  if (s.video) {
    m["video.avg_kbps"] = static_cast<double>(tx.video_bytes_encoded()) * 8 / s.duration.seconds() / 1000;
    m["video.qp_mean"] = tx.frames_encoded() ? static_cast<double>(tx.qp_sum()) / static_cast<double>(tx.frames_encoded()) : 0;
    m["video.encode_height_mean"] =
        tx.frames_encoded() ? static_cast<double>(tx.height_sum()) / static_cast<double>(tx.frames_encoded()) : 0;
    m["video.keyframes"] = static_cast<double>(tx.keyframes_sent());
    m["video.keyframe_requests"] = static_cast<double>(tx.keyframe_requests());
    m["video.encoder_dropped"] = static_cast<double>(tx.frames_skipped());
    m["video.encoder_target_avg_kbps"] =
        tx.frames_encoded() + tx.frames_skipped()
            ? static_cast<double>(tx.encoder_target_bps_sum()) / static_cast<double>(tx.frames_encoded() + tx.frames_skipped()) / 1000
            : 0;
  }

  // Congestion control: utilization, queueing and settle time.
  const double demand_kbps = (s.video ? s.video_max.kbps() : 0) + audio_wire_rate.kbps();
  AddCcMetrics(s, demand_kbps, rep);
  m["cc.queue_p95_ms"] = Percentile(queue_samples, 95);
  m["cc.queue_max_ms"] = queue_samples.empty() ? 0 : *std::max_element(queue_samples.begin(), queue_samples.end());
  {
    size_t over = 0;
    for (double q : queue_samples) over += q > 200;
    m["cc.queue_over_200ms_pct"] = queue_samples.empty() ? 0 : 100.0 * static_cast<double>(over) / static_cast<double>(queue_samples.size());
  }
  m["cc.rtt_p50_ms"] = Percentile(rtt_samples, 50);
  m["rtcp.audio_report_blocks"] = static_cast<double>(tx.audio_reports().size());
  const auto& ls = forward.stats();
  m["net.loss_pct"] = ls.sent ? 100.0 * static_cast<double>(ls.lost + ls.queue_dropped) / static_cast<double>(ls.sent) : 0;
  AddLinkKindMetrics(ls, s, rep);
  if (dm.audio_ts_first && dm.audio_ts_last->second - dm.audio_ts_first->second >= TimeDelta::Seconds(1)) {
    const double hz = static_cast<double>(static_cast<uint32_t>(dm.audio_ts_last->first - dm.audio_ts_first->first)) /
                      (static_cast<double>((dm.audio_ts_last->second - dm.audio_ts_first->second).us()) / 1e6);
    rep.metrics["net.audio_rtp_clock_hz"] = std::round(hz / 1000) * 1000;
  }
  if (dm.audio_start_seq) {
    rep.metrics["net.audio_start_seq"] = *dm.audio_start_seq;
    rep.metrics["net.audio_first_marker"] = dm.audio_first_marker ? 1 : 0;
  }
  if (dm.video_start_seq) rep.metrics["net.video_start_seq"] = *dm.video_start_seq;
  rep.metrics["net.padding_media_ssrc_packets"] = static_cast<double>(dm.padding_media_ssrc);
  rep.metrics["net.padding_rtx_ssrc_packets"] = static_cast<double>(dm.padding_rtx_ssrc);
  rep.metrics["net.padding_distinct_sizes"] = static_cast<double>(dm.padding_sizes.size());
  if (opt.link_trace) rep.link_trace = forward.trace();
  if (opt.link_trace) rep.reverse_trace = reverse.trace();
  rep.audio_send_trace.insert(rep.audio_send_trace.end(), tx.opus_encode_rows().begin(), tx.opus_encode_rows().end());
  dm.repair.AddMetrics(m, s.video, end_ms);
  if (s.apm_rate) ProbeApm(*s.apm_rate, m);

  EvaluateExpectations(rep);
  if (opt.library_metrics) {
    MapMetricsWriter w;
    registry.Export(w);
    rep.library = w.values();
  }
  if (!opt.timeseries) rep.timeseries.clear();
  return rep;
}

namespace {

std::string Num(double v) {
  if (!std::isfinite(v)) return "null";
  char buf[40];
  if (v == std::floor(v) && std::fabs(v) < 1e15) std::snprintf(buf, sizeof(buf), "%.0f", v);
  else std::snprintf(buf, sizeof(buf), "%.4g", v);
  return buf;
}

std::string Str(const std::string& s) {
  std::string o = "\"";
  for (char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\t': o += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          o += buf;
        } else {
          o += c;
        }
    }
  }
  return o + "\"";
}

}  // namespace

std::string ToJson(const Report& r, bool include_timeseries, bool include_library) {
  std::string o = "{\n";
  o += "  \"scenario\": " + Str(r.scenario.name) + ",\n";
  o += "  \"description\": " + Str(r.scenario.description) + ",\n";
  o += "  \"passed\": " + std::string(r.passed ? "true" : "false") + ",\n";
  o += "  \"metrics\": {";
  bool first = true;
  for (const auto& [k, v] : r.metrics) {
    o += std::string(first ? "\n" : ",\n") + "    " + Str(k) + ": " + Num(v);
    first = false;
  }
  o += "\n  },\n  \"checks\": [";
  first = true;
  for (const auto& c : r.checks) {
    o += std::string(first ? "\n" : ",\n") + "    {\"expect\": " + Str(c.expectation.text) + ", \"actual\": " +
         (c.found ? Num(c.actual) : "null") + ", \"passed\": " + (c.passed ? "true" : "false") + "}";
    first = false;
  }
  o += "\n  ],\n  \"events\": {";
  first = true;
  for (const auto& [k, v] : r.events) {
    o += std::string(first ? "" : ", ") + Str(k) + ": " + std::to_string(v);
    first = false;
  }
  o += "}";
  if (include_library) {
    o += ",\n  \"library\": {";
    first = true;
    for (const auto& [k, v] : r.library) {
      o += std::string(first ? "\n" : ",\n") + "    " + Str(k) + ": " + Num(v);
      first = false;
    }
    o += "\n  }";
  }
  // The 100 ms samples: "timeseries", or "up.timeseries" and "down.timeseries" for a two-way call (#749).
  auto timeseries = [&](const std::string& key, const std::vector<Sample>& rows) {
    o += ",\n  " + Str(key) + ": {\n    \"columns\": [\"t_s\", \"capacity_kbps\", \"target_kbps\", \"send_kbps\", "
         "\"goodput_kbps\", \"queue_ms\", \"rtt_ms\", \"audio_buffer_ms\", \"audio_concealed_pct\", \"video_fps\", "
         "\"video_delay_ms\", \"fec_pct\"],\n    \"rows\": [";
    bool first_row = true;
    for (const auto& x : rows) {
      o += std::string(first_row ? "\n" : ",\n") + "      [" + Num(x.t_s) + "," + Num(x.capacity_kbps) + "," +
           Num(x.target_kbps) + "," + Num(x.send_kbps) + "," + Num(x.goodput_kbps) + "," + Num(x.queue_ms) + "," +
           Num(x.rtt_ms) + "," + Num(x.audio_buffer_ms) + "," + Num(x.audio_concealed_pct) + "," + Num(x.video_fps) +
           "," + Num(x.video_delay_ms) + "," + Num(x.fec_pct) + "]";
      first_row = false;
    }
    o += "\n    ]\n  }";
  };
  if (include_timeseries && r.down) {
    timeseries("up.timeseries", r.timeseries);
    timeseries("down.timeseries", r.down->timeseries);
  } else if (include_timeseries) {
    timeseries("timeseries", r.timeseries);
  }
  o += "\n}\n";
  return o;
}

std::string ToText(const Report& r) {
  std::string o = (r.passed ? "PASS " : "FAIL ") + r.scenario.name;
  if (!r.scenario.description.empty()) o += " - " + r.scenario.description;
  o += "\n";
  for (const auto& c : r.checks) {
    o += std::string("  [") + (c.passed ? "ok" : "FAILED") + "] " + c.expectation.text + "  (actual " +
         (c.found ? Num(c.actual) : std::string("<unknown metric>")) + ")\n";
  }
  for (const auto& [k, v] : r.metrics) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "  %-30s %s\n", k.c_str(), Num(v).c_str());
    o += buf;
  }
  return o;
}

}  // namespace sm::testbench
