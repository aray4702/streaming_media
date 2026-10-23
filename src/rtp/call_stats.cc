#include "sm/rtp/call_stats.h"

#include <algorithm>

namespace sm::rtp {

void CallStats::EnsureStarted(Timestamp now) {
  if (next_update_.IsFinite()) return;
  next_update_ = now + kUpdateInterval;
}

// call_stats2.cc OnRttUpdate: the report is stamped with the time it arrives.
std::optional<CallStats::Update> CallStats::OnRttUpdate(int64_t rtt_ms, Timestamp now) {
  reports_.push_back({rtt_ms, now.ms()});
  return UpdateAndReport(now);
}

std::optional<CallStats::Update> CallStats::Process(Timestamp now) {
  std::optional<Update> last;
  while (next_update_.IsFinite() && now >= next_update_) {
    // The repeating task runs at its scheduled time.
    if (auto u = UpdateAndReport(next_update_)) last = u;
    next_update_ += kUpdateInterval;
  }
  return last;
}

// call_stats2.cc UpdateAndReport, with RemoveOldReports, GetMaxRttMs and
// GetNewAvgRttMs.
std::optional<CallStats::Update> CallStats::UpdateAndReport(Timestamp now) {
  const int64_t now_ms = now.ms();
  reports_.erase(std::remove_if(reports_.begin(), reports_.end(),
                                [now_ms](const RttTime& r) { return now_ms - r.time > kRttTimeoutMs; }),
                 reports_.end());
  max_rtt_ms_ = -1;
  for (const RttTime& r : reports_) max_rtt_ms_ = std::max(r.rtt, max_rtt_ms_);
  if (reports_.empty()) {
    avg_rtt_ms_ = -1;  // Reset (invalid average).
  } else {
    int64_t sum = 0;
    for (const RttTime& r : reports_) sum += r.rtt;
    const int64_t cur_rtt_ms = sum / static_cast<int64_t>(reports_.size());
    if (avg_rtt_ms_ == -1) {
      avg_rtt_ms_ = cur_rtt_ms;
    } else {
      // The old average weighs 70 % against the new one, in float as libwebrtc.
      constexpr float kWeightFactor = 0.3f;
      avg_rtt_ms_ = static_cast<int64_t>(static_cast<float>(avg_rtt_ms_) * (1.0f - kWeightFactor) +
                                         static_cast<float>(cur_rtt_ms) * kWeightFactor);
    }
  }
  if (max_rtt_ms_ < 0) return std::nullopt;
  return Update{avg_rtt_ms_, max_rtt_ms_};
}

}  // namespace sm::rtp
