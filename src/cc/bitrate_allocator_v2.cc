#include "sm/cc/bitrate_allocator_v2.h"

#include <algorithm>

namespace sm::cc {

namespace {
// Allow packets to be transmitted in up to 2 times max video bitrate if the
// bandwidth estimate allows it (kTransmissionMaxBitrateMultiplier).
constexpr int64_t kTransmissionMaxBitrateMultiplier = 2;
// Require a bitrate increase of max(10%, 20kbps) to resume paused streams.
constexpr double kToggleFactor = 0.1;
constexpr int64_t kMinToggleBitrateBps = 20000;

using Tracks = std::vector<int64_t>;  // One allocation per track, in track order.

double MediaRatio(int64_t allocated, int64_t protection) {
  if (protection == 0) return 1.0;
  return static_cast<double>(allocated - protection) / static_cast<double>(allocated);
}
}  // namespace

int64_t BitrateAllocatorV2::Track::MinWithHysteresis() const {
  int64_t min_bitrate = cfg.min.bps();
  if (LastAllocated() == 0) {
    min_bitrate += std::max(static_cast<int64_t>(kToggleFactor * static_cast<double>(min_bitrate)), kMinToggleBitrateBps);
  }
  // Account for protection bitrate used by this observer in the previous allocation.
  if (media_ratio > 0.0 && media_ratio < 1.0) min_bitrate += static_cast<int64_t>(static_cast<double>(min_bitrate) * (1.0 - media_ratio));
  return min_bitrate;
}

namespace {
struct View {
  int64_t min, max, priority_rate, min_hyst, last;
  bool enforce_min;
  double priority;
};
}  // namespace

// Splits `bitrate` evenly to the tracks in `list` (those with a non-zero
// allocation unless include_zero), each limited to max_multiplier x its max.
static void DistributeEvenly(const std::vector<View>& v, int64_t bitrate, bool include_zero, int64_t max_multiplier, Tracks& alloc) {
  std::vector<size_t> order;
  for (size_t i = 0; i < v.size(); ++i) {
    if (include_zero || alloc[i] != 0) order.push_back(i);
  }
  // multimap<max, track>: ascending by max, equal maxes in insertion order.
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return v[a].max < v[b].max; });
  size_t remaining = order.size();
  for (size_t i : order) {
    int64_t extra = bitrate / static_cast<int64_t>(remaining);
    int64_t total = extra + alloc[i];
    bitrate -= extra;
    if (total > max_multiplier * v[i].max) {
      // More than this observer can use: carry over to the remaining ones.
      bitrate += total - max_multiplier * v[i].max;
      total = max_multiplier * v[i].max;
    }
    alloc[i] = total;
    --remaining;
  }
}

static void DistributeRelatively(const std::vector<View>& v, int64_t remaining, const Tracks& capacities, Tracks& alloc) {
  struct Obs {
    size_t idx;
    int64_t capacity;
    double priority;
  };
  std::vector<Obs> obs;
  double priority_sum = 0;
  for (size_t i = 0; i < v.size(); ++i) {
    obs.push_back({i, capacities[i], v[i].priority});
    priority_sum += v[i].priority;
  }
  // Observers that fill their capacity first, normalised by priority.
  std::sort(obs.begin(), obs.end(), [](const Obs& a, const Obs& b) {
    return static_cast<double>(a.capacity) / a.priority < static_cast<double>(b.capacity) / b.priority;
  });
  size_t i = 0;
  for (; i < obs.size(); ++i) {
    double share = obs[i].priority / priority_sum;
    double allocation = share * static_cast<double>(remaining);
    if (!(allocation >= static_cast<double>(obs[i].capacity))) break;
    alloc[obs[i].idx] += obs[i].capacity;
    remaining -= obs[i].capacity;
    priority_sum -= obs[i].priority;
  }
  for (; i < obs.size(); ++i) {
    double fraction = obs[i].priority / priority_sum;
    alloc[obs[i].idx] += static_cast<int64_t>(fraction * static_cast<double>(remaining));
  }
}

