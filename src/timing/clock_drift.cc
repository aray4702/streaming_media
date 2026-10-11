#include "sm/timing/clock_drift.h"

#include <algorithm>

namespace sm::timing {

void RlsLineFit::Reset() {
  w_[0] = w_[1] = 0;
  p_[0][0] = p_[1][1] = 1e6;
  p_[0][1] = p_[1][0] = 0;
  n_ = 0;
  has_x0_ = false;
}

void RlsLineFit::Update(double x, double y) {
  if (!has_x0_) {
    x0_ = x;
    has_x0_ = true;
  }
  // Keep the regressor well conditioned: fit on (x - x0), then report
  // intercept relative to x = 0.
  double phi[2] = {x - x0_, 1.0};
  double intercept_local = w_[1] + w_[0] * x0_;
  double wl[2] = {w_[0], intercept_local};
  double pphi[2] = {p_[0][0] * phi[0] + p_[0][1] * phi[1], p_[1][0] * phi[0] + p_[1][1] * phi[1]};
  double denom = lambda_ + phi[0] * pphi[0] + phi[1] * pphi[1];
  double k[2] = {pphi[0] / denom, pphi[1] / denom};
  double err = y - (wl[0] * phi[0] + wl[1]);
  wl[0] += k[0] * err;
  wl[1] += k[1] * err;
  double np[2][2];
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 2; ++j) np[i][j] = (p_[i][j] - k[i] * pphi[j]) / lambda_;
  }
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 2; ++j) p_[i][j] = np[i][j];
  }
  w_[0] = wl[0];
  w_[1] = wl[1] - wl[0] * x0_;
  ++n_;
}

void ClockDriftEstimator::OnPacket(uint32_t rtp_ts, int clock_rate, Timestamp arrival) {
  double media_s = static_cast<double>(unwrapper_.Unwrap(rtp_ts)) / clock_rate;
  double transit_ms = arrival.us() / 1000.0 - media_s * 1000.0;
  if (!window_start_.IsFinite()) window_start_ = arrival;
  if (arrival - window_start_ >= cfg_.window) {
    if (window_has_) {
      fit_.Update(window_media_s_, window_min_transit_ms_);
      // Slope in ms of transit per second of media = 1e3 ppm.
      if (fit_.samples() >= 2) drift_ppm_ = fit_.slope() * 1e3;
    }
    window_start_ = arrival;
    window_has_ = false;
  }
  if (!window_has_ || transit_ms < window_min_transit_ms_) {
    window_min_transit_ms_ = transit_ms;
    window_media_s_ = media_s;
    window_has_ = true;
  }
}

void ClockDriftEstimator::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("drift_ppm", drift_ppm_);
  w.Gauge("converged", converged() ? 1 : 0);
  w.Counter("windows", fit_.samples());
}

void DriftCompensator::UpdateFromLevel(double level_ms, double target_ms) {
  level_ms_ = level_ms_ < 0 ? level_ms : level_ms_ + cfg_.level_smoothing * (level_ms - level_ms_);
  double err = level_ms_ - target_ms;  // Too much buffered -> play faster (ratio < 1).
  integral_ = std::clamp(integral_ + err, -cfg_.max_ratio_dev / cfg_.ki, cfg_.max_ratio_dev / cfg_.ki);
  double dev = -(cfg_.kp * err + cfg_.ki * integral_);
  ratio_ = 1.0 + std::clamp(dev, -cfg_.max_ratio_dev, cfg_.max_ratio_dev);
}

void DriftCompensator::Process(std::span<const float> in, std::vector<float>& out) {
  // hist_ holds the 3 samples preceding `in`; together they form a virtual
  // signal s[0..] = hist_[0..2], in[0..]. pos_ indexes s, starting at 1 so
  // cubic interpolation has one sample of left context.
  const double step = 1.0 / ratio_;
  auto s = [&](int64_t i) -> float {
    if (i < 3) return hist_[static_cast<size_t>(i)];
    return in[static_cast<size_t>(i - 3)];
  };
  const int64_t avail = static_cast<int64_t>(in.size()) + 3;
  if (pos_ < 1) pos_ = 1;
  while (pos_ + 2 < static_cast<double>(avail)) {
    int64_t i = static_cast<int64_t>(pos_);
    float t = static_cast<float>(pos_ - static_cast<double>(i));
    float y0 = s(i - 1), y1 = s(i), y2 = s(i + 1), y3 = s(i + 2);
    // Catmull-Rom cubic Hermite.
    float a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    float b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float c = -0.5f * y0 + 0.5f * y2;
    out.push_back(((a * t + b) * t + c) * t + y1);
    ++out_samples_;
    pos_ += step;
  }
  // Slide the window: keep the last 3 samples as history.
  for (int k = 0; k < 3; ++k) hist_[static_cast<size_t>(k)] = s(avail - 3 + k);
  pos_ -= static_cast<double>(in.size());
  in_samples_ += in.size();
}

void DriftCompensator::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("ratio_ppm", (ratio_ - 1.0) * 1e6);
  w.Counter("in_samples", in_samples_);
  w.Counter("out_samples", out_samples_);
}

}  // namespace sm::timing
