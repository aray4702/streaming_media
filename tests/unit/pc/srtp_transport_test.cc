// SrtpTransport (#1007): libwebrtc M157's pc/srtp_transport_unittest.cc, case for case, over FakePacketTransport.
// The two network-route cases (NetworkRouteOverhead*WhenSrtpActivated) are left out with the network route
// (sm/pc/rtp_transport.h); a re-keying case (SetRtpParams on active sessions, as a DTLS restart does) is added.
#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include "pc_test_util.h"
#include "sm/pc/srtp_transport.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::pc;
using namespace sm::pc::test;

namespace {

struct Fixture {
  std::unique_ptr<FakePacketTransport> rtp_packet_transport1 = std::make_unique<FakePacketTransport>("fake1");
  std::unique_ptr<FakePacketTransport> rtp_packet_transport2 = std::make_unique<FakePacketTransport>("fake2");
  std::unique_ptr<SrtpTransport> srtp_transport1 = std::make_unique<SrtpTransport>(/*rtcp_mux_enabled=*/true);
  std::unique_ptr<SrtpTransport> srtp_transport2 = std::make_unique<SrtpTransport>(/*rtcp_mux_enabled=*/true);
  TransportObserver rtp_sink1, rtp_sink2;
  uint16_t sequence_number = 0;

  Fixture() {
    rtp_packet_transport1->SetDestination(rtp_packet_transport2.get());
    srtp_transport1->SetRtpPacketTransport(rtp_packet_transport1.get());
    srtp_transport2->SetRtpPacketTransport(rtp_packet_transport2.get());
    rtp_sink1.Attach(srtp_transport1.get());
    rtp_sink2.Attach(srtp_transport2.get());
    // 0x00 is the payload type of PcmuFrame.
    srtp_transport1->RegisterRtpDemuxerSink(PayloadTypeCriteria(0), &rtp_sink1);
    srtp_transport2->RegisterRtpDemuxerSink(PayloadTypeCriteria(0), &rtp_sink2);
  }
  ~Fixture() {
    srtp_transport1->UnregisterRtpDemuxerSink(&rtp_sink1);
    srtp_transport2->UnregisterRtpDemuxerSink(&rtp_sink2);
  }
  Timestamp now() const { return rtp_packet_transport1->now; }

