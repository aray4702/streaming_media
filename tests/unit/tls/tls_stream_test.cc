// TLS over a byte stream (#1035, C4 part 3): TlsStream, the engine of libwebrtc's OpenSSLAdapter, client against a
// server over an in-memory pipe, on each backend. The client cases libwebrtc tests for TURN over TLS: the handshake
// at TLS 1.3 and 1.2, SNI, ALPN and curves passed through, a certificate the roots don't trust refused and accepted
// under the insecure policy, the host name checked after the handshake, and the custom verifier.
// (openssl_adapter_unittest.cc and ssl_adapter_unittest.cc are not in compare/webrtc/reference/; these follow
// openssl_adapter.cc's code paths.)
#include <memory>
#include <string>
#include <vector>

#include "sm/tls/ssl_identity.h"
#include "sm/tls/tls_stream.h"
#include "sm_test.h"

using namespace sm::tls;

namespace {

constexpr char kServerName[] = "turn.example.org";

// The server end, as libwebrtc's adapter tests run it: SetRole(SSL_SERVER), an identity, SetIgnoreBadCert(true).
TlsConfig ServerConfig() {
  TlsConfig c;
  c.role = TlsRole::kServer;
  c.ignore_bad_cert = true;
  c.load_system_roots = false;
  return c;
}

struct Pair {
  std::unique_ptr<TlsStream> client, server;
  std::vector<uint8_t> server_cert;  // DER

  explicit Pair(TlsConfig ccfg, TlsConfig scfg = ServerConfig(), const std::string& cn = kServerName) {
    auto id = SslIdentity::Create(cn);
    const auto der = id->certificate().der();
    server_cert.assign(der.begin(), der.end());
    server = std::make_unique<TlsStream>(std::move(scfg), std::move(id));
    client = std::make_unique<TlsStream>(std::move(ccfg));
  }
  // Moves bytes both ways until neither end has anything to send.
  void Pump() {
    for (int i = 0; i < 100; ++i) {
      bool moved = false;
      if (client->HasOutgoing()) {
        server->Feed(client->TakeOutgoing());
        moved = true;
      }
      if (server->HasOutgoing()) {
        client->Feed(server->TakeOutgoing());
        moved = true;
      }
      if (!moved) return;
    }
  }
  bool Connect() {
    if (!server->Start() || !client->Start()) return false;
    Pump();
    return client->state() == TlsState::kConnected && server->state() == TlsState::kConnected;
  }
};

TlsConfig ClientConfig(const std::vector<uint8_t>* root = nullptr) {
  TlsConfig c;
  c.server_name = kServerName;
  c.load_system_roots = false;
  if (root) c.trusted_roots.push_back(*root);
  return c;
}

// A client that trusts the server's (self-signed) certificate as a root.
Pair TrustingPair(TlsConfig ccfg = ClientConfig(), TlsConfig scfg = ServerConfig()) {
  Pair p(ccfg, scfg);
  ccfg.trusted_roots.push_back(p.server_cert);
  p.client = std::make_unique<TlsStream>(std::move(ccfg));
  return p;
}

std::string ReadAll(TlsStream& s) {
  std::string out;
  uint8_t buf[256];
  int n;
  while ((n = s.Read(buf)) > 0) out.append(reinterpret_cast<char*>(buf), static_cast<size_t>(n));
  return out;
}

std::vector<uint8_t> Bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

}  // namespace

SM_TEST(TlsTlsStream, HandshakeAtTls13WithSniAndDataBothWays) {
  Pair p = TrustingPair();
  SM_ASSERT_TRUE(p.Connect());
  SM_EXPECT_EQ(p.client->version(), kTls13);  // TLS_method's highest, as libwebrtc M157 offers.
  SM_EXPECT_EQ(p.server->version(), kTls13);
  SM_EXPECT_TRUE(p.client->peer_verified());
  SM_EXPECT_EQ(p.server->received_server_name(), std::string(kServerName));  // SNI
  SM_EXPECT_EQ(p.client->Write(Bytes("hello server")), 12);
  p.Pump();
  SM_EXPECT_EQ(ReadAll(*p.server), std::string("hello server"));
  SM_EXPECT_EQ(p.server->Write(Bytes("hello client")), 12);
  p.Pump();
  SM_EXPECT_EQ(ReadAll(*p.client), std::string("hello client"));
  uint8_t buf[16];
  SM_EXPECT_EQ(p.client->Read(buf), kTlsWouldBlock);  // Nothing more buffered.
}

