// Encoder overshoot control (issue #5, stage 1).
//
// EncoderOvershootDetector and EncoderBitrateAdjuster are ports of libwebrtc's
// video/encoder_overshoot_detector.{h,cc} and video/encoder_bitrate_adjuster.{h,cc}
// (M157, compare/webrtc/reference/), with their constants. VideoStreamEncoder
// runs the adjuster by default (RateControlSettings: bitrate_adjuster = true,
// adjuster_use_headroom = true): the encoder is configured with a bitrate
// lowered by how much it overshoots, so that its frames can be paced out at the
// target without building a queue; spare link headroom (the link allocation
// above the encoder target) pays for part of the overshoot.
//
// The detector is the whole of libwebrtc's, one instance. The adjuster is
// libwebrtc's for one stream with one temporal layer, which is all this
// library sends (one-to-one calls, no simulcast or SVC): the per-layer arrays,
// the weighted average over temporal layers and the layout bookkeeping reduce
// to one detector that exists while the target and the frame rate are both
// positive.
//
// Differences from libwebrtc (tools/catalog.py 5.8):
//  - libwebrtc takes the media utilization factor from `RateUtilizationTracker`
//    (rtc_base/rate_utilization_tracker.*, "newfangled headroom adjustment",
//    on by default). That file is not in compare/webrtc/reference/ (reported on
//    #57). Until it is, the factor comes from the detector's own
//    GetMediaRateUtilizationFactor(), which libwebrtc uses when the
//    newfangled adjustment is disabled.
//  - Rates are re-adjusted per captured frame, libwebrtc does it on every
//    bitrate update (about every 25 ms to 1 s).
// Takes `now`, reads no clock.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::cc {

class EncoderOvershootDetector {
 public:
  explicit EncoderOvershootDetector(int64_t window_size_ms) : window_size_ms_(window_size_ms) {}

  void SetTargetRate(DataRate target_bitrate, double target_framerate_fps, int64_t time_ms);
  // A frame has been encoded or dropped. `bytes` == 0 indicates a drop.
  void OnEncodedFrame(size_t bytes, int64_t time_ms);
  // 1.0 only if the frames can be sent at the target without growing queues.
  std::optional<double> GetNetworkRateUtilizationFactor(int64_t time_ms);
  // Based on frame sizes against ideal sizes; an undershoot may make up for an
  // overshoot.
  std::optional<double> GetMediaRateUtilizationFactor(int64_t time_ms);
  void Reset();

 private:
  int64_t IdealFrameSizeBits() const;
  void LeakBits(int64_t time_ms);
  void CullOldUpdates(int64_t time_ms);
  double HandleEncodedFrame(size_t frame_size_bits, int64_t ideal_frame_size_bits, int64_t time_ms,
                            int64_t* buffer_level_bits) const;

  struct BitrateUpdate {
    double network_utilization_factor;
    double media_utilization_factor;
    int64_t update_time_ms;
  };

  const int64_t window_size_ms_;
  int64_t time_last_update_ms_ = -1;
  std::deque<BitrateUpdate> utilization_factors_;
  double sum_network_utilization_factors_ = 0.0;
  double sum_media_utilization_factors_ = 0.0;
  DataRate target_bitrate_;
  double target_framerate_fps_ = 0;
  int64_t network_buffer_level_bits_ = 0;
  int64_t media_buffer_level_bits_ = 0;
};

struct EncoderBitrateAdjusterConfig {
  // RateControlSettings: bitrate_adjuster and adjuster_use_headroom.
  bool utilize_bandwidth_headroom = true;
  // Codec minimum bitrate (VideoCodec::minBitrate); 0 = none.
  DataRate min_bitrate;
};

class EncoderBitrateAdjuster : public MetricsSource {
 public:
  static constexpr int64_t kWindowSizeMs = 3000;
  static constexpr size_t kMinFramesSinceLayoutChange = 30;
  static constexpr double kDefaultUtilizationFactor = 1.2;

  explicit EncoderBitrateAdjuster(EncoderBitrateAdjusterConfig cfg = {})
      : cfg_(cfg), detector_(kWindowSizeMs) {}

  // AdjustRateAllocation: the bitrate to configure the encoder with. `target`
  // is the encoder target, `bandwidth_allocation` the link share for this
  // stream (>= target), `framerate_fps` the input frame rate.
  DataRate AdjustRate(DataRate target, DataRate bandwidth_allocation, double framerate_fps, Timestamp now);
  void OnEncodedFrame(DataSize size, Timestamp now);
  void OnFrameDropped(Timestamp now);
  void Reset(Timestamp now);

  double link_utilization_factor() const { return link_factor_; }
  double media_utilization_factor() const { return media_factor_; }
  DataRate adjusted() const { return adjusted_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  EncoderBitrateAdjusterConfig cfg_;
  bool detector_active_ = false;
  EncoderOvershootDetector detector_;
  size_t frames_since_layout_change_ = 0;
  DataRate last_target_, last_bandwidth_;
  double last_fps_ = 0;
  double link_factor_ = 1.0, media_factor_ = 1.0;
  DataRate adjusted_;
};

}  // namespace sm::cc
