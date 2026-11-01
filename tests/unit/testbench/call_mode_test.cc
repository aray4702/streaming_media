// The `call one-way|two-way` directive (issue #743, part 1 of #2): one-way is
// the default; two-way parses and round-trips, and runs a user <-> AI call with
// audio (#746) and video (#747) in each direction.
#include <cmath>
#include <string>

#include "call.h"
#include "scenario.h"
#include "turn_detector.h"
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

// Issue #755: conv.user_speech_lost_pct is the share of the user's speech the AI side concealed or never played. It is
// 0 on a clean link, follows an outage that falls inside the user's turn (500 ms of a 3 s turn) and ignores one
// outside it, and is reported only with user turns.
SM_TEST(TestbenchCallMode, UserSpeechLostPct) {
  const std::string script = "turn user 1s-4s\nturn ai after=800ms len=3s\n";
  const Report clean = RunTurns(script);
  SM_ASSERT_TRUE(clean.metrics.count("conv.user_speech_lost_pct") == 1);
  SM_EXPECT_TRUE(clean.metrics.at("conv.user_speech_lost_pct") == 0);
  SM_EXPECT_TRUE(RunTurns("").metrics.count("conv.user_speech_lost_pct") == 0);
  const double in_turn = RunTurns(script + "at 2s down\nat 2500ms up\n").metrics.at("conv.user_speech_lost_pct");
  SM_EXPECT_TRUE(in_turn > 15 && in_turn < 22);  // 500 ms of 3 s is 16.7%, plus the recovery after the outage.
  SM_EXPECT_TRUE(RunTurns(script + "at 6s down\nat 6500ms up\n").metrics.at("conv.user_speech_lost_pct") == 0);
  SM_EXPECT_TRUE(RunTurns(script + "at 0s loss=10%\n").metrics.at("conv.user_speech_lost_pct") > 0);
}

// Issue #755: the meter on a known pull pattern, 10 ms pulls at 48 kHz over a 100 ms speech window.
SM_TEST(TestbenchCallMode, SpeechLossMeterPattern) {
  using W = SpeechLossMeter::Window;
  {  // Every pull plays received audio: nothing lost.
    SpeechLossMeter m({W{480, 5280}});
    for (int64_t p = 480; p <= 9600; p += 480) m.OnPull(p, 480, 0);
    SM_EXPECT_TRUE(m.lost_pct() == 0);
  }
  {  // Two pulls in the window concealed (one Expand period of 960 samples, spread over them): 20%.
    SpeechLossMeter m({W{480, 5280}});
    for (int64_t p = 480; p <= 9600; p += 480) m.OnPull(p, 480, p == 2400 ? 960 : 0);
    SM_EXPECT_TRUE(std::abs(m.lost_pct() - 20) < 1e-9);
  }
  {  // 30 ms of the window never played (the playout position jumps past it): 30%.
    SpeechLossMeter m({W{480, 5280}});
    for (int64_t p = 480; p <= 9600; p += 480) m.OnPull(p >= 2400 ? p + 1440 : p, 480, 0);
    SM_EXPECT_TRUE(std::abs(m.lost_pct() - 30) < 1e-9);
  }
  {  // Speech after the last playout position is still in flight, not lost.
    SpeechLossMeter m({W{480, 5280}});
    for (int64_t p = 480; p <= 2880; p += 480) m.OnPull(p, 480, 0);
    SM_EXPECT_TRUE(m.lost_pct() == 0);
  }
}

// Issue #753: the AI answers its detected end of the user's turn, and the response delay is what the transport added.
SM_TEST(TestbenchCallMode, AiAnswersTheDetectedEndOfTurn) {
  const std::string script = "turn user 1s-3s\nturn ai after=800ms len=2s\nturn user 7s-9s\nturn ai after=500ms len=2s\n";
  const Report r = RunTurns(script);
  SM_EXPECT_TRUE(r.metrics.at("conv.responses") == 2);
  // 100 ms RTT, 20 ms frames and the jitter buffers on both sides: a few hundred ms, never negative.
  SM_EXPECT_TRUE(r.metrics.at("conv.response_delay_p50_ms") > 100);
  SM_EXPECT_TRUE(r.metrics.at("conv.response_delay_p95_ms") < 500);
  // More delay on the links adds about as much to the response.
  const Report slow = RunTurns(script + "at 0s delay=150ms\nreverse at 0s delay=150ms\n");
  const double added = slow.metrics.at("conv.response_delay_p50_ms") - r.metrics.at("conv.response_delay_p50_ms");
  SM_EXPECT_TRUE(added > 150 && added < 260);
  SM_EXPECT_TRUE(RunTurns(script).metrics == r.metrics);
  SM_EXPECT_TRUE(RunTwoWay(1).metrics.count("conv.responses") == 0);  // No turns, no conversation metrics.
}

