#include "sm/sync/capture_time.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

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
// Interpolated capture NTP for an RTP timestamp `delta` ticks later.
// Exact integer arithmetic (UQ32.32: one second = 2^32); no long double,
// which is software-emulated (slow) on some targets such as WebAssembly.
uint64_t Advance(uint64_t ntp, int64_t delta_ticks, int rate) {
  const int64_t whole = delta_ticks / rate;
  const int64_t rem = delta_ticks % rate;  // |rem| < rate, so rem << 32 fits.
  const int64_t frac = (rem * (int64_t{1} << 32)) / rate;
  return ntp + static_cast<uint64_t>(whole * (int64_t{1} << 32) + frac);
}
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

std::optional<AbsoluteCaptureTime> AbsoluteCaptureTimeSender::OnPacket(uint32_t source, uint32_t rtp, int rate,
                                                                        const AbsoluteCaptureTime& actual, Timestamp now) {
  ++packets_;
  bool send = !has_last_ || source != last_source_ || rate != last_rate_ || now - last_send_ >= cfg_.max_interval ||
              actual.clock_offset != last_.clock_offset;
  if (!send) {
    int64_t delta = static_cast<int32_t>(rtp - last_rtp_);
    uint64_t predicted = Advance(last_.capture_ntp, delta, rate);
    double err_s = std::fabs(static_cast<double>(static_cast<int64_t>(actual.capture_ntp - predicted))) / 4294967296.0;
    send = err_s > cfg_.max_error.seconds();
  }
  if (!send) return std::nullopt;
  has_last_ = true;
  last_source_ = source;
  last_rtp_ = rtp;
  last_rate_ = rate;
  last_ = actual;
  last_send_ = now;
  ++sent_;
  return actual;
}

void AbsoluteCaptureTimeSender::ExportMetrics(MetricsWriter& w) const {
  w.Counter("packets", packets_);
  w.Counter("extensions_sent", sent_);
  w.Gauge("send_ratio", packets_ ? static_cast<double>(sent_) / static_cast<double>(packets_) : 0.0);
}

std::optional<AbsoluteCaptureTime> AbsoluteCaptureTimeInterpolator::OnPacket(
    uint32_t source, uint32_t rtp, int rate, const std::optional<AbsoluteCaptureTime>& received, Timestamp now) {
  if (received) {
    has_ = true;
    source_ = source;
    rtp_ = rtp;
    rate_ = rate;
    last_ = *received;
    at_ = now;
    return received;
  }
  if (!has_ || source != source_ || rate != rate_ || now - at_ > max_age_) return std::nullopt;
  AbsoluteCaptureTime a = last_;
  a.capture_ntp = Advance(last_.capture_ntp, static_cast<int32_t>(rtp - rtp_), rate);
  return a;
}

void LatencyEstimator::OnSenderReport(timing::NtpTime sr_ntp, Timestamp arrival, TimeDelta rtt) {
  // The SR was sent ~RTT/2 before it arrived.
  TimeDelta sample = sr_ntp.ToTimestamp() - (arrival - rtt / 2);
  // Keep the smallest-latency (least queued) samples: min filter with slow leak.
  if (!offset_ || sample < *offset_) {
    offset_ = sample;
  } else {
    offset_ = *offset_ + (sample - *offset_) * 0.05;
  }
}

std::optional<TimeDelta> LatencyEstimator::OnRendered(const AbsoluteCaptureTime& c, Timestamp render_local) {
  if (!offset_) return std::nullopt;
  timing::NtpTime ntp{c.capture_ntp};
  Timestamp capture_remote = ntp.ToTimestamp();
  // A capturer behind a mixer may run its own clock: apply its offset too.
  if (c.clock_offset) {
    // Q32.32 seconds -> microseconds.
    capture_remote = capture_remote + TimeDelta::Micros(static_cast<int64_t>(static_cast<double>(*c.clock_offset) / 4294967296.0 * 1e6));
  }
  TimeDelta latency = render_local - (capture_remote - *offset_);
  latency_ms_.Add(std::max(0.0, latency.ms_f()));
  return latency;
}

void LatencyEstimator::ExportMetrics(MetricsWriter& w) const {
  if (offset_) w.Gauge("clock_offset_ms", offset_->ms_f());
  w.Hist("capture_to_render_ms", latency_ms_);
}

}  // namespace sm::sync
