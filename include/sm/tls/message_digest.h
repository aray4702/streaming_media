// Message digests and HMAC: libwebrtc's rtc_base/message_digest.h (M157), ported for the connectivity layer
// (milestone 3, #984). STUN's MESSAGE-INTEGRITY (HMAC-SHA1), TURN's long-term key (MD5) and the certificate
// fingerprints in SDP (SHA-256) use it. The digests come from the TLS backend (BoringSSL; mbedTLS later, #944).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace sm::tls {

// The algorithm names, as RFC 4572 / SDP a=fingerprint spells them (message_digest.cc).
inline constexpr char kDigestMd5[] = "md5";
inline constexpr char kDigestSha1[] = "sha-1";
inline constexpr char kDigestSha224[] = "sha-224";
inline constexpr char kDigestSha256[] = "sha-256";
inline constexpr char kDigestSha384[] = "sha-384";
inline constexpr char kDigestSha512[] = "sha-512";

class MessageDigest {
 public:
  static constexpr size_t kMaxSize = 64;  // SHA-512.
  virtual ~MessageDigest() = default;
  // The digest output size (16 for MD5, 20 for SHA-1).
  virtual size_t Size() const = 0;
  virtual void Update(const void* buf, size_t len) = 0;
  // Writes the digest to `buf` (at least Size() bytes) and resets for the next one. Returns the bytes written, or 0
  // if `len` is too small.
  virtual size_t Finish(void* buf, size_t len) = 0;
};

// MessageDigestFactory::Create: null for an unknown algorithm.
std::unique_ptr<MessageDigest> CreateMessageDigest(std::string_view alg);

// RFC 4572 section 5: the FIPS 180 algorithms a self-signed certificate's fingerprint must use.
bool IsFips180DigestAlgorithm(std::string_view alg);

// The digest of `input` into `output`; the bytes written, or 0 for an unknown algorithm or a short `output`.
size_t ComputeDigest(std::string_view alg, std::span<const uint8_t> input, std::span<uint8_t> output);
// The digest of `input`, hex-encoded (lower case); empty for an unknown algorithm.
std::string ComputeDigest(std::string_view alg, std::string_view input);

// RFC 2104 HMAC, as libwebrtc computes it (for digests with a 64-byte block: MD5, SHA-1, SHA-224, SHA-256). The
// bytes written, or 0 for an unknown or unsupported algorithm or a short `output`.
size_t ComputeHmac(std::string_view alg, std::span<const uint8_t> key, std::span<const uint8_t> input,
                   std::span<uint8_t> output);
// The HMAC of `input` with `key`, hex-encoded; empty for an unknown or unsupported algorithm.
std::string ComputeHmac(std::string_view alg, std::string_view key, std::string_view input);

}  // namespace sm::tls
