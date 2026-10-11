// 8.3 Capture-to-render latency: the absolute capture time header
// extension (http://www.webrtc.org/experiments/rtp-hdrext/abs-capture-time).
//
// Payload: 64-bit NTP capture time (UQ32.32) of the media in the packet,
// optionally followed by a 64-bit signed estimated capture clock offset
// (Q32.32) between the capturer's clock and the sender's. Unlike the RTP
// timestamp it survives mixers and SFUs that rewrite RTP timestamps, so
// the receiver can measure true glass-to-glass latency.
//
// To save bandwidth the sender only attaches it when the receiver could
// not interpolate it from the previous one (every `max_interval`, when the
// RTP clock/source changes, or when the interpolation error would exceed
// `max_error`). The receiver interpolates for the packets in between, and
// converts capture time to its own clock using a clock offset estimated
// from RTCP (sender NTP in SR vs. local receive time, minus RTT/2).
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "sm/core/metrics.h"
#include "sm/core/units.h"
#include "sm/timing/rtcp_stats.h"

namespace sm::sync {

struct AbsoluteCaptureTime {
  uint64_t capture_ntp = 0;                  // UQ32.32
  std::optional<int64_t> clock_offset;       // Q32.32
};

std::vector<uint8_t> SerializeAbsoluteCaptureTime(const AbsoluteCaptureTime& a);
std::optional<AbsoluteCaptureTime> ParseAbsoluteCaptureTime(std::span<const uint8_t> data);

struct CaptureTimeSenderConfig {
  TimeDelta max_interval = TimeDelta::Seconds(1);
  TimeDelta max_error = TimeDelta::Millis(1);
};

class AbsoluteCaptureTimeSender : public MetricsSource {
 public:
  explicit AbsoluteCaptureTimeSender(CaptureTimeSenderConfig cfg = {}) : cfg_(cfg) {}
  // Returns the extension to attach to this packet, or nullopt to omit it.
  std::optional<AbsoluteCaptureTime> OnPacket(uint32_t source, uint32_t rtp, int clock_rate,
                                               const AbsoluteCaptureTime& actual, Timestamp now);
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  CaptureTimeSenderConfig cfg_;
  bool has_last_ = false;
  uint32_t last_source_ = 0, last_rtp_ = 0;
  int last_rate_ = 0;
  AbsoluteCaptureTime last_;
  Timestamp last_send_;
  uint64_t packets_ = 0, sent_ = 0;
};

class AbsoluteCaptureTimeInterpolator {
 public:
  explicit AbsoluteCaptureTimeInterpolator(TimeDelta max_age = TimeDelta::Seconds(5)) : max_age_(max_age) {}
  // `received`: the extension if present. Returns the (interpolated)
  // capture time for this packet, if known.
  std::optional<AbsoluteCaptureTime> OnPacket(uint32_t source, uint32_t rtp, int clock_rate,
                                               const std::optional<AbsoluteCaptureTime>& received, Timestamp now);

 private:
  TimeDelta max_age_;
  bool has_ = false;
  uint32_t source_ = 0, rtp_ = 0;
  int rate_ = 0;
  AbsoluteCaptureTime last_;
  Timestamp at_;
};

// Estimates (sender NTP clock - local clock) from SRs, and turns capture
// times into end-to-end latency at render time.
class LatencyEstimator : public MetricsSource {
 public:
  // An SR sent at sender NTP `sr_ntp` arrived locally at `arrival`.
  void OnSenderReport(timing::NtpTime sr_ntp, Timestamp arrival, TimeDelta rtt);
  // Media with this capture time (sender/capturer clock) is rendered now.
  std::optional<TimeDelta> OnRendered(const AbsoluteCaptureTime& capture, Timestamp render_local);
  std::optional<TimeDelta> remote_to_local_offset() const { return offset_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  std::optional<TimeDelta> offset_;  // local = remote - offset
  Histogram latency_ms_;
};

}  // namespace sm::sync
