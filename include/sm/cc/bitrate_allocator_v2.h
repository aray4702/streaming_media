// 5.7 Bitrate allocation as libwebrtc: call/bitrate_allocator.cc (issue #104).
//
// A port of webrtc::BitrateAllocator, one observer per stream (audio, video):
//   - below the sum of the minimums (LowRateAllocation): a stream that enforces
//     its minimum gets it, then streams that were active last round get their
//     minimum with hysteresis, then paused streams, then the remainder is shared
//     evenly among the streams that have an allocation;
//   - between the minimums and the maximums (NormalRateAllocation): minimums, then
//     the priority rate of each stream in order, then the rest in proportion to
//     bitrate_priority up to each stream's capacity;
//   - above the maximums (MaxRateAllocation): every stream gets its maximum and
//     the surplus is shared evenly up to 2x each maximum.
// A paused stream resumes at min + max(10 %, 20 kbps), plus the protection share
// it used last time. The limits (UpdateAllocationLimits) are what libwebrtc hands
// to the network controller: the sum of the enforced minimums is its lower bound
// for the target (GoogCC min_total_allocated_bitrate) and the pacing floor.
// Not ported: the elastic-rate allocation (WebRTC-ElasticBitrateAllocation, off by
// default; GetUsedRate is nullopt for video).
// Rates are integer bits per second, as libwebrtc's uint32 bps.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::cc {

// MediaStreamAllocationConfig.
struct StreamAllocationConfig {
  DataRate min;
  DataRate max;
  DataRate pad_up;
  DataRate priority_rate;  // priority_bitrate_bps: allocated before the proportional share.
  bool enforce_min = false;
  double priority = 1.0;   // bitrate_priority
};

// BitrateAllocationLimits.
struct AllocationLimits {
  DataRate min_allocatable;
  DataRate max_padding;
  DataRate max_allocatable;
  bool operator==(const AllocationLimits&) const = default;
};

class BitrateAllocatorV2 : public MetricsSource {
 public:
  // Called with the stream and its new allocation; returns the protection (FEC
  // and retransmission) bitrate the stream takes out of it, which sets the media
  // ratio of the next hysteresis (BitrateAllocatorObserver::OnBitrateUpdated).
  using Observer = std::function<DataRate(DataRate allocation)>;

  explicit BitrateAllocatorV2(EventSink* events = nullptr) : events_(events) {}

  // Adds the stream, or updates the config of an existing one.
  void AddObserver(const std::string& id, StreamAllocationConfig cfg, Observer on_update = nullptr);
  void RemoveObserver(const std::string& id);
  // OnNetworkEstimateChanged: reallocates and notifies every observer. The
  // congestion-window reduce ratio rides on the update to every stream
  // (BitrateAllocationUpdate::cwnd_reduce_ratio). The other reallocations
  // (AddObserver here) pass none, which the streams take as 0
  // (ReallocateAndNotifyObservers(std::nullopt) -> value_or(0)), so the
  // video encoder stops its congestion-window frame drops until the next
  // network update (issue #411, D8).
  void OnTargetChanged(DataRate target, Timestamp now, double cwnd_reduce_ratio = 0);

  // The last allocation of the stream; Zero when it is paused or unknown.
  DataRate allocation(const std::string& id) const;
  bool paused(const std::string& id) const;
  // The stream's registered config; nullopt if it is not registered.
  std::optional<StreamAllocationConfig> config(const std::string& id) const;
  // The cwnd_reduce_ratio of the last update the stream was given; 0 if none.
  double cwnd_reduce_ratio(const std::string& id) const;
  DataRate target() const { return DataRate::BitsPerSec(last_target_bps_); }
  const AllocationLimits& limits() const { return limits_; }
  uint64_t pause_events() const { return pause_events_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  struct Track {
    std::string id;
    StreamAllocationConfig cfg;
    Observer observer;
    int64_t allocated_bps = -1;  // -1: no allocation yet.
    double media_ratio = 1.0;
    double cwnd_reduce_ratio = 0;  // Of the last BitrateAllocationUpdate.
    int64_t LastAllocated() const { return allocated_bps == -1 ? cfg.min.bps() : allocated_bps; }
    int64_t MinWithHysteresis() const;
  };
  std::vector<int64_t> Allocate(int64_t bitrate) const;
  void Reallocate(Timestamp now, double cwnd_reduce_ratio);  // ReallocateAndNotifyObservers.
  void UpdateAllocationLimits();

  EventSink* events_;
  std::vector<Track> tracks_;
  int64_t last_target_bps_ = 0;
  uint64_t pause_events_ = 0;
  AllocationLimits limits_;
};

}  // namespace sm::cc
