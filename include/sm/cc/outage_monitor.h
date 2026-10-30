// 5.11 Link outage detection and recovery.
//
// Watches feedback from the remote side (RTCP, transport feedback, any
// packet). States:
//   kUp        feedback within degraded_after
//   kDegraded  no feedback for degraded_after
//   kDown      no feedback for down_after
// Process() also proposes actions (OutageActions): an encoder pause on kDown,
// an ICE restart request after ice_restart_after of continuous outage, and on
// the first feedback after kDown a resume, a keyframe and a reset of the
// estimate to a fraction of the pre-outage rate.
//
// libwebrtc M157 has no outage monitor and takes no sender action on an
// outage: SendSideBandwidthEstimation backs off on missing feedback
// (kMaxRtcpFeedbackInterval) and the receiver's decode time-out asks for the
// keyframe. So on the call path rtp::SendSideController runs Process() every
// time it processes, only to follow the link (state, metrics, events), and
// acts on none of these actions (issues #151, #849).
#pragma once

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::cc {

enum class LinkState { kUp, kDegraded, kDown };
const char* ToString(LinkState s);

struct OutageConfig {
  TimeDelta degraded_after = TimeDelta::Millis(600);
  TimeDelta down_after = TimeDelta::Millis(2000);
  TimeDelta ice_restart_after = TimeDelta::Seconds(5);
  double resume_rate_fraction = 0.5;
  EventSink* events = nullptr;
};

struct OutageActions {
  bool pause_encoder = false;
  bool resume_encoder = false;
  bool request_keyframe = false;
  bool ice_restart = false;
  bool reset_estimate = false;
  DataRate reset_rate;
};

class OutageMonitor : public MetricsSource {
 public:
  explicit OutageMonitor(OutageConfig cfg = {}) : cfg_(cfg) {}
  void OnFeedback(Timestamp now);
  void SetCurrentRate(DataRate r) {
    if (state_ == LinkState::kUp) last_good_rate_ = r;
  }
  OutageActions Process(Timestamp now);
  LinkState state() const { return state_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  OutageConfig cfg_;
  LinkState state_ = LinkState::kUp;
  Timestamp last_feedback_ = Timestamp::MinusInfinity();
  Timestamp down_since_ = Timestamp::MinusInfinity();
  bool recovered_pending_ = false;
  bool ice_restart_sent_ = false;
  DataRate last_good_rate_ = DataRate::KilobitsPerSec(300);
  uint64_t outages_ = 0, ice_restarts_ = 0;
  Histogram outage_ms_;
};

}  // namespace sm::cc
