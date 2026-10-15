// 5.1 Transport-wide congestion control feedback
// (draft-holmer-rmcat-transport-wide-cc-extensions-01) and REMB.
//
// Every outgoing packet carries a transport-wide sequence number. The
// receiver reports, per sequence number, whether it arrived and the arrival
// time (250 us resolution); the sender joins that with its send history to
// get per-packet (send time, receive time, size) results, which feed the
// delay-based and loss-based estimators.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

#include "sm/core/metrics.h"
#include "sm/core/seq_num.h"
#include "sm/core/units.h"

namespace sm::cc {

struct PacketResult {
  int64_t seq = 0;  // Unwrapped transport sequence number.
  Timestamp send_time;
  Timestamp receive_time = Timestamp::PlusInfinity();  // +inf = lost.
  DataSize size;
  int probe_cluster = -1;
  bool received() const { return receive_time.IsFinite(); }
};

struct TransportFeedback {
  uint32_t sender_ssrc = 0;
  uint32_t media_ssrc = 0;
  uint16_t base_seq = 0;
  uint8_t feedback_count = 0;
  int32_t reference_time = 0;  // Units of 64 ms (24-bit signed on the wire).
  struct Packet {
    uint16_t seq = 0;
    bool received = false;
    int64_t delta_ticks = 0;   // 250 us ticks from the previous received packet.
  };
  std::vector<Packet> packets;
  // Absolute arrival time of each received packet (derived).
  std::vector<std::pair<uint16_t, Timestamp>> ArrivalTimes() const;
};

std::vector<uint8_t> SerializeTransportFeedback(const TransportFeedback& fb);
std::optional<TransportFeedback> ParseTransportFeedback(std::span<const uint8_t> data);

// REMB (draft-alvestrand-rmcat-remb): receiver-estimated max bitrate.
std::vector<uint8_t> SerializeRemb(uint32_t sender_ssrc, DataRate bitrate, const std::vector<uint32_t>& ssrcs);
std::optional<std::pair<DataRate, std::vector<uint32_t>>> ParseRemb(std::span<const uint8_t> data);

struct FeedbackBuilderStats {
  uint64_t packets = 0;
  uint64_t late_after_report = 0;
  uint64_t feedbacks = 0;
};

// Receiver side: records arrivals and periodically builds feedback.
class TransportFeedbackBuilder : public MetricsSource {
 public:
  explicit TransportFeedbackBuilder(uint32_t sender_ssrc = 0, uint32_t media_ssrc = 0)
      : sender_ssrc_(sender_ssrc), media_ssrc_(media_ssrc) {}
  void OnPacket(uint16_t transport_seq, Timestamp arrival);
  // Feedback covering everything since the previous call (may be split
  // into several messages when deltas do not fit). Empty if nothing new.
  std::vector<TransportFeedback> Build();
  const FeedbackBuilderStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  uint32_t sender_ssrc_, media_ssrc_;
  SeqNumUnwrapper<uint16_t> unwrapper_;
  std::map<int64_t, Timestamp> arrivals_;
  int64_t next_to_report_ = INT64_MIN;
  uint8_t fb_count_ = 0;
  FeedbackBuilderStats stats_;
};

// Sender side: remembers what was sent and turns feedback into results.
class SendHistory : public MetricsSource {
 public:
  void OnPacketSent(uint16_t transport_seq, Timestamp send_time, DataSize size, int probe_cluster = -1);
  // Results sorted by sequence number; unknown sequence numbers are skipped.
  std::vector<PacketResult> OnFeedback(const TransportFeedback& fb, Timestamp now);
  DataSize InFlight() const { return in_flight_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  struct Sent {
    Timestamp time;
    DataSize size;
    int probe;
    bool reported = false;
  };
  SeqNumUnwrapper<uint16_t> unwrapper_;
  std::map<int64_t, Sent> sent_;
  DataSize in_flight_;
  int64_t highest_reported_ = INT64_MIN;
  int64_t forgotten_below_ = INT64_MIN;  // Packets below this are settled for in_flight_.
  uint64_t lost_ = 0, acked_ = 0;
};

}  // namespace sm::cc
