// The TLS client socket TURN over TLS stands on (issue #1035, C4 part 3, milestone 3).
//
// libwebrtc's rtc_base/ssl_adapter.h at M157 (compare/webrtc/reference/): `SSLAdapter`, a Socket that wraps a stream
// socket and runs TLS over it, created by `SSLAdapter::Create` and configured before `StartSSL`; and the
// `SSLCertificateVerifier` hook (rtc_base/ssl_certificate.h) that PacketSocketTcpOptions carries. sm::p2p has no TLS
// library, so `Create` uses the implementation installed with SetSslAdapterFactory; sm::p2p_tls installs the one on
// sm::tls with InstallSslAdapterFromTls() (sm/p2p/ssl_adapter_tls.h), as InstallStunCryptoFromTls() does for STUN's
// crypto. Without one, Create returns nullptr, and BasicPacketSocketFactory's TLS sockets fail as libwebrtc's do when
// SSLAdapter::Create fails.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "sm/p2p/socket.h"

namespace sm::p2p {

// SSLCertificateVerifier: decides on a chain that failed against the trusted roots.
class SslCertificateVerifier {
 public:
  virtual ~SslCertificateVerifier() = default;
  // The peer's chain, leaf first, each certificate DER. True to accept it.
  virtual bool VerifyChain(const std::vector<std::vector<uint8_t>>& chain) = 0;
};

// SSLRole.
enum class SslRole { kClient, kServer };

class SslAdapter : public Socket {
 public:
  // For tests only, as libwebrtc says: accept any certificate (OPT_TLS_INSECURE, TLS_CERT_POLICY_INSECURE_NO_CHECK).
  virtual void SetIgnoreBadCert(bool ignore) = 0;
  virtual void SetAlpnProtocols(const std::vector<std::string>& protocols) = 0;
  virtual void SetEllipticCurves(const std::vector<std::string>& curves) = 0;
  // Not owned; must outlive the adapter.
  virtual void SetCertVerifier(SslCertificateVerifier* verifier) = 0;
  virtual void SetRole(SslRole role) = 0;
  // Roots the server's chain may end at (DER), besides the system's. libwebrtc builds its roots in
  // (rtc_base/ssl_roots.h); the library takes them from the caller.
  virtual void AddTrustedRoot(std::vector<uint8_t> der) = 0;
  // Returns 0 on success. Called while the socket is closed or connecting, the handshake starts once it connects.
  // `hostname` is the SNI and the name the certificate must match.
  virtual int StartSSL(std::string_view hostname) = 0;
  // Always false: no session cache (see sm/tls/tls_stream.h).
  virtual bool IsResumedSession() = 0;

  // SSLAdapter::Create: wraps `socket`, or returns nullptr (and drops `socket`) when no implementation is installed
  // or it can't be created.
  static std::unique_ptr<SslAdapter> Create(std::unique_ptr<Socket> socket);

  // Not supported (RTC_CHECK(false) in libwebrtc); fail with EINVAL here.
  int Listen(int) override {
    SetError(kSocketEInval);
    return -1;
  }
  std::unique_ptr<Socket> Accept(Timestamp, SocketAddress*) override {
    SetError(kSocketEInval);
    return nullptr;
  }
};

// What SslAdapter::Create calls. Install once at startup, before any TLS socket is created (not thread-safe, like
// SetStunCrypto); nullptr removes it.
using SslAdapterFactoryFn = std::unique_ptr<SslAdapter> (*)(std::unique_ptr<Socket> socket);
void SetSslAdapterFactory(SslAdapterFactoryFn factory);

}  // namespace sm::p2p
