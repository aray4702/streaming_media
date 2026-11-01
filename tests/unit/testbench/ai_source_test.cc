// The AI endpoint's media source (testbench/ai_source.h; issue #4, #758 part 1): its generation schedule
// (speed, burst, gap, first_chunk), the real-time drain of its app-side queue, and the `ai_source`
// scenario directive.
#include <string>
#include <vector>

#include "ai_source.h"
#include "scenario.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::testbench;

namespace {

AiSourceConfig Config(double speed, int64_t burst_ms, int64_t gap_ms, int64_t first_chunk_ms) {
  AiSourceConfig c;
  c.speed = speed;
  c.burst = TimeDelta::Millis(burst_ms);
  c.gap = TimeDelta::Millis(gap_ms);
  c.first_chunk = TimeDelta::Millis(first_chunk_ms);
  return c;
}

// Runs one turn of `length_ms` from t = 1000 s, polling every `step_us`, and returns every frame sent.
std::vector<AiFrame> RunTurn(AiMediaSource& src, int64_t length_ms, int64_t step_us) {
  const Timestamp t0 = Timestamp::Seconds(1000);
  src.StartTurn(t0, TimeDelta::Millis(length_ms));
  std::vector<AiFrame> sent;
  for (Timestamp now = t0; src.active(); now += TimeDelta::Micros(step_us)) {
    for (const AiFrame& f : src.Poll(now)) sent.push_back(f);
  }
  return sent;
}

}  // namespace

SM_TEST(TestbenchAiSource, GenerationTimeOfAContinuousSource) {
  const AiSourceConfig c = Config(2, 0, 0, 400);
  SM_EXPECT_EQ(AiGenerationTime(c, TimeDelta::Zero()).us(), 400000);  // Nothing before the first chunk.
  SM_EXPECT_EQ(AiGenerationTime(c, TimeDelta::Millis(20)).us(), 410000);
  SM_EXPECT_EQ(AiGenerationTime(c, TimeDelta::Seconds(1)).us(), 900000);  // 1 s of media at 2x: 500 ms.
  SM_EXPECT_EQ(AiGenerationTime(Config(1, 0, 0, 0), TimeDelta::Seconds(3)).us(), 3000000);
}

SM_TEST(TestbenchAiSource, GenerationTimeOfBursts) {
  // #4's example: 1 s of audio per burst at 3x, 300 ms between bursts, 400 ms to the first chunk.
  const AiSourceConfig c = Config(3, 1000, 300, 400);
  SM_EXPECT_EQ(AiGenerationTime(c, TimeDelta::Seconds(1)).us(), 400000 + 333333);  // The first burst's end: no gap yet.
  SM_EXPECT_EQ(AiGenerationTime(c, TimeDelta::Millis(1020)).us(), 400000 + 340000 + 300000);  // After one pause.
  SM_EXPECT_EQ(AiGenerationTime(c, TimeDelta::Seconds(2)).us(), 400000 + 666667 + 300000);
  SM_EXPECT_EQ(AiGenerationTime(c, TimeDelta::Millis(2001)).us(), 400000 + 667000 + 600000);
}

SM_TEST(TestbenchAiSource, FastSourceIsSentAtRealTimeWithItsQueueAhead) {
  AiMediaSource src(Config(3, 0, 0, 400));
  const std::vector<AiFrame> sent = RunTurn(src, 1000, 1000);
  SM_ASSERT_EQ(sent.size(), 50u);
  const Timestamp t0 = Timestamp::Seconds(1000);
  // The first frame leaves as soon as it is generated (400 ms + 20 ms / 3), then one every 20 ms.
  for (size_t k = 0; k < sent.size(); ++k) {
    SM_EXPECT_EQ(sent[k].index, static_cast<int64_t>(k));
    SM_EXPECT_EQ((sent[k].send - t0).us(), 406667 + 20000 * static_cast<int64_t>(k));
    SM_EXPECT_TRUE(!sent[k].late);
    SM_EXPECT_LE(sent[k].generated, sent[k].send);
  }
  SM_EXPECT_EQ(src.stats().first_frame_delay->us(), 406667);
  SM_EXPECT_EQ(src.stats().underruns, 0);
  SM_EXPECT_EQ(src.stats().frames_generated, 50);
  SM_EXPECT_EQ(src.stats().frames_sent, 50);
  // The whole reply is generated at 733.3 ms, when frames 0-16 have been sent: frames 17-49 (660 ms) wait.
  SM_EXPECT_EQ(src.stats().max_queue_ahead.us(), 660000);
  SM_EXPECT_EQ(src.QueueAhead(t0 + TimeDelta::Micros(733333)).us(), 660000);
  SM_EXPECT_EQ(src.QueueAhead(t0 + TimeDelta::Millis(300)).us(), 0);  // Nothing generated yet.
}

