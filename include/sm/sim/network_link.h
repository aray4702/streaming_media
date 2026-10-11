// Deterministic network emulator for end-to-end tests and benchmarks.
//
// Models, in order: a bottleneck link with finite capacity and a drop-tail
// queue (bufferbloat), random or bursty (Gilbert-Elliott) loss, propagation
// delay with Gaussian jitter (optionally reordering) and duplication. It can
// also be taken down to emulate a link outage.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <queue>
#include <vector>

#include "sm/core/random.h"
#include "sm/core/units.h"

namespace sm::sim {

struct LinkConfig {
  TimeDelta base_delay = TimeDelta::Millis(20);
  TimeDelta jitter_stddev = TimeDelta::Zero();
  bool allow_reordering = true;
  double loss_rate = 0;  // Bernoulli loss when !gilbert.
  // Gilbert-Elliott two-state burst loss.
  bool gilbert = false;
  double p_good_to_bad = 0.0;
  double p_bad_to_good = 0.5;
  double loss_in_bad = 1.0;
  double loss_in_good = 0.0;
  DataRate capacity = DataRate::PlusInfinity();
  DataSize queue_limit = DataSize::Zero();  // 0 = unlimited.
  double duplicate_rate = 0;
  bool down = false;  // Outage: drop everything.
};

struct LinkStats {
  uint64_t sent = 0;
  uint64_t lost = 0;           // Random/burst loss and outage.
  uint64_t queue_dropped = 0;  // Drop-tail overflow.
  uint64_t delivered = 0;
  uint64_t duplicated = 0;
  int64_t bytes_delivered = 0;
};

template <class T>
class NetworkLink {
 public:
  explicit NetworkLink(LinkConfig cfg = {}, uint64_t seed = 1) : cfg_(cfg), rng_(seed) {}

  void SetConfig(const LinkConfig& cfg) { cfg_ = cfg; }
  const LinkConfig& config() const { return cfg_; }
  const LinkStats& stats() const { return stats_; }

  // Bytes waiting in the bottleneck queue at `now`.
  DataSize QueuedBytes(Timestamp now) const {
    if (!cfg_.capacity.IsFinite() || link_free_ <= now) return DataSize::Zero();
    return cfg_.capacity * (link_free_ - now);
  }

  void Send(Timestamp now, T packet, DataSize size) {
    ++stats_.sent;
    if (cfg_.down) {
      ++stats_.lost;
      return;
    }
    Timestamp depart = now;
    if (cfg_.capacity.IsFinite() && cfg_.capacity.bps() > 0) {
      if (cfg_.queue_limit.bytes() > 0 && QueuedBytes(now) + size > cfg_.queue_limit) {
        ++stats_.queue_dropped;
        return;
      }
      Timestamp start = std::max(now, link_free_);
      depart = start + TimeDelta::Micros(size.bits() * 1000000 / cfg_.capacity.bps());
      link_free_ = depart;
    }
    if (IsLost()) {
      ++stats_.lost;
      return;
    }
    Enqueue(depart, packet, size);
    if (cfg_.duplicate_rate > 0 && rng_.Bernoulli(cfg_.duplicate_rate)) {
      ++stats_.duplicated;
      Enqueue(depart, std::move(packet), size);
    }
  }

  // Returns every packet that has arrived by `now`, in arrival order.
  std::vector<T> Receive(Timestamp now) {
    std::vector<T> out;
    while (!in_flight_.empty() && in_flight_.top().arrival <= now) {
      out.push_back(std::move(const_cast<Item&>(in_flight_.top()).packet));
      stats_.bytes_delivered += in_flight_.top().size;
      in_flight_.pop();
      ++stats_.delivered;
    }
    return out;
  }

  // Like Receive, but also reports each packet's arrival time.
  template <class F>
  void ReceiveEach(Timestamp now, F&& f) {
    while (!in_flight_.empty() && in_flight_.top().arrival <= now) {
      Item it = std::move(const_cast<Item&>(in_flight_.top()));
      in_flight_.pop();
      stats_.bytes_delivered += it.size;
      ++stats_.delivered;
      f(std::move(it.packet), it.arrival);
    }
  }

  Timestamp NextArrival() const {
    return in_flight_.empty() ? Timestamp::PlusInfinity() : in_flight_.top().arrival;
  }
  size_t InFlight() const { return in_flight_.size(); }

 private:
  struct Item {
    Timestamp arrival;
    uint64_t order;
    int64_t size;
    T packet;
  };
  struct Later {
    bool operator()(const Item& a, const Item& b) const {
      return a.arrival != b.arrival ? a.arrival > b.arrival : a.order > b.order;
    }
  };

  bool IsLost() {
    if (cfg_.gilbert) {
      if (bad_) {
        if (rng_.Bernoulli(cfg_.p_bad_to_good)) bad_ = false;
      } else if (rng_.Bernoulli(cfg_.p_good_to_bad)) {
        bad_ = true;
      }
      return rng_.Bernoulli(bad_ ? cfg_.loss_in_bad : cfg_.loss_in_good);
    }
    return cfg_.loss_rate > 0 && rng_.Bernoulli(cfg_.loss_rate);
  }

  void Enqueue(Timestamp depart, T packet, DataSize size) {
    TimeDelta jitter = TimeDelta::Zero();
    if (cfg_.jitter_stddev.us() > 0) {
      jitter = TimeDelta::Micros(static_cast<int64_t>(
          std::abs(rng_.Gaussian(0, static_cast<double>(cfg_.jitter_stddev.us())))));
    }
    Timestamp arrival = depart + cfg_.base_delay + jitter;
    if (!cfg_.allow_reordering) arrival = std::max(arrival, last_arrival_);
    last_arrival_ = arrival;
    in_flight_.push(Item{arrival, order_++, size.bytes(), std::move(packet)});
  }

  LinkConfig cfg_;
  Random rng_;
  LinkStats stats_;
  bool bad_ = false;
  Timestamp link_free_ = Timestamp::Micros(0);
  Timestamp last_arrival_ = Timestamp::Micros(0);
  uint64_t order_ = 0;
  std::priority_queue<Item, std::vector<Item>, Later> in_flight_;
};

}  // namespace sm::sim
