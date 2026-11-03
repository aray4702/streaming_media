#include "sm/video/bitrate_adjuster.h"

#include <algorithm>
#include <cmath>

namespace sm::video {

BitrateAdjuster::BitrateAdjuster(float min_adjusted_bitrate_pct, float max_adjusted_bitrate_pct)
    : min_adjusted_bitrate_pct_(min_adjusted_bitrate_pct), max_adjusted_bitrate_pct_(max_adjusted_bitrate_pct) {}

void BitrateAdjuster::SetTargetBitrateBps(uint32_t bitrate_bps) {
  // A large change in target is taken at once (bandwidth was likely gained or
  // lost); a small one waits for the next adjustment, unless small steps have
  // added up past the tolerance since the last adjustment.
  if (!IsWithinTolerance(bitrate_bps, target_bitrate_bps_) ||
      !IsWithinTolerance(bitrate_bps, last_adjusted_target_bitrate_bps_)) {
    adjusted_bitrate_bps_ = bitrate_bps;
    last_adjusted_target_bitrate_bps_ = bitrate_bps;
  }
  target_bitrate_bps_ = bitrate_bps;
}

std::optional<uint32_t> BitrateAdjuster::GetEstimatedBitrateBps(Timestamp now) {
  const auto rate = bitrate_tracker_.Rate(now.ms());
  if (!rate) return std::nullopt;
  return static_cast<uint32_t>(*rate);
}

void BitrateAdjuster::Update(size_t frame_size, Timestamp now) {
  const int64_t now_ms = now.ms();
  bitrate_tracker_.Update(static_cast<int64_t>(frame_size), now_ms);
  UpdateBitrate(now_ms);
}

bool BitrateAdjuster::IsWithinTolerance(uint32_t bitrate_bps, uint32_t target_bitrate_bps) const {
  if (target_bitrate_bps == 0) return false;
  const float delta = std::fabs(static_cast<float>(bitrate_bps) - static_cast<float>(target_bitrate_bps));
  const float delta_pct = delta / static_cast<float>(target_bitrate_bps);
  return delta_pct < kBitrateTolerancePct;
}

void BitrateAdjuster::UpdateBitrate(int64_t now_ms) {
  const int64_t elapsed_ms = now_ms - last_bitrate_update_time_ms_;
  ++frames_since_last_update_;
  if (elapsed_ms < kBitrateUpdateIntervalMs || frames_since_last_update_ < kBitrateUpdateFrameInterval) return;
  const float target_bitrate_bps = static_cast<float>(target_bitrate_bps_);
  const auto rate = bitrate_tracker_.Rate(now_ms);
  const float estimated_bitrate_bps = rate ? static_cast<float>(*rate) : target_bitrate_bps;
  const float error = target_bitrate_bps - estimated_bitrate_bps;
  // Adjust on any overshoot, or on an undershoot of more than the tolerance.
  if (estimated_bitrate_bps > target_bitrate_bps || error > kBitrateTolerancePct * target_bitrate_bps) {
    float adjusted_bitrate_bps = target_bitrate_bps + 0.5f * error;
    adjusted_bitrate_bps = std::max(adjusted_bitrate_bps, min_adjusted_bitrate_pct_ * target_bitrate_bps);
    adjusted_bitrate_bps = std::min(adjusted_bitrate_bps, max_adjusted_bitrate_pct_ * target_bitrate_bps);
    const auto adjusted = static_cast<uint32_t>(adjusted_bitrate_bps);
    if (adjusted != adjusted_bitrate_bps_) {
      adjusted_bitrate_bps_ = adjusted;
      last_adjusted_target_bitrate_bps_ = target_bitrate_bps_;
    }
  }
  last_bitrate_update_time_ms_ = now_ms;
  frames_since_last_update_ = 0;
  last_adjusted_target_bitrate_bps_ = target_bitrate_bps_;
}

}  // namespace sm::video
