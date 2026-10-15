// Pieces of a test bench call that other call implementations share, so
// they apply the same link timeline and compute metrics the same way
// (compare/ runs other libraries through the test bench).
#pragma once

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "scenario.h"
#include "sm/sim/network_link.h"

namespace sm::testbench {

// Applies scenario link changes to an emulated link.
struct LinkState {
  sim::LinkConfig cfg;
  TimeDelta queue = TimeDelta::Zero();
  DataRate cross = DataRate::Zero();
  double loss = 0, burst = 1;

  void Apply(const LinkChange& c) {
    if (c.capacity) cfg.capacity = *c.capacity;
    if (c.delay) cfg.base_delay = *c.delay;
    if (c.jitter) cfg.jitter_stddev = *c.jitter;
    if (c.loss) loss = *c.loss;
    if (c.burst_len) burst = *c.burst_len;
    if (c.duplicate) cfg.duplicate_rate = *c.duplicate;
    if (c.reorder) cfg.allow_reordering = *c.reorder;
    if (c.queue) queue = *c.queue;
    if (c.cross) cross = *c.cross;
    if (c.down) cfg.down = *c.down;
    // Loss: Bernoulli, or Gilbert-Elliott with the requested mean burst
    // length and the same average loss rate.
    if (burst > 1 && loss > 0 && loss < 1) {
      cfg.gilbert = true;
      cfg.p_bad_to_good = 1.0 / burst;
      cfg.p_good_to_bad = loss * cfg.p_bad_to_good / (1 - loss);
      cfg.loss_in_bad = 1.0;
      cfg.loss_in_good = 0.0;
    } else {
      cfg.gilbert = false;
      cfg.loss_rate = loss;
    }
    cfg.queue_limit = (cfg.capacity.IsFinite() && queue.us() > 0)
                          ? std::max(DataSize::Bytes(1500), cfg.capacity * queue)
                          : DataSize::Zero();
  }
};

inline LinkState DefaultForward() {
  LinkState l;
  l.cfg.base_delay = TimeDelta::Millis(20);
  return l;
}

// Steps the forward and reverse links through the scenario's timeline.
class LinkTimeline {
 public:
  explicit LinkTimeline(const Scenario& s) : s_(s), fwd_(DefaultForward()), rev_(DefaultForward()) {
    // An unset reverse link mirrors the forward link's initial delay.
    if (s.reverse.empty() && !s.forward.empty() && s.forward[0].at.IsZero() && s.forward[0].delay) {
      rev_.cfg.base_delay = *s.forward[0].delay;
    }
  }
  const LinkState& forward() const { return fwd_; }
  const LinkState& reverse() const { return rev_; }

  // Applies every change due by `t` (time since the call started). Returns
  // {forward changed, reverse changed}.
  std::pair<bool, bool> Advance(TimeDelta t) {
    bool f = false, r = false;
    while (next_fwd_ < s_.forward.size() && s_.forward[next_fwd_].at <= t) {
      fwd_.Apply(s_.forward[next_fwd_++]);
      f = true;
    }
    while (next_rev_ < s_.reverse.size() && s_.reverse[next_rev_].at <= t) {
      rev_.Apply(s_.reverse[next_rev_++]);
      r = true;
    }
    return {f, r};
  }

 private:
  const Scenario& s_;
  LinkState fwd_, rev_;
  size_t next_fwd_ = 0, next_rev_ = 0;
};

// Bottleneck queue delay of a link, in ms.
template <class T>
double QueueDelayMs(const sim::NetworkLink<T>& link, const LinkState& st, Timestamp now) {
  if (!st.cfg.capacity.IsFinite() || st.cfg.capacity.bps() <= 0) return 0;
  return static_cast<double>(link.QueuedBytes(now).bits()) * 1000.0 / static_cast<double>(st.cfg.capacity.bps());
}

inline double Percentile(std::vector<double> v, double p) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  double idx = p / 100.0 * static_cast<double>(v.size() - 1);
  size_t lo = static_cast<size_t>(std::floor(idx));
  size_t hi = std::min(v.size() - 1, lo + 1);
  return v[lo] + (v[hi] - v[lo]) * (idx - static_cast<double>(lo));
}

inline double Mean(const std::vector<double>& v) {
  if (v.empty()) return 0;
  double s = 0;
  for (double x : v) s += x;
  return s / static_cast<double>(v.size());
}

}  // namespace sm::testbench
