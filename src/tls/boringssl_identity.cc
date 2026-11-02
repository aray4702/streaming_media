// libwebrtc's rtc_base/boringssl_identity.cc, boringssl_certificate.cc and openssl_key_pair.cc (M157) on
// BoringSSL (#984 part 1b): key pairs, self-signed certificates built with CBB as MakeCertificate does, PEM.
#include <openssl/base64.h>
#include <openssl/bytestring.h>
#include <openssl/digest.h>
#include <openssl/ec.h>
#include <openssl/ec_key.h>
#include <openssl/evp.h>
#include <openssl/mem.h>
#include <openssl/nid.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <cstring>
#include <ctime>

#include "sm/tls/message_digest.h"
#include "sm/tls/ssl_identity.h"

namespace sm::tls {
namespace {

// The signature algorithm OIDs libwebrtc accepts (boringssl_certificate.cc).
const uint8_t kMD5WithRSA[] = {0x2b, 0x0e, 0x03, 0x02, 0x03};
const uint8_t kMD5WithRSAEncryption[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x04};
const uint8_t kECDSAWithSHA1[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x01};
const uint8_t kDSAWithSHA1[] = {0x2a, 0x86, 0x48, 0xce, 0x38, 0x04, 0x03};
const uint8_t kDSAWithSHA1_2[] = {0x2b, 0x0e, 0x03, 0x02, 0x1b};
const uint8_t kSHA1WithRSA[] = {0x2b, 0x0e, 0x03, 0x02, 0x1d};
const uint8_t kSHA1WithRSAEncryption[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05};
const uint8_t kECDSAWithSHA224[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x01};
const uint8_t kSHA224WithRSAEncryption[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0e};
const uint8_t kDSAWithSHA224[] = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x01};
const uint8_t kECDSAWithSHA256[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02};
const uint8_t kSHA256WithRSAEncryption[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b};
const uint8_t kDSAWithSHA256[] = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x02};
const uint8_t kECDSAWithSHA384[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x03};
const uint8_t kSHA384WithRSAEncryption[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0c};
const uint8_t kECDSAWithSHA512[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x04};
const uint8_t kSHA512WithRSAEncryption[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0d};

template <size_t N>
bool OidIs(const CBS& oid, const uint8_t (&want)[N]) {
  return CBS_len(&oid) == N && std::memcmp(CBS_data(&oid), want, N) == 0;
}

bool AddSha256SignatureAlgorithm(CBB* cbb, KeyType key_type) {
  // RFC 5280 4.1.1.2 AlgorithmIdentifier.
  CBB sequence, oid, params;
  if (!CBB_add_asn1(cbb, &sequence, CBS_ASN1_SEQUENCE) || !CBB_add_asn1(&sequence, &oid, CBS_ASN1_OBJECT)) return false;
  if (key_type == KeyType::kRsa) {
    if (!CBB_add_bytes(&oid, kSHA256WithRSAEncryption, sizeof(kSHA256WithRSAEncryption)) ||
        !CBB_add_asn1(&sequence, &params, CBS_ASN1_NULL)) {
      return false;
    }
  } else if (!CBB_add_bytes(&oid, kECDSAWithSHA256, sizeof(kECDSAWithSHA256))) {
    return false;
  }
  return CBB_flush(cbb) == 1;
}

bool AddCommonName(CBB* cbb, std::string_view common_name) {
  static const uint8_t kCommonName[] = {0x55, 0x04, 0x03};  // RFC 4519.
  if (common_name.empty()) return false;
  CBB rdns, rdn, attr, type, value;
  return CBB_add_asn1(cbb, &rdns, CBS_ASN1_SEQUENCE) && CBB_add_asn1(&rdns, &rdn, CBS_ASN1_SET) &&
         CBB_add_asn1(&rdn, &attr, CBS_ASN1_SEQUENCE) && CBB_add_asn1(&attr, &type, CBS_ASN1_OBJECT) &&
         CBB_add_bytes(&type, kCommonName, sizeof(kCommonName)) && CBB_add_asn1(&attr, &value, CBS_ASN1_UTF8STRING) &&
         CBB_add_bytes(&value, reinterpret_cast<const uint8_t*>(common_name.data()), common_name.size()) && CBB_flush(cbb);
}

bool AddTime(CBB* cbb, int64_t t) {
  bssl::UniquePtr<ASN1_TIME> asn1(ASN1_TIME_new());
  if (!asn1 || !ASN1_TIME_set(asn1.get(), static_cast<time_t>(t))) return false;
  unsigned tag;
  switch (ASN1_STRING_type(asn1.get())) {
    case V_ASN1_UTCTIME: tag = CBS_ASN1_UTCTIME; break;
    case V_ASN1_GENERALIZEDTIME: tag = CBS_ASN1_GENERALIZEDTIME; break;
    default: return false;
  }
  CBB child;
  return CBB_add_asn1(cbb, &child, tag) &&
         CBB_add_bytes(&child, ASN1_STRING_get0_data(asn1.get()), ASN1_STRING_length(asn1.get())) && CBB_flush(cbb);
}

