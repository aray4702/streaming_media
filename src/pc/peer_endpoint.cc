// The transport half of libwebrtc M157's pc/sdp_offer_answer.cc and pc/peer_connection.cc for C6's subset (#1064);
// see sm/pc/peer_endpoint.h. The data channels (C7 part 5, #1091) follow pc/data_channel_controller.cc and the SCTP
// half of pc/sdp_offer_answer.cc (GetOptionsForOffer / MaybeNegotiateSctp, GetOptionsForAnswer,
// UpdateDataChannelTransport, PushdownMediaDescription) and pc/peer_connection.cc (StartSctpTransport,
// DestroyDataChannelTransport). Transceivers, renegotiation and remote tracks (C18, #1160) follow the Unified Plan half
// of pc/sdp_offer_answer.cc: GetOptionsForUnifiedPlanOffer / GetOptionsForUnifiedPlanAnswer,
// GetMediaDescriptionOptionsForTransceiver, UpdateTransceiversAndDataChannels / AssociateTransceiver /
// FindAvailableTransceiverToReceive, ApplyRemoteDescriptionUpdateTransceiverState / ProcessRemovalOfRemoteTrack and
// RemoveStoppedTransceivers, with call/call.cc's receive stream creation and destruction.
#include "sm/pc/peer_endpoint.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <utility>

#include "sm/pc/codec.h"
#include "sm/pc/media_options.h"
#include "sm/pc/rtp_parameters.h"
#include "sm/sync/header_extensions.h"
#include "sm/tls/crypto_random.h"

#if defined(SM_WITH_DATACHANNEL) && SM_WITH_DATACHANNEL
#define SM_ENDPOINT_DATA_CHANNELS 1
#include "sm/pc/data_channel_controller.h"
#include "sm/pc/sctp_data_channel.h"
#include "sm/sctp/dcsctp_transport.h"
#include "sm/sctp/sctp_transport.h"
#else
#define SM_ENDPOINT_DATA_CHANNELS 0
#endif

namespace sm::pc {

namespace {

// PeerConnection's kRtcpCnameLength.
constexpr size_t kRtcpCnameLength = 16;

bool Fail(std::string* error, std::string message) {
  if (error) *error = std::move(message);
  return false;
}

// absl::EqualsIgnoreCase, as codecs are matched by name.
bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

// The answer's codec named `name` (the first), or null.
const Codec* FindCodec(const std::vector<Codec>& codecs, std::string_view name) {
  for (const Codec& c : codecs) {
    if (EqualsIgnoreCase(c.name, name)) return &c;
  }
  return nullptr;
}

// The RTX codec whose apt is `pt`.
const Codec* FindRtxFor(const std::vector<Codec>& codecs, int pt) {
  for (const Codec& c : codecs) {
    std::string apt;
    if (EqualsIgnoreCase(c.name, kRtxCodecName) && c.GetParam(kCodecParamAssociatedPayloadType, &apt) &&
        apt == std::to_string(pt)) {
      return &c;
    }
  }
  return nullptr;
}

const StreamParams* FirstStream(const MediaContentDescription* d) {
  return d && !d->streams().empty() && d->streams()[0].has_ssrcs() ? &d->streams()[0] : nullptr;
}

// The first data (m=application) section of `desc`, or null.
const ContentInfo* FirstDataContent(const SessionDescription& desc) {
  for (const ContentInfo& c : desc.contents()) {
    if (c.media_description() && c.media_description()->type() == MediaType::DATA) return &c;
  }
  return nullptr;
}

bool IsAudioOrVideo(MediaType t) { return t == MediaType::AUDIO || t == MediaType::VIDEO; }

const ContentInfo* ContentAt(const SessionDescriptionInterface* d, size_t i) {
  if (!d || i >= d->description()->contents().size()) return nullptr;
  return &d->description()->contents()[i];
}

// The section's negotiated MID extension.
bool HasMidExtension(const MediaContentDescription* d) {
  if (!d) return false;
  for (const RtpExtension& e : d->rtp_header_extensions()) {
    if (e.uri == RtpExtension::kMidUri) return true;
  }
  return false;
}

}  // namespace

#if SM_ENDPOINT_DATA_CHANNELS
struct PeerEndpoint::DataChannels {
  DataChannelController controller;
  std::unique_ptr<sctp::SctpTransport> sctp;
  ~DataChannels() {
    if (sctp) controller.TeardownDataChannelTransport_n(RtcError(RtcErrorType::kOperationErrorWithData, "Transport closed"));
    sctp.reset();
  }
};
#else
struct PeerEndpoint::DataChannels {};
#endif

// BaseChannel's demuxer sink: what the transport demuxes to one section goes to the RTP session.
class PeerEndpoint::ChannelSink : public RtpPacketSinkInterface {
 public:
  explicit ChannelSink(PeerEndpoint* endpoint) : endpoint_(endpoint) {}
  void OnRtpPacket(const RtpPacketReceived& packet) override {
    endpoint_->Deliver(packet.buffer, /*rtcp=*/false, packet.arrival_time);
  }

