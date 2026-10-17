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
}  // namespace

GccController::GccController(GccConfig cfg)
    : cfg_(cfg), configured_min_(cfg.min_rate), delay_(DelayCfg(cfg)), loss_(LossCfg(cfg)), probe_ctrl_(ProbeCfg(cfg)), target_(cfg.start_rate) {}

NetworkControlUpdate GccController::Start(Timestamp now) {
  delay_.SetStartBitrate(cfg_.start_rate, now);
  loss_.Reset();
  target_ = cfg_.start_rate;
  std::vector<ProbeCluster> probes;
  if (cfg_.enable_probing) probes = probe_ctrl_.SetBitrates(cfg_.min_rate, cfg_.start_rate, cfg_.max_rate, now);
  for (const auto& p : probes) probe_est_.SetClusterInfo(p);
  return MakeUpdate(now, std::move(probes));
}

void GccController::SetMinAllocatable(DataRate r) {
  cfg_.min_rate = std::max(configured_min_, r);
  if (cfg_.max_rate < cfg_.min_rate) cfg_.max_rate = cfg_.min_rate;
  delay_.SetMinRate(cfg_.min_rate);
  loss_.SetMinRate(cfg_.min_rate);
  target_ = std::max(target_, cfg_.min_rate);
  cfg_.start_rate = std::max(cfg_.start_rate, cfg_.min_rate);
}

void GccController::OnPacketSent(uint16_t seq, Timestamp t, DataSize size, int probe_cluster) {
  history_.OnPacketSent(seq, t, size, probe_cluster);
}

void GccController::OnRttUpdate(TimeDelta rtt) {
  delay_.SetRtt(rtt);
  loss_.SetRtt(rtt);
}

NetworkControlUpdate GccController::OnTransportFeedback(const TransportFeedback& fb, Timestamp now) {
  std::vector<PacketResult> results = history_.OnFeedback(fb, now);
  last_feedback_max_rtt_.reset();
  for (const auto& r : results) {
    if (r.received()) last_feedback_max_rtt_ = std::max(last_feedback_max_rtt_.value_or(TimeDelta::Zero()), now - r.send_time);
  }
  if (results.empty()) return MakeUpdate(now, {});
  acked_.OnPacketResults(results);
  for (const auto& r : results) probe_est_.OnPacketResult(r);
  std::optional<DataRate> probe = probe_est_.FetchAndResetLastEstimate();
  DataRate delay_est = delay_.OnPacketResults(results, acked_.bitrate(), probe, now);
  DataRate loss_limit = loss_.OnPacketResults(results, target_, now);
  target_ = std::min(delay_est, loss_limit);
  if (remb_) target_ = std::min(target_, *remb_);
  target_ = Clamp(target_, cfg_.min_rate, cfg_.max_rate);
  std::vector<ProbeCluster> probes;
  if (cfg_.enable_probing) probes = probe_ctrl_.SetEstimate(target_, now);
  for (const auto& p : probes) probe_est_.SetClusterInfo(p);
  return MakeUpdate(now, std::move(probes));
}

NetworkControlUpdate GccController::OnProcessInterval(Timestamp now, DataRate send_rate) {
  // Application-limited when sending well below the estimate.
  probe_ctrl_.SetApplicationLimited(send_rate < target_ * 0.65, now);
  std::vector<ProbeCluster> probes;
  if (cfg_.enable_probing) probes = probe_ctrl_.Process(now);
  for (const auto& p : probes) probe_est_.SetClusterInfo(p);
  return MakeUpdate(now, std::move(probes));
}

NetworkControlUpdate GccController::ResetEstimate(DataRate rate, Timestamp now) {
  delay_.SetStartBitrate(rate, now);
  loss_.Reset();
  target_ = Clamp(rate, cfg_.min_rate, cfg_.max_rate);
  std::vector<ProbeCluster> probes;
  if (cfg_.enable_probing) probes = probe_ctrl_.SetBitrates(cfg_.min_rate, target_, cfg_.max_rate, now);
  for (const auto& p : probes) probe_est_.SetClusterInfo(p);
  SM_EVENT(cfg_.events, now, "gcc", "reset", rate.bps(), 0);
  return MakeUpdate(now, std::move(probes));
}

NetworkControlUpdate GccController::MakeUpdate(Timestamp now, std::vector<ProbeCluster> probes) {
  NetworkControlUpdate u;
  u.target_rate = target_;
  u.pacing_rate = target_ * cfg_.pacing_factor;
  u.probes = std::move(probes);
  u.changed = target_ != last_reported_;
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
  w.Gauge("loss_limit_kbps", loss_.estimate().kbps());
  w.Gauge("loss", loss_.last_loss());
  w.Gauge("in_flight_bytes", static_cast<double>(history_.InFlight().bytes()));
  if (acked_.bitrate()) w.Gauge("acked_kbps", acked_.bitrate()->kbps());
  w.Hist("target_changes_kbps", target_kbps_);
  delay_.ExportMetrics(w);
}

}  // namespace sm::cc
