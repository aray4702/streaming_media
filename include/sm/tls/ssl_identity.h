// A DTLS identity: a key pair and a self-signed X.509 certificate. libwebrtc's rtc_base/ssl_identity.h,
// ssl_certificate.h and boringssl_identity/boringssl_certificate (M157), ported for milestone 3 (#984 part 1b).
// WebRTC endpoints make one per connection (or reuse one); its SHA-256 fingerprint goes in SDP's a=fingerprint
// and the peer checks the DTLS certificate against it.
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sm::tls {

// KeyType: libwebrtc's default is ECDSA (P-256), which browsers use too.
enum class KeyType { kRsa, kEcdsa };
inline constexpr int kRsaDefaultModSize = 2048;  // ssl_identity.h
inline constexpr int64_t kDefaultCertificateLifetimeSeconds = 60 * 60 * 24 * 30;  // 30 days.
inline constexpr int64_t kCertificateWindowSeconds = -60 * 60 * 24;               // not_before: a day back.

struct SslIdentityParams {
  std::string common_name;
  int64_t not_before = 0;  // Seconds since the epoch.
  int64_t not_after = 0;
  KeyType key_type = KeyType::kEcdsa;
};

// An X.509 certificate, held as DER.
class SslCertificate {
 public:
  explicit SslCertificate(std::vector<uint8_t> der) : der_(std::move(der)) {}
  static std::unique_ptr<SslCertificate> FromPem(std::string_view pem);

  std::span<const uint8_t> der() const { return der_; }
  std::string ToPem() const;
  // The digest of the DER (ComputeDigest): false for an unknown algorithm.
  bool ComputeDigest(std::string_view algorithm, std::vector<uint8_t>& out) const;
  // The signature's digest algorithm, from its OID ("sha-256" for an ECDSA-SHA256 certificate): the algorithm the
  // fingerprint uses (SSLFingerprint::CreateFromCertificate). False if the OID is not one libwebrtc knows.
  bool GetSignatureDigestAlgorithm(std::string& algorithm) const;
  // The validity's notAfter, in seconds since the epoch, or -1.
  int64_t CertificateExpirationTime() const;

  bool operator==(const SslCertificate& o) const { return der_ == o.der_; }

 private:
  std::vector<uint8_t> der_;
};

class SslIdentity {
 public:
  // A new key pair and a certificate valid from a day ago for `lifetime_s` (SSLIdentity::Create). Null on failure.
  static std::unique_ptr<SslIdentity> Create(std::string_view common_name, KeyType key_type = KeyType::kEcdsa,
                                             int64_t lifetime_s = kDefaultCertificateLifetimeSeconds);
  static std::unique_ptr<SslIdentity> CreateForTest(const SslIdentityParams& params);
  // From a PKCS#8 or SEC1/PKCS#1 private key and a certificate, both PEM (CreateFromPEMStrings). Null if they don't
  // parse or don't match.
  static std::unique_ptr<SslIdentity> CreateFromPem(std::string_view private_key_pem, std::string_view certificate_pem);

  virtual ~SslIdentity() = default;
  virtual const SslCertificate& certificate() const = 0;
  virtual std::string PrivateKeyToPem() const = 0;
  virtual std::string PublicKeyToPem() const = 0;
  virtual std::unique_ptr<SslIdentity> Clone() const = 0;
  // The backend's key (an EVP_PKEY* for BoringSSL), for the DTLS adapter (#984 part 2).
  virtual void* native_key() const = 0;
};

bool operator==(const SslIdentity& a, const SslIdentity& b);

// PEM <-> DER (SSLIdentity::PemToDer / DerToPem): `type` is the label, e.g. "CERTIFICATE".
bool PemToDer(std::string_view type, std::string_view pem, std::vector<uint8_t>& der);
std::string DerToPem(std::string_view type, std::span<const uint8_t> der);

}  // namespace sm::tls
