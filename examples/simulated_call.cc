// Example: a one-way audio + video call over a simulated unstable link,
// wiring together modules from every section, with metrics exported in
// Prometheus format and decisions recorded in the event log.
//
//   sender:   G.711 mu-law audio (8 kHz) -> RED -> pacer (audio priority)
//             video frames -> pacer (GCC target rate) + RTX history
//   network:  2 Mbps bottleneck, 30 ms + jitter, 1% loss, drops to 700 kbps
//   receiver: transport-cc feedback, NACK, audio jitter buffer (NetEq-lite),
//             RTCP receive statistics, freeze detection
//
// Build: cmake -DSM_BUILD_EXAMPLES=ON ..; run: ./examples/simulated_call
#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "sm/cc/pacer.h"
#include "sm/cc/send_side_bwe.h"
#include "sm/cc/transport_feedback.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/loss/nack_tracker.h"
#include "sm/loss/red.h"
#include "sm/loss/rtx.h"
#include "sm/sim/network_link.h"
#include "sm/sim/signals.h"
#include "sm/timing/audio_jitter_buffer.h"
#include "sm/timing/rtcp_stats.h"
#include "sm/video/freeze_detector.h"

using namespace sm;

namespace {
struct WirePacket {
  bool audio;
  uint16_t tseq;   // Transport-wide sequence number.
  RtpPacket rtp;
};

// G.711 mu-law (ITU-T G.711), the simplest real audio codec: 64 kbps.
uint8_t MuLawEncode(float x) {
  int s = static_cast<int>(std::clamp(x, -1.0f, 1.0f) * 32635.0f);
  int sign = s < 0 ? 0x80 : 0;
  if (s < 0) s = -s;
  s += 0x84;
  int exp = 7;
  for (int mask = 0x4000; exp > 0 && !(s & mask); mask >>= 1) --exp;
  int mant = (s >> (exp + 3)) & 0x0f;
  return static_cast<uint8_t>(~(sign | (exp << 4) | mant));
}
float MuLawDecode(uint8_t u) {
  u = static_cast<uint8_t>(~u);
  int t = ((u & 0x0f) << 3) + 0x84;
  t <<= (u & 0x70) >> 4;
  return static_cast<float>((u & 0x80) ? 0x84 - t : t - 0x84) / 32768.0f;
}
}  // namespace

