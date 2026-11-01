#include "turn_detector.h"

namespace sm::testbench {

namespace {

constexpr TimeDelta kFrame = TimeDelta::Millis(10);

// A duration in whole 10 ms frames, rounded up, at least one.
int64_t Frames(TimeDelta d) {
  const int64_t n = (d.us() + kFrame.us() - 1) / kFrame.us();
  return n < 1 ? 1 : n;
}

}  // namespace

const char* TurnEventName(TurnEventType type) {
  switch (type) {
    case TurnEventType::kSpeechStart:
      return "speech_start";
    case TurnEventType::kEndOfTurn:
      return "end_of_turn";
    case TurnEventType::kBargeIn:
      return "barge_in";
  }
  return "?";
}

TurnDetector::TurnDetector(TurnDetectorConfig config) : config_(config), vad_(config.aggressiveness) {}

void TurnDetector::Reset() {
  vad_.Reset();
  stats_ = {};
  in_utterance_ = false;
  barged_in_ = false;
  pending_barge_in_ = false;
  active_run_ = 0;
  passive_run_ = 0;
  utterance_active_ = 0;
  run_start_ = Timestamp::Micros(0);
  speech_start_ = Timestamp::Micros(0);
  last_speech_ = Timestamp::Micros(0);
}

std::optional<TurnEvent> TurnDetector::Process(std::span<const float> frame, Timestamp now, bool ai_speaking) {
  ++stats_.frames;
  bool active = false;
  if (frame.size() != frame_samples()) {
    ++stats_.invalid_frames;
  } else {
    const audio::Vad::Activity a = vad_.VoiceActivity(frame, config_.sample_rate_hz);
    if (a == audio::Vad::kError) ++stats_.invalid_frames;
    active = a == audio::Vad::kActive;
  }
  const Timestamp frame_end = now + kFrame;
  auto event = [&](TurnEventType type) {
    return TurnEvent{type, frame_end, speech_start_, last_speech_};
  };

  if (active) {
    ++stats_.active_frames;
    if (active_run_ == 0) run_start_ = now;
    ++active_run_;
    passive_run_ = 0;
    last_speech_ = frame_end;
    if (in_utterance_) ++utterance_active_;
  } else {
    active_run_ = 0;
    ++passive_run_;
  }

  std::optional<TurnEvent> out;
  if (!in_utterance_) {
    if (active_run_ >= Frames(config_.speech_onset)) {
      in_utterance_ = true;
      barged_in_ = false;
      speech_start_ = run_start_;
      utterance_active_ = active_run_;
      ++stats_.speech_starts;
      out = event(TurnEventType::kSpeechStart);
    }
  } else if (passive_run_ >= Frames(config_.end_of_turn_silence)) {
    in_utterance_ = false;
    pending_barge_in_ = false;
    ++stats_.end_of_turns;
    return event(TurnEventType::kEndOfTurn);
  }

  if (in_utterance_ && !barged_in_ && ai_speaking && utterance_active_ >= Frames(config_.barge_in_min_speech)) {
    pending_barge_in_ = true;
  }
  if (pending_barge_in_ && !out) {
    pending_barge_in_ = false;
    barged_in_ = true;
    ++stats_.barge_ins;
    out = event(TurnEventType::kBargeIn);
  }
  return out;
}

TurnDetectErrors CountTurnDetectErrors(std::span<const ScriptedSpeech> script, std::span<const TurnEvent> events,
                                       TurnDetectErrorConfig config) {
  TurnDetectErrors out;
  std::vector<bool> ended(script.size(), false), barged(script.size(), false);
  // The first window not yet credited whose [edge - early, edge + late] holds `t`.
  auto match = [&](Timestamp t, bool barge_ins_only, std::vector<bool>& credited) -> bool {
    for (size_t i = 0; i < script.size(); ++i) {
      if (credited[i] || (barge_ins_only && !script[i].barge_in)) continue;
      const Timestamp edge = barge_ins_only ? script[i].start : script[i].end;
      if (t >= edge - config.early && t <= edge + config.late) {
        credited[i] = true;
        return true;
      }
    }
    return false;
  };
  for (const TurnEvent& e : events) {
    if (e.type == TurnEventType::kEndOfTurn) {
      if (!match(e.last_speech, false, ended)) ++out.false_end_of_turn;
    } else if (e.type == TurnEventType::kBargeIn) {
      if (!match(e.speech_start, true, barged)) ++out.false_barge_in;
    }
  }
  for (size_t i = 0; i < script.size(); ++i) {
    if (!ended[i]) ++out.missed_end_of_turn;
    if (script[i].barge_in && !barged[i]) ++out.missed_barge_in;
  }
  return out;
}

}  // namespace sm::testbench
