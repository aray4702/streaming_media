#include "sm/timing/video_jitter_estimator.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace sm::timing {

VideoJitterEstimator::VideoJitterEstimator(VideoJitterConfig cfg) : cfg_(cfg) { Reset(); }

void VideoJitterEstimator::Reset() {
  theta_[0] = 1.0 / (512e3 / 8.0);  // 1/capacity in ms per byte, start at 512 kbps.
  theta_[1] = 0;
  cov_[0][0] = 1e-4;
  cov_[1][1] = 1e2;
  cov_[0][1] = cov_[1][0] = 0;
  var_noise_ = 4.0;
  avg_frame_size_ = 0;
  max_frame_size_ = 0;
  var_frame_size_ = 100;
  alpha_count_ = 1;
  has_prev_ = false;
  frames_ = 0;
}

double VideoJitterEstimator::noise_std_ms() const { return std::sqrt(var_noise_); }

void VideoJitterEstimator::KalmanUpdate(double d, double ds) {
  // Process noise.
  const double q00 = 2.5e-10, q11 = 1e-10;
  cov_[0][0] += q00;
  cov_[1][1] += q11;
  double h[2] = {ds, 1.0};
  double mh[2] = {cov_[0][0] * h[0] + cov_[0][1] * h[1], cov_[1][0] * h[0] + cov_[1][1] * h[1]};
  double residual = d - (theta_[0] * h[0] + theta_[1]);
  // Measurement noise model: noise variance plus a term for frame size
  // irregularity (as in libwebrtc).
  double sigma = (300.0 * std::exp(-std::fabs(ds) / (1e0 * max_frame_size_ + 1)) + 1) * std::sqrt(var_noise_);
  if (sigma < 1.0) sigma = 1.0;
  double denom = sigma + h[0] * mh[0] + h[1] * mh[1];
  if (denom < 1e-9) return;
  double k[2] = {mh[0] / denom, mh[1] / denom};
  theta_[0] += k[0] * residual;
  theta_[1] += k[1] * residual;
  if (theta_[0] < 1e-6) theta_[0] = 1e-6;  // Capacity cannot be infinite/negative.
  double t00 = cov_[0][0], t01 = cov_[0][1], t10 = cov_[1][0], t11 = cov_[1][1];
  cov_[0][0] = (1 - k[0] * h[0]) * t00 - k[0] * h[1] * t10;
  cov_[0][1] = (1 - k[0] * h[0]) * t01 - k[0] * h[1] * t11;
  cov_[1][0] = (1 - k[1] * h[1]) * t10 - k[1] * h[0] * t00;
  cov_[1][1] = (1 - k[1] * h[1]) * t11 - k[1] * h[0] * t01;
}

void VideoJitterEstimator::Update(TimeDelta frame_delay, DataSize frame_size) {
  double size = static_cast<double>(frame_size.bytes());
  double d = frame_delay.ms_f();
  ++frames_;
  if (!has_prev_) {
    prev_frame_size_ = size;
    avg_frame_size_ = size;
    max_frame_size_ = size;
    has_prev_ = true;
    return;
  }
  double ds = size - prev_frame_size_;
  // Track frame size statistics; only non-key-sized frames update the average.
  double avg_new = cfg_.phi * avg_frame_size_ + (1 - cfg_.phi) * size;
  double dev = size - avg_new;
  double std_size = std::sqrt(var_frame_size_);
  if (dev < 2 * std_size || frames_ < 30) {
    avg_frame_size_ = avg_new;
    var_frame_size_ = std::max(cfg_.phi * var_frame_size_ + (1 - cfg_.phi) * dev * dev, 1.0);
  }
  max_frame_size_ = std::max(cfg_.psi * max_frame_size_, size);
  prev_frame_size_ = size;

  // Noise variance with outlier clipping.
  double residual = d - (theta_[0] * ds + theta_[1]);
  double max_dev = cfg_.outlier_std_devs * std::sqrt(var_noise_);
  bool outlier = std::fabs(residual) >= max_dev && frames_ > 30;
  double r = outlier ? std::copysign(max_dev, residual) : residual;
  double alpha = (alpha_count_ - 1) / alpha_count_;
  alpha_count_ = std::min(alpha_count_ + 1, cfg_.alpha_count_max);
  var_noise_ = std::max(alpha * var_noise_ + (1 - alpha) * (r - 0) * (r - 0), 1.0);
  if (!outlier) KalmanUpdate(d, ds);
  jitter_ms_.Add(JitterDelay().ms_f());
}

