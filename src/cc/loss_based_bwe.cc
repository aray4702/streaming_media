#include "sm/cc/loss_based_bwe.h"

namespace sm::cc {

DataRate LossBasedBwe::OnPacketResults(const std::vector<PacketResult>& results, DataRate current_target,
                                       Timestamp now) {
  if (!window_start_.IsFinite()) window_start_ = now;
  for (const auto& r : results) {
    ++total_;
    if (!r.received()) ++lost_;
  }
  if (total_ < cfg_.min_packets || now - window_start_ < cfg_.min_window) return rate_;
  last_loss_ = static_cast<double>(lost_) / static_cast<double>(total_);
  lost_ = total_ = 0;
  window_start_ = now;
  ++windows_;
  if (last_loss_ < cfg_.low_loss) {
    rate_ = cfg_.max_rate;  // Not limiting.
  } else if (last_loss_ <= cfg_.high_loss) {
    rate_ = current_target;  // Hold.
    ++holds_;
  } else if (!last_decrease_.IsFinite() || now - last_decrease_ >= cfg_.decrease_interval + rtt_) {
    rate_ = current_target * (1.0 - 0.5 * last_loss_);
    last_decrease_ = now;
    ++decreases_;
    SM_EVENT(cfg_.events, now, "loss_bwe", "decrease", last_loss_, current_target.bps());
  }
  rate_ = Clamp(rate_, cfg_.min_rate, cfg_.max_rate);
  return rate_;
}

void LossBasedBwe::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("limit_kbps", rate_.kbps());
  w.Gauge("limiting", limiting() ? 1 : 0);
  w.Gauge("loss", last_loss_);
  w.Counter("windows", windows_);
  w.Counter("holds", holds_);
  w.Counter("decreases", decreases_);
}

}  // namespace sm::cc
