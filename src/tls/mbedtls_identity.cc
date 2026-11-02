// libwebrtc's rtc_base/boringssl_identity.cc, boringssl_certificate.cc and openssl_key_pair.cc (M157) on Mbed TLS
// (#998): key pairs, self-signed certificates as MakeCertificate builds them, PEM. The certificate has the same
// fields as the BoringSSL backend's (boringssl_identity.cc): version 3, a random 64-bit serial, SHA-256 signature,
// the common name as issuer and subject (UTF8String), the validity, the public key and no extensions.
#include <mbedtls/asn1.h>
#include <mbedtls/asn1write.h>
#include <mbedtls/base64.h>
#include <mbedtls/ecp.h>
#include <mbedtls/oid.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/x509_crt.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>

#include "mbedtls_common.h"
#include "sm/tls/message_digest.h"
#include "sm/tls/ssl_identity.h"

namespace sm::tls {
inline namespace SM_TLS_BACKEND_NS {
namespace {

// The signature algorithm OIDs libwebrtc accepts (boringssl_certificate.cc), as in boringssl_identity.cc.
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

// PKCS#8 AlgorithmIdentifier contents (RFC 5480, RFC 8017).
const uint8_t kOidEcPublicKey[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01};
const uint8_t kOidPrime256v1[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07};
const uint8_t kOidRsaEncryption[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01};

template <size_t N>
bool OidIs(const mbedtls_asn1_buf& oid, const uint8_t (&want)[N]) {
  return oid.len == N && std::memcmp(oid.p, want, N) == 0;
}

struct PkDeleter {
  void operator()(mbedtls_pk_context* pk) const {
    mbedtls_pk_free(pk);
    delete pk;
  }
};
// A key shared by an identity and its clones (EVP_PKEY_up_ref in the BoringSSL backend).
using SharedPk = std::shared_ptr<mbedtls_pk_context>;

SharedPk NewPk() {
  auto* pk = new mbedtls_pk_context;
  mbedtls_pk_init(pk);
  return SharedPk(pk, PkDeleter());
}

// OpenSSLKeyPair::Generate's MakeKey: P-256, or RSA 2048 with e = 65537.
SharedPk MakeKey(KeyType type) {
  if (!MbedtlsInit()) return nullptr;
  SharedPk pk = NewPk();
  if (type == KeyType::kRsa) {
    if (mbedtls_pk_setup(pk.get(), mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)) != 0 ||
        mbedtls_rsa_gen_key(mbedtls_pk_rsa(*pk), MbedtlsRng, nullptr, kRsaDefaultModSize, 0x10001) != 0) {
      return nullptr;
    }
    return pk;
  }
  if (mbedtls_pk_setup(pk.get(), mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0 ||
      mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(*pk), MbedtlsRng, nullptr) != 0) {
    return nullptr;
  }
  return pk;
}

// "YYYYMMDDhhmmss" in UTC, as mbedtls_x509write_crt_set_validity takes it.
bool FormatTime(int64_t t, char (&out)[15]) {
  const time_t tt = static_cast<time_t>(t);
  std::tm tm{};
  if (!gmtime_r(&tt, &tm)) return false;
  return std::snprintf(out, sizeof(out), "%04d%02d%02d%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                       tm.tm_hour, tm.tm_min, tm.tm_sec) == 14;
}

// "CN=<name>" with the characters Mbed TLS's name parser treats specially escaped.
std::string SubjectName(std::string_view common_name) {
  std::string s = "CN=";
  for (char c : common_name) {
    if (std::strchr(",=+<>#;\"\\", c) != nullptr) s.push_back('\\');
    s.push_back(c);
  }
  return s;
}

// MakeCertificate: a self-signed certificate for `pk` (RFC 5280 4.1), signed with SHA-256.
std::vector<uint8_t> MakeCertificate(mbedtls_pk_context* pk, const SslIdentityParams& params) {
  if (params.common_name.empty()) return {};  // AddCommonName refuses an empty name.
  char not_before[15], not_after[15];
  if (!FormatTime(params.not_before, not_before) || !FormatTime(params.not_after, not_after)) return {};
  // The serial: 64 random bits as a DER INTEGER's content (CBB_add_asn1_uint64): minimal, and positive.
  uint8_t raw[8];
  if (MbedtlsRng(nullptr, raw, sizeof(raw)) != 0) return {};
  uint8_t serial[9];
  size_t first = 0;
  while (first < 7 && raw[first] == 0 && raw[first + 1] < 0x80) ++first;
  size_t serial_len = 0;
  if (raw[first] & 0x80) serial[serial_len++] = 0;
  for (size_t i = first; i < sizeof(raw); ++i) serial[serial_len++] = raw[i];

  const std::string name = SubjectName(params.common_name);
  mbedtls_x509write_cert crt;
  mbedtls_x509write_crt_init(&crt);
  std::vector<uint8_t> buf(4096);
  int n = -1;
  mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
  mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
  mbedtls_x509write_crt_set_subject_key(&crt, pk);
  mbedtls_x509write_crt_set_issuer_key(&crt, pk);
  if (mbedtls_x509write_crt_set_serial_raw(&crt, serial, serial_len) == 0 &&
      mbedtls_x509write_crt_set_subject_name(&crt, name.c_str()) == 0 &&
      mbedtls_x509write_crt_set_issuer_name(&crt, name.c_str()) == 0 &&
      mbedtls_x509write_crt_set_validity(&crt, not_before, not_after) == 0) {
    // Written at the end of the buffer.
    n = mbedtls_x509write_crt_der(&crt, buf.data(), buf.size(), MbedtlsRng, nullptr);
  }
  mbedtls_x509write_crt_free(&crt);
  if (n <= 0) return {};
  return std::vector<uint8_t>(buf.end() - n, buf.end());
}

// The key as PKCS#8 PrivateKeyInfo DER (RFC 5208), as EVP_marshal_private_key writes it: Mbed TLS writes only SEC1
// and PKCS#1, so the backend wraps them.
std::vector<uint8_t> PrivateKeyPkcs8(const mbedtls_pk_context& pk) {
  std::vector<uint8_t> inner(8192);
  const int inner_len = mbedtls_pk_write_key_der(&pk, inner.data(), inner.size());
  if (inner_len <= 0) return {};
  std::vector<uint8_t> out(inner_len + 64);
  unsigned char* const start = out.data();
  unsigned char* p = start + out.size();
  size_t len = 0;
  int r;
  auto add = [&](int ret) {
    if (ret < 0) return false;
    len += static_cast<size_t>(ret);
    return true;
  };
  // privateKey OCTET STRING.
  if (!add(mbedtls_asn1_write_raw_buffer(&p, start, inner.data() + inner.size() - inner_len, inner_len)) ||
      !add(mbedtls_asn1_write_len(&p, start, static_cast<size_t>(inner_len))) ||
      !add(mbedtls_asn1_write_tag(&p, start, MBEDTLS_ASN1_OCTET_STRING))) {
    return {};
  }
  // privateKeyAlgorithm.
  size_t alg_len = 0;
  if (mbedtls_pk_get_type(&pk) == MBEDTLS_PK_RSA) {
    if ((r = mbedtls_asn1_write_null(&p, start)) < 0) return {};
    alg_len += r;
    if ((r = mbedtls_asn1_write_oid(&p, start, reinterpret_cast<const char*>(kOidRsaEncryption),
                                    sizeof(kOidRsaEncryption))) < 0) {
      return {};
    }
    alg_len += r;
  } else {
    if ((r = mbedtls_asn1_write_oid(&p, start, reinterpret_cast<const char*>(kOidPrime256v1), sizeof(kOidPrime256v1))) < 0) {
      return {};
    }
    alg_len += r;
    if ((r = mbedtls_asn1_write_oid(&p, start, reinterpret_cast<const char*>(kOidEcPublicKey),
                                    sizeof(kOidEcPublicKey))) < 0) {
      return {};
    }
    alg_len += r;
  }
  len += alg_len;
  if (!add(mbedtls_asn1_write_len(&p, start, alg_len)) ||
      !add(mbedtls_asn1_write_tag(&p, start, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE)) ||
      !add(mbedtls_asn1_write_int(&p, start, 0)) ||  // version
      !add(mbedtls_asn1_write_len(&p, start, len)) ||
      !add(mbedtls_asn1_write_tag(&p, start, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE))) {
    return {};
  }
  return std::vector<uint8_t>(p, start + out.size());
}

class MbedtlsIdentity final : public SslIdentity {
 public:
  MbedtlsIdentity(SharedPk key, SslCertificate cert) : key_(std::move(key)), cert_(std::move(cert)) {}

  const SslCertificate& certificate() const override { return cert_; }
  std::string PrivateKeyToPem() const override {
    const std::vector<uint8_t> der = PrivateKeyPkcs8(*key_);
    return der.empty() ? std::string() : DerToPem("PRIVATE KEY", der);
  }
  std::string PublicKeyToPem() const override {
    std::vector<uint8_t> buf(4096);
    const int n = mbedtls_pk_write_pubkey_der(key_.get(), buf.data(), buf.size());
    if (n <= 0) return {};
    return DerToPem("PUBLIC KEY", std::span<const uint8_t>(buf.data() + buf.size() - n, static_cast<size_t>(n)));
  }
  std::unique_ptr<SslIdentity> Clone() const override { return std::make_unique<MbedtlsIdentity>(key_, cert_); }
  void* native_key() const override { return key_.get(); }

 private:
  SharedPk key_;
  SslCertificate cert_;
};

std::unique_ptr<SslIdentity> CreateInternal(const SslIdentityParams& params) {
  if (params.common_name.empty()) return nullptr;
  SharedPk key = MakeKey(params.key_type);
  if (!key) return nullptr;
  std::vector<uint8_t> der = MakeCertificate(key.get(), params);
  if (der.empty()) return nullptr;
  return std::make_unique<MbedtlsIdentity>(std::move(key), SslCertificate(std::move(der)));
}

// The certificate's signatureAlgorithm OID (the second element of the outer SEQUENCE).
bool SignatureOid(std::span<const uint8_t> der, mbedtls_asn1_buf* oid) {
  unsigned char* p = const_cast<unsigned char*>(der.data());
  const unsigned char* end = p + der.size();
  size_t len = 0;
  if (mbedtls_asn1_get_tag(&p, end, &len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE) != 0) return false;
  end = p + len;
  // Skip tbsCertificate.
  if (mbedtls_asn1_get_tag(&p, end, &len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE) != 0) return false;
  p += len;
  if (mbedtls_asn1_get_tag(&p, end, &len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE) != 0) return false;
  const unsigned char* alg_end = p + len;
  if (mbedtls_asn1_get_tag(&p, alg_end, &len, MBEDTLS_ASN1_OID) != 0) return false;
  oid->tag = MBEDTLS_ASN1_OID;
  oid->len = len;
  oid->p = p;
  return true;
}

// Days since 1970-01-01 of a proleptic Gregorian date (Howard Hinnant's days_from_civil).
int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

struct ParsedCert {
  mbedtls_x509_crt crt;
  bool ok;
  explicit ParsedCert(std::span<const uint8_t> der) {
    mbedtls_x509_crt_init(&crt);
    ok = mbedtls_x509_crt_parse_der(&crt, der.data(), der.size()) == 0;
  }
  ~ParsedCert() { mbedtls_x509_crt_free(&crt); }
  ParsedCert(const ParsedCert&) = delete;
  ParsedCert& operator=(const ParsedCert&) = delete;
};

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
  if (!MbedtlsInit()) return nullptr;
  std::vector<uint8_t> key_der;
  if (!PemToDer("PRIVATE KEY", private_key_pem, key_der)) return nullptr;
  SharedPk key = NewPk();
  if (mbedtls_pk_parse_key(key.get(), key_der.data(), key_der.size(), nullptr, 0, MbedtlsRng, nullptr) != 0) {
    return nullptr;
  }
  std::unique_ptr<SslCertificate> cert = SslCertificate::FromPem(certificate_pem);
  if (!cert) return nullptr;
  // The certificate's public key must be the private key's.
  ParsedCert parsed(cert->der());
  if (!parsed.ok || mbedtls_pk_check_pair(&parsed.crt.pk, key.get(), MbedtlsRng, nullptr) != 0) return nullptr;
  return std::make_unique<MbedtlsIdentity>(std::move(key), std::move(*cert));
}

bool operator==(const SslIdentity& a, const SslIdentity& b) {
  return a.certificate() == b.certificate() && a.PrivateKeyToPem() == b.PrivateKeyToPem();
}

std::unique_ptr<SslCertificate> SslCertificate::FromPem(std::string_view pem) {
  std::vector<uint8_t> der;
  if (!PemToDer("CERTIFICATE", pem, der)) return nullptr;
  ParsedCert parsed(der);
  if (!parsed.ok) return nullptr;
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
  mbedtls_asn1_buf oid{};
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
  ParsedCert parsed(der_);
  if (!parsed.ok) return -1;
  const mbedtls_x509_time& t = parsed.crt.valid_to;
  return DaysFromCivil(t.year, static_cast<unsigned>(t.mon), static_cast<unsigned>(t.day)) * 86400 +
         int64_t{t.hour} * 3600 + int64_t{t.min} * 60 + t.sec;
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
  size_t out_len = 0;
  const auto* in = reinterpret_cast<const unsigned char*>(b64.data());
  if (mbedtls_base64_decode(nullptr, 0, &out_len, in, b64.size()) == MBEDTLS_ERR_BASE64_INVALID_CHARACTER) return false;
  der.resize(out_len);
  if (mbedtls_base64_decode(der.data(), der.size(), &out_len, in, b64.size()) != 0) return false;
  der.resize(out_len);
  return true;
}

std::string DerToPem(std::string_view type, std::span<const uint8_t> der) {
  size_t enc_len = 0;
  mbedtls_base64_encode(nullptr, 0, &enc_len, der.data(), der.size());
  std::string b64(enc_len, '\0');
  size_t n = 0;
  if (mbedtls_base64_encode(reinterpret_cast<unsigned char*>(b64.data()), b64.size(), &n, der.data(), der.size()) != 0) {
    return {};
  }
  b64.resize(n);
  std::string pem = "-----BEGIN " + std::string(type) + "-----\n";
  for (size_t i = 0; i < b64.size(); i += 64) pem += b64.substr(i, 64) + "\n";  // SSLIdentity::DerToPem: 64 a line.
  pem += "-----END " + std::string(type) + "-----\n";
  return pem;
}

}  // namespace SM_TLS_BACKEND_NS
}  // namespace sm::tls
