#include "sm/cc/loss_based_bwe_v2.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sm::cc {

namespace {

constexpr TimeDelta kInitHoldDuration = TimeDelta::Millis(300);
constexpr TimeDelta kMaxHoldDuration = TimeDelta::Seconds(60);
// kCongestionControllerMinBitrate (bwe_defines.h).
constexpr DataRate kMinBitrate = DataRate::BitsPerSec(5000);

double ToKiloBytes(DataSize s) { return static_cast<double>(s.bytes()) / 1000.0; }

// webrtc units round to nearest when scaling by a double.
// Unit * double, which rounds with std::llround as libwebrtc (#461).
DataRate Scale(DataRate r, double f) { return r * f; }
DataSize Scale(DataSize s, double f) { return s * f; }

double GetLossProbability(double inherent_loss, std::optional<DataRate> loss_limited_bandwidth, std::optional<DataRate> sending_rate) {
  inherent_loss = std::min(std::max(inherent_loss, 0.0), 1.0);
  double loss_probability = inherent_loss;
  if (sending_rate && sending_rate->IsFinite() && loss_limited_bandwidth && loss_limited_bandwidth->IsFinite() &&
      *sending_rate > *loss_limited_bandwidth) {
    loss_probability += (1 - inherent_loss) * static_cast<double>((*sending_rate - *loss_limited_bandwidth).bps()) /
                        static_cast<double>(sending_rate->bps());
  }
  return std::min(std::max(loss_probability, 1.0e-6), 1.0 - 1.0e-6);
}

}  // namespace

LossBasedBweV2::LossBasedBweV2(LossBasedBweV2Config cfg) : cfg_(std::move(cfg)) {
  current_best_estimate_.inherent_loss = cfg_.initial_inherent_loss_estimate;
  observations_.resize(cfg_.observation_window_size);
  temporal_weights_.resize(cfg_.observation_window_size);
  instant_upper_bound_temporal_weights_.resize(cfg_.observation_window_size);
  CalculateTemporalWeights();
  last_hold_info_.duration = kInitHoldDuration;
}

bool LossBasedBweV2::IsReady() const {
  return current_best_estimate_.loss_limited_bandwidth.has_value() && num_observations_ >= cfg_.min_num_observations;
}

LossBasedBweV2::Result LossBasedBweV2::GetLossBasedResult() const {
  if (!IsReady()) {
    return {delay_based_estimate_.IsFinite() ? delay_based_estimate_ : DataRate::PlusInfinity(),
            LossBasedState::kDelayBasedEstimate};
  }
  return loss_based_result_;
}

void LossBasedBweV2::SetAcknowledgedBitrate(DataRate acknowledged_bitrate) {
  if (acknowledged_bitrate.IsFinite()) {
    acknowledged_bitrate_ = acknowledged_bitrate;
    CalculateInstantLowerBound();
  }
}

void LossBasedBweV2::SetBandwidthEstimate(DataRate bandwidth_estimate) {
  if (bandwidth_estimate.IsFinite()) {
    current_best_estimate_.loss_limited_bandwidth = bandwidth_estimate;
    loss_based_result_ = {bandwidth_estimate, LossBasedState::kDelayBasedEstimate};
  }
}

void LossBasedBweV2::SetMinMaxBitrate(DataRate min_bitrate, DataRate max_bitrate) {
  if (min_bitrate.IsFinite()) {
    min_bitrate_ = min_bitrate;
    CalculateInstantLowerBound();
  }
  if (max_bitrate.IsFinite()) max_bitrate_ = max_bitrate;
}

