// 5.4 Send-side bandwidth estimation: the GCC network controller.
//
// Wires transport feedback into the delay-based estimator, the loss-based
// estimator, the acknowledged-bitrate estimator and the probe machinery,
// and produces the target rate for the encoders (min of delay-, loss- and
// REMB-based estimates, clamped to [min, max]) plus a pacing rate
// (target x pacing_factor) and any probe clusters to send.
#pragma once

#include <optional>
#include <vector>

#include "sm/cc/alr_detector.h"
#include "sm/cc/delay_based_bwe.h"
#include "sm/cc/loss_based_bwe.h"
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
  // kProbeCluster: a probe cluster created (bitrate = its target, packets = its id;
  // libwebrtc's RtcEventProbeClusterCreated). kProbeResult: a probe result
  // (bitrate = the measured rate, 0 on a failure; packets = the cluster id;
  // RtcEventProbeResultSuccess / Failure). kAlr: the ALR detector's state changed
  // (bitrate = 1 entering ALR, 0 leaving it; RtcEventAlrState). kSent: the sent
  // packets the controller's OnSentPacket received in a 100 ms bucket starting at
  // time_us (bitrate = their bytes over 100 ms, packets = their count), the input of
  // the AlrDetector; on libwebrtc's side, fraction_loss = how many of them had a send time
  // before the previous one's, and loss_limit_bps = the largest such step back in ms.
  // kControllerTarget: the controller's own target output
  // (libwebrtc only; this library's kTarget rows already are). Issue #152.
  enum class Kind { kDelayBased, kLossBased, kTarget, kProbeCluster, kProbeResult, kAlr, kSent, kControllerTarget };
  int64_t time_us = 0;
  Kind kind = Kind::kTarget;
  int64_t bitrate_bps = 0;
  int detector = -1;            // BandwidthUsage as an int (delay-based rows).
  int fraction_loss = -1;       // 0-255 (loss-based rows).
  int packets = -1;             // Packets in the loss report (loss-based rows).
  int loss_state = -1;          // LossBasedState (this library's loss-based rows only).
  int64_t loss_limit_bps = -1;  // LossBasedBweV2's loss-limited estimate (this library only).
  int64_t instant_ub_bps = -1;  // LossBasedBweV2's instant upper bound (this library only).
};

