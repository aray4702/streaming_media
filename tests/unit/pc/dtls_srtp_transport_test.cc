// DtlsSrtpTransport (#1007): libwebrtc M157's pc/dtls_srtp_transport_unittest.cc, case for case, with the library's
// real DTLS transport (#1006) over the fake ICE transport on a simulated clock in place of libwebrtc's
// FakeDtlsTransport, plus a media path: RTP and RTCP both ways over DTLS-SRTP through a 10 ms link, and a DTLS
// restart (new DTLS transports, certificates and fingerprints) that re-keys SRTP.
#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../p2p_dtls/fake_ice_transport.h"
#include "pc_test_util.h"
#include "sm/pc/dtls_srtp_transport.h"
#include "sm/tls/message_digest.h"
#include "sm/tls/ssl_fingerprint.h"
#include "sm/tls/ssl_identity.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::pc;
using namespace sm::pc::test;
using sm::p2p::test::FakeIceTransport;
using sm::p2p::test::FakeNetwork;

namespace {

constexpr int kRtpAuthTagLen = 10;
constexpr int kComponentRtp = 1;
constexpr int kComponentRtcp = 2;

// libwebrtc's FakeDtlsTransport over FakeIceTransportInternal: here a real DTLS transport over a fake ICE one.
struct Dtls {
  std::unique_ptr<FakeIceTransport> ice;
  std::unique_ptr<p2p::DtlsTransport> dtls;
  Dtls(FakeNetwork* net, const std::string& name, int component)
      : ice(std::make_unique<FakeIceTransport>(name, component, net)),
        dtls(std::make_unique<p2p::DtlsTransport>(ice.get())) {}
  p2p::DtlsTransport* get() { return dtls.get(); }
};

struct Fixture {
  FakeNetwork net;
  // The DTLS transports outlive the DTLS-SRTP transports that use them.
  std::vector<std::unique_ptr<Dtls>> owned;
  std::unique_ptr<DtlsSrtpTransport> dtls_srtp_transport1, dtls_srtp_transport2;
  TransportObserver transport_observer1, transport_observer2;
  uint16_t sequence_number = 0;

  Fixture() { net.delay = TimeDelta::Zero(); }
  ~Fixture() {
    if (dtls_srtp_transport1) dtls_srtp_transport1->UnregisterRtpDemuxerSink(&transport_observer1);
    if (dtls_srtp_transport2) dtls_srtp_transport2->UnregisterRtpDemuxerSink(&transport_observer2);
  }
  Dtls* CreateFakeDtlsTransport(const std::string& name, int component) {
    owned.push_back(std::make_unique<Dtls>(&net, name, component));
    return owned.back().get();
  }
  std::unique_ptr<DtlsSrtpTransport> MakeDtlsSrtpTransport(Dtls* rtp, Dtls* rtcp, bool rtcp_mux_enabled) {
    auto t = std::make_unique<DtlsSrtpTransport>(rtcp_mux_enabled);
    t->SetDtlsTransports(rtp ? rtp->get() : nullptr, rtcp ? rtcp->get() : nullptr);
    return t;
  }
  void MakeDtlsSrtpTransports(Dtls* rtp1, Dtls* rtcp1, Dtls* rtp2, Dtls* rtcp2, bool rtcp_mux_enabled) {
    dtls_srtp_transport1 = MakeDtlsSrtpTransport(rtp1, rtcp1, rtcp_mux_enabled);
    dtls_srtp_transport2 = MakeDtlsSrtpTransport(rtp2, rtcp2, rtcp_mux_enabled);
    transport_observer1.Attach(dtls_srtp_transport1.get());
    transport_observer2.Attach(dtls_srtp_transport2.get());
    // 0x00 is the payload type of PcmuFrame.
    dtls_srtp_transport1->RegisterRtpDemuxerSink(PayloadTypeCriteria(0), &transport_observer1);
    dtls_srtp_transport2->RegisterRtpDemuxerSink(PayloadTypeCriteria(0), &transport_observer2);
  }