void LossBasedBweV2::UpdateBandwidthEstimate(std::span<const PacketResult> packet_results, DataRate delay_based_estimate,
                                             bool in_alr, Timestamp now) {
  delay_based_estimate_ = delay_based_estimate;
  if (packet_results.empty()) return;
  if (!PushBackObservation(packet_results)) return;

  if (!current_best_estimate_.loss_limited_bandwidth) {
    if (!delay_based_estimate.IsFinite()) return;
    current_best_estimate_.loss_limited_bandwidth = delay_based_estimate;
    loss_based_result_ = {delay_based_estimate, LossBasedState::kDelayBasedEstimate};
  }

  ChannelParameters best_candidate = current_best_estimate_;
  double objective_max = std::numeric_limits<double>::lowest();
  for (ChannelParameters candidate : GetCandidates(in_alr)) {
    NewtonsMethodUpdate(candidate);
    const double candidate_objective = GetObjective(candidate);
    if (candidate_objective > objective_max ||
        (candidate_objective == objective_max && *candidate.loss_limited_bandwidth > *best_candidate.loss_limited_bandwidth)) {
      // The highest objective, or the higher bandwidth if they are tied.
      objective_max = candidate_objective;
      best_candidate = candidate;
    }
  }
  if (*best_candidate.loss_limited_bandwidth < *current_best_estimate_.loss_limited_bandwidth) {
    last_time_estimate_reduced_ = last_send_time_most_recent_observation_;
  }

  // Do not increase the estimate if the average loss is greater than the current inherent loss.
  if (average_reported_loss_ratio_ > best_candidate.inherent_loss && cfg_.not_increase_if_inherent_loss_less_than_average_loss &&
      *current_best_estimate_.loss_limited_bandwidth < *best_candidate.loss_limited_bandwidth) {
    best_candidate.loss_limited_bandwidth = current_best_estimate_.loss_limited_bandwidth;
  }

  if (IsInLossLimitedState()) {
    // Bound the increase if the estimate was increased less than delayed_increase_window ago and the best
    // candidate is above bandwidth_limit_in_current_window.
    if (recovering_after_loss_timestamp_.IsFinite() &&
        recovering_after_loss_timestamp_ + cfg_.delayed_increase_window > last_send_time_most_recent_observation_ &&
        *best_candidate.loss_limited_bandwidth > bandwidth_limit_in_current_window_) {
      best_candidate.loss_limited_bandwidth = bandwidth_limit_in_current_window_;
    }

    const bool increasing_when_loss_limited =
        IsEstimateIncreasingWhenLossLimited(*current_best_estimate_.loss_limited_bandwidth, *best_candidate.loss_limited_bandwidth);
    // Bound the best candidate by the acked bitrate.
    if (increasing_when_loss_limited && acknowledged_bitrate_) {
      double rampup_factor = cfg_.bandwidth_rampup_upper_bound_factor;
      if (last_hold_info_.rate.IsFinite() &&
          static_cast<double>(acknowledged_bitrate_->bps()) <
              cfg_.bandwidth_rampup_hold_threshold * static_cast<double>(last_hold_info_.rate.bps())) {
        rampup_factor = cfg_.bandwidth_rampup_upper_bound_factor_in_hold;
      }
      best_candidate.loss_limited_bandwidth =
          std::max(*current_best_estimate_.loss_limited_bandwidth,
                   std::min(*best_candidate.loss_limited_bandwidth, Scale(*acknowledged_bitrate_, rampup_factor)));
      // Increase the estimate by at least 1 bps so that the state switches to kIncreasing, which triggers padding.
      if (loss_based_result_.state == LossBasedState::kDecreasing &&
          *best_candidate.loss_limited_bandwidth == *current_best_estimate_.loss_limited_bandwidth) {
        best_candidate.loss_limited_bandwidth = *current_best_estimate_.loss_limited_bandwidth + DataRate::BitsPerSec(1);
      }
    }
  }

  DataRate bounded_bandwidth_estimate = DataRate::PlusInfinity();
  if (delay_based_estimate_.IsFinite()) {
    bounded_bandwidth_estimate =
        std::max(GetInstantLowerBound(),
                 std::min({*best_candidate.loss_limited_bandwidth, GetInstantUpperBound(), delay_based_estimate_}));
  } else {
    bounded_bandwidth_estimate =
        std::max(GetInstantLowerBound(), std::min(*best_candidate.loss_limited_bandwidth, GetInstantUpperBound()));
  }
  if (cfg_.bound_best_candidate && bounded_bandwidth_estimate < *best_candidate.loss_limited_bandwidth) {
    // If the network is lossy, cap the best estimate by the instant upper bound (450 kbps at 50 % loss);
    // otherwise by the delay-based estimate or the maximum bitrate.
    current_best_estimate_.loss_limited_bandwidth = bounded_bandwidth_estimate;
    current_best_estimate_.inherent_loss = 0;
  } else {
    current_best_estimate_ = best_candidate;
    if (cfg_.lower_bound_by_acked_rate_factor > 0.0) {
      current_best_estimate_.loss_limited_bandwidth = std::max(*current_best_estimate_.loss_limited_bandwidth, GetInstantLowerBound());
    }
  }

  if (loss_based_result_.state == LossBasedState::kDecreasing && last_hold_info_.timestamp > last_send_time_most_recent_observation_ &&
      bounded_bandwidth_estimate < delay_based_estimate_) {
    // The acked rate is the lower bound of the HOLD rate.
    if (cfg_.lower_bound_by_acked_rate_factor > 0.0) {
      last_hold_info_.rate = std::max(GetInstantLowerBound(), last_hold_info_.rate);
    }
    // The estimate may not rise above the HOLD rate: HOLD exists so as not to ramp straight back to a rate that
    // caused loss.
    loss_based_result_.bandwidth_estimate = std::min(last_hold_info_.rate, bounded_bandwidth_estimate);
    ++holds_;
    return;
  }

  if (IsEstimateIncreasingWhenLossLimited(loss_based_result_.bandwidth_estimate, bounded_bandwidth_estimate) &&
      CanKeepIncreasingState(bounded_bandwidth_estimate) && bounded_bandwidth_estimate < delay_based_estimate_ &&
      bounded_bandwidth_estimate < max_bitrate_) {
    if (cfg_.padding_duration > TimeDelta::Zero() &&
        (!last_padding_info_.padding_rate || bounded_bandwidth_estimate > *last_padding_info_.padding_rate)) {
      // Start a new padding duration.
      last_padding_info_.padding_rate = bounded_bandwidth_estimate;
      last_padding_info_.padding_timestamp = last_send_time_most_recent_observation_;
    }
    loss_based_result_.state =
        cfg_.padding_duration > TimeDelta::Zero() ? LossBasedState::kIncreaseUsingPadding : LossBasedState::kIncreasing;
  } else if (bounded_bandwidth_estimate < delay_based_estimate_ && bounded_bandwidth_estimate < max_bitrate_) {
    if (loss_based_result_.state != LossBasedState::kDecreasing && cfg_.hold_duration_factor > 0) {
      last_hold_info_ = {last_send_time_most_recent_observation_ + last_hold_info_.duration,
                         std::min(kMaxHoldDuration, last_hold_info_.duration * cfg_.hold_duration_factor), bounded_bandwidth_estimate};
    }
    last_padding_info_ = PaddingInfo();
    if (loss_based_result_.state != LossBasedState::kDecreasing) {
      ++decreases_;
      SM_EVENT(cfg_.events, now, "loss_bwe_v2", "decrease", bounded_bandwidth_estimate.bps(),
               static_cast<int64_t>(average_reported_loss_ratio_ * 1e6));
    }
    loss_based_result_.state = LossBasedState::kDecreasing;
  } else {
    // Reset the HOLD info if the delay-based estimate works, to avoid getting stuck at a low bitrate.
    last_hold_info_ = {Timestamp::MinusInfinity(), kInitHoldDuration, DataRate::PlusInfinity()};
    last_padding_info_ = PaddingInfo();
    loss_based_result_.state = LossBasedState::kDelayBasedEstimate;
  }
  loss_based_result_.bandwidth_estimate = bounded_bandwidth_estimate;

  if (IsInLossLimitedState() && (!recovering_after_loss_timestamp_.IsFinite() ||
                                 recovering_after_loss_timestamp_ + cfg_.delayed_increase_window < last_send_time_most_recent_observation_)) {
    bandwidth_limit_in_current_window_ =
        std::max(kMinBitrate, Scale(*current_best_estimate_.loss_limited_bandwidth, cfg_.max_increase_factor));
    recovering_after_loss_timestamp_ = last_send_time_most_recent_observation_;
  }
}

