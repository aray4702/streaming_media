// libwebrtc's rtc_base/openssl_adapter.cc in TLS mode (M157) on Mbed TLS (#1035): the same TlsStream as
// boringssl_tls.cc, for microcontrollers. Bytes move through two buffers (mbedtls_ssl_set_bio).
//
// Where Mbed TLS 3.6 differs from BoringSSL (tools/catalog.py, #1035):
// - Cipher suites: the ECDHE suites BoringSSL picks from libwebrtc's list ("ALL:!SHA256:!SHA384:!aPSK:!ECDSA+SHA1:
//   !ADH:!LOW:!EXP:!MD5:!3DES") and its TLS 1.3 suites; the static-RSA suites are left out (libwebrtc's TURN servers
//   use ECDHE, and the DTLS backend leaves them out too, #998).
// - No OCSP stapling or SCT request (Mbed TLS has neither) and no extension permutation.
// - The verify callback sees the chain from the root down; a chain that fails is handed to the custom verifier leaf
//   first, as libwebrtc's X509_STORE_CTX_get0_chain gives it. The host name is checked after the handshake, as
//   SSLPostConnectionCheck does, from Mbed TLS's CN/SAN mismatch flag.
#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#include <mbedtls/error.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ssl_ciphersuites.h>
#include <mbedtls/x509_crt.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "mbedtls_common.h"
#include "sm/tls/tls_stream.h"

