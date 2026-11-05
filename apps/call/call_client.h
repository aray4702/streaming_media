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
//   (libopus, testbench/opus_*.h), 20 ms frames, at the rate the congestion controller allocates to audio: the stream
//   registers [opus_min, opus_max] with the bitrate allocator (32 kbps both by default, as WebRtcAudioSendStream::
//   UpdateAllowedBitrateRange, webrtc_voice_engine.cc:1195-1216), and each allocation is clamped to that range plus
//   the packet overhead and handed to Opus less the overhead (AudioSendStream::OnBitrateUpdated, audio_send_stream.cc:
//   502-523; AudioEncoderOpusImpl::OnReceivedUplinkAllocation and SetTargetBitrate, audio_encoder_opus.cc:505-528,
//   716-737: 6 to 510 kbps). Video (when built with SM_WITH_VIDEOTOOLBOX) is H.264 through VideoToolbox
//   (sm/video/videotoolbox_h264.h) at the congestion controller's video target, with the camera frame's rotation in
//   the video-orientation extension (CVO); received frames are decoded the same way, turned upright by the rotation
//   they arrived with (as libwebrtc's renderers apply VideoFrame::rotation), and the latest one is kept for the UI.
// - Audio processing: the library's capture chain at 48 kHz, in libwebrtc APM's order (AudioProcessingImpl::
//   ProcessCaptureStreamLocked): high-pass filter, noise suppressor analysis, AEC3 (with the played-out audio as its
//   render signal), noise suppression, AGC, then the mute fade (Session::ProcessAudioCapture). The AGC is AGC1's
//   GainControl in kFixedDigital with APM's GainController1 defaults (3 dBFS target, 9 dB compression gain, limiter
//   on): the mode WebRtcVoiceEngine::ApplyOptions picks on iOS and Android (webrtc_voice_engine.cc:740-749), fed the
//   render's lowest band as APM does without AgcManagerDirect. That is the library's default (AEC3, as libwebrtc's
//   desktop APM), used on macOS, by the AI server and by sm_call_smoke on macOS. On iOS the app does what
//   libwebrtc's iOS client does (#1124): WebRtcVoiceEngine::ApplyOptions turns the software AEC and AGC off under
//   WEBRTC_IOS for VoiceProcessingIO's built-in ones (webrtc_voice_engine.cc:633-656), and keeps the high-pass filter
//   and the noise suppressor (the iOS ADM has no built-in NS, :714-725). ApplyIosAudioOptions sets that chain; the
//   device then runs VoiceProcessingIO's processing (sm::device EchoCanceller::kSystem, its AGC on as
//   voice_processing_audio_unit.mm checks it).
// - Live stats (C8): Session::GetStats and pc::GetConnectionStats once a second, summarized (CallStats).
// - LiveKit (C19, #1164; docs/deploy/livekit.md), with `livekit` set: the call is a LiveKit room instead. The host
//   connects the WebSocket to livekit::JoinPath (apps/call/livekit_proto.h) with a token from the signaling service's
//   /lk-token, sends what `send_signal` gives it as binary frames and hands every frame it receives to OnSignal.
//   LiveKit gives every client two peer connections, as its SDKs have them:
//   - the publisher (`ep_`): after the JoinResponse, AddTrackRequest for the microphone and the camera (their cids are
//     the a=msid track ids), then, once both are published (TrackPublishedResponse), our offer; the server's answer
//     and its trickled candidates (target PUBLISHER) complete it. The JoinResponse's ICE servers (the host's coturn)
//     are added to /turn's. Pings (ping_req) every ping_interval keep the session.
//   - the subscriber (`sub_`, C20 #1172): the server offers it and we answer (target SUBSCRIBER). Its first offer
//     carries only LiveKit's data channels; each participant who joins or publishes later brings a re-offer with its
//     tracks (and one who leaves, a re-offer that makes them inactive), which C18's renegotiation applies on the one
//     connection: every audio and video section is received on a recvonly transceiver of its own (OnTrack /
//     OnRemoveTrack). A track is mapped to its participant by its msid (LiveKit's stream id "PA_<sid>|TR_<sid>", the
//     track id the track sid) and the participants' TrackInfo from the JoinResponse and ParticipantUpdates.
//   Media of a group: each remote audio track decodes with its own Opus decoder (the endpoint's decoder factory), and
//   the played-out audio is the sum of every remote stream's 10 ms (up to 5; libwebrtc's AudioMixerImpl and its
//   limiter are not in compare/webrtc/reference, so this is a plain saturating mix); each remote video track decodes
//   with its own VideoToolbox decoder, and its latest frame is kept per participant (TakeParticipantVideo). Video
//   subscriptions ask for `livekit_receive_width` x `livekit_receive_height` (UpdateTrackSettings), from which the
//   server picks a simulcast layer, as the SDKs' adaptive stream does for a tile of that size.
//   Active speaker: LiveKit's SpeakersChanged (from the RFC 6464 audio levels the publishers send; ours are written
//   by the RTP sender) gives each participant's level and whether it is active; before the first update, a local
//   meter on each participant's played-out audio (-40 dBFS, 500 ms hangover) stands in. Mute and video off are
//   announced with MuteTrackRequest (so the others show them), and a mute from the server is applied.
//   Hangup sends a LeaveRequest. A server leave ends the call.
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

