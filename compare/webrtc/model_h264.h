// The H.264 access units libwebrtc's side of sm_compare sends (issue #339):
// the ones this library's sender makes for the same frame size
// (rtp::MakeFakeH264Frame). Its SPS and PPS are valid parameter sets
// (libwebrtc's depacketizer drops a packet whose SPS or PPS fails to parse,
// video_rtp_depacketizer_h264.cc), so both stacks put the same packets on the
// wire, and both senders rewrite the SPS VUI the same way (#342).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sm/rtp/fake_h264.h"

namespace sm::compare {

// The Annex-B access unit of a frame of `bytes`: [SPS, PPS, IDR slice] for a
// keyframe, one non-IDR slice otherwise.
inline std::vector<uint8_t> ModelAccessUnit(bool key, size_t bytes) { return sm::rtp::MakeFakeH264Frame(key, bytes); }

}  // namespace sm::compare
