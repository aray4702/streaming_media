// DTLS packet classification and the packet stash: libwebrtc's p2p/dtls/dtls_utils.{h,cc} at M157
// (compare/webrtc/reference/), for the DTLS transport (#1006). DTLS records share the ICE transport with STUN and
// SRTP; RFC 7983 tells them apart by the first byte (DTLS is 20-63).
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_set>
#include <vector>

namespace sm::p2p {

inline constexpr size_t kDtlsRecordHeaderLen = 13;
inline constexpr size_t kMaxDtlsPacketLen = 2048;

// A DTLS record: at least a record header, first byte 20-63 (RFC 7983).
bool IsDtlsPacket(std::span<const uint8_t> payload);
// A handshake record carrying a ClientHello.
bool IsDtlsClientHelloPacket(std::span<const uint8_t> payload);
// A handshake or ChangeCipherSpec record.
bool IsDtlsHandshakePacket(std::span<const uint8_t> payload);
// The CRC-32 of the packet (rtc_base/crc32), which identifies a stashed packet.
uint32_t ComputeDtlsPacketHash(std::span<const uint8_t> dtls_packet);

// A few DTLS packets kept for later (the ClientHellos that arrive before DTLS starts), read round-robin.
class PacketStash {
 public:
  void Add(std::span<const uint8_t> packet);
  // Adds the packet unless one with the same hash is stashed; false if it was a duplicate.
  bool AddIfUnique(std::span<const uint8_t> packet);
  // Removes the packets with these hashes; returns how many were removed.
  size_t Prune(const std::unordered_set<uint32_t>& packet_hashes);
  // Keeps the newest `max_size` packets.
  void Prune(uint32_t max_size);
  // The next packet, round-robin. Not empty() first.
  std::span<const uint8_t> GetNext();
  std::vector<std::span<const uint8_t>> GetAll() const;
  void clear() {
    packets_.clear();
    pos_ = 0;
  }
  bool empty() const { return packets_.empty(); }
  int size() const { return static_cast<int>(packets_.size()); }
  static uint32_t Hash(std::span<const uint8_t> packet) { return ComputeDtlsPacketHash(packet); }

 private:
  struct StashedPacket {
    uint32_t hash;
    std::vector<uint8_t> buffer;
  };
  uint32_t pos_ = 0;
  std::vector<StashedPacket> packets_;  // Very few items, so a vector rather than a map.
};

}  // namespace sm::p2p
