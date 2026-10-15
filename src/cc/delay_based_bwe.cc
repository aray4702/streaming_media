#include "sm/cc/delay_based_bwe.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace sm::cc {

const char* ToString(BandwidthUsage u) {
  switch (u) {
    case BandwidthUsage::kNormal: return "normal";
    case BandwidthUsage::kUnderusing: return "underusing";
    case BandwidthUsage::kOverusing: return "overusing";
  }
  return "?";
}

// --- InterArrival ----------------------------------------------------------------

std::optional<InterArrival::Deltas> InterArrival::OnPacket(Timestamp send_time, Timestamp arrival, DataSize size) {
  static const bool ia = getenv("SM_X_IA") != nullptr;
  std::optional<Deltas> out;
  if (!ia) {
  if (!current_.valid()) {
    current_ = Group{send_time, send_time, arrival, arrival, size.bytes()};
    return out;
  }
  if (send_time < current_.first_send) return out;  // Reordered across groups: ignore.
  // Same burst: sent close to the group's first packet.
  bool same_group = send_time - current_.first_send <= group_length_;
  if (same_group) {
    current_.last_send = std::max(current_.last_send, send_time);
    current_.last_arrival = std::max(current_.last_arrival, arrival);
    current_.size += size.bytes();
    return out;
  }
  if (prev_.valid()) {
    Deltas d;
    d.send_delta = current_.last_send - prev_.last_send;
    d.recv_delta = current_.last_arrival - prev_.last_arrival;
    d.size_delta = current_.size - prev_.size;
    out = d;
  }
  prev_ = current_;
  current_ = Group{send_time, send_time, arrival, arrival, size.bytes()};
  return out;
  }
  // libwebrtc's InterArrivalDelta.
  if (!current_.valid()) {
    current_ = Group{send_time, send_time, arrival, arrival, 0};
  } else if (current_.first_send > send_time) {
    return out;  // Reordered packet.
  } else if (!BelongsToBurst(arrival, send_time) && send_time - current_.first_send > group_length_) {
    // First packet of a later send burst: the previous group's sample is ready.
    if (prev_.valid()) {
      Deltas d;
      d.send_delta = current_.last_send - prev_.last_send;
      d.recv_delta = current_.last_arrival - prev_.last_arrival;
      if (d.recv_delta < TimeDelta::Zero()) {
        // The group arrived out of order after its local arrival time was taken.
        if (++reordered_ >= kReorderedResetThreshold) {
          prev_ = Group{};
          current_ = Group{};
        }
        return out;
      }
      reordered_ = 0;
      d.size_delta = current_.size - prev_.size;
      out = d;
    }
    prev_ = current_;
    current_ = Group{send_time, send_time, arrival, arrival, 0};
  } else {
    current_.last_send = std::max(current_.last_send, send_time);
  }
  current_.size += size.bytes();
  current_.last_arrival = arrival;
  return out;
}

// Packets that arrive in a tight train, faster than they were sent, belong to the group already open: they are
// one burst (a probe cluster or a frame) that the bottleneck spread out, not separate groups.
bool InterArrival::BelongsToBurst(Timestamp arrival, Timestamp send_time) const {
  TimeDelta send_delta = send_time - current_.last_send;
  if (send_delta.IsZero()) return true;
  TimeDelta arrival_delta = arrival - current_.last_arrival;
  TimeDelta propagation_delta = arrival_delta - send_delta;
  return propagation_delta < TimeDelta::Zero() && arrival_delta <= TimeDelta::Millis(5) &&
         arrival - current_.first_arrival < TimeDelta::Millis(100);
}

// --- TrendlineEstimator -------------------------------------------------------------

void TrendlineEstimator::Update(double recv_delta_ms, double send_delta_ms, double arrival_ms) {
  const double delta = recv_delta_ms - send_delta_ms;
  ++num_deltas_;
  if (first_arrival_ms_ < 0) first_arrival_ms_ = arrival_ms;
  accumulated_ += delta;
  smoothed_ = cfg_.smoothing * smoothed_ + (1 - cfg_.smoothing) * accumulated_;
  window_.emplace_back(arrival_ms - first_arrival_ms_, smoothed_);
  if (window_.size() > cfg_.window) window_.pop_front();
  if (window_.size() == cfg_.window) {
    // Least squares slope of smoothed delay vs. time.
    double mx = 0, my = 0;
    for (auto& [x, y] : window_) {
      mx += x;
      my += y;
    }
    mx /= static_cast<double>(window_.size());
    my /= static_cast<double>(window_.size());
    double num = 0, den = 0;
    for (auto& [x, y] : window_) {
      num += (x - mx) * (y - my);
      den += (x - mx) * (x - mx);
    }
    if (den > 0) trend_ = num / den;
  }
  Detect(send_delta_ms, arrival_ms);
}

