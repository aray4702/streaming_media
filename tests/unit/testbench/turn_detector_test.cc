// Unit tests: the AI endpoint's turn detector (#752, testbench/turn_detector.h).
// End of turn and barge-in on synthetic speech: clean, with concealed gaps
// (the fading repetition NetEq's expand plays for lost packets), and with a
// late burst (the end of an utterance delayed past an underrun), so that
// loss and delay move the detection as they would in a deployment.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "sm/sim/signals.h"
#include "sm_test.h"
#include "turn_detector.h"

using namespace sm;
using namespace sm::testbench;

namespace {

constexpr int kRate = 48000;
constexpr size_t kFrameSamples = kRate / 100;

std::vector<float> Silence(double seconds) { return std::vector<float>(static_cast<size_t>(seconds * kRate), 0.0f); }

// Continuous speech (no pauses) of `seconds`.
std::vector<float> Speech(double seconds, uint64_t seed) { return sim::SpeechLike(kRate, seconds, seed, false); }

void Append(std::vector<float>& x, const std::vector<float>& y) { x.insert(x.end(), y.begin(), y.end()); }

// Replaces `frames` 10 ms frames from `first_frame` with what NetEq's expand
// plays for lost packets: the last good frame repeated, 0.7x quieter each
// frame, so it fades to silence within about 150 ms.
void Conceal(std::vector<float>& x, size_t first_frame, size_t frames) {
  std::vector<float> last(x.begin() + static_cast<ptrdiff_t>((first_frame - 1) * kFrameSamples),
                          x.begin() + static_cast<ptrdiff_t>(first_frame * kFrameSamples));
  for (size_t f = first_frame; f < first_frame + frames; ++f) {
    for (auto& v : last) v *= 0.7f;
    std::copy(last.begin(), last.end(), x.begin() + static_cast<ptrdiff_t>(f * kFrameSamples));
  }
}

struct Run {
  std::vector<TurnEvent> events;
  TurnDetectorStats stats;
  std::vector<TurnEvent> Of(TurnEventType type) const {
    std::vector<TurnEvent> out;
    for (const auto& e : events)
      if (e.type == type) out.push_back(e);
    return out;
  }
};

Run Detect(const std::vector<float>& x, const std::function<bool(Timestamp)>& ai_speaking = nullptr,
           TurnDetector* reuse = nullptr) {
  TurnDetector local;
  TurnDetector& d = reuse ? *reuse : local;
  Run r;
  for (size_t i = 0; i + kFrameSamples <= x.size(); i += kFrameSamples) {
    const Timestamp now = Timestamp::Micros(static_cast<int64_t>(i) * 1000000 / kRate);
    auto e = d.Process(std::span<const float>(x.data() + i, kFrameSamples), now, ai_speaking ? ai_speaking(now) : false);
    if (e) r.events.push_back(*e);
  }
  r.stats = d.stats();
  return r;
}

int64_t Ms(Timestamp t) { return t.us() / 1000; }

// The clean turn: 1 s of silence, 2 s of speech, 2 s of silence.
std::vector<float> CleanTurn(uint64_t seed) {
  std::vector<float> x = Silence(1.0);
  Append(x, Speech(2.0, seed));
  Append(x, Silence(2.0));
  return x;
}

}  // namespace

SM_TEST(TurnDetector, CleanTurnStartsAtTheSpeechAndEndsAfterTheSilence) {
  for (uint64_t seed = 1; seed <= 5; ++seed) {
    const Run r = Detect(CleanTurn(seed));
    SM_ASSERT_EQ(r.events.size(), size_t{2});
    SM_EXPECT_TRUE(r.events[0].type == TurnEventType::kSpeechStart);
    SM_EXPECT_TRUE(r.events[1].type == TurnEventType::kEndOfTurn);
    // The utterance starts with the speech (the first frames are the syllable envelope's quiet part at most).
    SM_EXPECT_GE(Ms(r.events[0].speech_start), 1000);
    SM_EXPECT_LE(Ms(r.events[0].speech_start), 1100);
    // The start is decided one onset (100 ms) after it.
    SM_EXPECT_EQ(Ms(r.events[0].at) - Ms(r.events[0].speech_start), 100);
    // The speech ends at 3.0 s; the VAD's hangover (up to 140 ms) is inside last_speech.
    SM_EXPECT_GE(Ms(r.events[1].last_speech), 2950);
    SM_EXPECT_LE(Ms(r.events[1].last_speech), 3150);
    // The end of turn comes exactly the configured silence after the last active frame.
    SM_EXPECT_EQ(Ms(r.events[1].at) - Ms(r.events[1].last_speech), 600);
    SM_EXPECT_EQ(r.stats.invalid_frames, uint64_t{0});
    SM_EXPECT_EQ(r.stats.barge_ins, uint64_t{0});
  }
}

