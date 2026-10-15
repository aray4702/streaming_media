// 5.3 Loss-based bandwidth estimation.
//
// v1 (default): the classic GCC sender-side rule.
//
// Applied to the current target rate A once per measurement window:
//   loss < 2%   no loss-based limit (the delay-based estimate governs)
//   2% - 10%    hold: limit = A
//   loss > 10%  decrease: limit = A * (1 - 0.5 * loss), at most once per
//               300 ms + RTT
//
// Loss is measured over windows of at least 1 s and 20 packets, so a lucky
// or unlucky handful of packets cannot swing the estimate. (libwebrtc v1
// additionally caps increases at 8%/s after start-up; here the delay-based
// controller and probing own the upward direction.)
#pragma once

#include <array>
#include <optional>
#include <vector>

#include "sm/cc/transport_feedback.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"

namespace sm::cc {

struct LossBweConfig {
  double low_loss = 0.02;
  double high_loss = 0.10;
  TimeDelta decrease_interval = TimeDelta::Millis(300);
  TimeDelta min_window = TimeDelta::Seconds(1);
  int min_packets = 20;
  DataRate min_rate = DataRate::KilobitsPerSec(30);
  DataRate max_rate = DataRate::KilobitsPerSec(20000);
  EventSink* events = nullptr;
};

class LossBasedBwe : public MetricsSource {
 public:
  explicit LossBasedBwe(LossBweConfig cfg = {}) : cfg_(cfg), rate_(cfg.max_rate) {}
  void Reset() { rate_ = cfg_.max_rate; lost_ = total_ = 0; window_start_ = Timestamp::MinusInfinity(); }
  void SetRtt(TimeDelta rtt) { rtt_ = rtt; }
  // Accumulates results; returns the loss-based limit (max_rate = no limit).
  DataRate OnPacketResults(const std::vector<PacketResult>& results, DataRate current_target, Timestamp now);
  DataRate estimate() const { return rate_; }
  bool limiting() const { return rate_ < cfg_.max_rate; }
  double last_loss() const { return last_loss_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  LossBweConfig cfg_;
  DataRate rate_;
  TimeDelta rtt_ = TimeDelta::Millis(100);
  int64_t lost_ = 0, total_ = 0;
  Timestamp window_start_ = Timestamp::MinusInfinity();
  double last_loss_ = 0;
  Timestamp last_decrease_ = Timestamp::MinusInfinity();
  uint64_t windows_ = 0, holds_ = 0, decreases_ = 0;
};

// ---------------------------------------------------------------------------
// v2: inherent-loss model (after libwebrtc LossBasedBweV2), selected with
// GccConfig::loss_bwe = LossBweVersion::kV2. v1 above stays the default.
//
// Model: loss probability at sending rate R is
//   p(R) = q                                 for R <= B
//   p(R) = q + (1 - q) * (R - B) / R         for R >  B
// where q is the link's inherent loss (present at any rate) and B the
// loss-limited bandwidth. (q, B) are fitted by maximum likelihood to a
// window of observations (>= 250 ms of sent packets each: sending rate,
// delivered rate, bytes sent and lost), weighted 0.9^age. Each update
// evaluates candidate B values (current x {1.02, 1.0, 0.95}, the
// acknowledged rate, the delay-based estimate), takes one Newton step on q
// for each and keeps the most likely, plus a small bias toward higher B
// while loss is below 20%. q is bounded by [0.001, 0.05 + 100 kbps / B].
// Loss at a level the model calls inherent therefore does not limit the
// rate; loss that grows with the sending rate does.
//
// Limit = clamp(B, lower = acknowledged rate, upper = 100 kbps /
// (avg loss - 5%) when avg loss > 5%), never above the delay-based
// estimate. Increases while loss-limited are capped at 1.5 x acked (1.2 x
// during a hold, which follows each new decrease: 300 ms, doubling up to
// 60 s).
//
// Policer detection (not in libwebrtc; after BBR v1's long-term bandwidth
// sampling): when the newest observations all lose clearly more than the
// inherent rate while their delivered rate stays within +-12.5% although
// the sending rate varied or exceeded it, the link is treated as policed
// and the limit is capped at the delivered rate for 5 s (renewed while the
// pattern holds).
enum class LossBweVersion { kV1, kV2 };

enum class LossLimitCause { kNone, kLossModel, kPolicer, kInstantBound };

struct LossBweV2Config {
  TimeDelta observation_min_duration = TimeDelta::Millis(250);
  int observation_min_packets = 10;
  int window = 15;
  double temporal_weight = 0.9;
  std::array<double, 3> candidate_factors = {1.02, 1.0, 0.95};
  double inherent_loss_lower_bound = 0.001;
  double inherent_loss_upper_bound_offset = 0.05;
  DataRate inherent_loss_upper_bound_balance = DataRate::KilobitsPerSec(100);
  double initial_inherent_loss = 0.01;
  double newton_step = 0.75;
  double higher_bw_bias = 0.0002;
  double higher_log_bw_bias = 0.02;
  double high_bw_preference_loss = 0.20;
  double bw_preference_smoothing = 0.002;
  double not_increase_loss_ratio = 1.25;   // B may not rise while avg loss >
  double not_increase_loss_margin = 0.005; //   ratio x inherent + margin.
  double instant_upper_bound_loss_offset = 0.05;
  DataRate instant_upper_bound_balance = DataRate::KilobitsPerSec(100);
  double rampup_factor = 1.5;
  double rampup_factor_in_hold = 1.2;
  TimeDelta initial_hold = TimeDelta::Millis(300);
  TimeDelta max_hold = TimeDelta::Seconds(60);
  int min_observations = 3;
  // Policer detection.
  int policer_observations = 4;
  double policer_excess_loss = 0.05;
  double policer_rate_tolerance = 0.125;
  double policer_send_over_delivered = 1.15;
  TimeDelta policer_hold = TimeDelta::Seconds(5);
  DataRate min_rate = DataRate::KilobitsPerSec(30);
  DataRate max_rate = DataRate::KilobitsPerSec(20000);
  EventSink* events = nullptr;
};

class LossBasedBweV2 : public MetricsSource {
 public:
  explicit LossBasedBweV2(LossBweV2Config cfg = {});
  void Reset();
  void SetAcknowledgedBitrate(std::optional<DataRate> acked) { acked_ = acked; }
  void SetDelayBasedEstimate(DataRate d) { delay_based_ = d; }
  // The target the encoders and repair currently follow; used to turn a
  // limit on the total sending rate into a limit on the target.
  void SetCurrentTarget(DataRate t) { target_ = t; }
  // Accumulates results; returns the loss-based limit (max_rate = no limit).
  DataRate OnPacketResults(const std::vector<PacketResult>& results, Timestamp now);
  DataRate estimate() const { return limit_; }
  bool limiting() const { return limit_ < cfg_.max_rate; }
  LossLimitCause cause() const { return cause_; }
  double inherent_loss() const { return inherent_; }
  // Loss-limited bandwidth B of the model (max_rate until fitted).
  DataRate loss_limited_bandwidth() const { return bandwidth_.IsFinite() ? bandwidth_ : cfg_.max_rate; }
  double average_loss() const { return avg_loss_; }
  bool policer_detected() const { return policer_rate_.has_value(); }
  void ExportMetrics(MetricsWriter& w) const override;

