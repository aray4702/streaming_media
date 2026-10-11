#include "sm/sync/rtp_to_ntp.h"

#include <cmath>

namespace sm::sync {

void RtpToNtpEstimator::Fit() {
  const size_t n = points_.size();
  if (n < 2) {
    valid_ = false;
    return;
  }
  // Centre the regressors for numerical stability.
  double mx = 0, my = 0;
  for (auto& [x, y] : points_) {
    mx += static_cast<double>(x);
    my += y;
  }
  mx /= static_cast<double>(n);
  my /= static_cast<double>(n);
  double sxy = 0, sxx = 0;
  for (auto& [x, y] : points_) {
    double dx = static_cast<double>(x) - mx;
    sxy += dx * (y - my);
    sxx += dx * dx;
  }
  if (sxx <= 0) {
    valid_ = false;
    return;
  }
  slope_ = sxy / sxx;
  intercept_ = my - slope_ * mx;
  valid_ = slope_ > 0;
}

RtpToNtpEstimator::UpdateResult RtpToNtpEstimator::UpdateMeasurements(timing::NtpTime ntp, uint32_t rtp) {
  if (ntp.value == 0) return UpdateResult::kInvalid;
  int64_t x = unwrapper_.Unwrap(rtp);
  double y = static_cast<double>(ntp.ToTimestamp().us()) / 1000.0;
  for (auto& [px, py] : points_) {
    if (px == x || py == y) return UpdateResult::kSameMeasurement;
  }
  if (valid_ && points_.size() >= 3) {
    double pred = slope_ * static_cast<double>(x) + intercept_;
    if (std::fabs(pred - y) > cfg_.max_deviation.ms_f()) {
      ++rejected_;
      if (++consecutive_rejects_ >= cfg_.reset_after) {
        points_.clear();
        points_.emplace_back(x, y);
        consecutive_rejects_ = 0;
        valid_ = false;
        ++resets_;
        return UpdateResult::kReset;
      }
      return UpdateResult::kRejected;
    }
  }
  consecutive_rejects_ = 0;
  points_.emplace_back(x, y);
  if (points_.size() > cfg_.max_measurements) points_.pop_front();
  ++accepted_;
  Fit();
  return UpdateResult::kNewMeasurement;
}

std::optional<Timestamp> RtpToNtpEstimator::Estimate(uint32_t rtp) const {
  if (!valid_) return std::nullopt;
  int64_t x = unwrapper_.PeekUnwrap(rtp);
  return Timestamp::Micros(static_cast<int64_t>(std::llround((slope_ * static_cast<double>(x) + intercept_) * 1000.0)));
}

std::optional<double> RtpToNtpEstimator::EstimatedFrequencyHz() const {
  if (!valid_) return std::nullopt;
  return 1000.0 / slope_;
}

void RtpToNtpEstimator::ExportMetrics(MetricsWriter& w) const {
  w.Counter("accepted", accepted_);
  w.Counter("rejected", rejected_);
  w.Counter("resets", resets_);
  w.Gauge("measurements", static_cast<double>(points_.size()));
  if (auto f = EstimatedFrequencyHz()) w.Gauge("frequency_hz", *f);
}

}  // namespace sm::sync