namespace sm::tls {
inline namespace SM_TLS_BACKEND_NS {

namespace {
constexpr int kVerifyDepth = 4;  // SSL_CTX_set_verify_depth(ctx, 4)

const int kCipherSuites[] = {
    // TLS 1.3 (BoringSSL's order).
    MBEDTLS_TLS1_3_AES_128_GCM_SHA256,
    MBEDTLS_TLS1_3_AES_256_GCM_SHA384,
    MBEDTLS_TLS1_3_CHACHA20_POLY1305_SHA256,
    // TLS 1.2: libwebrtc's list on BoringSSL, ECDHE only (see the file comment).
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
    MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA,
    0,
};

// Where the platform keeps its CA bundle (Debian/Ubuntu/Alpine, Fedora/RHEL, macOS and the BSDs).
const char* const kSystemRootFiles[] = {
    "/etc/ssl/certs/ca-certificates.crt",
    "/etc/pki/tls/certs/ca-bundle.crt",
    "/etc/ssl/cert.pem",
};

// SSL_set1_curves_list's names, as IANA group ids. False for a name BoringSSL wouldn't know either.
bool GroupId(const std::string& name, uint16_t& id) {
  if (name == "X25519") id = MBEDTLS_SSL_IANA_TLS_GROUP_X25519;
  else if (name == "P-256" || name == "prime256v1" || name == "secp256r1") id = MBEDTLS_SSL_IANA_TLS_GROUP_SECP256R1;
  else if (name == "P-384" || name == "secp384r1") id = MBEDTLS_SSL_IANA_TLS_GROUP_SECP384R1;
  else if (name == "P-521" || name == "secp521r1") id = MBEDTLS_SSL_IANA_TLS_GROUP_SECP521R1;
  else return false;
  return true;
}
}  // namespace

struct TlsStream::Impl {
  TlsStream* owner = nullptr;
  mbedtls_ssl_config conf;
  mbedtls_ssl_context ssl;
  mbedtls_x509_crt own_cert;
  mbedtls_x509_crt roots;
  bool have_roots = false;
  bool set_up = false;
  bool handshake_over = false;
  std::vector<uint8_t> in;
  size_t in_pos = 0;
  std::vector<uint8_t> out;
  std::vector<std::string> alpn_storage;
  std::vector<const char*> alpn_list;  // mbedtls_ssl_conf_alpn_protocols keeps the pointer.
  std::vector<uint16_t> groups;        // mbedtls_ssl_conf_groups keeps the pointer.
  std::string sni;                     // What the client sent, on a server.
  // The chain as the verify callback saw it (root first) and the flags it reported before any override.
  std::vector<std::vector<uint8_t>> chain;
  uint32_t chain_flags = 0;

  Impl() {
    mbedtls_ssl_config_init(&conf);
    mbedtls_ssl_init(&ssl);
    mbedtls_x509_crt_init(&own_cert);
    mbedtls_x509_crt_init(&roots);
  }
  ~Impl() {
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_x509_crt_free(&own_cert);
    mbedtls_x509_crt_free(&roots);
  }

  static int Send(void* ctx, const unsigned char* buf, size_t len) {
    auto* i = static_cast<Impl*>(ctx);
    i->out.insert(i->out.end(), buf, buf + len);
    return static_cast<int>(len);
  }
  static int Recv(void* ctx, unsigned char* buf, size_t len) {
    auto* i = static_cast<Impl*>(ctx);
    const size_t avail = i->in.size() - i->in_pos;
    if (avail == 0) return MBEDTLS_ERR_SSL_WANT_READ;
    const size_t n = std::min(avail, len);
    std::memcpy(buf, i->in.data() + i->in_pos, n);
    i->in_pos += n;
    if (i->in_pos == i->in.size()) {
      i->in.clear();
      i->in_pos = 0;
    }
    return static_cast<int>(n);
  }
  // SSLVerifyCallback + SSLVerifyInternal, called from the top of the chain down to the leaf (depth 0).
  static int Verify(void* ctx, mbedtls_x509_crt* crt, int depth, uint32_t* flags) {
    auto* i = static_cast<Impl*>(ctx);
    TlsStream* s = i->owner;
    i->chain.emplace_back(crt->raw.p, crt->raw.p + crt->raw.len);
    // The name is checked after the handshake (SSLPostConnectionCheck), not as part of the chain: keep Mbed TLS's
    // CN/SAN mismatch flag out of the decision, and in the result for PostConnectionCheck.
    i->chain_flags |= *flags & ~MBEDTLS_X509_BADCERT_CN_MISMATCH;
    if (depth >= kVerifyDepth) i->chain_flags |= MBEDTLS_X509_BADCERT_OTHER;  // Deeper than 4: not verified.
    if (depth != 0) {
      *flags = 0;  // Decided at the leaf, with the whole chain.
      return 0;
    }
    if (i->chain_flags == 0) return 0;
    const uint32_t name_flag = *flags & MBEDTLS_X509_BADCERT_CN_MISMATCH;
    if (s->config_.cert_verifier) {
      std::vector<std::vector<uint8_t>> leaf_first(i->chain.rbegin(), i->chain.rend());
      if (s->config_.cert_verifier(leaf_first)) {
        s->custom_verified_ = true;
        *flags = name_flag;
        return 0;
      }
    }
    if (s->config_.ignore_bad_cert) {
      *flags = name_flag;
      return 0;
    }
    return MBEDTLS_ERR_X509_FATAL_ERROR;  // Fails the handshake whatever the authmode, as SSL_VERIFY_PEER does.
  }
  static int Sni(void* ctx, mbedtls_ssl_context*, const unsigned char* name, size_t len) {
    static_cast<Impl*>(ctx)->sni.assign(reinterpret_cast<const char*>(name), len);
    return 0;
  }
};

TlsStream::TlsStream(TlsConfig config, std::unique_ptr<SslIdentity> identity)
    : config_(std::move(config)), identity_(std::move(identity)), impl_(std::make_unique<Impl>()) {
  impl_->owner = this;
}

TlsStream::~TlsStream() = default;

std::string TransformAlpnProtocols(const std::vector<std::string>& protocols) {
  std::string out;
  for (const std::string& p : protocols) {
    if (p.empty() || p.size() > 0xFF) return "";
    out += static_cast<char>(p.size());
    out += p;
  }
  return out;
}

void TlsStream::Fail(std::string why) {
  state_ = TlsState::kFailed;
  if (error_.empty()) error_ = std::move(why);
}

bool TlsStream::Start() {
  if (state_ != TlsState::kNew) return false;
  if (!MbedtlsInit()) {
    Fail("psa_crypto_init");
    return false;
  }
  Impl& im = *impl_;
  const bool server = config_.role == TlsRole::kServer;
  if (mbedtls_ssl_config_defaults(&im.conf, server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
                                  MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
    Fail("mbedtls_ssl_config_defaults");
    return false;
  }
  mbedtls_ssl_conf_rng(&im.conf, MbedtlsRng, nullptr);
  mbedtls_ssl_conf_min_tls_version(&im.conf, MBEDTLS_SSL_VERSION_TLS1_2);
  mbedtls_ssl_conf_max_tls_version(&im.conf, config_.max_version == kTls12 ? MBEDTLS_SSL_VERSION_TLS1_2
                                                                           : MBEDTLS_SSL_VERSION_TLS1_3);
  mbedtls_ssl_conf_ciphersuites(&im.conf, kCipherSuites);
  // Trusted roots: the system's bundle, then the caller's.
  if (config_.load_system_roots) {
    for (const char* f : kSystemRootFiles) {
      if (mbedtls_x509_crt_parse_file(&im.roots, f) >= 0 && im.roots.raw.p) break;
    }
  }
  for (const auto& der : config_.trusted_roots) mbedtls_x509_crt_parse_der(&im.roots, der.data(), der.size());
  im.have_roots = im.roots.raw.p != nullptr;
  if (im.have_roots) mbedtls_ssl_conf_ca_chain(&im.conf, &im.roots, nullptr);
  // SSL_VERIFY_PEER: the peer's certificate is checked (a server asks the client for one); Verify decides what a
  // bad chain does. OPTIONAL so that the host name waits for the post-connection check.
  mbedtls_ssl_conf_authmode(&im.conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
  mbedtls_ssl_conf_verify(&im.conf, Impl::Verify, &im);
  if (identity_) {
    const auto der = identity_->certificate().der();
    if (mbedtls_x509_crt_parse_der(&im.own_cert, der.data(), der.size()) != 0 ||
        mbedtls_ssl_conf_own_cert(&im.conf, &im.own_cert, static_cast<mbedtls_pk_context*>(identity_->native_key())) !=
            0) {
      Fail("ConfigureIdentity");
      return false;
    }
  }
  if (!config_.alpn_protocols.empty() && !TransformAlpnProtocols(config_.alpn_protocols).empty()) {
    im.alpn_storage = config_.alpn_protocols;
    for (const auto& p : im.alpn_storage) im.alpn_list.push_back(p.c_str());
    im.alpn_list.push_back(nullptr);
    mbedtls_ssl_conf_alpn_protocols(&im.conf, im.alpn_list.data());
  }
  if (!config_.elliptic_curves.empty()) {
    std::vector<uint16_t> ids;
    bool known = true;
    for (const auto& c : config_.elliptic_curves) {
      uint16_t id = 0;
      if (!GroupId(c, id)) known = false;
      ids.push_back(id);
    }
    // SSL_set1_curves_list rejects the whole list for one unknown name, and libwebrtc then keeps the defaults.
    if (known) {
      im.groups = std::move(ids);
      im.groups.push_back(MBEDTLS_SSL_IANA_TLS_GROUP_NONE);
      mbedtls_ssl_conf_groups(&im.conf, im.groups.data());
    }
  }
  if (server) mbedtls_ssl_conf_sni(&im.conf, Impl::Sni, &im);
  mbedtls_ssl_conf_renegotiation(&im.conf, MBEDTLS_SSL_RENEGOTIATION_DISABLED);

  if (mbedtls_ssl_setup(&im.ssl, &im.conf) != 0) {
    Fail("mbedtls_ssl_setup");
    return false;
  }
  im.set_up = true;
  if (!server) {
    // SNI and the expected name. An empty name is set to NULL explicitly: Mbed TLS 3.6 refuses to verify a server
    // without one otherwise, and the post-connection check fails it as libwebrtc's does.
    if (mbedtls_ssl_set_hostname(&im.ssl, config_.server_name.empty() ? nullptr : config_.server_name.c_str()) != 0) {
      Fail("mbedtls_ssl_set_hostname");
      return false;
    }
  }
  mbedtls_ssl_set_bio(&im.ssl, &im, Impl::Send, Impl::Recv, nullptr);
  state_ = TlsState::kConnecting;
  Continue();
  return state_ != TlsState::kFailed;
}

void TlsStream::Continue() {
  if (state_ != TlsState::kConnecting) return;
  Impl& im = *impl_;
  const int r = mbedtls_ssl_handshake(&im.ssl);
  if (r == 0) {
    im.handshake_over = true;
    chain_verified_ = mbedtls_ssl_get_peer_cert(&im.ssl) != nullptr && im.chain_flags == 0;
    if (!PostConnectionCheck()) {
      Fail("TLS post connection check failed");
      return;
    }
    state_ = TlsState::kConnected;
    return;
  }
  if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) Fail("handshake failed");
}

bool TlsStream::PostConnectionCheck() {
  // VerifyPeerCertMatchesHost: a leaf, a name, and no CN/SAN mismatch.
  bool valid = false;
  if (!config_.server_name.empty() && config_.role == TlsRole::kClient &&
      mbedtls_ssl_get_peer_cert(&impl_->ssl) != nullptr) {
    valid = (mbedtls_ssl_get_verify_result(&impl_->ssl) & MBEDTLS_X509_BADCERT_CN_MISMATCH) == 0;
  }
  valid = valid && (chain_verified_ || custom_verified_);
  if (!valid && config_.ignore_bad_cert) valid = true;
  return valid;
}

void TlsStream::Feed(std::span<const uint8_t> data) {
  if (state_ != TlsState::kConnecting && state_ != TlsState::kConnected) return;
  impl_->in.insert(impl_->in.end(), data.begin(), data.end());
  Continue();
}

std::vector<uint8_t> TlsStream::TakeOutgoing() {
  std::vector<uint8_t> out;
  out.swap(impl_->out);
  return out;
}

bool TlsStream::HasOutgoing() const { return !impl_->out.empty(); }

int TlsStream::Write(std::span<const uint8_t> data) {
  if (state_ == TlsState::kConnecting || state_ == TlsState::kNew) return kTlsWouldBlock;
  if (state_ != TlsState::kConnected) return kTlsError;
  if (data.empty()) return 0;
  // The BIO takes everything, so a record is never left half written; mbedtls_ssl_write may still split `data`
  // into records of at most the maximum fragment length, so loop.
  size_t done = 0;
  while (done < data.size()) {
    const int r = mbedtls_ssl_write(&impl_->ssl, data.data() + done, data.size() - done);
    if (r > 0) {
      done += static_cast<size_t>(r);
      continue;
    }
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) break;
    Fail("mbedtls_ssl_write");
    return kTlsError;
  }
  return done > 0 ? static_cast<int>(done) : kTlsWouldBlock;
}

int TlsStream::Read(std::span<uint8_t> buffer) {
  if (state_ == TlsState::kClosed) return 0;
  if (state_ == TlsState::kConnecting || state_ == TlsState::kNew) return kTlsWouldBlock;
  if (state_ != TlsState::kConnected) return kTlsError;
  if (buffer.empty()) return 0;
  for (;;) {
    const int n = mbedtls_ssl_read(&impl_->ssl, buffer.data(), buffer.size());
    if (n > 0) return n;
    // A TLS 1.3 NewSessionTicket is handled inside; read on.
    if (n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;
    if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) return kTlsWouldBlock;
    if (n == 0 || n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
      state_ = TlsState::kClosed;
      return 0;
    }
    Fail("mbedtls_ssl_read");
    return kTlsError;
  }
}

int TlsStream::version() const {
  if (!impl_->handshake_over || (state_ != TlsState::kConnected && state_ != TlsState::kClosed)) return 0;
  return static_cast<int>(mbedtls_ssl_get_version_number(&impl_->ssl));
}

std::string TlsStream::alpn_protocol() const {
  if (!impl_->set_up) return "";
  const char* p = mbedtls_ssl_get_alpn_protocol(&impl_->ssl);
  return p ? p : "";
}

std::string TlsStream::received_server_name() const { return impl_->sni; }

}  // namespace SM_TLS_BACKEND_NS
}  // namespace sm::tls
