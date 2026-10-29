#include "sm/timing/vcm_timing.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace sm::timing {
namespace {

// libwebrtc's unit arithmetic (api/units, rtc_base/units/unit_base.h):
// TimeDelta::ms() rounds to the nearest millisecond, halves away from zero;
// TimeDelta * double and TimeDelta / double round to the nearest
// microsecond; TimeDelta::Millis(double) truncates to the microsecond.
int64_t RoundMs(TimeDelta d) {
  const int64_t us = d.us();
  return us >= 0 ? (us + 500) / 1000 : -((-us + 500) / 1000);
}
TimeDelta Scale(TimeDelta d, double f) { return TimeDelta::Micros(std::llround(static_cast<double>(d.us()) * f)); }
// Frequency::hertz<double>() and TimeDelta::seconds<double>() (unit_base.h ToFraction): times 1/denominator.
double MilliHzToHz(int64_t mhz) { return static_cast<double>(mhz) * (1 / 1000.0); }

// --- rtt_filter.cc ---
constexpr TimeDelta kMaxRtt = TimeDelta::Seconds(3);
constexpr uint32_t kFilterFactorMax = 35;
constexpr double kJumpStddev = 2.5;
constexpr double kDriftStdDev = 3.5;

// --- frame_delay_variation_kalman_filter.cc ---
constexpr double kMaxBandwidth = 0.000001;  // 1 / bytes per ms.

// --- jitter_estimator.cc ---
constexpr size_t kFrameProcessingStartupCount = 30;
constexpr size_t kFramesUntilSizeFiltering = 5;
constexpr double kInitialAvgAndMaxFrameSizeBytes = 500.0;
constexpr double kPhi = 0.97;
constexpr double kPsi = 0.9999;
constexpr double kNumStdDevDelayClamp = 3.5;
constexpr double kNumStdDevDelayOutlier = 15.0;
constexpr double kNumStdDevSizeOutlier = 3.0;
constexpr double kCongestionRejectionFactor = -0.25;
constexpr size_t kAlphaCountMax = 400;
constexpr double kNoiseStdDevs = 2.33;
constexpr double kNoiseStdDevOffset = 30.0;
constexpr TimeDelta kMinJitterEstimate = TimeDelta::Millis(1);
constexpr TimeDelta kMaxJitterEstimate = TimeDelta::Seconds(10);
constexpr TimeDelta kOperatingSystemJitter = TimeDelta::Millis(10);
constexpr TimeDelta kNackCountTimeout = TimeDelta::Seconds(60);
constexpr int kNackLimit = 3;
constexpr double kRttMult = 0.9;
constexpr TimeDelta kRttMultAddCap = TimeDelta::Millis(200);
constexpr int64_t kMaxFramerateEstimateMilliHz = 200'000;

// --- timestamp_extrapolator.cc (and its default Config) ---
constexpr double kLambda = 1;
constexpr int kStartUpFilterDelayInPackets = 2;
constexpr int kMinimumSamplesToLogEstimatedClockDrift = 3000;
constexpr double kP11 = 1e10;
constexpr double kStartResidualVariance = 3000.0 * 3000.0;
constexpr TimeDelta kHardResetTimeout = TimeDelta::Seconds(10);
constexpr int64_t kHardResetRtpTimestampJumpThreshold = 900'000;
constexpr int kOutlierRejectionStartupDelay = 300;
constexpr int kOutlierRejectionMaxConsecutive = 150;
constexpr double kOutlierRejectionForgettingFactor = 0.999;
constexpr double kOutlierRejectionStddev = 2.0;
constexpr int kAlarmThreshold = 60000;
constexpr int kAccDrift = 6600;
constexpr int kAccMaxError = 7000;
constexpr double kP00 = 1.0;

// --- decode_time_percentile_filter.cc ---
constexpr int kIgnoredSampleCount = 5;
constexpr double kPercentile = 0.95;
constexpr int64_t kTimeLimitMs = 10000;

// --- timing.cc ---
constexpr TimeDelta kLowLatencyStreamMaxPlayoutDelayThreshold = TimeDelta::Millis(500);

}  // namespace

// --- RollingMean -------------------------------------------------------------

void RollingMean::Reset() {
  next_index_ = 0;
  size_ = 0;
  mean_ = 0;
}