SM_TEST(TestbenchAiSource, SlowSourceUnderrunsOnEveryFrame) {
  AiMediaSource src(Config(0.5, 0, 0, 0));
  const std::vector<AiFrame> sent = RunTurn(src, 200, 1000);
  SM_ASSERT_EQ(sent.size(), 10u);
  for (size_t k = 0; k < sent.size(); ++k) {
    SM_EXPECT_EQ(sent[k].send, sent[k].generated);  // Each frame leaves when it is generated, 40 ms apart.
    SM_EXPECT_EQ(sent[k].late, k > 0);
  }
  SM_EXPECT_EQ(src.stats().underruns, 9);
  SM_EXPECT_EQ(src.stats().max_queue_ahead.us(), 0);
}

SM_TEST(TestbenchAiSource, PausesBetweenBurstsUnderrunWhenTheBurstIsShort) {
  // 200 ms of media at 2x takes 100 ms, then a 300 ms pause: generation falls behind at each burst's start.
  AiMediaSource src(Config(2, 200, 300, 0));
  const std::vector<AiFrame> sent = RunTurn(src, 1000, 1000);
  SM_ASSERT_EQ(sent.size(), 50u);
  int64_t late = 0;
  for (const AiFrame& f : sent) {
    if (f.late) {
      ++late;
      SM_EXPECT_EQ(f.index % 10, 0);  // The first frame of a burst.
    }
  }
  SM_EXPECT_EQ(late, 4);
  SM_EXPECT_EQ(src.stats().underruns, 4);
  const Timestamp t0 = Timestamp::Seconds(1000);
  SM_EXPECT_EQ((sent[10].send - t0).us(), 410000);  // 220 ms of media at 2x plus one pause.
}

SM_TEST(TestbenchAiSource, LongBurstsKeepTheQueueAheadAcrossPauses) {
  // #4's example: 1 s bursts at 3x with 300 ms pauses stay ahead of real time, so nothing underruns.
  AiMediaSource src(Config(3, 1000, 300, 400));
  const std::vector<AiFrame> sent = RunTurn(src, 5000, 1000);
  SM_ASSERT_EQ(sent.size(), 250u);
  SM_EXPECT_EQ(src.stats().underruns, 0);
  SM_EXPECT_EQ(src.stats().first_frame_delay->us(), 406667);
  SM_EXPECT_GT(src.stats().max_queue_ahead.us(), 1000000);
  // The last frame leaves 4.98 s after the first: real time, whatever the bursts did.
  SM_EXPECT_EQ((sent.back().send - sent.front().send).us(), 4980000);
}

SM_TEST(TestbenchAiSource, ResultsDoNotDependOnHowOftenItIsPolled) {
  const AiSourceConfig c = Config(2.5, 700, 250, 300);
  AiMediaSource fine(c), coarse(c);
  const std::vector<AiFrame> a = RunTurn(fine, 3010, 1);
  const std::vector<AiFrame> b = RunTurn(coarse, 3010, 37000);
  SM_ASSERT_EQ(a.size(), b.size());
  SM_ASSERT_EQ(a.size(), 151u);
  for (size_t k = 0; k < a.size(); ++k) {
    SM_EXPECT_EQ(a[k].send, b[k].send);
    SM_EXPECT_EQ(a[k].generated, b[k].generated);
    SM_EXPECT_EQ(a[k].late, b[k].late);
  }
  SM_EXPECT_EQ(a.back().length.us(), 10000);  // 3010 ms: the last frame holds the last 10 ms.
  SM_EXPECT_EQ(fine.stats().max_queue_ahead, coarse.stats().max_queue_ahead);
  SM_EXPECT_EQ(fine.stats().underruns, coarse.stats().underruns);
}

