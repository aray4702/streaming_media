// One DTLS endpoint of the BoringSSL <-> mbedTLS cross test (#998), behind an interface that names no sm::tls type,
// so a test can hold one endpoint of each backend. cross_peer.cc is compiled once against each backend's sm::tls
// build; each copy defines MakePeer in tls_cross::<backend's inline namespace>.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sm/core/units.h"

namespace tls_cross {

class Peer {
 public:
  virtual ~Peer() = default;
  // This endpoint's certificate fingerprint, as SDP carries it: algorithm and digest.
  virtual std::string fingerprint_algorithm() const = 0;
  virtual std::vector<uint8_t> fingerprint_digest() const = 0;
  virtual bool SetPeerFingerprint(const std::string& algorithm, const std::vector<uint8_t>& digest, sm::Timestamp now) = 0;
  virtual bool Start(sm::Timestamp now) = 0;
  virtual void OnPacket(const std::vector<uint8_t>& datagram, sm::Timestamp now) = 0;
  virtual std::optional<sm::Timestamp> NextTimeout() const = 0;
  virtual void OnTimeout(sm::Timestamp now) = 0;
  virtual std::vector<std::vector<uint8_t>> TakeOutgoing() = 0;
  virtual bool Send(const std::vector<uint8_t>& data, sm::Timestamp now) = 0;
  virtual std::vector<std::vector<uint8_t>> TakeReceived() = 0;
  virtual bool connected() const = 0;
  virtual bool failed() const = 0;
  virtual int version() const = 0;
  virtual int srtp_crypto_suite() const = 0;
  // The TLS cipher suite: IANA id and standard name (#1059).
  virtual int ssl_cipher_suite() const = 0;
  virtual std::string ssl_cipher_suite_name() const = 0;
  // The RFC 5764 keying material for the negotiated profile (empty before the handshake completes).
  virtual std::vector<uint8_t> SrtpKeyingMaterial() const = 0;
  virtual int retransmissions() const = 0;
  virtual std::string backend() const = 0;
};

// One TLS endpoint (TlsStream, #1035) behind the same kind of interface. A server makes an identity for `name`; a
// client checks the server's certificate for `name`, trusting the roots given (DER).
class TlsPeer {
 public:
  virtual ~TlsPeer() = default;
  virtual std::vector<uint8_t> certificate() const = 0;  // A server's, DER.
  virtual bool Start() = 0;
  virtual void Feed(const std::vector<uint8_t>& data) = 0;
  virtual std::vector<uint8_t> TakeOutgoing() = 0;
  virtual int Write(const std::string& data) = 0;
  virtual std::string ReadAll() = 0;
  virtual bool connected() const = 0;
  virtual bool failed() const = 0;
  virtual int version() const = 0;
  virtual std::string alpn_protocol() const = 0;
  virtual std::string received_server_name() const = 0;
};

struct TlsPeerOptions {
  bool server = false;
  std::string name = "turn.example.org";
  std::vector<std::vector<uint8_t>> trusted_roots;
  std::vector<std::string> alpn_protocols;
  int max_version = 0x0304;  // sm::tls::kTls13
};

// libwebrtc M157's defaults (DtlsConfig{}), unless `srtp_crypto_suites` is given.
namespace boringssl_backend {
std::unique_ptr<Peer> MakePeer(bool server, std::optional<std::vector<int>> srtp_crypto_suites = std::nullopt);
std::unique_ptr<TlsPeer> MakeTlsPeer(const TlsPeerOptions& options);
}
namespace mbedtls_backend {
std::unique_ptr<Peer> MakePeer(bool server, std::optional<std::vector<int>> srtp_crypto_suites = std::nullopt);
std::unique_ptr<TlsPeer> MakeTlsPeer(const TlsPeerOptions& options);
}

}  // namespace tls_cross
