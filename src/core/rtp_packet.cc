#include "sm/core/rtp_packet.h"

namespace sm {

namespace {
void Put16(std::vector<uint8_t>& b, uint16_t v) {
  b.push_back(static_cast<uint8_t>(v >> 8));
  b.push_back(static_cast<uint8_t>(v));
}
void Put32(std::vector<uint8_t>& b, uint32_t v) {
  Put16(b, static_cast<uint16_t>(v >> 16));
  Put16(b, static_cast<uint16_t>(v));
}
uint16_t Get16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t Get32(const uint8_t* p) { return (uint32_t{Get16(p)} << 16) | Get16(p + 2); }
}  // namespace

size_t RtpPacket::HeaderSize() const {
  size_t s = kFixedHeaderSize + 4 * csrcs.size();
  if (!extension.empty()) s += 4 + ((extension.size() + 3) / 4) * 4;
  return s;
}

std::vector<uint8_t> RtpPacket::Serialize() const {
  std::vector<uint8_t> b;
  b.reserve(size());
  bool has_ext = !extension.empty();
  b.push_back(static_cast<uint8_t>(0x80 | (has_ext ? 0x10 : 0) | (csrcs.size() & 0x0f)));
  b.push_back(static_cast<uint8_t>((marker ? 0x80 : 0) | (payload_type & 0x7f)));
  Put16(b, seq);
  Put32(b, timestamp);
  Put32(b, ssrc);
  for (uint32_t c : csrcs) Put32(b, c);
  if (has_ext) {
    size_t words = (extension.size() + 3) / 4;
    Put16(b, extension_profile);
    Put16(b, static_cast<uint16_t>(words));
    b.insert(b.end(), extension.begin(), extension.end());
    b.resize(b.size() + words * 4 - extension.size(), 0);
  }
  b.insert(b.end(), payload.begin(), payload.end());
  return b;
}

std::optional<RtpPacket> RtpPacket::Parse(std::span<const uint8_t> d) {
  if (d.size() < kFixedHeaderSize || (d[0] >> 6) != 2) return std::nullopt;
  RtpPacket p;
  bool padding = d[0] & 0x20;
  bool has_ext = d[0] & 0x10;
  size_t cc = d[0] & 0x0f;
  p.marker = d[1] & 0x80;
  p.payload_type = d[1] & 0x7f;
  p.seq = Get16(&d[2]);
  p.timestamp = Get32(&d[4]);
  p.ssrc = Get32(&d[8]);
  size_t off = kFixedHeaderSize;
  if (d.size() < off + 4 * cc) return std::nullopt;
  for (size_t i = 0; i < cc; ++i, off += 4) p.csrcs.push_back(Get32(&d[off]));
  if (has_ext) {
    if (d.size() < off + 4) return std::nullopt;
    p.extension_profile = Get16(&d[off]);
    size_t len = size_t{Get16(&d[off + 2])} * 4;
    off += 4;
    if (d.size() < off + len) return std::nullopt;
    p.extension.assign(d.begin() + static_cast<std::ptrdiff_t>(off), d.begin() + static_cast<std::ptrdiff_t>(off + len));
    off += len;
  }
  size_t end = d.size();
  if (padding) {
    uint8_t pad = d[end - 1];
    if (pad == 0 || pad > end - off) return std::nullopt;
    end -= pad;
  }
  p.payload.assign(d.begin() + static_cast<std::ptrdiff_t>(off), d.begin() + static_cast<std::ptrdiff_t>(end));
  return p;
}

}  // namespace sm
