#include "sm/timing/packet_buffer.h"

#include <algorithm>

namespace sm::timing {

size_t AssembledFrame::size_bytes() const {
  size_t s = 0;
  for (const auto& p : packets) s += p.payload.size();
  return s;
}

PacketBuffer::PacketBuffer(PacketBufferConfig cfg) : cfg_(cfg) {}

InsertResult PacketBuffer::Insert(RtpPacket p, bool frame_start, Timestamp now) {
  int64_t s = unwrapper_.Unwrap(p.seq);
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
  highest_ = std::max(highest_, s);
  ++stats_.inserted;
  packets_.emplace(s, Entry{std::move(p), frame_start, now});
  TryAssemble(s);
  Trim();
  return InsertResult::kInserted;
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
  w.Counter("reordered", stats_.reordered);
  w.Counter("frames_assembled", stats_.frames_assembled);
  w.Gauge("buffered_packets", static_cast<double>(packets_.size()));
  w.Hist("reorder_distance", stats_.reorder_distance);
}

}  // namespace sm::timing
