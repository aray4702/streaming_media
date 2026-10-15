// 5.8 Encoder rate overshoot control.
//
// Real encoders miss their target: scene changes and keyframes overshoot,
// and some hardware encoders run persistently hot. Two complementary tools:
//
// FrameDropper (leaky bucket on encoded bytes): every encoded frame adds its
// size; the bucket leaks target/fps bytes per frame. When the bucket holds
// more than `window` worth of the target rate, the next frame is dropped
// before encoding. Keyframe bytes are spread over the following frames so a
// single keyframe does not cause a run of drops.
//
// EncoderOvershootDetector: measures actual/target utilization over a
// sliding window and returns an adjusted target (target / utilization) to
// feed back into the encoder's rate control, removing systematic overshoot.
#pragma once

#include <deque>

#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::cc {

struct FrameDropperConfig {
  TimeDelta window = TimeDelta::Millis(500);  // Allowed accumulated overshoot.
  int keyframe_spread_frames = 15;
  // The bucket and the pending keyframe bytes never hold more than this much
  // of the target rate (libwebrtc FrameDropper::CapAccumulator caps at
  // kAccumulatorCapBufferSizeSecs = 3 s; 1.5 s keeps the stall after a run of
  // keyframes short on slow links). Keyframes bypass ShouldDrop() but their
  // bytes enter the bucket: without a cap, a run of keyframe requests keeps it
  // above its limit and every delta frame is dropped for seconds.
  TimeDelta max_hold = TimeDelta::Millis(1500);
};

class FrameDropper : public MetricsSource {
 public:
  explicit FrameDropper(FrameDropperConfig cfg = {}) : cfg_(cfg) {}
  void SetRates(DataRate target, double fps);
  // Before encoding: should this frame be skipped?
  bool ShouldDrop();
  // After encoding.
  void OnEncodedFrame(DataSize size, bool keyframe);
  double bucket_bytes() const { return bucket_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  FrameDropperConfig cfg_;
  void CapBucket();
  double target_bytes_per_frame_ = 0;
  double target_bps_ = 0;
  double max_bucket_ = 0;
  double bucket_ = 0;
  double keyframe_debt_ = 0;
  double keyframe_leak_ = 0;
  uint64_t frames_ = 0, dropped_ = 0;
};

class EncoderOvershootDetector : public MetricsSource {
 public:
  explicit EncoderOvershootDetector(TimeDelta window = TimeDelta::Seconds(2)) : window_(window) {}
  void SetTarget(DataRate target) { target_ = target; }
  void OnEncodedFrame(DataSize size, Timestamp now);
  // Actual / target over the window (1.0 = on target).
  double Utilization(Timestamp now) const;
  // Target to configure the encoder with to land on SetTarget().
  DataRate AdjustedTarget(Timestamp now) const;
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  TimeDelta window_;
  DataRate target_;
  std::deque<std::pair<Timestamp, int64_t>> frames_;
  Timestamp last_now_ = Timestamp::Micros(0);
};

}  // namespace sm::cc