 private:
  PeerEndpoint* endpoint_;
};

// RtpTransceiver (pc/rtp_transceiver.h), with its sender's options, its receiver's remote track and its channel's
// demuxer sink on the BUNDLE transport.
struct PeerEndpoint::Transceiver {
  MediaType kind = MediaType::AUDIO;
  bool created_by_addtrack = false;  // The config's audio and video.
  RtpTransceiverDirection direction = RtpTransceiverDirection::kSendRecv;
  std::optional<RtpTransceiverDirection> current_direction;
  std::optional<std::string> mid;
  std::optional<size_t> mline_index;
  bool stopping = false;
  bool stopped = false;
  bool has_ever_been_used_to_send = false;
  // The sender: its id and msid streams, and the host's fixed SSRCs (external_media).
  std::string track_id;
  std::vector<std::string> stream_ids;
  std::optional<uint32_t> ssrc, rtx_ssrc;
  // Simulcast send (C17): the RIDs of the sender's encodings still in the simulcast envelope (the config's
  // video_simulcast_rids; DisableSimulcastInSender and DisableEncodingLayers take encodings out), and whether the
  // session sends them (an answer was applied with this transceiver's RIDs).
  std::vector<std::string> send_rids;
  bool simulcast_negotiated = false;
  // The receiver: the remote track being received, and the decoder the factory made for it.
  std::optional<RemoteTrack> remote;
  std::unique_ptr<rtp::AudioDecoder> decoder;
  bool uses_config_decoder = false;
  // The channel's demuxer sink (its MID and the remote SSRCs) on the BUNDLE transport.
  std::unique_ptr<ChannelSink> sink;
  bool sink_registered = false;
  std::vector<uint32_t> sink_ssrcs;
};

PeerEndpoint::PeerEndpoint(PeerEndpointConfig config, Timestamp now)
    : config_(std::move(config)),
      certificate_(config_.certificate ? config_.certificate : tls::SslIdentity::Create(config_.certificate_name)),
      transport_controller_(config_.allocator, config_.transport) {
  transport_desc_factory_.set_certificate(certificate_);
  codec_vendor_ = std::make_unique<CodecVendor>(DefaultAudioCodecs(), DefaultAudioCodecs(), DefaultVideoCodecs(),
                                                DefaultVideoCodecs());
  session_desc_factory_ = std::make_unique<MediaSessionDescriptionFactory>(&ssrc_generator_, &transport_desc_factory_,
                                                                           codec_vendor_.get(), &suggester_);
  // PeerConnection sets the factory from CryptoOptions::Srtp::enable_encrypted_rtp_header_extensions.
  session_desc_factory_->set_enable_encrypted_rtp_header_extensions(
      config_.transport.enable_encrypted_rtp_header_extensions);
  transport_controller_.SetLocalCertificate(certificate_);
  transport_controller_.SubscribeIceCandidateGathered(
      [this](const std::string& name, const p2p::Candidate& c) { OnCandidateGathered(name, c); });
  // WebRtcSessionDescriptionFactory: a random 64-bit session id (CreateRandomId64), as decimal.
  const uint64_t id = (uint64_t{tls::CreateRandomNonZeroId()} << 32) | tls::CreateRandomNonZeroId();
  session_id_ = std::to_string(id & 0x7FFFFFFFFFFFFFFFull);
  // PeerConnection's one CNAME for every sender (GenerateRtcpCname).
  rtcp_cname_ = tls::CreateRandomString(kRtcpCnameLength);
  last_now_ = now;
  // The app's AddTrack: one sendrecv transceiver per kind the config asks for.
  if (config_.audio) {
    auto t = std::make_unique<Transceiver>();
    t->kind = MediaType::AUDIO;
    t->created_by_addtrack = true;
    t->track_id = config_.audio_track_id;
    t->stream_ids = {config_.stream_id};
    t->ssrc = config_.audio_ssrc;
    transceivers_.push_back(std::move(t));
  }
  if (config_.video) {
    auto t = std::make_unique<Transceiver>();
    t->kind = MediaType::VIDEO;
    t->created_by_addtrack = true;
    t->track_id = config_.video_track_id;
    t->stream_ids = {config_.stream_id};
    t->ssrc = config_.video_ssrc;
    if (config_.video_ssrc) t->rtx_ssrc = config_.video_rtx_ssrc;
    if (config_.video_simulcast_rids.size() > 1) t->send_rids = config_.video_simulcast_rids;
    transceivers_.push_back(std::move(t));
  }
#if SM_ENDPOINT_DATA_CHANNELS
  if (config_.enable_data_channels) {
    data_channels_ = std::make_unique<DataChannels>();
    data_channels_->controller.SetNow(now);
  }
#endif
}

PeerEndpoint::~PeerEndpoint() {
  for (auto& t : transceivers_) {
    if (t) UnregisterSink(*t);
  }
  // The SCTP transport goes before the DTLS transport under it.
  data_channels_.reset();
}

// UniqueStringGenerator::GenerateString: the next number that no description has used as a MID.
std::string PeerEndpoint::GenerateMid() {
  for (;;) {
    std::string mid = std::to_string(next_mid_++);
    if (std::find(known_mids_.begin(), known_mids_.end(), mid) == known_mids_.end()) {
      known_mids_.push_back(mid);
      return mid;
    }
  }
}

PeerEndpoint::Transceiver* PeerEndpoint::FindByMid(std::string_view mid) const {
  for (const auto& t : transceivers_) {
    if (t && t->mid && *t->mid == mid) return t.get();
  }
  return nullptr;
}

// GetMediaDescriptionOptionsForTransceiver: the msid (sender) is signaled while the transceiver sends, and once it
// has sent, in every later description until it is stopped (JSEP).
MediaDescriptionOptions PeerEndpoint::OptionsForTransceiver(const Transceiver& t, const std::string& mid,
                                                            bool offer) const {
  const bool stopped = offer ? (t.stopping || t.stopped) : t.stopped;
  MediaDescriptionOptions o(t.kind, mid, stopped ? RtpTransceiverDirection::kInactive : t.direction, stopped);
  o.header_extensions =
      t.kind == MediaType::AUDIO ? DefaultAudioRtpHeaderExtensions() : DefaultVideoRtpHeaderExtensions();
  if (stopped || (!RtpTransceiverDirectionHasSend(t.direction) && !t.has_ever_been_used_to_send)) return o;
  if (t.kind == MediaType::AUDIO) {
    o.AddAudioSender(t.track_id, t.stream_ids);
  } else if (!t.send_rids.empty() && (offer || t.simulcast_negotiated)) {
    // Simulcast (C17): a send RID per encoding and the simulcast layers, paused where the encoding is inactive; with
    // RIDs, num_sim_layers is 0 (no SSRCs: the layers' SSRCs are made when the first answer is applied, and every
    // later description reuses this stream, AddStreamParams). A subsequent offer or answer signals the same RIDs, so
    // an SFU's re-offer keeps the layers (C18). An answer signals them only once they are negotiated: a remote
    // offer's simulcast section never takes this transceiver (AssociateTransceiver), so a first answer has none.
    std::vector<RidDescription> rids;
    SimulcastLayerList layers;
    for (const std::string& rid : t.send_rids) {
      rids.emplace_back(rid, RidDirection::kSend);
      auto it = std::find_if(video_simulcast_.begin(), video_simulcast_.end(),
                             [&](const SimulcastEncoding& e) { return e.rid == rid; });
      layers.AddLayer(SimulcastLayer(rid, /*is_paused=*/it != video_simulcast_.end() && !it->active));
    }
    o.AddVideoSender(t.track_id, t.stream_ids, rids, layers, 0);
  } else {
    o.AddVideoSender(t.track_id, t.stream_ids, {}, SimulcastLayerList(), 1);
  }
  return o;
}

// GetOptionsForUnifiedPlanOffer (JSEP 5.2.1 and 5.2.2): every section of the current descriptions keeps its place,
// with its transceiver's options, or rejected when it has none (stopped and removed) or is stopping; new transceivers
// take the recyclable (rejected) sections first, then go at the end, with new MIDs; the data section last
// (MaybeNegotiateSctp) when local data channels were requested and none is negotiated.
MediaSessionOptions PeerEndpoint::OfferOptions() {
  MediaSessionOptions opts;
  opts.bundle_enabled = true;
  opts.rtcp_mux_enabled = true;
  opts.rtcp_cname = rtcp_cname_;
  const size_t local_n = local_description_ ? local_description_->description()->contents().size() : 0;
  const size_t remote_n = remote_description_ ? remote_description_->description()->contents().size() : 0;
  std::deque<size_t> recyclable;
  for (size_t i = 0; i < std::max(local_n, remote_n); ++i) {
    const ContentInfo* local = ContentAt(local_description_.get(), i);
    const ContentInfo* remote = ContentAt(remote_description_.get(), i);
    const bool had_been_rejected = (local && local->rejected) || (remote && remote->rejected);
    const std::string mid = local ? local->mid() : remote->mid();
    const MediaContentDescription* d = (local ? local : remote)->media_description();
    const MediaType type = d ? d->type() : MediaType::UNSUPPORTED;
    if (IsAudioOrVideo(type)) {
      Transceiver* t = FindByMid(mid);
      if (!t || (had_been_rejected && t->stopping)) {
        recyclable.push_back(i);
        opts.media_description_options.emplace_back(type, mid, RtpTransceiverDirection::kInactive, /*stopped=*/true);
      } else {
        opts.media_description_options.push_back(OptionsForTransceiver(*t, mid, /*offer=*/true));
        t->mline_index = i;
      }
    } else if (type == MediaType::DATA) {
      const bool active = !had_been_rejected && sctp_mid_ && *sctp_mid_ == mid;
      opts.media_description_options.emplace_back(MediaType::DATA, mid,
                                                  active ? RtpTransceiverDirection::kSendRecv
                                                         : RtpTransceiverDirection::kInactive,
                                                  /*stopped=*/!active);
    } else {
      opts.media_description_options.emplace_back(MediaType::UNSUPPORTED, mid, RtpTransceiverDirection::kInactive,
                                                  /*stopped=*/true);
    }
  }
  for (auto& t : transceivers_) {
    if (!t || t->mid || t->stopping || t->stopped) continue;
    size_t index;
    if (!recyclable.empty()) {
      index = recyclable.front();
      recyclable.pop_front();
      opts.media_description_options[index] = OptionsForTransceiver(*t, GenerateMid(), /*offer=*/true);
    } else {
      index = opts.media_description_options.size();
      opts.media_description_options.push_back(OptionsForTransceiver(*t, GenerateMid(), /*offer=*/true));
    }
    t->mline_index = index;
  }
#if SM_ENDPOINT_DATA_CHANNELS
  // GetOptionsForUnifiedPlanOffer: lastly, an m=application section if local data channels were requested
  // (MaybeNegotiateSctp, with the mid generator's next mid) and none is negotiated.
  if (data_channels_ && !sctp_mid_ && data_channels_->controller.HasDataChannels()) {
    opts.media_description_options.emplace_back(MediaType::DATA, GenerateMid(), RtpTransceiverDirection::kSendRecv,
                                                false);
  }
#endif
  return opts;
}

// GetOptionsForUnifiedPlanAnswer (JSEP 5.3.1 and 5.3.2): each section of the offer is answered with the transceiver
// SetRemoteDescription associated with it (AssociateTransceivers), or rejected when there is none. The data section
// that UpdateDataChannelTransport took (the first one, with data channels on) is answered; any other is rejected.
// The DTLS role of a negotiated transport is kept (WebRtcSessionDescriptionFactory::InternalCreateAnswer sets
// prefer_passive_role from GetDtlsRole), so a subsequent offer from the side that answered first can't swap it.
MediaSessionOptions PeerEndpoint::AnswerOptions(const SessionDescription& offer) const {
  MediaSessionOptions opts;
  opts.bundle_enabled = true;
  opts.rtcp_mux_enabled = true;
  opts.rtcp_cname = rtcp_cname_;
  for (const ContentInfo& content : offer.contents()) {
    const MediaContentDescription* d = content.media_description();
    const MediaType type = d ? d->type() : MediaType::UNSUPPORTED;
    if (IsAudioOrVideo(type)) {
      if (const Transceiver* t = FindByMid(content.mid())) {
        opts.media_description_options.push_back(OptionsForTransceiver(*t, content.mid(), /*offer=*/false));
      } else {
        opts.media_description_options.emplace_back(type, content.mid(), RtpTransceiverDirection::kInactive,
                                                    /*stopped=*/true);
      }
    } else if (type == MediaType::DATA && data_channels_ && !content.rejected && sctp_mid_ &&
               content.mid() == *sctp_mid_) {
      opts.media_description_options.emplace_back(MediaType::DATA, content.mid(), RtpTransceiverDirection::kSendRecv,
                                                  /*stopped=*/false);
    } else {
      opts.media_description_options.emplace_back(type == MediaType::ANY ? MediaType::UNSUPPORTED : type,
                                                  content.mid(), RtpTransceiverDirection::kInactive,
                                                  /*stopped=*/true);
    }
    if (const std::optional<p2p::SslRole> role = transport_controller_.GetDtlsRole(content.mid())) {
      opts.media_description_options.back().transport_options.prefer_passive_role = *role == p2p::SslRole::kServer;
    }
  }
  return opts;
}

std::unique_ptr<SessionDescriptionInterface> PeerEndpoint::Wrap(SdpType type, std::unique_ptr<SessionDescription> d) {
  if (!d) return nullptr;
  return SessionDescriptionInterface::Create(type, std::move(d), session_id_, std::to_string(session_version_++));
}

std::unique_ptr<SessionDescriptionInterface> PeerEndpoint::CreateOffer(std::string* error) {
  if (signaling_state_ != SignalingState::kStable && signaling_state_ != SignalingState::kHaveLocalOffer) {
    Fail(error, "CreateOffer called in a wrong state.");
    return nullptr;
  }
  MediaSessionOptions opts = OfferOptions();
  std::unique_ptr<SessionDescription> d = session_desc_factory_->CreateOfferOrError(
      opts, local_description_ ? local_description_->description() : nullptr, error);
  ApplyFixedSsrcs(d.get());
  return Wrap(SdpType::kOffer, std::move(d));
}

std::unique_ptr<SessionDescriptionInterface> PeerEndpoint::CreateAnswer(std::string* error) {
  if (signaling_state_ != SignalingState::kHaveRemoteOffer || !remote_description_) {
    Fail(error, "CreateAnswer called when not in have-remote-offer state.");
    return nullptr;
  }
  MediaSessionOptions opts = AnswerOptions(*remote_description_->description());
  std::unique_ptr<SessionDescription> d = session_desc_factory_->CreateAnswerOrError(
      remote_description_->description(), opts, local_description_ ? local_description_->description() : nullptr,
      error);
  ApplyFixedSsrcs(d.get());
  return Wrap(SdpType::kAnswer, std::move(d));
}

// The host's fixed SSRCs replace the generated ones in a transceiver's sender stream (SSRC munging before
// SetLocalDescription, which libwebrtc accepts), with the FID group for video RTX. A section belongs to the
// transceiver with its MID, or, in an offer not applied yet, to the new transceiver given its m-line index.
void PeerEndpoint::ApplyFixedSsrcs(SessionDescription* desc) const {
  if (!desc) return;
  for (size_t i = 0; i < desc->contents().size(); ++i) {
    ContentInfo& content = desc->contents()[i];
    MediaContentDescription* d = content.media_description();
    if (content.rejected || !d || d->streams().empty()) continue;
    const Transceiver* t = FindByMid(content.mid());
    if (!t) {
      for (const auto& c : transceivers_) {
        if (c && !c->mid && c->mline_index == i) t = c.get();
      }
    }
    if (!t || !t->ssrc || t->kind != d->type()) continue;
    StreamParams& sp = d->mutable_streams()[0];
    sp.ssrcs = {*t->ssrc};
    sp.ssrc_groups.clear();
    if (t->kind == MediaType::VIDEO && t->rtx_ssrc) sp.AddFidSsrc(*t->ssrc, *t->rtx_ssrc);
  }
}

// UpdateTransceiversAndDataChannels / AssociateTransceiver for every audio and video section. A local description's
// sections take the transceiver with their MID, else the unassociated one CreateOffer put at their m-line index
// (FindUnassociatedByMLineIndex). A remote one's take the transceiver with their MID, else (sendrecv or recvonly, no
// simulcast) the first unassociated AddTrack transceiver of their kind (FindAvailableTransceiverToReceive), else a new
// recvonly transceiver (with receive_remote_tracks; off, the section has none and the answer rejects it).
bool PeerEndpoint::AssociateTransceivers(bool local, const SessionDescription& desc, std::string* error) {
  const auto& contents = desc.contents();
  for (size_t i = 0; i < contents.size(); ++i) {
    const ContentInfo& content = contents[i];
    if (std::find(known_mids_.begin(), known_mids_.end(), content.mid()) == known_mids_.end()) {
      known_mids_.push_back(content.mid());  // mid_generator_.AddKnownId.
    }
    const MediaContentDescription* d = content.media_description();
    if (!d || !IsAudioOrVideo(d->type())) continue;
    Transceiver* t = FindByMid(content.mid());
    if (local) {
      if (!t) {
        for (auto& c : transceivers_) {
          if (c && !c->mid && c->mline_index == i) t = c.get();
        }
      }
      if (!t) {
        if (content.rejected) continue;
        return Fail(error, "Transceiver not found based on m-line index");
      }
    } else {
      if (!t && RtpTransceiverDirectionHasRecv(d->direction()) && !d->HasSimulcast()) {
        for (auto& c : transceivers_) {
          if (c && c->kind == d->type() && c->created_by_addtrack && !c->mid && !c->stopped && !c->stopping) {
            t = c.get();
            break;
          }
        }
      }
      if (!t) {
        // A rejected section needs no transceiver (libwebrtc makes one and stops it at once).
        if (content.rejected || !config_.receive_remote_tracks) continue;
        auto created = std::make_unique<Transceiver>();
        created->kind = d->type();
        created->direction = RtpTransceiverDirection::kRecvOnly;
        created->track_id = tls::CreateRandomUuid();
        t = created.get();
        transceivers_.push_back(std::move(created));
      }
    }
    if (t->kind != d->type()) return Fail(error, "Transceiver type does not match media description type.");
    t->mid = content.mid();
    t->mline_index = i;
  }
  return true;
}

bool PeerEndpoint::SetLocalDescription(std::unique_ptr<SessionDescriptionInterface> desc, Timestamp now,
                                       std::string* error) {
  if (!desc) return Fail(error, "SessionDescription is NULL.");
  last_now_ = std::max(last_now_, now);
  const SdpType type = desc->GetType();
  if (!(type == SdpType::kOffer &&
        (signaling_state_ == SignalingState::kStable || signaling_state_ == SignalingState::kHaveLocalOffer)) &&
      !(type == SdpType::kAnswer && signaling_state_ == SignalingState::kHaveRemoteOffer)) {
    return Fail(error, std::string("Called in wrong state for a local ") + SdpTypeToString(type) + ".");
  }
  if (!transport_controller_.SetLocalDescription(type, desc->description(),
                                                 remote_description_ ? remote_description_->description() : nullptr,
                                                 now, error)) {
    return false;
  }
  if (!AssociateTransceivers(/*local=*/true, *desc->description(), error)) return false;
  suggester_.Update(desc->description(), /*local=*/true, type);
  local_description_ = std::move(desc);
  if (!UpdateDataChannelTransport(*local_description_->description(), error)) return false;
  if (type == SdpType::kOffer) {
    signaling_state_ = SignalingState::kHaveLocalOffer;
  } else {
    signaling_state_ = SignalingState::kStable;
    if (!ApplyNegotiation(error)) return false;
    if (!MaybeStartSctp(last_now_, error)) return false;
  }
  transport_controller_.MaybeStartGathering(now);
  return true;
}

bool PeerEndpoint::SetRemoteDescription(std::unique_ptr<SessionDescriptionInterface> desc, Timestamp now,
                                        std::string* error) {
  if (!desc) return Fail(error, "SessionDescription is NULL.");
  last_now_ = std::max(last_now_, now);
  const SdpType type = desc->GetType();
  if (!(type == SdpType::kOffer &&
        (signaling_state_ == SignalingState::kStable || signaling_state_ == SignalingState::kHaveRemoteOffer)) &&
      !(type == SdpType::kAnswer && signaling_state_ == SignalingState::kHaveLocalOffer)) {
    return Fail(error, std::string("Called in wrong state for a remote ") + SdpTypeToString(type) + ".");
  }
  if (!transport_controller_.SetRemoteDescription(type,
                                                  local_description_ ? local_description_->description() : nullptr,
                                                  desc->description(), now, error)) {
    return false;
  }
  if (!AssociateTransceivers(/*local=*/false, *desc->description(), error)) return false;
  suggester_.Update(desc->description(), /*local=*/false, type);
  remote_description_ = std::move(desc);
  if (!UpdateDataChannelTransport(*remote_description_->description(), error)) return false;
  if (type == SdpType::kOffer) {
    signaling_state_ = SignalingState::kHaveRemoteOffer;
  } else {
    signaling_state_ = SignalingState::kStable;
    if (!ApplyNegotiation(error)) return false;
    if (!MaybeStartSctp(last_now_, error)) return false;
  }
  ApplyRemoteCandidates(*remote_description_, now);
  return true;
}

// UseCandidatesInRemoteDescription.
bool PeerEndpoint::ApplyRemoteCandidates(const SessionDescriptionInterface& desc, Timestamp now) {
  bool ok = true;
  const auto& contents = desc.description()->contents();
  for (size_t i = 0; i < desc.number_of_mediasections() && i < contents.size(); ++i) {
    const IceCandidateCollection* candidates = desc.candidates(i);
    if (!candidates || contents[i].rejected) continue;
    std::vector<p2p::Candidate> list;
    for (const auto& c : candidates->candidates()) list.push_back(c->candidate());
    if (!list.empty()) ok = transport_controller_.AddRemoteCandidates(contents[i].mid(), list, now) && ok;
  }
  return ok;
}

// SdpOfferAnswerHandler::AddIceCandidate / UseCandidate: by mid, else by m-line index; the candidate joins the remote
// description, and goes to the transport of its section.
bool PeerEndpoint::AddIceCandidate(const IceCandidate& candidate, Timestamp now, std::string* error) {
  if (!remote_description_) return Fail(error, "The remote description is not set.");
  last_now_ = std::max(last_now_, now);
  const SessionDescription* remote = remote_description_->description();
  const ContentInfo* content = nullptr;
  if (!candidate.sdp_mid().empty()) {
    content = remote->GetContentByName(candidate.sdp_mid());
  } else if (candidate.sdp_mline_index() >= 0 &&
             static_cast<size_t>(candidate.sdp_mline_index()) < remote->contents().size()) {
    content = &remote->contents()[static_cast<size_t>(candidate.sdp_mline_index())];
  }
  if (!content) return Fail(error, "The candidate's mid or m-line index matches no m= section.");
  remote_description_->AddCandidate(&candidate);
  // PeerConnection::AddRemoteCandidate: a candidate the transports refuse (a rejected section's) is only logged.
  if (!content->rejected) transport_controller_.AddRemoteCandidates(content->mid(), {candidate.candidate()}, now);
  return true;
}

// PeerConnection::OnTransportControllerCandidatesGathered: the candidate goes out for the transport's (first) mid.
void PeerEndpoint::OnCandidateGathered(const std::string& transport_name, const p2p::Candidate& candidate) {
  if (!on_ice_candidate_ || !local_description_) return;
  const auto& contents = local_description_->description()->contents();
  for (size_t i = 0; i < contents.size(); ++i) {
    if (contents[i].mid() != transport_name) continue;
    on_ice_candidate_(CreateIceCandidate(transport_name, static_cast<int>(i), candidate));
    return;
  }
}

JsepTransport* PeerEndpoint::media_transport() const {
  const SessionDescriptionInterface* d = local_description_ ? local_description_.get() : remote_description_.get();
  if (!d) return nullptr;
  for (const ContentInfo& c : d->description()->contents()) {
    if (c.rejected || c.type != MediaProtocolType::kRtp) continue;
    if (JsepTransport* t = transport_controller_.GetJsepTransportForMid(c.mid())) return t;
  }
  return nullptr;
}

bool PeerEndpoint::IsConnected() const {
  const JsepTransport* t = media_transport();
  return t && t->ice_transport()->writable() &&
         t->rtp_dtls_transport()->dtls_state() == p2p::DtlsTransportState::kConnected &&
         t->rtp_transport()->IsSrtpActive();
}

// --- Transceivers ---------------------------------------------------------------------------------------------------

int PeerEndpoint::AddTransceiver(MediaType kind, const TransceiverInit& init, std::string* error) {
  if (signaling_state_ == SignalingState::kClosed) {
    Fail(error, "AddTransceiver called on a closed endpoint.");
    return -1;
  }
  if (!IsAudioOrVideo(kind)) {
    Fail(error, "media_type must be audio or video.");
    return -1;
  }
  if (init.direction == RtpTransceiverDirection::kStopped) {
    Fail(error, "Cannot add a stopped transceiver.");
    return -1;
  }
  if (!config_.external_media && RtpTransceiverDirectionHasSend(init.direction)) {
    Fail(error, "Only the config's transceivers send from the endpoint's own session; use external_media.");
    return -1;
  }
  auto t = std::make_unique<Transceiver>();
  t->kind = kind;
  t->direction = init.direction;
  t->track_id = init.track_id.empty() ? tls::CreateRandomUuid() : init.track_id;
  t->stream_ids = init.stream_ids;
  t->ssrc = init.ssrc;
  if (init.ssrc && kind == MediaType::VIDEO) t->rtx_ssrc = init.rtx_ssrc;
  transceivers_.push_back(std::move(t));
  return static_cast<int>(transceivers_.size()) - 1;
}

bool PeerEndpoint::SetTransceiverDirection(int index, RtpTransceiverDirection direction) {
  if (index < 0 || static_cast<size_t>(index) >= transceivers_.size() || !transceivers_[static_cast<size_t>(index)]) {
    return false;
  }
  Transceiver& t = *transceivers_[static_cast<size_t>(index)];
  if (t.stopping || t.stopped || direction == RtpTransceiverDirection::kStopped) return false;
  if (!config_.external_media && !t.created_by_addtrack && RtpTransceiverDirectionHasSend(direction)) return false;
  t.direction = direction;
  return true;
}

// RtpTransceiver::StopStandard: stopping, so the next offer rejects the section; the receiver stops at once
// (StopSendingAndReceiving), so its receive stream goes now.
bool PeerEndpoint::StopTransceiver(int index) {
  if (index < 0 || static_cast<size_t>(index) >= transceivers_.size() || !transceivers_[static_cast<size_t>(index)]) {
    return false;
  }
  Transceiver& t = *transceivers_[static_cast<size_t>(index)];
  if (t.stopping || t.stopped) return true;
  t.stopping = true;
  RemoveRemoteTrack(t);
  if (!t.mid) t.stopped = true;  // Never negotiated: nothing to reject.
  return true;
}

std::vector<std::pair<int, TransceiverState>> PeerEndpoint::transceivers() const {
  std::vector<std::pair<int, TransceiverState>> out;
  for (size_t i = 0; i < transceivers_.size(); ++i) {
    const Transceiver* t = transceivers_[i].get();
    if (!t) continue;
    TransceiverState s;
    s.kind = t->kind;
    s.mid = t->mid;
    s.direction = t->stopped ? RtpTransceiverDirection::kStopped : t->direction;
    s.current_direction = t->current_direction;
    s.stopped = t->stopped;
    s.stopping = t->stopping;
    out.emplace_back(static_cast<int>(i), s);
  }
  return out;
}

std::vector<RemoteTrack> PeerEndpoint::remote_tracks() const {
  std::vector<RemoteTrack> out;
  for (const auto& t : transceivers_) {
    if (t && t->remote) out.push_back(*t->remote);
  }
  return out;
}

std::optional<rtp::AudioRecvId> PeerEndpoint::audio_receive() const {
  for (const auto& t : transceivers_) {
    if (t && t->remote && t->remote->audio) return t->remote->audio;
  }
  return std::nullopt;
}

std::optional<rtp::VideoRecvId> PeerEndpoint::video_receive() const {
  for (const auto& t : transceivers_) {
    if (t && t->remote && t->remote->video) return t->remote->video;
  }
  return std::nullopt;
}

// --- The completed offer/answer -------------------------------------------------------------------------------------

// ApplyLocalDescription / ApplyRemoteDescriptionUpdateTransceiverState for an answer, when the offer/answer completes:
// each associated transceiver's current direction (its side of the answer), a rejected section stops its transceiver;
// the RTP session is made at the first answer with an audio or video section, then each section is received or not
// (UpdateTransceiverReceive), and stopped transceivers are removed (RemoveStoppedTransceivers).
bool PeerEndpoint::ApplyNegotiation(std::string* error) {
  const SessionDescription* local = local_description_->description();
  const SessionDescription* remote = remote_description_->description();
  const bool local_answer = local_description_->GetType() == SdpType::kAnswer;
  const SessionDescription* answer = local_answer ? local : remote;
  for (auto& t : transceivers_) {
    if (!t || !t->mid) continue;
    const ContentInfo* c = answer->GetContentByName(*t->mid);
    if (!c || !c->media_description()) continue;
    if (c->rejected) {
      t->stopped = true;
      t->stopping = false;
      t->current_direction = RtpTransceiverDirection::kStopped;
      continue;
    }
    const RtpTransceiverDirection dir = local_answer
                                            ? c->media_description()->direction()
                                            : RtpTransceiverDirectionReversed(c->media_description()->direction());
    t->current_direction = dir;
    if (RtpTransceiverDirectionHasSend(dir)) t->has_ever_been_used_to_send = true;
  }
  if (!media_rtp_transport_) {
    bool has_media = false;
    for (const ContentInfo& c : answer->contents()) {
      has_media = has_media || (!c.rejected && c.type == MediaProtocolType::kRtp && c.media_description() &&
                                IsAudioOrVideo(c.media_description()->type()));
    }
    if (has_media && !CreateMediaSession(*answer, error)) return false;
  } else if (session_) {
    // A later answer may bring extensions the first one didn't have (same ids across the BUNDLE group).
    bool added = false;
    for (const ContentInfo& c : answer->contents()) {
      if (c.rejected || !c.media_description() || c.type != MediaProtocolType::kRtp) continue;
      for (const RtpExtension& e : c.media_description()->rtp_header_extensions()) {
        if (e.id < 1 || e.id > 255 || extensions_.IdOf(e.uri)) continue;
        added = extensions_.Register(static_cast<uint8_t>(e.id), e.uri) || added;
      }
    }
    if (added) session_->SetExtensions(extensions_);
    // The simulcast sender's layers after a subsequent offer/answer (an SFU's re-offer, or the answer to our own):
    // the remote section's state for them (AssociateTransceiver for an existing transceiver).
    for (auto& t : transceivers_) {
      if (!t || !t->mid || !t->simulcast_negotiated || t->stopping || t->stopped) continue;
      const ContentInfo* rc = remote->GetContentByName(*t->mid);
      if (rc && !rc->rejected && rc->media_description()) UpdateSimulcastLayers(*t, *rc->media_description());
    }
  }
  for (auto& t : transceivers_) {
    if (!t || !t->mid) continue;
    UpdateTransceiverReceive(*t, answer->GetContentByName(*t->mid), remote->GetContentByName(*t->mid));
  }
  RemoveStoppedTransceivers();
  return true;
}

// The voice and video channels' SetLocalContent / SetRemoteContent for the send side, on the one RTP session (C6):
// the session is configured from the first answer with an audio or video section, and the config's (AddTrack)
// transceivers send on the local section's SSRCs (video with its RTX SSRC from the FID group), with the answer's
// payload types (Opus; the first H.264 and its RTX), the negotiated header extensions (one id map: BUNDLE requires
// the same ids), their MIDs when the MID extension is negotiated, NACK as negotiated for H.264, and the CNAME.
bool PeerEndpoint::CreateMediaSession(const SessionDescription& answer, std::string* error) {
  const SessionDescription* local = local_description_->description();
  rtp::SessionConfig cfg = config_.session;
  for (const ContentInfo& content : answer.contents()) {
    if (content.rejected || content.type != MediaProtocolType::kRtp || !content.media_description()) continue;
    for (const RtpExtension& e : content.media_description()->rtp_header_extensions()) {
      if (e.id < 1 || e.id > 255) continue;
      extensions_.Register(static_cast<uint8_t>(e.id), e.uri);
    }
    if (!media_rtp_transport_) media_rtp_transport_ = transport_controller_.GetRtpTransport(content.mid());
  }
  if (!media_rtp_transport_) return Fail(error, "No transport for the media sections.");
  struct Send {
    Transceiver* transceiver = nullptr;
    const ContentInfo* content = nullptr;
    const MediaContentDescription* answer = nullptr;
    const StreamParams* stream = nullptr;
    std::optional<StreamParams> simulcast;  // The RID stream with the SSRCs made for its layers.
  } audio, video;
  for (const auto& t : transceivers_) {
    if (!t || !t->created_by_addtrack || !t->mid) continue;
    Send& s = t->kind == MediaType::AUDIO ? audio : video;
    const ContentInfo* c = answer.GetContentByName(*t->mid);
    const MediaContentDescription* l = local->GetContentDescriptionByName(*t->mid);
    if (s.content || !c || c->rejected || !l || !RtpTransceiverDirectionHasSend(l->direction())) continue;
    const StreamParams* sp = FirstStream(l);
    if (!sp && t->kind == MediaType::VIDEO && !t->send_rids.empty() && !l->streams().empty() &&
        l->streams()[0].has_rids()) {
      // BaseChannel::UpdateLocalStreams_w: a sender signaled with RIDs and no SSRCs (simulcast, C17) gets an SSRC
      // per layer, and an RTX SSRC for each when RTX is negotiated. Made once: later descriptions keep signaling the
      // RIDs without SSRCs, and the session keeps these layers (C18 keeps the send side as first negotiated).
      s.simulcast = l->streams()[0];
      const Codec* h264 = FindCodec(c->media_description()->codecs(), kH264CodecName);
      const bool rtx = h264 && FindRtxFor(c->media_description()->codecs(), h264->id);
      s.simulcast->GenerateSsrcs(static_cast<int>(s.simulcast->rids().size()), rtx, /*generate_fec_fr=*/false,
                                 [this] { return ssrc_generator_.GenerateId(); });
      sp = &*s.simulcast;
    }
    if (!sp) continue;
    s.transceiver = t.get();
    s.content = c;
    s.answer = c->media_description();
    s.stream = sp;
  }
  cfg.extensions = extensions_;
  if (audio.content && HasMidExtension(audio.answer)) cfg.mid_audio = audio.content->mid();
  if (video.content && HasMidExtension(video.answer)) cfg.mid_video = video.content->mid();
  if (audio.stream) cfg.local_ssrc = audio.stream->first_ssrc();
  else if (video.stream) cfg.local_ssrc = video.stream->first_ssrc();
  else cfg.local_ssrc = ssrc_generator_.GenerateId();
  if (audio.stream || video.stream) cfg.cname = (audio.stream ? audio.stream : video.stream)->cname;
  media_rtp_transport_->SubscribeRtcpPacketReceived(
      this, [this](const std::vector<uint8_t>& p, std::optional<Timestamp> arrival) { OnRtcp(p, arrival); });
  if (config_.external_media) return true;
  session_ = std::make_unique<rtp::Session>(cfg);
  if (audio.content) {
    const Codec* opus = FindCodec(audio.answer->codecs(), kOpusCodecName);
    if (!opus) return Fail(error, "The audio section has no Opus.");
    rtp::AudioStreamConfig a = config_.audio_stream;
    a.payload_type = static_cast<uint8_t>(opus->id);
    a.clock_rate = opus->clockrate;
    a.ssrc = audio.stream->first_ssrc();
    audio_send_ = session_->AddAudioSend(a);
  }
  if (video.content) {
    const Codec* h264 = FindCodec(video.answer->codecs(), kH264CodecName);
    if (!h264) return Fail(error, "The video section has no H.264.");
    rtp::VideoStreamConfig v = config_.video_stream;
    v.codec = rtp::VideoCodecType::kH264;
    v.payload_type = static_cast<uint8_t>(h264->id);
    v.clock_rate = h264->clockrate;
    v.nack = h264->HasFeedbackParam(FeedbackParam(kRtcpFbParamNack));
    std::string mode;
    v.h264_packetization_mode = h264->GetParam("packetization-mode", &mode) && mode == "1"
                                    ? rtp::H264PacketizationMode::kNonInterleaved
                                    : rtp::H264PacketizationMode::kSingleNalUnit;
    const Codec* rtx = FindRtxFor(video.answer->codecs(), h264->id);
    if (rtx) v.rtx_payload_type = static_cast<uint8_t>(rtx->id);
    v.parser = &h264_parser_;
    if (video.simulcast) {
      // One send stream per encoding, with its SSRC, RTX SSRC and RID (RtpVideoSender's module per layer); each
      // encoding's state comes from the remote answer (UpdateSimulcastLayers).
      std::vector<uint32_t> primaries;
      video.stream->GetPrimarySsrcs(&primaries);
      const auto& rids = video.stream->rids();
      for (size_t i = 0; i < rids.size() && i < primaries.size(); ++i) {
        SimulcastEncoding enc;
        enc.rid = rids[i].rid;
        v.ssrc = primaries[i];
        uint32_t fid = 0;
        v.rtx_ssrc = rtx && video.stream->GetFidSsrc(v.ssrc, &fid) ? std::optional<uint32_t>(fid) : std::nullopt;
        v.rid = enc.rid;
        video_simulcast_sends_.push_back(session_->AddVideoSend(v));
        video_simulcast_.push_back(enc);
      }
      v.rid.clear();
      if (!video_simulcast_sends_.empty()) video_send_ = video_simulcast_sends_.front();
      video.transceiver->simulcast_negotiated = true;
      const MediaContentDescription* remote_video =
          remote_description_->description()->GetContentDescriptionByName(video.content->mid());
      if (remote_video) UpdateSimulcastLayers(*video.transceiver, *remote_video);
    } else {
      v.ssrc = video.stream->first_ssrc();
      uint32_t fid = 0;
      v.rtx_ssrc = rtx && video.stream->GetFidSsrc(v.ssrc, &fid) ? std::optional<uint32_t>(fid) : std::nullopt;
      video_send_ = session_->AddVideoSend(v);
    }
  }
  return true;
}

// The simulcast sender's encodings from a remote description's section (AssociateTransceiver, sdp_offer_answer.cc):
// SimulcastIsRejected (the remote section has no a=simulcast or no RID extension, while ours offered simulcast)
// leaves only the first encoding (DisableSimulcastInSender); otherwise UpdateSimulcastLayerStatusInSender: an encoding
// the remote's receive layers pause is inactive and one they leave out is taken out of the envelope (DisableEncoding
// Layers: inactive, and no longer signaled). The send streams themselves stay (video_simulcast_sends()).
void PeerEndpoint::UpdateSimulcastLayers(Transceiver& t, const MediaContentDescription& remote) {
  bool rid_negotiated = false;
  for (const RtpExtension& e : remote.rtp_header_extensions()) rid_negotiated |= e.uri == RtpExtension::kRidUri;
  if (!remote.HasSimulcast() || !rid_negotiated) {
    for (size_t i = 1; i < video_simulcast_.size(); ++i) video_simulcast_[i].active = false;
    if (t.send_rids.size() > 1) t.send_rids.resize(1);
    return;
  }
  const std::vector<SimulcastLayer> layers = remote.simulcast_description().receive_layers().GetAllLayers();
  for (SimulcastEncoding& enc : video_simulcast_) {
    auto rid = std::find(t.send_rids.begin(), t.send_rids.end(), enc.rid);
    if (rid == t.send_rids.end()) continue;  // Disabled earlier: stays out.
    auto it = std::find_if(layers.begin(), layers.end(), [&](const SimulcastLayer& l) { return l.rid == enc.rid; });
    if (it == layers.end()) {
      enc.active = false;
      t.send_rids.erase(rid);
      continue;
    }
    enc.active = !it->is_paused;
  }
}

// One associated transceiver after the offer/answer completed. Its channel (BaseChannel) registers the section's
// header extensions and its demuxer sink (MID plus the remote SSRCs, or the payload types without SSRCs) on the BUNDLE
// transport while the section is accepted. The section is received while the peer sends on it (its remote direction
// has send) with signaled SSRCs: a receive stream in the session (Call::CreateAudioReceiveStream /
// CreateVideoReceiveStream: the section's MID, the msid stream as sync group, the answer's payload types, RTX from the
// FID group) and OnTrack; when the peer stops (sendonly to inactive or recvonly, rejected, stopped) or moves to
// another SSRC, the stream goes and OnRemoveTrack runs (ProcessRemovalOfRemoteTrack, DestroyAudioReceiveStream).
void PeerEndpoint::UpdateTransceiverReceive(Transceiver& t, const ContentInfo* answer, const ContentInfo* remote) {
  const bool live = answer && !answer->rejected && answer->media_description() && !t.stopped && !t.stopping;
  if (!live) {
    RemoveRemoteTrack(t);
    UnregisterSink(t);
    return;
  }
  const std::string& mid = *t.mid;
  const MediaContentDescription* ad = answer->media_description();
  const MediaContentDescription* rd = remote && !remote->rejected ? remote->media_description() : nullptr;
  const StreamParams* rsp = FirstStream(rd);
  const std::vector<uint32_t> ssrcs = rsp ? rsp->ssrcs : std::vector<uint32_t>{};
  if (media_rtp_transport_ && (!t.sink_registered || ssrcs != t.sink_ssrcs)) {
    UnregisterSink(t);
    media_rtp_transport_->RegisterRtpHeaderExtensionMap(mid, ad->rtp_header_extensions());
    RtpDemuxerCriteria criteria;
    criteria.mid = mid;
    criteria.ssrcs = ssrcs;
    if (criteria.ssrcs.empty()) {
      for (const Codec& c : ad->codecs()) criteria.payload_types.push_back(static_cast<uint8_t>(c.id));
    }
    if (!t.sink) t.sink = std::make_unique<ChannelSink>(this);
    t.sink_registered = media_rtp_transport_->RegisterRtpDemuxerSink(criteria, t.sink.get());
    t.sink_ssrcs = ssrcs;
  }
  const bool receive = rd && rsp && RtpTransceiverDirectionHasSend(rd->direction());
  if (t.remote && (!receive || t.remote->ssrc != rsp->first_ssrc())) RemoveRemoteTrack(t);
  if (!receive || t.remote) return;

  RemoteTrack rt;
  rt.mid = mid;
  rt.kind = t.kind;
  rt.track_id = rsp->id;
  rt.stream_ids = rsp->stream_ids();
  rt.ssrc = rsp->first_ssrc();
  rt.cname = rsp->cname;
  uint32_t fid = 0;
  if (t.kind == MediaType::VIDEO && rsp->GetFidSsrc(rt.ssrc, &fid)) rt.rtx_ssrc = fid;
  const std::string sync_group = rt.stream_ids.empty() ? std::string() : rt.stream_ids[0];
  if (session_ && t.kind == MediaType::AUDIO) {
    rtp::AudioDecoder* decoder = nullptr;
    if (config_.audio_decoder_factory) {
      t.decoder = config_.audio_decoder_factory(mid);
      decoder = t.decoder.get();
    } else if (config_.audio_decoder && !audio_decoder_in_use_) {
      decoder = config_.audio_decoder;
      audio_decoder_in_use_ = true;
      t.uses_config_decoder = true;
    }
    const Codec* opus = FindCodec(ad->codecs(), kOpusCodecName);
    if (decoder && opus) {
      rtp::AudioStreamConfig a = config_.audio_stream;
      a.payload_type = static_cast<uint8_t>(opus->id);
      a.clock_rate = opus->clockrate;
      a.ssrc = rt.ssrc;
      a.mid = mid;
      a.sync_group = sync_group;
      rt.audio = session_->AddAudioReceive(a, decoder);
    }
  } else if (session_ && t.kind == MediaType::VIDEO) {
    if (const Codec* h264 = FindCodec(ad->codecs(), kH264CodecName)) {
      rtp::VideoStreamConfig v = config_.video_stream;
      v.codec = rtp::VideoCodecType::kH264;
      v.payload_type = static_cast<uint8_t>(h264->id);
      v.clock_rate = h264->clockrate;
      v.nack = h264->HasFeedbackParam(FeedbackParam(kRtcpFbParamNack));
      std::string mode;
      v.h264_packetization_mode = h264->GetParam("packetization-mode", &mode) && mode == "1"
                                      ? rtp::H264PacketizationMode::kNonInterleaved
                                      : rtp::H264PacketizationMode::kSingleNalUnit;
      const Codec* rtx = FindRtxFor(ad->codecs(), h264->id);
      if (rtx) v.rtx_payload_type = static_cast<uint8_t>(rtx->id);
      v.parser = &h264_parser_;
      v.ssrc = rt.ssrc;
      v.rtx_ssrc = rtx ? rt.rtx_ssrc : std::nullopt;
      v.mid = mid;
      v.sync_group = sync_group;
      rt.video = session_->AddVideoReceive(v);
    }
  }
  t.remote = rt;
  if (on_track_) on_track_(rt);
}

void PeerEndpoint::RemoveRemoteTrack(Transceiver& t) {
  if (!t.remote) return;
  const RemoteTrack rt = *t.remote;
  t.remote.reset();
  if (session_ && rt.audio) session_->RemoveAudioReceive(*rt.audio);
  if (session_ && rt.video) session_->RemoveVideoReceive(*rt.video);
  if (t.uses_config_decoder) {
    t.uses_config_decoder = false;
    audio_decoder_in_use_ = false;
  }
  if (on_remove_track_) on_remove_track_(rt);
  t.decoder.reset();  // After the stream that used it, and after the callback (which may still read it).
}

void PeerEndpoint::UnregisterSink(Transceiver& t) {
  if (t.sink_registered && media_rtp_transport_) {
    media_rtp_transport_->UnregisterRtpDemuxerSink(t.sink.get());
    if (t.mid) media_rtp_transport_->UnregisterRtpHeaderExtensionMap(*t.mid);
  }
  t.sink_registered = false;
  t.sink_ssrcs.clear();
}

// RemoveStoppedTransceivers: a stopped transceiver leaves the set (its channel cleared first), and its section can be
// recycled by the next offer.
void PeerEndpoint::RemoveStoppedTransceivers() {
  for (auto& t : transceivers_) {
    if (!t || !t->stopped) continue;
    RemoveRemoteTrack(*t);
    UnregisterSink(*t);
    t.reset();
  }
}

void PeerEndpoint::Deliver(std::span<const uint8_t> packet, bool rtcp, Timestamp arrival) {
  // The session's clock never goes back.
  last_now_ = std::max(last_now_, arrival);
  if (config_.external_media) {
    if (config_.on_media_packet) config_.on_media_packet(packet, rtcp, arrival);
    return;
  }
  if (!session_) return;
  session_->OnPacket(packet, last_now_);
}

void PeerEndpoint::OnRtcp(const std::vector<uint8_t>& packet, std::optional<Timestamp> arrival) {
  Deliver(packet, /*rtcp=*/true, arrival.value_or(last_now_));
}

bool PeerEndpoint::SendMediaPacket(std::span<const uint8_t> packet, bool rtcp, Timestamp now) {
  last_now_ = std::max(last_now_, now);
  if (!media_rtp_transport_) return false;
  std::vector<uint8_t> bytes(packet.begin(), packet.end());
  const bool sent = rtcp ? media_rtp_transport_->SendRtcpPacket(bytes, p2p::PacketOptions{}, p2p::kPfSrtpBypass, last_now_)
                         : media_rtp_transport_->SendRtpPacket(bytes, p2p::PacketOptions{}, p2p::kPfSrtpBypass, last_now_);
  if (!sent) ++send_failures_;
  return sent;
}

void PeerEndpoint::ProcessTransport(Timestamp now) {
  last_now_ = std::max(last_now_, now);
  transport_controller_.Process(last_now_);
}

void PeerEndpoint::FlushOutgoing(Timestamp now) {
  if (!session_ || !media_rtp_transport_) return;
  for (rtp::OutgoingPacket& p : session_->TakeOutgoing()) {
    const bool rtcp = p.kind == rtp::OutgoingKind::kRtcp;
    const bool sent = rtcp ? media_rtp_transport_->SendRtcpPacket(p.bytes, p2p::PacketOptions{}, p2p::kPfSrtpBypass, now)
                           : media_rtp_transport_->SendRtpPacket(p.bytes, p2p::PacketOptions{}, p2p::kPfSrtpBypass, now);
    if (!sent) ++send_failures_;
  }
}

// SdpOfferAnswerHandler::UpdateDataChannelTransport, for the first data section (the one sctp_mid names once set):
// a rejected one destroys the data channel transport, an accepted one becomes the SCTP mid
// (PeerConnection::CreateDataChannelTransport). A section that isn't SCTP can't carry data channels.
bool PeerEndpoint::UpdateDataChannelTransport(const SessionDescription& desc, std::string* error) {
  if (!data_channels_) return true;
  const ContentInfo* content = sctp_mid_ ? desc.GetContentByName(*sctp_mid_) : FirstDataContent(desc);
  if (!content) return true;
  if (content->rejected) {
    DestroyDataChannelTransport("Rejected data channel transport");
    return true;
  }
  if (content->type != MediaProtocolType::kSctp) return Fail(error, "Failed to create data channel.");
  sctp_mid_ = content->mid();
  return true;
}

// PushdownMediaDescription: with a complete offer/answer that has the SCTP section, start SCTP (StartSctpTransport)
// with the local and remote ports and the smaller max message size; a remote max message size of 0 means any size,
// so the local one is used.
bool PeerEndpoint::MaybeStartSctp(Timestamp now, std::string* error) {
#if SM_ENDPOINT_DATA_CHANNELS
  if (!data_channels_ || !sctp_mid_ || !local_description_ || !remote_description_ || data_channels_->sctp) {
    return true;
  }
  const ContentInfo* local = local_description_->description()->GetContentByName(*sctp_mid_);
  const ContentInfo* remote = remote_description_->description()->GetContentByName(*sctp_mid_);
  const SctpDataContentDescription* local_sctp = local ? local->media_description()->as_sctp() : nullptr;
  const SctpDataContentDescription* remote_sctp = remote ? remote->media_description()->as_sctp() : nullptr;
  if (!local_sctp || !remote_sctp || local->rejected || remote->rejected) return true;
  JsepTransport* transport = transport_controller_.GetJsepTransportForMid(*sctp_mid_);
  if (!transport || !transport->rtp_dtls_transport()) return Fail(error, "No DTLS transport for the data section.");
  sctp::SctpOptions options;
  options.local_port = local_sctp->port();
  options.remote_port = remote_sctp->port();
  options.max_message_size = remote_sctp->max_message_size() == 0
                                 ? local_sctp->max_message_size()
                                 : std::min(local_sctp->max_message_size(), remote_sctp->max_message_size());
  p2p::DtlsTransport* dtls = transport->rtp_dtls_transport();
  data_channels_->sctp =
      std::make_unique<sctp::SctpTransport>(std::make_unique<sctp::DcSctpTransport>(now, dtls), dtls);
  data_channels_->controller.SetNow(now);
  data_channels_->controller.SetupDataChannelTransport_n(data_channels_->sctp.get());
  if (!data_channels_->sctp->Start(now, options)) return Fail(error, "Failed to start the SCTP transport.");
  return true;
#else
  (void)now;
  (void)error;
  return true;
#endif
}

// PeerConnection::DestroyDataChannelTransport: the channels close with the error, and the SCTP transport goes.
void PeerEndpoint::DestroyDataChannelTransport(const char* reason) {
#if SM_ENDPOINT_DATA_CHANNELS
  RtcError error(RtcErrorType::kOperationErrorWithData, reason);
  data_channels_->controller.TeardownDataChannelTransport_n(error);
  data_channels_->sctp.reset();
#else
  (void)reason;
#endif
  sctp_mid_.reset();
}

std::shared_ptr<SctpDataChannel> PeerEndpoint::CreateDataChannel(std::string_view label, const DataChannelInit& init,
                                                                 Timestamp now, RtcError* error) {
#if SM_ENDPOINT_DATA_CHANNELS
  if (!data_channels_) {
    if (error) *error = RtcError(RtcErrorType::kInvalidState, "Data channels are off for this connection");
    return nullptr;
  }
  last_now_ = std::max(last_now_, now);
  return data_channels_->controller.CreateDataChannel(last_now_, label, init, error);
#else
  (void)label;
  (void)init;
  (void)now;
  if (error) *error = RtcError(RtcErrorType::kUnsupportedOperation, "Data channels are not supported");
  return nullptr;
#endif
}

void PeerEndpoint::SetOnDataChannel(std::function<void(std::shared_ptr<SctpDataChannel>)> callback) {
#if SM_ENDPOINT_DATA_CHANNELS
  if (data_channels_) data_channels_->controller.SetOnDataChannel(std::move(callback));
#else
  (void)callback;
#endif
}

DataChannelController* PeerEndpoint::data_channel_controller() {
#if SM_ENDPOINT_DATA_CHANNELS
  return data_channels_ ? &data_channels_->controller : nullptr;
#else
  return nullptr;
#endif
}

sctp::SctpTransport* PeerEndpoint::sctp_transport() {
#if SM_ENDPOINT_DATA_CHANNELS
  return data_channels_ ? data_channels_->sctp.get() : nullptr;
#else
  return nullptr;
#endif
}

void PeerEndpoint::Process(Timestamp now) {
  last_now_ = std::max(last_now_, now);
  transport_controller_.Process(last_now_);
#if SM_ENDPOINT_DATA_CHANNELS
  if (data_channels_) {
    data_channels_->controller.SetNow(last_now_);
    if (data_channels_->sctp) data_channels_->sctp->internal()->Process(last_now_);
    data_channels_->controller.Process(last_now_);
  }
#endif
  if (session_) {
    session_->Process(last_now_);
    FlushOutgoing(last_now_);
  }
}

Timestamp PeerEndpoint::NextProcessTime(Timestamp now) const {
  Timestamp next = transport_controller_.NextProcessTime();
  if (session_) next = std::min(next, session_->NextProcessTime(std::max(now, last_now_)));
#if SM_ENDPOINT_DATA_CHANNELS
  if (data_channels_) {
    next = std::min(next, data_channels_->controller.NextProcessTime());
    if (data_channels_->sctp) next = std::min(next, data_channels_->sctp->internal()->NextProcessTime());
  }
#endif
  return next;
}

}  // namespace sm::pc
