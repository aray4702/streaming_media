#include "sm/cc/congestion_window_pushback.h"

#include <algorithm>

namespace sm::cc {

void CongestionWindowPushback::OnFeedbackRtt(TimeDelta max_feedback_rtt) {
  rtts_ms_.push_back(max_feedback_rtt.us() / 1000);
  if (rtts_ms_.size() > cfg_.rtt_window) rtts_ms_.pop_front();
}

void CongestionWindowPushback::UpdateWindow(DataRate target) {
  if (!cfg_.enabled || rtts_ms_.empty()) return;
  const TimeDelta min_rtt = TimeDelta::Millis(*std::min_element(rtts_ms_.begin(), rtts_ms_.end()));
  const DataSize kMinWindow = DataSize::Bytes(2 * 1500);
  DataSize w = target * (min_rtt + cfg_.queue_time);
  // goog_cc_network_control.cc:399-404, with the units' saturating operators (#451).
  w = window_ ? std::max(kMinWindow, (w + *window_) / 2) : std::max(kMinWindow, w);
  window_ = w;
}

DataRate CongestionWindowPushback::UpdateTargetBitrate(DataRate target, Timestamp now) {
  if (!cfg_.enabled || !window_ || window_->bytes() == 0) {
    reduce_ratio_ = 0;
    return target;
  }
  const int64_t total = outstanding_.bytes() + (cfg_.add_pacing ? pacing_.bytes() : 0);
  const double fill = static_cast<double>(total) / static_cast<double>(window_->bytes());
  if (fill > 1.5) {
    ratio_ *= 0.9;
  } else if (fill > 1) {
    ratio_ *= 0.95;
  } else if (fill < 0.1) {
    ratio_ = 1.0;
  } else {
    ratio_ = std::min(ratio_ * 1.05, 1.0);
  }
  const DataRate adjusted = DataRate::BitsPerSec(static_cast<int64_t>(static_cast<double>(target.bps()) * ratio_));
  const DataRate out = adjusted < cfg_.min_pushback_rate ? std::min(target, cfg_.min_pushback_rate) : adjusted;
  const double reduce = target.bps() > 0 ? static_cast<double>(target.bps() - out.bps()) / static_cast<double>(target.bps()) : 0;
  if (reduce > 0.01 && reduce_ratio_ <= 0.01) {
    ++reductions_;
    SM_EVENT(cfg_.events, now, "cwnd", "pushback", static_cast<int64_t>(reduce * 1000), total);
  }
  reduce_ratio_ = reduce;
  fill_.Add(fill);
  return out;
}

CongestionWindowPushback::FrameDrop CongestionWindowPushback::FrameDropInterval(DataRate target, double reduce_ratio,
                                                                                DataRate codec_min) {
  FrameDrop out;
  out.encoder_target = target;
  if (reduce_ratio > 0.01 && target.bps() > 0 && target > codec_min) {
    const int64_t reduce_bps = std::min(static_cast<int64_t>(static_cast<double>(target.bps()) * reduce_ratio),
                                        target.bps() - codec_min.bps());
    if (reduce_bps > 0) {
      out.interval = static_cast<int>(std::max<int64_t>(2, target.bps() / reduce_bps));
      out.encoder_target = DataRate::BitsPerSec(target.bps() - target.bps() / out.interval);
    }
  }
  return out;
}

void CongestionWindowPushback::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("encoding_rate_ratio", ratio_);
  w.Gauge("reduce_ratio", reduce_ratio_);
  w.Gauge("window_bytes", window_ ? static_cast<double>(window_->bytes()) : 0.0);
  w.Gauge("outstanding_bytes", static_cast<double>(outstanding_.bytes()));
  w.Counter("reductions", reductions_);
  w.Hist("fill_ratio", fill_);
}

}  // namespace sm::cc
