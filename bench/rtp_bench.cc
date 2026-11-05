// RTP session layer benchmarks (section 9: 9.1 RTCP engine, 9.2 RTP sender, 9.3 RTP receiver,
// 9.4 session facade, 9.5 H.264 packetization). Simulated time only: `now` is passed in.
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include "sm/cc/transport_feedback.h"
#include "sm/core/rtp_packet.h"
#include "sm/rtp/fake_h264.h"
#include "sm/rtp/h264.h"
#include "sm/rtp/rtcp_engine.h"
#include "sm/rtp/rtcp_sdes_bye.h"
#include "sm/rtp/rtp_receiver.h"
#include "sm/rtp/rtp_sender.h"
#include "sm/rtp/send_side_controller.h"
#include "sm/rtp/session.h"
#include "sm/sim/network_link.h"
#include "sm/sync/header_extensions.h"
#include "sm/timing/rtcp_stats.h"
#include "sm_bench.h"

using namespace sm;
using namespace sm::rtp;

// ---------------------------------------------------------------- 9.1 RTCP engine, SDES and BYE

SM_BENCH(rtp, SdesCname_SerializeParse) {
  std::vector<SdesChunk> chunks = {{0x1234, {{kSdesCname, "streaming-media-peer"}}}};
  for (auto _ : st) {
    auto b = SerializeSdes(chunks);
    bench::DoNotOptimize(ParseSdes(b));
  }
  st.BudgetNsPerItem(3000);
}

SM_BENCH(rtp, Bye_SerializeParse) {
  Bye bye{{0x1234, 0x5678}, "hangup"};
  for (auto _ : st) {
    auto b = SerializeBye(bye);
    bench::DoNotOptimize(ParseBye(b));
  }
  st.BudgetNsPerItem(3000);
}

// One compound report: a sender's engine builds SR + 3 report blocks + SDES CNAME, the peer's engine
// parses it (SR to its receive statistics' view, report blocks to RTT, SDES).
SM_BENCH(rtp, RtcpEngine_CompoundReportBuildAndParse) {
  RtcpConfig ca;
  ca.local_ssrc = 10;
  ca.cname = "alice@streaming-media";
  RtcpEngine a(ca);
  RtcpConfig cb;
  cb.local_ssrc = 20;
  cb.cname = "bob@streaming-media";
  RtcpEngine b(cb);
  b.AddLocalMediaSsrc(30);  // The report blocks describe Bob's stream 30 (RTT) and two more.
  timing::ReceiveStatistics s1(30, 90000, true), s2(31, 48000, true), s3(32, 90000, true);
  a.AddReceiveStatistics(&s1);
  a.AddReceiveStatistics(&s2);
  a.AddReceiveStatistics(&s3);
  RtcpSenderState ss;
  ss.ssrc = 10;
  ss.send_bitrate = DataRate::KilobitsPerSec(1500);
  a.Process(Timestamp::Millis(0));
  b.Process(Timestamp::Millis(0));
  uint16_t seq = 0;
  int64_t t_ms = 1;
  size_t blocks = 0, sdes = 0;
  for (auto _ : st) {
    const Timestamp now = Timestamp::Millis(t_ms);
    // One packet per stream keeps every block due (a block stops after 8 s without packets).
    s1.OnPacket(seq, static_cast<uint32_t>(t_ms * 90), now);
    s2.OnPacket(seq, static_cast<uint32_t>(t_ms * 48), now);
    s3.OnPacket(seq, static_cast<uint32_t>(t_ms * 90), now);
    ++seq;
    ss.packet_count = seq;
    ss.octet_count = seq * 1000u;
    ss.rtp_timestamp = static_cast<uint32_t>(t_ms * 90);
    ss.reference_time = now;
    a.SetSenderState(ss);
    for (const auto& out : a.SendReport(now)) {
      RtcpIncoming in = b.OnRtcp(out.bytes, now + TimeDelta::Millis(20));
      blocks = in.report_blocks.size();
      sdes = in.sdes.size();
      bench::DoNotOptimize(in);
    }
    ++t_ms;
  }
  st.Counter("report_blocks", static_cast<double>(blocks));
  st.Require("report_blocks", static_cast<double>(blocks), ">=", 3);
  st.Require("sdes_chunks", static_cast<double>(sdes), ">=", 1);
  st.BudgetNsPerItem(8000);  // One compound report (SR + 3 blocks + SDES) built by one engine and parsed by another.
}

