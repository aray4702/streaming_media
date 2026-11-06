/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/crc32c.cc (compare/webrtc/reference/net/dcsctp/packet/crc32c.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#include "sm/dcsctp/packet/crc32c.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#if defined(__ARM_FEATURE_CRC32)
#include <arm_acle.h>
#define SM_DCSCTP_CRC32C_ARM 1
#elif defined(__SSE4_2__) && (defined(__x86_64__) || defined(_M_X64))
#include <nmmintrin.h>
#define SM_DCSCTP_CRC32C_SSE42 1
#endif

namespace sm::dcsctp {
namespace {

// libwebrtc computes the checksum with absl::ComputeCrc32c (CRC-32C,
// Castagnoli: the reflected polynomial 0x82F63B78, initial value and final
// XOR 0xFFFFFFFF, RFC 4960 appendix B) and byte-swaps the result
// (absl::byteswap), so that a big-endian store puts it on the wire in the
// order RFC 4960 asks for. absl isn't a dependency here. Like absl, this
// uses the CPU's CRC-32C instructions where the target has them (ARMv8 CRC
// extension, x86 SSE4.2) and otherwise a table-driven software CRC, here
// slicing-by-8 (8 bytes per step) rather than one byte per step. All paths
// give the same values (crc32c_test.cc's vectors, among them RFC 3720
// appendix B.4's, and a check of every length and alignment against the
// bitwise definition).

constexpr uint32_t ByteSwap(uint32_t v) {
  return (v >> 24) | ((v >> 8) & 0x0000FF00u) | ((v << 8) & 0x00FF0000u) | (v << 24);
}

#if defined(SM_DCSCTP_CRC32C_ARM)

uint32_t Crc32CUpdate(uint32_t crc, const uint8_t* p, size_t n) {
  for (; n >= 8; p += 8, n -= 8) {
    uint64_t v;
    std::memcpy(&v, p, 8);
    crc = __crc32cd(crc, v);
  }
  for (; n > 0; ++p, --n) crc = __crc32cb(crc, *p);
  return crc;
}

#elif defined(SM_DCSCTP_CRC32C_SSE42)

uint32_t Crc32CUpdate(uint32_t crc, const uint8_t* p, size_t n) {
  uint64_t c = crc;
  for (; n >= 8; p += 8, n -= 8) {
    uint64_t v;
    std::memcpy(&v, p, 8);
    c = _mm_crc32_u64(c, v);
  }
  crc = static_cast<uint32_t>(c);
  for (; n > 0; ++p, --n) crc = _mm_crc32_u8(crc, *p);
  return crc;
}

#else

using Crc32CTables = std::array<std::array<uint32_t, 256>, 8>;

// tables[0] is the bytewise table; tables[k][i] is the CRC of byte i
// followed by k zero bytes, which slicing-by-8 needs.
constexpr Crc32CTables MakeCrc32CTables() {
  Crc32CTables tables{};
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t crc = i;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 1) ? (crc >> 1) ^ 0x82F63B78u : crc >> 1;
    }
    tables[0][i] = crc;
  }
  for (uint32_t i = 0; i < 256; ++i) {
    for (size_t k = 1; k < 8; ++k) {
      const uint32_t prev = tables[k - 1][i];
      tables[k][i] = (prev >> 8) ^ tables[0][prev & 0xFF];
    }
  }
  return tables;
}

constexpr Crc32CTables kCrc32CTables = MakeCrc32CTables();

// Byte order independent: the reflected CRC consumes bytes in memory order.
constexpr uint32_t LoadLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

uint32_t Crc32CUpdate(uint32_t crc, const uint8_t* p, size_t n) {
  const auto& t = kCrc32CTables;
  for (; n >= 8; p += 8, n -= 8) {
    const uint32_t lo = crc ^ LoadLe32(p);
    const uint32_t hi = LoadLe32(p + 4);
    crc = t[7][lo & 0xFF] ^ t[6][(lo >> 8) & 0xFF] ^ t[5][(lo >> 16) & 0xFF] ^ t[4][lo >> 24] ^ t[3][hi & 0xFF] ^
          t[2][(hi >> 8) & 0xFF] ^ t[1][(hi >> 16) & 0xFF] ^ t[0][hi >> 24];
  }
  for (; n > 0; ++p, --n) crc = t[0][(crc ^ *p) & 0xFF] ^ (crc >> 8);
  return crc;
}

#endif

}  // namespace

uint32_t GenerateCrc32C(std::span<const uint8_t> data) {
  const uint32_t crc = Crc32CUpdate(0xFFFFFFFFu, data.data(), data.size());
  return ByteSwap(crc ^ 0xFFFFFFFFu);
}
}  // namespace sm::dcsctp
