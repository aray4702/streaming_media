#include "sm/cc/bitrate_prober.h"

#include <algorithm>
#include <cmath>

namespace sm::cc {

namespace {
constexpr TimeDelta kProbeClusterTimeout = TimeDelta::Seconds(5);
constexpr size_t kMaxPendingProbeClusters = 5;

// libwebrtc's unit arithmetic (api/units/data_rate.h): DataRate * TimeDelta rounds to nearest,
// DataSize / DataRate truncates (#166).
DataSize Mul(DataRate r, TimeDelta t) {
  return DataSize::Bytes(static_cast<int64_t>(std::llround(static_cast<double>(r.bps()) * static_cast<double>(t.us()) / 8e6)));
}
TimeDelta Div(DataSize s, DataRate r) {
  return TimeDelta::Micros(s.bytes() * 8000000 / r.bps());
}
}  // namespace

BitrateProber::BitrateProber(BitrateProberConfig cfg) : cfg_(cfg) {}

void BitrateProber::MaybeSetActiveState(DataSize packet_size) {
  if (ReadyToSetActiveState(packet_size)) {
    next_probe_time_ = Timestamp::MinusInfinity();
    probing_state_ = State::kActive;
  }
}

bool BitrateProber::ReadyToSetActiveState(DataSize packet_size) const {
  if (clusters_.empty()) return false;
  switch (probing_state_) {
    case State::kDisabled:
    case State::kActive:
      return false;
    case State::kInactive:
      if (cfg_.allow_start_probing_immediately) return true;
      // If a cluster has been requested, wait until a packet of at least the
      // recommended size arrives, so the first probe packet is not tiny.
      return packet_size >= std::min(RecommendedMinProbeSize(), cfg_.min_packet_size);
  }
  return false;
}

void BitrateProber::OnIncomingPacket(DataSize packet_size) { MaybeSetActiveState(packet_size); }

void BitrateProber::CreateProbeCluster(const ProbeCluster& c) {
  while (!clusters_.empty() &&
         (c.at_time - clusters_.front().requested_at > kProbeClusterTimeout || clusters_.size() > kMaxPendingProbeClusters)) {
    clusters_.pop_front();
    ++discarded_;
  }
  Cluster cluster;
  cluster.requested_at = c.at_time;
  cluster.info.min_probes = c.min_probes;
  cluster.info.min_bytes = c.min_bytes.bytes();
  cluster.min_probe_delta = c.min_probe_delta;
  cluster.info.send_bitrate = c.rate;
  cluster.info.cluster_id = c.id;
  clusters_.push_back(cluster);
  MaybeSetActiveState(DataSize::Zero());
}

Timestamp BitrateProber::NextProbeTime(Timestamp) const {
  if (probing_state_ != State::kActive || clusters_.empty()) return Timestamp::PlusInfinity();
  return next_probe_time_;
}

std::optional<ProbeSendInfo> BitrateProber::CurrentCluster(Timestamp now) {
  if (clusters_.empty() || probing_state_ != State::kActive) return std::nullopt;
  if (next_probe_time_.IsFinite() && now - next_probe_time_ > cfg_.max_probe_delay) {
    // Probe delay too high: discard the cluster.
    clusters_.pop_front();
    ++discarded_;
    if (clusters_.empty()) {
      probing_state_ = State::kInactive;
      return std::nullopt;
    }
  }
  ProbeSendInfo info = clusters_.front().info;
  info.bytes_sent = clusters_.front().sent_bytes;
  return info;
}

DataSize BitrateProber::RecommendedMinProbeSize() const {
  if (clusters_.empty()) return DataSize::Zero();
  return Mul(clusters_.front().info.send_bitrate, clusters_.front().min_probe_delta);
}

void BitrateProber::ProbeSent(Timestamp now, DataSize size) {
  if (clusters_.empty()) return;
  Cluster* cluster = &clusters_.front();
  if (cluster->sent_probes == 0) cluster->started_at = now;
  cluster->sent_bytes += static_cast<int>(size.bytes());
  cluster->sent_probes += 1;
  next_probe_time_ = CalculateNextProbeTime(*cluster);
  if (cluster->sent_bytes >= cluster->info.min_bytes && cluster->sent_probes >= cluster->info.min_probes) {
    clusters_.pop_front();
  }
  if (clusters_.empty()) probing_state_ = State::kInactive;
}

Timestamp BitrateProber::CalculateNextProbeTime(const Cluster& cluster) const {
  // The next probe is due when the bytes sent so far would have been sent at the cluster rate.
  const TimeDelta delta = Div(DataSize::Bytes(cluster.sent_bytes), cluster.info.send_bitrate);
  return cluster.started_at + delta;
}

}  // namespace sm::cc
