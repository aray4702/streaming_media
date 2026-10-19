#include "sm/cc/delay_based_bwe.h"

#include <algorithm>
#include <cmath>

namespace sm::cc {

const char* ToString(BandwidthUsage u) {
  switch (u) {
    case BandwidthUsage::kNormal: return "normal";
    case BandwidthUsage::kUnderusing: return "underusing";
    case BandwidthUsage::kOverusing: return "overusing";
  }
  return "?";
}

std::vector<PacketResult> SortedByReceiveTime(const std::vector<PacketResult>& results) {
  std::vector<PacketResult> sorted;
  for (const auto& r : results)
    if (r.received()) sorted.push_back(r);
  std::sort(sorted.begin(), sorted.end(), [](const PacketResult& a, const PacketResult& b) {
    if (a.receive_time != b.receive_time) return a.receive_time < b.receive_time;
    if (a.send_time != b.send_time) return a.send_time < b.send_time;
    return a.seq < b.seq;
  });
  return sorted;
}

// --- InterArrival ----------------------------------------------------------------

std::optional<InterArrival::Deltas> InterArrival::OnPacket(Timestamp send_time, Timestamp arrival, DataSize size) {
  std::optional<Deltas> out;
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

// --- InterArrivalDelta ------------------------------------------------------------------

namespace {
constexpr TimeDelta kBurstDeltaThreshold = TimeDelta::Millis(5);
constexpr TimeDelta kMaxBurstDuration = TimeDelta::Millis(100);
}  // namespace

std::optional<InterArrivalDelta::Deltas> InterArrivalDelta::ComputeDeltas(Timestamp send_time, Timestamp arrival_time,
                                                                          Timestamp system_time, size_t packet_size) {
  std::optional<Deltas> out;
  if (current_.IsFirstPacket()) {
    // Not enough data yet: store it until two groups are complete.
    current_.send_time = send_time;
    current_.first_send_time = send_time;
    current_.first_arrival = arrival_time;
  } else if (current_.first_send_time > send_time) {
    return std::nullopt;  // Reordered packet.
  } else if (NewTimestampGroup(arrival_time, send_time)) {
    if (prev_.complete_time.IsFinite()) {
      Deltas d;
      d.send_delta = current_.send_time - prev_.send_time;
      d.arrival_delta = current_.complete_time - prev_.complete_time;
      TimeDelta system_time_delta = current_.last_system_time - prev_.last_system_time;
      if (d.arrival_delta - system_time_delta >= kArrivalTimeOffsetThreshold) {
        Reset();
        return std::nullopt;
      }
      if (d.arrival_delta < TimeDelta::Zero()) {
        // The group was reordered after its local arrival timestamp.
        ++num_consecutive_reordered_packets_;
        if (num_consecutive_reordered_packets_ >= kReorderedResetThreshold) Reset();
        return std::nullopt;
      }
      num_consecutive_reordered_packets_ = 0;
      d.size_delta = static_cast<int>(current_.size) - static_cast<int>(prev_.size);
      out = d;
    }
    prev_ = current_;
    current_.first_send_time = send_time;
    current_.send_time = send_time;
    current_.first_arrival = arrival_time;
    current_.size = 0;
  } else {
    current_.send_time = std::max(current_.send_time, send_time);
  }
  current_.size += packet_size;
  current_.complete_time = arrival_time;
  current_.last_system_time = system_time;
  return out;
}

bool InterArrivalDelta::NewTimestampGroup(Timestamp arrival_time, Timestamp send_time) const {
  if (current_.IsFirstPacket()) return false;
  if (BelongsToBurst(arrival_time, send_time)) return false;
  return send_time - current_.first_send_time > send_time_group_length_;
}

bool InterArrivalDelta::BelongsToBurst(Timestamp arrival_time, Timestamp send_time) const {
  TimeDelta arrival_time_delta = arrival_time - current_.complete_time;
  TimeDelta send_time_delta = send_time - current_.send_time;
  if (send_time_delta == TimeDelta::Zero()) return true;
  TimeDelta propagation_delta = arrival_time_delta - send_time_delta;
  return propagation_delta < TimeDelta::Zero() && arrival_time_delta <= kBurstDeltaThreshold &&
         arrival_time - current_.first_arrival < kMaxBurstDuration;
}

void InterArrivalDelta::Reset() {
  num_consecutive_reordered_packets_ = 0;
  current_ = SendTimeGroup();
  prev_ = SendTimeGroup();
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
  for (const auto& r : results) {
    if (!r.received()) continue;
    window_.emplace_back(r.receive_time, r.size.bytes());
  }
  if (window_.empty()) return;
  Timestamp newest = window_.back().first;
  // 500 ms sliding window of acknowledged bytes.
  while (window_.size() > 1 && newest - window_.front().first > TimeDelta::Millis(500)) window_.pop_front();
  TimeDelta span = newest - window_.front().first;
  if (span < TimeDelta::Millis(100)) return;
  int64_t bytes = 0;
  for (size_t i = 1; i < window_.size(); ++i) bytes += window_[i].second;
  DataRate r = DataRate::FromSizeAndTime(DataSize::Bytes(bytes), span);
  bitrate_ = bitrate_ ? DataRate::BitsPerSec(static_cast<int64_t>(0.7 * bitrate_->bps() + 0.3 * r.bps())) : r;
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

DataRate AimdRateController::Update(BandwidthUsage usage, std::optional<DataRate> acked, Timestamp now) {
  if (!last_change_.IsFinite()) last_change_ = now;
  // State machine (GCC paper, Fig. 2).
  switch (usage) {
    case BandwidthUsage::kNormal:
      if (state_ == State::kHold) state_ = State::kIncrease;
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
      // Near convergence: additive, ~one 1200 B packet per response time.
      double response_s = (rtt_ + TimeDelta::Millis(100)).seconds();
      inc_bps = std::max(1000.0, 1200 * 8 * dt.seconds() / response_s);
    } else {
      inc_bps = std::max(1000.0, current_.bps() * (std::pow(1 + cfg_.multiplicative_per_s, dt.seconds()) - 1));
    }
    next = current_ + DataRate::BitsPerSec(static_cast<int64_t>(inc_bps));
    // Do not run too far ahead of what the network actually delivers, but
    // never decrease as a side effect of that cap.
    if (acked) next = std::min(next, std::max(current_, *acked * 1.5 + DataRate::KilobitsPerSec(10)));
  } else if (state_ == State::kDecrease) {
    // Reduce at most once per RTT (clamped to [10, 200] ms) unless the
    // delivered rate has collapsed below half the estimate: the previous
    // decrease needs a round trip before its effect shows in the feedback.
    TimeDelta interval = Clamp(rtt_, TimeDelta::Millis(10), TimeDelta::Millis(200));
    bool collapsed = acked && *acked < current_ * 0.5;
    if (last_decrease_.IsFinite() && now - last_decrease_ < interval && !collapsed) {
      state_ = State::kHold;
      last_change_ = now;
      return current_;
    }
    last_decrease_ = now;
    DataRate base = acked ? *acked : current_;
    next = base * cfg_.beta;
    if (next > current_) next = current_ * cfg_.beta;  // Never increase on overuse.
    if (acked) {
      double a = acked->kbps();
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

DelayBasedBwe::DelayBasedBwe(Config cfg) : cfg_(cfg), trendline_(cfg.trendline), aimd_(cfg.aimd) {
  if (cfg.version >= 2) rc2_.SetMinBitrate(cfg.aimd.min_rate);
}

void DelayBasedBwe::SetStartBitrate(DataRate r, Timestamp now) {
  if (cfg_.version >= 2) {
    rc2_.SetStartBitrate(r);
  } else {
    aimd_.SetEstimate(r, now);
  }
}

void DelayBasedBwe::SetRtt(TimeDelta rtt) {
  aimd_.SetRtt(rtt);
  rc2_.SetRtt(rtt);
}

void DelayBasedBwe::SetMinRate(DataRate r) {
  aimd_.SetMinRate(r);
  if (cfg_.version >= 2) rc2_.SetMinBitrate(r);
}

DataRate DelayBasedBwe::MaybeUpdateEstimateV2(std::optional<DataRate> acked, std::optional<DataRate> probe, Timestamp now) {
  const BandwidthUsage usage = trendline_.State();
  DataRate before = rc2_.LatestEstimate();
  last_result_ = {};
  if (usage == BandwidthUsage::kOverusing) {
    if (acked && rc2_.TimeToReduceFurther(now, *acked)) {
      rc2_.Update(usage, acked, now);
      last_result_.updated = rc2_.ValidEstimate();  // UpdateEstimate returns ValidEstimate().
    } else if (!acked && rc2_.ValidEstimate() && rc2_.InitialTimeToReduceFurther(now)) {
      last_result_.updated = true;
      // Overusing before there is an acknowledged rate: halve every
      // clamp(rtt, 10, 200) ms.
      rc2_.SetEstimate(rc2_.LatestEstimate() * 0.5, now);
      SM_EVENT(cfg_.events, now, "gcc", "halve_no_acked", rc2_.LatestEstimate().bps(), before.bps());
    }
  } else if (probe) {
    rc2_.SetEstimate(*probe, now);
    last_result_.updated = true;
    last_result_.probe = true;
    SM_EVENT(cfg_.events, now, "gcc", "probe_jump", probe->bps(), 0);
  } else {
    rc2_.Update(usage, acked, now);
    last_result_.updated = rc2_.ValidEstimate();
    last_result_.recovered_from_overuse = recovered_from_overuse_;
  }
  DataRate r = rc2_.LatestEstimate();
  if (r < before) {
    ++stats_.decreases;
    SM_EVENT(cfg_.events, now, "gcc", "decrease", r.bps(), before.bps());
  }
  return r;
}

DataRate DelayBasedBwe::OnPacketResults(const std::vector<PacketResult>& results, std::optional<DataRate> acked,
                                        std::optional<DataRate> probe_estimate, Timestamp now) {
  ++stats_.feedbacks;
  if (cfg_.version >= 2) {
    // "Very late feedback received": libwebrtc returns an empty Result without
    // running MaybeUpdateEstimate when no packet in the feedback was received.
    bool any_received = false;
    for (const auto& r : results) any_received |= r.received();
    if (!any_received) {
      last_result_ = {};
      return rc2_.LatestEstimate();
    }
    OnPacketResultsV2(results, now);
  } else {
    for (const auto& r : results) {
      if (!r.received()) continue;
      if (auto d = inter_arrival_.OnPacket(r.send_time, r.receive_time, r.size)) {
        trendline_.Update(d->recv_delta.ms_f(), d->send_delta.ms_f(), r.receive_time.us() / 1000.0);
        stats_.trend.Add(std::fabs(trendline_.modified_trend()));
      }
    }
  }
  BandwidthUsage usage = trendline_.State();
  if (usage == BandwidthUsage::kOverusing && last_usage_ != BandwidthUsage::kOverusing) {
    ++stats_.overuse_signals;
    SM_EVENT(cfg_.events, now, "gcc", "overuse", trendline_.modified_trend(), estimate().bps());
  }
  if (usage == BandwidthUsage::kUnderusing) ++stats_.underuse_signals;
  last_usage_ = usage;
  if (cfg_.version >= 2) return MaybeUpdateEstimateV2(acked, probe_estimate, now);
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

// libwebrtc DelayBasedBwe::IncomingPacketFeedbackVector / IncomingPacketFeedback.
void DelayBasedBwe::OnPacketResultsV2(const std::vector<PacketResult>& results, Timestamp now) {
  constexpr TimeDelta kStreamTimeOut = TimeDelta::Seconds(2);
  constexpr TimeDelta kSendTimeGroupLength = TimeDelta::Millis(5);
  // TransportPacketsFeedback::SortedByReceiveTime (PacketResult::ReceiveTimeOrder).
  std::vector<PacketResult> sorted = SortedByReceiveTime(results);
  if (sorted.empty()) return;  // "Very late feedback received."
  recovered_from_overuse_ = false;
  BandwidthUsage prev_detector_state = trendline_.State();
  for (const auto& r : sorted) {
    // Reset if the stream has timed out.
    if (!last_seen_packet_.IsFinite() || now - last_seen_packet_ > kStreamTimeOut) {
      if (last_seen_packet_.IsFinite()) ++stats_.stream_resets;
      inter_arrival_delta_ = InterArrivalDelta(kSendTimeGroupLength);
      trendline_ = TrendlineEstimator(cfg_.trendline);
    }
    last_seen_packet_ = now;
    auto d = inter_arrival_delta_.ComputeDeltas(r.send_time, r.receive_time, now, static_cast<size_t>(r.size.bytes()));
    if (!d) continue;
    // libwebrtc passes integer milliseconds as the arrival time.
    trendline_.Update(d->arrival_delta.ms_f(), d->send_delta.ms_f(), std::round(r.receive_time.us() / 1000.0));
    stats_.trend.Add(std::fabs(trendline_.modified_trend()));
    if (prev_detector_state == BandwidthUsage::kUnderusing && trendline_.State() == BandwidthUsage::kNormal) {
      recovered_from_overuse_ = true;
    }
    prev_detector_state = trendline_.State();
  }
}

void DelayBasedBwe::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("estimate_kbps", estimate().kbps());
  w.Gauge("trend", trendline_.trend());
  w.Gauge("threshold", trendline_.threshold());
  w.Gauge("usage", static_cast<double>(trendline_.State()));
  w.Counter("feedbacks", stats_.feedbacks);
  w.Counter("overuse_signals", stats_.overuse_signals);
  w.Counter("underuse_signals", stats_.underuse_signals);
  w.Counter("decreases", stats_.decreases);
  w.Counter("stream_resets", stats_.stream_resets);
  w.Hist("abs_modified_trend", stats_.trend);
}

}  // namespace sm::cc