// Early feedback: a NACK of 10 sequence numbers and a PLI queued, sent in one compound by Process and
// parsed by the media sender's engine.
SM_BENCH(rtp, RtcpEngine_NackPliFeedbackBuildAndParse) {
  RtcpConfig cr;
  cr.local_ssrc = 20;
  RtcpEngine recv(cr);
  RtcpConfig cs;
  cs.local_ssrc = 10;
  RtcpEngine send(cs);
  timing::ReceiveStatistics stats(30, 90000, true);
  recv.AddReceiveStatistics(&stats);
  recv.Process(Timestamp::Millis(0));
  uint16_t base = 0;
  int64_t t_ms = 1;
  size_t nacks = 0;
  std::vector<uint16_t> seqs(10);
  for (auto _ : st) {
    const Timestamp now = Timestamp::Millis(t_ms);
    stats.OnPacket(base, static_cast<uint32_t>(t_ms * 90), now);
    for (size_t i = 0; i < seqs.size(); ++i) seqs[i] = static_cast<uint16_t>(base + 2 * i);
    recv.QueueNack(30, seqs);
    recv.QueuePli(30);
    for (const auto& out : recv.Process(now)) {
      RtcpIncoming in = send.OnRtcp(out.bytes, now + TimeDelta::Millis(20));
      nacks += in.nacks.size();
      bench::DoNotOptimize(in);
    }
    base = static_cast<uint16_t>(base + 32);
    ++t_ms;
  }
  st.Require("nack_messages", static_cast<double>(nacks), ">=", 1);
  st.BudgetNsPerItem(10000);  // One feedback compound (RR + NACK + PLI) built and parsed.
}

// ---------------------------------------------------------------- 9.2 RTP sender and send-side controller

namespace {
sync::ExtensionMap MediaExtensions() {
  sync::ExtensionMap map;
  map.Register(1, sync::uri::kTransportCc);
  map.Register(2, sync::uri::kAbsSendTime);
  map.Register(3, sync::uri::kMid);
  return map;
}
}  // namespace

// One 30 fps video frame of 6000 bytes (5 packets): H.264 packetization (FU-A), RtpSender (header
// extensions, sequence numbers, RTX history) and SendSideController (transport-wide sequence number,
// pacing, serialization), with the pacer run at its own process times until the next frame. Transport
// feedback for the packets (20 ms each way, no queueing) comes back once per frame, so GCC holds its
// target and the pacer keeps up.
SM_BENCH(rtp, RtpSender_SendVideoFramePaced) {
  const sync::ExtensionMap map = MediaExtensions();
  RtpSenderConfig sc;
  sc.ssrc = 0x1000;
  sc.rtx_ssrc = 0x2000;
  sc.mid = "v";
  sc.mid_packets = 5;
  sc.extensions = &map;
  RtpSender sender(sc);
  SendSideConfig cfg;
  cfg.gcc.start_rate = DataRate::KilobitsPerSec(2500);
  SendSideController ctl(cfg, &map);
  ctl.SetPaddingSender(&sender);
  ctl.Start(Timestamp::Millis(0));
  cc::TransportFeedbackBuilder tcc;
  std::vector<std::pair<uint16_t, Timestamp>> in_flight;  // Transport seq, arrival at the receiver.
  const std::vector<uint8_t> key = MakeFakeH264Frame(true, 6000), delta = MakeFakeH264Frame(false, 6000);
  int64_t frame = 0;
  size_t video_released = 0, queued = 0;
  auto send_one = [&] {
    const Timestamp now = Timestamp::Micros(frame * 33333);
    const Timestamp next = Timestamp::Micros((frame + 1) * 33333);
    // Feedback for what arrived up to 20 ms ago.
    size_t k = 0;
    for (; k < in_flight.size() && in_flight[k].second + TimeDelta::Millis(20) <= now; ++k) {
      tcc.OnPacket(in_flight[k].first, in_flight[k].second);
    }
    in_flight.erase(in_flight.begin(), in_flight.begin() + static_cast<std::ptrdiff_t>(k));
    for (const auto& fb : tcc.Build()) ctl.OnTransportFeedback(fb, now);
    const bool is_key = frame == 0;
    PayloadSizeLimits limits;  // The packet's room less the header the next packet carries (Session::SendVideo).
    limits.max_payload_len =
        static_cast<int>(sender.config().max_payload_size + RtpPacket::kFixedHeaderSize - sender.MediaHeaderSize());
    auto chunks = H264Packetizer::Packetize(is_key ? key : delta, limits);
    sender.SendVideoFrame(chunks, static_cast<uint32_t>(frame * 3000), now, is_key);
    for (auto& p : sender.TakePackets()) {
      ++queued;
      ctl.Enqueue(std::move(p), now);
    }
    for (Timestamp t = now; t < next; t = std::max(ctl.NextProcessTime(t), t + TimeDelta::Micros(1))) {
      for (auto& r : ctl.Process(t)) {
        video_released += r.kind == PacketKind::kVideo;
        if (r.transport_seq != 0) in_flight.emplace_back(r.transport_seq, r.send_time + TimeDelta::Millis(20));
        bench::DoNotOptimize(r.bytes.data());
      }
    }
    ++frame;
  };
  for (int i = 0; i < 60; ++i) send_one();  // Warm-up: 2 s, keyframe and probes.
  const size_t queued0 = queued, released0 = video_released;
  for (auto _ : st) send_one();
  // The pacer keeps up: every video packet queued in the loop left it, give or take one frame.
  st.Counter("packets_per_frame", static_cast<double>(queued - queued0) / static_cast<double>(st.iterations()));
  st.Counter("target_kbps", ctl.target().kbps());
  st.Require("unsent_video_packets",
             static_cast<double>(queued - queued0) - static_cast<double>(video_released - released0), "<=", 6);
  st.BudgetNsPerItem(120000);  // One video frame: packetize, RTP headers, RTX store, pace and serialize.
}

