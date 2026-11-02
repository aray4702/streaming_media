// The ICE comparison's scenarios (issue #1004, compare/webrtc/ice_scenarios.h), this library's side, run natively:
// the NAT matrix connects with the candidate types of libwebrtc M157's P2PTransportChannelMatrixTest::kMatrix
// (p2p/base/p2p_transport_channel_unittest.cc), and a run is the same for the same seed. libwebrtc's side of each
// scenario runs in the compare build (sm_ice_compare).
#include <string>

#include "../../../compare/webrtc/ice_scenarios.h"
#include "ice_compare_relay.h"
#include "sm_test.h"
#include "test_crypto.h"

using namespace sm::ice_compare;

namespace {

// kMatrix's OPEN, CONE, ADDR, PORT and SYMM rows and columns (originator by row, receiver by column) as the
// controlling side's local/remote candidate types. The cells that need a relay (RUPU, PURU) are "" here: they wait for
// TURN in this comparison (C4), and without it the pair fails, as libwebrtc's does.
const char* const kMatrix[5][5] = {
    /*OP*/ {"host/host", "host/srflx", "host/srflx", "host/srflx", "host/prflx"},
    /*CO*/ {"srflx/host", "srflx/srflx", "srflx/srflx", "srflx/srflx", "srflx/prflx"},
    /*AD*/ {"srflx/host", "srflx/srflx", "srflx/srflx", "srflx/srflx", "srflx/prflx"},
    /*PO*/ {"srflx/host", "srflx/srflx", "srflx/srflx", "srflx/srflx", ""},
    /*SY*/ {"prflx/host", "prflx/srflx", "prflx/srflx", "", ""},
};

}  // namespace

SM_TEST(P2pIceCompare, NatMatrixMatchesLibwebrtc) {
  sm::p2p::testing::InstallTestCryptoWithUniqueIds();
  const Nat kNats[] = {Nat::kOpen, Nat::kFullCone, Nat::kAddrRestricted, Nat::kPortRestricted, Nat::kSymmetric};
  for (int a = 0; a < 5; ++a) {
    for (int b = 0; b < 5; ++b) {
      Scenario s;
      s.nat[0] = kNats[a];
      s.nat[1] = kNats[b];
      const Result r = RunSm(s, 1);
      const std::string expected = kMatrix[a][b];
      SM_EXPECT_EQ(r.connected, !expected.empty());
      SM_EXPECT_EQ(r.pair, expected);
      if (r.connected) {
        // Within libwebrtc's kMediumTimeout, after at least one check each way.
        SM_EXPECT_TRUE(r.connect_ms > 0 && r.connect_ms < 3000);
        SM_EXPECT_TRUE(r.checks_at_connect >= 2);
      }
    }
  }
}

SM_TEST(P2pIceCompare, ScenariosRunAndAreDeterministic) {
  sm::p2p::testing::InstallTestCryptoWithUniqueIds();
  for (const Scenario& s : Scenarios()) {
    if (s.loss == 0) continue;  // The matrix test covers the lossless ones.
    const Result a = RunSm(s, 2);
    const Result b = RunSm(s, 2);
    SM_EXPECT_TRUE(a.connected);
    SM_EXPECT_EQ(a.connected, b.connected);
    SM_EXPECT_EQ(a.connect_ms, b.connect_ms);
    SM_EXPECT_EQ(a.pair, b.pair);
    SM_EXPECT_EQ(a.checks_total, b.checks_total);
    SM_EXPECT_TRUE(a.checks_total >= a.checks_at_connect);
  }
}

// Recovery after a network change (#1004 part 2): with continual gathering the call moves to the new interface on
// either side (also from Wi-Fi to cellular), within the 30 s libwebrtc's P2PTransportChannelMultihomedTest failover
// tests wait; when the Wi-Fi link dies with cellular up from the start, it fails over to cellular even when gathering
// once, as TestFailoverControllingSide and TestFailoverControlledSide; when an interface is replaced with
// PeerConnection's default gathering (once) there is no candidate on the new interface, so it does not recover without
// an ICE restart. A run is the same for the same seed.
SM_TEST(P2pIceCompare, RecoversAfterNetworkChange) {
  sm::p2p::testing::InstallTestCryptoWithUniqueIds();
  for (const Scenario& s : ChangeScenarios()) {
    const Result a = RunSm(s, 1);
    const Result b = RunSm(s, 1);
    SM_EXPECT_TRUE(a.connected);
    const bool recovers = s.continual || s.dead;
    SM_EXPECT_EQ(a.recovered, recovers);
    if (recovers) {
      SM_EXPECT_TRUE(a.recover_ms > 0 && a.recover_ms < kRecoveryTime.us() / 1000.0);
      SM_EXPECT_EQ(a.pair_after, a.pair);  // The same NATs, so the same candidate types.
    }
    SM_EXPECT_EQ(a.recovered, b.recovered);
    SM_EXPECT_EQ(a.recover_ms, b.recover_ms);
    SM_EXPECT_EQ(a.pair_after, b.pair_after);
    SM_EXPECT_EQ(a.checks_total, b.checks_total);
  }
}

