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

// Detection errors against the script (#756, part 6 of #3): how often the
// AI's end-of-turn and barge-in decisions disagree with what the user did.
//
// The script is what the user said: each window is a user turn or a
// barge-in, in the call's capture time. The AI hears each window a transport
// delay later (the network, the jitter buffer, concealment), so a detection
// is judged on the speech it describes, in the AI's playout time:
//
//  - **End of turn:** every user window (turn or barge-in) ends a turn the
//    AI should detect once. A detected end of turn is correct when its
//    `last_speech` falls in [end - early, end + late] of a window that has
//    no correct one yet. One that ends the speech early (a concealed gap or
//    an underrun split the turn) or late is false; a window without a
//    correct one is missed.
//  - **Barge-in:** every barge-in window should be detected once. A detected
//    barge-in is correct when its `speech_start` falls in
//    [start - early, start + late] of a barge-in window without one yet;
//    otherwise it is false (the AI took a user turn, or a split one, as an
//    interruption). A barge-in window without one is missed (the AI talked
//    on, or had already stopped because its reply started late).
//
// A detection the transport delays by more than `late` is both a miss and a
// false detection: the AI acted on it, too late to be the right one.
// Speech-start events are not scored.
struct ScriptedSpeech {
  Timestamp start = Timestamp::Micros(0);
  Timestamp end = Timestamp::Micros(0);
  bool barge_in = false;  // A scripted barge-in rather than a user turn.
};

struct TurnDetectErrorConfig {
  // How much earlier than the script a detection's speech may start or end:
  // the VAD's 10 ms frames and quiet speech edges it calls passive. The
  // transport only ever makes the AI hear later.
  TimeDelta early = TimeDelta::Millis(200);
  // How much later: the transport delay (one way, plus the jitter buffer)
  // up to which a detection still counts as the right one, and the VAD's
  // hangover (up to 140 ms in kVadNormal) inside an end of turn's
  // `last_speech`. A conversation whose AI hears the user more than about
  // 1 s late has failed that turn (program.md 6.3: 500 ms of added delay
  // is sev:2).
  TimeDelta late = TimeDelta::Millis(1000);
};

struct TurnDetectErrors {
  int missed_end_of_turn = 0;
  int false_end_of_turn = 0;
  int missed_barge_in = 0;
  int false_barge_in = 0;
  int missed() const { return missed_end_of_turn + missed_barge_in; }
  int false_detections() const { return false_end_of_turn + false_barge_in; }
  int total() const { return missed() + false_detections(); }
};

// Scores the detector's `events` (in the order it reported them) against
// the user's scripted speech.
TurnDetectErrors CountTurnDetectErrors(std::span<const ScriptedSpeech> script, std::span<const TurnEvent> events,
                                       TurnDetectErrorConfig config = {});

}  // namespace sm::testbench
