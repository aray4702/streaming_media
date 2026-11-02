// The DTLS transport over a fake ICE transport (#1006): the cases of libwebrtc's p2p/dtls/dtls_transport_unittest.cc
// (M157) that apply without DTLS-in-STUN, on a simulated clock, with DTLS 1.2 and 1.3, plus loss and reordering,
// and JsepTransport::NegotiateDtlsRole's a=setup table.
#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "fake_ice_transport.h"
#include "sm/p2p/dtls_transport.h"
#include "sm/tls/message_digest.h"
#include "sm/tls/ssl_fingerprint.h"
#include "sm/tls/ssl_identity.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::p2p;
using sm::p2p::test::FakeIceTransport;
using sm::p2p::test::FakeNetwork;

namespace {

constexpr size_t kPacketNumOffset = 8;
constexpr size_t kPacketHeaderLen = 12;
constexpr uint8_t kRtpLeadByte = 0x80;
constexpr int64_t kFakePacketId = 0x1234;

struct Client {
  std::string name;
  FakeNetwork* net;
  int max_version = tls::kDtls12;
  std::shared_ptr<const tls::SslIdentity> cert;
  std::unique_ptr<FakeIceTransport> ice;
  std::unique_ptr<DtlsTransport> dtls;
  size_t packet_size = 0;
  std::vector<uint32_t> received;  // Packet numbers of the expected packets received.
  std::vector<ReceivedPacket::DecryptionInfo> infos;
  SentPacketInfo last_sent;
  std::vector<DtlsTransportState> states;
  int closes = 0;

  Client(std::string n, FakeNetwork* network) : name(std::move(n)), net(network) {}
  void CreateCertificate() { cert = tls::SslIdentity::Create(name); }
  void SetupTransports(std::optional<int> rtt_ms = std::nullopt) {
    dtls.reset();
    ice = std::make_unique<FakeIceTransport>(name, 1, net);
    ice->set_rtt_estimate(rtt_ms);
    dtls = std::make_unique<DtlsTransport>(ice.get(), tls::DtlsConfig{}.srtp_crypto_suites, max_version);
    if (cert) SM_EXPECT_TRUE(dtls->SetLocalCertificate(cert));
    dtls->RegisterReceivedPacketCallback(this, [this](PacketTransportInternal*, const ReceivedPacket& p) {
      infos.push_back(p.decryption_info);
      if (p.payload.size() != packet_size || (p.payload[0] != 0 && p.payload[0] != kRtpLeadByte)) return;
      const uint32_t num = (uint32_t{p.payload[kPacketNumOffset]} << 24) |
                           (uint32_t{p.payload[kPacketNumOffset + 1]} << 16) |
                           (uint32_t{p.payload[kPacketNumOffset + 2]} << 8) | p.payload[kPacketNumOffset + 3];
      for (size_t i = kPacketHeaderLen; i < p.payload.size(); ++i) {
        if (p.payload[i] != (num & 0xff)) return;
      }
      received.push_back(num);
    });
    dtls->SubscribeSentPacket(this, [this](PacketTransportInternal*, const SentPacketInfo& i) { last_sent = i; });
    dtls->SubscribeDtlsTransportState(this, [this](DtlsTransport*, DtlsTransportState s) { states.push_back(s); });
    dtls->SetOnCloseCallback([this] { ++closes; });
  }
  static std::vector<uint8_t> MakePacket(size_t size, uint32_t num, bool srtp) {
    std::vector<uint8_t> p(size, static_cast<uint8_t>(num & 0xff));
    p[0] = srtp ? kRtpLeadByte : 0x00;  // Not DTLS either way.
    for (int i = 0; i < 4; ++i) p[kPacketNumOffset + i] = static_cast<uint8_t>(num >> (24 - 8 * i));
    return p;
  }
  int SendPacket(size_t size, uint32_t num, bool srtp, const PacketOptions& options) {
    const int flags = (cert && srtp) ? kPfSrtpBypass : kPfNormal;  // The bypass flag only with DTLS active.
    return dtls->SendPacket(net->now, MakePacket(size, num, srtp), options, flags);
  }
};

struct Harness {
  FakeNetwork net;
  Client c1{"P1", &net};
  Client c2{"P2", &net};
  bool use_dtls = false;

