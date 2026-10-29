#include "sm/cc/aimd_rate_control.h"

#include <algorithm>
#include <cmath>

#include "sm/cc/delay_based_bwe.h"

namespace sm::cc {

// --- BitrateEstimator (goog_cc/bitrate_estimator.cc) -----------------------------

namespace {
constexpr int kInitialRateWindowMs = 500;
constexpr int kRateWindowMs = 150;
constexpr float kUncertaintyScale = 10.0f;
}  // namespace

void BitrateEstimator::Update(Timestamp at_time, DataSize amount, bool in_alr) {
  (void)in_alr;  // scale_alr defaults to scale: ALR does not change the estimate.
  int rate_window_ms = kRateWindowMs;
  // A larger window at the beginning gives a more stable first sample.
  if (bitrate_estimate_kbps_ < 0.f) rate_window_ms = kInitialRateWindowMs;
  bool is_small_sample = false;
  float bitrate_sample_kbps = UpdateWindow(at_time.ms(), static_cast<int>(amount.bytes()), rate_window_ms, &is_small_sample);
  if (bitrate_sample_kbps < 0.0f) return;
  if (bitrate_estimate_kbps_ < 0.0f) {
    bitrate_estimate_kbps_ = bitrate_sample_kbps;  // The very first sample.
    return;
  }
  // Sample uncertainty grows with the distance from the current estimate.
  float sample_uncertainty =
      kUncertaintyScale * std::abs(bitrate_estimate_kbps_ - bitrate_sample_kbps) / (bitrate_estimate_kbps_ + std::min(bitrate_sample_kbps, 0.0f));
  float sample_var = sample_uncertainty * sample_uncertainty;
  // Bayesian update; the estimate's own uncertainty grows by 5 per update.
  float pred_bitrate_estimate_var = bitrate_estimate_var_ + 5.f;
  bitrate_estimate_kbps_ = (sample_var * bitrate_estimate_kbps_ + pred_bitrate_estimate_var * bitrate_sample_kbps) / (sample_var + pred_bitrate_estimate_var);
  bitrate_estimate_kbps_ = std::max(bitrate_estimate_kbps_, 0.0f);
  bitrate_estimate_var_ = sample_var * pred_bitrate_estimate_var / (sample_var + pred_bitrate_estimate_var);
}

float BitrateEstimator::UpdateWindow(int64_t now_ms, int bytes, int rate_window_ms, bool* is_small_sample) {
  *is_small_sample = false;
  // Reset if time moves backwards.
  if (now_ms < prev_time_ms_) {
    prev_time_ms_ = -1;
    sum_ = 0;
    current_window_ms_ = 0;
  }
  if (prev_time_ms_ >= 0) {
    current_window_ms_ += static_cast<int>(now_ms - prev_time_ms_);
    // Reset if nothing has been received for more than a full window.
    if (now_ms - prev_time_ms_ > rate_window_ms) {
      sum_ = 0;
      current_window_ms_ %= rate_window_ms;
    }
  }
  prev_time_ms_ = now_ms;
  float bitrate_sample = -1.0f;
  if (current_window_ms_ >= rate_window_ms) {
    *is_small_sample = sum_ < 0;  // small_thresh defaults to zero bytes.
    bitrate_sample = 8.0f * static_cast<float>(sum_) / static_cast<float>(rate_window_ms);
    current_window_ms_ -= rate_window_ms;
    sum_ = 0;
  }
  sum_ += bytes;
  return bitrate_sample;
}

std::optional<DataRate> BitrateEstimator::bitrate() const {
  if (bitrate_estimate_kbps_ < 0.f) return std::nullopt;
  return DataRate::BitsPerSec(static_cast<int64_t>(bitrate_estimate_kbps_ * 1000.0f));
}

std::optional<DataRate> BitrateEstimator::PeekRate() const {
  if (current_window_ms_ > 0) return DataRate::BitsPerSec(static_cast<int64_t>(sum_) * 8000 / current_window_ms_);
  return std::nullopt;
}

void BitrateEstimator::ExpectFastRateChange() {
  // A larger estimate variance lets the rate change fast for the next samples.
  bitrate_estimate_var_ += 200;
}

void AckedBitrateEstimatorV2::OnPacketResults(const std::vector<PacketResult>& results) {
  for (const auto& r : SortedByReceiveTime(results)) {
    if (alr_ended_time_ && r.send_time > *alr_ended_time_) {
      Estimator().ExpectFastRateChange();
      alr_ended_time_.reset();
    }
    Estimator().Update(r.receive_time, r.size, in_alr_);
  }
}

// --- LinkCapacityEstimator (goog_cc/link_capacity_estimator.cc) -----------------

DataRate LinkCapacityEstimator::UpperBound() const {
  if (estimate_kbps_) return DataRate::BitsPerSec(static_cast<int64_t>((*estimate_kbps_ + 3 * deviation_estimate_kbps()) * 1000));
  return DataRate::PlusInfinity();
}

DataRate LinkCapacityEstimator::LowerBound() const {
  if (estimate_kbps_) return DataRate::BitsPerSec(static_cast<int64_t>(std::max(0.0, *estimate_kbps_ - 3 * deviation_estimate_kbps()) * 1000));
  return DataRate::Zero();
}

DataRate LinkCapacityEstimator::estimate() const { return DataRate::BitsPerSec(static_cast<int64_t>(*estimate_kbps_ * 1000)); }

void LinkCapacityEstimator::Update(DataRate capacity_sample, double alpha) {
  double sample_kbps = capacity_sample.kbps();
  if (!estimate_kbps_) {
    estimate_kbps_ = sample_kbps;
  } else {
    estimate_kbps_ = (1 - alpha) * *estimate_kbps_ + alpha * sample_kbps;
  }
  // The variance of the estimate, normalised with the estimate.
  const double norm = std::max(*estimate_kbps_, 1.0);
  double error_kbps = *estimate_kbps_ - sample_kbps;
  deviation_kbps_ = (1 - alpha) * deviation_kbps_ + alpha * error_kbps * error_kbps / norm;
  // 0.4 ~= 14 kbit/s at 500 kbit/s; 2.5 ~= 35 kbit/s at 500 kbit/s.
  deviation_kbps_ = std::clamp(deviation_kbps_, static_cast<double>(0.4f), static_cast<double>(2.5f));
}

double LinkCapacityEstimator::deviation_estimate_kbps() const { return std::sqrt(deviation_kbps_ * *estimate_kbps_); }

// --- AimdRateControl (remote_bitrate_estimator/aimd_rate_control.cc) -----------

void AimdRateControl::SetStartBitrate(DataRate start_bitrate) {
  current_bitrate_ = start_bitrate;
  latest_estimated_throughput_ = current_bitrate_;
  bitrate_is_initialized_ = true;
}

void AimdRateControl::SetMinBitrate(DataRate min_bitrate) {
  min_configured_bitrate_ = min_bitrate;
  current_bitrate_ = std::max(min_bitrate, current_bitrate_);
}

bool AimdRateControl::TimeToReduceFurther(Timestamp at_time, DataRate estimated_throughput) const {
  const TimeDelta bitrate_reduction_interval = Clamp(rtt_, TimeDelta::Millis(10), TimeDelta::Millis(200));
  // A never-changed rate (time = -inf) counts as long ago.
  if (!time_last_bitrate_change_.IsFinite() || at_time - time_last_bitrate_change_ >= bitrate_reduction_interval) return true;
  if (ValidEstimate()) {
    const DataRate threshold = LatestEstimate() * 0.5;
    return estimated_throughput < threshold;
  }
  return false;
}

bool AimdRateControl::InitialTimeToReduceFurther(Timestamp at_time) const {
  return ValidEstimate() && TimeToReduceFurther(at_time, DataRate::BitsPerSec(LatestEstimate().bps() / 2 - 1));
}

DataRate AimdRateControl::Update(BandwidthUsage usage, std::optional<DataRate> estimated_throughput, Timestamp at_time) {
  // The initial rate is what we receive after 5 s of throughput estimates.
  if (!bitrate_is_initialized_) {
    const TimeDelta kInitializationTime = TimeDelta::Seconds(5);
    if (!time_first_throughput_estimate_.IsFinite()) {
      if (estimated_throughput) time_first_throughput_estimate_ = at_time;
    } else if (at_time - time_first_throughput_estimate_ > kInitializationTime && estimated_throughput) {
      current_bitrate_ = *estimated_throughput;
      bitrate_is_initialized_ = true;
    }
  }
  ChangeBitrate(usage, estimated_throughput, at_time);
  return current_bitrate_;
}

void AimdRateControl::SetEstimate(DataRate bitrate, Timestamp at_time) {
  bitrate_is_initialized_ = true;
  DataRate prev_bitrate = current_bitrate_;
  current_bitrate_ = ClampBitrate(bitrate);
  time_last_bitrate_change_ = at_time;
  if (current_bitrate_ < prev_bitrate) time_last_bitrate_decrease_ = at_time;
}

double AimdRateControl::GetNearMaxIncreaseRateBpsPerSecond() const {
  const double frame_size_bits = current_bitrate_.bps() / 30.0;  // 1/30 s of data.
  const double kPacketSizeBits = 1200 * 8;
  double packets_per_frame = std::ceil(frame_size_bits / kPacketSizeBits);
  double avg_packet_size_bits = frame_size_bits / packets_per_frame;
  // Approximate the over-use estimator delay to 100 ms.
  TimeDelta response_time = (rtt_ + TimeDelta::Millis(100)) * 2.0;
  double increase_rate_bps_per_second = avg_packet_size_bits / response_time.seconds();
  const double kMinIncreaseRateBpsPerSecond = 4000;
  return std::max(kMinIncreaseRateBpsPerSecond, increase_rate_bps_per_second);
}

void AimdRateControl::ChangeBitrate(BandwidthUsage usage, std::optional<DataRate> input_throughput, Timestamp at_time) {
  std::optional<DataRate> new_bitrate;
  DataRate estimated_throughput = input_throughput.value_or(latest_estimated_throughput_);
  if (input_throughput) latest_estimated_throughput_ = *input_throughput;

  // An over-use triggers a reduction even before the first estimate exists.
  if (!bitrate_is_initialized_ && usage != BandwidthUsage::kOverusing) return;

  ChangeState(usage, at_time);

  switch (state_) {
    case State::kHold:
      break;
    case State::kIncrease: {
      if (estimated_throughput > link_capacity_.UpperBound()) link_capacity_.Reset();
      // Limit the increase by the throughput; more lag at very low rates.
      DataRate increase_limit = estimated_throughput * 1.5 + DataRate::KilobitsPerSec(10);
      if (current_bitrate_ < increase_limit) {
        DataRate increased_bitrate;
        if (link_capacity_.has_estimate()) {
          // Close to the link capacity: additive increase.
          increased_bitrate = current_bitrate_ + AdditiveRateIncrease(at_time, time_last_bitrate_change_);
        } else {
          // Unknown capacity: faster ramp up to discover it.
          increased_bitrate = current_bitrate_ + MultiplicativeRateIncrease(at_time, time_last_bitrate_change_, current_bitrate_);
        }
        new_bitrate = std::min(increased_bitrate, increase_limit);
      }
      time_last_bitrate_change_ = at_time;
      break;
    }
    case State::kDecrease: {
      // Slightly below the measured throughput to drain self-induced delay.
      DataRate decreased_bitrate = estimated_throughput * beta_;
      if (decreased_bitrate > DataRate::KilobitsPerSec(5)) decreased_bitrate = decreased_bitrate - DataRate::KilobitsPerSec(5);

      if (decreased_bitrate > current_bitrate_) {
        if (link_capacity_.has_estimate()) decreased_bitrate = link_capacity_.estimate() * beta_;
      }
      // Avoid increasing the rate when over-using.
      if (decreased_bitrate < current_bitrate_) new_bitrate = decreased_bitrate;

      if (bitrate_is_initialized_ && estimated_throughput < current_bitrate_) {
        last_decrease_ = new_bitrate ? current_bitrate_ - *new_bitrate : DataRate::Zero();
      }
      if (estimated_throughput < link_capacity_.LowerBound()) {
        // Far from the capacity estimate: clear it to allow an immediate update.
        link_capacity_.Reset();
      }
      bitrate_is_initialized_ = true;
      link_capacity_.OnOveruseDetected(estimated_throughput);
      // Stay on hold until the pipes are cleared.
      state_ = State::kHold;
      time_last_bitrate_change_ = at_time;
      time_last_bitrate_decrease_ = at_time;
      break;
    }
  }
  current_bitrate_ = ClampBitrate(new_bitrate.value_or(current_bitrate_));
}

DataRate AimdRateControl::ClampBitrate(DataRate r) const { return std::max(r, min_configured_bitrate_); }

DataRate AimdRateControl::MultiplicativeRateIncrease(Timestamp at_time, Timestamp last_time, DataRate current) const {
  double alpha = 1.08;
  if (last_time.IsFinite()) alpha = std::pow(alpha, std::min((at_time - last_time).seconds(), 1.0));
  return std::max(current * (alpha - 1.0), DataRate::BitsPerSec(1000));
}

DataRate AimdRateControl::AdditiveRateIncrease(Timestamp at_time, Timestamp last_time) const {
  double time_period_seconds = (at_time - last_time).seconds();
  double data_rate_increase_bps = GetNearMaxIncreaseRateBpsPerSecond() * time_period_seconds;
  return DataRate::BitsPerSec(static_cast<int64_t>(data_rate_increase_bps));
}

void AimdRateControl::ChangeState(BandwidthUsage usage, Timestamp at_time) {
  switch (usage) {
    case BandwidthUsage::kNormal:
      if (state_ == State::kHold) {
        time_last_bitrate_change_ = at_time;
        state_ = State::kIncrease;
      }
      break;
    case BandwidthUsage::kOverusing:
      if (state_ != State::kDecrease) state_ = State::kDecrease;
      break;
    case BandwidthUsage::kUnderusing:
      state_ = State::kHold;
      break;
  }
}

}  // namespace sm::cc

