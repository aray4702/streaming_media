// 5.6 Bandwidth probing.
//
// ProbeController decides when to probe: at start-up (clusters at 3x and 6x
// the start rate), again whenever an estimate reaches 70% of the last probe
// rate (exponential probing upward), when the max bitrate is raised, and
// periodically while the application is limited (ALR) so a stale estimate
// can be revalidated. ProbeBitrateEstimator turns the feedback of one probe
// cluster into a rate: the receive rate if the link could not keep up
// (receive < 90% of send), else the send rate.
#pragma once

#include <map>
#include <optional>
#include <vector>

#include "sm/cc/transport_feedback.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"

namespace sm::cc {

struct ProbeCluster {
  int id = 0;
  DataRate rate;
  int min_probes = 5;
  DataSize min_bytes;
};

struct ProbeControllerConfig {
  double first_multiplier = 3.0;
  double second_multiplier = 6.0;
  double further_probe_threshold = 0.7;
  double further_multiplier = 2.0;
  TimeDelta alr_probe_interval = TimeDelta::Seconds(5);
  TimeDelta probe_duration = TimeDelta::Millis(15);
  EventSink* events = nullptr;
};

class ProbeController : public MetricsSource {
 public:
  explicit ProbeController(ProbeControllerConfig cfg = {}) : cfg_(cfg) {}
  std::vector<ProbeCluster> SetBitrates(DataRate min, DataRate start, DataRate max, Timestamp now);
  std::vector<ProbeCluster> SetEstimate(DataRate estimate, Timestamp now);
  std::vector<ProbeCluster> SetMaxBitrate(DataRate max, Timestamp now);
  // ALR: application-limited (sending well below the estimate).
  void SetApplicationLimited(bool alr, Timestamp now);
  std::vector<ProbeCluster> Process(Timestamp now);
  void EnablePeriodicAlrProbing(bool e) { periodic_alr_ = e; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  std::vector<ProbeCluster> Initiate(std::vector<DataRate> rates, Timestamp now, bool further);
  ProbeControllerConfig cfg_;
  DataRate max_ = DataRate::KilobitsPerSec(20000);
  DataRate estimate_;
  DataRate min_rate_to_continue_ = DataRate::PlusInfinity();  // Exponential probing.
  bool alr_ = false;
  bool periodic_alr_ = true;
  Timestamp alr_start_ = Timestamp::MinusInfinity();
  Timestamp last_probe_ = Timestamp::MinusInfinity();
  int next_id_ = 1;
  uint64_t clusters_ = 0;
};

class ProbeBitrateEstimator : public MetricsSource {
 public:
  void SetClusterInfo(const ProbeCluster& c) { clusters_[c.id].config = c; }
  // Feed every packet result; returns an estimate when a cluster completes.
  std::optional<DataRate> OnPacketResult(const PacketResult& r);
  std::optional<DataRate> FetchAndResetLastEstimate();
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  struct Cluster {
    ProbeCluster config;
    Timestamp first_send = Timestamp::PlusInfinity(), last_send = Timestamp::MinusInfinity();
    Timestamp first_recv = Timestamp::PlusInfinity(), last_recv = Timestamp::MinusInfinity();
    int64_t size_last_send = 0, size_first_recv = 0, bytes = 0;
    int count = 0;
    bool done = false;
  };
  std::map<int, Cluster> clusters_;
  std::optional<DataRate> last_;
  uint64_t estimates_ = 0, invalid_ = 0;
};

}  // namespace sm::cc
