// 5.2 Delay-based bandwidth estimation (Google Congestion Control).
//
// libwebrtc's DelayBasedBwe (goog_cc/delay_based_bwe.cc, M157,
// compare/webrtc/reference/). Pipeline, per transport feedback:
//   InterArrivalDelta   groups packets sent within 5 ms (a burst) and yields
//                       per-group send/arrival deltas; feedback is sorted by
//                       receive time, and 2 s without feedback resets it.
//   TrendlineEstimator  accumulates the one-way delay variation
//                       (recv_delta - send_delta), smooths it, and fits a
//                       least-squares slope over the last 20 groups. A rising
//                       queue shows up as a positive slope.
//   Overuse detection   compares the scaled slope against an adaptive
//                       threshold (which grows slowly toward |trend| so
//                       competing TCP flows do not starve us).
//   AimdRateControl     libwebrtc's rate control (cc/aimd_rate_control.h),
//                       driven by MaybeUpdateEstimate: increase, hold, or
//                       decrease to 0.85 x the acknowledged throughput on
//                       overuse; a probe result sets the estimate directly.
#pragma once

#include <algorithm>

#include <deque>
#include <optional>
#include <utility>
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

// libwebrtc modules/congestion_controller/goog_cc/inter_arrival_delta.{h,cc}.
// Groups packets by send time (a group spans at most
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
  size_t num_deltas() const { return num_deltas_; }

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

struct DelayBweStats {
  uint64_t stream_resets = 0;  // kStreamTimeOut (2 s without feedback) resets.
  uint64_t feedbacks = 0;
  uint64_t overuse_signals = 0;
  uint64_t underuse_signals = 0;
  uint64_t decreases = 0;
  Histogram trend;
};

// One TrendlineEstimator::Update with calculated deltas, for the BWE
// trace's detector rows (issue #152).
struct DetectorSample {
  double recv_delta_ms = 0, send_delta_ms = 0;  // The group deltas.
  double send_ms = 0, arrival_ms = 0;           // The times Update was given.
  double modified_trend = 0, threshold = 0;     // After the update.
  BandwidthUsage state = BandwidthUsage::kNormal;
  size_t num_deltas = 0;
};

class DelayBasedBwe : public MetricsSource {
 public:
  struct Config {
    DataRate min_rate = DataRate::KilobitsPerSec(30);  // AimdRateControl::SetMinBitrate.
    TrendlineConfig trendline;
    EventSink* events = nullptr;
    // Record every trendline update with calculated deltas (TakeDetectorTrace).
    bool trace_detector = false;
  };
  DelayBasedBwe() : DelayBasedBwe(Config{}) {}
  explicit DelayBasedBwe(Config cfg);
  void SetStartBitrate(DataRate r);
  void SetRtt(TimeDelta rtt);
  void SetMinRate(DataRate r);
  // Results must be sorted by send time / sequence number.
  DataRate OnPacketResults(const std::vector<PacketResult>& results, std::optional<DataRate> acked,
                           std::optional<DataRate> probe_estimate, Timestamp now);
  // DelayBasedBwe::Result of the last OnPacketResults.
  struct Result {
    bool updated = false;
    bool probe = false;
    bool recovered_from_overuse = false;
  };
  const Result& last_result() const { return last_result_; }
  DataRate estimate() const { return rc2_.LatestEstimate(); }
  const AimdRateControl& rate_control() const { return rc2_; }
  BandwidthUsage usage() const { return trendline_.State(); }
  const TrendlineEstimator& trendline() const { return trendline_; }
  // The detector samples recorded since the last call (Config::trace_detector).
  std::vector<DetectorSample> TakeDetectorTrace() { return std::exchange(detector_trace_, {}); }
  const DelayBweStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  Config cfg_;
  void IncomingPacketFeedback(const std::vector<PacketResult>& results, Timestamp now);
  // libwebrtc DelayBasedBwe::MaybeUpdateEstimate.
  DataRate MaybeUpdateEstimate(std::optional<DataRate> acked, std::optional<DataRate> probe, Timestamp now);
  InterArrivalDelta inter_arrival_delta_{TimeDelta::Millis(5)};
  Timestamp last_seen_packet_ = Timestamp::MinusInfinity();
  TrendlineEstimator trendline_;
  AimdRateControl rc2_;
  BandwidthUsage last_usage_ = BandwidthUsage::kNormal;
  bool recovered_from_overuse_ = false;
  Result last_result_;
  DelayBweStats stats_;
  std::vector<DetectorSample> detector_trace_;
};

}  // namespace sm::cc
