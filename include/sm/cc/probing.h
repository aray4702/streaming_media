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
  // `cc v2` (ProbeClusterConfig): when the cluster was requested and the
  // smallest time between two bursts (BitrateProber::RecommendedMinProbeSize).
  Timestamp at_time;
  TimeDelta min_probe_delta = TimeDelta::Millis(2);
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

// `cc v2`: libwebrtc's ProbeController (goog_cc/probe_controller.{h,cc}, M157,
// compare/webrtc/reference/) with its defaults (WebRTC-Bwe-ProbingConfiguration
// unset) and no NetworkStateEstimate (the default GoogCC has no estimator):
//   - exponential probing when the network is available and the bitrates are
//     set: clusters at 3x and 6x the start rate, then 2x the result for as long
//     as it exceeds 0.7 of the last probe rate;
//   - a probe when the max bitrate or the max total allocated bitrate rises;
//   - after a big drop, one probe at 0.85 x the rate before it when the delay
//     detector recovers from overuse (RequestProbe), within 5 s of the drop and
//     in or just after an ALR;
//   - periodic ALR probing at 2x the estimate every 5 s, only when the stream
//     requests it (EnablePeriodicAlrProbing; off by default);
//   - no probes while the estimate is limited by delay increase, loss or RTT.
// A cluster has 5 packets and 15 ms of the probe rate, bursts 2 ms apart.
enum class BandwidthLimitedCause : int {
  kLossLimitedBweIncreasing = 0,
  kLossLimitedBwe = 1,
  kDelayBasedLimited = 2,
  kDelayBasedLimitedDelayIncreased = 3,
  kRttBasedBackOffHighRtt = 4,
};

struct ProbeControllerV2Config {
  double first_exponential_probe_scale = 3.0;
  double second_exponential_probe_scale = 6.0;
  double further_exponential_probe_scale = 2.0;
  double further_probe_threshold = 0.7;
  bool abort_further_probe_if_max_lower_than_current = false;
  TimeDelta repeated_initial_probing_time_period = TimeDelta::Seconds(5);
  TimeDelta initial_probe_duration = TimeDelta::Millis(100);
  TimeDelta initial_min_probe_delta = TimeDelta::Millis(20);
  TimeDelta alr_probing_interval = TimeDelta::Seconds(5);
  double alr_probe_scale = 2.0;
  bool probe_on_max_allocated_bitrate_change = true;
  bool probe_on_max_allocated_bitrate_change_without_alr = true;
  double first_allocation_probe_scale = 1.0;
  double second_allocation_probe_scale = 2.0;
  double allocation_probe_limit_by_current_scale = 2.0;
  int min_probe_packets_sent = 5;
  TimeDelta min_probe_duration = TimeDelta::Millis(15);
  TimeDelta min_probe_delta = TimeDelta::Millis(2);
  double loss_limited_probe_scale = 1.5;
  EventSink* events = nullptr;
};

class ProbeControllerV2 : public MetricsSource {
 public:
  explicit ProbeControllerV2(ProbeControllerV2Config cfg = {});
  std::vector<ProbeCluster> SetBitrates(DataRate min_bitrate, DataRate start_bitrate, DataRate max_bitrate, Timestamp at_time);
  std::vector<ProbeCluster> OnMaxTotalAllocatedBitrate(DataRate max_total_allocated_bitrate, Timestamp at_time);
  std::vector<ProbeCluster> OnNetworkAvailability(bool available, Timestamp at_time);
  std::vector<ProbeCluster> SetEstimatedBitrate(DataRate bitrate, BandwidthLimitedCause cause, Timestamp at_time);
  void EnablePeriodicAlrProbing(bool enable) { enable_periodic_alr_probing_ = enable; }
  void EnableRepeatedInitialProbing(bool enable) { repeated_initial_probing_enabled_ = enable; }
  void SetAlrStartTime(std::optional<Timestamp> alr_start_time) { alr_start_time_ = alr_start_time; }
  void SetAlrEndedTime(Timestamp alr_end_time) { alr_end_time_ = alr_end_time; }
  std::vector<ProbeCluster> RequestProbe(Timestamp at_time);
  void Reset(Timestamp at_time);
  std::vector<ProbeCluster> Process(Timestamp at_time);
  void ExportMetrics(MetricsWriter& w) const override;

  enum class State { kInit, kWaitingForProbingResult, kProbingComplete };
  State state() const { return state_; }
  DataRate estimated_bitrate() const { return estimated_bitrate_; }
  DataRate max_bitrate() const { return max_bitrate_; }

 private:
  void UpdateState(State s);
  std::vector<ProbeCluster> InitiateExponentialProbing(Timestamp at_time);
  std::vector<ProbeCluster> InitiateProbing(Timestamp now, const std::vector<DataRate>& bitrates, bool probe_further);
  bool TimeForAlrProbe(Timestamp at_time) const;
  bool TimeForNextRepeatedInitialProbe(Timestamp at_time) const;
  ProbeCluster CreateProbeCluster(Timestamp at_time, DataRate bitrate);

  ProbeControllerV2Config cfg_;
  bool network_available_ = false;
  bool repeated_initial_probing_enabled_ = false;
  Timestamp last_allowed_repeated_initial_probe_ = Timestamp::MinusInfinity();
  BandwidthLimitedCause bandwidth_limited_cause_ = BandwidthLimitedCause::kDelayBasedLimited;
  State state_ = State::kInit;
  DataRate min_bitrate_to_probe_further_ = DataRate::PlusInfinity();
  Timestamp time_last_probing_initiated_ = Timestamp::MinusInfinity();
  DataRate estimated_bitrate_ = DataRate::Zero();
  DataRate start_bitrate_ = DataRate::Zero();
  DataRate max_bitrate_ = DataRate::PlusInfinity();
  Timestamp last_bwe_drop_probing_time_;
  std::optional<Timestamp> alr_start_time_;
  std::optional<Timestamp> alr_end_time_;
  bool enable_periodic_alr_probing_ = false;
  Timestamp time_of_last_large_drop_ = Timestamp::MinusInfinity();
  DataRate bitrate_before_last_large_drop_ = DataRate::Zero();
  DataRate max_total_allocated_bitrate_ = DataRate::Zero();
  int next_probe_cluster_id_ = 1;
  uint64_t clusters_ = 0;
};

class ProbeBitrateEstimator : public MetricsSource {
 public:
  // `libwebrtc` (`cc v2`): goog_cc/probe_bitrate_estimator.cc as is: only
  // received packets count, an estimate needs 80% of the probes and bytes, a
  // result more than twice the send rate is rejected, old clusters are erased
  // after 1 s, and the newest estimate wins.
  explicit ProbeBitrateEstimator(bool libwebrtc = false) : libwebrtc_(libwebrtc) {}
  void SetClusterInfo(const ProbeCluster& c);
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
  std::optional<DataRate> OnPacketResultV2(const PacketResult& r);
  void EraseOldClusters(Timestamp timestamp);
  bool libwebrtc_;
  std::map<int, Cluster> clusters_;
  std::map<int, ProbeCluster> configs_;  // v2: the pacing info of the cluster a packet was sent in.
  std::optional<DataRate> last_;
  uint64_t estimates_ = 0, invalid_ = 0;
};

}  // namespace sm::cc
