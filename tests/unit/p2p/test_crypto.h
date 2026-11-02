// Test-only SHA-1 and SHA-256 (FIPS 180-4), their HMACs (RFC 2104) and MD5 (RFC 1321), so
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
void Sha256(std::span<const uint8_t> data, uint8_t out[32]);
bool HmacSha256(std::span<const uint8_t> key, std::span<const uint8_t> data, uint8_t out[32]);
// Deterministic bytes (a counter), so transaction ids are reproducible. It is an 8-bit counter, so 12-byte STUN
// transaction ids repeat after 64 of them.
bool CounterBytes(std::span<uint8_t> out);
// Deterministic bytes that don't repeat (a 64-bit xorshift), for runs long enough to reach 64 transaction ids:
// StunRequestManager::Send drops a request whose id is still pending, as libwebrtc DCHECKs a fresh id.
bool UniqueBytes(std::span<uint8_t> out);

// Installs HmacSha1, Md5, HmacSha256 and CounterBytes with SetStunCrypto().
void InstallTestCrypto();
// InstallTestCrypto() with UniqueBytes as random_bytes: transaction ids that never repeat, as a real random source
// gives (libwebrtc's CreateRandomString). The ICE comparison (compare/webrtc/ice_scenarios.h) and the TURN tests use
// it.
void InstallTestCryptoWithUniqueIds();

}  // namespace sm::p2p::testing
