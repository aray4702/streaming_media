// 7.7 (render stage) Pre-render smoothing: libwebrtc's render queue.
//
// A port of libwebrtc M157's IncomingVideoStream and VideoRenderFrames
// (video/render/incoming_video_stream.{h,cc}, video/render/video_render_frames.{h,cc}),
// which VideoReceiveStream2 puts between the decoder and the renderer when
// `enable_prerenderer_smoothing` is on, its default (video/video_receive_stream2.cc:400).
//
// Each decoded frame carries its render time. The queue:
//   - drops a frame whose render time is more than 500 ms in the past, but only
//     when other frames are waiting (kOldRenderTimestampMS);
//   - drops a frame more than 10 s in the future (kFutureRenderTimestampMS);
//   - drops a frame whose render time is earlier than the last one queued
//     ("scheduled out of order");
//   - releases frames `render_delay` before their render time; when several are
//     due at once, only the newest is rendered and the older ones are dropped.
// A frame added to an empty queue is dequeued at once (IncomingVideoStream::OnFrame
// calls Dequeue when AddFrame returns 1); while frames wait, the next dequeue runs
// at the front frame's release time (PostDelayedHighPrecisionTask). Times are in
// whole ms, as libwebrtc's clock reads.
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::video {

struct RenderFramesConfig {
  // VideoReceiveStreamInterface::Config::render_delay_ms (10); values outside
  // [10, 500] ms become 10 (EnsureValidRenderDelay).
  TimeDelta render_delay = TimeDelta::Millis(10);
  EventSink* events = nullptr;
};

struct RenderFramesStats {
  uint64_t frames_in = 0, rendered = 0;
  uint64_t dropped_old = 0, dropped_future = 0, dropped_out_of_order = 0, dropped_skipped = 0;
  uint64_t dropped() const { return dropped_old + dropped_future + dropped_out_of_order + dropped_skipped; }
};

// A frame leaving the render queue: shown, or dropped without being shown.
struct RenderDecision {
  int64_t id = 0;
  bool render = false;
};

class RenderFrames : public MetricsSource {
 public:
  explicit RenderFrames(RenderFramesConfig cfg = {});
  // A decoded frame (IncomingVideoStream::OnFrame). Decisions are queued for TakeDecisions().
  void OnFrame(int64_t id, Timestamp render_time, Timestamp now);
  // Runs the delayed dequeue when it is due.
  void Process(Timestamp now);
  std::vector<RenderDecision> TakeDecisions();
  // Next time Process() has work to do.
  Timestamp NextWakeup() const { return next_dequeue_; }
  size_t queued() const { return queue_.size(); }
  TimeDelta render_delay() const { return TimeDelta::Millis(render_delay_ms_); }
  const RenderFramesStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  struct Entry {
    int64_t id;
    int64_t render_time_ms;
  };
  // VideoRenderFrames::AddFrame: the queue size, or -1 if the frame was dropped.
  int AddFrame(int64_t id, int64_t render_time_ms, int64_t now_ms);
  // IncomingVideoStream::Dequeue.
  void Dequeue(Timestamp now);
  int64_t TimeToNextFrameReleaseMs(int64_t now_ms) const;

  RenderFramesConfig cfg_;
  int64_t render_delay_ms_;
  std::deque<Entry> queue_;  // Oldest first.
  int64_t last_render_time_ms_ = 0;
  Timestamp next_dequeue_ = Timestamp::PlusInfinity();
  std::vector<RenderDecision> out_;
  RenderFramesStats stats_;
};

}  // namespace sm::video
