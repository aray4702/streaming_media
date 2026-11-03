// The transport half of libwebrtc M157's pc/sdp_offer_answer.cc and pc/peer_connection.cc for C6's subset (#1064);
// see sm/pc/peer_endpoint.h.
#include "sm/pc/peer_endpoint.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "sm/pc/codec.h"
#include "sm/pc/media_options.h"
#include "sm/pc/rtp_parameters.h"
#include "sm/sync/header_extensions.h"
#include "sm/tls/crypto_random.h"

namespace sm::pc {

namespace {

constexpr char kAudioMid[] = "0";
constexpr char kVideoMid[] = "1";
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

}  // namespace

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
  last_now_ = now;
}

PeerEndpoint::~PeerEndpoint() {
  if (media_rtp_transport_) {
    for (auto& s : sinks_) media_rtp_transport_->UnregisterRtpDemuxerSink(s.get());
  }
}

MediaSessionOptions PeerEndpoint::OfferOptions() const {
  MediaSessionOptions opts;
  opts.bundle_enabled = true;
  opts.rtcp_mux_enabled = true;
  if (config_.audio) {
    MediaDescriptionOptions o(MediaType::AUDIO, kAudioMid, RtpTransceiverDirection::kSendRecv, false);
    o.header_extensions = DefaultAudioRtpHeaderExtensions();
    o.AddAudioSender(config_.audio_track_id, {config_.stream_id});
    opts.media_description_options.push_back(std::move(o));
  }
  if (config_.video) {
    MediaDescriptionOptions o(MediaType::VIDEO, kVideoMid, RtpTransceiverDirection::kSendRecv, false);
    o.header_extensions = DefaultVideoRtpHeaderExtensions();
    o.AddVideoSender(config_.video_track_id, {config_.stream_id}, {}, SimulcastLayerList(), 1);
    opts.media_description_options.push_back(std::move(o));
  }
  return opts;
}

// The transceivers follow the offer's sections in order (Unified Plan): the first audio and the first video section
// get this end's transceivers; the rest (and the data channel) are rejected.
MediaSessionOptions PeerEndpoint::AnswerOptions(const SessionDescription& offer) const {
  MediaSessionOptions opts;
  opts.bundle_enabled = true;
  opts.rtcp_mux_enabled = true;
  bool audio_used = false, video_used = false;
  for (const ContentInfo& content : offer.contents()) {
    const MediaContentDescription* d = content.media_description();
    const MediaType type = d ? d->type() : MediaType::UNSUPPORTED;
    const bool audio = type == MediaType::AUDIO && config_.audio && !audio_used && !content.rejected;
    const bool video = type == MediaType::VIDEO && config_.video && !video_used && !content.rejected;
    if (audio || video) {
      MediaDescriptionOptions o(type, content.mid(), RtpTransceiverDirection::kSendRecv, false);
      o.header_extensions = audio ? DefaultAudioRtpHeaderExtensions() : DefaultVideoRtpHeaderExtensions();
      if (audio) {
        o.AddAudioSender(config_.audio_track_id, {config_.stream_id});
        audio_used = true;
      } else {
        o.AddVideoSender(config_.video_track_id, {config_.stream_id}, {}, SimulcastLayerList(), 1);
        video_used = true;
      }
      opts.media_description_options.push_back(std::move(o));
    } else {
      opts.media_description_options.emplace_back(type == MediaType::ANY ? MediaType::UNSUPPORTED : type,
                                                  content.mid(), RtpTransceiverDirection::kInactive,
                                                  /*stopped=*/true);
    }
  }
  return opts;
}

std::unique_ptr<SessionDescriptionInterface> PeerEndpoint::Wrap(SdpType type, std::unique_ptr<SessionDescription> d) {
  if (!d) return nullptr;
  return SessionDescriptionInterface::Create(type, std::move(d), session_id_, std::to_string(session_version_++));
}

std::unique_ptr<SessionDescriptionInterface> PeerEndpoint::CreateOffer(std::string* error) {
  if (signaling_state_ != SignalingState::kStable || remote_description_) {
    Fail(error, "CreateOffer: renegotiation is not supported.");
    return nullptr;
  }
  MediaSessionOptions opts = OfferOptions();
  opts.rtcp_cname = tls::CreateRandomString(kRtcpCnameLength);
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
  opts.rtcp_cname = tls::CreateRandomString(kRtcpCnameLength);
  std::unique_ptr<SessionDescription> d =
      session_desc_factory_->CreateAnswerOrError(remote_description_->description(), opts, nullptr, error);
  ApplyFixedSsrcs(d.get());
  return Wrap(SdpType::kAnswer, std::move(d));
}