bool LossBasedBweV2::IsEstimateIncreasingWhenLossLimited(DataRate old_estimate, DataRate new_estimate) const {
  return (old_estimate < new_estimate ||
          (old_estimate == new_estimate && (loss_based_result_.state == LossBasedState::kIncreasing ||
                                            loss_based_result_.state == LossBasedState::kIncreaseUsingPadding))) &&
         IsInLossLimitedState();
}

void LossBasedBweV2::UpdateAverageReportedLossRatio() {
  average_reported_loss_ratio_ =
      cfg_.use_byte_loss_rate ? CalculateAverageReportedByteLossRatio() : CalculateAverageReportedPacketLossRatio();
}

double LossBasedBweV2::CalculateAverageReportedPacketLossRatio() const {
  if (num_observations_ <= 0) return 0.0;
  double num_packets = 0;
  double num_lost_packets = 0;
  for (const Observation& observation : observations_) {
    if (!observation.IsInitialized()) continue;
    const double w = instant_upper_bound_temporal_weights_[(num_observations_ - 1) - observation.id];
    num_packets += w * observation.num_packets;
    num_lost_packets += w * observation.num_lost_packets;
  }
  return num_lost_packets / num_packets;
}

// The average over the window, without the observations with the lowest and highest loss ratio (loss spikes).
double LossBasedBweV2::CalculateAverageReportedByteLossRatio() const {
  if (num_observations_ <= 0) return 0.0;
  DataSize total_bytes, lost_bytes;
  double min_loss_rate = 1.0;
  double max_loss_rate = 0.0;
  DataSize min_lost_bytes, max_lost_bytes, min_bytes_received, max_bytes_received;
  DataRate send_rate_of_max_loss_observation = DataRate::Zero();
  for (const Observation& observation : observations_) {
    if (!observation.IsInitialized()) continue;
    const double w = instant_upper_bound_temporal_weights_[(num_observations_ - 1) - observation.id];
    total_bytes += Scale(observation.size, w);
    lost_bytes += Scale(observation.lost_size, w);
    const double loss_rate = observation.size.bytes() != 0
                                 ? static_cast<double>(observation.lost_size.bytes()) / static_cast<double>(observation.size.bytes())
                                 : 0.0;
    if (num_observations_ > 3) {
      if (loss_rate > max_loss_rate) {
        max_loss_rate = loss_rate;
        max_lost_bytes = Scale(observation.lost_size, w);
        max_bytes_received = Scale(observation.size, w);
        send_rate_of_max_loss_observation = observation.sending_rate.value_or(DataRate::Zero());
      }
      if (loss_rate < min_loss_rate) {
        min_loss_rate = loss_rate;
        min_lost_bytes = Scale(observation.lost_size, w);
        min_bytes_received = Scale(observation.size, w);
      }
    }
  }
  if (Scale(GetMedianSendingRate(), cfg_.median_sending_rate_factor) <= send_rate_of_max_loss_observation) {
    // The median sending rate is less than half of the rate of the observation with the highest loss: we suddenly
    // sent a lot of data, so the loss may not be a spike.
    return static_cast<double>(lost_bytes.bytes()) / static_cast<double>(total_bytes.bytes());
  }
  if (total_bytes == max_bytes_received + min_bytes_received) {
    // Happens if the observation window was 2.
    return static_cast<double>(lost_bytes.bytes()) / static_cast<double>(total_bytes.bytes());
  }
  return static_cast<double>((lost_bytes - min_lost_bytes - max_lost_bytes).bytes()) /
         static_cast<double>((total_bytes - max_bytes_received - min_bytes_received).bytes());
}

