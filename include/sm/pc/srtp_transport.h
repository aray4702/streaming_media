// SRTP over an RTP transport: libwebrtc's pc/srtp_transport.{h,cc} at M157 (compare/webrtc/reference/), ported for
// DTLS-SRTP (C5 part 3, #1007) on sm::tls::SrtpSession (#995).
//
// What it does, as libwebrtc does:
// - It is active once SetRtpParams has created the send and receive sessions; until then nothing is sent and every
//   received packet is dropped. RTP goes through the RTP sessions, and RTCP too unless SetRtcpParams made separate
//   RTCP sessions (non-muxed RTCP runs its own DTLS handshake, so its own keys).
// - SetRtpParams again updates the existing sessions' keys (srtp_update), as a DTLS restart's re-keying does; a
//   failure resets every session. SetRtcpParams works once.
// - Replay protection, the auth tag and the crypto suites are SrtpSession's (libwebrtc's policy). Encrypted header
//   extensions (RFC 6904) are used only for the ids the caller passes, none by default; cryptex is off by default.
// - Writable is SRTP active and the packet transports writable; the writable signal fires on a change.
// - Unprotect failures are counted (and logged every 100th, as libwebrtc throttles them).
//
// Differences from libwebrtc, none of them on the wire: those in sm/pc/rtp_transport.h, and libwebrtc's two field
// trials are an options struct, both off by default as in M157 (experiments/field_trials.py):
// WebRTC-SrtpRemoveReceiveStream (RemoveReceiveStream below) and WebRTC-UpdateNetworkRouteOnSrtpActivation (left
// out with the network route).
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "sm/pc/rtp_transport.h"
#include "sm/tls/srtp_session.h"

namespace sm::pc {

struct SrtpTransportOptions {
  // WebRTC-SrtpRemoveReceiveStream: unregistering a demuxer sink also removes its signalled SSRCs from the
  // receive session (replay window and rollover counter).
  bool remove_receive_stream = false;
};

class SrtpTransport : public RtpTransport {
 public:
  explicit SrtpTransport(bool rtcp_mux_enabled, SrtpTransportOptions options = {});
  ~SrtpTransport() override;

  // Protects the packet, then sends it; false (nothing sent) while SRTP is inactive or if protection fails.
  bool SendRtpPacket(std::vector<uint8_t>& packet, const p2p::PacketOptions& options, int flags,
                     Timestamp now) override;
  bool SendRtcpPacket(std::vector<uint8_t>& packet, const p2p::PacketOptions& options, int flags,
                      Timestamp now) override;

  // Active once the send and receive sessions exist.
  bool IsSrtpActive() const override;
  bool IsWritable(bool rtcp) const override;

  // Enables or disables cryptex (RFC 9335) on the existing sessions and the ones made later.
  bool UseCryptex(bool enable, bool require);

  // Creates the RTP send/receive sessions with the negotiated suite and key (master key then salt) and the
  // encrypted header extension ids, or updates their keys if they exist (from SDES, or the DTLS export).
  bool SetRtpParams(int send_crypto_suite, const std::vector<uint8_t>& send_key,
                    const std::vector<int>& send_extension_ids, int recv_crypto_suite,
                    const std::vector<uint8_t>& recv_key, const std::vector<int>& recv_extension_ids);
  // Creates separate RTCP sessions (non-muxed RTCP). Only once.
  bool SetRtcpParams(int send_crypto_suite, const std::vector<uint8_t>& send_key,
                     const std::vector<int>& send_extension_ids, int recv_crypto_suite,
                     const std::vector<uint8_t>& recv_key, const std::vector<int>& recv_extension_ids);
  void ResetParams();

  // The bytes protection adds to an RTP packet (the auth tag); false while inactive.
  bool GetSrtpOverhead(int* srtp_overhead) const;

  // Also removes the sink's SSRCs from the receive session with remove_receive_stream.
  bool UnregisterRtpDemuxerSink(RtpPacketSinkInterface* sink) override;

  // Received packets that failed to unprotect.
  int rtp_decryption_failures() const { return decryption_failure_count_; }
  int rtcp_decryption_failures() const { return rtcp_decryption_failure_count_; }

 protected:
  // If the writable state changed, fires the writable signal.
  void MaybeUpdateWritableState();

  void OnRtpPacketReceived(const p2p::ReceivedPacket& packet) override;
  void OnRtcpPacketReceived(const p2p::ReceivedPacket& packet) override;
  void OnWritableState(p2p::PacketTransportInternal* packet_transport) override;

 private:
  void CreateSrtpSessions();
  bool ProtectRtp(std::vector<uint8_t>& buffer);
  bool ProtectRtcp(std::vector<uint8_t>& buffer);
  bool UnprotectRtp(std::vector<uint8_t>& buffer);
  bool UnprotectRtcp(std::vector<uint8_t>& buffer);

  const SrtpTransportOptions options_;
  std::unique_ptr<tls::SrtpSession> send_session_;
  std::unique_ptr<tls::SrtpSession> recv_session_;
  // Non-muxed RTCP needs its own sessions: it has its own DTLS handshake.
  std::unique_ptr<tls::SrtpSession> send_rtcp_session_;
  std::unique_ptr<tls::SrtpSession> recv_rtcp_session_;

  bool writable_ = false;
  int decryption_failure_count_ = 0;
  int rtcp_decryption_failure_count_ = 0;
  bool enable_cryptex_ = false;
  bool require_cryptex_ = false;
};

}  // namespace sm::pc
