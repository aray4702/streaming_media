// libwebrtc's rtc_base/ssl_fingerprint.cc (M157) (#984 part 1b).
#include "sm/tls/ssl_fingerprint.h"

#include <cctype>

#include "sm/tls/message_digest.h"

namespace sm::tls {

std::unique_ptr<SslFingerprint> SslFingerprint::Create(std::string_view algorithm, const SslCertificate& cert) {
  std::vector<uint8_t> digest;
  if (!cert.ComputeDigest(algorithm, digest)) return nullptr;
  return std::make_unique<SslFingerprint>(algorithm, digest);
}

std::unique_ptr<SslFingerprint> SslFingerprint::Create(std::string_view algorithm, const SslIdentity& identity) {
  return Create(algorithm, identity.certificate());
}

std::unique_ptr<SslFingerprint> SslFingerprint::CreateFromRfc4572(std::string_view algorithm, std::string_view fp) {
  if (algorithm.empty() || !IsFips180DigestAlgorithm(algorithm) || fp.empty()) return nullptr;
  // HexDecodeWithDelimiter(':'): two hex digits per byte, ':' between them.
  std::vector<uint8_t> bytes;
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < fp.size();) {
    if (i + 1 >= fp.size()) return nullptr;
    const int hi = hex(fp[i]), lo = hex(fp[i + 1]);
    if (hi < 0 || lo < 0) return nullptr;
    bytes.push_back(static_cast<uint8_t>(hi * 16 + lo));
    i += 2;
    if (i < fp.size()) {
      if (fp[i] != ':' || i + 1 >= fp.size()) return nullptr;
      ++i;
    }
  }
  if (bytes.empty() || bytes.size() > MessageDigest::kMaxSize) return nullptr;
  return std::make_unique<SslFingerprint>(algorithm, bytes);
}

std::unique_ptr<SslFingerprint> SslFingerprint::CreateFromCertificate(const SslIdentity& identity) {
  std::string alg;
  if (!identity.certificate().GetSignatureDigestAlgorithm(alg)) return nullptr;
  return Create(alg, identity);
}

std::string SslFingerprint::GetRfc4572Fingerprint() const {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string s;
  for (size_t i = 0; i < digest.size(); ++i) {
    if (i) s.push_back(':');
    s.push_back(kHex[digest[i] >> 4]);
    s.push_back(kHex[digest[i] & 15]);
  }
  return s;
}

std::string SslFingerprint::ToString() const { return algorithm + " " + GetRfc4572Fingerprint(); }

}  // namespace sm::tls
