// Types shared by the pacer versions (cc/pacer.h).
#pragma once

#include <array>
#include <cstdint>

#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::cc {

// The queue level in the libwebrtc port is derived from the priority: kFec
// shares the video level.
enum class PacketPriority : int { kAudio = 0, kRetransmission = 1, kVideo = 2, kFec = 3, kPadding = 4 };

struct PacedPacket {
  uint64_t id = 0;  // Caller's handle.
  PacketPriority priority = PacketPriority::kVideo;
  DataSize size;
  Timestamp enqueue_time;
  int probe_cluster = -1;  // Set by the pacer when sent as part of a probe.
  bool is_padding = false;
};

struct PacerStats {
  std::array<uint64_t, 5> sent_packets{};
  std::array<int64_t, 5> sent_bytes{};
  uint64_t probe_packets = 0;
  uint64_t drain_boosts = 0;  // Times the queue raised the rate above the pacing rate.
  uint64_t keepalives = 0;       // v2.
  uint64_t circuit_breaks = 0;   // v2.
  std::array<Histogram, 5> queue_delay_ms;
};

}  // namespace sm::cc