  struct Observation {
    int packets = 0;
    int lost = 0;
    DataSize size;
    DataSize lost_size;
    DataRate sending_rate;
    DataRate delivered_rate;
    int64_t id = -1;
  };
  // Model loss probability, exposed for tests.
  static double LossProbability(double inherent, DataRate bandwidth, DataRate sending_rate);

 private:
  bool PushObservation(const std::vector<PacketResult>& results);
  double Weight(const Observation& o) const;
  double Objective(double inherent, DataRate bandwidth) const;
  double NewtonStep(double inherent, DataRate bandwidth) const;
  double FeasibleInherent(double inherent, DataRate bandwidth) const;
  std::optional<DataRate> DetectPolicer() const;
  void Update(Timestamp now);

  LossBweV2Config cfg_;
  std::vector<Observation> obs_;  // Ring buffer, cfg_.window entries.
  int64_t num_obs_ = 0;
  // Partial observation.
  int part_packets_ = 0, part_lost_ = 0;
  DataSize part_size_, part_lost_size_;
  Timestamp last_obs_send_ = Timestamp::PlusInfinity();
  std::optional<DataRate> acked_;
  DataRate delay_based_ = DataRate::PlusInfinity();
  DataRate target_ = DataRate::PlusInfinity();
  double inherent_;
  DataRate bandwidth_ = DataRate::PlusInfinity();  // Not fitted yet.
  DataRate limit_;
  double avg_loss_ = 0;
  bool loss_limited_ = false;
  LossLimitCause cause_ = LossLimitCause::kNone;
  Timestamp hold_until_ = Timestamp::MinusInfinity();
  TimeDelta hold_duration_;
  DataRate hold_rate_ = DataRate::PlusInfinity();
  std::optional<DataRate> policer_rate_;
  Timestamp policer_until_ = Timestamp::MinusInfinity();
  uint64_t observations_ = 0, decreases_ = 0, policer_detections_ = 0;
};

}  // namespace sm::cc