SM_TEST(TlsTlsStream, Tls12WhenCapped) {
  TlsConfig c = ClientConfig();
  c.max_version = kTls12;
  Pair p = TrustingPair(c);
  SM_ASSERT_TRUE(p.Connect());
  SM_EXPECT_EQ(p.client->version(), kTls12);
  SM_EXPECT_EQ(p.server->version(), kTls12);
  SM_EXPECT_EQ(p.client->Write(Bytes("over 1.2")), 8);
  p.Pump();
  SM_EXPECT_EQ(ReadAll(*p.server), std::string("over 1.2"));
}

SM_TEST(TlsTlsStream, WriteBeforeHandshakeWouldBlock) {
  Pair p = TrustingPair();
  SM_ASSERT_TRUE(p.server->Start());
  SM_ASSERT_TRUE(p.client->Start());
  SM_EXPECT_TRUE(p.client->HasOutgoing());  // The ClientHello.
  SM_EXPECT_EQ(p.client->state(), TlsState::kConnecting);
  SM_EXPECT_EQ(p.client->Write(Bytes("early")), kTlsWouldBlock);
}

SM_TEST(TlsTlsStream, AlpnAndCurvesPassedThrough) {
  TlsConfig c = ClientConfig();
  c.alpn_protocols = {"stun.turn", "h2"};
  c.elliptic_curves = {"P-256"};
  TlsConfig s = ServerConfig();
  s.alpn_protocols = {"stun.turn"};
  Pair p = TrustingPair(c, s);
  SM_ASSERT_TRUE(p.Connect());
  SM_EXPECT_EQ(p.client->alpn_protocol(), std::string("stun.turn"));
  SM_EXPECT_EQ(p.server->alpn_protocol(), std::string("stun.turn"));
}

SM_TEST(TlsTlsStream, CurvesTheServerLacksFailTheHandshake) {
  TlsConfig c = ClientConfig();
  c.elliptic_curves = {"P-384"};
  TlsConfig s = ServerConfig();
  s.elliptic_curves = {"X25519"};
  Pair p = TrustingPair(c, s);
  SM_EXPECT_FALSE(p.Connect());
  // The server finds no shared group and fails (BoringSSL alerts the client; Mbed TLS's server doesn't).
  SM_EXPECT_EQ(p.server->state(), TlsState::kFailed);
  SM_EXPECT_NE(p.client->state(), TlsState::kConnected);
}

SM_TEST(TlsTlsStream, TransformAlpnProtocols) {
  SM_EXPECT_EQ(TransformAlpnProtocols({"h2", "stun.turn"}), std::string("\x02h2\x09stun.turn"));
  SM_EXPECT_EQ(TransformAlpnProtocols({"h2", ""}), std::string());
  SM_EXPECT_EQ(TransformAlpnProtocols({std::string(256, 'a')}), std::string());
}

SM_TEST(TlsTlsStream, UntrustedCertificateRefused) {
  Pair p(ClientConfig());  // No root for the server's self-signed certificate.
  SM_EXPECT_FALSE(p.Connect());
  SM_EXPECT_EQ(p.client->state(), TlsState::kFailed);
  SM_EXPECT_FALSE(p.client->peer_verified());
  SM_EXPECT_EQ(p.client->Write(Bytes("x")), kTlsError);
}

SM_TEST(TlsTlsStream, SystemRootsDoNotTrustASelfSignedCertificate) {
  TlsConfig c = ClientConfig();
  c.load_system_roots = true;
  Pair p(c);
  SM_EXPECT_FALSE(p.Connect());
  SM_EXPECT_EQ(p.client->state(), TlsState::kFailed);
}

