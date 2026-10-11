#include "sm/timing/delay_manager.h"

#include <algorithm>
#include <cmath>

namespace sm::timing {

DelayManager::DelayManager(DelayManagerConfig cfg)
    : cfg_(cfg), hist_(static_cast<size_t>(cfg.max_delay_ms / cfg.bucket_ms) + 1, 0.0),
      target_ms_(cfg.initial_target_ms) {}

void DelayManager::Reset() {
  std::fill(hist_.begin(), hist_.end(), 0.0);
  transit_window_.clear();
  unwrapper_.Reset();
  target_ms_ = cfg_.initial_target_ms;
  updates_ = 0;
}

std::optional<double> DelayManager::Update(uint32_t rtp_ts, int sample_rate, Timestamp arrival) {
  int64_t ts = unwrapper_.Unwrap(rtp_ts);
  double transit_ms = static_cast<double>(arrival.us()) / 1000.0 - static_cast<double>(ts) * 1000.0 / sample_rate;
  while (!transit_window_.empty() && arrival - transit_window_.front().first > cfg_.history) {
    transit_window_.pop_front();
  }
  // Monotonic deque keeps the minimum transit at the front.
  while (!transit_window_.empty() && transit_window_.back().second >= transit_ms) transit_window_.pop_back();
  transit_window_.emplace_back(arrival, transit_ms);
  double rel = std::max(0.0, transit_ms - transit_window_.front().second);

  for (double& h : hist_) h *= cfg_.forget_factor;
  size_t b = std::min(hist_.size() - 1, static_cast<size_t>(rel / cfg_.bucket_ms));
  hist_[b] += 1.0 - cfg_.forget_factor;
  ++updates_;
  relative_delay_ms_.Add(rel);
  Recompute();
  return rel;
}

void DelayManager::Recompute() {
  double total = 0;
  for (double h : hist_) total += h;
  if (total <= 0) return;
  double acc = 0;
  size_t idx = hist_.size() - 1;
  for (size_t i = 0; i < hist_.size(); ++i) {
    acc += hist_[i];
    if (acc >= cfg_.quantile * total) {
      idx = i;
      break;
    }
  }
  int target = static_cast<int>((idx + 1) * static_cast<size_t>(cfg_.bucket_ms));
  // At least one packet must be buffered, and round to whole packets.
  target = std::max(target, packet_ms_);
  target = ((target + packet_ms_ - 1) / packet_ms_) * packet_ms_;
  target_ms_ = std::clamp(target, std::max(cfg_.min_delay_ms, packet_ms_), cfg_.max_delay_ms);
}

void DelayManager::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("target_delay_ms", target_ms_);
  w.Counter("updates", updates_);
  w.Hist("relative_delay_ms", relative_delay_ms_);
}

}  // namespace sm::timing
