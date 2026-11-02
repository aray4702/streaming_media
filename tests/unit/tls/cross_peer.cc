// tls_cross::Peer on whichever sm::tls build this copy is compiled against (cross_peer.h, #998).
#include "cross_peer.h"

#include "sm/tls/dtls_transport.h"
#include "sm/tls/ssl_fingerprint.h"
#include "sm/tls/ssl_identity.h"
#include "sm/tls/tls_stream.h"

namespace tls_cross {
namespace SM_TLS_BACKEND_NS {
namespace {

#define SM_STR2(x) #x
#define SM_STR(x) SM_STR2(x)

class PeerImpl final : public Peer {
 public:
  PeerImpl(bool server, std::optional<std::vector<int>> suites) {
    auto id = sm::tls::SslIdentity::Create(server ? "server" : "client");
    fp_ = sm::tls::SslFingerprint::CreateFromCertificate(*id);
    sm::tls::DtlsConfig cfg;
    if (suites) cfg.srtp_crypto_suites = *suites;
    t_ = std::make_unique<sm::tls::DtlsTransport>(std::move(id),
                                                  server ? sm::tls::DtlsRole::kServer : sm::tls::DtlsRole::kClient, cfg);
  }
  std::string fingerprint_algorithm() const override { return fp_->algorithm; }
  std::vector<uint8_t> fingerprint_digest() const override { return fp_->digest; }
  bool SetPeerFingerprint(const std::string& algorithm, const std::vector<uint8_t>& digest, sm::Timestamp now) override {
    return t_->SetPeerFingerprint(sm::tls::SslFingerprint(algorithm, digest), now);
  }
  bool Start(sm::Timestamp now) override { return t_->Start(now); }
  void OnPacket(const std::vector<uint8_t>& d, sm::Timestamp now) override { t_->OnPacket(d, now); }
  std::optional<sm::Timestamp> NextTimeout() const override { return t_->NextTimeout(); }
  void OnTimeout(sm::Timestamp now) override { t_->OnTimeout(now); }
  std::vector<std::vector<uint8_t>> TakeOutgoing() override { return t_->TakeOutgoing(); }
  bool Send(const std::vector<uint8_t>& d, sm::Timestamp now) override { return t_->Send(d, now); }
  std::vector<std::vector<uint8_t>> TakeReceived() override { return t_->TakeReceived(); }
  bool connected() const override { return t_->state() == sm::tls::DtlsState::kConnected; }
  bool failed() const override { return t_->state() == sm::tls::DtlsState::kFailed; }
  int version() const override { return t_->version(); }
  int srtp_crypto_suite() const override { return t_->srtp_crypto_suite(); }
  std::vector<uint8_t> SrtpKeyingMaterial() const override {
    int key = 0, salt = 0;
    if (!sm::tls::SrtpKeyAndSaltLength(t_->srtp_crypto_suite(), key, salt)) return {};
    std::vector<uint8_t> out(2 * static_cast<size_t>(key + salt));
    if (!t_->ExportSrtpKeyingMaterial(out)) return {};
    return out;
  }
  int retransmissions() const override { return t_->retransmissions(); }
  std::string backend() const override { return SM_STR(SM_TLS_BACKEND_NS); }

 private:
  std::unique_ptr<sm::tls::SslFingerprint> fp_;
  std::unique_ptr<sm::tls::DtlsTransport> t_;
};

class TlsPeerImpl final : public TlsPeer {
 public:
  explicit TlsPeerImpl(const TlsPeerOptions& o) {
    sm::tls::TlsConfig cfg;
    cfg.load_system_roots = false;
    cfg.alpn_protocols = o.alpn_protocols;
    cfg.max_version = o.max_version;
    std::unique_ptr<sm::tls::SslIdentity> id;
    if (o.server) {
      cfg.role = sm::tls::TlsRole::kServer;
      cfg.ignore_bad_cert = true;  // As libwebrtc's adapter tests run the server end.
      id = sm::tls::SslIdentity::Create(o.name);
      const auto der = id->certificate().der();
      cert_.assign(der.begin(), der.end());
    } else {
      cfg.server_name = o.name;
      cfg.trusted_roots = o.trusted_roots;
    }
    t_ = std::make_unique<sm::tls::TlsStream>(std::move(cfg), std::move(id));
  }
  std::vector<uint8_t> certificate() const override { return cert_; }
  bool Start() override { return t_->Start(); }
  void Feed(const std::vector<uint8_t>& data) override { t_->Feed(data); }
  std::vector<uint8_t> TakeOutgoing() override { return t_->TakeOutgoing(); }
  int Write(const std::string& data) override {
    return t_->Write(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(data.data()), data.size()));
  }
  std::string ReadAll() override {
    std::string out;
    uint8_t buf[256];
    int n;
    while ((n = t_->Read(buf)) > 0) out.append(reinterpret_cast<char*>(buf), static_cast<size_t>(n));
    return out;
  }
  bool connected() const override { return t_->state() == sm::tls::TlsState::kConnected; }
  bool failed() const override { return t_->state() == sm::tls::TlsState::kFailed; }
  int version() const override { return t_->version(); }
  std::string alpn_protocol() const override { return t_->alpn_protocol(); }
  std::string received_server_name() const override { return t_->received_server_name(); }

 private:
  std::vector<uint8_t> cert_;
  std::unique_ptr<sm::tls::TlsStream> t_;
};

}  // namespace

std::unique_ptr<TlsPeer> MakeTlsPeer(const TlsPeerOptions& options) { return std::make_unique<TlsPeerImpl>(options); }

std::unique_ptr<Peer> MakePeer(bool server, std::optional<std::vector<int>> srtp_crypto_suites) {
  return std::make_unique<PeerImpl>(server, std::move(srtp_crypto_suites));
}

}  // namespace SM_TLS_BACKEND_NS
}  // namespace tls_cross
