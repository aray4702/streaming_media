#include "sm/cc/alr_detector.h"

#include <algorithm>
#include <cmath>

namespace sm::cc {

namespace {
constexpr int64_t kWindowMs = 500;  // interval_budget.cc
}  // namespace

IntervalBudget::IntervalBudget(int initial_target_rate_kbps, bool can_build_up_underuse)
    : can_build_up_underuse_(can_build_up_underuse) {
  set_target_rate_kbps(initial_target_rate_kbps);
}

void IntervalBudget::set_target_rate_kbps(int target_rate_kbps) {
  target_rate_kbps_ = target_rate_kbps;
  max_bytes_in_budget_ = (kWindowMs * target_rate_kbps_) / 8;
  bytes_remaining_ = std::min(std::max(-max_bytes_in_budget_, bytes_remaining_), max_bytes_in_budget_);
}

void IntervalBudget::IncreaseBudget(int64_t delta_time_ms) {
  const int64_t bytes = target_rate_kbps_ * delta_time_ms / 8;
  if (bytes_remaining_ < 0 || can_build_up_underuse_) {
    // We overused last interval, compensate this interval, or underuse can build up.
    bytes_remaining_ = std::min(bytes_remaining_ + bytes, max_bytes_in_budget_);
  } else {
    // If we underused last interval we can't use it this interval.
    bytes_remaining_ = std::min(bytes, max_bytes_in_budget_);
  }
}

void IntervalBudget::UseBudget(size_t bytes) {
  bytes_remaining_ = std::max(bytes_remaining_ - static_cast<int>(bytes), -max_bytes_in_budget_);
}

size_t IntervalBudget::bytes_remaining() const { return static_cast<size_t>(std::max<int64_t>(0, bytes_remaining_)); }

double IntervalBudget::budget_ratio() const {
  if (max_bytes_in_budget_ == 0) return 0.0;
  return static_cast<double>(bytes_remaining_) / static_cast<double>(max_bytes_in_budget_);
}

void AlrDetector::OnBytesSent(DataSize bytes_sent, Timestamp send_time) {
  if (!last_send_time_.has_value()) {
    last_send_time_ = send_time;
    // Since the duration for sending the bytes is unknown, the initial budget
    // is not touched by the first packet.
    return;
  }
  const TimeDelta delta_time = send_time - *last_send_time_;
  last_send_time_ = send_time;

  alr_budget_.UseBudget(static_cast<size_t>(bytes_sent.bytes()));
  // TimeDelta::ms() rounds to the nearest millisecond (DivideRoundToNearest in
  // rtc_base/units/unit_base.h), so a 0.5-1 ms gap between packets credits 1 ms.
  alr_budget_.IncreaseBudget(delta_time.ms());
  if (alr_budget_.budget_ratio() > kStartBudgetLevelRatio && !alr_started_time_) {
    // The libwebrtc detector stamps the state with the clock; the send time of
    // the packet that triggered it is the same instant here.
    alr_started_time_ = send_time;
  } else if (alr_budget_.budget_ratio() < kStopBudgetLevelRatio && alr_started_time_) {
    alr_started_time_ = std::nullopt;
  }
}

void AlrDetector::SetEstimatedBitrate(DataRate bitrate) {
  // (bitrate * ratio).kbps(): DataRate * double and kbps() both round to nearest.
  const int64_t scaled_bps = std::llround(static_cast<double>(bitrate.bps()) * kBandwidthUsageRatio);
  alr_budget_.set_target_rate_kbps(static_cast<int>(std::llround(static_cast<double>(scaled_bps) / 1000.0)));
}

}  // namespace sm::cc
