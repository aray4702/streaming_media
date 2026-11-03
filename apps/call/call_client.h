// The calling app's C++ call controller (C14, #1116): one call through the signaling service (docs/deploy/signaling.md)
// with the library, for the iOS app (apps/ios/) and any other native client.
//
// - Signaling: the protocol the AI server (apps/ai_server/) and the web app (apps/web/) speak: join a room, the peer
//   that joins later offers, candidates trickle, bye / peer-left end the call. The transport is the host's: it hands
//   the controller every message it receives (OnSignal) and sends what `send_signal` gives it, so the iOS app can
//   use the platform's wss:// (URLSessionWebSocketTask) and a test the plain ws:// client of apps/net. An `ai-` room
//   is answered by the AI server.
// - STUN/TURN: the host GETs /turn and passes the body (SetTurnResponse); the AI server's ParseTurnResponse reads it
//   (coturn REST credentials). No secret is ever in the app.
// - Transport and media: pc::PeerEndpoint (ICE, DTLS-SRTP, the RTP session with its congestion control, pacer, NACK
//   and NetEq-style jitter buffer) over OS sockets (apps/net/os_sockets.h), as the AI server sets it up. Audio is Opus
//   (libopus, testbench/opus_*.h), 20 ms frames. Video (when built with SM_WITH_VIDEOTOOLBOX) is H.264 through
//   VideoToolbox (sm/video/videotoolbox_h264.h) at the congestion controller's video target; received frames are
//   decoded the same way and the latest rendered one is kept for the UI.
// - Audio processing: the library's capture chain at 48 kHz, in libwebrtc APM's order: high-pass filter, noise
//   suppressor analysis, AEC3 (with the played-out audio as its render signal), noise suppression, then the mute fade
//   (Session::ProcessAudioCapture). Not in it yet: AGC (AGC1/AGC2), which libwebrtc's default APM config enables.
// - Live stats (C8): Session::GetStats and pc::GetConnectionStats once a second, summarized (CallStats).
//
// Threads: the call runs on the host's call thread (Start, OnSignal, Step, Wait, Hangup, SetTurnResponse). The device
// side may come from other threads: PushCapturedAudio and PullPlayout (the audio thread), PushCameraFrame (the camera
// queue), TakeRemoteVideo, SetMuted, SetVideoEnabled, state() and stats() (the UI). The library itself reads no clock;
// the host passes the call thread's `now`.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "os_sockets.h"
#include "sm/core/units.h"
#include "sm/p2p/port_allocator.h"
#include "sm/video/frame.h"

namespace sm::pc {
class PeerEndpoint;
}
namespace sm::p2p {
class StaticNetworkManager;
class BasicPortAllocator;
}
namespace sm::testbench {
class OpusAudioDecoder;
class OpusSendEncoder;
}

namespace sm::apps {

enum class CallState { kIdle, kWaitingForPeer, kConnecting, kConnected, kEnded };
const char* CallStateName(CallState s);

struct CallClientConfig {
  std::string room;
  std::string peer_id;  // Empty: "ios-<random>".
  // Interfaces to gather on; empty: every IPv4 interface that is up and not loopback.
  std::vector<p2p::IpAddress> interfaces;
  int opus_bitrate_bps = 32000;
  bool audio_processing = true;  // The library's capture chain (above); off: the raw microphone.
  int video_fps = 30;
  bool verbose = false;          // Log to stderr.
};

// The UI's summary of the call, refreshed once a second.
struct CallStats {
  CallState state = CallState::kIdle;
  double connect_ms = -1;          // Start to ICE + DTLS-SRTP up.
  double duration_s = 0;
  std::string local_candidate;     // Selected pair: "relay (udp)", "srflx", "host", ...
  std::string remote_candidate;
  double rtt_ms = -1;
  double target_kbps = 0;          // The congestion controller's target.
  double send_kbps = 0, recv_kbps = 0;  // On the selected pair, over the last second.
  std::string dtls_cipher, srtp_cipher;
  uint64_t audio_packets_sent = 0, audio_packets_received = 0;
  int64_t audio_packets_lost = 0;
  double audio_jitter_ms = 0;
  double audio_concealed_pct = 0;
  double jitter_buffer_ms = 0;
  double audio_played_s = 0;       // Received audio played out, not concealed (NetEq lifetime stats).
  uint64_t video_frames_sent = 0, video_frames_received = 0, video_frames_decoded = 0, video_frames_rendered = 0;
  int video_send_width = 0, video_send_height = 0, video_recv_width = 0, video_recv_height = 0;
  double capture_level = 0;  // RMS of the processed microphone, 0 to 1.
  std::string ended_reason;
  std::string Text() const;  // Multi-line, for the UI.
  std::string ToJson() const;
};

class CallClient {
 public:
  CallClient(CallClientConfig config, std::function<void(const std::string&)> send_signal);
  ~CallClient();
  CallClient(const CallClient&) = delete;
  CallClient& operator=(const CallClient&) = delete;

