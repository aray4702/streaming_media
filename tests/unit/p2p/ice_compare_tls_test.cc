// The ICE comparison's TURN over TLS rows (issue #1055 part 2, compare/webrtc/ice_scenarios.h), this library's side:
// the TURN server's TLS listener is TestTurnServer's (a server-role SSL adapter with a self-signed certificate, as
// turn_port_tls_test.cc sets it up), and our client runs sm::tls (InstallSslAdapterFromTls). Built with SM_WITH_TLS.
#include <memory>

#include "ice_compare_relay.h"
#include "sm/p2p/ssl_adapter.h"
#include "sm/p2p/ssl_adapter_tls.h"
#include "sm/tls/ssl_identity.h"
#include "test_crypto.h"

namespace {

void InstallTls() {
  sm::p2p::InstallSslAdapterFromTls();
  static const std::unique_ptr<sm::tls::SslIdentity> identity = sm::tls::SslIdentity::Create("turn.example.org");
  sm::ice_compare::TlsServerWrapper() = [](std::unique_ptr<sm::p2p::Socket> socket) -> std::unique_ptr<sm::p2p::Socket> {
    auto adapter = std::make_unique<sm::p2p::TlsSslAdapter>(std::move(socket));
    adapter->SetRole(sm::p2p::SslRole::kServer);
    adapter->SetIdentity(identity->Clone());
    adapter->SetIgnoreBadCert(true);
    adapter->SetLoadSystemRoots(false);
    if (adapter->StartSSL("") != 0) return nullptr;
    return adapter;
  };
}

}  // namespace

// libwebrtc M157's column of the TURN over TLS rows (sm_ice_compare, where libwebrtc's client is its OpenSSLAdapter;
// the same on every seed, and our side gave the same values on every seed). Against TCP, the TLS handshake adds one
// round trip to the server (30 ms) to relay only, and 48 ms and two checks at connect to the NAT pairs.
SM_TEST(P2pIceCompareTls, TlsRelayMatchesLibwebrtc) {
  sm::p2p::testing::InstallTestCryptoWithUniqueIds();
  InstallTls();
  const ice_compare_relay::Row kLibwebrtc[] = {
      {"port-sym-turntls", 300, 14.0, 418.0, {4.0, 7.0, 0, 0}, "srflx/relay", "-/tls"},
      {"sym-port-turntls", 300, 14.0, 418.0, {4.0, 7.0, 0, 0}, "prflx/relay", "-/tls"},
      {"sym-sym-turntls", 300, 14.0, 418.0, {4.0, 8.0, 0, 0}, "prflx/relay", "-/tls"},
      {"open-open-turntls-relayonly", 250, 4.0, 16.0, {4.0, 2.0, 0, 0}, "relay/relay", "tls/tls"},
  };
  const std::vector<sm::ice_compare::Scenario> scenarios = sm::ice_compare::StreamRelayScenarios();
  for (const ice_compare_relay::Row& row : kLibwebrtc) ice_compare_relay::ExpectMatches(scenarios, row);
  sm::ice_compare::TlsServerWrapper() = nullptr;
}
