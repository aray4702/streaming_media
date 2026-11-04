// 5.6 Bandwidth probing.
//
// libwebrtc's ProbeController (ProbeControllerV2 below) decides when to probe:
// at start-up (clusters at 3x and 6x the start rate), again whenever an
// estimate reaches 70% of the last probe rate (exponential probing upward),
// when the max bitrate is raised, after a large drop, and periodically in ALR
// when the stream asks for it. ProbeBitrateEstimator turns the feedback of one
// probe cluster into a rate: 95% of the receive rate if the link could not
// keep up (receive < 90% of send), else the lower of the two.
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
  // ProbeClusterConfig: when the cluster was requested and the
  // smallest time between two bursts (BitrateProber::RecommendedMinProbeSize).
  Timestamp at_time;
  TimeDelta min_probe_delta = TimeDelta::Millis(2);
  // A recovery probe (issue #14, not in libwebrtc): ProbeBitrateEstimator reports
  // its result only if the link kept up with it (receive rate >= 0.9 x send rate).
  bool recovery = false;
};

// libwebrtc's ProbeController (goog_cc/probe_controller.{h,cc}, M157,
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
// Beyond libwebrtc (issue #14, ProbeControllerV2Config::recovery_probing, which
// GccConfig turns on): a recovery probe at 2x the estimate after a 5 s hold below
// half the rate the link delivered in the last 30 s, on a clear path (no standing
// queue, little loss) and not application-limited.
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
  // Recovery probing (issue #14; beyond libwebrtc, an Owner decision). The level
  // is the highest acknowledged rate of the last `recovery_window` (what the link
  // delivered). When the estimate has been below `recovery_probe_fraction` of it
  // for a whole `recovery_probe_interval` (so the queue the drop built has
  // drained), the limit is the delay-based estimate with the detector normal (no
  // delay increase, loss or RTT limit), the sender is not application-limited
  // (ALR) and the path is clear (SetRecoveryPathClear: the feedback RTT near its
  // minimum and little loss), Process() sends a probe at `recovery_probe_scale` x
  // the estimate, never above the level, at most every `recovery_probe_interval`.
  // It probes further at 2x a result above 0.7 of the probe rate, as exponential
  // probing does, and stops once a probe finds no room or the delay rises
  // (InitiateProbing sends nothing then). Its clusters are marked `recovery`: a
  // result counts only if the link kept up with the probe, so a probe into a full
  // link (the capacity still low, or a competing flow that FIFO sharing would let a
  // saturated probe overrate) leaves the estimate alone.
  // libwebrtc probes after a drop only within 5 s and in or just after an ALR
  // (RequestProbe), so after a long hold at a low rate its target climbs back
  // additively from a stale link-capacity estimate (15 s and more on capacity_drop).
  // Off here, as in libwebrtc; GccConfig::recovery_probing turns it on.
  bool recovery_probing = false;
  TimeDelta recovery_window = TimeDelta::Seconds(30);
  double recovery_probe_fraction = 0.5;
  double recovery_probe_scale = 2.0;
  TimeDelta recovery_probe_interval = TimeDelta::Seconds(5);
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
  bool periodic_alr_probing() const { return enable_periodic_alr_probing_; }
  void EnableRepeatedInitialProbing(bool enable) { repeated_initial_probing_enabled_ = enable; }
  void SetAlrStartTime(std::optional<Timestamp> alr_start_time) { alr_start_time_ = alr_start_time; }
  void SetAlrEndedTime(Timestamp alr_end_time) { alr_end_time_ = alr_end_time; }
  std::vector<ProbeCluster> RequestProbe(Timestamp at_time);
  // The acknowledged rate, for recovery probing (issue #14; not in libwebrtc): the
  // highest of the last recovery_window is the level a recovery probe goes back to.
  void OnAcknowledgedRate(DataRate acked, Timestamp at_time);
  // Whether the path is clear: no standing queue (the feedback RTT near its minimum)
  // and little loss. A recovery probe goes out only then (issue #14). A probe into a full drop-tail
  // queue loses packets, and the estimator, which sees only the received ones,
  // would read the rest as a clean probe.
  void SetRecoveryPathClear(bool clear) { recovery_path_clear_ = clear; }
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
  // `recovery` marks the clusters (and the further probes they lead to) as recovery probes (issue #14).
  std::vector<ProbeCluster> InitiateProbing(Timestamp now, const std::vector<DataRate>& bitrates, bool probe_further, bool recovery = false);
  bool TimeForAlrProbe(Timestamp at_time) const;
  bool TimeForNextRepeatedInitialProbe(Timestamp at_time) const;
  std::vector<ProbeCluster> MaybeRecoveryProbe(Timestamp at_time);
  void UpdateRecoveryHold(Timestamp at_time);
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
  // Recovery probing: the highest acknowledged rate of the last recovery_window
  // (restarted from the current one once it is older), and the last recovery probe.
  DataRate recovery_level_ = DataRate::Zero();
  Timestamp recovery_level_time_ = Timestamp::MinusInfinity();
  Timestamp last_recovery_probe_ = Timestamp::MinusInfinity();
  std::optional<Timestamp> recovery_low_since_;  // Since when the estimate is below the fraction.
  uint64_t recovery_probes_ = 0;
  bool recovery_chain_ = false;  // The probes being waited for are recovery probes.
  bool recovery_path_clear_ = false;
};

class ProbeBitrateEstimator : public MetricsSource {
 public:
  // goog_cc/probe_bitrate_estimator.cc as is: only received packets count, an
  // estimate needs 80% of the probes and bytes, a result more than twice the
  // send rate is rejected, old clusters are erased after 1 s, and the newest
  // estimate wins.
  // Not in libwebrtc (issue #14): a recovery cluster's result is dropped when the
  // receive rate is below 0.9 x the send rate (the link did not keep up).
  ProbeBitrateEstimator() = default;
  void SetClusterInfo(const ProbeCluster& c);
  // Feed every packet result; returns an estimate when a cluster completes.
  std::optional<DataRate> OnPacketResult(const PacketResult& r);
  std::optional<DataRate> FetchAndResetLastEstimate();
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  struct Cluster {
    Timestamp first_send = Timestamp::PlusInfinity(), last_send = Timestamp::MinusInfinity();
    Timestamp first_recv = Timestamp::PlusInfinity(), last_recv = Timestamp::MinusInfinity();
    int64_t size_last_send = 0, size_first_recv = 0, bytes = 0;
    int count = 0;
  };
  void EraseOldClusters(Timestamp timestamp);
  std::map<int, Cluster> clusters_;
  std::map<int, ProbeCluster> configs_;  // The pacing info of the cluster a packet was sent in.
  std::optional<DataRate> last_;
  uint64_t estimates_ = 0, invalid_ = 0, recovery_saturated_ = 0;
};

}  // namespace sm::cc
