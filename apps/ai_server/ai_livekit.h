// The AI server as a participant of a LiveKit room (C20, #1172): `sm_ai_server --livekit=...` joins one room through
// the native LiveKit client of apps/call (call_client.h, call_driver.h) and answers turns from the room's audio.
//
// - Joining: a token from the signaling service's /lk-token (identity `ai-server` unless set), the WebSocket to
//   LiveKit's /rtc, the publisher connection with the AI's microphone track (no camera) and the subscriber connection
//   that receives every participant (C18's renegotiation, C20's subscriber path).
// - Listening: the AI hears one participant at a time, the active speaker. Every remote audio track is decoded and
//   played out on its own (CallClient's remote audio sink, before the mix); each 10 ms, the turn detector gets the
//   10 ms of the participant it listens to. It listens to the room's active speaker (LiveKit's SpeakersChanged, from
//   the publishers' RFC 6464 levels; else the client's local meter) whenever that is another participant, and keeps
//   listening to the last one while the AI itself speaks or nobody does, so a barge-in and the end of a turn are heard
//   from the same voice.
// - Replying: the same conversation as a one-to-one call (AiConversation: the reply source on a worker thread, paced by
//   the test bench's AI source, cut by a barge-in), one 20 ms slot at a time, handed to the client as its microphone
//   (no capture processing: the reply is clean), which encodes it as Opus and publishes it to the room.
// - Metrics: the call's AiCallMetrics (room, connect time, turns, replies, response delay as the AI sees it, barge-ins),
//   plus who it listened to.
//
// One loop thread, as AiServer: Step(now) at the time it returns and Wait() in between.
#pragma once

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ai_server.h"
#include "call_driver.h"

namespace sm::apps {

struct AiLiveKitConfig {
  std::string token_url;     // The signaling service's http[s]://HOST:PORT/lk-token.
  std::string livekit_url;   // ws[s]://HOST:PORT; empty: the URL /lk-token returns, else the token service's origin.
  std::string room;
  std::string identity = "ai-server";
};

class AiLiveKitParticipant {
 public:
  // `server` gives the reply source, AI source, turn detector, Opus rate, interfaces, relay policy, /turn URL and the
  // metrics file, as for a one-to-one call.
  AiLiveKitParticipant(const AiServerConfig& server, AiLiveKitConfig config);
  ~AiLiveKitParticipant();

  // Gets the token and connects; false (with error()) if either fails.
  bool Start(Timestamp now);
  Timestamp Step(Timestamp now);
  void Wait(Timestamp now, TimeDelta wait);
  // Leaves the room and writes the metrics line.
  void Shutdown(Timestamp now);

  bool ended() const { return driver_->client().state() == CallState::kEnded; }
  const std::string& error() const { return driver_->error(); }
  const AiCallMetrics& metrics() const { return metrics_; }
  CallClient& client() { return driver_->client(); }
  // Who the AI listens to now, and everyone it has listened to.
  const std::string& listening_to() const { return listening_; }
  const std::vector<std::string>& listened() const { return listened_; }
  // A JSON line: the client's stats with the conversation's metrics and who it listened to.
  std::string StatusJson() const;

 private:
  struct Heard {
    std::vector<float> pcm;
    bool concealed = false;
  };
  void OnRemoteAudio(const std::string& identity, std::span<const float> pcm, bool concealed);
  void Listen();
  void Log(const std::string& line) const;

  AiServerConfig server_;
  AiLiveKitConfig config_;
  AiCallMetrics metrics_;
  std::unique_ptr<CallDriver> driver_;
  std::unique_ptr<AiConversation> conversation_;
  std::map<std::string, std::deque<Heard>> heard_;  // Each participant's played-out 10 ms frames not yet taken.
  std::string listening_;
  std::vector<std::string> listened_;
  Timestamp start_ = Timestamp::MinusInfinity();
  Timestamp next_pull_ = Timestamp::PlusInfinity();
  Timestamp next_send_ = Timestamp::PlusInfinity();
  bool finished_ = false;
};

}  // namespace sm::apps
