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
#pragma once

#include <optional>
#include <vector>

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
  int consecutive_checks = 2;
  double smoothing = 0.1;  // EMA weight per frame.
};

class CpuOveruseDetector : public MetricsSource {
 public:
  enum class Signal { kNone, kOveruse, kUnderuse };
  explicit CpuOveruseDetector(CpuOveruseConfig cfg = {}) : cfg_(cfg) {}
  void OnFrameEncoded(TimeDelta encode_time, TimeDelta frame_interval);
  Signal Check(Timestamp now);
  double usage() const { return usage_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  CpuOveruseConfig cfg_;
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
