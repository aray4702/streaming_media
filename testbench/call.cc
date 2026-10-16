#include "call.h"

#include "call_common.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <optional>
#include <set>
#include <unordered_map>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/random.h"
#include "sm/core/rtp_packet.h"
#include "sm/core/seq_num.h"
#include "sm/loss/protection_controller.h"
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
constexpr size_t kMaxVideoPayload = 1100;
// WebRTC counts an audio interruption when concealment lasts over 150 ms
// (RTCInboundRtpStreamStats.interruptionCount).
constexpr int64_t kInterruptionMs = 150;

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

// The modelled audio codec: a frame is identified by its RTP timestamp, and
// decodes to the matching 20 ms of the speech signal.
class SpeechDecoder : public rtp::AudioDecoder {
 public:
  SpeechDecoder(const std::vector<float>& speech, size_t frame_samples) : speech_(speech), frame_samples_(frame_samples) {}
  std::optional<std::vector<float>> Decode(std::span<const uint8_t>, uint32_t rtp_timestamp) override {
    size_t off = rtp_timestamp;
    if (off + frame_samples_ > speech_.size()) return std::nullopt;
    return std::vector<float>(speech_.begin() + static_cast<std::ptrdiff_t>(off),
                              speech_.begin() + static_cast<std::ptrdiff_t>(off + frame_samples_));
  }

 private:
  const std::vector<float>& speech_;
  size_t frame_samples_;
};

// Records which video media sequence numbers the receiver ended up with
// (originals, retransmissions and FEC recoveries).
class ReceivedSeqs : public rtp::VideoMediaObserver {
 public:
  void OnVideoMediaPacket(uint16_t seq, Timestamp) override { seqs_.insert(unwrap_.Unwrap(seq)); }
  bool Has(int64_t useq) const { return seqs_.count(useq) > 0; }

 private:
  SeqNumUnwrapper<uint16_t> unwrap_;
  std::set<int64_t> seqs_;
};

}  // namespace

const std::vector<std::pair<std::string, std::string>>& MetricCatalog() {
  static const std::vector<std::pair<std::string, std::string>> k = {
      {"audio.concealed_pct", "Share of played audio that was synthesized by concealment (after the first packet)"},
      {"audio.interruptions", "Concealment runs longer than 150 ms (WebRTC interruptionCount)"},
      {"audio.interruption_ms", "Total duration of those interruptions"},
      {"audio.longest_interruption_ms", "Longest continuous concealment run"},
      {"audio.jb_delay_p50_ms", "Median jitter buffer delay per decoded packet"},
      {"audio.jb_delay_p95_ms", "95th percentile jitter buffer delay"},
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
      {"video.keyframes", "Keyframes encoded"},
      {"video.keyframe_requests", "PLI/FIR received by the sender"},
      {"video.encoder_dropped", "Frames skipped by the frame dropper or a paused encoder"},
      {"video.undecodable_frames", "Frames received but discarded because a reference was missing"},
      {"video.residual_loss_pct", "Video media packets never recovered (after NACK/RTX and FEC)"},
      {"cc.target_avg_kbps", "Average GCC target bitrate"},
      {"cc.goodput_kbps", "Average media + repair bitrate delivered"},
      {"cc.utilization_pct", "Delivered bitrate / usable capacity, over samples where the link is up and finite"},
      {"cc.queue_p95_ms", "95th percentile bottleneck queue delay (bufferbloat)"},
      {"cc.queue_max_ms", "Maximum bottleneck queue delay"},
      {"cc.queue_over_200ms_pct", "Share of time the bottleneck queue exceeded 200 ms"},
      {"cc.send_over_target_ms", "Time after the first 2 s that the send rate (100 ms windows, all traffic) exceeded 1.5 x the GCC target"},
      {"cc.send_over_target_p95_pct", "95th percentile of how far the send rate (100 ms windows after the first 2 s) is above the GCC target, in % of the target (0 when at or below)"},
      {"cc.target_max_drop_pct", "Largest fall of the target below its peak of the preceding 5 s, outside the first 5 s and the 5 s after each link change"},
      {"cc.max_settle_ms", "Longest time after a link change until the target settles in [0.5, 1.0] x usable capacity"},
      {"cc.unsettled", "Link changes after which the target never settled before the next change"},
      {"cc.rtt_p50_ms", "Median RTT measured by RTCP"},
      {"net.loss_pct", "Forward link loss (random, burst, outage and queue drops)"},
      {"repair.overhead_pct", "(RTX + FEC packet bytes) / (audio + video media packet bytes), headers included"},
      {"repair.nack_recovered", "Packets recovered by retransmission"},
      {"repair.fec_recovered", "Packets recovered by FEC"},
  };
  return k;
}

