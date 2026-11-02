// libwebrtc's rtc_base/openssl_stream_adapter.cc in DTLS mode (M157) on Mbed TLS (#998): the same DtlsTransport as
// boringssl_dtls.cc, for microcontrollers. Datagrams move through two queues (mbedtls_ssl_set_bio), and Mbed TLS's
// DTLS timer (mbedtls_ssl_set_timer_cb) runs on the caller's clock, so retransmissions follow simulated time.
//
// Where Mbed TLS 3.6 can't do what BoringSSL does, the backend says so (tools/catalog.py, #998):
// - DTLS 1.2 only: Mbed TLS 3.6 has no DTLS 1.3. A BoringSSL peer offering 1.3 negotiates 1.2.
// - SRTP profiles AES128_CM_SHA1_80 and _32 only: Mbed TLS's use_srtp has no AEAD-GCM profiles (RFC 7714). The GCM
//   entries of DtlsConfig::srtp_crypto_suites are skipped.
// - Cipher suites: the ECDHE suites of libwebrtc's list (AES-128-GCM, ChaCha20-Poly1305, AES-128/256-CBC-SHA). Its
//   static-RSA suites are left out: they need an RSA certificate, and libwebrtc's are ECDSA.
// - The SRTP keys are exported from the master secret that Mbed TLS hands to its key-export callback, with the
//   handshake's own PRF (RFC 5705), not with mbedtls_ssl_export_keying_material: in 3.6 that reads ssl->transform,
//   which is still unset on the end that sent the last flight until the peer's first record arrives (a crash), and
//   it takes the PRF hash from the suite's MAC, which is SHA-1 for the CBC-SHA suites instead of TLS 1.2's SHA-256.
// - No HelloVerifyRequest cookie exchange on the server (BoringSSL's DTLS 1.2 server has none either).
#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#include <mbedtls/error.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ssl_ciphersuites.h>
#include <mbedtls/x509_crt.h>

#include <cstring>

#include "mbedtls_common.h"
#include "sm/tls/dtls_transport.h"

