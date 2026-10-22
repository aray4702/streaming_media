// 7.8 Quality scaler: resolution decisions from the encoder's QP.
//
// When the bitrate is too low for the resolution the encoder raises its
// quantizer (QP) and the picture turns blocky; a smaller resolution at the
// same bitrate looks better. The scaler averages the QP of encoded frames
// (exponential filter) and, every `sampling_period`:
//   avg QP > high threshold, or > 60% frames dropped  -> adapt down
//   avg QP < low threshold                             -> adapt up
// Thresholds are per codec (libwebrtc defaults): VP8 29/95 (0..127),
// VP9 120/180 and AV1 145/205 (0..255), H.264 24/37 (0..51). After a
// resolution change the filter is reset so the next decision uses QP
// measured at the new resolution. Region-of-interest QP (lower QP on faces
// or text) is an encoder-side refinement on top of these thresholds.
//
// `version = 2` is libwebrtc M157's QualityScaler (#84 step 2), ported from
// compare/webrtc/reference/modules/video_coding/utility/quality_scaler.cc with
// its constants: the first check 2 s (`kMeasureMs`) after the first Check()
// call (libwebrtc starts its task at construction), then every 2 s while
// `fast_rampup_` (until the first adapt-down) and every 2 s x 2.5
// (`kSamplePeriodScaleFactor`) = 5 s after it; 1 s while the previous check
// saw too few frames. A check needs 60 frames (`kMinFramesNeededToScale`)
// over a 150-frame window (`5 * 30`), encoded or dropped. It reports high QP
// when the drop rate over that window is >= 60% (`kFramedropPercentThreshold`,
// rounded down) or the smoothed QP is > high, and low QP when it is <= low;
// every report clears the samples. QP is smoothed as QualityScalingExperiment
// does by default (it is on unless the `WebRTC-Video-QualityScaling` trial
// says `Disabled`): two rtc::ExpFilter-style filters over the sample times in
// ms, alpha 0.9995 for the high check and 0.9999 for the low check, read as
// int, with drops of every reason counted. The experiment's source is not in
// reference/ (#57); sm_video_compare matches libwebrtc's binary on every
// quality-scaler row with these values (docs/results/video-webrtc.md).
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::video {

struct QpThresholds {
  int low = 24;
  int high = 37;
  static QpThresholds ForCodec(const std::string& codec);
};

struct QualityScalerConfig {
  QpThresholds thresholds;
  TimeDelta sampling_period = TimeDelta::Millis(1500);
  int min_frames = 15;
  double smoothing = 0.15;           // EMA weight per frame.
  double max_drop_ratio = 0.6;
  // 2: libwebrtc M157's QualityScaler (see above); the four fields above
  // apply to version 1 only.
  int version = 1;
  EventSink* events = nullptr;
};

class QualityScaler : public MetricsSource {
 public:
  enum class Signal { kNone, kAdaptDown, kAdaptUp };
  explicit QualityScaler(QualityScalerConfig cfg = {}) : cfg_(cfg) {}
  // `now` is the frame's send time (libwebrtc's ReportQp time_sent_us); only
  // version 2 uses it, and without it takes the time of the last Check().
  void OnEncodedFrame(int qp, Timestamp now = Timestamp::MinusInfinity());
  void OnFrameDropped();
  Signal Check(Timestamp now);
  // Call after the resolution changed in response to a signal. Version 2
  // already cleared its samples with the signal, as libwebrtc does, so this
  // does nothing there.
  void OnResolutionChanged();
  double average_qp() const { return avg_qp_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  // rtc::ExpFilter over the sample time in ms (QualityScaler::QpSmoother).
  struct QpSmoother {
    float alpha = 0;
    float filtered = -1.0f;  // ExpFilter::kValueUndefined.
    int64_t last_sample_ms = 0;
    void Add(float sample, int64_t now_ms);
  };
  enum class Result { kInsufficientSamples, kNormalQp, kHighQp, kLowQp };
  Signal CheckV2(Timestamp now);
  Result CheckQpV2() const;
  TimeDelta NextCheckDelayV2() const;
  void ClearSamplesV2();

  QualityScalerConfig cfg_;
  double avg_qp_ = 0;
  // Version 2 state.
  static constexpr size_t kDropWindow = 5 * 30;
  std::array<uint8_t, kDropWindow> drops_{};  // 100 per dropped frame, as MovingAverage.
  size_t drop_count_ = 0;                     // Samples since the last clear.
  int drop_sum_ = 0;
  QpSmoother high_{0.9995f}, low_{0.9999f};
  bool fast_rampup_ = true;
  bool prev_observed_enough_ = false;  // CheckQpTask::Result::observed_enough_frames.
  Timestamp next_check_ = Timestamp::MinusInfinity();
  int frames_ = 0, dropped_ = 0;
  Timestamp last_check_ = Timestamp::MinusInfinity();
  uint64_t downs_ = 0, ups_ = 0;
};

}  // namespace sm::video
