// The AI endpoint's turn detector (#752, part 2 of #3): end of the user's
// turn and barge-in, decided on the audio the AI endpoint plays out.
//
// A deployed AI agent decides when the user has finished speaking, and when
// the user talks over it, from the audio its receiver plays out: after
// NetEq's concealment, accelerate and preemptive expand, and after the
// jitter buffer's delay. So loss and delay move its decisions. A concealed
// gap can end a turn early (the concealment fades to silence), and a late
// burst of speech can split one turn in two. The test bench runs this
// detector on the AI side's played-out audio so that the conversation
// metrics (#753 response delay, #754 barge-in stop delay, #756 detection
// errors) see those effects. It is a test bench model of the AI side, not a
// library component: libwebrtc has no turn detector.
//
// The detector runs the library's VAD (`sm::audio::Vad`, libwebrtc's
// `webrtc::Vad` ported bit-exact, include/sm/audio/vad.h) on each 10 ms
// frame and adds three rules on top of its per-frame decision:
//
//  - **Speech start:** `speech_onset` of consecutive active frames start an
//    utterance. The utterance starts at the first of those frames, so the
//    onset delays the decision, not the recorded start. The VAD's hangover
//    counts as active, so a burst of a few tens of ms (50 ms in the tests)
//    can start an utterance; the barge-in minimum keeps such a cough from
//    interrupting the AI.
//  - **End of turn:** while in an utterance, `end_of_turn_silence` of
//    consecutive passive frames end the user's turn. The event is decided
//    at the end of the last of those frames; `last_speech` is the end of the
//    last active frame. The VAD's own hangover (vad_core.c `kOverHangMax*`:
//    8 frames after a short burst, 14 after more than 6 speech frames, in
//    `kVadNormal`) is inside the active frames, so the end of turn comes
//    `end_of_turn_silence` plus up to 140 ms after the speech stops.
//    Passive frames shorter than that (a pause between words) leave the
//    utterance open.
//  - **Barge-in:** while the AI is speaking (`ai_speaking`, which the AI
//    endpoint knows because it sends that audio), an utterance with
//    `barge_in_min_speech` of active frames is a barge-in. One per
//    utterance.
//
// The parameters below are the test bench's model of a voice agent's
// endpointing, not a libwebrtc value (stage 2, #3). Deployed agents use an
// end-of-turn silence of about 0.5-1 s and a barge-in minimum of about
// 0.2-0.3 s so that a cough or a backchannel ("mm-hm") is not taken as
// an interruption.
//
// Deterministic: the result is a function of the frames, their times and
// the `ai_speaking` flags only; `Reset()` returns to the initial state.
// Times are passed in as `Timestamp now`; it reads no clock.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "sm/audio/vad.h"
#include "sm/core/units.h"

namespace sm::testbench {

struct TurnDetectorConfig {
  // The played-out audio's rate (8, 16, 32 or 48 kHz, as the VAD takes).
  int sample_rate_hz = 48000;
  // The VAD's mode: libwebrtc's `webrtc::Vad` default.
  audio::Vad::Aggressiveness aggressiveness = audio::Vad::kVadNormal;
  // Consecutive active frames that start an utterance.
  TimeDelta speech_onset = TimeDelta::Millis(100);
  // Consecutive passive frames after speech that end the user's turn.
  TimeDelta end_of_turn_silence = TimeDelta::Millis(600);
  // Active frames in an utterance, while the AI speaks, that make it a barge-in.
  TimeDelta barge_in_min_speech = TimeDelta::Millis(200);
};

enum class TurnEventType { kSpeechStart, kEndOfTurn, kBargeIn };

const char* TurnEventName(TurnEventType type);

struct TurnEvent {
  TurnEventType type = TurnEventType::kSpeechStart;
  // When the detector decided: the end of the frame that completed the rule.
  Timestamp at = Timestamp::Micros(0);
  // The utterance's start (its first active frame).
  Timestamp speech_start = Timestamp::Micros(0);
  // The end of the utterance's last active frame so far.
  Timestamp last_speech = Timestamp::Micros(0);
};

struct TurnDetectorStats {
  uint64_t frames = 0;
  uint64_t active_frames = 0;
  uint64_t invalid_frames = 0;  // Wrong length, or the VAD returned an error.
  uint64_t speech_starts = 0;
  uint64_t end_of_turns = 0;
  uint64_t barge_ins = 0;
};

class TurnDetector {
 public:
  explicit TurnDetector(TurnDetectorConfig config = {});

  // One 10 ms frame of played-out audio in [-1, 1) that starts playing at
  // `now`. `ai_speaking` is whether the AI's own audio is playing during it.
  // Returns the event this frame completes, if any (at most one per frame:
  // a speech start and a barge-in can't complete on the same frame unless
  // `barge_in_min_speech` <= `speech_onset`, and then the barge-in is
  // reported on the next frame). A frame of the wrong length counts as
  // passive and in `invalid_frames`.
  std::optional<TurnEvent> Process(std::span<const float> frame, Timestamp now, bool ai_speaking);

  void Reset();

  bool in_utterance() const { return in_utterance_; }
  size_t frame_samples() const { return static_cast<size_t>(config_.sample_rate_hz / 100); }
  const TurnDetectorConfig& config() const { return config_; }
  const TurnDetectorStats& stats() const { return stats_; }

 private:
  TurnDetectorConfig config_;
  audio::Vad vad_;
  TurnDetectorStats stats_;
  bool in_utterance_ = false;
  bool barged_in_ = false;
  bool pending_barge_in_ = false;
  int64_t active_run_ = 0;   // Consecutive active frames.
  int64_t passive_run_ = 0;  // Consecutive passive frames.
  int64_t utterance_active_ = 0;  // Active frames in the current utterance.
  Timestamp run_start_ = Timestamp::Micros(0);
  Timestamp speech_start_ = Timestamp::Micros(0);
  Timestamp last_speech_ = Timestamp::Micros(0);
};

}  // namespace sm::testbench

namespace sm::testbench {

// The AI detector's decisions against the script (issue #756). `user` are the scripted user speech windows (turns
// and barge-ins) and `ai` the windows the AI actually spoke in (a reply cut by a barge-in ends there), both on the
// call's clock. A window expects an end of turn whose utterance's last speech (TurnEvent::last_speech) is within
// `tolerance` after the window's end; a window that starts while the AI speaks also expects a barge-in whose
// utterance started (TurnEvent::speech_start) within `tolerance` after the window's start. An expected decision with
// no such event is missed; an event that matches no expectation (or a second one for the same expectation) is false.
// The tolerance absorbs the network and jitter-buffer delay on the way to the AI.
struct TurnWindow {
  Timestamp start, end;
};
struct TurnDetectErrors {
  int missed_end_of_turn = 0, false_end_of_turn = 0;
  int missed_barge_in = 0, false_barge_in = 0;
  int total() const { return missed_end_of_turn + false_end_of_turn + missed_barge_in + false_barge_in; }
};
TurnDetectErrors CountTurnDetectErrors(const std::vector<TurnWindow>& user, const std::vector<TurnWindow>& ai,
                                       const std::vector<TurnEvent>& events, TimeDelta tolerance);

}  // namespace sm::testbench
