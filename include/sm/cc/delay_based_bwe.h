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
//                       decreases and cap increases. Like libwebrtc's
//                       BitrateEstimator: one 500 ms window for the first
//                       sample, then non-overlapping 150 ms windows folded
//                       in by a Bayesian update.
#pragma once

#include <deque>
#include <optional>
#include <vector>

#include "sm/cc/transport_feedback.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::cc {

enum class BandwidthUsage { kNormal, kUnderusing, kOverusing };
const char* ToString(BandwidthUsage u);

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
  static constexpr int64_t kInitialWindowMs = 500;  // First sample only.
  static constexpr int64_t kWindowMs = 150;
  void Update(Timestamp at, int64_t bytes);
  int64_t prev_ms_ = -1, current_window_ms_ = 0, sum_bytes_ = 0;
  double estimate_kbps_ = -1, var_ = 50;
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
  };
  DelayBasedBwe() : DelayBasedBwe(Config{}) {}
  explicit DelayBasedBwe(Config cfg);
  void SetStartBitrate(DataRate r, Timestamp now) { aimd_.SetEstimate(r, now); }
  void SetRtt(TimeDelta rtt) { aimd_.SetRtt(rtt); }
  // Results must be sorted by send time / sequence number.
  DataRate OnPacketResults(const std::vector<PacketResult>& results, std::optional<DataRate> acked,
                           std::optional<DataRate> probe_estimate, Timestamp now);
  DataRate estimate() const { return aimd_.estimate(); }
  BandwidthUsage usage() const { return trendline_.State(); }
  const TrendlineEstimator& trendline() const { return trendline_; }
  const DelayBweStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  Config cfg_;
  InterArrival inter_arrival_;
  TrendlineEstimator trendline_;
  AimdRateController aimd_;
  BandwidthUsage last_usage_ = BandwidthUsage::kNormal;
  DelayBweStats stats_;
};

}  // namespace sm::cc