// ---------------------------------------------------------------- 9.3 RTP receiver

// One RTP packet of a 30 fps H.264 stream through RtpReceiver: parse, receive statistics, NACK
// tracking, packet buffer insert, frame assembly and release (Process per frame). The stream is 3 s
// (one 30000-byte keyframe, 89 delta frames of 4000 bytes) replayed with sequence numbers and
// timestamps continued, so the receiver sees one endless stream.
SM_BENCH(rtp, RtpReceiver_ReceivePacket) {
  const H264PayloadParser parser;
  RtpReceiverConfig rc;
  rc.ssrc = 0x1000;
  rc.rtx_ssrc = 0x2000;
  rc.parser = &parser;
  rc.timing.version = 2;
  RtpReceiver receiver(rc);
  receiver.Start(Timestamp::Millis(0));

  struct Pkt {
    std::vector<uint8_t> bytes;
    int frame;
    bool marker;
  };
  std::vector<Pkt> cycle;
  constexpr int kFrames = 90;
  for (int f = 0; f < kFrames; ++f) {
    const auto au = MakeFakeH264Frame(f == 0, f == 0 ? 30000 : 4000);
    auto payloads = H264Packetizer::Packetize(au, 1200 - 12);
    for (size_t i = 0; i < payloads.size(); ++i) {
      RtpPacket p;
      p.payload_type = 96;
      p.ssrc = 0x1000;
      p.marker = i + 1 == payloads.size();
      p.payload = std::move(payloads[i]);
      cycle.push_back({p.Serialize(), f, p.marker});
    }
  }
  const auto n = static_cast<uint32_t>(cycle.size());
  uint64_t i = 0;
  size_t frames_out = 0;
  auto receive_one = [&] {
    const uint64_t pass = i / n;
    Pkt& p = cycle[i % n];
    const uint16_t seq = static_cast<uint16_t>(i);
    const int64_t frame = static_cast<int64_t>(pass) * kFrames + p.frame;
    const uint32_t ts = static_cast<uint32_t>(frame * 3000);
    p.bytes[2] = static_cast<uint8_t>(seq >> 8);
    p.bytes[3] = static_cast<uint8_t>(seq);
    p.bytes[4] = static_cast<uint8_t>(ts >> 24);
    p.bytes[5] = static_cast<uint8_t>(ts >> 16);
    p.bytes[6] = static_cast<uint8_t>(ts >> 8);
    p.bytes[7] = static_cast<uint8_t>(ts);
    const Timestamp now = Timestamp::Micros(frame * 33333 + 100);
    bench::DoNotOptimize(receiver.OnPacket(p.bytes, now));
    if (p.marker) {
      receiver.Process(now);
      frames_out += receiver.TakeFrames().size();
      bench::DoNotOptimize(receiver.TakeFeedback());
    }
    ++i;
  };
  for (uint32_t k = 0; k < n; ++k) receive_one();  // Warm-up: one cycle.
  const size_t frames0 = frames_out;
  const uint64_t i0 = i;
  for (auto _ : st) receive_one();
  const double frames_in = static_cast<double>((i - i0) / n * kFrames);
  if (frames_in >= kFrames) {
    st.Counter("frames_out_per_frame_in", static_cast<double>(frames_out - frames0) / frames_in);
    st.Require("frames_out_per_frame_in", static_cast<double>(frames_out - frames0) / frames_in, ">=", 0.95);
  }
  st.BudgetNsPerItem(8000);  // One RTP packet received (Process and frame release once per frame).
}

