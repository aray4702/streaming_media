#include "sm/cc/send_side_bwe.h"

#include <algorithm>

namespace sm::cc {

namespace {
DelayBasedBwe::Config DelayCfg(const GccConfig& g) {
  DelayBasedBwe::Config c;
  c.aimd.min_rate = g.min_rate;
  c.aimd.max_rate = g.max_rate;
  c.events = g.events;
  c.version = g.version;
  return c;
}
LossBweConfig LossCfg(const GccConfig& g) {
  LossBweConfig c;
  c.min_rate = g.min_rate;
  c.max_rate = g.max_rate;
  c.events = g.events;
  return c;
}
ProbeControllerConfig ProbeCfg(const GccConfig& g) {
  ProbeControllerConfig c;
  c.events = g.events;
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
}  // namespace

GccController::GccController(GccConfig cfg)
    : cfg_(cfg),
      configured_min_(cfg.min_rate),
      delay_(DelayCfg(cfg)),
      loss_(LossCfg(cfg)),
      ssbe_(cfg.events),
      probe_ctrl_(ProbeCfg(cfg)),
      probe2_(ProbeCfg2(cfg)),
      probe_est_(cfg.version >= 2),
      target_(cfg.start_rate) {
  probe2_.EnablePeriodicAlrProbing(cfg.periodic_alr_probing);
}

std::vector<ProbeCluster> GccController::RegisterProbes(std::vector<ProbeCluster> probes) {
  for (const auto& p : probes) probe_est_.SetClusterInfo(p);
  return probes;
}

NetworkControlUpdate GccController::Start(Timestamp now) {
  delay_.SetStartBitrate(cfg_.start_rate, now);
  loss_.Reset();
  target_ = cfg_.start_rate;
  last_now_ = now;
  std::vector<ProbeCluster> probes;
  if (cfg_.version >= 2) {
    ssbe_.SetBitrates(cfg_.start_rate, cfg_.min_rate, cfg_.max_rate, now);
    target_ = ssbe_.target_rate();
    if (cfg_.enable_probing) {
      // OnNetworkAvailability(true), then the first ResetConstraints.
      probes = probe2_.OnNetworkAvailability(true, now);
      auto more = probe2_.SetBitrates(cfg_.min_rate, cfg_.start_rate, cfg_.max_rate, now);
      probes.insert(probes.end(), more.begin(), more.end());
    }
  } else if (cfg_.enable_probing) {
    probes = probe_ctrl_.SetBitrates(cfg_.min_rate, cfg_.start_rate, cfg_.max_rate, now);
  }
  return MakeUpdate(now, RegisterProbes(std::move(probes)));
}

std::vector<ProbeCluster> GccController::OnMaxTotalAllocatedBitrate(DataRate max_total_allocated_bitrate, Timestamp now) {
  if (cfg_.version < 2 || !cfg_.enable_probing) return {};
  return RegisterProbes(probe2_.OnMaxTotalAllocatedBitrate(max_total_allocated_bitrate, now));
}

void GccController::SetMinAllocatable(DataRate r) {
  cfg_.min_rate = std::max(configured_min_, r);
  if (cfg_.max_rate < cfg_.min_rate) cfg_.max_rate = cfg_.min_rate;
  delay_.SetMinRate(cfg_.min_rate);
  loss_.SetMinRate(cfg_.min_rate);
  if (cfg_.version >= 2) {
    ssbe_.SetMinMaxBitrate(cfg_.min_rate, cfg_.max_rate);
    ssbe_.UpdateDelayBasedEstimate(last_now_, ssbe_.delay_based_limit().IsFinite() ? ssbe_.delay_based_limit() : DataRate::Zero());
    target_ = ssbe_.target_rate();
  }
  target_ = std::max(target_, cfg_.min_rate);
  cfg_.start_rate = std::max(cfg_.start_rate, cfg_.min_rate);
}

void GccController::OnPacketSent(uint16_t seq, Timestamp t, DataSize size, int probe_cluster) {
  history_.OnPacketSent(seq, t, size, probe_cluster);
  if (cfg_.version >= 2) {
    // GoogCcNetworkController::OnSentPacket.
    alr_.OnBytesSent(size, t);
    acked_v2_.SetAlr(alr_.GetApplicationLimitedRegionStartTime().has_value());
    if (!first_packet_sent_) {
      first_packet_sent_ = true;
      // Initialise the feedback time to the send time to allow an RTT estimate until the first feedback.
      ssbe_.UpdatePropagationRtt(t, TimeDelta::Zero());
    }
    ssbe_.OnSentPacket(t);
  }
}

void GccController::OnRttUpdate(TimeDelta rtt) {
  delay_.SetRtt(rtt);
  loss_.SetRtt(rtt);
  if (cfg_.version >= 2) ssbe_.UpdateRtt(rtt, last_now_);
}

void GccController::OnRemb(DataRate remb) {
  remb_ = remb;
  if (cfg_.version >= 2) {
    ssbe_.UpdateReceiverEstimate(last_now_, remb);
    target_ = ssbe_.target_rate();
  }
}

// GoogCcNetworkController::OnTransportLossReport.
NetworkControlUpdate GccController::OnTransportLossReport(int64_t packets_lost_delta, int64_t packets_delta, Timestamp now) {
  last_now_ = now;
  if (cfg_.version < 2) return MakeUpdate(now, {});
  ssbe_.UpdatePacketsLost(packets_lost_delta, packets_delta, now);
  target_ = ssbe_.target_rate();
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
  if (cfg_.version >= 2) return OnTransportFeedbackV2(results, now);
  acked_.OnPacketResults(results);
  std::optional<DataRate> acked = acked_.bitrate();
  for (const auto& r : results) probe_est_.OnPacketResult(r);
  std::optional<DataRate> probe = probe_est_.FetchAndResetLastEstimate();
  DataRate delay_est = delay_.OnPacketResults(results, acked, probe, now);
  DataRate loss_limit = loss_.OnPacketResults(results, target_, now);
  target_ = std::min(delay_est, loss_limit);
  if (remb_) target_ = std::min(target_, *remb_);
  target_ = Clamp(target_, cfg_.min_rate, cfg_.max_rate);
  std::vector<ProbeCluster> probes;
  if (cfg_.enable_probing) probes = probe_ctrl_.SetEstimate(target_, now);
  return MakeUpdate(now, RegisterProbes(std::move(probes)));
}

// GoogCcNetworkController::OnTransportPacketsFeedback.
NetworkControlUpdate GccController::OnTransportFeedbackV2(const std::vector<PacketResult>& results, Timestamp now) {
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
  acked_v2_.OnPacketResults(results);
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
  if (result.updated) {
    if (result.probe) ssbe_.SetSendBitrate(delay_est, now);
    // SetSendBitrate resets the delay-based limit, so this comes after it.
    ssbe_.UpdateDelayBasedEstimate(now, delay_est);
  }
  ssbe_.UpdateLossBasedEstimator(results, alr_start_time.has_value(), now);
  target_ = ssbe_.target_rate();
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

NetworkControlUpdate GccController::OnProcessInterval(Timestamp now, DataRate send_rate) {
  last_now_ = now;
  std::vector<ProbeCluster> probes;
  if (cfg_.version >= 2) {
    ssbe_.UpdateEstimate(now);
    target_ = ssbe_.target_rate();
    // Avoid ALR probing before the first transport feedback is received.
    probe2_.SetAlrStartTime(first_feedback_received_ ? alr_.GetApplicationLimitedRegionStartTime() : std::nullopt);
    if (cfg_.enable_probing) probes = probe2_.Process(now);
  } else {
    // Application-limited when sending well below the estimate.
    probe_ctrl_.SetApplicationLimited(send_rate < target_ * 0.65, now);
    if (cfg_.enable_probing) probes = probe_ctrl_.Process(now);
  }
  return MakeUpdate(now, RegisterProbes(std::move(probes)));
}

NetworkControlUpdate GccController::ResetEstimate(DataRate rate, Timestamp now) {
  delay_.SetStartBitrate(rate, now);
  loss_.Reset();
  target_ = Clamp(rate, cfg_.min_rate, cfg_.max_rate);
  last_now_ = now;
  std::vector<ProbeCluster> probes;
  if (cfg_.version >= 2) {
    // OnNetworkRouteChange: the send-side estimation starts over.
    ssbe_.OnRouteChange();
    ssbe_.SetBitrates(target_, cfg_.min_rate, cfg_.max_rate, now);
    target_ = ssbe_.target_rate();
    // RouteChange: a new probe estimator, and the probe controller starts over.
    probe_est_ = ProbeBitrateEstimator(true);
    probe2_.Reset(now);
    if (cfg_.enable_probing) probes = probe2_.SetBitrates(cfg_.min_rate, target_, cfg_.max_rate, now);
  } else if (cfg_.enable_probing) {
    probes = probe_ctrl_.SetBitrates(cfg_.min_rate, target_, cfg_.max_rate, now);
  }
  SM_EVENT(cfg_.events, now, "gcc", "reset", rate.bps(), 0);
  return MakeUpdate(now, RegisterProbes(std::move(probes)));
}

NetworkControlUpdate GccController::MakeUpdate(Timestamp now, std::vector<ProbeCluster> probes) {
  NetworkControlUpdate u;
  u.target_rate = target_;
  u.pacing_rate = target_ * cfg_.pacing_factor;
  u.probes = std::move(probes);
  u.changed = target_ != last_reported_;
  if (cfg_.version >= 2) {
    // MaybeTriggerOnNetworkChanged: when the target or the bandwidth-limited
    // state changes, the ALR detector gets the new rate and the probe controller
    // the new estimate (which may start the next exponential probe).
    const bool is_bandwidth_limited = !alr_.GetApplicationLimitedRegionStartTime().has_value();
    if (u.changed || is_bandwidth_limited != last_is_bandwidth_limited_) {
      last_is_bandwidth_limited_ = is_bandwidth_limited;
      alr_.SetEstimatedBitrate(target_);
      if (cfg_.enable_probing) {
        auto more = probe2_.SetEstimatedBitrate(target_, LimitedCause(ssbe_.loss_based_state(), ssbe_.IsRttAboveLimit(), delay_.usage()), now);
        u.probes.insert(u.probes.end(), more.begin(), more.end());
      }
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
  if (cfg_.version >= 2) {
    if (auto limited = ssbe_.loss_based().loss_limited_bandwidth()) w.Gauge("loss_limit_kbps", limited->kbps());
    w.Gauge("loss", ssbe_.loss_based().average_reported_loss_ratio());
    ssbe_.ExportMetrics(w);
  } else {
    w.Gauge("loss_limit_kbps", loss_.estimate().kbps());
    w.Gauge("loss", loss_.last_loss());
  }
  w.Gauge("in_flight_bytes", static_cast<double>(history_.InFlight().bytes()));
  if (cfg_.version >= 2) w.Gauge("alr", alr_.GetApplicationLimitedRegionStartTime().has_value() ? 1 : 0);
  auto acked = cfg_.version >= 2 ? acked_v2_.bitrate() : acked_.bitrate();
  if (acked) w.Gauge("acked_kbps", acked->kbps());
  w.Hist("target_changes_kbps", target_kbps_);
  delay_.ExportMetrics(w);
}

}  // namespace sm::cc
