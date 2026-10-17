// libwebrtc's LossBasedBweV2 (`cc v2`), ported from
// compare/webrtc/reference/modules/congestion_controller/goog_cc/loss_based_bwe_v2.{h,cc}
// (M157) with the constants and defaults of CreateConfig (no field trial set),
// and the target state machine of SendSideBandwidthEstimation around it
// (send_side_bandwidth_estimation.{h,cc}).
//
// LossBasedBweV2 keeps a window of 15 observations (each at least 250 ms of
// sending: packets, bytes, bytes lost, sending rate). It fits the channel model
//   p(loss) = q + (1 - q) * (R - B) / R     when R > B, else q
// (q the inherent loss, B the loss-limited bandwidth, R the sending rate) to the
// window by maximum likelihood, one Newton step on q per candidate bandwidth
// (current x {1.02, 1.0, 0.95}, the acknowledged rate, a higher delay-based
// estimate), and picks the candidate with the highest objective, which includes
// a bias toward higher bandwidth when the average loss is under 20 %. The result
// is bounded by the instant upper bound (100 kbps / (loss - 5 %)), the acked
// rate and the delay-based estimate, held (HOLD, 300 ms doubling to 60 s) after
// a decrease, and ramped up at most 1.3 x per 300 ms and 1.5 x the acked rate.
//
// A packet counts as lost only when a feedback message covers it and reports it
// as not received (PacketResult::received() false); a packet that a later
// feedback reports as received is removed from the lost set again.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "sm/cc/transport_feedback.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::cc {

enum class LossBasedState {
  kIncreasing = 0,
  kIncreaseUsingPadding = 1,
  kDecreasing = 2,
  kDelayBasedEstimate = 3,
};

struct LossBasedBweV2Config {
  double bandwidth_rampup_upper_bound_factor = 1.5;
  double bandwidth_rampup_upper_bound_factor_in_hold = 1.2;
  double bandwidth_rampup_hold_threshold = 1.3;
  double rampup_acceleration_max_factor = 0.0;
  TimeDelta rampup_acceleration_maxout_time = TimeDelta::Seconds(60);
  std::vector<double> candidate_factors = {1.02, 1.0, 0.95};
  double higher_bandwidth_bias_factor = 0.0002;
  double higher_log_bandwidth_bias_factor = 0.02;
  double inherent_loss_lower_bound = 1.0e-3;
  double loss_threshold_of_high_bandwidth_preference = 0.2;
  double bandwidth_preference_smoothing_factor = 0.002;
  DataRate inherent_loss_upper_bound_bandwidth_balance = DataRate::KilobitsPerSec(100);
  double inherent_loss_upper_bound_offset = 0.05;
  double initial_inherent_loss_estimate = 0.01;
  int newton_iterations = 1;
  double newton_step_size = 0.75;
  bool append_acknowledged_rate_candidate = true;
  bool append_delay_based_estimate_candidate = true;
  bool append_upper_bound_candidate_in_alr = false;
  TimeDelta observation_duration_lower_bound = TimeDelta::Millis(250);
  int observation_window_size = 15;
  double sending_rate_smoothing_factor = 0.0;
  double instant_upper_bound_temporal_weight_factor = 0.9;
  DataRate instant_upper_bound_bandwidth_balance = DataRate::KilobitsPerSec(100);
  double instant_upper_bound_loss_offset = 0.05;
  double temporal_weight_factor = 0.9;
  double bandwidth_backoff_lower_bound_factor = 1.0;
  double max_increase_factor = 1.3;
  TimeDelta delayed_increase_window = TimeDelta::Millis(300);
  bool not_increase_if_inherent_loss_less_than_average_loss = true;
  bool not_use_acked_rate_in_alr = true;
  bool use_in_start_phase = true;
  int min_num_observations = 3;
  double lower_bound_by_acked_rate_factor = 1.0;
  double hold_duration_factor = 2.0;
  bool use_byte_loss_rate = true;
  TimeDelta padding_duration = TimeDelta::Seconds(2);
  bool bound_best_candidate = true;
  double median_sending_rate_factor = 2.0;
  EventSink* events = nullptr;
};

class LossBasedBweV2 : public MetricsSource {
 public:
  struct Result {
    DataRate bandwidth_estimate = DataRate::Zero();
    LossBasedState state = LossBasedState::kDelayBasedEstimate;
  };

  explicit LossBasedBweV2(LossBasedBweV2Config cfg = {});

  // True iff a bandwidth can be calculated: initialised with an estimate and
  // enough observations.
  bool IsReady() const;
  bool ReadyToUseInStartPhase() const { return IsReady() && cfg_.use_in_start_phase; }
  bool UseInStartPhase() const { return cfg_.use_in_start_phase; }
  // PlusInfinity if no bandwidth can be calculated yet (the delay-based
  // estimate when there is one).
  Result GetLossBasedResult() const;

