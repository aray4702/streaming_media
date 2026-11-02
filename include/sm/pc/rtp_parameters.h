// The parts of libwebrtc M157's api/media_types.h, api/rtp_transceiver_direction.h, api/rtp_parameters.h
// (RtpExtension, RtcpFeedbackType) and api/rtp_header_extension_id.h that the session description model uses
// (#1062, C6 part 1). api/rtp_parameters.h and api/rtp_transceiver_direction.h are not in
// compare/webrtc/reference/; the types follow their M157 declarations.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sm::pc {

// api/media_types.h.
enum class MediaType { AUDIO, VIDEO, DATA, UNSUPPORTED, ANY };

// api/rtp_transceiver_direction.h.
enum class RtpTransceiverDirection { kSendRecv, kSendOnly, kRecvOnly, kInactive, kStopped };

// api/rtp_parameters.h.
enum class RtcpFeedbackType { CCM, LNTF, NACK, REMB, TRANSPORT_CC, CCFB };

// An RTP header extension as SDP's a=extmap (RFC 8285) negotiates it.
struct RtpExtension {
  RtpExtension() = default;
  RtpExtension(std::string_view uri, int id) : uri(uri), id(id) {}
  RtpExtension(std::string_view uri, int id, bool encrypt) : uri(uri), id(id), encrypt(encrypt) {}

  // RFC 8285 section 4: one-byte headers carry ids 1-14, two-byte headers 1-255.
  static constexpr int kMinId = 1;
  static constexpr int kMaxId = 255;
  static constexpr int kMaxValueSize = 255;
  static constexpr int kOneByteHeaderExtensionMaxId = 14;
  static constexpr int kOneByteHeaderExtensionMaxValueSize = 16;

  // RFC 6904: encrypted header extensions.
  static constexpr char kEncryptHeaderExtensionsUri[] = "urn:ietf:params:rtp-hdrext:encrypt";
  static constexpr char kAudioLevelUri[] = "urn:ietf:params:rtp-hdrext:ssrc-audio-level";
  static constexpr char kTimestampOffsetUri[] = "urn:ietf:params:rtp-hdrext:toffset";
  static constexpr char kAbsSendTimeUri[] = "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time";
  static constexpr char kAbsoluteCaptureTimeUri[] = "http://www.webrtc.org/experiments/rtp-hdrext/abs-capture-time";
  static constexpr char kVideoRotationUri[] = "urn:3gpp:video-orientation";
  static constexpr char kTransportSequenceNumberUri[] =
      "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01";
  static constexpr char kTransportSequenceNumberV2Uri[] = "http://www.webrtc.org/experiments/rtp-hdrext/transport-wide-cc-02";
  static constexpr char kPlayoutDelayUri[] = "http://www.webrtc.org/experiments/rtp-hdrext/playout-delay";
  static constexpr char kVideoContentTypeUri[] = "http://www.webrtc.org/experiments/rtp-hdrext/video-content-type";
  static constexpr char kVideoTimingUri[] = "http://www.webrtc.org/experiments/rtp-hdrext/video-timing";
  static constexpr char kGenericFrameDescriptorUri00[] =
      "http://www.webrtc.org/experiments/rtp-hdrext/generic-frame-descriptor-00";
  static constexpr char kDependencyDescriptorUri[] =
      "https://aomediacodec.github.io/av1-rtp-spec/#dependency-descriptor-rtp-header-extension";
  static constexpr char kVideoLayersAllocationUri[] =
      "http://www.webrtc.org/experiments/rtp-hdrext/video-layers-allocation00";
  static constexpr char kColorSpaceUri[] = "http://www.webrtc.org/experiments/rtp-hdrext/color-space";
  static constexpr char kMidUri[] = "urn:ietf:params:rtp-hdrext:sdes:mid";
  static constexpr char kRidUri[] = "urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id";
  static constexpr char kRepairedRidUri[] = "urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id";
  static constexpr char kVideoFrameTrackingIdUri[] =
      "http://www.webrtc.org/experiments/rtp-hdrext/video-frame-tracking-id";
  static constexpr char kCsrcAudioLevelsUri[] = "urn:ietf:params:rtp-hdrext:csrc-audio-level";

  friend bool operator==(const RtpExtension&, const RtpExtension&) = default;
  // "{uri: <uri>, id: <id>[, encrypt]}".
  std::string ToString() const;

  std::string uri;
  int id = 0;
  bool encrypt = false;
};

// api/rtp_header_extension_id.h's RtpHeaderExtensionId::Create: an id in [kMinId, kMaxId], or nullopt.
inline std::optional<int> CreateRtpHeaderExtensionId(int value) {
  if (value < RtpExtension::kMinId || value > RtpExtension::kMaxId) return std::nullopt;
  return value;
}

}  // namespace sm::pc
