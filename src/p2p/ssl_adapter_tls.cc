// sm/p2p/ssl_adapter_tls.h: OpenSSLAdapter's state machine (rtc_base/openssl_adapter.cc, M157) over
// sm::tls::TlsStream.
#include "sm/p2p/ssl_adapter_tls.h"

#include <algorithm>
#include <array>
#include <utility>

namespace sm::p2p {

TlsSslAdapter::TlsSslAdapter(std::unique_ptr<Socket> socket) : socket_(std::move(socket)) {
  socket_->SubscribeConnectEvent(this, [this](Socket*, Timestamp now) { OnConnectEvent(now); });
  socket_->SubscribeReadEvent(this, [this](Socket*, Timestamp now) { OnReadEvent(now); });
  socket_->SubscribeWriteEvent(this, [this](Socket*, Timestamp now) { OnWriteEvent(now); });
  socket_->SubscribeCloseEvent(this, [this](Socket*, int err) { NotifyCloseEvent(err); });
}

TlsSslAdapter::~TlsSslAdapter() {
  socket_->UnsubscribeAll(this);
  Cleanup();
}

int TlsSslAdapter::StartSSL(std::string_view hostname) {
  if (state_ != SslState::kNone) return -1;
  host_name_.assign(hostname);
  if (socket_->GetState() != ConnState::kConnected) {
    state_ = SslState::kWait;
    return 0;
  }
  state_ = SslState::kConnecting;
  // StartSSL takes no time (libwebrtc's signature): a socket already connected sends the first flight at the time of
  // the adapter's last call. BasicPacketSocketFactory calls StartSSL before Connect, so the handshake starts on the
  // connect event, at its time.
  if (int err = BeginSSL(last_now_)) {
    Error(err, false);
    return err;
  }
  return 0;
}

int TlsSslAdapter::BeginSSL(Timestamp now) {
  tls::TlsConfig config;
  config.role = role_ == SslRole::kClient ? tls::TlsRole::kClient : tls::TlsRole::kServer;
  config.server_name = host_name_;
  config.alpn_protocols = alpn_protocols_;
  config.elliptic_curves = elliptic_curves_;
  config.trusted_roots = trusted_roots_;
  config.load_system_roots = load_system_roots_;
  config.ignore_bad_cert = ignore_bad_cert_;
  if (cert_verifier_) {
    SslCertificateVerifier* v = cert_verifier_;
    config.cert_verifier = [v](const std::vector<std::vector<uint8_t>>& chain) { return v->VerifyChain(chain); };
  }
  tls_ = std::make_unique<tls::TlsStream>(std::move(config), identity_ ? identity_->Clone() : nullptr);
  // EarlyExitCatcher: any failure cleans up.
  if (!tls_->Start() || !FlushToSocket(now)) {
    Cleanup();
    return -1;
  }
  return ContinueSSL(now);
}

int TlsSslAdapter::ContinueSSL(Timestamp now) {
  if (!PullFromSocket(now) || !FlushToSocket(now)) {
    Cleanup();
    return -1;
  }
  switch (tls_->state()) {
    case tls::TlsState::kConnected: {
      state_ = SslState::kConnected;
      NotifyConnectEvent(now);
      // Application data that arrived with the handshake's last flight (see the header).
      SignalReadWhileBuffered(now);
      return 0;
    }
    case tls::TlsState::kConnecting:
      return 0;
    default:
      // The handshake or the post-connection check failed: make sure the socket is closed.
      Cleanup();
      return -1;
  }
}

void TlsSslAdapter::Error(int err, bool signal) {
  state_ = SslState::kError;
  SetError(err);
  if (signal) NotifyCloseEvent(err);
}

void TlsSslAdapter::Cleanup() {
  state_ = SslState::kNone;
  tls_.reset();
  wire_out_.clear();
  plain_.clear();
}

bool TlsSslAdapter::PullFromSocket(Timestamp now) {
  std::array<uint8_t, 4096> buf;
  for (;;) {
    const int n = socket_->Recv(now, buf);
    if (n > 0) {
      tls_->Feed(std::span<const uint8_t>(buf.data(), static_cast<size_t>(n)));
      continue;
    }
    // 0: the peer closed the connection; the engine sees no more bytes. -1: blocked, or an error.
    return n == 0 || socket_->IsBlocking();
  }
}

bool TlsSslAdapter::FlushToSocket(Timestamp now) {
  if (tls_ && tls_->HasOutgoing()) {
    const std::vector<uint8_t> out = tls_->TakeOutgoing();
    wire_out_.insert(wire_out_.end(), out.begin(), out.end());
  }
  size_t sent = 0;
  bool ok = true;
  while (sent < wire_out_.size()) {
    const int n = socket_->Send(now, std::span<const uint8_t>(wire_out_).subspan(sent));
    if (n > 0) {
      sent += static_cast<size_t>(n);
      continue;
    }
    ok = socket_->IsBlocking();
    break;
  }
  wire_out_.erase(wire_out_.begin(), wire_out_.begin() + static_cast<long>(sent));
  return ok;
}

int TlsSslAdapter::Send(Timestamp now, std::span<const uint8_t> data) {
  last_now_ = now;
  switch (state_) {
    case SslState::kNone:
      return socket_->Send(now, data);
    case SslState::kWait:
    case SslState::kConnecting:
      SetError(kSocketENotConn);
      return -1;
    case SslState::kConnected:
      break;
    case SslState::kError:
      return -1;
  }
  // Data from an earlier Send is still waiting: nothing more until it has gone.
  if (!wire_out_.empty()) {
    if (!FlushToSocket(now)) {
      Error(-1, false);
      return -1;
    }
    if (!wire_out_.empty()) {
      SetError(kSocketEWouldBlock);
      return -1;
    }
  }
  if (data.empty()) return 0;
  if (tls_->Write(data) < 0) {
    Error(-1, false);  // Error("SSL_write", -1, false).
    return -1;
  }
  if (!FlushToSocket(now)) {
    Error(-1, false);
    return -1;
  }
  // Whatever the socket didn't take waits for its write event; the caller may consider it all sent.
  return static_cast<int>(data.size());
}

int TlsSslAdapter::Recv(Timestamp now, std::span<uint8_t> buffer) {
  last_now_ = now;
  switch (state_) {
    case SslState::kNone:
      return socket_->Recv(now, buffer);
    case SslState::kWait:
    case SslState::kConnecting:
      SetError(kSocketENotConn);
      return -1;
    case SslState::kConnected:
      break;
    case SslState::kError:
      return -1;
  }
  if (buffer.empty()) return 0;
  if (!plain_.empty()) {
    const size_t n = std::min(buffer.size(), plain_.size());
    std::copy_n(plain_.begin(), n, buffer.begin());
    plain_.erase(plain_.begin(), plain_.begin() + static_cast<long>(n));
    return static_cast<int>(n);
  }
  if (!PullFromSocket(now)) {
    Error(-1, false);
    return -1;
  }
  const int code = tls_->Read(buffer);
  FlushToSocket(now);  // Anything the engine answered (a TLS 1.3 key update); a failure shows on the next call.
  if (code > 0) return code;
  if (code == tls::kTlsError) {
    Error(-1, false);  // Error("SSL_read", -1, false).
    return -1;
  }
  // Nothing whole yet, or close_notify (SSL_ERROR_ZERO_RETURN): EWOULDBLOCK either way.
  SetError(kSocketEWouldBlock);
  return -1;
}

int TlsSslAdapter::Close() {
  Cleanup();
  state_ = SslState::kNone;
  return socket_->Close();
}

Socket::ConnState TlsSslAdapter::GetState() const {
  ConnState state = socket_->GetState();
  if (state == ConnState::kConnected && (state_ == SslState::kWait || state_ == SslState::kConnecting)) {
    state = ConnState::kConnecting;
  }
  return state;
}

void TlsSslAdapter::OnConnectEvent(Timestamp now) {
  last_now_ = now;
  if (state_ != SslState::kWait) {
    NotifyConnectEvent(now);
    return;
  }
  state_ = SslState::kConnecting;
  if (int err = BeginSSL(now)) NotifyCloseEvent(err);
}

void TlsSslAdapter::OnReadEvent(Timestamp now) {
  last_now_ = now;
  if (state_ == SslState::kNone) {
    NotifyReadEvent(now);
    return;
  }
  if (state_ == SslState::kConnecting) {
    if (int err = ContinueSSL(now)) Error(err, true);
    return;
  }
  if (state_ != SslState::kConnected) return;
  NotifyReadEvent(now);
  SignalReadWhileBuffered(now);
}

bool TlsSslAdapter::BufferPlaintext() {
  if (plain_.empty() && tls_) {
    std::array<uint8_t, 16384> buf;  // One TLS record's worth.
    const int n = tls_->Read(buf);
    if (n > 0) plain_.assign(buf.begin(), buf.begin() + n);
  }
  return !plain_.empty();
}

void TlsSslAdapter::SignalReadWhileBuffered(Timestamp now) {
  // A reader that stops after one Recv leaves decrypted data here that no socket event will announce: signal again
  // while some is left, as a socket does while bytes are buffered (VirtualSocket's PostSignalReadEvent). Stop when
  // a read event takes nothing, so a reader that doesn't read can't loop.
  while (state_ == SslState::kConnected && BufferPlaintext()) {
    const size_t before = plain_.size();
    NotifyReadEvent(now);
    if (state_ == SslState::kConnected && plain_.size() == before) break;
  }
}

void TlsSslAdapter::OnWriteEvent(Timestamp now) {
  last_now_ = now;
  if (state_ == SslState::kNone) {
    NotifyWriteEvent(now);
    return;
  }
  if (state_ == SslState::kConnecting) {
    if (int err = ContinueSSL(now)) Error(err, true);
    return;
  }
  if (state_ != SslState::kConnected) return;
  // A previous Send's record the socket couldn't take: finish it.
  if (!wire_out_.empty() && !FlushToSocket(now)) {
    Error(-1, true);
    return;
  }
  NotifyWriteEvent(now);
}

namespace {
std::unique_ptr<SslAdapter> CreateTlsSslAdapter(std::unique_ptr<Socket> socket) {
  return std::make_unique<TlsSslAdapter>(std::move(socket));
}
}  // namespace

void InstallSslAdapterFromTls() { SetSslAdapterFactory(&CreateTlsSslAdapter); }

}  // namespace sm::p2p
