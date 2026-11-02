// libwebrtc's rtc_base/openssl_stream_adapter.cc in DTLS mode (M157), on BoringSSL (#984 part 2). The stream
// adapter's BIO over a StreamInterface becomes a BIO over two datagram queues; its task-queue retransmission timer
// becomes NextTimeout/OnTimeout on the caller's clock.
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pool.h>
#include <openssl/ssl.h>

#include <cstring>

#include "sm/tls/dtls_transport.h"

namespace sm::tls {

namespace {
constexpr char kDtlsSrtpExporterLabel[] = "EXTRACTOR-dtls_srtp";

// The transport calling into BoringSSL on this thread: its clock is the one BoringSSL reads. BoringSSL asks for the
// time with a null SSL* too (context-level checks), so the callback can't rely on SSL_get_app_data alone.
thread_local const Timestamp* t_now = nullptr;
struct ClockScope {
  explicit ClockScope(const Timestamp* now) : prev(t_now) { t_now = now; }
  ~ClockScope() { t_now = prev; }
  const Timestamp* prev;
};

const char* SrtpProfileName(int suite) {
  switch (suite) {  // kSrtpCipherMap
    case kSrtpAes128CmSha1_80: return "SRTP_AES128_CM_SHA1_80";
    case kSrtpAes128CmSha1_32: return "SRTP_AES128_CM_SHA1_32";
    case kSrtpAeadAes128Gcm: return "SRTP_AEAD_AES_128_GCM";
    case kSrtpAeadAes256Gcm: return "SRTP_AEAD_AES_256_GCM";
  }
  return nullptr;
}
}  // namespace

struct DtlsTransport::Impl {
  DtlsTransport* owner = nullptr;
  SSL_CTX* ctx = nullptr;
  SSL* ssl = nullptr;
  Timestamp now = Timestamp::Micros(0);
  std::deque<std::vector<uint8_t>> in, out;
  std::vector<std::vector<uint8_t>> received;
  bool verify_failed = false;

  ~Impl() {
    if (ssl) SSL_free(ssl);
    if (ctx) SSL_CTX_free(ctx);
  }

