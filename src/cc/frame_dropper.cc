#include "sm/cc/frame_dropper.h"

#include <algorithm>

namespace sm::cc {

namespace {
constexpr float kDefaultFrameSizeAlpha = 0.9f;
constexpr float kDefaultKeyFrameRatioAlpha = 0.99f;
// 1 key frame every 10th second in 30 fps.
constexpr float kDefaultKeyFrameRatioValue = 1 / 300.0f;
constexpr float kDefaultDropRatioAlpha = 0.9f;
constexpr float kDefaultDropRatioValue = 0.96f;
constexpr float kDefaultTargetBitrateKbps = 300.0f;
constexpr float kDefaultIncomingFrameRate = 30;
constexpr float kLeakyBucketSizeSeconds = 0.5f;
// A delta frame bigger than this many times the average delta frame is spread out.
constexpr int kLargeDeltaFactor = 3;
constexpr float kAccumulatorCapBufferSizeSecs = 3.0f;
}  // namespace

FrameDropper::FrameDropper()
    : key_frame_ratio_(kDefaultKeyFrameRatioAlpha),
      delta_frame_size_avg_kbits_(kDefaultFrameSizeAlpha),
      drop_ratio_(kDefaultDropRatioAlpha, kDefaultDropRatioValue) {
  Reset();
}

void FrameDropper::Reset() {
  key_frame_ratio_.Reset(kDefaultKeyFrameRatioAlpha);
  key_frame_ratio_.Apply(1.0f, kDefaultKeyFrameRatioValue);
  delta_frame_size_avg_kbits_.Reset(kDefaultFrameSizeAlpha);

  accumulator_ = 0.0f;
  accumulator_max_ = kDefaultTargetBitrateKbps / 2;
  target_bitrate_ = kDefaultTargetBitrateKbps;
  incoming_frame_rate_ = kDefaultIncomingFrameRate;

  large_frame_accumulation_count_ = 0;
  large_frame_accumulation_chunk_size_ = 0;
  large_frame_accumulation_spread_ = 0.5f * kDefaultIncomingFrameRate;

  drop_next_ = false;
  drop_ratio_.Reset(0.9f);
  drop_ratio_.Apply(0.0f, 0.0f);
  drop_count_ = 0;
  was_below_max_ = true;
}

void FrameDropper::OnEncodedFrame(DataSize size, bool keyframe) {
  if (size.bytes() <= 0) return;  // VideoStreamEncoder fills only non-empty frames.
  float framesize_kbits = 8.0f * static_cast<float>(size.bytes()) / 1000.0f;
  if (keyframe) {
    key_frame_ratio_.Apply(1.0, 1.0);
    // Do not spread if we are already doing it (or we risk dropping bits that
    // need accumulation).
    if (large_frame_accumulation_count_ == 0) {
      if (key_frame_ratio_.filtered() > 1e-5 &&
          1 / key_frame_ratio_.filtered() < large_frame_accumulation_spread_) {
        large_frame_accumulation_count_ = static_cast<int32_t>(1 / key_frame_ratio_.filtered() + 0.5);
      } else {
        large_frame_accumulation_count_ = static_cast<int32_t>(large_frame_accumulation_spread_ + 0.5);
      }
      large_frame_accumulation_chunk_size_ = framesize_kbits / large_frame_accumulation_count_;
      framesize_kbits = 0;
    }
  } else {
    // Spread an unusually large delta frame the same way.
    if (delta_frame_size_avg_kbits_.filtered() != -1 &&
        (framesize_kbits > kLargeDeltaFactor * delta_frame_size_avg_kbits_.filtered()) &&
        large_frame_accumulation_count_ == 0) {
      large_frame_accumulation_count_ = static_cast<int32_t>(large_frame_accumulation_spread_ + 0.5);
      large_frame_accumulation_chunk_size_ = framesize_kbits / large_frame_accumulation_count_;
      framesize_kbits = 0;
    } else {
      delta_frame_size_avg_kbits_.Apply(1, framesize_kbits);
    }
    key_frame_ratio_.Apply(1.0, 0.0);
  }
  accumulator_ += framesize_kbits;
  CapAccumulator();
}

void FrameDropper::Leak(uint32_t input_framerate) {
  if (input_framerate < 1) return;
  if (target_bitrate_ < 0.0f) return;
  // Lower bound for the large frame accumulation spread.
  large_frame_accumulation_spread_ = std::max(0.5f * static_cast<float>(input_framerate), 5.0f);
  float expected_bits_per_frame = target_bitrate_ / static_cast<float>(input_framerate);
  if (large_frame_accumulation_count_ > 0) {
    expected_bits_per_frame -= large_frame_accumulation_chunk_size_;
    --large_frame_accumulation_count_;
  }
  accumulator_ -= expected_bits_per_frame;
  if (accumulator_ < 0.0f) accumulator_ = 0.0f;
  UpdateRatio();
}

void FrameDropper::UpdateRatio() {
  if (accumulator_ > 1.3f * accumulator_max_) {
    drop_ratio_.UpdateBase(0.8f);  // Too far above the max: react faster.
  } else {
    drop_ratio_.UpdateBase(0.9f);
  }
  if (accumulator_ > accumulator_max_) {
    // Above the max: raise the drop ratio and drop a frame later.
    if (was_below_max_) drop_next_ = true;
    drop_ratio_.Apply(1.0f, 1.0f);
    drop_ratio_.UpdateBase(0.9f);
  } else {
    drop_ratio_.Apply(1.0f, 0.0f);
  }
  was_below_max_ = accumulator_ < accumulator_max_;
}

bool FrameDropper::ShouldDrop(double input_fps) {
  ++frames_;
  Leak(static_cast<uint32_t>(input_fps));
  const bool drop = DropFrame();
  if (drop) ++dropped_;
  return drop;
}

// Smooths the drops over time with drop_ratio_.
bool FrameDropper::DropFrame() {
  if (drop_next_) {
    drop_next_ = false;
    drop_count_ = 0;
  }
  if (drop_ratio_.filtered() >= 0.5f) {  // Drops per keep.
    float denom = 1.0f - drop_ratio_.filtered();
    if (denom < 1e-5f) denom = 1e-5f;
    int32_t limit = static_cast<int32_t>(1.0f / denom - 1.0f + 0.5f);
    // Bound the frames dropped between kept frames by the maximum drop duration.
    int max_limit = static_cast<int>(incoming_frame_rate_ * max_drop_duration_secs_);
    if (limit > max_limit) limit = max_limit;
    if (drop_count_ < 0) drop_count_ = -drop_count_;
    if (drop_count_ < limit) {
      drop_count_++;
      return true;
    }
    drop_count_ = 0;
    return false;
  } else if (drop_ratio_.filtered() > 0.0f && drop_ratio_.filtered() < 0.5f) {  // Keeps per drop.
    float denom = drop_ratio_.filtered();
    if (denom < 1e-5f) denom = 1e-5f;
    int32_t limit = -static_cast<int32_t>(1.0f / denom - 1.0f + 0.5f);
    if (drop_count_ > 0) drop_count_ = -drop_count_;
    if (drop_count_ > limit) {
      if (drop_count_ == 0) {
        drop_count_--;
        return true;
      }
      drop_count_--;
      return false;
    }
    drop_count_ = 0;
    return false;
  }
  drop_count_ = 0;
  return false;
}

void FrameDropper::SetRates(DataRate target, double fps) {
  const float bitrate = static_cast<float>((target.bps() + 500) / 1000);
  accumulator_max_ = bitrate * kLeakyBucketSizeSeconds;
  if (target_bitrate_ > 0.0f && bitrate < target_bitrate_ && accumulator_ > accumulator_max_) {
    // Rescale the accumulator level if the accumulator max decreases.
    accumulator_ = bitrate / target_bitrate_ * accumulator_;
  }
  target_bitrate_ = bitrate;
  CapAccumulator();
  incoming_frame_rate_ = static_cast<float>(fps);
}

// Don't let the accumulator grow beyond some level (screencast fix in libwebrtc).
void FrameDropper::CapAccumulator() {
  const float max_accumulator = target_bitrate_ * kAccumulatorCapBufferSizeSecs;
  if (accumulator_ > max_accumulator) accumulator_ = max_accumulator;
}

void FrameDropper::ExportMetrics(MetricsWriter& w) const {
  w.Counter("frames", frames_);
  w.Counter("dropped", dropped_);
  w.Gauge("bucket_bytes", bucket_bytes());
  w.Gauge("drop_ratio", frames_ ? static_cast<double>(dropped_) / static_cast<double>(frames_) : 0.0);
}

}  // namespace sm::cc
