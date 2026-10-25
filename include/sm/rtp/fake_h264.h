// A stand-in for an H.264 encoder, for the test bench, the examples and the
// tests (docs/design/rtp-session.md, section 6.3). Follows libwebrtc's
// FakeH264Encoder (test/fake_encoder.cc): the bitstream is not decodable, but
// it has the real NAL structure. A keyframe is SPS (nal type 7, 8 bytes), PPS
// (type 8, 11 bytes) and an IDR slice (type 5) with the rest of the frame; a
// delta frame is one non-IDR slice (type 1). Differences from libwebrtc's fake:
// a keyframe is made when asked for, not on a fixed 1-in-10 cycle; the SPS and
// PPS are valid parameter sets of the same sizes (the ones sm_compare's model
// encoder gives libwebrtc, issue #339), so the sender's SPS VUI rewrite (#342)
// parses them as libwebrtc's does; and the slice filler never contains a zero
// byte, so no start code or emulation-prevention concern arises and what the
// packetizer splits is what the depacketizer joins.
// The first byte after a slice header has its top bit set, which is
// first_mb_in_slice = 0, the marker the depacketizer uses to find frame starts.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sm::rtp {

// SPS: profile_idc 66 with constraint_set0-2 and level 3.1 (profile-level-id
// 42e01f), sps id 0, log2_max_frame_num_minus4 0, pic_order_cnt_type 2, one
// reference frame, 320x192 (20x12 macroblocks), frame macroblocks only, no
// cropping, no VUI. A 1280x720 SPS does not fit in 7 bytes; nothing decodes
// the frames, so the size is not used.
inline constexpr uint8_t kFakeH264Sps[7] = {0x42, 0xe0, 0x1f, 0xda, 0x05, 0x06, 0x64};
// PPS: pps id 0 and sps id 0 (the ids the slice headers' first byte, 0xAA,
// refers to), CAVLC, one slice group, QP 26, deblocking control present, then
// two flat 4x4 scaling lists (all 16), which fill it to 10 bytes.
inline constexpr uint8_t kFakeH264Pps[10] = {0xce, 0x3c, 0x61, 0x0f, 0xff, 0xe4, 0x21, 0xff, 0xfc, 0xc0};

// An Annex-B access unit of exactly max(bytes, the smallest valid frame) bytes.
inline std::vector<uint8_t> MakeFakeH264Frame(bool keyframe, size_t bytes) {
  constexpr uint8_t kStart[4] = {0, 0, 0, 1};
  std::vector<uint8_t> out;
  auto parameter_set = [&](uint8_t header, std::span<const uint8_t> body) {
    out.insert(out.end(), kStart, kStart + 4);
    out.push_back(header);
    out.insert(out.end(), body.begin(), body.end());
  };
  auto slice = [&](uint8_t header, size_t nal_bytes) {
    out.insert(out.end(), kStart, kStart + 4);
    out.push_back(header);
    for (size_t i = 1; i < nal_bytes; ++i) {
      out.push_back(i == 1 ? 0xAA : static_cast<uint8_t>(0x11 + (i * 7) % 0xE0));
    }
  };
  if (keyframe) {
    parameter_set(0x67, kFakeH264Sps);
    parameter_set(0x68, kFakeH264Pps);
    const size_t used = out.size();
    const size_t idr = bytes > used + 4 + 2 ? bytes - used - 4 : 2;
    slice(0x65, idr);
  } else {
    slice(0x41, std::max<size_t>(bytes, 4 + 2) - 4);
  }
  return out;
}

}  // namespace sm::rtp
