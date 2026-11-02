// The TLS client socket over TCP (issue #1035, C4 part 3): SslAdapter on sm::tls (TlsSslAdapter) through
// BasicPacketSocketFactory's CreateClientTcpSocket with OPT_TLS and OPT_TLS_INSECURE, against a TLS echo server in
// the emulated network (rtc_base/test_echo_server.h behind an SSLAdapter in server role, as libwebrtc's adapter
// tests run one). The client cases of openssl_adapter.cc that TURN over TLS uses: the handshake, SNI and ALPN, a
// certificate the roots don't trust refused and accepted under the insecure policy, the custom verifier, Send before
// the handshake, and packets larger than a TLS record and than the socket's send buffer.
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/async_packet_socket.h"
#include "sm/p2p/async_tcp_socket.h"
#include "sm/p2p/socket.h"
#include "sm/p2p/socket_address.h"
#include "sm/p2p/ssl_adapter.h"
#include "sm/p2p/ssl_adapter_tls.h"
#include "sm/sim/emulated_sockets.h"
#include "sm/tls/ssl_identity.h"
#include "sm_test.h"

using namespace sm;
using p2p::AsyncPacketSocket;
using p2p::PacketSocketFactory;
using p2p::ReceivedPacket;

namespace {

constexpr uint32_t kClientIp = sim::SocketAddress::Ip(11, 11, 11, 11);
constexpr uint32_t kServerIp = sim::SocketAddress::Ip(22, 22, 22, 22);
constexpr char kServerName[] = "turn.example.org";

sim::LinkConfig Link(int delay_ms) {
  sim::LinkConfig c;
  c.base_delay = TimeDelta::Millis(delay_ms);
  return c;
}

using Packet = std::vector<uint8_t>;

Timestamp Run(const std::vector<sim::EmulatedSocketServer*>& servers, TimeDelta horizon = TimeDelta::Seconds(100)) {
  Timestamp last = Timestamp::Micros(0);
  Timestamp until = Timestamp::PlusInfinity();
  for (;;) {
    Timestamp t = Timestamp::PlusInfinity();
    for (auto* s : servers) t = std::min(t, s->NextEvent());
    if (!t.IsFinite() || t > until) return last;
    if (!until.IsFinite()) until = t + horizon;
    for (auto* s : servers) s->Deliver(t);
    last = t;
  }
}

// A TLS echo server: each accepted connection runs TLS in server role (an identity for kServerName, the insecure
// policy, as libwebrtc's server-role adapter tests do) under the 2-byte framing, and echoes every packet.
class TlsEchoListenSocket final : public p2p::AsyncTcpListenSocket {
 public:
  TlsEchoListenSocket(std::unique_ptr<p2p::Socket> socket, const tls::SslIdentity& identity,
                      std::vector<std::string> alpn)
      : AsyncTcpListenSocket(std::move(socket)), identity_(identity), alpn_(std::move(alpn)) {}
  p2p::TlsSslAdapter* last_adapter = nullptr;

 protected:
  std::unique_ptr<AsyncPacketSocket> WrapIncomingConnection(std::unique_ptr<p2p::Socket> socket) override {
    auto adapter = std::make_unique<p2p::TlsSslAdapter>(std::move(socket));
    adapter->SetRole(p2p::SslRole::kServer);
    adapter->SetIdentity(identity_.Clone());
    adapter->SetIgnoreBadCert(true);
    adapter->SetLoadSystemRoots(false);
    adapter->SetAlpnProtocols(alpn_);
    SM_EXPECT_EQ(adapter->StartSSL(""), 0);
    last_adapter = adapter.get();
    return std::make_unique<p2p::AsyncTcpSocket>(std::move(adapter));
  }

 private:
  const tls::SslIdentity& identity_;
  std::vector<std::string> alpn_;
};

struct Fixture {
  sim::ByteNetwork net;
  sim::EmulatedSocketServer client_host{net, net.AddHost(kClientIp, 0, Link(10), Link(10))};
  sim::EmulatedSocketServer server_host{net, net.AddHost(kServerIp, 0, Link(10), Link(10))};
  std::unique_ptr<tls::SslIdentity> identity = tls::SslIdentity::Create(kServerName);
  std::unique_ptr<TlsEchoListenSocket> listen;
  std::vector<std::unique_ptr<AsyncPacketSocket>> server_conns;
  std::unique_ptr<AsyncPacketSocket> client;
  std::vector<Packet> received;
  int connects = 0;
  int closes = 0;
  int close_error = 0;
  Timestamp now = Timestamp::Seconds(1);

