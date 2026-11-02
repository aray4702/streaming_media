// SRTP for DTLS-SRTP: libwebrtc's pc/srtp_session.{h,cc} (M157) on libsrtp 2.8.0, ported for milestone 3 (C5
// part 1, #995). One SrtpSession protects what this end sends (SetSend) or unprotects what it receives (SetReceive),
// never both, as in libwebrtc. libsrtp is initialized on the first session that sets a key and shut down when the
// last one goes, as libwebrtc's LibSrtpInitializer does.
//
// libwebrtc's policy, kept as is (SrtpSession::DoSetKey): the crypto suite's libsrtp profile for RTP and RTCP, a
// replay window of 1024 packets, retransmission of an already protected index allowed (allow_repeat_tx), the
// encrypted header extension ids (RFC 6904) unless cryptex (RFC 9335) is on, and no MKI.
//
// Differences from libwebrtc, none of them in what goes on the wire:
// - Buffers are a span with its capacity and the used length (or a std::vector that grows), not a
//   CopyOnWriteBuffer; the span form keeps libwebrtc's capacity check.
// - libwebrtc's UMA histograms (WebRTC.PeerConnection.SrtpUnprotectError, SrtcpUnprotectError) are counters and the
//   last libsrtp error code here; its throttled log lines and the RTP dump field trial are left out.
// - No SequenceChecker: like every module in the library, a session is used from one thread at a time. The libsrtp
//   init count is process-wide and takes a lock, as libwebrtc's does.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

struct srtp_ctx_t_;  // libsrtp's session; the trailing _ is libsrtp's.

namespace sm::tls {

class DtlsTransport;

// libsrtp status codes the tests and callers look at (srtp_err_status_t).
inline constexpr int kSrtpErrOk = 0;
inline constexpr int kSrtpErrBadParam = 2;
inline constexpr int kSrtpErrAuthFail = 7;
inline constexpr int kSrtpErrReplayFail = 9;
inline constexpr int kSrtpErrCantCheck = 14;
inline constexpr int kSrtpErrCryptex = 28;

// Prohibits this library from initializing libsrtp, when another library in the process does (libwebrtc's
// ProhibitLibsrtpInitialization). Call it before the first session sets a key.
void ProhibitLibsrtpInitialization();

class SrtpSession {
 public:
  SrtpSession() = default;
  ~SrtpSession();
  SrtpSession(const SrtpSession&) = delete;
  SrtpSession& operator=(const SrtpSession&) = delete;

  // Enables or disables cryptex (RFC 9335); may update an existing session.
  bool UseCryptex(bool enabled, bool require, bool sending_session);

  // The crypto suite (sm::tls::kSrtp*) and the master key followed by the master salt. False if the suite is
  // unknown, the key has the wrong length, or (Set*) the session already has a key.
  bool SetSend(int crypto_suite, std::span<const uint8_t> key, const std::vector<int>& extension_ids = {});
  bool UpdateSend(int crypto_suite, std::span<const uint8_t> key, const std::vector<int>& extension_ids = {});
  bool SetReceive(int crypto_suite, std::span<const uint8_t> key, const std::vector<int>& extension_ids = {});
  bool UpdateReceive(int crypto_suite, std::span<const uint8_t> key, const std::vector<int>& extension_ids = {});

  // Protects the packet in buffer[0, len) in place; `buffer` is the whole space available. False (and nothing
  // changed) if the space can't take the auth tag (and, for RTCP, the 4-byte SRTCP index) or libsrtp refuses it.
  // On success `len` is the protected length. `index`, if given, gets the packet's SRTP index as libwebrtc
  // reports it (the extended sequence number shifted left 16 bits, in network byte order).
  bool ProtectRtp(std::span<uint8_t> buffer, size_t& len, int64_t* index = nullptr);
  bool ProtectRtcp(std::span<uint8_t> buffer, size_t& len);
  // Verifies and decrypts buffer[0, len) in place; on success `len` is the plain length.
  bool UnprotectRtp(std::span<uint8_t> buffer, size_t& len);
  bool UnprotectRtcp(std::span<uint8_t> buffer, size_t& len);

  // The same on a std::vector, which grows to take the trailer.
  bool ProtectRtp(std::vector<uint8_t>& packet, int64_t* index = nullptr);
  bool ProtectRtcp(std::vector<uint8_t>& packet);
  bool UnprotectRtp(std::vector<uint8_t>& packet);
  bool UnprotectRtcp(std::vector<uint8_t>& packet);

  // The RTP auth tag length in bytes: what protecting adds to an RTP packet (GetSrtpOverhead).
  int GetSrtpOverhead() const { return rtp_auth_tag_len_; }

  // Removes a received SSRC's state (replay window, rollover counter). Only for SSRCs that are received: reusing a
  // sent SSRC with the same key breaks SRTP (RFC 3711 section 8).
  bool RemoveSsrcFromSession(uint32_t ssrc);

  // Unprotect failures so far, and the libsrtp status of the last one (libwebrtc's histograms).
  int rtp_unprotect_failures() const { return decryption_failure_count_; }
  int rtcp_unprotect_failures() const { return rtcp_decryption_failure_count_; }
  int last_rtp_unprotect_error() const { return last_rtp_error_; }
  int last_rtcp_unprotect_error() const { return last_rtcp_error_; }

 private:
  bool DoSetKey(int type, int crypto_suite, std::span<const uint8_t> key, const std::vector<int>& extension_ids);
  bool SetKey(int type, int crypto_suite, std::span<const uint8_t> key, const std::vector<int>& extension_ids);
  bool UpdateKey(int type, int crypto_suite, std::span<const uint8_t> key, const std::vector<int>& extension_ids);
  bool GetSendStreamPacketIndex(std::span<const uint8_t> packet, int64_t* index);

  srtp_ctx_t_* session_ = nullptr;
  // The auth tag lengths for RTP and RTCP; equal except for AES-CM-128-SHA1-32 (RTP 4, RTCP 10). The 4-byte SRTCP
  // index is not included.
  int rtp_auth_tag_len_ = 0;
  int rtcp_auth_tag_len_ = 0;
  bool inited_ = false;
  int last_send_seq_num_ = -1;
  int decryption_failure_count_ = 0;
  int rtcp_decryption_failure_count_ = 0;
  int last_rtp_error_ = kSrtpErrOk;
  int last_rtcp_error_ = kSrtpErrOk;
  bool use_cryptex_ = false;
  bool require_cryptex_ = false;
};

// The SRTP keys for one end of a DTLS-SRTP association: libwebrtc's DtlsSrtpTransport::ExtractParams. The RFC 5764
// keying material (client key | server key | client salt | server salt) is split into key+salt per direction, and
// the DTLS role picks which one this end sends with. The keys are zeroed when the object goes (ZeroOnFreeBuffer).
struct SrtpKeys {
  int crypto_suite = 0;
  std::vector<uint8_t> send_key;  // master key followed by master salt
  std::vector<uint8_t> recv_key;
  SrtpKeys() = default;
  SrtpKeys(const SrtpKeys&) = delete;
  SrtpKeys& operator=(const SrtpKeys&) = delete;
  ~SrtpKeys();
};
// False if the transport isn't connected, no SRTP profile was negotiated, or the export fails.
bool ExtractSrtpKeys(const DtlsTransport& dtls, SrtpKeys& keys);

}  // namespace sm::tls
