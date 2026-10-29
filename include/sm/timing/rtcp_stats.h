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

// Extended reports (RFC 3611, PT 207) with the two blocks libwebrtc uses to
// measure the RTT of an endpoint that sends no media: the receiver reference
// time report (RRTR, block type 4) and the DLRR (block type 5), whose sub-blocks
// echo the RRTRs received (rtcp_packet/extended_reports.cc, rrtr.cc, dlrr.cc).
struct ReceiveTimeInfo {
  uint32_t ssrc = 0;                 // Sender of the RRTR.
  uint32_t last_rr = 0;              // Compact NTP of that RRTR.
  uint32_t delay_since_last_rr = 0;  // 1/65536 s.
};

struct ExtendedReports {
  uint32_t sender_ssrc = 0;
  std::optional<NtpTime> rrtr;
  std::vector<ReceiveTimeInfo> dlrr;
};

// ExtendedReports::kMaxNumberOfDlrrItems.
inline constexpr size_t kMaxNumberOfDlrrItems = 50;

// RRTR first, then one DLRR block with every item (ExtendedReports::Create).
std::vector<uint8_t> SerializeExtendedReports(const ExtendedReports& xr);
// Parses a single XR packet. Blocks of other types are skipped, and an RRTR
// or DLRR block of the wrong length is ignored, as ExtendedReports::Parse does.
std::optional<ExtendedReports> ParseExtendedReports(std::span<const uint8_t> data);

struct ReceiveStatsSnapshot {
  uint32_t packets_received = 0;
  int64_t expected = 0;
  int64_t cumulative_lost = 0;
  double fraction_lost = 0;  // Since the previous report.
  double jitter_ms = 0;
};

// Per-SSRC receive statistics as defined in RFC 3550 A.3 and A.8.
// rtp_rtcp_config.h: the max reordering threshold of a receive stream, in
// sequence numbers, unless the receiver sets another.
inline constexpr int kDefaultMaxReorderingThreshold = 50;
// RtpVideoStreamReceiver2 (rtp_video_stream_receiver2.cc:106, :365-367): the
// threshold of a video stream with NACK on.
inline constexpr int kMaxPacketAgeToNack = 450;

class ReceiveStatistics : public MetricsSource {
 public:
  ReceiveStatistics(uint32_t ssrc, int clock_rate) : ssrc_(ssrc), clock_rate_(clock_rate) {}
  // `v2`: the sequence and report rules of libwebrtc's StreamStatisticianImpl (issue #575).
  // `max_reordering_threshold` is its SetMaxReorderingThreshold: a packet further than this
  // from the highest sequence number is held; if the next packet follows it, the stream
  // restarted and the gap is not loss, else it was an old packet (receive_statistics_impl.cc:
  // 71-115). Fraction lost is 255 * lost / expected, and a negative cumulative loss reports 0
  // (:263-274). Off: no threshold (any later packet is the new maximum and a jump is loss),
  // fraction lost 256 * lost / expected capped at 255, and the cumulative loss as counted.
  ReceiveStatistics(uint32_t ssrc, int clock_rate, bool v2, int max_reordering_threshold = kDefaultMaxReorderingThreshold)
      : ssrc_(ssrc), clock_rate_(clock_rate), v2_(v2), max_reordering_threshold_(max_reordering_threshold) {}
  void OnPacket(uint16_t seq, uint32_t rtp_ts, Timestamp arrival);
  // As above for a stream whose payload type changes the RTP clock rate:
  // `payload_frequency` is the rate of this packet (libwebrtc's
  // `payload_type_frequency()`); the jitter is rescaled when it changes
  // (`StreamStatisticianImpl::ReviseFrequencyAndJitter`).
  void OnPacket(uint16_t seq, uint32_t rtp_ts, Timestamp arrival, int payload_frequency);
  void OnSenderReport(const SenderReport& sr, Timestamp arrival);
  // StreamStatisticianImpl::MaybeAppendReportBlockAndReset (receive_statistics_impl.cc:241-250):
  // a stream is reported once it has received a packet, and not after 8 s (kStatisticsTimeout)
  // without an in-order one.
  bool ReportBlockDue(Timestamp now) const {
    return started_ && now - last_receive_time_ < TimeDelta::Seconds(8);
  }
  // Builds a report block and starts a new fraction-lost interval.
  ReportBlock MakeReportBlock(Timestamp now);
  ReceiveStatsSnapshot Snapshot() const;
  double jitter_ms() const { return jitter_q4_ / 16.0 * 1000.0 / clock_rate_; }
  int jitter_frequency() const { return clock_rate_; }
  bool v2() const { return v2_; }
  int max_reordering_threshold() const { return max_reordering_threshold_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  uint32_t ssrc_;
  int clock_rate_;
  // StreamStatisticianImpl's sequence state (receive_statistics_impl.cc).
  bool UpdateOutOfOrder(uint16_t seq, int64_t sequence_number);
  bool v2_ = false;
  int max_reordering_threshold_ = kDefaultMaxReorderingThreshold;
  SeqNumUnwrapper<uint16_t> unwrapper_;
  bool started_ = false;
  uint32_t received_ = 0;  // receive_counters_.transmitted.packets.
  int64_t received_seq_first_ = -1;
  int64_t received_seq_max_ = -1;
  std::optional<uint16_t> received_seq_out_of_order_;
  int32_t cumulative_loss_ = 0;
  int32_t cumulative_loss_rtcp_offset_ = 0;
  int32_t last_report_cumulative_loss_ = 0;
  int64_t last_report_seq_max_ = -1;
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
