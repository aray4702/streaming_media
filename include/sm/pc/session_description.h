// The session description model (#1062, C6 part 1): libwebrtc M157's pc/session_description.{h,cc}
// (compare/webrtc/reference/pc/). A SessionDescription is what an SDP offer or answer says: its contents (one per
// m= section: a mid, a protocol type, rejected/bundle-only, and a media description), its transports (ICE and DTLS
// per mid, sm/p2p/transport_description.h), its groups (BUNDLE) and the session-level msid signalling,
// extmap-allow-mixed and cryptex. webrtc_sdp (sm/pc/webrtc_sdp.h) writes and reads it; offer/answer generation (C6
// part 2) builds it.
//
// Differences from libwebrtc: the deprecated ExtmapAllowMixed enum is left out, and the payload types are ints
// (sm/pc/codec.h).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sm/p2p/socket_address.h"
#include "sm/p2p/transport_description.h"
#include "sm/pc/codec.h"
#include "sm/pc/media_protocol_names.h"
#include "sm/pc/rtp_parameters.h"
#include "sm/pc/simulcast_description.h"
#include "sm/pc/stream_params.h"

namespace sm::pc {

using p2p::TransportDescription;
using p2p::TransportInfo;
using p2p::TransportInfos;
using RtpHeaderExtensions = std::vector<RtpExtension>;

// p2p/base/p2p_constants: the BUNDLE group's semantics.
inline constexpr char GROUP_TYPE_BUNDLE[] = "BUNDLE";

inline constexpr int kAutoBandwidth = -1;

class AudioContentDescription;
class VideoContentDescription;
class SctpDataContentDescription;
class UnsupportedContentDescription;

// One m= section's media: its protocol, direction, RTCP options, bandwidth, header extensions, streams, codecs and
// simulcast.
class MediaContentDescription {
 public:
  MediaContentDescription() = default;
  virtual ~MediaContentDescription() = default;

  virtual MediaType type() const = 0;

  virtual AudioContentDescription* as_audio() { return nullptr; }
  virtual const AudioContentDescription* as_audio() const { return nullptr; }
  virtual VideoContentDescription* as_video() { return nullptr; }
  virtual const VideoContentDescription* as_video() const { return nullptr; }
  virtual SctpDataContentDescription* as_sctp() { return nullptr; }
  virtual const SctpDataContentDescription* as_sctp() const { return nullptr; }
  virtual UnsupportedContentDescription* as_unsupported() { return nullptr; }
  virtual const UnsupportedContentDescription* as_unsupported() const { return nullptr; }

  std::unique_ptr<MediaContentDescription> Clone() const { return std::unique_ptr<MediaContentDescription>(CloneInternal()); }

  // The m= line's <proto>, e.g. "UDP/TLS/RTP/SAVPF".
  const std::string& protocol() const { return protocol_; }
  virtual void set_protocol(std::string_view protocol) { protocol_ = std::string(protocol); }

  RtpTransceiverDirection direction() const { return direction_; }
  void set_direction(RtpTransceiverDirection direction) { direction_ = direction; }

  bool rtcp_mux() const { return rtcp_mux_; }
  void set_rtcp_mux(bool mux) { rtcp_mux_ = mux; }
  bool rtcp_reduced_size() const { return rtcp_reduced_size_; }
  void set_rtcp_reduced_size(bool reduced_size) { rtcp_reduced_size_ = reduced_size; }
  // RFC 3611 a=rtcp-xr:rcvr-rtt.
  bool receive_non_sender_rtt() const { return receive_non_sender_rtt_; }
  void set_receive_non_sender_rtt(bool enable) { receive_non_sender_rtt_ = enable; }
  bool remote_estimate() const { return remote_estimate_; }
  void set_remote_estimate(bool remote_estimate) { remote_estimate_ = remote_estimate; }
  // RFC 8888 congestion control feedback for every codec (a=rtcp-fb:* ack ccfb).
  bool rtcp_fb_ack_ccfb() const { return rtcp_fb_ack_ccfb_; }
  void set_rtcp_fb_ack_ccfb(bool enable) { rtcp_fb_ack_ccfb_ = enable; }
  bool rtcp_fb_ack_transport_cc() const {
    for (const auto& codec : codecs_) {
      if (codec.feedback_params.Has(FeedbackParam(kRtcpFbParamTransportCc))) return true;
    }
    return false;
  }
  std::optional<RtcpFeedbackType> preferred_rtcp_cc_ack_type() const {
    if (rtcp_fb_ack_ccfb_) return RtcpFeedbackType::CCFB;
    if (rtcp_fb_ack_transport_cc()) return RtcpFeedbackType::TRANSPORT_CC;
    return std::nullopt;
  }

