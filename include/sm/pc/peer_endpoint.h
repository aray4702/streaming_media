// An endpoint that sets up a call from SDP (C6 part 3, #1064): the transport half of libwebrtc M157's
// pc/sdp_offer_answer.cc and pc/peer_connection.cc (compare/webrtc/reference/pc/) over JsepTransportController
// (sm/pc/jsep_transport_controller.h), with the library's RTP session (sm/rtp/session.h) as the media channels.
//
// What it does, as libwebrtc does for C6's subset (one audio and one video transceiver, Unified Plan):
// - CreateOffer / CreateAnswer through MediaSessionDescriptionFactory (#1063) with the engines' default codecs
//   (Opus; H.264 constrained baseline with RTX) and header extensions, BUNDLE and rtcp-mux, a fresh 64-bit session
//   id and version 2 (WebRtcSessionDescriptionFactory). The answer follows the offer's m= sections: audio and video
//   are answered (sendrecv while this end sends), the data section as below, and any other section is rejected.
// - SetLocalDescription / SetRemoteDescription check the signaling state (stable -> have-local-offer or
//   have-remote-offer -> stable), apply the description to the transports, record the payload types
//   (SdpPayloadTypeSuggester) and, after a local description, start ICE gathering. Candidates in a remote
//   description are added (UseCandidatesInRemoteDescription).
// - AddIceCandidate adds a trickled candidate to the transport of its mid (or of its m-line index); a candidate for
//   a rejected section is ignored. Gathered candidates come out through the ice candidate callback with their mid
//   and m-line index (OnIceCandidate).
// - When an answer is applied, the RTP session is configured from the negotiated sections, as the voice and video
//   channels are by SetLocalContent / SetRemoteContent: the send streams take the local section's SSRCs (video with
//   its RTX SSRC from the FID group), the receive streams the remote section's, both the answer's payload types
//   (Opus; the first H.264 and its RTX), the negotiated header extensions (one id map: BUNDLE requires the same
//   ids), the MIDs when the MID extension is negotiated, NACK as negotiated for H.264, and the CNAME of the local
//   stream. Each section's demuxer sink (its MID and the remote SSRCs) and header extension map are registered on
//   the BUNDLE transport, as BaseChannel does; RTP and RTCP it receives go to the session, and the session's
//   packets go out through it, protected with the DTLS-SRTP keys (PF_SRTP_BYPASS).
// - Data channels (C7 part 5, #1091), as pc/data_channel_controller.cc and the SCTP half of pc/sdp_offer_answer.cc,
//   with the per-connection switch `enable_data_channels` on (the default, as libwebrtc):
//   - CreateDataChannel before or after the offer; the offer carries the m=application section (UDP/DTLS/SCTP,
//     a=sctp-port 5000, a=max-message-size 262144) once a channel exists (GetOptionsForOffer adds it last, when
//     HasDataChannels(), with the next mid), on the BUNDLE transport;
//   - an answer accepts the first data section of a remote offer (GetOptionsForAnswer) and rejects the others;
//   - when both descriptions have the section (PushdownMediaDescription: "complete offer/answer with an SCTP m=
//     section before starting SCTP"), the SCTP transport (sm/sctp/, #1082) starts over the section's DTLS
//     transport with the local and remote ports and the smaller max message size (the local one if the remote's
//     is 0); stream ids follow the DTLS role (DataChannelController);
//   - in-band (DCEP) and negotiated channels open, and the remote side's in-band channels are announced through
//     the data channel callback; a rejected data section closes the channels (DestroyDataChannelTransport).
//   With the switch off: no m=application in an offer, a data section in a remote offer is rejected (port 0), no
//   SCTP object is created, and CreateDataChannel fails (kInvalidState). Built without SM_WITH_DATACHANNEL, none of
//   this is compiled in and CreateDataChannel fails with kUnsupportedOperation.
//
// The library's time model: nothing reads a clock or starts a thread. The owner runs Process(now) at
// NextProcessTime(now) and delivers the sockets' packets (the port allocator's socket factory) itself.
//
// Differences from libwebrtc, none of them on the wire:
// - One audio and one video transceiver, added at construction; no tracks, streams or transceiver API, no
//   renegotiation after the first offer/answer (a second offer is refused), no rollback or pranswer, and no
//   unsignaled (SSRC-less) receive streams.
// - The RTP session is created when the answer is applied, not per channel at the first description; until then
//   nothing is sent (no early media), which libwebrtc can't send either before DTLS-SRTP is up.
// - The SCTP transport is created by the endpoint when SCTP starts, on the DTLS transport the data section ended
//   up on, rather than one per JsepTransport at its creation (JsepTransportController's sctp factory); the same
//   association on the wire. No always_negotiate_data_channels, no SCTP SNAP (sctp-init), no renegotiation.
// - STUN needs its crypto hooks: call p2p::InstallStunCryptoFromTls() once at startup (sm::p2p_tls).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/port_allocator.h"
#include "sm/pc/codec_vendor.h"
#include "sm/pc/data_channel_interface.h"
#include "sm/pc/jsep.h"
#include "sm/pc/jsep_transport_controller.h"
#include "sm/pc/media_session.h"
#include "sm/pc/sdp_payload_type_suggester.h"
#include "sm/pc/transport_description_factory.h"
#include "sm/pc/unique_id_generator.h"
#include "sm/rtp/h264.h"
#include "sm/rtp/session.h"
#include "sm/tls/ssl_identity.h"

