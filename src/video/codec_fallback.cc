#include "sm/video/codec_fallback.h"

namespace sm::video {

void CodecFallbackController::SwitchTo(CodecImpl impl, Timestamp now, const char* reason) {
  if (impl == impl_) return;
  impl_ = impl;
  keyframe_ = true;
  consecutive_ = 0;
  recent_.clear();
  recent_errors_ = 0;
  if (impl == CodecImpl::kSoftware) {
    ++stats_.fallbacks;
    fell_back_at_ = now;
    // A failed retry doubles the wait before the next one.
    if (retrying_) retry_after_ = retry_after_ * 2.0;
    retrying_ = false;
  } else {
    ++stats_.hardware_retries;
    retrying_ = true;
  }
  SM_EVENT(cfg_.events, now, "codec_fallback", reason, impl == CodecImpl::kSoftware ? 1 : 0, 0);
}

void CodecFallbackController::OnInitResult(bool ok, Timestamp now) {
  if (!ok && impl_ == CodecImpl::kHardware) SwitchTo(CodecImpl::kSoftware, now, "init_failed");
}

void CodecFallbackController::OnFrameResult(bool ok, Timestamp now) {
  ++stats_.frames;
  if (!ok) ++stats_.errors;
  if (impl_ == CodecImpl::kSoftware) return;  // Software is the last resort.
  consecutive_ = ok ? 0 : consecutive_ + 1;
  recent_.push_back(!ok);
  recent_errors_ += !ok;
  if (recent_.size() > cfg_.window) {
    recent_errors_ -= recent_.front();
    recent_.pop_front();
  }
  if (consecutive_ >= cfg_.max_consecutive_errors) {
    SwitchTo(CodecImpl::kSoftware, now, "consecutive_errors");
  } else if (recent_.size() >= cfg_.window / 2 &&
             static_cast<double>(recent_errors_) / static_cast<double>(recent_.size()) > cfg_.max_error_rate) {
    SwitchTo(CodecImpl::kSoftware, now, "error_rate");
  } else if (retrying_ && recent_.size() >= cfg_.window) {
    retrying_ = false;  // Hardware proved healthy again.
    retry_after_ = cfg_.retry_hardware_after;
  }
}

void CodecFallbackController::Process(Timestamp now) {
  if (impl_ == CodecImpl::kSoftware && cfg_.retry_hardware_after > TimeDelta::Zero() &&
      now - fell_back_at_ >= retry_after_) {
    SwitchTo(CodecImpl::kHardware, now, "retry_hardware");
  }
}

bool CodecFallbackController::TakeKeyframeRequest() {
  bool k = keyframe_;
  keyframe_ = false;
  return k;
}

void CodecFallbackController::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("hardware", impl_ == CodecImpl::kHardware ? 1 : 0);
  w.Counter("frames", stats_.frames);
  w.Counter("errors", stats_.errors);
  w.Counter("fallbacks", stats_.fallbacks);
  w.Counter("hardware_retries", stats_.hardware_retries);
}

}  // namespace sm::video
