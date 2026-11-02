// Shared plumbing of the Mbed TLS backend (#998): PSA initialisation and the random generator that the key
// generation, certificate signing and DTLS use. Internal to sm::tls.
#pragma once

#include <cstddef>

#include "sm/tls/backend.h"

namespace sm::tls {
inline namespace SM_TLS_BACKEND_NS {

// psa_crypto_init, once per process. False if it failed.
bool MbedtlsInit();
// An f_rng for Mbed TLS (mbedtls_psa_get_random behind a lock, since the default build has no MBEDTLS_THREADING_C).
int MbedtlsRng(void* ctx, unsigned char* out, size_t len);

}  // namespace SM_TLS_BACKEND_NS
}  // namespace sm::tls