namespace sm::sctp {
class SctpTransport;
}  // namespace sm::sctp

namespace sm::pc {

class DataChannelController;
class SctpDataChannel;

struct PeerEndpointConfig {
  // Not owned; must outlive the endpoint. Its socket factory carries the ICE traffic.
  p2p::PortAllocator* allocator = nullptr;
  // The DTLS certificate; null: one is made (RTCCertificateGenerator) with this name.
  std::shared_ptr<const tls::SslIdentity> certificate;
  std::string certificate_name = "WebRTC";
  JsepTransportController::Config transport;
  bool audio = true;  // An audio transceiver (sendrecv).
  bool video = true;  // A video transceiver (sendrecv).
  // The track and stream ids of the senders (a=msid).
  std::string stream_id = "stream";
  std::string audio_track_id = "audio";
  std::string video_track_id = "video";
  // The RTP session's settings; SSRCs, CNAME, MIDs, extensions and payload types are filled in from the SDP.
  rtp::SessionConfig session;
  rtp::AudioStreamConfig audio_stream;
  rtp::VideoStreamConfig video_stream;
  // Decodes the received Opus frames; not owned. Null: no audio receive stream.
  rtp::AudioDecoder* audio_decoder = nullptr;
  // Data channels for this connection (C7's runtime switch). Off: no SCTP object and no m=application. Has no
  // effect when the library is built without SM_WITH_DATACHANNEL.
  bool enable_data_channels = true;

  // The host runs its own media sessions (the test bench, #1087): the endpoint makes no rtp::Session. It
  // demuxes what the BUNDLE transport receives for the negotiated sections to `on_media_packet` (RTP and RTCP,
  // after SRTP), and sends the host's packets with SendMediaPacket. Off: the endpoint's own session, as above.
  bool external_media = false;
  std::function<void(std::span<const uint8_t> packet, bool rtcp, Timestamp arrival)> on_media_packet;
  // The SSRCs the local description announces (a=ssrc, the FID group), for a host whose sessions send on fixed
  // SSRCs, as an app that munges the SDP before SetLocalDescription. Unset: the generated ones.
  std::optional<uint32_t> audio_ssrc, video_ssrc, video_rtx_ssrc;
};

// PeerConnectionInterface::SignalingState, for C6's subset.
enum class SignalingState { kStable, kHaveLocalOffer, kHaveRemoteOffer, kClosed };

class PeerEndpoint {
 public:
  using IceCandidateCallback = std::function<void(std::unique_ptr<IceCandidate>)>;

  PeerEndpoint(PeerEndpointConfig config, Timestamp now);
  ~PeerEndpoint();
  PeerEndpoint(const PeerEndpoint&) = delete;
  PeerEndpoint& operator=(const PeerEndpoint&) = delete;

  std::unique_ptr<SessionDescriptionInterface> CreateOffer(std::string* error = nullptr);
  // Needs a remote offer.
  std::unique_ptr<SessionDescriptionInterface> CreateAnswer(std::string* error = nullptr);
  bool SetLocalDescription(std::unique_ptr<SessionDescriptionInterface> desc, Timestamp now,
                           std::string* error = nullptr);
  bool SetRemoteDescription(std::unique_ptr<SessionDescriptionInterface> desc, Timestamp now,
                            std::string* error = nullptr);
  bool AddIceCandidate(const IceCandidate& candidate, Timestamp now, std::string* error = nullptr);
  void SetOnIceCandidate(IceCandidateCallback cb) { on_ice_candidate_ = std::move(cb); }

  SignalingState signaling_state() const { return signaling_state_; }
  const SessionDescriptionInterface* local_description() const { return local_description_.get(); }
  const SessionDescriptionInterface* remote_description() const { return remote_description_.get(); }
  JsepTransportController& transport_controller() { return transport_controller_; }
  // The transport of the first audio or video section (the BUNDLE transport); null before a description.
  JsepTransport* media_transport() const;
  // Both ends' ICE is writable, DTLS connected and SRTP keyed.
  bool IsConnected() const;

