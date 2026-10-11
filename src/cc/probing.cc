#include "sm/cc/probing.h"

#include <algorithm>

namespace sm::cc {

std::vector<ProbeCluster> ProbeController::Initiate(std::vector<DataRate> rates, Timestamp now, bool further) {
  std::vector<ProbeCluster> out;
  for (DataRate r : rates) {
    r = std::min(r, max_);
    if (r <= estimate_) continue;
    ProbeCluster c;
    c.id = next_id_++;
    c.rate = r;
    c.min_probes = 5;
    c.min_bytes = r * cfg_.probe_duration;
    out.push_back(c);
    ++clusters_;
    SM_EVENT(cfg_.events, now, "probe", "cluster", r.bps(), c.id);
  }
  if (!out.empty()) last_probe_ = now;
  // Keep probing upward while probes keep succeeding.
  min_rate_to_continue_ = (further && !out.empty()) ? out.back().rate * cfg_.further_probe_threshold
                                                    : DataRate::PlusInfinity();
  return out;
}

std::vector<ProbeCluster> ProbeController::SetBitrates(DataRate min, DataRate start, DataRate max, Timestamp now) {
  (void)min;
  max_ = max;
  estimate_ = start;
  return Initiate({start * cfg_.first_multiplier, start * cfg_.second_multiplier}, now, true);
}

std::vector<ProbeCluster> ProbeController::SetEstimate(DataRate estimate, Timestamp now) {
  estimate_ = estimate;
  if (estimate >= min_rate_to_continue_ && estimate < max_) {
    return Initiate({estimate * cfg_.further_multiplier}, now, true);
  }
  return {};
}

std::vector<ProbeCluster> ProbeController::SetMaxBitrate(DataRate max, Timestamp now) {
  DataRate old = max_;
  max_ = max;
  if (max > old && estimate_ >= old * 0.9) return Initiate({max}, now, false);
  return {};
}

void ProbeController::SetApplicationLimited(bool alr, Timestamp now) {
  if (alr && !alr_) alr_start_ = now;
  alr_ = alr;
}

std::vector<ProbeCluster> ProbeController::Process(Timestamp now) {
  if (periodic_alr_ && alr_ && now - std::max(alr_start_, last_probe_) >= cfg_.alr_probe_interval) {
    return Initiate({estimate_ * 2.0}, now, true);
  }
  return {};
}

void ProbeController::ExportMetrics(MetricsWriter& w) const {
  w.Counter("clusters", clusters_);
  w.Gauge("alr", alr_ ? 1 : 0);
}

std::optional<DataRate> ProbeBitrateEstimator::OnPacketResult(const PacketResult& r) {
  if (r.probe_cluster < 0) return std::nullopt;
  auto it = clusters_.find(r.probe_cluster);
  if (it == clusters_.end()) return std::nullopt;
  Cluster& c = it->second;
  if (c.done) return std::nullopt;
  c.count += 1;
  int64_t sz = r.size.bytes();
  if (r.send_time < c.first_send) c.first_send = r.send_time;
  if (r.send_time > c.last_send) {
    c.last_send = r.send_time;
    c.size_last_send = sz;
  }
  if (r.received()) {
    if (r.receive_time < c.first_recv) {
      c.first_recv = r.receive_time;
      c.size_first_recv = sz;
    }
    c.last_recv = std::max(c.last_recv, r.receive_time);
    c.bytes += sz;
  }
  const int min_packets = std::max(2, c.config.min_probes * 8 / 10);
  if (c.count < min_packets || c.bytes < c.config.min_bytes.bytes() * 8 / 10) return std::nullopt;
  c.done = true;
  TimeDelta send_iv = c.last_send - c.first_send;
  TimeDelta recv_iv = c.last_recv - c.first_recv;
  if (send_iv <= TimeDelta::Zero() || send_iv > TimeDelta::Seconds(1) || recv_iv <= TimeDelta::Zero() ||
      recv_iv > TimeDelta::Seconds(1)) {
    ++invalid_;
    return std::nullopt;
  }
  DataRate send_rate = DataRate::FromSizeAndTime(DataSize::Bytes(c.bytes - c.size_last_send), send_iv);
  DataRate recv_rate = DataRate::FromSizeAndTime(DataSize::Bytes(c.bytes - c.size_first_recv), recv_iv);
  DataRate est = std::min(send_rate, recv_rate);
  if (recv_rate < send_rate * 0.9) est = recv_rate * 0.95;  // Link saturated.
  last_ = est;
  ++estimates_;
  return est;
}

std::optional<DataRate> ProbeBitrateEstimator::FetchAndResetLastEstimate() {
  auto r = last_;
  last_.reset();
  return r;
}

void ProbeBitrateEstimator::ExportMetrics(MetricsWriter& w) const {
  w.Counter("estimates", estimates_);
  w.Counter("invalid", invalid_);
}

}  // namespace sm::cc
