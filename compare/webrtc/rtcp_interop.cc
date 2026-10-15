// RTCP interop check: this library's serialized NACK, PLI, FIR and loss
// notification (and a compound packet of them) are parsed by libwebrtc's
// rtcp_packet parsers, and libwebrtc's serialized packets are parsed by this
// library. Also covers SDES, BYE and padded RTP packets (plan PR 1 of
// docs/design/rtp-session.md). Exits non-zero on any mismatch.
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
#include "modules/rtp_rtcp/source/rtcp_packet/sdes.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "sm/core/rtp_packet.h"
#include "sm/loss/rtcp_feedback.h"
#include "sm/rtp/rtcp_sdes_bye.h"

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
}  // namespace

int main() {
  SmToWebrtc();
  WebrtcToSm();
  if (g_failures) {
    std::printf("%d interop check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("all interop checks passed\n");
  return 0;
}
