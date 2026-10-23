#include "sm/cc/control_handler.h"

namespace sm::cc {

std::optional<TargetTransferRate> CongestionControlHandler::GetUpdate() {
  if (!last_incoming_) return std::nullopt;
  TargetTransferRate new_outgoing = *last_incoming_;
  const bool pause_encoding = !network_available_ || pacer_expected_queue_ms_ > kMaxExpectedQueueLength.ms();
  if (pause_encoding) new_outgoing.target_rate = DataRate::Zero();
  if (!last_reported_ || last_reported_->target_rate != new_outgoing.target_rate ||
      last_reported_->cwnd_reduce_ratio != new_outgoing.cwnd_reduce_ratio ||
      (!new_outgoing.target_rate.IsZero() && (last_reported_->fraction_loss != new_outgoing.fraction_loss ||
                                              last_reported_->round_trip_time != new_outgoing.round_trip_time))) {
    encoder_paused_in_last_report_ = pause_encoding;
    last_reported_ = new_outgoing;
    return new_outgoing;
  }
  return std::nullopt;
}

}  // namespace sm::cc