void RollingMean::AddSample(uint64_t sample) {
  if (size_ == static_cast<int64_t>(kWindow)) {
    // RunningStatistics::RemoveSample of the oldest sample.
    --size_;
    const double delta = static_cast<double>(samples_[next_index_]) - mean_;
    mean_ -= delta / static_cast<double>(size_);
  }
  samples_[next_index_] = sample;
  // RunningStatistics::AddSample: Welford's incremental update.
  ++size_;
  const double delta = static_cast<double>(sample) - mean_;
  mean_ += delta / static_cast<double>(size_);
  next_index_ = (next_index_ + 1) % kWindow;
}

// --- RttFilter ---------------------------------------------------------------

void RttFilter::Reset() {
  got_non_zero_update_ = false;
  avg_rtt_ = TimeDelta::Zero();
  var_rtt_ = 0;
  max_rtt_ = TimeDelta::Zero();
  filt_fact_count_ = 1;
  jump_buf_.clear();
  drift_buf_.clear();
}

void RttFilter::Update(TimeDelta rtt) {
  if (!got_non_zero_update_) {
    if (rtt.IsZero()) return;
    got_non_zero_update_ = true;
  }
  if (rtt > kMaxRtt) rtt = kMaxRtt;
  double filt_factor = 0;
  if (filt_fact_count_ > 1) filt_factor = static_cast<double>(filt_fact_count_ - 1) / filt_fact_count_;
  ++filt_fact_count_;
  if (filt_fact_count_ > kFilterFactorMax) filt_fact_count_ = kFilterFactorMax;
  const TimeDelta old_avg = avg_rtt_;
  const int64_t old_var = var_rtt_;
  avg_rtt_ = Scale(avg_rtt_, filt_factor) + Scale(rtt, 1 - filt_factor);
  const int64_t delta_ms = RoundMs(rtt - avg_rtt_);
  var_rtt_ = static_cast<int64_t>(filt_factor * static_cast<double>(var_rtt_) +
                                  (1 - filt_factor) * static_cast<double>(delta_ms * delta_ms));
  max_rtt_ = std::max(rtt, max_rtt_);
  if (!JumpDetection(rtt) || !DriftDetection(rtt)) {
    avg_rtt_ = old_avg;
    var_rtt_ = old_var;
  }
}

bool RttFilter::JumpDetection(TimeDelta rtt) {
  const TimeDelta diff_from_avg = avg_rtt_ - rtt;
  const TimeDelta jump_threshold = TimeDelta::MillisF(kJumpStddev * std::sqrt(static_cast<double>(var_rtt_)));
  if (TimeDelta::Micros(std::llabs(diff_from_avg.us())) > jump_threshold) {
    const bool positive_diff = diff_from_avg >= TimeDelta::Zero();
    if (!jump_buf_.empty() && positive_diff != last_jump_positive_) jump_buf_.clear();
    if (jump_buf_.size() < kMaxDriftJumpCount) {
      jump_buf_.push_back(rtt);
      last_jump_positive_ = positive_diff;
    }
    if (jump_buf_.size() >= kMaxDriftJumpCount) {
      ShortRttFilter(jump_buf_);
      filt_fact_count_ = kMaxDriftJumpCount + 1;
      jump_buf_.clear();
    } else {
      return false;
    }
  } else {
    jump_buf_.clear();
  }
  return true;
}

bool RttFilter::DriftDetection(TimeDelta rtt) {
  const TimeDelta drift_threshold = TimeDelta::MillisF(kDriftStdDev * std::sqrt(static_cast<double>(var_rtt_)));
  if (max_rtt_ - avg_rtt_ > drift_threshold) {
    if (drift_buf_.size() < kMaxDriftJumpCount) drift_buf_.push_back(rtt);
    if (drift_buf_.size() >= kMaxDriftJumpCount) {
      ShortRttFilter(drift_buf_);
      filt_fact_count_ = kMaxDriftJumpCount + 1;
      drift_buf_.clear();
    }
  } else {
    drift_buf_.clear();
  }
  return true;
}

void RttFilter::ShortRttFilter(const std::deque<TimeDelta>& buf) {
  max_rtt_ = TimeDelta::Zero();
  avg_rtt_ = TimeDelta::Zero();
  for (const TimeDelta& rtt : buf) {
    if (rtt > max_rtt_) max_rtt_ = rtt;
    avg_rtt_ += rtt;
  }
  // TimeDelta / double rounds to the nearest microsecond.
  avg_rtt_ = TimeDelta::Micros(std::llround(static_cast<double>(avg_rtt_.us()) / static_cast<double>(buf.size())));
}

// --- FrameDelayVariationKalmanFilter -----------------------------------------