// OpenSSLKeyPair::Generate's MakeKey.
bssl::UniquePtr<EVP_PKEY> MakeKey(KeyType type) {
  bssl::UniquePtr<EVP_PKEY> pkey(EVP_PKEY_new());
  if (!pkey) return nullptr;
  if (type == KeyType::kRsa) {
    bssl::UniquePtr<BIGNUM> e(BN_new());
    RSA* rsa = RSA_new();
    if (!e || !rsa || !BN_set_word(e.get(), 0x10001) || !RSA_generate_key_ex(rsa, kRsaDefaultModSize, e.get(), nullptr) ||
        !EVP_PKEY_assign_RSA(pkey.get(), rsa)) {
      RSA_free(rsa);
      return nullptr;
    }
    return pkey;
  }
  EC_KEY* ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
  if (!ec || !EC_KEY_generate_key(ec) || !EVP_PKEY_assign_EC_KEY(pkey.get(), ec)) {
    EC_KEY_free(ec);
    return nullptr;
  }
  return pkey;
}

// MakeCertificate: a self-signed certificate for `pkey` (RFC 5280 4.1), signed with SHA-256.
std::vector<uint8_t> MakeCertificate(EVP_PKEY* pkey, const SslIdentityParams& params) {
  bssl::ScopedCBB cbb;
  CBB tbs_cert, version, validity;
  uint8_t* tbs_bytes = nullptr;
  size_t tbs_len = 0;
  uint64_t serial = 0;
  if (!CBB_init(cbb.get(), 64) || !CBB_add_asn1(cbb.get(), &tbs_cert, CBS_ASN1_SEQUENCE) ||
      !CBB_add_asn1(&tbs_cert, &version, CBS_ASN1_CONTEXT_SPECIFIC | CBS_ASN1_CONSTRUCTED | 0) ||
      !CBB_add_asn1_uint64(&version, 2) || !RAND_bytes(reinterpret_cast<uint8_t*>(&serial), sizeof(serial)) ||
      !CBB_add_asn1_uint64(&tbs_cert, serial) || !AddSha256SignatureAlgorithm(&tbs_cert, params.key_type) ||
      !AddCommonName(&tbs_cert, params.common_name) ||  // issuer
      !CBB_add_asn1(&tbs_cert, &validity, CBS_ASN1_SEQUENCE) || !AddTime(&validity, params.not_before) ||
      !AddTime(&validity, params.not_after) || !AddCommonName(&tbs_cert, params.common_name) ||  // subject
      !EVP_marshal_public_key(&tbs_cert, pkey) || !CBB_finish(cbb.get(), &tbs_bytes, &tbs_len)) {
    return {};
  }
  bssl::UniquePtr<uint8_t> free_tbs(tbs_bytes);
  CBB cert, signature;
  bssl::ScopedEVP_MD_CTX ctx;
  uint8_t* sig_out = nullptr;
  size_t sig_len = 0;
  uint8_t* cert_bytes = nullptr;
  size_t cert_len = 0;
  if (!CBB_init(cbb.get(), tbs_len) || !CBB_add_asn1(cbb.get(), &cert, CBS_ASN1_SEQUENCE) ||
      !CBB_add_bytes(&cert, tbs_bytes, tbs_len) || !AddSha256SignatureAlgorithm(&cert, params.key_type) ||
      !CBB_add_asn1(&cert, &signature, CBS_ASN1_BITSTRING) || !CBB_add_u8(&signature, 0 /* no unused bits */) ||
      !EVP_DigestSignInit(ctx.get(), nullptr, EVP_sha256(), nullptr, pkey) ||
      !EVP_DigestSign(ctx.get(), nullptr, &sig_len, tbs_bytes, tbs_len) ||
      !CBB_reserve(&signature, &sig_out, sig_len) ||
      !EVP_DigestSign(ctx.get(), sig_out, &sig_len, tbs_bytes, tbs_len) || !CBB_did_write(&signature, sig_len) ||
      !CBB_finish(cbb.get(), &cert_bytes, &cert_len)) {
    return {};
  }
  bssl::UniquePtr<uint8_t> free_cert(cert_bytes);
  return std::vector<uint8_t>(cert_bytes, cert_bytes + cert_len);
}

