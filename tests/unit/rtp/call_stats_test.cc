#include <optional>

#include "sm/rtp/call_stats.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::rtp;

namespace {
Timestamp Ms(int64_t ms) { return Timestamp::Millis(ms); }
}  // namespace

// call_stats2.cc UpdateAndReport: each report reaches the observers at once.
SM_TEST(CallStats, AReportIsGivenToTheObserversAtOnce) {
  CallStats cs;
  cs.EnsureStarted(Ms(0));
  SM_EXPECT_TRUE(!cs.Process(Ms(999)).has_value());
  auto u = cs.OnRttUpdate(25, Ms(100));
  SM_ASSERT_TRUE(u.has_value());
  SM_EXPECT_EQ(u->max_rtt_ms, 25);
  SM_EXPECT_EQ(u->avg_rtt_ms, 25);
  SM_EXPECT_EQ(cs.LastProcessedRtt(), 25);
}

// The observers get the largest report of the last 1.5 s, not the latest.
SM_TEST(CallStats, TheMaximumOfTheLastOneAndAHalfSecondsIsReported) {
  CallStats cs;
  cs.EnsureStarted(Ms(0));
  cs.OnRttUpdate(100, Ms(0));
  auto u = cs.OnRttUpdate(40, Ms(1000));
  SM_ASSERT_TRUE(u.has_value());
  SM_EXPECT_EQ(u->max_rtt_ms, 100);
  // 70 % of the old average (100) against 30 % of the new one ((100 + 40) / 2).
  SM_EXPECT_EQ(u->avg_rtt_ms, 91);
  // At 1.5 s the first report is exactly 1500 ms old: kept (now - time > 1500 removes).
  u = cs.OnRttUpdate(40, Ms(1500));
  SM_EXPECT_EQ(u->max_rtt_ms, 100);
  u = cs.OnRttUpdate(40, Ms(1501));
  SM_EXPECT_EQ(u->max_rtt_ms, 40);
}

// The 1 s repeating update expires old reports; with none left it reports
// nothing and resets the average.
SM_TEST(CallStats, TheRepeatingUpdateExpiresReports) {
  CallStats cs;
  cs.EnsureStarted(Ms(0));
  SM_EXPECT_EQ(cs.NextProcessTime().ms(), 1000);
  cs.OnRttUpdate(60, Ms(500));
  auto u = cs.Process(Ms(1000));
  SM_ASSERT_TRUE(u.has_value());
  SM_EXPECT_EQ(u->max_rtt_ms, 60);
  SM_EXPECT_EQ(cs.NextProcessTime().ms(), 2000);
  SM_EXPECT_TRUE(cs.Process(Ms(2000)).has_value());  // 1500 ms old: kept.
  SM_EXPECT_TRUE(!cs.Process(Ms(3000)).has_value());
  SM_EXPECT_EQ(cs.LastProcessedRtt(), -1);
  SM_EXPECT_EQ(cs.max_rtt_ms(), -1);
  // A new report starts a new average.
  auto v = cs.OnRttUpdate(30, Ms(3100));
  SM_EXPECT_EQ(v->avg_rtt_ms, 30);
}

// EnsureStarted only starts the task once.
SM_TEST(CallStats, EnsureStartedIsIdempotent) {
  CallStats cs;
  SM_EXPECT_TRUE(!cs.NextProcessTime().IsFinite());
  cs.EnsureStarted(Ms(10));
  cs.EnsureStarted(Ms(500));
  SM_EXPECT_EQ(cs.NextProcessTime().ms(), 1010);
}
