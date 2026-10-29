#include "sm/sync/capture_time.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace sm::sync {

namespace {
void Put64(std::vector<uint8_t>& b, uint64_t v) {
  for (int i = 7; i >= 0; --i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
uint64_t Get64(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
  return v;
}
constexpr double kFractionsPerSecond = 4294967296.0;  // NtpTime::kFractionsPerSecond

// remote_ntp_time_estimator.cc: subtract and add keeping full precision.
// Negation goes through uint64_t, so the extremes don't overflow.
int64_t Subtract(uint64_t a, uint64_t b) {
  return a >= b ? static_cast<int64_t>(a - b) : static_cast<int64_t>(uint64_t{0} - (b - a));
}
uint64_t Add(uint64_t lhs, int64_t rhs) { return lhs + static_cast<uint64_t>(rhs); }
}  // namespace

std::vector<uint8_t> SerializeAbsoluteCaptureTime(const AbsoluteCaptureTime& a) {
  std::vector<uint8_t> b;
  Put64(b, a.capture_ntp);
  if (a.clock_offset) Put64(b, static_cast<uint64_t>(*a.clock_offset));
  return b;
}

std::optional<AbsoluteCaptureTime> ParseAbsoluteCaptureTime(std::span<const uint8_t> d) {
  if (d.size() != 8 && d.size() != 16) return std::nullopt;
  AbsoluteCaptureTime a;
  a.capture_ntp = Get64(d.data());
  if (d.size() == 16) a.clock_offset = static_cast<int64_t>(Get64(d.data() + 8));
  return a;
}

int64_t UQ32x32ToInt64Ms(uint64_t q) { return static_cast<int64_t>(std::round(static_cast<double>(q) * (1000.0 / kFractionsPerSecond))); }
int64_t Q32x32ToInt64Ms(int64_t q) { return static_cast<int64_t>(std::round(static_cast<double>(q) * (1000.0 / kFractionsPerSecond))); }
int64_t Q32x32ToInt64Us(int64_t q) { return static_cast<int64_t>(std::round(static_cast<double>(q) * (1'000'000.0 / kFractionsPerSecond))); }

int64_t NtpToMs(uint64_t ntp) {
  constexpr double kNtpFracPerMs = 4.294967296E6;  // 2^32 / 1000.
  const double frac_ms = static_cast<double>(ntp & 0xFFFFFFFFu) / kNtpFracPerMs;
  return 1000 * static_cast<int64_t>(ntp >> 32) + static_cast<int64_t>(frac_ms + 0.5);
}

uint64_t Int64MsToUQ32x32(int64_t ms) {
  const double result = std::round(static_cast<double>(ms) * (kFractionsPerSecond / 1000.0));
  if (result <= 0.0) return 0;
  if (result >= static_cast<double>(std::numeric_limits<uint64_t>::max())) return std::numeric_limits<uint64_t>::max();
  return static_cast<uint64_t>(result);
}

int64_t ToNtpUnits(TimeDelta delta) {
  // ntp_time_util.h: saturated_cast<int32_t>(us) * 2^32 / 1e6.
  const int64_t us = std::clamp<int64_t>(delta.us(), std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max());
  return (us * (int64_t{1} << 32)) / 1'000'000;
}

uint32_t AbsoluteCaptureTimeSender::GetSource(uint32_t ssrc, std::span<const uint32_t> csrcs) {
  return AbsoluteCaptureTimeInterpolator::GetSource(ssrc, csrcs);
}

std::optional<AbsoluteCaptureTime> AbsoluteCaptureTimeSender::OnPacket(uint32_t source, uint32_t rtp, int rate,
                                                                        const AbsoluteCaptureTime& actual, Timestamp now,
                                                                        bool force) {
  ++packets_;
  if (!(force || ShouldSend(source, rtp, rate, actual, now))) return std::nullopt;
  last_source_ = source;
  last_rtp_ = rtp;
  last_rate_ = rate;
  last_ = actual;
  last_send_ = now;
  ++sent_;
  return actual;
}

// absolute_capture_time_sender.cc, ShouldSendExtension.
bool AbsoluteCaptureTimeSender::ShouldSend(uint32_t source, uint32_t rtp, int rate, const AbsoluteCaptureTime& actual,
                                           Timestamp now) const {
  if (last_send_.IsMinusInfinity() || now - last_send_ > cfg_.max_interval) return true;
  if (last_source_ != source) return true;
  if (last_rate_ != rate) return true;
  if (rate <= 0) return true;
  if (last_.clock_offset != actual.clock_offset) return true;
  const uint64_t interpolated = AbsoluteCaptureTimeInterpolator::Interpolate(rtp, rate, last_rtp_, last_.capture_ntp);
  const int64_t error_ms =
      UQ32x32ToInt64Ms(std::min(interpolated - actual.capture_ntp, actual.capture_ntp - interpolated));
  return error_ms > cfg_.max_error.ms();
}
void AbsoluteCaptureTimeSender::ExportMetrics(MetricsWriter& w) const {
  w.Counter("packets", packets_);
  w.Counter("extensions_sent", sent_);
  w.Gauge("send_ratio", packets_ ? static_cast<double>(sent_) / static_cast<double>(packets_) : 0.0);
}

uint32_t AbsoluteCaptureTimeInterpolator::GetSource(uint32_t ssrc, std::span<const uint32_t> csrcs) {
  return csrcs.empty() ? ssrc : csrcs[0];
}

uint64_t AbsoluteCaptureTimeInterpolator::Interpolate(uint32_t rtp, int rate, uint32_t last_rtp, uint64_t last_capture_ntp) {
  // absolute_capture_time_interpolator.cc: the RTP delta shifted into the
  // top 32 bits and read as signed, so a timestamp behind the last one
  // interpolates backwards.
  return last_capture_ntp + static_cast<uint64_t>(static_cast<int64_t>(uint64_t{rtp - last_rtp} << 32) / rate);
}

std::optional<AbsoluteCaptureTime> AbsoluteCaptureTimeInterpolator::OnPacket(
    uint32_t source, uint32_t rtp, int rate, const std::optional<AbsoluteCaptureTime>& received, Timestamp now) {
  if (received) {
    source_ = source;
    rtp_ = rtp;
    rate_ = rate;
    last_ = *received;
    at_ = now;
    return received;
  }
  // ShouldInterpolateExtension; a packet that can't be interpolated also
  // retires the stored extension.
  if (at_.IsMinusInfinity() || now - at_ > max_age_ || source != source_ || rate != rate_ || rate <= 0) {
    at_ = Timestamp::MinusInfinity();
    return std::nullopt;
  }
  return AbsoluteCaptureTime{Interpolate(rtp, rate, rtp_, last_.capture_ntp), last_.clock_offset};
}

std::optional<int64_t> CaptureClockOffsetUpdater::AdjustEstimatedCaptureClockOffset(
    std::optional<int64_t> remote_capture_clock_offset) const {
  if (!remote_capture_clock_offset || !remote_to_local_) return std::nullopt;
  // Unsigned, so an overflow wraps deterministically.
  return static_cast<int64_t>(static_cast<uint64_t>(*remote_capture_clock_offset) + static_cast<uint64_t>(*remote_to_local_));
}

std::optional<TimeDelta> CaptureClockOffsetUpdater::ConvertToTimeDelta(std::optional<int64_t> q32x32) {
  if (!q32x32) return std::nullopt;
  return TimeDelta::Millis(Q32x32ToInt64Ms(*q32x32));
}

void MovingMedianFilter::Insert(int64_t v) {
  samples_.push_back(v);
  if (samples_.size() > window_) samples_.pop_front();
}

int64_t MovingMedianFilter::GetFilteredValue() const {
  if (samples_.empty()) return 0;
  std::vector<int64_t> sorted(samples_.begin(), samples_.end());
  // PercentileFilter's index: static_cast<int64_t>(0.5f * (size - 1)).
  const auto index = static_cast<size_t>(0.5f * static_cast<float>(sorted.size() - 1));
  std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(index), sorted.end());
  return sorted[index];
}

bool RemoteNtpTimeEstimator::UpdateRtcpTimestamp(TimeDelta rtt, timing::NtpTime sender_send_time, uint32_t rtp_timestamp,
                                                 Timestamp now) {
  switch (rtp_to_ntp_.UpdateMeasurements(sender_send_time, rtp_timestamp)) {
    case RtpToNtpEstimator::UpdateResult::kInvalidMeasurement:
      return false;
    case RtpToNtpEstimator::UpdateResult::kSameMeasurement:
      return true;
    case RtpToNtpEstimator::UpdateResult::kNewMeasurement:
      break;
  }
  // A symmetric path: the SR took half the RTT.
  const int64_t deliver_time_ntp = ToNtpUnits(rtt) / 2;
  const uint64_t arrival = timing::NtpTime::FromTimestamp(now).value;
  offsets_.Insert(Subtract(arrival, sender_send_time.value) - deliver_time_ntp);
  return true;
}

timing::NtpTime RemoteNtpTimeEstimator::EstimateNtp(uint32_t rtp_timestamp) const {
  timing::NtpTime sender_capture = rtp_to_ntp_.EstimateNtp(rtp_timestamp);
  if (sender_capture.value == 0) return sender_capture;
  return timing::NtpTime{Add(sender_capture.value, offsets_.GetFilteredValue())};
}

std::optional<int64_t> RemoteNtpTimeEstimator::EstimateRemoteToLocalClockOffset() const {
  if (offsets_.GetNumberOfSamplesStored() < static_cast<size_t>(kMinimumNumberOfSamples)) return std::nullopt;
  return offsets_.GetFilteredValue();
}

void LatencyEstimator::OnSenderReport(timing::NtpTime sr_ntp, Timestamp arrival, TimeDelta rtt) {
  // RemoteNtpTimeEstimator::UpdateRtcpTimestamp's sample.
  offsets_.Insert(Subtract(timing::NtpTime::FromTimestamp(arrival).value, sr_ntp.value) - ToNtpUnits(rtt) / 2);
  if (offsets_.GetNumberOfSamplesStored() >= static_cast<size_t>(RemoteNtpTimeEstimator::kMinimumNumberOfSamples)) {
    updater_.SetRemoteToLocalClockOffset(offsets_.GetFilteredValue());
  }
}

std::optional<TimeDelta> LatencyEstimator::remote_to_local_offset() const {
  if (offsets_.GetNumberOfSamplesStored() < static_cast<size_t>(RemoteNtpTimeEstimator::kMinimumNumberOfSamples)) {
    return std::nullopt;
  }
  return TimeDelta::Micros(-Q32x32ToInt64Us(offsets_.GetFilteredValue()));
}

std::optional<TimeDelta> LatencyEstimator::OnRendered(const AbsoluteCaptureTime& c, Timestamp render_local) {
  const std::optional<TimeDelta> local_offset =
      CaptureClockOffsetUpdater::ConvertToTimeDelta(updater_.AdjustEstimatedCaptureClockOffset(c.clock_offset));
  if (!local_offset) return std::nullopt;
  const Timestamp capture_local = timing::NtpTime{c.capture_ntp}.ToTimestamp() + *local_offset;
  const TimeDelta latency = render_local - capture_local;
  latency_ms_.Add(std::max(0.0, latency.ms_f()));
  return latency;
}

void LatencyEstimator::ExportMetrics(MetricsWriter& w) const {
  if (auto offset = remote_to_local_offset()) w.Gauge("clock_offset_ms", offset->ms_f());
  w.Hist("capture_to_render_ms", latency_ms_);
}

}  // namespace sm::sync