class BoringSslIdentity final : public SslIdentity {
 public:
  BoringSslIdentity(bssl::UniquePtr<EVP_PKEY> key, SslCertificate cert) : key_(std::move(key)), cert_(std::move(cert)) {}

  const SslCertificate& certificate() const override { return cert_; }
  std::string PrivateKeyToPem() const override {
    bssl::ScopedCBB cbb;
    uint8_t* der = nullptr;
    size_t len = 0;
    if (!CBB_init(cbb.get(), 0) || !EVP_marshal_private_key(cbb.get(), key_.get()) || !CBB_finish(cbb.get(), &der, &len)) {
      return {};
    }
    bssl::UniquePtr<uint8_t> free_der(der);
    return DerToPem("PRIVATE KEY", std::span<const uint8_t>(der, len));
  }
  std::string PublicKeyToPem() const override {
    bssl::ScopedCBB cbb;
    uint8_t* der = nullptr;
    size_t len = 0;
    if (!CBB_init(cbb.get(), 0) || !EVP_marshal_public_key(cbb.get(), key_.get()) || !CBB_finish(cbb.get(), &der, &len)) {
      return {};
    }
    bssl::UniquePtr<uint8_t> free_der(der);
    return DerToPem("PUBLIC KEY", std::span<const uint8_t>(der, len));
  }
  std::unique_ptr<SslIdentity> Clone() const override {
    EVP_PKEY_up_ref(key_.get());
    return std::make_unique<BoringSslIdentity>(bssl::UniquePtr<EVP_PKEY>(key_.get()), cert_);
  }
  void* native_key() const override { return key_.get(); }

 private:
  bssl::UniquePtr<EVP_PKEY> key_;
  SslCertificate cert_;
};

std::unique_ptr<SslIdentity> CreateInternal(const SslIdentityParams& params) {
  bssl::UniquePtr<EVP_PKEY> key = MakeKey(params.key_type);
  if (!key) return nullptr;
  std::vector<uint8_t> der = MakeCertificate(key.get(), params);
  if (der.empty()) return nullptr;
  return std::make_unique<BoringSslIdentity>(std::move(key), SslCertificate(std::move(der)));
}

// The certificate's signatureAlgorithm OID (the second element of the outer SEQUENCE).
bool SignatureOid(std::span<const uint8_t> der, CBS* oid) {
  CBS cbs, cert, alg;
  CBS_init(&cbs, der.data(), der.size());
  return CBS_get_asn1(&cbs, &cert, CBS_ASN1_SEQUENCE) && CBS_get_asn1(&cert, nullptr, CBS_ASN1_SEQUENCE) &&
         CBS_get_asn1(&cert, &alg, CBS_ASN1_SEQUENCE) && CBS_get_asn1(&alg, oid, CBS_ASN1_OBJECT);
}

}  // namespace

std::unique_ptr<SslIdentity> SslIdentity::Create(std::string_view common_name, KeyType key_type, int64_t lifetime_s) {
  SslIdentityParams p;
  p.common_name = std::string(common_name);
  p.key_type = key_type;
  const int64_t now = static_cast<int64_t>(std::time(nullptr));
  p.not_before = now + kCertificateWindowSeconds;
  p.not_after = now + lifetime_s;
  if (p.not_before > p.not_after) return nullptr;
  return CreateInternal(p);
}

std::unique_ptr<SslIdentity> SslIdentity::CreateForTest(const SslIdentityParams& params) { return CreateInternal(params); }

std::unique_ptr<SslIdentity> SslIdentity::CreateFromPem(std::string_view private_key_pem, std::string_view certificate_pem) {
  std::vector<uint8_t> key_der;
  if (!PemToDer("PRIVATE KEY", private_key_pem, key_der)) return nullptr;
  CBS cbs;
  CBS_init(&cbs, key_der.data(), key_der.size());
  bssl::UniquePtr<EVP_PKEY> key(EVP_parse_private_key(&cbs));
  std::unique_ptr<SslCertificate> cert = SslCertificate::FromPem(certificate_pem);
  if (!key || !cert) return nullptr;
  // The certificate's public key must be the private key's.
  const uint8_t* p = cert->der().data();
  bssl::UniquePtr<X509> parsed(d2i_X509(nullptr, &p, static_cast<long>(cert->der().size())));
  if (!parsed) return nullptr;
  bssl::UniquePtr<EVP_PKEY> pub(X509_get_pubkey(parsed.get()));
  if (!pub || EVP_PKEY_cmp(pub.get(), key.get()) != 1) return nullptr;
  return std::make_unique<BoringSslIdentity>(std::move(key), std::move(*cert));
}