SM_TEST(TestbenchAiSource, ANewTurnReplacesTheOneInProgress) {
  AiMediaSource src(Config(3, 0, 0, 400));
  const Timestamp t0 = Timestamp::Seconds(5);
  src.StartTurn(t0, TimeDelta::Seconds(2));
  SM_EXPECT_EQ(src.Poll(t0 + TimeDelta::Millis(500)).size(), 5u);  // Sent at 406.7, 426.7, ... 486.7 ms.
  const Timestamp t1 = t0 + TimeDelta::Millis(500);
  src.StartTurn(t1, TimeDelta::Millis(100));
  SM_EXPECT_EQ(*src.NextSendTime(), t1 + TimeDelta::Micros(406667));
  SM_EXPECT_TRUE(!src.stats().first_frame_delay.has_value());
  const std::vector<AiFrame> rest = src.Poll(t1 + TimeDelta::Seconds(2));
  SM_EXPECT_EQ(rest.size(), 5u);
  SM_EXPECT_EQ(rest.front().index, 0);
  SM_EXPECT_EQ(src.stats().turns, 2);
  SM_EXPECT_EQ(src.stats().first_frame_delay->us(), 406667);
  SM_EXPECT_TRUE(!src.active());
  SM_EXPECT_TRUE(!src.NextSendTime().has_value());
}

SM_TEST(TestbenchAiSource, ScenarioDirectiveParsesAndRoundTrips) {
  const auto parsed = ParseScenario("name ai\nai_source speed=3x burst=1s gap=300ms first_chunk=400ms\n");
  SM_ASSERT_TRUE(parsed.scenario.has_value());
  SM_ASSERT_TRUE(parsed.scenario->ai_source.has_value());
  SM_EXPECT_TRUE(*parsed.scenario->ai_source == Config(3, 1000, 300, 400));
  const std::string text = FormatScenario(*parsed.scenario);
  SM_EXPECT_TRUE(text.find("ai_source speed=3x burst=1s gap=300ms first_chunk=400ms\n") != std::string::npos);
  const auto again = ParseScenario(text);
  SM_ASSERT_TRUE(again.scenario.has_value());
  SM_EXPECT_TRUE(again.scenario->ai_source == parsed.scenario->ai_source);
  SM_EXPECT_EQ(FormatScenario(*again.scenario), text);

  // Every key is optional; a bare number is a speed too.
  const auto bare = ParseScenario("ai_source\n");
  SM_ASSERT_TRUE(bare.scenario.has_value());
  SM_EXPECT_TRUE(*bare.scenario->ai_source == AiSourceConfig{});
  const auto frame = ParseScenario("ai_source speed=1.5 frame=10ms\n");
  SM_ASSERT_TRUE(frame.scenario.has_value());
  SM_EXPECT_EQ(frame.scenario->ai_source->speed, 1.5);
  SM_EXPECT_EQ(frame.scenario->ai_source->frame.us(), 10000);
  SM_EXPECT_TRUE(FormatScenario(*frame.scenario).find("ai_source speed=1.5x frame=10ms\n") != std::string::npos);
}

SM_TEST(TestbenchAiSource, OneWayScenariosAreUnchanged) {
  const auto parsed = ParseScenario("name plain\nduration 10s\nat 0s capacity=1mbps\n");
  SM_ASSERT_TRUE(parsed.scenario.has_value());
  SM_EXPECT_TRUE(!parsed.scenario->ai_source.has_value());
  SM_EXPECT_TRUE(FormatScenario(*parsed.scenario).find("ai_source") == std::string::npos);
}

SM_TEST(TestbenchAiSource, ScenarioDirectiveRejectsBadValues) {
  for (const char* bad : {"ai_source speed=0\n", "ai_source speed=101x\n", "ai_source speed=fast\n",
                          "ai_source speed=2y\n", "ai_source burst=-1s\n", "ai_source gap=2h\n",
                          "ai_source frame=0ms\n", "ai_source frame=2s\n", "ai_source pace=2\n", "ai_source 3x\n",
                          "ai_source speed=2\nai_source speed=3\n"}) {
    const auto r = ParseScenario(bad);
    SM_EXPECT_TRUE(!r.scenario.has_value());
    SM_EXPECT_TRUE(r.error.find("ai_source") != std::string::npos);
  }
}