  // In bps (b=AS is converted from kbps); kAutoBandwidth when not set.
  int bandwidth() const { return bandwidth_; }
  void set_bandwidth(int bandwidth) { bandwidth_ = bandwidth; }
  const std::string& bandwidth_type() const { return bandwidth_type_; }
  void set_bandwidth_type(std::string bandwidth_type) { bandwidth_type_ = std::move(bandwidth_type); }

  const RtpHeaderExtensions& rtp_header_extensions() const { return rtp_header_extensions_; }
  void set_rtp_header_extensions(const RtpHeaderExtensions& extensions) { rtp_header_extensions_ = extensions; }
  void AddRtpHeaderExtension(const RtpExtension& ext) { rtp_header_extensions_.push_back(ext); }

  const StreamParamsVec& streams() const { return send_streams_; }
  StreamParamsVec& mutable_streams() { return send_streams_; }
  void AddStream(const StreamParams& stream) { send_streams_.push_back(stream); }
  void AddLegacyStream(uint32_t ssrc) { AddStream(StreamParams::CreateLegacy(ssrc)); }
  void AddLegacyStream(uint32_t ssrc, uint32_t fid_ssrc) {
    StreamParams sp = StreamParams::CreateLegacy(ssrc);
    sp.AddFidSsrc(ssrc, fid_ssrc);
    AddStream(sp);
  }
  uint32_t first_ssrc() const { return send_streams_.empty() ? 0 : send_streams_[0].first_ssrc(); }
  bool has_ssrcs() const { return !send_streams_.empty() && send_streams_[0].has_ssrcs(); }

  void set_conference_mode(bool enable) { conference_mode_ = enable; }
  bool conference_mode() const { return conference_mode_; }

  // The c= line's address with the m= line's port.
  void set_connection_address(const p2p::SocketAddress& address) { connection_address_ = address; }
  const p2p::SocketAddress& connection_address() const { return connection_address_; }

  // Where an attribute that may be at either level (extmap-allow-mixed, cryptex) was given.
  enum class AttributeLevel { kNone, kSession, kMedia };
  void set_extmap_allow_mixed_level(AttributeLevel level) {
    if (level == AttributeLevel::kMedia && extmap_allow_mixed_level_ == AttributeLevel::kSession) {
      // Do not downgrade from session level to media level.
      return;
    }
    extmap_allow_mixed_level_ = level;
  }
  AttributeLevel extmap_allow_mixed_level() const { return extmap_allow_mixed_level_; }
  bool extmap_allow_mixed() const { return extmap_allow_mixed_level_ != AttributeLevel::kNone; }

  bool HasSimulcast() const { return !simulcast_.empty(); }
  SimulcastDescription& simulcast_description() { return simulcast_; }
  const SimulcastDescription& simulcast_description() const { return simulcast_; }
  void set_simulcast_description(const SimulcastDescription& simulcast) { simulcast_ = simulcast; }
  const std::vector<RidDescription>& receive_rids() const { return receive_rids_; }
  void set_receive_rids(const std::vector<RidDescription>& rids) { receive_rids_ = rids; }

  bool sframe_enabled() const { return sframe_enabled_; }
  void set_sframe_enabled(bool sframe_enabled) { sframe_enabled_ = sframe_enabled; }

  const std::vector<Codec>& codecs() const { return codecs_; }
  void set_codecs(const std::vector<Codec>& codecs) { codecs_ = codecs; }
  virtual bool has_codecs() const { return !codecs_.empty(); }
  bool HasCodec(int id) {
    for (const Codec& codec : codecs_) {
      if (codec.id == id) return true;
    }
    return false;
  }
  void AddCodec(const Codec& codec) { codecs_.push_back(codec); }
  void AddOrReplaceCodec(const Codec& codec) {
    for (auto it = codecs_.begin(); it != codecs_.end(); ++it) {
      if (it->id == codec.id) {
        *it = codec;
        return;
      }
    }
    AddCodec(codec);
  }
  void AddCodecs(const std::vector<Codec>& codecs) {
    for (const auto& codec : codecs) AddCodec(codec);
  }

