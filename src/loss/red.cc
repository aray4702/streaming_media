#include "sm/loss/red.h"

#include <algorithm>

namespace sm::loss {

std::optional<std::vector<RedBlock>> ParseRed(std::span<const uint8_t> d, uint32_t rtp_ts) {
  struct Hdr {
    uint8_t pt;
    uint32_t offset;
    size_t len;
  };
  std::vector<Hdr> hdrs;
  size_t off = 0;
  for (;;) {
    if (off >= d.size()) return std::nullopt;
    bool follow = d[off] & 0x80;
    uint8_t pt = d[off] & 0x7f;
    if (!follow) {
      hdrs.push_back({pt, 0, 0});
      off += 1;
      break;
    }
    if (off + 4 > d.size()) return std::nullopt;
    uint32_t ts_off = (uint32_t{d[off + 1]} << 6) | (d[off + 2] >> 2);
    size_t len = (size_t{d[off + 2] & 0x03u} << 8) | d[off + 3];
    hdrs.push_back({pt, ts_off, len});
    off += 4;
  }
  std::vector<RedBlock> out;
  for (size_t i = 0; i < hdrs.size(); ++i) {
    bool primary = i + 1 == hdrs.size();
    size_t len = primary ? d.size() - off : hdrs[i].len;
    if (off + len > d.size()) return std::nullopt;
    RedBlock b;
    b.payload_type = hdrs[i].pt;
    b.timestamp = rtp_ts - hdrs[i].offset;
    b.payload.assign(d.begin() + static_cast<std::ptrdiff_t>(off), d.begin() + static_cast<std::ptrdiff_t>(off + len));
    b.primary = primary;
    off += len;
    out.push_back(std::move(b));
  }
  return out;
}

std::optional<std::vector<RedBlock>> SplitRed(std::span<const uint8_t> d, uint32_t rtp_ts, uint8_t red_pt) {
  constexpr size_t kMaxRedBlocks = 32;
  constexpr size_t kRedHeaderLength = 4;
  constexpr size_t kRedLastHeaderLength = 1;
  struct Hdr {
    uint8_t pt;
    uint32_t timestamp;
    size_t len;
  };
  std::vector<Hdr> hdrs;
  size_t off = 0, sum_length = 0;
  for (bool last = false; !last;) {
    if (off >= d.size()) return std::nullopt;  // "SplitRed header too short".
    last = (d[off] & 0x80) == 0;
    Hdr h{static_cast<uint8_t>(d[off] & 0x7f), rtp_ts, 0};
    if (last) {
      sum_length += kRedLastHeaderLength;
      h.len = d.size() - sum_length;
      off += kRedLastHeaderLength;
    } else {
      if (d.size() - off < kRedHeaderLength) return std::nullopt;
      h.timestamp = rtp_ts - ((uint32_t{d[off + 1]} << 6) + ((d[off + 2] & 0xFCu) >> 2));
      h.len = (size_t{d[off + 2] & 0x03u} << 8) + d[off + 3];
      sum_length += h.len + kRedHeaderLength;
      off += kRedHeaderLength;
    }
    if (h.len > 0) hdrs.push_back(h);
  }
  // A length mismatch keeps the blocks before it, but SplitRed then returns
  // false and NetEq fails the whole packet, as it does for too many blocks.
  if (hdrs.size() > kMaxRedBlocks) return std::nullopt;
  std::vector<RedBlock> out;
  for (size_t i = 0; i < hdrs.size(); ++i) {
    if (off + hdrs[i].len > d.size()) return std::nullopt;  // "SplitRed length mismatch".
    RedBlock b;
    b.payload_type = hdrs[i].pt;
    b.timestamp = hdrs[i].timestamp;
    b.payload.assign(d.begin() + static_cast<std::ptrdiff_t>(off),
                     d.begin() + static_cast<std::ptrdiff_t>(off + hdrs[i].len));
    b.red_level = static_cast<int>(hdrs.size() - 1 - i);
    b.primary = b.red_level == 0;
    off += hdrs[i].len;
    out.insert(out.begin(), std::move(b));  // new_packets.push_front.
  }
  // CheckRedPayloads: no nested RED, and no redundant payload of another type.
  // (DTMF and comfort noise are not received here.)
  std::erase_if(out, [&](const RedBlock& b) { return b.payload_type == red_pt; });
  if (out.empty()) return std::nullopt;
  const uint8_t main_pt = out.front().payload_type;
  std::erase_if(out, [&](const RedBlock& b) { return b.payload_type != main_pt; });
  return out;
}

RedEncoder::RedEncoder(int distance, size_t max_redundant_bytes)
    : distance_(distance), max_redundant_bytes_(std::min<size_t>(max_redundant_bytes, 1023)) {}

std::vector<uint8_t> RedEncoder::Encode(uint8_t pt, uint32_t ts, std::span<const uint8_t> frame) {
  std::vector<const RedBlock*> red;
  for (const RedBlock& b : history_) {
    uint32_t offset = ts - b.timestamp;
    if (offset < (1u << 14) && b.payload.size() <= max_redundant_bytes_) red.push_back(&b);
  }
  std::vector<uint8_t> out;
  for (const RedBlock* b : red) {
    uint32_t offset = ts - b->timestamp;
    size_t len = b->payload.size();
    out.push_back(static_cast<uint8_t>(0x80 | b->payload_type));
    out.push_back(static_cast<uint8_t>(offset >> 6));
    out.push_back(static_cast<uint8_t>(((offset & 0x3f) << 2) | (len >> 8)));
    out.push_back(static_cast<uint8_t>(len));
  }
  out.push_back(pt & 0x7f);
  for (const RedBlock* b : red) {
    out.insert(out.end(), b->payload.begin(), b->payload.end());
    stats_.redundant_bytes += static_cast<int64_t>(b->payload.size());
  }
  out.insert(out.end(), frame.begin(), frame.end());
  stats_.primary_bytes += static_cast<int64_t>(frame.size());
  ++stats_.packets;

  if (distance_ > 0) {
    history_.push_back(RedBlock{pt, ts, std::vector<uint8_t>(frame.begin(), frame.end()), false});
    while (static_cast<int>(history_.size()) > distance_) history_.pop_front();
  }
  return out;
}

void RedEncoder::ExportMetrics(MetricsWriter& w) const {
  w.Counter("packets", stats_.packets);
  w.Counter("primary_bytes", static_cast<uint64_t>(stats_.primary_bytes));
  w.Counter("redundant_bytes", static_cast<uint64_t>(stats_.redundant_bytes));
  w.Gauge("overhead", stats_.primary_bytes ? static_cast<double>(stats_.redundant_bytes) / stats_.primary_bytes : 0.0);
}

std::vector<RedBlock> RedReceiver::OnPacket(std::span<const uint8_t> payload, uint32_t rtp_ts) {
  std::vector<RedBlock> out;
  auto blocks = ParseRed(payload, rtp_ts);
  ++stats_.packets;
  if (!blocks) {
    ++stats_.parse_errors;
    return out;
  }
  for (RedBlock& b : *blocks) {
    int64_t u = unwrapper_.Unwrap(b.timestamp);
    if (std::find(seen_.begin(), seen_.end(), u) != seen_.end()) continue;
    // Ignore redundancy older than anything we remember: it was played out.
    if (!b.primary && !seen_.empty() && u < *std::min_element(seen_.begin(), seen_.end())) continue;
    Count(b);
    out.push_back(std::move(b));
  }
  return out;
}

std::vector<RedBlock> RedReceiver::SplitForNetEq(std::span<const uint8_t> payload, uint32_t rtp_ts, uint8_t red_pt) {
  ++stats_.packets;
  auto blocks = SplitRed(payload, rtp_ts, red_pt);
  if (!blocks) {
    ++stats_.parse_errors;
    return {};
  }
  // The statistics as OnPacket's: each frame once, oldest first.
  for (auto b = blocks->rbegin(); b != blocks->rend(); ++b) {
    const int64_t u = unwrapper_.Unwrap(b->timestamp);
    if (std::find(seen_.begin(), seen_.end(), u) != seen_.end()) continue;
    if (!b->primary && !seen_.empty() && u < *std::min_element(seen_.begin(), seen_.end())) continue;
    Count(*b);
  }
  return std::move(*blocks);
}

void RedReceiver::Count(const RedBlock& b) {
  seen_.push_back(unwrapper_.Unwrap(b.timestamp));
  if (seen_.size() > memory_) seen_.pop_front();
  ++stats_.frames_out;
  if (!b.primary) ++stats_.recovered_frames;
}

void RedReceiver::ExportMetrics(MetricsWriter& w) const {
  w.Counter("packets", stats_.packets);
  w.Counter("frames_out", stats_.frames_out);
  w.Counter("recovered_frames", stats_.recovered_frames);
  w.Counter("parse_errors", stats_.parse_errors);
}

}  // namespace sm::loss
