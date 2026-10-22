#include "sm/sync/rtp_to_ntp.h"

#include <cmath>
#include <limits>

namespace sm::sync {

// UpdateParameters: least squares of NTP on the unwrapped RTP timestamp.
void RtpToNtpEstimator::UpdateParameters() {
  const size_t n = measurements_.size();
  if (n < 2) return;
  double avg_x = 0, avg_y = 0;
  for (const Measurement& m : measurements_) {
    avg_x += static_cast<double>(m.unwrapped_rtp);
    avg_y += static_cast<double>(m.ntp);
  }
  avg_x /= static_cast<double>(n);
  avg_y /= static_cast<double>(n);
  double variance_x = 0, covariance_xy = 0;
  for (const Measurement& m : measurements_) {
    double nx = static_cast<double>(m.unwrapped_rtp) - avg_x;
    double ny = static_cast<double>(m.ntp) - avg_y;
    variance_x += nx * nx;
    covariance_xy += nx * ny;
  }
  if (std::fabs(variance_x) < 1e-8) return;
  double k = covariance_xy / variance_x;
  params_ = Parameters{k, avg_y - k * avg_x};
}

RtpToNtpEstimator::UpdateResult RtpToNtpEstimator::UpdateMeasurements(timing::NtpTime ntp, uint32_t rtp) {
  int64_t unwrapped = unwrapper_.Unwrap(rtp);
  for (const Measurement& m : measurements_) {
    // Either equal: two equal timestamps would give zero frequency.
    if (m.ntp == ntp.value || m.unwrapped_rtp == unwrapped) return UpdateResult::kSameMeasurement;
  }
  if (ntp.value == 0) return UpdateResult::kInvalidMeasurement;
  bool invalid_sample = false;
  if (!measurements_.empty()) {
    const Measurement& newest = measurements_.front();
    if (ntp.value <= newest.ntp || ntp.value > newest.ntp + kMaxAllowedRtcpNtpInterval) {
      invalid_sample = true;
    } else if (unwrapped <= newest.unwrapped_rtp) {
      invalid_sample = true;  // Newer SR with an older RTP timestamp.
    } else if (unwrapped - newest.unwrapped_rtp > kMaxRtpJump) {
      invalid_sample = true;  // No jumps too far into the future in RTP.
    }
  }
  if (invalid_sample) {
    ++invalid_;
    if (++consecutive_invalid_samples_ < kMaxInvalidSamples) return UpdateResult::kInvalidMeasurement;
    // Multiple consecutive invalid SRs: clear the history.
    measurements_.clear();
    params_.reset();
    ++resets_;
  }
  consecutive_invalid_samples_ = 0;
  if (measurements_.size() == kNumRtcpReportsToUse) measurements_.pop_back();
  measurements_.push_front({ntp.value, unwrapped});
  ++accepted_;
  UpdateParameters();
  return UpdateResult::kNewMeasurement;
}

timing::NtpTime RtpToNtpEstimator::EstimateNtp(uint32_t rtp) const {
  if (!params_) return {};
  double estimated = static_cast<double>(unwrapper_.Unwrap(rtp)) * params_->slope + params_->offset + 0.5f;
  // saturated_cast<uint64_t>.
  if (!(estimated > 0)) return {};
  if (estimated >= 18446744073709551616.0) return {std::numeric_limits<uint64_t>::max()};
  return {static_cast<uint64_t>(estimated)};
}

std::optional<Timestamp> RtpToNtpEstimator::Estimate(uint32_t rtp) const {
  timing::NtpTime ntp = EstimateNtp(rtp);
  if (ntp.value == 0) return std::nullopt;
  return ntp.ToTimestamp();
}

double RtpToNtpEstimator::EstimatedFrequencyKhz() const {
  if (!params_) return 0.0;
  constexpr double kNtpUnitPerMs = 4.294967296E6;  // 2^32 / 1000.
  return kNtpUnitPerMs / params_->slope;
}

std::optional<double> RtpToNtpEstimator::EstimatedFrequencyHz() const {
  if (!params_) return std::nullopt;
  return EstimatedFrequencyKhz() * 1000.0;
}

void RtpToNtpEstimator::ExportMetrics(MetricsWriter& w) const {
  w.Counter("accepted", accepted_);
  w.Counter("invalid", invalid_);
  w.Counter("resets", resets_);
  w.Gauge("measurements", static_cast<double>(measurements_.size()));
  if (auto f = EstimatedFrequencyHz()) w.Gauge("frequency_hz", *f);
}

}  // namespace sm::sync
