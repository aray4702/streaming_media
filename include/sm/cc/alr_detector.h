// Application-limited-region detector, `cc v2`: libwebrtc's AlrDetector and
// IntervalBudget (goog_cc/alr_detector.{h,cc}, pacing/interval_budget.{h,cc},
// M157, compare/webrtc/reference/), with their defaults and no field trials.
//
// A budget of `bandwidth_usage_ratio` (0.65) x the estimate refills with the
// time between sent packets and is drained by the bytes sent. The sender is
// application limited when the budget is more than `start_budget_level_ratio`
// (0.80) full, and stops being so when it falls under `stop_budget_level_ratio`
// (0.50). GoogCC uses the state to mark the acknowledged-rate estimate, to
// start periodic ALR probes and to allow a probe after a drop (RequestProbe).
#pragma once

#include <cstdint>
#include <optional>

#include "sm/core/units.h"

namespace sm::cc {

class IntervalBudget {
 public:
  explicit IntervalBudget(int initial_target_rate_kbps, bool can_build_up_underuse = false);
  void set_target_rate_kbps(int target_rate_kbps);
  void IncreaseBudget(int64_t delta_time_ms);
  void UseBudget(size_t bytes);
  size_t bytes_remaining() const;
  double budget_ratio() const;
  int target_rate_kbps() const { return target_rate_kbps_; }

 private:
  int target_rate_kbps_ = 0;
  int64_t max_bytes_in_budget_ = 0;
  int64_t bytes_remaining_ = 0;
  bool can_build_up_underuse_;
};

class AlrDetector {
 public:
  AlrDetector() : alr_budget_(0, true) {}
  void OnBytesSent(DataSize bytes_sent, Timestamp send_time);
  void SetEstimatedBitrate(DataRate bitrate);
  // Set while the sender is application limited; the time it started.
  std::optional<Timestamp> GetApplicationLimitedRegionStartTime() const { return alr_started_time_; }
  const IntervalBudget& budget() const { return alr_budget_; }

 private:
  static constexpr double kBandwidthUsageRatio = 0.65;
  static constexpr double kStartBudgetLevelRatio = 0.80;
  static constexpr double kStopBudgetLevelRatio = 0.50;
  std::optional<Timestamp> last_send_time_;
  IntervalBudget alr_budget_;
  std::optional<Timestamp> alr_started_time_;
};

}  // namespace sm::cc
