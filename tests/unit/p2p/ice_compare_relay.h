// The relay rows of the ICE comparison (issue #1055, compare/webrtc/ice_scenarios.h): libwebrtc M157's column of a
// relay table, and the check that this library's side gives it on seeds 1-5. Shared by the TCP rows
// (ice_compare_test.cc) and the TLS rows (ice_compare_tls_test.cc).
#pragma once

#include <string>
#include <vector>

#include "../../../compare/webrtc/ice_scenarios.h"
#include "sm_test.h"

namespace ice_compare_relay {

using namespace sm::ice_compare;

// One scenario's libwebrtc values, the same on every seed: means over seeds 1-5 of the connect time and the checks,
// the TURN requests (Allocate, CreatePermission, ChannelBind, Refresh), the controlling side's selected local/remote
// candidate types and each endpoint's relay protocol.
struct Row {
  const char* name;
  double connect_ms;
  double checks_at_connect;
  double checks_total;
  double turn[kNumTurnRequests];
  const char* pair;
  const char* relay;
};

inline void ExpectMatches(const std::vector<Scenario>& scenarios, const Row& row) {
  const Scenario* s = nullptr;
  for (const Scenario& c : scenarios) {
    if (c.name == row.name) s = &c;
  }
  SM_ASSERT_TRUE(s != nullptr);
  int connected = 0;
  double ms = 0, at_connect = 0, total = 0, turn[kNumTurnRequests] = {0, 0, 0, 0};
  for (int seed = 1; seed <= 5; ++seed) {
    const Result r = RunSm(*s, seed);
    if (r.connected) {
      ++connected;
      ms += r.connect_ms;
      at_connect += r.checks_at_connect;
      SM_EXPECT_EQ(r.pair, std::string(row.pair));
      SM_EXPECT_EQ(r.relay, std::string(row.relay));
    }
    total += r.checks_total;
    for (int t = 0; t < kNumTurnRequests; ++t) turn[t] += r.turn[t];
  }
  SM_EXPECT_EQ(connected, 5);
  SM_EXPECT_NEAR(ms / 5, row.connect_ms, 1.0);
  SM_EXPECT_NEAR(at_connect / 5, row.checks_at_connect, 0.05);
  SM_EXPECT_NEAR(total / 5, row.checks_total, 0.05);
  for (int t = 0; t < kNumTurnRequests; ++t) SM_EXPECT_NEAR(turn[t] / 5, row.turn[t], 0.05);
}

}  // namespace ice_compare_relay
