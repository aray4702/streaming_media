// Test-only SHA-1 (FIPS 180-4), HMAC-SHA1 (RFC 2104) and MD5 (RFC 1321), so
// the STUN tests can install the crypto hooks of sm/p2p/stun.h without a
// crypto library. The application installs BoringSSL's (#984). Not
// constant-time and not for production use.
#pragma once

#include <cstdint>
#include <span>

namespace sm::p2p::testing {

void Sha1(std::span<const uint8_t> data, uint8_t out[20]);
bool HmacSha1(std::span<const uint8_t> key, std::span<const uint8_t> data, uint8_t out[20]);
bool Md5(std::span<const uint8_t> data, uint8_t out[16]);
// Deterministic bytes (a counter), so transaction ids are reproducible.
bool CounterBytes(std::span<uint8_t> out);

// Installs HmacSha1, Md5 and CounterBytes with SetStunCrypto().
void InstallTestCrypto();

}  // namespace sm::p2p::testing
