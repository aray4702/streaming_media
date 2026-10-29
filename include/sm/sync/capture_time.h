// 8.3 Capture-to-render latency: the absolute capture time header
// extension (http://www.webrtc.org/experiments/rtp-hdrext/abs-capture-time),
// as libwebrtc M157's AbsoluteCaptureTimeSender,
// AbsoluteCaptureTimeInterpolator, CaptureClockOffsetUpdater and
// RemoteNtpTimeEstimator (modules/rtp_rtcp/source/absolute_capture_time_*,
// capture_clock_offset_updater.*, remote_ntp_time_estimator.cc).
//
// Payload: 64-bit NTP capture time (UQ32.32) of the media in the packet,
// optionally followed by a 64-bit signed estimated capture clock offset
// (Q32.32) between the capturer's clock and the sender's. Unlike the RTP
// timestamp it survives mixers and SFUs that rewrite RTP timestamps, so
// the receiver can measure true glass-to-glass latency.
//
// To save bandwidth the sender only attaches it when the receiver could
// not interpolate it from the previous one: when more than
// `kInterpolationMaxInterval` 1 s passed since the last one, the source or
// RTP clock rate changed (or the rate is not positive), the clock offset
// changed, or the interpolation error rounds to more than
// `kInterpolationMaxError` 1 ms. libwebrtc's video sender asks once per
// frame (the result goes on the frame's first packet,
// rtp_sender_video.cc:695-707, 425-428) with offset 0; its audio sender
// once per packet (rtp_sender_audio.cc:142-157). The receiver interpolates
// for up to 5 s (`kInterpolationMaxInterval`) on the same source and rate,
// and forgets the last extension at the first packet it can't interpolate.
// It converts capture time to its own clock by adding the remote-to-local
// clock offset (CaptureClockOffsetUpdater) that RemoteNtpTimeEstimator
// takes from SRs: local arrival - sender NTP - RTT/2, the median of the
// last 7, once 3 are stored.
#pragma once

#include <cstdint>
#include <optional>
#include <deque>
#include <span>
#include <vector>

#include "sm/core/metrics.h"
#include "sm/core/units.h"
#include "sm/sync/rtp_to_ntp.h"
#include "sm/timing/rtcp_stats.h"

namespace sm::sync {

struct AbsoluteCaptureTime {
  uint64_t capture_ntp = 0;                  // UQ32.32
  std::optional<int64_t> clock_offset;       // Q32.32
};

std::vector<uint8_t> SerializeAbsoluteCaptureTime(const AbsoluteCaptureTime& a);
std::optional<AbsoluteCaptureTime> ParseAbsoluteCaptureTime(std::span<const uint8_t> data);

// UQ32.32 / Q32.32 seconds to milliseconds and microseconds, rounded
// (system_wrappers/include/ntp_time.h).
int64_t UQ32x32ToInt64Ms(uint64_t q32x32);
int64_t Q32x32ToInt64Ms(int64_t q32x32);
int64_t Q32x32ToInt64Us(int64_t q32x32);
// A duration in NTP units (1/2^32 s), as ntp_time_util.h's ToNtpUnits.
int64_t ToNtpUnits(TimeDelta delta);

struct CaptureTimeSenderConfig {
  TimeDelta max_interval = TimeDelta::Seconds(1);  // kInterpolationMaxInterval
  TimeDelta max_error = TimeDelta::Millis(1);      // kInterpolationMaxError
};

class AbsoluteCaptureTimeSender : public MetricsSource {
 public:
  static constexpr TimeDelta kInterpolationMaxInterval = TimeDelta::Seconds(1);
  static constexpr TimeDelta kInterpolationMaxError = TimeDelta::Millis(1);

  explicit AbsoluteCaptureTimeSender(CaptureTimeSenderConfig cfg = {}) : cfg_(cfg) {}
  // The capture system: the first CSRC, else the SSRC.
  static uint32_t GetSource(uint32_t ssrc, std::span<const uint32_t> csrcs);
  // Returns the extension to attach, or nullopt to omit it. `force` always
  // returns it. A video sender calls this once per frame and puts the result
  // on the frame's first packet.
  std::optional<AbsoluteCaptureTime> OnPacket(uint32_t source, uint32_t rtp, int clock_rate,
                                               const AbsoluteCaptureTime& actual, Timestamp now,
                                               bool force = false);
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  bool ShouldSend(uint32_t source, uint32_t rtp, int rate, const AbsoluteCaptureTime& actual, Timestamp now) const;
  CaptureTimeSenderConfig cfg_;
  Timestamp last_send_ = Timestamp::MinusInfinity();
  uint32_t last_source_ = 0, last_rtp_ = 0;
  int last_rate_ = 0;
  AbsoluteCaptureTime last_;
  uint64_t packets_ = 0, sent_ = 0;
};

class AbsoluteCaptureTimeInterpolator {
 public:
  static constexpr TimeDelta kInterpolationMaxInterval = TimeDelta::Seconds(5);