void VideoJitterEstimator::OnFrame(uint32_t rtp_ts, Timestamp arrival, DataSize size) {
  int64_t ts = unwrapper_.Unwrap(rtp_ts);
  if (frames_ == 0) {
    prev_ts_ = ts;
    prev_arrival_ = arrival;
    Update(TimeDelta::Zero(), size);
    return;
  }
  TimeDelta d = (arrival - prev_arrival_) - TimeDelta::Micros((ts - prev_ts_) * 1000 / 90);
  prev_ts_ = ts;
  prev_arrival_ = arrival;
  Update(d, size);
}

TimeDelta VideoJitterEstimator::JitterDelay() const {
  double noise = cfg_.noise_std_devs * std::sqrt(var_noise_) - cfg_.noise_offset_ms;
  if (noise < 1.0) noise = 1.0;
  double j = theta_[0] * (max_frame_size_ - avg_frame_size_) + noise;
  return TimeDelta::MillisF(std::max(1.0, j));
}

void VideoJitterEstimator::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("jitter_delay_ms", JitterDelay().ms_f());
  w.Gauge("noise_std_ms", noise_std_ms());
  w.Gauge("capacity_kbps", theta_[0] > 0 ? 8.0 / theta_[0] : 0.0);
  w.Gauge("avg_frame_bytes", avg_frame_size_);
  w.Gauge("max_frame_bytes", max_frame_size_);
  w.Counter("frames", frames_);
  w.Hist("jitter_ms", jitter_ms_);
}

// --- VideoTiming -------------------------------------------------------------

VideoTiming::VideoTiming(VideoTimingConfig cfg) : cfg_(cfg) {}

void VideoTiming::IncomingTimestamp(uint32_t rtp_ts, Timestamp arrival) {
  double rtp_ms = static_cast<double>(unwrapper_.Unwrap(rtp_ts)) / 90.0;
  double arr_ms = static_cast<double>(arrival.us()) / 1000.0;
  // The lower envelope of (arrival - rtp time) is the jitter-free offset.
  // A small upward leak per frame (5 us, ~150 ppm at 30 fps) lets the
  // envelope follow a sender clock that runs slow; a fast one is followed
  // by the min() directly.
  if (!has_ts_) min_offset_ms_ = arr_ms - rtp_ms;
  min_offset_ms_ = std::min(min_offset_ms_ + 0.005, arr_ms - rtp_ms);
  has_ts_ = true;
}

void VideoTiming::AddDecodeTime(TimeDelta d) {
  decode_us_.push_back(d.us());
  if (decode_us_.size() > cfg_.decode_time_window) decode_us_.pop_front();
}

TimeDelta VideoTiming::DecodeTimeP95() const {
  if (decode_us_.empty()) return TimeDelta::Millis(5);
  std::vector<int64_t> v(decode_us_.begin(), decode_us_.end());
  size_t k = std::min(v.size() - 1, static_cast<size_t>(0.95 * static_cast<double>(v.size())));
  std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
  return TimeDelta::Micros(v[k]);
}

TimeDelta VideoTiming::TargetDelay() const {
  TimeDelta t = jitter_delay_ + DecodeTimeP95() + cfg_.render_delay;
  return Clamp(std::max(t, cfg_.min_playout_delay), TimeDelta::Zero(), cfg_.max_playout_delay);
}

void VideoTiming::UpdateCurrentDelay(Timestamp now) {
  TimeDelta target = TargetDelay();
  if (!last_update_.IsFinite() || current_delay_.IsZero()) {
    current_delay_ = target;
  } else {
    TimeDelta max_step = (now - last_update_) * cfg_.max_delay_change_per_s;
    TimeDelta diff = target - current_delay_;
    current_delay_ += Clamp(diff, -max_step, max_step);
  }
  last_update_ = now;
}

Timestamp VideoTiming::LocalCaptureTime(uint32_t rtp_ts) const {
  double rtp_ms = static_cast<double>(unwrapper_.PeekUnwrap(rtp_ts)) / 90.0;
  return Timestamp::Micros(static_cast<int64_t>((rtp_ms + min_offset_ms_) * 1000.0));
}

Timestamp VideoTiming::RenderTime(uint32_t rtp_ts, Timestamp now) const {
  if (!has_ts_) return now;
  return LocalCaptureTime(rtp_ts) + current_delay_;
}

void VideoTiming::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("target_delay_ms", TargetDelay().ms_f());
  w.Gauge("current_delay_ms", current_delay_.ms_f());
  w.Gauge("jitter_delay_ms", jitter_delay_.ms_f());
  w.Gauge("decode_p95_ms", DecodeTimeP95().ms_f());
}

}  // namespace sm::timing
