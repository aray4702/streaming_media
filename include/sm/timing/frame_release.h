// 7.7 (v2) Frame release as libwebrtc M157 (issue #24).
//
// A port of the scheduling half of libwebrtc's VideoStreamBufferController
// (video/video_stream_buffer_controller.cc) and FrameDecodeTiming
// (video/frame_decode_timing.cc), used by rtp::RtpReceiver under
// `timing.version = 2` in place of video::FrameScheduler:
//   - the next decodable frame is scheduled when it reaches the front, with
//     its render time from VcmTiming::RenderTime and a latest decode time of
//     render_time - estimated decode time - render delay (10 ms), clamped to
//     [now, now + the 3 s frame time-out - 1 ms];
//   - a frame is fast-forwarded (dropped without decoding) only if it is at
//     least 5 ms past that point *and* a later temporal unit is decodable.
//     Delta frames each reference the one before them, so only a later
//     keyframe can be; a late delta frame with no keyframe behind it is
//     decoded and rendered, however late (libwebrtc has no 10 ms late-drop
//     rule);
//   - on release: VcmTiming::OnDecodableTemporalUnit (jitter estimate,
//     minimum delay) and, unless the frame was retransmitted,
//     UpdateCurrentDelay; the timing is reset if the render time is more
//     than 10 s from now or the target delay is over 10 s;
//   - while a keyframe is required (call start) the first keyframe is
//     released at once and the frames before it are dropped.
// The decoder is modelled as ready again as soon as a frame is released.
// libwebrtc's FrameHasBadRenderTiming/TargetVideoDelayIsTooLarge
// (video/frame_helpers.cc) are not in compare/webrtc/reference/; the 10 s
// bounds follow their kMaxVideoDelay (noted on #57).
#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"
#include "sm/timing/vcm_timing.h"

namespace sm::timing {

struct ReleaseFrame {
  int64_t id = 0;
  uint32_t rtp_timestamp = 0;
  bool keyframe = false;
  DataSize size;
  Timestamp receive_time;      // Arrival of the frame's last packet.
  bool retransmitted = false;  // A packet of it was NACKed (delayed_by_retransmission).
};

struct ReleaseAction {
  int64_t id = 0;
  bool decode = false;   // false: dropped without decoding (fast-forward or waiting for a keyframe).
  Timestamp render_time;
  TimeDelta lateness;    // now - latest decode time; positive = late.
};

struct FrameReleaseConfig {
  TimeDelta max_wait_for_frame = TimeDelta::Seconds(3);        // kMaxWaitForFrame.
  TimeDelta max_wait_for_keyframe = TimeDelta::Millis(200);    // kMaxWaitForKeyFrame.
  EventSink* events = nullptr;
};

struct FrameReleaseStats {
  uint64_t frames_in = 0, released = 0, fast_forwarded = 0, dropped_for_keyframe = 0, timing_resets = 0;
  Histogram lateness_ms;  // Released frames, negative clamped to 0.
};

class FrameReleaseController : public MetricsSource {
 public:
  FrameReleaseController(VcmTiming* timing, FrameReleaseConfig cfg = {});
  // A decodable frame, in decode order. Calls VcmTiming::OnCompleteFrame.
  void Insert(const ReleaseFrame& f);
  // VideoStreamBufferController::MaybeScheduleFrameForRelease, which InsertFrame
  // calls for each frame that adds decodable units (video_stream_buffer_controller.cc:
  // 153-177, 380-423): call it after each Insert. It schedules the next frame
  // with the render time VcmTiming gives it now, from the timestamps inserted
  // so far, and keeps a schedule already made for that frame; it drops late
  // frames ahead of the last decodable one, and releases a required keyframe at
  // once (ForceKeyFrameReleaseImmediately). Returns those drops and releases.
  std::vector<ReleaseAction> MaybeSchedule(Timestamp now, TimeDelta decode_time);
  // Releases what is due. `decode_time` is fed to the decode-time estimate
  // for each decoded frame (the decoder's measured time in libwebrtc).
  std::vector<ReleaseAction> Process(Timestamp now, TimeDelta decode_time);
  // The frame buffer was cleared (FrameBuffer::Clear): the queued frames and
  // the scheduled one are forgotten, without decoding.
  void Clear();
  Timestamp NextWakeup() const;
  size_t queued() const { return queue_.size(); }
  const FrameReleaseStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  struct Schedule {
    int64_t id;
    Timestamp latest_decode_time;
    Timestamp render_time;
  };
  TimeDelta MaxWaitingTime(Timestamp render_time, Timestamp now, bool too_many_frames_queued) const;
  void Release(Timestamp render_time, Timestamp now, TimeDelta decode_time, std::vector<ReleaseAction>* out);
  void Drop(Timestamp now, bool fast_forward, std::vector<ReleaseAction>* out);
  int64_t LastDecodableId() const;
  // FrameDecodeTiming::OnFrameBufferUpdated for the next frame, and the drops
  // when it is too late: leaves the next frame scheduled, or the queue empty.
  void ScheduleNext(Timestamp now, std::vector<ReleaseAction>* out);

  VcmTiming* timing_;
  FrameReleaseConfig cfg_;
  std::deque<ReleaseFrame> queue_;
  std::optional<Schedule> scheduled_;
  bool keyframe_required_ = true;
  Timestamp last_decode_scheduled_ = Timestamp::Micros(0);
  std::optional<Timestamp> timeout_;  // VideoReceiveStreamTimeoutTracker deadline.
  FrameReleaseStats stats_;
};

}  // namespace sm::timing
