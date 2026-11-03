// Prints CreateTurnRestCredentials(secret, user, expiry) as {"username":...,"credential":...}, so the
// signaling service's unit tests can check /turn against the library's own output (#1102).
// Usage: sm_turn_credentials SECRET USER EXPIRY
#include <cstdio>
#include <cstdlib>

#include "sm/p2p/stun_crypto_tls.h"
#include "sm/p2p/turn_port.h"

int main(int argc, char** argv) {
  if (argc != 4) {
    std::fprintf(stderr, "usage: %s SECRET USER EXPIRY\n", argv[0]);
    return 2;
  }
  sm::p2p::InstallStunCryptoFromTls();
  const auto c = sm::p2p::CreateTurnRestCredentials(argv[1], argv[2], std::strtoll(argv[3], nullptr, 10));
  std::printf("{\"username\":\"%s\",\"credential\":\"%s\"}\n", c.username.c_str(), c.password.c_str());
  return 0;
}
