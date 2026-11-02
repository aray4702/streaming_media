/* Additions to Mbed TLS's default mbedtls_config.h for sm::tls (#998, cmake/SmMbedTLS.cmake). */
#ifndef SM_MBEDTLS_USER_CONFIG_H
#define SM_MBEDTLS_USER_CONFIG_H

/* DTLS-SRTP (RFC 5764): the use_srtp extension, for DtlsTransport. Off in the default configuration. */
#define MBEDTLS_SSL_DTLS_SRTP

#endif
