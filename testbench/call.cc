#include "call.h"

#include "call_common.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <optional>
#include <set>
#include <unordered_map>

#include "sm/cc/frame_dropper.h"
#include "sm/cc/outage_monitor.h"
#include "sm/cc/pacer.h"
#include "sm/cc/send_side_bwe.h"
#include "sm/cc/transport_feedback.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/random.h"
#include "sm/core/seq_num.h"
#include "sm/loss/fec.h"
#include "sm/loss/keyframe_request.h"
#include "sm/loss/nack_tracker.h"
#include "sm/loss/protection_controller.h"
#include "sm/loss/red.h"
#include "sm/loss/rtx.h"
#include "sm/sim/network_link.h"
#include "sm/sim/signals.h"
#include "sm/timing/audio_jitter_buffer.h"
#include "sm/timing/packet_buffer.h"
#include "sm/timing/rtcp_stats.h"
#include "sm/timing/video_jitter_estimator.h"
#include "sm/video/frame_scheduler.h"
#include "sm/video/freeze_detector.h"

namespace sm::testbench {
namespace {

constexpr uint32_t kAudioSsrc = 1, kVideoSsrc = 2, kRtxSsrc = 3;
constexpr uint8_t kAudioPt = 111, kVideoPt = 96, kRtxPt = 97;
constexpr int64_t kUdpIpOverhead = 28;
constexpr size_t kMaxVideoPayload = 1100;
constexpr size_t kVideoHeader = 9;  // flags + 64-bit frame id (stands in for the codec descriptor).
// WebRTC counts an audio interruption when concealment lasts over 150 ms
// (RTCInboundRtpStreamStats.interruptionCount).
constexpr int64_t kInterruptionMs = 150;

enum class Kind : uint8_t { kAudio, kVideo, kRtx, kFec, kPadding, kSr, kCross };
struct Fwd {
  Kind kind = Kind::kCross;
  int32_t tseq = -1;  // Transport-wide sequence number, -1 = none.
  RtpPacket rtp;
  std::vector<uint8_t> rtcp;
};

enum class RKind : uint8_t { kFeedback, kNack, kPli, kLntf, kRr };
struct Rev {
  RKind kind = RKind::kFeedback;
  std::vector<uint8_t> bytes;
  std::vector<uint16_t> nacks;
};

void WriteVideoHeader(std::vector<uint8_t>& payload, bool start, bool key, int64_t frame_id) {
  payload[0] = static_cast<uint8_t>((start ? 1 : 0) | (key ? 2 : 0));
  for (int i = 0; i < 8; ++i) payload[1 + static_cast<size_t>(i)] = static_cast<uint8_t>(static_cast<uint64_t>(frame_id) >> (8 * i));
}

struct VideoHeader {
  bool start = false, key = false;
  int64_t frame_id = -1;
};

VideoHeader ReadVideoHeader(const std::vector<uint8_t>& payload) {
  VideoHeader h;
  if (payload.size() < kVideoHeader) return h;
  h.start = payload[0] & 1;
  h.key = payload[0] & 2;
  uint64_t id = 0;
  for (int i = 0; i < 8; ++i) id |= static_cast<uint64_t>(payload[1 + static_cast<size_t>(i)]) << (8 * i);
  h.frame_id = static_cast<int64_t>(id);
  return h;
}

// Sliding-window byte counter for rates.
class RateWindow {
 public:
  explicit RateWindow(TimeDelta window) : window_(window) {}
  void Add(Timestamp now, int64_t bytes) {
    q_.emplace_back(now, bytes);
    sum_ += bytes;
  }
  DataRate Rate(Timestamp now) {
    while (!q_.empty() && q_.front().first <= now - window_) {
      sum_ -= q_.front().second;
      q_.pop_front();
    }
    return DataRate::BitsPerSec(sum_ * 8 * 1000000 / window_.us());
  }