  explicit AbsoluteCaptureTimeInterpolator(TimeDelta max_age = kInterpolationMaxInterval) : max_age_(max_age) {}
  static uint32_t GetSource(uint32_t ssrc, std::span<const uint32_t> csrcs);
  // `received`: the extension if present. Returns the received or
  // interpolated extension for this packet, if known.
  std::optional<AbsoluteCaptureTime> OnPacket(uint32_t source, uint32_t rtp, int clock_rate,
                                               const std::optional<AbsoluteCaptureTime>& received, Timestamp now);
  // last + (rtp - last_rtp) / rate in UQ32.32, with libwebrtc's integer rule.
  static uint64_t Interpolate(uint32_t rtp, int rate, uint32_t last_rtp, uint64_t last_capture_ntp);

 private:
  TimeDelta max_age_;
  Timestamp at_ = Timestamp::MinusInfinity();  // MinusInfinity: nothing to interpolate from.
  uint32_t source_ = 0, rtp_ = 0;
  int rate_ = 0;
  AbsoluteCaptureTime last_;
};

// Turns the offset in a received extension (capturer clock vs the remote
// sender's) into the capturer's offset against the local clock, by adding
// the remote-to-local offset from RTCP.
class CaptureClockOffsetUpdater {
 public:
  // nullopt unless both offsets are known; the sum wraps as unsigned.
  std::optional<int64_t> AdjustEstimatedCaptureClockOffset(std::optional<int64_t> remote_capture_clock_offset) const;
  // Q32.32 seconds, local - remote.
  void SetRemoteToLocalClockOffset(std::optional<int64_t> offset_q32x32) { remote_to_local_ = offset_q32x32; }
  // Q32.32 to whole milliseconds, rounded.
  static std::optional<TimeDelta> ConvertToTimeDelta(std::optional<int64_t> q32x32);

 private:
  std::optional<int64_t> remote_to_local_;
};

// libwebrtc's MovingMedianFilter (rtc_base/numerics/moving_percentile_filter.h):
// the lower median of the last `window` samples.
class MovingMedianFilter {
 public:
  explicit MovingMedianFilter(size_t window) : window_(window) {}
  void Insert(int64_t v);
  int64_t GetFilteredValue() const;  // 0 when empty.
  size_t GetNumberOfSamplesStored() const { return samples_.size(); }

 private:
  size_t window_;
  std::deque<int64_t> samples_;
};

// Estimates an RTP timestamp's capture time on the local clock, and the
// remote-to-local clock offset, from SRs.
class RemoteNtpTimeEstimator {
 public:
  static constexpr int kMinimumNumberOfSamples = 3;
  static constexpr size_t kClocksOffsetSmoothingWindow = 7;

  // An SR (sender NTP time and RTP timestamp) that arrived `now`, with the
  // current RTT. False if the SR is not a valid measurement.
  bool UpdateRtcpTimestamp(TimeDelta rtt, timing::NtpTime sender_send_time, uint32_t rtp_timestamp, Timestamp now);
  // Local NTP time of `rtp_timestamp`; value 0 when unknown.
  timing::NtpTime EstimateNtp(uint32_t rtp_timestamp) const;
  // Local - remote NTP clock in 1/2^32 s, once 3 samples are stored.
  std::optional<int64_t> EstimateRemoteToLocalClockOffset() const;

 private:
  MovingMedianFilter offsets_{kClocksOffsetSmoothingWindow};
  RtpToNtpEstimator rtp_to_ntp_;
};

// Glass-to-glass latency at render time, as libwebrtc's receivers compute
// the capture time under `WebRTC-UseAbsCapTimeForG2gMetric`
// (rtp_video_stream_receiver2.cc:1056-1064): the extension's capture time
// plus its offset adjusted to the local clock. The clock offset is
// RemoteNtpTimeEstimator's (median of the last 7 SRs, once 3 are stored).
// libwebrtc reports no such metric by default; this is a library extra.
class LatencyEstimator : public MetricsSource {
 public:
  // An SR sent at sender NTP `sr_ntp` arrived locally at `arrival`. Call
  // once per new SR, as RemoteNtpTimeEstimator skips a repeated one.
  void OnSenderReport(timing::NtpTime sr_ntp, Timestamp arrival, TimeDelta rtt);
  // Media with this capture time (sender/capturer clock) is rendered now.
  // nullopt until the offset is known, or when the extension has no
  // capture clock offset (libwebrtc's senders always write 0).
  std::optional<TimeDelta> OnRendered(const AbsoluteCaptureTime& capture, Timestamp render_local);
  // Remote - local clock, once 3 SRs are in.
  std::optional<TimeDelta> remote_to_local_offset() const;
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  MovingMedianFilter offsets_{RemoteNtpTimeEstimator::kClocksOffsetSmoothingWindow};  // Local - remote, Q32.32.
  CaptureClockOffsetUpdater updater_;
  Histogram latency_ms_;
};

}  // namespace sm::sync
