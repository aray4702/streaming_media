// Congestion-window pushback (issue #53; part of the send-side controller, 5.4).
//
// Port of libwebrtc's CongestionWindowPushbackController
// (modules/congestion_controller/goog_cc/congestion_window_pushback_controller.cc)
// and of the data-window arithmetic in GoogCcNetworkController
// (UpdateCongestionWindowSize). The congestion window is the data the path
// may hold: the target rate times (the smallest of the recent maximum
// feedback RTTs + `queue_time`), averaged with the previous window and never
// below two packets. When the bytes in flight (optionally plus the pacer
// queue) exceed it, the encoder is asked for less: a ratio shrinks by 0.9
// (fill > 1.5), 0.95 (fill > 1) or grows by 1.05 (fill <= 1, back to 1.0 at
// fill < 0.1), and the encoder target is target x ratio, never below
// `min_pushback_rate` unless the target itself is.
//
// Like libwebrtc with "WebRTC-CongestionWindow/DropFrame:true", the ratio is
// handed to the encoder as `reduce_ratio()`: the encoder keeps the full
// target and drops one captured frame in every N (the interval from
// FrameDropInterval(), VideoStreamEncoder::UpdateTargetBitrate), lowering its
// own target to match. The pacing rate is not reduced, so the pacer does not
// build a queue when pushback occurs.
#pragma once

#include <cstdint>
#include <deque>
#include <optional>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::cc {

struct CongestionWindowConfig {
  bool enabled = true;
  // "QueueSize": the standing queue the window allows on top of the RTT.
  TimeDelta queue_time = TimeDelta::Millis(350);
  // "MinBitrate": pushback never takes the target below this (unless the
  // target already is).
  DataRate min_pushback_rate = DataRate::KilobitsPerSec(30);
  // "WebRTC-AddPacingToCongestionWindowPushback": count the pacer queue.
  bool add_pacing = false;
  // Feedback RTT window (GoogCc kMaxFeedbackRttWindow).
  size_t rtt_window = 32;
  EventSink* events = nullptr;
};

class CongestionWindowPushback : public MetricsSource {
 public:
  explicit CongestionWindowPushback(CongestionWindowConfig cfg = {}) : cfg_(cfg) {}

  // One per transport feedback: the largest (feedback time - send time) of
  // its received packets.
  void OnFeedbackRtt(TimeDelta max_feedback_rtt);
  // Recomputes the window from the loss-based target. Call after each
  // feedback and on the process interval.
  void UpdateWindow(DataRate target);
  // Sets the window directly (libwebrtc SetDataWindow; the tests use it).
  void SetDataWindow(DataSize w) { window_ = w; }
  void UpdateOutstandingData(DataSize in_flight) { outstanding_ = in_flight; }
  void UpdatePacingQueue(DataSize queued) { pacing_ = queued; }
  DataSize pacing_queue() const { return pacing_; }
  // The encoder target after pushback. Moves the ratio, so call it where
  // libwebrtc does (after feedback, the process interval, every sent packet).
  DataRate UpdateTargetBitrate(DataRate target, Timestamp now);

  // 1 - pushback target / target at the last update; 0 when not pushing.
  double reduce_ratio() const { return reduce_ratio_; }
  double encoding_rate_ratio() const { return ratio_; }
  std::optional<DataSize> data_window() const { return window_; }

  // Encoder side: drop every N-th captured frame (0 = none) and the target
  // the encoder rate control then gets. Mirrors
  // VideoStreamEncoder::UpdateTargetBitrate (frame dropping needs a ratio over
  // 1% and a target above `codec_min`; at most every second frame).
  struct FrameDrop {
    int interval = 0;
    DataRate encoder_target;
  };
  static FrameDrop FrameDropInterval(DataRate target, double reduce_ratio, DataRate codec_min);

  void ExportMetrics(MetricsWriter& w) const override;

 private:
  CongestionWindowConfig cfg_;
  std::deque<int64_t> rtts_ms_;
  std::optional<DataSize> window_;
  DataSize outstanding_;
  DataSize pacing_;
  double ratio_ = 1.0;
  double reduce_ratio_ = 0;
  uint64_t reductions_ = 0;
  Histogram fill_;
};

}  // namespace sm::cc
