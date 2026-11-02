// Issue #988 (C8): the text log's compile-time and runtime levels, and the
// observability build switches.
#include <string>
#include <vector>

#include "sm/core/logging.h"
#include "sm/core/observability.h"
#include "sm_test.h"

using namespace sm;

namespace {
// Captures messages for one test and restores the default sink and level after it.
struct Capture {
  Capture() {
    SetLogSink([this](const LogMessage& m) {
      severities.push_back(m.severity);
      texts.push_back(m.text);
    });
  }
  ~Capture() {
    SetLogSink(nullptr);
    SetLogSeverity(LogCompiled(kDefaultLogSeverity) ? kDefaultLogSeverity : LogSeverity::kNone);
  }
  std::vector<LogSeverity> severities;
  std::vector<std::string> texts;
};
}  // namespace

SM_TEST(Logging, DefaultsToWarningsAndAbove) {
  if (!LogCompiled(LogSeverity::kWarning)) return;  // A build with warnings compiled out.
  SM_EXPECT_TRUE(GetLogSeverity() == LogSeverity::kWarning);
  Capture c;
  SM_LOG(kVerbose, "verbose %d", 1);
  SM_LOG(kInfo, "info %d", 2);
  SM_LOG(kWarning, "warning %d", 3);
  SM_LOG(kError, "error %s", "four");
  SM_ASSERT_EQ(c.texts.size(), 2u);
  SM_EXPECT_TRUE(c.severities[0] == LogSeverity::kWarning);
  SM_EXPECT_TRUE(c.texts[0] == "warning 3");
  SM_EXPECT_TRUE(c.texts[1] == "error four");
}

SM_TEST(Logging, RuntimeLevelSelectsMessages) {
  Capture c;
  if (LogCompiled(LogSeverity::kVerbose)) {
    SM_EXPECT_TRUE(SetLogSeverity(LogSeverity::kVerbose) == ObsStatus::kOk);
    SM_LOG(kVerbose, "v");
    SM_LOG(kInfo, "i");
    SM_EXPECT_EQ(c.texts.size(), 2u);
  }
  SM_EXPECT_TRUE(SetLogSeverity(LogSeverity::kNone) == ObsStatus::kOk);  // Off is always allowed.
  const size_t before = c.texts.size();
  SM_LOG(kError, "e");
  SM_EXPECT_EQ(c.texts.size(), before);
  SM_EXPECT_FALSE(LogEnabled(LogSeverity::kError));
}

SM_TEST(Logging, ACompiledOutLevelIsNotSupportedAndChangesNothing) {
  Capture c;
  for (int s = 0; s < 4; ++s) {
    const auto sev = static_cast<LogSeverity>(s);
    const LogSeverity before = GetLogSeverity();
    const ObsStatus st = SetLogSeverity(sev);
    if (s < SM_LOG_MIN_LEVEL) {
      SM_EXPECT_TRUE(st == ObsStatus::kNotSupported);
      SM_EXPECT_TRUE(GetLogSeverity() == before);
      SM_EXPECT_FALSE(LogEnabled(sev));
    } else {
      SM_EXPECT_TRUE(st == ObsStatus::kOk);
      SM_EXPECT_TRUE(GetLogSeverity() == sev);
      SM_EXPECT_TRUE(LogEnabled(sev));
    }
  }
}

SM_TEST(Logging, LongMessagesAreTruncated) {
  if (!LogCompiled(LogSeverity::kError)) return;
  Capture c;
  const std::string big(5000, 'x');
  SM_LOG(kError, "%s", big.c_str());
  SM_ASSERT_EQ(c.texts.size(), 1u);
  SM_EXPECT_EQ(c.texts[0].size(), 1023u);
}

SM_TEST(Observability, CompiledFeaturesMatchTheBuild) {
  constexpr ObservabilityBuild b = CompiledObservability();
  SM_EXPECT_EQ(b.metrics, SM_ENABLE_METRICS != 0);
  SM_EXPECT_EQ(b.events, SM_ENABLE_EVENTS != 0);
  SM_EXPECT_EQ(b.stats, SM_ENABLE_STATS != 0);
  SM_EXPECT_EQ(b.log_min_level, SM_LOG_MIN_LEVEL);
}
