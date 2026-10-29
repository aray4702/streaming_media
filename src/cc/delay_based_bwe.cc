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

// --- DelayBasedBwe -----------------------------------------------------------------

DelayBasedBwe::DelayBasedBwe(Config cfg) : cfg_(cfg), trendline_(cfg.trendline) { rc2_.SetMinBitrate(cfg.min_rate); }

void DelayBasedBwe::SetStartBitrate(DataRate r) { rc2_.SetStartBitrate(r); }

void DelayBasedBwe::SetRtt(TimeDelta rtt) { rc2_.SetRtt(rtt); }

void DelayBasedBwe::SetMinRate(DataRate r) { rc2_.SetMinBitrate(r); }

// libwebrtc DelayBasedBwe::MaybeUpdateEstimate.
DataRate DelayBasedBwe::MaybeUpdateEstimate(std::optional<DataRate> acked, std::optional<DataRate> probe, Timestamp now) {
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
      rc2_.SetEstimate(rc2_.LatestEstimate() / 2, now);  // delay_based_bwe.cc:231, integer division
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
  // "Very late feedback received": libwebrtc returns an empty Result without
  // running MaybeUpdateEstimate when no packet in the feedback was received.
  bool any_received = false;
  for (const auto& r : results) any_received |= r.received();
  if (!any_received) {
    last_result_ = {};
    return rc2_.LatestEstimate();
  }
  IncomingPacketFeedback(results, now);
  BandwidthUsage usage = trendline_.State();
  if (usage == BandwidthUsage::kOverusing && last_usage_ != BandwidthUsage::kOverusing) {
    ++stats_.overuse_signals;
    SM_EVENT(cfg_.events, now, "gcc", "overuse", trendline_.modified_trend(), estimate().bps());
  }
  if (usage == BandwidthUsage::kUnderusing) ++stats_.underuse_signals;
  last_usage_ = usage;
  return MaybeUpdateEstimate(acked, probe_estimate, now);
}

// libwebrtc DelayBasedBwe::IncomingPacketFeedbackVector / IncomingPacketFeedback.
void DelayBasedBwe::IncomingPacketFeedback(const std::vector<PacketResult>& results, Timestamp now) {
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
    if (cfg_.trace_detector) {
      DetectorSample x;
      x.recv_delta_ms = d->arrival_delta.ms_f();
      x.send_delta_ms = d->send_delta.ms_f();
      x.send_ms = std::round(r.send_time.us() / 1000.0);
      x.arrival_ms = std::round(r.receive_time.us() / 1000.0);
      x.modified_trend = trendline_.modified_trend();
      x.threshold = trendline_.threshold();
      x.state = trendline_.State();
      x.num_deltas = trendline_.num_deltas();
      detector_trace_.push_back(x);
    }
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
