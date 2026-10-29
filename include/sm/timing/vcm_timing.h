// 4.6 (v2) Video receive timing as libwebrtc M157 (issue #24).
//
// A port of libwebrtc's receive-side video timing, used by rtp::RtpReceiver
// under `timing.version = 2` (the test bench's `timing v2`):
//   - JitterEstimator (video/timing/jitter_estimator.cc) with its Kalman
//     filter (frame_delay_variation_kalman_filter.cc), the inter-frame delay
//     variation calculator (inter_frame_delay_variation_calculator.cc) and
//     the RTT filter (rtt_filter.cc): noise threshold 2.33 sigma - 30 ms,
//     10 ms OS jitter, +0.9 x RTT (at most 200 ms) once 3 frames within 60 s
//     were NACKed, scaled down below 10 fps;
//   - TimestampExtrapolator (video/timing/timestamp_extrapolator.cc): a
//     recursive least squares fit of RTP time against local arrival time,
//     with hard outlier rejection and a CUSUM delay-change detector;
//   - DecodeTimePercentileFilter (modules/video_coding/timing/
//     decode_time_percentile_filter.cc): 95th percentile over 10 s;
//   - VcmTiming (modules/video_coding/timing/timing.cc, with
//     video/timing/default_video_jitter_timing.cc folded in): the target
//     delay max(min_playout, jitter + decode + 10 ms render delay), and the
//     current delay, which only grows when a frame is released late.
// libwebrtc reads its clock inside these classes; here every call takes
// `now`. Field-trial overrides are not ported; every default is.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <set>

#include "sm/core/metrics.h"
#include "sm/core/seq_num.h"
#include "sm/core/units.h"

namespace sm::timing {

// video/timing/rtt_filter.cc: a jump- and drift-detecting RTT filter whose
// output is the largest RTT seen since the last reset or jump.
class RttFilter {
 public:
  RttFilter() { Reset(); }
  void Reset();
  void Update(TimeDelta rtt);
  TimeDelta Rtt() const { return max_rtt_; }

 private:
  static constexpr size_t kMaxDriftJumpCount = 5;
  bool JumpDetection(TimeDelta rtt);
  bool DriftDetection(TimeDelta rtt);
  void ShortRttFilter(const std::deque<TimeDelta>& buf);
  bool got_non_zero_update_ = false;
  TimeDelta avg_rtt_;
  int64_t var_rtt_ = 0;
  TimeDelta max_rtt_;
  uint32_t filt_fact_count_ = 1;
  bool last_jump_positive_ = false;
  std::deque<TimeDelta> jump_buf_, drift_buf_;
};

// video/timing/frame_delay_variation_kalman_filter.cc.
class FrameDelayVariationKalmanFilter {
 public:
  FrameDelayVariationKalmanFilter();
  void PredictAndUpdate(double frame_delay_variation_ms, double frame_size_variation_bytes,
                        double max_frame_size_bytes, double var_noise);
  double GetFrameDelayVariationEstimateSizeBased(double frame_size_variation_bytes) const {
    return estimate_[0] * frame_size_variation_bytes;
  }
  double GetFrameDelayVariationEstimateTotal(double frame_size_variation_bytes) const {
    return GetFrameDelayVariationEstimateSizeBased(frame_size_variation_bytes) + estimate_[1];
  }

 private:
  double estimate_[2];
  double estimate_cov_[2][2];
  double process_noise_cov_diag_[2];
};

// video/timing/inter_frame_delay_variation_calculator.cc.
class InterFrameDelayVariationCalculator {
 public:
  std::optional<TimeDelta> Calculate(uint32_t rtp_timestamp, Timestamp now);
  void Reset();

 private:
  SeqNumUnwrapper<uint32_t> unwrapper_;
  std::optional<Timestamp> prev_wall_clock_;
  int64_t prev_rtp_timestamp_unwrapped_ = 0;
};

// rtc_base/rolling_accumulator.h's RollingAccumulator<uint64_t>, mean only: a window of the last
// kWindow samples whose mean is rtc_base/numerics/running_statistics.h's Welford update, with its
// reciprocal when the oldest sample leaves. Not sum / count: the two can differ in the last bit,
// which moves a truncated mean by 1 (#674).
class RollingMean {
 public:
  static constexpr size_t kWindow = 30;  // JitterEstimator's fps_counter_(30).
  void Reset();
  void AddSample(uint64_t sample);
  double ComputeMean() const { return size_ == 0 ? 0 : mean_; }  // GetMean().value_or(0).

 private:
  std::array<uint64_t, kWindow> samples_{};
  size_t next_index_ = 0;
  int64_t size_ = 0;
  double mean_ = 0;
};

// video/timing/jitter_estimator.cc with its default Config.
class JitterEstimator {
 public:
  JitterEstimator() { Reset(); }
  void Reset();
  void UpdateEstimate(TimeDelta frame_delay, DataSize frame_size, Timestamp now);
  TimeDelta GetEstimate(Timestamp now);
  void FrameNacked(Timestamp now);
  void UpdateRtt(TimeDelta rtt) { rtt_filter_.Update(rtt); }
  // Diagnostics.
  double var_noise_ms2() const { return var_noise_ms2_; }
  double frame_rate_hz() const;
  int nack_count() const { return nack_count_; }

 private:
  void EstimateRandomJitter(double d_dt, Timestamp now);
  double NoiseThreshold() const;
  TimeDelta CalculateEstimate();
  // GetFrameRate(): a Frequency, so whole millihertz.
  int64_t GetFrameRateMilliHz() const;

