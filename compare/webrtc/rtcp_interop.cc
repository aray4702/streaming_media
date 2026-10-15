// RTCP interop check: this library's serialized NACK, PLI, FIR and loss
// notification (and a compound packet of them) are parsed by libwebrtc's
// rtcp_packet parsers, and libwebrtc's serialized packets are parsed by this
// library. Also covers SDES, BYE and padded RTP packets (plan PR 1 of
// docs/design/rtp-session.md) and the datagrams of rtp::RtcpEngine: report
// and feedback compounds with SR/RR, SDES, transport-cc and REMB (plan PR 2), and the RTP packets of rtp::RtpSender and
// rtp::SendSideController with MID, transport-wide sequence number and
// abs-send-time extensions, RTX and padding on the RTX SSRC (plan PR 3). Exits non-zero on any mismatch.
#include <cstdio>
#include <memory>
#include <vector>

#include "modules/rtp_rtcp/source/rtcp_packet/common_header.h"
#include "modules/rtp_rtcp/source/rtcp_packet/bye.h"
#include "modules/rtp_rtcp/source/rtcp_packet/compound_packet.h"
#include "modules/rtp_rtcp/source/rtcp_packet/fir.h"
#include "modules/rtp_rtcp/source/rtcp_packet/loss_notification.h"
#include "modules/rtp_rtcp/source/rtcp_packet/nack.h"
#include "modules/rtp_rtcp/source/rtcp_packet/pli.h"
#include "modules/rtp_rtcp/source/rtcp_packet/receiver_report.h"
#include "modules/rtp_rtcp/source/rtcp_packet/remb.h"
#include "modules/rtp_rtcp/source/rtcp_packet/sender_report.h"
#include "modules/rtp_rtcp/source/rtcp_packet/transport_feedback.h"
#include "modules/rtp_rtcp/source/rtcp_packet/sdes.h"
#include "modules/rtp_rtcp/include/rtp_header_extension_map.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "sm/core/rtp_packet.h"
#include "sm/loss/rtcp_feedback.h"
#include "sm/rtp/rtcp_engine.h"
#include "sm/loss/rtx.h"
#include "sm/rtp/rtcp_sdes_bye.h"
#include "sm/rtp/rtp_sender.h"
#include "sm/rtp/send_side_controller.h"
#include "sm/sync/header_extensions.h"

