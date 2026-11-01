// The `call one-way|two-way` directive (issue #743, part 1 of #2): one-way is
// the default; two-way parses and round-trips, and since #746 runs an audio call
// both ways (video in a two-way call is reported as unsupported until #747).
#include <string>

#include "call.h"
#include "scenario.h"
#include "sm_test.h"

using namespace sm::testbench;

SM_TEST(TestbenchCallMode, OneWayByDefault) {
  auto parsed = ParseScenario("name plain\nduration 2s\n");
  SM_ASSERT_TRUE(parsed.scenario.has_value());
  SM_EXPECT_FALSE(parsed.scenario->two_way);
  SM_EXPECT_TRUE(UnsupportedReason(*parsed.scenario).empty());
  // The default is not written, so every existing scenario's canonical text is unchanged.
  SM_EXPECT_TRUE(FormatScenario(*parsed.scenario).find("call ") == std::string::npos);
}

SM_TEST(TestbenchCallMode, ParsesBothModes) {
  auto one = ParseScenario("call one-way\nduration 2s\n");
  SM_ASSERT_TRUE(one.scenario.has_value());
  SM_EXPECT_FALSE(one.scenario->two_way);
  auto two = ParseScenario("call two-way\nduration 2s\n");
  SM_ASSERT_TRUE(two.scenario.has_value());
  SM_EXPECT_TRUE(two.scenario->two_way);
  // `#` comments and case of the directive, as every other directive.
  auto commented = ParseScenario("CALL two-way  # both ends talk\n");
  SM_ASSERT_TRUE(commented.scenario.has_value());
  SM_EXPECT_TRUE(commented.scenario->two_way);
}

SM_TEST(TestbenchCallMode, RejectsBadValues) {
  for (const char* bad : {"call\n", "call two_way\n", "call TWO-WAY\n", "call both\n", "call one-way two-way\n"}) {
    auto parsed = ParseScenario(bad);
    SM_EXPECT_FALSE(parsed.scenario.has_value());
    SM_EXPECT_TRUE(parsed.error.find("usage: call one-way|two-way") != std::string::npos);
  }
}

SM_TEST(TestbenchCallMode, TwoWayRoundTrips) {
  auto parsed = ParseScenario("name conv\ncall two-way\nduration 5s\nat 0s capacity=1000kbps loss=5%\n");
  SM_ASSERT_TRUE(parsed.scenario.has_value());
  const std::string text = FormatScenario(*parsed.scenario);
  SM_EXPECT_TRUE(text.find("call two-way\n") != std::string::npos);
  auto again = ParseScenario(text);
  SM_ASSERT_TRUE(again.scenario.has_value());
  SM_EXPECT_TRUE(again.scenario->two_way);
  SM_EXPECT_TRUE(FormatScenario(*again.scenario) == text);
}

SM_TEST(TestbenchCallMode, TwoWayWithVideoIsUnsupportedUntil747) {
  auto parsed = ParseScenario("call two-way\n");
  SM_ASSERT_TRUE(parsed.scenario.has_value());
  const std::string why = UnsupportedReason(*parsed.scenario);
  SM_EXPECT_FALSE(why.empty());
  SM_EXPECT_TRUE(why.find("two-way") != std::string::npos);
  auto audio_only = ParseScenario("call two-way\nvideo off\n");
  SM_ASSERT_TRUE(audio_only.scenario.has_value());
  SM_EXPECT_TRUE(UnsupportedReason(*audio_only.scenario).empty());
}

namespace {
Report RunTwoWay(uint64_t seed) {
  auto parsed = ParseScenario("name tw\ncall two-way\nvideo off\nduration 12s\nseed " + std::to_string(seed) +
                              "\nat 0s capacity=1000kbps delay=30ms loss=2%\nreverse at 0s capacity=800kbps delay=20ms\n");
  RunOptions opt;
  opt.timeseries = false;
  opt.library_metrics = false;
  return RunCall(*parsed.scenario, opt);
}
}  // namespace

// Issue #746: each direction's audio reaches the other end, with its metrics as up.* and down.*.
SM_TEST(TestbenchCallMode, TwoWayAudioFlowsBothWays) {
  const Report r = RunTwoWay(1);
  for (const char* dir : {"up.", "down."}) {
    const std::string d = dir;
    SM_ASSERT_TRUE(r.metrics.count(d + "audio.concealed_pct") == 1);
    SM_EXPECT_TRUE(r.metrics.at(d + "audio.time_to_first_ms") < 500);
    SM_EXPECT_TRUE(r.metrics.at(d + "net.audio_kbps") > 30);
    SM_EXPECT_TRUE(r.metrics.at(d + "cc.target_avg_kbps") > 0);
    SM_EXPECT_TRUE(r.metrics.at(d + "rtcp.audio_report_blocks") > 0);
  }
  // Each direction's RTT is about the two links' delays, 30 + 20 ms.
  SM_EXPECT_TRUE(r.metrics.at("up.cc.rtt_p50_ms") >= 50 && r.metrics.at("up.cc.rtt_p50_ms") < 80);
  SM_EXPECT_TRUE(r.metrics.at("down.cc.rtt_p50_ms") >= 50 && r.metrics.at("down.cc.rtt_p50_ms") < 80);
  // Only the forward link loses packets: the up direction sees loss, the down direction none.
  SM_EXPECT_TRUE(r.metrics.at("up.net.loss_pct") > 0);
  SM_EXPECT_TRUE(r.metrics.at("down.net.loss_pct") == 0);
  // One-way names are not reported in a two-way call.
  SM_EXPECT_TRUE(r.metrics.count("audio.concealed_pct") == 0);
}

// Issue #746: a fixed seed gives the same call.
SM_TEST(TestbenchCallMode, TwoWayIsDeterministic) {
  const Report a = RunTwoWay(3), b = RunTwoWay(3);
  SM_EXPECT_TRUE(a.metrics == b.metrics);
  const Report c = RunTwoWay(4);
  SM_EXPECT_FALSE(a.metrics == c.metrics);
}

// Issue #746: --metrics lists the per-direction names.
SM_TEST(TestbenchCallMode, CatalogListsPerDirectionNames) {
  bool up = false, down = false;
  for (const auto& [name, meaning] : MetricCatalog()) {
    up = up || name == "up.audio.concealed_pct";
    down = down || name == "down.cc.rtt_p50_ms";
  }
  SM_EXPECT_TRUE(up);
  SM_EXPECT_TRUE(down);
}