// ---------------------------------------------------------------- 9.4 RTP session facade

namespace {
constexpr uint32_t kVideoSsrc = 2, kRtxSsrc = 3;

SessionConfig MakeSessionConfig(uint32_t ssrc, const char* cname) {
  SessionConfig c;
  c.local_ssrc = ssrc;
  c.cname = cname;
  c.extensions.Register(1, sync::uri::kTransportCc);
  c.extensions.Register(2, sync::uri::kAbsSendTime);
  c.extensions.Register(3, sync::uri::kMid);
  c.mid_video = "v";
  c.send_side.gcc.start_rate = DataRate::KilobitsPerSec(800);
  return c;
}

// Alice sends H.264 video to Bob over two sim::NetworkLink (25 ms, 3 Mbit/s); Bob sends RTCP back.
// The encoder model sizes each frame from the session's video target and answers keyframe requests.
struct VideoCall {
  explicit VideoCall(double loss, uint64_t seed)
      : a(MakeSessionConfig(10, "alice")), b(MakeSessionConfig(20, "bob")), ab(Link(loss), seed), ba(Link(0), seed + 100) {
    VideoStreamConfig v;
    v.ssrc = kVideoSsrc;
    v.rtx_ssrc = kRtxSsrc;
    v.parser = &parser;
    a_video = a.AddVideoSend(v);
    b_video = b.AddVideoReceive(v);
  }
  static sim::LinkConfig Link(double loss) {
    sim::LinkConfig lc;
    lc.base_delay = TimeDelta::Millis(25);
    lc.capacity = DataRate::KilobitsPerSec(3000);
    lc.queue_limit = DataSize::Bytes(60000);
    lc.loss_rate = loss;
    return lc;
  }
  // One 30 fps frame interval: a frame is encoded and sent, then 33 1-ms ticks of both sessions and links.
  void RunFrame() {
    const int64_t start = t_ms;
    const Timestamp now0 = Timestamp::Millis(start);
    const bool key = key_needed;
    key_needed = false;
    const int64_t bytes = std::clamp<int64_t>(a.rates().video_target.bps() / 8 / 30, 300, 60000);
    const auto au = MakeFakeH264Frame(key, static_cast<size_t>(key ? bytes * 4 : bytes));
    a.SendVideo(a_video, {static_cast<uint32_t>(frames_sent * 3000), au, key}, now0);
    ++frames_sent;
    for (; t_ms < start + 33; ++t_ms) {
      const Timestamp now = Timestamp::Millis(t_ms);
      a.Process(now);
      b.Process(now);
      for (auto& p : a.TakeOutgoing()) ab.Send(now, std::move(p.bytes), p.wire_size);
      for (auto& p : b.TakeOutgoing()) ba.Send(now, std::move(p.bytes), p.wire_size);
      ab.ReceiveEach(now, [&](std::vector<uint8_t> bytes, Timestamp at) { b.OnPacket(bytes, at); });
      ba.ReceiveEach(now, [&](std::vector<uint8_t> bytes, Timestamp at) { a.OnPacket(bytes, at); });
      for (const auto& f : b.TakeVideoFrames(b_video)) {
        if (!f.render) continue;
        ++rendered;
        if (last_render_ms >= 0) longest_gap_ms = std::max(longest_gap_ms, t_ms - last_render_ms);
        last_render_ms = t_ms;
      }
      for (const auto& e : a.TakeEvents()) key_needed |= e.type == SessionEventType::kKeyframeRequested;
      b.TakeEvents();
    }
  }
  const H264PayloadParser parser;
  Session a, b;
  VideoSendId a_video;
  VideoRecvId b_video;
  sim::NetworkLink<std::vector<uint8_t>> ab, ba;
  bool key_needed = true;
  int64_t t_ms = 0, frames_sent = 0, rendered = 0, last_render_ms = -1, longest_gap_ms = 0;
};
}  // namespace

