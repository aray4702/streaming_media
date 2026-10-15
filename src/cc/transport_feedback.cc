#include "sm/cc/transport_feedback.h"

#include <algorithm>
#include <cmath>

namespace sm::cc {

namespace {
constexpr int64_t kTickUs = 250;
constexpr int64_t kRefUnitUs = 64000;

void Put(std::vector<uint8_t>& b, uint64_t v, int bytes) {
  for (int i = bytes - 1; i >= 0; --i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
uint64_t Get(const uint8_t* p, int bytes) {
  uint64_t v = 0;
  for (int i = 0; i < bytes; ++i) v = (v << 8) | p[i];
  return v;
}
// Symbol per packet: 0 not received, 1 small delta (1 byte), 2 large (2 bytes).
int SymbolOf(const TransportFeedback::Packet& p) {
  if (!p.received) return 0;
  return (p.delta_ticks >= 0 && p.delta_ticks <= 255) ? 1 : 2;
}
}  // namespace

std::vector<std::pair<uint16_t, Timestamp>> TransportFeedback::ArrivalTimes() const {
  std::vector<std::pair<uint16_t, Timestamp>> out;
  int64_t t = static_cast<int64_t>(reference_time) * kRefUnitUs;
  for (const Packet& p : packets) {
    if (!p.received) continue;
    t += p.delta_ticks * kTickUs;
    out.emplace_back(p.seq, Timestamp::Micros(t));
  }
  return out;
}

std::vector<uint8_t> SerializeTransportFeedback(const TransportFeedback& fb) {
  std::vector<uint8_t> b;
  b.push_back(0x80 | 15);  // V=2, FMT=15.
  b.push_back(205);        // RTPFB.
  Put(b, 0, 2);            // Length, patched below.
  Put(b, fb.sender_ssrc, 4);
  Put(b, fb.media_ssrc, 4);
  Put(b, fb.base_seq, 2);
  Put(b, fb.packets.size(), 2);
  Put(b, static_cast<uint32_t>(fb.reference_time) & 0xFFFFFF, 3);
  Put(b, fb.feedback_count, 1);
  // Packet status chunks.
  const size_t n = fb.packets.size();
  std::vector<int> sym(n);
  for (size_t i = 0; i < n; ++i) sym[i] = SymbolOf(fb.packets[i]);
  size_t i = 0;
  while (i < n) {
    size_t run = 1;
    while (i + run < n && sym[i + run] == sym[i] && run < 8191) ++run;
    if (run >= 14) {
      Put(b, (static_cast<uint32_t>(sym[i]) << 13) | run, 2);  // Run-length chunk.
      i += run;
      continue;
    }
    bool any_large = false;
    for (size_t k = i; k < std::min(n, i + 14); ++k) any_large = any_large || sym[k] == 2;
    if (!any_large) {
      uint32_t c = 0x8000;  // T=1, S=0: 14 one-bit symbols.
      for (size_t k = 0; k < 14; ++k) {
        if (i + k < n && sym[i + k]) c |= 1u << (13 - k);
      }
      Put(b, c, 2);
      i += 14;
    } else {
      uint32_t c = 0xC000;  // T=1, S=1: 7 two-bit symbols.
      for (size_t k = 0; k < 7; ++k) {
        if (i + k < n) c |= static_cast<uint32_t>(sym[i + k]) << (2 * (6 - k));
      }
      Put(b, c, 2);
      i += 7;
    }
  }
  // Receive deltas.
  for (size_t k = 0; k < n; ++k) {
    if (sym[k] == 1) Put(b, static_cast<uint64_t>(fb.packets[k].delta_ticks), 1);
    if (sym[k] == 2) Put(b, static_cast<uint16_t>(static_cast<int16_t>(fb.packets[k].delta_ticks)), 2);
  }
  while (b.size() % 4) b.push_back(0);
  size_t words = b.size() / 4 - 1;
  b[2] = static_cast<uint8_t>(words >> 8);
  b[3] = static_cast<uint8_t>(words);
  return b;
}

std::optional<TransportFeedback> ParseTransportFeedback(std::span<const uint8_t> d) {
  if (d.size() < 20 || (d[0] >> 6) != 2 || (d[0] & 0x1f) != 15 || d[1] != 205) return std::nullopt;
  size_t len = (Get(&d[2], 2) + 1) * 4;
  if (len > d.size()) return std::nullopt;
  TransportFeedback fb;
  fb.sender_ssrc = static_cast<uint32_t>(Get(&d[4], 4));
  fb.media_ssrc = static_cast<uint32_t>(Get(&d[8], 4));
  fb.base_seq = static_cast<uint16_t>(Get(&d[12], 2));
  size_t count = Get(&d[14], 2);
  uint32_t ref = static_cast<uint32_t>(Get(&d[16], 3));
  fb.reference_time = (ref & 0x800000) ? static_cast<int32_t>(ref | 0xFF000000u) : static_cast<int32_t>(ref);
  fb.feedback_count = d[19];
  size_t off = 20;
  std::vector<int> sym;
  while (sym.size() < count) {
    if (off + 2 > len) return std::nullopt;
    uint32_t c = static_cast<uint32_t>(Get(&d[off], 2));
    off += 2;
    if (!(c & 0x8000)) {
      int s = static_cast<int>((c >> 13) & 3);
      size_t run = c & 0x1FFF;
      for (size_t k = 0; k < run && sym.size() < count; ++k) sym.push_back(s);
    } else if (!(c & 0x4000)) {
      for (int k = 0; k < 14 && sym.size() < count; ++k) sym.push_back((c >> (13 - k)) & 1);
    } else {
      for (int k = 0; k < 7 && sym.size() < count; ++k) sym.push_back(static_cast<int>((c >> (2 * (6 - k))) & 3));
    }
  }
  fb.packets.resize(count);
  for (size_t k = 0; k < count; ++k) {
    auto& p = fb.packets[k];
    p.seq = static_cast<uint16_t>(fb.base_seq + k);
    if (sym[k] == 0) continue;
    p.received = true;
    if (sym[k] == 1) {
      if (off + 1 > len) return std::nullopt;
      p.delta_ticks = d[off];
      off += 1;
    } else {
      if (off + 2 > len) return std::nullopt;
      p.delta_ticks = static_cast<int16_t>(Get(&d[off], 2));
      off += 2;
    }
  }
  return fb;
}

std::vector<uint8_t> SerializeRemb(uint32_t sender_ssrc, DataRate bitrate, const std::vector<uint32_t>& ssrcs) {
  std::vector<uint8_t> b;
  b.push_back(0x80 | 15);  // FMT=15 (application layer FB).
  b.push_back(206);        // PSFB.
  size_t words = 4 + ssrcs.size();
  Put(b, words, 2);
  Put(b, sender_ssrc, 4);
  Put(b, 0, 4);  // Media SSRC unused.
  b.insert(b.end(), {'R', 'E', 'M', 'B'});
  uint64_t br = static_cast<uint64_t>(std::max<int64_t>(0, bitrate.bps()));
  uint32_t exp = 0;
  while (br >= (1u << 18)) {
    br >>= 1;
    ++exp;
  }
  b.push_back(static_cast<uint8_t>(ssrcs.size()));
  Put(b, (static_cast<uint32_t>(exp) << 18) | static_cast<uint32_t>(br), 3);
  for (uint32_t s : ssrcs) Put(b, s, 4);
  return b;
}

std::optional<std::pair<DataRate, std::vector<uint32_t>>> ParseRemb(std::span<const uint8_t> d) {
  if (d.size() < 20 || d[1] != 206 || (d[0] & 0x1f) != 15) return std::nullopt;
  if (d[12] != 'R' || d[13] != 'E' || d[14] != 'M' || d[15] != 'B') return std::nullopt;
  size_t n = d[16];
  uint32_t v = static_cast<uint32_t>(Get(&d[17], 3));
  uint64_t br = static_cast<uint64_t>(v & 0x3FFFF) << (v >> 18);
  if (d.size() < 20 + 4 * n) return std::nullopt;
  std::vector<uint32_t> ssrcs;
  for (size_t i = 0; i < n; ++i) ssrcs.push_back(static_cast<uint32_t>(Get(&d[20 + 4 * i], 4)));
  return std::make_pair(DataRate::BitsPerSec(static_cast<int64_t>(br)), ssrcs);
}

// --- TransportFeedbackBuilder ---------------------------------------------------

void TransportFeedbackBuilder::OnPacket(uint16_t seq, Timestamp arrival) {
  int64_t s = unwrapper_.Unwrap(seq);
  ++stats_.packets;
  if (next_to_report_ != INT64_MIN && s < next_to_report_) {
    ++stats_.late_after_report;
    return;
  }
  arrivals_.emplace(s, arrival);
}

std::vector<TransportFeedback> TransportFeedbackBuilder::Build() {
  std::vector<TransportFeedback> out;
  if (arrivals_.empty()) return out;
  int64_t base = next_to_report_ == INT64_MIN ? arrivals_.begin()->first : next_to_report_;
  base = std::min(base, arrivals_.begin()->first);
  const int64_t last = arrivals_.rbegin()->first;
  while (base <= last) {
    TransportFeedback fb;
    fb.sender_ssrc = sender_ssrc_;
    fb.media_ssrc = media_ssrc_;
    fb.base_seq = static_cast<uint16_t>(base);
    fb.feedback_count = fb_count_++;
    // Reference time: floor of the first arrival to 64 ms.
    auto first = arrivals_.lower_bound(base);
    int64_t ref_units = first->second.us() / kRefUnitUs;
    fb.reference_time = static_cast<int32_t>(ref_units & 0x7FFFFF);
    int64_t prev_us = ref_units * kRefUnitUs;
    int64_t s = base;
    for (; s <= last && fb.packets.size() < 0xFFFF; ++s) {
      TransportFeedback::Packet p;
      p.seq = static_cast<uint16_t>(s);
      auto it = arrivals_.find(s);
      if (it != arrivals_.end()) {
        int64_t ticks = (it->second.us() - prev_us) / kTickUs;
        if (ticks < -32768 || ticks > 32767) break;  // Start a new message.
        p.received = true;
        p.delta_ticks = ticks;
        prev_us += ticks * kTickUs;
      }
      fb.packets.push_back(p);
    }
    // Trailing not-received packets are not reported yet (they may arrive).
    while (!fb.packets.empty() && !fb.packets.back().received) fb.packets.pop_back();
    if (fb.packets.empty()) break;
    base = base + static_cast<int64_t>(fb.packets.size());
    out.push_back(std::move(fb));
    ++stats_.feedbacks;
  }
  next_to_report_ = last + 1;
  arrivals_.clear();
  return out;
}

void TransportFeedbackBuilder::ExportMetrics(MetricsWriter& w) const {
  w.Counter("packets", stats_.packets);
  w.Counter("late_after_report", stats_.late_after_report);
  w.Counter("feedbacks", stats_.feedbacks);
}

// --- SendHistory --------------------------------------------------------------------

void SendHistory::OnPacketSent(uint16_t seq, Timestamp send_time, DataSize size, int probe_cluster) {
  int64_t s = unwrapper_.Unwrap(seq);
  sent_[s] = Sent{send_time, size, probe_cluster};
  in_flight_ += size;
  // Bounded history: forget packets that will never be reported.
  while (sent_.size() > 8192) {
    if (!sent_.begin()->second.reported) in_flight_ -= sent_.begin()->second.size;
    sent_.erase(sent_.begin());
  }
}

std::vector<PacketResult> SendHistory::OnFeedback(const TransportFeedback& fb, Timestamp now) {
  std::vector<PacketResult> out;
  auto arrivals = fb.ArrivalTimes();
  size_t ai = 0;
  for (const auto& p : fb.packets) {
    int64_t s = unwrapper_.PeekUnwrap(p.seq);
    auto it = sent_.find(s);
    Timestamp arrival = Timestamp::PlusInfinity();
    if (p.received) {
      arrival = arrivals[ai++].second;
    }
    if (it == sent_.end()) continue;
    PacketResult r;
    r.seq = s;
    r.send_time = it->second.time;
    r.size = it->second.size;
    r.probe_cluster = it->second.probe;
    r.receive_time = arrival;
    if (!it->second.reported) {
      in_flight_ -= it->second.size;
      it->second.reported = true;
    }
    highest_reported_ = std::max(highest_reported_, s);
    (p.received ? acked_ : lost_)++;
    out.push_back(r);
  }
  // A feedback covers a contiguous range of sequence numbers, so a packet below
  // the highest one reported but never reported itself was in a feedback that
  // was lost: it will not be reported later, and is no longer in flight.
  for (auto it = sent_.lower_bound(forgotten_below_); it != sent_.end() && it->first < highest_reported_; ++it) {
    if (!it->second.reported) {
      in_flight_ -= it->second.size;
      it->second.reported = true;
    }
    forgotten_below_ = it->first + 1;
  }
  // Drop entries older than 60 s.
  while (!sent_.empty() && now - sent_.begin()->second.time > TimeDelta::Seconds(60)) {
    if (!sent_.begin()->second.reported) in_flight_ -= sent_.begin()->second.size;
    sent_.erase(sent_.begin());
  }
  return out;
}

void SendHistory::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("in_flight_bytes", static_cast<double>(in_flight_.bytes()));
  w.Counter("acked", acked_);
  w.Counter("lost", lost_);
}

}  // namespace sm::cc