// The host's fixed SSRCs replace the generated ones in the local sender's stream (SSRC munging before
// SetLocalDescription, which libwebrtc accepts), with the FID group for video RTX.
void PeerEndpoint::ApplyFixedSsrcs(SessionDescription* desc) const {
  if (!desc) return;
  for (ContentInfo& content : desc->contents()) {
    MediaContentDescription* d = content.media_description();
    if (content.rejected || !d || d->streams().empty()) continue;
    StreamParams& sp = d->mutable_streams()[0];
    if (d->type() == MediaType::AUDIO && config_.audio_ssrc) {
      sp.ssrcs = {*config_.audio_ssrc};
      sp.ssrc_groups.clear();
    } else if (d->type() == MediaType::VIDEO && config_.video_ssrc) {
      sp.ssrcs = {*config_.video_ssrc};
      sp.ssrc_groups.clear();
      if (config_.video_rtx_ssrc) sp.AddFidSsrc(*config_.video_ssrc, *config_.video_rtx_ssrc);
    }
  }
}

bool PeerEndpoint::SetLocalDescription(std::unique_ptr<SessionDescriptionInterface> desc, Timestamp now,
                                       std::string* error) {
  if (!desc) return Fail(error, "SessionDescription is NULL.");
  last_now_ = std::max(last_now_, now);
  const SdpType type = desc->GetType();
  if (!(type == SdpType::kOffer && signaling_state_ == SignalingState::kStable && !remote_description_) &&
      !(type == SdpType::kAnswer && signaling_state_ == SignalingState::kHaveRemoteOffer)) {
    return Fail(error, std::string("Called in wrong state for a local ") + SdpTypeToString(type) + ".");
  }
  if (!transport_controller_.SetLocalDescription(type, desc->description(),
                                                 remote_description_ ? remote_description_->description() : nullptr,
                                                 now, error)) {
    return false;
  }
  suggester_.Update(desc->description(), /*local=*/true, type);
  local_description_ = std::move(desc);
  if (type == SdpType::kOffer) {
    signaling_state_ = SignalingState::kHaveLocalOffer;
  } else {
    signaling_state_ = SignalingState::kStable;
    if (!CreateMediaSession(error)) return false;
  }
  transport_controller_.MaybeStartGathering(now);
  return true;
}