  void Step() {
    net.now = net.now + TimeDelta::Millis(1);
    net.DeliverDue();
    for (Dtls* d : live_) {
      if (auto due = d->dtls->NextTimeout(); due && *due <= net.now) d->dtls->OnTimeout(net.now);
    }
    net.DeliverDue();
  }
  bool WaitUntil(const std::function<bool()>& pred, int timeout_ms = 10000) {
    for (int i = 0; i < timeout_ms && !pred(); ++i) Step();
    return pred();
  }
  // Certificates, roles (1 server, 2 client) and fingerprints, then ICE connects (SetDestination) and the handshake
  // runs to completion.
  void CompleteDtlsHandshake(Dtls* d1, Dtls* d2, const std::string& name = "session1") {
    std::shared_ptr<const tls::SslIdentity> cert1 = tls::SslIdentity::Create(name + "a");
    std::shared_ptr<const tls::SslIdentity> cert2 = tls::SslIdentity::Create(name + "b");
    SM_EXPECT_TRUE(d1->dtls->SetLocalCertificate(cert1));
    SM_EXPECT_TRUE(d2->dtls->SetLocalCertificate(cert2));
    d1->dtls->SetDtlsRole(p2p::SslRole::kServer);
    d2->dtls->SetDtlsRole(p2p::SslRole::kClient);
    auto fp1 = tls::SslFingerprint::Create(tls::kDigestSha256, *cert1);
    auto fp2 = tls::SslFingerprint::Create(tls::kDigestSha256, *cert2);
    SM_EXPECT_TRUE(d1->dtls->SetRemoteParameters(fp2->algorithm, fp2->digest, std::nullopt, net.now));
    SM_EXPECT_TRUE(d2->dtls->SetRemoteParameters(fp1->algorithm, fp1->digest, std::nullopt, net.now));
    live_.push_back(d1);
    live_.push_back(d2);
    d1->ice->SetDestination(d2->ice.get());
    SM_EXPECT_TRUE(WaitUntil([&] {
      return d1->dtls->dtls_state() == p2p::DtlsTransportState::kConnected &&
             d2->dtls->dtls_state() == p2p::DtlsTransportState::kConnected && d1->dtls->writable() &&
             d2->dtls->writable();
    }));
  }
  void Forget(Dtls* d) { std::erase(live_, d); }

  void SendRecvRtpPackets() {
    SM_ASSERT_TRUE(dtls_srtp_transport1 && dtls_srtp_transport2);
    SM_ASSERT_TRUE(dtls_srtp_transport1->IsSrtpActive());
    SM_ASSERT_TRUE(dtls_srtp_transport2->IsSrtpActive());
    std::vector<uint8_t> original = PcmuFrame();
    // A sequence number is used once: libsrtp's replay protection would refuse it again.
    SetSeq(original, ++sequence_number);
    for (int dir = 0; dir < 2; ++dir) {
      DtlsSrtpTransport* from = dir == 0 ? dtls_srtp_transport1.get() : dtls_srtp_transport2.get();
      TransportObserver& to = dir == 0 ? transport_observer2 : transport_observer1;
      std::vector<uint8_t> packet = original;
      const int prev = to.rtp_count();
      SM_ASSERT_TRUE(from->SendRtpPacket(packet, p2p::PacketOptions{}, p2p::kPfSrtpBypass, net.now));
      SM_EXPECT_EQ(packet.size(), original.size() + kRtpAuthTagLen);
      SM_EXPECT_NE(0, std::memcmp(packet.data(), original.data(), original.size()));  // What went on the wire.
      net.DeliverDue();
      SM_EXPECT_EQ(to.rtp_count(), prev + 1);
      SM_EXPECT_TRUE(to.last_recv_rtp_packet() == original);
    }
  }
  void SendRecvRtcpPackets() {
    for (int dir = 0; dir < 2; ++dir) {
      DtlsSrtpTransport* from = dir == 0 ? dtls_srtp_transport1.get() : dtls_srtp_transport2.get();
      TransportObserver& to = dir == 0 ? transport_observer2 : transport_observer1;
      std::vector<uint8_t> packet = kFakeRtcpReport;
      const int prev = to.rtcp_count();
      SM_ASSERT_TRUE(from->SendRtcpPacket(packet, p2p::PacketOptions{}, p2p::kPfSrtpBypass, net.now));
      SM_EXPECT_EQ(packet.size(), kFakeRtcpReport.size() + 4 + kRtpAuthTagLen);
      net.DeliverDue();
      SM_EXPECT_EQ(to.rtcp_count(), prev + 1);
      SM_EXPECT_TRUE(to.last_recv_rtcp_packet() == kFakeRtcpReport);
    }
  }
  void SendRecvPackets() {
    SendRecvRtpPackets();
    SendRecvRtcpPackets();
  }