struct GccConfig {
  DataRate min_rate = DataRate::KilobitsPerSec(30);
  DataRate start_rate = DataRate::KilobitsPerSec(300);
  DataRate max_rate = DataRate::KilobitsPerSec(10000);
  double pacing_factor = 2.5;
  bool enable_probing = true;
  // 1: the original controller. 2 (`cc v2`): libwebrtc's GoogCC, ported in
  // stages (#61); so far the delay detector's front end (#19), the acked-rate
  // estimator and AimdRateControl (#15), and the AlrDetector, ProbeController
  // and ProbeBitrateEstimator (#48; the BitrateProber is in `pacer v2`), and
  // SendSideBandwidthEstimation with LossBasedBweV2 (#12), and GetPacingRates (#61).
  int version = 1;
  // `cc v2`: StreamsConfig::requests_alr_probing, which a screen-share stream
  // sets; periodic ALR probes are off otherwise.
  bool periodic_alr_probing = false;
  EventSink* events = nullptr;
  // `cc v2`: when set, rows are appended as libwebrtc's RtcEventLog would log them
  // (delay-based and loss-based updates when they change) and the target when it
  // changes. The test bench sets it for --link-trace.
  std::vector<BweTraceRow>* bwe_trace = nullptr;
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
  // The RTT the controller last took (rounded to whole ms with `cc v2`).
  TimeDelta last_rtt() const { return last_rtt_; }
  void OnRemb(DataRate remb);
  // `cc v2`: GoogCcNetworkController::OnTransportLossReport. The loss and packet
  // counts are the deltas between two receiver reports (summed over the report
  // blocks). Used for the loss rate until LossBasedBweV2 has three observations.
  NetworkControlUpdate OnTransportLossReport(int64_t packets_lost_delta, int64_t packets_delta, Timestamp now);
  // `cc v2`: StreamsConfig::max_total_allocated_bitrate, the sum of the maximum
  // bitrates the allocator hands out: a rise probes up to twice that rate, and
  // it caps every probe at twice that rate. Returns the clusters to send. No
  // effect with `cc v1`.
  std::vector<ProbeCluster> OnMaxTotalAllocatedBitrate(DataRate max_total_allocated_bitrate, Timestamp now);
  // The sum of the minimums the bitrate allocator must fund is a lower bound for
  // the estimates and the target (GoogCC use_min_allocatable_as_lower_bound_,
  // ClampConstraints). Never below the configured minimum.
  void SetMinAllocatable(DataRate r);
  // `cc v2`: StreamsConfig::min_total_allocated_bitrate and max_padding_rate,
  // which GetPacingRates() reads (the allocator's limits). Takes effect with the
  // next update.
  void SetPacingLimits(DataRate min_total_allocated, DataRate max_padding);
  // `cc v2`: StreamsConfig::pacing_factor (GoogCcNetworkController::OnStreamsConfig).
  // Once set, GetPacingRates() uses it in place of the defaults (2.5 before the
  // first transport feedback, 1.1 after it). A video stream with transport-cc sets
  // 1.1 from the start (VideoSendStreamImpl's kStrictPacingMultiplier, issue #187).
  // Takes effect with the next update.
  void SetPacingFactor(double factor);
  // `cc v2` with `pacer v2`: the pacing and padding rates as GetPacingRates() computes them (the
  // caller takes the minimum of the padding rate and the congestion-window pushback target).
  // `pacing_rate` of the update stays target x pacing_factor, for `pacer v1`.
  // GccConfig::pacing_factor is `pacer v1`'s; SetPacingFactor() sets this one.
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
  const LossBasedBwe& loss_bwe() const { return loss_; }
  const SendSideBandwidthEstimation& send_side_bwe() const { return ssbe_; }
  const SendHistory& history() const { return history_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  NetworkControlUpdate MakeUpdate(Timestamp now, std::vector<ProbeCluster> probes);
  void TraceDelayBased(Timestamp now, DataRate delay_est, bool updated);
  void TraceLossBased(Timestamp now);
  std::vector<ProbeCluster> RegisterProbes(std::vector<ProbeCluster> probes);
  NetworkControlUpdate OnTransportFeedbackV2(const std::vector<PacketResult>& results, Timestamp now);
  GccConfig cfg_;
  DataRate configured_min_;
  SendHistory history_;
  DelayBasedBwe delay_;
  LossBasedBwe loss_;
  SendSideBandwidthEstimation ssbe_;  // `cc v2`.
  AcknowledgedBitrateEstimator acked_;
  // `cc v2`: AcknowledgedBitrateEstimatorInterface::Create returns the RobustThroughputEstimator by default (M157).
  RobustThroughputEstimator acked_v2_;
  ProbeController probe_ctrl_;
  ProbeControllerV2 probe2_;
  AlrDetector alr_;
  ProbeBitrateEstimator probe_est_;
  bool first_feedback_received_ = false;
  bool previously_in_alr_ = false;
  bool last_is_bandwidth_limited_ = true;
  bool first_packet_sent_ = false;
  DataRate min_total_allocated_;
  DataRate max_padding_;
  std::optional<double> streams_pacing_factor_;  // StreamsConfig::pacing_factor.
  Timestamp last_now_ = Timestamp::Micros(0);
  int64_t trace_sent_bucket_ = -1;  // kSent rows (#152): the open 100 ms bucket, its bytes and packets.
  int64_t trace_sent_bytes_ = 0;
  int trace_sent_packets_ = 0;
  std::optional<DataRate> remb_;
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
