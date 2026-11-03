#pragma once
// Offer/answer generation (C6 part 2, #1063): libwebrtc M157's pc/media_session.{h,cc}
// (MediaSessionDescriptionFactory, compare/webrtc/reference/pc/), on the model and SDP writer of C6 part 1 (#1062),
// the transport description factory, the codec vendor and the payload type suggester next to it.
//
// What it builds, as libwebrtc does: an m= section per MediaDescriptionOptions in order (audio and video RTP
// sections, the SCTP m=application section, unsupported sections rejected), rtcp-mux and rtcp-rsize, the BUNDLE
// group with the first section's ICE credentials and DTLS role shared by the others, Unified Plan msid signalling,
// header extension ids that are unique across the bundle, SSRCs (and RTX SSRCs in a FID group) for each sender,
// codecs negotiated against the offer (the offer's payload types and order, the H.264 level), directions per
// RFC 3264, ICE restart and the DTLS role (setup actpass / active / passive).
//
// Differences from libwebrtc:
// - Errors: CreateOfferOrError / CreateAnswerOrError return null and write the reason to `error` (no RTCError).
// - No field trials: RFC 8888 ("ack ccfb", WebRTC-RFC8888CongestionControlFeedback) is neither offered nor
//   accepted, receive-non-sender-RTT (WebRTC-RtcpXrReceiverReferenceTime) is on, FlexFEC is not signalled, and
//   payload types are not kept per transport (WebRTC-PayloadTypesInTransport), all as libwebrtc's defaults.
// - Cryptex (CryptoOptions) is off, libwebrtc's default policy; SCTP SNAP (sctp-init) is not offered.
// - No codec preferences; no Plan B (`set_is_unified_plan(false)` only drops the MID/RID extensions and switches
//   msid signalling, as in libwebrtc).
// - The data channel switch (SM_WITH_DATACHANNEL) is the caller's: the factory writes an m=application section when
//   the options ask for one.
#include <memory>
#include <string>
#include <vector>

#include "sm/pc/codec_vendor.h"
#include "sm/pc/media_options.h"
#include "sm/pc/sdp_payload_type_suggester.h"
#include "sm/pc/session_description.h"
#include "sm/pc/transport_description_factory.h"
#include "sm/pc/unique_id_generator.h"

namespace sm::pc {

// media/sctp/sctp_transport_internal.h: the a=max-message-size this side offers.
inline constexpr int kSctpSendBufferSize = 256 * 1024;

class MediaSessionDescriptionFactory {
 public:
  // `ssrc_generator` may be null (the factory then owns one). The other pointers must outlive the factory.
  MediaSessionDescriptionFactory(UniqueRandomIdGenerator* ssrc_generator,
                                 const TransportDescriptionFactory* transport_desc_factory,
                                 const CodecVendor* codec_vendor, SdpPayloadTypeSuggester* suggester);

  RtpHeaderExtensions filtered_rtp_header_extensions(RtpHeaderExtensions extensions) const;
  void set_enable_encrypted_rtp_header_extensions(bool enable) { enable_encrypted_rtp_header_extensions_ = enable; }
  void set_is_unified_plan(bool is_unified_plan) { is_unified_plan_ = is_unified_plan; }

  std::unique_ptr<SessionDescription> CreateOfferOrError(const MediaSessionOptions& options,
                                                         const SessionDescription* current_description,
                                                         std::string* error = nullptr) const;
  std::unique_ptr<SessionDescription> CreateAnswerOrError(const SessionDescription* offer,
                                                          const MediaSessionOptions& options,
                                                          const SessionDescription* current_description,
                                                          std::string* error = nullptr) const;

