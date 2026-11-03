// transport.overhead_pct of a `transport ice` call is repeatable (issue #1109). The setup's DTLS handshake carries
// ECDSA signatures and a random certificate serial whose DER lengths vary from run to run, so a count that included
// the setup changed by a few bytes between two runs of the same build, and the observability-off check (C8, #988)
// saw it as a difference. The metric counts the media phase, which is repeatable.
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
