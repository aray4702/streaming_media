// libwebrtc's acknowledged-bitrate and AIMD rate control (`cc v2`), ported
// from compare/webrtc/reference/modules/congestion_controller/goog_cc/
// {bitrate_estimator,acknowledged_bitrate_estimator,link_capacity_estimator}.{h,cc}
// and modules/remote_bitrate_estimator/aimd_rate_control.{h,cc} (M157), with
// their constants and defaults (no field trials set).
//
//   BitrateEstimator    Bayesian estimate of the delivered rate: 500 ms first
//                       window, then 150 ms windows, sample uncertainty
//                       scale 10.
//   LinkCapacityEstimator  EWMA (alpha 0.05 on overuse) of the acked rate at
//                       overuse, with a normalised deviation in [0.4, 2.5].
//   AimdRateControl     send-side AimdRateControl: hold/increase/decrease,
//                       beta 0.85, reduce again only after clamp(rtt, 10,
//                       200) ms, increase limited to 1.5 x acked + 10 kbps.
//
// The ALR state (cc/alr_detector.h, #48) reaches the estimator as in libwebrtc:
// SetAlr and, when the region ends, SetAlrEndedTime, after which the first
// packet sent later calls ExpectFastRateChange.
//
// Differences that remain (see tools/catalog.py 5.2): PacketResult has no
// prior_unacked_data (always zero here); no NetworkStateEstimate;
// SetInApplicationLimitedRegion of the rate control only matters with a field
// trial that is off by default.
#pragma once

#include <optional>
#include <vector>

#include "sm/cc/transport_feedback.h"
#include "sm/core/units.h"

namespace sm::cc {

enum class BandwidthUsage;

class BitrateEstimator {
 public:
  void Update(Timestamp at_time, DataSize amount, bool in_alr);
  std::optional<DataRate> bitrate() const;
  std::optional<DataRate> PeekRate() const;
  void ExpectFastRateChange();

 private:
  float UpdateWindow(int64_t now_ms, int bytes, int rate_window_ms, bool* is_small_sample);
  int sum_ = 0;
  int current_window_ms_ = 0;
  int64_t prev_time_ms_ = -1;
  float bitrate_estimate_kbps_ = -1.0f;
  float bitrate_estimate_var_ = 50.0f;
};

// AcknowledgedBitrateEstimator::IncomingPacketFeedbackVector: every received
// packet, in receive-time order, updates the estimator.
class AckedBitrateEstimatorV2 {
 public:
  void OnPacketResults(const std::vector<PacketResult>& results);
  std::optional<DataRate> bitrate() const { return estimator_.bitrate(); }
  std::optional<DataRate> PeekRate() const { return estimator_.PeekRate(); }
  void SetAlr(bool in_alr) { in_alr_ = in_alr; }
  void SetAlrEndedTime(Timestamp alr_ended_time) { alr_ended_time_ = alr_ended_time; }

 private:
  BitrateEstimator estimator_;
  bool in_alr_ = false;
  std::optional<Timestamp> alr_ended_time_;
};

class LinkCapacityEstimator {
 public:
  DataRate UpperBound() const;
  DataRate LowerBound() const;
  void Reset() { estimate_kbps_.reset(); }
  void OnOveruseDetected(DataRate acknowledged_rate) { Update(acknowledged_rate, 0.05); }
  void OnProbeRate(DataRate probe_rate) { Update(probe_rate, 0.5); }
  bool has_estimate() const { return estimate_kbps_.has_value(); }
  DataRate estimate() const;
  std::optional<double> estimate_kbps() const { return estimate_kbps_; }

 private:
  friend class AimdRateControl;
  void Update(DataRate capacity_sample, double alpha);
  double deviation_estimate_kbps() const;
  std::optional<double> estimate_kbps_;
  double deviation_kbps_ = 0.4;
};

class AimdRateControl {
 public:
  AimdRateControl() = default;
  void SetStartBitrate(DataRate start_bitrate);
  void SetMinBitrate(DataRate min_bitrate);
  bool ValidEstimate() const { return bitrate_is_initialized_; }
  void SetRtt(TimeDelta rtt) { rtt_ = rtt; }
  // libwebrtc's RateControlInput(bw_state, estimated_throughput).
  DataRate Update(BandwidthUsage usage, std::optional<DataRate> estimated_throughput, Timestamp at_time);
  void SetEstimate(DataRate bitrate, Timestamp at_time);
  DataRate LatestEstimate() const { return current_bitrate_; }
  bool TimeToReduceFurther(Timestamp at_time, DataRate estimated_throughput) const;
  bool InitialTimeToReduceFurther(Timestamp at_time) const;
  const LinkCapacityEstimator& link_capacity() const { return link_capacity_; }
  // Rate reduction of the last decrease (last_decrease_ in libwebrtc).
  std::optional<DataRate> last_decrease() const { return last_decrease_; }
  double GetNearMaxIncreaseRateBpsPerSecond() const;

 private:
  enum class State { kHold, kIncrease, kDecrease };
  void ChangeBitrate(BandwidthUsage usage, std::optional<DataRate> estimated_throughput, Timestamp at_time);
  void ChangeState(BandwidthUsage usage, Timestamp at_time);
  DataRate ClampBitrate(DataRate r) const;
  DataRate MultiplicativeRateIncrease(Timestamp at_time, Timestamp last_time, DataRate current) const;
  DataRate AdditiveRateIncrease(Timestamp at_time, Timestamp last_time) const;

  DataRate min_configured_bitrate_ = DataRate::KilobitsPerSec(5);  // kCongestionControllerMinBitrate
  DataRate current_bitrate_ = DataRate::KilobitsPerSec(30000);
  DataRate latest_estimated_throughput_ = DataRate::KilobitsPerSec(30000);
  LinkCapacityEstimator link_capacity_;
  State state_ = State::kHold;
  Timestamp time_last_bitrate_change_ = Timestamp::MinusInfinity();
  Timestamp time_last_bitrate_decrease_ = Timestamp::MinusInfinity();
  Timestamp time_first_throughput_estimate_ = Timestamp::MinusInfinity();
  bool bitrate_is_initialized_ = false;
  double beta_ = 0.85;
  TimeDelta rtt_ = TimeDelta::Millis(200);
  std::optional<DataRate> last_decrease_;
};

}  // namespace sm::cc
