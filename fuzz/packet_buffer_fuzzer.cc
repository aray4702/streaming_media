// The video packet buffer, as libwebrtc's test/fuzzers/packet_buffer_fuzzer.cc
// drives video_coding::PacketBuffer: a sequence of packets with fuzzed
// sequence numbers, timestamps, marker and frame-start bits goes in, complete
// frames come out. Each record is [seq:2][ts:4][flags:1] then a
// length-prefixed H.264 payload; the frame start and keyframe flag come from
// the depacketizer, as the receiver derives them, unless flags bit 2 asks
// for the raw bits (so starts the depacketizer would never set are reached).
#include <cstddef>
#include <cstdint>
#include <span>

#include "fuzz_data_reader.h"
#include "sm/core/rtp_packet.h"
#include "sm/rtp/h264.h"
#include "sm/timing/packet_buffer.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  sm::fuzz::FuzzDataReader r(std::span<const uint8_t>(data, size));
  // Small sizes, so growth, a full buffer and a clear are reached quickly.
  sm::timing::PacketBufferConfig cfg;
  cfg.start_size = 16;
  cfg.max_size = 64;
  cfg.window = 64;
  sm::timing::PacketBuffer buffer(cfg);
  int64_t now_ms = 0;
  size_t packets = 0;
  while (r.BytesLeft() > 0 && packets++ < 4096) {
    sm::RtpPacket p;
    p.seq = r.ReadU16();
    p.timestamp = r.ReadU32();
    const uint8_t flags = r.ReadU8();
    p.marker = (flags & 1) != 0;
    auto payload = r.ReadLengthPrefixed();
    p.payload.assign(payload.begin(), payload.end());
    bool start = (flags & 2) != 0;
    bool idr = (flags & 8) != 0;
    if ((flags & 4) == 0) {
      const auto info = sm::rtp::H264Depacketizer::Parse(p.payload);
      if (!info.valid) continue;
      start = info.first_packet_in_frame;
      idr = info.idr;
    }
    now_ms += flags >> 4;
    if (payload.empty() && (flags & 4) != 0) {
      buffer.NotePadding(p.seq);
    } else {
      (void)buffer.Insert(std::move(p), start, sm::Timestamp::Millis(now_ms), idr);
    }
    for (const auto& f : buffer.PopCompleteFrames()) {
      (void)f.size_bytes();
      if ((flags & 0x10) != 0) buffer.ClearTo(f.last_seq);
    }
  }
  return 0;
}