 private:
  TimeDelta window_;
  std::deque<std::pair<Timestamp, int64_t>> q_;
  int64_t sum_ = 0;
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
      {"video.longest_freeze_ms", "Longest gap between rendered frames (or call length if nothing rendered)"},
      {"video.e2e_delay_p50_ms", "Median capture -> render delay"},
      {"video.e2e_delay_p95_ms", "95th percentile capture -> render delay"},
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
      {"cc.max_settle_ms", "Longest time after a link change until the target settles in [0.5, 1.0] x usable capacity"},
      {"cc.unsettled", "Link changes after which the target never settled before the next change"},
      {"cc.rtt_p50_ms", "Median RTT measured by RTCP"},
      {"net.loss_pct", "Forward link loss (random, burst, outage and queue drops)"},
      {"repair.overhead_pct", "(RTX + FEC bytes) / media bytes"},
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

  // --- Sender ---------------------------------------------------------------
  cc::GccConfig gcfg;
  gcfg.min_rate = s.min_rate;
  gcfg.start_rate = s.start_rate;
  gcfg.max_rate = s.max_rate;
  gcfg.events = &events;
  cc::GccController gcc(gcfg);
  cc::PacerConfig pcfg;
  pcfg.events = &events;
  cc::Pacer pacer(pcfg);
  cc::OutageConfig ocfg;
  ocfg.events = &events;
  cc::OutageMonitor outage(ocfg);
  cc::FrameDropper dropper;
  loss::RedEncoder red_enc(1);
  loss::RtxConfig rtx_cfg;
  rtx_cfg.rtx_ssrc = kRtxSsrc;
  rtx_cfg.rtx_payload_type = kRtxPt;
  rtx_cfg.events = &events;
  loss::RtxSender rtx(rtx_cfg);
  loss::FecConfig fec_cfg;
  fec_cfg.events = &events;
  loss::FecEncoder fec_enc(fec_cfg);
  loss::ProtectionConfig prot_cfg;
  prot_cfg.events = &events;
  loss::ProtectionController protection(prot_cfg);
  timing::RttEstimator rtt_est;
  registry.Register("sender.gcc", &gcc);
  registry.Register("sender.pacer", &pacer);
  registry.Register("sender.outage", &outage);
  registry.Register("sender.frame_dropper", &dropper);
  registry.Register("sender.rtx", &rtx);
  registry.Register("sender.fec", &fec_enc);
  registry.Register("sender.protection", &protection);
  registry.Register("sender.rtt", &rtt_est);

  // --- Receiver -------------------------------------------------------------
  cc::TransportFeedbackBuilder feedback;
  loss::NackConfig nack_cfg;
  nack_cfg.events = &events;
  loss::NackTracker nack(nack_cfg);
  loss::FecDecoder fec_dec(fec_cfg);
  loss::RedReceiver red_rx;
  timing::PacketBufferConfig pb_cfg;
  pb_cfg.events = &events;
  timing::PacketBuffer packet_buffer(pb_cfg);
  loss::KeyframeRequestConfig kf_cfg;
  kf_cfg.events = &events;
  loss::KeyframeRequester keyframes(kf_cfg);
  timing::VideoJitterEstimator vjitter;
  timing::VideoTiming vtiming;
  video::FrameSchedulerConfig fs_cfg;
  fs_cfg.events = &events;
  video::FrameScheduler scheduler(fs_cfg);
  video::FreezeDetector freezes(&events);
  timing::AudioJitterBufferConfig jb_cfg;
  jb_cfg.sample_rate = s.audio_sample_rate;
  jb_cfg.events = &events;
  timing::AudioJitterBuffer jitter_buffer(jb_cfg);
  timing::ReceiveStatistics video_stats(kVideoSsrc, 90000);
  registry.Register("receiver.feedback", &feedback);
  registry.Register("receiver.nack", &nack);
  registry.Register("receiver.fec", &fec_dec);
  registry.Register("receiver.red", &red_rx);
  registry.Register("receiver.packet_buffer", &packet_buffer);
  registry.Register("receiver.keyframes", &keyframes);
  registry.Register("receiver.video_jitter", &vjitter);
  registry.Register("receiver.video_timing", &vtiming);
  registry.Register("receiver.frame_scheduler", &scheduler);
  registry.Register("receiver.freezes", &freezes);
  registry.Register("receiver.neteq", &jitter_buffer);
  registry.Register("receiver.video_rtp", &video_stats);

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