  explicit Fixture(std::vector<std::string> server_alpn = {}) {
    p2p::InstallSslAdapterFromTls();
    auto s = server_host.CreateStreamSocket(p2p::IpFamily::kV4);
    s->Bind(p2p::SocketAddress(server_host.ip(), 443));
    listen = std::make_unique<TlsEchoListenSocket>(std::move(s), *identity, std::move(server_alpn));
    listen->SubscribeNewConnection(this, [this](p2p::AsyncListenSocket*, std::unique_ptr<AsyncPacketSocket>& c) {
      AsyncPacketSocket* raw = c.get();
      raw->RegisterReceivedPacketCallback([this, raw](AsyncPacketSocket*, const ReceivedPacket& p) {
        raw->Send(p.arrival_time, p.payload, {});  // Echo.
      });
      server_conns.push_back(std::move(c));
    });
  }
  std::vector<uint8_t> server_cert() const {
    const auto der = identity->certificate().der();
    return {der.begin(), der.end()};
  }
  p2p::SocketAddress server_address(const std::string& hostname = kServerName) const {
    p2p::SocketAddress a = listen->GetLocalAddress();
    a.SetHostname(hostname);
    return a;
  }
  bool Connect(const p2p::PacketSocketTcpOptions& options, const p2p::SocketAddress& remote) {
    client = client_host.CreateClientTcpSocket(now, p2p::SocketAddress(client_host.ip(), 0), remote, options);
    if (!client) return false;
    client->SubscribeConnect(this, [this](AsyncPacketSocket*) { ++connects; });
    client->SubscribeCloseEvent(this, [this](AsyncPacketSocket*, int err) {
      ++closes;
      close_error = err;
    });
    client->RegisterReceivedPacketCallback([this](AsyncPacketSocket*, const ReceivedPacket& p) {
      received.emplace_back(p.payload.begin(), p.payload.end());
    });
    now = Run({&client_host, &server_host});
    return true;
  }
  void Send(const Packet& p) {
    client->Send(now, p, {});
    now = Run({&client_host, &server_host});
  }
};

p2p::PacketSocketTcpOptions TlsOptions(std::vector<std::vector<uint8_t>> roots, int opts = PacketSocketFactory::kOptTls) {
  p2p::PacketSocketTcpOptions o;
  o.opts = opts;
  o.tls_trusted_roots = std::move(roots);
  return o;
}

class FixedVerifier final : public p2p::SslCertificateVerifier {
 public:
  explicit FixedVerifier(bool answer) : answer_(answer) {}
  bool VerifyChain(const std::vector<std::vector<uint8_t>>& chain) override {
    ++calls;
    last_chain = chain;
    return answer_;
  }
  int calls = 0;
  std::vector<std::vector<uint8_t>> last_chain;

 private:
  bool answer_;
};

}  // namespace

SM_TEST(P2pSslAdapter, TlsClientConnectsWithSniAndEchoes) {
  Fixture f;
  SM_ASSERT_TRUE(f.Connect(TlsOptions({f.server_cert()}), f.server_address()));
  SM_EXPECT_EQ(f.connects, 1);  // After the handshake and the post-connection check.
  SM_EXPECT_EQ(f.closes, 0);
  SM_EXPECT_TRUE(f.client->GetState() == AsyncPacketSocket::State::kConnected);
  SM_ASSERT_TRUE(f.listen->last_adapter != nullptr);
  SM_ASSERT_TRUE(f.listen->last_adapter->tls() != nullptr);
  SM_EXPECT_EQ(f.listen->last_adapter->tls()->received_server_name(), std::string(kServerName));
  SM_EXPECT_EQ(f.listen->last_adapter->tls()->version(), tls::kTls13);
  const Packet ping = {'p', 'i', 'n', 'g'};
  f.Send(ping);
  SM_ASSERT_EQ(f.received.size(), 1u);
  SM_EXPECT_TRUE(f.received[0] == ping);
}

SM_TEST(P2pSslAdapter, AlpnAndCurvesReachTheServer) {
  Fixture f({"stun.turn"});
  auto o = TlsOptions({f.server_cert()});
  o.tls_alpn_protocols = {"stun.turn"};
  o.tls_elliptic_curves = {"X25519", "P-256"};
  SM_ASSERT_TRUE(f.Connect(o, f.server_address()));
  SM_ASSERT_EQ(f.connects, 1);
  SM_EXPECT_EQ(f.listen->last_adapter->tls()->alpn_protocol(), std::string("stun.turn"));
}

SM_TEST(P2pSslAdapter, UntrustedCertificateClosesTheSocket) {
  Fixture f;
  SM_ASSERT_TRUE(f.Connect(TlsOptions({}), f.server_address()));
  SM_EXPECT_EQ(f.connects, 0);
  SM_EXPECT_EQ(f.closes, 1);  // Error("ContinueSSL", err) signals the close.
  SM_EXPECT_NE(f.close_error, 0);
  SM_EXPECT_EQ(f.client->Send(f.now, Packet{1, 2, 3}, {}), -1);
}

