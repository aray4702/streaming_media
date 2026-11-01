// 4.1 Packet buffer: reordering, de-duplication and frame assembly.
//
// Packets are stored by unwrapped sequence number in a bounded window. After
// each insert the buffer walks forward from the packet as libwebrtc's
// `PacketBuffer::FindFrames` does (packet_buffer.cc): through packets that are
// continuous (a frame start, or the next packet of the same RTP timestamp
// after a continuous one), stopping at a missing packet or one already handed
// out, and a continuous packet with the marker bit completes a frame: the run
// of consecutive sequence numbers with its RTP timestamp. (A frame may open
// with several start packets, e.g. H.264 parameter sets before the IDR slice:
// it starts at the first of the contiguous packets that share its timestamp,
// as libwebrtc's `PacketBuffer::FindFrames` walks back for H.264 without a
// generic descriptor, packet_buffer.cc. A start whose predecessor of the same
// frame is missing opens the frame there, as libwebrtc does; issue #847. The
// walk back does not stop at a marker bit, so two frames with one RTP
// timestamp go out as one, as in libwebrtc; issue #899.)
// Complete frames are returned immediately (possibly out of order; the frame
// scheduler orders them by decodability). A copy of a packet still in the
// buffer and packets older than the window are rejected and counted. A packet
// leaves the buffer when its frame is handed out, so a copy that arrives after
// that is taken again until `ClearTo`, as libwebrtc's empty slot takes it
// (`InsertPacket`'s duplicate check compares only a buffered packet's sequence
// number): a copy of a one-packet frame is assembled a second time, and the
// frame buffer drops the repeat (`FrameBuffer::InsertFrame`); issue #899.
// `drop_copies_after_assembly` keeps the old filter for a receiver that has no
// frame buffer behind the packet buffer (`timing v1`).
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
  // Reject a copy of a packet whose frame was already handed out, as a
  // duplicate. Off by default, as in libwebrtc, whose slot is empty after
  // assembly (issue #899). A deviation for `timing v1` only, whose receive
  // path has no frame buffer to drop a frame assembled twice: RtpReceiver
  // turns it on there.
  bool drop_copies_after_assembly = false;
  EventSink* events = nullptr;
};

struct PacketBufferStats {
  uint64_t inserted = 0;
  uint64_t duplicates = 0;       // A copy of a packet still in the buffer (or, with drop_copies_after_assembly, already handed out).
  uint64_t reinserted = 0;       // A copy of a packet already handed out in a frame, taken again (its slot is empty).
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
  // frame_start: the depacketizer's `is_first_packet_in_frame` (e.g. VP8 S
  // bit; H.264: a parameter set, AUD, SEI, or a slice or FU-A start with
  // first_mb_in_slice 0); the end is the RTP marker bit.
  //
  // `idr`: the packet carries an IDR slice (or its first FU-A fragment), so a
  // frame with it is an H.264 keyframe for the `missing_packets` rule.
  InsertResult Insert(RtpPacket p, bool frame_start, Timestamp now, bool idr = false);
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
    bool idr;
    Timestamp arrival;
    bool continuous = false;  // PacketBuffer::Packet::continuous: FindFrames reached it from a frame start.
  };
  bool PotentialNewFrame(int64_t seq) const;
  void FindFrames(int64_t seq);
  bool AssembleFrame(int64_t start, int64_t last);
  void UpdateMissingPackets(int64_t seq);
  void Trim();
  bool SlotTaken(int64_t seq) const;

  PacketBufferConfig cfg_;
  SeqNumUnwrapper<uint16_t> unwrapper_;
  std::map<int64_t, Entry> packets_;
  // Padding-only sequence numbers within the window, for PrecedingMediaSeq.
  std::set<int64_t> padding_;
  // PacketBuffer::received_padding_: the padding FindFrames skips. A found
  // frame forgets it from where the walk started through its last packet, so
  // it is kept apart from `padding_`, which stands in for the reference
  // finder's own padding record.
  std::set<int64_t> received_padding_;
  std::set<int64_t> done_;  // Sequence numbers already delivered in frames (or padding).
  std::set<int64_t> missing_;  // missing_packets: not received, below newest_inserted_.
  int64_t newest_inserted_ = INT64_MIN;
  int64_t highest_ = INT64_MIN;
  size_t size_ = 0;                      // Current slot count.
  bool cleared_to_first_seq_ = false;    // ClearTo moved first_seq_; older packets are ignored.
  int64_t first_seq_ = INT64_MIN;        // Lowest sequence number seen since the last clear.
  std::vector<AssembledFrame> ready_;
  PacketBufferStats stats_;
};

}  // namespace sm::timing