void TrendlineEstimator::Detect(double send_delta_ms, double now_ms) {
  if (num_deltas_ < 2) {
    state_ = BandwidthUsage::kNormal;
    return;
  }
  modified_trend_ = static_cast<double>(std::min<size_t>(num_deltas_, 60)) * trend_ * cfg_.threshold_gain;
  if (modified_trend_ > threshold_) {
    if (time_over_using_ < 0) {
      time_over_using_ = send_delta_ms / 2;  // First sample: assume halfway.
    } else {
      time_over_using_ += send_delta_ms;
    }
    ++overuse_counter_;
    if (time_over_using_ > cfg_.overusing_time.ms_f() && overuse_counter_ > 1 && trend_ >= prev_trend_) {
      time_over_using_ = 0;
      overuse_counter_ = 0;
      state_ = BandwidthUsage::kOverusing;
    }
  } else if (modified_trend_ < -threshold_) {
    time_over_using_ = -1;
    overuse_counter_ = 0;
    state_ = BandwidthUsage::kUnderusing;
  } else {
    time_over_using_ = -1;
    overuse_counter_ = 0;
    state_ = BandwidthUsage::kNormal;
  }
  prev_trend_ = trend_;
  UpdateThreshold(modified_trend_, now_ms);
}

void TrendlineEstimator::UpdateThreshold(double modified_trend, double now_ms) {
  if (last_threshold_update_ms_ < 0) last_threshold_update_ms_ = now_ms;
  // Do not let sudden spikes (e.g. a route change) move the threshold.
  if (std::fabs(modified_trend) > threshold_ + 15.0) {
    last_threshold_update_ms_ = now_ms;
    return;
  }
  double k = std::fabs(modified_trend) < threshold_ ? cfg_.k_down : cfg_.k_up;
  double dt = std::min(now_ms - last_threshold_update_ms_, 100.0);
  threshold_ += k * (std::fabs(modified_trend) - threshold_) * dt;
  threshold_ = std::clamp(threshold_, 6.0, 600.0);
  last_threshold_update_ms_ = now_ms;
}

// --- AcknowledgedBitrateEstimator ---------------------------------------------------

