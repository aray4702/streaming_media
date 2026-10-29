#include "sm/rtp/h264.h"

#include <algorithm>
#include <optional>

#include "sm/rtp/sps_vui_rewriter.h"

namespace sm::rtp {
namespace {
constexpr uint8_t kStartCode[4] = {0, 0, 0, 1};
constexpr size_t kFuAOverhead = 2;  // FU indicator + FU header.
constexpr size_t kLengthFieldSize = 2;  // A STAP-A unit's size field.

// ParseStapA (video_rtp_depacketizer_h264.cc:44-64): the aggregated units of
// a STAP-A after its header byte; false if there are none, or a size field is
// cut off, zero or runs past the end. Calls `f(offset, unit)` only once the
// whole packet has parsed.
template <typename F>
bool ForEachStapAUnit(std::span<const uint8_t> p, F&& f) {
  if (p.size() <= 1) return false;  // "Incorrect StapA packet."
  for (int pass = 0; pass < 2; ++pass) {
    for (size_t off = 1; off < p.size();) {
      if (off + kLengthFieldSize > p.size()) return false;
      const size_t len = (static_cast<size_t>(p[off]) << 8) | p[off + 1];
      off += kLengthFieldSize;
      if (len == 0 || off + len > p.size()) return false;
      if (pass == 1) f(off, p.subspan(off, len));
      off += len;
    }
  }
  return true;
}

// VideoRtpDepacketizerH264's ProcessStapAOrSingleNalu
// (video_rtp_depacketizer_h264.cc:66-224) for a single NAL unit packet or a
// STAP-A: false where it returns nullopt (the packet is dropped). Each SPS goes
// through SpsVuiRewriter::ParseAndRewriteSps in the incoming direction; when
// one is rewritten, `rewritten` (if given) receives the new payload, built as
// libwebrtc builds its output buffer (the bytes before the last SPS that
// parsed are copied from the received payload).
bool ProcessStapAOrSingleNalu(std::span<const uint8_t> payload, H264PayloadInfo& info, std::vector<uint8_t>* rewritten) {
  const bool stap_a = (payload[0] & 0x1F) == kH264NalStapA;
  bool ok = true;
  bool modified_buffer = false;
  std::vector<uint8_t> output_buffer;
  auto process = [&](size_t unit_offset, std::span<const uint8_t> nal_unit) {
    if (!ok) return;
    const uint8_t type = nal_unit[0] & 0x1F;
    const std::span<const uint8_t> nalu_data = nal_unit.subspan(1);
    if (nalu_data.empty()) return;  // "Skipping empty NAL unit."
    VideoNaluInfo nalu;
    nalu.type = type;
    switch (type) {
      case kH264NalSps: {
        // Copy any previous data first (likely just the first header).
        output_buffer.clear();
        const size_t start_offset = unit_offset + 1;
        const size_t end_offset = start_offset + nalu_data.size();
        output_buffer.insert(output_buffer.end(), payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(start_offset));
        SpsState sps;
        const SpsRewriteResult result = ParseAndRewriteSps(nalu_data, output_buffer, &sps);
        if (result == SpsRewriteResult::kFailure) {
          ok = false;
          return;
        }
        if (result == SpsRewriteResult::kVuiRewritten) {
          if (stap_a) {
            // Rewrite the length field to the new SPS size (it counts the
            // NAL header byte too).
            const size_t length_field_offset = start_offset - (1 + kLengthFieldSize);
            const size_t rewritten_size = output_buffer.size() - start_offset + 1;
            if (rewritten_size > 0xFFFF) {
              ok = false;
              return;
            }
            output_buffer[length_field_offset] = static_cast<uint8_t>(rewritten_size >> 8);
            output_buffer[length_field_offset + 1] = static_cast<uint8_t>(rewritten_size);
          }
          output_buffer.insert(output_buffer.end(), payload.begin() + static_cast<std::ptrdiff_t>(end_offset), payload.end());
          modified_buffer = true;
        }
        nalu.sps_id = static_cast<int32_t>(sps.id);
        info.width = sps.width;
        info.height = sps.height;
        info.keyframe = true;
        info.sps = true;
        info.first_packet_in_frame = true;
        break;
      }
      case kH264NalPps: {
        const std::optional<PpsIds> ids = ParsePpsIds(nalu_data);
        if (!ids) {
          ok = false;
          return;
        }
        nalu.pps_id = static_cast<int32_t>(ids->pps_id);
        nalu.sps_id = static_cast<int32_t>(ids->sps_id);
        info.pps = true;
        info.first_packet_in_frame = true;
        break;
      }
      case kH264NalIdr:
      case kH264NalSlice: {
        if (type == kH264NalIdr) {
          info.keyframe = true;
          info.idr = true;
        }
        const std::optional<SliceHeader> slice = ParseSliceHeader(nalu_data);
        if (!slice) {
          ok = false;
          return;
        }
        nalu.pps_id = static_cast<int32_t>(slice->pic_parameter_set_id);
        if (slice->first_mb_in_slice == 0) info.first_packet_in_frame = true;
        break;
      }
      case kH264NalAud:
      case kH264NalSei:
        info.first_packet_in_frame = true;
        break;
      case kH264NalStapA:
      case kH264NalFuA:
        ok = false;  // "Unexpected STAP-A or FU-A received."
        return;
      default:
        break;  // End of sequence or stream, filler, and the other types: no ids.
    }
    info.nalus.push_back(nalu);
  };
  if (stap_a) {
    if (!ForEachStapAUnit(payload, process)) return false;  // "Incorrect StapA packet."
  } else {
    process(0, payload);
  }
  if (!ok) return false;
  if (modified_buffer && rewritten) *rewritten = std::move(output_buffer);
  return true;
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

std::vector<std::vector<uint8_t>> H264Packetizer::Packetize(std::span<const uint8_t> annexb, const PayloadSizeLimits& in,
                                                           H264PacketizationMode mode) {
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
    if (mode == H264PacketizationMode::kSingleNalUnit) {
      // PacketizeSingleNalu: a unit that does not fit its packet fails the frame.
      if (static_cast<int>(nal.size()) > capacity) return {};
      out.emplace_back(nal.begin(), nal.end());
      ++i;
      continue;
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
  if (p.empty()) return info;  // "Empty payload."
  if ((p[0] & 0x1F) != kH264NalFuA) {
    if (!ProcessStapAOrSingleNalu(p, info, nullptr)) return H264PayloadInfo{};
    info.valid = true;
    return info;
  }
  // ParseFuaNalu (video_rtp_depacketizer_h264.cc:226-290).
  if (p.size() < kFuAOverhead) return info;  // "FU-A NAL units truncated."
  const uint8_t original_nal_type = p[1] & 0x1F;
  const bool first_fragment = (p[1] & 0x80) != 0;
  info.fu_start = first_fragment;
  info.fu_end = (p[1] & 0x40) != 0;
  info.fu_continuation = !first_fragment;
  if (first_fragment) {
    VideoNaluInfo nalu;
    nalu.type = original_nal_type;
    if (original_nal_type == kH264NalIdr || original_nal_type == kH264NalSlice) {
      // A slice header that does not parse is only logged here.
      const std::optional<SliceHeader> slice = ParseSliceHeader(p.subspan(kFuAOverhead));
      if (slice) {
        nalu.pps_id = static_cast<int32_t>(slice->pic_parameter_set_id);
        info.first_packet_in_frame = slice->first_mb_in_slice == 0;
      }
    }
    info.idr = original_nal_type == kH264NalIdr;
    info.nalus = {nalu};
  }
  info.keyframe = original_nal_type == kH264NalIdr;
  info.valid = true;
  return info;
}

bool H264Depacketizer::Reassemble(std::span<const RtpPacket> packets, std::vector<uint8_t>& out) {
  // H264SpsPpsTracker::CopyAndFixBitstream (h264_sps_pps_tracker.cc:37-183)
  // on the payloads VideoRtpDepacketizerH264::Parse leaves: a start code
  // before each STAP-A unit, before a single NAL unit and before the first
  // fragment of an FU-A (with its NAL header restored), none before the other
  // fragments. Parameter sets received out of band are not supported.
  std::vector<uint8_t> rewritten;
  for (const RtpPacket& pkt : packets) {
    auto p = std::span<const uint8_t>(pkt.payload);
    if (p.empty()) return false;
    const uint8_t type = p[0] & 0x1F;
    if (type == kH264NalFuA) {
      if (p.size() < kFuAOverhead) return false;
      if (p[1] & 0x80) {
        out.insert(out.end(), kStartCode, kStartCode + 4);
        out.push_back(static_cast<uint8_t>((p[0] & 0xE0) | (p[1] & 0x1F)));
      }
      out.insert(out.end(), p.begin() + kFuAOverhead, p.end());
      continue;
    }
    H264PayloadInfo info;
    rewritten.clear();
    if (!ProcessStapAOrSingleNalu(p, info, &rewritten)) return false;
    if (!rewritten.empty()) p = rewritten;  // An SPS whose VUI was rewritten on receive.
    if (type == kH264NalStapA) {
      if (!ForEachStapAUnit(p, [&](size_t, std::span<const uint8_t> nal) { AppendNal(out, nal); })) return false;
    } else if (p.size() > 1) {
      AppendNal(out, p);
    } else {
      out.insert(out.end(), p.begin(), p.end());  // No NAL unit info, so no start code.
    }
  }
  return true;
}

VideoPayloadInfo H264PayloadParser::Parse(const RtpPacket& p) const {
  H264PayloadInfo h = H264Depacketizer::Parse(p.payload);
  VideoPayloadInfo i;
  i.valid = h.valid;
  i.frame_start = h.first_packet_in_frame;
  i.idr = h.idr;
  i.keyframe = h.keyframe;
  i.sps = h.sps;
  i.pps = h.pps;
  i.nalus = std::move(h.nalus);
  return i;
}

}  // namespace sm::rtp