  static BIO_METHOD* Method() {
    static BIO_METHOD* m = [] {
      BIO_METHOD* bm = BIO_meth_new(BIO_TYPE_BIO, "sm_dtls_datagrams");
      BIO_meth_set_write(bm, [](BIO* b, const char* data, int len) -> int {
        auto* i = static_cast<Impl*>(BIO_get_data(b));
        i->out.emplace_back(reinterpret_cast<const uint8_t*>(data), reinterpret_cast<const uint8_t*>(data) + len);
        return len;
      });
      BIO_meth_set_read(bm, [](BIO* b, char* data, int len) -> int {
        auto* i = static_cast<Impl*>(BIO_get_data(b));
        BIO_clear_retry_flags(b);
        if (i->in.empty()) {
          BIO_set_retry_read(b);
          return -1;
        }
        std::vector<uint8_t> d = std::move(i->in.front());
        i->in.pop_front();
        const int n = static_cast<int>(std::min<size_t>(d.size(), static_cast<size_t>(len)));
        std::memcpy(data, d.data(), static_cast<size_t>(n));
        return n;
      });
      BIO_meth_set_ctrl(bm, [](BIO* b, int cmd, long, void*) -> long {
        auto* i = static_cast<Impl*>(BIO_get_data(b));
        switch (cmd) {
          case BIO_CTRL_FLUSH: return 1;
          case BIO_CTRL_PENDING: return i->in.empty() ? 0 : static_cast<long>(i->in.front().size());
          case BIO_CTRL_WPENDING: return 0;
          default: return 0;  // BIO_CTRL_DGRAM_QUERY_MTU: SSL_OP_NO_QUERY_MTU and SSL_set_mtu instead.
        }
      });
      BIO_meth_set_create(bm, [](BIO* b) -> int {
        BIO_set_init(b, 1);
        return 1;
      });
      return bm;
    }();
    return m;
  }
};

DtlsTransport::DtlsTransport(std::unique_ptr<SslIdentity> identity, DtlsRole role, DtlsConfig config)
    : identity_(std::move(identity)), role_(role), config_(std::move(config)), impl_(std::make_unique<Impl>()) {
  impl_->owner = this;
}

DtlsTransport::~DtlsTransport() = default;

bool DtlsTransport::Start(Timestamp now) {
  if (state_ != DtlsState::kNew || !identity_) return false;
  Impl& im = *impl_;
  im.now = now;
  ClockScope clock(&im.now);
  // SetupSSLContext.
  im.ctx = SSL_CTX_new(DTLS_with_buffers_method());
  if (!im.ctx) return false;
  SSL_CTX_set_min_proto_version(im.ctx, DTLS1_2_VERSION);
#ifdef DTLS1_3_VERSION
  SSL_CTX_set_max_proto_version(im.ctx, config_.max_version == kDtls12 ? DTLS1_2_VERSION : DTLS1_3_VERSION);
#else
  SSL_CTX_set_max_proto_version(im.ctx, DTLS1_2_VERSION);
#endif
  // The caller's clock (libwebrtc's clock_for_testing_ path): retransmissions run on simulated time.
  SSL_CTX_set_current_time_cb(im.ctx, [](const SSL*, timeval* out) {
    const int64_t us = t_now ? t_now->us() : 0;
    out->tv_sec = static_cast<decltype(out->tv_sec)>(us / 1000000);
    out->tv_usec = static_cast<decltype(out->tv_usec)>(us % 1000000);
  });
  // ConfigureIdentity: the certificate and its key.
  const SslCertificate& cert = identity_->certificate();
  CRYPTO_BUFFER* buf = CRYPTO_BUFFER_new(cert.der().data(), cert.der().size(), nullptr);
  CRYPTO_BUFFER* chain[] = {buf};
  const int ok = SSL_CTX_set_chain_and_key(im.ctx, chain, 1, static_cast<EVP_PKEY*>(identity_->native_key()), nullptr);
  CRYPTO_BUFFER_free(buf);
  if (!ok) return false;
  // Both ends present a certificate (client auth is on for DTLS); it is checked against SDP's fingerprint, not a CA.
  SSL_CTX_set_custom_verify(im.ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                            [](SSL* ssl, uint8_t*) -> ssl_verify_result_t {
                              auto* i = static_cast<Impl*>(SSL_get_app_data(ssl));
                              const STACK_OF(CRYPTO_BUFFER)* peer = SSL_get0_peer_certificates(ssl);
                              if (!peer || sk_CRYPTO_BUFFER_num(peer) == 0) return ssl_verify_invalid;
                              const CRYPTO_BUFFER* c = sk_CRYPTO_BUFFER_value(peer, 0);
                              const uint8_t* d = CRYPTO_BUFFER_data(c);
                              i->owner->peer_cert_.emplace(std::vector<uint8_t>(d, d + CRYPTO_BUFFER_len(c)));
                              // Without the digest yet, wait to verify until it is known (SSLVerifyCallback).
                              if (!i->owner->peer_fingerprint_) return ssl_verify_ok;
                              if (!i->owner->VerifyPeerCertificate()) {
                                i->verify_failed = true;
                                return ssl_verify_invalid;
                              }
                              return ssl_verify_ok;
                            });
  SSL_CTX_set_cipher_list(im.ctx, "DEFAULT:!NULL:!aNULL:!SHA256:!SHA384:!aECDH:!AESGCM+AES256:!aPSK:!3DES");
  std::string profiles;
  for (int suite : config_.srtp_crypto_suites) {
    if (const char* name = SrtpProfileName(suite)) profiles += (profiles.empty() ? "" : ":") + std::string(name);
  }
  // SSL_CTX_set_tlsext_use_srtp returns 0 on success.
  if (!profiles.empty() && SSL_CTX_set_tlsext_use_srtp(im.ctx, profiles.c_str()) != 0) return false;
  SSL_CTX_set_permute_extensions(im.ctx, 1);
  SSL_CTX_set_options(im.ctx, SSL_OP_NO_TICKET);

  // BeginSSL.
  im.ssl = SSL_new(im.ctx);
  if (!im.ssl) return false;
  SSL_set_app_data(im.ssl, &im);
  BIO* bio = BIO_new(Impl::Method());
  if (!bio) return false;
  BIO_set_data(bio, &im);
  SSL_set_bio(im.ssl, bio, bio);  // The SSL owns the BIO.
  SSL_set_options(im.ssl, SSL_OP_NO_QUERY_MTU);
  SSL_set_mtu(im.ssl, static_cast<unsigned>(config_.mtu));
  DTLSv1_set_initial_timeout_duration(im.ssl, static_cast<unsigned>(config_.initial_timeout_ms));
  if (!config_.ssl_group_ids.empty() &&
      !SSL_set1_group_ids(im.ssl, config_.ssl_group_ids.data(), config_.ssl_group_ids.size())) {
    return false;
  }
  if (role_ == DtlsRole::kServer) SSL_set_accept_state(im.ssl);
  else SSL_set_connect_state(im.ssl);
  state_ = DtlsState::kConnecting;
  Continue(now);
  return state_ != DtlsState::kFailed;
}

void DtlsTransport::Continue(Timestamp now) {
  Impl& im = *impl_;
  im.now = now;
  ClockScope clock(&im.now);
  if (state_ == DtlsState::kConnecting) {
    const int r = SSL_do_handshake(im.ssl);
    if (r == 1) {
      // Connected; verified now, or once SetPeerFingerprint gives the digest.
      if (peer_fingerprint_) state_ = VerifyPeerCertificate() ? DtlsState::kConnected : DtlsState::kFailed;
      else state_ = DtlsState::kConnecting;
    } else {
      const int err = SSL_get_error(im.ssl, r);
      if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) state_ = DtlsState::kFailed;
    }
    if (state_ == DtlsState::kConnecting && SSL_is_init_finished(im.ssl) && !peer_fingerprint_) return;
  }
  if (state_ == DtlsState::kConnected) {
    uint8_t buf[2048];
    for (;;) {
      const int n = SSL_read(im.ssl, buf, sizeof(buf));
      if (n > 0) {
        im.received.emplace_back(buf, buf + n);
        continue;
      }
      const int err = SSL_get_error(im.ssl, n);
      if (err == SSL_ERROR_ZERO_RETURN) state_ = DtlsState::kClosed;
      else if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) state_ = DtlsState::kFailed;
      break;
    }
  }
}

