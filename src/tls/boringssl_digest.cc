// libwebrtc's rtc_base/openssl_digest.cc (M157): MessageDigest on BoringSSL's EVP digests (#984).
#include <openssl/digest.h>

#include "sm/tls/message_digest.h"

namespace sm::tls {
namespace {

const EVP_MD* MdFor(std::string_view alg) {
  // OpenSSLDigest::GetDigestEVP.
  if (alg == kDigestMd5) return EVP_md5();
  if (alg == kDigestSha1) return EVP_sha1();
  if (alg == kDigestSha224) return EVP_sha224();
  if (alg == kDigestSha256) return EVP_sha256();
  if (alg == kDigestSha384) return EVP_sha384();
  if (alg == kDigestSha512) return EVP_sha512();
  return nullptr;
}

class BoringSslDigest final : public MessageDigest {
 public:
  explicit BoringSslDigest(const EVP_MD* md) : md_(md), ctx_(EVP_MD_CTX_new()) { EVP_DigestInit_ex(ctx_, md_, nullptr); }
  ~BoringSslDigest() override { EVP_MD_CTX_free(ctx_); }
  BoringSslDigest(const BoringSslDigest&) = delete;
  BoringSslDigest& operator=(const BoringSslDigest&) = delete;

  size_t Size() const override { return EVP_MD_size(md_); }
  void Update(const void* buf, size_t len) override { EVP_DigestUpdate(ctx_, buf, len); }
  size_t Finish(void* buf, size_t len) override {
    if (len < Size()) return 0;
    unsigned int n = 0;
    EVP_DigestFinal_ex(ctx_, static_cast<unsigned char*>(buf), &n);
    EVP_DigestInit_ex(ctx_, md_, nullptr);  // Ready for reuse, as OpenSSLDigest::Finish.
    return n;
  }

 private:
  const EVP_MD* md_;
  EVP_MD_CTX* ctx_;
};

}  // namespace

std::unique_ptr<MessageDigest> CreateMessageDigest(std::string_view alg) {
  const EVP_MD* md = MdFor(alg);
  if (!md) return nullptr;
  return std::make_unique<BoringSslDigest>(md);
}

}  // namespace sm::tls