static Tracks LowRate(const std::vector<View>& v, int64_t bitrate) {
  Tracks alloc(v.size(), 0);
  // Observers enforcing a minimum first: remaining may turn negative.
  int64_t remaining = bitrate;
  for (size_t i = 0; i < v.size(); ++i) {
    alloc[i] = v[i].enforce_min ? v[i].min : 0;
    remaining -= alloc[i];
  }
  // Previously active streams.
  if (remaining > 0) {
    for (size_t i = 0; i < v.size(); ++i) {
      if (v[i].enforce_min || v[i].last == 0) continue;
      if (remaining >= v[i].min_hyst) {
        alloc[i] = v[i].min_hyst;
        remaining -= v[i].min_hyst;
      }
    }
  }
  // Previously paused streams, with the hysteresis.
  if (remaining > 0) {
    for (size_t i = 0; i < v.size(); ++i) {
      if (v[i].last != 0) continue;
      if (remaining >= v[i].min_hyst) {
        alloc[i] = v[i].min_hyst;
        remaining -= v[i].min_hyst;
      }
    }
  }
  // A possible remainder, evenly on all streams with an allocation.
  if (remaining > 0) DistributeEvenly(v, remaining, false, 1, alloc);
  return alloc;
}

static Tracks NormalRate(const std::vector<View>& v, int64_t bitrate, int64_t sum_min) {
  Tracks alloc(v.size()), capacities(v.size());
  for (size_t i = 0; i < v.size(); ++i) {
    alloc[i] = v[i].min;
    capacities[i] = v[i].max - v[i].min;
  }
  bitrate -= sum_min;
  // Streams with a priority rate take it first-come-first-served.
  for (size_t i = 0; i < v.size(); ++i) {
    int64_t margin = v[i].priority_rate - alloc[i];
    if (margin > 0 && bitrate > 0) {
      int64_t extra = std::min(margin, bitrate);
      alloc[i] += extra;
      capacities[i] -= extra;
      bitrate -= extra;
    }
  }
  if (bitrate > 0) DistributeRelatively(v, bitrate, capacities, alloc);
  return alloc;
}

static Tracks MaxRate(const std::vector<View>& v, int64_t bitrate) {
  Tracks alloc(v.size());
  for (size_t i = 0; i < v.size(); ++i) {
    alloc[i] = v[i].max;
    bitrate -= v[i].max;
  }
  DistributeEvenly(v, bitrate, true, kTransmissionMaxBitrateMultiplier, alloc);
  return alloc;
}

std::vector<int64_t> BitrateAllocatorV2::Allocate(int64_t bitrate) const {
  if (tracks_.empty()) return {};
  if (bitrate == 0) return Tracks(tracks_.size(), 0);
  std::vector<View> v;
  int64_t sum_min = 0, sum_max = 0;
  for (const Track& t : tracks_) {
    v.push_back({t.cfg.min.bps(), t.cfg.max.bps(), t.cfg.priority_rate.bps(), t.MinWithHysteresis(), t.LastAllocated(), t.cfg.enforce_min, t.cfg.priority});
    sum_min += t.cfg.min.bps();
    sum_max += t.cfg.max.bps();
  }
  // EnoughBitrateForAllObservers.
  bool enough = bitrate >= sum_min;
  if (enough) {
    const int64_t extra = (bitrate - sum_min) / static_cast<int64_t>(v.size());
    for (const View& o : v) {
      if (o.min + extra < o.min_hyst) enough = false;
    }
  }
  // Not enough for all observers: enforced minimums, then the previously
  // allocated, then the paused streams.
  if (!enough) return LowRate(v, bitrate);
  if (bitrate <= sum_max) return NormalRate(v, bitrate, sum_min);
  return MaxRate(v, bitrate);
}

void BitrateAllocatorV2::AddObserver(const std::string& id, StreamAllocationConfig cfg, Observer on_update) {
  auto it = std::find_if(tracks_.begin(), tracks_.end(), [&](const Track& t) { return t.id == id; });
  if (it != tracks_.end()) {
    it->cfg = cfg;
    if (on_update) it->observer = std::move(on_update);
  } else {
    Track t;
    t.id = id;
    t.cfg = cfg;
    t.observer = std::move(on_update);
    tracks_.push_back(std::move(t));
  }
  if (last_target_bps_ > 0) {
    Reallocate(Timestamp::MinusInfinity(), /*cwnd_reduce_ratio=*/0);
  } else {
    // An encoder may not produce frames yet; the observer is told a zero allocation.
    for (Track& t : tracks_) {
      if (t.id != id) continue;
      t.cwnd_reduce_ratio = 0;
      if (t.observer) t.observer(DataRate::Zero());
    }
    UpdateAllocationLimits();
  }
}

