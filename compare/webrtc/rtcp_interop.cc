// RTCP interop check: this library's serialized NACK, PLI, FIR and loss
// notification (and a compound packet of them) are parsed by libwebrtc's
// rtcp_packet parsers, and libwebrtc's serialized packets are parsed by this
// library. Also covers SDES, BYE and padded RTP packets (plan PR 1 of
// docs/design/rtp-session.md) and the datagrams of rtp::RtcpEngine: report
// and feedback compounds with SR/RR, SDES, transport-cc and REMB (plan PR 2), and the RTP packets of rtp::RtpSender and
// rtp::SendSideController with MID, transport-wide sequence number and
// abs-send-time extensions, RTX and padding on the RTX SSRC (plan PR 3), and
// the H.264 payload format (RFC 6184 mode 1): rtp::H264Packetizer against
// libwebrtc's RtpPacketizerH264 and VideoRtpDepacketizerH264 (plan PR 7).
// Exits non-zero on any mismatch.
#include <algorithm>
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
#include "api/rtp_header_extension_id.h"
#include "modules/rtp_rtcp/include/rtp_header_extension_map.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_format.h"
#include "modules/rtp_rtcp/source/rtp_format_h264.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/rtp_rtcp/source/rtp_packet_to_send.h"
#include "modules/rtp_rtcp/source/video_rtp_depacketizer_h264.h"
#include "modules/video_coding/h26x_packet_buffer.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "sm/core/rtp_packet.h"
#include "sm/loss/rtcp_feedback.h"
#include "sm/rtp/fake_h264.h"
#include "sm/rtp/h264.h"
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
  wmap.Register<webrtc::TransportSequenceNumber>(webrtc::RtpHeaderExtensionId(1));
  wmap.Register<webrtc::RtpMid>(webrtc::RtpHeaderExtensionId(2));
  wmap.Register<webrtc::AbsoluteSendTime>(webrtc::RtpHeaderExtensionId(3));

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
  wmap.Register<webrtc::TransportSequenceNumber>(webrtc::RtpHeaderExtensionId(1));
  wmap.Register<webrtc::RtpMid>(webrtc::RtpHeaderExtensionId(2));
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
// H.264 access units of the shapes the test bench produces (keyframe with SPS
// and PPS, delta frames from one packet to many) and a few with several NAL
// units, at the payload limit the bench uses.
// libwebrtc's depacketizer parses SPS and PPS, so unlike the fake encoder's
// filler these are real parameter sets (Constrained Baseline, 640x480).
struct BitWriter {
  Bytes out;
  int used = 8;  // Bits used in the last byte; 8 means start a new one.
  void Bit(int b) {
    if (used == 8) {
      out.push_back(0);
      used = 0;
    }
    out.back() = static_cast<uint8_t>(out.back() | (b << (7 - used++)));
  }
  void Bits(uint32_t v, int n) {
    for (int i = n - 1; i >= 0; --i) Bit((v >> i) & 1);
  }
  void Ue(uint32_t v) {
    int len = 0;
    while (((v + 1) >> (len + 1)) != 0) ++len;
    Bits(0, len);
    Bits(v + 1, len + 1);
  }
  void Se(int v) { Ue(v <= 0 ? static_cast<uint32_t>(-2 * v) : static_cast<uint32_t>(2 * v - 1)); }
  Bytes Finish() {
    Bit(1);  // rbsp_stop_one_bit; the rest of the byte is already zero.
    return out;
  }
};

Bytes RealSps() {
  BitWriter w;
  w.Bits(0x67, 8);
  w.Bits(66, 8);    // profile_idc: Baseline
  w.Bits(0xC0, 8);  // constraint_set0/1 flags
  w.Bits(31, 8);    // level_idc 3.1
  w.Ue(0);          // seq_parameter_set_id
  w.Ue(0);          // log2_max_frame_num_minus4
  w.Ue(0);          // pic_order_cnt_type
  w.Ue(0);          // log2_max_pic_order_cnt_lsb_minus4
  w.Ue(1);          // max_num_ref_frames
  w.Bit(0);         // gaps_in_frame_num_value_allowed_flag
  w.Ue(39);         // pic_width_in_mbs_minus1 (640)
  w.Ue(29);         // pic_height_in_map_units_minus1 (480)
  w.Bit(1);         // frame_mbs_only_flag
  w.Bit(1);         // direct_8x8_inference_flag
  w.Bit(0);         // frame_cropping_flag
  w.Bit(0);         // vui_parameters_present_flag
  return w.Finish();
}

