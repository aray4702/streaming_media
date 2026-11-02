// H.264 RTP payloads (single NAL unit, STAP-A, FU-A), as libwebrtc's
// test/fuzzers/h264_depacketizer_fuzzer.cc feeds
// VideoRtpDepacketizerH264::Parse. The input is one payload; it is also
// reassembled as a one-packet frame and, split into length-prefixed records,
// as a multi-packet frame.
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "fuzz_data_reader.h"
#include "sm/core/rtp_packet.h"
#include "sm/rtp/h264.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::span<const uint8_t> in(data, size);
  (void)sm::rtp::H264Depacketizer::Parse(in);
  std::vector<uint8_t> out;
  {
    sm::RtpPacket p;
    p.payload.assign(in.begin(), in.end());
    p.marker = true;
    (void)sm::rtp::H264Depacketizer::Reassemble(std::span<const sm::RtpPacket>(&p, 1), out);
  }
  sm::fuzz::FuzzDataReader r(in);
  std::vector<sm::RtpPacket> frame;
  uint16_t seq = 0;
  while (r.BytesLeft() > 0 && frame.size() < 64) {
    sm::RtpPacket p;
    p.seq = seq++;
    auto payload = r.ReadLengthPrefixed();
    p.payload.assign(payload.begin(), payload.end());
    (void)sm::rtp::H264Depacketizer::Parse(p.payload);
    frame.push_back(std::move(p));
  }
  if (!frame.empty()) {
    frame.back().marker = true;
    out.clear();
    (void)sm::rtp::H264Depacketizer::Reassemble(frame, out);
  }
  return 0;
}