  // --- State ----------------------------------------------------------------
  std::unordered_map<uint64_t, Fwd> paced;  // Pacer id -> packet.
  uint64_t next_id = 0;
  uint16_t tseq = 0, audio_seq = 0, video_seq = 0;
  DataRate target = s.start_rate;
  TimeDelta rtt = TimeDelta::Millis(100);
  bool video_paused = false, keyframe_needed = true;
  int64_t frame_id = 0, frames_encoded = 0, frames_skipped = 0, keyframes_sent = 0, keyframe_requests = 0;
  int64_t video_bytes_encoded = 0;
  Timestamp last_keyframe = Timestamp::MinusInfinity();
  std::unordered_map<int64_t, Timestamp> frame_capture;  // Frame id -> capture time.
  std::unordered_map<int64_t, uint32_t> frame_rtp_ts;
  int64_t media_bytes = 0, repair_bytes = 0;
  std::vector<int64_t> video_send_ms;  // By unwrapped media sequence number.
  std::set<int64_t> video_seqs_received;
  SeqNumUnwrapper<uint16_t> video_unwrap;
  RateWindow send_rate(TimeDelta::Millis(500));
  // Receiver-side burst tracking for the protection controller (a real
  // receiver would report this in RTCP XR).
  int64_t highest_video_seq = -1;
  uint64_t gaps = 0, missing = 0, last_gaps = 0, last_missing = 0;

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

  auto apply = [&](const cc::NetworkControlUpdate& u) {
    target = u.target_rate;
    pacer.SetPacingRates(u.pacing_rate, DataRate::Zero());
    for (const auto& c : u.probes) pacer.AddProbeCluster(c);
    outage.SetCurrentRate(target);
  };
  auto enqueue = [&](Fwd f, cc::PacketPriority prio, Timestamp now) {
    int64_t size = static_cast<int64_t>(f.rtp.size()) + kUdpIpOverhead;
    paced[next_id] = std::move(f);
    pacer.Enqueue({next_id++, prio, DataSize::Bytes(size), now});
  };
  auto video_rate = [&]() {
    DataRate r = target - audio_wire_rate;
    r = r * (1.0 / (1.0 + fec_enc.params().protection));
    return std::clamp(r, s.video_min, s.video_max);
  };

  // Receiver: a video media packet (original, retransmitted or FEC-recovered).
  std::function<void(const RtpPacket&, Timestamp, bool)> on_video = [&](const RtpPacket& p, Timestamp at, bool feed_fec) {
    VideoHeader h = ReadVideoHeader(p.payload);
    int64_t useq = video_unwrap.Unwrap(p.seq);
    video_seqs_received.insert(useq);
    if (useq > highest_video_seq) {
      if (highest_video_seq >= 0 && useq > highest_video_seq + 1) {
        ++gaps;
        missing += static_cast<uint64_t>(useq - highest_video_seq - 1);
      }
      highest_video_seq = useq;
    }
    if (s.nack) nack.OnReceivedPacket(p.seq, h.key && h.start, at);
    packet_buffer.Insert(p, h.start, at);
    if (feed_fec && s.fec) {
      for (auto& r : fec_dec.OnMediaPacket(p)) on_video(r, at, false);
    }
  };

  apply(gcc.Start(Timestamp::Micros(0)));
  gcc.OnRttUpdate(rtt);
  keyframes.OnStreamStart(Timestamp::Micros(0));

  const int64_t end_ms = s.duration.ms();
  int64_t next_frame_ms = 0;
  double cross_debt = 0;