  explicit Harness(int version = tls::kDtls12) { c1.max_version = c2.max_version = version; }

  void PrepareDtls() {
    c1.CreateCertificate();
    c2.CreateCertificate();
    use_dtls = true;
  }
  // One millisecond: deliver what is due, then fire the handshake timers.
  void Step() {
    net.now = net.now + TimeDelta::Millis(1);
    net.DeliverDue();
    for (Client* c : {&c1, &c2}) {
      if (!c->dtls) continue;
      if (auto due = c->dtls->NextTimeout(); due && *due <= net.now) c->dtls->OnTimeout(net.now);
    }
    net.DeliverDue();
  }
  void Advance(int ms) {
    for (int i = 0; i < ms; ++i) Step();
  }
  bool WaitUntil(const std::function<bool()>& pred, int timeout_ms = 10000) {
    for (int i = 0; i < timeout_ms && !pred(); ++i) Step();
    return pred();
  }
  void SetRemoteFingerprintFromCert(DtlsTransport* dtls, const tls::SslIdentity& cert, bool modify = false) {
    auto fp = tls::SslFingerprint::Create(tls::kDigestSha256, cert);
    if (modify) ++fp->digest[0];
    SM_EXPECT_TRUE(dtls->SetRemoteParameters(fp->algorithm, fp->digest, std::nullopt, net.now));
  }
  void Negotiate(bool c1_server = true) {
    c1.SetupTransports();
    c2.SetupTransports();
    c1.dtls->SetDtlsRole(c1_server ? SslRole::kServer : SslRole::kClient);
    c2.dtls->SetDtlsRole(c1_server ? SslRole::kClient : SslRole::kServer);
    if (c2.cert) SetRemoteFingerprintFromCert(c1.dtls.get(), *c2.cert);
    if (c1.cert) SetRemoteFingerprintFromCert(c2.dtls.get(), *c1.cert);
  }
  // Negotiates before ICE is writable, then connects ICE (as the libwebrtc test's Connect).
  bool Connect(bool c1_server = true) {
    Negotiate(c1_server);
    c1.ice->SetDestination(c2.ice.get());
    if (!WaitUntil([&] { return c1.dtls->writable() && c2.dtls->writable(); })) return false;
    if (use_dtls) {
      SslRole r1, r2;
      SM_EXPECT_TRUE(c1.dtls->GetDtlsRole(&r1) && c2.dtls->GetDtlsRole(&r2));
      SM_EXPECT_TRUE(r1 == (c1_server ? SslRole::kServer : SslRole::kClient));
      SM_EXPECT_TRUE(r2 == (c1_server ? SslRole::kClient : SslRole::kServer));
      // GCM is offered but not preferred: AES-CM-128-SHA1-80.
      int s1 = 0, s2 = 0;
      SM_EXPECT_TRUE(c1.dtls->GetSrtpCryptoSuite(&s1) && c2.dtls->GetSrtpCryptoSuite(&s2));
      SM_EXPECT_EQ(s1, tls::kSrtpAes128CmSha1_80);
      SM_EXPECT_EQ(s2, tls::kSrtpAes128CmSha1_80);
      int v1 = 0, v2 = 0;
      SM_EXPECT_TRUE(c1.dtls->GetSslVersionBytes(&v1) && c2.dtls->GetSslVersionBytes(&v2));
      SM_EXPECT_EQ(v1, c1.max_version);
      SM_EXPECT_EQ(v2, c2.max_version);
    } else {
      int s = 0;
      SM_EXPECT_FALSE(c1.dtls->GetSrtpCryptoSuite(&s));
      SM_EXPECT_FALSE(c2.dtls->GetSrtpCryptoSuite(&s));
    }
    return true;
  }
  // c1 sends `count` packets of `size` to c2; all must arrive intact.
  bool TestTransfer(size_t size, uint32_t count, bool srtp) {
    c2.packet_size = size;
    c2.received.clear();
    PacketOptions options;
    options.packet_id = kFakePacketId;
    for (uint32_t i = 0; i < count; ++i) {
      if (c1.SendPacket(size, i, srtp, options) != static_cast<int>(size)) return false;
    }
    return WaitUntil([&] { return c2.received.size() == count; });
  }
};

}  // namespace

