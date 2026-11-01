// The `call one-way|two-way` directive (issue #743, part 1 of #2): one-way is
// the default and the only call the bench runs; two-way parses and round-trips
// but is reported as unsupported until #746 runs it.
#include <string>

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

SM_TEST(TestbenchCallMode, TwoWayIsUnsupportedUntilItRuns) {
  auto parsed = ParseScenario("call two-way\n");
  SM_ASSERT_TRUE(parsed.scenario.has_value());
  const std::string why = UnsupportedReason(*parsed.scenario);
  SM_EXPECT_FALSE(why.empty());
  SM_EXPECT_TRUE(why.find("two-way") != std::string::npos);
}
