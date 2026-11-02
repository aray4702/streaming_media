#include "test_crypto.h"

#include <cstring>
#include <string_view>
#include <vector>

#include "sm/p2p/stun.h"

namespace sm::p2p::testing {
namespace {

uint32_t Rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

// Pads as SHA-1 and MD5 do: 0x80, zeros, then the bit length in 8 bytes
// (big-endian for SHA-1, little-endian for MD5).
std::vector<uint8_t> Pad(std::span<const uint8_t> data, bool big_endian) {
  std::vector<uint8_t> m(data.begin(), data.end());
  uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
  m.push_back(0x80);
  while (m.size() % 64 != 56) m.push_back(0);
  for (int i = 0; i < 8; ++i) {
    int shift = big_endian ? 56 - 8 * i : 8 * i;
    m.push_back(static_cast<uint8_t>(bits >> shift));
  }
  return m;
}

}  // namespace

void Sha1(std::span<const uint8_t> data, uint8_t out[20]) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  std::vector<uint8_t> m = Pad(data, true);
  for (size_t off = 0; off < m.size(); off += 64) {
    uint32_t w[80];
    for (int t = 0; t < 16; ++t) {
      const uint8_t* p = &m[off + 4 * static_cast<size_t>(t)];
      w[t] = (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | p[3];
    }
    for (int t = 16; t < 80; ++t) w[t] = Rotl(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int t = 0; t < 80; ++t) {
      uint32_t f, k;
      if (t < 20) {
        f = (b & c) | (~b & d);
        k = 0x5A827999;
      } else if (t < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1;
      } else if (t < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDC;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6;
      }
      uint32_t tmp = Rotl(a, 5) + f + e + k + w[t];
      e = d;
      d = c;
      c = Rotl(b, 30);
      b = a;
      a = tmp;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
  }
  for (int i = 0; i < 5; ++i) {
    out[4 * i] = static_cast<uint8_t>(h[i] >> 24);
    out[4 * i + 1] = static_cast<uint8_t>(h[i] >> 16);
    out[4 * i + 2] = static_cast<uint8_t>(h[i] >> 8);
    out[4 * i + 3] = static_cast<uint8_t>(h[i]);
  }
}

bool HmacSha1(std::span<const uint8_t> key, std::span<const uint8_t> data, uint8_t out[20]) {
  uint8_t k[64] = {};
  if (key.size() > 64) {
    Sha1(key, k);
  } else if (!key.empty()) {
    std::memcpy(k, key.data(), key.size());
  }
  std::vector<uint8_t> inner(64 + data.size());
  for (int i = 0; i < 64; ++i) inner[static_cast<size_t>(i)] = k[i] ^ 0x36;
  if (!data.empty()) std::memcpy(inner.data() + 64, data.data(), data.size());
  uint8_t ih[20];
  Sha1(inner, ih);
  uint8_t outer[64 + 20];
  for (int i = 0; i < 64; ++i) outer[i] = k[i] ^ 0x5c;
  std::memcpy(outer + 64, ih, 20);
  Sha1(outer, out);
  return true;
}

bool Md5(std::span<const uint8_t> data, uint8_t out[16]) {
  static const uint32_t kS[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                  5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                  4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                  6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
  static const uint32_t kK[64] = {
      0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
      0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
      0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
      0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
      0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
      0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
      0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
      0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
  uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
  std::vector<uint8_t> m = Pad(data, false);
  for (size_t off = 0; off < m.size(); off += 64) {
    uint32_t w[16];
    for (int t = 0; t < 16; ++t) {
      const uint8_t* p = &m[off + 4 * static_cast<size_t>(t)];
      w[t] = uint32_t{p[0]} | (uint32_t{p[1]} << 8) | (uint32_t{p[2]} << 16) | (uint32_t{p[3]} << 24);
    }
    uint32_t a = a0, b = b0, c = c0, d = d0;
    for (int i = 0; i < 64; ++i) {
      uint32_t f;
      int g;
      if (i < 16) {
        f = (b & c) | (~b & d);
        g = i;
      } else if (i < 32) {
        f = (d & b) | (~d & c);
        g = (5 * i + 1) % 16;
      } else if (i < 48) {
        f = b ^ c ^ d;
        g = (3 * i + 5) % 16;
      } else {
        f = c ^ (b | ~d);
        g = (7 * i) % 16;
      }
      f = f + a + kK[i] + w[g];
      a = d;
      d = c;
      c = b;
      b = b + Rotl(f, static_cast<int>(kS[i]));
    }
    a0 += a;
    b0 += b;
    c0 += c;
    d0 += d;
  }
  uint32_t h[4] = {a0, b0, c0, d0};
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) out[4 * i + j] = static_cast<uint8_t>(h[i] >> (8 * j));
  }
  return true;
}

bool CounterBytes(std::span<uint8_t> out) {
  static uint8_t next = 0;
  for (auto& b : out) b = next++;
  return true;
}

void Sha256(std::span<const uint8_t> data, uint8_t out[32]) {
  static const uint32_t k[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
  std::vector<uint8_t> m = Pad(data, true);
  for (size_t off = 0; off < m.size(); off += 64) {
    uint32_t w[64];
    for (int t = 0; t < 16; ++t) {
      const uint8_t* p = &m[off + 4 * static_cast<size_t>(t)];
      w[t] = (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | p[3];
    }
    for (int t = 16; t < 64; ++t) {
      uint32_t s0 = rotr(w[t - 15], 7) ^ rotr(w[t - 15], 18) ^ (w[t - 15] >> 3);
      uint32_t s1 = rotr(w[t - 2], 17) ^ rotr(w[t - 2], 19) ^ (w[t - 2] >> 10);
      w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int t = 0; t < 64; ++t) {
      uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[t] + w[t];
      uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      hh = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
  }
  for (int i = 0; i < 8; ++i) {
    for (int j = 0; j < 4; ++j) out[4 * i + j] = static_cast<uint8_t>(h[i] >> (24 - 8 * j));
  }
}

bool HmacSha256(std::span<const uint8_t> key, std::span<const uint8_t> data, uint8_t out[32]) {
  uint8_t k[64] = {};
  if (key.size() > 64) {
    Sha256(key, k);
  } else {
    std::memcpy(k, key.data(), key.size());
  }
  std::vector<uint8_t> inner(64 + data.size());
  for (int i = 0; i < 64; ++i) inner[static_cast<size_t>(i)] = k[i] ^ 0x36;
  std::memcpy(inner.data() + 64, data.data(), data.size());
  uint8_t ih[32];
  Sha256(inner, ih);
  uint8_t outer[64 + 32];
  for (int i = 0; i < 64; ++i) outer[i] = k[i] ^ 0x5c;
  std::memcpy(outer + 64, ih, 32);
  Sha256(outer, out);
  return true;
}

void InstallTestCrypto() {
  StunCrypto c;
  c.hmac_sha1 = &HmacSha1;
  c.md5 = &Md5;
  c.random_bytes = &CounterBytes;
  c.hmac_sha256 = &HmacSha256;
  SetStunCrypto(c);
}

}  // namespace sm::p2p::testing
