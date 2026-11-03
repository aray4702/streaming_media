// transport.overhead_pct of a `transport ice` call is repeatable (issue #1109). The setup's DTLS handshake carries
// ECDSA signatures and a random certificate serial whose DER lengths vary from run to run, so a count that included
// the setup changed by a few bytes between two runs of the same build, and the observability-off check (C8, #988)
// saw it as a difference. The metric counts the media phase, which is repeatable.
#include <algorithm>
#include <algorithm>
#include <cmath>
#include <string>

#include "call.h"
#include "ice_transport.h"
#include "scenario.h"
#include "sm_test.h"

using namespace sm::testbench;

SM_TEST(TestbenchIceOverhead, RepeatableAcrossRuns) {
  if (!IceLink::Available()) return;  // A build without TLS (wasm) has no `transport ice`.
  // A short call, so a single byte of difference shows in the unrounded metric.
  auto parsed = ParseScenario(
      "name ice_overhead_repeatable\ntransport ice\ncompare off\nduration 2s\ncall two-way\nvideo off\n"
      "at 0s capacity=2500kbps delay=25ms queue=300ms\nreverse at 0s capacity=2500kbps delay=25ms queue=300ms\n");
  SM_ASSERT_TRUE(parsed.scenario.has_value());
  double first = -1;
  // Each run makes new certificates and a new DTLS handshake; with the setup counted, six runs with the same
  // handshake size were very unlikely.
  for (int run = 0; run < 6; ++run) {
    Report rep = RunCall(*parsed.scenario);
    SM_ASSERT_TRUE(rep.metrics.count("setup.connected") == 1);
    SM_EXPECT_TRUE(rep.metrics["setup.connected"] == 1);
    const double overhead = rep.metrics["transport.overhead_pct"];
    SM_EXPECT_TRUE(overhead > 0);
    if (run == 0) first = overhead;
    SM_EXPECT_TRUE(overhead == first);
  }
}

// The header extension ids on the wire are the ones the SDP negotiated (issue #1095). The bench's sessions used to
// send transport-cc as id 1 while the endpoints' SDP negotiated 3.
SM_TEST(TestbenchIceExtensions, WireIdsEqualNegotiated) {
  if (!IceLink::Available()) return;
  auto parsed = ParseScenario(
      "name ice_extension_ids\ntransport ice\ncompare off\nduration 2s\nvideo off\n"
      "at 0s capacity=2500kbps delay=25ms queue=300ms\nreverse at 0s capacity=2500kbps delay=25ms queue=300ms\n");
  SM_ASSERT_TRUE(parsed.scenario.has_value());
  constexpr int kNegotiatedTransportCc = 3;  // What PeerEndpoint's offer and answer carry (a=extmap:3 ...transport-wide-cc).
  int checked = 0, wrong = 0;
  RunOptions opt;
  opt.packet_sink = [&](const char* kind, bool, std::span<const uint8_t> b) {
    const std::string k = kind;
    if (k == "rtcp" || k == "padding" || b.size() < 16 || (b[0] & 0x10) == 0) return;
    const size_t ext = 12 + 4 * (b[0] & 0x0f);  // After the fixed header and the CSRCs.
    if (b.size() < ext + 4 || b[ext] != 0xBE || b[ext + 1] != 0xDE) return;
    const size_t end = std::min(b.size(), ext + 4 + 4 * ((size_t{b[ext + 2]} << 8) | b[ext + 3]));
    bool found = false;
    for (size_t i = ext + 4; i < end;) {
      if (b[i] == 0) { ++i; continue; }
      const int id = b[i] >> 4, len = (b[i] & 0x0f) + 1;
      if (id == 15) break;
      if (id == kNegotiatedTransportCc && len == 2) found = true;
      i += 1 + static_cast<size_t>(len);
    }
    ++checked;
    if (!found) ++wrong;
  };
  Report rep = RunCall(*parsed.scenario, opt);
  SM_EXPECT_TRUE(rep.metrics["setup.connected"] == 1);
  SM_EXPECT_TRUE(checked > 50);
  SM_EXPECT_TRUE(wrong == 0);
}
