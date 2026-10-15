// RtpSender and SendSideController end to end over sim::NetworkLink (plan PR 3
// of docs/design/rtp-session.md). Every packet crosses the link as bytes. The
// receiver here is minimal and lives in the test: it parses the bytes, finds
// gaps, and NACKs through rtp::RtcpEngine bytes on a reverse link, which the
// sender side parses with its own RtcpEngine and hands to RtpSender::OnNack.
#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include "sm/cc/transport_feedback.h"
#include "sm/core/seq_num.h"
#include "sm/loss/rtx.h"
#include "sm/rtp/rtcp_engine.h"
#include "sm/rtp/rtp_sender.h"
#include "sm/rtp/send_side_controller.h"
#include "sm/sim/network_link.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::rtp;

namespace {
constexpr uint32_t kMediaSsrc = 0x1000, kRtxSsrc = 0x2000;

struct Result {
  size_t frames_sent = 0, media_packets = 0, missing_at_end = 0, rtx_received = 0, rtx_sent = 0;
  uint64_t link_lost = 0;
  bool seqs_contiguous = true;
  bool all_parse = true;
  uint64_t padding_received = 0;
};

Result Run(double loss, bool nack, uint64_t seed) {
  sync::ExtensionMap map;
  map.Register(1, sync::uri::kTransportCc);
  map.Register(2, sync::uri::kMid);
  RtpSenderConfig sc;
  sc.ssrc = kMediaSsrc;
  sc.payload_type = 96;
  sc.rtx_ssrc = kRtxSsrc;
  sc.mid = "v";
  sc.mid_packets = 5;
  sc.extensions = &map;
  RtpSender sender(sc);
  SendSideConfig cfg;
  cfg.gcc.start_rate = DataRate::KilobitsPerSec(800);
  SendSideController ctl(cfg, &map);
  ctl.SetPaddingSender(&sender);
  ctl.Start(Timestamp::Millis(0));

  sim::LinkConfig lc;
  lc.base_delay = TimeDelta::Millis(25);
  lc.capacity = DataRate::KilobitsPerSec(3000);
  lc.queue_limit = DataSize::Bytes(60000);
  lc.loss_rate = loss;
  sim::NetworkLink<std::vector<uint8_t>> fwd(lc, seed);
  lc.loss_rate = 0;
  sim::NetworkLink<std::vector<uint8_t>> rev(lc, seed + 1);

  RtcpConfig rc_send;
  rc_send.local_ssrc = 1;
  RtcpEngine send_rtcp(rc_send);
  RtcpConfig rc_recv;
  rc_recv.local_ssrc = 2;
  RtcpEngine recv_rtcp(rc_recv);
  cc::TransportFeedbackBuilder tcc(2, 1);
  sync::ExtensionAccessor access(map);

  Result res;
  SeqNumUnwrapper<uint16_t> unwrap;
  std::set<int64_t> have;
  std::map<int64_t, Timestamp> first_gap_seen;
  std::set<int64_t> last_tseq;
  int64_t highest = -1, first_media = -1;
  std::vector<int> tseqs;
  const int64_t end_ms = 8000;
  for (int64_t t = 0; t < end_ms; ++t) {
    Timestamp now = Timestamp::Millis(t);
    if (t % 33 == 0 && t < end_ms - 600) {  // ~30 fps, 4 to 5 packets of about 1 kB.
      std::vector<std::vector<uint8_t>> chunks;
      for (size_t n : SplitEvenly(5000, sender.config().max_payload_size)) chunks.emplace_back(n, static_cast<uint8_t>(t));
      sender.SendVideoFrame(chunks, static_cast<uint32_t>(t * 90), now);
      ++res.frames_sent;
    }
    for (auto& p : sender.TakePackets()) ctl.Enqueue(std::move(p), now);
    for (auto& r : ctl.Process(now)) {
      tseqs.push_back(r.transport_seq);
      fwd.Send(now, std::move(r.bytes), r.wire_size);
    }
    // Receiver.
    fwd.ReceiveEach(now, [&](std::vector<uint8_t> bytes, Timestamp at) {
      auto p = RtpPacket::Parse(bytes);
      if (!p) {
        res.all_parse = false;
        return;
      }
      if (auto ts = access.GetTransportSeq(*p)) tcc.OnPacket(*ts, at);
      if (p->ssrc == kRtxSsrc) {
        if (p->padding_size > 0 && p->payload.empty()) {
          ++res.padding_received;
          return;
        }
        ++res.rtx_received;
        auto m = loss::RestoreFromRtx(*p, kMediaSsrc, 96);
        if (!m) return;
        p = m;
      }
      int64_t u = unwrap.Unwrap(p->seq);
      if (first_media < 0) first_media = u;
      have.insert(u);
      highest = std::max(highest, u);
    });
    if (nack && t % 20 == 0 && highest >= 0) {
      std::vector<uint16_t> want;
      for (int64_t s = first_media; s < highest; ++s) {
        if (have.count(s)) continue;
        auto it = first_gap_seen.find(s);
        if (it == first_gap_seen.end()) {
          first_gap_seen[s] = now;  // Wait one tick for reordering before asking.
        } else if (now - it->second >= TimeDelta::Millis(40) &&
                   (now - it->second).ms() % 100 < 20) {  // Ask again every 100 ms.
          want.push_back(static_cast<uint16_t>(s));
        }
      }
      if (!want.empty()) recv_rtcp.QueueNack(kMediaSsrc, want);
    }
    for (auto& o : recv_rtcp.Process(now)) rev.Send(now, std::move(o.bytes), DataSize::Bytes(o.bytes.size() + 28));
    if (t % 50 == 0) {
      for (auto& fb : tcc.Build()) ctl.OnTransportFeedback(fb, now);
    }
    // Sender side of the feedback path.
    rev.ReceiveEach(now, [&](std::vector<uint8_t> bytes, Timestamp) {
      RtcpIncoming in = send_rtcp.OnRtcp(bytes, now);
      for (const auto& n : in.nacks) sender.OnNack(n.seqs, now, TimeDelta::Millis(50));
    });
    send_rtcp.Process(now);
  }
  res.media_packets = sender.stats().media_packets;
  res.rtx_sent = sender.stats().rtx_packets;
  res.link_lost = fwd.stats().lost + fwd.stats().queue_dropped;
  for (size_t i = 1; i < tseqs.size(); ++i)
    if (static_cast<uint16_t>(tseqs[i]) != static_cast<uint16_t>(tseqs[i - 1] + 1)) res.seqs_contiguous = false;
  // Count what never arrived, leaving the last 600 ms for in-flight repairs.
  for (int64_t s = first_media; s <= highest - 30; ++s)
    if (!have.count(s)) ++res.missing_at_end;
  return res;
}
}  // namespace