void AddCcMetrics(const Scenario& s, double demand_kbps, Report& rep) {
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
      for (const auto& c : s.forward) excluded = excluded || (t >= c.at.seconds() && t < c.at.seconds() + 5.0);
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
  for (const auto& c : s.forward) {
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
  std::vector<double> encode, pacer, network, jitter;
  auto ms = [](TimeDelta d) { return static_cast<double>(d.us()) / 1000.0; };
  for (const auto& f : frames) {
    if (!f.rendered.IsFinite() || !f.capture.IsFinite() || !f.submitted.IsFinite() || !f.last_sent.IsFinite() ||
        !f.complete.IsFinite()) {
      continue;
    }
    encode.push_back(ms(f.submitted - f.capture));
    pacer.push_back(ms(f.last_sent - f.submitted));
    // A frame completed by a retransmission arrives after its last original
    // packet was sent; a frame completed by FEC can look complete before it.
    network.push_back(std::max(0.0, ms(f.complete - f.last_sent)));
    jitter.push_back(ms(f.rendered - f.complete));
  }
  if (encode.empty()) return;
  auto& m = rep.metrics;
  m["video.delay_encode_ms"] = Mean(encode);
  m["video.delay_pacer_p95_ms"] = Percentile(pacer, 95);
  m["video.delay_network_p95_ms"] = Percentile(network, 95);
  m["video.delay_jitter_buffer_p95_ms"] = Percentile(jitter, 95);
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

Report RunCall(const Scenario& s, const RunOptions& opt) {
  Report rep;
  rep.scenario = s;

  EventLog events(64);
  events.SetCallback([&](const Event& e) { ++rep.events[std::string(e.module) + "/" + e.name]; });
  MetricsRegistry registry;

  // --- Links ----------------------------------------------------------------
  LinkTimeline links(s);
  const LinkState& fwd_state = links.forward();
  sim::NetworkLink<Fwd> forward(fwd_state.cfg, s.seed);
  sim::NetworkLink<Rev> reverse(links.reverse().cfg, s.seed + 1000003);

  // --- Media sources --------------------------------------------------------
  const int sr = s.audio_sample_rate;
  const size_t frame_samples = static_cast<size_t>(sr / 50);  // 20 ms.
  std::vector<float> speech;
  if (s.audio) speech = sim::SpeechLike(sr, s.duration.seconds() + 1, s.seed + 7, true);
  const size_t audio_frame_bytes = static_cast<size_t>(std::max<int64_t>(10, s.audio_bitrate.bps() / 8 / 50));
  const DataRate audio_wire_rate =
      s.audio ? DataRate::BitsPerSec((static_cast<int64_t>(audio_frame_bytes) * (s.red ? 2 : 1) + 12 + 4 + kUdpIpOverhead) * 8 * 50)
              : DataRate::Zero();
  Random enc_rng(s.seed + 13);
  SpeechDecoder decoder(speech, frame_samples);
  rtp::H264PayloadParser parser;
  ReceivedSeqs received_seqs;

  // --- The two ends of the call ---------------------------------------------
  auto session_config = [&](uint32_t rtcp_ssrc, const char* cname) {
    rtp::SessionConfig c;
    c.local_ssrc = rtcp_ssrc;
    c.cname = cname;
    c.extensions.Register(1, sync::uri::kTransportCc);
    c.rtcp.has_video = s.video;
    c.send_side.gcc.min_rate = s.min_rate;
    c.send_side.gcc.start_rate = s.start_rate;
    c.send_side.gcc.max_rate = s.max_rate;
    c.send_side.transport_overhead_bytes = kUdpIpOverhead;
    if (s.pacer_v2) c.send_side.pacer.version = cc::PacerVersion::kV2;
    c.send_side.video_min = s.video_min;
    c.send_side.video_max = s.video_max;
    if (s.tcc_v2) c.transport_feedback_version = 2;
    if (s.rtx_v2) c.rtx_version = 2;
    c.events = &events;
    return c;
  };
  rtp::SessionConfig sender_cfg = session_config(kSenderRtcpSsrc, "sender@testbench");
  rtp::SessionConfig receiver_cfg = session_config(kReceiverRtcpSsrc, "receiver@testbench");
  // Receiver reports on the call's 1 s grid, half a period after the sender's.
  if (s.video) receiver_cfg.rtcp.first_report_delay = TimeDelta::Millis(1000);
  rtp::Session sender(sender_cfg), receiver(receiver_cfg);

  rtp::AudioStreamConfig audio_cfg;
  audio_cfg.ssrc = kAudioSsrc;
  audio_cfg.payload_type = kAudioPt;
  if (s.red) audio_cfg.red_payload_type = kAudioRedPt;
  audio_cfg.clock_rate = sr;
  audio_cfg.nominal_rate = audio_wire_rate;
  rtp::VideoStreamConfig video_cfg;
  video_cfg.ssrc = kVideoSsrc;
  video_cfg.payload_type = kVideoPt;
  video_cfg.rtx_ssrc = kRtxSsrc;
  video_cfg.rtx_payload_type = kRtxPt;
  video_cfg.fec = s.fec;
  video_cfg.max_payload_size = kMaxVideoPayload + 16;  // The session reserves 16 bytes for header extensions.
  video_cfg.parser = &parser;
  video_cfg.media_observer = &received_seqs;
  video_cfg.nack = s.nack;
  // A loss notification needs frame ids and dependencies from a generic
  // descriptor, which H.264 has none of (design 6.4): libwebrtc sends PLI and
  // NACK only, and so does the bench.
  video_cfg.keyframe.send_lntf = false;
  if (s.keyframe_v2) video_cfg.keyframe.version = 2;
  // The bench's H.264 stream has no picture id, so with NACK on libwebrtc sends
  // no video FEC (ShouldDisableRedAndUlpfec) and neither does the bench.
  const bool video_fec = s.video && rtp::VideoFecEnabled(video_cfg);
  video_cfg.deliver_bitstream = false;  // The bench has no decoder.

  rtp::AudioSendId audio_send;
  rtp::VideoSendId video_send;
  rtp::AudioRecvId audio_recv;
  rtp::VideoRecvId video_recv;
  if (s.audio) {
    audio_send = sender.AddAudioSend(audio_cfg);
    audio_recv = receiver.AddAudioReceive(audio_cfg, &decoder);
  }
  if (s.video) {
    video_send = sender.AddVideoSend(video_cfg);
    video_recv = receiver.AddVideoReceive(video_cfg);
  }

  // The encoder side: the frame dropper and the FEC protection decision.
  rtp::VideoSendControllerConfig vsc_cfg;
  vsc_cfg.keyframe_interval = s.keyframe_interval;
  vsc_cfg.fps = s.video_fps;
  vsc_cfg.bitrate_adjuster = s.bitrate_adjuster;
  rtp::VideoSendController video_ctl(vsc_cfg, sender);
  loss::ProtectionConfig prot_cfg;
  prot_cfg.events = &events;
  loss::ProtectionController protection(prot_cfg);
  video::FreezeDetector freezes(&events);
  registry.Register("sender.session", &sender);
  registry.Register("sender.send_side", &sender.send_side());
  registry.Register("sender.gcc", &sender.send_side().gcc());
  registry.Register("sender.pacer", &sender.send_side().pacer());
  registry.Register("sender.outage", &sender.send_side().outage());
  registry.Register("sender.rtcp", &sender.rtcp());
  registry.Register("sender.frame_dropper", &video_ctl.frame_dropper());
  registry.Register("sender.bitrate_adjuster", &video_ctl.bitrate_adjuster());
  registry.Register("sender.protection", &protection);
  registry.Register("receiver.session", &receiver);
  registry.Register("receiver.rtcp", &receiver.rtcp());
  registry.Register("receiver.freezes", &freezes);
  if (s.video) {
    registry.Register("sender.video", &sender.video_sender(video_send));
    const rtp::RtpReceiver& vr = receiver.video_receiver(video_recv);
    registry.Register("receiver.video", &vr);
    registry.Register("receiver.nack", &vr.nack());
    registry.Register("receiver.fec", &vr.fec());
    registry.Register("receiver.packet_buffer", &vr.packet_buffer());
    registry.Register("receiver.keyframes", &vr.keyframes());
    registry.Register("receiver.video_jitter", &vr.video_jitter());
    registry.Register("receiver.video_timing", &vr.video_timing());
    registry.Register("receiver.frame_scheduler", &vr.scheduler());
  }
  if (s.audio) {
    registry.Register("sender.audio", &sender.audio_sender(audio_send));
    registry.Register("receiver.audio", &receiver.audio_receiver(audio_recv));
    registry.Register("receiver.red", &receiver.audio_receiver(audio_recv).red());
    registry.Register("receiver.neteq", &receiver.audio_receiver(audio_recv).jitter_buffer());
  }

  // --- State ----------------------------------------------------------------
  int64_t frames_encoded = 0, frames_skipped = 0, keyframes_sent = 0, keyframe_requests = 0;
  int64_t video_bytes_encoded = 0;
  std::unordered_map<uint32_t, Timestamp> frame_capture;  // RTP timestamp -> capture time.
  std::unordered_map<uint32_t, VideoFrameTimes> frame_times;  // RTP timestamp -> its path (issue #60).
  int64_t media_bytes = 0, repair_bytes = 0;
  std::vector<int64_t> video_send_ms;  // By unwrapped media sequence number.
  SeqNumUnwrapper<uint16_t> video_sent_unwrap;
  double cross_debt = 0;

  std::vector<double> audio_net_delay, video_e2e, rtt_samples, queue_samples;
  bool audio_started = false;
  Timestamp audio_first = Timestamp::MinusInfinity(), video_first = Timestamp::MinusInfinity();
  Timestamp last_render = Timestamp::MinusInfinity();
  int64_t played_ms = 0, concealed_ms = 0, stretch_ms = 0, conceal_run = 0, longest_conceal = 0;
  int64_t interruptions = 0, interruption_ms = 0;
  int64_t frames_rendered = 0;
  std::vector<float> audio_out(static_cast<size_t>(sr / 100));

  // Per-sample window counters.
  int64_t w_send = 0, w_good = 0, w_frames = 0, w_conceal = 0, w_played = 0;
  double last_video_delay = 0;
  // Receiver-side burst tracking for the protection controller (a real
  // receiver would report this in RTCP XR); read when a report block arrives.
  uint64_t last_gaps = 0, last_missing = 0;

  const int64_t end_ms = s.duration.ms();
  int64_t next_frame_ms = 0;

  // Sends what a session has ready onto its link; returns nothing to count
  // beyond the pacer-released bytes the caller tallies.
  auto put_on_link = [&](rtp::Session& from, auto& link, auto make, Timestamp now, bool is_forward) {
    for (auto& p : from.TakeOutgoing()) {
      const Kind kind = ToKind(p.kind);
      if (is_forward) {
        if (kind == Kind::kAudio || kind == Kind::kVideo) media_bytes += static_cast<int64_t>(p.bytes.size());
        if (kind == Kind::kRtx || kind == Kind::kFec) repair_bytes += static_cast<int64_t>(p.bytes.size());
        if (kind != Kind::kRtcp) w_send += p.wire_size.bytes();
        if (kind == Kind::kVideo) {
          video_send_ms.push_back(now.ms());
          auto ft = frame_times.find(RtpTimestamp(p.bytes));
          if (ft != frame_times.end()) ft->second.last_sent = now;
          (void)video_sent_unwrap.Unwrap(RtpSeq(p.bytes));
        }
      }
      link.Send(now, make(kind, std::move(p.bytes)), p.wire_size);
    }
  };

  for (int64_t t = 0; t < end_ms; ++t) {
    const Timestamp now = Timestamp::Millis(t);

    // --- Link timeline ------------------------------------------------------
    auto [fwd_changed, rev_changed] = links.Advance(now - Timestamp::Micros(0));
    if (fwd_changed) forward.SetConfig(fwd_state.cfg);
    if (rev_changed) reverse.SetConfig(links.reverse().cfg);

    // --- Sender: audio --------------------------------------------------------
    if (s.audio && t % 20 == 0) {
      std::vector<uint8_t> frame(audio_frame_bytes, static_cast<uint8_t>(t / 20));
      sender.SendAudio(audio_send, {static_cast<uint32_t>(t * sr / 1000), frame, false}, now);
    }

    // --- Sender: video --------------------------------------------------------
    if (s.video && t >= next_frame_ms) {
      int64_t k = frames_encoded + frames_skipped + 1;
      next_frame_ms = k * 1000 / s.video_fps;
      // The library decides: encode or drop, keyframe or not, and the rate.
      const rtp::EncodeDecision dec = video_ctl.OnCapturedFrame({}, now);
      const DataRate vr = dec.target;
      const bool key = dec.keyframe;
      if (!dec.encode) {
        ++frames_skipped;
      } else {
        ++frames_encoded;
        double per_frame = static_cast<double>(vr.bps()) / 8 / s.video_fps;
        int64_t bytes = static_cast<int64_t>(key ? per_frame * 4 : per_frame * enc_rng.Uniform(0.8, 1.2));
        bytes = std::max<int64_t>(bytes, 200);
        video_ctl.OnEncoded({DataSize::Bytes(bytes), key}, now);
        if (key) ++keyframes_sent;
        video_bytes_encoded += bytes;
        const uint32_t rtp_ts = static_cast<uint32_t>(t * 90);
        frame_capture[rtp_ts] = now;
        frame_times[rtp_ts].capture = now;
        frame_times[rtp_ts].submitted = now;
        const auto frame = rtp::MakeFakeH264Frame(key, static_cast<size_t>(bytes));
        sender.SendVideo(video_send, {rtp_ts, frame, key}, now);
      }
    }

    // --- Sender: pacer, RTCP and cross traffic ----------------------------------
    sender.Process(now);
    put_on_link(sender, forward, [](Kind k, std::vector<uint8_t> b) { return Fwd{k, std::move(b)}; }, now, true);
    if (fwd_state.cross.bps() > 0) {
      cross_debt += static_cast<double>(fwd_state.cross.bps()) / 8 / 1000;
      while (cross_debt >= 1200) {
        cross_debt -= 1200;
        forward.Send(now, Fwd{}, DataSize::Bytes(1200));
      }
    }

    // --- Receiver -----------------------------------------------------------
    forward.ReceiveEach(now, [&](Fwd w, Timestamp at) {
      if (w.kind == Kind::kCross) return;
      int64_t wire = static_cast<int64_t>(w.bytes.size()) + kUdpIpOverhead;
      switch (w.kind) {
        case Kind::kAudio: {
          w_good += wire;
          int64_t capture_ms = static_cast<int64_t>(RtpTimestamp(w.bytes)) * 1000 / sr;
          audio_net_delay.push_back(static_cast<double>((at - Timestamp::Millis(capture_ms)).us()) / 1000.0);
          if (!audio_started) {
            audio_started = true;
            audio_first = at;
          }
          break;
        }
        case Kind::kVideo:
        case Kind::kRtx:
        case Kind::kFec: {
          w_good += wire;
          // The last media or RTX packet of a frame (RFC 4588 keeps the
          // original RTP timestamp) before it renders marks it complete.
          if (w.kind != Kind::kFec) {
            auto ft = frame_times.find(RtpTimestamp(w.bytes));
            if (ft != frame_times.end() && !ft->second.rendered.IsFinite()) ft->second.complete = at;
          }
          break;
        }
        default:
          break;
      }
      receiver.OnPacket(w.bytes, at);
    });
    // The receiver learns the RTT the sender measured (a one-way receiver has
    // no report blocks of its own to measure it from).
    receiver.SetRtt(sender.rtt());
    receiver.Process(now);

    // Render video.
    if (s.video) {
      for (const auto& f : receiver.TakeVideoFrames(video_recv)) {
        if (!f.render) continue;
        freezes.OnFrameRendered(now);
        ++frames_rendered;
        ++w_frames;
        if (!video_first.IsFinite()) video_first = now;
        last_render = now;
        if (auto ft = frame_times.find(f.rtp_timestamp); ft != frame_times.end()) ft->second.rendered = now;
        auto cap = frame_capture.find(f.rtp_timestamp);
        if (cap != frame_capture.end()) {
          last_video_delay = static_cast<double>((now - cap->second).us()) / 1000.0;
          video_e2e.push_back(last_video_delay);
          frame_capture.erase(cap);
        }
      }
    }
    receiver.TakeEvents();

    // Audio playout: the device pulls 10 ms.
    if (s.audio && t % 10 == 0 && audio_started) {
      auto op = receiver.GetAudio(audio_recv, audio_out, now);
      played_ms += 10;
      ++w_played;
      if (op == timing::AudioOperation::kAccelerate || op == timing::AudioOperation::kPreemptiveExpand) stretch_ms += 10;
      if (op == timing::AudioOperation::kExpand) {
        concealed_ms += 10;
        ++w_conceal;
        conceal_run += 10;
      } else if (conceal_run > 0) {
        longest_conceal = std::max(longest_conceal, conceal_run);
        if (conceal_run > kInterruptionMs) {
          ++interruptions;
          interruption_ms += conceal_run;
        }
        conceal_run = 0;
      }
    }

    // Receiver feedback (NACK, keyframe requests, transport-cc, reports).
    put_on_link(receiver, reverse, [](Kind, std::vector<uint8_t> b) { return Rev{std::move(b)}; }, now, false);

    // --- Sender: feedback -----------------------------------------------------
    reverse.ReceiveEach(now, [&](Rev r, Timestamp at) { sender.OnPacket(r.bytes, at); });
    for (const auto& e : sender.TakeEvents()) {
      video_ctl.OnSessionEvent(e);
      switch (e.type) {
        case rtp::SessionEventType::kKeyframeRequested:
          // A PLI or FIR names the media SSRC; the outage monitor's own request does not.
          if (e.ssrc != 0) ++keyframe_requests;
          break;
        case rtp::SessionEventType::kEncoderPause:
        case rtp::SessionEventType::kEncoderResume: break;  // Handled by the video controller.
        case rtp::SessionEventType::kRttUpdated:
          rtt_samples.push_back(static_cast<double>(e.rtt.us()) / 1000.0);
          break;
        case rtp::SessionEventType::kReportBlock:
          if (video_fec && e.ssrc == kVideoSsrc) {
            const auto& rs = receiver.video_receiver(video_recv).stats();
            uint64_t g = rs.gaps - last_gaps, m = rs.missing - last_missing;
            last_gaps = rs.gaps;
            last_missing = rs.missing;
            loss::ProtectionInput in;
            in.rtt = sender.rtt();
            in.loss_rate = e.loss_fraction;
            in.mean_burst_length = g ? static_cast<double>(m) / static_cast<double>(g) : 1.0;
            double pkts = std::max(1.0, static_cast<double>(sender.rates().video_target.bps()) / 8 / s.video_fps / kMaxVideoPayload);
            in.packets_per_frame = pkts;
            sender.SetFecParams(video_send, protection.Update(in, now).fec);
          }
          break;
        case rtp::SessionEventType::kTargetRateChanged: break;
      }
    }

    // --- Measurement ----------------------------------------------------------
    double queue_ms = QueueDelayMs(forward, fwd_state, now);
    if (t % 10 == 0) queue_samples.push_back(queue_ms);
    if (t % 100 == 99) {
      Sample smp;
      smp.t_s = static_cast<double>(t + 1) / 1000.0;
      smp.capacity_kbps = fwd_state.cfg.down ? 0 : fwd_state.cfg.capacity.IsFinite() ? fwd_state.cfg.capacity.kbps() : -1;
      smp.target_kbps = sender.rates().target.kbps();
      smp.send_kbps = static_cast<double>(w_send) * 8 / 100;
      smp.goodput_kbps = static_cast<double>(w_good) * 8 / 100;
      smp.queue_ms = queue_ms;
      smp.rtt_ms = static_cast<double>(sender.rtt().us()) / 1000.0;
      smp.audio_buffer_ms = s.audio ? receiver.audio_receiver(audio_recv).jitter_buffer().CurrentLevelMs() : 0;
      smp.audio_concealed_pct = w_played ? 100.0 * static_cast<double>(w_conceal) / static_cast<double>(w_played) : 0;
      smp.video_fps = static_cast<double>(w_frames) * 10;
      smp.video_delay_ms = last_video_delay;
      smp.fec_pct = s.video ? sender.video_sender(video_send).fec_params().protection * 100 : 0;
      rep.timeseries.push_back(smp);
      w_send = w_good = w_frames = w_conceal = w_played = 0;
    }
  }

  // --- Summary ----------------------------------------------------------------
  const Timestamp end = Timestamp::Millis(end_ms);
  auto& m = rep.metrics;
  if (conceal_run > 0) {
    longest_conceal = std::max(longest_conceal, conceal_run);
    if (conceal_run > kInterruptionMs) {
      ++interruptions;
      interruption_ms += conceal_run;
    }
  }
  if (s.audio) {
    const auto& jbs = receiver.audio_receiver(audio_recv).jitter_buffer().stats();
    m["audio.concealed_pct"] = played_ms ? 100.0 * static_cast<double>(concealed_ms) / static_cast<double>(played_ms) : 100.0;
    m["audio.interruptions"] = static_cast<double>(interruptions);
    m["audio.interruption_ms"] = static_cast<double>(interruption_ms);
    m["audio.longest_interruption_ms"] = static_cast<double>(longest_conceal);
    m["audio.jb_delay_p50_ms"] = jbs.jitter_buffer_delay_ms.Percentile(50);
    m["audio.jb_delay_p95_ms"] = jbs.jitter_buffer_delay_ms.Percentile(95);
    m["audio.net_delay_p95_ms"] = Percentile(audio_net_delay, 95);
    m["audio.mouth_to_ear_p95_ms"] = m["audio.net_delay_p95_ms"] + m["audio.jb_delay_p95_ms"];
    m["audio.stretch_pct"] = played_ms ? 100.0 * static_cast<double>(stretch_ms) / static_cast<double>(played_ms) : 0;
    m["audio.late_discards"] = static_cast<double>(jbs.packets_discarded_late);
    m["audio.time_to_first_ms"] = audio_first.IsFinite() ? static_cast<double>(audio_first.ms()) : static_cast<double>(end_ms);
  }
  if (s.video) {
    const rtp::RtpReceiver& vrx = receiver.video_receiver(video_recv);
    const auto& fz = freezes.stats();
    double freeze_count = static_cast<double>(fz.freeze_count);
    double freeze_total = fz.total_freezes_ms;
    double longest = fz.freeze_ms.count() ? fz.freeze_ms.max() : 0;
    double frame_ms = 1000.0 / s.video_fps;
    // Pauses as libwebrtc's VideoQualityObserver counts them: a gap of 5 s or more is
    // a pause, not a freeze. One still in progress at the end of the call is counted
    // too, so a blackout that lasts to the end is visible.
    double pause_count = static_cast<double>(fz.pause_count);
    double pause_total = fz.total_pauses_ms;
    if (!last_render.IsFinite()) {
      freeze_count = 1;
      freeze_total = longest = static_cast<double>(end_ms);
    } else {
      // A freeze still in progress when the call ends.
      double tail = static_cast<double>((end - last_render).us()) / 1000.0;
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
    m["video.frames_rendered"] = static_cast<double>(frames_rendered);
    m["video.render_fps"] = static_cast<double>(frames_rendered) / s.duration.seconds();
    m["video.render_ratio_pct"] = frames_encoded ? 100.0 * static_cast<double>(frames_rendered) / static_cast<double>(frames_encoded) : 0;
    m["video.freeze_count"] = freeze_count;
    m["video.freeze_total_ms"] = freeze_total;
    m["video.longest_freeze_ms"] = longest;
    m["video.pause_count"] = pause_count;
    m["video.pause_total_ms"] = pause_total;
    m["video.e2e_delay_p50_ms"] = Percentile(video_e2e, 50);
    m["video.e2e_delay_p95_ms"] = Percentile(video_e2e, 95);
    {
      std::vector<VideoFrameTimes> all;
      all.reserve(frame_times.size());
      for (const auto& [ts, ft] : frame_times) all.push_back(ft);
      AddVideoDelayMetrics(all, rep);
    }
    m["video.time_to_first_frame_ms"] = video_first.IsFinite() ? static_cast<double>(video_first.ms()) : static_cast<double>(end_ms);
    m["video.avg_kbps"] = static_cast<double>(video_bytes_encoded) * 8 / s.duration.seconds() / 1000;
    m["video.keyframes"] = static_cast<double>(keyframes_sent);
    m["video.keyframe_requests"] = static_cast<double>(keyframe_requests);
    m["video.encoder_dropped"] = static_cast<double>(frames_skipped);
    m["video.undecodable_frames"] = static_cast<double>(vrx.keyframes().stats().frames_dropped);
    {
      // Packets sent in the last second may still be in flight or under repair.
      int64_t counted = 0, lost = 0;
      for (size_t i = 0; i < video_send_ms.size(); ++i) {
        if (video_send_ms[i] > end_ms - 1000) break;
        ++counted;
        lost += !received_seqs.Has(static_cast<int64_t>(i));
      }
      m["video.residual_loss_pct"] = counted ? 100.0 * static_cast<double>(lost) / static_cast<double>(counted) : 0;
    }
    m["repair.nack_recovered"] = static_cast<double>(vrx.nack().stats().recovered);
    m["repair.fec_recovered"] = static_cast<double>(vrx.fec().stats().recovered);
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
  const auto& ls = forward.stats();
  m["net.loss_pct"] = ls.sent ? 100.0 * static_cast<double>(ls.lost + ls.queue_dropped) / static_cast<double>(ls.sent) : 0;
  m["repair.overhead_pct"] = media_bytes ? 100.0 * static_cast<double>(repair_bytes) / static_cast<double>(media_bytes) : 0;

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
  if (include_timeseries) {
    o += ",\n  \"timeseries\": {\n    \"columns\": [\"t_s\", \"capacity_kbps\", \"target_kbps\", \"send_kbps\", "
         "\"goodput_kbps\", \"queue_ms\", \"rtt_ms\", \"audio_buffer_ms\", \"audio_concealed_pct\", \"video_fps\", "
         "\"video_delay_ms\", \"fec_pct\"],\n    \"rows\": [";
    first = true;
    for (const auto& x : r.timeseries) {
      o += std::string(first ? "\n" : ",\n") + "      [" + Num(x.t_s) + "," + Num(x.capacity_kbps) + "," +
           Num(x.target_kbps) + "," + Num(x.send_kbps) + "," + Num(x.goodput_kbps) + "," + Num(x.queue_ms) + "," +
           Num(x.rtt_ms) + "," + Num(x.audio_buffer_ms) + "," + Num(x.audio_concealed_pct) + "," + Num(x.video_fps) +
           "," + Num(x.video_delay_ms) + "," + Num(x.fec_pct) + "]";
      first = false;
    }
    o += "\n    ]\n  }";
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
