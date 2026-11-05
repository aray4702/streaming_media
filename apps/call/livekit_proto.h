// LiveKit's client signaling messages (C19, #1164): the fields of livekit_rtc.proto and livekit_models.proto (vendored
// in apps/call/livekit/protocol/, livekit/protocol at the commit LiveKit server v1.13.11 pins) that a native client
// needs to join a room, publish and subscribe, with a small protobuf wire-format codec (varints and length-delimited
// fields; proto3, so a field at its default is not written). Unknown fields are skipped, as protobuf parsers do.
//
// The client sends SignalRequest and receives SignalResponse, one message per binary WebSocket frame on /rtc
// (LiveKit's SignalClient). Only the oneof members below are encoded or decoded; any other response decodes as
// kOther with its field number.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sm::apps::livekit {

// --- Wire format ------------------------------------------------------------------------------------------------------

class PbWriter {
 public:
  void Varint(int field, uint64_t v);  // int32/int64/uint32/uint64/bool/enum; 0 is not written (proto3).
  void Bool(int field, bool v) { Varint(field, v ? 1 : 0); }
  void Bytes(int field, std::string_view v);  // string/bytes; empty is not written.
  void Message(int field, const PbWriter& m);  // A sub-message; written even when empty (it sets a oneof).
  const std::string& data() const { return data_; }

 private:
  void Tag(int field, int wire_type);
  void Raw(uint64_t v);
  std::string data_;
};

class PbReader {
 public:
  explicit PbReader(std::string_view data) : data_(data) {}
  // The next field; false at the end or on malformed input (then ok() is false).
  bool Next();
  int field() const { return field_; }
  uint64_t varint() const { return varint_; }  // Wire type 0.
  std::string_view bytes() const { return bytes_; }  // Wire type 2.
  bool is_varint() const { return wire_ == 0; }
  bool is_bytes() const { return wire_ == 2; }
  bool ok() const { return ok_; }

 private:
  bool ReadVarint(uint64_t* v);
  std::string_view data_;
  size_t pos_ = 0;
  int field_ = 0, wire_ = -1;
  uint64_t varint_ = 0;
  std::string_view bytes_;
  bool ok_ = true;
};

// --- Messages ----------------------------------------------------------------------------------------------------------

enum class TrackType { kAudio = 0, kVideo = 1, kData = 2 };
enum class TrackSource { kUnknown = 0, kCamera = 1, kMicrophone = 2, kScreenShare = 3, kScreenShareAudio = 4 };
enum class SignalTarget { kPublisher = 0, kSubscriber = 1 };
enum class VideoQuality { kLow = 0, kMedium = 1, kHigh = 2, kOff = 3 };
// DisconnectReason (livekit_models.proto); CLIENT_INITIATED for our own leave.
constexpr int kClientInitiated = 1;

struct ICEServer {
  std::vector<std::string> urls;
  std::string username, credential;
};

struct VideoLayer {
  VideoQuality quality = VideoQuality::kHigh;
  uint32_t width = 0, height = 0, bitrate = 0, ssrc = 0;
};

struct TrackInfo {
  std::string sid;
  TrackType type = TrackType::kAudio;
  std::string name;
  bool muted = false;
  uint32_t width = 0, height = 0;
  TrackSource source = TrackSource::kUnknown;
  std::string mime_type, mid;
};

struct ParticipantInfo {
  enum class State { kJoining = 0, kJoined = 1, kActive = 2, kDisconnected = 3 };
  std::string sid, identity, name;
  State state = State::kJoining;
  std::vector<TrackInfo> tracks;
};

struct JoinResponse {
  std::string room_sid, room_name;
  ParticipantInfo participant;
  std::vector<ParticipantInfo> other_participants;
  std::string server_version;
  std::vector<ICEServer> ice_servers;
  bool subscriber_primary = false;
  int32_t ping_timeout_s = 0, ping_interval_s = 0;
  bool fast_publish = false;
};

struct SessionDescription {
  std::string type;  // "offer" | "answer"
  std::string sdp;
  uint32_t id = 0;
  std::map<std::string, std::string> mid_to_track_id;
};