bool DtlsTransport::SetPeerFingerprint(const SslFingerprint& fingerprint, Timestamp now) {
  peer_fingerprint_ = fingerprint;
  if (peer_cert_ && !VerifyPeerCertificate()) {
    // Error("SetPeerCertificateDigest", -1, SSL_AD_BAD_CERTIFICATE): Cleanup sends the peer a fatal alert, so it
    // fails too instead of waiting (#1006).
    if (impl_->ssl && (state_ == DtlsState::kConnecting || state_ == DtlsState::kConnected)) {
      impl_->now = now;
      ClockScope clock(&impl_->now);
      SSL_send_fatal_alert(impl_->ssl, SSL_AD_BAD_CERTIFICATE);
    }
    state_ = DtlsState::kFailed;
    return false;
  }
  // A handshake that finished while waiting for the digest is connected now (SetPeerCertificateDigest).
  if (state_ == DtlsState::kConnecting && impl_->ssl && SSL_is_init_finished(impl_->ssl)) {
    state_ = DtlsState::kConnected;
    Continue(now);
  }
  return true;
}

bool DtlsTransport::VerifyPeerCertificate() const {
  if (!peer_fingerprint_ || !peer_cert_) return false;
  std::vector<uint8_t> digest;
  if (!peer_cert_->ComputeDigest(peer_fingerprint_->algorithm, digest)) return false;
  return digest == peer_fingerprint_->digest;
}