Bytes RealPps() {
  BitWriter w;
  w.Bits(0x68, 8);
  w.Ue(0);       // pic_parameter_set_id
  w.Ue(0);       // seq_parameter_set_id
  w.Bit(0);      // entropy_coding_mode_flag
  w.Bit(0);      // bottom_field_pic_order_in_frame_present_flag
  w.Ue(0);       // num_slice_groups_minus1
  w.Ue(0);       // num_ref_idx_l0_default_active_minus1
  w.Ue(0);       // num_ref_idx_l1_default_active_minus1
  w.Bit(0);      // weighted_pred_flag
  w.Bits(0, 2);  // weighted_bipred_idc
  w.Se(0);       // pic_init_qp_minus26
  w.Se(0);       // pic_init_qs_minus26
  w.Se(0);       // chroma_qp_index_offset
  w.Bit(1);      // deblocking_filter_control_present_flag
  w.Bit(0);      // constrained_intra_pred_flag
  w.Bit(0);      // redundant_pic_cnt_present_flag
  return w.Finish();
}

// The fake encoder's frame with its SPS and PPS replaced by the real ones.
Bytes WithRealParameterSets(const Bytes& fake) {
  auto nals = sm::rtp::SplitAnnexB(fake);
  Bytes out = {0, 0, 0, 1};
  Bytes sps = RealSps(), pps = RealPps();
  out.insert(out.end(), sps.begin(), sps.end());
  out.insert(out.end(), {0, 0, 0, 1});
  out.insert(out.end(), pps.begin(), pps.end());
  out.insert(out.end(), {0, 0, 0, 1});
  out.insert(out.end(), nals[2].begin(), nals[2].end());
  return out;
}

std::vector<Bytes> H264Frames() {
  std::vector<Bytes> frames;
  for (size_t bytes : {200u, 1100u, 1300u, 2500u, 7000u, 30000u}) {
    frames.push_back(WithRealParameterSets(sm::rtp::MakeFakeH264Frame(true, bytes)));
    frames.push_back(sm::rtp::MakeFakeH264Frame(false, bytes));
  }
  // Many small slices: STAP-A with more than two units, then a split one.
  Bytes slices;
  for (int k = 0; k < 40; ++k) {
    // Slice header: first_mb_in_slice (0, then 1), slice_type 1, pps id 0.
    Bytes nal = {0, 0, 0, 1, 0x41, static_cast<uint8_t>(k == 0 ? 0xAA : 0x4A)};
    for (int i = 0; i < 90; ++i) nal.push_back(static_cast<uint8_t>(0x30 + (i + k) % 100));
    slices.insert(slices.end(), nal.begin(), nal.end());
  }
  frames.push_back(slices);
  return frames;
}

constexpr size_t kH264Limit = 1188;

// This library's packets are parsed by libwebrtc's depacketizer, and the frame
// libwebrtc rebuilds is the one that went in.
void H264SmToWebrtc() {
  bool ok = true, key_seen = false, delta_seen = false, firsts_ok = true;
  size_t frames = 0;
  // The real receive path: Parse gives NAL payloads without start codes (and
  // STAP-A as it came), and H26xPacketBuffer finds the frame, adds the start
  // codes and returns its packets. One buffer for the whole sequence, as a
  // receiver has; the first frame is a keyframe.
  webrtc::H26xPacketBuffer buffer(/*h264_idr_only_keyframes_allowed=*/true);
  webrtc::VideoRtpDepacketizerH264 dep;
  int64_t seq = 1000;
  uint32_t timestamp = 90000;
  for (const Bytes& frame : H264Frames()) {
    auto payloads = sm::rtp::H264Packetizer::Packetize(frame, kH264Limit);
    bool frame_key = false;
    Bytes rebuilt;
    for (size_t i = 0; i < payloads.size(); ++i) {
      ok = ok && payloads[i].size() <= kH264Limit;
      auto parsed = dep.Parse(webrtc::CopyOnWriteBuffer(payloads[i].data(), payloads[i].size()));
      if (!parsed) {
        std::printf("    frame %zu (%zu bytes): packet %zu/%zu (%zu bytes, type %u) not parsed\n", frames, frame.size(), i,
                    payloads.size(), payloads[i].size(), payloads[i][0] & 0x1F);
        ok = false;
        ++seq;
        continue;
      }
      if (i == 0) firsts_ok = firsts_ok && parsed->video_header.is_first_packet_in_frame;
      auto mine = sm::rtp::H264Depacketizer::Parse(payloads[i]);
      if (mine.fu_continuation) firsts_ok = firsts_ok && !parsed->video_header.is_first_packet_in_frame;
      frame_key = frame_key || parsed->video_header.frame_type == webrtc::VideoFrameType::kVideoFrameKey;
      const bool last = i + 1 == payloads.size();
      webrtc::RtpPacketReceived rtp;
      rtp.SetPayloadType(96);
      rtp.SetSequenceNumber(static_cast<uint16_t>(seq));
      rtp.SetTimestamp(timestamp);
      rtp.SetSsrc(0x1111);
      rtp.SetMarker(last);
      parsed->video_header.is_last_packet_in_frame = last;
      auto packet = std::make_unique<webrtc::video_coding::PacketBuffer::Packet>(rtp, seq, parsed->video_header);
      packet->video_payload = parsed->video_payload;
      auto result = buffer.InsertPacket(std::move(packet));
      for (const auto& released : result.packets)
        rebuilt.insert(rebuilt.end(), released->video_payload.cdata(), released->video_payload.cdata() + released->video_payload.size());
      ++seq;
    }
    timestamp += 3000;
    const bool is_key = frame.size() > 5 && frame[4] == 0x67;
    if (frame_key != is_key) std::printf("    frame %zu (%zu bytes): key=%d, libwebrtc says key=%d\n", frames, frame.size(), is_key, frame_key);
    ok = ok && frame_key == is_key;
    (is_key ? key_seen : delta_seen) = true;
    // Same NAL units, whatever the start code length.
    auto want = sm::rtp::SplitAnnexB(frame);
    auto got = sm::rtp::SplitAnnexB(rebuilt);
    bool same = want.size() == got.size();
    for (size_t k = 0; same && k < want.size(); ++k) same = std::equal(want[k].begin(), want[k].end(), got[k].begin(), got[k].end());
    if (!same) {
      std::printf("    frame %zu (%zu bytes, %zu packets): H26xPacketBuffer gave %zu bytes in %zu NAL units, expected %zu units\n",
                  frames, frame.size(), payloads.size(), rebuilt.size(), got.size(), want.size());
    }
    ok = ok && same;
    ++frames;
  }
  Check(ok && key_seen && delta_seen && frames > 10, "H.264: packetized here, parsed and reassembled by libwebrtc");
  Check(firsts_ok, "H.264: first-packet and FU-A continuation flags agree");
}