struct TrickleRequest {
  std::string candidate_init;  // RTCIceCandidateInit as JSON: {"candidate","sdpMid","sdpMLineIndex",...}
  SignalTarget target = SignalTarget::kPublisher;
  bool final = false;
};

struct AddTrackRequest {
  std::string cid;  // The client's id of the track: the a=msid track id in the publisher's offer.
  std::string name;
  TrackType type = TrackType::kAudio;
  uint32_t width = 0, height = 0;
  bool muted = false;
  TrackSource source = TrackSource::kUnknown;
  std::vector<VideoLayer> layers;
  bool disable_red = false;
};

struct LeaveRequest {
  bool can_reconnect = false;
  int reason = 0;  // DisconnectReason
  int action = 0;  // 0 DISCONNECT, 1 RESUME, 2 RECONNECT
};

struct Pong {
  int64_t last_ping_timestamp = 0, timestamp = 0;
};

struct TrackPublishedResponse {
  std::string cid;
  TrackInfo track;
};

// SignalRequest: one oneof member each.
std::string EncodeOffer(const SessionDescription& sd);   // field 1: the publisher's offer
std::string EncodeAnswer(const SessionDescription& sd);  // field 2: the answer to a subscriber offer
std::string EncodeTrickle(const TrickleRequest& t);      // field 3
std::string EncodeAddTrack(const AddTrackRequest& a);    // field 4
std::string EncodeMute(const std::string& sid, bool muted);  // field 5
std::string EncodeLeave(const LeaveRequest& l);          // field 8
std::string EncodePing(int64_t timestamp_ms, int64_t rtt_ms);  // field 16, ping_req (Ping)

// SignalResponse.
struct SignalResponse {
  enum class Kind {
    kInvalid,  // Not a protobuf message.
    kJoin = 1,
    kAnswer = 2,
    kOffer = 3,
    kTrickle = 4,
    kUpdate = 5,
    kTrackPublished = 6,
    kLeave = 8,
    kMute = 9,
    kRefreshToken = 16,
    kTrackUnpublished = 17,
    kPong = 18,
    kPongResp = 20,
    kOther,  // Any other member (speakers, connection quality, stream state, ...): `field` says which.
  };
  Kind kind = Kind::kInvalid;
  int field = 0;
  JoinResponse join;
  SessionDescription description;  // kAnswer, kOffer
  TrickleRequest trickle;
  std::vector<ParticipantInfo> participants;  // kUpdate
  TrackPublishedResponse track_published;
  LeaveRequest leave;
  std::string text;  // kRefreshToken: the token; kTrackUnpublished: the track sid
  Pong pong;  // kPong (timestamp only), kPongResp
};
SignalResponse DecodeSignalResponse(std::string_view data);

// Test support: the server's side of the codec (what a LiveKit server would send and how it reads a request), so the
// unit tests can check both directions without a server.
std::string EncodeSignalResponseForTest(const SignalResponse& r);
struct SignalRequestForTest {
  int field = 0;
  SessionDescription description;
  TrickleRequest trickle;
  AddTrackRequest add_track;
  LeaveRequest leave;
  int64_t ping_timestamp = 0, ping_rtt = 0;
};
std::optional<SignalRequestForTest> DecodeSignalRequestForTest(std::string_view data);

// The /rtc URL's path and query for a join, as LiveKit's SignalClient (createConnectionParams) builds it: the token,
// auto_subscribe and this client's protocol version. No `sdk`, so the server's ClientInfo says UNKNOWN rather than
// claiming one of LiveKit's SDKs. `base_path` is the URL's path ("/" or a prefix such as "/lk/").
std::string JoinPath(std::string_view base_path, std::string_view token, bool auto_subscribe);
// The protocol version this client speaks (ClientInfo.protocol): 15, so the server uses the subscriber connection as
// the primary one (> 2), fast start (> 7) and non-error request responses (> 14), and no packet trailers (> 16).
constexpr int kProtocolVersion = 15;

}  // namespace sm::apps::livekit
