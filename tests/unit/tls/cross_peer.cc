// tls_cross::Peer on whichever sm::tls build this copy is compiled against (cross_peer.h, #998).
#include "cross_peer.h"

#include "sm/tls/dtls_transport.h"
#include "sm/tls/ssl_fingerprint.h"
#include "sm/tls/ssl_identity.h"

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

}  // namespace

std::unique_ptr<Peer> MakePeer(bool server, std::optional<std::vector<int>> srtp_crypto_suites) {
  return std::make_unique<PeerImpl>(server, std::move(srtp_crypto_suites));
}

}  // namespace SM_TLS_BACKEND_NS
}  // namespace tls_cross
