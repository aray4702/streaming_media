#include "sm/cc/frame_dropper.h"

#include <algorithm>

namespace sm::cc {

void FrameDropper::SetRates(DataRate target, double fps) {
  target_bytes_per_frame_ = fps > 0 ? target.bps() / 8.0 / fps : 0;
  max_bucket_ = target.bps() / 8.0 * cfg_.window.seconds();
}

bool FrameDropper::ShouldDrop() {
  ++frames_;
  // Leak one frame interval worth of budget.
  bucket_ = std::max(0.0, bucket_ - target_bytes_per_frame_);
  // Spread outstanding keyframe bytes over the following frames.
  if (keyframe_debt_ > 0) {
    double add = std::min(keyframe_debt_, keyframe_leak_);
    keyframe_debt_ -= add;
    bucket_ += add;
  }
  if (bucket_ > max_bucket_) {
    ++dropped_;
    return true;
  }
  return false;
}

void FrameDropper::OnEncodedFrame(DataSize size, bool keyframe) {
  double s = static_cast<double>(size.bytes());
  if (keyframe && cfg_.keyframe_spread_frames > 1) {
    // Count one average frame now and spread the excess.
    double now_part = std::min(s, target_bytes_per_frame_);
    bucket_ += now_part;
    keyframe_debt_ += s - now_part;
    keyframe_leak_ = keyframe_debt_ / cfg_.keyframe_spread_frames;
  } else {
    bucket_ += s;
  }
}

void FrameDropper::ExportMetrics(MetricsWriter& w) const {
  w.Counter("frames", frames_);
  w.Counter("dropped", dropped_);
  w.Gauge("bucket_bytes", bucket_);
  w.Gauge("drop_ratio", frames_ ? static_cast<double>(dropped_) / static_cast<double>(frames_) : 0.0);
}

void EncoderOvershootDetector::OnEncodedFrame(DataSize size, Timestamp now) {
  frames_.emplace_back(now, size.bytes());
  last_now_ = now;
  while (!frames_.empty() && now - frames_.front().first > window_) frames_.pop_front();
}

double EncoderOvershootDetector::Utilization(Timestamp now) const {
  if (frames_.size() < 2 || target_.bps() <= 0) return 1.0;
  int64_t bytes = 0;
  for (const auto& f : frames_) bytes += f.second;
  TimeDelta span = std::max(now - frames_.front().first, TimeDelta::Millis(100));
  DataRate actual = DataRate::FromSizeAndTime(DataSize::Bytes(bytes), span);
  return actual / target_;
}

DataRate EncoderOvershootDetector::AdjustedTarget(Timestamp now) const {
  double u = Utilization(now);
  // Only correct overshoot; undershoot is left to the encoder.
  return u > 1.0 ? target_ * (1.0 / u) : target_;
}

void EncoderOvershootDetector::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("utilization", Utilization(last_now_));
  w.Gauge("target_kbps", target_.kbps());
}

}  // namespace sm::cc