void AcknowledgedBitrateEstimator::OnPacketResults(const std::vector<PacketResult>& results) {
  std::vector<std::pair<Timestamp, int64_t>> rx;
  for (const auto& r : results) {
    if (r.received()) rx.emplace_back(r.receive_time, r.size.bytes());
  }
  std::stable_sort(rx.begin(), rx.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  for (const auto& [t, bytes] : rx) Update(t, bytes);
}

// libwebrtc's BitrateEstimator: non-overlapping windows (the long one only
// for the first sample), each sample folded in with a Bayesian update whose
// weight grows with how far the sample is from the estimate.
void AcknowledgedBitrateEstimator::Update(Timestamp at, int64_t bytes) {
  const int64_t now_ms = at.ms();
  const int64_t window_ms = estimate_kbps_ < 0 ? kInitialWindowMs : kWindowMs;
  if (now_ms < prev_ms_) {
    prev_ms_ = -1;
    sum_bytes_ = 0;
    current_window_ms_ = 0;
  }
  if (prev_ms_ >= 0) {
    current_window_ms_ += now_ms - prev_ms_;
    if (now_ms - prev_ms_ > window_ms) {
      sum_bytes_ = 0;
      current_window_ms_ %= window_ms;
    }
  }
  prev_ms_ = now_ms;
  double sample_kbps = -1;
  if (current_window_ms_ >= window_ms) {
    sample_kbps = 8.0 * static_cast<double>(sum_bytes_) / static_cast<double>(window_ms);
    current_window_ms_ -= window_ms;
    sum_bytes_ = 0;
  }
  sum_bytes_ += bytes;
  if (sample_kbps < 0) return;
  if (estimate_kbps_ < 0) {
    estimate_kbps_ = sample_kbps;
  } else {
    double uncertainty = 10.0 * std::fabs(estimate_kbps_ - sample_kbps) / estimate_kbps_;
    double sample_var = uncertainty * uncertainty;
    double pred_var = var_ + 5.0;
    estimate_kbps_ = (sample_var * estimate_kbps_ + pred_var * sample_kbps) / (sample_var + pred_var);
    var_ = sample_var * pred_var / (sample_var + pred_var);
  }
  estimate_kbps_ = std::max(estimate_kbps_, 0.0);
  bitrate_ = DataRate::BitsPerSec(static_cast<int64_t>(estimate_kbps_ * 1000));
}

// --- AimdRateController ---------------------------------------------------------------

void AimdRateController::SetEstimate(DataRate r, Timestamp now) {
  current_ = Clamp(r, cfg_.min_rate, cfg_.max_rate);
  has_estimate_ = true;
  last_change_ = now;
}

std::optional<double> AimdRateController::link_capacity_kbps() const {
  if (link_kbps_ < 0) return std::nullopt;
  return link_kbps_;
}

bool AimdRateController::TimeToReduceFurther(Timestamp now, DataRate acked) const {
  TimeDelta interval = Clamp(rtt_, TimeDelta::Millis(10), TimeDelta::Millis(200));
  if (now - last_change_ >= interval) return true;
  return has_estimate_ && acked < current_ * 0.5;
}

DataRate AimdRateController::Update(BandwidthUsage usage, std::optional<DataRate> acked, Timestamp now) {
  if (!last_change_.IsFinite()) last_change_ = now;
  static const bool d4 = getenv("SM_X_D4") != nullptr, d2 = getenv("SM_X_D2") != nullptr,
                    d5 = getenv("SM_X_D5") != nullptr;
  static const bool dbg = getenv("SM_DBG") != nullptr;
  if (dbg && usage == BandwidthUsage::kOverusing)
    fprintf(stderr, "t=%.3f overuse acked=%.0f cur=%.0f last_change=%.3f link=%.0f\n", now.us() / 1e6, acked ? acked->kbps() : -1.0, current_.kbps(), last_change_.us() / 1e6, link_kbps_);
  if (usage == BandwidthUsage::kOverusing && d4) {
    // As libwebrtc's DelayBasedBwe::MaybeUpdateEstimate: an overuse acts only when the last rate change of any
    // kind is one reduction interval old (or the delivered rate has collapsed); with no delivered rate yet it
    // halves the estimate at that pace instead.
    if (acked) {
      if (!TimeToReduceFurther(now, *acked)) return current_;
    } else if (getenv("SM_X_NOHALF")) {
      TimeDelta interval = Clamp(rtt_, TimeDelta::Millis(10), TimeDelta::Millis(200));
      if (last_decrease_.IsFinite() && now - last_decrease_ < interval) return current_;
      last_decrease_ = now;
      current_ = Clamp(current_ * cfg_.beta, cfg_.min_rate, cfg_.max_rate);
      last_change_ = now;
      state_ = State::kHold;
      return current_;
    } else {
      if (!TimeToReduceFurther(now, current_ * 0.5 - DataRate::BitsPerSec(1))) return current_;
      current_ = Clamp(current_ * 0.5, cfg_.min_rate, cfg_.max_rate);
      last_change_ = now;
      last_decrease_ = now;
      state_ = State::kHold;
      return current_;
    }
  }
  // State machine (GCC paper, Fig. 2).
  switch (usage) {
    case BandwidthUsage::kNormal:
      if (state_ == State::kHold) {
        state_ = State::kIncrease;
        if (d4) last_change_ = now;
      }
      break;
    case BandwidthUsage::kOverusing:
      state_ = State::kDecrease;
      break;
    case BandwidthUsage::kUnderusing:
      state_ = State::kHold;
      break;
  }
  TimeDelta dt = std::min(now - last_change_, TimeDelta::Seconds(1));
  DataRate next = current_;
  if (state_ == State::kIncrease) {
    // Leave link capacity estimate if we are clearly above it.
    if (link_kbps_ > 0 && acked && acked->kbps() > link_kbps_ + 3 * std::sqrt(link_var_ * link_kbps_)) {
      link_kbps_ = -1;
    }
    double inc_bps;
    if (link_kbps_ > 0) {
      // Near convergence: additive.
      double response_s = (rtt_ + TimeDelta::Millis(100)).seconds();
      if (d5) {
        double frame_bytes = current_.bps() / 8.0 / 30.0;
        double pkts = std::max(1.0, std::ceil(frame_bytes / 1200.0));
        inc_bps = std::max(4000.0, frame_bytes / pkts * 8 / (2 * response_s)) * dt.seconds();
      } else {
        inc_bps = std::max(1000.0, 1200 * 8 * dt.seconds() / response_s);
      }
    } else {
      inc_bps = std::max(1000.0, current_.bps() * (std::pow(1 + cfg_.multiplicative_per_s, dt.seconds()) - 1));
    }
    next = current_ + DataRate::BitsPerSec(static_cast<int64_t>(inc_bps));
    // Do not run too far ahead of what the network actually delivers, but
    // never decrease as a side effect of that cap.
    if (acked) next = std::min(next, std::max(current_, *acked * 1.5 + DataRate::KilobitsPerSec(10)));
  } else if (state_ == State::kDecrease) {
    if (!d4) {
      // Reduce at most once per RTT (clamped to [10, 200] ms) unless the delivered rate has collapsed.
      TimeDelta interval = Clamp(rtt_, TimeDelta::Millis(10), TimeDelta::Millis(200));
      bool collapsed = acked && *acked < current_ * 0.5;
      if (last_decrease_.IsFinite() && now - last_decrease_ < interval && !collapsed) {
        state_ = State::kHold;
        last_change_ = now;
        return current_;
      }
    }
    last_decrease_ = now;
    DataRate base = acked ? *acked : current_;
    if (d2) {
      // libwebrtc: beta*acked - 5 kbps; fall back to beta*link estimate if that would raise the rate;
      // never raise on overuse, and with nothing to base it on, do not cut.
      DataRate dec = base * cfg_.beta;
      if (dec > DataRate::KilobitsPerSec(5)) dec = dec - DataRate::KilobitsPerSec(5);
      if (dec > current_ && link_kbps_ > 0) dec = DataRate::BitsPerSec(static_cast<int64_t>(cfg_.beta * link_kbps_ * 1000));
      if (dec < current_) next = dec;
    } else {
      next = base * cfg_.beta;
      if (next > current_) next = current_ * cfg_.beta;  // Never increase on overuse.
    }
    if (acked) {
      double a = acked->kbps();
      // As libwebrtc's AimdRateControl: throughput far below the capacity
      // estimate means the estimate is stale, so re-seed it from this decrease.
      if (!getenv("SM_X_NOD3") && link_kbps_ > 0 && a < link_kbps_ - 3 * std::sqrt(link_var_ * link_kbps_)) link_kbps_ = -1;
      if (link_kbps_ < 0) {
        link_kbps_ = a;
      } else {
        double norm = std::max(link_kbps_, 1.0);
        link_var_ = std::clamp(0.95 * link_var_ + 0.05 * (link_kbps_ - a) * (link_kbps_ - a) / norm, 0.4, 2.5);
        link_kbps_ = 0.95 * link_kbps_ + 0.05 * a;
      }
    }
    state_ = State::kHold;
  }
  current_ = Clamp(next, cfg_.min_rate, cfg_.max_rate);
  last_change_ = now;
  has_estimate_ = true;
  return current_;
}

// --- DelayBasedBwe -----------------------------------------------------------------

DelayBasedBwe::DelayBasedBwe(Config cfg) : cfg_(cfg), trendline_(cfg.trendline), aimd_(cfg.aimd) {}

DataRate DelayBasedBwe::OnPacketResults(const std::vector<PacketResult>& results, std::optional<DataRate> acked,
                                        std::optional<DataRate> probe_estimate, Timestamp now) {
  ++stats_.feedbacks;
  std::vector<PacketResult> sorted;
  static const bool ia = getenv("SM_X_IA") != nullptr;
  if (ia) {
    for (const auto& r : results) if (r.received()) sorted.push_back(r);
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& x, const auto& y) { return x.receive_time < y.receive_time; });
  }
  for (const auto& r : (ia ? sorted : results)) {
    if (!r.received()) continue;
    if (auto d = inter_arrival_.OnPacket(r.send_time, r.receive_time, r.size)) {
      trendline_.Update(d->recv_delta.ms_f(), d->send_delta.ms_f(), r.receive_time.us() / 1000.0);
      stats_.trend.Add(std::fabs(trendline_.modified_trend()));
    }
  }
  BandwidthUsage usage = trendline_.State();
  if (usage == BandwidthUsage::kOverusing && last_usage_ != BandwidthUsage::kOverusing) {
    ++stats_.overuse_signals;
    SM_EVENT(cfg_.events, now, "gcc", "overuse", trendline_.modified_trend(), aimd_.estimate().bps());
  }
  if (usage == BandwidthUsage::kUnderusing) ++stats_.underuse_signals;
  last_usage_ = usage;
  if (probe_estimate && usage != BandwidthUsage::kOverusing) {
    // A successful probe jumps the estimate up directly.
    if (*probe_estimate > aimd_.estimate()) {
      aimd_.SetEstimate(*probe_estimate, now);
      SM_EVENT(cfg_.events, now, "gcc", "probe_jump", probe_estimate->bps(), 0);
      return aimd_.estimate();
    }
  }
  DataRate before = aimd_.estimate();
  DataRate r = aimd_.Update(usage, acked, now);
  if (r < before) {
    ++stats_.decreases;
    SM_EVENT(cfg_.events, now, "gcc", "decrease", r.bps(), before.bps());
  }
  return r;
}

void DelayBasedBwe::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("estimate_kbps", aimd_.estimate().kbps());
  w.Gauge("trend", trendline_.trend());
  w.Gauge("threshold", trendline_.threshold());
  w.Gauge("usage", static_cast<double>(trendline_.State()));
  w.Counter("feedbacks", stats_.feedbacks);
  w.Counter("overuse_signals", stats_.overuse_signals);
  w.Counter("underuse_signals", stats_.underuse_signals);
  w.Counter("decreases", stats_.decreases);
  w.Hist("abs_modified_trend", stats_.trend);
}

}  // namespace sm::cc