// Without DTLS (no certificates) the transport passes RTP through.
SM_TEST(P2pDtlsTransport, TransferRtpWithoutDtls) {
  Harness h;
  SM_ASSERT_TRUE(h.Connect());
  SM_EXPECT_FALSE(h.c1.dtls->IsDtlsActive());
  SM_EXPECT_TRUE(h.TestTransfer(1000, 100, false));
  SM_EXPECT_TRUE(h.TestTransfer(1000, 100, true));
  SM_EXPECT_TRUE(h.c2.infos.back() == ReceivedPacket::kNotDecrypted);
}

// SignalSentPacket is wired up and packet options reach ICE, through DTLS and on the SRTP bypass.
SM_TEST(P2pDtlsTransport, SentPacketAndOptions) {
  for (bool srtp : {false, true}) {
    Harness h;
    h.PrepareDtls();
    SM_ASSERT_TRUE(h.Connect());
    h.c1.last_sent = SentPacketInfo{};
    SM_EXPECT_EQ(h.c1.last_sent.packet_id, -1);
    SM_EXPECT_TRUE(h.TestTransfer(1000, 1, srtp));
    SM_EXPECT_EQ(h.c1.last_sent.packet_id, kFakePacketId);
    SM_EXPECT_TRUE(h.c1.last_sent.send_time == h.net.now - h.net.delay);
  }
}

// DTLS-encrypted data, SRTP bypass, both on one transport, and the answerer as the server.
SM_TEST(P2pDtlsTransport, TransferDtlsAndSrtp) {
  for (int version : {tls::kDtls12, tls::kDtls13}) {
    for (bool c1_server : {true, false}) {
      Harness h(version);
      h.PrepareDtls();
      SM_ASSERT_TRUE(h.Connect(c1_server));
      // What goes on the wire for DTLS data is a DTLS application data record, not the packet.
      std::vector<std::vector<uint8_t>> wire;
      h.c1.ice->set_packet_send_filter([&](std::span<const uint8_t> d) {
        wire.emplace_back(d.begin(), d.end());
        return false;
      });
      SM_EXPECT_TRUE(h.TestTransfer(1000, 100, false));
      SM_EXPECT_TRUE(h.c2.infos.back() == ReceivedPacket::kDtlsDecrypted);
      SM_ASSERT_EQ(wire.size(), 100u);
      SM_EXPECT_TRUE(IsDtlsPacket(wire[0]));
      SM_EXPECT_GT(wire[0].size(), 1000u);
      wire.clear();
      SM_EXPECT_TRUE(h.TestTransfer(1000, 100, true));
      SM_EXPECT_TRUE(h.c2.infos.back() == ReceivedPacket::kSrtpEncrypted);
      SM_ASSERT_EQ(wire.size(), 100u);
      SM_EXPECT_TRUE(wire[0] == Client::MakePacket(1000, 0, true));  // Untouched.
    }
  }
}

// An "SRTP" packet that isn't RTP is refused (TestTransferDtlsInvalidSrtpPacket); nothing is sent before DTLS
// connects.
SM_TEST(P2pDtlsTransport, RefusesInvalidSrtpAndEarlySends) {
  Harness h;
  h.PrepareDtls();
  h.Negotiate();
  SM_EXPECT_EQ(h.c1.SendPacket(100, 0, false, {}), -1);  // kNew.
  h.c1.ice->SetDestination(h.c2.ice.get());
  SM_EXPECT_TRUE(h.c1.dtls->dtls_state() == DtlsTransportState::kConnecting);
  SM_EXPECT_EQ(h.c1.SendPacket(100, 0, false, {}), -1);  // kConnecting.
  SM_ASSERT_TRUE(h.WaitUntil([&] { return h.c1.dtls->writable() && h.c2.dtls->writable(); }));
  SM_EXPECT_EQ(h.c1.dtls->SendPacket(h.net.now, std::vector<uint8_t>(100, 0), {}, kPfSrtpBypass), -1);
}

