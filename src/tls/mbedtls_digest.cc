// libwebrtc's rtc_base/openssl_digest.cc (M157) on Mbed TLS (#998): MessageDigest on mbedtls_md, and the backend's
// random generator (PSA's, which CryptoRandom and the DTLS adapter use).
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/error.h>
extern "C" {  // psa_util.h has no C++ guard of its own.
#include <mbedtls/psa_util.h>
}
#include <psa/crypto.h>

#include <mutex>

#include "backend_random.h"
#include "mbedtls_common.h"
#include "sm/tls/message_digest.h"

namespace sm::tls {
inline namespace SM_TLS_BACKEND_NS {
namespace {

mbedtls_md_type_t MdFor(std::string_view alg) {
  // OpenSSLDigest::GetDigestEVP's table.
  if (alg == kDigestMd5) return MBEDTLS_MD_MD5;
  if (alg == kDigestSha1) return MBEDTLS_MD_SHA1;
  if (alg == kDigestSha224) return MBEDTLS_MD_SHA224;
  if (alg == kDigestSha256) return MBEDTLS_MD_SHA256;
  if (alg == kDigestSha384) return MBEDTLS_MD_SHA384;
  if (alg == kDigestSha512) return MBEDTLS_MD_SHA512;
  return MBEDTLS_MD_NONE;
}

class MbedtlsDigest final : public MessageDigest {
 public:
  explicit MbedtlsDigest(const mbedtls_md_info_t* info) : info_(info) {
    mbedtls_md_init(&ctx_);
    ok_ = mbedtls_md_setup(&ctx_, info_, 0) == 0 && mbedtls_md_starts(&ctx_) == 0;
  }
  ~MbedtlsDigest() override { mbedtls_md_free(&ctx_); }
  MbedtlsDigest(const MbedtlsDigest&) = delete;
  MbedtlsDigest& operator=(const MbedtlsDigest&) = delete;

  bool ok() const { return ok_; }
  size_t Size() const override { return mbedtls_md_get_size(info_); }
  void Update(const void* buf, size_t len) override {
    mbedtls_md_update(&ctx_, static_cast<const unsigned char*>(buf), len);
  }
  size_t Finish(void* buf, size_t len) override {
    if (len < Size()) return 0;
    mbedtls_md_finish(&ctx_, static_cast<unsigned char*>(buf));
    mbedtls_md_starts(&ctx_);  // Ready for reuse, as OpenSSLDigest::Finish.
    return Size();
  }

 private:
  const mbedtls_md_info_t* info_;
  mbedtls_md_context_t ctx_;
  bool ok_ = false;
};

std::mutex& RngLock() {
  static std::mutex& m = *new std::mutex();
  return m;
}

}  // namespace

std::unique_ptr<MessageDigest> CreateMessageDigest(std::string_view alg) {
  const mbedtls_md_type_t type = MdFor(alg);
  const mbedtls_md_info_t* info = type == MBEDTLS_MD_NONE ? nullptr : mbedtls_md_info_from_type(type);
  if (!info) return nullptr;
  auto d = std::make_unique<MbedtlsDigest>(info);
  if (!d->ok()) return nullptr;
  return d;
}

bool MbedtlsInit() {
  static const bool ok = psa_crypto_init() == PSA_SUCCESS;
  return ok;
}

int MbedtlsRng(void*, unsigned char* out, size_t len) {
  if (!MbedtlsInit()) return MBEDTLS_ERR_ERROR_GENERIC_ERROR;
  std::lock_guard<std::mutex> lock(RngLock());
  return mbedtls_psa_get_random(MBEDTLS_PSA_RANDOM_STATE, out, len);
}

bool BackendRandomBytes(uint8_t* buf, size_t len) { return MbedtlsRng(nullptr, buf, len) == 0; }
void BackendCleanse(void* p, size_t len) { mbedtls_platform_zeroize(p, len); }

}  // namespace SM_TLS_BACKEND_NS
}  // namespace sm::tls
