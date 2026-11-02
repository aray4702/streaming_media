// TURN over TLS (issue #1036, C4 part 4): the TLS cases of libwebrtc M157's
// p2p/base/turn_port_unittest.cc (turn_stream_cases.h) against the test TURN
// server's TLS listener. The server runs TLS in server role on sm::tls
// (TlsSslAdapter) with a self-signed certificate, as libwebrtc's
// TestTurnServer does with ignore_bad_cert, and the client takes the
// insecure policy (TLS_CERT_POLICY_INSECURE_NO_CHECK), as libwebrtc's test.
#include <memory>

#include "sm/p2p/ssl_adapter.h"
#include "sm/p2p/ssl_adapter_tls.h"
#include "sm/tls/ssl_identity.h"
#include "turn_stream_cases.h"

namespace {

constexpr ProtocolType kProto = PROTO_TLS;

// TestTurnServer::AddInternalSocket for PROTO_TLS: an SSL adapter in server
// role, its handshake started, on each accepted connection.
void SetUpTls(TurnFixture& f) {
  InstallSslAdapterFromTls();
  static const std::unique_ptr<tls::SslIdentity> identity = tls::SslIdentity::Create("turn.example.org");
  f.tls_wrapper = [](std::unique_ptr<Socket> socket) -> std::unique_ptr<Socket> {
    auto adapter = std::make_unique<TlsSslAdapter>(std::move(socket));
    adapter->SetRole(SslRole::kServer);
    adapter->SetIdentity(identity->Clone());
    adapter->SetIgnoreBadCert(true);
    adapter->SetLoadSystemRoots(false);
    if (adapter->StartSSL("") != 0) return nullptr;
    return adapter;
  };
}

struct TlsFixture : TurnFixture {
  TlsFixture() { SetUpTls(*this); }
};

}  // namespace

// TestReconstructedServerUrlForTls; TLS allocation.
SM_TEST(P2pTurnPortTls, Allocate) {
  TlsFixture f;
  turn_stream::Allocate(f, kProto);
}

SM_TEST(P2pTurnPortTls, AllocateMismatch) {
  TlsFixture f;
  turn_stream::AllocateMismatch(f, kProto);
}

// TestPrivateIpTurnCandidateErrorAddressScrubbedTls.
SM_TEST(P2pTurnPortTls, PrivateServerErrorAddressScrubbed) {
  TlsFixture f;
  turn_stream::ErrorAddressScrubbing(f, kProto, /*private_server=*/true);
}

// TestPublicIpTurnCandidateErrorAddressNotScrubbedTls.
SM_TEST(P2pTurnPortTls, PublicServerErrorAddressNotScrubbed) {
  TlsFixture f;
  turn_stream::ErrorAddressScrubbing(f, kProto, /*private_server=*/false);
}

SM_TEST(P2pTurnPortTls, ConnectionRefused) {
  TlsFixture f;
  turn_stream::ConnectionRefused(f, kProto);
}

// TestTurnAlternateServerTLS.
SM_TEST(P2pTurnPortTls, AlternateServer) {
  TlsFixture f;
  turn_stream::AlternateServer(f, kProto);
}

// TestTurnAlternateServerPingPongTLS, ...DetectRepetitionTLS,
// ...LoopbackTlsIpv4, ...AnyAddressTlsIpv4.
SM_TEST(P2pTurnPortTls, AlternateServerRefused) { turn_stream::AlternateServerRefusedCases(kProto, &SetUpTls); }

// TestTurnTlsConnection.
SM_TEST(P2pTurnPortTls, Connection) {
  TlsFixture f;
  turn_stream::Connection(f, kProto);
}

// TestTurnSendDataTurnTlsToUdp.
SM_TEST(P2pTurnPortTls, SendData) {
  TlsFixture f;
  turn_stream::SendData(f, kProto);
}

// TestTurnTLSReleaseAllocation.
SM_TEST(P2pTurnPortTls, ReleaseAllocation) {
  TlsFixture f;
  turn_stream::ReleaseAllocation(f, kProto);
}

// TestTurnTLSGracefulReleaseAllocation.
SM_TEST(P2pTurnPortTls, GracefulReleaseAllocation) {
  TlsFixture f;
  turn_stream::GracefulReleaseAllocation(f, kProto);
}

SM_TEST(P2pTurnPortTls, ConnectionClosedAfterAllocation) {
  TlsFixture f;
  turn_stream::ConnectionClosedAfterAllocation(f, kProto);
}

// Relay candidates over TLS with UDP blocked in the emulated network.
SM_TEST(P2pTurnPortTls, AllocatorGathersRelayWithUdpBlocked) {
  TlsFixture f;
  turn_stream::AllocatorGathersRelayWithUdpBlocked(f, kProto);
}

// TURN over TLS on port 443, where firewalls that block everything but HTTPS
// let it through (an allowed TURN port, AllowedTurnPort).
SM_TEST(P2pTurnPortTls, AllocateOnPort443) {
  TlsFixture f;
  const SocketAddress server(kTurnTcpIp, 443);
  turn_stream::AddServer(f, f.turn_tcp, server, kProto);
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(server, kProto));
  SM_ASSERT_TRUE(f.turn_port != nullptr);
  f.turn_port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_ready; }, TimeToGetTurnCandidate(kProto)));
  SM_ASSERT_EQ(f.turn_port->Candidates().size(), size_t{1});
  SM_EXPECT_EQ(f.turn_port->Candidates()[0].url(), std::string("turns:99.99.99.4:443?transport=tcp"));
  SM_EXPECT_EQ(f.turn_port->Candidates()[0].relay_protocol(), std::string("tls"));
}
