// 5.8 Encoder rate overshoot control.
//
// Real encoders miss their target: scene changes and keyframes overshoot,
// and some hardware encoders run persistently hot. Two complementary tools:
//
// FrameDropper: a port of libwebrtc's
// modules/video_coding/utility/frame_dropper.{h,cc} (M157), with its constants
// and defaults. A leaky bucket (the accumulator, in kbits) is filled by every
// encoded frame and leaks target/fps per captured frame. Above half a second of
// the target rate it raises a smoothed drop ratio (ExpFilter) and DropFrame()
// spreads the drops over time, at most 4 s of frames in a row. Keyframes and
// unusually large delta frames (> 3x the running average) are not added at
// once but spread over 0.5 s of frames. The accumulator is capped at 3 s of the
// target rate, and rescaled by new/old target when the target drops
// (SetRates). The caller order per captured frame is as VideoStreamEncoder:
// Leak(fps), then DropFrame(); SetRates() on a rate update; Fill() after each
// encoded frame. Keyframes are not exempt, as in libwebrtc.
//
// The encoder's overshoot correction (EncoderOvershootDetector and
// EncoderBitrateAdjuster) is in encoder_bitrate_adjuster.h.
#pragma once

#include <cmath>
#include <cstdint>
#include <deque>

#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::cc {

// rtc_base/numerics/exp_filter.{h,cc}: filtered = a^exp * filtered + (1 - a^exp) * sample,
// seeded by the first sample, optionally capped at `max`. Not in
// compare/webrtc/reference (reported on #57); written from the upstream class.
class ExpFilter {
 public:
  static constexpr float kValueUndefined = -1.0f;
  explicit ExpFilter(float alpha, float max = kValueUndefined) : alpha_(alpha), max_(max) {}
  void Reset(float alpha) {
    alpha_ = alpha;
    filtered_ = kValueUndefined;
  }
  float Apply(float exp, float sample) {
    if (filtered_ == kValueUndefined) {
      filtered_ = sample;
    } else if (exp == 1.0f) {
      filtered_ = alpha_ * filtered_ + (1 - alpha_) * sample;
    } else {
      const float alpha = std::pow(alpha_, exp);
      filtered_ = alpha * filtered_ + (1 - alpha) * sample;
    }
    if (max_ != kValueUndefined && filtered_ > max_) filtered_ = max_;
    return filtered_;
  }
  void UpdateBase(float alpha) { alpha_ = alpha; }
  float filtered() const { return filtered_; }

 private:
  float alpha_;
  float filtered_ = kValueUndefined;
  float max_;
};

class FrameDropper : public MetricsSource {
 public:
  FrameDropper();
  // Back to the initial state (target 300 kbps, 30 fps).
  void Reset();
  // The caller's per-frame sequence, as VideoStreamEncoder::OnFrame:
  // Leak(fps) then DropFrame(). Returns true if the frame is to be dropped.
  bool ShouldDrop(double input_fps);
  void Leak(uint32_t input_framerate);
  bool DropFrame();
  // `target` is rounded to whole kbps as VideoStreamEncoder::OnBitrateUpdated does.
  void SetRates(DataRate target, double fps);
  // After encoding.
  void OnEncodedFrame(DataSize size, bool keyframe);
  double bucket_bytes() const { return accumulator_ * 1000.0 / 8.0; }
  double incoming_frame_rate() const { return incoming_frame_rate_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  void UpdateRatio();
  void CapAccumulator();

  ExpFilter key_frame_ratio_;
  ExpFilter delta_frame_size_avg_kbits_;
  float large_frame_accumulation_spread_ = 0;
  int large_frame_accumulation_count_ = 0;
  float large_frame_accumulation_chunk_size_ = 0;
  float accumulator_ = 0;
  float accumulator_max_ = 0;
  float target_bitrate_ = 0;  // kbps
  bool drop_next_ = false;
  ExpFilter drop_ratio_;
  int drop_count_ = 0;
  float incoming_frame_rate_ = 0;
  bool was_below_max_ = true;
  const float max_drop_duration_secs_ = 4.0f;
  uint64_t frames_ = 0, dropped_ = 0;
};

}  // namespace sm::cc