// libwebrtc's packets are parsed and reassembled by this library, and they
// are the same payloads this packetizer makes (STAP-A, single NAL, FU-A).
void H264WebrtcToSm() {
  bool ok = true, same = true;
  size_t frames = 0, packets = 0;
  for (const Bytes& frame : H264Frames()) {
    webrtc::RtpPacketizer::PayloadSizeLimits limits;
    limits.max_payload_len = static_cast<int>(kH264Limit);
    webrtc::RtpPacketizerH264 packetizer(std::span<const uint8_t>(frame), limits, webrtc::H264PacketizationMode::NonInterleaved);
    std::vector<sm::RtpPacket> pkts;
    const size_t n = packetizer.NumPackets();
    for (size_t i = 0; i < n; ++i) {
      webrtc::RtpPacketToSend out(nullptr);
      if (!packetizer.NextPacket(&out)) {
        ok = false;
        break;
      }
      sm::RtpPacket p;
      p.marker = out.Marker();
      p.payload.assign(out.payload().begin(), out.payload().end());
      ok = ok && p.payload.size() <= kH264Limit && p.marker == (i + 1 == n);
      ok = ok && sm::rtp::H264Depacketizer::Parse(p.payload).valid;
      pkts.push_back(std::move(p));
    }
    Bytes rebuilt;
    ok = ok && sm::rtp::H264Depacketizer::Reassemble(pkts, rebuilt) && rebuilt == frame;
    auto mine = sm::rtp::H264Packetizer::Packetize(frame, kH264Limit);
    bool eq = mine.size() == pkts.size();
    for (size_t i = 0; eq && i < mine.size(); ++i) eq = mine[i] == pkts[i].payload;
    if (!eq) {
      std::printf("    frame %zu (%zu bytes): this library %zu packets, libwebrtc %zu\n", frames, frame.size(), mine.size(), pkts.size());
      for (size_t i = 0; i < std::min(mine.size(), pkts.size()); ++i) {
        if (mine[i] != pkts[i].payload) {
          std::printf("      first different payload %zu: %zu bytes (type %u) vs %zu bytes (type %u)\n", i, mine[i].size(),
                      mine[i][0] & 0x1F, pkts[i].payload.size(), pkts[i].payload[0] & 0x1F);
          break;
        }
      }
    }
    same = same && eq;
    packets += pkts.size();
    ++frames;
  }
  Check(ok && frames > 10 && packets > frames, "H.264: packetized by libwebrtc, parsed and reassembled here");
  Check(same, "H.264: this packetizer makes the same payloads as libwebrtc's");
}
}  // namespace

int main() {
  SmToWebrtc();
  WebrtcToSm();
  EngineToWebrtc();
  WebrtcToEngine();
  SenderToWebrtc();
  WebrtcToSender();
  H264SmToWebrtc();
  H264WebrtcToSm();
  if (g_failures) {
    std::printf("%d interop check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("all interop checks passed\n");
  return 0;
}