// Several DTLS records in one packet (TestTransferDtlsCombineRecords).
SM_TEST(P2pDtlsTransport, CombinedRecords) {
  Harness h;
  h.PrepareDtls();
  SM_ASSERT_TRUE(h.Connect());
  h.c1.ice->combine_outgoing_packets(true);
  SM_EXPECT_TRUE(h.TestTransfer(500, 100, false));
}

// Both ends export the same RFC 5764 keying material (KeyingMaterialExporter).
SM_TEST(P2pDtlsTransport, KeyingMaterialExporter) {
  for (int version : {tls::kDtls12, tls::kDtls13}) {
    Harness h(version);
    h.PrepareDtls();
    SM_ASSERT_TRUE(h.Connect());
    std::vector<uint8_t> k1 = {0xaa}, k2;
    SM_ASSERT_TRUE(h.c1.dtls->AppendSrtpKeyingMaterial(k1));
    SM_ASSERT_TRUE(h.c2.dtls->AppendSrtpKeyingMaterial(k2));
    SM_EXPECT_EQ(k1.size(), 1u + 2 * (16 + 14));  // Appended after what was there.
    SM_EXPECT_EQ(k1[0], 0xaa);
    SM_EXPECT_TRUE(std::equal(k1.begin() + 1, k1.end(), k2.begin(), k2.end()));
    SM_EXPECT_TRUE(std::any_of(k2.begin(), k2.end(), [](uint8_t b) { return b != 0; }));
  }
}

// Setting the same role and fingerprint again before connecting is harmless (TestRenegotiateBeforeConnect).
SM_TEST(P2pDtlsTransport, RenegotiateBeforeConnect) {
  Harness h;
  h.PrepareDtls();
  h.Negotiate();
  h.c1.dtls->SetDtlsRole(SslRole::kServer);
  h.c2.dtls->SetDtlsRole(SslRole::kClient);
  h.SetRemoteFingerprintFromCert(h.c1.dtls.get(), *h.c2.cert);
  h.SetRemoteFingerprintFromCert(h.c2.dtls.get(), *h.c1.cert);
  h.c1.ice->SetDestination(h.c2.ice.get());
  SM_ASSERT_TRUE(h.WaitUntil([&] { return h.c1.dtls->writable() && h.c2.dtls->writable(); }));
  SM_EXPECT_TRUE(h.TestTransfer(1000, 100, true));
}

// Certificates before and after connecting (TestCertificatesBeforeConnect, TestCertificatesAfterConnect).
SM_TEST(P2pDtlsTransport, Certificates) {
  Harness h;
  h.PrepareDtls();
  h.Negotiate();
  SM_EXPECT_TRUE(h.c1.dtls->local_certificate()->certificate().ToPem() !=
                 h.c2.dtls->local_certificate()->certificate().ToPem());
  SM_EXPECT_TRUE(h.c1.dtls->GetRemoteCertificate() == nullptr);
  SM_EXPECT_TRUE(h.c2.dtls->GetRemoteCertificate() == nullptr);
  h.c1.ice->SetDestination(h.c2.ice.get());
  SM_ASSERT_TRUE(h.WaitUntil([&] { return h.c1.dtls->writable() && h.c2.dtls->writable(); }));
  SM_ASSERT_TRUE(h.c1.dtls->GetRemoteCertificate() != nullptr);
  SM_ASSERT_TRUE(h.c2.dtls->GetRemoteCertificate() != nullptr);
  SM_EXPECT_TRUE(*h.c1.dtls->GetRemoteCertificate() == h.c2.cert->certificate());
  SM_EXPECT_TRUE(*h.c2.dtls->GetRemoteCertificate() == h.c1.cert->certificate());
  // The local identity can't change once DTLS is active; the same one is accepted again.
  SM_EXPECT_TRUE(h.c1.dtls->SetLocalCertificate(h.c1.cert));
  SM_EXPECT_FALSE(h.c1.dtls->SetLocalCertificate(tls::SslIdentity::Create("other")));
}

