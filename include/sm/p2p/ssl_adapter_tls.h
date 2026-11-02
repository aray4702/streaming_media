// SslAdapter on sm::tls (issue #1035, C4 part 3): libwebrtc's rtc_base/openssl_adapter.{h,cc} at M157, the adapter
// half: the state machine (SSL_NONE, SSL_WAIT, SSL_CONNECTING, SSL_CONNECTED, SSL_ERROR), Send and Recv over the
// TLS connection, and the socket events it passes up. The TLS engine is sm::tls::TlsStream (sm/tls/tls_stream.h),
// on BoringSSL or Mbed TLS. In the sm::p2p_tls library, built when SM_WITH_TLS is on.
//
// OpenSSLAdapter's BIO reads and writes the wrapped socket directly; here the adapter moves the bytes. The wrapped
// socket's bytes are drained into the engine on each read event and on Recv, and the engine's output goes to the
// socket at once, the rest waiting for its write event. That keeps libwebrtc's behaviour at the interface:
// - Send before the handshake completes fails with ENOTCONN. Once connected it takes the whole buffer; if the
//   socket could not take all of the encrypted record, the rest waits (pending_data_), and the next Send fails
//   with EWOULDBLOCK until a write event has flushed it.
// - Recv fails with EWOULDBLOCK when no whole record is buffered, and when the peer sent close_notify.
// - The connect event goes up when the handshake and the post-connection check pass; a failure raises the close
//   event with the error (Error("ContinueSSL", err)).
// One addition: the adapter pulls everything the socket has into the engine, so bytes that would still sit in the
// socket's buffer under libwebrtc's BIO (which reads one record at a time) sit here instead. A socket signals a read
// again while bytes are buffered (VirtualSocket::RecvFrom's PostSignalReadEvent, "to behave like a real socket"),
// so the adapter signals a read again while it holds decrypted data the reader left, including data that came in
// the same flight as the handshake's end (announced after the connect event).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "sm/p2p/ssl_adapter.h"
#include "sm/tls/ssl_identity.h"
#include "sm/tls/tls_stream.h"

namespace sm::p2p {

class TlsSslAdapter final : public SslAdapter {
 public:
  explicit TlsSslAdapter(std::unique_ptr<Socket> socket);
  ~TlsSslAdapter() override;

  // SslAdapter.
  void SetIgnoreBadCert(bool ignore) override { ignore_bad_cert_ = ignore; }
  void SetAlpnProtocols(const std::vector<std::string>& protocols) override { alpn_protocols_ = protocols; }
  void SetEllipticCurves(const std::vector<std::string>& curves) override { elliptic_curves_ = curves; }
  void SetCertVerifier(SslCertificateVerifier* verifier) override { cert_verifier_ = verifier; }
  void SetRole(SslRole role) override { role_ = role; }
  void AddTrustedRoot(std::vector<uint8_t> der) override { trusted_roots_.push_back(std::move(der)); }
  int StartSSL(std::string_view hostname) override;
  bool IsResumedSession() override { return false; }
  // SetIdentity: the certificate this end presents (a server needs one).
  void SetIdentity(std::unique_ptr<tls::SslIdentity> identity) { identity_ = std::move(identity); }
  // Whether to add the system's roots to the trusted ones (default true).
  void SetLoadSystemRoots(bool load) { load_system_roots_ = load; }

  // The engine, once the handshake has begun: version, ALPN, SNI; nullptr before.
  const tls::TlsStream* tls() const { return tls_.get(); }

  // Socket (AsyncSocketAdapter's forwarding, with OpenSSLAdapter's overrides).
  SocketAddress GetLocalAddress() const override { return socket_->GetLocalAddress(); }
  SocketAddress GetRemoteAddress() const override { return socket_->GetRemoteAddress(); }
  int Bind(const SocketAddress& addr) override { return socket_->Bind(addr); }
  int Connect(Timestamp now, const SocketAddress& addr) override {
    last_now_ = now;
    return socket_->Connect(now, addr);
  }
  int Send(Timestamp now, std::span<const uint8_t> data) override;
  int Recv(Timestamp now, std::span<uint8_t> buffer) override;
  int Close() override;
  int GetError() const override { return socket_->GetError(); }
  void SetError(int error) override { socket_->SetError(error); }
  ConnState GetState() const override;
  int GetOption(SocketOption opt, int* value) override { return socket_->GetOption(opt, value); }
  int SetOption(SocketOption opt, int value) override { return socket_->SetOption(opt, value); }

 private:
  enum class SslState { kNone, kWait, kConnecting, kConnected, kError };

  int BeginSSL(Timestamp now);
  int ContinueSSL(Timestamp now);
  void Error(int err, bool signal);
  void Cleanup();
  // Moves what the wrapped socket has into the engine; false on a socket error.
  bool PullFromSocket(Timestamp now);
  // Sends the engine's output and what waits from before; false on a socket error (blocking is not one).
  bool FlushToSocket(Timestamp now);
  // Decrypts the next record into plain_ if it is empty; true if plain_ holds data.
  bool BufferPlaintext();
  void SignalReadWhileBuffered(Timestamp now);

  void OnConnectEvent(Timestamp now);
  void OnReadEvent(Timestamp now);
  void OnWriteEvent(Timestamp now);

  std::unique_ptr<Socket> socket_;
  SslState state_ = SslState::kNone;
  SslRole role_ = SslRole::kClient;
  std::string host_name_;
  bool ignore_bad_cert_ = false;
  bool load_system_roots_ = true;
  std::vector<std::string> alpn_protocols_;
  std::vector<std::string> elliptic_curves_;
  std::vector<std::vector<uint8_t>> trusted_roots_;
  SslCertificateVerifier* cert_verifier_ = nullptr;
  std::unique_ptr<tls::SslIdentity> identity_;
  std::unique_ptr<tls::TlsStream> tls_;
  std::vector<uint8_t> wire_out_;  // Encrypted bytes the socket has not taken yet.
  Timestamp last_now_ = Timestamp::Micros(0);  // The time of the last call that carried one.
  std::vector<uint8_t> plain_;     // Decrypted data read ahead of the reader (see the file comment).
};

// Installs TlsSslAdapter with SetSslAdapterFactory(). Call once at startup.
void InstallSslAdapterFromTls();

}  // namespace sm::p2p
