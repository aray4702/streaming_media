#include "sm/timing/rtcp_stats.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace sm::timing {

namespace {
constexpr uint64_t kNtpUnixOffsetSeconds = 2208988800ULL;

void Put(std::vector<uint8_t>& b, uint64_t v, int bytes) {
  for (int i = bytes - 1; i >= 0; --i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
uint64_t Get(const uint8_t* p, int bytes) {
  uint64_t v = 0;
  for (int i = 0; i < bytes; ++i) v = (v << 8) | p[i];
  return v;
}
void PutBlock(std::vector<uint8_t>& b, const ReportBlock& r) {
  Put(b, r.ssrc, 4);
  Put(b, r.fraction_lost, 1);
  Put(b, static_cast<uint32_t>(std::clamp(r.cumulative_lost, -0x800000, 0x7FFFFF)) & 0xFFFFFF, 3);
  Put(b, r.extended_highest_seq, 4);
  Put(b, r.jitter, 4);
  Put(b, r.last_sr, 4);
  Put(b, r.delay_since_last_sr, 4);
}
ReportBlock GetBlock(const uint8_t* p) {
  ReportBlock r;
  r.ssrc = static_cast<uint32_t>(Get(p, 4));
  r.fraction_lost = p[4];
  uint32_t cl = static_cast<uint32_t>(Get(p + 5, 3));
  r.cumulative_lost = (cl & 0x800000) ? static_cast<int32_t>(cl | 0xFF000000u) : static_cast<int32_t>(cl);
  r.extended_highest_seq = static_cast<uint32_t>(Get(p + 8, 4));
  r.jitter = static_cast<uint32_t>(Get(p + 12, 4));
  r.last_sr = static_cast<uint32_t>(Get(p + 16, 4));
  r.delay_since_last_sr = static_cast<uint32_t>(Get(p + 20, 4));
  return r;
}
void PutHeader(std::vector<uint8_t>& b, size_t count, uint8_t pt, size_t total_bytes) {
  b.push_back(static_cast<uint8_t>(0x80 | (count & 0x1f)));
  b.push_back(pt);
  Put(b, total_bytes / 4 - 1, 2);
}
}  // namespace

NtpTime NtpTime::FromTimestamp(Timestamp t) {
  int64_t us = t.us();
  uint64_t secs = static_cast<uint64_t>(us / 1000000) + kNtpUnixOffsetSeconds;
  uint64_t frac = (static_cast<uint64_t>(us % 1000000) << 32) / 1000000;
  return NtpTime{(secs << 32) | frac};
}

Timestamp NtpTime::ToTimestamp() const {
  int64_t secs = static_cast<int64_t>(value >> 32) - static_cast<int64_t>(kNtpUnixOffsetSeconds);
  int64_t us = static_cast<int64_t>(((value & 0xFFFFFFFFULL) * 1000000 + (1ULL << 31)) >> 32);
  return Timestamp::Micros(secs * 1000000 + us);
}

TimeDelta CompactNtpToDelta(uint32_t c) {
  return TimeDelta::Micros(static_cast<int64_t>((static_cast<uint64_t>(c) * 1000000 + (1u << 15)) >> 16));
}

std::vector<uint8_t> SerializeSenderReport(const SenderReport& sr) {
  std::vector<uint8_t> b;
  size_t total = 28 + 24 * sr.blocks.size();
  PutHeader(b, sr.blocks.size(), 200, total);
  Put(b, sr.ssrc, 4);
  Put(b, sr.ntp.value, 8);
  Put(b, sr.rtp_timestamp, 4);
  Put(b, sr.packet_count, 4);
  Put(b, sr.octet_count, 4);
  for (const auto& r : sr.blocks) PutBlock(b, r);
  return b;
}

std::vector<uint8_t> SerializeReceiverReport(const ReceiverReport& rr) {
  std::vector<uint8_t> b;
  size_t total = 8 + 24 * rr.blocks.size();
  PutHeader(b, rr.blocks.size(), 201, total);
  Put(b, rr.ssrc, 4);
  for (const auto& r : rr.blocks) PutBlock(b, r);
  return b;
}

std::optional<SenderReport> ParseSenderReport(std::span<const uint8_t> d) {
  if (d.size() < 28 || (d[0] >> 6) != 2 || d[1] != 200) return std::nullopt;
  size_t count = d[0] & 0x1f;
  size_t len = (Get(&d[2], 2) + 1) * 4;
  if (len > d.size() || len < 28 + 24 * count) return std::nullopt;
  SenderReport sr;
  sr.ssrc = static_cast<uint32_t>(Get(&d[4], 4));
  sr.ntp.value = Get(&d[8], 8);
  sr.rtp_timestamp = static_cast<uint32_t>(Get(&d[16], 4));
  sr.packet_count = static_cast<uint32_t>(Get(&d[20], 4));
  sr.octet_count = static_cast<uint32_t>(Get(&d[24], 4));
  for (size_t i = 0; i < count; ++i) sr.blocks.push_back(GetBlock(&d[28 + 24 * i]));
  return sr;
}

std::optional<ReceiverReport> ParseReceiverReport(std::span<const uint8_t> d) {
  if (d.size() < 8 || (d[0] >> 6) != 2 || d[1] != 201) return std::nullopt;
  size_t count = d[0] & 0x1f;
  size_t len = (Get(&d[2], 2) + 1) * 4;
  if (len > d.size() || len < 8 + 24 * count) return std::nullopt;
  ReceiverReport rr;
  rr.ssrc = static_cast<uint32_t>(Get(&d[4], 4));
  for (size_t i = 0; i < count; ++i) rr.blocks.push_back(GetBlock(&d[8 + 24 * i]));
  return rr;
}

// --- ReceiveStatistics -------------------------------------------------------

void ReceiveStatistics::OnPacket(uint16_t seq, uint32_t rtp_ts, Timestamp arrival) {
  int64_t s = unwrapper_.Unwrap(seq);
  const bool first = !started_;
  if (first) {
    started_ = true;
    base_seq_ = max_seq_ = s;
  }
  ++received_;
  // StreamStatisticianImpl::UpdateCounters: only an in-order packet moves the
  // jitter state, and only one with a new timestamp after the first.
  if (!first && s <= max_seq_) return;
  max_seq_ = std::max(max_seq_, s);
  if (!first && rtp_ts != last_timestamp_) {
    // UpdateJitter: the receive gap in RTP units against the timestamp gap,
    // Q4 integer filter; a jump of 5 s of 90 kHz video clock is skipped.
    constexpr int32_t kJumpLimit = 5 * 90000;
    int64_t receive_diff_us = (arrival - last_receive_time_).us();
    uint32_t receive_diff_rtp = static_cast<uint32_t>(receive_diff_us * clock_rate_ / 1000000);
    int32_t time_diff_samples = static_cast<int32_t>(receive_diff_rtp - (rtp_ts - last_timestamp_));
    if (time_diff_samples < kJumpLimit && time_diff_samples > -kJumpLimit) {
      int32_t jitter_diff_q4 = (std::abs(time_diff_samples) << 4) - jitter_q4_;
      jitter_q4_ += (jitter_diff_q4 + 8) >> 4;
    }
  }
  last_timestamp_ = rtp_ts;
  last_receive_time_ = arrival;
}

void ReceiveStatistics::OnSenderReport(const SenderReport& sr, Timestamp arrival) {
  last_sr_compact_ = sr.ntp.Compact();
  last_sr_arrival_ = arrival;
}

ReceiveStatsSnapshot ReceiveStatistics::Snapshot() const {
  ReceiveStatsSnapshot s;
  s.packets_received = received_;
  s.expected = started_ ? max_seq_ - base_seq_ + 1 : 0;
  s.cumulative_lost = s.expected - received_;
  s.fraction_lost = last_fraction_ / 256.0;
  s.jitter_ms = jitter_ms();
  return s;
}

ReportBlock ReceiveStatistics::MakeReportBlock(Timestamp now) {
  ReportBlock rb;
  rb.ssrc = ssrc_;
  int64_t expected = started_ ? max_seq_ - base_seq_ + 1 : 0;
  int64_t expected_interval = expected - expected_prior_;
  int64_t received_interval = static_cast<int64_t>(received_) - received_prior_;
  int64_t lost_interval = expected_interval - received_interval;
  expected_prior_ = expected;
  received_prior_ = received_;
  rb.fraction_lost = (expected_interval == 0 || lost_interval <= 0)
                         ? 0
                         : static_cast<uint8_t>(std::min<int64_t>(255, (lost_interval << 8) / expected_interval));
  last_fraction_ = rb.fraction_lost;
  rb.cumulative_lost = static_cast<int32_t>(expected - received_);
  rb.extended_highest_seq = static_cast<uint32_t>(max_seq_);
  rb.jitter = static_cast<uint32_t>(jitter_q4_ >> 4);
  if (last_sr_arrival_.IsFinite()) {
    rb.last_sr = last_sr_compact_;
    rb.delay_since_last_sr = static_cast<uint32_t>(((now - last_sr_arrival_).us() << 16) / 1000000);
  }
  return rb;
}

void ReceiveStatistics::ExportMetrics(MetricsWriter& w) const {
  auto s = Snapshot();
  w.Counter("packets_received", s.packets_received);
  w.Gauge("cumulative_lost", static_cast<double>(s.cumulative_lost));
  w.Gauge("fraction_lost", s.fraction_lost);
  w.Gauge("jitter_ms", s.jitter_ms);
}

// --- RttEstimator --------------------------------------------------------------

std::optional<TimeDelta> RttEstimator::OnReportBlock(const ReportBlock& rb, Timestamp arrival) {
  if (rb.last_sr == 0) return std::nullopt;
  uint32_t a = NtpTime::FromTimestamp(arrival).Compact();
  uint32_t rtt_compact = a - rb.last_sr - rb.delay_since_last_sr;
  // CompactNtpRttToTimeDelta: a value past half the range is a negative
  // interval from a non-monotonic clock, and anything below 1 ms is too good
  // to be true; both count as 1 ms.
  TimeDelta rtt = rtt_compact > 0x80000000u ? TimeDelta::Zero() : CompactNtpToDelta(rtt_compact);
  rtt = std::max(rtt, TimeDelta::Millis(1));
  last_ = rtt;
  smoothed_ = samples_ == 0 ? rtt : smoothed_ * 0.875 + rtt * 0.125;
  min_ = std::min(min_, rtt);
  ++samples_;
  rtt_ms_.Add(rtt.ms_f());
  return rtt;
}

void RttEstimator::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("rtt_ms", last_.ms_f());
  w.Gauge("smoothed_rtt_ms", smoothed_.ms_f());
  w.Counter("samples", samples_);
  w.Hist("rtt_hist_ms", rtt_ms_);
}

}  // namespace sm::timing
