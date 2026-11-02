// sm/p2p/stun_crypto_tls.h.
#include "sm/p2p/stun_crypto_tls.h"

#include <cstdint>
#include <span>

#include "sm/p2p/stun.h"
#include "sm/tls/crypto_random.h"
#include "sm/tls/message_digest.h"

namespace sm::p2p {
namespace {

bool TlsHmacSha1(std::span<const uint8_t> key, std::span<const uint8_t> data, uint8_t out[20]) {
  return tls::ComputeHmac(tls::kDigestSha1, key, data, std::span<uint8_t>(out, 20)) == 20;
}

bool TlsHmacSha256(std::span<const uint8_t> key, std::span<const uint8_t> data, uint8_t out[32]) {
  return tls::ComputeHmac(tls::kDigestSha256, key, data, std::span<uint8_t>(out, 32)) == 32;
}

bool TlsMd5(std::span<const uint8_t> data, uint8_t out[16]) {
  return tls::ComputeDigest(tls::kDigestMd5, data, std::span<uint8_t>(out, 16)) == 16;
}

bool TlsRandom(std::span<uint8_t> out) { return tls::CreateRandomBytes(out); }

}  // namespace

void InstallStunCryptoFromTls() {
  StunCrypto c;
  c.hmac_sha1 = &TlsHmacSha1;
  c.md5 = &TlsMd5;
  c.random_bytes = &TlsRandom;
  c.hmac_sha256 = &TlsHmacSha256;
  SetStunCrypto(c);
}

}  // namespace sm::p2p
