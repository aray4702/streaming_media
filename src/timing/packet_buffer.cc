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
  cleared_to_first_seq_ = false;
  highest_ = INT64_MIN;
  first_inserted_ = INT64_MIN;
  first_seq_ = INT64_MIN;
  size_ = cfg_.start_size;
}

InsertResult PacketBuffer::Insert(RtpPacket p, bool frame_start, Timestamp now, bool may_continue_frame) {
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
  if (!packets_.count(s) && !done_.count(s)) {
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
  if (packets_.count(s) || done_.count(s)) {
    ++stats_.duplicates;
    return InsertResult::kDuplicate;
  }
  if (highest_ != INT64_MIN && s < highest_) {
    ++stats_.reordered;
    stats_.reorder_distance.Add(static_cast<double>(highest_ - s));
  }
  if (first_inserted_ == INT64_MIN) first_inserted_ = s;
  highest_ = std::max(highest_, s);
  ++stats_.inserted;
  packets_.emplace(s, Entry{std::move(p), frame_start, may_continue_frame, now});
  // received_padding_.erase(begin, lower_bound(seq - buffer_.size() / 4)): padding only matters
  // near the packets being inserted.
  padding_.erase(padding_.begin(), padding_.lower_bound(s - static_cast<int64_t>(size_ / 4)));
  TryAssemble(s);
  Trim();
  return InsertResult::kInserted;
}

void PacketBuffer::ClearTo(int64_t seq) {
  if (first_seq_ == INT64_MIN || first_seq_ > seq) return;
  ++seq;
  packets_.erase(packets_.begin(), packets_.lower_bound(seq));
  padding_.erase(padding_.begin(), padding_.lower_bound(seq));
  first_seq_ = seq;
  cleared_to_first_seq_ = true;
}

void PacketBuffer::NotePadding(uint16_t seq) {
  int64_t s = unwrapper_.Unwrap(seq);
  if (highest_ != INT64_MIN && s + static_cast<int64_t>(cfg_.window) <= highest_) return;
  highest_ = std::max(highest_, s);
  done_.insert(s);
  padding_.insert(s);
  ++stats_.padding;
  Trim();
}

void PacketBuffer::TryAssemble(int64_t seq) {
  const uint32_t ts = packets_.at(seq).packet.timestamp;
  // Walk back to the frame start. A frame can open with several packets that
  // each look like a start (H.264: the parameter sets, then the IDR slice), so
  // the walk goes on through contiguous packets of the same timestamp and
  // stops where the previous packet is missing, belongs to another frame, or
  // ended its frame with the marker bit.
  int64_t first = seq;
  for (;;) {
    const Entry& e = packets_.at(first);
    if (e.packet.timestamp != ts) return;
    auto prev = packets_.find(first - 1);
    if (prev != packets_.end() && prev->second.packet.timestamp == ts && !prev->second.packet.marker) {
      first = first - 1;
      continue;
    }
    if (!e.frame_start) return;
    // A start that may continue a frame (the IDR slice after its parameter
    // sets) opens it only if the packet before is known to belong to the
    // previous frame. If that one is missing (lost or on its way) it may be
    // the frame's own earlier packet, so wait for it: NACK recovers it, or the
    // keyframe request follows if it never comes. The first packet ever
    // inserted has nothing before it, and padding or a delivered frame ahead
    // of it settles it too.
    if (e.may_continue_frame && prev == packets_.end() && !done_.count(first - 1) && first != first_inserted_) return;
    break;
  }
  // Walk forward to the marker.
  int64_t last = seq;
  for (;;) {
    const Entry& e = packets_.at(last);
    if (e.packet.timestamp != ts) return;
    if (e.packet.marker) break;
    auto next = packets_.find(last + 1);
    if (next == packets_.end()) return;
    last = last + 1;
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
  // A found frame drops the padding from the packet that completed it through its last packet.
  padding_.erase(padding_.lower_bound(seq), padding_.upper_bound(last));
  ++stats_.frames_assembled;
  SM_EVENT(cfg_.events, f.last_arrival, "packet_buffer", "frame", static_cast<double>(last - first + 1), first);
  ready_.push_back(std::move(f));
}

void PacketBuffer::Trim() {
  const int64_t oldest = highest_ - static_cast<int64_t>(cfg_.window);
  while (!packets_.empty() && packets_.begin()->first <= oldest) packets_.erase(packets_.begin());
  while (!done_.empty() && *done_.begin() <= oldest) done_.erase(done_.begin());
  while (!padding_.empty() && *padding_.begin() <= oldest) padding_.erase(padding_.begin());
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
  w.Counter("too_old", stats_.too_old);
  w.Counter("padding", stats_.padding);
  w.Counter("cleared", stats_.cleared);
  w.Counter("reordered", stats_.reordered);
  w.Counter("frames_assembled", stats_.frames_assembled);
  w.Gauge("buffered_packets", static_cast<double>(packets_.size()));
  w.Hist("reorder_distance", stats_.reorder_distance);
}

}  // namespace sm::timing