// Issue #1048: our side counted 16 checks in 10 s on the pairs that need a relay where libwebrtc counts 418, 32
// against 50 after connecting behind NATs, and took 1067 ms against 501 ms to connect port-port at 20% loss. The ping
// schedule was libwebrtc's; the comparison's test crypto was not: CounterBytes is an 8-bit counter, so its 12-byte
// transaction ids repeat after 64, and StunRequestManager::Send drops a check whose id is still pending. libwebrtc's
// side draws its ids from a real random source. With ids that don't repeat (InstallTestCryptoWithUniqueIds, as
// sm_ice_compare now installs) our side gives libwebrtc M157's values from #1044's table (CI compare-webrtc, 5 seeds),
// cell for cell.
SM_TEST(P2pIceCompare, ChecksMatchLibwebrtc) {
  sm::p2p::testing::InstallTestCryptoWithUniqueIds();
  struct Row {
    const char* name;
    double connect_ms;  // -1: does not connect.
    double checks_at_connect;
    double checks_total;
  };
  // libwebrtc's column of #1044's table, means of seeds 1-5.
  const Row kLibwebrtc[] = {
      {"open-open", 40, 2.0, 16.0},          {"open-cone", 88, 4.0, 33.0},
      {"cone-cone", 88, 4.0, 50.0},          {"port-port", 136, 6.0, 50.0},
      {"cone-sym", 136, 6.0, 53.0},          {"sym-addr", 232, 10.0, 53.0},
      {"port-sym", -1, 0.0, 418.0},          {"sym-sym", -1, 0.0, 418.0},
      {"cone-port-loss5", 165, 7.2, 50.0},   {"open-open-loss20", 270, 8.6, 26.2},
      {"cone-port-loss20", 434, 18.4, 61.0}, {"port-port-loss20", 501, 21.0, 63.4},
  };
  const std::vector<Scenario> scenarios = Scenarios();
  for (const Row& row : kLibwebrtc) {
    const Scenario* s = nullptr;
    for (const Scenario& c : scenarios) {
      if (c.name == row.name) s = &c;
    }
    SM_ASSERT_TRUE(s != nullptr);
    int connected = 0;
    double ms = 0, at_connect = 0, total = 0;
    for (int seed = 1; seed <= 5; ++seed) {
      const Result r = RunSm(*s, seed);
      if (r.connected) {
        ++connected;
        ms += r.connect_ms;
        at_connect += r.checks_at_connect;
      }
      total += r.checks_total;
    }
    SM_EXPECT_EQ(connected, row.connect_ms < 0 ? 0 : 5);
    if (connected > 0) {
      SM_EXPECT_NEAR(ms / connected, row.connect_ms, 1.0);
      SM_EXPECT_NEAR(at_connect / connected, row.checks_at_connect, 0.05);
    }
    SM_EXPECT_NEAR(total / 5, row.checks_total, 0.05);
  }
}

// Relay (#1055): with a TURN server over UDP, the NAT pairs that need a relay (kMatrix's RUPU and PURU cells), an open
// pair with relay only and sym-sym at 5% loss all connect, through a relay: the selected pair has a relay candidate and
// an endpoint's selected local candidate relays over UDP (relay to relay with relay only). Both endpoints allocate (a
// 401, then the allocation, 4 Allocates at least) and the relayed pair needs a permission; no ChannelBind or Refresh
// in 10 s (TurnEntry binds a channel only for media, and refreshes come minutes later). A run is the same for the same
// seed.
SM_TEST(P2pIceCompare, RelayScenariosConnectThroughTurn) {
  sm::p2p::testing::InstallTestCryptoWithUniqueIds();
  const char* const kNames[] = {"port-sym-turnudp", "sym-port-turnudp", "sym-sym-turnudp",
                                "open-open-turnudp-relayonly", "sym-sym-turnudp-loss5"};
  const std::vector<Scenario> scenarios = RelayScenarios();
  SM_ASSERT_EQ(scenarios.size(), std::size(kNames));
  for (size_t k = 0; k < scenarios.size(); ++k) {
    const Scenario& s = scenarios[k];
    SM_EXPECT_EQ(s.name, std::string(kNames[k]));
    for (int seed = 1; seed <= 3; ++seed) {
      const Result a = RunSm(s, seed);
      const Result b = RunSm(s, seed);
      SM_EXPECT_TRUE(a.connected);
      SM_EXPECT_TRUE(a.pair.find("relay") != std::string::npos);
      SM_EXPECT_TRUE(a.relay.find("udp") != std::string::npos);
      if (s.relay_only) {
        SM_EXPECT_EQ(a.pair, std::string("relay/relay"));
        SM_EXPECT_EQ(a.relay, std::string("udp/udp"));
      }
      SM_EXPECT_TRUE(a.turn[kAllocate] >= 4);
      SM_EXPECT_TRUE(a.turn[kCreatePermission] >= 1);
      SM_EXPECT_EQ(a.turn[kChannelBind], 0);
      SM_EXPECT_EQ(a.turn[kRefresh], 0);
      SM_EXPECT_EQ(a.connect_ms, b.connect_ms);
      SM_EXPECT_EQ(a.pair, b.pair);
      SM_EXPECT_EQ(a.checks_total, b.checks_total);
      for (int t = 0; t < kNumTurnRequests; ++t) SM_EXPECT_EQ(a.turn[t], b.turn[t]);
    }
  }
}

