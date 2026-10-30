// 3.3 RED audio redundancy (RFC 2198). Each packet carries the current
// (primary) audio frame plus copies of the previous `distance` frames, so a
// single (or burst up to `distance`) loss is repaired by the next packet at
// the cost of extra bitrate and no extra delay beyond one frame.
#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

#include "sm/core/metrics.h"
#include "sm/core/seq_num.h"

namespace sm::loss {

struct RedBlock {
  uint8_t payload_type = 0;
  uint32_t timestamp = 0;
  std::vector<uint8_t> payload;
  bool primary = false;
  int red_level = 0;  // SplitRed: 0 for the primary, 1 for the newest redundant block, and so on.
};

// Parses a RED payload. `rtp_timestamp` is the RTP timestamp of the carrying
// packet (= timestamp of the primary block). Blocks are returned oldest first.
std::optional<std::vector<RedBlock>> ParseRed(std::span<const uint8_t> payload, uint32_t rtp_timestamp);

// libwebrtc's RedPayloadSplitter::SplitRed followed by CheckRedPayloads
// (modules/audio_coding/neteq/red_payload_splitter.cc), the split NetEq
// inserts: the primary first, then the redundant blocks newest to oldest,
// each with its red_level. Empty blocks are skipped, and a block whose payload
// type differs from the first block's (or is `red_payload_type`) is removed.
// Nullopt where NetEqImpl::InsertPacketInternal fails the packet: a header
// that is too short, block lengths past the payload, more than 32 blocks, or
// nothing left.
std::optional<std::vector<RedBlock>> SplitRed(std::span<const uint8_t> payload, uint32_t rtp_timestamp,
                                              uint8_t red_payload_type);

struct RedStats {
  uint64_t packets = 0;
  int64_t primary_bytes = 0;
  int64_t redundant_bytes = 0;
  uint64_t frames_out = 0;          // Receiver: unique frames delivered.
  uint64_t recovered_frames = 0;    // Receiver: delivered from a redundant block.
  uint64_t parse_errors = 0;
};

class RedEncoder : public MetricsSource {
 public:
  // distance: number of previous frames to repeat (1-3 typical).
  explicit RedEncoder(int distance = 1, size_t max_redundant_bytes = 1023);
  std::vector<uint8_t> Encode(uint8_t payload_type, uint32_t timestamp, std::span<const uint8_t> frame);
  const RedStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  int distance_;
  size_t max_redundant_bytes_;
  std::deque<RedBlock> history_;
  RedStats stats_;
};

// Turns a stream of RED packets back into unique frames ordered as they
// become available, filling holes from redundancy.
class RedReceiver : public MetricsSource {
 public:
  explicit RedReceiver(size_t memory = 64) : memory_(memory) {}
  std::vector<RedBlock> OnPacket(std::span<const uint8_t> payload, uint32_t rtp_timestamp);
  // NetEq's split (SplitRed above): every block, in the order NetEq inserts
  // them, with no deduplication (NetEq's packet buffer and arrival history
  // handle copies). The statistics count as OnPacket does.
  std::vector<RedBlock> SplitForNetEq(std::span<const uint8_t> payload, uint32_t rtp_timestamp,
                                      uint8_t red_payload_type);
  const RedStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  size_t memory_;
  SeqNumUnwrapper<uint32_t> unwrapper_;
  std::deque<int64_t> seen_;  // Recently delivered timestamps.
  RedStats stats_;

  void Count(const RedBlock& b);
};

}  // namespace sm::loss
