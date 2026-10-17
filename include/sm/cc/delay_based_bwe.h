// 5.2 Delay-based bandwidth estimation (Google Congestion Control).
//
// Pipeline, per transport feedback:
//   InterArrival        groups packets sent within 5 ms (a burst) and yields
//                       per-group send/receive deltas.
//   TrendlineEstimator  accumulates the one-way delay variation
//                       (recv_delta - send_delta), smooths it, and fits a
//                       least-squares slope over the last 20 groups. A rising
//                       queue shows up as a positive slope.
//   Overuse detection   compares the scaled slope against an adaptive
//                       threshold (which grows slowly toward |trend| so
//                       competing TCP flows do not starve us).
//   AimdRateController  increase (multiplicative far from the link capacity
//                       estimate, additive near it), hold, or decrease to
//                       0.85 x the acknowledged throughput on overuse.
//   AcknowledgedBitrate throughput actually delivered, used to anchor
//                       decreases and cap increases.
#pragma once

#include <algorithm>

#include <deque>
#include <optional>
#include <vector>

#include "sm/cc/aimd_rate_control.h"
#include "sm/cc/transport_feedback.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::cc {

enum class BandwidthUsage { kNormal, kUnderusing, kOverusing };
const char* ToString(BandwidthUsage u);

// TransportPacketsFeedback::SortedByReceiveTime: the received packets,
// ordered by (receive time, send time, sequence number) as
// PacketResult::ReceiveTimeOrder.
std::vector<PacketResult> SortedByReceiveTime(const std::vector<PacketResult>& results);

class InterArrival {
 public:
  explicit InterArrival(TimeDelta group_length = TimeDelta::Millis(5)) : group_length_(group_length) {}
  struct Deltas {
    TimeDelta send_delta;
    TimeDelta recv_delta;
    int64_t size_delta;
  };
  // Returns deltas when a group completes (on the first packet of the next).
  std::optional<Deltas> OnPacket(Timestamp send_time, Timestamp arrival, DataSize size);

 private:
  struct Group {
    Timestamp first_send = Timestamp::MinusInfinity();
    Timestamp last_send;
    Timestamp first_arrival;
    Timestamp last_arrival;
    int64_t size = 0;
    bool valid() const { return first_send.IsFinite(); }
  };
  TimeDelta group_length_;
  Group current_, prev_;
};

// libwebrtc modules/congestion_controller/goog_cc/inter_arrival_delta.{h,cc}
// (`cc v2`). Groups packets by send time (a group spans at most
// `send_time_group_length` from its first packet; packets that arrive as a
// burst stay in the group, BelongsToBurst), drops packets reordered across
// groups, resets after 3 consecutive groups with a negative arrival delta or
// a 3 s jump of arrival time against the system clock.
class InterArrivalDelta {
 public:
  static constexpr int kReorderedResetThreshold = 3;
  static constexpr TimeDelta kArrivalTimeOffsetThreshold = TimeDelta::Seconds(3);
  explicit InterArrivalDelta(TimeDelta send_time_group_length) : send_time_group_length_(send_time_group_length) {}
  struct Deltas {
    TimeDelta send_delta;
    TimeDelta arrival_delta;
    int size_delta = 0;
  };
  // Returns the deltas between the last two completed groups when the packet
  // starts a new group.
  std::optional<Deltas> ComputeDeltas(Timestamp send_time, Timestamp arrival_time, Timestamp system_time, size_t packet_size);

 private:
  struct SendTimeGroup {
    size_t size = 0;
    Timestamp first_send_time = Timestamp::MinusInfinity();
    Timestamp send_time = Timestamp::MinusInfinity();
    Timestamp first_arrival = Timestamp::MinusInfinity();
    Timestamp complete_time = Timestamp::MinusInfinity();
    Timestamp last_system_time = Timestamp::MinusInfinity();
    bool IsFirstPacket() const { return !complete_time.IsFinite(); }
  };
  bool NewTimestampGroup(Timestamp arrival_time, Timestamp send_time) const;
  bool BelongsToBurst(Timestamp arrival_time, Timestamp send_time) const;
  void Reset();
  TimeDelta send_time_group_length_;
  SendTimeGroup current_, prev_;
  int num_consecutive_reordered_packets_ = 0;
};

struct TrendlineConfig {
  size_t window = 20;
  double smoothing = 0.9;
  double threshold_gain = 4.0;
  double k_up = 0.0087;
  double k_down = 0.039;
  double initial_threshold = 12.5;
  TimeDelta overusing_time = TimeDelta::Millis(10);
};

class TrendlineEstimator {
 public:
  explicit TrendlineEstimator(TrendlineConfig cfg = {}) : cfg_(cfg), threshold_(cfg.initial_threshold) {}
  void Update(double recv_delta_ms, double send_delta_ms, double arrival_ms);
  BandwidthUsage State() const { return state_; }
  double trend() const { return trend_; }
  double modified_trend() const { return modified_trend_; }
  double threshold() const { return threshold_; }

