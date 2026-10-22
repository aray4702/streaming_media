// 8.1 RTP timestamp -> NTP (sender wall clock) mapping from RTCP Sender
// Reports, as libwebrtc M157's RtpToNtpEstimator
// (rtc_base/rtp_to_ntp_estimator.*).
//
// Each SR pairs the sender's NTP time with the RTP timestamp of the same
// instant. A least-squares line (NTP units of 2^-32 s on the unwrapped RTP
// timestamp) through the last `kNumRtcpReportsToUse` 20 pairs maps any RTP
// timestamp of the stream to the sender's capture wall-clock time; the
// slope also yields the actual media clock rate, which absorbs sender clock
// drift. An SR with the same NTP time or the same RTP timestamp as a stored
// one is the same measurement. A new SR is invalid unless, compared with
// the newest stored one, its NTP time moves forward by at most 1 h
// (`kMaxAllowedRtcpNtpInterval`) and its RTP timestamp moves forward by at
// most `1 << 25` ticks. After `kMaxInvalidSamples` 3 consecutive invalid
// SRs the history is cleared and the third one starts a new line, so a
// genuine discontinuity (a sender restart) is followed.
#pragma once

#include <cstdint>
#include <deque>
#include <optional>

#include "sm/core/metrics.h"
#include "sm/core/seq_num.h"
#include "sm/core/units.h"
#include "sm/timing/rtcp_stats.h"

namespace sm::sync {

class RtpToNtpEstimator : public MetricsSource {
 public:
  static constexpr int kMaxInvalidSamples = 3;
  static constexpr size_t kNumRtcpReportsToUse = 20;
  static constexpr uint64_t kMaxAllowedRtcpNtpInterval = uint64_t{60 * 60} << 32;
  static constexpr int64_t kMaxRtpJump = int64_t{1} << 25;

  enum class UpdateResult { kInvalidMeasurement, kSameMeasurement, kNewMeasurement };
  UpdateResult UpdateMeasurements(timing::NtpTime ntp, uint32_t rtp);
  // NTP time of a media RTP timestamp; value 0 (invalid) without a line.
  // Like libwebrtc's, it advances the RTP unwrapper.
  timing::NtpTime EstimateNtp(uint32_t rtp) const;
  // The same as a Timestamp (Unix epoch).
  std::optional<Timestamp> Estimate(uint32_t rtp) const;
  // libwebrtc's EstimatedFrequencyKhz: 0 without a line.
  double EstimatedFrequencyKhz() const;
  std::optional<double> EstimatedFrequencyHz() const;
  size_t size() const { return measurements_.size(); }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  struct Measurement {
    uint64_t ntp;  // UQ32.32
    int64_t unwrapped_rtp;
  };
  struct Parameters {
    double slope;   // NTP units per RTP tick.
    double offset;  // NTP units.
  };
  void UpdateParameters();
  int consecutive_invalid_samples_ = 0;
  std::deque<Measurement> measurements_;  // Newest first.
  std::optional<Parameters> params_;
  mutable SeqNumUnwrapper<uint32_t> unwrapper_;
  uint64_t accepted_ = 0, invalid_ = 0, resets_ = 0;
};

}  // namespace sm::sync
