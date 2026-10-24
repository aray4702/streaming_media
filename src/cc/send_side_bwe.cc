#include "sm/cc/send_side_bwe.h"

#include <algorithm>

namespace sm::cc {

namespace {
DelayBasedBwe::Config DelayCfg(const GccConfig& g) {
  DelayBasedBwe::Config c;
  c.min_rate = g.min_rate;
  c.events = g.events;
  c.trace_detector = g.bwe_trace != nullptr;
  return c;
}
ProbeControllerV2Config ProbeCfg2(const GccConfig& g) {
  ProbeControllerV2Config c;
  c.events = g.events;
  return c;
}
// GoogCcNetworkController GetBandwidthLimitedCause.
BandwidthLimitedCause LimitedCause(LossBasedState loss_based_state, bool is_rtt_above_limit, BandwidthUsage usage) {
  if (usage == BandwidthUsage::kOverusing || usage == BandwidthUsage::kUnderusing) {
    return BandwidthLimitedCause::kDelayBasedLimitedDelayIncreased;
  } else if (is_rtt_above_limit) {
    return BandwidthLimitedCause::kRttBasedBackOffHighRtt;
  }
  switch (loss_based_state) {
    case LossBasedState::kDecreasing:
    case LossBasedState::kIncreaseUsingPadding:
      // Probes may not be sent in these states.
      return BandwidthLimitedCause::kLossLimitedBwe;
    case LossBasedState::kIncreasing:
      // Probes may be sent in this state.
      return BandwidthLimitedCause::kLossLimitedBweIncreasing;
    case LossBasedState::kDelayBasedEstimate:
      break;
  }
  return BandwidthLimitedCause::kDelayBasedLimited;
}
// GoogCcNetworkController::limit_probes_lower_than_throughput_estimate_: limit
// the backoff of a probe result to something slightly below the acked rate.
constexpr double kProbeDropThroughputFraction = 0.85;
// Pacing rate relative to the target: kDefaultPaceMultiplier until the first
// transport feedback, kDefaultPaceMultiplierWithSendSideBwe after it
// (goog_cc_network_control.cc).
constexpr double kDefaultPaceMultiplier = 2.5;
constexpr double kDefaultPaceMultiplierWithSendSideBwe = 1.1;
}  // namespace

GccController::GccController(GccConfig cfg)
    : cfg_(cfg),
      configured_min_(cfg.min_rate),
      delay_(DelayCfg(cfg)),
      ssbe_(cfg.events),
      probe2_(ProbeCfg2(cfg)),
      target_(cfg.start_rate) {
  probe2_.EnablePeriodicAlrProbing(cfg.periodic_alr_probing);
}

std::vector<ProbeCluster> GccController::RegisterProbes(std::vector<ProbeCluster> probes) {
  for (const auto& p : probes) {
    probe_est_.SetClusterInfo(p);
    if (cfg_.bwe_trace) {
      BweTraceRow r;
      r.time_us = p.at_time.us();
      r.kind = BweTraceRow::Kind::kProbe;
      r.bitrate_bps = p.rate.bps();
      r.packets = p.id;
      cfg_.bwe_trace->push_back(r);
    }
  }
  return probes;
}

NetworkControlUpdate GccController::Start(Timestamp now) {
  delay_.SetStartBitrate(cfg_.start_rate);
  last_now_ = now;
  std::vector<ProbeCluster> probes;
  ssbe_.SetBitrates(cfg_.start_rate, cfg_.min_rate, cfg_.max_rate, now);
  target_ = ssbe_.target_rate();
  started_ = true;
  if (cfg_.enable_probing) {
    // OnNetworkAvailability(true), then the first ResetConstraints.
    probes = probe2_.OnNetworkAvailability(true, now);
    auto more = probe2_.SetBitrates(cfg_.min_rate, cfg_.start_rate, cfg_.max_rate, now);
    probes.insert(probes.end(), more.begin(), more.end());
    // The streams' bitrates arrive after the initial probes (issue #62). libwebrtc's
    // RtpTransportControllerSend::MaybeCreateControllers creates GoogCC when the network
    // comes up (rtp_transport_controller_send.cc:413-428, 747-773), and the call signals
    // that before the streams start (sm_compare's webrtc_call.cc; WebRtcVoiceSendChannel::
    // OnReadyToSend). AudioSendStream::Start then registers with the BitrateAllocator, and
    // OnStreamsConfig hands max_total_allocated_bitrate to a ProbeController that is already
    // waiting for its probe results, so the 3x and 6x start-rate probes are not capped at
    // twice the allocation (probe_controller.cc:224-268, 567-576).
    if (pending_max_total_allocated_) {
      auto alloc = probe2_.OnMaxTotalAllocatedBitrate(*pending_max_total_allocated_, now);
      probes.insert(probes.end(), alloc.begin(), alloc.end());
    }
  }
  pending_max_total_allocated_.reset();
  return MakeUpdate(now, RegisterProbes(std::move(probes)));
}

std::vector<ProbeCluster> GccController::OnMaxTotalAllocatedBitrate(DataRate max_total_allocated_bitrate, Timestamp now) {
  if (!cfg_.enable_probing) return {};
  if (!started_) {
    pending_max_total_allocated_ = max_total_allocated_bitrate;
    return {};
  }
  return RegisterProbes(probe2_.OnMaxTotalAllocatedBitrate(max_total_allocated_bitrate, now));
}

void GccController::EnablePeriodicAlrProbing(bool enable) {
  cfg_.periodic_alr_probing = enable;
  probe2_.EnablePeriodicAlrProbing(enable);
}

void GccController::SetMinAllocatable(DataRate r) {
  cfg_.min_rate = std::max(configured_min_, r);
  if (cfg_.max_rate < cfg_.min_rate) cfg_.max_rate = cfg_.min_rate;
  delay_.SetMinRate(cfg_.min_rate);
  ssbe_.SetMinMaxBitrate(cfg_.min_rate, cfg_.max_rate);
  ssbe_.UpdateDelayBasedEstimate(last_now_, ssbe_.delay_based_limit().IsFinite() ? ssbe_.delay_based_limit() : DataRate::Zero());
  target_ = ssbe_.target_rate();
  target_ = std::max(target_, cfg_.min_rate);
  cfg_.start_rate = std::max(cfg_.start_rate, cfg_.min_rate);
}

void GccController::SetPacingLimits(DataRate min_total_allocated, DataRate max_padding) {
  min_total_allocated_ = min_total_allocated;
  max_padding_ = max_padding;
}

void GccController::SetPacingFactor(double factor) { streams_pacing_factor_ = factor; }

// GoogCcNetworkController::GetPacingRates. Based on the target before the
// congestion-window pushback, so the pacer builds no queue when pushback occurs.
// The streams' pacing factor, when set, replaces the defaults.
DataRate GccController::PacingRate() const {
  return std::max(min_total_allocated_, target_) *
         streams_pacing_factor_.value_or(first_feedback_received_ ? kDefaultPaceMultiplierWithSendSideBwe : kDefaultPaceMultiplier);
}

DataRate GccController::PaddingRate() const {
  return ssbe_.loss_based_state() == LossBasedState::kIncreaseUsingPadding ? std::max(max_padding_, target_) : max_padding_;
}

void GccController::OnPacketSent(uint16_t seq, Timestamp t, DataSize size, int probe_cluster) {
  history_.OnPacketSent(seq, t, size, probe_cluster);
  // GoogCcNetworkController::OnSentPacket.
  const bool was_in_alr = alr_.GetApplicationLimitedRegionStartTime().has_value();
  alr_.OnBytesSent(size, t);
  const bool in_alr = alr_.GetApplicationLimitedRegionStartTime().has_value();
  if (cfg_.bwe_trace && in_alr != was_in_alr) {
    BweTraceRow r;
    r.time_us = t.us();
    r.kind = BweTraceRow::Kind::kAlr;
    r.bitrate_bps = in_alr ? 1 : 0;
    cfg_.bwe_trace->push_back(r);
  }
  acked_v2_.SetAlr(alr_.GetApplicationLimitedRegionStartTime().has_value());
  if (!first_packet_sent_) {
    first_packet_sent_ = true;
    // Initialise the feedback time to the send time to allow an RTT estimate until the first feedback.
    ssbe_.UpdatePropagationRtt(t, TimeDelta::Zero());
  }
  ssbe_.OnSentPacket(t);
}

void GccController::OnRttUpdate(TimeDelta rtt) {
  // RtpTransportControllerSend::OnRttUpdate: `rtt.RoundTo(1 ms)`, and a zero RTT is not passed on.
  rtt = TimeDelta::Millis((rtt.us() + 500) / 1000);
  if (rtt.IsZero()) return;
  last_rtt_ = rtt;
  delay_.SetRtt(rtt);
  ssbe_.UpdateRtt(rtt, last_now_);
}

void GccController::OnRemb(DataRate remb) {
  ssbe_.UpdateReceiverEstimate(last_now_, remb);
  target_ = ssbe_.target_rate();
}

// GoogCcNetworkController::OnTransportLossReport.
NetworkControlUpdate GccController::OnTransportLossReport(int64_t packets_lost_delta, int64_t packets_delta, Timestamp now) {
  last_now_ = now;
  ssbe_.UpdatePacketsLost(packets_lost_delta, packets_delta, now);
  target_ = ssbe_.target_rate();
  TraceLossBased(now);
  return MakeUpdate(now, {});
}

NetworkControlUpdate GccController::OnTransportFeedback(const TransportFeedback& fb, Timestamp now) {
  last_now_ = now;
  std::vector<PacketResult> results = history_.OnFeedback(fb, now);
  last_feedback_max_rtt_.reset();
  for (const auto& r : results) {
    if (r.received()) last_feedback_max_rtt_ = std::max(last_feedback_max_rtt_.value_or(TimeDelta::Zero()), now - r.send_time);
  }
  if (results.empty()) return MakeUpdate(now, {});
  // GoogCcNetworkController::OnTransportPacketsFeedback.
  first_feedback_received_ = true;
  {
    // min propagation RTT: the feedback RTT less the time the packet waited for later arrivals in the report.
    Timestamp max_recv_time = Timestamp::MinusInfinity();
    for (const auto& r : results) {
      if (r.received()) max_recv_time = std::max(max_recv_time, r.receive_time);
    }
    TimeDelta min_propagation_rtt = TimeDelta::PlusInfinity();
    bool any = false;
    for (const auto& r : results) {
      if (!r.received()) continue;
      any = true;
      min_propagation_rtt = std::min(min_propagation_rtt, (now - r.send_time) - (max_recv_time - r.receive_time));
    }
    if (any) ssbe_.UpdatePropagationRtt(now, min_propagation_rtt);
  }
  const std::optional<Timestamp> alr_start_time = alr_.GetApplicationLimitedRegionStartTime();
  if (previously_in_alr_ && !alr_start_time) {
    acked_v2_.SetAlrEndedTime(now);
    probe2_.SetAlrEndedTime(now);
  }
  previously_in_alr_ = alr_start_time.has_value();
  acked_v2_.OnPacketResults(SortedByReceiveTime(results));
  std::optional<DataRate> acked = acked_v2_.bitrate();
  ssbe_.SetAcknowledgedRate(acked, now);
  for (const auto& r : SortedByReceiveTime(results)) {
    if (r.probe_cluster >= 0) probe_est_.OnPacketResult(r);
  }
  std::optional<DataRate> probe = probe_est_.FetchAndResetLastEstimate();
  if (probe && acked) {
    // Limit the backoff to something slightly below the acknowledged rate (to
    // drain the queues if we are actually overusing); never above the estimate.
    const DataRate limit = std::min(delay_.estimate(), *acked * kProbeDropThroughputFraction);
    probe = std::max(*probe, limit);
  }
  DataRate delay_est = delay_.OnPacketResults(results, acked, probe, now);
  const DelayBasedBwe::Result result = delay_.last_result();
  TraceDetector(now);
  TraceDelayBased(now, delay_est, result.updated);
  if (result.updated) {
    if (result.probe) ssbe_.SetSendBitrate(delay_est, now);
    // SetSendBitrate resets the delay-based limit, so this comes after it.
    ssbe_.UpdateDelayBasedEstimate(now, delay_est);
  }
  ssbe_.UpdateLossBasedEstimator(results, alr_start_time.has_value(), now);
  target_ = ssbe_.target_rate();
  TraceLossBased(now);
  std::vector<ProbeCluster> probes;
  NetworkControlUpdate u = MakeUpdate(now, {});
  probes = std::move(u.probes);
  if (cfg_.enable_probing && result.recovered_from_overuse) {
    probe2_.SetAlrStartTime(alr_start_time);
    auto more = probe2_.RequestProbe(now);
    probes.insert(probes.end(), more.begin(), more.end());
  }
  u.probes = RegisterProbes(std::move(probes));
  return u;
}

NetworkControlUpdate GccController::OnProcessInterval(Timestamp now, DataRate /*current_send_rate*/) {
  last_now_ = now;
  std::vector<ProbeCluster> probes;
  ssbe_.UpdateEstimate(now);
  target_ = ssbe_.target_rate();
  TraceLossBased(now);
  // Avoid ALR probing before the first transport feedback is received.
  probe2_.SetAlrStartTime(first_feedback_received_ ? alr_.GetApplicationLimitedRegionStartTime() : std::nullopt);
  if (cfg_.enable_probing) probes = probe2_.Process(now);
  return MakeUpdate(now, RegisterProbes(std::move(probes)));
}

NetworkControlUpdate GccController::ResetEstimate(DataRate rate, Timestamp now) {
  delay_.SetStartBitrate(rate);
  target_ = Clamp(rate, cfg_.min_rate, cfg_.max_rate);
  last_now_ = now;
  std::vector<ProbeCluster> probes;
  // OnNetworkRouteChange: the send-side estimation starts over.
  ssbe_.OnRouteChange();
  ssbe_.SetBitrates(target_, cfg_.min_rate, cfg_.max_rate, now);
  target_ = ssbe_.target_rate();
  // RouteChange: a new probe estimator, and the probe controller starts over.
  probe_est_ = ProbeBitrateEstimator();
  probe2_.Reset(now);
  if (cfg_.enable_probing) probes = probe2_.SetBitrates(cfg_.min_rate, target_, cfg_.max_rate, now);
  SM_EVENT(cfg_.events, now, "gcc", "reset", rate.bps(), 0);
  return MakeUpdate(now, RegisterProbes(std::move(probes)));
}

// The trendline updates of this feedback, one row per send-time group (issue #152).
void GccController::TraceDetector(Timestamp now) {
  if (!cfg_.bwe_trace) return;
  for (const DetectorSample& x : delay_.TakeDetectorTrace()) {
    BweTraceRow r;
    r.time_us = now.us();
    r.kind = BweTraceRow::Kind::kDetector;
    r.detector = static_cast<int>(x.state);
    r.packets = static_cast<int>(x.num_deltas);
    r.modified_trend = x.modified_trend;
    r.threshold = x.threshold;
    r.send_delta_ms = x.send_delta_ms;
    r.recv_delta_ms = x.recv_delta_ms;
    r.send_ms = x.send_ms;
    r.arrival_ms = x.arrival_ms;
    cfg_.bwe_trace->push_back(r);
  }
}

// DelayBasedBwe::MaybeUpdateEstimate's RtcEventBweUpdateDelayBased: logged when the
// estimate changed or the detector state did.
void GccController::TraceDelayBased(Timestamp now, DataRate delay_est, bool updated) {
  if (!cfg_.bwe_trace) return;
  const BandwidthUsage state = delay_.usage();
  if ((updated && trace_delay_bitrate_ != delay_est) || state != trace_delay_state_) {
    BweTraceRow r;
    r.time_us = now.us();
    r.kind = BweTraceRow::Kind::kDelayBased;
    r.bitrate_bps = (updated ? delay_est : trace_delay_bitrate_).bps();
    r.detector = static_cast<int>(state);
    cfg_.bwe_trace->push_back(r);
    trace_delay_bitrate_ = updated ? delay_est : trace_delay_bitrate_;
    trace_delay_state_ = state;
  }
}

// SendSideBandwidthEstimation::MaybeLogLossBasedEvent's RtcEventBweUpdateLossBased: the
// send-side target, logged when it or the loss fraction changed, or every 5 s.
void GccController::TraceLossBased(Timestamp now) {
  if (!cfg_.bwe_trace) return;
  const DataRate target = ssbe_.target_rate();
  const int fraction = ssbe_.fraction_loss();
  if (target.bps() != trace_loss_target_bps_ || fraction != trace_loss_fraction_ ||
      now - trace_loss_time_ > TimeDelta::Millis(5000)) {
    BweTraceRow r;
    r.time_us = now.us();
    r.kind = BweTraceRow::Kind::kLossBased;
    r.bitrate_bps = target.bps();
    r.fraction_loss = fraction;
    r.packets = static_cast<int>(ssbe_.expected_packets_since_last_loss_update());
    const LossBasedBweV2& lb = ssbe_.loss_based();
    r.loss_state = static_cast<int>(ssbe_.loss_based_state());
    r.loss_limit_bps = lb.loss_limited_bandwidth() ? lb.loss_limited_bandwidth()->bps() : -1;
    r.instant_ub_bps = lb.instant_upper_bound().bps();
    cfg_.bwe_trace->push_back(r);
    trace_loss_target_bps_ = target.bps();
    trace_loss_fraction_ = fraction;
    trace_loss_time_ = now;
  }
}

NetworkControlUpdate GccController::MakeUpdate(Timestamp now, std::vector<ProbeCluster> probes) {
  NetworkControlUpdate u;
  u.target_rate = target_;
  if (cfg_.bwe_trace && target_.bps() != trace_target_bps_) {
    BweTraceRow r;
    r.time_us = now.us();
    r.kind = BweTraceRow::Kind::kTarget;
    r.bitrate_bps = target_.bps();
    cfg_.bwe_trace->push_back(r);
    trace_target_bps_ = target_.bps();
  }
  u.pacing_rate = PacingRate();
  u.probes = std::move(probes);
  u.changed = target_ != last_reported_;
  // MaybeTriggerOnNetworkChanged: when the target, the loss-based state, the
  // loss fraction, the RTT or the bandwidth-limited state changes, the ALR
  // detector gets the loss-based target and the probe controller the new
  // estimate and limiting cause (which may start the next exponential probe).
  // SendSideController applies the congestion-window pushback after this, and
  // the detector takes the target before it, as libwebrtc's does.
  const bool is_bandwidth_limited = !alr_.GetApplicationLimitedRegionStartTime().has_value();
  const LossBasedState loss_based_state = ssbe_.loss_based_state();
  const uint8_t fraction_loss = ssbe_.fraction_loss();
  const TimeDelta round_trip_time = ssbe_.round_trip_time();
  if (u.changed || loss_based_state != last_loss_based_state_ || fraction_loss != last_fraction_loss_ ||
      round_trip_time != last_round_trip_time_ || is_bandwidth_limited != last_is_bandwidth_limited_) {
    last_is_bandwidth_limited_ = is_bandwidth_limited;
    last_loss_based_state_ = loss_based_state;
    last_fraction_loss_ = fraction_loss;
    last_round_trip_time_ = round_trip_time;
    alr_.SetEstimatedBitrate(target_);
    if (cfg_.enable_probing) {
      auto more = probe2_.SetEstimatedBitrate(target_, LimitedCause(ssbe_.loss_based_state(), ssbe_.IsRttAboveLimit(), delay_.usage()), now);
      u.probes.insert(u.probes.end(), more.begin(), more.end());
    }
  }
  if (u.changed) {
    target_kbps_.Add(target_.kbps());
    SM_EVENT(cfg_.events, now, "gcc", "target", target_.bps(), last_reported_.bps());
  }
  last_reported_ = target_;
  return u;
}

void GccController::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("target_kbps", target_.kbps());
  w.Gauge("delay_estimate_kbps", delay_.estimate().kbps());
  if (auto limited = ssbe_.loss_based().loss_limited_bandwidth()) w.Gauge("loss_limit_kbps", limited->kbps());
  w.Gauge("loss", ssbe_.loss_based().average_reported_loss_ratio());
  ssbe_.ExportMetrics(w);
  w.Gauge("in_flight_bytes", static_cast<double>(history_.InFlight().bytes()));
  w.Gauge("alr", alr_.GetApplicationLimitedRegionStartTime().has_value() ? 1 : 0);
  auto acked = acked_v2_.bitrate();
  if (acked) w.Gauge("acked_kbps", acked->kbps());
  w.Hist("target_changes_kbps", target_kbps_);
  delay_.ExportMetrics(w);
}

}  // namespace sm::cc
