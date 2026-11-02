// DTLS-SRTP: libwebrtc's pc/dtls_srtp_transport.{h,cc} at M157 (compare/webrtc/reference/), ported for C5 part 3
// (#1007) on the DTLS transport of #1006 (sm/p2p/dtls_transport.h).
//
// What it does, as libwebrtc does:
// - It sends over the DTLS transports (RTP, and RTCP without rtcp-mux) and watches their DTLS state. When every
//   transport in use is writable and its handshake has finished, it exports the RFC 5764 keying material for the
//   negotiated SRTP profile (client key | server key | client salt | server salt) and sets up SRTP: the server
//   sends with the server halves and receives with the client halves, the client the other way round. Non-muxed
//   RTCP gets its own keys from its own handshake.
// - Any DTLS state other than connected resets SRTP (a DTLS restart, a failure, a transport change); when the new
//   handshake connects, SRTP is set up again with the new keys.
// - Enabling rtcp-mux sets SRTP up from the RTP transport alone; removing the RTCP transport while muxing keeps
//   the SRTP context (its rollover counter and SRTCP index; bugs.webrtc.org/8996).
// - The encrypted header extension ids (RFC 6904) are empty unless set; changing them after the handshake re-keys
//   the RTP sessions with them.
// - The crypto suites offered, in libwebrtc's default order, are the DTLS transport's (sm::tls::DtlsConfig).
//
// Differences from libwebrtc: those in sm/pc/srtp_transport.h and sm/pc/rtp_transport.h; the DTLS transport is the
// library's one class (libwebrtc's DtlsTransportInternal interface).
#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "sm/p2p/dtls_transport.h"
#include "sm/pc/srtp_transport.h"

namespace sm::pc {

class DtlsSrtpTransport : public SrtpTransport {
 public:
  explicit DtlsSrtpTransport(bool rtcp_mux_enabled, SrtpTransportOptions options = {});
  ~DtlsSrtpTransport() override;

  // The RTP and RTCP DTLS transports; `rtcp_dtls` is null with rtcp-mux. They must outlive this transport (or the
  // next call that replaces them).
  void SetDtlsTransports(p2p::DtlsTransport* rtp_dtls, p2p::DtlsTransport* rtcp_dtls);
  void SetDtlsTransportsOwned(std::unique_ptr<p2p::DtlsTransport> rtp_dtls,
                              std::unique_ptr<p2p::DtlsTransport> rtcp_dtls);

  void SetRtcpMuxEnabled(bool enable) override;

  // The header extension ids to encrypt.
  void UpdateSendEncryptedHeaderExtensionIds(const std::vector<int>& send_extension_ids);
  void UpdateRecvEncryptedHeaderExtensionIds(const std::vector<int>& recv_extension_ids);

  // Called on every DTLS state change of either transport.
  void SetOnDtlsStateChange(std::function<void()> callback) { on_dtls_state_change_ = std::move(callback); }

  p2p::DtlsTransport* rtp_dtls_transport() const { return static_cast<p2p::DtlsTransport*>(rtp_packet_transport()); }
  p2p::DtlsTransport* rtcp_dtls_transport() const {
    return static_cast<p2p::DtlsTransport*>(rtcp_packet_transport());
  }

 private:
  bool IsDtlsActive() const;
  bool IsDtlsConnected() const;
  bool IsDtlsWritable() const;
  bool DtlsHandshakeCompleted() const;
  void MaybeSetupDtlsSrtp();
  void SetupRtpDtlsSrtp();
  void SetupRtcpDtlsSrtp();
  bool ExtractParams(p2p::DtlsTransport* dtls_transport, int* selected_crypto_suite, std::vector<uint8_t>* send_key,
                     std::vector<uint8_t>* recv_key);
  void SetupDtlsTransport(p2p::DtlsTransport* dtls_transport, bool is_rtcp);
  // Unsubscribes from the old transport if it changed; true if it changed.
  bool MaybeUnsubscribe(p2p::DtlsTransport* old_transport, p2p::DtlsTransport* new_transport);
  void OnDtlsState(p2p::DtlsTransport* dtls_transport, p2p::DtlsTransportState state);
  void OnWritableState(p2p::PacketTransportInternal* packet_transport) override;

  std::optional<std::vector<int>> send_extension_ids_;
  std::optional<std::vector<int>> recv_extension_ids_;
  std::function<void()> on_dtls_state_change_;
};

}  // namespace sm::pc
