// 4.6 Video jitter estimation and render timing.
//
// VideoJitterEstimator (libwebrtc JitterEstimator model): the extra delay
// d of a frame relative to the previous one is modelled as
//     d = theta0 * dSize + theta1 + noise
// where theta0 = 1/capacity (ms per byte) captures that a larger frame takes
// longer to arrive and theta1 is the queuing offset. A 2-state Kalman filter
// tracks theta; the noise variance is tracked separately with outlier
// clipping. The jitter delay is
//     theta0 * (max_frame_size - avg_frame_size) + k * sqrt(var_noise) - offset
// clamped at >= 1 ms: enough to absorb a worst-case-sized frame plus most
// of the random queueing noise (k = 3, offset = 0 by default; libwebrtc's
// 2.33 and 30 ms are available through the config).
//
// VideoTiming maps RTP timestamps to local render times: a timestamp
// extrapolator (lower envelope of arrival minus RTP time) plus a target delay of
// jitter + decode time (95th percentile) + render delay, which the current
// delay approaches smoothly to avoid visible playback speed jumps.
#pragma once

#include <cstdint>
#include <deque>

#include "sm/core/metrics.h"
#include "sm/core/seq_num.h"
#include "sm/core/units.h"

namespace sm::timing {

struct VideoJitterConfig {
  // libwebrtc uses 2.33 sigma - 30 ms, because its pipeline adds separate
  // OS-jitter and render allowances. Here VideoTiming adds decode and
  // render delay explicitly, so the full noise term is kept: 3 sigma of the
  // frame-delay noise covers ~99% of arrivals.
  double noise_std_devs = 3.0;
  double noise_offset_ms = 0.0;
  double outlier_std_devs = 15.0;
  double phi = 0.97;  // Average frame size filter.
  double psi = 0.9999;  // Max frame size decay.
  double alpha_count_max = 400;
};

class VideoJitterEstimator : public MetricsSource {
 public:
  explicit VideoJitterEstimator(VideoJitterConfig cfg = {});
  // frame_delay: (arrival_i - arrival_{i-1}) - (capture_i - capture_{i-1}).
  void Update(TimeDelta frame_delay, DataSize frame_size);
  // Convenience: computes frame_delay from 90 kHz RTP timestamps.
  void OnFrame(uint32_t rtp_ts, Timestamp arrival, DataSize size);
  TimeDelta JitterDelay() const;
  double slope_ms_per_byte() const { return theta_[0]; }
  double noise_std_ms() const;
  void Reset();
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  void KalmanUpdate(double frame_delay_ms, double delta_bytes);
  VideoJitterConfig cfg_;
  double theta_[2];
  double cov_[2][2];
  double var_noise_;
  double avg_frame_size_ = 0, var_frame_size_ = 100, max_frame_size_ = 0;
  double prev_frame_size_ = 0;
  double alpha_count_ = 1;
  bool has_prev_ = false;
  SeqNumUnwrapper<uint32_t> unwrapper_;
  int64_t prev_ts_ = 0;
  Timestamp prev_arrival_;
  uint64_t frames_ = 0;
  Histogram jitter_ms_;
};

struct VideoTimingConfig {
  // 1 = VideoJitterEstimator + VideoTiming + video::FrameScheduler. 2 =
  // libwebrtc M157's receive timing (timing::VcmTiming and
  // timing::FrameReleaseController, issue #24), the default here as in
  // rtp::VideoStreamConfig (#579).
  int version = 2;
  TimeDelta render_delay = TimeDelta::Millis(10);
  TimeDelta min_playout_delay = TimeDelta::Zero();
  TimeDelta max_playout_delay = TimeDelta::Seconds(10);
  double max_delay_change_per_s = 0.1;  // Fraction of wall time per second.
  size_t decode_time_window = 64;
};

class VideoTiming : public MetricsSource {
 public:
  explicit VideoTiming(VideoTimingConfig cfg = {});
  void IncomingTimestamp(uint32_t rtp_ts, Timestamp arrival);
  void SetJitterDelay(TimeDelta d) { jitter_delay_ = d; }
  void AddDecodeTime(TimeDelta d);
  void SetMinPlayoutDelay(TimeDelta d) { cfg_.min_playout_delay = d; }
  TimeDelta TargetDelay() const;
  TimeDelta CurrentDelay() const { return current_delay_; }
  // Moves the current delay toward the target (rate limited).
  void UpdateCurrentDelay(Timestamp now);
  // Local time at which the frame with this timestamp should be shown.
  Timestamp RenderTime(uint32_t rtp_ts, Timestamp now) const;
  // Local time at which the frame was (estimated to be) captured+sent.
  Timestamp LocalCaptureTime(uint32_t rtp_ts) const;
  TimeDelta DecodeTimeP95() const;
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  VideoTimingConfig cfg_;
  mutable SeqNumUnwrapper<uint32_t> unwrapper_;
  double min_offset_ms_ = 0;  // Lower envelope of (arrival - rtp time).
  TimeDelta jitter_delay_ = TimeDelta::Zero();
  TimeDelta current_delay_ = TimeDelta::Zero();
  Timestamp last_update_ = Timestamp::MinusInfinity();
  std::deque<int64_t> decode_us_;
  bool has_ts_ = false;
};

}  // namespace sm::timing
