// Connectivity benchmark for 1.12, the endpoint (PeerEndpoint), in the p2p section. Built with sm::pc_endpoint
// (SM_WITH_TLS, SM_WITH_SYNC and SM_WITH_RTP), so not in the Wasm build. Two endpoints on the emulated network, the
// signaling carried as SDP text (tests/unit/pc_endpoint/endpoint_world.h, as the unit tests run them). The budget is
// about 5-10x the Release time on an Apple M4.
#include <memory>

#include "../tests/unit/pc_endpoint/endpoint_world.h"
#include "sm/tls/ssl_identity.h"
#include "sm_bench.h"

using namespace sm;
using namespace sm::pc;

// A call set up from SDP: the offer and answer written, parsed and applied, trickled candidates, ICE and the DTLS
// handshake on the BUNDLE transport, until both endpoints are connected (DTLS-SRTP keys in place). The endpoints and
// their certificates are made outside the timed part (an application makes its certificate once, not per call).
SM_BENCH(p2p, Endpoint_CallSetup) {
  const std::shared_ptr<const tls::SslIdentity> certs[2] = {tls::SslIdentity::Create("a"),
                                                            tls::SslIdentity::Create("b")};
  int connected = 0;
  double virtual_ms = 0;
  for (auto _ : st) {
    st.PauseTiming();
    auto w = std::make_unique<test::EndpointWorld>();
    PeerEndpoint* ep[2];
    for (int i = 0; i < 2; ++i) {
      ep[i] = w->AddEndpoint(i == 0 ? "a" : "b", [&certs, i](PeerEndpointConfig& c) {
        c.certificate = certs[i];
        c.enable_data_channels = false;
      });
    }
    test::EndpointWorld::Link* link = w->Connect(ep[0], ep[1]);
    const Timestamp start = w->now;
    st.ResumeTiming();
    link->Offer(0, w->now);
    connected += w->RunUntil([&] { return ep[0]->IsConnected() && ep[1]->IsConnected(); }, TimeDelta::Seconds(10)) &&
                 link->ok;
    st.PauseTiming();
    virtual_ms += static_cast<double>((w->now - start).ms());
    w.reset();
    st.ResumeTiming();
  }
  st.Counter("connect_virtual_ms", virtual_ms / static_cast<double>(st.iterations()));
  st.Require("connected_fraction", static_cast<double>(connected) / static_cast<double>(st.iterations()), ">=", 1);
  // One call's set-up, both endpoints, audio and video on one BUNDLE transport (0.9 ms on an M4).
  st.BudgetNsPerItem(7e6);
}
