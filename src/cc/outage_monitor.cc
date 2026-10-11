#include "sm/cc/outage_monitor.h"

namespace sm::cc {

const char* ToString(LinkState s) {
  switch (s) {
    case LinkState::kUp: return "up";
    case LinkState::kDegraded: return "degraded";
    case LinkState::kDown: return "down";
  }
  return "?";
}

void OutageMonitor::OnFeedback(Timestamp now) {
  if (state_ == LinkState::kDown) {
    recovered_pending_ = true;
    outage_ms_.Add((now - down_since_).ms_f());
    SM_EVENT(cfg_.events, now, "outage", "recovered", (now - down_since_).ms_f(), 0);
  }
  last_feedback_ = now;
  state_ = LinkState::kUp;
  ice_restart_sent_ = false;
}

OutageActions OutageMonitor::Process(Timestamp now) {
  OutageActions a;
  if (!last_feedback_.IsFinite()) last_feedback_ = now;
  if (recovered_pending_) {
    recovered_pending_ = false;
    a.resume_encoder = true;
    a.request_keyframe = true;
    a.reset_estimate = true;
    a.reset_rate = last_good_rate_ * cfg_.resume_rate_fraction;
    return a;
  }
  TimeDelta silent = now - last_feedback_;
  if (state_ != LinkState::kDown && silent >= cfg_.down_after) {
    state_ = LinkState::kDown;
    down_since_ = last_feedback_;
    ++outages_;
    a.pause_encoder = true;
    SM_EVENT(cfg_.events, now, "outage", "down", silent.ms_f(), 0);
  } else if (state_ == LinkState::kUp && silent >= cfg_.degraded_after) {
    state_ = LinkState::kDegraded;
    SM_EVENT(cfg_.events, now, "outage", "degraded", silent.ms_f(), 0);
  }
  if (state_ == LinkState::kDown && !ice_restart_sent_ && silent >= cfg_.ice_restart_after) {
    ice_restart_sent_ = true;
    a.ice_restart = true;
    ++ice_restarts_;
    SM_EVENT(cfg_.events, now, "outage", "ice_restart", silent.ms_f(), 0);
  }
  return a;
}

void OutageMonitor::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("state", static_cast<double>(state_));
  w.Counter("outages", outages_);
  w.Counter("ice_restarts", ice_restarts_);
  w.Hist("outage_ms", outage_ms_);
}

}  // namespace sm::cc