// A ClientHello before any remote parameters makes this end the server (TestImplicitRoleDetection).
SM_TEST(P2pDtlsTransport, ImplicitRoleDetection) {
  Harness h;
  h.PrepareDtls();
  h.c1.SetupTransports();
  h.c2.SetupTransports();
  h.c2.dtls->SetDtlsRole(SslRole::kClient);
  h.SetRemoteFingerprintFromCert(h.c2.dtls.get(), *h.c1.cert);
  h.c2.ice->SetDestination(h.c1.ice.get());
  SslRole role;
  SM_EXPECT_TRUE(h.WaitUntil([&] { return h.c1.dtls->GetDtlsRole(&role) && role == SslRole::kServer; }));
  // With the fingerprint it then connects without the client retransmitting.
  h.SetRemoteFingerprintFromCert(h.c1.dtls.get(), *h.c2.cert);
  SM_EXPECT_TRUE(h.WaitUntil([&] { return h.c1.dtls->writable() && h.c2.dtls->writable(); }));
  SM_EXPECT_EQ(h.c1.ice->received_client_hellos, 1);
}

// The handshake retransmits on BoringSSL's schedule from libwebrtc's 50 ms initial timeout, doubling to 60 s
// (TestRetransmissionSchedule).
SM_TEST(P2pDtlsTransport, RetransmissionSchedule) {
  Harness h;
  h.net.delay = TimeDelta::Zero();
  h.PrepareDtls();
  h.Negotiate();
  // P2 (the client) becomes writable, P1 doesn't: P1 receives ClientHellos and never answers.
  h.c2.ice->SetDestination(h.c1.ice.get(), /*asymmetric=*/true);
  h.net.DeliverDue();
  SM_EXPECT_EQ(h.c1.ice->received_client_hellos, 1);
  SM_EXPECT_FALSE(h.c1.ice->writable());
  const int schedule_ms[] = {50, 100, 200, 400, 800, 1600, 3200, 6400, 12800, 25600, 51200, 60000};
  int expected = 1;
  for (int t : schedule_ms) {
    h.Advance(t - 1);
    SM_EXPECT_EQ(h.c1.ice->received_client_hellos, expected);
    h.Advance(1);
    SM_EXPECT_EQ(h.c1.ice->received_client_hellos, ++expected);
  }
  SM_EXPECT_EQ(h.c2.dtls->GetRetransmissionCount(), expected - 1);
}

// The first timeout is 2 x the ICE RTT, clamped to 50-3000 ms (ConfigureHandshakeTimeout).
SM_TEST(P2pDtlsTransport, HandshakeTimeoutFromIceRtt) {
  for (auto [rtt, first] : std::vector<std::pair<int, int>>{{100, 200}, {10, 50}, {5000, 3000}}) {
    Harness h;
    h.net.delay = TimeDelta::Zero();
    h.PrepareDtls();
    h.c1.SetupTransports();
    h.c2.SetupTransports(rtt);
    h.c1.dtls->SetDtlsRole(SslRole::kServer);
    h.c2.dtls->SetDtlsRole(SslRole::kClient);
    h.SetRemoteFingerprintFromCert(h.c2.dtls.get(), *h.c1.cert);
    h.c2.ice->SetDestination(h.c1.ice.get(), /*asymmetric=*/true);
    h.net.DeliverDue();
    SM_EXPECT_EQ(h.c1.ice->received_client_hellos, 1);
    h.Advance(first - 1);
    SM_EXPECT_EQ(h.c1.ice->received_client_hellos, 1);
    h.Advance(1);
    SM_EXPECT_EQ(h.c1.ice->received_client_hellos, 2);
  }
}