  void SetAcknowledgedBitrate(DataRate acknowledged_bitrate);
  void SetMinMaxBitrate(DataRate min_bitrate, DataRate max_bitrate);
  // `delay_based_estimate` PlusInfinity means there is none.
  void UpdateBandwidthEstimate(std::span<const PacketResult> packet_results, DataRate delay_based_estimate,
                               bool in_alr, Timestamp now = Timestamp::MinusInfinity());
  void SetBandwidthEstimate(DataRate bandwidth_estimate);

  // For metrics and tests.
  double inherent_loss() const { return current_best_estimate_.inherent_loss; }
  double average_reported_loss_ratio() const { return average_reported_loss_ratio_; }
  int num_observations() const { return num_observations_; }
  std::optional<DataRate> loss_limited_bandwidth() const { return current_best_estimate_.loss_limited_bandwidth; }
  DataRate instant_upper_bound() const { return GetInstantUpperBound(); }
  const LossBasedBweV2Config& config() const { return cfg_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  struct ChannelParameters {
    double inherent_loss = 0.0;
    std::optional<DataRate> loss_limited_bandwidth;  // nullopt: not initialised (MinusInfinity).
  };
  struct Derivatives {
    double first = 0.0;
    double second = 0.0;
  };
  struct Observation {
    bool IsInitialized() const { return id != -1; }
    int num_packets = 0;
    int num_lost_packets = 0;
    int num_received_packets = 0;
    std::optional<DataRate> sending_rate;
    DataSize size;
    DataSize lost_size;
    int id = -1;
  };
  struct PartialObservation {
    int num_packets = 0;
    std::unordered_map<int64_t, DataSize> lost_packets;
    DataSize size;
  };
  struct PaddingInfo {
    std::optional<DataRate> padding_rate;  // nullopt: MinusInfinity.
    Timestamp padding_timestamp = Timestamp::MinusInfinity();
  };
  struct HoldInfo {
    Timestamp timestamp = Timestamp::MinusInfinity();
    TimeDelta duration = TimeDelta::Zero();
    DataRate rate = DataRate::PlusInfinity();
  };

  void UpdateAverageReportedLossRatio();
  double CalculateAverageReportedPacketLossRatio() const;
  double CalculateAverageReportedByteLossRatio() const;
  std::vector<ChannelParameters> GetCandidates(bool in_alr) const;
  DataRate GetCandidateBandwidthUpperBound() const;
  Derivatives GetDerivatives(const ChannelParameters& channel_parameters) const;
  double GetFeasibleInherentLoss(const ChannelParameters& channel_parameters) const;
  double GetInherentLossUpperBound(DataRate bandwidth) const;
  double AdjustBiasFactor(double loss_rate, double bias_factor) const;
  double GetHighBandwidthBias(DataRate bandwidth) const;
  double GetObjective(const ChannelParameters& channel_parameters) const;
  DataRate GetSendingRate(DataRate instantaneous_sending_rate) const;
  DataRate GetInstantUpperBound() const { return cached_instant_upper_bound_.value_or(max_bitrate_); }
  void CalculateInstantUpperBound();
  DataRate GetInstantLowerBound() const { return cached_instant_lower_bound_.value_or(DataRate::Zero()); }
  void CalculateInstantLowerBound();
  void CalculateTemporalWeights();
  void NewtonsMethodUpdate(ChannelParameters& channel_parameters) const;
  bool PushBackObservation(std::span<const PacketResult> packet_results);
  bool IsEstimateIncreasingWhenLossLimited(DataRate old_estimate, DataRate new_estimate) const;
  bool IsInLossLimitedState() const { return loss_based_result_.state != LossBasedState::kDelayBasedEstimate; }
  bool CanKeepIncreasingState(DataRate estimate) const;
  DataRate GetMedianSendingRate() const;

  LossBasedBweV2Config cfg_;
  std::optional<DataRate> acknowledged_bitrate_;
  ChannelParameters current_best_estimate_;
  int num_observations_ = 0;
  std::vector<Observation> observations_;
  PartialObservation partial_observation_;
  Timestamp last_send_time_most_recent_observation_ = Timestamp::PlusInfinity();
  Timestamp last_time_estimate_reduced_ = Timestamp::MinusInfinity();
  std::optional<DataRate> cached_instant_upper_bound_;
  std::optional<DataRate> cached_instant_lower_bound_;
  std::vector<double> instant_upper_bound_temporal_weights_;
  std::vector<double> temporal_weights_;
  Timestamp recovering_after_loss_timestamp_ = Timestamp::MinusInfinity();
  DataRate bandwidth_limit_in_current_window_ = DataRate::PlusInfinity();
  DataRate min_bitrate_ = DataRate::KilobitsPerSec(1);
  DataRate max_bitrate_ = DataRate::PlusInfinity();
  DataRate delay_based_estimate_ = DataRate::PlusInfinity();
  Result loss_based_result_;
  HoldInfo last_hold_info_;
  PaddingInfo last_padding_info_;
  double average_reported_loss_ratio_ = 0.0;
  uint64_t decreases_ = 0, holds_ = 0;
};

}  // namespace sm::cc