  // RFC 9335 a=cryptex.
  void set_cryptex_level(AttributeLevel level) {
    if (level == AttributeLevel::kMedia && cryptex_level_ == AttributeLevel::kSession) return;
    cryptex_level_ = level;
  }
  AttributeLevel cryptex_level() const { return cryptex_level_; }
  bool cryptex() const { return cryptex_level_ != AttributeLevel::kNone; }

 protected:
  MediaContentDescription(const MediaContentDescription&) = default;
  MediaContentDescription& operator=(const MediaContentDescription&) = default;

  std::string protocol_;

 private:
  virtual MediaContentDescription* CloneInternal() const = 0;

  bool rtcp_mux_ = false;
  bool rtcp_reduced_size_ = false;
  bool receive_non_sender_rtt_ = false;
  bool remote_estimate_ = false;
  bool rtcp_fb_ack_ccfb_ = false;
  int bandwidth_ = kAutoBandwidth;
  std::string bandwidth_type_{kApplicationSpecificBandwidth};
  std::vector<RtpExtension> rtp_header_extensions_;
  StreamParamsVec send_streams_;
  bool conference_mode_ = false;
  RtpTransceiverDirection direction_ = RtpTransceiverDirection::kSendRecv;
  p2p::SocketAddress connection_address_;
  AttributeLevel extmap_allow_mixed_level_ = AttributeLevel::kMedia;
  SimulcastDescription simulcast_;
  std::vector<RidDescription> receive_rids_;
  bool sframe_enabled_ = false;
  std::vector<Codec> codecs_;
  AttributeLevel cryptex_level_ = AttributeLevel::kNone;
};

class RtpMediaContentDescription : public MediaContentDescription {};

class AudioContentDescription final : public RtpMediaContentDescription {
 public:
  AudioContentDescription() = default;
  void set_protocol(std::string_view protocol) override { protocol_ = std::string(protocol); }
  MediaType type() const override { return MediaType::AUDIO; }
  AudioContentDescription* as_audio() override { return this; }
  const AudioContentDescription* as_audio() const override { return this; }

 private:
  AudioContentDescription* CloneInternal() const override { return new AudioContentDescription(*this); }
};

class VideoContentDescription final : public RtpMediaContentDescription {
 public:
  VideoContentDescription() = default;
  void set_protocol(std::string_view protocol) override { protocol_ = std::string(protocol); }
  MediaType type() const override { return MediaType::VIDEO; }
  VideoContentDescription* as_video() override { return this; }
  const VideoContentDescription* as_video() const override { return this; }

 private:
  VideoContentDescription* CloneInternal() const override { return new VideoContentDescription(*this); }
};

// m=application over SCTP: the SCTP port, max-message-size and draft-hancke-tsvwg-snap's sctp-init.
class SctpDataContentDescription final : public MediaContentDescription {
 public:
  SctpDataContentDescription() = default;
  MediaType type() const override { return MediaType::DATA; }
  SctpDataContentDescription* as_sctp() override { return this; }
  const SctpDataContentDescription* as_sctp() const override { return this; }
  bool has_codecs() const override { return false; }
  void set_protocol(std::string_view protocol) override { protocol_ = std::string(protocol); }

  // The legacy a=sctpmap form (draft-ietf-mmusic-sctp-sdp-04); false for a=sctp-port.
  bool use_sctpmap() const { return use_sctpmap_; }
  void set_use_sctpmap(bool enable) { use_sctpmap_ = enable; }
  int port() const { return port_; }
  void set_port(int port) { port_ = port; }
  int max_message_size() const { return max_message_size_; }
  void set_max_message_size(int max_message_size) { max_message_size_ = max_message_size; }
  const std::optional<std::vector<uint8_t>>& sctp_init() const { return sctp_init_; }
  void set_sctp_init(std::optional<std::vector<uint8_t>> sctp_init) { sctp_init_ = std::move(sctp_init); }

 private:
  SctpDataContentDescription* CloneInternal() const override { return new SctpDataContentDescription(*this); }