  for (int64_t t = 0; t < end_ms; ++t) {
    const Timestamp now = Timestamp::Millis(t);

    // --- Link timeline ------------------------------------------------------
    auto [fwd_changed, rev_changed] = links.Advance(now - Timestamp::Micros(0));
    if (fwd_changed) forward.SetConfig(fwd_state.cfg);
    if (rev_changed) reverse.SetConfig(links.reverse().cfg);

    // --- Sender: audio --------------------------------------------------------
    if (s.audio && t % 20 == 0) {
      uint32_t ts = static_cast<uint32_t>(t * sr / 1000);
      std::vector<uint8_t> frame(audio_frame_bytes, static_cast<uint8_t>(t / 20));
      RtpPacket p;
      p.ssrc = kAudioSsrc;
      p.payload_type = kAudioPt;
      p.seq = audio_seq++;
      p.timestamp = ts;
      p.payload = s.red ? red_enc.Encode(kAudioPt, ts, frame) : frame;
      media_bytes += static_cast<int64_t>(audio_frame_bytes);
      enqueue({Kind::kAudio, -1, std::move(p), {}}, cc::PacketPriority::kAudio, now);
    }

    // --- Sender: video --------------------------------------------------------
    if (s.video && t >= next_frame_ms) {
      int64_t k = frames_encoded + frames_skipped + 1;
      next_frame_ms = k * 1000 / s.video_fps;
      DataRate vr = video_rate();
      dropper.SetRates(vr, s.video_fps);
      bool periodic = s.keyframe_interval.us() > 0 && now - last_keyframe >= s.keyframe_interval;
      bool key = keyframe_needed || periodic;
      if (video_paused || (!key && dropper.ShouldDrop())) {
        ++frames_skipped;
      } else {
        ++frames_encoded;
        double per_frame = static_cast<double>(vr.bps()) / 8 / s.video_fps;
        int64_t bytes = static_cast<int64_t>(key ? per_frame * 4 : per_frame * enc_rng.Uniform(0.8, 1.2));
        bytes = std::max<int64_t>(bytes, 200);
        dropper.OnEncodedFrame(DataSize::Bytes(bytes), key);
        if (key) {
          keyframe_needed = false;
          last_keyframe = now;
          ++keyframes_sent;
        }
        video_bytes_encoded += bytes;
        int64_t id = frame_id++;
        frame_capture[id] = now;
        uint32_t rtp_ts = static_cast<uint32_t>(t * 90);
        frame_rtp_ts[id] = rtp_ts;
        size_t npkts = static_cast<size_t>((bytes + static_cast<int64_t>(kMaxVideoPayload) - 1) / static_cast<int64_t>(kMaxVideoPayload));
        for (size_t i = 0; i < npkts; ++i) {
          size_t len = static_cast<size_t>(bytes) / npkts + (i < static_cast<size_t>(bytes) % npkts ? 1 : 0);
          RtpPacket p;
          p.ssrc = kVideoSsrc;
          p.payload_type = kVideoPt;
          p.seq = video_seq++;
          p.timestamp = rtp_ts;
          p.marker = i + 1 == npkts;
          p.payload.assign(std::max(len, kVideoHeader), 0);
          WriteVideoHeader(p.payload, i == 0, key, id);
          media_bytes += static_cast<int64_t>(p.payload.size());
          video_send_ms.push_back(t);
          rtx.OnPacketSent(p, now);
          if (s.fec) {
            for (RtpPacket& f : fec_enc.AddMediaPacket(p, p.marker)) {
              repair_bytes += static_cast<int64_t>(f.payload.size());
              enqueue({Kind::kFec, -1, std::move(f), {}}, cc::PacketPriority::kFec, now);
            }
          }
          enqueue({Kind::kVideo, -1, std::move(p), {}}, cc::PacketPriority::kVideo, now);
        }
      }
    }

    // --- Sender: pacer and cross traffic ---------------------------------------
    for (const auto& sent : pacer.Process(now)) {
      Fwd w;
      if (sent.is_padding) {
        w.kind = Kind::kPadding;
      } else {
        auto it = paced.find(sent.id);
        if (it == paced.end()) continue;
        w = std::move(it->second);
        paced.erase(it);
      }
      w.tseq = tseq;
      gcc.OnPacketSent(tseq++, now, sent.size, sent.probe_cluster);
      send_rate.Add(now, sent.size.bytes());
      w_send += sent.size.bytes();
      forward.Send(now, std::move(w), sent.size);
    }
    if (fwd_state.cross.bps() > 0) {
      cross_debt += static_cast<double>(fwd_state.cross.bps()) / 8 / 1000;
      while (cross_debt >= 1200) {
        cross_debt -= 1200;
        forward.Send(now, Fwd{}, DataSize::Bytes(1200));
      }
    }
    if (s.video && t % 1000 == 500) {  // RTCP sender report.
      timing::SenderReport sr_msg;
      sr_msg.ssrc = kVideoSsrc;
      sr_msg.ntp = timing::NtpTime::FromTimestamp(now);
      sr_msg.rtp_timestamp = static_cast<uint32_t>(t * 90);
      Fwd w;
      w.kind = Kind::kSr;
      w.rtcp = timing::SerializeSenderReport(sr_msg);
      int64_t size = static_cast<int64_t>(w.rtcp.size()) + kUdpIpOverhead;
      forward.Send(now, std::move(w), DataSize::Bytes(size));
    }

    // --- Receiver -----------------------------------------------------------
    forward.ReceiveEach(now, [&](Fwd w, Timestamp at) {
      if (w.kind == Kind::kCross) return;
      if (w.tseq >= 0) feedback.OnPacket(static_cast<uint16_t>(w.tseq), at);
      int64_t wire = static_cast<int64_t>(w.rtp.size()) + kUdpIpOverhead;
      switch (w.kind) {
        case Kind::kAudio: {
          w_good += wire;
          int64_t capture_ms = static_cast<int64_t>(w.rtp.timestamp) * 1000 / sr;
          audio_net_delay.push_back(static_cast<double>((at - Timestamp::Millis(capture_ms)).us()) / 1000.0);
          std::vector<loss::RedBlock> blocks;
          if (s.red) {
            blocks = red_rx.OnPacket(w.rtp.payload, w.rtp.timestamp);
          } else {
            loss::RedBlock b;
            b.timestamp = w.rtp.timestamp;
            b.primary = true;
            blocks.push_back(std::move(b));
          }
          for (const auto& b : blocks) {
            size_t off = b.timestamp;
            if (off + frame_samples > speech.size()) continue;
            std::vector<float> pcm(speech.begin() + static_cast<std::ptrdiff_t>(off),
                                   speech.begin() + static_cast<std::ptrdiff_t>(off + frame_samples));
            jitter_buffer.InsertPacket(b.timestamp, std::move(pcm), at);
            if (!audio_started) {
              audio_started = true;
              audio_first = at;
            }
          }
          break;
        }
        case Kind::kVideo:
          w_good += wire;
          video_stats.OnPacket(w.rtp.seq, w.rtp.timestamp, at);
          on_video(w.rtp, at, true);
          break;
        case Kind::kRtx:
          w_good += wire;
          if (auto m = loss::RestoreFromRtx(w.rtp, kVideoSsrc, kVideoPt)) on_video(*m, at, true);
          break;
        case Kind::kFec:
          w_good += wire;
          if (s.fec) {
            for (auto& r : fec_dec.OnFecPacket(w.rtp)) on_video(r, at, false);
          }
          break;
        case Kind::kSr:
          if (auto sr_msg = timing::ParseSenderReport(w.rtcp)) video_stats.OnSenderReport(*sr_msg, at);
          break;
        default:
          break;
      }
    });

    // Assemble, decode and render video.
    for (auto& f : packet_buffer.PopCompleteFrames()) {
      if (f.packets.empty()) continue;
      VideoHeader h = ReadVideoHeader(f.packets.front().payload);
      if (h.frame_id < 0) continue;
      loss::FrameInfo info;
      info.frame_id = h.frame_id;
      info.keyframe = h.key;
      if (!h.key && h.frame_id > 0) info.references.push_back(h.frame_id - 1);
      keyframes.OnFrame(info, now);
      vjitter.OnFrame(f.timestamp, f.last_arrival, DataSize::Bytes(static_cast<int64_t>(f.size_bytes())));
      vtiming.IncomingTimestamp(f.timestamp, f.last_arrival);
    }
    keyframes.UpdateRtt(rtt);
    for (const auto& msg : keyframes.Process(now)) {
      // A loss notification is informational: the sender must not encode a
      // keyframe for it (only PLI and FIR request one).
      const RKind kind = msg.type == loss::FeedbackType::kLntf ? RKind::kLntf : RKind::kPli;
      reverse.Send(now, Rev{kind, {}, {}}, DataSize::Bytes(12 + 8 + kUdpIpOverhead));
    }
    if (s.nack && nack.TakeKeyframeRequest()) {
      reverse.Send(now, Rev{RKind::kPli, {}, {}}, DataSize::Bytes(12 + 8 + kUdpIpOverhead));
    }
    for (int64_t id : keyframes.TakeDecodableFrames()) {
      auto ts = frame_rtp_ts.find(id);
      if (ts == frame_rtp_ts.end()) continue;
      vtiming.SetJitterDelay(vjitter.JitterDelay());
      vtiming.AddDecodeTime(TimeDelta::Millis(5));
      vtiming.UpdateCurrentDelay(now);
      scheduler.Insert({id, vtiming.RenderTime(ts->second, now)});
    }
    for (const auto& a : scheduler.Process(now)) {
      if (!a.render) continue;
      freezes.OnFrameRendered(now);
      ++frames_rendered;
      ++w_frames;
      if (!video_first.IsFinite()) video_first = now;
      last_render = now;
      auto cap = frame_capture.find(a.id);
      if (cap != frame_capture.end()) {
        last_video_delay = static_cast<double>((now - cap->second).us()) / 1000.0;
        video_e2e.push_back(last_video_delay);
        frame_capture.erase(cap);
        frame_rtp_ts.erase(a.id);
      }
    }

    // Audio playout: the device pulls 10 ms.
    if (s.audio && t % 10 == 0 && audio_started) {
      auto op = jitter_buffer.GetAudio(audio_out, now);
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

    // Receiver feedback.
    if (s.nack && s.video) {
      nack.UpdateRtt(rtt);
      auto batch = nack.GetNackBatch(now);
      if (!batch.empty()) {
        int64_t size = 12 + 4 * static_cast<int64_t>(batch.size()) + kUdpIpOverhead;
        reverse.Send(now, Rev{RKind::kNack, {}, std::move(batch)}, DataSize::Bytes(size));
      }
    }
    if (t % 50 == 0) {
      for (const auto& fb : feedback.Build()) {
        auto bytes = cc::SerializeTransportFeedback(fb);
        int64_t size = static_cast<int64_t>(bytes.size()) + kUdpIpOverhead;
        reverse.Send(now, Rev{RKind::kFeedback, std::move(bytes), {}}, DataSize::Bytes(size));
      }
    }
    if (s.video && t % 1000 == 0 && t > 0) {
      timing::ReceiverReport rr;
      rr.ssrc = 100;
      rr.blocks.push_back(video_stats.MakeReportBlock(now));
      auto bytes = timing::SerializeReceiverReport(rr);
      int64_t size = static_cast<int64_t>(bytes.size()) + kUdpIpOverhead;
      reverse.Send(now, Rev{RKind::kRr, std::move(bytes), {}}, DataSize::Bytes(size));
    }

    // --- Sender: feedback -----------------------------------------------------
    reverse.ReceiveEach(now, [&](Rev r, Timestamp) {
      switch (r.kind) {
        case RKind::kFeedback:
          if (auto fb = cc::ParseTransportFeedback(r.bytes)) {
            apply(gcc.OnTransportFeedback(*fb, now));
            outage.OnFeedback(now);
          }
          break;
        case RKind::kNack:
          for (RtpPacket& p : rtx.OnNack(r.nacks, now, rtt)) {
            repair_bytes += static_cast<int64_t>(p.payload.size());
            enqueue({Kind::kRtx, -1, std::move(p), {}}, cc::PacketPriority::kRetransmission, now);
          }
          break;
        case RKind::kPli:
          ++keyframe_requests;
          keyframe_needed = true;
          break;
        case RKind::kLntf:
          break;
        case RKind::kRr:
          if (auto rr = timing::ParseReceiverReport(r.bytes)) {
            for (const auto& rb : rr->blocks) {
              if (auto m = rtt_est.OnReportBlock(rb, now)) {
                rtt = *m;
                rtt_samples.push_back(static_cast<double>(rtt.us()) / 1000.0);
                gcc.OnRttUpdate(rtt);
              }
              if (s.fec) {
                uint64_t g = gaps - last_gaps, m = missing - last_missing;
                last_gaps = gaps;
                last_missing = missing;
                loss::ProtectionInput in;
                in.rtt = rtt;
                in.loss_rate = rb.fraction_lost / 256.0;
                in.mean_burst_length = g ? static_cast<double>(m) / static_cast<double>(g) : 1.0;
                double pkts = std::max(1.0, static_cast<double>(video_rate().bps()) / 8 / s.video_fps / kMaxVideoPayload);
                in.packets_per_frame = pkts;
                fec_enc.SetParams(protection.Update(in, now).fec);
              }
            }
          }
          break;
      }
    });
    if (t % 25 == 0) apply(gcc.OnProcessInterval(now, send_rate.Rate(now)));
    if (t % 10 == 0) {
      cc::OutageActions act = outage.Process(now);
      if (act.pause_encoder) video_paused = true;
      if (act.resume_encoder) video_paused = false;
      if (act.request_keyframe) keyframe_needed = true;
      if (act.reset_estimate) apply(gcc.ResetEstimate(act.reset_rate, now));
    }

    // --- Measurement ----------------------------------------------------------
    double queue_ms = QueueDelayMs(forward, fwd_state, now);
    if (t % 10 == 0) queue_samples.push_back(queue_ms);
    if (t % 100 == 99) {
      Sample smp;
      smp.t_s = static_cast<double>(t + 1) / 1000.0;
      smp.capacity_kbps = fwd_state.cfg.down ? 0 : fwd_state.cfg.capacity.IsFinite() ? fwd_state.cfg.capacity.kbps() : -1;
      smp.target_kbps = target.kbps();
      smp.send_kbps = static_cast<double>(w_send) * 8 / 100;
      smp.goodput_kbps = static_cast<double>(w_good) * 8 / 100;
      smp.queue_ms = queue_ms;
      smp.rtt_ms = static_cast<double>(rtt.us()) / 1000.0;
      smp.audio_buffer_ms = jitter_buffer.CurrentLevelMs();
      smp.audio_concealed_pct = w_played ? 100.0 * static_cast<double>(w_conceal) / static_cast<double>(w_played) : 0;
      smp.video_fps = static_cast<double>(w_frames) * 10;
      smp.video_delay_ms = last_video_delay;
      smp.fec_pct = fec_enc.params().protection * 100;
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
  const auto& jbs = jitter_buffer.stats();
  if (s.audio) {
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
    const auto& fz = freezes.stats();
    double freeze_count = static_cast<double>(fz.freeze_count);
    double freeze_total = fz.total_freezes_ms;
    double longest = fz.freeze_ms.count() ? fz.freeze_ms.max() : 0;
    double frame_ms = 1000.0 / s.video_fps;
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
    }
    // Longest render gap also counts gaps below the freeze threshold.
    m["video.frames_rendered"] = static_cast<double>(frames_rendered);
    m["video.render_fps"] = static_cast<double>(frames_rendered) / s.duration.seconds();
    m["video.render_ratio_pct"] = frames_encoded ? 100.0 * static_cast<double>(frames_rendered) / static_cast<double>(frames_encoded) : 0;
    m["video.freeze_count"] = freeze_count;
    m["video.freeze_total_ms"] = freeze_total;
    m["video.longest_freeze_ms"] = longest;
    m["video.e2e_delay_p50_ms"] = Percentile(video_e2e, 50);
    m["video.e2e_delay_p95_ms"] = Percentile(video_e2e, 95);
    m["video.time_to_first_frame_ms"] = video_first.IsFinite() ? static_cast<double>(video_first.ms()) : static_cast<double>(end_ms);
    m["video.avg_kbps"] = static_cast<double>(video_bytes_encoded) * 8 / s.duration.seconds() / 1000;
    m["video.keyframes"] = static_cast<double>(keyframes_sent);
    m["video.keyframe_requests"] = static_cast<double>(keyframe_requests);
    m["video.encoder_dropped"] = static_cast<double>(frames_skipped);
    m["video.undecodable_frames"] = static_cast<double>(keyframes.stats().frames_dropped);
    {
      // Packets sent in the last second may still be in flight or under repair.
      int64_t counted = 0, lost = 0;
      for (size_t i = 0; i < video_send_ms.size(); ++i) {
        if (video_send_ms[i] > end_ms - 1000) break;
        ++counted;
        lost += !video_seqs_received.count(static_cast<int64_t>(i));
      }
      m["video.residual_loss_pct"] = counted ? 100.0 * static_cast<double>(lost) / static_cast<double>(counted) : 0;
    }
    m["repair.nack_recovered"] = static_cast<double>(nack.stats().recovered);
    m["repair.fec_recovered"] = static_cast<double>(fec_dec.stats().recovered);
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
