#include "sm/cc/bitrate_allocator.h"

#include <algorithm>

namespace sm::cc {

void BitrateAllocator::AddStream(const AllocatableStream& s) {
  RemoveStream(s.id);
  streams_.push_back(s);
  paused_[s.id] = false;
}

void BitrateAllocator::RemoveStream(const std::string& id) {
  std::erase_if(streams_, [&](const AllocatableStream& s) { return s.id == id; });
  paused_.erase(id);
  last_.erase(id);
}

std::map<std::string, DataRate> BitrateAllocator::Allocate(DataRate total) {
  last_total_ = total;
  std::map<std::string, DataRate> out;
  std::vector<const AllocatableStream*> order;
  for (const auto& s : streams_) order.push_back(&s);
  std::stable_sort(order.begin(), order.end(),
                   [](const AllocatableStream* a, const AllocatableStream* b) { return a->priority > b->priority; });
  // 1. Minimums.
  int64_t left = total.bps();
  std::vector<const AllocatableStream*> active;
  for (const auto* s : order) {
    if (s->enforce_min) {
      out[s->id] = s->min;
      left -= s->min.bps();
      active.push_back(s);
    }
  }
  for (const auto* s : order) {
    if (s->enforce_min) continue;
    bool was_paused = paused_[s->id];
    int64_t need = was_paused ? static_cast<int64_t>(s->min.bps() * (1 + hysteresis_)) : s->min.bps();
    if (left >= need) {
      out[s->id] = s->min;
      left -= s->min.bps();
      active.push_back(s);
      if (was_paused) ++resumes_;
      paused_[s->id] = false;
    } else {
      out[s->id] = DataRate::Zero();
      if (!was_paused) ++pauses_;
      paused_[s->id] = true;
    }
  }
  // 2. Water-fill the remainder by priority weight.
  left = std::max<int64_t>(0, left);
  std::vector<const AllocatableStream*> open = active;
  while (left > 0 && !open.empty()) {
    double weight = 0;
    for (const auto* s : open) weight += s->priority;
    int64_t distributed = 0;
    std::vector<const AllocatableStream*> still_open;
    for (const auto* s : open) {
      int64_t share = static_cast<int64_t>(static_cast<double>(left) * s->priority / weight);
      int64_t room = s->max.bps() - out[s->id].bps();
      int64_t give = std::min(share, room);
      out[s->id] = out[s->id] + DataRate::BitsPerSec(give);
      distributed += give;
      if (give < room) still_open.push_back(s);
    }
    left -= distributed;
    if (distributed == 0) break;
    open = still_open;
  }
  last_ = out;
  return out;
}

void BitrateAllocator::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("total_kbps", last_total_.kbps());
  for (const auto& [id, r] : last_) w.Gauge("alloc_kbps." + id, r.kbps());
  w.Counter("pauses", pauses_);
  w.Counter("resumes", resumes_);
}

}  // namespace sm::cc
