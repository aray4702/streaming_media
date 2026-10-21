#include "sm/video/quality_scaler.h"

#include <cctype>
#include <algorithm>
#include <cmath>

namespace sm::video {

namespace {
// modules/video_coding/utility/quality_scaler.cc (version 2).
constexpr TimeDelta kMeasure = TimeDelta::Millis(2000);  // kMeasureMs.
constexpr double kSamplePeriodScaleFactor = 2.5;
constexpr int kFramedropPercentThreshold = 60;
constexpr size_t kMinFramesNeededToScale = 2 * 30;
}  // namespace

QpThresholds QpThresholds::ForCodec(const std::string& codec) {
  std::string c;
  for (char ch : codec) c += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  if (c == "VP8") return {29, 95};
  if (c == "VP9") return {120, 180};
  if (c == "AV1") return {145, 205};
  return {24, 37};  // H.264 / H.265 (0..51).
}

void QualityScaler::QpSmoother::Add(float sample, int64_t now_ms) {
  // rtc_base/numerics/exp_filter.cc ExpFilter::Apply with exp = the time since
  // the previous sample in ms; the first sample after a reset is taken as is.
  const float exp = static_cast<float>(now_ms - last_sample_ms);
  last_sample_ms = now_ms;
  if (filtered == -1.0f) {
    filtered = sample;
  } else if (exp == 1.0f) {
    filtered = alpha * filtered + (1 - alpha) * sample;
  } else {
    const float a = std::pow(alpha, exp);
    filtered = a * filtered + (1 - a) * sample;
  }
}

void QualityScaler::OnEncodedFrame(int qp, Timestamp now) {
  if (cfg_.version >= 2) {
    // QualityScaler::ReportQp: a 0 into the drop window, QP into both smoothers.
    drop_sum_ -= drops_[drop_count_ % kDropWindow];
    drops_[drop_count_ % kDropWindow] = 0;
    ++drop_count_;
    const int64_t ms = now.IsFinite() ? now.ms() : (last_check_.IsFinite() ? last_check_.ms() : 0);
    high_.Add(static_cast<float>(qp), ms);
    low_.Add(static_cast<float>(qp), ms);
    avg_qp_ = high_.filtered;
    return;
  }
  avg_qp_ = frames_ == 0 ? qp : avg_qp_ + cfg_.smoothing * (qp - avg_qp_);
  ++frames_;
}

void QualityScaler::OnFrameDropped() {
  if (cfg_.version >= 2) {
    // ReportDroppedFrameByMediaOpt / ByEncoder: QualityScalingExperiment's
    // default counts drops of every reason (use_all_drop_reasons).
    drop_sum_ += 100 - drops_[drop_count_ % kDropWindow];
    drops_[drop_count_ % kDropWindow] = 100;
    ++drop_count_;
    return;
  }
  ++dropped_;
}

void QualityScaler::OnResolutionChanged() {
  if (cfg_.version >= 2) return;
  frames_ = 0;
  dropped_ = 0;
  avg_qp_ = 0;
}

void QualityScaler::ClearSamplesV2() {
  drops_.fill(0);
  drop_count_ = 0;
  drop_sum_ = 0;
  high_.filtered = -1.0f;
  low_.filtered = -1.0f;
}

QualityScaler::Result QualityScaler::CheckQpV2() const {
  const size_t frames = std::min(drop_count_, kDropWindow);
  if (frames < kMinFramesNeededToScale) return Result::kInsufficientSamples;
  // MovingAverage::GetAverageRoundedDown.
  if (drop_sum_ / static_cast<int>(frames) >= kFramedropPercentThreshold) return Result::kHighQp;
  if (high_.filtered != -1.0f && low_.filtered != -1.0f) {
    if (static_cast<int>(high_.filtered) > cfg_.thresholds.high) return Result::kHighQp;
    if (static_cast<int>(low_.filtered) <= cfg_.thresholds.low) return Result::kLowQp;
  }
  return Result::kNormalQp;
}

// CheckQpTask::GetCheckingQpDelayMs with QualityScalingExperiment on and no
// QualityScalerSettings scale factor.
TimeDelta QualityScaler::NextCheckDelayV2() const {
  if (fast_rampup_) return kMeasure;
  if (!prev_observed_enough_) return TimeDelta::Micros(kMeasure.us() / 2);
  return TimeDelta::Micros(static_cast<int64_t>(kMeasure.us() * kSamplePeriodScaleFactor));
}

QualityScaler::Signal QualityScaler::CheckV2(Timestamp now) {
  if (!next_check_.IsFinite()) {
    // libwebrtc starts the first CheckQpTask when the scaler is created.
    last_check_ = now;
    next_check_ = now + NextCheckDelayV2();
    return Signal::kNone;
  }
  if (now < next_check_) return Signal::kNone;
  last_check_ = now;
  const Result r = CheckQpV2();
  Signal s = Signal::kNone;
  if (r == Result::kHighQp) {
    fast_rampup_ = false;
    s = Signal::kAdaptDown;
    ++downs_;
  } else if (r == Result::kLowQp) {
    s = Signal::kAdaptUp;
    ++ups_;
  }
  if (s != Signal::kNone) {
    SM_EVENT(cfg_.events, now, "quality_scaler", s == Signal::kAdaptDown ? "down" : "up", high_.filtered,
             static_cast<double>(low_.filtered));
    ClearSamplesV2();
  }
  prev_observed_enough_ = r != Result::kInsufficientSamples;
  // The next task is posted when this one runs.
  next_check_ = now + NextCheckDelayV2();
  return s;
}

QualityScaler::Signal QualityScaler::Check(Timestamp now) {
  if (cfg_.version >= 2) return CheckV2(now);
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
