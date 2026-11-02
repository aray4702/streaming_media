// libwebrtc's p2p/dtls/dtls_utils.cc (M157), #1006.
#include "sm/p2p/dtls_utils.h"

#include <algorithm>

#include <cassert>
#include "sm/p2p/stun.h"

namespace sm::p2p {

namespace {
// RFC 5246 appendix A.1.
constexpr uint8_t kDtlsChangeCipherSpecRecord = 20;
constexpr uint8_t kDtlsHandshakeRecord = 22;
}  // namespace

bool IsDtlsPacket(std::span<const uint8_t> payload) {
  return payload.size() >= kDtlsRecordHeaderLen && payload[0] > 19 && payload[0] < 64;
}

bool IsDtlsClientHelloPacket(std::span<const uint8_t> payload) {
  if (!IsDtlsPacket(payload)) return false;
  return payload.size() > 17 && payload[0] == kDtlsHandshakeRecord && payload[13] == 1;
}

bool IsDtlsHandshakePacket(std::span<const uint8_t> payload) {
  if (!IsDtlsPacket(payload)) return false;
  // ChangeCipherSpec is not a handshake message, but it is followed by an encrypted handshake message.
  return payload.size() > 17 && (payload[0] == kDtlsHandshakeRecord || payload[0] == kDtlsChangeCipherSpecRecord);
}

uint32_t ComputeDtlsPacketHash(std::span<const uint8_t> dtls_packet) { return ComputeCrc32(dtls_packet); }

bool PacketStash::AddIfUnique(std::span<const uint8_t> packet) {
  const uint32_t h = ComputeDtlsPacketHash(packet);
  for (const auto& p : packets_) {
    if (p.hash == h) return false;
  }
  packets_.push_back({h, std::vector<uint8_t>(packet.begin(), packet.end())});
  return true;
}

void PacketStash::Add(std::span<const uint8_t> packet) {
  packets_.push_back({ComputeDtlsPacketHash(packet), std::vector<uint8_t>(packet.begin(), packet.end())});
}

size_t PacketStash::Prune(const std::unordered_set<uint32_t>& hashes) {
  if (hashes.empty()) return 0;
  const uint32_t before = static_cast<uint32_t>(packets_.size());
  std::erase_if(packets_, [&](const StashedPacket& p) { return hashes.contains(p.hash); });
  const uint32_t removed = before - static_cast<uint32_t>(packets_.size());
  if (pos_ >= removed) pos_ -= removed;
  if (pos_ >= packets_.size()) pos_ = packets_.empty() ? 0 : static_cast<uint32_t>(packets_.size()) - 1;
  return removed;
}

void PacketStash::Prune(uint32_t max_size) {
  const size_t size = packets_.size();
  if (size <= max_size) return;
  const size_t removed = size - max_size;
  packets_.erase(packets_.begin(), packets_.begin() + static_cast<std::ptrdiff_t>(removed));
  if (pos_ <= removed) pos_ = 0;
  else pos_ -= static_cast<uint32_t>(removed);
}

std::span<const uint8_t> PacketStash::GetNext() {
  assert(!packets_.empty());
  const uint32_t pos = pos_;
  pos_ = static_cast<uint32_t>((pos + 1) % packets_.size());
  return packets_[pos].buffer;
}

std::vector<std::span<const uint8_t>> PacketStash::GetAll() const {
  std::vector<std::span<const uint8_t>> ret;
  ret.reserve(packets_.size());
  for (const auto& p : packets_) ret.emplace_back(p.buffer);
  return ret;
}

}  // namespace sm::p2p
