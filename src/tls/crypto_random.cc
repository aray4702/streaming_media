// libwebrtc's rtc_base/crypto_random.cc (M157), on BoringSSL's RAND_bytes (#984).
#include "sm/tls/crypto_random.h"

#include <mutex>
#include <vector>

#include <openssl/rand.h>

namespace sm::tls {
namespace {

// TestRandomGenerator: libwebrtc's fixed linear congruential sequence, seed 7.
struct TestGenerator {
  int seed = 7;
  int Next() { return ((seed = seed * 214013L + 2531011L) >> 16) & 0x7fff; }
};

std::mutex& Lock() {
  static std::mutex& m = *new std::mutex();
  return m;
}
bool g_test_mode = false;
TestGenerator g_test;

bool Generate(uint8_t* buf, size_t len) {
  std::lock_guard<std::mutex> lock(Lock());
  if (g_test_mode) {
    for (size_t i = 0; i < len; ++i) buf[i] = static_cast<uint8_t>(g_test.Next());
    return true;
  }
  return len == 0 || RAND_bytes(buf, len) > 0;
}

constexpr char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr char kHex[] = "0123456789abcdef";
constexpr char kUuidDigit17[] = "89ab";

}  // namespace

bool CreateRandomBytes(std::span<uint8_t> out) { return Generate(out.data(), out.size()); }

std::string CreateRandomString(size_t length, std::string_view table) {
  // libwebrtc: the table size must divide 256, so every character is equally likely.
  if (table.empty() || 256 % table.size() != 0) return {};
  std::vector<uint8_t> bytes(length);
  if (!Generate(bytes.data(), length)) return {};
  std::string s;
  s.reserve(length);
  for (uint8_t b : bytes) s.push_back(table[b % table.size()]);
  return s;
}

std::string CreateRandomString(size_t length) { return CreateRandomString(length, std::string_view(kBase64, 64)); }

uint32_t CreateRandomId() {
  uint32_t id = 0;
  Generate(reinterpret_cast<uint8_t*>(&id), sizeof(id));
  return id;
}

uint32_t CreateRandomNonZeroId() {
  uint32_t id;
  do {
    id = CreateRandomId();
  } while (id == 0);
  return id;
}

std::string CreateRandomUuid() {
  uint8_t b[31];
  Generate(b, sizeof(b));
  std::string s;
  s.reserve(36);
  for (size_t i = 0; i < 8; ++i) s.push_back(kHex[b[i] % 16]);
  s.push_back('-');
  for (size_t i = 8; i < 12; ++i) s.push_back(kHex[b[i] % 16]);
  s.push_back('-');
  s.push_back('4');
  for (size_t i = 12; i < 15; ++i) s.push_back(kHex[b[i] % 16]);
  s.push_back('-');
  s.push_back(kUuidDigit17[b[15] % 4]);
  for (size_t i = 16; i < 19; ++i) s.push_back(kHex[b[i] % 16]);
  s.push_back('-');
  for (size_t i = 19; i < 31; ++i) s.push_back(kHex[b[i] % 16]);
  return s;
}

void SetRandomTestMode(bool test) {
  std::lock_guard<std::mutex> lock(Lock());
  g_test_mode = test;
  g_test = TestGenerator{};
}

}  // namespace sm::tls
