// Call-wide RTT statistics, as libwebrtc M157's CallStats
// (compare/webrtc/reference/video/call_stats2.{h,cc}). Every RTP/RTCP module
// of a call reports its RTT here from its 1 s PeriodicUpdate
// (rtp_rtcp_impl2.cc PeriodicUpdate: a sender's last report-block RTT, a
// receive-only stream's XR DLRR RTT). CallStats keeps the reports of the last
// 1.5 s and, on every report and every 1 s, gives its observers the maximum
// of them and a smoothed average (70 % old, 30 % new). VideoReceiveStream2::
// OnRttUpdate hands the maximum to NackRequester and the jitter estimator.
// Part of the RTP session layer (docs/design/rtp-session.md). Time is passed
// in; no clock, no threads.
#pragma once

#include <cstdint>
#include <deque>
#include <optional>

#include "sm/core/units.h"

namespace sm::rtp {

class CallStats {
 public:
  // CallStats::kUpdateInterval.
  static constexpr TimeDelta kUpdateInterval = TimeDelta::Millis(1000);
  // RemoveOldReports' kRttTimeoutMs.
  static constexpr int64_t kRttTimeoutMs = 1500;

  // What CallStatsObserver::OnRttUpdate receives.
  struct Update {
    int64_t avg_rtt_ms = -1;
    int64_t max_rtt_ms = -1;
  };

  // EnsureStarted: the repeating update runs kUpdateInterval after `now`
  // (RepeatingTaskHandle::DelayedStart). Later calls do nothing.
  void EnsureStarted(Timestamp now);
  // RtcpRttStats::OnRttUpdate: one module's RTT. Returns the update the
  // observers get at once (UpdateAndReport).
  std::optional<Update> OnRttUpdate(int64_t rtt_ms, Timestamp now);
  // Runs the repeating update if it is due; returns the last update it gave
  // the observers, if any (none while no report is younger than 1.5 s).
  std::optional<Update> Process(Timestamp now);
  // When Process must run next (infinite before EnsureStarted).
  Timestamp NextProcessTime() const { return next_update_; }

  // LastProcessedRtt: the average, or -1 when there is none.
  int64_t LastProcessedRtt() const { return avg_rtt_ms_; }
  int64_t max_rtt_ms() const { return max_rtt_ms_; }

 private:
  struct RttTime {
    int64_t rtt;
    int64_t time;
  };
  std::optional<Update> UpdateAndReport(Timestamp now);

  int64_t max_rtt_ms_ = -1;
  int64_t avg_rtt_ms_ = -1;
  Timestamp next_update_ = Timestamp::PlusInfinity();
  std::deque<RttTime> reports_;  // Oldest first.
};

}  // namespace sm::rtp