// Issue #754: when the AI hears the user over its reply it stops sending its speech; its audio stops at the user's
// ear once what is already in the path has played.
SM_TEST(TestbenchCallMode, BargeInStopsTheAi) {
  const std::string base = "turn user 1s-3s\nturn ai after=500ms len=6s\n";
  const Report full = RunTurns(base);
  const Report cut = RunTurns(base + "barge-in at 5s len=1s\n");
  SM_EXPECT_TRUE(full.metrics.count("conv.barge_in_stop_ms") == 0);
  SM_EXPECT_TRUE(cut.metrics.at("conv.barge_ins") == 1);
  // Onset 100 ms, barge-in after 200 ms of speech, 50 ms each way, and the user's jitter buffer.
  SM_EXPECT_TRUE(cut.metrics.at("conv.barge_in_stop_ms") > 300 && cut.metrics.at("conv.barge_in_stop_ms") < 500);
  // The rest of the reply is not sent.
  SM_EXPECT_TRUE(cut.metrics.at("down.net.audio_kbps") < full.metrics.at("down.net.audio_kbps"));
  SM_EXPECT_TRUE(RunTurns(base + "barge-in at 5s len=1s\n").metrics == cut.metrics);
}

// Issue #756: the AI's decisions against the script, one missed and one false.
SM_TEST(TestbenchCallMode, TurnDetectErrorsCountMissedAndFalse) {
  auto ms = [](int64_t v) { return sm::Timestamp::Millis(v); };
  auto event = [&](TurnEventType type, int64_t start, int64_t last) {
    TurnEvent e;
    e.type = type;
    e.speech_start = ms(start);
    e.last_speech = ms(last);
    e.at = ms(last + 600);
    return e;
  };
  // User turns 1-4 s and 10-12 s; a barge-in at 6 s while the AI speaks 5-8 s.
  const std::vector<TurnWindow> user = {{ms(1000), ms(4000)}, {ms(6000), ms(7000)}, {ms(10000), ms(12000)}};
  const std::vector<TurnWindow> ai = {{ms(5000), ms(6300)}};
  std::vector<TurnEvent> ev = {event(TurnEventType::kEndOfTurn, 1060, 4070),   // turn 1 ends: matched
                               event(TurnEventType::kBargeIn, 6060, 6260),     // barge-in: matched
                               event(TurnEventType::kEndOfTurn, 6060, 7080),   // barge-in utterance ends: matched
                               event(TurnEventType::kEndOfTurn, 10060, 10900)};  // a gap mid-turn: false
  // Turn 3's real end is never detected: missed.
  const TurnDetectErrors e = CountTurnDetectErrors(user, ai, ev, sm::TimeDelta::Millis(500));
  SM_EXPECT_TRUE(e.missed_end_of_turn == 1);
  SM_EXPECT_TRUE(e.false_end_of_turn == 1);
  SM_EXPECT_TRUE(e.missed_barge_in == 0 && e.false_barge_in == 0);
  SM_EXPECT_TRUE(e.total() == 2);
  // No barge-in event for the barge-in: missed; one barge-in while the AI is silent: false.
  ev[1].speech_start = ms(2000);
  const TurnDetectErrors b = CountTurnDetectErrors(user, ai, ev, sm::TimeDelta::Millis(500));
  SM_EXPECT_TRUE(b.missed_barge_in == 1 && b.false_barge_in == 1);
}

// Issue #756: an up-link outage in the middle of the user's turn makes the AI call the turn over early.
SM_TEST(TestbenchCallMode, OutageMidTurnIsAFalseEndOfTurn) {
  const std::string script = "turn user 1s-5s\nturn ai after=500ms len=2s\nturn ai after=500ms len=2s\n";
  SM_EXPECT_TRUE(RunTurns(script).metrics.at("conv.turn_detect_errors") == 0);
  const Report r = RunTurns(script + "at 2s down\nat 3s up\n");
  SM_EXPECT_TRUE(r.metrics.at("conv.false_end_of_turn") >= 1);
}

// Issue #762: `ai_send_ahead` hands the source's frames to the library early, which sends them ahead within the
// target, so the user's jitter buffer holds a cushion; without it the AI is paced at real time as before.
SM_TEST(TestbenchCallMode, AiSendAheadBuildsACushion) {
  auto p = ParseScenario("call two-way\nai_source speed=3x\nai_send_ahead 500ms\n");
  SM_ASSERT_TRUE(p.scenario.has_value());
  SM_EXPECT_TRUE(p.scenario->ai_send_ahead == sm::TimeDelta::Millis(500));
  SM_EXPECT_TRUE(FormatScenario(*p.scenario).find("ai_send_ahead 500ms\n") != std::string::npos);
  SM_EXPECT_FALSE(UnsupportedReason(*ParseScenario("call two-way\nai_send_ahead 500ms\n").scenario).empty());
  const std::string script = "ai_source speed=3x\nturn user 1s-3s\nturn ai after=500ms len=4s\n";
  const Report rt = RunTurns(script);
  const Report ahead = RunTurns(script + "ai_send_ahead 500ms\n");
  SM_EXPECT_TRUE(ahead.metrics.at("down.audio.jb_delay_p95_ms") > rt.metrics.at("down.audio.jb_delay_p95_ms") + 200);
  SM_EXPECT_TRUE(ahead.metrics.at("ai.turns") == 1);
  SM_EXPECT_TRUE(RunTurns(script + "ai_send_ahead 500ms\n").metrics == ahead.metrics);
}
