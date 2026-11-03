// libwebrtc's BitrateAdjuster (common_video/bitrate_adjuster.cc), which
// RTCVideoEncoderH264 (sdk/objc/components/video_codec/) uses to correct the
// rate a hardware encoder is given for how much it actually produces
// (issue #1104). Neither file is in compare/webrtc/reference/ yet; this port
// follows upstream M157 and is to be re-checked against them once added.
//
// The encoder is given GetAdjustedBitrateBps(). SetTargetBitrateBps resets the
// adjusted rate to the new target when the target moved by more than 10%
// (kBitrateTolerancePct) from the last one, or from the target last adjusted
// against. Update(frame_size, now) feeds each encoded frame into a 1.5 s
// BitrateTracker; at most once per second and per 30 frames, when the encoder
// overshot the target, or undershot it by more than 10%, the adjusted rate
// becomes target + (target - estimate) / 2, clamped to
// [min_pct, max_pct] * target. RTCVideoEncoderH264 uses min 0.5, max 0.95.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "sm/core/rate_statistics.h"
#include "sm/core/units.h"

namespace sm::video {

class BitrateAdjuster {
 public:
  static constexpr int64_t kBitrateUpdateIntervalMs = 1000;
  static constexpr uint32_t kBitrateUpdateFrameInterval = 30;
  static constexpr float kBitrateTolerancePct = 0.1f;
  static constexpr float kBytesPerMsToBitsPerSecond = 8 * 1000;

  BitrateAdjuster(float min_adjusted_bitrate_pct, float max_adjusted_bitrate_pct);

  void SetTargetBitrateBps(uint32_t bitrate_bps);
  uint32_t GetTargetBitrateBps() const { return target_bitrate_bps_; }
  // The rate to give the encoder.
  uint32_t GetAdjustedBitrateBps() const { return adjusted_bitrate_bps_; }
  // The encoder's output rate over the last 1.5 s.
  std::optional<uint32_t> GetEstimatedBitrateBps(Timestamp now);
  // One encoded frame of `frame_size` bytes.
  void Update(size_t frame_size, Timestamp now);

  float GetMinAdjustedBitratePct() const { return min_adjusted_bitrate_pct_; }
  float GetMaxAdjustedBitratePct() const { return max_adjusted_bitrate_pct_; }

 private:
  bool IsWithinTolerance(uint32_t bitrate_bps, uint32_t target_bitrate_bps) const;
  void UpdateBitrate(int64_t now_ms);

  const float min_adjusted_bitrate_pct_;
  const float max_adjusted_bitrate_pct_;
  uint32_t target_bitrate_bps_ = 0;
  uint32_t adjusted_bitrate_bps_ = 0;
  uint32_t last_adjusted_target_bitrate_bps_ = 0;
  int64_t last_bitrate_update_time_ms_ = 0;
  uint32_t frames_since_last_update_ = 0;
  RateStatistics bitrate_tracker_{static_cast<int64_t>(1.5 * kBitrateUpdateIntervalMs), kBytesPerMsToBitsPerSecond};
};

}  // namespace sm::video