namespace sm::cc {

// --- RobustThroughputEstimator (goog_cc/robust_throughput_estimator.cc) ---------------

bool RobustThroughputEstimator::FirstPacketOutsideWindow() const {
  if (window_.empty()) return false;
  if (window_.size() > settings_.max_window_packets) return true;
  const TimeDelta current_window_duration = window_.back().receive_time - window_.front().receive_time;
  if (current_window_duration > settings_.max_window_duration) return true;
  return window_.size() > settings_.window_packets && current_window_duration > settings_.min_window_duration;
}

void RobustThroughputEstimator::OnPacketResults(const std::vector<PacketResult>& results) {
  for (const auto& packet : results) {
    // Packets without valid send or receive times are ignored.
    if (!packet.receive_time.IsFinite() || !packet.send_time.IsFinite()) continue;
    window_.push_back(packet);
    // Feedback packets may be reordered: keep the window sorted by receive time.
    for (size_t i = window_.size() - 1; i > 0 && window_[i].receive_time < window_[i - 1].receive_time; i--) {
      std::swap(window_[i], window_[i - 1]);
    }
    constexpr TimeDelta kMaxReorderingTime = TimeDelta::Seconds(1);
    if (window_.back().receive_time - packet.receive_time > kMaxReorderingTime) {
      // Severe packet re-ordering or the timestamp offset changed.
      window_.clear();
      latest_discarded_send_time_ = Timestamp::MinusInfinity();
    }
  }
  while (FirstPacketOutsideWindow()) {
    latest_discarded_send_time_ = std::max(latest_discarded_send_time_, window_.front().send_time);
    window_.pop_front();
  }
}

std::optional<DataRate> RobustThroughputEstimator::bitrate() const {
  if (window_.empty() || window_.size() < settings_.required_packets) return std::nullopt;

  TimeDelta largest_recv_gap = TimeDelta::Zero();
  TimeDelta second_largest_recv_gap = TimeDelta::Zero();
  for (size_t i = 1; i < window_.size(); i++) {
    const TimeDelta gap = window_[i].receive_time - window_[i - 1].receive_time;
    if (gap > largest_recv_gap) {
      second_largest_recv_gap = largest_recv_gap;
      largest_recv_gap = gap;
    } else if (gap > second_largest_recv_gap) {
      second_largest_recv_gap = gap;
    }
  }

  Timestamp first_send_time = Timestamp::PlusInfinity();
  Timestamp last_send_time = Timestamp::MinusInfinity();
  Timestamp first_recv_time = Timestamp::PlusInfinity();
  Timestamp last_recv_time = Timestamp::MinusInfinity();
  DataSize recv_size = DataSize::Bytes(0);
  DataSize send_size = DataSize::Bytes(0);
  DataSize first_recv_size = DataSize::Bytes(0);
  DataSize last_send_size = DataSize::Bytes(0);
  size_t num_sent_packets_in_window = 0;
  for (const auto& packet : window_) {
    if (packet.receive_time < first_recv_time) {
      first_recv_time = packet.receive_time;
      first_recv_size = packet.size;
    }
    last_recv_time = std::max(last_recv_time, packet.receive_time);
    recv_size += packet.size;

    // A packet sent before one already dropped from the window was reordered;
    // its send time would underestimate the send rate.
    if (packet.send_time < latest_discarded_send_time_) continue;
    if (packet.send_time > last_send_time) {
      last_send_time = packet.send_time;
      last_send_size = packet.size;
    }
    first_send_time = std::min(first_send_time, packet.send_time);
    send_size += packet.size;
    ++num_sent_packets_in_window;
  }

  // N packets of size S every T ms span (N-1)*T: drop one packet's size (the
  // first received, the last sent).
  recv_size -= first_recv_size;
  send_size -= last_send_size;

  // Replace the largest receive gap by the second largest, so a delay spike
  // does not pull the estimate down; the send rate caps any overestimate.
  TimeDelta recv_duration = (last_recv_time - first_recv_time) - largest_recv_gap + second_largest_recv_gap;
  recv_duration = std::max(recv_duration, TimeDelta::Millis(1));
  if (num_sent_packets_in_window < settings_.required_packets) return DataRate::FromSizeAndTime(recv_size, recv_duration);

  TimeDelta send_duration = last_send_time - first_send_time;
  send_duration = std::max(send_duration, TimeDelta::Millis(1));
  return std::min(DataRate::FromSizeAndTime(send_size, send_duration), DataRate::FromSizeAndTime(recv_size, recv_duration));
}

}  // namespace sm::cc
