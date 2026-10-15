// 5.4 Send-side bandwidth estimation: the GCC network controller.
//
// Wires transport feedback into the delay-based estimator, the loss-based
// estimator, the acknowledged-bitrate estimator and the probe machinery,
// and produces the target rate for the encoders (min of delay-, loss- and
// REMB-based estimates, clamped to [min, max]) plus a pacing rate
// (target x pacing_factor) and any probe clusters to send. GccConfig::loss_bwe
// selects the loss-based estimator (5.3 v1 or v2).
#pragma once

#include <optional>
#include <vector>

#include "sm/cc/delay_based_bwe.h"
#include "sm/cc/loss_based_bwe.h"
#include "sm/cc/probing.h"
#include "sm/cc/transport_feedback.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"

namespace sm::cc {

struct GccConfig {
  DataRate min_rate = DataRate::KilobitsPerSec(30);
  DataRate start_rate = DataRate::KilobitsPerSec(300);
  DataRate max_rate = DataRate::KilobitsPerSec(10000);
  double pacing_factor = 2.5;
  bool enable_probing = true;
  // Loss-based estimator (5.3): v1 three-band rule (default) or v2
  // inherent-loss model.
  LossBweVersion loss_bwe = LossBweVersion::kV1;
  EventSink* events = nullptr;
};

struct NetworkControlUpdate {
  DataRate target_rate;
  DataRate pacing_rate;
  std::vector<ProbeCluster> probes;
  bool changed = false;
};

class GccController : public MetricsSource {
 public:
  explicit GccController(GccConfig cfg = {});
  NetworkControlUpdate Start(Timestamp now);
  void OnPacketSent(uint16_t transport_seq, Timestamp send_time, DataSize size, int probe_cluster = -1);
  NetworkControlUpdate OnTransportFeedback(const TransportFeedback& fb, Timestamp now);
  void OnRttUpdate(TimeDelta rtt);
  void OnRemb(DataRate remb) { remb_ = remb; }
  // Periodic tick (e.g. every 25 ms): ALR probing and outage handling.
  NetworkControlUpdate OnProcessInterval(Timestamp now, DataRate current_send_rate);
  // Hard reset after a network route change / outage recovery.
  NetworkControlUpdate ResetEstimate(DataRate rate, Timestamp now);
  DataRate target() const { return target_; }
  const DelayBasedBwe& delay_bwe() const { return delay_; }
  const LossBasedBwe& loss_bwe() const { return loss_; }
  const LossBasedBweV2& loss_bwe_v2() const { return loss_v2_; }
  // The active loss-based limit (max rate = not limiting).
  DataRate loss_limit() const { return cfg_.loss_bwe == LossBweVersion::kV2 ? loss_v2_.estimate() : loss_.estimate(); }
  const SendHistory& history() const { return history_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  NetworkControlUpdate MakeUpdate(Timestamp now, std::vector<ProbeCluster> probes);
  GccConfig cfg_;
  SendHistory history_;
  DelayBasedBwe delay_;
  LossBasedBwe loss_;
  LossBasedBweV2 loss_v2_;
  AcknowledgedBitrateEstimator acked_;
  ProbeController probe_ctrl_;
  ProbeBitrateEstimator probe_est_;
  std::optional<DataRate> remb_;
  DataRate target_;
  DataRate last_reported_;
  Histogram target_kbps_;
};

}  // namespace sm::cc
