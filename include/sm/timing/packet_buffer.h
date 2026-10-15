// 4.1 Packet buffer: reordering, de-duplication and frame assembly.
//
// Packets are stored by unwrapped sequence number in a bounded window. After
// each insert the buffer checks whether the packet completed a frame: a run
// of consecutive sequence numbers with the same RTP timestamp that starts
// with a frame-start packet and ends with the marker bit. (A frame may open
// with several start packets, e.g. H.264 parameter sets before the IDR slice:
// it starts at the first of the contiguous packets that share its timestamp.) Complete frames are
// returned immediately (possibly out of order; the frame scheduler orders
// them by decodability). Duplicates and packets older than the window are
// rejected and counted.
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/rtp_packet.h"
#include "sm/core/seq_num.h"
#include "sm/core/units.h"

namespace sm::timing {

enum class InsertResult { kInserted, kDuplicate, kTooOld };

struct AssembledFrame {
  int64_t first_seq = 0;  // Unwrapped.
  int64_t last_seq = 0;
  uint32_t timestamp = 0;
  std::vector<RtpPacket> packets;
  Timestamp last_arrival;
  size_t size_bytes() const;
};

struct PacketBufferConfig {
  size_t window = 2048;  // Packets of history kept for reordering and dedup.
  EventSink* events = nullptr;
};

struct PacketBufferStats {
  uint64_t inserted = 0;
  uint64_t duplicates = 0;
  uint64_t too_old = 0;
  uint64_t padding = 0;          // Padding-only packets that held a sequence number.
  uint64_t reordered = 0;        // Arrived after a higher sequence number.
  uint64_t frames_assembled = 0;
  Histogram reorder_distance;    // Packets the late packet was behind.
};

class PacketBuffer : public MetricsSource {
 public:
  explicit PacketBuffer(PacketBufferConfig cfg = {});
  // frame_start: the packetizer's "first packet of frame" flag (e.g. VP8 S
  // bit, H.264 FU-A start); the end is the RTP marker bit.
  //
  // `may_continue_frame`: the packet looks like a start but could follow
  // earlier packets of the same frame (an H.264 IDR slice after its SPS and
  // PPS). It then opens the frame only if the packet before it is known to
  // belong to the previous frame; if that one is missing, the buffer waits
  // for it (NACK recovers it) rather than hand over a frame without it.
  InsertResult Insert(RtpPacket p, bool frame_start, Timestamp now, bool may_continue_frame = false);
  // A padding-only packet occupied `seq`: it is received, there is nothing
  // to assemble, and it must not look like a hole (or a duplicate later).
  void NotePadding(uint16_t seq);
  // The sequence number just before `first_seq` that is not a padding-only
  // packet, i.e. the last packet of the preceding frame if it was received.
  int64_t PrecedingMediaSeq(int64_t first_seq) const;
  std::vector<AssembledFrame> PopCompleteFrames();
  size_t buffered_packets() const { return packets_.size(); }
  const PacketBufferStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  struct Entry {
    RtpPacket packet;
    bool frame_start;
    bool may_continue_frame;
    Timestamp arrival;
  };
  void TryAssemble(int64_t seq);
  void Trim();

  PacketBufferConfig cfg_;
  SeqNumUnwrapper<uint16_t> unwrapper_;
  std::map<int64_t, Entry> packets_;
  std::set<int64_t> padding_;  // Padding-only sequence numbers within the window.
  std::set<int64_t> done_;  // Sequence numbers already delivered in frames.
  int64_t highest_ = INT64_MIN;
  int64_t first_inserted_ = INT64_MIN;  // The first packet ever inserted has no predecessor to wait for.
  std::vector<AssembledFrame> ready_;
  PacketBufferStats stats_;
};

}  // namespace sm::timing
