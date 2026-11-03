#pragma once
// Options for offer/answer generation (C6 part 2, #1063): libwebrtc M157's pc/media_options.h and
// api/rtp_parameters' RtpHeaderExtensionCapability. pc/media_options.h is not in compare/webrtc/reference/; this
// follows its M157 declarations as media_session.cc and media_session_unittest.cc use them.
//
// Differences from libwebrtc: no codec preferences (`codec_preferences`, setCodecPreferences), no crypto options
// (cryptex is off, libwebrtc's default policy) and no SCTP SNAP (`use_sctp_snap`, off by default).
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sm/p2p/transport_description.h"
#include "sm/pc/rtp_parameters.h"
#include "sm/pc/simulcast_description.h"
#include "sm/pc/stream_params.h"
#include "sm/pc/transport_description_factory.h"

namespace sm::pc {

inline constexpr char kDefaultRtcpCname[] = "DefaultRtcpCname";

inline bool RtpTransceiverDirectionHasSend(RtpTransceiverDirection d) {
  return d == RtpTransceiverDirection::kSendRecv || d == RtpTransceiverDirection::kSendOnly;
}
inline bool RtpTransceiverDirectionHasRecv(RtpTransceiverDirection d) {
  return d == RtpTransceiverDirection::kSendRecv || d == RtpTransceiverDirection::kRecvOnly;
}
inline RtpTransceiverDirection RtpTransceiverDirectionFromSendRecv(bool send, bool recv) {
  if (send && recv) return RtpTransceiverDirection::kSendRecv;
  if (send) return RtpTransceiverDirection::kSendOnly;
  if (recv) return RtpTransceiverDirection::kRecvOnly;
  return RtpTransceiverDirection::kInactive;
}
inline RtpTransceiverDirection RtpTransceiverDirectionReversed(RtpTransceiverDirection d) {
  switch (d) {
    case RtpTransceiverDirection::kSendOnly:
      return RtpTransceiverDirection::kRecvOnly;
    case RtpTransceiverDirection::kRecvOnly:
      return RtpTransceiverDirection::kSendOnly;
    default:
      return d;
  }
}

// api/rtp_parameters.h RtpHeaderExtensionCapability.
struct RtpHeaderExtensionCapability {
  RtpHeaderExtensionCapability() = default;
  RtpHeaderExtensionCapability(std::string_view uri, int preferred_id, RtpTransceiverDirection direction)
      : uri(uri), preferred_id(preferred_id), direction(direction) {}
  RtpHeaderExtensionCapability(std::string_view uri, int preferred_id, bool preferred_encrypt,
                               RtpTransceiverDirection direction)
      : uri(uri), preferred_id(preferred_id), preferred_encrypt(preferred_encrypt), direction(direction) {}
  std::string uri;
  std::optional<int> preferred_id;
  bool preferred_encrypt = false;
  RtpTransceiverDirection direction = RtpTransceiverDirection::kSendRecv;
};

// Options for an individual sender in a media section.
struct SenderOptions {
  std::string track_id;
  std::vector<std::string> stream_ids;
  // Use RIDs and simulcast layers to indicate spec-compliant simulcast.
  std::vector<RidDescription> rids;
  SimulcastLayerList simulcast_layers;
  // Use `num_sim_layers` to indicate legacy simulcast.
  int num_sim_layers = 1;
};

// Options for an individual media description ("m=" section).
struct MediaDescriptionOptions {
  MediaDescriptionOptions(MediaType type, std::string_view mid, RtpTransceiverDirection direction, bool stopped)
      : type(type), mid(mid), direction(direction), stopped(stopped) {}

  void AddAudioSender(std::string_view track_id, const std::vector<std::string>& stream_ids) {
    AddSenderInternal(track_id, stream_ids, {}, SimulcastLayerList(), 1);
  }
  void AddVideoSender(std::string_view track_id, const std::vector<std::string>& stream_ids,
                      const std::vector<RidDescription>& rids, const SimulcastLayerList& simulcast_layers,
                      int num_sim_layers) {
    // Either RIDs or legacy simulcast layers, not both.
    AddSenderInternal(track_id, stream_ids, rids, simulcast_layers, rids.empty() ? num_sim_layers : 0);
  }

  MediaType type;
  std::string mid;
  RtpTransceiverDirection direction;
  bool stopped;
  TransportOptions transport_options;
  // Note: There's no equivalent "RtpReceiverOptions" because only send stream information goes in the SDP.
  std::vector<SenderOptions> sender_options;
  std::vector<RtpHeaderExtensionCapability> header_extensions;
  bool sframe_enabled = false;

 private:
  void AddSenderInternal(std::string_view track_id, const std::vector<std::string>& stream_ids,
                         const std::vector<RidDescription>& rids, const SimulcastLayerList& simulcast_layers,
                         int num_sim_layers) {
    SenderOptions options;
    options.track_id = std::string(track_id);
    options.stream_ids = stream_ids;
    options.simulcast_layers = simulcast_layers;
    options.rids = rids;
    options.num_sim_layers = num_sim_layers;
    sender_options.push_back(options);
  }
};

// Options for the whole session: the m= sections, in order, and what applies to all of them.
struct MediaSessionOptions {
  bool has_audio() const { return HasMediaDescription(MediaType::AUDIO); }
  bool has_video() const { return HasMediaDescription(MediaType::VIDEO); }
  bool has_data() const { return HasMediaDescription(MediaType::DATA); }
  bool HasMediaDescription(MediaType type) const {
    for (const auto& o : media_description_options) {
      if (o.type == type) return true;
    }
    return false;
  }

  bool vad_enabled = true;  // When false, the CN codecs are left out.
  bool rtcp_mux_enabled = true;
  bool bundle_enabled = false;
  bool offer_extmap_allow_mixed = false;
  std::string rtcp_cname = kDefaultRtcpCname;
  // List of media description options in the same order that the media descriptions will be generated.
  std::vector<MediaDescriptionOptions> media_description_options;
  std::vector<IceParameters> pooled_ice_credentials;
  // Use the draft-ietf-mmusic-sctp-sdp-03 obsolete syntax for SCTP data channels.
  bool use_obsolete_sctp_sdp = false;
};

}  // namespace sm::pc
