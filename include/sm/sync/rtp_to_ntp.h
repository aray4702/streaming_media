// 8.1 RTP timestamp -> NTP (sender wall clock) mapping from RTCP Sender
// Reports.
//
// Each SR pairs the sender's NTP time with the RTP timestamp of the same
// instant. A least-squares line through the last `max_measurements` pairs
// (RTP unwrapped) maps any RTP timestamp of the stream to the sender's
// capture wall-clock time; the slope also yields the actual media clock
// rate, which absorbs sender clock drift. Pairs that deviate from the
// current fit by more than `max_deviation` (an SR delayed in the sender, a
// timestamp jump) are rejected; after `reset_after` consecutive rejections
// the history is reset, so a genuine discontinuity is followed.
#pragma once

#include <cstdint>
#include <deque>
#include <optional>

#include "sm/core/metrics.h"
#include "sm/core/seq_num.h"
#include "sm/core/units.h"
#include "sm/timing/rtcp_stats.h"

namespace sm::sync {

struct RtpToNtpConfig {
  size_t max_measurements = 20;
  TimeDelta max_deviation = TimeDelta::Millis(30);
  int reset_after = 3;
};

class RtpToNtpEstimator : public MetricsSource {
 public:
  enum class UpdateResult { kInvalid, kSameMeasurement, kNewMeasurement, kRejected, kReset };
  explicit RtpToNtpEstimator(RtpToNtpConfig cfg = {}) : cfg_(cfg) {}
  UpdateResult UpdateMeasurements(timing::NtpTime ntp, uint32_t rtp);
  // Sender wall-clock (as Timestamp, Unix epoch) of a media RTP timestamp.
  std::optional<Timestamp> Estimate(uint32_t rtp) const;
  std::optional<double> EstimatedFrequencyHz() const;
  size_t size() const { return points_.size(); }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  void Fit();
  RtpToNtpConfig cfg_;
  mutable SeqNumUnwrapper<uint32_t> unwrapper_;
  std::deque<std::pair<int64_t, double>> points_;  // (unwrapped rtp, ntp ms)
  double slope_ = 0, intercept_ = 0;               // ntp_ms = slope * rtp + intercept
  bool valid_ = false;
  int consecutive_rejects_ = 0;
  uint64_t accepted_ = 0, rejected_ = 0, resets_ = 0;
};

}  // namespace sm::sync