// A wrong fingerprint fails both ends, and a new fingerprint later restarts DTLS.
SM_TEST(P2pDtlsTransport, FingerprintMismatchAndRestart) {
  Harness h;
  h.PrepareDtls();
  h.c1.SetupTransports();
  h.c2.SetupTransports();
  h.c1.dtls->SetDtlsRole(SslRole::kServer);
  h.c2.dtls->SetDtlsRole(SslRole::kClient);
  h.SetRemoteFingerprintFromCert(h.c1.dtls.get(), *h.c2.cert, /*modify=*/true);
  h.SetRemoteFingerprintFromCert(h.c2.dtls.get(), *h.c1.cert);
  h.c1.ice->SetDestination(h.c2.ice.get());
  SM_EXPECT_TRUE(h.WaitUntil([&] {
    return h.c1.dtls->dtls_state() == DtlsTransportState::kFailed &&
           h.c2.dtls->dtls_state() == DtlsTransportState::kFailed;
  }));
  SM_EXPECT_FALSE(h.c1.dtls->writable());
  SM_EXPECT_FALSE(h.c2.dtls->writable());
  // An unknown algorithm or a digest of the wrong length is invalid.
  SM_EXPECT_FALSE(h.c1.dtls->SetRemoteParameters("sha-999", std::vector<uint8_t>(32, 1), std::nullopt, h.net.now));
  SM_EXPECT_FALSE(h.c1.dtls->SetRemoteParameters("sha-256", std::vector<uint8_t>(20, 1), std::nullopt, h.net.now));

  // A connected transport whose fingerprint changes starts over.
  Harness g;
  g.PrepareDtls();
  SM_ASSERT_TRUE(g.Connect());
  auto other = tls::SslIdentity::Create("P2b");
  g.SetRemoteFingerprintFromCert(g.c1.dtls.get(), *other);
  SM_EXPECT_FALSE(g.c1.dtls->writable());
  SM_EXPECT_TRUE(g.c1.dtls->dtls_state() == DtlsTransportState::kConnecting);
  SM_EXPECT_TRUE(g.c1.states == (std::vector<DtlsTransportState>{
                                    DtlsTransportState::kConnecting, DtlsTransportState::kConnected,
                                    DtlsTransportState::kNew, DtlsTransportState::kConnecting}));
}

// An empty algorithm means the peer doesn't do DTLS: passthrough. Remote parameters before a local certificate
// are refused.
SM_TEST(P2pDtlsTransport, PeerWithoutDtls) {
  Harness h;
  h.c1.SetupTransports();
  SM_EXPECT_FALSE(h.c1.dtls->SetRemoteParameters("sha-256", std::vector<uint8_t>(32, 1), std::nullopt, h.net.now));
  h.PrepareDtls();
  h.c1.SetupTransports();
  SM_EXPECT_TRUE(h.c1.dtls->IsDtlsActive());
  SM_EXPECT_TRUE(h.c1.dtls->SetRemoteParameters("", {}, std::nullopt, h.net.now));
  SM_EXPECT_FALSE(h.c1.dtls->IsDtlsActive());
}

// Destroying the DTLS transport removes its callbacks from ICE (DestructionDeregistersCallbacksFromIceTransport).
SM_TEST(P2pDtlsTransport, DestructionDeregistersCallbacks) {
  Harness h;
  h.PrepareDtls();
  SM_ASSERT_TRUE(h.Connect());
  h.c1.dtls.reset();
  h.c1.ice->NotifyWritableState(h.c1.ice.get(), h.net.now);
  h.c1.ice->NotifyReceivingState(h.c1.ice.get(), h.net.now);
  h.c1.ice->NotifyReadyToSend(h.c1.ice.get(), h.net.now);
  h.c1.ice->NotifySentPacket(h.c1.ice.get(), SentPacketInfo{});
  h.c1.ice->Receive(Client::MakePacket(100, 0, true));
}

// Receiving follows ICE once connected, and not before.
SM_TEST(P2pDtlsTransport, ReceivingFollowsIceOnceConnected) {
  Harness h;
  h.PrepareDtls();
  h.Negotiate();
  h.c1.ice->SetReceiving(true);
  SM_EXPECT_FALSE(h.c1.dtls->receiving());
  h.c1.ice->SetDestination(h.c2.ice.get());
  SM_ASSERT_TRUE(h.WaitUntil([&] { return h.c1.dtls->writable(); }));
  h.c1.ice->SetReceiving(false);
  h.c1.ice->SetReceiving(true);
  SM_EXPECT_TRUE(h.c1.dtls->receiving());
}

