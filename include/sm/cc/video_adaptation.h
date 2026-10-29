// 5.9 Video adaptation to bandwidth and CPU (degradationPreference).
//
// DegradationController picks the encoded resolution and frame rate for a
// target bitrate using a bits-per-pixel budget:
//   bitrate >= width * height * fps * bpp
// Candidate formats are ordered by preference:
//   maintain-framerate:  lower resolution only
//   maintain-resolution: lower frame rate only (down to min_fps)
//   balanced:            frame rate down to balanced_fps first, then
//                        resolution, then frame rate down to min_fps
// Going down needs bpp_down, going back up needs the larger bpp_up, so the
// format does not oscillate around a threshold. Resource limits (CPU
// overuse, quality scaler) impose an extra cap on pixels or fps.
//
// CpuOveruseDetector: encode usage = encode time / frame interval,
// exponentially filtered. Above 85% for consecutive checks -> overuse
// (adapt down); below 42% -> underuse (adapt up), with a back-off after an
// adapt-up that caused overuse, to avoid oscillating.
//
// `version = 2` is libwebrtc M157's OveruseFrameDetector (issue #84 step 3),
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
#include <vector>

#include "sm/cc/frame_dropper.h"  // ExpFilter.
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::cc {

enum class DegradationPreference { kMaintainFramerate, kMaintainResolution, kBalanced, kDisabled };

struct VideoFormat {
  int width = 0;
  int height = 0;
  double fps = 0;
  int64_t pixels() const { return int64_t{width} * height; }
  bool operator==(const VideoFormat& o) const = default;
};

struct DegradationConfig {
  VideoFormat input{1280, 720, 30};
  DegradationPreference preference = DegradationPreference::kBalanced;
  double bpp_down = 0.04;
  double bpp_up = 0.06;
  double min_fps = 5;
  double balanced_fps = 15;
  int min_pixels = 320 * 180;
  EventSink* events = nullptr;
};

class DegradationController : public MetricsSource {
 public:
  explicit DegradationController(DegradationConfig cfg = {});
  VideoFormat OnTargetBitrate(DataRate target, Timestamp now = Timestamp::Micros(0));
  // Resource-driven steps (CPU overuse, quality scaler). Each call moves
  // the resource cap one step along the preference ladder.
  void OnResourceOveruse();
  void OnResourceUnderuse();
  VideoFormat current() const { return current_; }
  const std::vector<VideoFormat>& ladder() const { return ladder_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  bool Fits(const VideoFormat& f, DataRate r, double bpp) const;
  DegradationConfig cfg_;
  std::vector<VideoFormat> ladder_;  // Best first.
  size_t index_ = 0;                 // Bitrate-chosen step.
  size_t resource_cap_ = 0;          // Minimum step index forced by resources.
  VideoFormat current_;
  DataRate last_target_;
  uint64_t adapt_down_ = 0, adapt_up_ = 0;
};

struct CpuOveruseConfig {
  double high_threshold = 0.85;
  double low_threshold = 0.42;
  TimeDelta check_interval = TimeDelta::Seconds(5);
  int consecutive_checks = 2;  // Also v2's high_threshold_consecutive_count.
  double smoothing = 0.1;  // EMA weight per frame.
  // 1: the filter above. 2 (default, #579): libwebrtc M157's
  // OveruseFrameDetector (see above); the fields below are its
  // CpuOveruseOptions, with their defaults.
  int version = 2;
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
  // Version 1.
  void OnFrameEncoded(TimeDelta encode_time, TimeDelta frame_interval);
  // Version 1: a check every `check_interval`. Version 2: the repeating task,
  // 100 ms after the first call and then every 5 s; call it at least that often.
  Signal Check(Timestamp now);
  double usage() const { return version2() ? encode_usage_percent_.value_or(0) / 100.0 : usage_; }

  // Version 2 (OveruseFrameDetector). `now` is when the encoder first saw the
  // frame; `rtp_timestamp` identifies it in OnFrameSent.
  void OnFrameCaptured(int num_pixels, uint32_t rtp_timestamp, Timestamp now);
  // One encoded layer of the frame was sent at `time_sent`.
  void OnFrameSent(uint32_t rtp_timestamp, Timestamp time_sent);
  void OnTargetFramerateUpdated(int framerate_fps);
  // One run of CheckForOveruse, outside the task schedule.
  Signal CheckForOveruse(Timestamp now);
  // The usage of the last measured encode time; empty until one is measured
  // (and after a reset).
  std::optional<int> encode_usage_percent() const { return encode_usage_percent_; }

  void ExportMetrics(MetricsWriter& w) const override;

 private:
  bool version2() const { return cfg_.version >= 2; }
  // SendProcessingUsage1's FrameTiming; capture_us is when the frame was first seen.
  struct FrameTiming { uint32_t timestamp; int64_t capture_us; int64_t last_send_us = -1; };
  void ResetUsage();
  void ResetAll(int num_pixels);
  int UsageValue() const;
  bool IsOverusing(int usage_percent);
  bool IsUnderusing(int usage_percent, int64_t now_ms) const;

  CpuOveruseConfig cfg_;
  // Version 2 state (OveruseFrameDetector and SendProcessingUsage1).
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
  double usage_ = 0;
  bool has_usage_ = false;
  Timestamp last_check_ = Timestamp::MinusInfinity();
  int over_checks_ = 0;
  int under_checks_needed_ = 1;
  int under_checks_ = 0;
  bool last_was_up_ = false;
  uint64_t overuses_ = 0, underuses_ = 0;
};

}  // namespace sm::cc
