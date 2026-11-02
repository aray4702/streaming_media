// The TLS backend's secure generator and memory cleansing, for the backend-neutral files (#998): BoringSSL's
// RAND_bytes and OPENSSL_cleanse (boringssl_digest.cc) or mbedTLS's PSA generator and mbedtls_platform_zeroize
// (mbedtls_digest.cc). Internal to sm::tls.
#pragma once

#include <cstddef>
#include <cstdint>

#include "sm/tls/backend.h"

namespace sm::tls {
inline namespace SM_TLS_BACKEND_NS {

// Fills `buf` with `len` random bytes; false if the generator failed.
bool BackendRandomBytes(uint8_t* buf, size_t len);
// Zeroes `len` bytes at `p` in a way the compiler can't drop (for keys).
void BackendCleanse(void* p, size_t len);

}  // namespace SM_TLS_BACKEND_NS
}  // namespace sm::tls
