// RTCP source description (SDES, RFC 3550 6.5) and goodbye (BYE, RFC 3550
// 6.6) packets. SDES carries the CNAME that libwebrtc puts in every compound
// packet; BYE announces that a source leaves the session. Layouts follow
// libwebrtc's rtcp_packet/sdes and bye. Pure functions: no clock, no state.
// Part of the RTP session layer (docs/design/rtp-session.md, section 8.2).
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sm/loss/rtcp_feedback.h"  // kRtcpSdes, kRtcpBye.

namespace sm::rtp {

// SDES item type (RFC 3550 6.5). Only CNAME is sent by the library; other
// types are parsed and handed back untouched.
inline constexpr uint8_t kSdesCname = 1;

struct SdesItem {
  uint8_t type = 0;  // 1..255; 0 is the list terminator and cannot be an item.
  std::string text;  // At most 255 bytes.
};

struct SdesChunk {
  uint32_t ssrc = 0;
  std::vector<SdesItem> items;
};

// At most 31 chunks fit the 5-bit source count.
inline constexpr size_t kMaxSdesChunks = 31;
inline constexpr size_t kMaxRtcpText = 255;

// Serializes one SDES packet. Returns an empty vector when it cannot be
// encoded: no chunks, more than 31 chunks, an item of type 0 or with text
// longer than 255 bytes, or a packet longer than the 16-bit length allows.
std::vector<uint8_t> SerializeSdes(std::span<const SdesChunk> chunks);

// Parses exactly one SDES packet (header included). Returns nullopt for a
// wrong type, bad version, length mismatch or a chunk that runs past the end.
// Padding (P bit) is stripped.
std::optional<std::vector<SdesChunk>> ParseSdes(std::span<const uint8_t> data);

struct Bye {
  std::vector<uint32_t> ssrcs;  // 0..31 sources leaving.
  std::string reason;           // Optional, at most 255 bytes.
};

// Returns an empty vector for more than 31 sources or a reason over 255 bytes.
std::vector<uint8_t> SerializeBye(const Bye& b);
std::optional<Bye> ParseBye(std::span<const uint8_t> data);

}  // namespace sm::rtp