DataRate LossBasedBweV2::GetCandidateBandwidthUpperBound() const {
  DataRate candidate_bandwidth_upper_bound = max_bitrate_;
  if (IsInLossLimitedState() && bandwidth_limit_in_current_window_.IsFinite()) {
    candidate_bandwidth_upper_bound = bandwidth_limit_in_current_window_;
  }
  if (!acknowledged_bitrate_) return candidate_bandwidth_upper_bound;
  if (cfg_.rampup_acceleration_max_factor > 0.0) {
    const TimeDelta time_since_bandwidth_reduced =
        std::min(cfg_.rampup_acceleration_maxout_time,
                 std::max(TimeDelta::Zero(), last_send_time_most_recent_observation_ - last_time_estimate_reduced_));
    const double rampup_acceleration =
        cfg_.rampup_acceleration_max_factor * (time_since_bandwidth_reduced / cfg_.rampup_acceleration_maxout_time);
    candidate_bandwidth_upper_bound = candidate_bandwidth_upper_bound + Scale(*acknowledged_bitrate_, rampup_acceleration);
  }
  return candidate_bandwidth_upper_bound;
}

std::vector<LossBasedBweV2::ChannelParameters> LossBasedBweV2::GetCandidates(bool in_alr) const {
  const ChannelParameters best_estimate = current_best_estimate_;
  std::vector<DataRate> bandwidths;
  for (double candidate_factor : cfg_.candidate_factors) {
    bandwidths.push_back(Scale(*best_estimate.loss_limited_bandwidth, candidate_factor));
  }
  if (acknowledged_bitrate_ && cfg_.append_acknowledged_rate_candidate) {
    if (!(cfg_.not_use_acked_rate_in_alr && in_alr) ||
        (cfg_.padding_duration > TimeDelta::Zero() &&
         last_padding_info_.padding_timestamp + cfg_.padding_duration >= last_send_time_most_recent_observation_)) {
      bandwidths.push_back(Scale(*acknowledged_bitrate_, cfg_.bandwidth_backoff_lower_bound_factor));
    }
  }
  if (delay_based_estimate_.IsFinite() && cfg_.append_delay_based_estimate_candidate) {
    if (delay_based_estimate_ > *best_estimate.loss_limited_bandwidth) bandwidths.push_back(delay_based_estimate_);
  }
  if (in_alr && cfg_.append_upper_bound_candidate_in_alr && *best_estimate.loss_limited_bandwidth > GetInstantUpperBound()) {
    bandwidths.push_back(GetInstantUpperBound());
  }
  const DataRate candidate_bandwidth_upper_bound = GetCandidateBandwidthUpperBound();
  std::vector<ChannelParameters> candidates(bandwidths.size());
  for (size_t i = 0; i < bandwidths.size(); ++i) {
    ChannelParameters candidate = best_estimate;
    candidate.loss_limited_bandwidth =
        std::min(bandwidths[i], std::max(*best_estimate.loss_limited_bandwidth, candidate_bandwidth_upper_bound));
    candidate.inherent_loss = GetFeasibleInherentLoss(candidate);
    candidates[i] = candidate;
  }
  return candidates;
}