SM_TEST(TlsTlsStream, UntrustedCertificateAcceptedWhenIgnoringBadCerts) {
  TlsConfig c = ClientConfig();
  c.ignore_bad_cert = true;  // TLS_CERT_POLICY_INSECURE_NO_CHECK
  Pair p(c);
  SM_ASSERT_TRUE(p.Connect());
  SM_EXPECT_FALSE(p.client->peer_verified());
  SM_EXPECT_EQ(p.client->Write(Bytes("insecure")), 8);
  p.Pump();
  SM_EXPECT_EQ(ReadAll(*p.server), std::string("insecure"));
}

SM_TEST(TlsTlsStream, HostNameMismatchFailsThePostConnectionCheck) {
  TlsConfig c = ClientConfig();
  c.server_name = "other.example.org";
  Pair p = TrustingPair(c);
  SM_EXPECT_FALSE(p.Connect());
  SM_EXPECT_EQ(p.client->state(), TlsState::kFailed);
  SM_EXPECT_TRUE(p.client->peer_verified());  // The chain was fine; the name was not.
  c.ignore_bad_cert = true;
  Pair q = TrustingPair(c);
  SM_EXPECT_TRUE(q.Connect());
}

SM_TEST(TlsTlsStream, EmptyHostNameNeverMatches) {
  // VerifyPeerCertMatchesHost fails for an empty host, so a secure connection to a bare IP fails, as libwebrtc's.
  TlsConfig c = ClientConfig();
  c.server_name.clear();
  Pair p = TrustingPair(c);
  SM_EXPECT_FALSE(p.Connect());
  SM_EXPECT_EQ(p.server->received_server_name(), std::string());  // No SNI without a name.
}

SM_TEST(TlsTlsStream, CustomVerifierDecidesAnUntrustedChain) {
  std::vector<std::vector<uint8_t>> seen;
  TlsConfig c = ClientConfig();
  c.cert_verifier = [&seen](const std::vector<std::vector<uint8_t>>& chain) {
    seen = chain;
    return true;
  };
  Pair p(c);
  SM_ASSERT_TRUE(p.Connect());
  SM_EXPECT_TRUE(p.client->peer_verified());
  SM_ASSERT_EQ(seen.size(), 1u);
  SM_EXPECT_TRUE(seen[0] == p.server_cert);  // Leaf first.

  c.cert_verifier = [](const std::vector<std::vector<uint8_t>>&) { return false; };
  Pair q(c);
  SM_EXPECT_FALSE(q.Connect());
}

SM_TEST(TlsTlsStream, CustomVerifierNotAskedForATrustedChain) {
  int calls = 0;
  TlsConfig c = ClientConfig();
  c.cert_verifier = [&calls](const std::vector<std::vector<uint8_t>>&) {
    ++calls;
    return false;
  };
  Pair p = TrustingPair(c);
  SM_EXPECT_TRUE(p.Connect());
  SM_EXPECT_EQ(calls, 0);
}

SM_TEST(TlsTlsStream, PartialRecordWaitsForTheRest) {
  Pair p = TrustingPair();
  SM_ASSERT_TRUE(p.Connect());
  // A truncated record is not data yet.
  SM_ASSERT_EQ(p.server->Write(Bytes("abc")), 3);
  std::vector<uint8_t> rec = p.server->TakeOutgoing();
  SM_ASSERT_GT(rec.size(), 1u);
  p.client->Feed(std::vector<uint8_t>(rec.begin(), rec.end() - 1));
  uint8_t buf[16];
  SM_EXPECT_EQ(p.client->Read(buf), kTlsWouldBlock);
  p.client->Feed(std::vector<uint8_t>(rec.end() - 1, rec.end()));
  SM_EXPECT_EQ(p.client->Read(buf), 3);
}

SM_TEST(TlsTlsStream, WriteLargerThanARecordTakesItAll) {
  Pair p = TrustingPair();
  SM_ASSERT_TRUE(p.Connect());
  std::string big(60000, 'z');
  for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<char>('a' + i % 26);
  SM_EXPECT_EQ(p.client->Write(Bytes(big)), 60000);  // Four records.
  p.Pump();
  SM_EXPECT_EQ(ReadAll(*p.server), big);
}
