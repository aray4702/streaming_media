// 7.4 Frame cadence for screen sharing.
//
// libwebrtc M157's FrameCadenceAdapter (video/frame_cadence_adapter.cc in
// compare/webrtc/reference/, issue #878), which replaces the block-hash
// FrameChangeDetector.
//
// A screen capturer delivers a frame only when the screen changes (a
// "zero-hertz" source: min fps 0). The adapter sits between the source and
// the encoder:
//   - Passthrough mode (camera, and any source that is not zero-hertz):
//     every frame goes to the encoder at once, and the input frame rate is a
//     RateStatistics over 2970 ms (kFrameRateAveragingWindowSizeMs).
//   - Zero-hertz mode, on when the encoder is configured for screen content
//     (SetZeroHertzModeEnabled, from VideoStreamEncoder for kScreen) and the
//     source's constraints are min fps 0 and max fps > 0
//     (IsZeroHertzScreenshareEnabled). Each frame is delayed by one frame
//     period (1 / max fps). When no newer frame has arrived, the last frame
//     is repeated (an empty update) every frame period until every layer's
//     quality has converged (quality_convergence.h), then every
//     kZeroHertzIdleRepeatRatePeriod (1000 ms). A new frame cancels the
//     repeats; a keyframe request resets convergence and cuts an idle
//     repeat short (ProcessKeyFrameRequest). From creation, and again after
//     the source discards a frame, the adapter asks the source for a refresh
//     frame after 3 frame periods (kOnDiscardedFrameRefreshFramePeriod) and
//     then every frame period until a frame arrives. A restricted max frame
//     rate (UpdateVideoSourceRestrictions) lengthens the short repeats.
//
// The library reads no clock and runs no task queue: the caller passes `now`,
// calls Process(now) at NextProcessTime(), before any other call at that
// `now` and again after it, and receives the frames to encode and the
// refresh-frame requests in order. The delayed tasks run in the order libwebrtc's queue would run them
// (due time, then post order). Frames are opaque handles; the caller keeps
// the pixels. Differences from libwebrtc:
//   - Encoding takes no time on the caller's clock and frames are handled on
//     the caller's thread, so `queue_overload` (frames waiting on the encoder
//     queue while an encode overran a frame period) is never set.
//   - The VSync (metronome) mode is not ported: libwebrtc uses it only when
//     the call is given an encode metronome; without one it uses passthrough.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/rate_statistics.h"
#include "sm/core/units.h"

namespace sm::video {

// api/video_track_source_constraints.h.
struct VideoTrackSourceConstraints {
  std::optional<double> min_fps;
  std::optional<double> max_fps;
};

class FrameCadenceAdapter : public MetricsSource {
 public:
  // frame_cadence_adapter.h:39-48.
  static constexpr int64_t kFrameRateAveragingWindowSizeMs = (1000 / 30) * 90;
  static constexpr TimeDelta kZeroHertzIdleRepeatRatePeriod = TimeDelta::Millis(1000);
  static constexpr int kOnDiscardedFrameRefreshFramePeriod = 3;

  struct ZeroHertzModeParams {
    size_t num_simulcast_layers = 0;
  };

  // What Process() hands the caller, in order.
  struct Output {
    enum class Kind { kEncode, kRequestRefreshFrame };
    Kind kind = Kind::kEncode;
    // kEncode: the frame's handle, its capture time (for a repeat, the first
    // frame's capture time plus the time since repeating started), when it
    // was handed to the encoder, and whether it repeats the previous frame
    // (VideoFrame::is_repeat_frame, an empty update rect).
    int64_t frame = 0;
    Timestamp capture_time = Timestamp::Millis(0);
    Timestamp post_time = Timestamp::Millis(0);
    bool is_repeat = false;
    bool queue_overload = false;
  };

  explicit FrameCadenceAdapter(EventSink* events = nullptr) : events_(events) {}

