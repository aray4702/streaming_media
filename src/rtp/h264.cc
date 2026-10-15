#include "sm/rtp/h264.h"

#include <algorithm>

namespace sm::rtp {
namespace {
constexpr uint8_t kStartCode[4] = {0, 0, 0, 1};
constexpr size_t kFuAOverhead = 2;  // FU indicator + FU header.

bool IsSliceType(uint8_t t) { return t == kH264NalSlice || t == kH264NalIdr; }

// Whether a NAL unit begins an access unit, given its header and the byte
// after it.
bool StartsFrame(uint8_t type, std::span<const uint8_t> nal) {
  if (IsSliceType(type)) return nal.size() >= 2 && (nal[1] & 0x80) != 0;
  return type == kH264NalSei || type == kH264NalSps || type == kH264NalPps || type == kH264NalAud;
}

void Note(H264PayloadInfo& info, uint8_t type, std::span<const uint8_t> nal) {
  if (type == kH264NalIdr) info.idr = true;
  if (type == kH264NalSps) info.sps = true;
  if (type == kH264NalPps) info.pps = true;
  if (StartsFrame(type, nal)) info.first_packet_in_frame = true;
}

void AppendNal(std::vector<uint8_t>& out, std::span<const uint8_t> nal) {
  out.insert(out.end(), kStartCode, kStartCode + 4);
  out.insert(out.end(), nal.begin(), nal.end());
}
}  // namespace

std::vector<std::span<const uint8_t>> SplitAnnexB(std::span<const uint8_t> b) {
  std::vector<std::span<const uint8_t>> out;
  // Start code positions: index of the first byte after each 00 00 01.
  std::vector<std::pair<size_t, size_t>> codes;  // (start code begin, payload begin)
  for (size_t i = 0; i + 2 < b.size();) {
    if (b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1) {
      size_t begin = (i > 0 && b[i - 1] == 0) ? i - 1 : i;
      codes.emplace_back(begin, i + 3);
      i += 3;
    } else {
      ++i;
    }
  }
  if (codes.empty()) {
    if (!b.empty()) out.push_back(b);
    return out;
  }
  for (size_t k = 0; k < codes.size(); ++k) {
    size_t from = codes[k].second;
    size_t to = k + 1 < codes.size() ? codes[k + 1].first : b.size();
    if (to > from) out.push_back(b.subspan(from, to - from));
  }
  return out;
}

std::vector<std::vector<uint8_t>> H264Packetizer::Packetize(std::span<const uint8_t> annexb, size_t max_payload) {
  std::vector<std::vector<uint8_t>> out;
  max_payload = std::max<size_t>(max_payload, kFuAOverhead + 1);
  const auto nals = SplitAnnexB(annexb);
  for (size_t i = 0; i < nals.size();) {
    const auto nal = nals[i];
    if (nal.size() > max_payload) {
      // FU-A: the NAL header's F and NRI go in the indicator, its type in the
      // FU header, and the rest of the unit is split about equally.
      const uint8_t header = nal[0];
      const std::span<const uint8_t> rest = nal.subspan(1);
      const size_t room = max_payload - kFuAOverhead;
      const size_t n = (rest.size() + room - 1) / room;
      size_t off = 0;
      for (size_t k = 0; k < n; ++k) {
        size_t len = rest.size() / n + (k < rest.size() % n ? 1 : 0);
        std::vector<uint8_t> p;
        p.reserve(len + kFuAOverhead);
        p.push_back(static_cast<uint8_t>((header & 0xE0) | kH264NalFuA));
        p.push_back(static_cast<uint8_t>((k == 0 ? 0x80 : 0) | (k + 1 == n ? 0x40 : 0) | (header & 0x1F)));
        p.insert(p.end(), rest.begin() + static_cast<std::ptrdiff_t>(off), rest.begin() + static_cast<std::ptrdiff_t>(off + len));
        off += len;
        out.push_back(std::move(p));
      }
      ++i;
      continue;
    }
    // Aggregate this unit with the ones after it while the STAP-A fits.
    size_t total = 1 + 2 + nal.size();
    size_t j = i + 1;
    while (j < nals.size() && nals[j].size() <= max_payload && total + 2 + nals[j].size() <= max_payload) {
      total += 2 + nals[j].size();
      ++j;
    }
    std::vector<uint8_t> p;
    if (j == i + 1) {
      p.assign(nal.begin(), nal.end());  // Single NAL unit packet.
    } else {
      uint8_t nri = 0;
      for (size_t k = i; k < j; ++k) nri = std::max<uint8_t>(nri, nals[k][0] & 0x60);
      p.reserve(total);
      p.push_back(static_cast<uint8_t>(nri | kH264NalStapA));
      for (size_t k = i; k < j; ++k) {
        p.push_back(static_cast<uint8_t>(nals[k].size() >> 8));
        p.push_back(static_cast<uint8_t>(nals[k].size()));
        p.insert(p.end(), nals[k].begin(), nals[k].end());
      }
    }
    out.push_back(std::move(p));
    i = j;
  }
  return out;
}

H264PayloadInfo H264Depacketizer::Parse(std::span<const uint8_t> p) {
  H264PayloadInfo info;
  if (p.empty()) return info;
  const uint8_t type = p[0] & 0x1F;
  if (type == kH264NalStapA) {
    size_t off = 1;
    bool any = false;
    while (off < p.size()) {
      if (off + 2 > p.size()) return H264PayloadInfo{};
      const size_t len = (static_cast<size_t>(p[off]) << 8) | p[off + 1];
      off += 2;
      if (len == 0 || off + len > p.size()) return H264PayloadInfo{};
      const auto nal = p.subspan(off, len);
      Note(info, nal[0] & 0x1F, nal);
      off += len;
      any = true;
    }
    if (!any) return H264PayloadInfo{};
  } else if (type == kH264NalFuA) {
    if (p.size() < kFuAOverhead + 1) return H264PayloadInfo{};
    const bool start = p[1] & 0x80;
    const bool end = p[1] & 0x40;
    if (start && end) return H264PayloadInfo{};  // RFC 6184 5.8: a fragment cannot be both.
    if (p[1] & 0x20) return H264PayloadInfo{};   // The reserved bit must be 0.
    info.fu_start = start;
    info.fu_end = end;
    info.fu_continuation = !start;
    if (start) {
      // The first byte after the FU header is the first byte after the NAL header.
      const uint8_t nt = p[1] & 0x1F;
      if (nt == kH264NalIdr) info.idr = true;
      if (nt == kH264NalSps) info.sps = true;
      if (nt == kH264NalPps) info.pps = true;
      if (IsSliceType(nt) ? (p[2] & 0x80) != 0 : StartsFrame(nt, p.subspan(1))) info.first_packet_in_frame = true;
    }
  } else if (type >= 1 && type <= 23) {
    Note(info, type, p);
  } else {
    return H264PayloadInfo{};  // STAP-B, MTAP16/24, FU-B and the reserved values.
  }
  info.valid = true;
  return info;
}

bool H264Depacketizer::Reassemble(std::span<const RtpPacket> packets, std::vector<uint8_t>& out) {
  bool in_fu = false;
  for (const RtpPacket& pkt : packets) {
    const auto p = std::span<const uint8_t>(pkt.payload);
    if (p.empty()) return false;
    const uint8_t type = p[0] & 0x1F;
    if (type == kH264NalFuA) {
      if (p.size() < kFuAOverhead + 1) return false;
      const bool start = p[1] & 0x80;
      const bool end = p[1] & 0x40;
      if (start == in_fu || (start && end)) return false;  // Start inside a unit, or a fragment without its start.
      if (start) {
        out.insert(out.end(), kStartCode, kStartCode + 4);
        out.push_back(static_cast<uint8_t>((p[0] & 0xE0) | (p[1] & 0x1F)));
        in_fu = true;
      }
      out.insert(out.end(), p.begin() + kFuAOverhead, p.end());
      if (end) in_fu = false;
      continue;
    }
    if (in_fu) return false;  // A unit ended without its last fragment.
    if (type == kH264NalStapA) {
      size_t off = 1;
      while (off < p.size()) {
        if (off + 2 > p.size()) return false;
        const size_t len = (static_cast<size_t>(p[off]) << 8) | p[off + 1];
        off += 2;
        if (len == 0 || off + len > p.size()) return false;
        AppendNal(out, p.subspan(off, len));
        off += len;
      }
    } else if (type >= 1 && type <= 23) {
      AppendNal(out, p);
    } else {
      return false;
    }
  }
  return !in_fu;
}

VideoPayloadInfo H264PayloadParser::Parse(const RtpPacket& p) const {
  H264PayloadInfo h = H264Depacketizer::Parse(p.payload);
  VideoPayloadInfo i;
  i.valid = h.valid;
  i.frame_start = h.first_packet_in_frame;
  i.idr = h.idr;
  i.sps = h.sps;
  i.pps = h.pps;
  return i;
}

}  // namespace sm::rtp
