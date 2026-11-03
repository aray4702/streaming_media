// The TURN server's TLS (`turn tls`, #1087), in its own file: sm/p2p/ssl_adapter.h's SslRole and
// sm/p2p/dtls_transport.h's (the endpoint's) can't meet in one translation unit.
#if SM_TESTBENCH_ICE

#include <memory>

#include "sm/p2p/ssl_adapter_tls.h"
#include "sm/sim/turn_server.h"
#include "sm/tls/ssl_identity.h"

namespace sm::testbench {

void InstallIceSslAdapter() { p2p::InstallSslAdapterFromTls(); }

// TestTurnServer::AddInternalSocket for PROTO_TLS: an SSL adapter in the server role on each accepted connection.
sim::TurnServer::SslWrapper MakeTurnTlsWrapper() {
  static const std::shared_ptr<const tls::SslIdentity> identity = tls::SslIdentity::Create("turn.testbench.org");
  return [](std::unique_ptr<p2p::Socket> s) -> std::unique_ptr<p2p::Socket> {
    auto adapter = std::make_unique<p2p::TlsSslAdapter>(std::move(s));
    adapter->SetRole(p2p::SslRole::kServer);
    adapter->SetIdentity(identity->Clone());
    adapter->SetIgnoreBadCert(true);
    adapter->SetLoadSystemRoots(false);
    if (adapter->StartSSL("") != 0) return nullptr;
    return adapter;
  };
}

}  // namespace sm::testbench

#endif  // SM_TESTBENCH_ICE
