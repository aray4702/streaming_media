// Types shared by the pacer (cc/pacer.h) and its PacingController.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

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
  // Set by the sender on video media packets (PacingController::EnqueuePacket
  // reads RtpPacketToSend::is_key_frame() and is_first_packet_of_frame()).
  bool is_key_frame = false;
  bool first_packet_of_frame = false;
  // The RTP header bytes in `size` (RtpPacketToSend::headers_size()). libwebrtc's queue counts
  // payload and padding only, plus the transport overhead (PacingController::QueueSizeData).
  DataSize header_size = DataSize::Zero();
};

// PacketRouter::GeneratePadding, as the pacer sees it: the packets that make about
// `target` of padding, each with its real wire size, `priority` kPadding and an `id` the caller
// can recognise when the pacer sends it. Empty when no padding can be made. The pacer queues
// them at the padding level; without a generator it makes fixed-size placeholders
// (PacingControllerConfig::padding_packet) that the caller fills in when they are sent.
using PaddingGenerator = std::function<std::vector<PacedPacket>(DataSize target, Timestamp now)>;

// PacketSender::SendPacket, as the pacer sees it: called with each packet (probe cluster set) as the
// send loop takes it, at `now`.
using PacketSender = std::function<void(const PacedPacket& p, Timestamp now)>;

struct PacerStats {
  std::array<uint64_t, 5> sent_packets{};
  std::array<int64_t, 5> sent_bytes{};
  uint64_t probe_packets = 0;
  uint64_t drain_boosts = 0;  // Times the queue raised the rate above the pacing rate.
  uint64_t keepalives = 0;
  uint64_t circuit_breaks = 0;
  uint64_t keyframe_flushes = 0;  // Keyframes that flushed the video queue.
  uint64_t flushed_packets = 0;   // Packets discarded by those flushes.
  std::array<Histogram, 5> queue_delay_ms;
};

}  // namespace sm::cc
