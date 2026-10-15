// A 9-byte video payload header for the RTP receiver tests: start flag, key
// flag and a 64-bit frame id, like the test bench's private header today.
#pragma once

#include <cstdint>
#include <vector>

#include "sm/rtp/rtp_receiver.h"
#include "sm/rtp/rtp_sender.h"

namespace sm::test {

inline constexpr size_t kTestVideoHeader = 9;

inline std::vector<std::vector<uint8_t>> MakeTestFrameChunks(int64_t frame_id, bool key, size_t bytes, size_t max_payload) {
  std::vector<std::vector<uint8_t>> chunks;
  bool first = true;
  for (size_t n : rtp::SplitEvenly(bytes, max_payload)) {
    std::vector<uint8_t> c(std::max(n, kTestVideoHeader), 0x55);
    c[0] = static_cast<uint8_t>((first ? 1 : 0) | (key ? 2 : 0));
    for (int i = 0; i < 8; ++i) c[1 + static_cast<size_t>(i)] = static_cast<uint8_t>(static_cast<uint64_t>(frame_id) >> (8 * i));
    chunks.push_back(std::move(c));
    first = false;
  }
  return chunks;
}

class TestVideoParser : public rtp::VideoPayloadParser {
 public:
  rtp::VideoPayloadInfo Parse(const RtpPacket& p) const override {
    rtp::VideoPayloadInfo i;
    if (p.payload.size() < kTestVideoHeader) return i;
    i.valid = true;
    i.frame_start = p.payload[0] & 1;
    i.keyframe = p.payload[0] & 2;
    uint64_t id = 0;
    for (int b = 0; b < 8; ++b) id |= static_cast<uint64_t>(p.payload[1 + static_cast<size_t>(b)]) << (8 * b);
    i.frame_id = static_cast<int64_t>(id);
    return i;
  }
};

}  // namespace sm::test
