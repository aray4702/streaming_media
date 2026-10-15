#include "sm/cc/loss_based_bwe.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sm::cc {

DataRate LossBasedBwe::OnPacketResults(const std::vector<PacketResult>& results, DataRate current_target,
                                       Timestamp now) {
  if (!window_start_.IsFinite()) window_start_ = now;
  for (const auto& r : results) {
    ++total_;
    if (!r.received()) ++lost_;
  }
  if (total_ < cfg_.min_packets || now - window_start_ < cfg_.min_window) return rate_;
  last_loss_ = static_cast<double>(lost_) / static_cast<double>(total_);
  lost_ = total_ = 0;
  window_start_ = now;
  ++windows_;
  if (last_loss_ < cfg_.low_loss) {
    rate_ = cfg_.max_rate;  // Not limiting.
  } else if (last_loss_ <= cfg_.high_loss) {
    rate_ = current_target;  // Hold.
    ++holds_;
  } else if (!last_decrease_.IsFinite() || now - last_decrease_ >= cfg_.decrease_interval + rtt_) {
    rate_ = current_target * (1.0 - 0.5 * last_loss_);
    last_decrease_ = now;
    ++decreases_;
    SM_EVENT(cfg_.events, now, "loss_bwe", "decrease", last_loss_, current_target.bps());
  }
  rate_ = Clamp(rate_, cfg_.min_rate, cfg_.max_rate);
  return rate_;
}

void LossBasedBwe::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("limit_kbps", rate_.kbps());
  w.Gauge("limiting", limiting() ? 1 : 0);
  w.Gauge("loss", last_loss_);
  w.Counter("windows", windows_);
  w.Counter("holds", holds_);
  w.Counter("decreases", decreases_);
}

}  // namespace sm::cc

// --- v2: inherent-loss model ------------------------------------------------------------