void DtlsTransport::OnPacket(std::span<const uint8_t> datagram, Timestamp now) {
  if (state_ != DtlsState::kConnecting && state_ != DtlsState::kConnected) return;
  impl_->in.emplace_back(datagram.begin(), datagram.end());
  Continue(now);
}

void DtlsTransport::SetInitialRetransmissionTimeout(int timeout_ms) {
  config_.initial_timeout_ms = timeout_ms;
  if (impl_->ssl) DTLSv1_set_initial_timeout_duration(impl_->ssl, static_cast<unsigned>(timeout_ms));
}

bool DtlsTransport::handshake_complete() const { return impl_->ssl && SSL_is_init_finished(impl_->ssl); }

std::optional<Timestamp> DtlsTransport::NextTimeout() const {
  // The timer runs after the handshake too (MaybeSetTimeout after SSL_CONNECTED): a DTLS 1.3 client retransmits its
  // last flight until the server ACKs it (#1006).
  if (!impl_->ssl || (state_ != DtlsState::kConnecting && state_ != DtlsState::kConnected)) return std::nullopt;
  ClockScope clock(&impl_->now);
  timeval tv{};
  if (DTLSv1_get_timeout(impl_->ssl, &tv) != 1) return std::nullopt;
  return impl_->now + TimeDelta::Micros(static_cast<int64_t>(tv.tv_sec) * 1000000 + tv.tv_usec);
}

void DtlsTransport::OnTimeout(Timestamp now) {
  if (!impl_->ssl || (state_ != DtlsState::kConnecting && state_ != DtlsState::kConnected)) return;
  impl_->now = now;
  ClockScope clock(&impl_->now);
  const int r = DTLSv1_handle_timeout(impl_->ssl);
  if (r > 0) ++retransmissions_;
  else if (r < 0) state_ = DtlsState::kFailed;
}

std::vector<std::vector<uint8_t>> DtlsTransport::TakeOutgoing() {
  std::vector<std::vector<uint8_t>> out(std::make_move_iterator(impl_->out.begin()), std::make_move_iterator(impl_->out.end()));
  impl_->out.clear();
  return out;
}

bool DtlsTransport::Send(std::span<const uint8_t> data, Timestamp now) {
  if (state_ != DtlsState::kConnected) return false;
  impl_->now = now;
  ClockScope clock(&impl_->now);
  return SSL_write(impl_->ssl, data.data(), static_cast<int>(data.size())) == static_cast<int>(data.size());
}

std::vector<std::vector<uint8_t>> DtlsTransport::TakeReceived() {
  std::vector<std::vector<uint8_t>> r;
  r.swap(impl_->received);
  return r;
}

int DtlsTransport::version() const {
  if (state_ != DtlsState::kConnected) return 0;
  return SSL_version(impl_->ssl);
}

int DtlsTransport::srtp_crypto_suite() const {
  if (state_ != DtlsState::kConnected) return 0;
  const SRTP_PROTECTION_PROFILE* p = SSL_get_selected_srtp_profile(impl_->ssl);
  return p ? static_cast<int>(p->id) : 0;
}

bool DtlsTransport::ExportSrtpKeyingMaterial(std::span<uint8_t> out) const {
  if (state_ != DtlsState::kConnected) return false;
  return SSL_export_keying_material(impl_->ssl, out.data(), out.size(), kDtlsSrtpExporterLabel,
                                    sizeof(kDtlsSrtpExporterLabel) - 1, nullptr, 0, 0) == 1;
}

bool SrtpKeyAndSaltLength(int suite, int& key_len, int& salt_len) {
  switch (suite) {  // SrtpCryptoSuiteToKeyAndSaltLength (ssl_stream_adapter.cc)
    case kSrtpAes128CmSha1_32:
    case kSrtpAes128CmSha1_80: key_len = 16; salt_len = 14; return true;
    case kSrtpAeadAes128Gcm: key_len = 16; salt_len = 12; return true;
    case kSrtpAeadAes256Gcm: key_len = 32; salt_len = 12; return true;
  }
  return false;
}

}  // namespace sm::tls
