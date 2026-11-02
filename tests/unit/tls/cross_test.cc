// BoringSSL <-> mbedTLS (#998, C1 part 3): a DTLS-SRTP handshake between a DtlsTransport on each backend, in one
// binary (cross_peer.h). Both run libwebrtc M157's DtlsConfig defaults; they agree on DTLS 1.2 (Mbed TLS 3.6 has no
// DTLS 1.3), SRTP_AES128_CM_SHA1_80 (the first of M157's profiles, and one Mbed TLS has) and the same SRTP keys.
#include <functional>
#include <string>
#include <vector>

#include "cross_peer.h"
#include "sm_test.h"

using sm::TimeDelta;
using sm::Timestamp;
using tls_cross::Peer;

namespace {

constexpr int kDtls12 = 0xfefd;          // sm::tls::kDtls12
constexpr int kSrtpAes128CmSha1_80 = 1;  // sm::tls::kSrtpAes128CmSha1_80
constexpr int kSrtpAes128CmSha1_32 = 2;
constexpr int kSrtpAeadAes128Gcm = 7;

struct CrossPair {
  std::unique_ptr<Peer> client, server;
  Timestamp now = Timestamp::Seconds(1000);
  int dropped = 0;

  CrossPair(std::unique_ptr<Peer> c, std::unique_ptr<Peer> s) : client(std::move(c)), server(std::move(s)) {}
  void ExchangeFingerprints() {
    client->SetPeerFingerprint(server->fingerprint_algorithm(), server->fingerprint_digest(), now);
    server->SetPeerFingerprint(client->fingerprint_algorithm(), client->fingerprint_digest(), now);
  }
  bool StartBoth() { return server->Start(now) && client->Start(now); }
  // The same pipe as dtls_transport_test.cc: 10 ms one way, `drop` deciding per datagram whether it is lost.
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
      for (Peer* p : {client.get(), server.get()}) {
        if (auto due = p->NextTimeout(); due && *due <= now) p->OnTimeout(now);
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
};

CrossPair BoringClientMbedServer() {
  return CrossPair(tls_cross::boringssl_backend::MakePeer(false), tls_cross::mbedtls_backend::MakePeer(true));
}
CrossPair MbedClientBoringServer() {
  return CrossPair(tls_cross::mbedtls_backend::MakePeer(false), tls_cross::boringssl_backend::MakePeer(true));
}

void ExpectDtlsSrtpAgreement(CrossPair& p) {
  p.ExchangeFingerprints();
  SM_ASSERT_TRUE(p.StartBoth());
  p.Run(500);
  SM_ASSERT_TRUE(p.client->connected());
  SM_ASSERT_TRUE(p.server->connected());
  SM_EXPECT_EQ(p.client->version(), kDtls12);
  SM_EXPECT_EQ(p.server->version(), kDtls12);
  SM_EXPECT_EQ(p.client->srtp_crypto_suite(), kSrtpAes128CmSha1_80);
  SM_EXPECT_EQ(p.server->srtp_crypto_suite(), kSrtpAes128CmSha1_80);
  const std::vector<uint8_t> a = p.client->SrtpKeyingMaterial(), b = p.server->SrtpKeyingMaterial();
  SM_EXPECT_EQ(a.size(), 60u);  // 2 x (16-byte key + 14-byte salt).
  SM_EXPECT_TRUE(!a.empty() && a == b);  // RFC 5764 4.2: both ends derive the same keys.
  // Application data both ways (SCTP's path, C7).
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

}  // namespace

SM_TEST(TlsCrossBackend, BackendsAreDistinct) {
  SM_EXPECT_EQ(tls_cross::boringssl_backend::MakePeer(false)->backend(), std::string("boringssl_backend"));
  SM_EXPECT_EQ(tls_cross::mbedtls_backend::MakePeer(false)->backend(), std::string("mbedtls_backend"));
}

SM_TEST(TlsCrossBackend, BoringSslClientMbedTlsServer) {
  CrossPair p = BoringClientMbedServer();
  ExpectDtlsSrtpAgreement(p);
}

SM_TEST(TlsCrossBackend, MbedTlsClientBoringSslServer) {
  CrossPair p = MbedClientBoringServer();
  ExpectDtlsSrtpAgreement(p);
}

SM_TEST(TlsCrossBackend, WrongFingerprintFailsEitherWay) {
  for (bool boring_client : {true, false}) {
    CrossPair p = boring_client ? BoringClientMbedServer() : MbedClientBoringServer();
    // The client is given its own fingerprint, not the server's.
    p.client->SetPeerFingerprint(p.client->fingerprint_algorithm(), p.client->fingerprint_digest(), p.now);
    p.server->SetPeerFingerprint(p.client->fingerprint_algorithm(), p.client->fingerprint_digest(), p.now);
    SM_ASSERT_TRUE(p.StartBoth());
    p.Run(500);
    SM_EXPECT_TRUE(p.client->failed());
    SM_EXPECT_FALSE(p.client->connected() && p.server->connected());
  }
}

SM_TEST(TlsCrossBackend, RetransmitsALostFlightEitherWay) {
  for (bool boring_client : {true, false}) {
    CrossPair p = boring_client ? BoringClientMbedServer() : MbedClientBoringServer();
    p.ExchangeFingerprints();
    SM_ASSERT_TRUE(p.StartBoth());
    // The client's first flight is lost; its 50 ms timer resends it.
    p.Run(1000, [](bool to_server, int n) { return to_server && n == 0; });
    SM_EXPECT_TRUE(p.client->connected());
    SM_EXPECT_TRUE(p.server->connected());
    SM_EXPECT_TRUE(p.client->retransmissions() >= 1);
    SM_EXPECT_EQ(p.dropped, 1);
    SM_EXPECT_TRUE(!p.client->SrtpKeyingMaterial().empty() &&
                   p.client->SrtpKeyingMaterial() == p.server->SrtpKeyingMaterial());
  }
}

SM_TEST(TlsCrossBackend, GcmOnlyOnBoringSslSideFallsBackToACommonProfile) {
  // BoringSSL prefers AES-GCM; Mbed TLS has only the AES-CM profiles, so they agree on the AES-CM one both offer.
  CrossPair p(tls_cross::boringssl_backend::MakePeer(false, std::vector<int>{kSrtpAeadAes128Gcm, kSrtpAes128CmSha1_32}),
              tls_cross::mbedtls_backend::MakePeer(true, std::vector<int>{kSrtpAes128CmSha1_80, kSrtpAes128CmSha1_32,
                                                                          kSrtpAeadAes128Gcm}));
  p.ExchangeFingerprints();
  SM_ASSERT_TRUE(p.StartBoth());
  p.Run(500);
  SM_ASSERT_TRUE(p.client->connected());
  SM_ASSERT_TRUE(p.server->connected());
  SM_EXPECT_EQ(p.client->srtp_crypto_suite(), kSrtpAes128CmSha1_32);
  SM_EXPECT_EQ(p.server->srtp_crypto_suite(), kSrtpAes128CmSha1_32);
  SM_EXPECT_TRUE(!p.client->SrtpKeyingMaterial().empty() &&
                 p.client->SrtpKeyingMaterial() == p.server->SrtpKeyingMaterial());
}