SM_TEST(TurnDetector, PauseShorterThanTheSilenceKeepsOneTurn) {
  std::vector<float> x = Silence(1.0);
  Append(x, Speech(1.0, 3));
  Append(x, Silence(0.3));
  Append(x, Speech(1.0, 4));
  Append(x, Silence(2.0));
  const Run r = Detect(x);
  SM_EXPECT_EQ(r.Of(TurnEventType::kSpeechStart).size(), size_t{1});
  const auto ends = r.Of(TurnEventType::kEndOfTurn);
  SM_ASSERT_EQ(ends.size(), size_t{1});
  SM_EXPECT_GE(Ms(ends[0].last_speech), 3250);  // After the second part (it ends at 3.3 s).
}

SM_TEST(TurnDetector, ShortConcealedGapKeepsTheTurnAndItsEnd) {
  // 200 ms of lost packets in the middle of the speech, concealed: one turn, ending when the clean one does, within
  // 100 ms (the concealment moves the VAD's adaptive noise model, which moves its decisions after the gap a little).
  for (uint64_t seed = 1; seed <= 5; ++seed) {
    const std::vector<float> clean = CleanTurn(seed);
    std::vector<float> x = clean;
    Conceal(x, 190, 20);  // 1.9-2.1 s.
    const Run rc = Detect(clean);
    const Run r = Detect(x);
    SM_EXPECT_EQ(r.Of(TurnEventType::kSpeechStart).size(), size_t{1});
    const auto ends = r.Of(TurnEventType::kEndOfTurn);
    SM_ASSERT_EQ(ends.size(), size_t{1});
    SM_EXPECT_NEAR(Ms(ends[0].at), Ms(rc.Of(TurnEventType::kEndOfTurn)[0].at), 100);
  }
}

SM_TEST(TurnDetector, LongConcealedGapEndsTheTurnEarlyAndSplitsIt) {
  // 1 s of lost packets (1.5-2.5 s): the concealment fades to silence, so the detector ends the turn inside the
  // gap, and the speech after it is a second turn. This is the false end of turn loss causes in a deployment.
  for (uint64_t seed = 1; seed <= 5; ++seed) {
    std::vector<float> x = CleanTurn(seed);
    Conceal(x, 150, 100);
    const Run r = Detect(x);
    const auto starts = r.Of(TurnEventType::kSpeechStart);
    const auto ends = r.Of(TurnEventType::kEndOfTurn);
    SM_ASSERT_EQ(starts.size(), size_t{2});
    SM_ASSERT_EQ(ends.size(), size_t{2});
    // The first end of turn is decided inside the gap, 600 ms after the concealment stopped looking like speech.
    SM_EXPECT_GT(Ms(ends[0].at), 2100);
    SM_EXPECT_LT(Ms(ends[0].at), 2500);
    SM_EXPECT_EQ(Ms(ends[0].at) - Ms(ends[0].last_speech), 600);
    // The second utterance starts when the packets come back.
    SM_EXPECT_GE(Ms(starts[1].speech_start), 2500);
    SM_EXPECT_LE(Ms(starts[1].speech_start), 2600);
  }
}

SM_TEST(TurnDetector, LateBurstShorterThanTheSilenceDelaysTheEndOfTurn) {
  // The utterance's last 400 ms arrive 300 ms late: the receiver plays 300 ms of underrun silence first. One
  // turn, ending the delay later than the clean one (within 100 ms: the VAD adapts to the silence in between).
  for (uint64_t seed = 1; seed <= 5; ++seed) {
    const std::vector<float> clean = CleanTurn(seed);
    const size_t split = static_cast<size_t>(2.6 * kRate);
    std::vector<float> x(clean.begin(), clean.begin() + static_cast<ptrdiff_t>(split));
    Append(x, Silence(0.3));
    x.insert(x.end(), clean.begin() + static_cast<ptrdiff_t>(split), clean.end());
    const Run rc = Detect(clean);
    const Run r = Detect(x);
    SM_EXPECT_EQ(r.Of(TurnEventType::kSpeechStart).size(), size_t{1});
    const auto ends = r.Of(TurnEventType::kEndOfTurn);
    SM_ASSERT_EQ(ends.size(), size_t{1});
    SM_EXPECT_NEAR(Ms(ends[0].at) - Ms(rc.Of(TurnEventType::kEndOfTurn)[0].at), 300, 100);
  }
}

