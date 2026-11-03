// libwebrtc M157 pc/media_session.cc (compare/webrtc/reference/pc/media_session.cc), for C6's subset; see
// sm/pc/media_session.h for the differences.
#include "sm/pc/media_session.h"

#include <algorithm>
#include <optional>

namespace sm::pc {

namespace {

enum class ExtensionFilter { kDiscardEncryptedExtension, kPreferEncryptedExtension, kRequireEncryptedExtension };

bool Fail(std::string* error, const char* message) {
  if (error) *error = message;
  return false;
}

// api/rtp_parameters.cc RtpExtension helpers.
bool IsEncryptionSupported(std::string_view uri) { return uri != RtpExtension::kEncryptHeaderExtensionsUri; }

const RtpExtension* FindHeaderExtensionByUri(const std::vector<RtpExtension>& extensions, std::string_view uri,
                                             ExtensionFilter filter) {
  const RtpExtension* fallback_extension = nullptr;
  for (const auto& extension : extensions) {
    if (extension.uri != uri) continue;
    switch (filter) {
      case ExtensionFilter::kDiscardEncryptedExtension:
        if (extension.encrypt) continue;
        return &extension;
      case ExtensionFilter::kPreferEncryptedExtension:
        if (extension.encrypt) return &extension;
        fallback_extension = &extension;
        break;
      case ExtensionFilter::kRequireEncryptedExtension:
        if (extension.encrypt) return &extension;
        break;
    }
  }
  return fallback_extension;
}

const RtpExtension* FindHeaderExtensionByUriAndEncryption(const std::vector<RtpExtension>& extensions,
                                                          std::string_view uri, bool encrypt) {
  for (const auto& extension : extensions) {
    if (extension.uri == uri && extension.encrypt == encrypt) return &extension;
  }
  return nullptr;
}

RtpExtension RtpExtensionFromCapability(const RtpHeaderExtensionCapability& capability) {
  return RtpExtension(capability.uri, capability.preferred_id.value_or(1), capability.preferred_encrypt);
}

RtpHeaderExtensions UnstoppedRtpHeaderExtensionsFromCapabilities(
    const std::vector<RtpHeaderExtensionCapability>& capabilities) {
  RtpHeaderExtensions exts;
  for (const auto& capability : capabilities) {
    if (capability.direction != RtpTransceiverDirection::kStopped) exts.push_back(RtpExtensionFromCapability(capability));
  }
  return exts;
}

bool IsCapabilityPresent(const RtpHeaderExtensionCapability& capability, const RtpHeaderExtensions& extensions) {
  return std::any_of(extensions.begin(), extensions.end(),
                     [&capability](const RtpExtension& extension) { return capability.uri == extension.uri; });
}

RtpHeaderExtensions UnstoppedOrPresentRtpHeaderExtensions(
    const std::vector<RtpHeaderExtensionCapability>& capabilities,
    const RtpHeaderExtensions& all_encountered_extensions) {
  RtpHeaderExtensions extensions;
  for (const auto& capability : capabilities) {
    if (capability.direction != RtpTransceiverDirection::kStopped ||
        IsCapabilityPresent(capability, all_encountered_extensions)) {
      extensions.push_back(RtpExtensionFromCapability(capability));
    }
  }
  return extensions;
}

bool ContainsRtxCodec(const std::vector<Codec>& codecs) {
  return std::any_of(codecs.begin(), codecs.end(),
                     [](const Codec& c) { return c.GetResiliencyType() == Codec::ResiliencyType::kRtx; });
}

bool IsComfortNoiseCodec(const Codec& codec) {
  if (codec.name.size() != 2) return false;
  return (codec.name[0] == 'C' || codec.name[0] == 'c') && (codec.name[1] == 'N' || codec.name[1] == 'n');
}

RtpTransceiverDirection NegotiateRtpTransceiverDirection(RtpTransceiverDirection offer,
                                                         RtpTransceiverDirection wants) {
  const bool offer_send = RtpTransceiverDirectionHasSend(offer);
  const bool offer_recv = RtpTransceiverDirectionHasRecv(offer);
  const bool wants_send = RtpTransceiverDirectionHasSend(wants);
  const bool wants_recv = RtpTransceiverDirectionHasRecv(wants);
  return RtpTransceiverDirectionFromSendRecv(offer_recv && wants_send, offer_send && wants_recv);
}

bool IsMediaContentOfType(const ContentInfo* content, MediaType media_type) {
  if (!content || !content->media_description()) return false;
  return content->media_description()->type() == media_type;
}

StreamParamsVec GetCurrentStreamParams(const std::vector<const ContentInfo*>& active_local_contents) {
  StreamParamsVec stream_params;
  for (const ContentInfo* content : active_local_contents) {
    for (const StreamParams& params : content->media_description()->streams()) stream_params.push_back(params);
  }
  return stream_params;
}

StreamParams CreateStreamParamsForNewSenderWithSsrcs(const SenderOptions& sender, const std::string& rtcp_cname,
                                                     bool include_rtx_streams,
                                                     UniqueRandomIdGenerator* ssrc_generator) {
  StreamParams result;
  result.id = sender.track_id;
  // FlexFEC needs the WebRTC-FlexFEC-03 trial, off by default: no FEC-FR SSRC.
  result.GenerateSsrcs(sender.num_sim_layers, include_rtx_streams, /*generate_fec_fr=*/false,
                       [ssrc_generator] { return ssrc_generator->GenerateId(); });
  result.cname = rtcp_cname;
  result.set_stream_ids(sender.stream_ids);
  return result;
}

StreamParams CreateStreamParamsForNewSenderWithRids(const SenderOptions& sender, const std::string& rtcp_cname) {
  StreamParams result;
  result.id = sender.track_id;
  result.cname = rtcp_cname;
  result.set_stream_ids(sender.stream_ids);
  // More than one rid should be signaled.
  if (sender.rids.size() > 1) result.set_rids(sender.rids);
  return result;
}

// Adds the SimulcastDescription when the sender uses more than one RID.
void AddSimulcastToMediaDescription(const MediaDescriptionOptions& media_description_options,
                                    MediaContentDescription* description) {
  if (std::all_of(description->streams().begin(), description->streams().end(),
                  [](const StreamParams& params) { return !params.has_rids(); })) {
    return;
  }
  // One RID or less indicates that simulcast is not needed.
  if (description->streams()[0].rids().size() <= 1) return;
  // Only negotiate the send layers.
  SimulcastDescription simulcast;
  simulcast.send_layers() = media_description_options.sender_options[0].simulcast_layers;
  description->set_simulcast_description(simulcast);
}

// Adds a StreamParams for each SenderOptions; `current_streams` holds every known StreamParams of any media type.
void AddStreamParams(const std::vector<SenderOptions>& sender_options, const std::string& rtcp_cname,
                     UniqueRandomIdGenerator* ssrc_generator, StreamParamsVec* current_streams,
                     MediaContentDescription* content_description) {
  // SCTP streams are not negotiated using SDP/ContentDescriptions.
  if (IsSctpProtocol(content_description->protocol())) return;
  const bool include_rtx_streams = ContainsRtxCodec(content_description->codecs());
  for (const SenderOptions& sender : sender_options) {
    auto param = std::find_if(current_streams->begin(), current_streams->end(),
                              [&](const StreamParams& sp) { return sp.id == sender.track_id; });
    if (param == current_streams->end()) {
      // A new sender: SSRCs and legacy simulcast, or RIDs and spec-compliant simulcast.
      StreamParams stream_param = sender.rids.empty() ? CreateStreamParamsForNewSenderWithSsrcs(
                                                            sender, rtcp_cname, include_rtx_streams, ssrc_generator)
                                                      : CreateStreamParamsForNewSenderWithRids(sender, rtcp_cname);
      content_description->AddStream(stream_param);
      // Kept so that the CNAME is used for the other media types.
      current_streams->push_back(stream_param);
    } else {
      // Use the existing SSRCs and groups, with the stream ids updated (a track moved between MediaStreams).
      param->set_stream_ids(sender.stream_ids);
      content_description->AddStream(*param);
    }
  }
}

// The contents of `bundle_group` take the ICE credentials and DTLS role of its first content.
bool UpdateTransportInfoForBundle(const ContentGroup& bundle_group, SessionDescription* sdesc) {
  if (!sdesc || !bundle_group.FirstContentName()) return false;
  const std::string& selected_content_name = *bundle_group.FirstContentName();
  const TransportInfo* selected_transport_info = sdesc->GetTransportInfoByName(selected_content_name);
  if (!selected_transport_info) return false;
  const std::string selected_ufrag = selected_transport_info->description.ice_ufrag;
  const std::string selected_pwd = selected_transport_info->description.ice_pwd;
  const ConnectionRole selected_connection_role = selected_transport_info->description.connection_role;
  for (TransportInfo& transport_info : sdesc->transport_infos()) {
    if (bundle_group.HasContentName(transport_info.content_name) &&
        transport_info.content_name != selected_content_name) {
      transport_info.description.ice_ufrag = selected_ufrag;
      transport_info.description.ice_pwd = selected_pwd;
      transport_info.description.connection_role = selected_connection_role;
    }
  }
  return true;
}

std::vector<const ContentInfo*> GetActiveContents(const SessionDescription& description,
                                                  const MediaSessionOptions& session_options) {
  std::vector<const ContentInfo*> active_contents;
  for (size_t i = 0; i < description.contents().size() && i < session_options.media_description_options.size();
       ++i) {
    const ContentInfo& content = description.contents()[i];
    const MediaDescriptionOptions& media_options = session_options.media_description_options[i];
    if (!content.rejected && !media_options.stopped && content.mid() == media_options.mid) {
      active_contents.push_back(&content);
    }
  }
  return active_contents;
}

void CreateContentOffer(const MediaDescriptionOptions& media_description_options,
                        const MediaSessionOptions& session_options, const RtpHeaderExtensions& rtp_extensions,
                        MediaContentDescription* offer) {
  offer->set_rtcp_mux(session_options.rtcp_mux_enabled);
  offer->set_rtcp_reduced_size(true);
  // WebRTC-RtcpXrReceiverReferenceTime is not disabled.
  offer->set_receive_non_sender_rtt(true);
  // The header extensions this section's options have and don't stop.
  RtpHeaderExtensions extensions;
  for (const auto& extension_with_id : rtp_extensions) {
    for (const auto& extension : media_description_options.header_extensions) {
      if (extension_with_id.uri == extension.uri && extension_with_id.encrypt == extension.preferred_encrypt &&
          extension.direction != RtpTransceiverDirection::kStopped) {
        extensions.push_back(extension_with_id);
      }
    }
  }
  offer->set_rtp_header_extensions(extensions);
  AddSimulcastToMediaDescription(media_description_options, offer);
}

void CreateMediaContentOffer(const MediaDescriptionOptions& media_description_options,
                             const MediaSessionOptions& session_options, const std::vector<Codec>& codecs,
                             const RtpHeaderExtensions& rtp_extensions, UniqueRandomIdGenerator* ssrc_generator,
                             StreamParamsVec* current_streams, MediaContentDescription* offer) {
  offer->AddCodecs(codecs);
  AddStreamParams(media_description_options.sender_options, session_options.rtcp_cname, ssrc_generator,
                  current_streams, offer);
  CreateContentOffer(media_description_options, session_options, rtp_extensions, offer);
}

// Adds the extensions of `reference_extensions` that `offered_extensions` lacks, with ids unique across audio and
// video: an extension already used by the other media type keeps its id.
void MergeRtpHdrExts(const RtpHeaderExtensions& reference_extensions, bool enable_encrypted_rtp_header_extensions,
                     RtpHeaderExtensions* offered_extensions, RtpHeaderExtensions* all_encountered_extensions,
                     SdpPayloadTypeSuggester& suggester, std::string_view mid, RtpTransceiverIdDomain id_domain) {
  for (const auto& reference_extension : reference_extensions) {
    if (FindHeaderExtensionByUriAndEncryption(*offered_extensions, reference_extension.uri,
                                              reference_extension.encrypt)) {
      continue;
    }
    // Negotiation of encrypted headers is deactivated.
    if (reference_extension.encrypt && !enable_encrypted_rtp_header_extensions) continue;
    const RtpExtension* existing = FindHeaderExtensionByUriAndEncryption(
        *all_encountered_extensions, reference_extension.uri, reference_extension.encrypt);
    if (existing) {
      // E.g. the same RTP header extension used for audio and video.
      offered_extensions->push_back(*existing);
    } else {
      RtpExtension extension = reference_extension;
      if (auto suggested_id = suggester.SuggestRtpHeaderExtensionId(mid, reference_extension, id_domain)) {
        extension.id = *suggested_id;
      }
      all_encountered_extensions->push_back(extension);
      offered_extensions->push_back(std::move(extension));
    }
  }
}

// FindHeaderExtensionByUri that discards the encrypted extensions this implementation cannot encrypt.
const RtpExtension* FindHeaderExtensionByUriDiscardUnsupported(const std::vector<RtpExtension>& extensions,
                                                               std::string_view uri, ExtensionFilter filter) {
  if (!IsEncryptionSupported(uri)) {
    if (filter == ExtensionFilter::kRequireEncryptedExtension) return nullptr;
    filter = ExtensionFilter::kDiscardEncryptedExtension;
  }
  return FindHeaderExtensionByUri(extensions, uri, filter);
}

void NegotiateRtpHeaderExtensions(const RtpHeaderExtensions& local_extensions,
                                  const RtpHeaderExtensions& offered_extensions, ExtensionFilter filter,
                                  RtpHeaderExtensions* negotiated_extensions, SdpPayloadTypeSuggester& suggester,
                                  std::string_view mid) {
  bool frame_descriptor_in_local = false;
  bool dependency_descriptor_in_local = false;
  bool abs_capture_time_in_local = false;
  auto take_theirs = [&](const RtpExtension& theirs) {
    // We MUST use their id in the answer; the suggester records it.
    suggester.AddRtpHeaderExtensionMapping(mid, theirs, /*local=*/false);
    negotiated_extensions->push_back(theirs);
  };
  for (const RtpExtension& ours : local_extensions) {
    if (ours.uri == RtpExtension::kGenericFrameDescriptorUri00) {
      frame_descriptor_in_local = true;
    } else if (ours.uri == RtpExtension::kDependencyDescriptorUri) {
      dependency_descriptor_in_local = true;
    } else if (ours.uri == RtpExtension::kAbsoluteCaptureTimeUri) {
      abs_capture_time_in_local = true;
    }
    const RtpExtension* theirs = FindHeaderExtensionByUriDiscardUnsupported(offered_extensions, ours.uri, filter);
    if (theirs && theirs->encrypt == ours.encrypt) take_theirs(*theirs);
  }
  // Frame descriptors and absolute capture time: answered when offered even if not present locally.
  if (!dependency_descriptor_in_local) {
    if (const RtpExtension* theirs = FindHeaderExtensionByUriDiscardUnsupported(
            offered_extensions, RtpExtension::kDependencyDescriptorUri, filter)) {
      take_theirs(*theirs);
    }
  }
  if (!frame_descriptor_in_local) {
    if (const RtpExtension* theirs = FindHeaderExtensionByUriDiscardUnsupported(
            offered_extensions, RtpExtension::kGenericFrameDescriptorUri00, filter)) {
      take_theirs(*theirs);
    }
  }
  if (!abs_capture_time_in_local) {
    if (const RtpExtension* theirs = FindHeaderExtensionByUriDiscardUnsupported(
            offered_extensions, RtpExtension::kAbsoluteCaptureTimeUri, filter)) {
      take_theirs(*theirs);
    }
  }
}

void SetCodecsInAnswer(const MediaContentDescription* offer, const std::vector<Codec>& local_codecs,
                       const MediaDescriptionOptions& media_description_options,
                       const MediaSessionOptions& session_options, UniqueRandomIdGenerator* ssrc_generator,
                       StreamParamsVec* current_streams, MediaContentDescription* answer) {
  answer->AddCodecs(local_codecs);
  answer->set_protocol(offer->protocol());
  AddStreamParams(media_description_options.sender_options, session_options.rtcp_cname, ssrc_generator,
                  current_streams, answer);
}

void CreateMediaContentAnswer(const MediaContentDescription* offer,
                              const MediaDescriptionOptions& media_description_options,
                              const MediaSessionOptions& session_options,
                              const RtpHeaderExtensions& local_rtp_extensions,
                              bool enable_encrypted_rtp_header_extensions, MediaContentDescription* answer,
                              SdpPayloadTypeSuggester& suggester) {
  answer->set_extmap_allow_mixed_level(offer->extmap_allow_mixed_level());
  const ExtensionFilter extensions_filter = enable_encrypted_rtp_header_extensions
                                                ? ExtensionFilter::kPreferEncryptedExtension
                                                : ExtensionFilter::kDiscardEncryptedExtension;
  // Local extensions, filtered by the options' capabilities and direction.
  RtpHeaderExtensions local_rtp_extensions_to_reply_with;
  for (const auto& extension_with_id : local_rtp_extensions) {
    for (const auto& extension : media_description_options.header_extensions) {
      if (extension_with_id.uri == extension.uri && extension_with_id.encrypt == extension.preferred_encrypt &&
          extension.direction != RtpTransceiverDirection::kStopped) {
        local_rtp_extensions_to_reply_with.push_back(extension_with_id);
        break;
      }
    }
  }
  RtpHeaderExtensions negotiated_rtp_extensions;
  NegotiateRtpHeaderExtensions(local_rtp_extensions_to_reply_with, offer->rtp_header_extensions(), extensions_filter,
                               &negotiated_rtp_extensions, suggester, media_description_options.mid);
  answer->set_rtp_header_extensions(negotiated_rtp_extensions);
  answer->set_rtcp_mux(session_options.rtcp_mux_enabled && offer->rtcp_mux());
  answer->set_rtcp_reduced_size(offer->rtcp_reduced_size());
  answer->set_receive_non_sender_rtt(offer->receive_non_sender_rtt());
  answer->set_remote_estimate(offer->remote_estimate());
  AddSimulcastToMediaDescription(media_description_options, answer);
  answer->set_direction(NegotiateRtpTransceiverDirection(offer->direction(), media_description_options.direction));
  answer->set_sframe_enabled(offer->sframe_enabled() && media_description_options.sframe_enabled);
}

bool IsMediaProtocolSupported(MediaType type, std::string_view protocol, bool secure_transport) {
  // Not all applications serialize and deserialize the media protocol, so an empty one is accepted.
  if (protocol.empty()) return true;
  if (type == MediaType::DATA) return secure_transport ? IsDtlsSctp(protocol) : IsPlainSctp(protocol);
  // Non-DTLS RTP protocols are allowed even with DTLS, as JSEP specifies.
  return secure_transport ? (IsDtlsRtp(protocol) || IsPlainRtp(protocol)) : IsPlainRtp(protocol);
}

void SetMediaProtocol(bool secure_transport, MediaContentDescription* desc) {
  desc->set_protocol(secure_transport ? kMediaProtocolDtlsSavpf : kMediaProtocolAvpf);
}

const TransportDescription* GetTransportDescription(const std::string& content_name,
                                                    const SessionDescription* current_description) {
  if (!current_description) return nullptr;
  const TransportInfo* info = current_description->GetTransportInfoByName(content_name);
  return info ? &info->description : nullptr;
}

}  // namespace

MediaSessionDescriptionFactory::MediaSessionDescriptionFactory(
    UniqueRandomIdGenerator* ssrc_generator, const TransportDescriptionFactory* transport_desc_factory,
    const CodecVendor* codec_vendor, SdpPayloadTypeSuggester* suggester)
    : owned_ssrc_generator_(ssrc_generator ? nullptr : std::make_unique<UniqueRandomIdGenerator>()),
      ssrc_generator_(ssrc_generator ? ssrc_generator : owned_ssrc_generator_.get()),
      transport_desc_factory_(transport_desc_factory),
      codec_vendor_(codec_vendor),
      suggester_(suggester) {}

RtpHeaderExtensions MediaSessionDescriptionFactory::filtered_rtp_header_extensions(
    RtpHeaderExtensions extensions) const {
  if (!is_unified_plan_) {
    // Remove the extensions only supported with Unified Plan.
    std::erase_if(extensions, [](const RtpExtension& extension) {
      return extension.uri == RtpExtension::kMidUri || extension.uri == RtpExtension::kRidUri ||
             extension.uri == RtpExtension::kRepairedRidUri;
    });
  }
  return extensions;
}

std::unique_ptr<SessionDescription> MediaSessionDescriptionFactory::CreateOfferOrError(
    const MediaSessionOptions& session_options, const SessionDescription* current_description,
    std::string* error) const {
  // Must have options for each existing section.
  if (current_description &&
      current_description->contents().size() > session_options.media_description_options.size()) {
    Fail(error, "Fewer media description options than sections in the current description");
    return nullptr;
  }
  IceCredentialsIterator ice_credentials(session_options.pooled_ice_credentials);
  std::vector<const ContentInfo*> current_active_contents;
  if (current_description) current_active_contents = GetActiveContents(*current_description, session_options);
  StreamParamsVec current_streams = GetCurrentStreamParams(current_active_contents);
  AudioVideoRtpHeaderExtensions extensions_with_ids = GetOfferedRtpHeaderExtensionsWithIds(
      current_active_contents, session_options.offer_extmap_allow_mixed, session_options.media_description_options);

  auto offer = std::make_unique<SessionDescription>();
  // Iterate through the media description options, matching with existing media descriptions.
  size_t msection_index = 0;
  for (const MediaDescriptionOptions& media_description_options : session_options.media_description_options) {
    const ContentInfo* current_content = nullptr;
    if (current_description && msection_index < current_description->contents().size()) {
      current_content = &current_description->contents()[msection_index];
    }
    bool ok = false;
    switch (media_description_options.type) {
      case MediaType::AUDIO:
      case MediaType::VIDEO:
        ok = AddRtpContentForOffer(media_description_options, session_options, current_content, current_description,
                                   media_description_options.type == MediaType::AUDIO ? extensions_with_ids.audio
                                                                                      : extensions_with_ids.video,
                                   &current_streams, offer.get(), &ice_credentials, error);
        break;
      case MediaType::DATA:
        ok = AddDataContentForOffer(media_description_options, session_options, current_content, current_description,
                                    &current_streams, offer.get(), &ice_credentials, error);
        break;
      case MediaType::UNSUPPORTED:
        ok = AddUnsupportedContentForOffer(media_description_options, current_content, current_description,
                                           offer.get(), &ice_credentials, error);
        break;
      default:
        ok = Fail(error, "Unknown media type");
    }
    if (!ok) return nullptr;
    ++msection_index;
  }

  // Bundle the contents together if asked to, and update the parameters BUNDLE needs.
  if (session_options.bundle_enabled) {
    ContentGroup offer_bundle(GROUP_TYPE_BUNDLE);
    for (const ContentInfo& content : offer->contents()) {
      if (!content.rejected) offer_bundle.AddContentName(content.mid());
    }
    if (!offer_bundle.content_names().empty()) {
      offer->AddGroup(offer_bundle);
      if (!UpdateTransportInfoForBundle(offer_bundle, offer.get())) {
        Fail(error, "CreateOffer failed to UpdateTransportInfoForBundle");
        return nullptr;
      }
    }
  }
  // Unified Plan signals MSIDs with both a=msid and a=ssrc lines, for older (Plan B) answerers.
  if (is_unified_plan_) {
    offer->set_msid_signaling(kMsidSignalingSemantic | kMsidSignalingMediaSection | kMsidSignalingSsrcAttribute);
  } else {
    offer->set_msid_signaling(kMsidSignalingSemantic | kMsidSignalingSsrcAttribute);
  }
  offer->set_extmap_allow_mixed(session_options.offer_extmap_allow_mixed);
  offer->set_cryptex(false);  // CryptexPolicy::kDisabled.
  return offer;
}

std::unique_ptr<SessionDescription> MediaSessionDescriptionFactory::CreateAnswerOrError(
    const SessionDescription* offer, const MediaSessionOptions& session_options,
    const SessionDescription* current_description, std::string* error) const {
  if (!offer) {
    Fail(error, "Called without offer.");
    return nullptr;
  }
  // Must have options for exactly as many sections as in the offer.
  if (offer->contents().size() != session_options.media_description_options.size()) {
    Fail(error, "Media description options don't match the offer's sections");
    return nullptr;
  }
  IceCredentialsIterator ice_credentials(session_options.pooled_ice_credentials);
  std::vector<const ContentInfo*> current_active_contents;
  if (current_description) current_active_contents = GetActiveContents(*current_description, session_options);
  StreamParamsVec current_streams = GetCurrentStreamParams(current_active_contents);
  // RFC 8888 ("ack ccfb") is not accepted (WebRTC-RFC8888CongestionControlFeedback is off).

  auto answer = std::make_unique<SessionDescription>();
  // As many answer BUNDLE groups as offer BUNDLE groups, even if empty; `offer_bundles`, `answer_bundles` and
  // `bundle_transports` share indices.
  std::vector<const ContentGroup*> offer_bundles = offer->GetGroupsByName(GROUP_TYPE_BUNDLE);
  std::vector<ContentGroup> answer_bundles;
  std::vector<std::unique_ptr<TransportInfo>> bundle_transports;
  for (size_t i = 0; i < offer_bundles.size(); ++i) {
    answer_bundles.emplace_back(GROUP_TYPE_BUNDLE);
    bundle_transports.emplace_back(nullptr);
  }
  answer->set_extmap_allow_mixed(offer->extmap_allow_mixed());
  answer->set_cryptex(false);  // CryptexPolicy::kDisabled.

  size_t msection_index = 0;
  for (const MediaDescriptionOptions& media_description_options : session_options.media_description_options) {
    const ContentInfo* offer_content = &offer->contents()[msection_index];
    // Media types and MIDs must match between the remote offer and the options.
    if (!IsMediaContentOfType(offer_content, media_description_options.type) ||
        media_description_options.mid != offer_content->mid()) {
      Fail(error, "Media description options don't match the offer's sections");
      return nullptr;
    }
    // The BUNDLE group this mid belongs to, if any.
    std::optional<size_t> bundle_index;
    for (size_t i = 0; i < offer_bundles.size(); ++i) {
      if (offer_bundles[i]->HasContentName(media_description_options.mid)) {
        bundle_index = i;
        break;
      }
    }
    TransportInfo* bundle_transport = bundle_index ? bundle_transports[*bundle_index].get() : nullptr;
    const ContentInfo* current_content = nullptr;
    if (current_description && msection_index < current_description->contents().size()) {
      current_content = &current_description->contents()[msection_index];
    }
    const RtpHeaderExtensions header_extensions =
        UnstoppedRtpHeaderExtensionsFromCapabilities(media_description_options.header_extensions);
    bool ok = false;
    switch (media_description_options.type) {
      case MediaType::AUDIO:
      case MediaType::VIDEO:
        ok = AddRtpContentForAnswer(media_description_options, session_options, offer_content, offer, current_content,
                                    current_description, bundle_transport, header_extensions, &current_streams,
                                    answer.get(), &ice_credentials, error);
        break;
      case MediaType::DATA:
        ok = AddDataContentForAnswer(media_description_options, session_options, offer_content, offer,
                                     current_content, current_description, bundle_transport, &current_streams,
                                     answer.get(), &ice_credentials, error);
        break;
      case MediaType::UNSUPPORTED:
        ok = AddUnsupportedContentForAnswer(media_description_options, offer_content, offer, current_description,
                                            bundle_transport, answer.get(), &ice_credentials, error);
        break;
      default:
        ok = Fail(error, "Unknown media type");
    }
    if (!ok) return nullptr;
    ++msection_index;
    // Add the new m= section to the answer's BUNDLE group if it can be.
    ContentInfo& added = answer->contents().back();
    if (!added.rejected && session_options.bundle_enabled && bundle_index) {
      answer_bundles[*bundle_index].AddContentName(added.mid());
      bundle_transports[*bundle_index] = std::make_unique<TransportInfo>(*answer->GetTransportInfoByName(added.mid()));
    }
  }
  // RFC 5888: an answer to an offer with an understood a=group line has an a=group line with the same semantics.
  for (const ContentGroup& answer_bundle : answer_bundles) {
    answer->AddGroup(answer_bundle);
    // Share the same ICE credentials and DTLS role across all contents, as BUNDLE requires.
    if (answer_bundle.FirstContentName() && !UpdateTransportInfoForBundle(answer_bundle, answer.get())) {
      Fail(error, "CreateAnswer failed to UpdateTransportInfoForBundle.");
      return nullptr;
    }
  }
  if (is_unified_plan_) {
    const int msid_signaling = offer->msid_signaling();
    if (msid_signaling == (kMsidSignalingSemantic | kMsidSignalingMediaSection | kMsidSignalingSsrcAttribute)) {
      // Both a=msid and a=ssrc MSID: probably a Unified Plan endpoint, so just a=msid.
      answer->set_msid_signaling(kMsidSignalingSemantic | kMsidSignalingMediaSection);
    } else if (msid_signaling == (kMsidSignalingSemantic | kMsidSignalingSsrcAttribute) ||
               msid_signaling == kMsidSignalingSsrcAttribute) {
      // Only a=ssrc MSID: probably a Plan B endpoint, so just a=ssrc MSID.
      answer->set_msid_signaling(kMsidSignalingSemantic | kMsidSignalingSsrcAttribute);
    } else {
      // An empty offer, a data channel only offer, or media that is recvonly or inactive from the remote's view.
      answer->set_msid_signaling(kMsidSignalingSemantic | kMsidSignalingMediaSection);
    }
  } else {
    answer->set_msid_signaling(kMsidSignalingSemantic | kMsidSignalingSsrcAttribute);
  }
  return answer;
}

MediaSessionDescriptionFactory::AudioVideoRtpHeaderExtensions
MediaSessionDescriptionFactory::GetOfferedRtpHeaderExtensionsWithIds(
    const std::vector<const ContentInfo*>& current_active_contents, bool extmap_allow_mixed,
    const std::vector<MediaDescriptionOptions>& media_description_options) const {
  // All header extensions come from the same id range, to avoid problems with BUNDLE. extmap-allow-mixed signals
  // that two-byte header extensions are supported.
  const RtpTransceiverIdDomain id_domain =
      extmap_allow_mixed ? RtpTransceiverIdDomain::kTwoByteAllowed : RtpTransceiverIdDomain::kOneByteOnly;
  RtpHeaderExtensions all_encountered_extensions;
  AudioVideoRtpHeaderExtensions offered_extensions;
  // First the extensions of the current description, so that their ids are not reused for a new media type.
  for (const ContentInfo* content : current_active_contents) {
    if (IsMediaContentOfType(content, MediaType::AUDIO)) {
      MergeRtpHdrExts(content->media_description()->rtp_header_extensions(), enable_encrypted_rtp_header_extensions_,
                      &offered_extensions.audio, &all_encountered_extensions, *suggester_, content->mid(), id_domain);
    } else if (IsMediaContentOfType(content, MediaType::VIDEO)) {
      MergeRtpHdrExts(content->media_description()->rtp_header_extensions(), enable_encrypted_rtp_header_extensions_,
                      &offered_extensions.video, &all_encountered_extensions, *suggester_, content->mid(), id_domain);
    }
  }
  // Then those of the options that the current description doesn't have.
  for (const auto& entry : media_description_options) {
    RtpHeaderExtensions filtered_extensions = filtered_rtp_header_extensions(
        UnstoppedOrPresentRtpHeaderExtensions(entry.header_extensions, all_encountered_extensions));
    if (entry.type == MediaType::AUDIO) {
      MergeRtpHdrExts(filtered_extensions, enable_encrypted_rtp_header_extensions_, &offered_extensions.audio,
                      &all_encountered_extensions, *suggester_, entry.mid, id_domain);
    } else if (entry.type == MediaType::VIDEO) {
      MergeRtpHdrExts(filtered_extensions, enable_encrypted_rtp_header_extensions_, &offered_extensions.video,
                      &all_encountered_extensions, *suggester_, entry.mid, id_domain);
    }
  }
  return offered_extensions;
}

bool MediaSessionDescriptionFactory::AddTransportOffer(const std::string& content_name,
                                                       const TransportOptions& transport_options,
                                                       const SessionDescription* current_desc,
                                                       SessionDescription* offer_desc,
                                                       IceCredentialsIterator* ice_credentials,
                                                       std::string* error) const {
  const TransportDescription* current_tdesc = GetTransportDescription(content_name, current_desc);
  std::unique_ptr<TransportDescription> new_tdesc =
      transport_desc_factory_->CreateOffer(transport_options, current_tdesc, ice_credentials);
  // libwebrtc dereferences a null description here; fail instead.
  if (!new_tdesc) return Fail(error, "Failed to AddTransportOffer");
  offer_desc->AddTransportInfo(TransportInfo(content_name, *new_tdesc));
  return true;
}

std::unique_ptr<TransportDescription> MediaSessionDescriptionFactory::CreateTransportAnswer(
    const std::string& content_name, const SessionDescription* offer_desc, const TransportOptions& transport_options,
    const SessionDescription* current_desc, bool require_transport_attributes,
    IceCredentialsIterator* ice_credentials) const {
  const TransportDescription* offer_tdesc = GetTransportDescription(content_name, offer_desc);
  const TransportDescription* current_tdesc = GetTransportDescription(content_name, current_desc);
  return transport_desc_factory_->CreateAnswer(offer_tdesc, transport_options, require_transport_attributes,
                                               current_tdesc, ice_credentials);
}

// The codecs come from the codec vendor: the current section's first (re-offers keep their order and payload
// types), then the other supported codecs for the section's direction.
bool MediaSessionDescriptionFactory::AddRtpContentForOffer(
    const MediaDescriptionOptions& media_description_options, const MediaSessionOptions& session_options,
    const ContentInfo* current_content, const SessionDescription* current_description,
    const RtpHeaderExtensions& header_extensions, StreamParamsVec* current_streams,
    SessionDescription* session_description, IceCredentialsIterator* ice_credentials, std::string* error) const {
  std::optional<std::vector<Codec>> codecs = codec_vendor_->GetNegotiatedCodecsForOffer(
      media_description_options, session_options, current_content, *suggester_);
  if (!codecs) return Fail(error, "Failed to assign payload types for the offer");
  std::unique_ptr<MediaContentDescription> content_description;
  if (media_description_options.type == MediaType::AUDIO) {
    content_description = std::make_unique<AudioContentDescription>();
  } else {
    content_description = std::make_unique<VideoContentDescription>();
  }
  CreateMediaContentOffer(media_description_options, session_options, *codecs, header_extensions, ssrc_generator(),
                          current_streams, content_description.get());
  // Insecure transport should only occur in testing.
  SetMediaProtocol(!transport_desc_factory_->insecure(), content_description.get());
  content_description->set_direction(media_description_options.direction);
  content_description->set_sframe_enabled(media_description_options.sframe_enabled);
  const bool has_codecs = !content_description->codecs().empty();
  session_description->AddContent(media_description_options.mid, MediaProtocolType::kRtp,
                                  media_description_options.stopped || !has_codecs, std::move(content_description));
  return AddTransportOffer(media_description_options.mid, media_description_options.transport_options,
                           current_description, session_description, ice_credentials, error);
}

bool MediaSessionDescriptionFactory::AddDataContentForOffer(
    const MediaDescriptionOptions& media_description_options, const MediaSessionOptions& session_options,
    const ContentInfo* /*current_content*/, const SessionDescription* current_description,
    StreamParamsVec* /*current_streams*/, SessionDescription* desc, IceCredentialsIterator* ice_credentials,
    std::string* error) const {
  auto data = std::make_unique<SctpDataContentDescription>();
  // The protocol is set first so that no SSRCs are generated for SCTP.
  data->set_protocol(kMediaProtocolUdpDtlsSctp);
  data->set_use_sctpmap(session_options.use_obsolete_sctp_sdp);
  data->set_max_message_size(kSctpSendBufferSize);
  CreateContentOffer(media_description_options, session_options, RtpHeaderExtensions(), data.get());
  desc->AddContent(media_description_options.mid, MediaProtocolType::kSctp, media_description_options.stopped,
                   std::move(data));
  return AddTransportOffer(media_description_options.mid, media_description_options.transport_options,
                           current_description, desc, ice_credentials, error);
}

bool MediaSessionDescriptionFactory::AddUnsupportedContentForOffer(
    const MediaDescriptionOptions& media_description_options, const ContentInfo* current_content,
    const SessionDescription* current_description, SessionDescription* desc, IceCredentialsIterator* ice_credentials,
    std::string* error) const {
  // An unsupported section is only offered again, rejected, when the current description has it.
  if (!IsMediaContentOfType(current_content, MediaType::UNSUPPORTED)) {
    return Fail(error, "Unsupported media section without a current one");
  }
  const UnsupportedContentDescription* current_unsupported_description =
      current_content->media_description()->as_unsupported();
  auto unsupported = std::make_unique<UnsupportedContentDescription>(current_unsupported_description->media_type());
  unsupported->set_protocol(current_content->media_description()->protocol());
  desc->AddContent(media_description_options.mid, MediaProtocolType::kOther, /*rejected=*/true,
                   std::move(unsupported));
  return AddTransportOffer(media_description_options.mid, media_description_options.transport_options,
                           current_description, desc, ice_credentials, error);
}

bool MediaSessionDescriptionFactory::AddRtpContentForAnswer(
    const MediaDescriptionOptions& media_description_options, const MediaSessionOptions& session_options,
    const ContentInfo* offer_content, const SessionDescription* offer_description, const ContentInfo* current_content,
    const SessionDescription* current_description, const TransportInfo* bundle_transport,
    const RtpHeaderExtensions& header_extensions, StreamParamsVec* current_streams, SessionDescription* answer,
    IceCredentialsIterator* ice_credentials, std::string* error) const {
  const MediaContentDescription* offer_content_description = offer_content->media_description();
  // A bundled section (bundle_transport set) may lack the DTLS parameters; otherwise they MUST be present, unless
  // the section is rejected.
  std::unique_ptr<TransportDescription> transport = CreateTransportAnswer(
      media_description_options.mid, offer_description, media_description_options.transport_options,
      current_description, !offer_content->rejected && bundle_transport == nullptr, ice_credentials);
  if (!transport) return Fail(error, "Failed to create transport answer, transport is missing");

  // Codecs for the offer's direction and the one the answer takes.
  const auto wants_rtd = media_description_options.direction;
  const auto offer_rtd = offer_content_description->direction();
  const auto answer_rtd = NegotiateRtpTransceiverDirection(offer_rtd, wants_rtd);
  std::optional<std::vector<Codec>> codecs = codec_vendor_->GetNegotiatedCodecsForAnswer(
      media_description_options, session_options, offer_rtd, answer_rtd, current_content,
      offer_content_description->codecs(), *suggester_);
  if (!codecs) return Fail(error, "Failed to assign payload types for the answer");
  // Media codecs in common?
  const bool has_usable_media_codecs = std::any_of(
      codecs->begin(), codecs->end(), [](const Codec& c) { return c.IsMediaCodec() && !IsComfortNoiseCodec(c); });

  std::unique_ptr<MediaContentDescription> answer_content;
  if (media_description_options.type == MediaType::AUDIO) {
    answer_content = std::make_unique<AudioContentDescription>();
  } else {
    answer_content = std::make_unique<VideoContentDescription>();
  }
  SetCodecsInAnswer(offer_content_description, *codecs, media_description_options, session_options, ssrc_generator(),
                    current_streams, answer_content.get());
  CreateMediaContentAnswer(offer_content_description, media_description_options, session_options,
                           filtered_rtp_header_extensions(header_extensions), enable_encrypted_rtp_header_extensions_,
                           answer_content.get(), *suggester_);
  const bool secure = bundle_transport ? bundle_transport->description.secure() : transport->secure();
  const bool rejected = media_description_options.stopped || offer_content->rejected || !has_usable_media_codecs ||
                        !IsMediaProtocolSupported(MediaType::AUDIO, answer_content->protocol(), secure);
  answer->AddTransportInfo(TransportInfo(media_description_options.mid, *transport));
  answer->AddContent(media_description_options.mid, offer_content->type, rejected, std::move(answer_content));
  return true;
}

bool MediaSessionDescriptionFactory::AddDataContentForAnswer(
    const MediaDescriptionOptions& media_description_options, const MediaSessionOptions& session_options,
    const ContentInfo* offer_content, const SessionDescription* offer_description,
    const ContentInfo* /*current_content*/, const SessionDescription* current_description,
    const TransportInfo* bundle_transport, StreamParamsVec* /*current_streams*/, SessionDescription* answer,
    IceCredentialsIterator* ice_credentials, std::string* error) const {
  std::unique_ptr<TransportDescription> data_transport = CreateTransportAnswer(
      media_description_options.mid, offer_description, media_description_options.transport_options,
      current_description, !offer_content->rejected && bundle_transport == nullptr, ice_credentials);
  if (!data_transport) return Fail(error, "Failed to create transport answer, data transport is missing");
  const SctpDataContentDescription* offer_data_description = offer_content->media_description()->as_sctp();
  if (!offer_data_description) return Fail(error, "Non-SCTP data content found");
  auto data_answer = std::make_unique<SctpDataContentDescription>();
  // Respond with the offerer's proto, whatever it is.
  data_answer->set_protocol(offer_data_description->protocol());
  // Our max message size or the remote's, whichever is smaller; 0 means "any size", answered with ours.
  if (offer_data_description->max_message_size() <= 0) {
    data_answer->set_max_message_size(kSctpSendBufferSize);
  } else {
    data_answer->set_max_message_size(std::min(offer_data_description->max_message_size(), kSctpSendBufferSize));
  }
  CreateMediaContentAnswer(offer_data_description, media_description_options, session_options, RtpHeaderExtensions(),
                           enable_encrypted_rtp_header_extensions_, data_answer.get(), *suggester_);
  // Respond with sctpmap if the offer uses sctpmap.
  data_answer->set_use_sctpmap(offer_data_description->use_sctpmap());
  const bool secure = bundle_transport ? bundle_transport->description.secure() : data_transport->secure();
  const bool rejected = media_description_options.stopped || offer_content->rejected ||
                        !IsMediaProtocolSupported(MediaType::DATA, data_answer->protocol(), secure);
  answer->AddTransportInfo(TransportInfo(media_description_options.mid, *data_transport));
  answer->AddContent(media_description_options.mid, offer_content->type, rejected, std::move(data_answer));
  return true;
}

bool MediaSessionDescriptionFactory::AddUnsupportedContentForAnswer(
    const MediaDescriptionOptions& media_description_options, const ContentInfo* offer_content,
    const SessionDescription* offer_description, const SessionDescription* current_description,
    const TransportInfo* bundle_transport, SessionDescription* answer, IceCredentialsIterator* ice_credentials,
    std::string* error) const {
  std::unique_ptr<TransportDescription> unsupported_transport = CreateTransportAnswer(
      media_description_options.mid, offer_description, media_description_options.transport_options,
      current_description, !offer_content->rejected && bundle_transport == nullptr, ice_credentials);
  if (!unsupported_transport) {
    return Fail(error, "Failed to create transport answer, unsupported transport is missing");
  }
  const UnsupportedContentDescription* offer_unsupported_description =
      offer_content->media_description()->as_unsupported();
  auto unsupported_answer =
      std::make_unique<UnsupportedContentDescription>(offer_unsupported_description->media_type());
  unsupported_answer->set_protocol(offer_unsupported_description->protocol());
  answer->AddTransportInfo(TransportInfo(media_description_options.mid, *unsupported_transport));
  answer->AddContent(media_description_options.mid, offer_content->type, /*rejected=*/true,
                     std::move(unsupported_answer));
  return true;
}

bool IsMediaContent(const ContentInfo* content) {
  return content && (content->type == MediaProtocolType::kRtp || content->type == MediaProtocolType::kSctp);
}
bool IsAudioContent(const ContentInfo* content) { return IsMediaContentOfType(content, MediaType::AUDIO); }
bool IsVideoContent(const ContentInfo* content) { return IsMediaContentOfType(content, MediaType::VIDEO); }
bool IsDataContent(const ContentInfo* content) { return IsMediaContentOfType(content, MediaType::DATA); }
bool IsUnsupportedContent(const ContentInfo* content) { return IsMediaContentOfType(content, MediaType::UNSUPPORTED); }

const ContentInfo* GetFirstMediaContent(const ContentInfos& contents, MediaType media_type) {
  for (const ContentInfo& content : contents) {
    if (IsMediaContentOfType(&content, media_type)) return &content;
  }
  return nullptr;
}
const ContentInfo* GetFirstAudioContent(const ContentInfos& contents) {
  return GetFirstMediaContent(contents, MediaType::AUDIO);
}
const ContentInfo* GetFirstVideoContent(const ContentInfos& contents) {
  return GetFirstMediaContent(contents, MediaType::VIDEO);
}
const ContentInfo* GetFirstDataContent(const ContentInfos& contents) {
  return GetFirstMediaContent(contents, MediaType::DATA);
}
const ContentInfo* GetFirstAudioContent(const SessionDescription* sdesc) {
  return sdesc ? GetFirstAudioContent(sdesc->contents()) : nullptr;
}
const ContentInfo* GetFirstVideoContent(const SessionDescription* sdesc) {
  return sdesc ? GetFirstVideoContent(sdesc->contents()) : nullptr;
}
const ContentInfo* GetFirstDataContent(const SessionDescription* sdesc) {
  return sdesc ? GetFirstDataContent(sdesc->contents()) : nullptr;
}
const AudioContentDescription* GetFirstAudioContentDescription(const SessionDescription* sdesc) {
  const ContentInfo* content = GetFirstAudioContent(sdesc);
  return content ? content->media_description()->as_audio() : nullptr;
}
const VideoContentDescription* GetFirstVideoContentDescription(const SessionDescription* sdesc) {
  const ContentInfo* content = GetFirstVideoContent(sdesc);
  return content ? content->media_description()->as_video() : nullptr;
}
const SctpDataContentDescription* GetFirstSctpDataContentDescription(const SessionDescription* sdesc) {
  const ContentInfo* content = GetFirstDataContent(sdesc);
  return content ? content->media_description()->as_sctp() : nullptr;
}
AudioContentDescription* GetFirstAudioContentDescription(SessionDescription* sdesc) {
  return const_cast<AudioContentDescription*>(
      GetFirstAudioContentDescription(static_cast<const SessionDescription*>(sdesc)));
}
VideoContentDescription* GetFirstVideoContentDescription(SessionDescription* sdesc) {
  return const_cast<VideoContentDescription*>(
      GetFirstVideoContentDescription(static_cast<const SessionDescription*>(sdesc)));
}
SctpDataContentDescription* GetFirstSctpDataContentDescription(SessionDescription* sdesc) {
  return const_cast<SctpDataContentDescription*>(
      GetFirstSctpDataContentDescription(static_cast<const SessionDescription*>(sdesc)));
}

}  // namespace sm::pc