#include "livekit_proto.h"
#include "os_dns_resolver.h"
#include "os_sockets.h"
#include "sm/core/units.h"
#include "sm/p2p/port_allocator.h"
#include "sm/video/frame.h"
#include "sm/video/orientation.h"

namespace sm::pc {
class PeerEndpoint;
struct RemoteTrack;
}
namespace sm::p2p {
class StaticNetworkManager;
class BasicPortAllocator;
}
namespace sm::rtp {
class Session;
}
namespace sm::testbench {
class OpusAudioDecoder;
class OpusSendEncoder;
}

namespace sm::apps {

enum class CallState { kIdle, kWaitingForPeer, kConnecting, kConnected, kEnded };
const char* CallStateName(CallState s);

// The Opus rate for the session's current audio allocation, as AudioSendStream::OnBitrateUpdated clamps it to the
// stream's registered [min, max] (with the packet overhead) and AudioEncoderOpusImpl::OnReceivedUplinkAllocation
// takes the overhead off and clamps it to [6, 510] kbps. `max_bitrate_bps` is the registered payload maximum, which
// gives the overhead. Empty before the stream has an allocation.
std::optional<int> OpusRateFromAllocation(const rtp::Session& session, int max_bitrate_bps);

struct CallClientConfig {
  std::string room;
  std::string peer_id;  // Empty: "ios-<random>".
  // Interfaces to gather on; empty: every IPv4 interface that is up and not loopback.
  std::vector<p2p::IpAddress> interfaces;
  // The Opus payload range the stream registers with the bitrate allocator (AudioSendStream::Config::min_bitrate_bps,
  // max_bitrate_bps): libwebrtc's default is 32 kbps for both, so the allocation never moves it; a wider range (as
  // the WebRTC-Audio-Allocation field trial's min_bitrate / max_bitrate set it) lets the allocation move the encoder.
  int opus_min_bitrate_bps = 32000;
  int opus_max_bitrate_bps = 32000;
  bool audio_processing = true;  // The library's capture chain (above); off: the raw microphone.
  bool echo_cancellation = true;  // AEC3 in that chain (APM's echo_canceller.enabled).
  bool agc = true;               // The AGC in that chain (APM's gain_controller1.enabled).
  int video_fps = 30;
  // Relay only (#1121): gather and use TURN candidates only, as RTCConfiguration's iceTransportPolicy "relay"
  // (PeerConnection sets the allocator's candidate filter to CF_RELAY).
  bool relay_only = false;
  bool verbose = false;          // Log to stderr.
  // LiveKit (C19, above): the signaling is LiveKit's (binary protobuf messages), not the service's JSON.
  bool livekit = false;
  // Subscribe to the room's tracks (the join's auto_subscribe and the token's canSubscribe). Off: publish only.
  bool livekit_subscribe = true;
  bool livekit_publish_video = true;  // Publish a camera track (else the microphone only).
  int livekit_video_width = 640, livekit_video_height = 360;  // The camera size announced in AddTrackRequest.
  // The size each remote video is shown at (UpdateTrackSettings; 0: no settings sent, the server's default layer).
  int livekit_receive_width = 640, livekit_receive_height = 360;
};

// A remote participant of a LiveKit room (C20, #1172), for the grid: who it is, what it publishes, its speaker state
// and its receive stats (refreshed once a second; the speaker state ten times a second).
struct GroupParticipant {
  std::string identity, name, sid;
  bool has_audio = false, has_video = false;      // A track of that kind is being received.
  bool audio_muted = true, video_muted = true;    // Not published, or published muted (TrackInfo.muted).
  double audio_level = 0;                         // 0 to 1: LiveKit's speaker level, else the local meter's.
  bool speaking = false;                          // An active speaker.
  uint64_t audio_packets_received = 0;
  int64_t audio_packets_lost = 0;
  double audio_jitter_ms = 0, audio_concealed_pct = 0, audio_played_s = 0;
  uint64_t video_frames_received = 0, video_frames_decoded = 0;
  int video_width = 0, video_height = 0;
  double video_fps = 0;  // Decoded, over the last second.
  std::string ToJson() const;
};

// The capture chain of libwebrtc's iOS client (#1124): WebRtcVoiceEngine::ApplyOptions under WEBRTC_IOS sets
// echo_cancellation and auto_gain_control false (VoiceProcessingIO provides both), and the high-pass filter and the
// noise suppressor stay on. The host pairs it with sm::device EchoCanceller::kSystem.
void ApplyIosAudioOptions(CallClientConfig* config);

// The UI's summary of the call, refreshed once a second.
struct CallStats {
  CallState state = CallState::kIdle;
  double connect_ms = -1;          // Start to ICE + DTLS-SRTP up.
  double duration_s = 0;
  std::string local_candidate;     // Selected pair: "relay udp via udp 1.2.3.4:5", "srflx udp ...", "host udp ...".
  std::string remote_candidate;
  // The selected pair's candidate types (host, srflx, prflx, relay), its protocol (udp, tcp) and, for a local relay
  // candidate, the protocol to the TURN server (udp, tcp, tls), as RTCIceCandidateStats reports them.
  std::string local_type, remote_type, protocol, relay_protocol;
  bool relay_only = false;
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
  double agc_gain_db = 0;    // The AGC's digital gain at the end of the last frame.
  std::string capture_chain;  // The capture chain's stages: "hpf,ns,aec3,agc" (default), "hpf,ns" (iOS), "off".
  double opus_kbps = 0;      // The rate the Opus encoder is set to.
  double audio_allocation_kbps = 0;  // The congestion controller's audio allocation (with overhead).
  int video_send_rotation = 0, video_recv_rotation = 0;  // Degrees clockwise: sent and received in CVO.
  std::string ended_reason;
  // LiveKit (C19): the room, the other participants, our published tracks and the subscriber connection's state.
  std::string livekit_room;
  int livekit_participants = -1;  // Others in the room; -1: not a LiveKit call.
  int livekit_published = 0;
  std::string livekit_subscriber;
  double livekit_signal_rtt_ms = -1;  // ping_req / pong_resp
  // C20: the remote participants, the loudest active speaker (our own identity when it is us; empty: nobody), every
  // identity that has been an active speaker, and who joined and left since we joined.
  std::vector<GroupParticipant> livekit_group;
  std::string livekit_identity, livekit_active_speaker;
  double livekit_local_level = 0;
  std::vector<std::string> livekit_speakers_seen, livekit_joined, livekit_left;
  std::string Text() const;  // Multi-line, for the UI.
  std::string ToJson() const;

