// The test bench's RTCP schedule follows the scenario seed (issue #399), as
// libwebrtc's RTCPSender seeds its report-interval draws from the clock
// (compare/webrtc/reference/modules/rtp_rtcp/source/rtcp_sender.cc:130) and
// sm_compare starts that clock per seed.
#include <cstdint>
#include <vector>

#include "call.h"
#include "scenario.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::testbench;

namespace {

// Send times of the RTCP packets on the forward link (the sender's reports):
// an audio-only call has no cross traffic and no send-rate-dependent minimum
// interval, so only the interval draws move them.
std::vector<int64_t> SenderRtcpTimes(uint64_t seed) {
  auto parsed = ParseScenario(
      "name rtcp_seed\nduration 40s\nvideo off\nat 0s capacity=1000kbps delay=20ms\nseed " + std::to_string(seed) + "\n");
  SM_EXPECT_TRUE(parsed.scenario.has_value());
  std::vector<int64_t> t;
  if (!parsed.scenario) return t;
  RunOptions opt;
  opt.link_trace = true;
  const Report rep = RunCall(*parsed.scenario, opt);
  for (const auto& r : rep.link_trace)
    if (r.kind == sim::LinkKind::kOther) t.push_back(r.time_us);
  return t;
}

}  // namespace

SM_TEST(TestbenchRtcpSeed, ScheduleFollowsScenarioSeed) {
  const auto a = SenderRtcpTimes(1);
  const auto a2 = SenderRtcpTimes(1);
  const auto b = SenderRtcpTimes(2);
  // Audio only: the first report at half the 5 s nominal interval, then draws in [2.5, 7.5] s.
  SM_EXPECT_TRUE(a.size() >= 4);
  SM_EXPECT_TRUE(b.size() >= 4);
  SM_EXPECT_TRUE(a == a2);  // Same seed, same schedule.
  SM_EXPECT_TRUE(a != b);   // Another seed draws another schedule.
}

SM_TEST(TestbenchRtcpSeed, CompareClockStartParses) {
  auto parsed = ParseScenario("name clock\ncompare_clock_start 107919\nduration 2s\n");
  SM_ASSERT_TRUE(parsed.scenario.has_value());
  SM_EXPECT_EQ(parsed.scenario->compare_clock_start_s, 107919);
  SM_EXPECT_EQ(ParseScenario("duration 2s\n").scenario->compare_clock_start_s, 0);  // sm_compare derives it from the seed.
  SM_EXPECT_TRUE(!ParseScenario("compare_clock_start 0\n").scenario.has_value());
  SM_EXPECT_TRUE(!ParseScenario("compare_clock_start 1000001\n").scenario.has_value());
}
