// Cryptographically secure random data: libwebrtc's rtc_base/crypto_random.h (M157), for ICE credentials,
// STUN transaction ids and DTLS (milestone 3, #984). The generator is the TLS backend's (BoringSSL's RAND_bytes).
// A test mode makes the output a fixed sequence, as libwebrtc's SetRandomTestMode.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace sm::tls {

// Fills `out` with random bytes. False if the generator failed (never with BoringSSL).
bool CreateRandomBytes(std::span<uint8_t> out);

// `length` characters from libwebrtc's base64 alphabet (CreateRandomString: what ICE ufrags and passwords use).
std::string CreateRandomString(size_t length);
// `length` characters from `table` (CreateRandomString(len, table, out)). Empty if `table` is empty.
std::string CreateRandomString(size_t length, std::string_view table);
// A random 32-bit id, and one that is never zero (CreateRandomId, CreateRandomNonZeroId).
uint32_t CreateRandomId();
uint32_t CreateRandomNonZeroId();
// A version 4 UUID, lower-case hex with dashes (CreateRandomUuid).
std::string CreateRandomUuid();

// Test mode (SetRandomTestMode): a fixed, repeatable byte sequence instead of the backend's generator.
void SetRandomTestMode(bool test);

}  // namespace sm::tls
