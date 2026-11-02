// TLS over a byte stream (issue #1035, C4 part 3, milestone 3): the TLS engine of libwebrtc's
// rtc_base/openssl_adapter.{h,cc} at M157 (compare/webrtc/reference/), on both sm::tls backends (BoringSSL, as
// libwebrtc builds, and Mbed TLS, C1). TURN over TLS (C4 part 4, #1036) runs it through sm::p2p's SslAdapter
// (sm/p2p/ssl_adapter.h), which moves the bytes between it and a TCP socket.
//
// As DtlsTransport does for datagrams, the caller moves the bytes: Feed() what arrived from the peer, TakeOutgoing()
// what to send. Nothing here reads the caller's clock or starts a thread. Certificate validity is checked against the
// wall clock, as libwebrtc's X509 verification does.
//
// What it does as OpenSSLAdapter (BeginSSL, ContinueSSL, SSLPostConnectionCheck, CreateContext):
// - TLS 1.2 to 1.3 (TLS_method's range in M157's BoringSSL); `max_version` caps it at 1.2 for tests.
// - The cipher list "ALL:!SHA256:!SHA384:!aPSK:!ECDSA+SHA1:!ADH:!LOW:!EXP:!MD5:!3DES", verify depth 4,
//   SSL_VERIFY_PEER on both roles, OCSP stapling and SCT requested, extensions permuted (BoringSSL).
// - SNI set to `server_name` when it isn't empty; ALPN (TransformAlpnProtocols) and the curves list
//   (SSL_set1_curves_list, joined with ':') passed through.
// - The peer's chain is checked against the trusted roots (SSLVerifyCallback); a chain that fails is accepted if the
//   custom verifier (SSLCertificateVerifier::VerifyChain) accepts it, or if `ignore_bad_cert` is set
//   (TLS_CERT_POLICY_INSECURE_NO_CHECK, OPT_TLS_INSECURE). Otherwise the handshake fails.
// - After the handshake, the post-connection check: the leaf must match `server_name` (X509_check_host, so an empty
//   name never matches) and the chain must have verified; `ignore_bad_cert` overrides a failure.
//
// Differences from libwebrtc, none behavioural on the wire:
// - Trusted roots: libwebrtc loads its built-in root list (rtc_base/ssl_roots.h, not in the reference tree); here the
//   caller gives roots (`trusted_roots`), and `load_system_roots` adds the platform's store where the backend finds
//   one (BoringSSL's default verify paths; on Mbed TLS the usual CA bundle files). libwebrtc fails the context when no
//   root loads; here a context without roots still connects under `ignore_bad_cert` or a custom verifier.
// - No session cache (OpenSSLAdapterFactory / OpenSSLSessionCache): the TURN client creates its adapters with
//   SSLAdapter::Create, which runs without one, so IsResumedSession is always false there too.
// - Mbed TLS 3.6: OCSP stapling and SCT are not requested (it has neither), extensions are not permuted, and its
//   suites are the ECDHE ones of libwebrtc's list (as its DtlsTransport, tools/catalog.py, #998).
#pragma once

#include "sm/tls/backend.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "sm/tls/ssl_identity.h"

namespace sm::tls {
inline namespace SM_TLS_BACKEND_NS {

// TLS versions on the wire.
inline constexpr int kTls12 = 0x0303;
inline constexpr int kTls13 = 0x0304;

enum class TlsRole { kClient, kServer };
enum class TlsState { kNew, kConnecting, kConnected, kFailed, kClosed };

// Results of Read and Write besides a byte count.
inline constexpr int kTlsWouldBlock = -1;  // Nothing to read yet, or not connected.
inline constexpr int kTlsError = -2;       // The connection failed (state() says so).

struct TlsConfig {
  TlsRole role = TlsRole::kClient;
  // SNI and the name the server's certificate must match (ssl_host_name_, from StartSSL).
  std::string server_name;
  std::vector<std::string> alpn_protocols;
  std::vector<std::string> elliptic_curves;
  // Certificates the peer's chain may end at, DER.
  std::vector<std::vector<uint8_t>> trusted_roots;
  bool load_system_roots = true;
  // SetIgnoreBadCert: accept any certificate (TLS_CERT_POLICY_INSECURE_NO_CHECK).
  bool ignore_bad_cert = false;
  // SSLCertificateVerifier::VerifyChain, leaf first (DER): asked only when the chain fails against the roots.
  std::function<bool(const std::vector<std::vector<uint8_t>>& chain)> cert_verifier;
  // The highest version offered (kTls13, or kTls12 to cap it). The lowest is TLS 1.2.
  int max_version = kTls13;
};

class TlsStream {
 public:
  // `identity` is the certificate this end presents (SetIdentity); a server needs one, a client may have none.
  TlsStream(TlsConfig config, std::unique_ptr<SslIdentity> identity = nullptr);
  ~TlsStream();
  TlsStream(const TlsStream&) = delete;
  TlsStream& operator=(const TlsStream&) = delete;

  // BeginSSL: sets up the context and the connection and runs the first handshake step (a client's ClientHello).
  // False if it could not be set up, or the handshake failed at once.
  bool Start();
  // Bytes from the peer; the handshake advances. Application data waits for Read.
  void Feed(std::span<const uint8_t> data);
  // Bytes to send to the peer, oldest first.
  std::vector<uint8_t> TakeOutgoing();
  bool HasOutgoing() const;

  // SSL_write: the bytes taken (all of `data` once connected), kTlsWouldBlock before the handshake completes, or
  // kTlsError.
  int Write(std::span<const uint8_t> data);
  // SSL_read: the bytes read, 0 when the peer closed (close_notify), kTlsWouldBlock when no whole record is
  // buffered, or kTlsError.
  int Read(std::span<uint8_t> buffer);

  TlsState state() const { return state_; }
  // The negotiated version (kTls12 or kTls13), or 0 before the handshake completes.
  int version() const;
  // The negotiated ALPN protocol, empty if none.
  std::string alpn_protocol() const;
  // The server name the client sent (SNI), on a server; empty if none.
  std::string received_server_name() const;
  // The peer's chain verified against the roots or the custom verifier.
  bool peer_verified() const { return chain_verified_ || custom_verified_; }
  // Why the connection failed, for logs; empty otherwise.
  const std::string& error() const { return error_; }

  // Backend plumbing; not for callers.
  struct Impl;

 private:
  void Continue();
  bool PostConnectionCheck();
  void Fail(std::string why);

  TlsConfig config_;
  std::unique_ptr<SslIdentity> identity_;
  std::unique_ptr<Impl> impl_;
  TlsState state_ = TlsState::kNew;
  bool chain_verified_ = false;
  bool custom_verified_ = false;  // custom_cert_verifier_status_
  std::string error_;
};

// TransformAlpnProtocols: each protocol prefixed with its length; empty if one is empty or longer than 255.
std::string TransformAlpnProtocols(const std::vector<std::string>& protocols);

}  // namespace SM_TLS_BACKEND_NS
}  // namespace sm::tls
