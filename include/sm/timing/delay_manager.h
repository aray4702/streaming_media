// 4.3 Audio target-delay estimation (NetEq DelayManager style).
//
// Each packet's relative delay is its transit time minus the smallest
// transit seen in the last `history` window: how much later than the fastest
// recent packet it arrived. Relative delays go into a histogram with
// exponential forgetting; the target playout delay is the `quantile` of
// that histogram, so the buffer grows quickly when jitter rises and decays
// slowly when the network calms down.
#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include "sm/core/metrics.h"
#include "sm/core/seq_num.h"
#include "sm/core/units.h"

namespace sm::timing {

struct DelayManagerConfig {
  double quantile = 0.95;
  double forget_factor = 0.983;   // Per packet.
  int bucket_ms = 10;
  int max_delay_ms = 2000;
  int min_delay_ms = 0;
  TimeDelta history = TimeDelta::Seconds(2);
  int initial_target_ms = 60;
};

class DelayManager : public MetricsSource {
 public:
  explicit DelayManager(DelayManagerConfig cfg = {});
  // Returns the relative delay of this packet in ms.
  std::optional<double> Update(uint32_t rtp_ts, int sample_rate, Timestamp arrival);
  // Target delay rounded up to whole packets of `packet_ms`.
  int TargetDelayMs() const { return target_ms_; }
  void SetPacketDurationMs(int ms) { packet_ms_ = ms; }
  void SetMinimumDelayMs(int ms) { cfg_.min_delay_ms = ms; }
  void Reset();
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  void Recompute();
  DelayManagerConfig cfg_;
  SeqNumUnwrapper<uint32_t> unwrapper_;
  std::vector<double> hist_;
  std::deque<std::pair<Timestamp, double>> transit_window_;  // (arrival, transit ms)
  int target_ms_;
  int packet_ms_ = 20;
  uint64_t updates_ = 0;
  Histogram relative_delay_ms_;
};

}  // namespace sm::timing
