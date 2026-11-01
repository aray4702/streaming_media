// The `call one-way|two-way` directive (issue #743, part 1 of #2): one-way is
// the default; two-way parses and round-trips, and runs a user <-> AI call with
// audio (#746) and video (#747) in each direction.
#include <cmath>
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

// Issue #747: `two_way_video both|up|down` picks the directions that send video; a one-way call can't use it.
SM_TEST(TestbenchCallMode, TwoWayVideoDirections) {
  auto both = ParseScenario("call two-way\n");
  SM_ASSERT_TRUE(both.scenario.has_value());
  SM_EXPECT_TRUE(both.scenario->video_up && both.scenario->video_down);
  SM_EXPECT_TRUE(UnsupportedReason(*both.scenario).empty());
  SM_EXPECT_TRUE(FormatScenario(*both.scenario).find("two_way_video") == std::string::npos);
  for (const char* dir : {"up", "down"}) {
    const std::string text = std::string("call two-way\ntwo_way_video ") + dir + "\n";
    auto p = ParseScenario(text);
    SM_ASSERT_TRUE(p.scenario.has_value());
    SM_EXPECT_TRUE(p.scenario->video_up == (std::string(dir) == "up"));
    SM_EXPECT_TRUE(p.scenario->video_down == (std::string(dir) == "down"));
    SM_EXPECT_TRUE(FormatScenario(*p.scenario).find(std::string("two_way_video ") + dir + "\n") != std::string::npos);
  }
  SM_EXPECT_FALSE(ParseScenario("two_way_video sideways\n").scenario.has_value());
  auto one_way = ParseScenario("two_way_video up\n");
  SM_ASSERT_TRUE(one_way.scenario.has_value());
  SM_EXPECT_TRUE(UnsupportedReason(*one_way.scenario).find("call two-way") != std::string::npos);
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

// Issue #747: video both ways, each direction measured on its own; deterministic for a fixed seed.
SM_TEST(TestbenchCallMode, TwoWayVideoFlowsBothWays) {
  auto run = [](const char* dir) {
    auto parsed = ParseScenario(std::string("name tv\ncall two-way\ntwo_way_video ") + dir +
                                "\nduration 6s\nat 0s capacity=2500kbps delay=25ms\nreverse at 0s capacity=2500kbps delay=25ms\n");
    RunOptions opt;
    opt.timeseries = false;
    opt.library_metrics = false;
    return RunCall(*parsed.scenario, opt);
  };
  const Report both = run("both");
  for (const char* d : {"up.", "down."}) {
    SM_ASSERT_TRUE(both.metrics.count(std::string(d) + "video.frames_rendered") == 1);
    SM_EXPECT_TRUE(both.metrics.at(std::string(d) + "video.frames_rendered") > 100);
    SM_EXPECT_TRUE(both.metrics.count(std::string(d) + "sync.av_offset_p95_ms") == 1);
  }
  SM_EXPECT_TRUE(run("both").metrics == both.metrics);
  const Report up = run("up");
  SM_EXPECT_TRUE(up.metrics.count("up.video.frames_rendered") == 1);
  SM_EXPECT_TRUE(up.metrics.count("down.video.frames_rendered") == 0);
  SM_EXPECT_TRUE(up.metrics.count("down.audio.concealed_pct") == 1);
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

// Issue #751: scripted turns parse, round-trip and need a two-way call.
SM_TEST(TestbenchCallMode, TurnsParseAndRoundTrip) {
  const std::string text =
      "call two-way\nturn user 1s-4s\nturn ai after=800ms len=4s\nturn user 10s-12s\nturn ai after=500ms len=3s\n"
      "barge-in at 14s\nbarge-in at 16s len=500ms\n";
  auto p = ParseScenario(text);
  SM_ASSERT_TRUE(p.scenario.has_value());
  SM_ASSERT_TRUE(p.scenario->turns.size() == 6);
  SM_EXPECT_TRUE(p.scenario->turns[1].who == Scenario::Turn::Who::kAi);
  SM_EXPECT_TRUE(p.scenario->turns[1].after == sm::TimeDelta::Millis(800));
  SM_EXPECT_TRUE(p.scenario->turns[4].end == sm::TimeDelta::Seconds(15));  // A barge-in lasts 1 s by default.
  auto again = ParseScenario(FormatScenario(*p.scenario));
  SM_ASSERT_TRUE(again.scenario.has_value());
  SM_EXPECT_TRUE(again.scenario->turns == p.scenario->turns);
  SM_EXPECT_TRUE(UnsupportedReason(*p.scenario).empty());
  for (const char* bad : {"turn user 4s-1s\n", "turn user 1s\n", "turn ai after=1s len=2s\n", "turn user 0s-1s\nturn ai len=2s\n",
                          "barge-in 3s\n", "barge-in at 3s len=0s\n", "turn bot 0s-1s\n"}) {
    SM_EXPECT_FALSE(ParseScenario(std::string("call two-way\n") + bad).scenario.has_value());
  }
  auto one_way = ParseScenario("turn user 0s-1s\n");
  SM_ASSERT_TRUE(one_way.scenario.has_value());
  SM_EXPECT_FALSE(UnsupportedReason(*one_way.scenario).empty());
}

namespace {
Report RunTurns(const std::string& extra, uint64_t seed = 1) {
  auto parsed = ParseScenario("name turns\ncall two-way\nvideo off\nduration 12s\nseed " + std::to_string(seed) +
                              "\nat 0s capacity=2500kbps delay=50ms\nreverse at 0s capacity=2500kbps delay=50ms\n" + extra);
  RunOptions opt;
  opt.timeseries = false;
  opt.library_metrics = false;
  return RunCall(*parsed.scenario, opt);
}
}  // namespace

// Issue #751: an endpoint sends silence outside its turns, so its audio costs less than talking the whole call;
// the call is deterministic for a fixed seed.
SM_TEST(TestbenchCallMode, TurnsGateEachEndpointsSpeech) {
  const Report talk = RunTurns("");
  const std::string script = "turn user 1s-4s\nturn ai after=800ms len=3s\n";
  const Report turns = RunTurns(script);
  SM_EXPECT_TRUE(turns.metrics.at("up.net.audio_kbps") < talk.metrics.at("up.net.audio_kbps"));
  SM_EXPECT_TRUE(turns.metrics.at("down.net.audio_kbps") < talk.metrics.at("down.net.audio_kbps"));
  SM_EXPECT_TRUE(RunTurns(script).metrics == turns.metrics);
  SM_EXPECT_TRUE(turns.metrics.count("ai.turns") == 0);  // No ai_source: no source metrics.
}

// Issue #758 part 2: with `ai_source`, each AI turn's reply comes from the source, drained at real time.
SM_TEST(TestbenchCallMode, AiSourceDrivesTheAisTurns) {
  const Report r = RunTurns("ai_source speed=3x burst=1s gap=300ms first_chunk=400ms\nturn user 1s-3s\n"
                            "turn ai after=800ms len=2s\nturn user 6s-7s\nturn ai after=500ms len=2s\n");
  SM_EXPECT_TRUE(r.metrics.at("ai.turns") == 2);
  // The first frame of 20 ms at 3x is generated 6.667 ms after the first chunk's 400 ms.
  SM_EXPECT_TRUE(std::abs(r.metrics.at("ai.first_frame_delay_max_ms") - 406.667) < 0.01);
  SM_EXPECT_TRUE(r.metrics.at("ai.queue_ahead_max_ms") > 500);
  SM_EXPECT_TRUE(r.metrics.at("ai.underruns") == 0);
  SM_EXPECT_TRUE(r.metrics.at("down.audio.concealed_pct") < 1);
}