// One 33 ms frame interval of a one-way H.264 video call end to end: SendVideo (SPS rewrite,
// packetization), pacing and GCC on Alice, 33 ticks of Process on both sessions, every packet as bytes
// across the simulated links, Bob's receive path to a rendered frame and his RTCP back.
SM_BENCH(rtp, Session_VideoFrameEndToEnd) {
  VideoCall call(0, 1);
  for (int i = 0; i < 60; ++i) call.RunFrame();  // Warm-up: 2 s, past start-up and the first probes.
  const int64_t sent0 = call.frames_sent, rendered0 = call.rendered;
  for (auto _ : st) call.RunFrame();
  const double sent = static_cast<double>(call.frames_sent - sent0);
  if (sent >= 30) {
    st.Counter("rendered_per_sent", static_cast<double>(call.rendered - rendered0) / sent);
    st.Require("rendered_per_sent", static_cast<double>(call.rendered - rendered0) / sent, ">=", 0.95);
  }
  st.BudgetNsPerItem(500000);  // One 33 ms frame interval of a two-session call (33 ticks each).
}

// 20 s of the call over a 5% random-loss forward link: NACK and RTX keep the video going.
SM_BENCH_QUALITY(rtp, Q_Session_Video_5pct_loss) {
  VideoCall call(0.05, 1);
  for (int i = 0; i < 600; ++i) call.RunFrame();
  const double delivered = static_cast<double>(call.rendered) / static_cast<double>(call.frames_sent);
  st.Counter("frames_sent", static_cast<double>(call.frames_sent));
  st.Counter("delivered", delivered);
  st.Counter("longest_freeze_ms", static_cast<double>(call.longest_gap_ms));
  st.Counter("target_kbps", call.a.rates().target.kbps());
  // Measured 0.997 delivered and a 302 ms longest freeze (seed 1); the gates leave a margin.
  st.Require("delivered", delivered, ">=", 0.98);
  st.Require("longest_freeze_ms", static_cast<double>(call.longest_gap_ms), "<=", 500);
}

// ---------------------------------------------------------------- 9.5 H.264 packetization

namespace {
// Packetize an access unit (mode 1, 1188-byte payloads) and run the receive side: Parse each payload,
// then Reassemble the access unit (CopyAndFixBitstream) from the packets.
void PacketizeDepacketize(bench::State& st, const std::vector<uint8_t>& au) {
  PayloadSizeLimits limits;
  limits.max_payload_len = 1200 - 12;
  std::vector<RtpPacket> packets;
  std::vector<uint8_t> out;
  size_t n_packets = 0;
  bool ok = true;
  for (auto _ : st) {
    auto payloads = H264Packetizer::Packetize(au, limits);
    n_packets = payloads.size();
    packets.resize(payloads.size());
    for (size_t i = 0; i < payloads.size(); ++i) {
      ok &= H264Depacketizer::Parse(payloads[i]).valid;
      packets[i].payload = std::move(payloads[i]);
    }
    out.clear();
    ok &= H264Depacketizer::Reassemble(packets, out);
    bench::DoNotOptimize(out.data());
  }
  st.Counter("packets", static_cast<double>(n_packets));
  st.Require("reassembled", ok ? 1 : 0, ">=", 1);
}
}  // namespace

// A 1080p-size keyframe, 150 kB: SPS + PPS in a STAP-A, the IDR slice in 127 FU-A fragments.
SM_BENCH(rtp, H264_Keyframe1080p_PacketizeDepacketize) {
  PacketizeDepacketize(st, MakeFakeH264Frame(true, 150000));
  st.BudgetNsPerItem(800000);  // One 150 kB access unit packetized, parsed and reassembled.
}

// A 1080p-size delta frame, 20 kB: one slice in 17 FU-A fragments.
SM_BENCH(rtp, H264_DeltaFrame1080p_PacketizeDepacketize) {
  PacketizeDepacketize(st, MakeFakeH264Frame(false, 20000));
  st.BudgetNsPerItem(120000);  // One 20 kB access unit packetized, parsed and reassembled.
}