SM_TEST(TurnDetector, LateBurstLongerThanTheSilenceSplitsTheTurn) {
  // The same burst 900 ms late: the detector ends the turn in the underrun (a false end of turn, and an AI would
  // start replying), then the burst starts a second turn.
  for (uint64_t seed = 1; seed <= 5; ++seed) {
    const std::vector<float> clean = CleanTurn(seed);
    const size_t split = static_cast<size_t>(2.6 * kRate);
    std::vector<float> x(clean.begin(), clean.begin() + static_cast<ptrdiff_t>(split));
    Append(x, Silence(0.9));
    x.insert(x.end(), clean.begin() + static_cast<ptrdiff_t>(split), clean.end());
    const Run r = Detect(x);
    const auto starts = r.Of(TurnEventType::kSpeechStart);
    const auto ends = r.Of(TurnEventType::kEndOfTurn);
    SM_ASSERT_EQ(starts.size(), size_t{2});
    SM_ASSERT_EQ(ends.size(), size_t{2});
    SM_EXPECT_LT(Ms(ends[0].at), 3500);  // Inside the underrun (2.6-3.5 s).
    SM_EXPECT_GE(Ms(starts[1].speech_start), 3500);
    SM_EXPECT_LE(Ms(starts[1].speech_start), 3600);
  }
}

SM_TEST(TurnDetector, UserSpeechWhileTheAiSpeaksIsABargeIn) {
  for (uint64_t seed = 1; seed <= 5; ++seed) {
    // The AI speaks from 0 to 4 s; the user starts at 1 s.
    const Run r = Detect(CleanTurn(seed), [](Timestamp t) { return t < Timestamp::Millis(4000); });
    const auto starts = r.Of(TurnEventType::kSpeechStart);
    const auto barges = r.Of(TurnEventType::kBargeIn);
    SM_ASSERT_EQ(barges.size(), size_t{1});
    SM_ASSERT_EQ(starts.size(), size_t{1});
    // Decided once the utterance has 200 ms of active frames.
    SM_EXPECT_EQ(Ms(barges[0].at) - Ms(barges[0].speech_start), 200);
    SM_EXPECT_EQ(Ms(barges[0].speech_start), Ms(starts[0].speech_start));
    SM_EXPECT_EQ(r.Of(TurnEventType::kEndOfTurn).size(), size_t{1});
  }
}

SM_TEST(TurnDetector, NoBargeInWhenTheAiIsSilentOrTheBlipIsShort) {
  // The same speech while the AI is silent: no barge-in.
  SM_EXPECT_EQ(Detect(CleanTurn(1), [](Timestamp) { return false; }).stats.barge_ins, uint64_t{0});
  // A 50 ms blip (a cough) while the AI speaks: with the VAD's hangover (8 frames) it can reach the onset, but it
  // stays below the barge-in minimum, so the AI keeps speaking.
  std::vector<float> x = Silence(1.0);
  Append(x, Speech(0.05, 9));
  Append(x, Silence(1.0));
  const Run r = Detect(x, [](Timestamp) { return true; });
  SM_EXPECT_EQ(r.stats.barge_ins, uint64_t{0});
  SM_EXPECT_LE(r.stats.speech_starts, uint64_t{1});
}

SM_TEST(TurnDetector, BargeInLongerThanTheOnsetCanCompleteWithTheStart) {
  // With barge_in_min_speech <= speech_onset, the barge-in is reported on the frame after the start.
  TurnDetectorConfig c;
  c.barge_in_min_speech = TimeDelta::Millis(50);
  TurnDetector d(c);
  const Run r = Detect(CleanTurn(2), [](Timestamp) { return true; }, &d);
  SM_ASSERT_GE(r.events.size(), size_t{2});
  SM_EXPECT_TRUE(r.events[0].type == TurnEventType::kSpeechStart);
  SM_EXPECT_TRUE(r.events[1].type == TurnEventType::kBargeIn);
  SM_EXPECT_EQ(Ms(r.events[1].at) - Ms(r.events[0].at), 10);
}

SM_TEST(TurnDetector, DeterministicAndResettable) {
  std::vector<float> x = CleanTurn(4);
  Conceal(x, 150, 100);
  auto ai = [](Timestamp t) { return t < Timestamp::Millis(2000); };
  const Run a = Detect(x, ai);
  const Run b = Detect(x, ai);
  TurnDetector d;
  Detect(x, ai, &d);
  d.Reset();
  const Run c = Detect(x, ai, &d);
  for (const Run* o : {&b, &c}) {
    SM_ASSERT_EQ(o->events.size(), a.events.size());
    for (size_t i = 0; i < a.events.size(); ++i) {
      SM_EXPECT_TRUE(o->events[i].type == a.events[i].type);
      SM_EXPECT_EQ(o->events[i].at.us(), a.events[i].at.us());
      SM_EXPECT_EQ(o->events[i].speech_start.us(), a.events[i].speech_start.us());
      SM_EXPECT_EQ(o->events[i].last_speech.us(), a.events[i].last_speech.us());
    }
    SM_EXPECT_EQ(o->stats.active_frames, a.stats.active_frames);
  }
}

