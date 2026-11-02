// The TLS backend sm::tls is built on (#998): BoringSSL, as libwebrtc M157 builds, or Mbed TLS for
// microcontrollers (C1, decided on #944). CMake picks it with SM_TLS_BACKEND=boringssl|mbedtls and sets
// SM_TLS_BACKEND_MBEDTLS on every target that links an mbedTLS build of sm::tls.
//
// Each backend's API lives in its own inline namespace (sm::tls::boringssl_backend, sm::tls::mbedtls_backend).
// Callers still write sm::tls::DtlsTransport; the inline namespace only gives the two builds distinct symbols, so a
// BoringSSL build and an mbedTLS build can link into one binary (the cross test, tests/unit/tls/cross_test.cc).
#pragma once

#if defined(SM_TLS_BACKEND_MBEDTLS) && SM_TLS_BACKEND_MBEDTLS
#define SM_TLS_BACKEND_NS mbedtls_backend
#else
#define SM_TLS_BACKEND_NS boringssl_backend
#endif
