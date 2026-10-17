// libwebrtc's SendSideBandwidthEstimation (`cc v2`), ported from
// compare/webrtc/reference/modules/congestion_controller/goog_cc/
// send_side_bandwidth_estimation.{h,cc} (M157), with its constants and defaults.
//
// It owns the target rate: the delay-based estimate caps it, the loss-based
// estimator (LossBasedBweV2, cc/loss_based_bwe_v2.h) sets it once it has three
// observations, the RTT backoff (3 s limit, x0.8 per second, 5 kbps floor) cuts
// it when feedback stops, and the receiver limit (REMB) caps what target_rate()
// returns. Before the loss-based estimator is ready it uses the receiver-report
// loss rule of the original GCC (UpdatePacketsLost: below 2 % +8 % of the
// minimum of the last second, 2-10 % hold, above 10 % x(1 - loss/2)), and during
// the first two seconds after the first loss report it trusts the delay-based
// estimate.
#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <utility>

#include "sm/cc/loss_based_bwe_v2.h"
#include "sm/cc/transport_feedback.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::cc {

// RttBasedBackoff: WebRTC-Bwe-MaxRttLimit defaults.
class RttBasedBackoff {
 public:
  void UpdatePropagationRtt(Timestamp at_time, TimeDelta propagation_rtt) {
    last_propagation_rtt_update_ = at_time;
    last_propagation_rtt_ = propagation_rtt;
  }
  void OnSentPacket(Timestamp send_time) { last_packet_sent_ = send_time; }
  bool IsRttAboveLimit() const { return CorrectedRtt() > rtt_limit_; }
  TimeDelta CorrectedRtt() const;

  TimeDelta rtt_limit_ = TimeDelta::Seconds(3);
  double drop_fraction_ = 0.8;
  TimeDelta drop_interval_ = TimeDelta::Seconds(1);
  DataRate bandwidth_floor_ = DataRate::KilobitsPerSec(5);
  // By starting at plus infinity the backoff cannot trigger before the first packet is sent.
  Timestamp last_propagation_rtt_update_ = Timestamp::PlusInfinity();
  TimeDelta last_propagation_rtt_ = TimeDelta::Zero();
  Timestamp last_packet_sent_ = Timestamp::MinusInfinity();
};

class SendSideBandwidthEstimation : public MetricsSource {
 public:
  explicit SendSideBandwidthEstimation(EventSink* events = nullptr);

  void OnRouteChange();
  void SetBitrates(std::optional<DataRate> send_bitrate, DataRate min_bitrate, DataRate max_bitrate, Timestamp at_time);
  void SetSendBitrate(DataRate bitrate, Timestamp at_time);
  void SetMinMaxBitrate(DataRate min_bitrate, DataRate max_bitrate);
  DataRate target_rate() const;
  LossBasedState loss_based_state() const { return loss_based_state_; }
  bool IsRttAboveLimit() const { return rtt_backoff_.IsRttAboveLimit(); }
  uint8_t fraction_loss() const { return last_fraction_loss_; }
  DataRate delay_based_limit() const { return delay_based_limit_; }

  void UpdateReceiverEstimate(Timestamp at_time, DataRate bandwidth);
  void UpdateDelayBasedEstimate(Timestamp at_time, DataRate bitrate);
  void SetAcknowledgedRate(std::optional<DataRate> acknowledged_rate, Timestamp at_time);
  // The loss-based estimator reads the feedback's per-packet results.
  void UpdateLossBasedEstimator(std::span<const PacketResult> results, bool in_alr, Timestamp at_time);
  // Loss from receiver reports (GoogCcNetworkController::OnTransportLossReport).
  void UpdatePacketsLost(int64_t packets_lost, int64_t number_of_packets, Timestamp at_time);
  void UpdateRtt(TimeDelta rtt, Timestamp at_time);
  void UpdateEstimate(Timestamp at_time);
  void UpdatePropagationRtt(Timestamp at_time, TimeDelta propagation_rtt) { rtt_backoff_.UpdatePropagationRtt(at_time, propagation_rtt); }
  void OnSentPacket(Timestamp send_time) { rtt_backoff_.OnSentPacket(send_time); }

  const LossBasedBweV2& loss_based() const { return loss_v2_; }
  bool IsInStartPhase(Timestamp at_time) const;
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  void UpdateMinHistory(Timestamp at_time);
  DataRate GetUpperLimit() const;
  void UpdateTargetBitrate(DataRate new_bitrate, Timestamp at_time);
  void ApplyTargetLimits(Timestamp at_time) { UpdateTargetBitrate(current_target_, at_time); }

  EventSink* events_;
  RttBasedBackoff rtt_backoff_;
  std::deque<std::pair<Timestamp, DataRate>> min_bitrate_history_;
  int64_t lost_packets_since_last_loss_update_ = 0;
  int64_t expected_packets_since_last_loss_update_ = 0;
  DataRate current_target_ = DataRate::Zero();
  DataRate min_bitrate_configured_;
  DataRate max_bitrate_configured_;
  bool has_decreased_since_last_fraction_loss_ = false;
  Timestamp last_loss_feedback_ = Timestamp::MinusInfinity();
  Timestamp last_loss_packet_report_ = Timestamp::MinusInfinity();
  uint8_t last_fraction_loss_ = 0;
  TimeDelta last_round_trip_time_ = TimeDelta::Zero();
  DataRate receiver_limit_ = DataRate::PlusInfinity();
  DataRate delay_based_limit_ = DataRate::PlusInfinity();
  Timestamp time_last_decrease_ = Timestamp::MinusInfinity();
  Timestamp first_report_time_ = Timestamp::MinusInfinity();
  std::optional<DataRate> acknowledged_rate_;
  LossBasedBweV2 loss_v2_;
  LossBasedState loss_based_state_ = LossBasedState::kDelayBasedEstimate;
  uint64_t rtt_backoffs_ = 0;
};

}  // namespace sm::cc