  // Configuration, as FrameCadenceAdapterInterface.
  void SetZeroHertzModeEnabled(std::optional<ZeroHertzModeParams> params, Timestamp now);
  void OnConstraintsChanged(const VideoTrackSourceConstraints& constraints, Timestamp now);
  void UpdateLayerQualityConvergence(size_t spatial_index, bool converged);
  void UpdateLayerStatus(size_t spatial_index, bool enabled);
  void UpdateVideoSourceRestrictions(std::optional<double> max_frame_rate);
  void ProcessKeyFrameRequest(Timestamp now);

  // Source side. `capture_time` is the frame's timestamp_us.
  void OnFrame(int64_t frame, Timestamp capture_time, Timestamp now);
  void OnDiscardedFrame(Timestamp now);

  // Runs every task due at or before `now`.
  std::vector<Output> Process(Timestamp now);
  // The earliest pending task, or PlusInfinity.
  Timestamp NextProcessTime() const;

  bool zero_hertz() const { return zero_hertz_.has_value(); }
  std::optional<uint32_t> GetInputFrameRateFps() const;

  void ExportMetrics(MetricsWriter& w) const override;

 private:
  enum class TaskKind { kDeliver, kDelayedCadence, kRepeat, kRefresh };
  struct Task {
    Timestamp due;
    uint64_t seq;
    TaskKind kind;
    uint64_t adapter;      // zero-hertz instance; its tasks die with it (SafeTask).
    int frame_id = 0;      // kRepeat.
    Timestamp post_time;   // kDeliver, kDelayedCadence.
    int64_t frame = 0;     // kDeliver.
    Timestamp capture_time;  // kDeliver.
  };
  struct QueuedFrame {
    int64_t frame;
    Timestamp capture_time;
  };
  struct ScheduledRepeat {
    Timestamp scheduled;
    bool idle;
    Timestamp origin;
    Timestamp origin_capture_time;
  };
  // ZeroHertzAdapterMode's state.
  struct ZeroHertz {
    uint64_t id = 0;
    double max_fps = 0;
    TimeDelta frame_delay = TimeDelta::Zero();
    std::deque<QueuedFrame> queued_frames;
    int current_frame_id = 0;
    std::optional<ScheduledRepeat> scheduled_repeat;
    std::vector<std::optional<bool>> layer_trackers;  // quality_converged; empty = disabled.
    bool refresh_running = false;
    uint64_t refresh_generation = 0;
    std::optional<TimeDelta> restricted_frame_delay;
  };

  void Post(Task t);
  bool IsZeroHertzScreenshareEnabled() const;
  void MaybeReconfigureAdapters(bool was_zero_hertz_enabled, Timestamp now);
  // ZeroHertzAdapterMode.
  void ZhOnFrame(const QueuedFrame& f, Timestamp post_time, Timestamp now);
  bool HasQualityConverged() const;
  void ResetQualityConvergenceInfo();
  void ProcessOnDelayedCadence(Timestamp post_time, Timestamp now, std::vector<Output>& out);
  void ScheduleRepeat(int frame_id, bool idle_repeat, Timestamp now);
  void ProcessRepeatedFrameOnDelayedCadence(int frame_id, Timestamp now, std::vector<Output>& out);
  void SendFrameNow(const QueuedFrame& f, bool is_repeat, Timestamp now, std::vector<Output>& out);
  TimeDelta FrameDuration() const;
  TimeDelta RepeatDuration(bool idle_repeat) const;
  void MaybeStartRefreshFrameRequester(Timestamp now);
  void StopRefreshFrameRequester();

  EventSink* events_;
  std::vector<Task> tasks_;
  uint64_t next_seq_ = 0;
  uint64_t next_adapter_id_ = 1;
  std::optional<ZeroHertz> zero_hertz_;
  std::optional<ZeroHertzModeParams> zero_hertz_params_;
  std::optional<VideoTrackSourceConstraints> source_constraints_;
  std::optional<double> restricted_max_frame_rate_;
  // PassthroughAdapterMode's frame rate, updated for every frame in both modes.
  RateStatistics input_framerate_{kFrameRateAveragingWindowSizeMs, 1000};
  std::optional<int64_t> last_frame_rate_;

  uint64_t frames_in_ = 0, frames_out_ = 0, repeats_ = 0, idle_repeats_ = 0, refresh_requests_ = 0;
};

}  // namespace sm::video
