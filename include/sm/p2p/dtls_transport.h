// DTLS over an ICE transport: libwebrtc's p2p/dtls/dtls_transport.{h,cc} and dtls_transport_internal.h at M157
// (compare/webrtc/reference/), ported for DTLS-SRTP (C5 part 2, #1006) on sm::tls::DtlsTransport (#994), which
// plays libwebrtc's SSLStreamAdapter.
//
// What it does, as libwebrtc does:
// - Packets from ICE are demultiplexed by their first byte (RFC 7983): DTLS records go to the DTLS stack, and once
//   connected, RTP/RTCP (SRTP) passes up untouched, marked kSrtpEncrypted (SRTP bypass). STUN is ICE's own.
// - The DTLS role comes from SDP's a=setup (NegotiateDtlsRole, pc/jsep_transport.cc), or, if a ClientHello arrives
//   first, this end becomes the server. ClientHellos that arrive before DTLS starts are stashed (up to 4) and fed in
//   when it does.
// - The remote fingerprint from SDP is checked against the peer's certificate; a mismatch fails the transport (and
//   the peer, through a fatal alert). A fingerprint that arrives after the handshake is checked then.
// - The handshake starts when ICE first becomes writable, with the first retransmission timeout 2 x the ICE RTT
//   clamped to 50-3000 ms (ComputeRetransmissionTimeout), or the DTLS stack's 50 ms without an RTT estimate.
// - SendPacket encrypts with DTLS, or with kPfSrtpBypass sends an RTP/RTCP packet (already SRTP) directly.
// - Without a local certificate it is a passthrough (IsDtlsActive() false), as in libwebrtc.
//
// The library's time model: nothing reads a clock or starts a thread. Calls that can send take `now`, and the
// handshake's retransmission timer is NextTimeout()/OnTimeout(now), which the owner runs.
//
// Differences from libwebrtc, none of them on the wire:
// - DTLS-in-STUN (dtls_stun_piggyback_controller, SPED) is not ported: M157 has it behind a field trial that is off
//   by default (#1006).
// - The negotiated TLS cipher suite, key-exchange group and peer signature algorithm (GetSslCipherSuite,
//   GetSslGroupId, GetSslPeerSignatureAlgorithm, for getStats) are not exposed yet: sm::tls::DtlsTransport doesn't
//   report them (C8 stats).
// - No UMA histograms, RTC event log entries or network route (PacketTransportInternal's comment).
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/dtls_utils.h"
#include "sm/p2p/ice_transport_internal.h"
#include "sm/p2p/packet_transport_internal.h"
#include "sm/tls/dtls_transport.h"
#include "sm/tls/ssl_identity.h"

namespace sm::p2p {

// api/dtls_transport_interface.h.
enum class DtlsTransportState { kNew, kConnecting, kConnected, kClosed, kFailed };
using SslRole = tls::DtlsRole;

// p2p/base/transport_description.h's ConnectionRole: SDP's a=setup (RFC 4145).
enum class ConnectionRole { kNone, kActive, kPassive, kActpass, kHoldconn };
enum class SdpType { kOffer, kPrAnswer, kAnswer };

// JsepTransport::NegotiateDtlsRole: this end's DTLS role from the local description's type and both a=setup values
// (RFC 5763 section 5, RFC 8842 section 5.3). `current_role` is the role already negotiated, if any. Returns false
// (and leaves `role` alone) for a combination libwebrtc rejects.
bool NegotiateDtlsRole(SdpType local_description_type, ConnectionRole local_connection_role,
                       ConnectionRole remote_connection_role, std::optional<SslRole> current_role,
                       std::optional<SslRole>& role);

// libwebrtc's DtlsTransportInternalImpl.
class DtlsTransport : public PacketTransportInternal {
 public:
  using DtlsStateCallback = std::function<void(DtlsTransport*, DtlsTransportState)>;
  using RoleCallback = std::function<void(DtlsTransport*, SslRole)>;

  // See RFC 9147 section 5.8.2: 400 ms first timeout, as 2 x this estimate.
  static constexpr int kDefaultHandshakeEstimateRttMs = 200;

  // `ice_transport` must outlive this transport. `srtp_crypto_suites` are the DTLS-SRTP profiles offered
  // (CryptoOptions::GetSupportedDtlsSrtpCryptoSuites; empty for no DTLS-SRTP), and `max_version` the highest DTLS
  // version (SSL_PROTOCOL_DTLS_12 by default, as DtlsTransportInternalImpl's constructor).
  explicit DtlsTransport(IceTransportInternal* ice_transport,
                         std::vector<int> srtp_crypto_suites = tls::DtlsConfig{}.srtp_crypto_suites,
                         int max_version = tls::kDtls12);
  ~DtlsTransport() override;

  DtlsTransportState dtls_state() const { return dtls_state_; }
  const std::string& transport_name() const override { return ice_transport_->transport_name(); }
  int component() const { return ice_transport_->component(); }
  IceTransportInternal* ice_transport() { return ice_transport_; }

