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

namespace {
// RtpPacketizer::SplitAboutEqually (rtp_format.cc).
std::vector<int> SplitAboutEqually(size_t payload_size, const PayloadSizeLimits& limits) {
  std::vector<int> result;
  const int payload_len = static_cast<int>(payload_size);
  if (limits.max_payload_len >= limits.single_packet_reduction_len + payload_len) {
    result.push_back(payload_len);
    return result;
  }
  if (limits.max_payload_len - limits.first_packet_reduction_len < 1 || limits.max_payload_len - limits.last_packet_reduction_len < 1) {
    return result;
  }
  const int total_bytes = payload_len + limits.first_packet_reduction_len + limits.last_packet_reduction_len;
  int num_packets_left = (total_bytes + limits.max_payload_len - 1) / limits.max_payload_len;
  if (num_packets_left == 1) num_packets_left = 2;
  if (payload_len < num_packets_left) return result;

  const int bytes_per_packet_base = total_bytes / num_packets_left;
  const int num_larger_packets = total_bytes % num_packets_left;
  int bytes_per_packet = bytes_per_packet_base;
  int remaining_data = payload_len;
  bool first_packet = true;
  while (remaining_data > 0) {
    if (num_packets_left == num_larger_packets) ++bytes_per_packet;
    int current = bytes_per_packet;
    if (first_packet) {
      current = current > limits.first_packet_reduction_len + 1 ? current - limits.first_packet_reduction_len : 1;
    }
    if (current > remaining_data) current = remaining_data;
    if (num_packets_left == 2 && current == remaining_data) --current;
    result.push_back(current);
    remaining_data -= current;
    --num_packets_left;
    first_packet = false;
  }
  return result;
}
}  // namespace

std::vector<std::vector<uint8_t>> H264Packetizer::Packetize(std::span<const uint8_t> annexb, size_t max_payload) {
  PayloadSizeLimits limits;
  limits.max_payload_len = static_cast<int>(std::max<size_t>(max_payload, kFuAOverhead + 1));
  return Packetize(annexb, limits);
}

std::vector<std::vector<uint8_t>> H264Packetizer::Packetize(std::span<const uint8_t> annexb, const PayloadSizeLimits& in) {
  std::vector<std::vector<uint8_t>> out;
  // PayloadSizeLimits::Sanitize.
  PayloadSizeLimits limits = in;
  limits.max_payload_len = std::max(0, limits.max_payload_len);
  limits.single_packet_reduction_len = std::clamp(limits.single_packet_reduction_len, 0, limits.max_payload_len);
  limits.first_packet_reduction_len = std::clamp(limits.first_packet_reduction_len, 0, limits.max_payload_len);
  limits.last_packet_reduction_len = std::clamp(limits.last_packet_reduction_len, 0, limits.max_payload_len);
  const auto nals = SplitAnnexB(annexb);
  const size_t count = nals.size();
  for (size_t i = 0; i < count;) {
    const auto nal = nals[i];
    int capacity = limits.max_payload_len;
    if (count == 1) {
      capacity -= limits.single_packet_reduction_len;
    } else if (i == 0) {
      capacity -= limits.first_packet_reduction_len;
    } else if (i + 1 == count) {
      capacity -= limits.last_packet_reduction_len;
    }
    if (static_cast<int>(nal.size()) > capacity) {
      // FU-A (PacketizeFuA): the NAL header's F and NRI go in the indicator,
      // its type in the FU header, and the rest of the unit is split about
      // equally, the larger fragments last.
      if (limits.max_payload_len <= static_cast<int>(kFuAOverhead)) return {};
      PayloadSizeLimits fu = limits;
      fu.max_payload_len -= static_cast<int>(kFuAOverhead);
      if (count != 1) {
        if (i == count - 1) {
          fu.single_packet_reduction_len = limits.last_packet_reduction_len;
        } else if (i == 0) {
          fu.single_packet_reduction_len = limits.first_packet_reduction_len;
        } else {
          fu.single_packet_reduction_len = 0;
        }
      }
      if (i != 0) fu.first_packet_reduction_len = 0;
      if (i != count - 1) fu.last_packet_reduction_len = 0;
      const uint8_t header = nal[0];
      const std::span<const uint8_t> rest = nal.subspan(1);
      const std::vector<int> sizes = SplitAboutEqually(rest.size(), fu);
      if (sizes.empty()) return {};
      size_t off = 0;
      for (size_t k = 0; k < sizes.size(); ++k) {
        const size_t len = static_cast<size_t>(sizes[k]);
        std::vector<uint8_t> p;
        p.reserve(len + kFuAOverhead);
        p.push_back(static_cast<uint8_t>((header & 0xE0) | kH264NalFuA));
        p.push_back(static_cast<uint8_t>((k == 0 ? 0x80 : 0) | (k + 1 == sizes.size() ? 0x40 : 0) | (header & 0x1F)));
        p.insert(p.end(), rest.begin() + static_cast<std::ptrdiff_t>(off), rest.begin() + static_cast<std::ptrdiff_t>(off + len));
        off += len;
        out.push_back(std::move(p));
      }
      ++i;
      continue;
    }
    // STAP-A (PacketizeStapA): aggregate while the next unit fits, counting
    // the reduction of the packet that would hold the first or last unit.
    const bool has_first = i == 0;
    size_t left = static_cast<size_t>(limits.max_payload_len);
    size_t headers = 0;
    size_t j = i;
    auto needed = [&](size_t k) {
      const bool has_last = k == count - 1;
      int reduction = 0;
      if (has_first && has_last) {
        reduction = limits.single_packet_reduction_len;
      } else if (has_first) {
        reduction = limits.first_packet_reduction_len;
      } else if (has_last) {
        reduction = limits.last_packet_reduction_len;
      }
      return nals[k].size() + headers + static_cast<size_t>(reduction);
    };
    while (j < count && left >= needed(j)) {
      left -= nals[j].size() + headers;
      headers = headers == 0 ? 2 + 1 + 2 : 2;
      ++j;
    }
    if (j == i) return {};  // The first unit does not fit (libwebrtc: RTC_CHECK).
    std::vector<uint8_t> p;
    if (j == i + 1) {
      p.assign(nal.begin(), nal.end());  // Single NAL unit packet.
    } else {
      uint8_t nri = 0;
      size_t total = 1;
      for (size_t k = i; k < j; ++k) {
        nri = std::max<uint8_t>(nri, nals[k][0] & 0x60);
        total += 2 + nals[k].size();
      }
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
