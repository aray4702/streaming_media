// 4.2 Network measurement: RFC 3550 receiver statistics (loss, interarrival
// jitter, extended highest sequence number), RTCP SR/RR wire format, and RTT
// from report blocks (LSR/DLSR).
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "sm/core/metrics.h"
#include "sm/core/seq_num.h"
#include "sm/core/units.h"

namespace sm::timing {

// 64-bit NTP timestamp (seconds since 1900 in 32.32 fixed point). The
// library's Timestamp is interpreted as time since the Unix epoch.
struct NtpTime {
  uint64_t value = 0;
  static NtpTime FromTimestamp(Timestamp t);
  Timestamp ToTimestamp() const;
  // Middle 32 bits, used by LSR/DLSR.
  uint32_t Compact() const { return static_cast<uint32_t>(value >> 16); }
};
TimeDelta CompactNtpToDelta(uint32_t compact);  // 16.16 seconds -> TimeDelta.

struct ReportBlock {
  uint32_t ssrc = 0;
  uint8_t fraction_lost = 0;     // Q8.
  int32_t cumulative_lost = 0;   // 24-bit signed on the wire.
  uint32_t extended_highest_seq = 0;
  uint32_t jitter = 0;           // RTP timestamp units.
  uint32_t last_sr = 0;          // Compact NTP of the last SR received.
  uint32_t delay_since_last_sr = 0;  // 1/65536 s.
};

struct SenderReport {
  uint32_t ssrc = 0;
  NtpTime ntp;
  uint32_t rtp_timestamp = 0;
  uint32_t packet_count = 0;
  uint32_t octet_count = 0;
  std::vector<ReportBlock> blocks;
};

struct ReceiverReport {
  uint32_t ssrc = 0;
  std::vector<ReportBlock> blocks;
};

std::vector<uint8_t> SerializeSenderReport(const SenderReport& sr);
std::vector<uint8_t> SerializeReceiverReport(const ReceiverReport& rr);
// Parses a single SR (PT 200) or RR (PT 201) packet.
std::optional<SenderReport> ParseSenderReport(std::span<const uint8_t> data);
std::optional<ReceiverReport> ParseReceiverReport(std::span<const uint8_t> data);

struct ReceiveStatsSnapshot {
  uint32_t packets_received = 0;
  int64_t expected = 0;
  int64_t cumulative_lost = 0;
  double fraction_lost = 0;  // Since the previous report.
  double jitter_ms = 0;
};

// Per-SSRC receive statistics as defined in RFC 3550 A.3 and A.8.
class ReceiveStatistics : public MetricsSource {
 public:
  ReceiveStatistics(uint32_t ssrc, int clock_rate) : ssrc_(ssrc), clock_rate_(clock_rate) {}
  void OnPacket(uint16_t seq, uint32_t rtp_ts, Timestamp arrival);
  // As above for a stream whose payload type changes the RTP clock rate:
  // `payload_frequency` is the rate of this packet (libwebrtc's
  // `payload_type_frequency()`); the jitter is rescaled when it changes
  // (`StreamStatisticianImpl::ReviseFrequencyAndJitter`).
  void OnPacket(uint16_t seq, uint32_t rtp_ts, Timestamp arrival, int payload_frequency);
  void OnSenderReport(const SenderReport& sr, Timestamp arrival);
  // Builds a report block and starts a new fraction-lost interval.
  ReportBlock MakeReportBlock(Timestamp now);
  ReceiveStatsSnapshot Snapshot() const;
  double jitter_ms() const { return jitter_q4_ / 16.0 * 1000.0 / clock_rate_; }
  int jitter_frequency() const { return clock_rate_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  uint32_t ssrc_;
  int clock_rate_;
  SeqNumUnwrapper<uint16_t> unwrapper_;
  bool started_ = false;
  int64_t base_seq_ = 0, max_seq_ = 0;
  uint32_t received_ = 0;
  int64_t expected_prior_ = 0;
  uint32_t received_prior_ = 0;
  int32_t jitter_q4_ = 0;  // RTP units scaled by 16 (RFC 3550 A.8, integer as libwebrtc).
  uint32_t last_timestamp_ = 0;
  Timestamp last_receive_time_ = Timestamp::MinusInfinity();
  uint8_t last_fraction_ = 0;
  uint32_t last_sr_compact_ = 0;
  Timestamp last_sr_arrival_ = Timestamp::MinusInfinity();
};

// RTT from report blocks: RTT = A - LSR - DLSR (A = arrival, compact NTP).
class RttEstimator : public MetricsSource {
 public:
  std::optional<TimeDelta> OnReportBlock(const ReportBlock& rb, Timestamp arrival);
  TimeDelta last() const { return last_; }
  TimeDelta smoothed() const { return smoothed_; }
  TimeDelta min() const { return min_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  TimeDelta last_ = TimeDelta::Zero();
  TimeDelta smoothed_ = TimeDelta::Zero();
  TimeDelta min_ = TimeDelta::PlusInfinity();
  uint64_t samples_ = 0;
  Histogram rtt_ms_;
};

}  // namespace sm::timing