namespace sm::tls {
inline namespace SM_TLS_BACKEND_NS {

namespace {
constexpr char kDtlsSrtpExporterLabel[] = "EXTRACTOR-dtls_srtp";
constexpr uint32_t kMaxHandshakeTimeoutMs = 60000;  // BoringSSL caps the doubled DTLS timeout at 60 s (d1_lib.cc).

// The cipher suites, in BoringSSL's order for libwebrtc's cipher list (see the file comment).
const int kCipherSuites[] = {
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
    MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA,
    0,
};

bool MbedtlsSrtpProfile(int suite, mbedtls_ssl_srtp_profile& out) {
  switch (suite) {
    case kSrtpAes128CmSha1_80: out = MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_80; return true;
    case kSrtpAes128CmSha1_32: out = MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_32; return true;
  }
  return false;  // AEAD-GCM: not in Mbed TLS's use_srtp.
}
}  // namespace

struct DtlsTransport::Impl {
  DtlsTransport* owner = nullptr;
  mbedtls_ssl_config conf;
  mbedtls_ssl_context ssl;
  mbedtls_x509_crt own_cert;
  bool set_up = false;
  Timestamp now = Timestamp::Micros(0);
  std::deque<std::vector<uint8_t>> in, out;
  std::vector<std::vector<uint8_t>> received;
  mbedtls_ssl_srtp_profile srtp_profiles[MBEDTLS_TLS_SRTP_MAX_PROFILE_LIST_LENGTH + 1] = {};
  // The DTLS timer (mbedtls_ssl_set_timer_cb): started at `timer_start`, intermediate and final delays.
  std::optional<Timestamp> timer_start;
  uint32_t int_ms = 0, fin_ms = 0;
  bool handshake_over = false;
  bool verify_failed = false;
  // From the key-export callback: the TLS 1.2 master secret, client_random || server_random, and the PRF.
  std::vector<uint8_t> master_secret;
  uint8_t randoms[64] = {};
  mbedtls_tls_prf_types prf = MBEDTLS_SSL_TLS_PRF_NONE;

  Impl() {
    mbedtls_ssl_config_init(&conf);
    mbedtls_ssl_init(&ssl);
    mbedtls_x509_crt_init(&own_cert);
  }
  ~Impl() {
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_x509_crt_free(&own_cert);
  }

  static int Send(void* ctx, const unsigned char* buf, size_t len) {
    auto* i = static_cast<Impl*>(ctx);
    i->out.emplace_back(buf, buf + len);
    return static_cast<int>(len);
  }
  static int Recv(void* ctx, unsigned char* buf, size_t len) {
    auto* i = static_cast<Impl*>(ctx);
    if (i->in.empty()) return MBEDTLS_ERR_SSL_WANT_READ;
    std::vector<uint8_t> d = std::move(i->in.front());
    i->in.pop_front();
    const size_t n = std::min(d.size(), len);
    std::memcpy(buf, d.data(), n);
    return static_cast<int>(n);
  }
  static void SetTimer(void* ctx, uint32_t int_ms, uint32_t fin_ms) {
    auto* i = static_cast<Impl*>(ctx);
    if (fin_ms == 0) {
      i->timer_start.reset();
      return;
    }
    i->timer_start = i->now;
    i->int_ms = int_ms;
    i->fin_ms = fin_ms;
  }
  static int GetTimer(void* ctx) {
    auto* i = static_cast<Impl*>(ctx);
    if (!i->timer_start) return -1;
    const int64_t elapsed_ms = (i->now - *i->timer_start).ms();
    if (elapsed_ms >= i->fin_ms) return 2;
    if (elapsed_ms >= i->int_ms) return 1;
    return 0;
  }
  static void ExportKeys(void* ctx, mbedtls_ssl_key_export_type type, const unsigned char* secret, size_t secret_len,
                         const unsigned char client_random[32], const unsigned char server_random[32],
                         mbedtls_tls_prf_types prf) {
    if (type != MBEDTLS_SSL_KEY_EXPORT_TLS12_MASTER_SECRET) return;
    auto* i = static_cast<Impl*>(ctx);
    i->master_secret.assign(secret, secret + secret_len);
    std::memcpy(i->randoms, client_random, 32);
    std::memcpy(i->randoms + 32, server_random, 32);
    i->prf = prf;
  }
  // Both ends present a certificate; it is checked against SDP's fingerprint, not a CA (SSLVerifyCallback).
  static int Verify(void* ctx, mbedtls_x509_crt* crt, int depth, uint32_t* flags) {
    auto* i = static_cast<Impl*>(ctx);
    *flags = 0;  // Self-signed, from any issuer: only the fingerprint counts.
    if (depth != 0) return 0;
    i->owner->peer_cert_.emplace(std::vector<uint8_t>(crt->raw.p, crt->raw.p + crt->raw.len));
    // Without the digest yet, wait to verify until it is known.
    if (!i->owner->peer_fingerprint_) return 0;
    if (!i->owner->VerifyPeerCertificate()) {
      i->verify_failed = true;
      return MBEDTLS_ERR_X509_FATAL_ERROR;  // Fatal: fails the handshake whatever the authmode.
    }
    return 0;
  }
};

DtlsTransport::DtlsTransport(std::unique_ptr<SslIdentity> identity, DtlsRole role, DtlsConfig config)
    : identity_(std::move(identity)), role_(role), config_(std::move(config)), impl_(std::make_unique<Impl>()) {
  impl_->owner = this;
}

DtlsTransport::~DtlsTransport() = default;

bool DtlsTransport::Start(Timestamp now) {
  if (state_ != DtlsState::kNew || !identity_ || !MbedtlsInit()) return false;
  Impl& im = *impl_;
  im.now = now;
  // SetupSSLContext.
  if (mbedtls_ssl_config_defaults(&im.conf, role_ == DtlsRole::kServer ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
                                  MBEDTLS_SSL_TRANSPORT_DATAGRAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
    return false;
  }
  mbedtls_ssl_conf_rng(&im.conf, MbedtlsRng, nullptr);
  // DTLS 1.2 only (Mbed TLS 3.6 has no DTLS 1.3): config_.max_version can only cap at 1.2.
  mbedtls_ssl_conf_min_tls_version(&im.conf, MBEDTLS_SSL_VERSION_TLS1_2);
  mbedtls_ssl_conf_max_tls_version(&im.conf, MBEDTLS_SSL_VERSION_TLS1_2);
  mbedtls_ssl_conf_ciphersuites(&im.conf, kCipherSuites);
  // ConfigureIdentity: the certificate and its key.
  const SslCertificate& cert = identity_->certificate();
  if (mbedtls_x509_crt_parse_der(&im.own_cert, cert.der().data(), cert.der().size()) != 0 ||
      mbedtls_ssl_conf_own_cert(&im.conf, &im.own_cert, static_cast<mbedtls_pk_context*>(identity_->native_key())) != 0) {
    return false;
  }
  // The peer's certificate is required; Verify checks it against the fingerprint. OPTIONAL, not REQUIRED, because
  // REQUIRED needs a CA chain, which a fingerprint-checked self-signed certificate has none of; a peer that sends no
  // certificate fails in Continue.
  mbedtls_ssl_conf_authmode(&im.conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
  mbedtls_ssl_conf_verify(&im.conf, Impl::Verify, &im);
  // DTLS-SRTP with the profiles Mbed TLS has, in the caller's order.
  size_t n = 0;
  for (int suite : config_.srtp_crypto_suites) {
    mbedtls_ssl_srtp_profile p;
    if (MbedtlsSrtpProfile(suite, p) && n < MBEDTLS_TLS_SRTP_MAX_PROFILE_LIST_LENGTH) im.srtp_profiles[n++] = p;
  }
  im.srtp_profiles[n] = MBEDTLS_TLS_SRTP_UNSET;
  if (!config_.srtp_crypto_suites.empty() && n == 0) return false;  // Only profiles this backend lacks.
  if (n > 0 && mbedtls_ssl_conf_dtls_srtp_protection_profiles(&im.conf, im.srtp_profiles) != 0) return false;
  mbedtls_ssl_conf_handshake_timeout(&im.conf, static_cast<uint32_t>(config_.initial_timeout_ms), kMaxHandshakeTimeoutMs);
  if (role_ == DtlsRole::kServer) mbedtls_ssl_conf_dtls_cookies(&im.conf, nullptr, nullptr, nullptr);
  mbedtls_ssl_conf_renegotiation(&im.conf, MBEDTLS_SSL_RENEGOTIATION_DISABLED);
  mbedtls_ssl_conf_session_tickets(&im.conf, MBEDTLS_SSL_SESSION_TICKETS_DISABLED);  // SSL_OP_NO_TICKET.

  // BeginSSL.
  if (mbedtls_ssl_setup(&im.ssl, &im.conf) != 0) return false;
  im.set_up = true;
  mbedtls_ssl_set_bio(&im.ssl, &im, Impl::Send, Impl::Recv, nullptr);
  mbedtls_ssl_set_timer_cb(&im.ssl, &im, Impl::SetTimer, Impl::GetTimer);
  mbedtls_ssl_set_export_keys_cb(&im.ssl, Impl::ExportKeys, &im);
  mbedtls_ssl_set_mtu(&im.ssl, static_cast<uint16_t>(config_.mtu));
  state_ = DtlsState::kConnecting;
  Continue(now);
  return state_ != DtlsState::kFailed;
}

void DtlsTransport::Continue(Timestamp now) {
  Impl& im = *impl_;
  im.now = now;
  if (state_ == DtlsState::kConnecting && !im.handshake_over) {
    const int r = mbedtls_ssl_handshake(&im.ssl);
    if (r == 0) {
      im.handshake_over = true;
      im.timer_start.reset();
      // A peer that sent no certificate fails (SSL_VERIFY_FAIL_IF_NO_PEER_CERT).
      if (mbedtls_ssl_get_peer_cert(&im.ssl) == nullptr || !peer_cert_) {
        state_ = DtlsState::kFailed;
        return;
      }
    } else if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
      state_ = DtlsState::kFailed;
      return;
    }
  }
  if (state_ == DtlsState::kConnecting && im.handshake_over) {
    // Connected; verified now, or once SetPeerFingerprint gives the digest.
    if (!peer_fingerprint_) return;
    state_ = VerifyPeerCertificate() ? DtlsState::kConnected : DtlsState::kFailed;
  }
  if (state_ == DtlsState::kConnected) {
    uint8_t buf[2048];
    for (;;) {
      const int n = mbedtls_ssl_read(&im.ssl, buf, sizeof(buf));
      if (n > 0) {
        im.received.emplace_back(buf, buf + n);
        continue;
      }
      if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || n == 0) state_ = DtlsState::kClosed;
      else if (n != MBEDTLS_ERR_SSL_WANT_READ && n != MBEDTLS_ERR_SSL_WANT_WRITE && n != MBEDTLS_ERR_SSL_TIMEOUT)
        state_ = DtlsState::kFailed;
      break;
    }
  }
}

bool DtlsTransport::SetPeerFingerprint(const SslFingerprint& fingerprint, Timestamp now) {
  peer_fingerprint_ = fingerprint;
  if (peer_cert_ && !VerifyPeerCertificate()) {
    state_ = DtlsState::kFailed;
    return false;
  }
  // A handshake that finished while waiting for the digest is connected now (SetPeerCertificateDigest).
  if (state_ == DtlsState::kConnecting && impl_->handshake_over) {
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

std::optional<Timestamp> DtlsTransport::NextTimeout() const {
  const Impl& im = *impl_;
  if (!im.set_up || state_ != DtlsState::kConnecting || im.handshake_over || !im.timer_start) return std::nullopt;
  return *im.timer_start + TimeDelta::Millis(im.fin_ms);
}

void DtlsTransport::OnTimeout(Timestamp now) {
  Impl& im = *impl_;
  if (!im.set_up || state_ != DtlsState::kConnecting || im.handshake_over) return;
  im.now = now;
  if (Impl::GetTimer(&im) != 2) return;
  // The handshake sees the expired timer, resends its last flight and doubles the timeout
  // (mbedtls_ssl_fetch_input -> mbedtls_ssl_resend), or fails once the maximum is passed.
  const size_t before = im.out.size();
  Continue(now);
  if (state_ != DtlsState::kFailed && im.out.size() > before) ++retransmissions_;
}

std::vector<std::vector<uint8_t>> DtlsTransport::TakeOutgoing() {
  std::vector<std::vector<uint8_t>> out(std::make_move_iterator(impl_->out.begin()), std::make_move_iterator(impl_->out.end()));
  impl_->out.clear();
  return out;
}

bool DtlsTransport::Send(std::span<const uint8_t> data, Timestamp now) {
  if (state_ != DtlsState::kConnected) return false;
  impl_->now = now;
  return mbedtls_ssl_write(&impl_->ssl, data.data(), data.size()) == static_cast<int>(data.size());
}

std::vector<std::vector<uint8_t>> DtlsTransport::TakeReceived() {
  std::vector<std::vector<uint8_t>> r;
  r.swap(impl_->received);
  return r;
}

int DtlsTransport::version() const {
  if (state_ != DtlsState::kConnected) return 0;
  return mbedtls_ssl_get_version_number(&impl_->ssl) == MBEDTLS_SSL_VERSION_TLS1_2 ? kDtls12 : 0;
}

int DtlsTransport::srtp_crypto_suite() const {
  if (state_ != DtlsState::kConnected) return 0;
  mbedtls_dtls_srtp_info info{};
  mbedtls_ssl_get_dtls_srtp_negotiation_result(&impl_->ssl, &info);
  switch (info.MBEDTLS_PRIVATE(chosen_dtls_srtp_profile)) {
    case MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_80: return kSrtpAes128CmSha1_80;
    case MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_32: return kSrtpAes128CmSha1_32;
  }
  return 0;
}

bool DtlsTransport::ExportSrtpKeyingMaterial(std::span<uint8_t> out) const {
  if (state_ != DtlsState::kConnected || impl_->master_secret.empty()) return false;
  // RFC 5705 without a context: PRF(master_secret, label, client_random || server_random).
  return mbedtls_ssl_tls_prf(impl_->prf, impl_->master_secret.data(), impl_->master_secret.size(), kDtlsSrtpExporterLabel,
                             impl_->randoms, sizeof(impl_->randoms), out.data(), out.size()) == 0;
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

}  // namespace SM_TLS_BACKEND_NS
}  // namespace sm::tls