FrameDelayVariationKalmanFilter::FrameDelayVariationKalmanFilter() {
  estimate_[0] = 1 / (512e3 / 8);
  estimate_[1] = 0;
  estimate_cov_[0][0] = 1e-4;
  estimate_cov_[1][1] = 1e2;
  estimate_cov_[0][1] = estimate_cov_[1][0] = 0;
  process_noise_cov_diag_[0] = 2.5e-10;
  process_noise_cov_diag_[1] = 1e-10;
}

void FrameDelayVariationKalmanFilter::PredictAndUpdate(double frame_delay_variation_ms,
                                                       double frame_size_variation_bytes,
                                                       double max_frame_size_bytes, double var_noise) {
  if (max_frame_size_bytes < 1) return;
  if (var_noise <= 0.0) return;
  estimate_cov_[0][0] += process_noise_cov_diag_[0];
  estimate_cov_[1][1] += process_noise_cov_diag_[1];
  const double innovation =
      frame_delay_variation_ms - GetFrameDelayVariationEstimateTotal(frame_size_variation_bytes);
  double estim_cov_times_obs[2];
  estim_cov_times_obs[0] = estimate_cov_[0][0] * frame_size_variation_bytes + estimate_cov_[0][1];
  estim_cov_times_obs[1] = estimate_cov_[1][0] * frame_size_variation_bytes + estimate_cov_[1][1];
  double observation_noise_stddev =
      (300.0 * std::exp(-std::fabs(frame_size_variation_bytes) / (1e0 * max_frame_size_bytes)) + 1) *
      std::sqrt(var_noise);
  if (observation_noise_stddev < 1.0) observation_noise_stddev = 1.0;
  // libwebrtc adds the standard deviation, not the variance (its TODO notes it).
  const double innovation_var =
      frame_size_variation_bytes * estim_cov_times_obs[0] + estim_cov_times_obs[1] + observation_noise_stddev;
  if ((innovation_var < 1e-9 && innovation_var >= 0) || (innovation_var > -1e-9 && innovation_var <= 0)) return;
  double kalman_gain[2];
  kalman_gain[0] = estim_cov_times_obs[0] / innovation_var;
  kalman_gain[1] = estim_cov_times_obs[1] / innovation_var;
  estimate_[0] += kalman_gain[0] * innovation;
  estimate_[1] += kalman_gain[1] * innovation;
  if (estimate_[0] < kMaxBandwidth) estimate_[0] = kMaxBandwidth;
  const double t00 = estimate_cov_[0][0];
  const double t01 = estimate_cov_[0][1];
  estimate_cov_[0][0] = (1 - kalman_gain[0] * frame_size_variation_bytes) * t00 - kalman_gain[0] * estimate_cov_[1][0];
  estimate_cov_[0][1] = (1 - kalman_gain[0] * frame_size_variation_bytes) * t01 - kalman_gain[0] * estimate_cov_[1][1];
  estimate_cov_[1][0] = estimate_cov_[1][0] * (1 - kalman_gain[1]) - kalman_gain[1] * frame_size_variation_bytes * t00;
  estimate_cov_[1][1] = estimate_cov_[1][1] * (1 - kalman_gain[1]) - kalman_gain[1] * frame_size_variation_bytes * t01;
}

// --- InterFrameDelayVariationCalculator ---------------------------------------

void InterFrameDelayVariationCalculator::Reset() {
  // As libwebrtc, the unwrapper keeps its state.
  prev_wall_clock_ = std::nullopt;
  prev_rtp_timestamp_unwrapped_ = 0;
}