LossBasedBweV2::Derivatives LossBasedBweV2::GetDerivatives(const ChannelParameters& channel_parameters) const {
  Derivatives derivatives;
  for (const Observation& observation : observations_) {
    if (!observation.IsInitialized()) continue;
    const double loss_probability =
        GetLossProbability(channel_parameters.inherent_loss, channel_parameters.loss_limited_bandwidth, observation.sending_rate);
    const double temporal_weight = temporal_weights_[(num_observations_ - 1) - observation.id];
    if (cfg_.use_byte_loss_rate) {
      derivatives.first += temporal_weight * ((ToKiloBytes(observation.lost_size) / loss_probability) -
                                              (ToKiloBytes(observation.size - observation.lost_size) / (1.0 - loss_probability)));
      derivatives.second -= temporal_weight * ((ToKiloBytes(observation.lost_size) / std::pow(loss_probability, 2)) +
                                               (ToKiloBytes(observation.size - observation.lost_size) / std::pow(1.0 - loss_probability, 2)));
    } else {
      derivatives.first += temporal_weight * ((observation.num_lost_packets / loss_probability) -
                                              (observation.num_received_packets / (1.0 - loss_probability)));
      derivatives.second -= temporal_weight * ((observation.num_lost_packets / std::pow(loss_probability, 2)) +
                                               (observation.num_received_packets / std::pow(1.0 - loss_probability, 2)));
    }
  }
  // Mathematically guaranteed to be negative.
  if (derivatives.second >= 0.0) derivatives.second = -1.0e-6;
  return derivatives;
}

double LossBasedBweV2::GetFeasibleInherentLoss(const ChannelParameters& channel_parameters) const {
  return std::min(std::max(channel_parameters.inherent_loss, cfg_.inherent_loss_lower_bound),
                  GetInherentLossUpperBound(*channel_parameters.loss_limited_bandwidth));
}

double LossBasedBweV2::GetInherentLossUpperBound(DataRate bandwidth) const {
  if (bandwidth.IsZero()) return 1.0;
  const double inherent_loss_upper_bound =
      cfg_.inherent_loss_upper_bound_offset + cfg_.inherent_loss_upper_bound_bandwidth_balance / bandwidth;
  return std::min(inherent_loss_upper_bound, 1.0);
}