SM_TEST(E2E_RtpSender, CleanLinkDeliversEveryPacketAsParsableBytes) {
  Result r = Run(0.0, true, 1);
  SM_EXPECT_TRUE(r.all_parse);
  SM_EXPECT_TRUE(r.seqs_contiguous);
  SM_EXPECT_GT(r.frames_sent, 200u);
  SM_EXPECT_EQ(r.missing_at_end, 0u);
  SM_EXPECT_EQ(r.rtx_sent, 0u);
}

SM_TEST(E2E_RtpSender, NackAndRtxRepairRandomLoss) {
  for (uint64_t seed : {1, 2, 3}) {
    Result lossy = Run(0.05, false, seed);
    Result fixed = Run(0.05, true, seed);
    SM_EXPECT_TRUE(fixed.all_parse);
    SM_EXPECT_TRUE(fixed.seqs_contiguous);
    SM_EXPECT_GT(lossy.link_lost, 0u);
    SM_EXPECT_GT(lossy.missing_at_end, 10u);  // Without repair the loss shows.
    SM_EXPECT_GT(fixed.rtx_sent, 0u);
    SM_EXPECT_GT(fixed.rtx_received, 0u);
    SM_EXPECT_LE(fixed.missing_at_end, lossy.missing_at_end / 10);
  }
}