// Relay (#1055): libwebrtc M157's column of the relay table (CI compare-webrtc on #1056, means of seeds 1-5; our side
// gave the same values on every seed). The pair and relay columns are the controlling side's selected local/remote
// candidate types and each endpoint's relay protocol; at 5% loss sym-sym picks either side's relay by seed (3 of 5
// prflx/relay, 2 of 5 relay/prflx on both stacks).
SM_TEST(P2pIceCompare, RelayMatchesLibwebrtc) {
  sm::p2p::testing::InstallTestCryptoWithUniqueIds();
  struct Row {
    const char* name;
    double connect_ms;
    double checks_at_connect;
    double checks_total;
    double turn[kNumTurnRequests];  // Allocate, CreatePermission, ChannelBind, Refresh.
    const char* pair;               // On every seed, or "" when it varies by seed.
    const char* relay;
  };
  const Row kLibwebrtc[] = {
      {"port-sym-turnudp", 252, 12.0, 70.0, {4.0, 8.0, 0, 0}, "srflx/relay", "-/udp"},
      {"sym-port-turnudp", 252, 12.0, 65.0, {4.0, 8.0, 0, 0}, "prflx/relay", "-/udp"},
      {"sym-sym-turnudp", 252, 12.0, 67.0, {4.0, 10.0, 0, 0}, "prflx/relay", "-/udp"},
      {"open-open-turnudp-relayonly", 190, 4.0, 16.0, {4.0, 2.0, 0, 0}, "relay/relay", "udp/udp"},
      {"sym-sym-turnudp-loss5", 310, 14.4, 70.8, {4.8, 7.6, 0, 0}, "", ""},
  };
  const std::vector<Scenario> scenarios = RelayScenarios();
  for (const Row& row : kLibwebrtc) {
    const Scenario* s = nullptr;
    for (const Scenario& c : scenarios) {
      if (c.name == row.name) s = &c;
    }
    SM_ASSERT_TRUE(s != nullptr);
    int connected = 0, relay_on_ep1 = 0;
    double ms = 0, at_connect = 0, total = 0, turn[kNumTurnRequests] = {0, 0, 0, 0};
    for (int seed = 1; seed <= 5; ++seed) {
      const Result r = RunSm(*s, seed);
      if (r.connected) {
        ++connected;
        ms += r.connect_ms;
        at_connect += r.checks_at_connect;
        if (*row.pair) {
          SM_EXPECT_EQ(r.pair, std::string(row.pair));
          SM_EXPECT_EQ(r.relay, std::string(row.relay));
        }
        if (r.relay == "-/udp") ++relay_on_ep1;
      }
      total += r.checks_total;
      for (int t = 0; t < kNumTurnRequests; ++t) turn[t] += r.turn[t];
    }
    SM_EXPECT_EQ(connected, 5);
    SM_EXPECT_NEAR(ms / 5, row.connect_ms, 1.0);
    SM_EXPECT_NEAR(at_connect / 5, row.checks_at_connect, 0.05);
    SM_EXPECT_NEAR(total / 5, row.checks_total, 0.05);
    for (int t = 0; t < kNumTurnRequests; ++t) SM_EXPECT_NEAR(turn[t] / 5, row.turn[t], 0.05);
    if (!*row.pair) SM_EXPECT_EQ(relay_on_ep1, 3);
  }
}

// Relay over TCP (#1055 part 2): libwebrtc M157's column of the TURN over TCP rows (sm_ice_compare; the same on
// every seed, and our side gave the same values on every seed). The requests are counted as the TURN server receives
// them. The NAT pairs connect when the UDP ones do (the relayed remote candidate is in place before the first check
// succeeds); relay only connects 30 ms later than over UDP, the TCP connect to the server (one round trip).
SM_TEST(P2pIceCompare, TcpRelayMatchesLibwebrtc) {
  sm::p2p::testing::InstallTestCryptoWithUniqueIds();
  const ice_compare_relay::Row kLibwebrtc[] = {
      {"port-sym-turntcp", 252, 12.0, 68.0, {4.0, 7.0, 0, 0}, "srflx/relay", "-/tcp"},
      {"sym-port-turntcp", 252, 12.0, 63.0, {4.0, 7.0, 0, 0}, "prflx/relay", "-/tcp"},
      {"sym-sym-turntcp", 252, 12.0, 63.0, {4.0, 8.0, 0, 0}, "prflx/relay", "-/tcp"},
      {"open-open-turntcp-relayonly", 220, 4.0, 16.0, {4.0, 2.0, 0, 0}, "relay/relay", "tcp/tcp"},
  };
  const std::vector<Scenario> scenarios = StreamRelayScenarios();
  for (const ice_compare_relay::Row& row : kLibwebrtc) ice_compare_relay::ExpectMatches(scenarios, row);
}
