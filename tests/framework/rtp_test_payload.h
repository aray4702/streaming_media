// H.264 test frames for the RTP tests: the fake encoder's Annex-B access units
// packetized as RFC 6184 mode 1, and the parser that finds frames in them.
#pragma once

#include <cstdint>
#include <vector>

#include "sm/rtp/fake_h264.h"
#include "sm/rtp/h264.h"
#include "sm/rtp/rtp_receiver.h"

namespace sm::test {

// The payloads of one frame of `bytes` bytes (SPS, PPS and an IDR slice when
// `key`, else one non-IDR slice) in packets of at most `max_payload` bytes.
inline std::vector<std::vector<uint8_t>> MakeTestFrameChunks(bool key, size_t bytes, size_t max_payload) {
  return rtp::H264Packetizer::Packetize(rtp::MakeFakeH264Frame(key, bytes), max_payload);
}

using TestVideoParser = rtp::H264PayloadParser;

}  // namespace sm::test
