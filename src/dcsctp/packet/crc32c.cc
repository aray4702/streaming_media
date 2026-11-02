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
#include <cstdint>
#include <span>

namespace sm::dcsctp {
namespace {

// libwebrtc computes the checksum with absl::ComputeCrc32c (CRC-32C,
// Castagnoli: the reflected polynomial 0x82F63B78, initial value and final
// XOR 0xFFFFFFFF, RFC 4960 appendix B) and byte-swaps the result
// (absl::byteswap), so that a big-endian store puts it on the wire in the
// order RFC 4960 asks for. absl isn't a dependency here, so this is the
// table-driven CRC-32C, giving the same values (crc32c_test.cc's vectors,
// among them RFC 3720 appendix B.4's).
constexpr std::array<uint32_t, 256> MakeCrc32CTable() {
  std::array<uint32_t, 256> table{};
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t crc = i;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 1) ? (crc >> 1) ^ 0x82F63B78u : crc >> 1;
    }
    table[i] = crc;
  }
  return table;
}

constexpr std::array<uint32_t, 256> kCrc32CTable = MakeCrc32CTable();

constexpr uint32_t ByteSwap(uint32_t v) {
  return (v >> 24) | ((v >> 8) & 0x0000FF00u) | ((v << 8) & 0x00FF0000u) | (v << 24);
}

}  // namespace

uint32_t GenerateCrc32C(std::span<const uint8_t> data) {
  uint32_t crc = 0xFFFFFFFFu;
  for (uint8_t byte : data) {
    crc = kCrc32CTable[(crc ^ byte) & 0xFF] ^ (crc >> 8);
  }
  return ByteSwap(crc ^ 0xFFFFFFFFu);
}
}  // namespace sm::dcsctp
