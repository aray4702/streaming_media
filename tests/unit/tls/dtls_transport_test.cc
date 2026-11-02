// DTLS between two DtlsTransports over a simulated pipe (#984 part 2): libwebrtc's ssl_stream_adapter_unittest.cc
// scenarios that apply to DTLS-SRTP (handshake, fingerprint check, SRTP profile and keys, retransmission, versions).
#include <functional>
#include <string>
#include <vector>

#include "sm/tls/dtls_transport.h"
#include "sm/tls/ssl_fingerprint.h"
#include "sm/tls/ssl_identity.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::tls;

namespace {

struct Pair {
  std::unique_ptr<DtlsTransport> client, server;
  std::unique_ptr<SslFingerprint> client_fp, server_fp;
  Timestamp now = Timestamp::Seconds(1000);
  int dropped = 0;

  explicit Pair(DtlsConfig ccfg = {}, DtlsConfig scfg = {}) {
    auto cid = SslIdentity::Create("client");
    auto sid = SslIdentity::Create("server");
    client_fp = SslFingerprint::CreateFromCertificate(*cid);
    server_fp = SslFingerprint::CreateFromCertificate(*sid);
    client = std::make_unique<DtlsTransport>(std::move(cid), DtlsRole::kClient, ccfg);
    server = std::make_unique<DtlsTransport>(std::move(sid), DtlsRole::kServer, scfg);
  }
  // Runs the pipe (10 ms one way) for `ms`, `drop` deciding per datagram (direction, index) whether it is lost.
  void Run(int ms, const std::function<bool(bool to_server, int n)>& drop = nullptr) {
    struct InFlight { Timestamp at; bool to_server; std::vector<uint8_t> d; };
    std::vector<InFlight> flight;
    int n_cs = 0, n_sc = 0;
    for (int t = 0; t < ms; ++t, now = now + TimeDelta::Millis(1)) {
      for (size_t i = 0; i < flight.size();) {
        if (flight[i].at <= now) {
          (flight[i].to_server ? *server : *client).OnPacket(flight[i].d, now);
          flight.erase(flight.begin() + static_cast<long>(i));
        } else {
          ++i;
        }
      }
      for (DtlsTransport* tr : {client.get(), server.get()}) {
        if (auto due = tr->NextTimeout(); due && *due <= now) tr->OnTimeout(now);
      }
      for (auto& d : client->TakeOutgoing()) {
        if (drop && drop(true, n_cs++)) { ++dropped; continue; }
        flight.push_back({now + TimeDelta::Millis(10), true, std::move(d)});
      }
      for (auto& d : server->TakeOutgoing()) {
        if (drop && drop(false, n_sc++)) { ++dropped; continue; }
        flight.push_back({now + TimeDelta::Millis(10), false, std::move(d)});
      }
    }
  }
  void StartBoth() {
    SM_ASSERT_TRUE(server->Start(now));
    SM_ASSERT_TRUE(client->Start(now));
  }
};

}  // namespace

SM_TEST(TlsDtlsTransport, HandshakeVerifiesFingerprintsAndAgreesOnSrtp) {
  Pair p;
  p.client->SetPeerFingerprint(*p.server_fp, p.now);
  p.server->SetPeerFingerprint(*p.client_fp, p.now);
  p.StartBoth();
  p.Run(500);
  SM_ASSERT_TRUE(p.client->state() == DtlsState::kConnected);
  SM_ASSERT_TRUE(p.server->state() == DtlsState::kConnected);
  SM_EXPECT_EQ(p.client->version(), kDtls13);  // Both on BoringSSL: DTLS 1.3, as libwebrtc M157 offers.
  SM_EXPECT_EQ(p.client->srtp_crypto_suite(), kSrtpAes128CmSha1_80);  // The first of M157's defaults.
  SM_EXPECT_EQ(p.server->srtp_crypto_suite(), kSrtpAes128CmSha1_80);
  int key = 0, salt = 0;
  SM_ASSERT_TRUE(SrtpKeyAndSaltLength(kSrtpAes128CmSha1_80, key, salt));
  std::vector<uint8_t> a(2 * (key + salt)), b(a.size());
  SM_ASSERT_TRUE(p.client->ExportSrtpKeyingMaterial(a));
  SM_ASSERT_TRUE(p.server->ExportSrtpKeyingMaterial(b));
  SM_EXPECT_TRUE(a == b);  // RFC 5764: both ends derive the same material.
  SM_ASSERT_TRUE(p.client->peer_certificate() != nullptr);
  SM_EXPECT_TRUE(*SslFingerprint::Create("sha-256", *p.client->peer_certificate()) == *p.server_fp);
}

SM_TEST(TlsDtlsTransport, ApplicationDataBothWays) {
  Pair p;
  p.client->SetPeerFingerprint(*p.server_fp, p.now);
  p.server->SetPeerFingerprint(*p.client_fp, p.now);
  p.StartBoth();
  p.Run(300);
  const std::vector<uint8_t> hello = {'h', 'i'}, back = {'o', 'k', '!'};
  SM_ASSERT_TRUE(p.client->Send(hello, p.now));
  SM_ASSERT_TRUE(p.server->Send(back, p.now));
  p.Run(50);
  auto rs = p.server->TakeReceived(), rc = p.client->TakeReceived();
  SM_ASSERT_EQ(rs.size(), 1u);
  SM_ASSERT_EQ(rc.size(), 1u);
  SM_EXPECT_TRUE(rs[0] == hello);
  SM_EXPECT_TRUE(rc[0] == back);
}

