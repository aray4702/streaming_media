#include "sm/loss/rtcp_feedback.h"

#include <algorithm>

namespace sm::loss {

namespace {
constexpr uint8_t kFmtNack = 1;
constexpr uint8_t kFmtPli = 1;
constexpr uint8_t kFmtFir = 4;
constexpr uint8_t kFmtAfb = 15;  // Application-layer feedback (LNTF, REMB).
constexpr size_t kHeaderBytes = 4;
constexpr size_t kCommonFeedbackBytes = 12;  // Header + sender SSRC + media SSRC.

void Put(std::vector<uint8_t>& b, uint64_t v, int bytes) {
  for (int i = bytes - 1; i >= 0; --i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
uint32_t Get(const uint8_t* p, int bytes) {
  uint32_t v = 0;
  for (int i = 0; i < bytes; ++i) v = (v << 8) | p[i];
  return v;
}

void PutCommon(std::vector<uint8_t>& b, uint8_t fmt, uint8_t pt, size_t total_bytes, uint32_t sender,
               uint32_t media) {
  b.push_back(static_cast<uint8_t>(0x80 | (fmt & 0x1f)));
  b.push_back(pt);
  Put(b, total_bytes / 4 - 1, 2);
  Put(b, sender, 4);
  Put(b, media, 4);
}

// Validates the header of a single packet and strips padding. Returns the
// bytes after the common feedback header (sender + media SSRC) as `fci`.
struct Header {
  uint32_t sender = 0, media = 0;
  std::span<const uint8_t> fci;
};
std::optional<Header> ParseCommon(std::span<const uint8_t> d, uint8_t pt, uint8_t fmt) {
  if (d.size() < kCommonFeedbackBytes) return std::nullopt;
  if ((d[0] >> 6) != 2 || d[1] != pt || (d[0] & 0x1f) != fmt) return std::nullopt;
  if (d.size() != (static_cast<size_t>(Get(&d[2], 2)) + 1) * 4) return std::nullopt;
  size_t end = d.size();
  if (d[0] & 0x20) {  // Padding: the last byte counts the padding bytes.
    size_t pad = d[end - 1];
    if (pad == 0 || pad > end - kCommonFeedbackBytes) return std::nullopt;
    end -= pad;
  }
  Header h;
  h.sender = Get(&d[4], 4);
  h.media = Get(&d[8], 4);
  h.fci = d.subspan(kCommonFeedbackBytes, end - kCommonFeedbackBytes);
  return h;
}
}  // namespace

std::vector<uint8_t> SerializeNack(const Nack& n) {
  std::vector<uint8_t> items;  // PID+BLP pairs.
  size_t count = 0;
  size_t i = 0;
  while (i < n.seqs.size() && count < kMaxNackItems) {
    uint16_t pid = n.seqs[i++];
    uint16_t blp = 0;
    while (i < n.seqs.size()) {
      uint16_t diff = static_cast<uint16_t>(n.seqs[i] - pid);
      if (diff == 0) {  // Duplicate of the item's first number.
        ++i;
        continue;
      }
      if (diff > 16) break;
      blp |= static_cast<uint16_t>(1u << (diff - 1));
      ++i;
    }
    Put(items, pid, 2);
    Put(items, blp, 2);
    ++count;
  }
  if (count == 0) return {};
  std::vector<uint8_t> b;
  b.reserve(kCommonFeedbackBytes + items.size());
  PutCommon(b, kFmtNack, kRtcpRtpfb, kCommonFeedbackBytes + items.size(), n.sender_ssrc, n.media_ssrc);
  b.insert(b.end(), items.begin(), items.end());
  return b;
}

std::vector<uint8_t> SerializePli(const Pli& p) {
  std::vector<uint8_t> b;
  PutCommon(b, kFmtPli, kRtcpPsfb, kCommonFeedbackBytes, p.sender_ssrc, p.media_ssrc);
  return b;
}

std::vector<uint8_t> SerializeFir(const Fir& f) {
  if (f.entries.empty()) return {};
  std::vector<uint8_t> b;
  PutCommon(b, kFmtFir, kRtcpPsfb, kCommonFeedbackBytes + 8 * f.entries.size(), f.sender_ssrc, 0);
  for (const FirEntry& e : f.entries) {
    Put(b, e.ssrc, 4);
    Put(b, e.seq_nr, 1);
    Put(b, 0, 3);  // Reserved.
  }
  return b;
}

std::vector<uint8_t> SerializeLossNotification(const LossNotification& l) {
  uint16_t delta = static_cast<uint16_t>(l.last_received - l.last_decoded);
  if (delta > 0x7FFF) return {};
  std::vector<uint8_t> b;
  PutCommon(b, kFmtAfb, kRtcpPsfb, kCommonFeedbackBytes + 8, l.sender_ssrc, l.media_ssrc);
  b.insert(b.end(), {'L', 'N', 'T', 'F'});
  Put(b, l.last_decoded, 2);
  Put(b, (static_cast<uint32_t>(delta) << 1) | (l.decodability_flag ? 1u : 0u), 2);
  return b;
}

std::optional<Nack> ParseNack(std::span<const uint8_t> d) {
  auto h = ParseCommon(d, kRtcpRtpfb, kFmtNack);
  if (!h || h->fci.empty() || h->fci.size() % 4 != 0) return std::nullopt;
  Nack n;
  n.sender_ssrc = h->sender;
  n.media_ssrc = h->media;
  for (size_t off = 0; off < h->fci.size(); off += 4) {
    uint16_t pid = static_cast<uint16_t>(Get(&h->fci[off], 2));
    uint16_t blp = static_cast<uint16_t>(Get(&h->fci[off + 2], 2));
    n.seqs.push_back(pid);
    for (int bit = 0; bit < 16; ++bit) {
      if (blp & (1u << bit)) n.seqs.push_back(static_cast<uint16_t>(pid + bit + 1));
    }
  }
  return n;
}

std::optional<Pli> ParsePli(std::span<const uint8_t> d) {
  auto h = ParseCommon(d, kRtcpPsfb, kFmtPli);
  if (!h || !h->fci.empty()) return std::nullopt;
  return Pli{h->sender, h->media};
}

std::optional<Fir> ParseFir(std::span<const uint8_t> d) {
  auto h = ParseCommon(d, kRtcpPsfb, kFmtFir);
  if (!h || h->fci.empty() || h->fci.size() % 8 != 0) return std::nullopt;
  Fir f;
  f.sender_ssrc = h->sender;
  for (size_t off = 0; off < h->fci.size(); off += 8) {
    f.entries.push_back(FirEntry{Get(&h->fci[off], 4), h->fci[off + 4]});
  }
  return f;
}

std::optional<LossNotification> ParseLossNotification(std::span<const uint8_t> d) {
  auto h = ParseCommon(d, kRtcpPsfb, kFmtAfb);
  if (!h || h->fci.size() != 8) return std::nullopt;
  if (h->fci[0] != 'L' || h->fci[1] != 'N' || h->fci[2] != 'T' || h->fci[3] != 'F') return std::nullopt;
  LossNotification l;
  l.sender_ssrc = h->sender;
  l.media_ssrc = h->media;
  l.last_decoded = static_cast<uint16_t>(Get(&h->fci[4], 2));
  uint32_t v = Get(&h->fci[6], 2);
  l.last_received = static_cast<uint16_t>(l.last_decoded + (v >> 1));
  l.decodability_flag = (v & 1) != 0;
  return l;
}

namespace {
RtcpType Classify(std::span<const uint8_t> pkt, uint8_t pt, uint8_t fmt) {
  switch (pt) {
    case kRtcpSr: return RtcpType::kSenderReport;
    case kRtcpRr: return RtcpType::kReceiverReport;
    case kRtcpSdes: return RtcpType::kSdes;
    case kRtcpBye: return RtcpType::kBye;
    case kRtcpXr: return RtcpType::kXr;
    case kRtcpRtpfb:
      if (fmt == kFmtNack) return RtcpType::kNack;
      if (fmt == 15) return RtcpType::kTransportCc;
      return RtcpType::kOther;
    case kRtcpPsfb:
      if (fmt == kFmtPli) return RtcpType::kPli;
      if (fmt == kFmtFir) return RtcpType::kFir;
      if (fmt == kFmtAfb && pkt.size() >= 16) {
        if (pkt[12] == 'L' && pkt[13] == 'N' && pkt[14] == 'T' && pkt[15] == 'F') return RtcpType::kLossNotification;
        if (pkt[12] == 'R' && pkt[13] == 'E' && pkt[14] == 'M' && pkt[15] == 'B') return RtcpType::kRemb;
      }
      return RtcpType::kOther;
    default: return RtcpType::kOther;
  }
}
}  // namespace

std::optional<std::vector<RtcpPacketView>> SplitCompoundRtcp(std::span<const uint8_t> d) {
  std::vector<RtcpPacketView> out;
  size_t off = 0;
  while (off < d.size()) {
    if (d.size() - off < kHeaderBytes) return std::nullopt;
    const uint8_t* p = &d[off];
    if ((p[0] >> 6) != 2) return std::nullopt;
    size_t len = (static_cast<size_t>(Get(p + 2, 2)) + 1) * 4;
    if (len > d.size() - off) return std::nullopt;
    bool last = off + len == d.size();
    if ((p[0] & 0x20) && !last) return std::nullopt;
    RtcpPacketView v;
    v.packet_type = p[1];
    v.fmt = p[0] & 0x1f;
    v.data = d.subspan(off, len);
    v.type = Classify(v.data, v.packet_type, v.fmt);
    out.push_back(v);
    off += len;
  }
  if (out.empty()) return std::nullopt;
  return out;
}

std::vector<uint8_t> BuildCompoundRtcp(std::span<const std::vector<uint8_t>> packets) {
  if (packets.empty() || packets[0].size() < kHeaderBytes) return {};
  uint8_t first = packets[0][1];
  if (first != kRtcpSr && first != kRtcpRr) return {};
  std::vector<uint8_t> out;
  for (size_t i = 0; i < packets.size(); ++i) {
    const auto& p = packets[i];
    if (p.size() < kHeaderBytes || p.size() % 4 != 0) return {};
    if ((p[0] & 0x20) && i + 1 != packets.size()) return {};
    out.insert(out.end(), p.begin(), p.end());
  }
  return out;
}

}  // namespace sm::loss
