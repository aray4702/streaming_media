// Minimal RTP packet (RFC 3550 fixed header + RFC 8285 extension block).
// Header extension elements themselves are handled by sm/sync/header_extensions.h.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sm {

struct RtpPacket {
  uint8_t payload_type = 0;
  bool marker = false;
  uint16_t seq = 0;
  uint32_t timestamp = 0;
  uint32_t ssrc = 0;
  std::vector<uint32_t> csrcs;
  uint16_t extension_profile = 0;   // 0xBEDE one-byte, 0x1000 two-byte.
  std::vector<uint8_t> extension;   // Raw extension body, padded to 4 bytes.
  std::vector<uint8_t> payload;
  // Trailing padding bytes (RFC 3550 5.1), including the final count byte; 0 =
  // no P bit. A padding-only packet has an empty payload and is how a sender
  // pads or probes the link. Serialize writes zeros, then the count.
  uint8_t padding_size = 0;

  static constexpr size_t kFixedHeaderSize = 12;
  size_t HeaderSize() const;
  size_t size() const { return HeaderSize() + payload.size() + padding_size; }
  std::vector<uint8_t> Serialize() const;
  static std::optional<RtpPacket> Parse(std::span<const uint8_t> data);
};

}  // namespace sm
