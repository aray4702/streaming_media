// 5.3 Loss-based bandwidth estimation (classic GCC sender-side rule).
//
// Applied to the current target rate A once per measurement window:
//   loss < 2%   no loss-based limit (the delay-based estimate governs)
//   2% - 10%    hold: limit = A
//   loss > 10%  decrease: limit = A * (1 - 0.5 * loss), at most once per
//               300 ms + RTT
//
// Loss is measured over windows of at least 1 s and 20 packets, so a lucky
// or unlucky handful of packets cannot swing the estimate. (libwebrtc v1
// additionally caps increases at 8%/s after start-up; here the delay-based
// controller and probing own the upward direction.)
#pragma once

#include <vector>

#include "sm/cc/transport_feedback.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"

namespace sm::cc {

struct LossBweConfig {
  double low_loss = 0.02;
  double high_loss = 0.10;
  TimeDelta decrease_interval = TimeDelta::Millis(300);
  TimeDelta min_window = TimeDelta::Seconds(1);
  int min_packets = 20;
  DataRate min_rate = DataRate::KilobitsPerSec(30);
  DataRate max_rate = DataRate::KilobitsPerSec(20000);
  EventSink* events = nullptr;
};

class LossBasedBwe : public MetricsSource {
 public:
  explicit LossBasedBwe(LossBweConfig cfg = {}) : cfg_(cfg), rate_(cfg.max_rate) {}
  void Reset() { rate_ = cfg_.max_rate; lost_ = total_ = 0; window_start_ = Timestamp::MinusInfinity(); }
  void SetRtt(TimeDelta rtt) { rtt_ = rtt; }
  // Accumulates results; returns the loss-based limit (max_rate = no limit).
  DataRate OnPacketResults(const std::vector<PacketResult>& results, DataRate current_target, Timestamp now);
  DataRate estimate() const { return rate_; }
  bool limiting() const { return rate_ < cfg_.max_rate; }
  double last_loss() const { return last_loss_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  LossBweConfig cfg_;
  DataRate rate_;
  TimeDelta rtt_ = TimeDelta::Millis(100);
  int64_t lost_ = 0, total_ = 0;
  Timestamp window_start_ = Timestamp::MinusInfinity();
  double last_loss_ = 0;
  Timestamp last_decrease_ = Timestamp::MinusInfinity();
  uint64_t windows_ = 0, holds_ = 0, decreases_ = 0;
};

}  // namespace sm::cc
