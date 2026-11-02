// RtpTransport (#1007), libwebrtc M157's pc/rtp_transport.cc behaviour over FakePacketTransport: ready to send and
// writable with and without rtcp-mux, RFC 5761 classification, demultiplexing to sinks, packet-size checks, and the
// header extension map by MID. pc/rtp_transport_unittest.cc is not in compare/webrtc/reference/; the cases follow
// the code. Plus media/base/rtp_utils' classification.
#include <memory>
#include <vector>

#include "pc_test_util.h"
#include "sm/pc/rtp_transport.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::pc;
using namespace sm::pc::test;

namespace {

SM_TEST(PcRtpUtils, InferRtpPacketType) {
  SM_EXPECT_TRUE(InferRtpPacketType(PcmuFrame()) == RtpPacketType::kRtp);
  SM_EXPECT_TRUE(InferRtpPacketType(kFakeRtcpReport) == RtpPacketType::kRtcp);
  // Payload-type bytes 64-95 (RTCP 192-223 with the marker bit) are RTCP; 96 is RTP.
  std::vector<uint8_t> p = PcmuFrame();
  p[1] = 0x80 | 63;
  SM_EXPECT_TRUE(InferRtpPacketType(p) == RtpPacketType::kRtp);
  p[1] = 0x80 | 64;
  SM_EXPECT_TRUE(InferRtpPacketType(p) == RtpPacketType::kRtcp);
  p[1] = 95;
  SM_EXPECT_TRUE(InferRtpPacketType(p) == RtpPacketType::kRtcp);
  p[1] = 96;
  SM_EXPECT_TRUE(InferRtpPacketType(p) == RtpPacketType::kRtp);
  // Wrong version, or too short for either.
  p[0] = 0x40;
  SM_EXPECT_TRUE(InferRtpPacketType(p) == RtpPacketType::kUnknown);
  const std::vector<uint8_t> short_rtp = {0x80, 0x00, 0x00, 0x01, 0, 0, 0, 0};
  SM_EXPECT_TRUE(InferRtpPacketType(short_rtp) == RtpPacketType::kUnknown);
  const std::vector<uint8_t> short_rtcp = {0x80, 0xC9, 0x00};
  SM_EXPECT_TRUE(InferRtpPacketType(short_rtcp) == RtpPacketType::kUnknown);
}

SM_TEST(PcRtpUtils, IsValidRtpPacketSize) {
  SM_EXPECT_FALSE(IsValidRtpPacketSize(RtpPacketType::kRtp, 11));
  SM_EXPECT_TRUE(IsValidRtpPacketSize(RtpPacketType::kRtp, 12));
  SM_EXPECT_TRUE(IsValidRtpPacketSize(RtpPacketType::kRtcp, 4));
  SM_EXPECT_FALSE(IsValidRtpPacketSize(RtpPacketType::kRtcp, 3));
  SM_EXPECT_TRUE(IsValidRtpPacketSize(RtpPacketType::kRtp, 2048));
  SM_EXPECT_FALSE(IsValidRtpPacketSize(RtpPacketType::kRtp, 2049));
}

SM_TEST(PcRtpTransport, ReadyToSendWithRtcpMux) {
  FakePacketTransport a("a"), b("b");
  RtpTransport t(/*rtcp_mux_enabled=*/true);
  TransportObserver obs(&t);
  t.SetRtpPacketTransport(&a);
  SM_EXPECT_FALSE(t.IsReadyToSend());
  a.SetDestination(&b);
  SM_EXPECT_TRUE(t.IsReadyToSend());
  SM_EXPECT_TRUE(obs.ready_to_send());
  SM_EXPECT_EQ(obs.ready_to_send_signal_count(), 1);
  SM_EXPECT_TRUE(t.IsWritable(false) && t.IsWritable(true));
}

SM_TEST(PcRtpTransport, ReadyToSendWithoutRtcpMuxNeedsBoth) {
  FakePacketTransport rtp_a("a"), rtp_b("b"), rtcp_a("ac"), rtcp_b("bc");
  RtpTransport t(/*rtcp_mux_enabled=*/false);
  TransportObserver obs(&t);
  t.SetRtpPacketTransport(&rtp_a);
  t.SetRtcpPacketTransport(&rtcp_a);
  rtp_a.SetDestination(&rtp_b);
  SM_EXPECT_FALSE(t.IsReadyToSend());
  SM_EXPECT_TRUE(t.IsWritable(false));
  SM_EXPECT_FALSE(t.IsWritable(true));
  rtcp_a.SetDestination(&rtcp_b);
  SM_EXPECT_TRUE(t.IsReadyToSend());
  SM_EXPECT_TRUE(obs.ready_to_send());
  // Turning rtcp-mux on keeps it ready; RTCP then goes over the RTP transport.
  t.SetRtcpMuxEnabled(true);
  SM_EXPECT_TRUE(t.IsReadyToSend());
  std::vector<uint8_t> rtcp = kFakeRtcpReport;
  SM_EXPECT_TRUE(t.SendRtcpPacket(rtcp, p2p::PacketOptions{}, 0, rtp_a.now));
  SM_EXPECT_EQ(rtp_a.sent, 1);
  SM_EXPECT_EQ(rtcp_a.sent, 0);
}

SM_TEST(PcRtpTransport, RtcpWithoutMuxGoesOverRtcpTransport) {
  FakePacketTransport rtp_a("a"), rtp_b("b"), rtcp_a("ac"), rtcp_b("bc");
  RtpTransport t1(false), t2(false);
  t1.SetRtpPacketTransport(&rtp_a);
  t1.SetRtcpPacketTransport(&rtcp_a);
  t2.SetRtpPacketTransport(&rtp_b);
  t2.SetRtcpPacketTransport(&rtcp_b);
  rtp_a.SetDestination(&rtp_b);
  rtcp_a.SetDestination(&rtcp_b);
  TransportObserver obs2(&t2);
  std::vector<uint8_t> rtcp = kFakeRtcpReport;
  SM_EXPECT_TRUE(t1.SendRtcpPacket(rtcp, p2p::PacketOptions{}, 0, rtp_a.now));
  SM_EXPECT_EQ(rtcp_a.sent, 1);
  SM_EXPECT_EQ(rtp_a.sent, 0);
  SM_EXPECT_EQ(obs2.rtcp_count(), 1);
  SM_EXPECT_TRUE(obs2.last_recv_rtcp_packet() == kFakeRtcpReport);
}

SM_TEST(PcRtpTransport, DemuxesRtpAndRtcpOnOneTransport) {
  FakePacketTransport a("a"), b("b");
  RtpTransport t1(true), t2(true);
  t1.SetRtpPacketTransport(&a);
  t2.SetRtpPacketTransport(&b);
  a.SetDestination(&b);
  TransportObserver obs2(&t2);
  SM_EXPECT_TRUE(t2.RegisterRtpDemuxerSink(PayloadTypeCriteria(0), &obs2));
  std::vector<uint8_t> rtp = PcmuFrame();
  SM_EXPECT_TRUE(t1.SendRtpPacket(rtp, p2p::PacketOptions{}, 0, a.now));
  SM_EXPECT_EQ(obs2.rtp_count(), 1);
  SM_EXPECT_TRUE(obs2.last_recv_rtp_packet() == PcmuFrame());
  SM_EXPECT_EQ(obs2.last_rtp().ssrc, 1u);
  SM_EXPECT_EQ(obs2.last_rtp().seq, 1);
  std::vector<uint8_t> rtcp = kFakeRtcpReport;
  SM_EXPECT_TRUE(t1.SendRtcpPacket(rtcp, p2p::PacketOptions{}, 0, a.now));
  SM_EXPECT_EQ(obs2.rtcp_count(), 1);
  SM_EXPECT_EQ(obs2.rtp_count(), 1);
  // A payload type no sink takes goes to the undemuxable handler. (Another SSRC: the first packet bound SSRC 1 to
  // the sink, and a bound SSRC is routed whatever its payload type, as libwebrtc's RtpDemuxer does.)
  std::vector<uint8_t> other = PcmuFrame();
  other[1] = 111;
  other[11] = 2;
  SM_EXPECT_TRUE(t1.SendRtpPacket(other, p2p::PacketOptions{}, 0, a.now));
  SM_EXPECT_EQ(obs2.rtp_count(), 1);
  SM_EXPECT_EQ(obs2.un_demuxable_rtp_count(), 1);
  // After unregistering, nothing is demuxed to it.
  SM_EXPECT_TRUE(t2.UnregisterRtpDemuxerSink(&obs2));
  SM_EXPECT_FALSE(t2.UnregisterRtpDemuxerSink(&obs2));
  std::vector<uint8_t> again = PcmuFrame();
  SM_EXPECT_TRUE(t1.SendRtpPacket(again, p2p::PacketOptions{}, 0, a.now));
  SM_EXPECT_EQ(obs2.rtp_count(), 1);
  SM_EXPECT_EQ(obs2.un_demuxable_rtp_count(), 2);
}

SM_TEST(PcRtpTransport, DropsBadSizesAndOtherPackets) {
  FakePacketTransport a("a"), b("b");
  RtpTransport t2(true);
  t2.SetRtpPacketTransport(&b);
  a.SetDestination(&b);
  TransportObserver obs2(&t2);
  SM_EXPECT_TRUE(t2.RegisterRtpDemuxerSink(PayloadTypeCriteria(0), &obs2));
  std::vector<uint8_t> big = PcmuFrame();
  big.resize(kMaxRtpPacketLen + 1, 0);
  a.SendPacket(a.now, big, p2p::PacketOptions{}, 0);
  const std::vector<uint8_t> dtls = {22, 0xFE, 0xFD, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};  // A DTLS record.
  a.SendPacket(a.now, dtls, p2p::PacketOptions{}, 0);
  SM_EXPECT_EQ(obs2.rtp_count(), 0);
  SM_EXPECT_EQ(obs2.rtcp_count(), 0);
  SM_EXPECT_EQ(obs2.un_demuxable_rtp_count(), 0);
}

SM_TEST(PcRtpTransport, SentPacketAndWritableSignals) {
  FakePacketTransport a("a"), b("b");
  RtpTransport t(true);
  TransportObserver obs(&t);
  std::vector<bool> writable;
  t.SubscribeWritableState(&writable, [&](bool w) { writable.push_back(w); });
  t.SetRtpPacketTransport(&a);
  a.SetDestination(&b);
  std::vector<uint8_t> rtp = PcmuFrame();
  SM_EXPECT_TRUE(t.SendRtpPacket(rtp, p2p::PacketOptions{}, 0, a.now));
  SM_EXPECT_EQ(obs.sent_packet_count(), 1);
  a.SetWritable(false);
  SM_ASSERT_EQ(writable.size(), size_t{2});
  SM_EXPECT_TRUE(writable[0]);
  SM_EXPECT_FALSE(writable[1]);
  // Changing the transport stops the old one's signals.
  t.SetRtpPacketTransport(&b);
  a.SetWritable(true);
  SM_EXPECT_EQ(writable.size(), size_t{2});
}

SM_TEST(PcRtpTransport, HeaderExtensionMapByMid) {
  RtpTransport t(true);
  const RtpHeaderExtensions audio = {{sync::uri::kMid, 1}, {sync::uri::kAudioLevel, 2}};
  SM_EXPECT_TRUE(t.RegisterRtpHeaderExtensionMap("0", audio));
  SM_EXPECT_TRUE(t.header_extension_map().UriOf(2) == std::string(sync::uri::kAudioLevel));
  // Bad and duplicate ids are refused.
  SM_EXPECT_FALSE(t.VerifyRtpHeaderExtensionMap({{sync::uri::kMid, 0}}));
  SM_EXPECT_FALSE(t.VerifyRtpHeaderExtensionMap({{sync::uri::kMid, 256}}));
  SM_EXPECT_FALSE(t.VerifyRtpHeaderExtensionMap({{sync::uri::kMid, 3}, {sync::uri::kTransportCc, 3}}));
  // An id active on another MID with another URI is a reassignment: refused.
  SM_EXPECT_FALSE(t.RegisterRtpHeaderExtensionMap("1", {{sync::uri::kTransportCc, 2}}));
  // The same URI at the same id on another MID is fine (BUNDLE).
  const RtpHeaderExtensions video = {{sync::uri::kMid, 1}, {sync::uri::kTransportCc, 3}};
  SM_EXPECT_TRUE(t.RegisterRtpHeaderExtensionMap("1", video));
  SM_EXPECT_TRUE(t.header_extension_map().UriOf(3) == std::string(sync::uri::kTransportCc));
  SM_EXPECT_TRUE(t.header_extension_map().UriOf(2) == std::string(sync::uri::kAudioLevel));
  // Unregistering a MID drops what only it used.
  t.UnregisterRtpHeaderExtensionMap("0");
  SM_EXPECT_FALSE(t.header_extension_map().UriOf(2).has_value());
  SM_EXPECT_TRUE(t.header_extension_map().UriOf(1) == std::string(sync::uri::kMid));
}

// The demuxer reads the MID with the registered map: a sink for MID "v" gets the packets that carry it.
SM_TEST(PcRtpTransport, DemuxesByMid) {
  FakePacketTransport a("a"), b("b");
  RtpTransport t2(true);
  t2.SetRtpPacketTransport(&b);
  a.SetDestination(&b);
  SM_EXPECT_TRUE(t2.RegisterRtpHeaderExtensionMap("v", {{sync::uri::kMid, 1}}));
  TransportObserver audio_sink, video_sink;
  audio_sink.Attach(&t2);
  RtpDemuxerCriteria vc;
  vc.mid = "v";
  SM_EXPECT_TRUE(t2.RegisterRtpDemuxerSink(vc, &video_sink));
  SM_EXPECT_TRUE(t2.RegisterRtpDemuxerSink(PayloadTypeCriteria(0), &audio_sink));
  // PCMU with MID "v" in a one-byte extension (id 1, 1 byte).
  std::vector<uint8_t> p = {0x90, 0x00, 0x00, 0x05, 0, 0, 0, 0, 0, 0, 0, 0x07, 0xBE, 0xDE, 0x00, 0x01, 0x10, 'v', 0, 0};
  p.resize(p.size() + 20, 0xFF);
  a.SendPacket(a.now, p, p2p::PacketOptions{}, 0);
  SM_EXPECT_EQ(video_sink.rtp_count(), 1);
  SM_EXPECT_EQ(audio_sink.rtp_count(), 0);
  a.SendPacket(a.now, PcmuFrame(), p2p::PacketOptions{}, 0);
  SM_EXPECT_EQ(audio_sink.rtp_count(), 1);
}

}  // namespace
