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
#pragma once

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
  EventSink* events = nullptr;
};

class QualityScaler : public MetricsSource {
 public:
  enum class Signal { kNone, kAdaptDown, kAdaptUp };
  explicit QualityScaler(QualityScalerConfig cfg = {}) : cfg_(cfg) {}
  void OnEncodedFrame(int qp);
  void OnFrameDropped();
  Signal Check(Timestamp now);
  // Call after the resolution changed in response to a signal.
  void OnResolutionChanged();
  double average_qp() const { return avg_qp_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  QualityScalerConfig cfg_;
  double avg_qp_ = 0;
  int frames_ = 0, dropped_ = 0;
  Timestamp last_check_ = Timestamp::MinusInfinity();
  uint64_t downs_ = 0, ups_ = 0;
};

}  // namespace sm::video
