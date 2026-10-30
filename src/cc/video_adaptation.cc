#include "sm/cc/video_adaptation.h"

#include <algorithm>
#include <cmath>

namespace sm::cc {

namespace {
// video/adaptation/overuse_frame_detector.cc (M157).
constexpr int64_t kCheckForOveruseIntervalMs = 5000;
constexpr int64_t kTimeToFirstCheckForOveruseMs = 100;
constexpr int kQuickRampUpDelayMs = 10 * 1000;
constexpr int kStandardRampUpDelayMs = 40 * 1000;
constexpr int kMaxRampUpDelayMs = 240 * 1000;
constexpr double kRampUpBackoffFactor = 2.0;
constexpr int kMaxOverusesBeforeApplyRampupDelay = 4;
constexpr float kMaxExp = 7.0f;
constexpr int kDefaultFrameRate = 30;
constexpr float kDefaultSampleDiffMs = 1000.0f / kDefaultFrameRate;
constexpr float kMaxSampleDiffMarginFactor = 1.35f;
constexpr int kMinFramerate = 7;
constexpr int kMaxFramerate = 30;
// SendProcessingUsage1.
constexpr float kWeightFactorFrameDiff = 0.998f;
constexpr float kWeightFactorProcessing = 0.995f;
constexpr float kInitialSampleDiffMs = 40.0f;
constexpr int64_t kEncodingTimeMeasureWindowMs = 1000;
}  // namespace

CpuOveruseDetector::CpuOveruseDetector(CpuOveruseConfig cfg) : cfg_(cfg) {
  // OveruseFrameDetector::SetOptions: a new SendProcessingUsage1, and a reset
  // with the next frame (num_pixels_ = 0).
  ResetUsage();
}

void CpuOveruseDetector::ResetUsage() {
  // SendProcessingUsage1::Reset. Both filters start from the initial usage,
  // halfway between the thresholds, over a 40 ms frame interval.
  frame_timing_.clear();
  count_ = 0;
  last_processed_capture_time_us_ = -1;
  max_sample_diff_ms_ = kDefaultSampleDiffMs * kMaxSampleDiffMarginFactor;
  const float initial_usage =
      (cfg_.low_encode_usage_threshold_percent + cfg_.high_encode_usage_threshold_percent) / 2.0f;
  filtered_frame_diff_ms_.Reset(kWeightFactorFrameDiff);
  filtered_frame_diff_ms_.Apply(1.0f, kInitialSampleDiffMs);
  filtered_processing_ms_.Reset(kWeightFactorProcessing);
  filtered_processing_ms_.Apply(1.0f, initial_usage * kInitialSampleDiffMs / 100);
}

void CpuOveruseDetector::RestartCheckForOveruse() {
  ResetUsage();
  num_pixels_ = 0;
  next_check_ = Timestamp::MinusInfinity();
}

void CpuOveruseDetector::ResetAll(int num_pixels) {
  // Reset state, as a result resolution being changed. Do not however change
  // the current frame rate back to the default.
  num_pixels_ = num_pixels;
  ResetUsage();
  last_capture_time_us_ = -1;
  num_process_times_ = 0;
  encode_usage_percent_.reset();
  OnTargetFramerateUpdated(max_framerate_);
}

void CpuOveruseDetector::OnTargetFramerateUpdated(int framerate_fps) {
  max_framerate_ = std::min(kMaxFramerate, std::max(framerate_fps, 0));
  // SendProcessingUsage1::SetMaxSampleDiffMs; 1000 / fps is integer division.
  max_sample_diff_ms_ = static_cast<float>(1000 / std::max(kMinFramerate, max_framerate_)) * kMaxSampleDiffMarginFactor;
}

void CpuOveruseDetector::OnFrameCaptured(int num_pixels, uint32_t rtp_timestamp, Timestamp now) {
  const int64_t now_us = now.us();
  const bool timeout = last_capture_time_us_ != -1 && now_us - last_capture_time_us_ > cfg_.frame_timeout_interval.us();
  if (num_pixels != num_pixels_ || timeout) ResetAll(num_pixels);
  // SendProcessingUsage1::FrameCaptured.
  if (last_capture_time_us_ != -1) {
    const float sample_ms = static_cast<float>(1e-3 * static_cast<double>(now_us - last_capture_time_us_));
    filtered_frame_diff_ms_.Apply(std::min(sample_ms / kDefaultSampleDiffMs, kMaxExp), sample_ms);
  }
  frame_timing_.push_back(FrameTiming{rtp_timestamp, now_us});
  last_capture_time_us_ = now_us;
}

void CpuOveruseDetector::OnFrameSent(uint32_t rtp_timestamp, Timestamp time_sent) {
  // SendProcessingUsage1::FrameSent: a frame's encode time is taken once it
  // was first seen more than kEncodingTimeMeasureWindowMs ago, so every layer
  // sent by then counts.
  const int64_t time_sent_us = time_sent.us();
  std::optional<int64_t> encode_duration_us;
  for (auto& t : frame_timing_) {
    if (t.timestamp == rtp_timestamp) {
      t.last_send_us = time_sent_us;
      break;
    }
  }
  while (!frame_timing_.empty()) {
    const FrameTiming timing = frame_timing_.front();
    if (time_sent_us - timing.capture_us < kEncodingTimeMeasureWindowMs * 1000) break;
    if (timing.last_send_us != -1) {
      encode_duration_us = static_cast<int>(timing.last_send_us - timing.capture_us);
      if (last_processed_capture_time_us_ != -1) {
        // AddSample(float processing_ms, int64_t diff_last_sample_ms): the
        // interval is truncated to whole ms.
        const auto diff_ms =
            static_cast<int64_t>(1e-3 * static_cast<double>(timing.capture_us - last_processed_capture_time_us_));
        ++count_;
        const float exp = std::min(static_cast<float>(diff_ms) / kDefaultSampleDiffMs, kMaxExp);
        filtered_processing_ms_.Apply(exp, static_cast<float>(1e-3 * static_cast<double>(*encode_duration_us)));
      }
      last_processed_capture_time_us_ = timing.capture_us;
    }
    frame_timing_.pop_front();
  }
  // OveruseFrameDetector::EncodedFrameTimeMeasured.
  if (encode_duration_us) encode_usage_percent_ = UsageValue();
}

int CpuOveruseDetector::UsageValue() const {
  if (count_ < static_cast<uint64_t>(cfg_.min_frame_samples)) {
    const float initial_usage =
        (cfg_.low_encode_usage_threshold_percent + cfg_.high_encode_usage_threshold_percent) / 2.0f;
    return static_cast<int>(initial_usage + 0.5f);
  }
  float frame_diff_ms = std::max(filtered_frame_diff_ms_.filtered(), 1.0f);
  frame_diff_ms = std::min(frame_diff_ms, max_sample_diff_ms_);
  const float encode_usage_percent = 100.0f * filtered_processing_ms_.filtered() / frame_diff_ms;
  return static_cast<int>(encode_usage_percent + 0.5);
}

bool CpuOveruseDetector::IsOverusing(int usage_percent) {
  if (usage_percent >= cfg_.high_encode_usage_threshold_percent) {
    ++checks_above_threshold_;
  } else {
    checks_above_threshold_ = 0;
  }
  return checks_above_threshold_ >= cfg_.consecutive_checks;
}

bool CpuOveruseDetector::IsUnderusing(int usage_percent, int64_t now_ms) const {
  const int delay = in_quick_rampup_ ? kQuickRampUpDelayMs : current_rampup_delay_ms_;
  if (now_ms < last_rampup_time_ms_ + delay) return false;
  return usage_percent < cfg_.low_encode_usage_threshold_percent;
}

CpuOveruseDetector::Signal CpuOveruseDetector::CheckForOveruse(Timestamp now) {
  ++num_process_times_;
  if (num_process_times_ <= cfg_.min_process_count || !encode_usage_percent_) return Signal::kNone;
  const int64_t now_ms = now.ms();
  if (IsOverusing(*encode_usage_percent_)) {
    // If the last thing we did was going up, and now have to back down, we
    // need to check if this peak was short. If so we should back off to avoid
    // going back and forth between this load.
    const bool check_for_backoff = last_rampup_time_ms_ > last_overuse_time_ms_;
    if (check_for_backoff) {
      if (now_ms - last_rampup_time_ms_ < kStandardRampUpDelayMs ||
          num_overuse_detections_ > kMaxOverusesBeforeApplyRampupDelay) {
        current_rampup_delay_ms_ = static_cast<int>(current_rampup_delay_ms_ * kRampUpBackoffFactor);
        if (current_rampup_delay_ms_ > kMaxRampUpDelayMs) current_rampup_delay_ms_ = kMaxRampUpDelayMs;
      } else {
        current_rampup_delay_ms_ = kStandardRampUpDelayMs;
      }
    }
    last_overuse_time_ms_ = now_ms;
    in_quick_rampup_ = false;
    checks_above_threshold_ = 0;
    ++num_overuse_detections_;
    ++overuses_;
    SM_EVENT(cfg_.events, now, "cpu_overuse", "adapt_down", *encode_usage_percent_, current_rampup_delay_ms_);
    return Signal::kOveruse;
  }
  if (IsUnderusing(*encode_usage_percent_, now_ms)) {
    last_rampup_time_ms_ = now_ms;
    in_quick_rampup_ = true;
    ++underuses_;
    SM_EVENT(cfg_.events, now, "cpu_overuse", "adapt_up", *encode_usage_percent_, current_rampup_delay_ms_);
    return Signal::kUnderuse;
  }
  return Signal::kNone;
}

CpuOveruseDetector::Signal CpuOveruseDetector::Check(Timestamp now) {
  // StartCheckForOveruse's RepeatingTaskHandle::DelayedStart: the first run
  // 100 ms after the start, then every kCheckForOveruseIntervalMs after each run.
  if (!next_check_.IsFinite()) {
    next_check_ = now + TimeDelta::Millis(kTimeToFirstCheckForOveruseMs);
    return Signal::kNone;
  }
  if (now < next_check_) return Signal::kNone;
  next_check_ = now + TimeDelta::Millis(kCheckForOveruseIntervalMs);
  return CheckForOveruse(now);
}

void CpuOveruseDetector::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("encode_usage", usage());
  w.Counter("overuse_signals", overuses_);
  w.Counter("underuse_signals", underuses_);
}

}  // namespace sm::cc
