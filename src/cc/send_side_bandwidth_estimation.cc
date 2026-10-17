#include "sm/cc/send_side_bandwidth_estimation.h"

#include <algorithm>
#include <cmath>

namespace sm::cc {

namespace {
constexpr TimeDelta kBweIncreaseInterval = TimeDelta::Millis(1000);
constexpr TimeDelta kBweDecreaseInterval = TimeDelta::Millis(300);
constexpr TimeDelta kStartPhase = TimeDelta::Millis(2000);
constexpr int kLimitNumPackets = 20;
constexpr DataRate kDefaultMaxBitrate = DataRate::BitsPerSec(1000000000);
// Expecting that RTCP feedback is sent uniformly within [0.5, 1.5] s intervals.
constexpr TimeDelta kMaxRtcpFeedbackInterval = TimeDelta::Millis(5000);
constexpr float kDefaultLowLossThreshold = 0.02f;
constexpr float kDefaultHighLossThreshold = 0.1f;
// kCongestionControllerMinBitrate (bwe_defines.h).
constexpr DataRate kCongestionControllerMinBitrate = DataRate::BitsPerSec(5000);

LossBasedBweV2Config WithEvents(EventSink* events) {
  LossBasedBweV2Config c;
  c.events = events;
  return c;
}
}  // namespace

TimeDelta RttBasedBackoff::CorrectedRtt() const {
  // Avoid timeout when no packets are being sent.
  const TimeDelta timeout_correction =
      last_packet_sent_.IsFinite() && last_propagation_rtt_update_.IsFinite()
          ? std::max(last_packet_sent_ - last_propagation_rtt_update_, TimeDelta::Zero())
          : TimeDelta::Zero();
  return timeout_correction + last_propagation_rtt_;
}

SendSideBandwidthEstimation::SendSideBandwidthEstimation(EventSink* events)
    : events_(events),
      min_bitrate_configured_(kCongestionControllerMinBitrate),
      max_bitrate_configured_(kDefaultMaxBitrate),
      loss_v2_(WithEvents(events)) {
  loss_v2_.SetMinMaxBitrate(min_bitrate_configured_, max_bitrate_configured_);
}

void SendSideBandwidthEstimation::OnRouteChange() {
  lost_packets_since_last_loss_update_ = 0;
  expected_packets_since_last_loss_update_ = 0;
  current_target_ = DataRate::Zero();
  min_bitrate_configured_ = kCongestionControllerMinBitrate;
  max_bitrate_configured_ = kDefaultMaxBitrate;
  has_decreased_since_last_fraction_loss_ = false;
  last_loss_feedback_ = Timestamp::MinusInfinity();
  last_loss_packet_report_ = Timestamp::MinusInfinity();
  last_fraction_loss_ = 0;
  last_round_trip_time_ = TimeDelta::Zero();
  receiver_limit_ = DataRate::PlusInfinity();
  delay_based_limit_ = DataRate::PlusInfinity();
  time_last_decrease_ = Timestamp::MinusInfinity();
  first_report_time_ = Timestamp::MinusInfinity();
  if (loss_v2_.UseInStartPhase()) loss_v2_ = LossBasedBweV2(WithEvents(events_));
}

void SendSideBandwidthEstimation::SetBitrates(std::optional<DataRate> send_bitrate, DataRate min_bitrate, DataRate max_bitrate,
                                              Timestamp at_time) {
  SetMinMaxBitrate(min_bitrate, max_bitrate);
  if (send_bitrate) SetSendBitrate(*send_bitrate, at_time);
}

void SendSideBandwidthEstimation::SetSendBitrate(DataRate bitrate, Timestamp at_time) {
  // Reset to avoid being capped by the estimate.
  delay_based_limit_ = DataRate::PlusInfinity();
  UpdateTargetBitrate(bitrate, at_time);
  // Clear the sent bitrate history so the new value can be used directly and is not capped.
  min_bitrate_history_.clear();
}

void SendSideBandwidthEstimation::SetMinMaxBitrate(DataRate min_bitrate, DataRate max_bitrate) {
  min_bitrate_configured_ = std::max(min_bitrate, kCongestionControllerMinBitrate);
  if (max_bitrate > DataRate::Zero() && max_bitrate.IsFinite()) {
    max_bitrate_configured_ = std::max(min_bitrate_configured_, max_bitrate);
  } else {
    max_bitrate_configured_ = kDefaultMaxBitrate;
  }
  loss_v2_.SetMinMaxBitrate(min_bitrate_configured_, max_bitrate_configured_);
}

DataRate SendSideBandwidthEstimation::target_rate() const {
  return std::max(min_bitrate_configured_, std::min(current_target_, receiver_limit_));
}

void SendSideBandwidthEstimation::UpdateReceiverEstimate(Timestamp at_time, DataRate bandwidth) {
  receiver_limit_ = bandwidth.IsZero() ? DataRate::PlusInfinity() : bandwidth;
  ApplyTargetLimits(at_time);
}

void SendSideBandwidthEstimation::UpdateDelayBasedEstimate(Timestamp at_time, DataRate bitrate) {
  delay_based_limit_ = bitrate.IsZero() ? DataRate::PlusInfinity() : bitrate;
  ApplyTargetLimits(at_time);
}

void SendSideBandwidthEstimation::SetAcknowledgedRate(std::optional<DataRate> acknowledged_rate, Timestamp /*at_time*/) {
  acknowledged_rate_ = acknowledged_rate;
  if (!acknowledged_rate) return;
  loss_v2_.SetAcknowledgedBitrate(*acknowledged_rate);
}

void SendSideBandwidthEstimation::UpdateLossBasedEstimator(std::span<const PacketResult> results, bool in_alr, Timestamp at_time) {
  loss_v2_.UpdateBandwidthEstimate(results, delay_based_limit_, in_alr, at_time);
  UpdateEstimate(at_time);
}

void SendSideBandwidthEstimation::UpdatePacketsLost(int64_t packets_lost, int64_t number_of_packets, Timestamp at_time) {
  last_loss_feedback_ = at_time;
  if (!first_report_time_.IsFinite()) first_report_time_ = at_time;

  // Check the sequence number diff and weight the loss report.
  if (number_of_packets > 0) {
    const int64_t expected = expected_packets_since_last_loss_update_ + number_of_packets;
    // Don't generate a loss rate until it can be based on enough packets.
    if (expected < kLimitNumPackets) {
      expected_packets_since_last_loss_update_ = expected;
      lost_packets_since_last_loss_update_ += packets_lost;
      return;
    }
    has_decreased_since_last_fraction_loss_ = false;
    const int64_t lost_q8 = std::max<int64_t>(lost_packets_since_last_loss_update_ + packets_lost, 0) << 8;
    last_fraction_loss_ = static_cast<uint8_t>(std::min<int64_t>(lost_q8 / expected, 255));
    lost_packets_since_last_loss_update_ = 0;
    expected_packets_since_last_loss_update_ = 0;
    last_loss_packet_report_ = at_time;
    UpdateEstimate(at_time);
  }
}

void SendSideBandwidthEstimation::UpdateRtt(TimeDelta rtt, Timestamp /*at_time*/) {
  // Update the RTT if we were able to compute one from this RTCP.
  if (rtt > TimeDelta::Zero()) last_round_trip_time_ = rtt;
}

void SendSideBandwidthEstimation::UpdateEstimate(Timestamp at_time) {
  if (rtt_backoff_.IsRttAboveLimit()) {
    if ((!time_last_decrease_.IsFinite() || at_time - time_last_decrease_ >= rtt_backoff_.drop_interval_) &&
        current_target_ > rtt_backoff_.bandwidth_floor_) {
      time_last_decrease_ = at_time;
      const DataRate new_bitrate = std::max(current_target_ * rtt_backoff_.drop_fraction_, rtt_backoff_.bandwidth_floor_);
      ++rtt_backoffs_;
      SM_EVENT(events_, at_time, "send_side_bwe", "rtt_backoff", new_bitrate.bps(), rtt_backoff_.CorrectedRtt().ms());
      UpdateTargetBitrate(new_bitrate, at_time);
      return;
    }
    ApplyTargetLimits(at_time);
    return;
  }

  // We trust the REMB and/or delay-based estimate during the first 2 seconds if we haven't had any packet loss
  // reported, to allow start-up bitrate probing.
  if (last_fraction_loss_ == 0 && IsInStartPhase(at_time) && !loss_v2_.ReadyToUseInStartPhase()) {
    DataRate new_bitrate = current_target_;
    if (receiver_limit_.IsFinite()) new_bitrate = std::max(receiver_limit_, new_bitrate);
    if (delay_based_limit_.IsFinite()) new_bitrate = std::max(delay_based_limit_, new_bitrate);
    if (new_bitrate != current_target_) {
      min_bitrate_history_.clear();
      min_bitrate_history_.push_back({at_time, current_target_});
      UpdateTargetBitrate(new_bitrate, at_time);
      return;
    }
  }
  UpdateMinHistory(at_time);

  if (loss_v2_.IsReady()) {
    const LossBasedBweV2::Result result = loss_v2_.GetLossBasedResult();
    loss_based_state_ = result.state;
    UpdateTargetBitrate(result.bandwidth_estimate, at_time);
    return;
  }

  if (!last_loss_packet_report_.IsFinite()) {
    // No feedback received.
    ApplyTargetLimits(at_time);
    return;
  }

  const TimeDelta time_since_loss_packet_report = at_time - last_loss_packet_report_;
  if (time_since_loss_packet_report < kMaxRtcpFeedbackInterval * 1.2) {
    // We only care about loss above a given bitrate threshold (zero by default).
    const float loss = last_fraction_loss_ / 256.0f;
    if (current_target_ < DataRate::Zero() || loss <= kDefaultLowLossThreshold) {
      // Loss < 2 %: increase the rate by 8 % of the minimum bitrate of the last kBweIncreaseInterval. Remembering
      // the bitrate over the last second lets the rate ramp up a second faster than 8 % per second from now.
      DataRate new_bitrate = DataRate::BitsPerSec(static_cast<int64_t>(min_bitrate_history_.front().second.bps() * 1.08 + 0.5));
      // Add 1 kbps extra, just to make sure that we do not get stuck.
      new_bitrate += DataRate::BitsPerSec(1000);
      UpdateTargetBitrate(new_bitrate, at_time);
      return;
    } else if (current_target_ > DataRate::Zero()) {
      if (loss <= kDefaultHighLossThreshold) {
        // Loss between 2 % and 10 %: do nothing.
      } else {
        // Loss > 10 %: limit the rate decreases to once per kBweDecreaseInterval + rtt.
        if (!has_decreased_since_last_fraction_loss_ &&
            (!time_last_decrease_.IsFinite() || (at_time - time_last_decrease_) >= (kBweDecreaseInterval + last_round_trip_time_))) {
          time_last_decrease_ = at_time;
          // newRate = rate * (1 - 0.5 * lossRate), where packetLoss = 256 * lossRate.
          const DataRate new_bitrate =
              DataRate::BitsPerSec(static_cast<int64_t>((current_target_.bps() * static_cast<double>(512 - last_fraction_loss_)) / 512.0));
          has_decreased_since_last_fraction_loss_ = true;
          UpdateTargetBitrate(new_bitrate, at_time);
          return;
        }
      }
    }
  }
  ApplyTargetLimits(at_time);
}

bool SendSideBandwidthEstimation::IsInStartPhase(Timestamp at_time) const {
  return !first_report_time_.IsFinite() || at_time - first_report_time_ < kStartPhase;
}

void SendSideBandwidthEstimation::UpdateMinHistory(Timestamp at_time) {
  // Remove old data points from the history. The history precision is in ms, so add one to be able to increase the
  // bitrate if it is off by as little as 0.5 ms.
  while (!min_bitrate_history_.empty() &&
         at_time - min_bitrate_history_.front().first + TimeDelta::Millis(1) > kBweIncreaseInterval) {
    min_bitrate_history_.pop_front();
  }
  // Typical minimum sliding-window algorithm: pop values higher than the current bitrate before pushing it.
  while (!min_bitrate_history_.empty() && current_target_ <= min_bitrate_history_.back().second) {
    min_bitrate_history_.pop_back();
  }
  min_bitrate_history_.push_back({at_time, current_target_});
}

DataRate SendSideBandwidthEstimation::GetUpperLimit() const { return std::min(delay_based_limit_, max_bitrate_configured_); }

void SendSideBandwidthEstimation::UpdateTargetBitrate(DataRate new_bitrate, Timestamp /*at_time*/) {
  new_bitrate = std::min(new_bitrate, GetUpperLimit());
  if (new_bitrate < min_bitrate_configured_) new_bitrate = min_bitrate_configured_;
  current_target_ = new_bitrate;
}

void SendSideBandwidthEstimation::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("loss_based_state", static_cast<double>(static_cast<int>(loss_based_state_)));
  w.Gauge("fraction_loss", last_fraction_loss_ / 256.0);
  w.Counter("rtt_backoffs", rtt_backoffs_);
  loss_v2_.ExportMetrics(w);
}

}  // namespace sm::cc
