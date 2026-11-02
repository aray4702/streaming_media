// libwebrtc's rtc_base/numerics/event_based_exponential_moving_average.{h,cc} (M157): an exponential moving average
// for samples that arrive at irregular times, decaying by elapsed time with a configurable half time, with the
// sample variance and a 95 % confidence interval. The connection's RTT estimate uses it (#1000).
//
// Checked line by line against compare/webrtc/reference/rtc_base/numerics/event_based_exponential_moving_average.cc
// (window, #1000): the same arithmetic. Its unittest is ported in tests/unit/core/event_based_exponential_moving_average_test.cc.
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>

namespace sm {

class EventBasedExponentialMovingAverage {
 public:
  // `half_time` (in the unit AddSample's `now` uses) is the time after which a sample's weight has halved.
  explicit EventBasedExponentialMovingAverage(int half_time) { SetHalfTime(half_time); }

  void AddSample(int64_t now, int sample) {
    if (!last_observation_timestamp_.has_value()) {
      value_ = sample;
    } else {
      // libwebrtc DCHECKs now >= the last timestamp (simulated clocks deliver samples together).
      // The variance is computed from the second sample on.
      const int64_t age = now - *last_observation_timestamp_;
      const double e = std::exp(-static_cast<double>(age) / tau_);
      const double alpha = e / (1 + e);
      const double one_minus_alpha = 1 - alpha;
      const double sample_diff = sample - value_;
      value_ = one_minus_alpha * value_ + alpha * sample;
      estimator_variance_ = (one_minus_alpha * one_minus_alpha) * estimator_variance_ + (alpha * alpha);
      if (sample_variance_ == std::numeric_limits<double>::infinity()) {
        sample_variance_ = sample_diff * sample_diff;  // The first variance.
      } else {
        sample_variance_ = one_minus_alpha * sample_variance_ + alpha * sample_diff * sample_diff;
      }
    }
    last_observation_timestamp_ = now;
  }

  double GetAverage() const { return value_; }
  double GetVariance() const { return sample_variance_; }
  // The 95 % confidence interval for a normally distributed value: 1.96 standard deviations.
  double GetConfidenceInterval() const { return 1.96 * std::sqrt(sample_variance_ * estimator_variance_); }

  void Reset() {
    value_ = std::nan("uninit");
    sample_variance_ = std::numeric_limits<double>::infinity();
    estimator_variance_ = 1;
    last_observation_timestamp_.reset();
  }

  // Changes the half time; also resets the average.
  void SetHalfTime(int half_time) {
    tau_ = static_cast<double>(half_time) / std::numbers::ln2;
    Reset();
  }

 private:
  double tau_ = 0;
  double value_ = std::nan("uninit");
  double sample_variance_ = std::numeric_limits<double>::infinity();
  // This is the ratio between variance of the estimate and variance of samples.
  double estimator_variance_ = 1;
  std::optional<int64_t> last_observation_timestamp_;
};

}  // namespace sm
