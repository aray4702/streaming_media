// A stand-in for an H.264 encoder, for the test bench, the examples and the
// tests (docs/design/rtp-session.md, section 6.3). Follows libwebrtc's
// FakeH264Encoder (test/fake_encoder.cc): the bitstream is not decodable, but
// it has the real NAL structure. A keyframe is SPS (nal type 7, 8 bytes), PPS
// (type 8, 11 bytes) and an IDR slice (type 5) with the rest of the frame; a
// delta frame is one non-IDR slice (type 1). Differences from libwebrtc's fake:
// a keyframe is made when asked for, not on a fixed 1-in-10 cycle, and the
// filler never contains a zero byte, so no start code or emulation-prevention
// concern arises and what the packetizer splits is what the depacketizer joins.
// The first byte after a slice header has its top bit set, which is
// first_mb_in_slice = 0, the marker the depacketizer uses to find frame starts.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sm::rtp {

// An Annex-B access unit of exactly max(bytes, the smallest valid frame) bytes.
inline std::vector<uint8_t> MakeFakeH264Frame(bool keyframe, size_t bytes) {
  constexpr uint8_t kStart[4] = {0, 0, 0, 1};
  std::vector<uint8_t> out;
  auto nal = [&](uint8_t header, size_t nal_bytes, bool slice) {
    out.insert(out.end(), kStart, kStart + 4);
    out.push_back(header);
    for (size_t i = 1; i < nal_bytes; ++i) {
      out.push_back(slice && i == 1 ? 0xAA : static_cast<uint8_t>(0x11 + (i * 7) % 0xE0));
    }
  };
  if (keyframe) {
    constexpr size_t kSps = 8, kPps = 11;
    nal(0x67, kSps, false);
    nal(0x68, kPps, false);
    const size_t used = out.size();
    const size_t idr = bytes > used + 4 + 2 ? bytes - used - 4 : 2;
    nal(0x65, idr, true);
  } else {
    nal(0x41, std::max<size_t>(bytes, 4 + 2) - 4, true);
  }
  return out;
}

}  // namespace sm::rtp