// Every causally consistent order of: the caller (P1, server) gets the fingerprint, becomes writable, receives the
// ClientHello, finishes the handshake. Each connects without retransmitting a hello, or fails with a wrong
// fingerprint, on DTLS 1.2 and 1.3 (DtlsEventOrderingTest).
SM_TEST(P2pDtlsTransport, EventOrdering) {
  enum Event { kFingerprint, kWritable, kClientHello, kHandshakeFinishes };
  const std::vector<std::vector<Event>> orders = {
      {kFingerprint, kWritable, kClientHello, kHandshakeFinishes},
      {kWritable, kFingerprint, kClientHello, kHandshakeFinishes},
      {kWritable, kClientHello, kFingerprint, kHandshakeFinishes},
      {kWritable, kClientHello, kHandshakeFinishes, kFingerprint},
      {kFingerprint, kClientHello, kWritable, kHandshakeFinishes},
      {kClientHello, kFingerprint, kWritable, kHandshakeFinishes},
      {kClientHello, kWritable, kFingerprint, kHandshakeFinishes},
      {kClientHello, kWritable, kHandshakeFinishes, kFingerprint},
  };
  for (int version : {tls::kDtls12, tls::kDtls13}) {
    for (bool valid : {true, false}) {
      for (size_t o = 0; o < orders.size(); ++o) {
        Harness h(version);
        h.PrepareDtls();
        h.c1.SetupTransports();
        h.c2.SetupTransports();
        h.c1.dtls->SetDtlsRole(SslRole::kServer);
        h.c2.dtls->SetDtlsRole(SslRole::kClient);
        h.SetRemoteFingerprintFromCert(h.c2.dtls.get(), *h.c1.cert);
        for (Event e : orders[o]) {
          switch (e) {
            case kFingerprint:
              h.SetRemoteFingerprintFromCert(h.c1.dtls.get(), *h.c2.cert, !valid);
              break;
            case kWritable:
              h.c1.ice->SetDestination(h.c2.ice.get(), /*asymmetric=*/true);
              break;
            case kClientHello:
              SM_EXPECT_EQ(h.c1.ice->received_client_hellos, 0);
              h.c2.ice->SetDestination(h.c1.ice.get(), /*asymmetric=*/true);
              SM_EXPECT_TRUE(h.WaitUntil([&] { return h.c1.ice->received_client_hellos >= 1; }));
              break;
            case kHandshakeFinishes:
              SM_EXPECT_FALSE(h.c1.dtls->IsDtlsConnected() ||
                              h.c1.dtls->dtls_state() == DtlsTransportState::kFailed);
              SM_EXPECT_TRUE(h.WaitUntil([&] {
                return h.c1.dtls->IsDtlsConnected() || h.c1.dtls->dtls_state() == DtlsTransportState::kFailed;
              }));
              break;
          }
        }
        const DtlsTransportState want = valid ? DtlsTransportState::kConnected : DtlsTransportState::kFailed;
        SM_EXPECT_TRUE(h.WaitUntil([&] { return h.c1.dtls->dtls_state() == want; }));
        SM_EXPECT_TRUE(h.WaitUntil([&] { return h.c2.dtls->dtls_state() == want; }));
        SM_EXPECT_EQ(h.c1.dtls->writable(), valid);
        SM_EXPECT_EQ(h.c2.dtls->writable(), valid);
        SM_EXPECT_EQ(h.c1.ice->received_client_hellos, 1);  // No hello retransmitted.
        if (valid) SM_EXPECT_TRUE(h.TestTransfer(1000, 100, false));
        if (h.c1.ice->received_client_hellos != 1 || h.c1.dtls->dtls_state() != want || h.c2.dtls->dtls_state() != want) {
          std::printf("  order %zu, version %x, valid %d retx %d\n", o, version, valid, h.c2.dtls->GetRetransmissionCount());
        }
      }
    }
  }
}

