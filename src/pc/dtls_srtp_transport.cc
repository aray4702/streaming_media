// libwebrtc's pc/dtls_srtp_transport.cc (M157); see sm/pc/dtls_srtp_transport.h.
#include "sm/pc/dtls_srtp_transport.h"

#include <cassert>
#include <utility>

#include "sm/core/logging.h"

namespace sm::pc {
namespace {

// Zeroes key material before it goes (libwebrtc's ZeroOnFreeBuffer).
struct ZeroOnFree {
  std::vector<uint8_t>& v;
  ~ZeroOnFree() {
    volatile uint8_t* p = v.data();
    for (size_t i = 0; i < v.size(); ++i) p[i] = 0;
    v.clear();
  }
};

// GetSrtpKeyAndSaltLengths (rtc_base/ssl_stream_adapter.cc): RFC 3711, RFC 7714.
bool GetSrtpKeyAndSaltLengths(int crypto_suite, int* key_length, int* salt_length) {
  switch (crypto_suite) {
    case tls::kSrtpAes128CmSha1_32:
    case tls::kSrtpAes128CmSha1_80:
      *key_length = 16;
      *salt_length = 14;
      return true;
    case tls::kSrtpAeadAes128Gcm:
      *key_length = 16;
      *salt_length = 12;
      return true;
    case tls::kSrtpAeadAes256Gcm:
      *key_length = 32;
      *salt_length = 12;
      return true;
    default:
      return false;
  }
}

void ValidateAndLogTransport(p2p::DtlsTransport* rtp_dtls_transport, p2p::DtlsTransport* old_rtcp_dtls_transport,
                             p2p::DtlsTransport* rtcp_dtls_transport, bool is_srtp_active) {
  // A new RTCP transport after DTLS-SRTP is active needs BUNDLE without rtcp-mux, which BUNDLE doesn't allow.
  if (rtcp_dtls_transport && rtcp_dtls_transport != old_rtcp_dtls_transport) {
    assert(!is_srtp_active && "Setting RTCP for DTLS/SRTP after the DTLS is active should never happen.");
    (void)is_srtp_active;
  }
  if (rtcp_dtls_transport && rtp_dtls_transport) {
    assert(rtcp_dtls_transport->transport_name() == rtp_dtls_transport->transport_name());
  }
}

}  // namespace

DtlsSrtpTransport::DtlsSrtpTransport(bool rtcp_mux_enabled, SrtpTransportOptions options)
    : SrtpTransport(rtcp_mux_enabled, options) {}

DtlsSrtpTransport::~DtlsSrtpTransport() {
  if (rtp_dtls_transport()) rtp_dtls_transport()->UnsubscribeDtlsTransportState(this);
  if (rtcp_dtls_transport()) rtcp_dtls_transport()->UnsubscribeDtlsTransportState(this);
}

void DtlsSrtpTransport::SetDtlsTransports(p2p::DtlsTransport* rtp_dtls, p2p::DtlsTransport* rtcp_dtls) {
  ValidateAndLogTransport(rtp_dtls, rtcp_dtls_transport(), rtcp_dtls, IsSrtpActive());
  const bool rtp_changed = MaybeUnsubscribe(rtp_dtls_transport(), rtp_dtls);
  const bool rtcp_changed = MaybeUnsubscribe(rtcp_dtls_transport(), rtcp_dtls);
  // Now pass the transports to RtpTransport.
  SetRtpPacketTransport(rtp_dtls);
  SetRtcpPacketTransport(rtcp_dtls);
  if (rtp_changed) SetupDtlsTransport(rtp_dtls, /*is_rtcp=*/false);
  if (rtcp_changed) SetupDtlsTransport(rtcp_dtls, /*is_rtcp=*/true);
}

void DtlsSrtpTransport::SetDtlsTransportsOwned(std::unique_ptr<p2p::DtlsTransport> rtp_dtls,
                                               std::unique_ptr<p2p::DtlsTransport> rtcp_dtls) {
  ValidateAndLogTransport(rtp_dtls.get(), rtcp_dtls_transport(), rtcp_dtls.get(), IsSrtpActive());
  const bool rtp_changed = MaybeUnsubscribe(rtp_dtls_transport(), rtp_dtls.get());
  const bool rtcp_changed = MaybeUnsubscribe(rtcp_dtls_transport(), rtcp_dtls.get());
  SetRtpPacketTransportOwned(std::move(rtp_dtls));
  SetRtcpPacketTransportOwned(std::move(rtcp_dtls));
  if (rtp_changed) SetupDtlsTransport(rtp_dtls_transport(), /*is_rtcp=*/false);
  if (rtcp_changed) SetupDtlsTransport(rtcp_dtls_transport(), /*is_rtcp=*/true);
}

void DtlsSrtpTransport::SetRtcpMuxEnabled(bool enable) {
  SrtpTransport::SetRtcpMuxEnabled(enable);
  if (enable) MaybeSetupDtlsSrtp();
}

void DtlsSrtpTransport::UpdateSendEncryptedHeaderExtensionIds(const std::vector<int>& send_extension_ids) {
  if (send_extension_ids_ == send_extension_ids) return;
  send_extension_ids_.emplace(send_extension_ids);
  // Re-keys the sessions to update the send extension ids.
  if (DtlsHandshakeCompleted()) SetupRtpDtlsSrtp();
}

void DtlsSrtpTransport::UpdateRecvEncryptedHeaderExtensionIds(const std::vector<int>& recv_extension_ids) {
  if (recv_extension_ids_ == recv_extension_ids) return;
  recv_extension_ids_.emplace(recv_extension_ids);
  if (DtlsHandshakeCompleted()) SetupRtpDtlsSrtp();
}

bool DtlsSrtpTransport::IsDtlsActive() const {
  p2p::DtlsTransport* rtcp = rtcp_mux_enabled() ? nullptr : rtcp_dtls_transport();
  return rtp_dtls_transport() && rtp_dtls_transport()->IsDtlsActive() && (!rtcp || rtcp->IsDtlsActive());
}

bool DtlsSrtpTransport::IsDtlsConnected() const {
  p2p::DtlsTransport* rtcp = rtcp_mux_enabled() ? nullptr : rtcp_dtls_transport();
  return rtp_dtls_transport() && rtp_dtls_transport()->dtls_state() == p2p::DtlsTransportState::kConnected &&
         (!rtcp || rtcp->dtls_state() == p2p::DtlsTransportState::kConnected);
}

bool DtlsSrtpTransport::IsDtlsWritable() const {
  p2p::DtlsTransport* rtcp = rtcp_mux_enabled() ? nullptr : rtcp_dtls_transport();
  return rtp_dtls_transport() && rtp_dtls_transport()->writable() && (!rtcp || rtcp->writable());
}

bool DtlsSrtpTransport::DtlsHandshakeCompleted() const { return IsDtlsActive() && IsDtlsConnected(); }

void DtlsSrtpTransport::MaybeSetupDtlsSrtp() {
  if (IsSrtpActive() || !IsDtlsWritable()) return;
  SetupRtpDtlsSrtp();
  if (!rtcp_mux_enabled() && rtcp_dtls_transport()) SetupRtcpDtlsSrtp();
}

void DtlsSrtpTransport::SetupRtpDtlsSrtp() {
  // Empty encrypted header extension ids if not set: the handshake can finish before the offer/answer that
  // carries them.
  const std::vector<int> send_extension_ids = send_extension_ids_.value_or(std::vector<int>{});
  const std::vector<int> recv_extension_ids = recv_extension_ids_.value_or(std::vector<int>{});
  int selected_crypto_suite = 0;
  std::vector<uint8_t> send_key, recv_key;
  ZeroOnFree zero_send{send_key}, zero_recv{recv_key};
  if (!ExtractParams(rtp_dtls_transport(), &selected_crypto_suite, &send_key, &recv_key) ||
      !SetRtpParams(selected_crypto_suite, send_key, send_extension_ids, selected_crypto_suite, recv_key,
                    recv_extension_ids)) {
    SM_LOG(kWarning, "DtlsSrtpTransport: DTLS-SRTP key installation for RTP failed");
  }
}

void DtlsSrtpTransport::SetupRtcpDtlsSrtp() {
  // Nothing to do if DTLS-SRTP is active: RTCP needs no extension id update, and the keys stay.
  if (IsSrtpActive()) return;
  const std::vector<int> send_extension_ids = send_extension_ids_.value_or(std::vector<int>{});
  const std::vector<int> recv_extension_ids = recv_extension_ids_.value_or(std::vector<int>{});
  int selected_crypto_suite = 0;
  std::vector<uint8_t> rtcp_send_key, rtcp_recv_key;
  ZeroOnFree zero_send{rtcp_send_key}, zero_recv{rtcp_recv_key};
  if (!ExtractParams(rtcp_dtls_transport(), &selected_crypto_suite, &rtcp_send_key, &rtcp_recv_key) ||
      !SetRtcpParams(selected_crypto_suite, rtcp_send_key, send_extension_ids, selected_crypto_suite, rtcp_recv_key,
                     recv_extension_ids)) {
    SM_LOG(kWarning, "DtlsSrtpTransport: DTLS-SRTP key installation for RTCP failed");
  }
}

bool DtlsSrtpTransport::ExtractParams(p2p::DtlsTransport* dtls_transport, int* selected_crypto_suite,
                                      std::vector<uint8_t>* send_key, std::vector<uint8_t>* recv_key) {
  if (!dtls_transport || !dtls_transport->IsDtlsActive()) return false;
  if (!dtls_transport->GetSrtpCryptoSuite(selected_crypto_suite)) {
    SM_LOG(kError, "DtlsSrtpTransport: no DTLS-SRTP selected crypto suite");
    return false;
  }
  int key_len = 0, salt_len = 0;
  if (!GetSrtpKeyAndSaltLengths(*selected_crypto_suite, &key_len, &salt_len)) {
    SM_LOG(kError, "DtlsSrtpTransport: unknown DTLS-SRTP crypto suite %d", *selected_crypto_suite);
    return false;
  }
  // RFC 5705 exporter with the RFC 5764 parameters.
  std::vector<uint8_t> dtls_buffer;
  ZeroOnFree zero_buffer{dtls_buffer};
  if (!dtls_transport->AppendSrtpKeyingMaterial(dtls_buffer)) {
    SM_LOG(kError, "DtlsSrtpTransport: DTLS-SRTP key export failed");
    return false;
  }
  if (dtls_buffer.size() != static_cast<size_t>(2 * key_len + 2 * salt_len)) return false;
  // RFC 5764 section 4.2: client_write_key | server_write_key | client_write_salt | server_write_salt.
  const uint8_t* b = dtls_buffer.data();
  std::vector<uint8_t> client_write_key(b, b + key_len);
  std::vector<uint8_t> server_write_key(b + key_len, b + 2 * key_len);
  ZeroOnFree zero_client{client_write_key}, zero_server{server_write_key};
  client_write_key.insert(client_write_key.end(), b + 2 * key_len, b + 2 * key_len + salt_len);
  server_write_key.insert(server_write_key.end(), b + 2 * key_len + salt_len, b + 2 * key_len + 2 * salt_len);

  p2p::SslRole role;
  if (!dtls_transport->GetDtlsRole(&role)) {
    SM_LOG(kWarning, "DtlsSrtpTransport: failed to get the DTLS role");
    return false;
  }
  if (role == p2p::SslRole::kServer) {
    *send_key = server_write_key;
    *recv_key = client_write_key;
  } else {
    *send_key = client_write_key;
    *recv_key = server_write_key;
  }
  return true;
}

void DtlsSrtpTransport::SetupDtlsTransport(p2p::DtlsTransport* dtls_transport, bool is_rtcp) {
  if (dtls_transport) {
    dtls_transport->SubscribeDtlsTransportState(
        this, [this](p2p::DtlsTransport* transport, p2p::DtlsTransportState state) { OnDtlsState(transport, state); });
    // The initial state.
    OnDtlsState(dtls_transport, dtls_transport->dtls_state());
  } else {
    // A removed transport usually resets SRTP; but an RTCP transport removed because rtcp-mux is now on keeps it:
    // the RTP transport carries the SRTP session.
    if (is_rtcp && rtcp_mux_enabled()) return;
    OnDtlsState(nullptr, p2p::DtlsTransportState::kNew);
  }
}

bool DtlsSrtpTransport::MaybeUnsubscribe(p2p::DtlsTransport* old_transport, p2p::DtlsTransport* new_transport) {
  if (old_transport && old_transport != new_transport) old_transport->UnsubscribeDtlsTransportState(this);
  return old_transport != new_transport;
}

void DtlsSrtpTransport::OnDtlsState(p2p::DtlsTransport* /*transport*/, p2p::DtlsTransportState state) {
  if (on_dtls_state_change_) on_dtls_state_change_();
  if (state != p2p::DtlsTransportState::kConnected) ResetParams();
  MaybeSetupDtlsSrtp();
}

void DtlsSrtpTransport::OnWritableState(p2p::PacketTransportInternal* /*packet_transport*/) {
  MaybeSetupDtlsSrtp();
  MaybeUpdateWritableState();
}

}  // namespace sm::pc