  // The RTP session, once an answer is applied; null before.
  rtp::Session* session() { return session_.get(); }
  std::optional<rtp::AudioSendId> audio_send() const { return audio_send_; }
  std::optional<rtp::VideoSendId> video_send() const { return video_send_; }
  std::optional<rtp::AudioRecvId> audio_receive() const { return audio_recv_; }
  std::optional<rtp::VideoRecvId> video_receive() const { return video_recv_; }

  // RTCPeerConnection.createDataChannel(), before or after the offer. Null, with `error` set, when data channels are
  // off for this connection (kInvalidState), not built (kUnsupportedOperation), or the config is invalid.
  std::shared_ptr<SctpDataChannel> CreateDataChannel(std::string_view label, const DataChannelInit& init,
                                                     Timestamp now, RtcError* error = nullptr);
  // The remote side opened a channel in-band (PeerConnectionObserver::OnDataChannel).
  void SetOnDataChannel(std::function<void(std::shared_ptr<SctpDataChannel>)> callback);
  bool data_channels_enabled() const { return data_channels_ != nullptr; }
  // Null when data channels are off or not built.
  DataChannelController* data_channel_controller();
  // The SCTP transport; null until SCTP starts, and always with data channels off.
  sctp::SctpTransport* sctp_transport();
  // The negotiated data section's mid, once a description with one is applied.
  const std::optional<std::string>& sctp_mid() const { return sctp_mid_; }

  // ICE checks, DTLS retransmissions and the RTP session's timers due by `now`; the session's packets go out.
  void Process(Timestamp now);
  Timestamp NextProcessTime(Timestamp now) const;
  // Sends the session's queued packets (after SendAudio / SendVideo on session()).
  void FlushOutgoing(Timestamp now);

  // external_media: sends one RTP or RTCP packet of the host's sessions over the BUNDLE transport (SRTP); false if
  // it can't (not connected, no transport).
  bool SendMediaPacket(std::span<const uint8_t> packet, bool rtcp, Timestamp now);
  // The ICE and DTLS timers only (external_media, whose host runs the sessions).
  void ProcessTransport(Timestamp now);

  // Packets the session produced that the transport could not send (not writable yet, or SRTP not active).
  int send_failures() const { return send_failures_; }

 private:
  class ChannelSink;
  struct DataChannels;
  MediaSessionOptions OfferOptions() const;
  MediaSessionOptions AnswerOptions(const SessionDescription& offer) const;
  std::unique_ptr<SessionDescriptionInterface> Wrap(SdpType type, std::unique_ptr<SessionDescription> desc);
  void ApplyFixedSsrcs(SessionDescription* desc) const;
  bool ApplyRemoteCandidates(const SessionDescriptionInterface& desc, Timestamp now);
  void OnCandidateGathered(const std::string& transport_name, const p2p::Candidate& candidate);
  bool CreateMediaSession(std::string* error);
  void OnRtcp(const std::vector<uint8_t>& packet, std::optional<Timestamp> arrival);
  void Deliver(std::span<const uint8_t> packet, bool rtcp, Timestamp arrival);
  bool UpdateDataChannelTransport(const SessionDescription& desc, std::string* error);
  bool MaybeStartSctp(Timestamp now, std::string* error);
  void DestroyDataChannelTransport(const char* reason);

  PeerEndpointConfig config_;
  std::shared_ptr<const tls::SslIdentity> certificate_;
  TransportDescriptionFactory transport_desc_factory_;
  UniqueRandomIdGenerator ssrc_generator_;
  SdpPayloadTypeSuggester suggester_;
  std::unique_ptr<CodecVendor> codec_vendor_;
  std::unique_ptr<MediaSessionDescriptionFactory> session_desc_factory_;
  JsepTransportController transport_controller_;
  std::string session_id_;
  uint64_t session_version_ = 2;

  SignalingState signaling_state_ = SignalingState::kStable;
  std::unique_ptr<SessionDescriptionInterface> local_description_;
  std::unique_ptr<SessionDescriptionInterface> remote_description_;
  IceCandidateCallback on_ice_candidate_;

  rtp::H264PayloadParser h264_parser_;
  std::unique_ptr<rtp::Session> session_;
  DtlsSrtpTransport* media_rtp_transport_ = nullptr;
  std::vector<std::unique_ptr<ChannelSink>> sinks_;
  std::optional<rtp::AudioSendId> audio_send_;
  std::optional<rtp::VideoSendId> video_send_;
  std::optional<rtp::AudioRecvId> audio_recv_;
  std::optional<rtp::VideoRecvId> video_recv_;
  Timestamp last_now_ = Timestamp::MinusInfinity();
  int send_failures_ = 0;

  // Data channels (the controller and the SCTP transport); after transport_controller_, so they go before the DTLS
  // transport they use. Null with the switch off or without SM_WITH_DATACHANNEL.
  std::unique_ptr<DataChannels> data_channels_;
  std::optional<std::string> sctp_mid_;
};

}  // namespace sm::pc
