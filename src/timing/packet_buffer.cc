#include "sm/timing/packet_buffer.h"

#include <algorithm>

namespace sm::timing {

size_t AssembledFrame::size_bytes() const {
  size_t s = 0;
  for (const auto& p : packets) s += p.payload.size();
  return s;
}

PacketBuffer::PacketBuffer(PacketBufferConfig cfg) : cfg_(cfg), size_(cfg.start_size) {}

// A slot is taken when another unassembled packet maps to the same index
// (`seq % size_`), PacketBuffer::InsertPacket's `buffer_[index] != nullptr`.
bool PacketBuffer::SlotTaken(int64_t seq) const {
  const int64_t n = static_cast<int64_t>(size_);
  for (auto it = packets_.lower_bound(seq - static_cast<int64_t>(cfg_.max_size)); it != packets_.end(); ++it) {
    if (it->first > seq + static_cast<int64_t>(cfg_.max_size)) break;
    if (it->first != seq && ((it->first - seq) % n) == 0) return true;
  }
  return false;
}

void PacketBuffer::Clear() {
  packets_.clear();
  done_.clear();
  padding_.clear();
  received_padding_.clear();
  missing_.clear();
  newest_inserted_ = INT64_MIN;
  cleared_to_first_seq_ = false;
  highest_ = INT64_MIN;
  first_seq_ = INT64_MIN;
  size_ = cfg_.start_size;
}

InsertResult PacketBuffer::Insert(RtpPacket p, bool frame_start, Timestamp now, bool idr) {
  int64_t s = unwrapper_.Unwrap(p.seq);
  if (first_seq_ == INT64_MIN) {
    first_seq_ = s;
  } else if (s < first_seq_) {
    // Cleared past this packet by ClearTo: it is old, ignore it.
    if (cleared_to_first_seq_) {
      ++stats_.too_old;
      return InsertResult::kTooOld;
    }
    // Large backward jump: clear and treat this as the first packet.
    if (first_seq_ - s >= static_cast<int64_t>(cfg_.max_size / 2)) Clear();
    first_seq_ = s;
  }
  // InsertPacket: `buffer_[index]->seq_num() == seq_num` is a duplicate only
  // while the packet is buffered; after its frame is handed out the slot is
  // empty and a copy is taken again (issue #899).
  const bool copy_after_assembly = !packets_.count(s) && done_.count(s);
  const bool reject_copy = copy_after_assembly && cfg_.drop_copies_after_assembly;
  if (!packets_.count(s) && !reject_copy) {
    // The packet buffer is full: grow it, and clear it at the maximum.
    while (SlotTaken(s) && size_ < cfg_.max_size) {
      size_ = std::min(cfg_.max_size, 2 * size_);
      SM_EVENT(cfg_.events, now, "packet_buffer", "expanded", static_cast<double>(size_), s);
    }
    if (SlotTaken(s)) {
      Clear();
      ++stats_.cleared;
      SM_EVENT(cfg_.events, now, "packet_buffer", "cleared", 0, s);
      return InsertResult::kBufferCleared;
    }
  }
  if (highest_ != INT64_MIN && s + static_cast<int64_t>(cfg_.window) <= highest_) {
    ++stats_.too_old;
    return InsertResult::kTooOld;
  }
  if (packets_.count(s) || reject_copy) {
    ++stats_.duplicates;
    return InsertResult::kDuplicate;
  }
  if (copy_after_assembly) {
    ++stats_.reinserted;
    SM_EVENT(cfg_.events, now, "packet_buffer", "reinserted", 0, s);
  }
  if (highest_ != INT64_MIN && s < highest_) {
    ++stats_.reordered;
    stats_.reorder_distance.Add(static_cast<double>(highest_ - s));
  }
  highest_ = std::max(highest_, s);
  ++stats_.inserted;
  packets_.emplace(s, Entry{std::move(p), frame_start, idr, now});
  if (cfg_.missing_packets) UpdateMissingPackets(s);
  // received_padding_.erase(begin, lower_bound(seq - buffer_.size() / 4)): padding only matters
  // near the packets being inserted.
  received_padding_.erase(received_padding_.begin(), received_padding_.lower_bound(s - static_cast<int64_t>(size_ / 4)));
  padding_.erase(padding_.begin(), padding_.lower_bound(s - static_cast<int64_t>(size_ / 4)));
  FindFrames(s);
  Trim();
  return InsertResult::kInserted;
}

void PacketBuffer::ClearTo(int64_t seq) {
  if (first_seq_ == INT64_MIN || first_seq_ > seq) return;
  ++seq;
  packets_.erase(packets_.begin(), packets_.lower_bound(seq));
  padding_.erase(padding_.begin(), padding_.lower_bound(seq));
  received_padding_.erase(received_padding_.begin(), received_padding_.lower_bound(seq));
  missing_.erase(missing_.begin(), missing_.lower_bound(seq));
  first_seq_ = seq;
  cleared_to_first_seq_ = true;
}

void PacketBuffer::NotePadding(uint16_t seq) {
  int64_t s = unwrapper_.Unwrap(seq);
  // InsertPadding: the padding fills its hole, and FindFrames(seq + 1) runs.
  if (cfg_.missing_packets) UpdateMissingPackets(s);
  if (highest_ != INT64_MIN && s + static_cast<int64_t>(cfg_.window) <= highest_) return;
  highest_ = std::max(highest_, s);
  done_.insert(s);
  padding_.insert(s);
  received_padding_.insert(s);
  ++stats_.padding;
  FindFrames(s + 1);
  Trim();
}

// PacketBuffer::PotentialNewFrame: the packet at `seq` is buffered, and it
// starts a frame or follows a continuous packet of the same frame.
bool PacketBuffer::PotentialNewFrame(int64_t seq) const {
  auto it = packets_.find(seq);
  if (it == packets_.end()) return false;
  if (it->second.frame_start) return true;
  auto prev = packets_.find(seq - 1);
  if (prev == packets_.end()) return false;
  if (prev->second.packet.timestamp != it->second.packet.timestamp) return false;
  return prev->second.continuous;
}

// PacketBuffer::FindFrames: walk forward from `seq` (the packet just inserted,
// or the one after a padding packet) for as long as the packets are
// continuous, skipping received padding, and hand out each frame whose last
// packet is reached. The walk stops at the first packet that is missing or not
// continuous, which includes a packet already handed out in a frame (its slot
// is empty in libwebrtc), and at a frame the `missing_packets` rule holds.
void PacketBuffer::FindFrames(int64_t seq) {
  const int64_t start = seq;
  for (size_t i = 0; i < size_; ++i) {
    if (received_padding_.count(seq)) {
      ++seq;
      continue;
    }
    if (!PotentialNewFrame(seq)) break;
    Entry& e = packets_.at(seq);
    e.continuous = true;
    if (e.packet.marker && !AssembleFrame(start, seq)) return;
    ++seq;
  }
}

// The frame that ends at `last`, a continuous packet with the marker bit. For
// H.264 without a generic descriptor FindFrames walks back as long as the
// previous packet is buffered and has the same RTP timestamp ("we don't have a
// frame_begin bit ... this may cause the PacketBuffer to hand out incomplete
// frames", packet_buffer.cc): it goes past start packets (a frame can open
// with the parameter sets, then the IDR slice) and does not look at the marker
// bit, so two frames with one RTP timestamp go out as one. The walk stops at
// the first missing packet, so an IDR slice whose parameter-set packet was
// lost is handed out from there (issue #847). Returns false when the
// `missing_packets` rule holds the frame, which ends FindFrames' walk.
bool PacketBuffer::AssembleFrame(int64_t start, int64_t last) {
  const uint32_t ts = packets_.at(last).packet.timestamp;
  int64_t first = last;
  for (;;) {
    auto prev = packets_.find(first - 1);
    if (prev == packets_.end() || prev->second.packet.timestamp != ts) break;
    --first;
  }
  // FindFrames: an H.264 frame without an IDR slice is handed out only when
  // no sequence number at or before its first packet is missing, so a delta
  // frame never leaves ahead of a hole (packet_buffer.cc, "make sure there are
  // no gaps in the packet sequence numbers up until this point").
  if (cfg_.missing_packets) {
    bool idr = false;
    for (int64_t i = first; i <= last; ++i) idr = idr || packets_.at(i).idr;
    if (!idr && missing_.upper_bound(first) != missing_.begin()) {
      ++stats_.frames_held;
      SM_EVENT(cfg_.events, packets_.at(last).arrival, "packet_buffer", "frame_held",
               static_cast<double>(*missing_.begin()), first);
      return false;
    }
    missing_.erase(missing_.begin(), missing_.upper_bound(last));
  }
  AssembledFrame f;
  f.first_seq = first;
  f.last_seq = last;
  f.timestamp = ts;
  f.last_arrival = Timestamp::MinusInfinity();
  for (int64_t i = first; i <= last; ++i) {
    auto it = packets_.find(i);
    f.last_arrival = std::max(f.last_arrival, it->second.arrival);
    f.packets.push_back(std::move(it->second.packet));
    packets_.erase(it);
    done_.insert(i);
  }
  // A found frame drops the padding from where the walk started through its last packet.
  received_padding_.erase(received_padding_.lower_bound(start), received_padding_.upper_bound(last));
  ++stats_.frames_assembled;
  SM_EVENT(cfg_.events, f.last_arrival, "packet_buffer", "frame", static_cast<double>(last - first + 1), first);
  ready_.push_back(std::move(f));
  return true;
}

// PacketBuffer::UpdateMissingPackets, with its kMaxPaddingAge of 1000.
void PacketBuffer::UpdateMissingPackets(int64_t seq) {
  constexpr int64_t kMaxPaddingAge = 1000;
  if (newest_inserted_ == INT64_MIN) newest_inserted_ = seq;
  if (seq > newest_inserted_) {
    const int64_t old_seq_number = seq - kMaxPaddingAge;
    missing_.erase(missing_.begin(), missing_.lower_bound(old_seq_number));
    // Guard against inserting a large amount of missing packets if there is a
    // jump in the sequence number.
    if (old_seq_number > newest_inserted_) newest_inserted_ = old_seq_number;
    ++newest_inserted_;
    while (seq > newest_inserted_) {
      missing_.insert(newest_inserted_);
      ++newest_inserted_;
    }
  } else {
    missing_.erase(seq);
  }
}

void PacketBuffer::Trim() {
  const int64_t oldest = highest_ - static_cast<int64_t>(cfg_.window);
  while (!packets_.empty() && packets_.begin()->first <= oldest) packets_.erase(packets_.begin());
  while (!done_.empty() && *done_.begin() <= oldest) done_.erase(done_.begin());
  while (!padding_.empty() && *padding_.begin() <= oldest) padding_.erase(padding_.begin());
  while (!received_padding_.empty() && *received_padding_.begin() <= oldest)
    received_padding_.erase(received_padding_.begin());
}

int64_t PacketBuffer::PrecedingMediaSeq(int64_t first_seq) const {
  int64_t s = first_seq - 1;
  while (padding_.count(s)) --s;
  return s;
}

std::vector<AssembledFrame> PacketBuffer::PopCompleteFrames() {
  std::vector<AssembledFrame> r;
  r.swap(ready_);
  return r;
}

void PacketBuffer::ExportMetrics(MetricsWriter& w) const {
  w.Counter("inserted", stats_.inserted);
  w.Counter("duplicates", stats_.duplicates);
  w.Counter("reinserted", stats_.reinserted);
  w.Counter("too_old", stats_.too_old);
  w.Counter("padding", stats_.padding);
  w.Counter("cleared", stats_.cleared);
  w.Counter("reordered", stats_.reordered);
  w.Counter("frames_assembled", stats_.frames_assembled);
  w.Counter("frames_held", stats_.frames_held);
  w.Gauge("buffered_packets", static_cast<double>(packets_.size()));
  w.Hist("reorder_distance", stats_.reorder_distance);
}

}  // namespace sm::timing
