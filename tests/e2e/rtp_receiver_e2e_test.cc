// RtpSender, SendSideController and RtpReceiver end to end over
// sim::NetworkLink (plan PR 4 of docs/design/rtp-session.md). Every packet
// crosses the link as bytes, in both directions. The receiver's feedback
// (NACK, PLI/FIR, transport-cc, receiver reports) is queued on rtp::RtcpEngine
// and parsed by a second engine on the sender side, which dispatches it.
#include <algorithm>
#include <vector>

#include "rtp_test_payload.h"
#include "sm/cc/transport_feedback.h"
#include "sm/rtp/rtcp_engine.h"
#include "sm/rtp/rtp_receiver.h"
#include "sm/rtp/rtp_sender.h"
#include "sm/rtp/send_side_controller.h"
#include "sm/sim/network_link.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::rtp;

namespace {
constexpr uint32_t kMediaSsrc = 0x1000, kRtxSsrc = 0x2000;

struct Options {
  double loss = 0;
  bool nack = true;
  bool outage = false;  // 1.5 s without any packet delivered.
  uint64_t seed = 1;
};

struct Result {
  int frames_sent = 0, keyframes_sent = 0, frames_rendered = 0, frames_late = 0;
  uint64_t nack_seqs = 0, keyframe_requests = 0, rtx_received = 0, malformed = 0;
  int64_t longest_gap_ms = 0;  // Longest time between rendered frames.
  size_t pending_nacks = 0;
  uint64_t rr_received = 0;
};

Result Run(const Options& o) {
  sync::ExtensionMap map;
  map.Register(1, sync::uri::kTransportCc);
  RtpSenderConfig sc;
  sc.ssrc = kMediaSsrc;
  sc.payload_type = 96;
  sc.rtx_ssrc = kRtxSsrc;
  sc.extensions = &map;
  RtpSender sender(sc);
  SendSideConfig cfg;
  cfg.gcc.start_rate = DataRate::KilobitsPerSec(1000);
  SendSideController ctl(cfg, &map);
  ctl.SetPaddingSender(&sender);
  ctl.Start(Timestamp::Millis(0));

  const test::TestVideoParser parser;
  cc::TransportFeedbackBuilder tcc(2, 1);
  RtpReceiverConfig rc;
  rc.ssrc = kMediaSsrc;
  rc.payload_type = 96;
  rc.rtx_ssrc = kRtxSsrc;
  rc.parser = &parser;
  rc.nack = o.nack;
  rc.extensions = &map;
  rc.feedback = &tcc;
  RtpReceiver receiver(rc);
  receiver.Start(Timestamp::Millis(0));

  sim::LinkConfig lc;
  lc.base_delay = TimeDelta::Millis(25);
  lc.capacity = DataRate::KilobitsPerSec(3000);
  lc.queue_limit = DataSize::Bytes(60000);
  lc.loss_rate = o.loss;
  sim::NetworkLink<std::vector<uint8_t>> fwd(lc, o.seed);
  lc.loss_rate = 0;
  sim::NetworkLink<std::vector<uint8_t>> rev(lc, o.seed + 1);

  RtcpConfig rc_send;
  rc_send.local_ssrc = 1;
  RtcpEngine send_rtcp(rc_send);
  send_rtcp.AddLocalMediaSsrc(kMediaSsrc);
  RtcpConfig rc_recv;
  rc_recv.local_ssrc = 2;
  RtcpEngine recv_rtcp(rc_recv);
  recv_rtcp.AddReceiveStatistics(receiver.receive_statistics());

  Result res;
  bool key_needed = true;
  int64_t frame_id = 0, last_render_ms = -1;
  const int64_t end_ms = 10000;
  for (int64_t t = 0; t < end_ms; ++t) {
    Timestamp now = Timestamp::Millis(t);
    const bool down = o.outage && t >= 4000 && t < 5500;
    if (t % 33 == 0 && t < end_ms - 1000) {
      const bool key = key_needed;
      key_needed = false;
      auto chunks = test::MakeTestFrameChunks(key, key ? 6000 : 3000, sender.config().max_payload_size);
      sender.SendVideoFrame(chunks, static_cast<uint32_t>(t * 90), now);
      ++frame_id;
      ++res.frames_sent;
      res.keyframes_sent += key;
    }
    for (auto& p : sender.TakePackets()) ctl.Enqueue(std::move(p), now);
    for (auto& r : ctl.Process(now)) fwd.Send(now, std::move(r.bytes), r.wire_size);

    fwd.ReceiveEach(now, [&](std::vector<uint8_t> bytes, Timestamp at) {
      if (down) return;
      if (receiver.OnPacket(bytes, at) == ReceivedKind::kMalformed) ++res.malformed;
    });
    receiver.OnRtt(TimeDelta::Millis(50));
    receiver.Process(now);
    for (const auto& f : receiver.TakeFrames()) {
      if (!f.render) {
        ++res.frames_late;
        continue;
      }
      ++res.frames_rendered;
      if (last_render_ms >= 0) res.longest_gap_ms = std::max(res.longest_gap_ms, t - last_render_ms);
      last_render_ms = t;
    }
    ReceiverFeedback fb = receiver.TakeFeedback();
    if (!fb.nacks.empty()) recv_rtcp.QueueNack(kMediaSsrc, fb.nacks);
    for (const auto& m : fb.keyframe) {
      if (m.type == loss::FeedbackType::kPli) recv_rtcp.QueuePli(kMediaSsrc);
      if (m.type == loss::FeedbackType::kFir) recv_rtcp.QueueFir(kMediaSsrc);
    }
    if (t % 50 == 0) {
      for (auto& f : tcc.Build()) recv_rtcp.QueueTransportFeedback(std::move(f));
    }
    for (auto& out : recv_rtcp.Process(now)) rev.Send(now, std::move(out.bytes), DataSize::Bytes(out.bytes.size() + 28));

    rev.ReceiveEach(now, [&](std::vector<uint8_t> bytes, Timestamp) {
      RtcpIncoming in = send_rtcp.OnRtcp(bytes, now);
      for (const auto& n : in.nacks) sender.OnNack(n.seqs, now, TimeDelta::Millis(50));
      for (const auto& fbk : in.transport_feedback) ctl.OnTransportFeedback(fbk, now);
      if (!in.keyframe_requests.empty()) {
        key_needed = true;
        ++res.keyframe_requests;
      }
      res.rr_received += in.report_blocks.size();
    });
    send_rtcp.Process(now);
  }
  res.nack_seqs = receiver.nack().stats().nack_requests;
  res.rtx_received = receiver.stats().rtx;
  res.pending_nacks = receiver.nack().pending();
  return res;
}
}  // namespace