std::optional<TimeDelta> InterFrameDelayVariationCalculator::Calculate(uint32_t rtp_timestamp, Timestamp now) {
  const int64_t rtp_timestamp_unwrapped = unwrapper_.Unwrap(rtp_timestamp);
  if (!prev_wall_clock_) {
    prev_wall_clock_ = now;
    prev_rtp_timestamp_unwrapped_ = rtp_timestamp_unwrapped;
    return TimeDelta::Zero();
  }
  const uint32_t cropped_prev = static_cast<uint32_t>(prev_rtp_timestamp_unwrapped_);
  if (rtp_timestamp_unwrapped < prev_rtp_timestamp_unwrapped_ || !AheadOf<uint32_t>(rtp_timestamp, cropped_prev)) {
    return std::nullopt;
  }
  const TimeDelta delta_wall = now - *prev_wall_clock_;
  const int64_t d_rtp_ticks = rtp_timestamp_unwrapped - prev_rtp_timestamp_unwrapped_;
  // `d_rtp_ticks / k90kHz`: int64 / Frequency (frequency.h) is an integer division in microseconds and
  // millihertz, so it truncates.
  constexpr int64_t k90kHzInMilliHz = 90'000'000;
  const TimeDelta delta_rtp = TimeDelta::Micros(d_rtp_ticks * 1'000'000'000 / k90kHzInMilliHz);
  prev_wall_clock_ = now;
  prev_rtp_timestamp_unwrapped_ = rtp_timestamp_unwrapped;
  return delta_wall - delta_rtp;
}

// --- JitterEstimator -----------------------------------------------------------

void JitterEstimator::Reset() {
  avg_frame_size_bytes_ = kInitialAvgAndMaxFrameSizeBytes;
  max_frame_size_bytes_ = kInitialAvgAndMaxFrameSizeBytes;
  var_frame_size_bytes2_ = 100;
  last_update_time_ = std::nullopt;
  prev_estimate_ = std::nullopt;
  prev_frame_size_ = std::nullopt;
  avg_noise_ms_ = 0.0;
  var_noise_ms2_ = 4.0;
  alpha_count_ = 1;
  filter_jitter_estimate_ = TimeDelta::Zero();
  latest_nack_ = Timestamp::Micros(0);
  nack_count_ = 0;
  startup_frame_size_sum_bytes_ = 0;
  startup_frame_size_count_ = 0;
  startup_count_ = 0;
  rtt_filter_.Reset();
  fps_counter_.Reset();
  kalman_filter_ = FrameDelayVariationKalmanFilter();
}

void JitterEstimator::UpdateEstimate(TimeDelta frame_delay, DataSize frame_size, Timestamp now) {
  if (frame_size.bytes() == 0) return;
  const double size_bytes = static_cast<double>(frame_size.bytes());
  const double delta_frame_bytes =
      size_bytes - static_cast<double>(prev_frame_size_.value_or(DataSize::Zero()).bytes());
  if (startup_frame_size_count_ < kFramesUntilSizeFiltering) {
    startup_frame_size_sum_bytes_ += size_bytes;
    ++startup_frame_size_count_;
  } else if (startup_frame_size_count_ == kFramesUntilSizeFiltering) {
    avg_frame_size_bytes_ = startup_frame_size_sum_bytes_ / static_cast<double>(startup_frame_size_count_);
    ++startup_frame_size_count_;
  }
  const double avg_frame_size_bytes = kPhi * avg_frame_size_bytes_ + (1 - kPhi) * size_bytes;
  const double deviation_size_bytes = 2 * std::sqrt(var_frame_size_bytes2_);
  if (size_bytes < avg_frame_size_bytes_ + deviation_size_bytes) avg_frame_size_bytes_ = avg_frame_size_bytes;
  const double delta_bytes = size_bytes - avg_frame_size_bytes;
  var_frame_size_bytes2_ = std::max(kPhi * var_frame_size_bytes2_ + (1 - kPhi) * (delta_bytes * delta_bytes), 1.0);
  max_frame_size_bytes_ = std::max(kPsi * max_frame_size_bytes_, size_bytes);

  if (!prev_frame_size_) {
    prev_frame_size_ = frame_size;
    return;
  }
  prev_frame_size_ = frame_size;

  const TimeDelta max_time_deviation = TimeDelta::MillisF(kNumStdDevDelayClamp * std::sqrt(var_noise_ms2_) + 0.5);
  frame_delay = std::clamp(frame_delay, -max_time_deviation, max_time_deviation);
  const double frame_delay_ms = static_cast<double>(RoundMs(frame_delay));
  const double delay_deviation_ms =
      frame_delay_ms - kalman_filter_.GetFrameDelayVariationEstimateTotal(delta_frame_bytes);
  const bool abs_delay_is_not_outlier =
      std::fabs(delay_deviation_ms) < kNumStdDevDelayOutlier * std::sqrt(var_noise_ms2_);
  const bool size_is_positive_outlier =
      size_bytes > avg_frame_size_bytes_ + kNumStdDevSizeOutlier * std::sqrt(var_frame_size_bytes2_);
  if (abs_delay_is_not_outlier || size_is_positive_outlier) {
    const bool is_not_congested = delta_frame_bytes > kCongestionRejectionFactor * max_frame_size_bytes_;
    // estimate_noise_when_congested defaults to true.
    EstimateRandomJitter(delay_deviation_ms, now);
    if (is_not_congested) {
      kalman_filter_.PredictAndUpdate(frame_delay_ms, delta_frame_bytes, max_frame_size_bytes_, var_noise_ms2_);
    }
  } else {
    const double num_stddev = delay_deviation_ms >= 0 ? kNumStdDevDelayOutlier : -kNumStdDevDelayOutlier;
    EstimateRandomJitter(num_stddev * std::sqrt(var_noise_ms2_), now);
  }
  if (startup_count_ >= kFrameProcessingStartupCount) {
    filter_jitter_estimate_ = CalculateEstimate();  // PostProcessEstimate.
  } else {
    ++startup_count_;
  }
}

void JitterEstimator::FrameNacked(Timestamp now) {
  if (nack_count_ < kNackLimit) ++nack_count_;
  latest_nack_ = now;
}

void JitterEstimator::EstimateRandomJitter(double d_dt, Timestamp now) {
  if (last_update_time_) {
    fps_counter_.AddSample(static_cast<uint64_t>((now - *last_update_time_).us()));
  }
  last_update_time_ = now;
  if (alpha_count_ == 0) return;
  double alpha = static_cast<double>(alpha_count_ - 1) / static_cast<double>(alpha_count_);
  ++alpha_count_;
  if (alpha_count_ > kAlphaCountMax) alpha_count_ = kAlphaCountMax;
  // Scale the alpha weight relative to a 30 fps stream (Frequency / Frequency, in millihertz).
  const int64_t fps_mhz = GetFrameRateMilliHz();
  if (fps_mhz > 0) {
    double rate_scale = 30'000.0 / static_cast<double>(fps_mhz);
    if (alpha_count_ < kFrameProcessingStartupCount) {
      rate_scale = (static_cast<double>(alpha_count_) * rate_scale +
                    static_cast<double>(kFrameProcessingStartupCount - alpha_count_)) /
                   static_cast<double>(kFrameProcessingStartupCount);
    }
    alpha = std::pow(alpha, rate_scale);
  }
  const double avg_noise_ms = alpha * avg_noise_ms_ + (1 - alpha) * d_dt;
  const double var_noise_ms2 =
      alpha * var_noise_ms2_ + (1 - alpha) * (d_dt - avg_noise_ms_) * (d_dt - avg_noise_ms_);
  avg_noise_ms_ = avg_noise_ms;
  var_noise_ms2_ = var_noise_ms2;
  if (var_noise_ms2_ < 1.0) var_noise_ms2_ = 1.0;
}

double JitterEstimator::NoiseThreshold() const {
  double noise_threshold_ms = kNoiseStdDevs * std::sqrt(var_noise_ms2_) - kNoiseStdDevOffset;
  if (noise_threshold_ms < 1.0) noise_threshold_ms = 1.0;
  return noise_threshold_ms;
}

TimeDelta JitterEstimator::CalculateEstimate() {
  const double worst_case_frame_size_deviation_bytes = max_frame_size_bytes_ - avg_frame_size_bytes_;
  const double ret_ms =
      kalman_filter_.GetFrameDelayVariationEstimateSizeBased(worst_case_frame_size_deviation_bytes) +
      NoiseThreshold();
  TimeDelta ret = TimeDelta::MillisF(ret_ms);
  if (ret < kMinJitterEstimate) {
    ret = prev_estimate_.value_or(kMinJitterEstimate);
  } else if (ret > kMaxJitterEstimate) {
    ret = kMaxJitterEstimate;
  }
  prev_estimate_ = ret;
  return ret;
}

TimeDelta JitterEstimator::GetEstimate(Timestamp now) {
  TimeDelta jitter = CalculateEstimate() + kOperatingSystemJitter;
  if (now - latest_nack_ > kNackCountTimeout) nack_count_ = 0;
  if (filter_jitter_estimate_ > jitter) jitter = filter_jitter_estimate_;
  if (nack_count_ >= kNackLimit) jitter += std::min(Scale(rtt_filter_.Rtt(), kRttMult), kRttMultAddCap);
  constexpr int64_t kJitterScaleLowThresholdMilliHz = 5'000;
  constexpr int64_t kJitterScaleHighThresholdMilliHz = 10'000;
  const int64_t fps_mhz = GetFrameRateMilliHz();
  if (fps_mhz < kJitterScaleLowThresholdMilliHz) {
    if (fps_mhz == 0) return std::max(TimeDelta::Zero(), jitter);
    return TimeDelta::Zero();
  }
  if (fps_mhz < kJitterScaleHighThresholdMilliHz) {
    // `(1.0 / (high - low)) * (fps - low) * jitter`: 1 / Frequency is a TimeDelta in whole microseconds,
    // TimeDelta * Frequency is hertz<double>() * seconds<double>(), and double * TimeDelta rounds.
    const int64_t period_us = 1'000'000'000 / (kJitterScaleHighThresholdMilliHz - kJitterScaleLowThresholdMilliHz);
    const double factor =
        MilliHzToHz(fps_mhz - kJitterScaleLowThresholdMilliHz) * (static_cast<double>(period_us) * (1 / 1e6));
    jitter = Scale(jitter, factor);
  }
  return std::max(TimeDelta::Zero(), jitter);
}

double JitterEstimator::frame_rate_hz() const { return MilliHzToHz(GetFrameRateMilliHz()); }

int64_t JitterEstimator::GetFrameRateMilliHz() const {
  // TimeDelta::Micros(double) truncates.
  const int64_t mean_us = static_cast<int64_t>(fps_counter_.ComputeMean());
  if (mean_us <= 0) return 0;
  // `1 / mean_frame_period` (frequency.h): an integer division to millihertz.
  return std::min<int64_t>(1'000'000'000 / mean_us, kMaxFramerateEstimateMilliHz);
}

// --- TimestampExtrapolator -------------------------------------------------------

void TimestampExtrapolator::Reset(Timestamp start) {
  start_ = start;
  prev_ = start_;
  first_unwrapped_timestamp_ = std::nullopt;
  prev_unwrapped_timestamp_ = std::nullopt;
  w_[0] = 90.0;
  w_[1] = 0;
  p_[0][0] = kP00;
  p_[1][1] = kP11;
  p_[0][1] = p_[1][0] = 0;
  unwrapper_ = SeqNumUnwrapper<uint32_t>();
  packet_count_ = 0;
  residual_mean_ = 0.0;
  residual_variance_ = kStartResidualVariance;
  outliers_consecutive_count_ = 0;
  detector_accumulator_pos_ = 0;
  detector_accumulator_neg_ = 0;
}

void TimestampExtrapolator::Update(Timestamp now, uint32_t ts90khz) {
  if (now - prev_ > kHardResetTimeout) {
    Reset(now);
    ++resets_;
  } else {
    prev_ = now;
  }
  const int64_t unwrapped_ts90khz = unwrapper_.Unwrap(ts90khz);
  // Hard reset on a large RTP timestamp jump (outlier rejection is on by
  // default). `prev_` is `now` here, as in libwebrtc, so the expected change is 0.
  if (prev_unwrapped_timestamp_) {
    const int64_t expected_rtp_diff = static_cast<int64_t>(static_cast<double>(RoundMs(now - prev_)) * w_[0]);
    const int64_t actual_rtp_diff = unwrapped_ts90khz - *prev_unwrapped_timestamp_;
    const int64_t rtp_jump = actual_rtp_diff - expected_rtp_diff;
    if (std::llabs(rtp_jump) > kHardResetRtpTimestampJumpThreshold) {
      Reset(now);
      ++resets_;
    }
  }
  const double t_ms = static_cast<double>(RoundMs(now - start_));
  if (!first_unwrapped_timestamp_) {
    w_[1] = -w_[0] * t_ms;
    first_unwrapped_timestamp_ = unwrapped_ts90khz;
  }
  const double residual =
      (static_cast<double>(unwrapped_ts90khz) - static_cast<double>(*first_unwrapped_timestamp_)) - t_ms * w_[0] -
      w_[1];
  if (OutlierDetection(residual)) {
    ++outliers_consecutive_count_;
    if (outliers_consecutive_count_ <= kOutlierRejectionMaxConsecutive) return;
    SoftReset();
  }
  outliers_consecutive_count_ = 0;
  if (DelayChangeDetection(residual) && packet_count_ >= kStartUpFilterDelayInPackets) SoftReset();
  // With outlier rejection enabled, reordered frames are not dropped here.
  double k[2];
  k[0] = p_[0][0] * t_ms + p_[0][1];
  k[1] = p_[1][0] * t_ms + p_[1][1];
  const double tpt = kLambda + t_ms * k[0] + k[1];
  k[0] /= tpt;
  k[1] /= tpt;
  w_[0] = w_[0] + k[0] * residual;
  w_[1] = w_[1] + k[1] * residual;
  const double p00 = 1 / kLambda * (p_[0][0] - (k[0] * t_ms * p_[0][0] + k[0] * p_[1][0]));
  const double p01 = 1 / kLambda * (p_[0][1] - (k[0] * t_ms * p_[0][1] + k[0] * p_[1][1]));
  p_[1][0] = 1 / kLambda * (p_[1][0] - (k[1] * t_ms * p_[0][0] + k[1] * p_[1][0]));
  p_[1][1] = 1 / kLambda * (p_[1][1] - (k[1] * t_ms * p_[0][1] + k[1] * p_[1][1]));
  p_[0][0] = p00;
  p_[0][1] = p01;
  prev_unwrapped_timestamp_ = unwrapped_ts90khz;
  if (packet_count_ < kStartUpFilterDelayInPackets || packet_count_ < kMinimumSamplesToLogEstimatedClockDrift) {
    ++packet_count_;
  }
}

std::optional<Timestamp> TimestampExtrapolator::ExtrapolateLocalTime(uint32_t ts90khz) const {
  const int64_t unwrapped_ts90khz = unwrapper_.PeekUnwrap(ts90khz);
  if (!first_unwrapped_timestamp_) return std::nullopt;
  if (packet_count_ < kStartUpFilterDelayInPackets) {
    constexpr double kRtpTicksPerMs = 90;
    const TimeDelta diff =
        TimeDelta::MillisF(static_cast<double>(unwrapped_ts90khz - *prev_unwrapped_timestamp_) / kRtpTicksPerMs);
    if (prev_.us() + diff.us() < 0) return std::nullopt;
    return prev_ + diff;
  }
  if (w_[0] < 1e-3) return start_;
  const double timestamp_diff = static_cast<double>(unwrapped_ts90khz - *first_unwrapped_timestamp_);
  const TimeDelta diff = TimeDelta::Millis(static_cast<int64_t>((timestamp_diff - w_[1]) / w_[0] + 0.5));
  if (start_.us() + diff.us() < 0) return std::nullopt;
  return start_ + diff;
}

void TimestampExtrapolator::SoftReset() {
  // reset_full_cov_on_alarm defaults to false.
  p_[1][1] = kP11;
}

bool TimestampExtrapolator::OutlierDetection(double residual) {
  if (packet_count_ >= kOutlierRejectionStartupDelay) {
    const double threshold = kOutlierRejectionStddev * std::sqrt(residual_variance_);
    if (std::abs(residual - residual_mean_) > threshold) return true;
  }
  const double ff = kOutlierRejectionForgettingFactor;
  residual_mean_ = ff * residual_mean_ + (1.0 - ff) * residual;
  const double residual_deviation = residual - residual_mean_;
  residual_variance_ = std::max(ff * residual_variance_ + (1.0 - ff) * residual_deviation * residual_deviation, 1.0);
  return false;
}

bool TimestampExtrapolator::DelayChangeDetection(double residual) {
  const double acc_max_error = static_cast<double>(kAccMaxError);
  residual = residual > 0 ? std::min(residual, acc_max_error) : std::max(residual, -acc_max_error);
  detector_accumulator_pos_ = std::max(detector_accumulator_pos_ + residual - kAccDrift, 0.0);
  detector_accumulator_neg_ = std::min(detector_accumulator_neg_ + residual + kAccDrift, 0.0);
  if (detector_accumulator_pos_ > kAlarmThreshold || detector_accumulator_neg_ < -kAlarmThreshold) {
    detector_accumulator_pos_ = detector_accumulator_neg_ = 0;
    return true;
  }
  return false;
}

// --- DecodeTimePercentileFilter ----------------------------------------------------

void DecodeTimePercentileFilter::AddSample(int64_t decode_time_ms, int64_t now_ms) {
  if (ignored_sample_count_ < kIgnoredSampleCount) {
    ++ignored_sample_count_;
    return;
  }
  const int64_t capped_decode_ms = std::max<int64_t>(0, decode_time_ms);
  filter_.insert(capped_decode_ms);
  history_.push_back({capped_decode_ms, now_ms});
  while (!history_.empty() && now_ms - history_.front().sample_time_ms > kTimeLimitMs) {
    filter_.erase(filter_.find(history_.front().decode_time_ms));
    history_.pop_front();
  }
}

int64_t DecodeTimePercentileFilter::GetPercentileMs() const {
  // rtc_base/numerics/percentile_filter.h: element floor(p * (n - 1)), 0 when empty.
  if (filter_.empty()) return 0;
  const auto index = static_cast<std::ptrdiff_t>(kPercentile * static_cast<double>(filter_.size() - 1));
  return *std::next(filter_.begin(), index);
}

// --- VcmTiming ---------------------------------------------------------------------

VcmTiming::VcmTiming(VcmTimingConfig cfg) : cfg_(cfg), min_playout_delay_(cfg.min_playout_delay) {}

void VcmTiming::Reset(Timestamp now) {
  ts_extrapolator_.Reset(now);
  jitter_estimator_.Reset();
  decode_time_filter_ = DecodeTimePercentileFilter();
  minimum_delay_ = TimeDelta::Zero();
  estimated_max_decode_time_ = TimeDelta::Zero();
  min_playout_delay_ = cfg_.min_playout_delay;
  current_delay_ = TimeDelta::Zero();
  ++resets_;
}

void VcmTiming::SetMinimumDelay(TimeDelta d) {
  if (d != minimum_delay_) {
    minimum_delay_ = d;
    if (current_delay_.IsZero()) current_delay_ = minimum_delay_;
  }
}

void VcmTiming::OnCompleteFrame(uint32_t rtp_timestamp, Timestamp receive_time, bool was_retransmitted) {
  // WebRTC-IncomingTimestampOnMarkerBitOnly: one layer, so every frame is the last spatial layer.
  if (!was_retransmitted) ts_extrapolator_.Update(receive_time, rtp_timestamp);
}

void VcmTiming::OnDecodableTemporalUnit(uint32_t rtp_timestamp, DataSize size, Timestamp receive_time,
                                        bool was_retransmitted, Timestamp now) {
  if (was_retransmitted) {
    jitter_estimator_.FrameNacked(now);
    return;
  }
  if (auto ifdv = ifdv_calculator_.Calculate(rtp_timestamp, receive_time)) {
    jitter_estimator_.UpdateEstimate(*ifdv, size, now);
  }
  SetMinimumDelay(jitter_estimator_.GetEstimate(now));
}

void VcmTiming::UpdateCurrentDelay(Timestamp render_time, Timestamp actual_decode_time) {
  const TimeDelta target_delay = TargetDelay();
  const TimeDelta delayed = (actual_decode_time - render_time) + estimated_max_decode_time_ + cfg_.render_delay;
  if (RoundMs(delayed) < 0) return;
  if (current_delay_ + delayed <= target_delay) {
    current_delay_ += delayed;
  } else {
    current_delay_ = target_delay;
  }
  target_delay_ms_.Add(target_delay.ms_f());
  current_delay_ms_.Add(current_delay_.ms_f());
}

void VcmTiming::UpdateDecodeTimeEstimate(TimeDelta decode_time, Timestamp now) {
  decode_time_filter_.AddSample(RoundMs(decode_time), RoundMs(now - Timestamp::Micros(0)));
  ++num_decoded_frames_;
  estimated_max_decode_time_ = TimeDelta::Millis(decode_time_filter_.GetPercentileMs());
}

bool VcmTiming::UseLowLatencyRendering() const {
  return min_playout_delay_.IsZero() && cfg_.max_playout_delay <= kLowLatencyStreamMaxPlayoutDelayThreshold;
}

TimeDelta VcmTiming::TargetDelay() const {
  return std::max(min_playout_delay_, minimum_delay_ + estimated_max_decode_time_ + cfg_.render_delay);
}

Timestamp VcmTiming::RenderTime(uint32_t rtp_timestamp, Timestamp now) const {
  if (UseLowLatencyRendering()) return Timestamp::Micros(0);
  const std::optional<Timestamp> local_time = ts_extrapolator_.ExtrapolateLocalTime(rtp_timestamp);
  if (!local_time) return now;
  return *local_time + Clamp(current_delay_, min_playout_delay_, cfg_.max_playout_delay);
}

void VcmTiming::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("target_delay_ms", TargetDelay().ms_f());
  w.Gauge("current_delay_ms", current_delay_.ms_f());
  w.Gauge("jitter_delay_ms", minimum_delay_.ms_f());
  w.Gauge("decode_p95_ms", estimated_max_decode_time_.ms_f());
  w.Gauge("jitter_noise_std_ms", std::sqrt(jitter_estimator_.var_noise_ms2()));
  w.Gauge("jitter_fps", jitter_estimator_.frame_rate_hz());
  w.Counter("decoded_frames", num_decoded_frames_);
  w.Counter("resets", resets_);
  w.Counter("extrapolator_resets", ts_extrapolator_.resets());
  w.Hist("target_delay_hist_ms", target_delay_ms_);
  w.Hist("current_delay_hist_ms", current_delay_ms_);
}

}  // namespace sm::timing