// The handshake completes through random loss and reordering on both paths, on DTLS 1.2 and 1.3, and both ends
// agree on the keys.
SM_TEST(P2pDtlsTransport, HandshakeWithLossAndReordering) {
  for (int version : {tls::kDtls12, tls::kDtls13}) {
    for (uint64_t seed = 1; seed <= 5; ++seed) {
      Harness h(version);
      h.net.seed = seed;
      h.net.loss = 0.2;
      h.net.reorder = 0.3;
      h.PrepareDtls();
      h.Negotiate();
      h.c1.ice->SetDestination(h.c2.ice.get());
      SM_EXPECT_TRUE(h.WaitUntil([&] { return h.c1.dtls->writable() && h.c2.dtls->writable(); }, 60000));
      std::vector<uint8_t> k1, k2;
      SM_EXPECT_TRUE(h.c1.dtls->AppendSrtpKeyingMaterial(k1) && h.c2.dtls->AppendSrtpKeyingMaterial(k2));
      SM_EXPECT_TRUE(k1 == k2);
      h.net.loss = 0;
      SM_EXPECT_TRUE(h.TestTransfer(1000, 20, false));
      if (!h.c1.dtls->writable() || !h.c2.dtls->writable()) {
        std::printf("  version %x seed %llu dropped %d states %d %d retx %d %d conn %d %d\n", version, static_cast<unsigned long long>(seed), h.net.dropped, int(h.c1.dtls->dtls_state()), int(h.c2.dtls->dtls_state()), h.c1.dtls->GetRetransmissionCount(), h.c2.dtls->GetRetransmissionCount(), h.c1.dtls->IsDtlsConnected(), h.c2.dtls->IsDtlsConnected());
      }
    }
  }
}

// JsepTransport::NegotiateDtlsRole: a=setup as offerer and answerer (RFC 4145, 5763, 8842).
SM_TEST(P2pDtlsRole, NegotiateDtlsRole) {
  using C = ConnectionRole;
  struct Case {
    SdpType type;
    C local, remote;
    std::optional<SslRole> current;
    std::optional<SslRole> want;  // nullopt: rejected.
  };
  const Case cases[] = {
      // Offerer: actpass only; the answer's active makes us the server, passive the client.
      {SdpType::kOffer, C::kActpass, C::kActive, {}, SslRole::kServer},
      {SdpType::kOffer, C::kActpass, C::kPassive, {}, SslRole::kClient},
      {SdpType::kOffer, C::kActpass, C::kNone, {}, SslRole::kServer},
      {SdpType::kOffer, C::kActpass, C::kActpass, {}, std::nullopt},
      {SdpType::kOffer, C::kActive, C::kPassive, {}, std::nullopt},
      // Answerer: active is the client, passive the server.
      {SdpType::kAnswer, C::kActive, C::kActpass, {}, SslRole::kClient},
      {SdpType::kAnswer, C::kPassive, C::kActpass, {}, SslRole::kServer},
      {SdpType::kAnswer, C::kActive, C::kNone, {}, SslRole::kClient},
      {SdpType::kAnswer, C::kActpass, C::kActpass, {}, std::nullopt},
      // RFC 8842 5.3: an offer of active or passive takes the opposite answer.
      {SdpType::kAnswer, C::kPassive, C::kActive, {}, SslRole::kServer},
      {SdpType::kAnswer, C::kActive, C::kPassive, {}, SslRole::kClient},
      {SdpType::kAnswer, C::kActive, C::kActive, {}, std::nullopt},
      {SdpType::kAnswer, C::kPassive, C::kPassive, {}, std::nullopt},
      // A re-offer must keep the negotiated role.
      {SdpType::kAnswer, C::kPassive, C::kActive, SslRole::kServer, SslRole::kServer},
      {SdpType::kAnswer, C::kActive, C::kActive, SslRole::kClient, std::nullopt},
      {SdpType::kAnswer, C::kActive, C::kPassive, SslRole::kServer, std::nullopt},
      {SdpType::kPrAnswer, C::kActive, C::kActpass, {}, SslRole::kClient},
  };
  for (const Case& c : cases) {
    std::optional<SslRole> role;
    const bool ok = NegotiateDtlsRole(c.type, c.local, c.remote, c.current, role);
    SM_EXPECT_EQ(ok, c.want.has_value());
    if (ok) SM_EXPECT_TRUE(role == c.want);
  }
}
