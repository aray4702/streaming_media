// The transport's filter between GoogCC and the bitrate allocator (part of the
// send-side controller, 5.4; issue #204).
//
// Port of libwebrtc's CongestionControlHandler
// (modules/congestion_controller/rtp/control_handler.cc, M157). GoogCC hands
// every TargetTransferRate it makes to SetTargetRate(); GetUpdate() returns it
// only if it differs from the last one passed on in the target, the
// congestion-window reduce ratio or, unless the target is zero, the loss
// fraction or the RTT. Only a message passed on reaches the bitrate allocator
// (RtpTransportControllerSend::UpdateControlState), so the streams keep their
// allocation, and the encoder its target, until the next one.
//
// The target is zero (the encoder paused) while the network is unavailable or
// the pacer's expected queue time is above kMaxExpectedQueueLength (2 s), which
// the transport reports every 25 ms (kPacerQueueUpdateInterval).
#pragma once

#include <cstdint>
#include <optional>

#include "sm/core/units.h"

namespace sm::cc {

// The fields of libwebrtc's TargetTransferRate that the handler compares.
struct TargetTransferRate {
  DataRate target_rate;
  double cwnd_reduce_ratio = 0;
  uint8_t fraction_loss = 0;  // network_estimate.loss_rate_ratio x 255.
  TimeDelta round_trip_time = TimeDelta::PlusInfinity();
};

class CongestionControlHandler {
 public:
  // PacingController::kMaxExpectedQueueLength.
  static constexpr TimeDelta kMaxExpectedQueueLength = TimeDelta::Millis(2000);

  void SetTargetRate(const TargetTransferRate& new_target_rate) { last_incoming_ = new_target_rate; }
  void SetNetworkAvailability(bool network_available) { network_available_ = network_available; }
  void SetPacerQueue(TimeDelta expected_queue_time) { pacer_expected_queue_ms_ = expected_queue_time.ms(); }
  std::optional<TargetTransferRate> GetUpdate();

  bool encoder_paused() const { return encoder_paused_in_last_report_; }

 private:
  std::optional<TargetTransferRate> last_incoming_;
  std::optional<TargetTransferRate> last_reported_;
  bool network_available_ = true;
  bool encoder_paused_in_last_report_ = false;
  int64_t pacer_expected_queue_ms_ = 0;
};

}  // namespace sm::cc