  bool use_sctpmap_ = true;  // Note: "true" is no longer conformant.
  // Defaults should be constants imported from SCTP. Quick hack.
  int port_ = 5000;
  // draft-ietf-mmusic-sctp-sdp-23-section-5.2.1
  int max_message_size_ = 64 * 1024;
  std::optional<std::vector<uint8_t>> sctp_init_;
};

// An m= section of a media type this library doesn't know; it is kept (and rejected) so that the answer has an m=
// section for it.
class UnsupportedContentDescription final : public MediaContentDescription {
 public:
  explicit UnsupportedContentDescription(std::string_view media_type) : media_type_(media_type) {}
  MediaType type() const override { return MediaType::UNSUPPORTED; }
  UnsupportedContentDescription* as_unsupported() override { return this; }
  const UnsupportedContentDescription* as_unsupported() const override { return this; }
  bool has_codecs() const override { return false; }
  const std::string& media_type() const { return media_type_; }

 private:
  UnsupportedContentDescription* CloneInternal() const override { return new UnsupportedContentDescription(*this); }

  std::string media_type_;
};

// Protocol used for encoding media. This is the "top level" protocol that may be wrapped by zero or many transport
// protocols (UDP, ICE, etc.).
enum class MediaProtocolType {
  kRtp,   // Section will use the RTP protocol (e.g., for audio or video).
  kSctp,  // Section will use the SCTP protocol (e.g., for a data channel).
  kOther  // Section will use another top protocol which is not explicitly supported.
};

// One m= section.
class ContentInfo final {
 public:
  ContentInfo(MediaProtocolType type, std::string_view mid, std::unique_ptr<MediaContentDescription> description,
              bool rejected = false, bool bundle_only = false)
      : type(type), rejected(rejected), bundle_only(bundle_only), mid_(mid), description_(std::move(description)) {}
  ContentInfo(const ContentInfo& o)
      : type(o.type),
        rejected(o.rejected),
        bundle_only(o.bundle_only),
        mid_(o.mid_),
        description_(o.description_ ? o.description_->Clone() : nullptr) {}
  ContentInfo& operator=(const ContentInfo& o) = delete;
  ContentInfo(ContentInfo&& o) = default;
  ContentInfo& operator=(ContentInfo&& o) = default;

  const std::string& mid() const { return mid_; }
  void set_mid(std::string_view mid) { mid_ = std::string(mid); }

  MediaContentDescription* media_description() { return description_.get(); }
  const MediaContentDescription* media_description() const { return description_.get(); }

  MediaProtocolType type;
  bool rejected = false;
  bool bundle_only = false;

 private:
  std::string mid_;
  std::unique_ptr<MediaContentDescription> description_;
};

using ContentNames = std::vector<std::string>;

// RFC 5888 a=group: semantics ("BUNDLE") and the mids in it.
class ContentGroup {
 public:
  explicit ContentGroup(std::string semantics) : semantics_(std::move(semantics)) {}
  bool operator==(const ContentGroup& o) const = default;

  const std::string& semantics() const { return semantics_; }
  const ContentNames& content_names() const { return content_names_; }

  const std::string* FirstContentName() const;
  bool HasContentName(std::string_view content_name) const;
  // Ignores a name already in the group.
  void AddContentName(std::string_view content_name);
  bool RemoveContentName(std::string_view content_name);
  // "BUNDLE(audio video)".
  std::string ToString() const;

 private:
  std::string semantics_;
  ContentNames content_names_;
};

using ContentInfos = std::vector<ContentInfo>;
using ContentGroups = std::vector<ContentGroup>;

// How MediaStream ids are signalled (a bit set).
enum MsidSignaling {
  // Signal MediaStream info with neither a=msid nor a=ssrc lines.
  kMsidSignalingNotUsed = 0x0,
  // Signal MediaStream info in a=msid lines (Unified Plan).
  kMsidSignalingMediaSection = 0x1,
  // Signal MediaStream info in a=ssrc msid lines (Plan B).
  kMsidSignalingSsrcAttribute = 0x2,
  // Signal the msid-semantic line.
  kMsidSignalingSemantic = 0x4
};

// The whole description: contents, transports and groups.
class SessionDescription {
 public:
  SessionDescription() = default;
  std::unique_ptr<SessionDescription> Clone() const { return std::unique_ptr<SessionDescription>(new SessionDescription(*this)); }

