// libwebrtc's rtc_base/openssl_adapter.cc in TLS mode (M157) on BoringSSL (#1035): the TlsStream of
// sm/tls/tls_stream.h. OpenSSLAdapter's BIO over a Socket becomes a BIO over two byte buffers the caller moves; the
// X509 verification path (CreateContext without WEBRTC_USE_CRYPTO_BUFFER_CALLBACK, which is how libwebrtc builds
// with its built-in roots) is the one ported.
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <algorithm>
#include <cstring>

#include "sm/tls/tls_stream.h"

namespace sm::tls {
inline namespace SM_TLS_BACKEND_NS {

namespace {
// CreateContext's cipher list.
constexpr char kCipherList[] = "ALL:!SHA256:!SHA384:!aPSK:!ECDSA+SHA1:!ADH:!LOW:!EXP:!MD5:!3DES";
// SSL_CTX_set_verify_depth(ctx, 4): enough for TURN/TLS server chains.
constexpr int kVerifyDepth = 4;
}  // namespace

struct TlsStream::Impl {
  TlsStream* owner = nullptr;
  SSL_CTX* ctx = nullptr;
  SSL* ssl = nullptr;
  std::vector<uint8_t> in;
  size_t in_pos = 0;
  std::vector<uint8_t> out;

  ~Impl() {
    if (ssl) SSL_free(ssl);
    if (ctx) SSL_CTX_free(ctx);
  }

  static BIO_METHOD* Method() {
    static BIO_METHOD* m = [] {
      BIO_METHOD* bm = BIO_meth_new(BIO_TYPE_BIO, "sm_tls_bytes");
      BIO_meth_set_write(bm, [](BIO* b, const char* data, int len) -> int {
        auto* i = static_cast<Impl*>(BIO_get_data(b));
        BIO_clear_retry_flags(b);
        if (len <= 0) return 0;
        i->out.insert(i->out.end(), reinterpret_cast<const uint8_t*>(data), reinterpret_cast<const uint8_t*>(data) + len);
        return len;
      });
      BIO_meth_set_read(bm, [](BIO* b, char* data, int len) -> int {
        auto* i = static_cast<Impl*>(BIO_get_data(b));
        BIO_clear_retry_flags(b);
        const size_t avail = i->in.size() - i->in_pos;
        if (avail == 0) {
          BIO_set_retry_read(b);  // socket_read on a blocking socket.
          return -1;
        }
        const size_t n = std::min(avail, static_cast<size_t>(len));
        std::memcpy(data, i->in.data() + i->in_pos, n);
        i->in_pos += n;
        if (i->in_pos == i->in.size()) {
          i->in.clear();
          i->in_pos = 0;
        }
        return static_cast<int>(n);
      });
      BIO_meth_set_ctrl(bm, [](BIO*, int cmd, long, void*) -> long {
        // socket_ctrl: FLUSH succeeds; PENDING and WPENDING are 0, as libwebrtc's socket BIO reports.
        return cmd == BIO_CTRL_FLUSH ? 1 : 0;
      });
      BIO_meth_set_create(bm, [](BIO* b) -> int {
        BIO_set_init(b, 1);
        return 1;
      });
      return bm;
    }();
    return m;
  }

  // SSLVerifyCallback + SSLVerifyInternal.
  static int Verify(int status, X509_STORE_CTX* store) {
    SSL* ssl = static_cast<SSL*>(X509_STORE_CTX_get_ex_data(store, SSL_get_ex_data_X509_STORE_CTX_idx()));
    auto* i = static_cast<Impl*>(SSL_get_app_data(ssl));
    TlsStream* s = i->owner;
    // The custom verifier overrides errors; with none, or no error, the status stands.
    if (!status && s->config_.cert_verifier) {
      STACK_OF(X509)* chain = X509_STORE_CTX_get0_chain(store);
      if (chain && sk_X509_num(chain) > 0) {
        std::vector<std::vector<uint8_t>> certs;
        for (size_t k = 0; k < sk_X509_num(chain); ++k) {
          uint8_t* der = nullptr;
          const int len = i2d_X509(sk_X509_value(chain, k), &der);
          if (len <= 0) return status;
          certs.emplace_back(der, der + len);
          OPENSSL_free(der);
        }
        if (s->config_.cert_verifier(certs)) {
          s->custom_verified_ = true;
          status = 1;
        }
      }
    }
    // Only for debugging and development: accept anyway.
    if (!status && s->config_.ignore_bad_cert) return 1;
    return status;
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
  Impl& im = *impl_;
  // CreateContext(SSL_MODE_TLS, enable_cache = false).
  im.ctx = SSL_CTX_new(TLS_method());
  if (!im.ctx) {
    Fail("SSL_CTX_new");
    return false;
  }
  SSL_CTX_set_min_proto_version(im.ctx, TLS1_2_VERSION);
  SSL_CTX_set_max_proto_version(im.ctx, config_.max_version == kTls12 ? TLS1_2_VERSION : TLS1_3_VERSION);
  X509_STORE* store = SSL_CTX_get_cert_store(im.ctx);
  if (config_.load_system_roots) SSL_CTX_set_default_verify_paths(im.ctx);  // Best effort: none may be found.
  for (const auto& der : config_.trusted_roots) {
    const uint8_t* p = der.data();
    X509* x = d2i_X509(nullptr, &p, static_cast<long>(der.size()));
    if (!x) continue;
    X509_STORE_add_cert(store, x);
    X509_free(x);
  }
  SSL_CTX_set_verify(im.ctx, SSL_VERIFY_PEER, &Impl::Verify);
  SSL_CTX_set_verify_depth(im.ctx, kVerifyDepth);
  SSL_CTX_set_cipher_list(im.ctx, kCipherList);
  SSL_CTX_set_permute_extensions(im.ctx, 1);
  // ConfigureIdentity.
  if (identity_) {
    const auto der = identity_->certificate().der();
    const uint8_t* p = der.data();
    X509* x = d2i_X509(nullptr, &p, static_cast<long>(der.size()));
    const bool ok = x && SSL_CTX_use_certificate(im.ctx, x) == 1 &&
                    SSL_CTX_use_PrivateKey(im.ctx, static_cast<EVP_PKEY*>(identity_->native_key())) == 1;
    if (x) X509_free(x);
    if (!ok) {
      Fail("ConfigureIdentity");
      return false;
    }
  }

  // BeginSSL.
  im.ssl = SSL_new(im.ctx);
  if (!im.ssl) {
    Fail("SSL_new");
    return false;
  }
  SSL_set_app_data(im.ssl, &im);
  SSL_set_mode(im.ssl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
  if (!config_.server_name.empty() && config_.role == TlsRole::kClient) {
    SSL_set_tlsext_host_name(im.ssl, config_.server_name.c_str());
  }
  SSL_enable_ocsp_stapling(im.ssl);
  SSL_enable_signed_cert_timestamps(im.ssl);
  if (!config_.alpn_protocols.empty()) {
    const std::string alpn = TransformAlpnProtocols(config_.alpn_protocols);
    if (!alpn.empty()) {
      SSL_set_alpn_protos(im.ssl, reinterpret_cast<const uint8_t*>(alpn.data()), static_cast<unsigned>(alpn.size()));
    }
  }
  if (!config_.elliptic_curves.empty()) {
    std::string curves;
    for (const auto& c : config_.elliptic_curves) curves += (curves.empty() ? "" : ":") + c;
    SSL_set1_curves_list(im.ssl, curves.c_str());  // libwebrtc ignores a failure.
  }
  BIO* bio = BIO_new(Impl::Method());
  if (!bio) {
    Fail("BIO_new");
    return false;
  }
  BIO_set_data(bio, &im);
  SSL_set_bio(im.ssl, bio, bio);  // The SSL owns the BIO.
  if (config_.role == TlsRole::kServer) {
    // libwebrtc's adapter is a client (SetRole(SSL_SERVER) exists for tests) and sets no ALPN selection. A server
    // here picks its first protocol the client also offers (SSL_select_next_proto), so tests can check what the
    // client sent.
    if (!config_.alpn_protocols.empty()) {
      SSL_CTX_set_alpn_select_cb(
          im.ctx,
          [](SSL* ssl, const uint8_t** out, uint8_t* out_len, const uint8_t* in, unsigned in_len, void*) -> int {
            auto* i = static_cast<Impl*>(SSL_get_app_data(ssl));
            const std::string ours = TransformAlpnProtocols(i->owner->config_.alpn_protocols);
            uint8_t* sel = nullptr;
            if (SSL_select_next_proto(&sel, out_len, reinterpret_cast<const uint8_t*>(ours.data()),
                                      static_cast<unsigned>(ours.size()), in, in_len) != OPENSSL_NPN_NEGOTIATED) {
              return SSL_TLSEXT_ERR_NOACK;
            }
            *out = sel;
            return SSL_TLSEXT_ERR_OK;
          },
          nullptr);
    }
    SSL_set_accept_state(im.ssl);
  } else {
    SSL_set_connect_state(im.ssl);
  }
  state_ = TlsState::kConnecting;
  Continue();
  return state_ != TlsState::kFailed;
}

// ContinueSSL.
void TlsStream::Continue() {
  if (state_ != TlsState::kConnecting) return;
  Impl& im = *impl_;
  ERR_clear_error();
  const int code = SSL_do_handshake(im.ssl);
  switch (SSL_get_error(im.ssl, code)) {
    case SSL_ERROR_NONE:
      chain_verified_ = SSL_get_verify_result(im.ssl) == X509_V_OK;
      if (!PostConnectionCheck()) {
        Fail("TLS post connection check failed");
        return;
      }
      state_ = TlsState::kConnected;
      break;
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
      break;
    default:
      Fail("handshake failed");
      break;
  }
}

// SSLPostConnectionCheck.
bool TlsStream::PostConnectionCheck() {
  bool valid = false;
  if (config_.role == TlsRole::kClient || !config_.server_name.empty()) {
    // VerifyPeerCertMatchesHost.
    X509* peer = SSL_get_peer_certificate(impl_->ssl);
    if (peer && !config_.server_name.empty()) {
      valid = X509_check_host(peer, config_.server_name.data(), config_.server_name.size(), 0, nullptr) == 1;
    }
    if (peer) X509_free(peer);
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
  if (data.empty()) return 0;  // OpenSSL returns an error for a zero-byte write.
  // SSL_MODE_ENABLE_PARTIAL_WRITE makes SSL_write return after each record; the BIO takes every byte, so loop until
  // all of `data` is in records (libwebrtc's caller loops on the partial count the same way).
  size_t done = 0;
  while (done < data.size()) {
    ERR_clear_error();
    const int ret = SSL_write(impl_->ssl, data.data() + done, static_cast<int>(data.size() - done));
    if (ret > 0) {
      done += static_cast<size_t>(ret);
      continue;
    }
    switch (SSL_get_error(impl_->ssl, ret)) {
      case SSL_ERROR_WANT_READ:
      case SSL_ERROR_WANT_WRITE:
      case SSL_ERROR_ZERO_RETURN:
        return done > 0 ? static_cast<int>(done) : kTlsWouldBlock;
      default:
        Fail("SSL_write");
        return kTlsError;
    }
  }
  return static_cast<int>(done);
}

int TlsStream::Read(std::span<uint8_t> buffer) {
  if (state_ == TlsState::kClosed) return 0;
  if (state_ == TlsState::kConnecting || state_ == TlsState::kNew) return kTlsWouldBlock;
  if (state_ != TlsState::kConnected) return kTlsError;
  if (buffer.empty()) return 0;
  ERR_clear_error();
  const int code = SSL_read(impl_->ssl, buffer.data(), static_cast<int>(buffer.size()));
  if (code > 0) return code;
  switch (SSL_get_error(impl_->ssl, code)) {
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
      return kTlsWouldBlock;
    case SSL_ERROR_ZERO_RETURN:
      state_ = TlsState::kClosed;
      return 0;
    default:
      Fail("SSL_read");
      return kTlsError;
  }
}

int TlsStream::version() const {
  if (state_ != TlsState::kConnected && state_ != TlsState::kClosed) return 0;
  return SSL_version(impl_->ssl);
}

std::string TlsStream::alpn_protocol() const {
  if (!impl_->ssl) return "";
  const uint8_t* p = nullptr;
  unsigned len = 0;
  SSL_get0_alpn_selected(impl_->ssl, &p, &len);
  return std::string(reinterpret_cast<const char*>(p), len);
}

std::string TlsStream::received_server_name() const {
  if (!impl_->ssl) return "";
  const char* n = SSL_get_servername(impl_->ssl, TLSEXT_NAMETYPE_host_name);
  return n ? n : "";
}

}  // namespace SM_TLS_BACKEND_NS
}  // namespace sm::tls