 private:
  std::vector<Dtls*> live_;
};

// With rtcp-mux, transports set after the RTP transport finished the handshake set up SRTP.
SM_TEST(PcDtlsSrtpTransport, SetTransportsAfterHandshakeCompleteWithRtcpMux) {
  Fixture f;
  auto rtp_dtls1 = f.CreateFakeDtlsTransport("video", kComponentRtp);
  auto rtp_dtls2 = f.CreateFakeDtlsTransport("video", kComponentRtp);
  f.MakeDtlsSrtpTransports(rtp_dtls1, nullptr, rtp_dtls2, nullptr, /*rtcp_mux_enabled=*/true);
  auto rtp_dtls3 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  auto rtp_dtls4 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  f.CompleteDtlsHandshake(rtp_dtls3, rtp_dtls4);
  f.dtls_srtp_transport1->SetDtlsTransports(rtp_dtls3->get(), nullptr);
  f.dtls_srtp_transport2->SetDtlsTransports(rtp_dtls4->get(), nullptr);
  f.SendRecvPackets();
}

// Without rtcp-mux, transports set after both RTP and RTCP transports finished the handshake set up SRTP.
SM_TEST(PcDtlsSrtpTransport, SetTransportsAfterHandshakeCompleteWithoutRtcpMux) {
  Fixture f;
  auto rtp_dtls1 = f.CreateFakeDtlsTransport("video", kComponentRtp);
  auto rtcp_dtls1 = f.CreateFakeDtlsTransport("video", kComponentRtcp);
  auto rtp_dtls2 = f.CreateFakeDtlsTransport("video", kComponentRtp);
  auto rtcp_dtls2 = f.CreateFakeDtlsTransport("video", kComponentRtcp);
  f.MakeDtlsSrtpTransports(rtp_dtls1, rtcp_dtls1, rtp_dtls2, rtcp_dtls2, false);
  auto rtp_dtls3 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  auto rtcp_dtls3 = f.CreateFakeDtlsTransport("audio", kComponentRtcp);
  auto rtp_dtls4 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  auto rtcp_dtls4 = f.CreateFakeDtlsTransport("audio", kComponentRtcp);
  f.CompleteDtlsHandshake(rtp_dtls3, rtp_dtls4);
  f.CompleteDtlsHandshake(rtcp_dtls3, rtcp_dtls4, "session2");
  f.dtls_srtp_transport1->SetDtlsTransports(rtp_dtls3->get(), rtcp_dtls3->get());
  f.dtls_srtp_transport2->SetDtlsTransports(rtp_dtls4->get(), rtcp_dtls4->get());
  f.SendRecvPackets();
}

// With rtcp-mux, SRTP is set up as soon as the RTP DTLS handshake finishes.
SM_TEST(PcDtlsSrtpTransport, SetTransportsBeforeHandshakeCompleteWithRtcpMux) {
  Fixture f;
  auto rtp_dtls1 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  auto rtcp_dtls1 = f.CreateFakeDtlsTransport("audio", kComponentRtcp);
  auto rtp_dtls2 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  auto rtcp_dtls2 = f.CreateFakeDtlsTransport("audio", kComponentRtcp);
  f.MakeDtlsSrtpTransports(rtp_dtls1, rtcp_dtls1, rtp_dtls2, rtcp_dtls2, false);
  f.dtls_srtp_transport1->SetRtcpMuxEnabled(true);
  f.dtls_srtp_transport2->SetRtcpMuxEnabled(true);
  f.CompleteDtlsHandshake(rtp_dtls1, rtp_dtls2);
  f.SendRecvPackets();
}

// Without rtcp-mux, SRTP is set up when both the RTP and RTCP DTLS handshakes finish.
SM_TEST(PcDtlsSrtpTransport, SetTransportsBeforeHandshakeCompleteWithoutRtcpMux) {
  Fixture f;
  auto rtp_dtls1 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  auto rtcp_dtls1 = f.CreateFakeDtlsTransport("audio", kComponentRtcp);
  auto rtp_dtls2 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  auto rtcp_dtls2 = f.CreateFakeDtlsTransport("audio", kComponentRtcp);
  f.MakeDtlsSrtpTransports(rtp_dtls1, rtcp_dtls1, rtp_dtls2, rtcp_dtls2, false);
  f.CompleteDtlsHandshake(rtp_dtls1, rtp_dtls2);
  SM_EXPECT_FALSE(f.dtls_srtp_transport1->IsSrtpActive());
  SM_EXPECT_FALSE(f.dtls_srtp_transport2->IsSrtpActive());
  f.CompleteDtlsHandshake(rtcp_dtls1, rtcp_dtls2, "session2");
  f.SendRecvPackets();
}

// Changing the DTLS transport resets the DTLS-SRTP context; it is set up again when the new handshake completes.
SM_TEST(PcDtlsSrtpTransport, DtlsSrtpResetAfterDtlsTransportChange) {
  Fixture f;
  auto rtp_dtls1 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  auto rtp_dtls2 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  f.MakeDtlsSrtpTransports(rtp_dtls1, nullptr, rtp_dtls2, nullptr, true);
  f.CompleteDtlsHandshake(rtp_dtls1, rtp_dtls2);
  SM_EXPECT_TRUE(f.dtls_srtp_transport1->IsSrtpActive());
  SM_EXPECT_TRUE(f.dtls_srtp_transport2->IsSrtpActive());
  auto rtp_dtls3 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  auto rtp_dtls4 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  // The previous context is reset.
  f.dtls_srtp_transport1->SetDtlsTransports(rtp_dtls3->get(), nullptr);
  f.dtls_srtp_transport2->SetDtlsTransports(rtp_dtls4->get(), nullptr);
  SM_EXPECT_FALSE(f.dtls_srtp_transport1->IsSrtpActive());
  SM_EXPECT_FALSE(f.dtls_srtp_transport2->IsSrtpActive());
  // Re-setup.
  f.CompleteDtlsHandshake(rtp_dtls3, rtp_dtls4, "session2");
  f.SendRecvPackets();
}

// Only the RTP handshake finished, then rtcp-mux is enabled: SRTP is set up.
SM_TEST(PcDtlsSrtpTransport, RtcpMuxEnabledAfterRtpTransportHandshakeComplete) {
  Fixture f;
  auto rtp_dtls1 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  auto rtcp_dtls1 = f.CreateFakeDtlsTransport("audio", kComponentRtcp);
  auto rtp_dtls2 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  auto rtcp_dtls2 = f.CreateFakeDtlsTransport("audio", kComponentRtcp);
  f.MakeDtlsSrtpTransports(rtp_dtls1, rtcp_dtls1, rtp_dtls2, rtcp_dtls2, false);
  f.CompleteDtlsHandshake(rtp_dtls1, rtp_dtls2);
  // Inactive: the RTCP handshake didn't complete.
  SM_EXPECT_FALSE(f.dtls_srtp_transport1->IsSrtpActive());
  SM_EXPECT_FALSE(f.dtls_srtp_transport2->IsSrtpActive());
  f.dtls_srtp_transport1->SetRtcpMuxEnabled(true);
  f.dtls_srtp_transport2->SetRtcpMuxEnabled(true);
  f.SendRecvPackets();
}

// Setting the encrypted header extension ids after the handshake re-keys the sessions with them at once; media
// still flows (the ids are the same on both ends).
SM_TEST(PcDtlsSrtpTransport, EncryptedHeaderExtensionIdUpdated) {
  Fixture f;
  auto rtp_dtls1 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  auto rtp_dtls2 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  f.MakeDtlsSrtpTransports(rtp_dtls1, nullptr, rtp_dtls2, nullptr, true);
  f.CompleteDtlsHandshake(rtp_dtls1, rtp_dtls2);
  const std::vector<int> encrypted = {kHeaderExtensionIDs[0], kHeaderExtensionIDs[1]};
  f.dtls_srtp_transport1->UpdateSendEncryptedHeaderExtensionIds(encrypted);
  f.dtls_srtp_transport1->UpdateRecvEncryptedHeaderExtensionIds(encrypted);
  f.dtls_srtp_transport2->UpdateSendEncryptedHeaderExtensionIds(encrypted);
  f.dtls_srtp_transport2->UpdateRecvEncryptedHeaderExtensionIds(encrypted);
  SM_EXPECT_TRUE(f.dtls_srtp_transport1->IsSrtpActive());
  // Ids 1 and 4 are encrypted on the wire, 2 and 3 are not, and the frame arrives whole.
  std::vector<uint8_t> original = PcmuFrameWithExtensions();
  SetSeq(original, 100);
  std::vector<uint8_t> packet = original;
  SM_ASSERT_TRUE(f.dtls_srtp_transport1->SendRtpPacket(packet, p2p::PacketOptions{}, p2p::kPfSrtpBypass, f.net.now));
  SM_EXPECT_TRUE(ExtensionValue(packet, 1) != ExtensionValue(original, 1));
  SM_EXPECT_TRUE(ExtensionValue(packet, 2) == ExtensionValue(original, 2));
  SM_EXPECT_TRUE(ExtensionValue(packet, 3) == ExtensionValue(original, 3));
  SM_EXPECT_TRUE(ExtensionValue(packet, 4) != ExtensionValue(original, 4));
  f.net.DeliverDue();
  SM_EXPECT_TRUE(f.transport_observer2.last_recv_rtp_packet() == original);
}

SM_TEST(PcDtlsSrtpTransport, WritableStateChangesAfterSrtpActivation) {
  Fixture f;
  auto rtp_dtls1 = f.CreateFakeDtlsTransport("video", kComponentRtp);
  auto rtp_dtls2 = f.CreateFakeDtlsTransport("video", kComponentRtp);
  f.MakeDtlsSrtpTransports(rtp_dtls1, nullptr, rtp_dtls2, nullptr, true);
  std::vector<bool> writable_states;
  f.dtls_srtp_transport1->SubscribeWritableState(&writable_states, [&](bool w) { writable_states.push_back(w); });
  f.CompleteDtlsHandshake(rtp_dtls1, rtp_dtls2);
  SM_EXPECT_TRUE(writable_states == std::vector<bool>({true}));
  rtp_dtls1->ice->SetWritable(false);
  SM_EXPECT_TRUE(f.dtls_srtp_transport1->IsSrtpActive());
  SM_EXPECT_FALSE(f.dtls_srtp_transport1->IsWritable(/*rtcp=*/false));
  SM_EXPECT_TRUE(writable_states == std::vector<bool>({true, false}));
  // A media channel attached during the outage sees the recovery edge.
  std::vector<bool> late_subscriber_states;
  f.dtls_srtp_transport1->SubscribeWritableState(&late_subscriber_states,
                                                 [&](bool w) { late_subscriber_states.push_back(w); });
  rtp_dtls1->ice->SetWritable(true);
  SM_EXPECT_TRUE(writable_states == std::vector<bool>({true, false, true}));
  SM_EXPECT_TRUE(late_subscriber_states == std::vector<bool>({true}));
  f.SendRecvPackets();
  f.dtls_srtp_transport1->UnsubscribeWritableState(&late_subscriber_states);
  f.dtls_srtp_transport1->UnsubscribeWritableState(&writable_states);
}

SM_TEST(PcDtlsSrtpTransport, WritableStateRequiresRtpAndRtcpRecovery) {
  Fixture f;
  auto rtp_dtls1 = f.CreateFakeDtlsTransport("video", kComponentRtp);
  auto rtp_dtls2 = f.CreateFakeDtlsTransport("video", kComponentRtp);
  auto rtcp_dtls1 = f.CreateFakeDtlsTransport("video", kComponentRtcp);
  auto rtcp_dtls2 = f.CreateFakeDtlsTransport("video", kComponentRtcp);
  f.MakeDtlsSrtpTransports(rtp_dtls1, rtcp_dtls1, rtp_dtls2, rtcp_dtls2, false);
  f.CompleteDtlsHandshake(rtp_dtls1, rtp_dtls2);
  f.CompleteDtlsHandshake(rtcp_dtls1, rtcp_dtls2, "session2");
  std::vector<bool> writable_states;
  f.dtls_srtp_transport1->SubscribeWritableState(&writable_states, [&](bool w) { writable_states.push_back(w); });
  rtp_dtls1->ice->SetWritable(false);
  rtcp_dtls1->ice->SetWritable(false);
  SM_EXPECT_TRUE(writable_states == std::vector<bool>({false}));
  rtp_dtls1->ice->SetWritable(true);
  SM_EXPECT_TRUE(writable_states == std::vector<bool>({false}));
  rtcp_dtls1->ice->SetWritable(true);
  SM_EXPECT_TRUE(writable_states == std::vector<bool>({false, true}));
  f.SendRecvPackets();
  f.dtls_srtp_transport1->UnsubscribeWritableState(&writable_states);
}

// With rtcp-mux, the transport is ready to send once the RTP DTLS transport is.
SM_TEST(PcDtlsSrtpTransport, SignalReadyToSendFiredWithRtcpMux) {
  Fixture f;
  auto rtp_dtls1 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  auto rtp_dtls2 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  f.MakeDtlsSrtpTransports(rtp_dtls1, nullptr, rtp_dtls2, nullptr, true);
  f.CompleteDtlsHandshake(rtp_dtls1, rtp_dtls2);
  SM_EXPECT_TRUE(f.transport_observer1.ready_to_send());
  SM_EXPECT_TRUE(f.transport_observer2.ready_to_send());
}

// Without rtcp-mux, it is ready to send once both the RTP and RTCP DTLS transports are.
SM_TEST(PcDtlsSrtpTransport, SignalReadyToSendFiredWithoutRtcpMux) {
  Fixture f;
  auto rtp_dtls1 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  auto rtcp_dtls1 = f.CreateFakeDtlsTransport("audio", kComponentRtcp);
  auto rtp_dtls2 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  auto rtcp_dtls2 = f.CreateFakeDtlsTransport("audio", kComponentRtcp);
  f.MakeDtlsSrtpTransports(rtp_dtls1, rtcp_dtls1, rtp_dtls2, rtcp_dtls2, false);
  f.CompleteDtlsHandshake(rtp_dtls1, rtp_dtls2);
  SM_EXPECT_FALSE(f.transport_observer1.ready_to_send());
  SM_EXPECT_FALSE(f.transport_observer2.ready_to_send());
  f.CompleteDtlsHandshake(rtcp_dtls1, rtcp_dtls2, "session2");
  SM_EXPECT_TRUE(f.transport_observer1.ready_to_send());
  SM_EXPECT_TRUE(f.transport_observer2.ready_to_send());
}

// Fully enabling rtcp-mux (the RTCP transport set to null) doesn't reset the SRTP context: resetting it would
// reset the rollover counter and SRTCP index, and the peer would refuse the next packets as replays
// (bugs.webrtc.org/8996).
SM_TEST(PcDtlsSrtpTransport, SrtpSessionNotResetWhenRtcpTransportRemoved) {
  Fixture f;
  auto rtp_dtls1 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  auto rtcp_dtls1 = f.CreateFakeDtlsTransport("audio", kComponentRtcp);
  auto rtp_dtls2 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  auto rtcp_dtls2 = f.CreateFakeDtlsTransport("audio", kComponentRtcp);
  f.MakeDtlsSrtpTransports(rtp_dtls1, rtcp_dtls1, rtp_dtls2, rtcp_dtls2, true);
  f.CompleteDtlsHandshake(rtp_dtls1, rtp_dtls2);
  f.CompleteDtlsHandshake(rtcp_dtls1, rtcp_dtls2, "session2");
  // Some RTCP packets move the SRTCP index on.
  f.SendRecvRtcpPackets();
  f.dtls_srtp_transport1->SetDtlsTransports(rtp_dtls1->get(), nullptr);
  SM_EXPECT_TRUE(f.dtls_srtp_transport1->IsSrtpActive());
  // A reset context on one side would make the other refuse a repeated SRTCP index.
  f.SendRecvRtcpPackets();
  SM_EXPECT_EQ(f.dtls_srtp_transport1->rtcp_decryption_failures(), 0);
  SM_EXPECT_EQ(f.dtls_srtp_transport2->rtcp_decryption_failures(), 0);
}

// The keys are the RFC 5764 halves: what the server sends with, the client receives with. With the roles the other
// way round, SRTP still flows.
SM_TEST(PcDtlsSrtpTransport, KeysFollowTheDtlsRole) {
  Fixture f;
  auto rtp_dtls1 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  auto rtp_dtls2 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  f.MakeDtlsSrtpTransports(rtp_dtls1, nullptr, rtp_dtls2, nullptr, true);
  f.CompleteDtlsHandshake(rtp_dtls2, rtp_dtls1);  // Transport 2 is the DTLS server.
  p2p::SslRole role1, role2;
  SM_EXPECT_TRUE(rtp_dtls1->dtls->GetDtlsRole(&role1) && rtp_dtls2->dtls->GetDtlsRole(&role2));
  SM_EXPECT_TRUE(role1 == p2p::SslRole::kClient && role2 == p2p::SslRole::kServer);
  // AES-CM-128-SHA1-80 is libwebrtc's preferred suite of the default list.
  int suite = 0;
  SM_EXPECT_TRUE(rtp_dtls1->dtls->GetSrtpCryptoSuite(&suite));
  SM_EXPECT_EQ(suite, tls::kSrtpAes128CmSha1_80);
  f.SendRecvPackets();
}

// A DTLS failure (a fingerprint that doesn't match) leaves SRTP inactive: nothing is sent unprotected.
SM_TEST(PcDtlsSrtpTransport, NoSrtpWithoutVerifiedDtls) {
  Fixture f;
  auto rtp_dtls1 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  auto rtp_dtls2 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  f.MakeDtlsSrtpTransports(rtp_dtls1, nullptr, rtp_dtls2, nullptr, true);
  std::shared_ptr<const tls::SslIdentity> cert1 = tls::SslIdentity::Create("a");
  std::shared_ptr<const tls::SslIdentity> cert2 = tls::SslIdentity::Create("b");
  SM_EXPECT_TRUE(rtp_dtls1->dtls->SetLocalCertificate(cert1));
  SM_EXPECT_TRUE(rtp_dtls2->dtls->SetLocalCertificate(cert2));
  rtp_dtls1->dtls->SetDtlsRole(p2p::SslRole::kServer);
  rtp_dtls2->dtls->SetDtlsRole(p2p::SslRole::kClient);
  auto fp1 = tls::SslFingerprint::Create(tls::kDigestSha256, *cert1);
  auto fp2 = tls::SslFingerprint::Create(tls::kDigestSha256, *cert2);
  ++fp2->digest[0];  // Transport 1 expects another certificate.
  SM_EXPECT_TRUE(rtp_dtls1->dtls->SetRemoteParameters(fp2->algorithm, fp2->digest, std::nullopt, f.net.now));
  SM_EXPECT_TRUE(rtp_dtls2->dtls->SetRemoteParameters(fp1->algorithm, fp1->digest, std::nullopt, f.net.now));
  rtp_dtls1->ice->SetDestination(rtp_dtls2->ice.get());
  for (int i = 0; i < 3000; ++i) f.Step();
  SM_EXPECT_TRUE(rtp_dtls1->dtls->dtls_state() == p2p::DtlsTransportState::kFailed);
  SM_EXPECT_FALSE(f.dtls_srtp_transport1->IsSrtpActive());
  std::vector<uint8_t> p = PcmuFrame();
  SM_EXPECT_FALSE(f.dtls_srtp_transport1->SendRtpPacket(p, p2p::PacketOptions{}, p2p::kPfSrtpBypass, f.net.now));
}

// C5's media path: RTP and RTCP both ways over DTLS-SRTP over (fake) ICE through a 10 ms link, every packet
// protected on the wire and delivered whole, in order, with sequence numbers past a 16-bit wrap (the rollover
// counter).
SM_TEST(PcDtlsSrtpTransport, MediaPathOverIce) {
  Fixture f;
  f.net.delay = TimeDelta::Millis(10);
  auto rtp_dtls1 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  auto rtp_dtls2 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  f.MakeDtlsSrtpTransports(rtp_dtls1, nullptr, rtp_dtls2, nullptr, true);
  f.CompleteDtlsHandshake(rtp_dtls1, rtp_dtls2);
  SM_ASSERT_TRUE(f.dtls_srtp_transport1->IsSrtpActive() && f.dtls_srtp_transport2->IsSrtpActive());
  std::vector<std::vector<uint8_t>> plain_on_wire;
  rtp_dtls1->ice->set_packet_send_filter([&](std::span<const uint8_t> d) {
    plain_on_wire.emplace_back(d.begin(), d.end());
    return false;
  });
  std::vector<uint16_t> received_seq;
  struct SeqSink : RtpPacketSinkInterface {
    std::vector<uint16_t>* out;
    void OnRtpPacket(const RtpPacketReceived& p) override { out->push_back(p.packet.seq); }
  } sink;
  sink.out = &received_seq;
  f.dtls_srtp_transport2->UnregisterRtpDemuxerSink(&f.transport_observer2);
  SM_EXPECT_TRUE(f.dtls_srtp_transport2->RegisterRtpDemuxerSink(PayloadTypeCriteria(0), &sink));
  constexpr int kPackets = 300;
  const uint16_t first_seq = 65400;
  for (int i = 0; i < kPackets; ++i) {
    std::vector<uint8_t> p = PcmuFrame();
    SetSeq(p, static_cast<uint16_t>(first_seq + i));
    p[160] = static_cast<uint8_t>(i);
    SM_EXPECT_TRUE(f.dtls_srtp_transport1->SendRtpPacket(p, p2p::PacketOptions{}, p2p::kPfSrtpBypass, f.net.now));
    if (i % 25 == 0) {
      std::vector<uint8_t> rtcp = kFakeRtcpReport;
      SM_EXPECT_TRUE(f.dtls_srtp_transport2->SendRtcpPacket(rtcp, p2p::PacketOptions{}, p2p::kPfSrtpBypass, f.net.now));
    }
    for (int ms = 0; ms < 20; ++ms) f.Step();
  }
  for (int ms = 0; ms < 50; ++ms) f.Step();
  SM_ASSERT_EQ(received_seq.size(), static_cast<size_t>(kPackets));
  for (int i = 0; i < kPackets; ++i) SM_EXPECT_EQ(received_seq[static_cast<size_t>(i)], static_cast<uint16_t>(first_seq + i));
  SM_EXPECT_EQ(f.transport_observer1.rtcp_count(), kPackets / 25);
  // Nothing on the wire carried the plain payload.
  SM_ASSERT_EQ(plain_on_wire.size(), static_cast<size_t>(kPackets));
  const std::vector<uint8_t> plain = PcmuFrame();
  for (const auto& w : plain_on_wire) {
    SM_EXPECT_EQ(w.size(), plain.size() + kRtpAuthTagLen);
    SM_EXPECT_NE(0, std::memcmp(w.data() + 12, plain.data() + 12, 64));
  }
  SM_EXPECT_EQ(f.dtls_srtp_transport2->rtp_decryption_failures(), 0);
  f.dtls_srtp_transport2->UnregisterRtpDemuxerSink(&sink);
}

// A DTLS restart (new DTLS transports with new certificates and fingerprints on both ends) resets SRTP while the new handshake runs and re-keys it when it connects; media flows on the new keys.
SM_TEST(PcDtlsSrtpTransport, RekeyAfterDtlsRestart) {
  Fixture f;
  auto rtp_dtls1 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  auto rtp_dtls2 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  f.MakeDtlsSrtpTransports(rtp_dtls1, nullptr, rtp_dtls2, nullptr, true);
  f.CompleteDtlsHandshake(rtp_dtls1, rtp_dtls2);
  f.SendRecvPackets();
  std::vector<uint8_t> km_before;
  SM_EXPECT_TRUE(rtp_dtls1->dtls->AppendSrtpKeyingMaterial(km_before));
  int state_changes = 0;
  f.dtls_srtp_transport1->SetOnDtlsStateChange([&] { ++state_changes; });

  // The restart replaces the DTLS transports (JsepTransport makes new ones for new DTLS parameters).
  Dtls* d3 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  Dtls* d4 = f.CreateFakeDtlsTransport("audio", kComponentRtp);
  f.dtls_srtp_transport1->SetDtlsTransports(d3->get(), nullptr);
  f.dtls_srtp_transport2->SetDtlsTransports(d4->get(), nullptr);
  SM_EXPECT_FALSE(f.dtls_srtp_transport1->IsSrtpActive());
  f.Forget(rtp_dtls1);
  f.Forget(rtp_dtls2);
  f.CompleteDtlsHandshake(d3, d4, "restart");
  SM_EXPECT_GT(state_changes, 0);
  std::vector<uint8_t> km_after;
  SM_EXPECT_TRUE(d3->dtls->AppendSrtpKeyingMaterial(km_after));
  SM_EXPECT_TRUE(km_before != km_after);
  f.SendRecvPackets();
  SM_EXPECT_EQ(f.dtls_srtp_transport1->rtp_decryption_failures(), 0);
  SM_EXPECT_EQ(f.dtls_srtp_transport2->rtp_decryption_failures(), 0);
}

}  // namespace
