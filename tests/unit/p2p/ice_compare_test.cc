// The ICE comparison's scenarios (issue #1004, compare/webrtc/ice_scenarios.h), this library's side, run natively:
// the NAT matrix connects with the candidate types of libwebrtc M157's P2PTransportChannelMatrixTest::kMatrix
// (p2p/base/p2p_transport_channel_unittest.cc), and a run is the same for the same seed. libwebrtc's side of each
// scenario runs in the compare build (sm_ice_compare).
#include <string>

#include "../../../compare/webrtc/ice_scenarios.h"
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