  FrameDelayVariationKalmanFilter kalman_filter_;
  double avg_frame_size_bytes_ = 0;
  double var_frame_size_bytes2_ = 0;
  double max_frame_size_bytes_ = 0;
  double startup_frame_size_sum_bytes_ = 0;
  size_t startup_frame_size_count_ = 0;
  std::optional<Timestamp> last_update_time_;
  std::optional<TimeDelta> prev_estimate_;
  std::optional<DataSize> prev_frame_size_;
  double avg_noise_ms_ = 0;
  double var_noise_ms2_ = 0;
  size_t alpha_count_ = 1;
  TimeDelta filter_jitter_estimate_;
  size_t startup_count_ = 0;
  Timestamp latest_nack_ = Timestamp::Micros(0);
  int nack_count_ = 0;
  RttFilter rtt_filter_;
  RollingMean fps_counter_;  // RollingAccumulator<uint64_t>(30): frame periods in us.
};

// video/timing/timestamp_extrapolator.cc with its default Config.
class TimestampExtrapolator {
 public:
  explicit TimestampExtrapolator(Timestamp start = Timestamp::Micros(0)) { Reset(start); }
  void Reset(Timestamp start);
  void Update(Timestamp now, uint32_t ts90khz);
  std::optional<Timestamp> ExtrapolateLocalTime(uint32_t ts90khz) const;
  uint64_t resets() const { return resets_; }

 private:
  void SoftReset();
  bool OutlierDetection(double residual);
  bool DelayChangeDetection(double residual);
  double w_[2];
  double p_[2][2];
  Timestamp start_, prev_;
  std::optional<int64_t> first_unwrapped_timestamp_;
  SeqNumUnwrapper<uint32_t> unwrapper_;
  std::optional<int64_t> prev_unwrapped_timestamp_;
  int packet_count_ = 0;
  double residual_mean_ = 0, residual_variance_ = 0;
  int outliers_consecutive_count_ = 0;
  double detector_accumulator_pos_ = 0, detector_accumulator_neg_ = 0;
  uint64_t resets_ = 0;
};

// modules/video_coding/timing/decode_time_percentile_filter.cc.
class DecodeTimePercentileFilter {
 public:
  void AddSample(int64_t decode_time_ms, int64_t now_ms);
  int64_t GetPercentileMs() const;

 private:
  int ignored_sample_count_ = 0;
  std::multiset<int64_t> filter_;
  struct Sample {
    int64_t decode_time_ms, sample_time_ms;
  };
  std::deque<Sample> history_;
};

struct VcmTimingConfig {
  TimeDelta render_delay = TimeDelta::Millis(10);  // VideoDelayTimings::kDefaultRenderDelay.
  TimeDelta min_playout_delay = TimeDelta::Zero();
  TimeDelta max_playout_delay = TimeDelta::Seconds(10);
};

// modules/video_coding/timing/timing.cc (VCMTiming) driving
// DefaultVideoJitterTiming.
class VcmTiming : public MetricsSource {
 public:
  explicit VcmTiming(VcmTimingConfig cfg = {});
  void Reset(Timestamp now);
  void set_min_playout_delay(TimeDelta d) { min_playout_delay_ = d; }
  void set_playout_delay(TimeDelta min, TimeDelta max) {
    min_playout_delay_ = min;
    cfg_.max_playout_delay = max;
  }
  // Sets the jitter delay; the first one also sets the current delay.
  void SetMinimumDelay(TimeDelta d);
  // A complete frame entered the frame buffer.
  void OnCompleteFrame(uint32_t rtp_timestamp, Timestamp receive_time, bool was_retransmitted);
  // A temporal unit is released for decoding.
  void OnDecodableTemporalUnit(uint32_t rtp_timestamp, DataSize size, Timestamp receive_time,
                               bool was_retransmitted, Timestamp now);
  void OnNetworkUpdate(TimeDelta rtt) { jitter_estimator_.UpdateRtt(rtt); }
  void UpdateCurrentDelay(Timestamp render_time, Timestamp actual_decode_time);
  void UpdateDecodeTimeEstimate(TimeDelta decode_time, Timestamp now);
  // Timestamp::Micros(0) means "render as soon as possible" (low-latency path).
  Timestamp RenderTime(uint32_t rtp_timestamp, Timestamp now) const;
  bool UseLowLatencyRendering() const;
  TimeDelta TargetDelay() const;
  TimeDelta minimum_delay() const { return minimum_delay_; }
  TimeDelta current_delay() const { return current_delay_; }
  TimeDelta estimated_max_decode_time() const { return estimated_max_decode_time_; }
  TimeDelta render_delay() const { return cfg_.render_delay; }
  TimeDelta min_playout_delay() const { return min_playout_delay_; }
  TimeDelta max_playout_delay() const { return cfg_.max_playout_delay; }
  const JitterEstimator& jitter_estimator() const { return jitter_estimator_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  VcmTimingConfig cfg_;
  TimestampExtrapolator ts_extrapolator_;
  JitterEstimator jitter_estimator_;
  InterFrameDelayVariationCalculator ifdv_calculator_;
  DecodeTimePercentileFilter decode_time_filter_;
  TimeDelta minimum_delay_;
  TimeDelta estimated_max_decode_time_;
  TimeDelta min_playout_delay_;
  TimeDelta current_delay_;
  uint64_t num_decoded_frames_ = 0;
  uint64_t resets_ = 0;
  Histogram target_delay_ms_, current_delay_ms_;
};

}  // namespace sm::timing