bool PeerEndpoint::SetRemoteDescription(std::unique_ptr<SessionDescriptionInterface> desc, Timestamp now,
                                        std::string* error) {
  if (!desc) return Fail(error, "SessionDescription is NULL.");
  last_now_ = std::max(last_now_, now);
  const SdpType type = desc->GetType();
  if (!(type == SdpType::kOffer && signaling_state_ == SignalingState::kStable && !local_description_) &&
      !(type == SdpType::kAnswer && signaling_state_ == SignalingState::kHaveLocalOffer)) {
    return Fail(error, std::string("Called in wrong state for a remote ") + SdpTypeToString(type) + ".");
  }
  if (!transport_controller_.SetRemoteDescription(type,
                                                  local_description_ ? local_description_->description() : nullptr,
                                                  desc->description(), now, error)) {
    return false;
  }
  suggester_.Update(desc->description(), /*local=*/false, type);
  remote_description_ = std::move(desc);
  if (type == SdpType::kOffer) {
    signaling_state_ = SignalingState::kHaveRemoteOffer;
  } else {
    signaling_state_ = SignalingState::kStable;
    if (!CreateMediaSession(error)) return false;
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

// The voice and video channels' SetLocalContent / SetRemoteContent, on the one RTP session.
bool PeerEndpoint::CreateMediaSession(std::string* error) {
  const SessionDescription* local = local_description_->description();
  const SessionDescription* remote = remote_description_->description();
  const SessionDescription* answer =
      local_description_->GetType() == SdpType::kAnswer ? local : remote;
  rtp::SessionConfig cfg = config_.session;
  sync::ExtensionMap extensions;
  struct Section {
    const ContentInfo* content = nullptr;
    const MediaContentDescription* local = nullptr;
    const MediaContentDescription* remote = nullptr;
    const MediaContentDescription* answer = nullptr;
    bool mid_negotiated = false;
  } audio, video;
  for (const ContentInfo& content : answer->contents()) {
    if (content.rejected || content.type != MediaProtocolType::kRtp || !content.media_description()) continue;
    Section* s = content.media_description()->type() == MediaType::AUDIO   ? &audio
                 : content.media_description()->type() == MediaType::VIDEO ? &video
                                                                           : nullptr;
    if (!s || s->content) continue;
    s->content = &content;
    s->answer = content.media_description();
    s->local = local->GetContentDescriptionByName(content.mid());
    s->remote = remote->GetContentDescriptionByName(content.mid());
    for (const RtpExtension& e : s->answer->rtp_header_extensions()) {
      if (e.id < 1 || e.id > 255) continue;
      extensions.Register(static_cast<uint8_t>(e.id), e.uri);
      if (e.uri == RtpExtension::kMidUri) s->mid_negotiated = true;
    }
  }
  if (!audio.content && !video.content) return Fail(error, "No audio or video section was negotiated.");
  media_rtp_transport_ = transport_controller_.GetRtpTransport((audio.content ? audio : video).content->mid());
  if (!media_rtp_transport_) return Fail(error, "No transport for the media sections.");

  cfg.extensions = extensions;
  if (audio.mid_negotiated) cfg.mid_audio = audio.content->mid();
  if (video.mid_negotiated) cfg.mid_video = video.content->mid();
  const StreamParams* local_audio = FirstStream(audio.local);
  const StreamParams* local_video = FirstStream(video.local);
  const bool send_audio = local_audio && audio.local && RtpTransceiverDirectionHasSend(audio.local->direction());
  const bool send_video = local_video && video.local && RtpTransceiverDirectionHasSend(video.local->direction());
  const StreamParams* remote_audio = FirstStream(audio.remote);
  const StreamParams* remote_video = FirstStream(video.remote);
  const bool recv_audio = remote_audio && audio.remote && RtpTransceiverDirectionHasSend(audio.remote->direction()) &&
                          config_.audio_decoder;
  const bool recv_video = remote_video && video.remote && RtpTransceiverDirectionHasSend(video.remote->direction());
  if (send_audio) cfg.local_ssrc = local_audio->first_ssrc();
  else if (send_video) cfg.local_ssrc = local_video->first_ssrc();
  else cfg.local_ssrc = ssrc_generator_.GenerateId();
  if (send_audio || send_video) cfg.cname = (send_audio ? local_audio : local_video)->cname;
  if (!config_.external_media) session_ = std::make_unique<rtp::Session>(cfg);

  std::vector<uint8_t> audio_pts, video_pts;
  if (config_.external_media) {
    // The host's sessions: only the demuxer's payload types (used when the remote section has no SSRCs).
    for (Section* s : {&audio, &video}) {
      if (!s->content) continue;
      for (const Codec& c : s->answer->codecs()) (s == &audio ? audio_pts : video_pts).push_back(static_cast<uint8_t>(c.id));
    }
  } else if (audio.content) {
    const Codec* opus = FindCodec(audio.answer->codecs(), kOpusCodecName);
    if (!opus) return Fail(error, "The audio section has no Opus.");
    rtp::AudioStreamConfig a = config_.audio_stream;
    a.payload_type = static_cast<uint8_t>(opus->id);
    a.clock_rate = opus->clockrate;
    audio_pts.push_back(a.payload_type);
    if (send_audio) {
      a.ssrc = local_audio->first_ssrc();
      audio_send_ = session_->AddAudioSend(a);
    }
    if (recv_audio) {
      a.ssrc = remote_audio->first_ssrc();
      audio_recv_ = session_->AddAudioReceive(a, config_.audio_decoder);
    }
  }
  if (video.content && !config_.external_media) {
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
    video_pts.push_back(v.payload_type);
    if (rtx) video_pts.push_back(v.rtx_payload_type);
    v.parser = &h264_parser_;
    if (send_video) {
      v.ssrc = local_video->first_ssrc();
      uint32_t fid = 0;
      v.rtx_ssrc = rtx && local_video->GetFidSsrc(v.ssrc, &fid) ? std::optional<uint32_t>(fid) : std::nullopt;
      video_send_ = session_->AddVideoSend(v);
    }
    if (recv_video) {
      v.ssrc = remote_video->first_ssrc();
      uint32_t fid = 0;
      v.rtx_ssrc = rtx && remote_video->GetFidSsrc(v.ssrc, &fid) ? std::optional<uint32_t>(fid) : std::nullopt;
      video_recv_ = session_->AddVideoReceive(v);
    }
  }

  // BaseChannel::SetRtpTransport / RegisterRtpDemuxerSink and the header extension map, per section.
  for (Section* s : {&audio, &video}) {
    if (!s->content) continue;
    RtpHeaderExtensions exts = s->answer->rtp_header_extensions();
    media_rtp_transport_->RegisterRtpHeaderExtensionMap(s->content->mid(), exts);
    RtpDemuxerCriteria criteria;
    criteria.mid = s->content->mid();
    if (const StreamParams* sp = FirstStream(s->remote)) criteria.ssrcs = sp->ssrcs;
    if (criteria.ssrcs.empty()) criteria.payload_types = s == &audio ? audio_pts : video_pts;
    sinks_.push_back(std::make_unique<ChannelSink>(this));
    media_rtp_transport_->RegisterRtpDemuxerSink(criteria, sinks_.back().get());
  }
  media_rtp_transport_->SubscribeRtcpPacketReceived(
      this, [this](const std::vector<uint8_t>& p, std::optional<Timestamp> arrival) { OnRtcp(p, arrival); });
  return true;
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

void PeerEndpoint::Process(Timestamp now) {
  last_now_ = std::max(last_now_, now);
  transport_controller_.Process(last_now_);
  if (session_) {
    session_->Process(last_now_);
    FlushOutgoing(last_now_);
  }
}

Timestamp PeerEndpoint::NextProcessTime(Timestamp now) const {
  Timestamp next = transport_controller_.NextProcessTime();
  if (session_) next = std::min(next, session_->NextProcessTime(std::max(now, last_now_)));
  return next;
}

}  // namespace sm::pc