int main() {
  EventLog events(256);
  std::map<std::string, int> event_counts;
  events.SetCallback([&](const Event& e) { ++event_counts[std::string(e.module) + "/" + e.name]; });
  MetricsRegistry registry;

  // --- Sender ---------------------------------------------------------------
  cc::GccConfig gcfg;
  gcfg.events = &events;
  cc::GccController gcc(gcfg);
  cc::Pacer pacer;
  loss::RedEncoder red(1);
  loss::RtxConfig rtx_cfg;
  rtx_cfg.rtx_ssrc = 3;
  loss::RtxSender rtx(rtx_cfg);
  registry.Register("sender.gcc", &gcc);
  registry.Register("sender.pacer", &pacer);
  registry.Register("sender.rtx", &rtx);

  // --- Network --------------------------------------------------------------
  sim::LinkConfig link_cfg;
  link_cfg.base_delay = TimeDelta::Millis(30);
  link_cfg.jitter_stddev = TimeDelta::Millis(8);
  link_cfg.loss_rate = 0.01;
  link_cfg.capacity = DataRate::KilobitsPerSec(2000);
  link_cfg.queue_limit = DataSize::Bytes(150000);
  sim::NetworkLink<WirePacket> forward(link_cfg, 1);
  sim::LinkConfig back_cfg;
  back_cfg.base_delay = TimeDelta::Millis(30);
  sim::NetworkLink<std::vector<uint8_t>> backward(back_cfg, 2);

  // --- Receiver -------------------------------------------------------------
  cc::TransportFeedbackBuilder feedback;
  loss::NackConfig nack_cfg;
  nack_cfg.events = &events;
  loss::NackTracker nack(nack_cfg);
  loss::RedReceiver red_rx;
  timing::AudioJitterBufferConfig jb_cfg;
  jb_cfg.sample_rate = 8000;
  timing::AudioJitterBuffer jitter_buffer(jb_cfg);
  timing::ReceiveStatistics audio_stats(1, 8000);
  video::FreezeDetector freezes(&events);
  registry.Register("receiver.nack", &nack);
  registry.Register("receiver.neteq", &jitter_buffer);
  registry.Register("receiver.audio_rtp", &audio_stats);
  registry.Register("receiver.freezes", &freezes);

  auto speech = sim::SpeechLike(8000, 31.0, 7, true);
  std::map<uint64_t, WirePacket> queued;  // Pacer id -> packet.
  uint64_t next_id = 0;
  uint16_t tseq = 0, video_seq = 0, audio_seq = 0;
  DataRate target = DataRate::KilobitsPerSec(300);
  std::vector<float> out(80);  // 10 ms at 8 kHz.

  auto apply = [&](const cc::NetworkControlUpdate& u) {
    target = u.target_rate;
    pacer.SetPacingRates(u.pacing_rate, DataRate::Zero());
    for (const auto& c : u.probes) pacer.AddProbeCluster(c);
  };
  apply(gcc.Start(Timestamp::Millis(0)));
  gcc.OnRttUpdate(TimeDelta::Millis(60));
  nack.UpdateRtt(TimeDelta::Millis(60));

  for (int64_t t = 0; t < 30000; ++t) {
    Timestamp now = Timestamp::Millis(t);
    if (t == 15000) {  // The link degrades half way through.
      link_cfg.capacity = DataRate::KilobitsPerSec(700);
      forward.SetConfig(link_cfg);
    }
    // Audio: 20 ms G.711 frames (160 bytes) wrapped in RED.
    if (t % 20 == 0) {
      size_t off = static_cast<size_t>(t) * 8;
      std::vector<uint8_t> frame(160);
      for (size_t i = 0; i < 160; ++i) frame[i] = MuLawEncode(speech[off + i]);
      RtpPacket p;
      p.ssrc = 1;
      p.seq = audio_seq++;
      p.timestamp = static_cast<uint32_t>(t * 8);
      p.payload = red.Encode(0, p.timestamp, frame);
      queued[next_id] = {true, 0, p};
      pacer.Enqueue({next_id++, cc::PacketPriority::kAudio, DataSize::Bytes(static_cast<int64_t>(p.size())), now});
    }
    // Video: 30 fps, frame size from the GCC target minus audio (128 kbps
    // G.711 with RED plus headers).
    if (t % 33 == 0) {
      int64_t bytes = std::max<int64_t>(300, (target - DataRate::KilobitsPerSec(150)).bps() / 8 / 30);
      for (int64_t off = 0; off < bytes; off += 1100) {
        RtpPacket p;
        p.ssrc = 2;
        p.seq = video_seq++;
        p.timestamp = static_cast<uint32_t>(t * 90);
        p.marker = off + 1100 >= bytes;
        p.payload.assign(static_cast<size_t>(std::min<int64_t>(1100, bytes - off)), 0);
        rtx.OnPacketSent(p, now);
        queued[next_id] = {false, 0, p};
        pacer.Enqueue({next_id++, cc::PacketPriority::kVideo, DataSize::Bytes(static_cast<int64_t>(p.size())), now});
      }
    }
    for (const auto& sent : pacer.Process(now)) {
      WirePacket w = sent.is_padding ? WirePacket{false, 0, RtpPacket{}} : queued[sent.id];
      queued.erase(sent.id);
      w.tseq = tseq++;
      gcc.OnPacketSent(w.tseq, now, sent.size, sent.probe_cluster);
      forward.Send(now, w, sent.size);
    }
    // Receiver side.
    forward.ReceiveEach(now, [&](WirePacket w, Timestamp at) {
      feedback.OnPacket(w.tseq, at);
      if (w.audio) {
        audio_stats.OnPacket(w.rtp.seq, w.rtp.timestamp, at);
        for (auto& block : red_rx.OnPacket(w.rtp.payload, w.rtp.timestamp)) {
          std::vector<float> pcm(block.payload.size());
          for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = MuLawDecode(block.payload[i]);
          jitter_buffer.InsertPacket(block.timestamp, std::move(pcm), at);
        }
      } else if (w.rtp.ssrc == 2) {
        nack.OnReceivedPacket(w.rtp.seq, false, at);
        if (w.rtp.marker) freezes.OnFrameRendered(at);
      } else if (w.rtp.ssrc == 3) {
        if (auto orig = loss::RestoreFromRtx(w.rtp, 2, 96)) nack.OnReceivedPacket(orig->seq, false, at);
      }
    });
    if (t % 10 == 0) jitter_buffer.GetAudio(out, now);  // The speaker pulls 10 ms.
    if (t % 50 == 0) {
      for (const auto& fb : feedback.Build()) backward.Send(now, cc::SerializeTransportFeedback(fb), DataSize::Bytes(60));
    }
    auto nacks = nack.GetNackBatch(now);
    for (auto& r : rtx.OnNack(nacks, now, TimeDelta::Millis(60))) {
      queued[next_id] = {false, 0, r};
      pacer.Enqueue({next_id++, cc::PacketPriority::kRetransmission, DataSize::Bytes(static_cast<int64_t>(r.size())), now});
    }
    backward.ReceiveEach(now, [&](std::vector<uint8_t> bytes, Timestamp) {
      if (auto fb = cc::ParseTransportFeedback(bytes)) apply(gcc.OnTransportFeedback(*fb, now));
    });
    if (t % 25 == 0) apply(gcc.OnProcessInterval(now, target));
  }

  audio_stats.MakeReportBlock(Timestamp::Millis(30000));  // Closes the RTCP loss interval.
  std::printf("# Metrics after 30 s (Prometheus text format)\n%s\n", registry.ToPrometheus().c_str());
  std::printf("# Event counts (module/name)\n");
  for (const auto& [name, n] : event_counts) std::printf("%-28s %d\n", name.c_str(), n);
  std::printf("\nfinal GCC target: %.0f kbps (link: 700 kbps since 15 s)\n", target.kbps());
  return 0;
}