 private:
  void Detect(double send_delta_ms, double now_ms);
  void UpdateThreshold(double modified_trend, double now_ms);
  TrendlineConfig cfg_;
  size_t num_deltas_ = 0;
  double first_arrival_ms_ = -1;
  double accumulated_ = 0, smoothed_ = 0;
  std::deque<std::pair<double, double>> window_;  // (time ms, smoothed delay)
  double trend_ = 0, prev_trend_ = 0, modified_trend_ = 0;
  double threshold_;
  double last_threshold_update_ms_ = -1;
  double time_over_using_ = -1;
  int overuse_counter_ = 0;
  BandwidthUsage state_ = BandwidthUsage::kNormal;
};

class AcknowledgedBitrateEstimator {
 public:
  void OnPacketResults(const std::vector<PacketResult>& results);
  std::optional<DataRate> bitrate() const { return bitrate_; }

 private:
  std::deque<std::pair<Timestamp, int64_t>> window_;  // (receive time, bytes)
  std::optional<DataRate> bitrate_;
};

struct AimdConfig {
  DataRate min_rate = DataRate::KilobitsPerSec(30);
  DataRate max_rate = DataRate::KilobitsPerSec(20000);
  double beta = 0.85;
  double multiplicative_per_s = 0.08;
};

class AimdRateController {
 public:
  explicit AimdRateController(AimdConfig cfg = {}) : cfg_(cfg) {}
  void SetEstimate(DataRate r, Timestamp now);
  void SetRtt(TimeDelta rtt) { rtt_ = rtt; }
  // New lower bound (libwebrtc SetMinBitrate); lifts the estimate to it.
  void SetMinRate(DataRate r) { cfg_.min_rate = r; current_ = std::max(current_, r); }
  DataRate Update(BandwidthUsage usage, std::optional<DataRate> acked, Timestamp now);
  DataRate estimate() const { return current_; }
  bool has_estimate() const { return has_estimate_; }
  std::optional<double> link_capacity_kbps() const;

 private:
  enum class State { kHold, kIncrease, kDecrease };
  AimdConfig cfg_;
  State state_ = State::kIncrease;
  DataRate current_ = DataRate::KilobitsPerSec(300);
  bool has_estimate_ = false;
  Timestamp last_change_ = Timestamp::MinusInfinity();
  Timestamp last_decrease_ = Timestamp::MinusInfinity();
  TimeDelta rtt_ = TimeDelta::Millis(200);
  double link_kbps_ = -1, link_var_ = 0.4;
};

struct DelayBweStats {
  uint64_t stream_resets = 0;  // v2: kStreamTimeOut (2 s without feedback) resets.
  uint64_t feedbacks = 0;
  uint64_t overuse_signals = 0;
  uint64_t underuse_signals = 0;
  uint64_t decreases = 0;
  Histogram trend;
};

class DelayBasedBwe : public MetricsSource {
 public:
  struct Config {
    AimdConfig aimd;
    TrendlineConfig trendline;
    EventSink* events = nullptr;
    // 1: the original estimator. 2 (`cc v2`): libwebrtc's DelayBasedBwe
    // front end: feedback sorted by receive time, InterArrivalDelta, 2 s
    // stream time-out, and (#15) libwebrtc's AimdRateControl with the
    // MaybeUpdateEstimate decision logic.
    int version = 1;
  };
  DelayBasedBwe() : DelayBasedBwe(Config{}) {}
  explicit DelayBasedBwe(Config cfg);
  void SetStartBitrate(DataRate r, Timestamp now);
  void SetRtt(TimeDelta rtt);
  void SetMinRate(DataRate r);
  // Results must be sorted by send time / sequence number.
  DataRate OnPacketResults(const std::vector<PacketResult>& results, std::optional<DataRate> acked,
                           std::optional<DataRate> probe_estimate, Timestamp now);
  DataRate estimate() const { return cfg_.version >= 2 ? rc2_.LatestEstimate() : aimd_.estimate(); }
  const AimdRateControl& rate_control_v2() const { return rc2_; }
  BandwidthUsage usage() const { return trendline_.State(); }
  const TrendlineEstimator& trendline() const { return trendline_; }
  const DelayBweStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  Config cfg_;
  void OnPacketResultsV2(const std::vector<PacketResult>& results, Timestamp now);
  // libwebrtc DelayBasedBwe::MaybeUpdateEstimate.
  DataRate MaybeUpdateEstimateV2(std::optional<DataRate> acked, std::optional<DataRate> probe, Timestamp now);
  InterArrival inter_arrival_;
  InterArrivalDelta inter_arrival_delta_{TimeDelta::Millis(5)};
  Timestamp last_seen_packet_ = Timestamp::MinusInfinity();
  TrendlineEstimator trendline_;
  AimdRateController aimd_;
  AimdRateControl rc2_;
  BandwidthUsage last_usage_ = BandwidthUsage::kNormal;
  DelayBweStats stats_;
};

}  // namespace sm::cc