 private:
  std::string LiveKitJson() const;
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
  // Waits up to `wait` (at most 5 ms) for the call's sockets. `now` is the clock at this call: packets are stamped
  // from it (OsSocketServer::Poll, #1168).
  void Wait(Timestamp now, TimeDelta wait);
  // Sends bye and ends the call.
  void Hangup(Timestamp now);

  // --- Device side (any thread) ---
  // 10 ms of microphone at 48 kHz mono; `total_delay_ms` is the device's playout + record delay (for AEC3).
  void PushCapturedAudio(std::span<const int16_t> samples, int total_delay_ms = 0);
  // Fills `out` with played-out audio at 48 kHz mono; returns the samples written (the rest is silence).
  size_t PullPlayout(std::span<int16_t> out);
  // A camera frame and the clockwise turn that shows it upright (CapturedFrame::rotation), sent in CVO.
  void PushCameraFrame(video::I420Frame frame, video::Rotation rotation = video::Rotation::k0);
  // The latest rendered remote frame since the last call, if any, already turned upright. In a group: the first
  // remote video track's.
  bool TakeRemoteVideo(video::I420Frame* out);
  // A group (C20): the latest frame of `identity`'s video since the last call, if any, turned upright.
  bool TakeParticipantVideo(const std::string& identity, video::I420Frame* out);
  // A group: the remote participants (in the order they joined) and the active speaker, as in stats() but fresher.
  std::vector<GroupParticipant> participants() const;
  std::string active_speaker() const;
  // A group, before Start: each remote participant's 10 ms of played-out audio (48 kHz, [-1, 1]) before the mix, on
  // the call thread, with the jitter buffer's operation (the AI server listens to the active speaker, #1172).
  using RemoteAudioSink =
      std::function<void(const std::string& identity, std::span<const float> pcm, bool concealed, Timestamp at)>;
  void SetRemoteAudioSink(RemoteAudioSink sink) { remote_audio_sink_ = std::move(sink); }
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
  // LiveKit (C19).
  struct LiveKit;
  void OnLiveKit(const std::string& bytes, Timestamp now);
  void LiveKitPublish(Timestamp now);
  void LiveKitSubscriberOffer(const std::string& sdp, uint32_t id, Timestamp now);
  void LiveKitTrickle(const std::string& candidate_init, bool subscriber, Timestamp now);
  void MakeSubscriber(Timestamp now);
  void LiveKitSpeakers(const std::vector<livekit::SpeakerInfo>& speakers);
  void LiveKitAnnounceMute();
  void OnRemoteTrack(const pc::RemoteTrack& t, bool added);
  // The identity of a remote track's participant; empty while unknown.
  std::string TrackIdentity(const std::string& track_id, const std::vector<std::string>& stream_ids) const;
  void RefreshGroup(Timestamp now);
  // A group's playout: every remote audio track's 10 ms, mixed, into `pcm`.
  void MixGroupAudio(std::span<float> pcm, Timestamp now);
  // The endpoint whose audio plays and whose video renders: the subscriber with LiveKit, else the one call.
  pc::PeerEndpoint* receiver() const;