SM_TEST(TlsDtlsTransport, WrongFingerprintFails) {
  Pair p;
  auto other = SslFingerprint::CreateFromCertificate(*SslIdentity::Create("mallory"));
  p.client->SetPeerFingerprint(*other, p.now);  // Not the server's certificate.
  p.server->SetPeerFingerprint(*p.client_fp, p.now);
  p.StartBoth();
  p.Run(500);
  SM_EXPECT_TRUE(p.client->state() == DtlsState::kFailed);
  SM_EXPECT_FALSE(p.server->state() == DtlsState::kConnected && p.client->state() == DtlsState::kConnected);
}

SM_TEST(TlsDtlsTransport, FingerprintAfterTheHandshake) {
  // SDP's answer can arrive after DTLS has started: the peer is verified when the digest comes.
  Pair p;
  p.server->SetPeerFingerprint(*p.client_fp, p.now);
  p.StartBoth();
  p.Run(300);
  SM_EXPECT_TRUE(p.client->state() == DtlsState::kConnecting);
  SM_EXPECT_TRUE(p.client->SetPeerFingerprint(*p.server_fp, p.now));
  SM_EXPECT_TRUE(p.client->state() == DtlsState::kConnected);
  auto other = SslFingerprint::CreateFromCertificate(*SslIdentity::Create("mallory"));
  Pair q;
  q.server->SetPeerFingerprint(*q.client_fp, q.now);
  q.StartBoth();
  q.Run(300);
  SM_EXPECT_TRUE(q.server->state() == DtlsState::kConnected);
  SM_EXPECT_FALSE(q.client->SetPeerFingerprint(*other, q.now));
  SM_EXPECT_TRUE(q.client->state() == DtlsState::kFailed);
  // The client tells the server with a fatal alert (SetPeerCertificateDigest's Error(SSL_AD_BAD_CERTIFICATE), #1006),
  // so it fails too instead of staying connected to a peer that rejected it.
  q.Run(100);
  SM_EXPECT_TRUE(q.server->state() == DtlsState::kFailed);
}

SM_TEST(TlsDtlsTransport, RetransmitsALostFlight) {
  Pair p;
  p.client->SetPeerFingerprint(*p.server_fp, p.now);
  p.server->SetPeerFingerprint(*p.client_fp, p.now);
  p.StartBoth();
  // The client's first flight is lost: the 50 ms timer resends it (DTLSv1_handle_timeout).
  p.Run(1000, [](bool to_server, int n) { return to_server && n == 0; });
  SM_EXPECT_TRUE(p.client->state() == DtlsState::kConnected);
  SM_EXPECT_TRUE(p.server->state() == DtlsState::kConnected);
  SM_EXPECT_TRUE(p.client->retransmissions() >= 1);
  SM_EXPECT_EQ(p.dropped, 1);
}

SM_TEST(TlsDtlsTransport, Dtls13ClientResendsItsLastFlightAfterConnecting) {
  // In DTLS 1.3 the client is connected once it sends its Finished, and keeps the flight until the server ACKs it.
  // If it is lost the client's timer resends it: the timer runs after the handshake too (#1006).
  Pair p;
  p.client->SetPeerFingerprint(*p.server_fp, p.now);
  p.server->SetPeerFingerprint(*p.client_fp, p.now);
  p.StartBoth();
  bool dropped_final = false;
  p.Run(3000, [&](bool to_server, int) {
    if (!to_server || dropped_final || p.client->state() != DtlsState::kConnected) return false;
    dropped_final = true;
    return true;
  });
  SM_ASSERT_TRUE(dropped_final);
  SM_EXPECT_EQ(p.client->version(), kDtls13);
  SM_EXPECT_TRUE(p.server->state() == DtlsState::kConnected);
  SM_EXPECT_TRUE(p.client->retransmissions() >= 1);
}

SM_TEST(TlsDtlsTransport, CapsAtDtls12AndPicksACommonProfile) {
  DtlsConfig c12;
  c12.max_version = kDtls12;
  c12.srtp_crypto_suites = {kSrtpAeadAes128Gcm};
  DtlsConfig s;
  s.srtp_crypto_suites = {kSrtpAes128CmSha1_80, kSrtpAeadAes128Gcm};
  Pair p(c12, s);
  p.client->SetPeerFingerprint(*p.server_fp, p.now);
  p.server->SetPeerFingerprint(*p.client_fp, p.now);
  p.StartBoth();
  p.Run(500);
  SM_ASSERT_TRUE(p.client->state() == DtlsState::kConnected);
  SM_EXPECT_EQ(p.client->version(), kDtls12);  // An endpoint without DTLS 1.3 (mbedTLS, C1) gets 1.2.
  SM_EXPECT_EQ(p.server->version(), kDtls12);
  SM_EXPECT_EQ(p.client->srtp_crypto_suite(), kSrtpAeadAes128Gcm);
}
