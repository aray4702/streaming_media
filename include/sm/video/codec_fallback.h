// 7.2 Hardware -> software codec fallback.
//
// Hardware encoders/decoders (MediaCodec, VideoToolbox, V4L2 M2M) can fail
// to initialize, reject a resolution, or start returning errors under
// memory pressure. The controller switches to the software implementation
// when initialization fails, after `max_consecutive_errors` errors in a
// row, or when the error rate over the last `window` frames exceeds
// `max_error_rate`. Every switch requests a keyframe (the new codec has no
// reference state). Optionally the hardware path is retried after
// `retry_hardware_after`, with exponential back-off if it fails again.
#pragma once

#include <deque>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::video {

enum class CodecImpl { kHardware, kSoftware };

struct CodecFallbackConfig {
  int max_consecutive_errors = 3;
  size_t window = 100;
  double max_error_rate = 0.10;
  TimeDelta retry_hardware_after = TimeDelta::Seconds(60);  // Zero: never.
  EventSink* events = nullptr;
};

struct CodecFallbackStats {
  uint64_t frames = 0, errors = 0, fallbacks = 0, hardware_retries = 0;
};

class CodecFallbackController : public MetricsSource {
 public:
  explicit CodecFallbackController(CodecFallbackConfig cfg = {}) : cfg_(cfg), retry_after_(cfg.retry_hardware_after) {}
  void OnInitResult(bool ok, Timestamp now);
  void OnFrameResult(bool ok, Timestamp now);
  // Call periodically: may move back to hardware after the retry delay.
  void Process(Timestamp now);
  CodecImpl active() const { return impl_; }
  bool TakeKeyframeRequest();
  const CodecFallbackStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  void SwitchTo(CodecImpl impl, Timestamp now, const char* reason);
  CodecFallbackConfig cfg_;
  CodecImpl impl_ = CodecImpl::kHardware;
  int consecutive_ = 0;
  std::deque<bool> recent_;
  size_t recent_errors_ = 0;
  Timestamp fell_back_at_ = Timestamp::MinusInfinity();
  TimeDelta retry_after_;
  bool keyframe_ = false;
  bool retrying_ = false;
  CodecFallbackStats stats_;
};

}  // namespace sm::video