namespace sm::cc {

namespace {
double KiloBytes(DataSize s) { return static_cast<double>(s.bytes()) / 1000.0; }
double LossRatio(const LossBasedBweV2::Observation& o) {
  return o.size.bytes() > 0 ? static_cast<double>(o.lost_size.bytes()) / static_cast<double>(o.size.bytes()) : 0.0;
}
}  // namespace

LossBasedBweV2::LossBasedBweV2(LossBweV2Config cfg)
    : cfg_(cfg), inherent_(cfg.initial_inherent_loss), limit_(cfg.max_rate), hold_duration_(cfg.initial_hold) {
  cfg_.window = std::max(cfg_.window, 2);
  obs_.resize(static_cast<size_t>(cfg_.window));
}

void LossBasedBweV2::Reset() {
  for (auto& o : obs_) o = Observation{};
  num_obs_ = 0;
  part_packets_ = part_lost_ = 0;
  part_size_ = part_lost_size_ = DataSize();
  last_obs_send_ = Timestamp::PlusInfinity();
  acked_.reset();
  delay_based_ = DataRate::PlusInfinity();
  inherent_ = cfg_.initial_inherent_loss;
  bandwidth_ = DataRate::PlusInfinity();
  limit_ = cfg_.max_rate;
  avg_loss_ = 0;
  loss_limited_ = false;
  cause_ = LossLimitCause::kNone;
  hold_until_ = Timestamp::MinusInfinity();
  hold_duration_ = cfg_.initial_hold;
  hold_rate_ = DataRate::PlusInfinity();
  policer_rate_.reset();
  policer_until_ = Timestamp::MinusInfinity();
}

double LossBasedBweV2::LossProbability(double inherent, DataRate bandwidth, DataRate sending_rate) {
  inherent = std::clamp(inherent, 0.0, 1.0);
  double p = inherent;
  if (bandwidth.IsFinite() && sending_rate.IsFinite() && sending_rate > bandwidth && sending_rate.bps() > 0) {
    p += (1.0 - inherent) * ((sending_rate - bandwidth) / sending_rate);
  }
  return std::clamp(p, 1.0e-6, 1.0 - 1.0e-6);
}

double LossBasedBweV2::Weight(const Observation& o) const {
  return std::pow(cfg_.temporal_weight, static_cast<double>(num_obs_ - 1 - o.id));
}

double LossBasedBweV2::FeasibleInherent(double inherent, DataRate bandwidth) const {
  double upper = 1.0;
  if (bandwidth.IsFinite() && bandwidth.bps() > 0) {
    upper = std::min(1.0, cfg_.inherent_loss_upper_bound_offset + cfg_.inherent_loss_upper_bound_balance / bandwidth);
  }
  return std::clamp(inherent, cfg_.inherent_loss_lower_bound, std::max(upper, cfg_.inherent_loss_lower_bound));
}

double LossBasedBweV2::Objective(double inherent, DataRate bandwidth) const {
  // Weighted log-likelihood of the observed byte losses, plus a bias toward
  // higher bandwidth while the average loss is below the preference
  // threshold (it turns into a penalty above it).
  auto adjust = [&](double factor) {
    double d = cfg_.high_bw_preference_loss - avg_loss_;
    return factor * d / (cfg_.bw_preference_smoothing + std::fabs(d));
  };
  const double bias = adjust(cfg_.higher_bw_bias) * bandwidth.kbps() + adjust(cfg_.higher_log_bw_bias) * std::log(1.0 + bandwidth.kbps());
  double objective = 0;
  for (const auto& o : obs_) {
    if (o.id < 0) continue;
    const double p = LossProbability(inherent, bandwidth, o.sending_rate);
    const double w = Weight(o);
    objective += w * (KiloBytes(o.lost_size) * std::log(p) + KiloBytes(o.size - o.lost_size) * std::log(1.0 - p));
    objective += w * bias * KiloBytes(o.size);
  }
  return objective;
}

double LossBasedBweV2::NewtonStep(double inherent, DataRate bandwidth) const {
  double first = 0, second = 0;
  for (const auto& o : obs_) {
    if (o.id < 0) continue;
    const double p = LossProbability(inherent, bandwidth, o.sending_rate);
    const double w = Weight(o);
    const double lost = KiloBytes(o.lost_size), got = KiloBytes(o.size - o.lost_size);
    first += w * (lost / p - got / (1.0 - p));
    second -= w * (lost / (p * p) + got / ((1.0 - p) * (1.0 - p)));
  }
  if (second >= 0) second = -1.0e-6;
  return FeasibleInherent(inherent - cfg_.newton_step * first / second, bandwidth);
}

bool LossBasedBweV2::PushObservation(const std::vector<PacketResult>& results) {
  if (results.empty()) return false;
  Timestamp first_send = Timestamp::PlusInfinity(), last_send = Timestamp::MinusInfinity();
  for (const auto& r : results) {
    ++part_packets_;
    part_size_ += r.size;
    if (!r.received()) {
      ++part_lost_;
      part_lost_size_ += r.size;
    }
    first_send = std::min(first_send, r.send_time);
    last_send = std::max(last_send, r.send_time);
  }
  if (!last_obs_send_.IsFinite()) last_obs_send_ = first_send;
  const TimeDelta duration = last_send - last_obs_send_;
  if (duration <= TimeDelta::Zero() || duration < cfg_.observation_min_duration ||
      part_packets_ < cfg_.observation_min_packets) {
    return false;
  }
  Observation o;
  o.packets = part_packets_;
  o.lost = part_lost_;
  o.size = part_size_;
  o.lost_size = part_lost_size_;
  o.sending_rate = DataRate::FromSizeAndTime(part_size_, duration);
  o.delivered_rate = DataRate::FromSizeAndTime(part_size_ - part_lost_size_, duration);
  o.id = num_obs_++;
  obs_[static_cast<size_t>(o.id % cfg_.window)] = o;
  last_obs_send_ = last_send;
  part_packets_ = part_lost_ = 0;
  part_size_ = part_lost_size_ = DataSize();
  ++observations_;
  return true;
}

std::optional<DataRate> LossBasedBweV2::DetectPolicer() const {
  const int n = cfg_.policer_observations;
  if (num_obs_ < n || n > cfg_.window || n < 2) return std::nullopt;
  double sum_delivered = 0, sum_sent = 0, min_sent = 1e18, max_sent = 0;
  std::vector<double> delivered;
  for (int64_t id = num_obs_ - n; id < num_obs_; ++id) {
    const Observation& o = obs_[static_cast<size_t>(id % cfg_.window)];
    if (o.id != id || LossRatio(o) < inherent_ + cfg_.policer_excess_loss) return std::nullopt;
    delivered.push_back(static_cast<double>(o.delivered_rate.bps()));
    sum_delivered += delivered.back();
    const double sent = static_cast<double>(o.sending_rate.bps());
    sum_sent += sent;
    min_sent = std::min(min_sent, sent);
    max_sent = std::max(max_sent, sent);
  }
  const double mean = sum_delivered / n;
  if (mean <= 0) return std::nullopt;
  for (double d : delivered) {
    if (std::fabs(d - mean) > cfg_.policer_rate_tolerance * mean) return std::nullopt;
  }
  // The sender pushed more (or varied its rate) and delivery did not follow.
  const bool pushed = sum_sent / n >= cfg_.policer_send_over_delivered * mean ||
                      max_sent >= cfg_.policer_send_over_delivered * min_sent;
  if (!pushed) return std::nullopt;
  return DataRate::BitsPerSec(static_cast<int64_t>(mean));
}

DataRate LossBasedBweV2::OnPacketResults(const std::vector<PacketResult>& results, Timestamp now) {
  if (PushObservation(results)) Update(now);
  return limit_;
}

void LossBasedBweV2::Update(Timestamp now) {
  // Average byte loss over the window (newest weighted most); with more than
  // three observations the lowest- and highest-loss ones are left out, so a
  // single burst does not read as a loss level, unless the highest-loss
  // observation was sent at twice the median rate (then it is real).
  {
    double total = 0, lost = 0, min_r = 2, max_r = -1;
    double min_total = 0, min_lost = 0, max_total = 0, max_lost = 0;
    DataRate max_rate_obs;
    std::vector<int64_t> rates;
    for (const auto& o : obs_) {
      if (o.id < 0) continue;
      const double w = Weight(o);
      const double t = w * static_cast<double>(o.size.bytes()), l = w * static_cast<double>(o.lost_size.bytes());
      total += t;
      lost += l;
      rates.push_back(o.sending_rate.bps());
      const double r = LossRatio(o);
      if (r > max_r) { max_r = r; max_total = t; max_lost = l; max_rate_obs = o.sending_rate; }
      if (r < min_r) { min_r = r; min_total = t; min_lost = l; }
    }
    std::sort(rates.begin(), rates.end());
    const double median = rates.empty() ? 0 : static_cast<double>(rates[rates.size() / 2]);
    const bool spike_is_real = median > 0 && static_cast<double>(max_rate_obs.bps()) >= 2.0 * median;
    if (num_obs_ > 3 && !spike_is_real && total - min_total - max_total > 0) {
      avg_loss_ = (lost - min_lost - max_lost) / (total - min_total - max_total);
    } else {
      avg_loss_ = total > 0 ? lost / total : 0;
    }
  }

  // The model limits the total sending rate (media + repair); when the sender
  // puts more on the wire than its target (RTX/FEC), scale the limit down by
  // the same ratio so the total, not just the target, fits.
  double overhead = 1.0;
  if (target_.IsFinite() && target_.bps() > 0) {
    const Observation& last = obs_[static_cast<size_t>((num_obs_ - 1) % cfg_.window)];
    overhead = std::clamp(last.sending_rate / target_, 1.0, 3.0);
  }
  // B is a total sending rate; the delay-based estimate is a target.
  const DataRate delay_total = delay_based_.IsFinite() ? delay_based_ * overhead : delay_based_;
  if (!bandwidth_.IsFinite()) {
    if (!delay_total.IsFinite()) return;
    bandwidth_ = delay_total;
  }

  // Candidate bandwidths; each gets one Newton step on the inherent loss.
  std::vector<DataRate> candidates;
  for (double f : cfg_.candidate_factors) candidates.push_back(bandwidth_ * f);
  if (acked_) candidates.push_back(*acked_);
  if (delay_total.IsFinite() && delay_total > bandwidth_) candidates.push_back(delay_total);
  const DataRate upper = loss_limited_ ? std::max(bandwidth_, bandwidth_ * 1.3) : cfg_.max_rate;
  double best_obj = -std::numeric_limits<double>::infinity();
  DataRate best_bw = bandwidth_;
  double best_q = inherent_;
  for (DataRate c : candidates) {
    c = std::min(c, std::max(bandwidth_, upper));
    double q = NewtonStep(FeasibleInherent(inherent_, c), c);
    double obj = Objective(q, c);
    if (obj > best_obj || (obj == best_obj && c > best_bw)) {
      best_obj = obj;
      best_bw = c;
      best_q = q;
    }
  }
  // Do not raise B while the measured loss clearly exceeds the inherent
  // estimate. (libwebrtc compares without a margin; the damped Newton step
  // keeps q just under a steady loss rate, which would block every increase.)
  if (avg_loss_ > best_q * cfg_.not_increase_loss_ratio + cfg_.not_increase_loss_margin && best_bw > bandwidth_) {
    best_bw = bandwidth_;
  }
  // While loss-limited, ramp up no faster than a multiple of what is delivered.
  if (loss_limited_ && best_bw > bandwidth_ && acked_) {
    const bool in_hold = hold_rate_.IsFinite() && *acked_ < hold_rate_ * 1.3;
    const double factor = in_hold ? cfg_.rampup_factor_in_hold : cfg_.rampup_factor;
    best_bw = std::max(bandwidth_, std::min(best_bw, *acked_ * factor));
  }
  bandwidth_ = best_bw;
  inherent_ = best_q;

  // Bounds: never below what is being delivered, below 100 kbps / (loss - 5%)
  // under heavy loss, and never above the delay-based estimate.
  const DataRate lower = std::max(acked_ ? std::min(*acked_, bandwidth_) * (1.0 / overhead) : DataRate::Zero(), cfg_.min_rate);
  DataRate instant_upper = cfg_.max_rate;
  if (avg_loss_ > cfg_.instant_upper_bound_loss_offset) {
    instant_upper = DataRate::BitsPerSec(static_cast<int64_t>(
        static_cast<double>(cfg_.instant_upper_bound_balance.bps()) / (avg_loss_ - cfg_.instant_upper_bound_loss_offset)));
  }
  DataRate bounded = std::min({bandwidth_ * (1.0 / overhead), instant_upper, delay_based_, cfg_.max_rate});
  LossLimitCause cause = bounded == instant_upper && instant_upper < bandwidth_ ? LossLimitCause::kInstantBound
                                                                               : LossLimitCause::kLossModel;
  bounded = std::max(lower, bounded);
  // Keep B from drifting above what is usable (libwebrtc BoundBestCandidate).
  if (bounded * overhead < bandwidth_) bandwidth_ = bounded * overhead;

  // Policer: loss well above the inherent level at a flat delivered rate.
  if (auto rate = DetectPolicer()) {
    if (!policer_rate_) {
      ++policer_detections_;
      SM_EVENT(cfg_.events, now, "loss_bwe", "policer", rate->bps(), static_cast<int64_t>(inherent_ * 1e4));
    }
    policer_rate_ = rate;
    policer_until_ = now + cfg_.policer_hold;
  } else if (policer_rate_ && now >= policer_until_) {
    policer_rate_.reset();
  }
  if (policer_rate_ && *policer_rate_ < bounded) {
    bounded = std::max(*policer_rate_, cfg_.min_rate);
    cause = LossLimitCause::kPolicer;
  }

  const bool limited = num_obs_ >= cfg_.min_observations && bounded < delay_based_ && bounded < cfg_.max_rate;
  DataRate next = limited ? bounded : cfg_.max_rate;
  if (limited) {
    if (next < limit_) {
      // A new decrease: hold the reduced rate for a while (300 ms, doubling).
      if (now >= hold_until_) {
        hold_until_ = now + hold_duration_;
        hold_duration_ = std::min(cfg_.max_hold, hold_duration_ * 2.0);
        hold_rate_ = next;
      }
      ++decreases_;
      SM_EVENT(cfg_.events, now, "loss_bwe", "v2_decrease", next.bps(), static_cast<int64_t>(cause));
    } else if (now < hold_until_) {
      next = std::min(next, hold_rate_);
    }
  } else {
    hold_duration_ = cfg_.initial_hold;
    hold_rate_ = DataRate::PlusInfinity();
    hold_until_ = Timestamp::MinusInfinity();
  }
  if (limited != loss_limited_ || (limited && cause != cause_)) {
    SM_EVENT(cfg_.events, now, "loss_bwe", limited ? "v2_limit" : "v2_release", next.bps(), static_cast<int64_t>(cause));
  }
  loss_limited_ = limited;
  cause_ = limited ? cause : LossLimitCause::kNone;
  limit_ = Clamp(next, cfg_.min_rate, cfg_.max_rate);
}

void LossBasedBweV2::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("limit_kbps", limit_.kbps());
  w.Gauge("limiting", limiting() ? 1 : 0);
  w.Gauge("cause", static_cast<double>(cause_));
  w.Gauge("inherent_loss", inherent_);
  w.Gauge("average_loss", avg_loss_);
  w.Gauge("loss_limited_bw_kbps", loss_limited_bandwidth().kbps());
  w.Gauge("policer_kbps", policer_rate_ ? policer_rate_->kbps() : 0.0);
  w.Counter("observations", observations_);
  w.Counter("decreases", decreases_);
  w.Counter("policer_detections", policer_detections_);
}

}  // namespace sm::cc