SM_TEST(TurnDetector, WrongFrameLengthIsPassiveAndCounted) {
  TurnDetector d;
  std::vector<float> f(100, 0.1f);
  SM_EXPECT_FALSE(d.Process(f, Timestamp::Millis(0), false).has_value());
  SM_EXPECT_EQ(d.stats().invalid_frames, uint64_t{1});
  SM_EXPECT_EQ(d.stats().active_frames, uint64_t{0});
  SM_EXPECT_EQ(std::string(TurnEventName(TurnEventType::kEndOfTurn)), std::string("end_of_turn"));
}

// Issue #756: the AI's detections scored against the script. One user turn the
// AI never calls over (missed), one turn split by a concealed gap (a false end
// of turn mid-turn, then the right one), and a barge-in heard in time.
namespace {
TurnEvent Ev(TurnEventType type, int64_t speech_start_ms, int64_t last_speech_ms) {
  return TurnEvent{type, Timestamp::Millis(last_speech_ms + 600), Timestamp::Millis(speech_start_ms),
                   Timestamp::Millis(last_speech_ms)};
}
}  // namespace

SM_TEST(TurnDetectErrors, OneMissedAndOneFalse) {
  const std::vector<ScriptedSpeech> script = {
      {Timestamp::Millis(1000), Timestamp::Millis(4000), false},   // Split: false end at 2.5 s, then the right one.
      {Timestamp::Millis(10000), Timestamp::Millis(13000), false},  // Never called over: missed.
      {Timestamp::Millis(16000), Timestamp::Millis(17000), true},   // Barge-in, detected and ended in time.
  };
  const std::vector<TurnEvent> events = {
      Ev(TurnEventType::kSpeechStart, 1150, 1250),  // Not scored.
      Ev(TurnEventType::kEndOfTurn, 1150, 2500),
      Ev(TurnEventType::kEndOfTurn, 3300, 4120),
      Ev(TurnEventType::kBargeIn, 16080, 16300),
      Ev(TurnEventType::kEndOfTurn, 16080, 17090),
  };
  const TurnDetectErrors e = CountTurnDetectErrors(script, events);
  SM_EXPECT_EQ(e.missed_end_of_turn, 1);
  SM_EXPECT_EQ(e.false_end_of_turn, 1);
  SM_EXPECT_EQ(e.missed_barge_in, 0);
  SM_EXPECT_EQ(e.false_barge_in, 0);
  SM_EXPECT_EQ(e.missed(), 1);
  SM_EXPECT_EQ(e.false_detections(), 1);
  SM_EXPECT_EQ(e.total(), 2);
}

SM_TEST(TurnDetectErrors, BargeInsAndTolerance) {
  const std::vector<ScriptedSpeech> script = {
      {Timestamp::Millis(1000), Timestamp::Millis(3000), false},
      {Timestamp::Millis(6000), Timestamp::Millis(7000), true},
  };
  // All correct, at the edges of the window: 200 ms early, 1 s late.
  {
    const std::vector<TurnEvent> ok = {Ev(TurnEventType::kEndOfTurn, 1000, 4000), Ev(TurnEventType::kBargeIn, 5800, 6000),
                                       Ev(TurnEventType::kEndOfTurn, 5800, 6800)};
    SM_EXPECT_EQ(CountTurnDetectErrors(script, ok).total(), 0);
  }
  // A user turn taken for a barge-in is false; the scripted barge-in heard 1.2 s late is both missed and false;
  // a second end of turn for the same window is false.
  {
    const std::vector<TurnEvent> bad = {Ev(TurnEventType::kBargeIn, 1100, 1400), Ev(TurnEventType::kEndOfTurn, 1100, 3100),
                                        Ev(TurnEventType::kEndOfTurn, 1100, 3150), Ev(TurnEventType::kBargeIn, 7200, 7500),
                                        Ev(TurnEventType::kEndOfTurn, 7200, 7900)};
    const TurnDetectErrors e = CountTurnDetectErrors(script, bad);
    SM_EXPECT_EQ(e.false_barge_in, 2);
    SM_EXPECT_EQ(e.missed_barge_in, 1);
    SM_EXPECT_EQ(e.false_end_of_turn, 1);
    SM_EXPECT_EQ(e.missed_end_of_turn, 0);
  }
  // Nothing detected: every window's end of turn and every barge-in missed.
  const TurnDetectErrors none = CountTurnDetectErrors(script, {});
  SM_EXPECT_EQ(none.missed_end_of_turn, 2);
  SM_EXPECT_EQ(none.missed_barge_in, 1);
  SM_EXPECT_EQ(none.false_detections(), 0);
}
