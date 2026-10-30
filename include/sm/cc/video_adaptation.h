// 5.9 Video adaptation to bandwidth and CPU: CPU overuse detection.
//
// The adaptation itself (the restrictions a resource asks for, the
// degradation preferences, BalancedConstraint and BitrateConstraint) is
// video::VideoStreamAdapter and video::ResourceAdaptationProcessor, driven by
// rtp::VideoSendController. The library's own bits-per-pixel ladder
// (DegradationController) and the exponentially filtered v1 detector had no
// libwebrtc counterpart and were removed (issue #819).
//
// CpuOveruseDetector is libwebrtc M157's OveruseFrameDetector (issue #84 step 3),
// ported from compare/webrtc/reference/video/adaptation/overuse_frame_detector.cc
// with its default estimator, SendProcessingUsage1 (`WebRTC-CpuLoadEstimator`
// is off by default, so `filter_time_ms` stays 0) and the CpuOveruseOptions
// defaults that VideoStreamEncoderResourceManager::GetCpuOveruseOptions keeps
// for a software encoder (a hardware one gets 150/200):
//   - per frame, OnFrameCaptured (FrameCaptured, with the time the encoder
//     first saw the frame) and OnFrameSent (FrameSent, per encoded layer);
//   - a frame's encode time is its last send time minus that first-seen time,
//     taken once 1 s has passed since it was seen (kEncodingTimeMeasureWindowMs);
//   - usage % = ExpFilter(0.995) of encode time / ExpFilter(0.998) of the
//     capture interval, the interval capped at 1.35 x 1000 / fps for the
//     target frame rate (7..30 fps), exponents scaled by sample / 33.3 ms (max
//     7); before 120 encode samples it reads (42 + 85) / 2 = 63.5 -> 64;
//   - a new resolution, or 1.5 s without a frame, resets the estimate;
//   - the check runs 100 ms after the first Check() (StartCheckForOveruse's
//     delayed start), then every 5 s, and acts only after 3 checks
//     (min_process_count) once a usage has been measured: overuse after 2
//     checks in a row at >= 85%, underuse below 42% once the ramp-up delay has
//     passed (10 s after an adapt-up, otherwise 40 s, doubled up to 240 s when
//     an adapt-up was followed by overuse within 40 s or after more than 4
//     overuses).
// SendProcessingUsage2 (the experimental estimator) and the simulated
// overuse injector (`WebRTC-ForceSimulatedOveruseIntervalMs`) are not ported:
// neither is on by default.
#pragma once

#include <list>
#include <optional>

#include "sm/cc/frame_dropper.h"  // ExpFilter.
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::cc {

struct CpuOveruseConfig {
  // CpuOveruseOptions, with their defaults.
  int consecutive_checks = 2;  // high_threshold_consecutive_count.
  int high_encode_usage_threshold_percent = 85;
  int low_encode_usage_threshold_percent = (85 - 1) / 2;
  TimeDelta frame_timeout_interval = TimeDelta::Millis(1500);
  int min_frame_samples = 120;
  int min_process_count = 3;
  EventSink* events = nullptr;
};

class CpuOveruseDetector : public MetricsSource {
 public:
  enum class Signal { kNone, kOveruse, kUnderuse };
  explicit CpuOveruseDetector(CpuOveruseConfig cfg = {});
  // The repeating task: 100 ms after the first call and then every 5 s; call
  // it at least that often.
  Signal Check(Timestamp now);
  double usage() const { return encode_usage_percent_.value_or(0) / 100.0; }

  // `now` is when the encoder first saw the frame; `rtp_timestamp`
  // identifies it in OnFrameSent.
  void OnFrameCaptured(int num_pixels, uint32_t rtp_timestamp, Timestamp now);
  // One encoded layer of the frame was sent at `time_sent`.
  void OnFrameSent(uint32_t rtp_timestamp, Timestamp time_sent);
  void OnTargetFramerateUpdated(int framerate_fps);
  // OveruseFrameDetector::StopCheckForOveruse then StartCheckForOveruse
  // (EncodeUsageResource, when the encoder is configured
  // again): SetOptions' new SendProcessingUsage1, a reset with the next frame
  // (num_pixels_ = 0), and the check task started again, its first run 100 ms
  // after the next Check(). The overuse and rampup state are kept, as there.
  void RestartCheckForOveruse();
  // One run of CheckForOveruse, outside the task schedule.
  Signal CheckForOveruse(Timestamp now);
  // The usage of the last measured encode time; empty until one is measured
  // (and after a reset).
  std::optional<int> encode_usage_percent() const { return encode_usage_percent_; }

  void ExportMetrics(MetricsWriter& w) const override;

 private:
  // SendProcessingUsage1's FrameTiming; capture_us is when the frame was first seen.
  struct FrameTiming { uint32_t timestamp; int64_t capture_us; int64_t last_send_us = -1; };
  void ResetUsage();
  void ResetAll(int num_pixels);
  int UsageValue() const;
  bool IsOverusing(int usage_percent);
  bool IsUnderusing(int usage_percent, int64_t now_ms) const;

  CpuOveruseConfig cfg_;
  // OveruseFrameDetector and SendProcessingUsage1 state.
  std::list<FrameTiming> frame_timing_;
  uint64_t count_ = 0;
  int64_t last_processed_capture_time_us_ = -1;
  float max_sample_diff_ms_ = 0;
  ExpFilter filtered_processing_ms_{0.995f};
  ExpFilter filtered_frame_diff_ms_{0.998f};
  int num_process_times_ = 0;
  int64_t last_capture_time_us_ = -1;
  int num_pixels_ = 0;
  int max_framerate_ = 30;
  int64_t last_overuse_time_ms_ = -1;
  int checks_above_threshold_ = 0;
  int num_overuse_detections_ = 0;
  int64_t last_rampup_time_ms_ = -1;
  bool in_quick_rampup_ = false;
  int current_rampup_delay_ms_ = 40 * 1000;
  std::optional<int> encode_usage_percent_;
  Timestamp next_check_ = Timestamp::MinusInfinity();
  uint64_t overuses_ = 0, underuses_ = 0;
};

}  // namespace sm::cc