double LossBasedBweV2::AdjustBiasFactor(double loss_rate, double bias_factor) const {
  return bias_factor * (cfg_.loss_threshold_of_high_bandwidth_preference - loss_rate) /
         (cfg_.bandwidth_preference_smoothing_factor + std::abs(cfg_.loss_threshold_of_high_bandwidth_preference - loss_rate));
}

double LossBasedBweV2::GetHighBandwidthBias(DataRate bandwidth) const {
  if (bandwidth.IsFinite()) {
    return AdjustBiasFactor(average_reported_loss_ratio_, cfg_.higher_bandwidth_bias_factor) * bandwidth.kbps() +
           AdjustBiasFactor(average_reported_loss_ratio_, cfg_.higher_log_bandwidth_bias_factor) * std::log(1.0 + bandwidth.kbps());
  }
  return 0.0;
}

double LossBasedBweV2::GetObjective(const ChannelParameters& channel_parameters) const {
  double objective = 0.0;
  const double high_bandwidth_bias = GetHighBandwidthBias(*channel_parameters.loss_limited_bandwidth);
  for (const Observation& observation : observations_) {
    if (!observation.IsInitialized()) continue;
    const double loss_probability =
        GetLossProbability(channel_parameters.inherent_loss, channel_parameters.loss_limited_bandwidth, observation.sending_rate);
    const double temporal_weight = temporal_weights_[(num_observations_ - 1) - observation.id];
    if (cfg_.use_byte_loss_rate) {
      objective += temporal_weight * ((ToKiloBytes(observation.lost_size) * std::log(loss_probability)) +
                                      (ToKiloBytes(observation.size - observation.lost_size) * std::log(1.0 - loss_probability)));
      objective += temporal_weight * high_bandwidth_bias * ToKiloBytes(observation.size);
    } else {
      objective += temporal_weight * ((observation.num_lost_packets * std::log(loss_probability)) +
                                      (observation.num_received_packets * std::log(1.0 - loss_probability)));
      objective += temporal_weight * high_bandwidth_bias * observation.num_packets;
    }
  }
  return objective;
}

DataRate LossBasedBweV2::GetSendingRate(DataRate instantaneous_sending_rate) const {
  if (num_observations_ <= 0) return instantaneous_sending_rate;
  const Observation& most_recent_observation = observations_[(num_observations_ - 1) % cfg_.observation_window_size];
  const DataRate sending_rate_previous_observation = most_recent_observation.sending_rate.value_or(DataRate::Zero());
  return Scale(sending_rate_previous_observation, cfg_.sending_rate_smoothing_factor) +
         Scale(instantaneous_sending_rate, 1.0 - cfg_.sending_rate_smoothing_factor);
}

void LossBasedBweV2::CalculateInstantUpperBound() {
  DataRate instant_limit = max_bitrate_;
  if (average_reported_loss_ratio_ > cfg_.instant_upper_bound_loss_offset) {
    instant_limit = Scale(cfg_.instant_upper_bound_bandwidth_balance,
                          1.0 / (average_reported_loss_ratio_ - cfg_.instant_upper_bound_loss_offset));
  }
  cached_instant_upper_bound_ = instant_limit;
}

void LossBasedBweV2::CalculateInstantLowerBound() {
  DataRate instance_lower_bound = DataRate::Zero();
  if (acknowledged_bitrate_ && cfg_.lower_bound_by_acked_rate_factor > 0.0) {
    instance_lower_bound = Scale(*acknowledged_bitrate_, cfg_.lower_bound_by_acked_rate_factor);
  }
  if (min_bitrate_.IsFinite()) instance_lower_bound = std::max(instance_lower_bound, min_bitrate_);
  cached_instant_lower_bound_ = instance_lower_bound;
}

void LossBasedBweV2::CalculateTemporalWeights() {
  for (int i = 0; i < cfg_.observation_window_size; ++i) {
    temporal_weights_[i] = std::pow(cfg_.temporal_weight_factor, i);
    instant_upper_bound_temporal_weights_[i] = std::pow(cfg_.instant_upper_bound_temporal_weight_factor, i);
  }
}

