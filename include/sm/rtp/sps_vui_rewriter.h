// The outgoing H.264 SPS VUI rewrite libwebrtc applies to every keyframe
// before packetization (issue #342). Follows libwebrtc M157's
// FrameEncodeMetadataWriter::UpdateBitstream
// (video/frame_encode_metadata_writer.cc), which VideoStreamEncoder::
// AugmentEncodedImage calls, and SpsVuiRewriter
// (common_video/h264/sps_vui_rewriter.cc): each SPS gets a VUI with
// bitstream_restriction (max_num_reorder_frames 0, max_dec_frame_buffering =
// max_num_ref_frames), added when missing or rewritten when it allows
// reordering, so a decoder outputs each frame at once; access unit delimiters
// are dropped; every other NAL unit is copied.
//
// Differences from libwebrtc: there is no color space input (libwebrtc's
// `color_space` argument is the encoded image's, which is unset for the
// encoders this library models, and then the video signal type is copied as
// is), and the result histogram (WebRTC.Video.H264.SpsValid) is the count in
// SpsRewriteStats. The SPS fields up to the VUI are read as libwebrtc's
// SpsParser::ParseSpsUpToVui (common_video/h264/sps_parser.cc) and the RBSP
// escaping is h264_common.cc's ParseRbsp/WriteRbsp (#568). The receive side
// (VideoRtpDepacketizerH264, sm/rtp/h264.h) uses the same rewrite.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sm::rtp {

// The SPS fields SpsVuiRewriter reads (libwebrtc's SpsParser::SpsState, up to
// vui_parameters_present_flag).
struct SpsState {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t profile_idc = 0;
  uint32_t level_idc = 0;
  uint32_t id = 0;
  uint32_t log2_max_frame_num = 4;
  uint32_t pic_order_cnt_type = 0;
  uint32_t log2_max_pic_order_cnt_lsb = 4;
  uint32_t max_num_ref_frames = 0;
  uint32_t frame_mbs_only_flag = 1;
  uint32_t separate_colour_plane_flag = 0;
  bool vui_params_present = false;
};

// libwebrtc's H264 namespace bounds (common_video/h264/h264_common.h:38-63).
inline constexpr uint32_t kH264MaxReferenceIndex = 31;
inline constexpr uint32_t kH264MaxSpsId = 31;
inline constexpr uint32_t kH264MaxPpsId = 255;
// Level 5.2's limits (H.264 table A-1), which libwebrtc applies to every SPS.
inline constexpr int64_t kH264MaxMacroblockFrameSizeLevel52 = 36864;
inline constexpr int64_t kH264MaxMacroblockDimensionLevel52 = 543;
inline constexpr int64_t kH264MaxPixelDimensionLevel52 = kH264MaxMacroblockDimensionLevel52 * 16;

// H264::IsValidResolution (h264_common.h:56-64): both dimensions positive and
// at most 8688 pixels, and at most 36864 macroblocks in a frame.
constexpr bool IsValidH264Resolution(int64_t width, int64_t height) {
  if (width <= 0 || height <= 0 || width > kH264MaxPixelDimensionLevel52 ||
      height > kH264MaxPixelDimensionLevel52) {
    return false;
  }
  const int64_t width_in_mbs = (width + 15) / 16;
  const int64_t height_in_mbs = (height + 15) / 16;
  return width_in_mbs * height_in_mbs <= kH264MaxMacroblockFrameSizeLevel52;
}

// H.264 7.4.1: removes emulation prevention bytes (00 00 03 -> 00 00).
std::vector<uint8_t> ParseRbsp(std::span<const uint8_t> data);
// H.264 7.4.1: appends `rbsp` to `out` with an emulation prevention byte after
// every two zero bytes that precede a byte of 3 or less.
void WriteRbsp(std::span<const uint8_t> rbsp, std::vector<uint8_t>& out);

// The SPS fields up to and including vui_parameters_present_flag, from an SPS
// RBSP without its NAL header byte. Empty when it does not parse.
std::optional<SpsState> ParseSpsUpToVui(std::span<const uint8_t> rbsp);

enum class SpsRewriteResult { kFailure, kVuiOk, kVuiRewritten };

struct SpsRewriteStats {  // libwebrtc's kSentSps* histogram samples.
  uint64_t vui_ok = 0;
  uint64_t rewritten = 0;
  uint64_t parse_failures = 0;
};

// SpsVuiRewriter::ParseAndRewriteSps: `sps` is an SPS NAL unit payload without
// its header byte, still escaped. On kVuiRewritten the escaped new payload is
// appended to `destination`; otherwise `destination` is unchanged. `parsed`,
// if given, receives the SPS fields whenever they parse (kVuiOk and
// kVuiRewritten).
SpsRewriteResult ParseAndRewriteSps(std::span<const uint8_t> sps, std::vector<uint8_t>& destination,
                                    SpsState* parsed = nullptr);

// libwebrtc's PpsParser::ParsePpsIds and PpsParser::ParseSliceHeader
// (common_video/h264/pps_parser.cc:44-88), the fields of a PPS and a slice
// header that VideoRtpDepacketizerH264 reads: the leading ue(v) fields of
// H.264 7.3.2.2 (pic_parameter_set_id, seq_parameter_set_id) and 7.3.3
// (first_mb_in_slice, slice_type, pic_parameter_set_id) from the unescaped
// RBSP. Empty when the data ends first or an id is out of bounds (a PPS id
// over kH264MaxPpsId, an SPS id over kH264MaxSpsId). Both take the NAL unit
// payload without its header byte.
struct PpsIds {
  uint32_t pps_id = 0;
  uint32_t sps_id = 0;
};
std::optional<PpsIds> ParsePpsIds(std::span<const uint8_t> pps);
struct SliceHeader {
  uint32_t first_mb_in_slice = 0;
  uint32_t pic_parameter_set_id = 0;
};
std::optional<SliceHeader> ParseSliceHeader(std::span<const uint8_t> slice);

// SpsVuiRewriter::ParseOutgoingBitstreamAndRewrite on an Annex-B access unit:
// SPS units rewritten where needed, AUDs dropped, the rest copied with their
// start codes. Each SPS result is counted in `stats` if given.
std::vector<uint8_t> ParseOutgoingBitstreamAndRewrite(std::span<const uint8_t> annexb,
                                                      SpsRewriteStats* stats = nullptr);

}  // namespace sm::rtp