namespace {
using Bytes = std::vector<uint8_t>;
int g_failures = 0;

void Check(bool ok, const char* what) {
  std::printf("  %-58s %s\n", what, ok ? "ok" : "FAILED");
  if (!ok) ++g_failures;
}

template <typename P>
bool WebrtcParse(const uint8_t* data, size_t size, P& out) {
  webrtc::rtcp::CommonHeader h;
  return h.Parse(data, size) && h.packet_size() == size && out.Parse(h);
}

template <typename P>
Bytes WebrtcBuild(const P& p) {
  auto buf = p.Build();
  return Bytes(buf.data(), buf.data() + buf.size());
}

const std::vector<uint16_t> kNackSeqs = {65530, 65531, 65535, 0, 3, 17, 18, 40000, 40016, 40017};

void SmToWebrtc() {
  std::printf("this library -> libwebrtc\n");
  {
    Bytes b = sm::loss::SerializeNack({0x11223344, 0x55667788, kNackSeqs});
    webrtc::rtcp::Nack n;
    bool ok = WebrtcParse(b.data(), b.size(), n);
    Check(ok && n.sender_ssrc() == 0x11223344 && n.media_ssrc() == 0x55667788 && n.packet_ids() == kNackSeqs,
          "generic NACK (PID+BLP, wrap-around)");
  }
  {
    Bytes b = sm::loss::SerializePli({0xAABBCCDD, 0x01020304});
    webrtc::rtcp::Pli p;
    bool ok = WebrtcParse(b.data(), b.size(), p);
    Check(ok && p.sender_ssrc() == 0xAABBCCDD && p.media_ssrc() == 0x01020304, "PLI");
  }
  {
    Bytes b = sm::loss::SerializeFir({0x0A0B0C0D, {{0x01020304, 7}, {0x05060708, 255}}});
    webrtc::rtcp::Fir f;
    bool ok = WebrtcParse(b.data(), b.size(), f);
    ok = ok && f.sender_ssrc() == 0x0A0B0C0D && f.requests().size() == 2 && f.requests()[0].ssrc == 0x01020304 &&
         f.requests()[0].seq_nr == 7 && f.requests()[1].ssrc == 0x05060708 && f.requests()[1].seq_nr == 255;
    Check(ok, "FIR (two entries)");
  }
  for (bool decodable : {false, true}) {
    Bytes b = sm::loss::SerializeLossNotification({1, 2, 65530, static_cast<uint16_t>(65530 + 100), decodable});
    webrtc::rtcp::LossNotification l;
    bool ok = WebrtcParse(b.data(), b.size(), l);
    ok = ok && l.sender_ssrc() == 1 && l.media_ssrc() == 2 && l.last_decoded() == 65530 &&
         l.last_received() == static_cast<uint16_t>(65530 + 100) && l.decodability_flag() == decodable;
    Check(ok, decodable ? "loss notification (decodable)" : "loss notification (not decodable)");
  }
  {
    Bytes rr = {0x80, 201, 0x00, 0x01, 0x00, 0x00, 0x00, 77};
    std::vector<Bytes> parts = {rr, sm::loss::SerializeNack({77, 5, {1, 2}}), sm::loss::SerializePli({77, 5})};
    Bytes c = sm::loss::BuildCompoundRtcp(parts);
    webrtc::rtcp::CommonHeader h;
    bool ok = h.Parse(c.data(), c.size()) && h.type() == 201;
    const uint8_t* next = ok ? h.NextPacket() : nullptr;
    ok = ok && h.Parse(next, c.data() + c.size() - next) && h.type() == 205 && h.fmt() == 1;
    next = ok ? h.NextPacket() : nullptr;
    ok = ok && h.Parse(next, c.data() + c.size() - next) && h.type() == 206 && h.fmt() == 1 &&
         h.NextPacket() == c.data() + c.size();
    Check(ok, "compound RR + NACK + PLI");
  }
  {
    std::vector<sm::rtp::SdesChunk> chunks = {{0x01020304, {{sm::rtp::kSdesCname, "peer@example"}}}};
    Bytes b = sm::rtp::SerializeSdes(chunks);
    webrtc::rtcp::Sdes sdes;
    bool ok = WebrtcParse(b.data(), b.size(), sdes);
    ok = ok && sdes.chunks().size() == 1 && sdes.chunks()[0].ssrc == 0x01020304 &&
         sdes.chunks()[0].cname == "peer@example";
    Check(ok, "SDES (one CNAME chunk)");
  }
  {
    // libwebrtc's Bye keeps the first SSRC as the sender and the rest as CSRCs.
    Bytes b = sm::rtp::SerializeBye({{0x11223344, 0x55667788, 0x0A0B0C0D}, "going away"});
    webrtc::rtcp::Bye bye;
    bool ok = WebrtcParse(b.data(), b.size(), bye);
    ok = ok && bye.sender_ssrc() == 0x11223344 && bye.csrcs().size() == 2 && bye.csrcs()[0] == 0x55667788 &&
         bye.csrcs()[1] == 0x0A0B0C0D && bye.reason() == "going away";
    Check(ok, "BYE (three SSRCs and a reason)");
  }
  for (uint8_t pad : {1, 4, 255}) {
    sm::RtpPacket p;
    p.payload_type = 100;
    p.seq = 65535;
    p.timestamp = 0xCAFEBABE;
    p.ssrc = 0x01020304;
    p.padding_size = pad;
    Bytes b = p.Serialize();
    webrtc::RtpPacketReceived r;
    bool ok = r.Parse(b.data(), b.size()) && r.padding_size() == pad && r.payload_size() == 0 &&
              r.PayloadType() == 100 && r.SequenceNumber() == 65535 && r.Timestamp() == 0xCAFEBABE &&
              r.Ssrc() == 0x01020304;
    Check(ok, pad == 1 ? "RTP padding-only packet (1 byte)"
                       : pad == 4 ? "RTP padding-only packet (4 bytes)" : "RTP padding-only packet (255 bytes)");
  }
  {
    sm::RtpPacket p;
    p.payload_type = 96;
    p.payload = {1, 2, 3, 4, 5};
    p.padding_size = 7;
    Bytes b = p.Serialize();
    webrtc::RtpPacketReceived r;
    bool ok = r.Parse(b.data(), b.size()) && r.padding_size() == 7 && r.payload_size() == 5;
    Check(ok, "RTP media packet with padding");
  }
}

void WebrtcToSm() {
  std::printf("libwebrtc -> this library\n");
  {
    webrtc::rtcp::Nack n;
    n.SetSenderSsrc(0x11223344);
    n.SetMediaSsrc(0x55667788);
    n.SetPacketIds(kNackSeqs);
    Bytes b = WebrtcBuild(n);
    auto back = sm::loss::ParseNack(b);
    Check(back && back->sender_ssrc == 0x11223344 && back->media_ssrc == 0x55667788 && back->seqs == kNackSeqs,
          "generic NACK");
  }
  {
    webrtc::rtcp::Pli p;
    p.SetSenderSsrc(0xAABBCCDD);
    p.SetMediaSsrc(0x01020304);
    auto back = sm::loss::ParsePli(WebrtcBuild(p));
    Check(back && back->sender_ssrc == 0xAABBCCDD && back->media_ssrc == 0x01020304, "PLI");
  }
  {
    webrtc::rtcp::Fir f;
    f.SetSenderSsrc(0x0A0B0C0D);
    f.AddRequestTo(0x01020304, 7);
    f.AddRequestTo(0x05060708, 255);
    auto back = sm::loss::ParseFir(WebrtcBuild(f));
    bool ok = back && back->sender_ssrc == 0x0A0B0C0D && back->entries.size() == 2 &&
              back->entries[0].ssrc == 0x01020304 && back->entries[0].seq_nr == 7 &&
              back->entries[1].ssrc == 0x05060708 && back->entries[1].seq_nr == 255;
    Check(ok, "FIR (two entries)");
  }
  for (bool decodable : {false, true}) {
    webrtc::rtcp::LossNotification l;
    bool set = l.Set(65530, static_cast<uint16_t>(65530 + 100), decodable);
    l.SetSenderSsrc(1);
    l.SetMediaSsrc(2);
    auto back = sm::loss::ParseLossNotification(WebrtcBuild(l));
    bool ok = set && back && back->sender_ssrc == 1 && back->media_ssrc == 2 && back->last_decoded == 65530 &&
              back->last_received == static_cast<uint16_t>(65530 + 100) && back->decodability_flag == decodable;
    Check(ok, decodable ? "loss notification (decodable)" : "loss notification (not decodable)");
  }
  {
    webrtc::rtcp::CompoundPacket c;
    auto rr = std::make_unique<webrtc::rtcp::ReceiverReport>();
    rr->SetSenderSsrc(77);
    c.Append(std::move(rr));
    auto nack = std::make_unique<webrtc::rtcp::Nack>();
    nack->SetSenderSsrc(77);
    nack->SetMediaSsrc(5);
    nack->SetPacketIds(std::vector<uint16_t>{1, 2});
    c.Append(std::move(nack));
    auto pli = std::make_unique<webrtc::rtcp::Pli>();
    pli->SetSenderSsrc(77);
    pli->SetMediaSsrc(5);
    c.Append(std::move(pli));
    Bytes b = WebrtcBuild(c);
    auto views = sm::loss::SplitCompoundRtcp(b);
    bool ok = views && views->size() == 3 && (*views)[0].type == sm::loss::RtcpType::kReceiverReport &&
              (*views)[1].type == sm::loss::RtcpType::kNack && (*views)[2].type == sm::loss::RtcpType::kPli;
    ok = ok && sm::loss::ParseNack((*views)[1].data).has_value() && sm::loss::ParsePli((*views)[2].data).has_value();
    Check(ok, "compound RR + NACK + PLI");
  }

  {
    webrtc::rtcp::Sdes sdes;
    sdes.AddCName(0x01020304, "peer@example");
    auto back = sm::rtp::ParseSdes(WebrtcBuild(sdes));
    bool ok = back && back->size() == 1 && (*back)[0].ssrc == 0x01020304 && (*back)[0].items.size() == 1 &&
              (*back)[0].items[0].type == sm::rtp::kSdesCname && (*back)[0].items[0].text == "peer@example";
    Check(ok, "SDES (one CNAME chunk)");
  }
  {
    webrtc::rtcp::Bye bye;
    bye.SetSenderSsrc(0x11223344);
    bye.SetCsrcs({0x55667788, 0x0A0B0C0D});
    bye.SetReason("going away");
    auto back = sm::rtp::ParseBye(WebrtcBuild(bye));
    bool ok = back && back->ssrcs == std::vector<uint32_t>{0x11223344, 0x55667788, 0x0A0B0C0D} &&
              back->reason == "going away";
    Check(ok, "BYE (three SSRCs and a reason)");
  }
  for (size_t pad : {size_t{1}, size_t{4}, size_t{255}}) {
    webrtc::RtpPacketReceived r;
    r.SetPayloadType(100);
    r.SetSequenceNumber(65535);
    r.SetTimestamp(0xCAFEBABE);
    r.SetSsrc(0x01020304);
    r.SetPayloadSize(3);
    r.SetPadding(pad);
    auto back = sm::RtpPacket::Parse(std::span<const uint8_t>(r.data(), r.size()));
    bool ok = back && back->padding_size == pad && back->payload.size() == 3 && back->payload_type == 100 &&
              back->seq == 65535 && back->timestamp == 0xCAFEBABE && back->ssrc == 0x01020304;
    Check(ok, pad == 1 ? "RTP packet with padding (1 byte)"
                       : pad == 4 ? "RTP packet with padding (4 bytes)" : "RTP packet with padding (255 bytes)");
  }
}

// Walks a compound datagram with libwebrtc's CommonHeader and returns the
// header of each packet, or an empty list if libwebrtc rejects it.
std::vector<webrtc::rtcp::CommonHeader> WebrtcWalk(const Bytes& c) {
  std::vector<webrtc::rtcp::CommonHeader> out;
  const uint8_t* p = c.data();
  const uint8_t* end = c.data() + c.size();
  while (p != end) {
    webrtc::rtcp::CommonHeader h;
    if (!h.Parse(p, static_cast<size_t>(end - p))) return {};
    out.push_back(h);
    p = h.NextPacket();
  }
  return out;
}

void EngineToWebrtc() {
  std::printf("rtp::RtcpEngine datagrams -> libwebrtc\n");
  sm::timing::ReceiveStatistics stats(0x2000, 90000);
  stats.OnPacket(10, 0, sm::Timestamp::Millis(0));
  stats.OnPacket(12, 6000, sm::Timestamp::Millis(70));
  {
    sm::rtp::RtcpConfig c;
    c.local_ssrc = 0x1111;
    c.cname = "engine@example";
    c.first_report_delay = sm::TimeDelta::Millis(10);
    sm::rtp::RtcpEngine e(c);
    e.AddReceiveStatistics(&stats);
    e.Process(sm::Timestamp::Millis(0));
    auto out = e.Process(sm::Timestamp::Millis(10));
    bool ok = out.size() == 1;
    auto hs = ok ? WebrtcWalk(out[0].bytes) : std::vector<webrtc::rtcp::CommonHeader>{};
    webrtc::rtcp::ReceiverReport rr;
    webrtc::rtcp::Sdes sdes;
    ok = ok && hs.size() == 2 && rr.Parse(hs[0]) && sdes.Parse(hs[1]);
    ok = ok && rr.sender_ssrc() == 0x1111 && rr.report_blocks().size() == 1 &&
         rr.report_blocks()[0].source_ssrc() == 0x2000 && sdes.chunks().size() == 1 &&
         sdes.chunks()[0].cname == "engine@example";
    Check(ok, "report compound: RR with a block + SDES CNAME");
  }
  {
    sm::rtp::RtcpConfig c;
    c.first_report_delay = sm::TimeDelta::Millis(500);
    sm::rtp::RtcpEngine e(c);
    e.SetSenderState({.ssrc = 0x3333, .packet_count = 77, .octet_count = 9000, .rtp_timestamp = 5000});
    e.AddReceiveStatistics(&stats);
    e.Process(sm::Timestamp::Millis(0));
    auto out = e.Process(sm::Timestamp::Millis(500));
    bool ok = out.size() == 1;
    auto hs = ok ? WebrtcWalk(out[0].bytes) : std::vector<webrtc::rtcp::CommonHeader>{};
    webrtc::rtcp::SenderReport sr;
    ok = ok && !hs.empty() && sr.Parse(hs[0]);
    ok = ok && sr.sender_ssrc() == 0x3333 && sr.sender_packet_count() == 77 && sr.sender_octet_count() == 9000 &&
         sr.rtp_timestamp() == 5000u + 500u * 90u && sr.report_blocks().size() == 1;
    Check(ok, "report compound: SR with counters and a block");
  }
  {
    sm::rtp::RtcpConfig c;
    c.local_ssrc = 0x4444;
    sm::rtp::RtcpEngine e(c);
    e.Process(sm::Timestamp::Millis(0));
    std::vector<uint16_t> seqs = {65534, 65535, 0, 9};
    e.QueueNack(0x5555, seqs);
    e.QueuePli(0x5555);
    e.QueueFir(0x5555);
    sm::cc::TransportFeedbackBuilder b(0, 0x5555);
    for (int i = 0; i < 5; ++i) b.OnPacket(static_cast<uint16_t>(65533 + i), sm::Timestamp::Millis(7 * i));
    for (auto& fb : b.Build()) e.QueueTransportFeedback(fb);
    std::vector<uint32_t> ssrcs = {0x5555};
    e.QueueRemb(sm::DataRate::KilobitsPerSec(1200), ssrcs);
    auto out = e.Process(sm::Timestamp::Millis(1));
    bool ok = out.size() == 1;
    auto hs = ok ? WebrtcWalk(out[0].bytes) : std::vector<webrtc::rtcp::CommonHeader>{};
    ok = ok && hs.size() == 6;
    webrtc::rtcp::ReceiverReport lead;
    webrtc::rtcp::Nack nack;
    webrtc::rtcp::Pli pli;
    webrtc::rtcp::Fir fir;
    webrtc::rtcp::TransportFeedback tcc;
    webrtc::rtcp::Remb remb;
    ok = ok && lead.Parse(hs[0]) && lead.sender_ssrc() == 0x4444 && lead.report_blocks().empty() &&
         nack.Parse(hs[1]) && nack.packet_ids() == std::vector<uint16_t>(seqs) && pli.Parse(hs[2]) &&
         pli.media_ssrc() == 0x5555 && fir.Parse(hs[3]) && fir.requests().size() == 1 &&
         fir.requests()[0].ssrc == 0x5555 && tcc.Parse(hs[4]) && tcc.sender_ssrc() == 0x4444 &&
         tcc.media_ssrc() == 0x5555 && remb.Parse(hs[5]) && remb.sender_ssrc() == 0x4444 &&
         remb.ssrcs() == std::vector<uint32_t>{0x5555} && remb.bitrate_bps() > 1100000 &&
         remb.bitrate_bps() < 1300000;
    Check(ok, "feedback compound: RR + NACK + PLI + FIR + transport-cc + REMB");
  }
}

void WebrtcToEngine() {
  std::printf("libwebrtc -> rtp::RtcpEngine\n");
  webrtc::rtcp::ReceiverReport rr;
  rr.SetSenderSsrc(0x6666);
  webrtc::rtcp::ReportBlock rb;
  rb.SetMediaSsrc(0x7777);
  rb.SetFractionLost(25);
  rb.SetCumulativeLost(3);
  rb.SetExtHighestSeqNum(70000);
  rb.SetJitter(123);
  rb.SetLastSr(0x01020304);
  rb.SetDelayLastSr(0x00050000);
  rr.AddReportBlock(rb);
  webrtc::rtcp::Sdes sdes;
  sdes.AddCName(0x6666, "libwebrtc@example");
  webrtc::rtcp::Nack nack;
  nack.SetSenderSsrc(0x6666);
  nack.SetMediaSsrc(0x7777);
  nack.SetPacketIds(kNackSeqs.data(), kNackSeqs.size());
  webrtc::rtcp::Pli pli;
  pli.SetSenderSsrc(0x6666);
  pli.SetMediaSsrc(0x7777);
  Bytes dg;
  for (const Bytes& part : {WebrtcBuild(rr), WebrtcBuild(sdes), WebrtcBuild(nack), WebrtcBuild(pli)})
    dg.insert(dg.end(), part.begin(), part.end());
  sm::rtp::RtcpEngine e;
  auto in = e.OnRtcp(dg, sm::Timestamp::Millis(1000));
  bool ok = in.valid && in.report_blocks.size() == 1 && in.report_blocks[0].ssrc == 0x7777 &&
            in.report_blocks[0].fraction_lost == 25 && in.report_blocks[0].cumulative_lost == 3 &&
            in.report_blocks[0].extended_highest_seq == 70000 && in.report_blocks[0].jitter == 123 &&
            in.report_blocks[0].last_sr == 0x01020304 && in.report_blocks[0].delay_since_last_sr == 0x00050000 &&
            in.sdes.size() == 1 && in.sdes[0].items[0].text == "libwebrtc@example" && in.nacks.size() == 1 &&
            in.nacks[0].seqs == kNackSeqs && in.keyframe_requests.size() == 1 &&
            in.keyframe_requests[0] == 0x7777 && e.stats().malformed == 0;
  Check(ok, "compound RR + SDES + NACK + PLI");
}

// Packets made by rtp::RtpSender and released by rtp::SendSideController are
// parsed by libwebrtc's RtpPacketReceived with the same extension ids.
void SenderToWebrtc() {
  sm::sync::ExtensionMap map;
  map.Register(1, sm::sync::uri::kTransportCc);
  map.Register(2, sm::sync::uri::kMid);
  map.Register(3, sm::sync::uri::kAbsSendTime);
  webrtc::RtpHeaderExtensionMap wmap;
  wmap.Register<webrtc::TransportSequenceNumber>(1);
  wmap.Register<webrtc::RtpMid>(2);
  wmap.Register<webrtc::AbsoluteSendTime>(3);

  sm::rtp::RtpSenderConfig sc;
  sc.ssrc = 0x1111;
  sc.payload_type = 96;
  sc.rtx_ssrc = 0x2222;
  sc.rtx_payload_type = 97;
  sc.mid = "video0";
  sc.mid_packets = 10;
  sc.extensions = &map;
  sc.initial_seq = 65535;
  sm::rtp::RtpSender sender(sc);
  sm::rtp::SendSideController ctl({}, &map);
  ctl.SetPaddingSender(&sender);
  ctl.Start(sm::Timestamp::Millis(0));

  std::vector<Bytes> chunks = {Bytes(300, 1), Bytes(200, 2)};
  sender.SendVideoFrame(chunks, 90000, sm::Timestamp::Millis(0));
  uint16_t want[] = {65535};
  sender.OnNack(want, sm::Timestamp::Millis(30), sm::TimeDelta::Millis(10));
  for (auto& p : sender.TakePackets()) ctl.Enqueue(std::move(p), sm::Timestamp::Millis(0));
  ctl.pacer().SetPacingRates(sm::DataRate::KilobitsPerSec(2000), sm::DataRate::KilobitsPerSec(100));
  std::vector<sm::rtp::ReleasedPacket> out;
  for (int t = 0; t < 100; ++t)
    for (auto& r : ctl.Process(sm::Timestamp::Millis(t))) out.push_back(std::move(r));
  bool saw_media = false, saw_rtx = false, saw_padding = false;
  bool ok = !out.empty();
  for (const auto& r : out) {
    webrtc::RtpPacketReceived w(&wmap);
    bool parsed = w.Parse(r.bytes.data(), r.bytes.size());
    uint16_t tseq = 0;
    bool has_tseq = parsed && w.GetExtension<webrtc::TransportSequenceNumber>(&tseq);
    ok = ok && parsed && has_tseq && tseq == r.transport_seq;
    uint32_t abs = 0;
    ok = ok && w.GetExtension<webrtc::AbsoluteSendTime>(&abs);
    if (r.kind == sm::rtp::PacketKind::kVideo) {
      saw_media = true;
      std::string mid;
      ok = ok && w.Ssrc() == 0x1111 && w.PayloadType() == 96 && w.GetExtension<webrtc::RtpMid>(&mid) && mid == "video0" &&
           (w.payload_size() == 300 || w.payload_size() == 200);
    } else if (r.kind == sm::rtp::PacketKind::kRtx) {
      saw_rtx = true;
      // RFC 4588: the first two payload bytes carry the original sequence number.
      ok = ok && w.Ssrc() == 0x2222 && w.PayloadType() == 97 && w.payload_size() == 302 && w.payload()[0] == 0xFF &&
           w.payload()[1] == 0xFF;
    } else if (r.kind == sm::rtp::PacketKind::kPadding) {
      saw_padding = true;
      ok = ok && w.Ssrc() == 0x2222 && w.PayloadType() == 97 && w.payload_size() == 0 && w.padding_size() > 0;
    }
  }
  Check(ok && saw_media && saw_rtx && saw_padding,
        "RtpSender: media, RTX and padding with MID, transport-cc and abs-send-time");
}

// A libwebrtc packet with the same extensions is parsed by this library.
void WebrtcToSender() {
  sm::sync::ExtensionMap map;
  map.Register(1, sm::sync::uri::kTransportCc);
  map.Register(2, sm::sync::uri::kMid);
  webrtc::RtpHeaderExtensionMap wmap;
  wmap.Register<webrtc::TransportSequenceNumber>(1);
  wmap.Register<webrtc::RtpMid>(2);
  webrtc::RtpPacketReceived w(&wmap);
  w.SetPayloadType(96);
  w.SetSequenceNumber(4242);
  w.SetTimestamp(90000);
  w.SetSsrc(0x1111);
  w.SetExtension<webrtc::TransportSequenceNumber>(777);
  w.SetExtension<webrtc::RtpMid>("video0");
  w.SetPayloadSize(10);
  auto p = sm::RtpPacket::Parse(std::span<const uint8_t>(w.data(), w.size()));
  sm::sync::ExtensionAccessor access(map);
  bool ok = p && p->seq == 4242 && p->payload.size() == 10 && access.GetTransportSeq(*p) == std::optional<uint16_t>(777) &&
            access.GetString(*p, sm::sync::uri::kMid) == std::optional<std::string>("video0");
  Check(ok, "libwebrtc packet with MID and transport-cc parsed");
}
}  // namespace

int main() {
  SmToWebrtc();
  WebrtcToSm();
  EngineToWebrtc();
  WebrtcToEngine();
  SenderToWebrtc();
  WebrtcToSender();
  if (g_failures) {
    std::printf("%d interop check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("all interop checks passed\n");
  return 0;
}