  // DTLS is active once a local certificate is set; otherwise packets pass straight through to ICE.
  bool IsDtlsActive() const { return dtls_active_; }
  // Makes DTLS active. Must come before SetRemoteParameters. Setting the same identity again is a no-op; changing
  // it once active fails.
  bool SetLocalCertificate(std::shared_ptr<const tls::SslIdentity> identity);
  const tls::SslIdentity* local_certificate() const { return local_certificate_.get(); }

  // The remote fingerprint (from SDP a=fingerprint) and the DTLS role (from a=setup, NegotiateDtlsRole). The role
  // is set before the fingerprint, which starts DTLS setup. An empty `digest_alg` means the peer doesn't do DTLS
  // (passthrough). A new fingerprint after DTLS was set up restarts DTLS. False for invalid parameters; a well-formed
  // fingerprint that doesn't match the peer's certificate fails the transport but returns true, as libwebrtc.
  bool SetRemoteParameters(std::string_view digest_alg, std::span<const uint8_t> digest,
                           std::optional<SslRole> role, Timestamp now);

  bool GetDtlsRole(SslRole* role) const;
  // False if the role would change after DTLS was set up.
  bool SetDtlsRole(SslRole role);

  // SendPacket, through DTLS, or with kPfSrtpBypass directly (RTP/RTCP only). -1 until connected.
  int SendPacket(Timestamp now, std::span<const uint8_t> data, const PacketOptions& options,
                 int flags = kPfNormal) override;
  bool writable() const override { return writable_; }
  bool receiving() const override { return receiving_; }
  int GetError() override { return ice_transport_->GetError(); }
  int SetOption(SocketOption opt, int value) override { return ice_transport_->SetOption(opt, value); }
  bool GetOption(SocketOption opt, int* value) override { return ice_transport_->GetOption(opt, value); }

  // The negotiated DTLS version (tls::kDtls12 or kDtls13) and DTLS-SRTP profile; false unless connected.
  bool GetSslVersionBytes(int* version) const;
  bool GetSrtpCryptoSuite(int* cipher) const;
  // The peer's certificate, once the handshake received it (GetRemoteSSLCertChain).
  const tls::SslCertificate* GetRemoteCertificate() const;
  // Appends the RFC 5764 keying material for the negotiated profile (AppendSrtpKeyingMaterial): client key, server
  // key, client salt, server salt. False unless DTLS is set up and connected with a profile.
  bool AppendSrtpKeyingMaterial(std::vector<uint8_t>& keying_material) const;

  // The handshake finished; true even before the remote fingerprint verified the peer (IsDtlsConnected).
  bool IsDtlsConnected() const;
  // Handshake retransmissions so far (GetRetransmissionCount).
  int GetRetransmissionCount() const;
  // The handshake's retransmission timer: when it fires, and its handler.
  std::optional<Timestamp> NextTimeout() const;
  void OnTimeout(Timestamp now);

  // DtlsTransportInternal's signals: the DTLS state (any number of receivers), the role change (one).
  void SubscribeDtlsTransportState(const void* tag, DtlsStateCallback cb) { dtls_state_cbs_.Add(tag, std::move(cb)); }
  void UnsubscribeDtlsTransportState(const void* tag) { dtls_state_cbs_.Remove(tag); }
  void SubscribeDtlsRoleChange(RoleCallback cb) { role_change_cb_ = std::move(cb); }

  std::string ToString() const;

 private:
  void ConnectToIceTransport();
  void OnWritableState(Timestamp now);
  void OnReceivingState(Timestamp now);
  void OnReadPacket(const ReceivedPacket& packet);
  bool SetRemoteFingerprint(std::string_view digest_alg, std::span<const uint8_t> digest, Timestamp now);
  bool SetupDtls(Timestamp now);
  void MaybeStartDtls(Timestamp now);
  bool HandleDtlsPacket(std::span<const uint8_t> payload, Timestamp now);
  void ConfigureHandshakeTimeout();
  // Sends what the DTLS stack wrote and turns its state changes into this transport's (OnDtlsEvent).
  void Pump(Timestamp now, const PacketOptions* options = nullptr);
  void set_receiving(bool receiving, Timestamp now);
  void set_writable(bool writable, Timestamp now);
  void set_dtls_state(DtlsTransportState state);

  IceTransportInternal* const ice_transport_;
  const std::vector<int> srtp_ciphers_;
  const int ssl_max_version_;
  DtlsTransportState dtls_state_ = DtlsTransportState::kNew;
  std::unique_ptr<tls::DtlsTransport> dtls_;
  bool dtls_started_ = false;
  bool dtls_active_ = false;
  std::shared_ptr<const tls::SslIdentity> local_certificate_;
  std::optional<SslRole> dtls_role_;
  std::vector<uint8_t> remote_fingerprint_value_;
  std::string remote_fingerprint_algorithm_;
  // ClientHellos received before the handshake started (before ICE was writable, or before the remote
  // fingerprint arrived).
  PacketStash cached_client_hello_;
  bool receiving_ = false;
  bool writable_ = false;
  bool ice_has_been_writable_ = false;
  TaggedCallbackList<DtlsTransport*, DtlsTransportState> dtls_state_cbs_;
  RoleCallback role_change_cb_;
};

}  // namespace sm::p2p
