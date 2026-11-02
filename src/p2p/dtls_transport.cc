// libwebrtc's p2p/dtls/dtls_transport.cc (M157) and JsepTransport::NegotiateDtlsRole (pc/jsep_transport.cc), on
// sm::tls::DtlsTransport (#1006). libwebrtc's StreamInterfaceChannel (the bridge from the SSLStreamAdapter down to
// ICE) becomes Pump: sm::tls::DtlsTransport queues what it writes, and Pump sends it on ICE and turns the stack's
// state changes into OnDtlsEvent's.
#include "sm/p2p/dtls_transport.h"

#include <algorithm>

#include "sm/tls/message_digest.h"
#include "sm/tls/ssl_fingerprint.h"

namespace sm::p2p {

namespace {
// Not from rtputils.h, to avoid a layer violation (as libwebrtc).
constexpr size_t kMinRtpPacketLen = 12;
// The initial handshake timeout from the ICE RTT estimate is clamped to this range.
constexpr int kMinDtlsHandshakeTimeoutMs = 50;
constexpr int kMaxDtlsHandshakeTimeoutMs = 3000;
constexpr uint32_t kMaxCachedClientHello = 4;

bool IsRtpPacket(std::span<const uint8_t> payload) {
  return payload.size() >= kMinRtpPacketLen && (payload[0] & 0xC0) == 0x80;
}

int ComputeRetransmissionTimeout(int rtt_ms) {
  return std::max(kMinDtlsHandshakeTimeoutMs, std::min(kMaxDtlsHandshakeTimeoutMs, 2 * rtt_ms));
}

const char* StateName(DtlsTransportState s) {
  switch (s) {
    case DtlsTransportState::kNew: return "kNew";
    case DtlsTransportState::kConnecting: return "kConnecting";
    case DtlsTransportState::kConnected: return "kConnected";
    case DtlsTransportState::kClosed: return "kClosed";
    case DtlsTransportState::kFailed: return "kFailed";
  }
  return "?";
}
}  // namespace

bool NegotiateDtlsRole(SdpType local_description_type, ConnectionRole local_connection_role,
                       ConnectionRole remote_connection_role, std::optional<SslRole> current_role,
                       std::optional<SslRole>& role) {
  // RFC 4145 section 4.1 and RFC 5763 section 5: the offerer uses actpass and must be ready for a ClientHello
  // before the answer; the answerer picks active (recommended) or passive. actpass and passive act as the server,
  // active as the client. RFC 8842 section 5.3: the answerer also handles offers of active or passive.
  bool is_remote_server = false;
  if (local_description_type == SdpType::kOffer) {
    if (local_connection_role != ConnectionRole::kActpass) return false;  // Offerer must use actpass.
    if (remote_connection_role == ConnectionRole::kActive || remote_connection_role == ConnectionRole::kPassive ||
        remote_connection_role == ConnectionRole::kNone) {
      // A remote of none or active acts as the client.
      is_remote_server = remote_connection_role == ConnectionRole::kPassive;
    } else {
      return false;  // Answerer must use active or passive.
    }
  } else {
    if (remote_connection_role != ConnectionRole::kActpass && remote_connection_role != ConnectionRole::kNone) {
      // A remote role other than actpass is accepted if it matches the current negotiated role (dtls-sdp 5.5).
      if (!current_role) {
        if (remote_connection_role == ConnectionRole::kActive && local_connection_role != ConnectionRole::kPassive) {
          return false;  // Answerer must be passive when offerer is active.
        }
        if (remote_connection_role == ConnectionRole::kPassive && local_connection_role != ConnectionRole::kActive) {
          return false;  // Answerer must be active when offerer is passive.
        }
      } else if ((*current_role == SslRole::kClient && remote_connection_role == ConnectionRole::kActive) ||
                 (*current_role == SslRole::kServer && remote_connection_role == ConnectionRole::kPassive)) {
        return false;  // Offerer must use the current negotiated role.
      }
    }
    if (local_connection_role == ConnectionRole::kActive || local_connection_role == ConnectionRole::kPassive) {
      is_remote_server = local_connection_role == ConnectionRole::kActive;
    } else {
      return false;  // Answerer must use active or passive.
    }
  }
  role = is_remote_server ? SslRole::kClient : SslRole::kServer;
  return true;
}

DtlsTransport::DtlsTransport(IceTransportInternal* ice_transport, std::vector<int> srtp_crypto_suites, int max_version)
    : ice_transport_(ice_transport), srtp_ciphers_(std::move(srtp_crypto_suites)), ssl_max_version_(max_version) {
  ConnectToIceTransport();
}

DtlsTransport::~DtlsTransport() {
  ice_transport_->DeregisterReceivedPacketCallback(this);
  ice_transport_->UnsubscribeWritableState(this);
  ice_transport_->UnsubscribeReadyToSend(this);
  ice_transport_->UnsubscribeReceivingState(this);
  ice_transport_->UnsubscribeSentPacket(this);
}

void DtlsTransport::ConnectToIceTransport() {
  ice_transport_->SubscribeWritableState(this, [this](PacketTransportInternal*, Timestamp now) { OnWritableState(now); });
  ice_transport_->RegisterReceivedPacketCallback(
      this, [this](PacketTransportInternal*, const ReceivedPacket& packet) { OnReadPacket(packet); });
  ice_transport_->SubscribeSentPacket(
      this, [this](PacketTransportInternal*, const SentPacketInfo& info) { NotifySentPacket(this, info); });
  ice_transport_->SubscribeReadyToSend(this, [this](PacketTransportInternal*, Timestamp now) {
    if (writable()) NotifyReadyToSend(this, now);
  });
  ice_transport_->SubscribeReceivingState(this,
                                          [this](PacketTransportInternal*, Timestamp now) { OnReceivingState(now); });
}

std::string DtlsTransport::ToString() const {
  return "DtlsTransport[" + transport_name() + "|" + std::to_string(ice_transport_->component()) + "|" +
         (receiving_ ? "R" : "_") + (writable_ ? "W" : "_") + "|" + StateName(dtls_state_) + "]";
}

bool DtlsTransport::SetLocalCertificate(std::shared_ptr<const tls::SslIdentity> identity) {
  if (dtls_active_) {
    // The same identity again may happen during renegotiation; a different one can't replace it.
    return identity && (identity == local_certificate_ || *identity == *local_certificate_);
  }
  if (identity) {
    local_certificate_ = std::move(identity);
    dtls_active_ = true;
  }
  // A null identity: not doing DTLS.
  return true;
}

bool DtlsTransport::SetDtlsRole(SslRole role) {
  if (dtls_) {
    // The role can't be reversed after the session is set up.
    return *dtls_role_ == role;
  }
  if (!dtls_role_ || *dtls_role_ != role) {
    dtls_role_ = role;
    if (role_change_cb_) role_change_cb_(this, role);
  }
  return true;
}

bool DtlsTransport::GetDtlsRole(SslRole* role) const {
  if (!dtls_role_) return false;
  *role = *dtls_role_;
  return true;
}

bool DtlsTransport::SetRemoteParameters(std::string_view digest_alg, std::span<const uint8_t> digest,
                                        std::optional<SslRole> role, Timestamp now) {
  const std::vector<uint8_t> value(digest.begin(), digest.end());
  const bool is_dtls_restart = dtls_active_ && remote_fingerprint_value_ != value;
  // The role is set before the fingerprint, which starts DTLS setup.
  if (role) {
    if (is_dtls_restart) {
      if (!dtls_role_ || *dtls_role_ != *role) {
        dtls_role_ = *role;
        if (role_change_cb_) role_change_cb_(this, *role);
      }
    } else if (!SetDtlsRole(*role)) {
      return false;  // Failed to set the SSL role for the transport.
    }
  }
  return SetRemoteFingerprint(digest_alg, digest, now);
}

bool DtlsTransport::SetRemoteFingerprint(std::string_view digest_alg, std::span<const uint8_t> digest,
                                         Timestamp now) {
  std::vector<uint8_t> value(digest.begin(), digest.end());
  // With a local certificate, the same remote fingerprint may be set again (renegotiation).
  if (dtls_active_ && remote_fingerprint_value_ == value && !digest_alg.empty()) return true;
  // The other side doesn't do DTLS: passthrough.
  if (digest_alg.empty()) {
    dtls_active_ = false;
    return true;
  }
  // Otherwise a local certificate must come first.
  if (!dtls_active_) return false;

  // SetPeerCertificateDigest's checks: a known algorithm and a digest of its length.
  const auto md = tls::CreateMessageDigest(digest_alg);
  const bool digest_ok = md && md->Size() == value.size();

  const bool fingerprint_changing = !remote_fingerprint_value_.empty();
  remote_fingerprint_value_ = std::move(value);
  remote_fingerprint_algorithm_ = std::string(digest_alg);

  if (dtls_ && !fingerprint_changing) {
    // DTLS was set up before the fingerprint arrived (an early ClientHello).
    if (!digest_ok) {
      set_dtls_state(DtlsTransportState::kFailed);
      return false;
    }
    if (!dtls_->SetPeerFingerprint(tls::SslFingerprint(remote_fingerprint_algorithm_, remote_fingerprint_value_),
                                   now)) {
      // Formatted correctly but not the peer's certificate: the transport fails, SetRemoteDescription doesn't.
      set_dtls_state(DtlsTransportState::kFailed);
      Pump(now);
      return true;
    }
    Pump(now);
    return true;
  }

  // A changed fingerprint tears down the DTLS association and makes a new one.
  if (dtls_ && fingerprint_changing) {
    dtls_.reset();
    set_dtls_state(DtlsTransportState::kNew);
    set_writable(false, now);
  }
  if (!digest_ok || !SetupDtls(now)) {
    set_dtls_state(DtlsTransportState::kFailed);
    return false;
  }
  return true;
}

bool DtlsTransport::SetupDtls(Timestamp now) {
  if (!dtls_role_ || !local_certificate_) return false;
  tls::DtlsConfig config;
  config.srtp_crypto_suites = srtp_ciphers_;
  config.max_version = ssl_max_version_;
  dtls_ = std::make_unique<tls::DtlsTransport>(local_certificate_->Clone(), *dtls_role_, config);
  if (!remote_fingerprint_value_.empty()) {
    const auto md = tls::CreateMessageDigest(remote_fingerprint_algorithm_);
    if (!md || md->Size() != remote_fingerprint_value_.size()) return false;
    dtls_->SetPeerFingerprint(tls::SslFingerprint(remote_fingerprint_algorithm_, remote_fingerprint_value_), now);
  }
  // If ICE is already writable, DTLS may start right away.
  MaybeStartDtls(now);
  return true;
}

void DtlsTransport::MaybeStartDtls(Timestamp now) {
  if (!dtls_ || !ice_transport_->writable() || dtls_->state() != tls::DtlsState::kNew) return;
  ConfigureHandshakeTimeout();
  if (!dtls_->Start(now)) {
    // Our configuration's fault: nothing has come in yet.
    set_dtls_state(DtlsTransportState::kFailed);
    return;
  }
  set_dtls_state(DtlsTransportState::kConnecting);
  Pump(now);
  // Now the handshake has started, a stashed ClientHello can be processed.
  if (!cached_client_hello_.empty()) {
    if (*dtls_role_ == SslRole::kServer) {
      const int size = cached_client_hello_.size();
      for (int i = 0; i < size; ++i) {
        if (!HandleDtlsPacket(cached_client_hello_.GetNext(), now)) break;
      }
    }
    // Otherwise it is discarded: we don't have the server role.
    cached_client_hello_.clear();
  }
}

void DtlsTransport::ConfigureHandshakeTimeout() {
  // From the ICE RTT, clamped in case it is extreme; without one the DTLS stack's default (50 ms) stays.
  if (const std::optional<int> rtt_ms = ice_transport_->GetRttEstimate()) {
    dtls_->SetInitialRetransmissionTimeout(ComputeRetransmissionTimeout(*rtt_ms));
  }
}

bool DtlsTransport::HandleDtlsPacket(std::span<const uint8_t> payload, Timestamp now) {
  dtls_->OnPacket(payload, now);
  Pump(now);
  return true;
}

void DtlsTransport::Pump(Timestamp now, const PacketOptions* options) {
  if (!dtls_) return;
  // StreamInterfaceChannel::Write: the next packet carries the caller's options (one record per packet in DTLS);
  // packets the DTLS stack makes itself carry none.
  bool first = true;
  for (auto& d : dtls_->TakeOutgoing()) {
    ice_transport_->SendPacket(now, d, options && first ? *options : PacketOptions{}, kPfNormal);
    first = false;
  }
  // OnDtlsEvent.
  const tls::DtlsState s = dtls_->state();
  if (s == tls::DtlsState::kConnected && dtls_state_ == DtlsTransportState::kConnecting) {
    // SE_OPEN: the handshake completed and the peer is verified.
    set_dtls_state(DtlsTransportState::kConnected);
    set_writable(true, now);
  }
  if (dtls_state_ == DtlsTransportState::kConnected) {
    // SE_READ: one packet may hold several records.
    for (auto& r : dtls_->TakeReceived()) {
      NotifyPacketReceived(ReceivedPacket{r, SocketAddress(), now, ReceivedPacket::kDtlsDecrypted});
      if (!dtls_) return;
    }
  }
  if ((s == tls::DtlsState::kClosed || s == tls::DtlsState::kFailed) &&
      (dtls_state_ == DtlsTransportState::kConnecting || dtls_state_ == DtlsTransportState::kConnected)) {
    // The peer closed the association (close_notify: kClosed) or it failed (an alert, a handshake error: kFailed).
    const bool was_connected = dtls_state_ == DtlsTransportState::kConnected;
    set_writable(false, now);
    set_dtls_state(s == tls::DtlsState::kClosed ? DtlsTransportState::kClosed : DtlsTransportState::kFailed);
    if (was_connected) NotifyOnClose();
  }
}

bool DtlsTransport::GetSslVersionBytes(int* version) const {
  if (dtls_state_ != DtlsTransportState::kConnected) return false;
  *version = dtls_->version();
  return *version != 0;
}

bool DtlsTransport::GetSrtpCryptoSuite(int* cipher) const {
  if (dtls_state_ != DtlsTransportState::kConnected) return false;
  const int suite = dtls_->srtp_crypto_suite();
  if (suite == 0) return false;
  *cipher = suite;
  return true;
}

const tls::SslCertificate* DtlsTransport::GetRemoteCertificate() const {
  return dtls_ ? dtls_->peer_certificate() : nullptr;
}

bool DtlsTransport::AppendSrtpKeyingMaterial(std::vector<uint8_t>& keying_material) const {
  if (!dtls_) return false;
  int key_len = 0, salt_len = 0;
  if (!tls::SrtpKeyAndSaltLength(dtls_->srtp_crypto_suite(), key_len, salt_len)) return false;
  const size_t offset = keying_material.size();
  keying_material.resize(offset + 2 * static_cast<size_t>(key_len + salt_len));
  if (!dtls_->ExportSrtpKeyingMaterial(std::span<uint8_t>(keying_material).subspan(offset))) {
    keying_material.resize(offset);
    return false;
  }
  return true;
}

bool DtlsTransport::IsDtlsConnected() const { return dtls_ && dtls_->handshake_complete(); }

int DtlsTransport::GetRetransmissionCount() const { return dtls_ ? dtls_->retransmissions() : 0; }

std::optional<Timestamp> DtlsTransport::NextTimeout() const {
  return dtls_ ? dtls_->NextTimeout() : std::nullopt;
}

void DtlsTransport::OnTimeout(Timestamp now) {
  if (!dtls_) return;
  if (auto due = dtls_->NextTimeout(); !due || *due > now) return;
  dtls_->OnTimeout(now);
  Pump(now);
}

int DtlsTransport::SendPacket(Timestamp now, std::span<const uint8_t> data, const PacketOptions& options, int flags) {
  if (!dtls_active_) return ice_transport_->SendPacket(now, data, options, flags);  // Not doing DTLS.
  switch (dtls_state_) {
    case DtlsTransportState::kNew:
    case DtlsTransportState::kConnecting:
      return -1;  // Can't send data until the connection is active.
    case DtlsTransportState::kConnected:
      if (flags & kPfSrtpBypass) {
        if (!IsRtpPacket(data)) return -1;
        return ice_transport_->SendPacket(now, data, options, kPfNormal);
      }
      // In DTLS one write is one record in one packet, or nothing.
      if (!dtls_->Send(data, now)) return -1;
      Pump(now, &options);
      return static_cast<int>(data.size());
    case DtlsTransportState::kFailed:
    case DtlsTransportState::kClosed:
      return -1;  // Can't send anything once failed or closed.
  }
  return -1;
}

// The state follows ICE's when not doing DTLS. With DTLS, the transport is neither receiving nor writable before the
// handshake, ICE first becoming writable starts the handshake, and once it completes the state is ICE's again.
void DtlsTransport::OnWritableState(Timestamp now) {
  ice_has_been_writable_ = true;
  if (!dtls_active_) {
    set_writable(ice_transport_->writable(), now);
    return;
  }
  switch (dtls_state_) {
    case DtlsTransportState::kNew:
      MaybeStartDtls(now);
      break;
    case DtlsTransportState::kConnected:
      set_writable(ice_transport_->writable(), now);
      break;
    case DtlsTransportState::kConnecting:
    case DtlsTransportState::kFailed:
    case DtlsTransportState::kClosed:
      break;
  }
}

void DtlsTransport::OnReceivingState(Timestamp now) {
  if (!dtls_active_ || dtls_state_ == DtlsTransportState::kConnected) set_receiving(ice_transport_->receiving(), now);
}

void DtlsTransport::OnReadPacket(const ReceivedPacket& packet) {
  const Timestamp now = packet.arrival_time;
  if (!dtls_active_) {
    NotifyPacketReceived(packet);  // Not doing DTLS.
    return;
  }
  switch (dtls_state_) {
    case DtlsTransportState::kNew:
      // Before DTLS started: keep a ClientHello for when it does.
      if (IsDtlsClientHelloPacket(packet.payload)) {
        cached_client_hello_.AddIfUnique(packet.payload);
        cached_client_hello_.Prune(kMaxCachedClientHello);
        // Without a remote fingerprint and role yet, a ClientHello says the peer took the client role: set up as
        // the server. The fingerprint is checked when it is set.
        if (!dtls_ && local_certificate_) {
          SetDtlsRole(SslRole::kServer);
          SetupDtls(now);
        }
      }
      // Anything else is dropped.
      break;
    case DtlsTransportState::kConnecting:
    case DtlsTransportState::kConnected:
      // Only DTLS or SRTP here: ICE has taken STUN.
      if (IsDtlsPacket(packet.payload)) {
        HandleDtlsPacket(packet.payload, now);
      } else {
        // Not DTLS: the handshake must be complete, and it must be SRTP.
        if (dtls_state_ != DtlsTransportState::kConnected || !IsRtpPacket(packet.payload)) return;
        ReceivedPacket bypass = packet;
        bypass.decryption_info = ReceivedPacket::kSrtpEncrypted;
        NotifyPacketReceived(bypass);
      }
      break;
    case DtlsTransportState::kFailed:
    case DtlsTransportState::kClosed:
      break;  // Dropped.
  }
}

void DtlsTransport::set_receiving(bool receiving, Timestamp now) {
  if (receiving_ == receiving) return;
  receiving_ = receiving;
  NotifyReceivingState(this, now);
}

void DtlsTransport::set_writable(bool writable, Timestamp now) {
  if (writable_ == writable) return;
  // Writable is reported only once ICE has been writable (it can be earlier only with DTLS-in-STUN).
  if (writable && !ice_has_been_writable_) return;
  writable_ = writable;
  if (writable_) NotifyReadyToSend(this, now);
  NotifyWritableState(this, now);
}

void DtlsTransport::set_dtls_state(DtlsTransportState state) {
  if (dtls_state_ == state) return;
  dtls_state_ = state;
  dtls_state_cbs_.Send(this, state);
}

}  // namespace sm::p2p
