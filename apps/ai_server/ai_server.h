// The AI server (C12 part 1, #1103): a native process that answers the signaling service's `ai-` rooms with the
// library, as the AI side of the two-way call.
//
// - Connection: it connects to the signaling service (#1102's protocol, apps/signaling/server.py from #1110:
//   WebSocket at /ws, JSON text messages, relayed ones with "from") and registers as the AI with
//   {"type":"register-ai"}. For each {"type":"ai-call","room":R} it opens a
//   WebSocket of its own for that call, joins R as "ai-server-<n>" and sets the call up with a pc::PeerEndpoint
//   (#1089) over OS sockets (apps/net/os_sockets.h): the one that joins later sends the offer, as the protocol says,
//   so it usually offers. STUN and TURN come from the service's GET /turn (coturn REST credentials).
// - Audio in: the caller's Opus through the library (RTP session, NetEq-style jitter buffer) decoded with libopus
//   (testbench/opus_decoder.h). Every 10 ms of played-out audio goes to the turn detector (#752).
// - Audio out: at the end of the user's turn the reply comes from an AiReplySource (a scripted one by default),
//   generated on the test bench AI source's schedule (testbench/ai_source.h: first-chunk delay, speed, bursts) and
//   sent as Opus (libopus, testbench/opus_encoder.h), a 20 ms frame every 20 ms (silence between replies). A
//   barge-in (the user speaking over the reply) cancels the reply's frames not yet sent.
// - The audio clock (#1126): each 20 ms slot sends exactly one frame, the reply's frame due by that slot or silence,
//   so the RTP timestamps never run ahead of the send clock. The reply source runs on a worker thread, so a slow
//   model doesn't hold up the slots; the reply starts at the first slot after it is ready. This is libwebrtc's split:
//   ChannelSend::ProcessAndEncodeAudio (audio/channel_send.cc) takes the encoding off the audio capture thread so
//   that the 10 ms clock is never held up, and the RTP timestamp advances by the samples encoded.
// - Metrics: one JSON line per call (AiCallMetrics::ToJson) with the conversation metrics of milestone 2 as the AI
//   side sees them. They are the server's view, not the user's: the response delay runs from the end of the user's
//   speech as played out here to the reply's first frame sent (the uplink and downlink paths are not in it), and the
//   barge-in stop runs from the user's speech start as played out here to the reply's cancel.
//
// No video. One loop thread: the owner's loop calls Step(now) at the time it returns and Wait() in between; only the
// reply source's OnUserTurn runs on a worker thread. The library reads no clock; this app passes it the loop's.
#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "ai_source.h"
#include "os_sockets.h"
#include "sm/core/units.h"
#include "sm/p2p/port_allocator.h"
#include "turn_detector.h"
#include "websocket.h"

namespace sm::apps {

// Where a real model plugs in: what the AI says back.
class AiReplySource {
 public:
  virtual ~AiReplySource() = default;
  // The user's turn ended; `user_audio` is what the AI heard of it (48 kHz mono, as played out, from the detected
  // speech start). Returns the reply (48 kHz mono samples in [-1, 1]); empty: no reply. Runs on a worker thread
  // (#1126), so it may take as long as a model does without holding up the call's audio; the server never calls the
  // source from two threads at once.
  virtual std::vector<float> OnUserTurn(std::span<const float> user_audio, Timestamp now) = 0;
  // The user talked over the reply, which is cut. On the loop thread, after any OnUserTurn in progress has returned.
  virtual void OnBargeIn(Timestamp) {}
};

// Scripted replies: speech-like audio (sm::sim::SpeechLike) of the given lengths, one per turn, the last one
// repeated.
class ScriptedReplySource final : public AiReplySource {
 public:
  explicit ScriptedReplySource(std::vector<TimeDelta> lengths = {TimeDelta::Seconds(3)}) : lengths_(std::move(lengths)) {}
  std::vector<float> OnUserTurn(std::span<const float> user_audio, Timestamp now) override;

