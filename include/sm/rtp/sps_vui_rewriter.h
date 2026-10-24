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
// SpsRewriteStats. The SPS fields up to the VUI are read as in H.264 7.3.2.1.1
// and the RBSP escaping is H.264 7.4.1 (libwebrtc's SpsParser and h264_common
// are not in compare/webrtc/reference/).
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
// appended to `destination`; otherwise `destination` is unchanged.
SpsRewriteResult ParseAndRewriteSps(std::span<const uint8_t> sps, std::vector<uint8_t>& destination);

// SpsVuiRewriter::ParseOutgoingBitstreamAndRewrite on an Annex-B access unit:
// SPS units rewritten where needed, AUDs dropped, the rest copied with their
// start codes. Each SPS result is counted in `stats` if given.
std::vector<uint8_t> ParseOutgoingBitstreamAndRewrite(std::span<const uint8_t> annexb,
                                                      SpsRewriteStats* stats = nullptr);

}  // namespace sm::rtp