SM_TEST(E2E_RtpReceiver, CleanLinkRendersEveryFrameWithoutRepairRequests) {
  Result r = Run({});
  SM_EXPECT_EQ(r.malformed, 0u);
  SM_EXPECT_EQ(r.nack_seqs, 0u);
  // The stream-start request is the only one (the sender had already sent a
  // keyframe, as the test bench does).
  SM_EXPECT_LE(r.keyframe_requests, 1u);
  SM_EXPECT_LE(r.keyframes_sent, 2);
  SM_EXPECT_GE(r.frames_rendered, r.frames_sent - 10);
  SM_EXPECT_LE(r.longest_gap_ms, 100);
  SM_EXPECT_GT(r.rr_received, 0u);
}

SM_TEST(E2E_RtpReceiver, NackAndRtxKeepFramesFlowingUnderRandomLoss) {
  for (uint64_t seed : {1, 2, 3}) {
    Options o;
    o.loss = 0.05;
    o.seed = seed;
    o.nack = false;
    Result without = Run(o);
    o.nack = true;
    Result with = Run(o);
    SM_EXPECT_EQ(with.malformed, 0u);
    SM_EXPECT_GT(with.nack_seqs, 0u);
    SM_EXPECT_GT(with.rtx_received, 0u);
    SM_EXPECT_GT(with.frames_rendered, without.frames_rendered);
    SM_EXPECT_GE(with.frames_rendered, with.frames_sent * 9 / 10);
    SM_EXPECT_LE(with.keyframes_sent, 1 + with.frames_sent / 50);
  }
}

SM_TEST(E2E_RtpReceiver, OutageEndsWithAKeyframeRequestAndVideoResumes) {
  Options o;
  o.outage = true;
  Result r = Run(o);
  SM_EXPECT_EQ(r.malformed, 0u);
  SM_EXPECT_GT(r.keyframe_requests, 0u);
  SM_EXPECT_GE(r.keyframes_sent, 2);
  SM_EXPECT_GT(r.frames_rendered, r.frames_sent / 2);
  SM_EXPECT_LE(r.longest_gap_ms, 1500 + 1500);  // The outage plus recovery.
}
