// DTLS for WebRTC: libwebrtc's rtc_base/openssl_stream_adapter.cc in DTLS mode (M157), ported for milestone 3
// (#984 part 2). It runs the handshake over datagrams the caller moves (from ICE, or the test bench's emulated
// link), checks the peer's certificate against the fingerprint from SDP, negotiates DTLS-SRTP (RFC 5764) and exports
// the SRTP keys. Time is the caller's: BoringSSL's clock is set from it (SSL_CTX_set_current_time_cb), so runs are
// deterministic on a simulated clock.
#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "sm/core/units.h"
#include "sm/tls/ssl_fingerprint.h"
#include "sm/tls/ssl_identity.h"

namespace sm::tls {

// SRTP protection profiles (RFC 5764, RFC 7714 ids; ssl_stream_adapter.h).
inline constexpr int kSrtpAes128CmSha1_80 = 0x0001;
inline constexpr int kSrtpAes128CmSha1_32 = 0x0002;
inline constexpr int kSrtpAeadAes128Gcm = 0x0007;
inline constexpr int kSrtpAeadAes256Gcm = 0x0008;

// DTLS versions on the wire (ssl_stream_adapter.h kDtls12VersionBytes / kDtls13VersionBytes).
inline constexpr int kDtls12 = 0xfefd;
inline constexpr int kDtls13 = 0xfefc;

enum class DtlsRole { kClient, kServer };
enum class DtlsState { kNew, kConnecting, kConnected, kFailed, kClosed };

struct DtlsConfig {
  // CryptoOptions::GetSupportedDtlsSrtpCryptoSuites with M157's defaults: AES-128-CM-SHA1-80, then GCM (enabled,
  // not preferred).
  std::vector<int> srtp_crypto_suites = {kSrtpAes128CmSha1_80, kSrtpAeadAes256Gcm, kSrtpAeadAes128Gcm};
  // The highest version offered: DTLS 1.3 when the backend has it (GetMaxVersion with SSL_PROTOCOL_NOT_GIVEN),
  // DTLS 1.2 to cap it. The lowest is always DTLS 1.2.
  int max_version = kDtls13;
  int mtu = 1200;                     // dtls_mtu_
  int initial_timeout_ms = 50;        // dtls_handshake_timeout_ms_
};

class DtlsTransport {
 public:
  DtlsTransport(std::unique_ptr<SslIdentity> identity, DtlsRole role, DtlsConfig config = {});
  ~DtlsTransport();
  DtlsTransport(const DtlsTransport&) = delete;
  DtlsTransport& operator=(const DtlsTransport&) = delete;

  // The peer's fingerprint, from SDP. A certificate already received is checked now (SetPeerCertificateDigest); one
  // that arrives later is checked when it does. False if the check fails (the transport then fails).
  bool SetPeerFingerprint(const SslFingerprint& fingerprint, Timestamp now);

  // Starts the handshake: the client sends its first flight. False if the backend could not be set up.
  bool Start(Timestamp now);
  // A datagram from the peer (handshake or application data).
  void OnPacket(std::span<const uint8_t> datagram, Timestamp now);
  // When the handshake's retransmission timer fires, if one is running, and its handler.
  std::optional<Timestamp> NextTimeout() const;
  void OnTimeout(Timestamp now);

  // Datagrams to send to the peer, oldest first.
  std::vector<std::vector<uint8_t>> TakeOutgoing();
  // Application data (for SCTP, C7): false before the handshake completes.
  bool Send(std::span<const uint8_t> data, Timestamp now);
  std::vector<std::vector<uint8_t>> TakeReceived();

  DtlsState state() const { return state_; }
  DtlsRole role() const { return role_; }
  // The negotiated version (kDtls12 or kDtls13), or 0 before the handshake completes.
  int version() const;
  // The negotiated SRTP profile, or 0.
  int srtp_crypto_suite() const;
  // RFC 5764 4.2: the "EXTRACTOR-dtls_srtp" keying material (client key, server key, client salt, server salt).
  bool ExportSrtpKeyingMaterial(std::span<uint8_t> out) const;
  // The peer's certificate, once received.
  const SslCertificate* peer_certificate() const { return peer_cert_ ? &*peer_cert_ : nullptr; }
  int retransmissions() const { return retransmissions_; }

  // Backend plumbing (the BIO and the callbacks); not for callers.
  struct Impl;

 private:
  void Continue(Timestamp now);
  bool VerifyPeerCertificate() const;

  std::unique_ptr<SslIdentity> identity_;
  DtlsRole role_;
  DtlsConfig config_;
  std::unique_ptr<Impl> impl_;
  DtlsState state_ = DtlsState::kNew;
  std::optional<SslFingerprint> peer_fingerprint_;
  std::optional<SslCertificate> peer_cert_;
  int retransmissions_ = 0;
};

// The SRTP key and salt lengths of a profile, in bytes (SrtpCryptoSuiteToKeyAndSaltLength); false if unknown.
bool SrtpKeyAndSaltLength(int crypto_suite, int& key_len, int& salt_len);

}  // namespace sm::tls