bool operator==(const SslIdentity& a, const SslIdentity& b) {
  return a.certificate() == b.certificate() && a.PrivateKeyToPem() == b.PrivateKeyToPem();
}

std::unique_ptr<SslCertificate> SslCertificate::FromPem(std::string_view pem) {
  std::vector<uint8_t> der;
  if (!PemToDer("CERTIFICATE", pem, der)) return nullptr;
  const uint8_t* p = der.data();
  bssl::UniquePtr<X509> parsed(d2i_X509(nullptr, &p, static_cast<long>(der.size())));
  if (!parsed) return nullptr;
  return std::make_unique<SslCertificate>(std::move(der));
}

std::string SslCertificate::ToPem() const { return DerToPem("CERTIFICATE", der_); }

bool SslCertificate::ComputeDigest(std::string_view algorithm, std::vector<uint8_t>& out) const {
  out.assign(MessageDigest::kMaxSize, 0);
  const size_t n = sm::tls::ComputeDigest(algorithm, der_, out);
  out.resize(n);
  return n > 0;
}

bool SslCertificate::GetSignatureDigestAlgorithm(std::string& algorithm) const {
  CBS oid;
  if (!SignatureOid(der_, &oid)) return false;
  if (OidIs(oid, kMD5WithRSA) || OidIs(oid, kMD5WithRSAEncryption)) {
    algorithm = kDigestMd5;
  } else if (OidIs(oid, kECDSAWithSHA1) || OidIs(oid, kDSAWithSHA1) || OidIs(oid, kDSAWithSHA1_2) ||
             OidIs(oid, kSHA1WithRSA) || OidIs(oid, kSHA1WithRSAEncryption)) {
    algorithm = kDigestSha1;
  } else if (OidIs(oid, kECDSAWithSHA224) || OidIs(oid, kSHA224WithRSAEncryption) || OidIs(oid, kDSAWithSHA224)) {
    algorithm = kDigestSha224;
  } else if (OidIs(oid, kECDSAWithSHA256) || OidIs(oid, kSHA256WithRSAEncryption) || OidIs(oid, kDSAWithSHA256)) {
    algorithm = kDigestSha256;
  } else if (OidIs(oid, kECDSAWithSHA384) || OidIs(oid, kSHA384WithRSAEncryption)) {
    algorithm = kDigestSha384;
  } else if (OidIs(oid, kECDSAWithSHA512) || OidIs(oid, kSHA512WithRSAEncryption)) {
    algorithm = kDigestSha512;
  } else {
    return false;
  }
  return true;
}

int64_t SslCertificate::CertificateExpirationTime() const {
  const uint8_t* p = der_.data();
  bssl::UniquePtr<X509> x(d2i_X509(nullptr, &p, static_cast<long>(der_.size())));
  if (!x) return -1;
  int64_t t = 0;
  if (!ASN1_TIME_to_posix(X509_get0_notAfter(x.get()), &t)) return -1;
  return t;
}

bool PemToDer(std::string_view type, std::string_view pem, std::vector<uint8_t>& der) {
  const std::string begin = "-----BEGIN " + std::string(type) + "-----";
  const std::string end = "-----END " + std::string(type) + "-----";
  const size_t b = pem.find(begin);
  if (b == std::string_view::npos) return false;
  const size_t body = pem.find('\n', b);
  const size_t e = pem.find(end, body == std::string_view::npos ? b : body);
  if (body == std::string_view::npos || e == std::string_view::npos) return false;
  std::string b64;
  for (char c : pem.substr(body + 1, e - body - 1)) {
    if (c != '\n' && c != '\r' && c != ' ') b64.push_back(c);
  }
  size_t max_len = 0;
  if (!EVP_DecodedLength(&max_len, b64.size())) return false;
  der.resize(max_len);
  size_t out_len = 0;
  if (!EVP_DecodeBase64(der.data(), &out_len, max_len, reinterpret_cast<const uint8_t*>(b64.data()), b64.size())) {
    return false;
  }
  der.resize(out_len);
  return true;
}

std::string DerToPem(std::string_view type, std::span<const uint8_t> der) {
  size_t enc_len = 0;
  EVP_EncodedLength(&enc_len, der.size());
  std::string b64(enc_len, '\0');
  const size_t n = EVP_EncodeBlock(reinterpret_cast<uint8_t*>(b64.data()), der.data(), der.size());
  b64.resize(n);
  std::string pem = "-----BEGIN " + std::string(type) + "-----\n";
  for (size_t i = 0; i < b64.size(); i += 64) pem += b64.substr(i, 64) + "\n";  // SSLIdentity::DerToPem: 64 a line.
  pem += "-----END " + std::string(type) + "-----\n";
  return pem;
}

}  // namespace sm::tls
