// 5.4 Send-side bandwidth estimation: the GCC network controller.
//
// libwebrtc's GoogCcNetworkController (goog_cc_network_control.cc, M157,
// compare/webrtc/reference/). Wires transport feedback into the delay-based
// estimator, SendSideBandwidthEstimation with LossBasedBweV2, the acknowledged-
// bitrate estimator and the probe machinery, and produces the target rate for
// the encoders plus the pacing and padding rates (GetPacingRates) and any probe
// clusters to send.
#pragma once

#include <limits>
#include <optional>
#include <vector>

#include "sm/cc/alr_detector.h"
#include "sm/cc/delay_based_bwe.h"
#include "sm/cc/probing.h"
#include "sm/cc/send_side_bandwidth_estimation.h"
#include "sm/cc/transport_feedback.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"

namespace sm::cc {

// One row of the BWE trace (issue #149): what libwebrtc's RtcEventLog records as
// RtcEventBweUpdateDelayBased and RtcEventBweUpdateLossBased, plus the final
// target. sm_compare writes libwebrtc's rows in the same layout (BweTraceCsv in
// testbench/call.h), so tools/bwe_trace.py can lay the two stacks side by side.
// -1 marks a column the event does not have.
struct BweTraceRow {
  // kDetector: one TrendlineEstimator::Update with calculated deltas, i.e. one
  // completed send-time group (issue #152): detector = the state after it,
  // packets = the deltas seen so far (num_of_deltas_), and the trend columns below.
  // time_us is the feedback time. libwebrtc's rows come from its own
  // InterArrivalDelta and TrendlineEstimator fed with the feedback its GoogCC got.
  enum class Kind { kDelayBased, kLossBased, kTarget, kDetector };
  int64_t time_us = 0;
  Kind kind = Kind::kTarget;
  int64_t bitrate_bps = 0;
  int detector = -1;            // BandwidthUsage as an int (delay-based rows).
  int fraction_loss = -1;       // 0-255 (loss-based rows).
  int packets = -1;             // Packets in the loss report (loss-based rows).
  int loss_state = -1;          // LossBasedState (this library's loss-based rows only).
  int64_t loss_limit_bps = -1;  // LossBasedBweV2's loss-limited estimate (this library only).
  int64_t instant_ub_bps = -1;  // LossBasedBweV2's instant upper bound (this library only).
  // kDetector rows only (NaN elsewhere): the modified trend and the threshold after the
  // update, the group deltas it took, and the send and arrival time (ms) it was given.
  double modified_trend = std::numeric_limits<double>::quiet_NaN();
  double threshold = std::numeric_limits<double>::quiet_NaN();
  double send_delta_ms = std::numeric_limits<double>::quiet_NaN();
  double recv_delta_ms = std::numeric_limits<double>::quiet_NaN();
  double send_ms = std::numeric_limits<double>::quiet_NaN();
  double arrival_ms = std::numeric_limits<double>::quiet_NaN();
};

struct GccConfig {
  DataRate min_rate = DataRate::KilobitsPerSec(30);
  DataRate start_rate = DataRate::KilobitsPerSec(300);
  DataRate max_rate = DataRate::KilobitsPerSec(10000);
  bool enable_probing = true;
  // StreamsConfig::requests_alr_probing, which a screen-share stream
  // sets; periodic ALR probes are off otherwise.
  bool periodic_alr_probing = false;
  EventSink* events = nullptr;
  // When set, rows are appended as libwebrtc's RtcEventLog would log them
  // (delay-based and loss-based updates when they change) and the target when it
  // changes. The test bench sets it for --link-trace.
  std::vector<BweTraceRow>* bwe_trace = nullptr;
};

struct NetworkControlUpdate {
  DataRate target_rate;
  DataRate pacing_rate;
  std::vector<ProbeCluster> probes;
  bool changed = false;
  // GoogCC sent a TargetTransferRate that the transport passes on to the
  // bitrate allocator: the target, the loss fraction or the RTT changed since
  // the last one passed on (MaybeTriggerOnNetworkChanged, then the
  // transport's CongestionControlHandler::GetUpdate). Only then do the
  // streams get a new allocation (BitrateAllocator::OnNetworkEstimateChanged).
  bool notify = false;
};

class GccController : public MetricsSource {
 public:
  explicit GccController(GccConfig cfg = {});
  NetworkControlUpdate Start(Timestamp now);
  void OnPacketSent(uint16_t transport_seq, Timestamp send_time, DataSize size, int probe_cluster = -1);
  NetworkControlUpdate OnTransportFeedback(const TransportFeedback& fb, Timestamp now);
  void OnRttUpdate(TimeDelta rtt);
  // The RTT the controller last took (rounded to whole ms).
  TimeDelta last_rtt() const { return last_rtt_; }
  void OnRemb(DataRate remb);
  // GoogCcNetworkController::OnTransportLossReport. The loss and packet
  // counts are the deltas between two receiver reports (summed over the report
  // blocks). Used for the loss rate until LossBasedBweV2 has three observations.
  NetworkControlUpdate OnTransportLossReport(int64_t packets_lost_delta, int64_t packets_delta, Timestamp now);
  // StreamsConfig::max_total_allocated_bitrate, the sum of the maximum bitrates
  // the allocator hands out: a rise probes up to twice that rate, and it caps
  // every probe at twice that rate. Returns the clusters to send. Before Start()
  // the value is held and handed to the probe controller after the initial
  // exponential probes, so it does not cap them (issue #62): libwebrtc's call
  // creates GoogCC when the network comes up, before the streams start and
  // register their bitrates (see Start()).
  std::vector<ProbeCluster> OnMaxTotalAllocatedBitrate(DataRate max_total_allocated_bitrate, Timestamp now);
  // The sum of the minimums the bitrate allocator must fund is a lower bound for
  // the estimates and the target (GoogCC use_min_allocatable_as_lower_bound_,
  // ClampConstraints). Never below the configured minimum.
  void SetMinAllocatable(DataRate r);
  // StreamsConfig::min_total_allocated_bitrate and max_padding_rate,
  // which GetPacingRates() reads (the allocator's limits). Takes effect with the
  // next update.
  void SetPacingLimits(DataRate min_total_allocated, DataRate max_padding);
  // StreamsConfig::pacing_factor (GoogCcNetworkController::OnStreamsConfig).
  // Once set, GetPacingRates() uses it in place of the defaults (2.5 before the
  // first transport feedback, 1.1 after it). A video stream with transport-cc sets
  // 1.1 from the start (VideoSendStreamImpl's kStrictPacingMultiplier, issue #187).
  // Takes effect with the next update.
  void SetPacingFactor(double factor);
  // StreamsConfig::requests_alr_probing (GoogCcNetworkController::OnStreamsConfig
  // and the initial config): ProbeController::EnablePeriodicAlrProbing.
  void EnablePeriodicAlrProbing(bool enable);
  // The pacing and padding rates as GetPacingRates() computes them (the caller takes the
  // minimum of the padding rate and the congestion-window pushback target). An update's
  // `pacing_rate` is PacingRate() at the time of the update.
  DataRate PacingRate() const;
  DataRate PaddingRate() const;
  // Periodic tick (e.g. every 25 ms): ALR probing and outage handling.
  NetworkControlUpdate OnProcessInterval(Timestamp now, DataRate current_send_rate);
  // Hard reset after a network route change / outage recovery.
  NetworkControlUpdate ResetEstimate(DataRate rate, Timestamp now);
  DataRate target() const { return target_; }
  // The lower bound in force (configured minimum, raised to the allocator's minimum).
  DataRate min_rate() const { return cfg_.min_rate; }
  // Largest (feedback time - send time) among the packets the last feedback
  // reported as received; nullopt if it reported none.
  std::optional<TimeDelta> last_feedback_max_rtt() const { return last_feedback_max_rtt_; }
  const DelayBasedBwe& delay_bwe() const { return delay_; }
  const AlrDetector& alr_detector() const { return alr_; }
  const ProbeControllerV2& probe_controller_v2() const { return probe2_; }
  const SendSideBandwidthEstimation& send_side_bwe() const { return ssbe_; }
  const SendHistory& history() const { return history_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  NetworkControlUpdate MakeUpdate(Timestamp now, std::vector<ProbeCluster> probes);
  void TraceDetector(Timestamp now);
  void TraceDelayBased(Timestamp now, DataRate delay_est, bool updated);
  void TraceLossBased(Timestamp now);
  std::vector<ProbeCluster> RegisterProbes(std::vector<ProbeCluster> probes);
  GccConfig cfg_;
  DataRate configured_min_;
  SendHistory history_;
  DelayBasedBwe delay_;
  SendSideBandwidthEstimation ssbe_;
  // AcknowledgedBitrateEstimatorInterface::Create returns the RobustThroughputEstimator by default (M157).
  RobustThroughputEstimator acked_v2_;
  ProbeControllerV2 probe2_;
  AlrDetector alr_;
  ProbeBitrateEstimator probe_est_;
  bool first_feedback_received_ = false;
  bool previously_in_alr_ = false;
  bool last_is_bandwidth_limited_ = true;
  // The rest of MaybeTriggerOnNetworkChanged's change check (the
  // target and is_bandwidth_limited are the other two).
  LossBasedState last_loss_based_state_ = LossBasedState::kDelayBasedEstimate;
  uint8_t last_fraction_loss_ = 0;
  TimeDelta last_round_trip_time_ = TimeDelta::PlusInfinity();
  // The last update passed on to the allocator (`NetworkControlUpdate::notify`).
  bool notified_ = false;
  DataRate notified_target_;
  uint8_t notified_fraction_loss_ = 0;
  TimeDelta notified_round_trip_time_ = TimeDelta::PlusInfinity();
  bool first_packet_sent_ = false;
  bool started_ = false;
  // OnMaxTotalAllocatedBitrate before Start(), applied after the initial probes.
  std::optional<DataRate> pending_max_total_allocated_;
  DataRate min_total_allocated_;
  DataRate max_padding_;
  std::optional<double> streams_pacing_factor_;  // StreamsConfig::pacing_factor.
  Timestamp last_now_ = Timestamp::Micros(0);
  DataRate target_;
  DataRate last_reported_;
  std::optional<TimeDelta> last_feedback_max_rtt_;
  TimeDelta last_rtt_ = TimeDelta::Zero();
  Histogram target_kbps_;
  // BWE trace state (bwe_trace): the last values logged, as libwebrtc keeps them.
  DataRate trace_delay_bitrate_ = DataRate::Zero();
  BandwidthUsage trace_delay_state_ = BandwidthUsage::kNormal;
  int64_t trace_loss_target_bps_ = -1;
  int trace_loss_fraction_ = -1;
  Timestamp trace_loss_time_ = Timestamp::MinusInfinity();
  int64_t trace_target_bps_ = -1;
};

}  // namespace sm::cc