  // The /turn response body; before Start. Without it: host candidates only.
  void SetTurnResponse(const std::string& json);
  // Joins the room.
  void Start(Timestamp now);
  // A message from the signaling service.
  void OnSignal(const std::string& text, Timestamp now);
  // The signaling connection dropped: the call ends unless it is already connected.
  void OnSignalingClosed(Timestamp now);
  // Endpoint, audio and video due by `now`. Returns when it next needs to run.
  Timestamp Step(Timestamp now);
  // Waits up to `wait` (at most 5 ms) for the call's sockets.
  void Wait(Timestamp now, TimeDelta wait);
  // Sends bye and ends the call.
  void Hangup(Timestamp now);

  // --- Device side (any thread) ---
  // 10 ms of microphone at 48 kHz mono; `total_delay_ms` is the device's playout + record delay (for AEC3).
  void PushCapturedAudio(std::span<const int16_t> samples, int total_delay_ms = 0);
  // Fills `out` with played-out audio at 48 kHz mono; returns the samples written (the rest is silence).
  size_t PullPlayout(std::span<int16_t> out);
  void PushCameraFrame(video::I420Frame frame);
  // The latest rendered remote frame since the last call, if any.
  bool TakeRemoteVideo(video::I420Frame* out);
  void SetMuted(bool muted) { muted_ = muted; }
  void SetVideoEnabled(bool on) { video_enabled_ = on; }
  bool muted() const { return muted_; }
  bool video_enabled() const { return video_enabled_; }

  CallState state() const { return state_; }
  CallStats stats() const;
  const std::string& peer_id() const { return config_.peer_id; }
  const std::string& remote_peer() const { return remote_; }

 private:
  class AudioProcessor;
  class Video;
  void Log(const std::string& line) const;
  void End(const std::string& why, Timestamp now);
  void SendSignal(const std::string& json);
  std::string To() const;
  void MakeEndpoint(Timestamp now);
  void Offer(Timestamp now);
  void AddCandidate(std::map<std::string, std::string>& m, Timestamp now);
  void Audio(Timestamp now);
  void UpdateStats(Timestamp now);

  CallClientConfig config_;
  std::function<void(const std::string&)> send_signal_;
  interop::OsSocketServer sockets_;
  std::unique_ptr<p2p::StaticNetworkManager> network_;
  std::unique_ptr<p2p::BasicPortAllocator> allocator_;
  p2p::ServerAddresses stun_;
  std::vector<p2p::RelayServerConfig> turn_;
  std::unique_ptr<testbench::OpusAudioDecoder> decoder_;
  std::unique_ptr<testbench::OpusSendEncoder> encoder_;
  std::unique_ptr<AudioProcessor> apm_;
  std::unique_ptr<Video> video_;
  std::unique_ptr<pc::PeerEndpoint> ep_;
  std::string remote_;
  std::vector<std::map<std::string, std::string>> pending_candidates_;
  std::atomic<CallState> state_{CallState::kIdle};
  Timestamp start_ = Timestamp::MinusInfinity();
  Timestamp next_stats_ = Timestamp::PlusInfinity();
  double connect_ms_ = -1;
  std::string ended_reason_;

  // Audio, call thread.
  std::vector<int16_t> send_buf_;  // Processed capture waiting for a 20 ms frame.
  uint32_t rtp_ts_ = 0;
  uint64_t audio_frames_sent_ = 0;
  double capture_level_ = 0;
  uint64_t last_pair_bytes_sent_ = 0, last_pair_bytes_received_ = 0;

  // Device side.
  mutable std::mutex audio_mu_;
  std::deque<int16_t> capture_ring_, playout_ring_;
  int device_delay_ms_ = 0;
  std::atomic<bool> muted_{false};
  std::atomic<bool> video_enabled_{true};
  mutable std::mutex stats_mu_;
  CallStats stats_;
};

}  // namespace sm::apps