SM_TEST(P2pSslAdapter, InsecureOptionAcceptsAnyCertificate) {
  Fixture f;
  // OPT_TLS_INSECURE (TLS_CERT_POLICY_INSECURE_NO_CHECK): no roots, and a name the certificate doesn't carry.
  SM_ASSERT_TRUE(f.Connect(TlsOptions({}, PacketSocketFactory::kOptTlsInsecure), f.server_address("10.0.0.1")));
  SM_EXPECT_EQ(f.connects, 1);
  SM_EXPECT_EQ(f.closes, 0);
  const Packet p = {'x'};
  f.Send(p);
  SM_ASSERT_EQ(f.received.size(), 1u);
  SM_EXPECT_TRUE(f.received[0] == p);
}

SM_TEST(P2pSslAdapter, AddressWithoutHostnameFailsTheSecureCheck) {
  // StartSSL(remote_address.hostname()) with an IP-only address: no SNI, and the host check fails, as libwebrtc's.
  Fixture f;
  SM_ASSERT_TRUE(f.Connect(TlsOptions({f.server_cert()}), f.server_address("")));
  SM_EXPECT_EQ(f.connects, 0);
  SM_EXPECT_EQ(f.closes, 1);
}

SM_TEST(P2pSslAdapter, CustomVerifierAcceptsOrRefuses) {
  {
    Fixture f;
    FixedVerifier v(true);
    auto o = TlsOptions({});
    o.tls_cert_verifier = &v;
    SM_ASSERT_TRUE(f.Connect(o, f.server_address()));
    SM_EXPECT_EQ(f.connects, 1);
    SM_EXPECT_GE(v.calls, 1);
    SM_ASSERT_GE(v.last_chain.size(), 1u);
    SM_EXPECT_TRUE(v.last_chain[0] == f.server_cert());
  }
  {
    Fixture f;
    FixedVerifier v(false);
    auto o = TlsOptions({});
    o.tls_cert_verifier = &v;
    SM_ASSERT_TRUE(f.Connect(o, f.server_address()));
    SM_EXPECT_EQ(f.connects, 0);
    SM_EXPECT_EQ(f.closes, 1);
  }
}

SM_TEST(P2pSslAdapter, SendBeforeTheHandshakeIsNotConnected) {
  Fixture f;
  f.client = f.client_host.CreateClientTcpSocket(f.now, p2p::SocketAddress(f.client_host.ip(), 0), f.server_address(),
                                                 TlsOptions({f.server_cert()}));
  SM_ASSERT_TRUE(f.client != nullptr);
  SM_EXPECT_TRUE(f.client->GetState() == AsyncPacketSocket::State::kConnecting);
  // The TCP connection is not up yet, and then the TLS handshake isn't: ENOTCONN either way (SSL_WAIT, CONNECTING).
  SM_EXPECT_EQ(f.client->Send(f.now, Packet{1}, {}), -1);
  SM_EXPECT_EQ(f.client->GetError(), p2p::kSocketENotConn);
}

SM_TEST(P2pSslAdapter, LargePacketsCrossRecordsAndAFullSendBuffer) {
  Fixture f;
  SM_ASSERT_TRUE(f.Connect(TlsOptions({f.server_cert()}), f.server_address()));
  SM_ASSERT_EQ(f.connects, 1);
  // 60000 bytes: four TLS records, and more than the 32 KiB send buffer, so the adapter waits for write events.
  Packet big(60000);
  for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<uint8_t>(i * 7);
  f.Send(big);
  SM_ASSERT_EQ(f.received.size(), 1u);
  SM_EXPECT_TRUE(f.received[0] == big);
}

SM_TEST(P2pSslAdapter, NoTlsWithoutAnInstalledAdapterOrForFakeTls) {
  Fixture f;
  const p2p::SocketAddress local(f.client_host.ip(), 0);
  // OPT_TLS_FAKE (AsyncSSLSocket) is not ported.
  SM_EXPECT_TRUE(f.client_host.CreateClientTcpSocket(f.now, local, f.server_address(),
                                                     TlsOptions({}, PacketSocketFactory::kOptTlsFake)) == nullptr);
  // SSLAdapter::Create failing makes the factory fail.
  p2p::SetSslAdapterFactory(nullptr);
  SM_EXPECT_TRUE(f.client_host.CreateClientTcpSocket(f.now, local, f.server_address(), TlsOptions({})) == nullptr);
  SM_EXPECT_TRUE(p2p::SslAdapter::Create(f.client_host.CreateStreamSocket(p2p::IpFamily::kV4)) == nullptr);
  p2p::InstallSslAdapterFromTls();
  SM_EXPECT_TRUE(f.client_host.CreateClientTcpSocket(f.now, local, f.server_address(), TlsOptions({})) != nullptr);
}

SM_TEST(P2pSslAdapter, ServerSocketRefusesTls) {
  Fixture f;
  SM_EXPECT_TRUE(f.server_host.CreateServerTcpSocket(p2p::SocketAddress(f.server_host.ip(), 0), 0, 0,
                                                     PacketSocketFactory::kOptTls) == nullptr);
}
