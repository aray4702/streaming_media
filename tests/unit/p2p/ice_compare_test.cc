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
  sm::p2p::testing::InstallTestCrypto();
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
  sm::p2p::testing::InstallTestCrypto();
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
