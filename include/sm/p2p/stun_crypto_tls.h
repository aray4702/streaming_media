// STUN's crypto hooks (sm/p2p/stun.h) on sm::tls (#997): HMAC-SHA1 for
// MESSAGE-INTEGRITY, MD5 for the long-term credential key, and BoringSSL's
// random bytes for transaction ids, as libwebrtc's stun.cc gets them from
// rtc_base/message_digest and crypto_random. In the sm::p2p_tls library,
// built when SM_WITH_TLS is on.
#pragma once

namespace sm::p2p {

// Installs sm::tls's functions with SetStunCrypto(). Call once at startup,
// before any STUN use (not thread-safe, like SetStunCrypto).
void InstallStunCryptoFromTls();

}  // namespace sm::p2p