  CallClientConfig config_;
  std::function<void(const std::string&)> send_signal_;
  interop::OsSocketServer sockets_;
  // Resolves the remote mDNS (.local) candidates a browser signals (#1152); outlives the endpoint.
  interop::OsDnsResolverFactory resolver_;
  std::unique_ptr<p2p::StaticNetworkManager> network_;
  std::unique_ptr<p2p::BasicPortAllocator> allocator_;
  p2p::ServerAddresses stun_;
  std::vector<p2p::RelayServerConfig> turn_;
  std::unique_ptr<testbench::OpusAudioDecoder> decoder_;
  std::unique_ptr<testbench::OpusSendEncoder> encoder_;
  std::unique_ptr<AudioProcessor> apm_;
  std::unique_ptr<Video> video_;
  std::unique_ptr<pc::PeerEndpoint> ep_;
  // LiveKit: the subscriber connection, its allocator, and the session's state.
  std::unique_ptr<p2p::BasicPortAllocator> sub_allocator_;
  std::unique_ptr<pc::PeerEndpoint> sub_;
  std::unique_ptr<LiveKit> lk_;
  RemoteAudioSink remote_audio_sink_;
  mutable std::mutex group_mu_;
  std::vector<GroupParticipant> group_;  // participants(), under group_mu_.
  std::string active_speaker_;            // Under group_mu_.
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
  double audio_allocation_kbps_ = 0;
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
