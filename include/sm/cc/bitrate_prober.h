// Bitrate prober, `pacer v2`: libwebrtc's BitrateProber (pacing/bitrate_prober.
// {h,cc}, M157, compare/webrtc/reference/), with its defaults (WebRTC-Bwe-
// ProbingBehavior unset: max_probe_delay 10 ms, min_packet_size 200 B).
//
// The prober holds the probe clusters GoogCC asked for and tells the pacer when
// the next probe burst is due. It becomes active when a media packet of at least
// min(RecommendedMinProbeSize, 200 B) is enqueued, so a probe never starts on a
// tiny first packet; each burst carries `rate x min_probe_delta` bytes (>= 2 ms
// of the cluster rate); the next burst is due when the bytes sent so far would
// have been sent at the cluster rate; a burst more than max_probe_delay late
// drops the whole cluster; a cluster ends after min_probes packets and
// min_bytes bytes.
#pragma once

#include <deque>
#include <optional>

#include "sm/cc/probing.h"
#include "sm/core/units.h"

namespace sm::cc {

// PacedPacketInfo of the cluster being sent.
struct ProbeSendInfo {
  int cluster_id = -1;  // PacedPacketInfo::kNotAProbe
  int min_probes = -1;
  int64_t min_bytes = -1;
  DataRate send_bitrate;
  int64_t bytes_sent = 0;
};

struct BitrateProberConfig {
  TimeDelta max_probe_delay = TimeDelta::Millis(10);
  DataSize min_packet_size = DataSize::Bytes(200);
  bool allow_start_probing_immediately = false;
};

class BitrateProber {
 public:
  explicit BitrateProber(BitrateProberConfig cfg = {});
  bool is_probing() const { return probing_state_ == State::kActive; }
  void OnIncomingPacket(DataSize packet_size);
  void CreateProbeCluster(const ProbeCluster& cluster);
  // Plus infinity when not probing; minus infinity when a probe may start now.
  Timestamp NextProbeTime(Timestamp now) const;
  std::optional<ProbeSendInfo> CurrentCluster(Timestamp now);
  DataSize RecommendedMinProbeSize() const;
  void ProbeSent(Timestamp now, DataSize size);
  size_t pending_clusters() const { return clusters_.size(); }
  // Clusters dropped because a burst was too late or the queue was stale.
  uint64_t discarded() const { return discarded_; }

 private:
  enum class State { kDisabled, kInactive, kActive };
  struct Cluster {
    ProbeSendInfo info;
    int sent_probes = 0;
    int sent_bytes = 0;
    TimeDelta min_probe_delta = TimeDelta::Zero();
    Timestamp requested_at = Timestamp::MinusInfinity();
    Timestamp started_at = Timestamp::MinusInfinity();
  };
  Timestamp CalculateNextProbeTime(const Cluster& cluster) const;
  void MaybeSetActiveState(DataSize packet_size);
  bool ReadyToSetActiveState(DataSize packet_size) const;

  State probing_state_ = State::kInactive;
  std::deque<Cluster> clusters_;
  Timestamp next_probe_time_ = Timestamp::PlusInfinity();
  BitrateProberConfig cfg_;
  uint64_t discarded_ = 0;
};

}  // namespace sm::cc