 private:
  std::vector<TimeDelta> lengths_;
  size_t turn_ = 0;
};

struct AiCallMetrics {
  std::string room;
  std::string peer;    // The caller's peer id.
  bool connected = false;
  double connect_ms = -1;   // ai-call to ICE + DTLS-SRTP up.
  double duration_s = 0;
  int user_turns = 0;       // Ends of the user's turn detected.
  int replies = 0;          // Replies started.
  int barge_ins = 0;
  std::vector<double> response_delay_ms;  // conv.response_delay, the server's view (see the header comment).
  std::vector<double> barge_in_stop_ms;   // User speech start (played out) to the reply cut.
  double user_speech_ms = 0;               // Played-out audio inside the user's utterances.
  double user_speech_lost_ms = 0;          // Of it, concealed (NetEq expand).
  int64_t audio_frames_sent = 0;
  int64_t reply_frames_sent = 0;
  int64_t audio_packets_received = 0;
  // The audio clock (#1126). How late the loop sent a 20 ms slot's frame, at most; and how far the RTP timestamps
  // ran ahead of the send clock (frames sent beyond one per slot), at most: 0 when each slot sends one frame.
  double send_late_max_ms = 0;
  double rtp_ahead_max_ms = 0;
  std::vector<double> reply_generation_ms;  // The reply source's time per turn (OnUserTurn, on the worker thread).
  std::string ToJson() const;
};

struct AiServerConfig {
  std::string signaling_url = "ws://127.0.0.1:8080/ws";
  // GET for STUN/TURN; empty: http://<signaling host:port>/turn; "none": no request (host candidates only).
  std::string turn_url;
  // Interfaces to gather on; empty: every IPv4 interface that is up and not loopback.
  std::vector<p2p::IpAddress> interfaces;
  // Relay only (#1121): TURN candidates only, as iceTransportPolicy "relay" (the allocator's CF_RELAY filter).
  bool relay_only = false;
  std::string metrics_path;  // JSON lines appended per call; empty: not written.
  testbench::AiSourceConfig ai_source;
  testbench::TurnDetectorConfig turn_detector;
  int opus_bitrate_bps = 32000;
  TimeDelta max_call = TimeDelta::Seconds(3600);
  // Makes each call's reply source; null: ScriptedReplySource with its default.
  std::function<std::unique_ptr<AiReplySource>()> reply_factory;
  bool verbose = false;  // Log to stderr.
};

class AiCall;

class AiServer {
 public:
  explicit AiServer(AiServerConfig config);
  ~AiServer();
  AiServer(const AiServer&) = delete;
  AiServer& operator=(const AiServer&) = delete;

  // Connects to the signaling service and registers; false if the URL is bad. Reconnects by itself after a drop.
  bool Start(Timestamp now);
  // Signaling, endpoints and audio due by `now`. Returns when it next needs to run.
  Timestamp Step(Timestamp now);
  // Waits up to `wait` for the calls' sockets (and at most 5 ms, so that the WebSockets are polled too).
  void Wait(Timestamp now, TimeDelta wait);
  // Ends every call, logging its metrics.
  void Shutdown(Timestamp now);

  bool registered() const { return registered_; }
  int active_calls() const { return static_cast<int>(calls_.size()); }
  const std::vector<AiCallMetrics>& finished_calls() const { return finished_; }
  const AiServerConfig& config() const { return config_; }

 private:
  friend class AiCall;
  void Log(const std::string& line) const;
  void Finish(AiCall& call, Timestamp now);
  void HandleControl(const std::string& message, Timestamp now);

  AiServerConfig config_;
  net::Url signaling_;
  interop::OsSocketServer sockets_;
  std::unique_ptr<net::WebSocketClient> control_;
  Timestamp next_reconnect_ = Timestamp::PlusInfinity();
  bool registered_ = false;
  int next_call_id_ = 1;
  std::vector<std::unique_ptr<AiCall>> calls_;
  std::vector<AiCallMetrics> finished_;
};

// STUN and TURN servers from a /turn response ({"urls":[...],"username":...,"credential":...}).
void ParseTurnResponse(const std::string& json, p2p::ServerAddresses* stun, std::vector<p2p::RelayServerConfig>* turn);
// Every IPv4 interface that is up and not loopback (getifaddrs); the calling app (apps/call, #1116) gathers on them too.
std::vector<p2p::IpAddress> LocalIpv4Interfaces();

}  // namespace sm::apps