void LossBasedBweV2::NewtonsMethodUpdate(ChannelParameters& channel_parameters) const {
  if (num_observations_ <= 0) return;
  for (int i = 0; i < cfg_.newton_iterations; ++i) {
    const Derivatives derivatives = GetDerivatives(channel_parameters);
    channel_parameters.inherent_loss -= cfg_.newton_step_size * derivatives.first / derivatives.second;
    channel_parameters.inherent_loss = GetFeasibleInherentLoss(channel_parameters);
  }
}

bool LossBasedBweV2::PushBackObservation(std::span<const PacketResult> packet_results) {
  if (packet_results.empty()) return false;
  partial_observation_.num_packets += static_cast<int>(packet_results.size());
  Timestamp last_send_time = Timestamp::MinusInfinity();
  Timestamp first_send_time = Timestamp::PlusInfinity();
  for (const PacketResult& packet : packet_results) {
    // Lost only when a feedback covers the packet and reports it not received; a later report of the packet as
    // received removes it again.
    if (packet.received()) {
      partial_observation_.lost_packets.erase(packet.seq);
    } else {
      partial_observation_.lost_packets.emplace(packet.seq, packet.size);
    }
    partial_observation_.size += packet.size;
    last_send_time = std::max(last_send_time, packet.send_time);
    first_send_time = std::min(first_send_time, packet.send_time);
  }
  // The first packet report we have received.
  if (!last_send_time_most_recent_observation_.IsFinite()) last_send_time_most_recent_observation_ = first_send_time;

  const TimeDelta observation_duration = last_send_time - last_send_time_most_recent_observation_;
  // Too small to be meaningful.
  if (observation_duration <= TimeDelta::Zero() || observation_duration < cfg_.observation_duration_lower_bound) return false;

  last_send_time_most_recent_observation_ = last_send_time;

  Observation observation;
  observation.num_packets = partial_observation_.num_packets;
  observation.num_lost_packets = static_cast<int>(partial_observation_.lost_packets.size());
  observation.num_received_packets = observation.num_packets - observation.num_lost_packets;
  observation.sending_rate = GetSendingRate(DataRate::FromSizeAndTime(partial_observation_.size, observation_duration));
  for (const auto& [key, packet_size] : partial_observation_.lost_packets) observation.lost_size += packet_size;
  observation.size = partial_observation_.size;
  observation.id = num_observations_++;
  observations_[observation.id % cfg_.observation_window_size] = observation;

  partial_observation_ = PartialObservation();
  UpdateAverageReportedLossRatio();
  CalculateInstantUpperBound();
  return true;
}

bool LossBasedBweV2::CanKeepIncreasingState(DataRate estimate) const {
  if (cfg_.padding_duration == TimeDelta::Zero() || loss_based_result_.state != LossBasedState::kIncreaseUsingPadding) return true;
  // Keep kIncreaseUsingPadding while it has lasted less than the padding duration or the estimate increases.
  return last_padding_info_.padding_timestamp + cfg_.padding_duration >= last_send_time_most_recent_observation_ ||
         (last_padding_info_.padding_rate && *last_padding_info_.padding_rate < estimate);
}

DataRate LossBasedBweV2::GetMedianSendingRate() const {
  std::vector<DataRate> sending_rates;
  for (const Observation& observation : observations_) {
    if (!observation.IsInitialized() || !observation.sending_rate || !observation.sending_rate->IsFinite() ||
        observation.sending_rate->IsZero()) {
      continue;
    }
    sending_rates.push_back(*observation.sending_rate);
  }
  if (sending_rates.empty()) return DataRate::Zero();
  std::sort(sending_rates.begin(), sending_rates.end());
  if (sending_rates.size() % 2 == 0) {
    return Scale(sending_rates[sending_rates.size() / 2 - 1] + sending_rates[sending_rates.size() / 2], 0.5);
  }
  return sending_rates[sending_rates.size() / 2];
}

void LossBasedBweV2::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("inherent_loss", current_best_estimate_.inherent_loss);
  w.Gauge("avg_loss", average_reported_loss_ratio_);
  if (current_best_estimate_.loss_limited_bandwidth) w.Gauge("limited_kbps", current_best_estimate_.loss_limited_bandwidth->kbps());
  w.Gauge("state", static_cast<double>(static_cast<int>(loss_based_result_.state)));
  w.Gauge("observations", num_observations_);
  w.Counter("decreases", decreases_);
  w.Counter("holds", holds_);
}

}  // namespace sm::cc