void BitrateAllocatorV2::RemoveObserver(const std::string& id) {
  tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(), [&](const Track& t) { return t.id == id; }), tracks_.end());
  UpdateAllocationLimits();
}

void BitrateAllocatorV2::OnTargetChanged(DataRate target, Timestamp now, double cwnd_reduce_ratio) {
  last_target_bps_ = target.bps();
  Reallocate(now, cwnd_reduce_ratio);
}

void BitrateAllocatorV2::Reallocate(Timestamp now, double cwnd_reduce_ratio) {
  if (tracks_.empty()) return;
  const std::vector<int64_t> alloc = Allocate(last_target_bps_);
  for (size_t i = 0; i < tracks_.size(); ++i) {
    Track& t = tracks_[i];
    const int64_t allocated = alloc[i];
    t.cwnd_reduce_ratio = cwnd_reduce_ratio;
    const int64_t protection = t.observer ? t.observer(DataRate::BitsPerSec(allocated)).bps() : 0;
    if (allocated == 0 && t.allocated_bps > 0) {
      if (last_target_bps_ > 0) ++pause_events_;
      SM_EVENT(events_, now, "allocator", "pause", static_cast<double>(t.cfg.min.bps()), static_cast<double>(last_target_bps_));
    } else if (allocated > 0 && t.allocated_bps == 0) {
      if (last_target_bps_ > 0) ++pause_events_;
      SM_EVENT(events_, now, "allocator", "resume", static_cast<double>(allocated), static_cast<double>(protection));
    }
    // Only update the media ratio if the observer got an allocation.
    if (allocated > 0) t.media_ratio = MediaRatio(allocated, protection);
    t.allocated_bps = allocated;
  }
  UpdateAllocationLimits();
}

void BitrateAllocatorV2::UpdateAllocationLimits() {
  AllocationLimits l;
  for (const Track& t : tracks_) {
    DataRate padding = t.cfg.pad_up;
    if (t.cfg.enforce_min) {
      l.min_allocatable = l.min_allocatable + t.cfg.min;
    } else if (t.allocated_bps == 0) {
      padding = std::max(DataRate::BitsPerSec(t.MinWithHysteresis()), padding);
    }
    l.max_padding = l.max_padding + padding;
    l.max_allocatable = l.max_allocatable + t.cfg.max;
  }
  limits_ = l;
}

DataRate BitrateAllocatorV2::allocation(const std::string& id) const {
  for (const Track& t : tracks_) {
    if (t.id == id) return DataRate::BitsPerSec(std::max<int64_t>(0, t.allocated_bps));
  }
  return DataRate::Zero();
}

std::optional<StreamAllocationConfig> BitrateAllocatorV2::config(const std::string& id) const {
  for (const Track& t : tracks_) {
    if (t.id == id) return t.cfg;
  }
  return std::nullopt;
}

double BitrateAllocatorV2::cwnd_reduce_ratio(const std::string& id) const {
  for (const Track& t : tracks_) {
    if (t.id == id) return t.cwnd_reduce_ratio;
  }
  return 0;
}

bool BitrateAllocatorV2::paused(const std::string& id) const {
  for (const Track& t : tracks_) {
    if (t.id == id) return t.allocated_bps == 0;
  }
  return false;
}

void BitrateAllocatorV2::ExportMetrics(MetricsWriter& w) const {
  w.Counter("pause_events", pause_events_);
  w.Gauge("min_allocatable_kbps", limits_.min_allocatable.kbps());
  w.Gauge("max_allocatable_kbps", limits_.max_allocatable.kbps());
  for (const Track& t : tracks_) w.Gauge(t.id + "_allocated_kbps", static_cast<double>(std::max<int64_t>(0, t.allocated_bps)) / 1000.0);
}

}  // namespace sm::cc
