#include "sm/rtp/sps_vui_rewriter.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

#include "sm/rtp/h264.h"

namespace sm::rtp {

namespace {

// sps_vui_rewriter.cc: "The maximum expected growth from adding a VUI to the
// SPS. It's actually closer to 24 or so, but better safe than sorry."
constexpr size_t kMaxVuiSpsIncrease = 64;

// rtc_base/bitstream_reader.h: reads past the end, or a failed copy, make the
// reader invalid, and every later read returns zero.
class BitReader {
 public:
  explicit BitReader(std::span<const uint8_t> data) : data_(data), remaining_(static_cast<int64_t>(data.size()) * 8) {}

  uint64_t ReadBits(int bits) {
    if (bits < 0 || bits > 64 || remaining_ < bits) {
      Invalidate();
      return 0;
    }
    uint64_t v = 0;
    for (int i = 0; i < bits; ++i) {
      const size_t pos = data_.size() * 8 - static_cast<size_t>(remaining_);
      v = (v << 1) | ((data_[pos / 8] >> (7 - pos % 8)) & 1u);
      --remaining_;
    }
    return v;
  }
  uint32_t ReadBit() { return static_cast<uint32_t>(ReadBits(1)); }
  uint8_t ReadUInt8() { return static_cast<uint8_t>(ReadBits(8)); }
  // ue(v), H.264 9.1. More than 31 leading zeros does not fit and fails.
  uint32_t ReadExponentialGolomb() {
    int zeros = 0;
    while (Ok() && ReadBit() == 0) {
      if (++zeros > 31) {
        Invalidate();
        return 0;
      }
    }
    if (!Ok()) return 0;
    const uint64_t v = (uint64_t{1} << zeros) - 1 + ReadBits(zeros);
    return Ok() ? static_cast<uint32_t>(v) : 0;
  }
  // se(v), H.264 9.1.1.
  int32_t ReadSignedExponentialGolomb() {
    const uint32_t k = ReadExponentialGolomb();
    return (k & 1) ? static_cast<int32_t>((k + 1) / 2) : -static_cast<int32_t>(k / 2);
  }
  int RemainingBitCount() const { return static_cast<int>(remaining_); }
  bool Ok() const { return remaining_ >= 0; }
  void Invalidate() { remaining_ = -1; }

 private:
  std::span<const uint8_t> data_;
  int64_t remaining_;
};

// rtc_base/bit_buffer.h BitBufferWriter: writes (overwrites) bits into a
// buffer of fixed size; a write that does not fit fails and writes nothing.
class BitWriter {
 public:
  explicit BitWriter(std::vector<uint8_t>& buf) : buf_(buf) {}

  bool WriteBits(uint64_t v, int bits) {
    if (bits < 0 || bits > 64 || RemainingBits() < static_cast<size_t>(bits)) return false;
    for (int i = bits - 1; i >= 0; --i) {
      const uint8_t mask = static_cast<uint8_t>(0x80u >> bit_);
      if ((v >> i) & 1u) {
        buf_[byte_] = static_cast<uint8_t>(buf_[byte_] | mask);
      } else {
        buf_[byte_] = static_cast<uint8_t>(buf_[byte_] & ~mask);
      }
      if (++bit_ == 8) {
        bit_ = 0;
        ++byte_;
      }
    }
    return true;
  }
  bool WriteUInt8(uint8_t v) { return WriteBits(v, 8); }
  // ue(v): val + 1 in 2 * bits(val + 1) - 1 bits.
  bool WriteExponentialGolomb(uint32_t v) {
    if (v == UINT32_MAX) return false;
    const uint64_t p = uint64_t{v} + 1;
    int len = 0;
    while ((p >> (len + 1)) != 0) ++len;
    return WriteBits(p, 2 * len + 1);
  }
  void Seek(size_t byte, size_t bit) {
    byte_ = byte;
    bit_ = bit;
  }
  size_t byte_offset() const { return byte_; }
  size_t bit_offset() const { return bit_; }

