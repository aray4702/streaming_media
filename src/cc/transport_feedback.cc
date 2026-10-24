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

namespace {
// libwebrtc PacketArrivalTimeMap::kMaxNumberOfPackets and the generator's constants.
constexpr int64_t kMaxNumberOfPackets = 1 << 15;
constexpr TimeDelta kBackWindow = TimeDelta::Millis(500);
constexpr TimeDelta kMinInterval = TimeDelta::Millis(50);
constexpr TimeDelta kMaxInterval = TimeDelta::Millis(250);
constexpr int64_t kMaxMissingSequenceNumbers = 0x7FFE;
// kTwccReportSize: IPv4 20 + UDP 8 + SRTP 10 + average report 30 bytes.
constexpr int64_t kTwccReportBytes = 20 + 8 + 10 + 30;
}  // namespace

void TransportFeedbackBuilder::AddArrival(int64_t s, Timestamp arrival) {
  if (!seen_) {
    seen_ = true;
    arrivals_.clear();
    begin_ = s;
    end_ = s + 1;
  } else if (s >= begin_ && s < end_) {
    // Inside the span.
  } else if (s < begin_) {
    if (end_ - s > kMaxNumberOfPackets) return;  // Would drop newer packets.
    begin_ = s;
  } else {
    const int64_t new_end = s + 1;
    if (new_end >= end_ + kMaxNumberOfPackets) {
      arrivals_.clear();
      begin_ = s;
    } else if (begin_ < new_end - kMaxNumberOfPackets) {
      begin_ = new_end - kMaxNumberOfPackets;
      arrivals_.erase(arrivals_.begin(), arrivals_.lower_bound(begin_));
    }
    end_ = new_end;
  }
  arrivals_[s] = arrival;
}

void TransportFeedbackBuilder::RemoveOldPackets(int64_t seq, Timestamp limit) {
  const int64_t check_to = std::min(seq, end_);
  while (begin_ < check_to) {
    auto it = arrivals_.find(begin_);
    if (it != arrivals_.end() && it->second > limit) break;  // A missing slot counts as old.
    ++begin_;
  }
  arrivals_.erase(arrivals_.begin(), arrivals_.lower_bound(begin_));
}

void TransportFeedbackBuilder::OnSendBandwidthEstimateChanged(DataRate estimate) {
  // Let the reports use 5% of the send estimate; kMinTwccRate = report size / kMaxInterval.
  const int64_t twcc_bps = estimate.bps() / 20;
  const int64_t min_twcc_bps = kTwccReportBytes * 8 * 1000000 / kMaxInterval.us();
  if (twcc_bps <= min_twcc_bps) {
    send_interval_ = kMaxInterval;
  } else {
    send_interval_ = std::max(TimeDelta::Micros(kTwccReportBytes * 8 * 1000000 / twcc_bps), kMinInterval);
  }
}

void TransportFeedbackBuilder::OnPacket(uint16_t seq, Timestamp arrival) {
  int64_t s = unwrapper_.Unwrap(seq);
  ++stats_.packets;
  // MaybeCullOldPackets: when the window start has caught up with the end.
  if (window_start_ && *window_start_ >= end_ && arrival.us() >= kBackWindow.us()) {
    RemoveOldPackets(s, arrival - kBackWindow);
  }
  if (!window_start_ || s < *window_start_) window_start_ = s;
  if (seen_ && s >= begin_ && s < end_ && arrivals_.count(s)) return;  // Only the first arrival counts.
  AddArrival(s, arrival);
  if (*window_start_ < begin_) window_start_ = begin_;
}

// SendPeriodicFeedbacks + MaybeBuildFeedbackPacket. Arrivals stay in the map
// after a report; the window start says where the next report begins.
std::vector<TransportFeedback> TransportFeedbackBuilder::Build() {
  std::vector<TransportFeedback> out;
  if (!window_start_) return out;
  const int64_t end = end_;
  while (*window_start_ < end) {
    const int64_t begin = *window_start_;
    int64_t seq = std::clamp(begin, begin_, end_);
    const int64_t end_seq = std::clamp(end, begin_, end_);
    TransportFeedback fb;
    bool started = false;
    int64_t next = begin;
    int64_t prev_us = 0;
    for (auto it = arrivals_.lower_bound(seq); it != arrivals_.end() && it->first < end_seq; ++it) {
      const int64_t s = it->first;
      int64_t ticks = 0;
      if (!started) {
        // The base is the expected first sequence number; the reference time is the first arrival's.
        next = std::max(begin, s - kMaxMissingSequenceNumbers);
        fb.sender_ssrc = sender_ssrc_;
        fb.media_ssrc = media_ssrc_;
        fb.base_seq = static_cast<uint16_t>(next);
        fb.feedback_count = fb_count_++;
        const int64_t ref_units = it->second.us() / kRefUnitUs;
        fb.reference_time = static_cast<int32_t>(ref_units & 0x7FFFFF);
        prev_us = ref_units * kRefUnitUs;
        started = true;
      }
      ticks = (it->second.us() - prev_us) / kTickUs;
      // A message that cannot take another packet ends; the rest goes in the next one.
      if (!fb.packets.empty() && (ticks < -32768 || ticks > 32767 || fb.packets.size() + static_cast<size_t>(s - next) + 1 > 0xFFFF)) break;
      for (int64_t m = next; m < s; ++m) {
        TransportFeedback::Packet p;
        p.seq = static_cast<uint16_t>(m);
        fb.packets.push_back(p);
      }
      TransportFeedback::Packet p;
      p.seq = static_cast<uint16_t>(s);
      p.received = true;
      p.delta_ticks = ticks;
      prev_us += ticks * kTickUs;
      fb.packets.push_back(p);
      if (s < reported_high_) ++stats_.re_reported;
      next = s + 1;
    }
    if (!started) break;
    reported_high_ = std::max(reported_high_, next);
    window_start_ = next;
    out.push_back(std::move(fb));
    ++stats_.feedbacks;
  }
  return out;
}

void TransportFeedbackBuilder::ExportMetrics(MetricsWriter& w) const {
  w.Counter("packets", stats_.packets);
  w.Counter("feedbacks", stats_.feedbacks);
  w.Counter("re_reported", stats_.re_reported);
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
  // TransportFeedbackAdapter::ProcessTransportFeedback rounds each packet's time
  // since the feedback's base time down to 1 ms (RoundDownTo, i.e. toward zero)
  // before the estimators see it.
  const int64_t base_us = static_cast<int64_t>(fb.reference_time) * kRefUnitUs;
  size_t ai = 0;
  for (const auto& p : fb.packets) {
    int64_t s = unwrapper_.PeekUnwrap(p.seq);
    auto it = sent_.find(s);
    Timestamp arrival = Timestamp::PlusInfinity();
    if (p.received) {
      const int64_t since_base_us = arrivals[ai++].second.us() - base_us;
      arrival = Timestamp::Micros(base_us + since_base_us / 1000 * 1000);
    }
    if (it == sent_.end()) continue;
    // libwebrtc's TransportFeedbackAdapter erases a packet from its history
    // once it is reported received, so "a packet reported received more than
    // once" fails the lookup and is ignored. The receiver's generator re-reports
    // packets that arrive behind a sent report; counting them again feeds the
    // estimators receive times that go backwards.
    if (it->second.received) continue;
    if (p.received) it->second.received = true;
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