 private:
  struct AudioVideoRtpHeaderExtensions {
    RtpHeaderExtensions audio;
    RtpHeaderExtensions video;
  };
  AudioVideoRtpHeaderExtensions GetOfferedRtpHeaderExtensionsWithIds(
      const std::vector<const ContentInfo*>& current_active_contents, bool extmap_allow_mixed,
      const std::vector<MediaDescriptionOptions>& media_description_options) const;
  bool AddTransportOffer(const std::string& content_name, const TransportOptions& transport_options,
                         const SessionDescription* current_desc, SessionDescription* offer,
                         IceCredentialsIterator* ice_credentials, std::string* error) const;
  std::unique_ptr<TransportDescription> CreateTransportAnswer(const std::string& content_name,
                                                              const SessionDescription* offer_desc,
                                                              const TransportOptions& transport_options,
                                                              const SessionDescription* current_desc,
                                                              bool require_transport_attributes,
                                                              IceCredentialsIterator* ice_credentials) const;
  bool AddRtpContentForOffer(const MediaDescriptionOptions& media_description_options,
                             const MediaSessionOptions& session_options, const ContentInfo* current_content,
                             const SessionDescription* current_description,
                             const RtpHeaderExtensions& header_extensions, StreamParamsVec* current_streams,
                             SessionDescription* desc, IceCredentialsIterator* ice_credentials,
                             std::string* error) const;
  bool AddDataContentForOffer(const MediaDescriptionOptions& media_description_options,
                              const MediaSessionOptions& session_options, const ContentInfo* current_content,
                              const SessionDescription* current_description, StreamParamsVec* current_streams,
                              SessionDescription* desc, IceCredentialsIterator* ice_credentials,
                              std::string* error) const;
  bool AddUnsupportedContentForOffer(const MediaDescriptionOptions& media_description_options,
                                     const ContentInfo* current_content,
                                     const SessionDescription* current_description, SessionDescription* desc,
                                     IceCredentialsIterator* ice_credentials, std::string* error) const;
  bool AddRtpContentForAnswer(const MediaDescriptionOptions& media_description_options,
                              const MediaSessionOptions& session_options, const ContentInfo* offer_content,
                              const SessionDescription* offer_description, const ContentInfo* current_content,
                              const SessionDescription* current_description, const TransportInfo* bundle_transport,
                              const RtpHeaderExtensions& header_extensions, StreamParamsVec* current_streams,
                              SessionDescription* answer, IceCredentialsIterator* ice_credentials,
                              std::string* error) const;
  bool AddDataContentForAnswer(const MediaDescriptionOptions& media_description_options,
                               const MediaSessionOptions& session_options, const ContentInfo* offer_content,
                               const SessionDescription* offer_description, const ContentInfo* current_content,
                               const SessionDescription* current_description, const TransportInfo* bundle_transport,
                               StreamParamsVec* current_streams, SessionDescription* answer,
                               IceCredentialsIterator* ice_credentials, std::string* error) const;
  bool AddUnsupportedContentForAnswer(const MediaDescriptionOptions& media_description_options,
                                      const ContentInfo* offer_content, const SessionDescription* offer_description,
                                      const SessionDescription* current_description,
                                      const TransportInfo* bundle_transport, SessionDescription* answer,
                                      IceCredentialsIterator* ice_credentials, std::string* error) const;

  UniqueRandomIdGenerator* ssrc_generator() const { return ssrc_generator_; }

  bool is_unified_plan_ = true;
  std::unique_ptr<UniqueRandomIdGenerator> owned_ssrc_generator_;
  UniqueRandomIdGenerator* ssrc_generator_;
  bool enable_encrypted_rtp_header_extensions_ = true;
  const TransportDescriptionFactory* transport_desc_factory_;
  const CodecVendor* codec_vendor_;
  SdpPayloadTypeSuggester* suggester_;
};

bool IsMediaContent(const ContentInfo* content);
bool IsAudioContent(const ContentInfo* content);
bool IsVideoContent(const ContentInfo* content);
bool IsDataContent(const ContentInfo* content);
bool IsUnsupportedContent(const ContentInfo* content);
const ContentInfo* GetFirstMediaContent(const ContentInfos& contents, MediaType media_type);
const ContentInfo* GetFirstAudioContent(const ContentInfos& contents);
const ContentInfo* GetFirstVideoContent(const ContentInfos& contents);
const ContentInfo* GetFirstDataContent(const ContentInfos& contents);
const ContentInfo* GetFirstAudioContent(const SessionDescription* sdesc);
const ContentInfo* GetFirstVideoContent(const SessionDescription* sdesc);
const ContentInfo* GetFirstDataContent(const SessionDescription* sdesc);
const AudioContentDescription* GetFirstAudioContentDescription(const SessionDescription* sdesc);
const VideoContentDescription* GetFirstVideoContentDescription(const SessionDescription* sdesc);
const SctpDataContentDescription* GetFirstSctpDataContentDescription(const SessionDescription* sdesc);
AudioContentDescription* GetFirstAudioContentDescription(SessionDescription* sdesc);
VideoContentDescription* GetFirstVideoContentDescription(SessionDescription* sdesc);
SctpDataContentDescription* GetFirstSctpDataContentDescription(SessionDescription* sdesc);

}  // namespace sm::pc
