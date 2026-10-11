#include "sm/video/quality_scaler.h"

#include <cctype>

namespace sm::video {

QpThresholds QpThresholds::ForCodec(const std::string& codec) {
  std::string c;
  for (char ch : codec) c += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  if (c == "VP8") return {29, 95};
  if (c == "VP9") return {120, 180};
  if (c == "AV1") return {145, 205};
  return {24, 37};  // H.264 / H.265 (0..51).
}

void QualityScaler::OnEncodedFrame(int qp) {
  avg_qp_ = frames_ == 0 ? qp : avg_qp_ + cfg_.smoothing * (qp - avg_qp_);
  ++frames_;
}

void QualityScaler::OnFrameDropped() { ++dropped_; }

void QualityScaler::OnResolutionChanged() {
  frames_ = 0;
  dropped_ = 0;
  avg_qp_ = 0;
}

QualityScaler::Signal QualityScaler::Check(Timestamp now) {
  if (last_check_.IsFinite() && now - last_check_ < cfg_.sampling_period) return Signal::kNone;
  last_check_ = now;
  int total = frames_ + dropped_;
  if (total < cfg_.min_frames) return Signal::kNone;
  double drop_ratio = static_cast<double>(dropped_) / total;
  Signal s = Signal::kNone;
  if (drop_ratio > cfg_.max_drop_ratio || (frames_ >= cfg_.min_frames && avg_qp_ > cfg_.thresholds.high)) {
    s = Signal::kAdaptDown;
    ++downs_;
  } else if (frames_ >= cfg_.min_frames && avg_qp_ < cfg_.thresholds.low) {
    s = Signal::kAdaptUp;
    ++ups_;
  }
  dropped_ = 0;
  if (s != Signal::kNone) {
    SM_EVENT(cfg_.events, now, "quality_scaler", s == Signal::kAdaptDown ? "down" : "up", avg_qp_, 0);
  }
  return s;
}

void QualityScaler::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("average_qp", avg_qp_);
  w.Counter("adapt_down", downs_);
  w.Counter("adapt_up", ups_);
}

}  // namespace sm::video
