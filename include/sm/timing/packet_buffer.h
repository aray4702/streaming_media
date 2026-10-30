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

enum class InsertResult { kInserted, kDuplicate, kTooOld, kBufferCleared };

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
  // libwebrtc's `video_coding::PacketBuffer` sizing (packet_buffer.cc, #76):
  // the buffer starts at `start_size` slots and doubles up to `max_size` when
  // a slot is taken by another sequence number; when it is full at `max_size`
  // it is cleared and the caller asks for a keyframe (`kBufferCleared`); a
  // packet `max_size / 2` or more behind the lowest sequence number seen
  // restarts the buffer.
  size_t start_size = 512;
  size_t max_size = 2048;
  // libwebrtc's `missing_packets_` rule for H.264 without a generic
  // descriptor (packet_buffer.cc FindFrames, UpdateMissingPackets): the buffer
  // keeps the sequence numbers not yet received below the newest one (at most
  // 1000 behind it), and a complete frame without an IDR slice is held while
  // one of them is at or before its first packet. A keyframe is released, and
  // a released frame forgets the holes up to its last packet. A packet that
  // arrives later releases the held frames it makes contiguous, as FindFrames'
  // walk forward does. On by default, as libwebrtc applies it to every H.264
  // frame (issues #453, #580). The buffer does not know the codec: turn it off
  // for another codec, as rtp::Session does for streams that are not H.264.
  bool missing_packets = true;
  EventSink* events = nullptr;
};

struct PacketBufferStats {
  uint64_t inserted = 0;
  uint64_t duplicates = 0;
  uint64_t too_old = 0;
  uint64_t cleared = 0;          // The buffer was full at max_size and was cleared.
  uint64_t padding = 0;          // Padding-only packets that held a sequence number.
  uint64_t reordered = 0;        // Arrived after a higher sequence number.
  uint64_t frames_assembled = 0;
  uint64_t frames_held = 0;      // missing_packets: complete delta frames held behind a hole (each time).
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
  //
  // `idr`: the packet carries an IDR slice (or its first FU-A fragment), so a
  // frame with it is an H.264 keyframe for the `missing_packets` rule.
  InsertResult Insert(RtpPacket p, bool frame_start, Timestamp now, bool may_continue_frame = false,
                      bool idr = false);
  // A padding-only packet occupied `seq`: it is received, there is nothing
  // to assemble, and it must not look like a hole (or a duplicate later).
  void NotePadding(uint16_t seq);
  // The sequence number just before `first_seq` that is not a padding-only
  // packet, i.e. the last packet of the preceding frame if it was received.
  int64_t PrecedingMediaSeq(int64_t first_seq) const;
  // `PacketBuffer::ClearTo`: the frame ending at `seq` was decoded, so
  // every packet up to and including it is dropped, the lowest sequence number
  // moves past it, and a packet behind it that arrives later is ignored
  // (`kTooOld`) rather than treated as a backward jump.
  void ClearTo(int64_t seq);
  // `PacketBuffer::Clear`: drops every packet and the sequence-number history
  // of the window (the unwrapper keeps its state), as on a full buffer and as
  // RtpVideoStreamReceiver2 does when it re-anchors after a sequence-number
  // discontinuity.
  void Clear();
  std::vector<AssembledFrame> PopCompleteFrames();
  size_t capacity() const { return size_; }
  size_t buffered_packets() const { return packets_.size(); }
  const PacketBufferStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  struct Entry {
    RtpPacket packet;
    bool frame_start;
    bool may_continue_frame;
    bool idr;
    Timestamp arrival;
  };
  void TryAssemble(int64_t seq);
  void UpdateMissingPackets(int64_t seq);
  void ReleaseHeldFrames(int64_t seq);
  void Trim();
  bool SlotTaken(int64_t seq) const;

  PacketBufferConfig cfg_;
  SeqNumUnwrapper<uint16_t> unwrapper_;
  std::map<int64_t, Entry> packets_;
  std::set<int64_t> padding_;  // Padding-only sequence numbers within the window.
  std::set<int64_t> done_;  // Sequence numbers already delivered in frames.
  std::set<int64_t> missing_;  // missing_packets: not received, below newest_inserted_.
  int64_t newest_inserted_ = INT64_MIN;
  int64_t highest_ = INT64_MIN;
  size_t size_ = 0;                      // Current slot count.
  bool cleared_to_first_seq_ = false;    // ClearTo moved first_seq_; older packets are ignored.
  int64_t first_seq_ = INT64_MIN;        // Lowest sequence number seen since the last clear.
  int64_t first_inserted_ = INT64_MIN;  // The first packet ever inserted has no predecessor to wait for.
  std::vector<AssembledFrame> ready_;
  PacketBufferStats stats_;
};

}  // namespace sm::timing
