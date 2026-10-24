// The H.264 access units libwebrtc's side of sm_compare sends (issue #339):
// the ones this library's sender makes for the same frame size
// (rtp::MakeFakeH264Frame), with valid parameter sets in place of the
// filler, which libwebrtc's depacketizer can't parse
// (video_rtp_depacketizer_h264.cc drops a packet whose SPS or PPS fails to
// parse). The SPS and PPS have the same sizes as the filler (7 and 10 bytes
// after the NAL header), so both stacks put the same packets on the wire, and
// no zero byte, so no start code appears inside them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "sm/rtp/fake_h264.h"

namespace sm::compare {

// SPS: profile_idc 66 with constraint_set0-2 and level 3.1 (profile-level-id
// 42e01f), sps id 0, log2_max_frame_num_minus4 0, pic_order_cnt_type 2, one
// reference frame, 320x192 (20x12 macroblocks), frame macroblocks only, no
// cropping, no VUI. A 1280x720 SPS does not fit in 7 bytes; the model decoder
// ignores the size.
inline constexpr uint8_t kModelSps[7] = {0x42, 0xe0, 0x1f, 0xda, 0x05, 0x06, 0x64};
// PPS: pps id 0 and sps id 0 (the ids the slice headers' first byte, 0xAA,
// refers to), CAVLC, one slice group, QP 26, deblocking control present, then
// two flat 4x4 scaling lists (all 16), which fill it to 10 bytes.
inline constexpr uint8_t kModelPps[10] = {0xce, 0x3c, 0x61, 0x0f, 0xff, 0xe4, 0x21, 0xff, 0xfc, 0xc0};

// The Annex-B access unit of a frame of `bytes`: [SPS, PPS, IDR slice] for a
// keyframe, one non-IDR slice otherwise, exactly as large as
// rtp::MakeFakeH264Frame's.
inline std::vector<uint8_t> ModelAccessUnit(bool key, size_t bytes) {
  std::vector<uint8_t> au = sm::rtp::MakeFakeH264Frame(key, bytes);
  if (key) {
    // 00 00 00 01, 0x67, 7 bytes of SPS, 00 00 00 01, 0x68, 10 bytes of PPS, 00 00 00 01, 0x65, the slice.
    if (au.size() < 33 || au[4] != 0x67 || au[16] != 0x68 || au[31] != 0x65) std::abort();
    std::memcpy(au.data() + 5, kModelSps, sizeof(kModelSps));
    std::memcpy(au.data() + 17, kModelPps, sizeof(kModelPps));
  }
  return au;
}

}  // namespace sm::compare