  const ContentInfos& contents() const { return contents_; }
  ContentInfos& contents() { return contents_; }
  const ContentInfo* GetContentByName(std::string_view name) const;
  ContentInfo* GetContentByName(std::string_view name);
  const MediaContentDescription* GetContentDescriptionByName(std::string_view name) const;
  MediaContentDescription* GetContentDescriptionByName(std::string_view name);
  const ContentInfo* FirstContentByType(MediaProtocolType type) const;
  const ContentInfo* FirstContent() const;

  void AddContent(std::string_view name, MediaProtocolType type, std::unique_ptr<MediaContentDescription> description);
  void AddContent(std::string_view name, MediaProtocolType type, bool rejected,
                  std::unique_ptr<MediaContentDescription> description);
  void AddContent(std::string_view name, MediaProtocolType type, bool rejected, bool bundle_only,
                  std::unique_ptr<MediaContentDescription> description);
  // Sets the content's extmap-allow-mixed and cryptex to the session level when the session has them.
  void AddContent(ContentInfo&& content);
  bool RemoveContentByName(std::string_view name);

  const TransportInfos& transport_infos() const { return transport_infos_; }
  TransportInfos& transport_infos() { return transport_infos_; }
  const TransportInfo* GetTransportInfoByName(std::string_view name) const;
  TransportInfo* GetTransportInfoByName(std::string_view name);
  const TransportDescription* GetTransportDescriptionByName(std::string_view name) const {
    const TransportInfo* tinfo = GetTransportInfoByName(name);
    return tinfo ? &tinfo->description : nullptr;
  }
  void set_transport_infos(const TransportInfos& transport_infos) { transport_infos_ = transport_infos; }
  void AddTransportInfo(const TransportInfo& transport_info) { transport_infos_.push_back(transport_info); }
  bool RemoveTransportInfoByName(std::string_view name);

  const ContentGroups& groups() const { return content_groups_; }
  const ContentGroup* GetGroupByName(std::string_view name) const;
  std::vector<const ContentGroup*> GetGroupsByName(std::string_view name) const;
  bool HasGroup(std::string_view name) const;
  void AddGroup(const ContentGroup& group) { content_groups_.push_back(group); }
  // Removes the first group with that semantics.
  void RemoveGroupByName(std::string_view name);

  void set_msid_signaling(int msid_signaling) { msid_signaling_ = msid_signaling; }
  int msid_signaling() const { return msid_signaling_; }

  // Session-level a=extmap-allow-mixed: sets every content's level to kSession (or clears it, keeping media-level
  // ones).
  void set_extmap_allow_mixed(bool supported) {
    extmap_allow_mixed_ = supported;
    const auto level = supported ? MediaContentDescription::AttributeLevel::kSession
                                 : MediaContentDescription::AttributeLevel::kNone;
    for (auto& content : contents_) {
      // Do not set to kNone if it is set on media level.
      if (supported ||
          content.media_description()->extmap_allow_mixed_level() != MediaContentDescription::AttributeLevel::kMedia) {
        content.media_description()->set_extmap_allow_mixed_level(level);
      }
    }
  }
  bool extmap_allow_mixed() const { return extmap_allow_mixed_; }

  void set_cryptex(bool supported) {
    cryptex_ = supported;
    const auto level = supported ? MediaContentDescription::AttributeLevel::kSession
                                 : MediaContentDescription::AttributeLevel::kNone;
    for (auto& content : contents_) {
      if (supported || content.media_description()->cryptex_level() != MediaContentDescription::AttributeLevel::kMedia) {
        content.media_description()->set_cryptex_level(level);
      }
    }
  }
  bool cryptex() const { return cryptex_; }

 private:
  SessionDescription(const SessionDescription&) = default;

  ContentInfos contents_;
  TransportInfos transport_infos_;
  ContentGroups content_groups_;
  int msid_signaling_ = kMsidSignalingMediaSection | kMsidSignalingSemantic;
  bool extmap_allow_mixed_ = true;
  bool cryptex_ = false;
};

// Indicates whether a session description was sent by the local client or received from the remote client.
enum ContentSource { CS_LOCAL, CS_REMOTE };

}  // namespace sm::pc
