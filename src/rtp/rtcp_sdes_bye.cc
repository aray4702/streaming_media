#include "sm/rtp/rtcp_sdes_bye.h"

namespace sm::rtp {

namespace {
constexpr size_t kHeaderBytes = 4;

void Put(std::vector<uint8_t>& b, uint32_t v, int bytes) {
  for (int i = bytes - 1; i >= 0; --i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
uint32_t Get(const uint8_t* p, int bytes) {
  uint32_t v = 0;
  for (int i = 0; i < bytes; ++i) v = (v << 8) | p[i];
  return v;
}

void PadToWord(std::vector<uint8_t>& b) {
  while (b.size() % 4 != 0) b.push_back(0);
}

// Pads the body to a word and writes the header over its first four bytes.
bool Finish(std::vector<uint8_t>& b, uint8_t count, uint8_t pt) {
  PadToWord(b);
  size_t words = b.size() / 4 - 1;
  if (words > 0xFFFF) return false;
  b[0] = static_cast<uint8_t>(0x80 | (count & 0x1f));
  b[1] = pt;
  b[2] = static_cast<uint8_t>(words >> 8);
  b[3] = static_cast<uint8_t>(words);
  return true;
}

// Validates the header; returns the 5-bit count and the body (after the
// 4-byte header, padding removed).
struct Body {
  uint8_t count = 0;
  std::span<const uint8_t> data;
};
std::optional<Body> ParseHeader(std::span<const uint8_t> d, uint8_t pt) {
  if (d.size() < kHeaderBytes || (d[0] >> 6) != 2 || d[1] != pt) return std::nullopt;
  if (d.size() != (static_cast<size_t>(Get(&d[2], 2)) + 1) * 4) return std::nullopt;
  size_t end = d.size();
  if (d[0] & 0x20) {
    size_t pad = d[end - 1];
    if (pad == 0 || pad > end - kHeaderBytes) return std::nullopt;
    end -= pad;
  }
  return Body{static_cast<uint8_t>(d[0] & 0x1f), d.subspan(kHeaderBytes, end - kHeaderBytes)};
}
}  // namespace

std::vector<uint8_t> SerializeSdes(std::span<const SdesChunk> chunks) {
  if (chunks.empty() || chunks.size() > kMaxSdesChunks) return {};
  std::vector<uint8_t> b(kHeaderBytes, 0);
  for (const SdesChunk& c : chunks) {
    Put(b, c.ssrc, 4);
    for (const SdesItem& it : c.items) {
      if (it.type == 0 || it.text.size() > kMaxRtcpText) return {};
      b.push_back(it.type);
      b.push_back(static_cast<uint8_t>(it.text.size()));
      b.insert(b.end(), it.text.begin(), it.text.end());
    }
    b.push_back(0);  // List terminator, then zero bytes up to the next word.
    PadToWord(b);
  }
  if (!Finish(b, static_cast<uint8_t>(chunks.size()), loss::kRtcpSdes)) return {};
  return b;
}

std::optional<std::vector<SdesChunk>> ParseSdes(std::span<const uint8_t> d) {
  auto body = ParseHeader(d, loss::kRtcpSdes);
  if (!body) return std::nullopt;
  std::vector<SdesChunk> out;
  std::span<const uint8_t> p = body->data;
  size_t off = 0;
  for (uint8_t i = 0; i < body->count; ++i) {
    if (p.size() - off < 4) return std::nullopt;
    SdesChunk c;
    c.ssrc = Get(&p[off], 4);
    off += 4;
    for (;;) {
      if (off >= p.size()) return std::nullopt;  // Missing terminator.
      uint8_t type = p[off++];
      if (type == 0) break;
      if (off >= p.size()) return std::nullopt;
      size_t len = p[off++];
      if (p.size() - off < len) return std::nullopt;
      c.items.push_back({type, std::string(p.begin() + static_cast<std::ptrdiff_t>(off),
                                           p.begin() + static_cast<std::ptrdiff_t>(off + len))});
      off += len;
    }
    off = (off + 3) & ~size_t{3};  // Skip the terminator's padding.
    if (off > p.size()) return std::nullopt;
    out.push_back(std::move(c));
  }
  return out;
}

std::vector<uint8_t> SerializeBye(const Bye& bye) {
  if (bye.ssrcs.size() > 31 || bye.reason.size() > kMaxRtcpText) return {};
  std::vector<uint8_t> b(kHeaderBytes, 0);
  for (uint32_t s : bye.ssrcs) Put(b, s, 4);
  if (!bye.reason.empty()) {
    b.push_back(static_cast<uint8_t>(bye.reason.size()));
    b.insert(b.end(), bye.reason.begin(), bye.reason.end());
  }
  if (!Finish(b, static_cast<uint8_t>(bye.ssrcs.size()), loss::kRtcpBye)) return {};
  return b;
}

std::optional<Bye> ParseBye(std::span<const uint8_t> d) {
  auto body = ParseHeader(d, loss::kRtcpBye);
  if (!body) return std::nullopt;
  std::span<const uint8_t> p = body->data;
  size_t need = size_t{body->count} * 4;
  if (p.size() < need) return std::nullopt;
  Bye out;
  for (size_t off = 0; off < need; off += 4) out.ssrcs.push_back(Get(&p[off], 4));
  if (p.size() > need) {
    size_t len = p[need];
    if (p.size() - need - 1 < len) return std::nullopt;
    out.reason.assign(p.begin() + static_cast<std::ptrdiff_t>(need + 1),
                      p.begin() + static_cast<std::ptrdiff_t>(need + 1 + len));
  }
  return out;
}

}  // namespace sm::rtp
