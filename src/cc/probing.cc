#include "sm/cc/probing.h"

#include <algorithm>
#include <cmath>

namespace sm::cc {

namespace {
// libwebrtc's unit arithmetic: DataRate * double and DataRate * TimeDelta round to nearest.
DataRate Scale(DataRate r, double f) { return DataRate::BitsPerSec(static_cast<int64_t>(std::llround(static_cast<double>(r.bps()) * f))); }
DataSize Mul(DataRate r, TimeDelta t) {
  return DataSize::Bytes(static_cast<int64_t>(std::llround(static_cast<double>(r.bps()) * static_cast<double>(t.us()) / 8e6)));
}

// probe_controller.cc constants.
constexpr TimeDelta kMaxWaitingTimeForProbingResult = TimeDelta::Seconds(1);
constexpr DataRate kDefaultMaxProbingBitrate = DataRate::KilobitsPerSec(5000);
constexpr double kBitrateDropThreshold = 0.66;
constexpr TimeDelta kBitrateDropTimeout = TimeDelta::Seconds(5);
constexpr double kProbeFractionAfterDrop = 0.85;
constexpr TimeDelta kAlrEndedTimeout = TimeDelta::Seconds(3);
constexpr TimeDelta kMinTimeBetweenAlrProbes = TimeDelta::Seconds(5);
constexpr double kProbeUncertainty = 0.05;

// probe_bitrate_estimator.cc constants.
constexpr double kMinReceivedProbesRatio = 0.80;
constexpr double kMinReceivedBytesRatio = 0.80;
constexpr double kMaxValidRatio = 2.0;
constexpr double kMinRatioForUnsaturatedLink = 0.9;
constexpr double kTargetUtilizationFraction = 0.95;
constexpr TimeDelta kMaxClusterHistory = TimeDelta::Seconds(1);
constexpr TimeDelta kMaxProbeInterval = TimeDelta::Seconds(1);
}  // namespace

std::vector<ProbeCluster> ProbeController::Initiate(std::vector<DataRate> rates, Timestamp now, bool further) {
  std::vector<ProbeCluster> out;
  for (DataRate r : rates) {
    r = std::min(r, max_);
    if (r <= estimate_) continue;
    ProbeCluster c;
    c.id = next_id_++;
    c.rate = r;
    c.min_probes = 5;
    c.min_bytes = r * cfg_.probe_duration;
    out.push_back(c);
    ++clusters_;
    SM_EVENT(cfg_.events, now, "probe", "cluster", r.bps(), c.id);
  }
  if (!out.empty()) last_probe_ = now;
  // Keep probing upward while probes keep succeeding.
  min_rate_to_continue_ = (further && !out.empty()) ? out.back().rate * cfg_.further_probe_threshold
                                                    : DataRate::PlusInfinity();
  return out;
}

std::vector<ProbeCluster> ProbeController::SetBitrates(DataRate min, DataRate start, DataRate max, Timestamp now) {
  (void)min;
  max_ = max;
  estimate_ = start;
  return Initiate({start * cfg_.first_multiplier, start * cfg_.second_multiplier}, now, true);
}

std::vector<ProbeCluster> ProbeController::SetEstimate(DataRate estimate, Timestamp now) {
  estimate_ = estimate;
  if (estimate >= min_rate_to_continue_ && estimate < max_) {
    return Initiate({estimate * cfg_.further_multiplier}, now, true);
  }
  return {};
}

std::vector<ProbeCluster> ProbeController::SetMaxBitrate(DataRate max, Timestamp now) {
  DataRate old = max_;
  max_ = max;
  if (max > old && estimate_ >= old * 0.9) return Initiate({max}, now, false);
  return {};
}

void ProbeController::SetApplicationLimited(bool alr, Timestamp now) {
  if (alr && !alr_) alr_start_ = now;
  alr_ = alr;
}

std::vector<ProbeCluster> ProbeController::Process(Timestamp now) {
  if (periodic_alr_ && alr_ && now - std::max(alr_start_, last_probe_) >= cfg_.alr_probe_interval) {
    return Initiate({estimate_ * 2.0}, now, true);
  }
  return {};
}

void ProbeController::ExportMetrics(MetricsWriter& w) const {
  w.Counter("clusters", clusters_);
  w.Gauge("alr", alr_ ? 1 : 0);
}

void ProbeBitrateEstimator::SetClusterInfo(const ProbeCluster& c) {
  if (libwebrtc_) {
    configs_[c.id] = c;
    while (configs_.size() > 64) configs_.erase(configs_.begin());
  } else {
    clusters_[c.id].config = c;
  }
}

// ProbeBitrateEstimator::HandleProbeAndEstimateBitrate.
std::optional<DataRate> ProbeBitrateEstimator::OnPacketResultV2(const PacketResult& r) {
  if (!r.received()) return std::nullopt;  // libwebrtc is given the received packets only.
  auto cfg_it = configs_.find(r.probe_cluster);
  if (cfg_it == configs_.end()) return std::nullopt;
  EraseOldClusters(r.receive_time);
  Cluster& c = clusters_[r.probe_cluster];
  const int64_t sz = r.size.bytes();
  if (r.send_time < c.first_send) c.first_send = r.send_time;
  if (r.send_time > c.last_send) {
    c.last_send = r.send_time;
    c.size_last_send = sz;
  }
  if (r.receive_time < c.first_recv) {
    c.first_recv = r.receive_time;
    c.size_first_recv = sz;
  }
  if (r.receive_time > c.last_recv) c.last_recv = r.receive_time;
  c.bytes += sz;
  c.count += 1;

  const ProbeCluster& cfg = cfg_it->second;
  const int min_probes = static_cast<int>(cfg.min_probes * kMinReceivedProbesRatio);
  const int64_t min_size = static_cast<int64_t>(std::llround(static_cast<double>(cfg.min_bytes.bytes()) * kMinReceivedBytesRatio));
  if (c.count < min_probes || c.bytes < min_size) return std::nullopt;

  const TimeDelta send_interval = c.last_send - c.first_send;
  const TimeDelta receive_interval = c.last_recv - c.first_recv;
  if (send_interval <= TimeDelta::Zero() || send_interval > kMaxProbeInterval || receive_interval <= TimeDelta::Zero() ||
      receive_interval > kMaxProbeInterval) {
    ++invalid_;
    return std::nullopt;
  }
  // The intervals do not include the time to send the last packet or to receive
  // the first, so those sizes are left out of the rates.
  const DataRate send_rate = DataRate::FromSizeAndTime(DataSize::Bytes(c.bytes - c.size_last_send), send_interval);
  const DataRate receive_rate = DataRate::FromSizeAndTime(DataSize::Bytes(c.bytes - c.size_first_recv), receive_interval);
  const double ratio = receive_rate / send_rate;
  if (ratio > kMaxValidRatio) {
    ++invalid_;
    return std::nullopt;
  }
  DataRate res = std::min(send_rate, receive_rate);
  // Receiving at a significantly lower rate than we sent at suggests the true
  // link capacity was found: set the target slightly lower to not overuse at once.
  if (receive_rate < Scale(send_rate, kMinRatioForUnsaturatedLink)) res = Scale(receive_rate, kTargetUtilizationFraction);
  last_ = res;
  ++estimates_;
  return res;
}

void ProbeBitrateEstimator::EraseOldClusters(Timestamp timestamp) {
  for (auto it = clusters_.begin(); it != clusters_.end();) {
    if (it->second.last_recv + kMaxClusterHistory < timestamp) {
      it = clusters_.erase(it);
    } else {
      ++it;
    }
  }
}

std::optional<DataRate> ProbeBitrateEstimator::OnPacketResult(const PacketResult& r) {
  if (r.probe_cluster < 0) return std::nullopt;
  if (libwebrtc_) return OnPacketResultV2(r);
  auto it = clusters_.find(r.probe_cluster);
  if (it == clusters_.end()) return std::nullopt;
  Cluster& c = it->second;
  if (c.done) return std::nullopt;
  c.count += 1;
  int64_t sz = r.size.bytes();
  if (r.send_time < c.first_send) c.first_send = r.send_time;
  if (r.send_time > c.last_send) {
    c.last_send = r.send_time;
    c.size_last_send = sz;
  }
  if (r.received()) {
    if (r.receive_time < c.first_recv) {
      c.first_recv = r.receive_time;
      c.size_first_recv = sz;
    }
    c.last_recv = std::max(c.last_recv, r.receive_time);
    c.bytes += sz;
  }
  const int min_packets = std::max(2, c.config.min_probes * 8 / 10);
  if (c.count < min_packets || c.bytes < c.config.min_bytes.bytes() * 8 / 10) return std::nullopt;
  c.done = true;
  TimeDelta send_iv = c.last_send - c.first_send;
  TimeDelta recv_iv = c.last_recv - c.first_recv;
  if (send_iv <= TimeDelta::Zero() || send_iv > TimeDelta::Seconds(1) || recv_iv <= TimeDelta::Zero() ||
      recv_iv > TimeDelta::Seconds(1)) {
    ++invalid_;
    return std::nullopt;
  }
  DataRate send_rate = DataRate::FromSizeAndTime(DataSize::Bytes(c.bytes - c.size_last_send), send_iv);
  DataRate recv_rate = DataRate::FromSizeAndTime(DataSize::Bytes(c.bytes - c.size_first_recv), recv_iv);
  DataRate est = std::min(send_rate, recv_rate);
  if (recv_rate < send_rate * 0.9) est = recv_rate * 0.95;  // Link saturated.
  last_ = est;
  ++estimates_;
  return est;
}

std::optional<DataRate> ProbeBitrateEstimator::FetchAndResetLastEstimate() {
  auto r = last_;
  last_.reset();
  return r;
}

// --- ProbeControllerV2 (libwebrtc ProbeController) --------------------------------

ProbeControllerV2::ProbeControllerV2(ProbeControllerV2Config cfg) : cfg_(cfg) { Reset(Timestamp::Micros(0)); }

std::vector<ProbeCluster> ProbeControllerV2::SetBitrates(DataRate min_bitrate, DataRate start_bitrate, DataRate max_bitrate, Timestamp at_time) {
  if (start_bitrate > DataRate::Zero()) {
    start_bitrate_ = start_bitrate;
    estimated_bitrate_ = start_bitrate;
  } else if (start_bitrate_.IsZero()) {
    start_bitrate_ = min_bitrate;
  }
  // `max_bitrate_` must be set before InitiateProbing is called.
  const DataRate old_max_bitrate = max_bitrate_;
  max_bitrate_ = max_bitrate.IsFinite() ? max_bitrate : kDefaultMaxProbingBitrate;

  switch (state_) {
    case State::kInit:
      if (network_available_) return InitiateExponentialProbing(at_time);
      break;
    case State::kWaitingForProbingResult:
      break;
    case State::kProbingComplete:
      // If the new max bitrate is higher than both the old max bitrate and the
      // estimate then initiate probing.
      if (!estimated_bitrate_.IsZero() && old_max_bitrate < max_bitrate_ && estimated_bitrate_ < max_bitrate_) {
        return InitiateProbing(at_time, {max_bitrate_}, false);
      }
      break;
  }
  return {};
}

std::vector<ProbeCluster> ProbeControllerV2::OnMaxTotalAllocatedBitrate(DataRate max_total_allocated_bitrate, Timestamp at_time) {
  const bool in_alr = alr_start_time_.has_value();
  const bool allow_allocation_probe = in_alr || cfg_.probe_on_max_allocated_bitrate_change_without_alr;
  if (cfg_.probe_on_max_allocated_bitrate_change && state_ == State::kProbingComplete &&
      max_total_allocated_bitrate > max_total_allocated_bitrate_ && estimated_bitrate_ < max_bitrate_ &&
      estimated_bitrate_ < max_total_allocated_bitrate && allow_allocation_probe) {
    max_total_allocated_bitrate_ = max_total_allocated_bitrate;
    if (cfg_.first_allocation_probe_scale == 0) return {};

    DataRate first_probe_rate = Scale(max_total_allocated_bitrate, cfg_.first_allocation_probe_scale);
    const DataRate current_bwe_limit = Scale(estimated_bitrate_, cfg_.allocation_probe_limit_by_current_scale);
    bool limited_by_current_bwe = current_bwe_limit < first_probe_rate;
    if (limited_by_current_bwe) first_probe_rate = current_bwe_limit;

    std::vector<DataRate> probes = {first_probe_rate};
    if (!limited_by_current_bwe && cfg_.second_allocation_probe_scale != 0) {
      DataRate second_probe_rate = Scale(max_total_allocated_bitrate, cfg_.second_allocation_probe_scale);
      limited_by_current_bwe = current_bwe_limit < second_probe_rate;
      if (limited_by_current_bwe) second_probe_rate = current_bwe_limit;
      if (second_probe_rate > first_probe_rate) probes.push_back(second_probe_rate);
    }
    return InitiateProbing(at_time, probes, limited_by_current_bwe);
  }
  if (!max_total_allocated_bitrate.IsZero()) last_allowed_repeated_initial_probe_ = at_time;
  max_total_allocated_bitrate_ = max_total_allocated_bitrate;
  return {};
}

std::vector<ProbeCluster> ProbeControllerV2::OnNetworkAvailability(bool available, Timestamp at_time) {
  network_available_ = available;
  if (!network_available_ && state_ == State::kWaitingForProbingResult) {
    state_ = State::kProbingComplete;
    min_bitrate_to_probe_further_ = DataRate::PlusInfinity();
  }
  if (network_available_ && state_ == State::kInit && !start_bitrate_.IsZero()) return InitiateExponentialProbing(at_time);
  return {};
}

void ProbeControllerV2::UpdateState(State s) {
  state_ = s;
  if (s == State::kProbingComplete) min_bitrate_to_probe_further_ = DataRate::PlusInfinity();
}

std::vector<ProbeCluster> ProbeControllerV2::InitiateExponentialProbing(Timestamp at_time) {
  // When probing at 1.8 Mbps (6x 300), this represents a threshold of 1.2 Mbps to continue probing.
  std::vector<DataRate> probes = {Scale(start_bitrate_, cfg_.first_exponential_probe_scale)};
  if (cfg_.second_exponential_probe_scale > 0) probes.push_back(Scale(start_bitrate_, cfg_.second_exponential_probe_scale));
  if (repeated_initial_probing_enabled_ && max_total_allocated_bitrate_.IsZero()) {
    last_allowed_repeated_initial_probe_ = at_time + cfg_.repeated_initial_probing_time_period;
  }
  return InitiateProbing(at_time, probes, true);
}

std::vector<ProbeCluster> ProbeControllerV2::SetEstimatedBitrate(DataRate bitrate, BandwidthLimitedCause cause, Timestamp at_time) {
  bandwidth_limited_cause_ = cause;
  if (bitrate < Scale(estimated_bitrate_, kBitrateDropThreshold)) {
    time_of_last_large_drop_ = at_time;
    bitrate_before_last_large_drop_ = estimated_bitrate_;
  }
  estimated_bitrate_ = bitrate;

  if (state_ == State::kWaitingForProbingResult) {
    // Continue probing if probing results indicate the channel has greater
    // capacity unless we already reached the needed bitrate.
    if (cfg_.abort_further_probe_if_max_lower_than_current &&
        (bitrate > max_bitrate_ || (!max_total_allocated_bitrate_.IsZero() && bitrate > max_total_allocated_bitrate_ * 2.0))) {
      min_bitrate_to_probe_further_ = DataRate::PlusInfinity();
    }
    if (bitrate > min_bitrate_to_probe_further_) {
      return InitiateProbing(at_time, {Scale(bitrate, cfg_.further_exponential_probe_scale)}, true);
    }
  }
  return {};
}

std::vector<ProbeCluster> ProbeControllerV2::RequestProbe(Timestamp at_time) {
  // Called once we have returned to the normal state after a large drop in the
  // estimated bandwidth. The response is a single probe session (if not already
  // probing) at the previous bitrate. If it fails, the drop was a real one from a
  // competing flow or a network change.
  const bool in_alr = alr_start_time_.has_value();
  const bool alr_ended_recently = alr_end_time_.has_value() && at_time - *alr_end_time_ < kAlrEndedTimeout;
  if (in_alr || alr_ended_recently) {
    if (state_ == State::kProbingComplete) {
      const DataRate suggested_probe = Scale(bitrate_before_last_large_drop_, kProbeFractionAfterDrop);
      const DataRate min_expected_probe_result = Scale(suggested_probe, 1 - kProbeUncertainty);
      const TimeDelta time_since_drop = at_time - time_of_last_large_drop_;
      const TimeDelta time_since_probe = at_time - last_bwe_drop_probing_time_;
      if (min_expected_probe_result > estimated_bitrate_ && time_since_drop < kBitrateDropTimeout &&
          time_since_probe > kMinTimeBetweenAlrProbes) {
        last_bwe_drop_probing_time_ = at_time;
        return InitiateProbing(at_time, {suggested_probe}, false);
      }
    }
  }
  return {};
}

void ProbeControllerV2::Reset(Timestamp at_time) {
  bandwidth_limited_cause_ = BandwidthLimitedCause::kDelayBasedLimited;
  state_ = State::kInit;
  min_bitrate_to_probe_further_ = DataRate::PlusInfinity();
  time_last_probing_initiated_ = Timestamp::Micros(0);
  estimated_bitrate_ = DataRate::Zero();
  start_bitrate_ = DataRate::Zero();
  max_bitrate_ = kDefaultMaxProbingBitrate;
  last_bwe_drop_probing_time_ = at_time;
  alr_end_time_.reset();
  time_of_last_large_drop_ = at_time;
  bitrate_before_last_large_drop_ = DataRate::Zero();
}

bool ProbeControllerV2::TimeForAlrProbe(Timestamp at_time) const {
  if (enable_periodic_alr_probing_ && alr_start_time_) {
    const Timestamp next_probe_time = std::max(*alr_start_time_, time_last_probing_initiated_) + cfg_.alr_probing_interval;
    return at_time >= next_probe_time;
  }
  return false;
}

bool ProbeControllerV2::TimeForNextRepeatedInitialProbe(Timestamp at_time) const {
  if (state_ != State::kWaitingForProbingResult && last_allowed_repeated_initial_probe_ > at_time) {
    const Timestamp next_probe_time = time_last_probing_initiated_ + kMaxWaitingTimeForProbingResult;
    if (at_time >= next_probe_time) return true;
  }
  return false;
}

std::vector<ProbeCluster> ProbeControllerV2::Process(Timestamp at_time) {
  if (at_time - time_last_probing_initiated_ > kMaxWaitingTimeForProbingResult) {
    if (state_ == State::kWaitingForProbingResult) UpdateState(State::kProbingComplete);
  }
  if (estimated_bitrate_.IsZero() || state_ != State::kProbingComplete) return {};
  if (TimeForNextRepeatedInitialProbe(at_time)) {
    return InitiateProbing(at_time, {Scale(estimated_bitrate_, cfg_.first_exponential_probe_scale)}, true);
  }
  if (TimeForAlrProbe(at_time)) {
    return InitiateProbing(at_time, {Scale(estimated_bitrate_, cfg_.alr_probe_scale)}, true);
  }
  return {};
}

ProbeCluster ProbeControllerV2::CreateProbeCluster(Timestamp at_time, DataRate bitrate) {
  ProbeCluster c;
  c.at_time = at_time;
  c.rate = bitrate;
  TimeDelta duration;
  if (at_time < last_allowed_repeated_initial_probe_) {
    duration = cfg_.initial_probe_duration;
    c.min_probe_delta = cfg_.initial_min_probe_delta;
  } else {
    duration = cfg_.min_probe_duration;
    c.min_probe_delta = cfg_.min_probe_delta;
  }
  c.min_bytes = Mul(bitrate, duration);
  c.min_probes = cfg_.min_probe_packets_sent;
  c.id = next_probe_cluster_id_++;
  SM_EVENT(cfg_.events, at_time, "probe", "cluster", bitrate.bps(), c.id);
  ++clusters_;
  return c;
}

std::vector<ProbeCluster> ProbeControllerV2::InitiateProbing(Timestamp now, const std::vector<DataRate>& bitrates_to_probe, bool probe_further) {
  DataRate max_probe_bitrate = max_bitrate_;
  if (max_total_allocated_bitrate_ > DataRate::Zero()) {
    // If a max allocated bitrate has been configured, allow probing up to 2x that
    // rate. This allows some overhead to account for bursty streams, which
    // otherwise would have to ramp up when the overshoot is already in progress.
    max_probe_bitrate = std::min(max_probe_bitrate, max_total_allocated_bitrate_ * 2.0);
  }
  switch (bandwidth_limited_cause_) {
    case BandwidthLimitedCause::kRttBasedBackOffHighRtt:
    case BandwidthLimitedCause::kDelayBasedLimitedDelayIncreased:
    case BandwidthLimitedCause::kLossLimitedBwe:
      return {};  // Not sending a probe in a bandwidth limited state.
    case BandwidthLimitedCause::kLossLimitedBweIncreasing:
      max_probe_bitrate = std::min(max_probe_bitrate, Scale(estimated_bitrate_, cfg_.loss_limited_probe_scale));
      break;
    case BandwidthLimitedCause::kDelayBasedLimited:
      break;
  }

  std::vector<ProbeCluster> pending;
  for (DataRate bitrate : bitrates_to_probe) {
    if (bitrate >= max_probe_bitrate) {
      bitrate = max_probe_bitrate;
      probe_further = false;
    }
    pending.push_back(CreateProbeCluster(now, bitrate));
  }
  time_last_probing_initiated_ = now;
  if (probe_further) {
    UpdateState(State::kWaitingForProbingResult);
    // Don't expect probe results to be larger than a fraction of the actual probe rate.
    min_bitrate_to_probe_further_ = Scale(pending.back().rate, cfg_.further_probe_threshold);
  } else {
    UpdateState(State::kProbingComplete);
  }
  return pending;
}

void ProbeControllerV2::ExportMetrics(MetricsWriter& w) const {
  w.Counter("clusters", clusters_);
  w.Gauge("state", static_cast<double>(state_));
}

void ProbeBitrateEstimator::ExportMetrics(MetricsWriter& w) const {
  w.Counter("estimates", estimates_);
  w.Counter("invalid", invalid_);
}

}  // namespace sm::cc