  // One packet each way: it arrives decrypted, and what went on the wire is not the plain packet.
  void SendRecvRtpOneWay(SrtpTransport* from, FakePacketTransport* wire, TransportObserver& to,
                         const std::vector<uint8_t>& original, int suite, const std::vector<int>& encrypted_ids) {
    std::vector<uint8_t> packet = original;
    const int before = to.rtp_count();
    SM_ASSERT_TRUE(from->SendRtpPacket(packet, p2p::PacketOptions{}, p2p::kPfSrtpBypass, now()));
    SM_EXPECT_EQ(packet.size(), original.size() + rtp_auth_tag_len(suite));
    SM_EXPECT_EQ(to.rtp_count(), before + 1);
    SM_EXPECT_TRUE(to.last_recv_rtp_packet() == original);
    SM_ASSERT_TRUE(wire->last_sent_packet().has_value());
    const std::vector<uint8_t>& sent = *wire->last_sent_packet();
    SM_EXPECT_NE(0, std::memcmp(sent.data(), original.data(), original.size()));
    if (original[0] & 0x10) {  // CompareHeaderExtensions: only the listed ids are encrypted.
      for (int id = 1; id <= 4; ++id) {
        const bool encrypted = std::find(encrypted_ids.begin(), encrypted_ids.end(), id) != encrypted_ids.end();
        SM_EXPECT_EQ(ExtensionValue(sent, id) != ExtensionValue(original, id), encrypted);
      }
    }
  }
  void TestSendRecvRtpPacket(int suite, bool with_extensions = false, const std::vector<int>& encrypted_ids = {}) {
    std::vector<uint8_t> original = with_extensions ? PcmuFrameWithExtensions() : PcmuFrame();
    // A sequence number is used once: libsrtp's replay protection would refuse it again.
    SetSeq(original, ++sequence_number);
    SendRecvRtpOneWay(srtp_transport1.get(), rtp_packet_transport1.get(), rtp_sink2, original, suite, encrypted_ids);
    SendRecvRtpOneWay(srtp_transport2.get(), rtp_packet_transport2.get(), rtp_sink1, original, suite, encrypted_ids);
  }
  void TestSendRecvRtcpPacket(int suite) {
    for (int dir = 0; dir < 2; ++dir) {
      SrtpTransport* from = dir == 0 ? srtp_transport1.get() : srtp_transport2.get();
      FakePacketTransport* wire = dir == 0 ? rtp_packet_transport1.get() : rtp_packet_transport2.get();
      TransportObserver& to = dir == 0 ? rtp_sink2 : rtp_sink1;
      std::vector<uint8_t> packet = kFakeRtcpReport;
      SM_ASSERT_TRUE(from->SendRtcpPacket(packet, p2p::PacketOptions{}, p2p::kPfSrtpBypass, now()));
      SM_EXPECT_EQ(packet.size(), kFakeRtcpReport.size() + 4 + rtcp_auth_tag_len(suite));
      SM_EXPECT_TRUE(to.last_recv_rtcp_packet() == kFakeRtcpReport);
      SM_EXPECT_NE(0, std::memcmp(wire->last_sent_packet()->data(), kFakeRtcpReport.data(), kFakeRtcpReport.size()));
    }
  }
  void TestSendRecvPacket(int suite, const std::vector<uint8_t>& key1, const std::vector<uint8_t>& key2) {
    SM_EXPECT_EQ(key1.size(), key2.size());
    const std::vector<int> ids;
    SM_EXPECT_TRUE(srtp_transport1->SetRtpParams(suite, key1, ids, suite, key2, ids));
    SM_EXPECT_TRUE(srtp_transport2->SetRtpParams(suite, key2, ids, suite, key1, ids));
    SM_EXPECT_TRUE(srtp_transport1->SetRtcpParams(suite, key1, ids, suite, key2, ids));
    SM_EXPECT_TRUE(srtp_transport2->SetRtcpParams(suite, key2, ids, suite, key1, ids));
    SM_EXPECT_TRUE(srtp_transport1->IsSrtpActive());
    SM_EXPECT_TRUE(srtp_transport2->IsSrtpActive());
    TestSendRecvRtpPacket(suite);
    TestSendRecvRtcpPacket(suite);
  }
  void TestSendRecvEncryptedHeaderExtension(int suite, const std::vector<uint8_t>& key1,
                                            const std::vector<uint8_t>& key2) {
    // Ids 1 and 4 encrypted; 2 and 3 not.
    const std::vector<int> encrypted = {kHeaderExtensionIDs[0], kHeaderExtensionIDs[1]};
    SM_EXPECT_TRUE(srtp_transport1->SetRtpParams(suite, key1, encrypted, suite, key2, encrypted));
    SM_EXPECT_TRUE(srtp_transport2->SetRtpParams(suite, key2, encrypted, suite, key1, encrypted));
    SM_EXPECT_TRUE(srtp_transport1->IsSrtpActive());
    SM_EXPECT_TRUE(srtp_transport2->IsSrtpActive());
    TestSendRecvRtpPacket(suite, /*with_extensions=*/true, encrypted);
  }
};

SM_TEST(PcSrtpTransport, SendAndRecvPacket_AES_CM_128_HMAC_SHA1_80) {
  Fixture f;
  f.TestSendRecvPacket(tls::kSrtpAes128CmSha1_80, kTestKey1, kTestKey2);
}
SM_TEST(PcSrtpTransport, SendAndRecvPacketWithHeaderExtension_AES_CM_128_HMAC_SHA1_80) {
  Fixture f;
  f.TestSendRecvEncryptedHeaderExtension(tls::kSrtpAes128CmSha1_80, kTestKey1, kTestKey2);
}
SM_TEST(PcSrtpTransport, SendAndRecvPacket_AES_CM_128_HMAC_SHA1_32) {
  Fixture f;
  f.TestSendRecvPacket(tls::kSrtpAes128CmSha1_32, kTestKey1, kTestKey2);
}
SM_TEST(PcSrtpTransport, SendAndRecvPacketWithHeaderExtension_AES_CM_128_HMAC_SHA1_32) {
  Fixture f;
  f.TestSendRecvEncryptedHeaderExtension(tls::kSrtpAes128CmSha1_32, kTestKey1, kTestKey2);
}
SM_TEST(PcSrtpTransport, SendAndRecvPacket_kSrtpAeadAes128Gcm) {
  Fixture f;
  f.TestSendRecvPacket(tls::kSrtpAeadAes128Gcm, kTestKeyGcm128_1, kTestKeyGcm128_2);
}
SM_TEST(PcSrtpTransport, SendAndRecvPacketWithHeaderExtension_kSrtpAeadAes128Gcm) {
  Fixture f;
  f.TestSendRecvEncryptedHeaderExtension(tls::kSrtpAeadAes128Gcm, kTestKeyGcm128_1, kTestKeyGcm128_2);
}
SM_TEST(PcSrtpTransport, SendAndRecvPacket_kSrtpAeadAes256Gcm) {
  Fixture f;
  f.TestSendRecvPacket(tls::kSrtpAeadAes256Gcm, kTestKeyGcm256_1, kTestKeyGcm256_2);
}
SM_TEST(PcSrtpTransport, SendAndRecvPacketWithHeaderExtension_kSrtpAeadAes256Gcm) {
  Fixture f;
  f.TestSendRecvEncryptedHeaderExtension(tls::kSrtpAeadAes256Gcm, kTestKeyGcm256_1, kTestKeyGcm256_2);
}

// Directly setting the params with bogus keys.
SM_TEST(PcSrtpTransport, TestSetParamsKeyTooShort) {
  Fixture f;
  const std::vector<int> ids;
  const std::vector<uint8_t> short_key(kTestKey1.begin(), kTestKey1.end() - 1);
  SM_EXPECT_FALSE(
      f.srtp_transport1->SetRtpParams(tls::kSrtpAes128CmSha1_80, short_key, ids, tls::kSrtpAes128CmSha1_80, short_key,
                                      ids));
  SM_EXPECT_FALSE(f.srtp_transport1->SetRtcpParams(tls::kSrtpAes128CmSha1_80, short_key, ids,
                                                   tls::kSrtpAes128CmSha1_80, short_key, ids));
  SM_EXPECT_FALSE(f.srtp_transport1->IsSrtpActive());
}

SM_TEST(PcSrtpTransport, RemoveSrtpReceiveStream) {
  // With WebRTC-SrtpRemoveReceiveStream (off by default in M157).
  // The packet transport outlives the SRTP transport that uses it.
  auto rtp_packet_transport = std::make_unique<FakePacketTransport>("fake_packet_transport_loopback");
  auto srtp_transport = std::make_unique<SrtpTransport>(/*rtcp_mux_enabled=*/true,
                                                        SrtpTransportOptions{.remove_receive_stream = true});
  rtp_packet_transport->SetDestination(rtp_packet_transport.get());
  srtp_transport->SetRtpPacketTransport(rtp_packet_transport.get());
  TransportObserver rtp_sink;
  const std::vector<int> ids;
  SM_EXPECT_TRUE(srtp_transport->SetRtpParams(tls::kSrtpAeadAes128Gcm, kTestKeyGcm128_1, ids,
                                              tls::kSrtpAeadAes128Gcm, kTestKeyGcm128_1, ids));
  RtpDemuxerCriteria criteria;
  criteria.ssrcs = {1};  // The SSRC of PcmuFrame.
  SM_EXPECT_TRUE(srtp_transport->RegisterRtpDemuxerSink(criteria, &rtp_sink));
  const Timestamp now = rtp_packet_transport->now;

  // The first attempt succeeds.
  std::vector<uint8_t> first_try = PcmuFrame();
  SM_EXPECT_TRUE(srtp_transport->SendRtpPacket(first_try, p2p::PacketOptions{}, p2p::kPfSrtpBypass, now));
  SM_EXPECT_EQ(rtp_sink.rtp_count(), 1);
  // libsrtp rejects the second as a replay (srtp_err_status_replay_fail): the sequence number was seen.
  std::vector<uint8_t> second_try = PcmuFrame();
  SM_EXPECT_TRUE(srtp_transport->SendRtpPacket(second_try, p2p::PacketOptions{}, p2p::kPfSrtpBypass, now));
  SM_EXPECT_EQ(rtp_sink.rtp_count(), 1);
  SM_EXPECT_EQ(srtp_transport->rtp_decryption_failures(), 1);
  // Unregistering the sink removes SSRC 1 from the receive session.
  SM_EXPECT_TRUE(srtp_transport->UnregisterRtpDemuxerSink(&rtp_sink));
  SM_EXPECT_TRUE(srtp_transport->RegisterRtpDemuxerSink(criteria, &rtp_sink));
  // So the third succeeds again: libsrtp no longer remembers the sequence number.
  std::vector<uint8_t> third_try = PcmuFrame();
  SM_EXPECT_TRUE(srtp_transport->SendRtpPacket(third_try, p2p::PacketOptions{}, p2p::kPfSrtpBypass, now));
  SM_EXPECT_EQ(rtp_sink.rtp_count(), 2);
  srtp_transport->UnregisterRtpDemuxerSink(&rtp_sink);
}

// Without the field trial (M157's default), the receive session keeps the SSRC: the replay is still refused.
SM_TEST(PcSrtpTransport, ReceiveStreamKeptByDefault) {
  auto rtp_packet_transport = std::make_unique<FakePacketTransport>("loopback");
  auto srtp_transport = std::make_unique<SrtpTransport>(/*rtcp_mux_enabled=*/true);
  rtp_packet_transport->SetDestination(rtp_packet_transport.get());
  srtp_transport->SetRtpPacketTransport(rtp_packet_transport.get());
  TransportObserver rtp_sink;
  const std::vector<int> ids;
  SM_EXPECT_TRUE(srtp_transport->SetRtpParams(tls::kSrtpAeadAes128Gcm, kTestKeyGcm128_1, ids,
                                              tls::kSrtpAeadAes128Gcm, kTestKeyGcm128_1, ids));
  RtpDemuxerCriteria criteria;
  criteria.ssrcs = {1};
  SM_EXPECT_TRUE(srtp_transport->RegisterRtpDemuxerSink(criteria, &rtp_sink));
  const Timestamp now = rtp_packet_transport->now;
  for (int i = 0; i < 2; ++i) {
    std::vector<uint8_t> p = PcmuFrame();
    SM_EXPECT_TRUE(srtp_transport->SendRtpPacket(p, p2p::PacketOptions{}, p2p::kPfSrtpBypass, now));
    if (i == 0) {
      SM_EXPECT_TRUE(srtp_transport->UnregisterRtpDemuxerSink(&rtp_sink));
      SM_EXPECT_TRUE(srtp_transport->RegisterRtpDemuxerSink(criteria, &rtp_sink));
    }
  }
  SM_EXPECT_EQ(rtp_sink.rtp_count(), 1);
  srtp_transport->UnregisterRtpDemuxerSink(&rtp_sink);
}

// Inactive SRTP sends nothing and drops what it receives.
SM_TEST(PcSrtpTransport, InactiveSendsNothingAndDropsReceived) {
  Fixture f;
  SM_EXPECT_FALSE(f.srtp_transport1->IsSrtpActive());
  SM_EXPECT_FALSE(f.srtp_transport1->IsWritable(false));
  std::vector<uint8_t> p = PcmuFrame();
  SM_EXPECT_FALSE(f.srtp_transport1->SendRtpPacket(p, p2p::PacketOptions{}, p2p::kPfSrtpBypass, f.now()));
  std::vector<uint8_t> r = kFakeRtcpReport;
  SM_EXPECT_FALSE(f.srtp_transport1->SendRtcpPacket(r, p2p::PacketOptions{}, p2p::kPfSrtpBypass, f.now()));
  SM_EXPECT_EQ(f.rtp_packet_transport1->sent, 0);
  // Plain RTP arriving at an inactive SRTP transport is dropped, not demuxed.
  SM_EXPECT_EQ(f.rtp_packet_transport2->SendPacket(f.now(), PcmuFrame(), p2p::PacketOptions{}, 0),
               static_cast<int>(PcmuFrame().size()));
  SM_EXPECT_EQ(f.rtp_sink1.rtp_count(), 0);
  int overhead = 0;
  SM_EXPECT_FALSE(f.srtp_transport1->GetSrtpOverhead(&overhead));
}

// Writable follows SRTP activation and reset; the overhead is the RTP auth tag.
SM_TEST(PcSrtpTransport, WritableAndOverheadFollowActivation) {
  Fixture f;
  std::vector<bool> states;
  f.srtp_transport1->SubscribeWritableState(&states, [&](bool w) { states.push_back(w); });
  const std::vector<int> ids;
  SM_EXPECT_TRUE(f.srtp_transport1->SetRtpParams(tls::kSrtpAes128CmSha1_32, kTestKey1, ids,
                                                 tls::kSrtpAes128CmSha1_32, kTestKey2, ids));
  SM_EXPECT_TRUE(f.srtp_transport1->IsWritable(false) && f.srtp_transport1->IsWritable(true));
  int overhead = 0;
  SM_EXPECT_TRUE(f.srtp_transport1->GetSrtpOverhead(&overhead));
  SM_EXPECT_EQ(overhead, 4);
  f.srtp_transport1->ResetParams();
  SM_EXPECT_FALSE(f.srtp_transport1->IsSrtpActive());
  SM_ASSERT_EQ(states.size(), size_t{2});
  SM_EXPECT_TRUE(states[0]);
  SM_EXPECT_FALSE(states[1]);
}

// Re-keying (a DTLS restart): SetRtpParams on active sessions updates their keys, and media flows on the new keys.
SM_TEST(PcSrtpTransport, RekeyActiveSessions) {
  Fixture f;
  f.TestSendRecvPacket(tls::kSrtpAes128CmSha1_80, kTestKey1, kTestKey2);
  const std::vector<int> ids;
  SM_EXPECT_TRUE(f.srtp_transport1->SetRtpParams(tls::kSrtpAes128CmSha1_80, kTestKey2, ids,
                                                 tls::kSrtpAes128CmSha1_80, kTestKey1, ids));
  // Only one side re-keyed: the other can't decrypt it.
  std::vector<uint8_t> p = PcmuFrame();
  SetSeq(p, ++f.sequence_number);
  const int before = f.rtp_sink2.rtp_count();
  SM_EXPECT_TRUE(f.srtp_transport1->SendRtpPacket(p, p2p::PacketOptions{}, p2p::kPfSrtpBypass, f.now()));
  SM_EXPECT_EQ(f.rtp_sink2.rtp_count(), before);
  SM_EXPECT_EQ(f.srtp_transport2->rtp_decryption_failures(), 1);
  SM_EXPECT_TRUE(f.srtp_transport2->SetRtpParams(tls::kSrtpAes128CmSha1_80, kTestKey1, ids,
                                                 tls::kSrtpAes128CmSha1_80, kTestKey2, ids));
  f.TestSendRecvRtpPacket(tls::kSrtpAes128CmSha1_80);
}

}  // namespace