 private:
  size_t RemainingBits() const { return byte_ >= buf_.size() ? 0 : (buf_.size() - byte_) * 8 - bit_; }
  std::vector<uint8_t>& buf_;
  size_t byte_ = 0;
  size_t bit_ = 0;
};

uint8_t CopyUInt8(BitReader& source, BitWriter& destination) {
  const uint8_t tmp = source.ReadUInt8();
  if (!destination.WriteUInt8(tmp)) source.Invalidate();
  return tmp;
}

uint32_t CopyExpGolomb(BitReader& source, BitWriter& destination) {
  const uint32_t tmp = source.ReadExponentialGolomb();
  if (!destination.WriteExponentialGolomb(tmp)) source.Invalidate();
  return tmp;
}

uint32_t CopyBits(int bits, BitReader& source, BitWriter& destination) {
  const uint64_t tmp = source.ReadBits(bits);
  if (!destination.WriteBits(tmp, bits)) source.Invalidate();
  return static_cast<uint32_t>(tmp);
}

// Copies a VUI HRD parameters segment (sps_vui_rewriter.cc CopyHrdParameters).
void CopyHrdParameters(BitReader& source, BitWriter& destination) {
  // cpb_cnt_minus1: ue(v)
  const uint32_t cpb_cnt_minus1 = CopyExpGolomb(source, destination);
  // bit_rate_scale and cpb_size_scale: u(4) each
  CopyBits(8, source, destination);
  for (size_t i = 0; source.Ok() && i <= cpb_cnt_minus1; ++i) {
    // bit_rate_value_minus1 and cpb_size_value_minus1: ue(v) each
    CopyExpGolomb(source, destination);
    CopyExpGolomb(source, destination);
    // cbr_flag: u(1)
    CopyBits(1, source, destination);
  }
  // initial_cpb_removal_delay_length_minus1, cpb_removal_delay_length_minus1,
  // dpb_output_delay_length_minus1, time_offset_length: u(5) each
  CopyBits(5 * 4, source, destination);
}

// Adds a bitstream restriction VUI segment (AddBitstreamRestriction).
bool AddBitstreamRestriction(BitWriter& destination, uint32_t max_num_ref_frames) {
  // motion_vectors_over_pic_boundaries_flag: u(1). Default is 1 when not present.
  if (!destination.WriteBits(1, 1)) return false;
  // max_bytes_per_pic_denom: ue(v). Default is 2 when not present.
  if (!destination.WriteExponentialGolomb(2)) return false;
  // max_bits_per_mb_denom: ue(v). Default is 1 when not present.
  if (!destination.WriteExponentialGolomb(1)) return false;
  // log2_max_mv_length_horizontal, log2_max_mv_length_vertical: ue(v). Both default to 16.
  if (!destination.WriteExponentialGolomb(16)) return false;
  if (!destination.WriteExponentialGolomb(16)) return false;
  // max_num_reorder_frames: ue(v)
  if (!destination.WriteExponentialGolomb(0)) return false;
  // max_dec_frame_buffering: ue(v)
  return destination.WriteExponentialGolomb(max_num_ref_frames);
}

// CopyOrRewriteVideoSignalTypeInfo with color_space == nullptr: the video
// signal type is copied as is and never makes a rewrite.
bool CopyVideoSignalTypeInfo(BitReader& source, BitWriter& destination) {
  // video_signal_type_present_flag: u(1)
  const uint32_t present = CopyBits(1, source, destination);
  if (present) {
    // video_format: u(3), video_full_range_flag: u(1)
    CopyBits(4, source, destination);
    // colour_description_present_flag: u(1)
    if (CopyBits(1, source, destination)) {
      // colour_primaries, transfer_characteristics, matrix_coefficients: u(8) each
      CopyBits(24, source, destination);
    }
  }
  return source.Ok();
}

bool CopyAndRewriteVui(const SpsState& sps, BitReader& source, BitWriter& destination, SpsRewriteResult& out) {
  out = SpsRewriteResult::kVuiOk;

  // vui_parameters_present_flag: u(1)
  if (!destination.WriteBits(1, 1)) return false;

  // Add the VUI if it isn't present, and rewrite the frame reordering values
  // so no reordering is allowed.
  if (!sps.vui_params_present) {
    // aspect_ratio_info_present_flag, overscan_info_present_flag,
    // video_signal_type_present_flag (no color space given): u(1) each.
    if (!destination.WriteBits(0, 3)) return false;
    // chroma_loc_info_present_flag, timing_info_present_flag,
    // nal_hrd_parameters_present_flag, vcl_hrd_parameters_present_flag,
    // pic_struct_present_flag: u(1) each.
    if (!destination.WriteBits(0, 5)) return false;
    // bitstream_restriction_flag: u(1)
    if (!destination.WriteBits(1, 1)) return false;
    if (!AddBitstreamRestriction(destination, sps.max_num_ref_frames)) return false;
    out = SpsRewriteResult::kVuiRewritten;
  } else {
    // aspect_ratio_info_present_flag: u(1)
    if (CopyBits(1, source, destination)) {
      // aspect_ratio_idc: u(8)
      if (CopyUInt8(source, destination) == 255u) {  // Extended_SAR
        // sar_width, sar_height: u(16) each.
        CopyBits(32, source, destination);
      }
    }
    // overscan_info_present_flag: u(1)
    if (CopyBits(1, source, destination)) {
      // overscan_appropriate_flag: u(1)
      CopyBits(1, source, destination);
    }
    CopyVideoSignalTypeInfo(source, destination);
    // chroma_loc_info_present_flag: u(1)
    if (CopyBits(1, source, destination) == 1) {
      // chroma_sample_loc_type_top_field, _bottom_field: ue(v) each.
      CopyExpGolomb(source, destination);
      CopyExpGolomb(source, destination);
    }
    // timing_info_present_flag: u(1)
    if (CopyBits(1, source, destination) == 1) {
      // num_units_in_tick, time_scale: u(32) each
      CopyBits(32, source, destination);
      CopyBits(32, source, destination);
      // fixed_frame_rate_flag: u(1)
      CopyBits(1, source, destination);
    }
    // nal_hrd_parameters_present_flag: u(1)
    const uint32_t nal_hrd = CopyBits(1, source, destination);
    if (nal_hrd == 1) CopyHrdParameters(source, destination);
    // vcl_hrd_parameters_present_flag: u(1)
    const uint32_t vcl_hrd = CopyBits(1, source, destination);
    if (vcl_hrd == 1) CopyHrdParameters(source, destination);
    if (nal_hrd == 1 || vcl_hrd == 1) {
      // low_delay_hrd_flag: u(1)
      CopyBits(1, source, destination);
    }
    // pic_struct_present_flag: u(1)
    CopyBits(1, source, destination);

    // bitstream_restriction_flag: u(1)
    const uint32_t bitstream_restriction_flag = source.ReadBit();
    if (!destination.WriteBits(1, 1)) return false;
    if (bitstream_restriction_flag == 0) {
      // Adding one from scratch.
      if (!AddBitstreamRestriction(destination, sps.max_num_ref_frames)) return false;
      out = SpsRewriteResult::kVuiRewritten;
    } else {
      // Replacing. motion_vectors_over_pic_boundaries_flag: u(1)
      CopyBits(1, source, destination);
      // max_bytes_per_pic_denom, max_bits_per_mb_denom,
      // log2_max_mv_length_horizontal, log2_max_mv_length_vertical: ue(v) each
      CopyExpGolomb(source, destination);
      CopyExpGolomb(source, destination);
      CopyExpGolomb(source, destination);
      CopyExpGolomb(source, destination);
      // max_num_reorder_frames and max_dec_frame_buffering: set to 0 and
      // max_num_ref_frames; no rewrite if they already were no greater.
      const uint32_t max_num_reorder_frames = source.ReadExponentialGolomb();
      const uint32_t max_dec_frame_buffering = source.ReadExponentialGolomb();
      if (!destination.WriteExponentialGolomb(0)) return false;
      if (!destination.WriteExponentialGolomb(sps.max_num_ref_frames)) return false;
      if (max_num_reorder_frames != 0 || max_dec_frame_buffering > sps.max_num_ref_frames) {
        out = SpsRewriteResult::kVuiRewritten;
      }
    }
  }
  return source.Ok();
}

bool CopyRemainingBits(BitReader& source, BitWriter& destination) {
  // Try to get at least the destination aligned.
  if (source.RemainingBitCount() > 0 && source.RemainingBitCount() % 8 != 0) {
    CopyBits(source.RemainingBitCount() % 8, source, destination);
  }
  while (source.RemainingBitCount() > 0) {
    CopyBits(std::min(32, source.RemainingBitCount()), source, destination);
  }
  return source.Ok();
}

// scaling_list(), H.264 7.3.2.1.1.1: only read past.
void SkipScalingList(BitReader& r, int size) {
  int32_t last = 8, next = 8;
  for (int j = 0; j < size && r.Ok(); ++j) {
    if (next != 0) {
      const int32_t delta = r.ReadSignedExponentialGolomb();
      if (delta < -128 || delta > 127) {
        r.Invalidate();
        return;
      }
      next = (last + delta + 256) % 256;
    }
    last = next == 0 ? last : next;
  }
}

struct NaluIndex {
  size_t start_offset = 0;          // First byte of the start code.
  size_t payload_start_offset = 0;  // First byte of the NAL unit.
  size_t payload_size = 0;
};

// The NAL units of an Annex-B stream with their start codes (3 bytes, or 4
// with a leading zero); a trailing zero before the next start code belongs to
// that start code. Bytes before the first start code are not a unit.
std::vector<NaluIndex> FindNaluIndices(std::span<const uint8_t> b) {
  std::vector<NaluIndex> out;
  for (size_t i = 0; i + 3 <= b.size();) {
    if (b[i + 2] > 1) {
      i += 3;
    } else if (b[i + 2] == 1) {
      if (b[i] == 0 && b[i + 1] == 0) {
        NaluIndex n{i, i + 3, 0};
        if (n.start_offset > 0 && b[n.start_offset - 1] == 0) --n.start_offset;
        if (!out.empty()) out.back().payload_size = n.start_offset - out.back().payload_start_offset;
        out.push_back(n);
      }
      i += 3;
    } else {
      ++i;
    }
  }
  if (!out.empty()) out.back().payload_size = b.size() - out.back().payload_start_offset;
  return out;
}

}  // namespace

std::vector<uint8_t> ParseRbsp(std::span<const uint8_t> data) {
  std::vector<uint8_t> out;
  out.reserve(data.size());
  for (size_t i = 0; i < data.size();) {
    if (data.size() - i >= 3 && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 3) {
      out.push_back(0);
      out.push_back(0);
      i += 3;
    } else {
      out.push_back(data[i++]);
    }
  }
  return out;
}

void WriteRbsp(std::span<const uint8_t> rbsp, std::vector<uint8_t>& out) {
  int zeros = 0;
  for (uint8_t byte : rbsp) {
    if (byte <= 3 && zeros >= 2) {
      out.push_back(3);
      zeros = 0;
    }
    out.push_back(byte);
    zeros = byte == 0 ? zeros + 1 : 0;
  }
}

namespace {

std::optional<SpsState> ParseSpsUpToVui(BitReader& r) {
  SpsState s;
  s.profile_idc = r.ReadUInt8();
  // constraint_set0..5_flag and reserved_zero_2bits: u(8)
  r.ReadBits(8);
  s.level_idc = r.ReadUInt8();
  s.id = r.ReadExponentialGolomb();
  if (s.id > 31) return std::nullopt;
  uint32_t chroma_format_idc = 1;
  switch (s.profile_idc) {
    case 100: case 110: case 122: case 244: case 44: case 83: case 86: case 118: case 128: case 138:
    case 139: case 134: {
      chroma_format_idc = r.ReadExponentialGolomb();
      if (chroma_format_idc > 3) return std::nullopt;
      if (chroma_format_idc == 3) s.separate_colour_plane_flag = r.ReadBit();
      // bit_depth_luma_minus8, bit_depth_chroma_minus8: ue(v)
      r.ReadExponentialGolomb();
      r.ReadExponentialGolomb();
      // qpprime_y_zero_transform_bypass_flag: u(1)
      r.ReadBit();
      // seq_scaling_matrix_present_flag: u(1)
      if (r.ReadBit()) {
        const int lists = chroma_format_idc != 3 ? 8 : 12;
        for (int i = 0; i < lists && r.Ok(); ++i) {
          if (r.ReadBit()) SkipScalingList(r, i < 6 ? 16 : 64);
        }
      }
      break;
    }
    default:
      break;
  }
  const uint32_t log2_max_frame_num_minus4 = r.ReadExponentialGolomb();
  if (log2_max_frame_num_minus4 > 12) return std::nullopt;
  s.log2_max_frame_num = log2_max_frame_num_minus4 + 4;
  s.pic_order_cnt_type = r.ReadExponentialGolomb();
  if (s.pic_order_cnt_type == 0) {
    const uint32_t lsb_minus4 = r.ReadExponentialGolomb();
    if (lsb_minus4 > 12) return std::nullopt;
    s.log2_max_pic_order_cnt_lsb = lsb_minus4 + 4;
  } else if (s.pic_order_cnt_type == 1) {
    // delta_pic_order_always_zero_flag: u(1)
    r.ReadBit();
    // offset_for_non_ref_pic, offset_for_top_to_bottom_field: se(v)
    r.ReadSignedExponentialGolomb();
    r.ReadSignedExponentialGolomb();
    // num_ref_frames_in_pic_order_cnt_cycle: ue(v), not bounded, as
    // sps_parser.cc: the loop ends when the data does.
    const uint32_t cycle = r.ReadExponentialGolomb();
    for (uint32_t i = 0; i < cycle; ++i) {
      r.ReadSignedExponentialGolomb();  // offset_for_ref_frame[i]
      if (!r.Ok()) return std::nullopt;
    }
  } else if (s.pic_order_cnt_type > 2) {
    return std::nullopt;
  }
  s.max_num_ref_frames = r.ReadExponentialGolomb();
  // gaps_in_frame_num_value_allowed_flag: u(1)
  r.ReadBit();
  const uint32_t width_mbs_minus1 = r.ReadExponentialGolomb();
  const uint32_t height_map_units_minus1 = r.ReadExponentialGolomb();
  s.frame_mbs_only_flag = r.ReadBit();
  // mb_adaptive_frame_field_flag: u(1)
  if (!s.frame_mbs_only_flag) r.ReadBit();
  // direct_8x8_inference_flag: u(1)
  r.ReadBit();
  uint32_t crop_left = 0, crop_right = 0, crop_top = 0, crop_bottom = 0;
  // frame_cropping_flag: u(1)
  if (r.ReadBit()) {
    crop_left = r.ReadExponentialGolomb();
    crop_right = r.ReadExponentialGolomb();
    crop_top = r.ReadExponentialGolomb();
    crop_bottom = r.ReadExponentialGolomb();
  }
  s.vui_params_present = r.ReadBit() != 0;
  if (!r.Ok() || width_mbs_minus1 > 0xFFFF || height_map_units_minus1 > 0xFFFF) return std::nullopt;

  // Crop units in luma samples, H.264 7.4.2.1.1 (table 6-1 for SubWidthC/SubHeightC).
  const uint32_t array_type = s.separate_colour_plane_flag ? 0 : chroma_format_idc;
  const uint32_t sub_w = array_type == 1 || array_type == 2 ? 2 : 1;
  const uint32_t sub_h = array_type == 1 ? 2 : 1;
  const uint32_t frame_height_factor = 2 - s.frame_mbs_only_flag;
  const uint64_t w = 16ull * (width_mbs_minus1 + 1);
  const uint64_t h = 16ull * frame_height_factor * (height_map_units_minus1 + 1);
  const uint64_t cw = uint64_t{sub_w} * (crop_left + uint64_t{crop_right});
  const uint64_t ch = uint64_t{sub_h} * frame_height_factor * (crop_top + uint64_t{crop_bottom});
  if (cw >= w || ch >= h) return std::nullopt;
  s.width = static_cast<uint32_t>(w - cw);
  s.height = static_cast<uint32_t>(h - ch);
  return s;
}

}  // namespace

std::optional<SpsState> ParseSpsUpToVui(std::span<const uint8_t> rbsp) {
  BitReader r(rbsp);
  return ParseSpsUpToVui(r);
}

SpsRewriteResult ParseAndRewriteSps(std::span<const uint8_t> sps, std::vector<uint8_t>& destination, SpsState* parsed) {
  const std::vector<uint8_t> rbsp = ParseRbsp(sps);
  BitReader source(rbsp);
  const std::optional<SpsState> state = ParseSpsUpToVui(source);
  if (!state) return SpsRewriteResult::kFailure;
  if (parsed) *parsed = *state;

  std::vector<uint8_t> out(sps.size() + kMaxVuiSpsIncrease, 0);
  BitWriter writer(out);

  // Copy what the parser has read in bulk, last partial byte included.
  const size_t total_bit_offset = rbsp.size() * 8 - static_cast<size_t>(source.RemainingBitCount());
  size_t byte_offset = total_bit_offset / 8;
  size_t bit_offset = total_bit_offset % 8;
  std::memcpy(out.data(), rbsp.data(), byte_offset + (bit_offset > 0 ? 1 : 0));

  // The parser has read vui_parameters_present_flag, which is rewritten: back off a bit.
  if (bit_offset == 0) {
    --byte_offset;
    bit_offset = 7;
  } else {
    --bit_offset;
  }
  writer.Seek(byte_offset, bit_offset);

  SpsRewriteResult result;
  if (!CopyAndRewriteVui(*state, source, writer, result)) return SpsRewriteResult::kFailure;
  if (result == SpsRewriteResult::kVuiOk) return result;  // No update necessary after all.
  if (!CopyRemainingBits(source, writer)) return SpsRewriteResult::kFailure;

  // Pad up to the next byte with zero bits.
  byte_offset = writer.byte_offset();
  if (writer.bit_offset() > 0) {
    writer.WriteBits(0, static_cast<int>(8 - writer.bit_offset()));
    ++byte_offset;
  }
  out.resize(byte_offset);
  WriteRbsp(out, destination);
  return SpsRewriteResult::kVuiRewritten;
}

std::optional<PpsIds> ParsePpsIds(std::span<const uint8_t> pps) {
  const std::vector<uint8_t> rbsp = ParseRbsp(pps);
  BitReader r(rbsp);
  PpsIds ids;
  ids.pps_id = r.ReadExponentialGolomb();  // pic_parameter_set_id: ue(v)
  ids.sps_id = r.ReadExponentialGolomb();  // seq_parameter_set_id: ue(v)
  if (!r.Ok()) return std::nullopt;
  return ids;
}

std::optional<SliceHeader> ParseSliceHeader(std::span<const uint8_t> slice) {
  const std::vector<uint8_t> rbsp = ParseRbsp(slice);
  BitReader r(rbsp);
  SliceHeader h;
  h.first_mb_in_slice = r.ReadExponentialGolomb();     // first_mb_in_slice: ue(v)
  r.ReadExponentialGolomb();                           // slice_type: ue(v)
  h.pic_parameter_set_id = r.ReadExponentialGolomb();  // pic_parameter_set_id: ue(v)
  if (!r.Ok()) return std::nullopt;
  return h;
}

std::vector<uint8_t> ParseOutgoingBitstreamAndRewrite(std::span<const uint8_t> annexb, SpsRewriteStats* stats) {
  const std::vector<NaluIndex> nalus = FindNaluIndices(annexb);
  std::vector<uint8_t> out;
  out.reserve(annexb.size() + nalus.size() * kMaxVuiSpsIncrease);
  for (const NaluIndex& n : nalus) {
    const std::span<const uint8_t> start_code = annexb.subspan(n.start_offset, n.payload_start_offset - n.start_offset);
    const std::span<const uint8_t> nalu = annexb.subspan(n.payload_start_offset, n.payload_size);
    if (nalu.empty()) continue;
    const uint8_t type = nalu[0] & 0x1F;
    if (type == kH264NalSps) {
      // Rewrite the VUI to restrict reordering: decoders then output each
      // frame at once (also done on receive in libwebrtc's depacketizer).
      std::vector<uint8_t> rewritten = {nalu[0]};
      const SpsRewriteResult r = ParseAndRewriteSps(nalu.subspan(1), rewritten);
      if (stats) {
        if (r == SpsRewriteResult::kVuiRewritten) ++stats->rewritten;
        if (r == SpsRewriteResult::kVuiOk) ++stats->vui_ok;
        if (r == SpsRewriteResult::kFailure) ++stats->parse_failures;
      }
      if (r == SpsRewriteResult::kVuiRewritten) {
        out.insert(out.end(), start_code.begin(), start_code.end());
        out.insert(out.end(), rewritten.begin(), rewritten.end());
        continue;
      }
    } else if (type == kH264NalAud) {
      continue;  // Access unit delimiters are not copied.
    }
    out.insert(out.end(), start_code.begin(), start_code.end());
    out.insert(out.end(), nalu.begin(), nalu.end());
  }
  return out;
}

}  // namespace sm::rtp
