#include "sm/video/frame_cadence_adapter.h"

#include <algorithm>
#include <cmath>

namespace sm::video {

namespace {
// TimeDelta::Seconds(1) / fps: unit_base.h's division by a double rounds
// to the nearest microsecond (std::llround). Deviation (issue #887): at least
// 1 us. From about 2e6 fps libwebrtc's period rounds to 0 and each repeat is
// due when it is posted, which spins its encoder queue; here one Process()
// call would never return. Rates below 2e6 fps get libwebrtc's period.
TimeDelta PeriodOf(double fps) { return TimeDelta::Micros(std::max<int64_t>(1, std::llround(1e6 / fps))); }
}  // namespace

void FrameCadenceAdapter::Post(Task t) {
  t.seq = next_seq_++;
  tasks_.push_back(t);
}

Timestamp FrameCadenceAdapter::NextProcessTime() const {
  Timestamp next = Timestamp::PlusInfinity();
  for (const Task& t : tasks_) next = std::min(next, t.due);
  return next;
}

std::vector<FrameCadenceAdapter::Output> FrameCadenceAdapter::Process(Timestamp now) {
  std::vector<Output> out;
  for (;;) {
    // The earliest due task, in post order among equal times, as the task
    // queue runs them.
    auto it = std::min_element(tasks_.begin(), tasks_.end(), [](const Task& a, const Task& b) {
      return a.due != b.due ? a.due < b.due : a.seq < b.seq;
    });
    if (it == tasks_.end() || it->due > now) break;
    Task t = *it;
    tasks_.erase(it);
    if (t.adapter != 0 && (!zero_hertz_ || zero_hertz_->id != t.adapter)) continue;  // SafeTask.
    // A task runs at its due time; the caller may be late.
    Timestamp at = t.due;
    switch (t.kind) {
      case TaskKind::kDeliver: {
        // FrameCadenceAdapterImpl::OnFrameOnMainQueue.
        QueuedFrame f{t.frame, t.capture_time};
        if (zero_hertz_) {
          ZhOnFrame(f, t.post_time, at);
        } else {
          Output o;
          o.frame = f.frame;
          o.capture_time = f.capture_time;
          o.post_time = t.post_time;
          out.push_back(o);
          ++frames_out_;
        }
        // PassthroughAdapterMode::UpdateFrameRate: RateStatistics reports
        // too high a rate right after an Update.
        last_frame_rate_ = input_framerate_.Rate(t.post_time.ms());
        input_framerate_.Update(1, t.post_time.ms());
        break;
      }
      case TaskKind::kDelayedCadence:
        ProcessOnDelayedCadence(t.post_time, at, out);
        break;
      case TaskKind::kRepeat:
        ProcessRepeatedFrameOnDelayedCadence(t.frame_id, at, out);
        break;
      case TaskKind::kRefresh: {
        if (!zero_hertz_->refresh_running || t.frame_id != static_cast<int>(zero_hertz_->refresh_generation)) break;
        Output o;
        o.kind = Output::Kind::kRequestRefreshFrame;
        out.push_back(o);
        ++refresh_requests_;
        SM_EVENT(events_, at, "frame_cadence", "refresh_frame_request", 1, 0);
        Task next = t;
        next.due = at + zero_hertz_->frame_delay;
        Post(next);
        break;
      }
    }
  }
  return out;
}

void FrameCadenceAdapter::OnFrame(int64_t frame, Timestamp capture_time, Timestamp now) {
  ++frames_in_;
  Task t{};
  t.due = now;
  t.kind = TaskKind::kDeliver;
  t.adapter = 0;
  t.post_time = now;
  t.frame = frame;
  t.capture_time = capture_time;
  Post(t);
}

void FrameCadenceAdapter::OnDiscardedFrame(Timestamp now) {
  if (zero_hertz_) MaybeStartRefreshFrameRequester(now);
}

void FrameCadenceAdapter::SetZeroHertzModeEnabled(std::optional<ZeroHertzModeParams> params, Timestamp now) {
  bool was_zero_hertz_enabled = zero_hertz_params_.has_value();
  zero_hertz_params_ = params;
  MaybeReconfigureAdapters(was_zero_hertz_enabled, now);
}

void FrameCadenceAdapter::OnConstraintsChanged(const VideoTrackSourceConstraints& constraints, Timestamp now) {
  bool was_zero_hertz_enabled = IsZeroHertzScreenshareEnabled();
  source_constraints_ = constraints;
  MaybeReconfigureAdapters(was_zero_hertz_enabled, now);
}

bool FrameCadenceAdapter::IsZeroHertzScreenshareEnabled() const {
  return source_constraints_.has_value() && source_constraints_->max_fps.value_or(-1) > 0 &&
         source_constraints_->min_fps.value_or(-1) == 0 && zero_hertz_params_.has_value();
}

// FrameCadenceAdapterImpl::MaybeReconfigureAdapters (frame_cadence_adapter.cc:1012-1037).
void FrameCadenceAdapter::MaybeReconfigureAdapters(bool was_zero_hertz_enabled, Timestamp now) {
  if (IsZeroHertzScreenshareEnabled()) {
    std::optional<uint32_t> fps = GetInputFrameRateFps();
    bool max_fps_has_changed =
        (fps ? static_cast<double>(*fps) : -1.0) != source_constraints_->max_fps.value_or(-1);
    if (!was_zero_hertz_enabled || max_fps_has_changed) {
      ZeroHertz zh;
      zh.id = next_adapter_id_++;
      zh.max_fps = *source_constraints_->max_fps;
      zh.frame_delay = PeriodOf(zh.max_fps);
      zero_hertz_ = std::move(zh);
      SM_EVENT(events_, now, "frame_cadence", "zero_hertz_on", zero_hertz_->max_fps, 0);
      MaybeStartRefreshFrameRequester(now);
      UpdateVideoSourceRestrictions(restricted_max_frame_rate_);
    }
    // ReconfigureParameters: every layer starts unconverged.
    zero_hertz_->layer_trackers.assign(zero_hertz_params_->num_simulcast_layers, std::optional<bool>(false));
  } else if (was_zero_hertz_enabled && zero_hertz_) {
    zero_hertz_.reset();
    SM_EVENT(events_, now, "frame_cadence", "zero_hertz_off", 0, 0);
  }
}

std::optional<uint32_t> FrameCadenceAdapter::GetInputFrameRateFps() const {
  if (zero_hertz_) return static_cast<uint32_t>(zero_hertz_->max_fps);
  if (last_frame_rate_) return static_cast<uint32_t>(*last_frame_rate_);
  return std::nullopt;
}

void FrameCadenceAdapter::UpdateLayerQualityConvergence(size_t spatial_index, bool converged) {
  if (!zero_hertz_ || spatial_index >= zero_hertz_->layer_trackers.size()) return;
  auto& tracker = zero_hertz_->layer_trackers[spatial_index];
  if (tracker.has_value()) tracker = converged;
}

void FrameCadenceAdapter::UpdateLayerStatus(size_t spatial_index, bool enabled) {
  if (!zero_hertz_ || spatial_index >= zero_hertz_->layer_trackers.size()) return;
  auto& tracker = zero_hertz_->layer_trackers[spatial_index];
  if (enabled) {
    if (!tracker.has_value()) tracker = false;  // Unconverged until told otherwise.
  } else {
    tracker = std::nullopt;
  }
}

void FrameCadenceAdapter::UpdateVideoSourceRestrictions(std::optional<double> max_frame_rate) {
  restricted_max_frame_rate_ = max_frame_rate;
  if (!zero_hertz_) return;
  if (max_frame_rate.value_or(0) > 0) {
    zero_hertz_->restricted_frame_delay = PeriodOf(*max_frame_rate);
  } else {
    zero_hertz_->restricted_frame_delay = std::nullopt;
  }
}

// ZeroHertzAdapterMode::ProcessKeyFrameRequest (frame_cadence_adapter.cc:554-592).
void FrameCadenceAdapter::ProcessKeyFrameRequest(Timestamp now) {
  if (!zero_hertz_) return;
  ZeroHertz& zh = *zero_hertz_;
  // The next frame is a keyframe, which needs refinement frames: no idle
  // repeats right after it.
  ResetQualityConvergenceInfo();
  // Not repeating, or repeating at the short period: a frame goes out soon.
  if (!zh.scheduled_repeat || !zh.scheduled_repeat->idle) return;
  // The idle repeat is due within a frame period anyway.
  if (zh.scheduled_repeat->scheduled + RepeatDuration(/*idle_repeat=*/true) - now <= zh.frame_delay) return;
  // Cancel the idle repeat and repeat at the short period from now.
  SM_EVENT(events_, now, "frame_cadence", "keyframe_short_repeat", 1, 0);
  ScheduleRepeat(++zh.current_frame_id, /*idle_repeat=*/false, now);
}

// ZeroHertzAdapterMode::OnFrame (frame_cadence_adapter.cc:490-521).
void FrameCadenceAdapter::ZhOnFrame(const QueuedFrame& f, Timestamp post_time, Timestamp now) {
  ZeroHertz& zh = *zero_hertz_;
  StopRefreshFrameRequester();
  ResetQualityConvergenceInfo();
  if (zh.scheduled_repeat.has_value()) zh.queued_frames.pop_front();  // The repeated frame.
  zh.queued_frames.push_back(f);
  zh.current_frame_id++;
  zh.scheduled_repeat.reset();
  TimeDelta time_spent_since_post = now - post_time;
  Task t{};
  t.due = now + std::max(zh.frame_delay - time_spent_since_post, TimeDelta::Zero());
  t.kind = TaskKind::kDelayedCadence;
  t.adapter = zh.id;
  t.post_time = post_time;
  Post(t);
}

bool FrameCadenceAdapter::HasQualityConverged() const {
  // Unconverged with no layers configured; a disabled layer counts as
  // converged.
  const auto& trackers = zero_hertz_->layer_trackers;
  return !trackers.empty() &&
         std::all_of(trackers.begin(), trackers.end(), [](const std::optional<bool>& c) { return c.value_or(true); });
}

void FrameCadenceAdapter::ResetQualityConvergenceInfo() {
  for (auto& tracker : zero_hertz_->layer_trackers) {
    if (tracker.has_value()) tracker = false;
  }
}

// ZeroHertzAdapterMode::ProcessOnDelayedCadence (frame_cadence_adapter.cc:621-641).
void FrameCadenceAdapter::ProcessOnDelayedCadence(Timestamp /*post_time*/, Timestamp now, std::vector<Output>& out) {
  ZeroHertz& zh = *zero_hertz_;
  if (zh.queued_frames.empty()) return;
  QueuedFrame front = zh.queued_frames.front();
  if (zh.queued_frames.size() > 1) {
    zh.queued_frames.pop_front();  // A newer frame follows: no repeats of this one.
  } else {
    ScheduleRepeat(zh.current_frame_id, HasQualityConverged(), now);
  }
  SendFrameNow(front, /*is_repeat=*/false, now, out);
}

void FrameCadenceAdapter::ScheduleRepeat(int frame_id, bool idle_repeat, Timestamp now) {
  ZeroHertz& zh = *zero_hertz_;
  if (!zh.scheduled_repeat.has_value()) {
    zh.scheduled_repeat = ScheduledRepeat{now, false, now, zh.queued_frames.front().capture_time};
  }
  zh.scheduled_repeat->scheduled = now;
  zh.scheduled_repeat->idle = idle_repeat;
  Task t{};
  t.due = now + RepeatDuration(idle_repeat);
  t.kind = TaskKind::kRepeat;
  t.adapter = zh.id;
  t.frame_id = frame_id;
  Post(t);
}

// ZeroHertzAdapterMode::ProcessRepeatedFrameOnDelayedCadence (frame_cadence_adapter.cc:662-700).
void FrameCadenceAdapter::ProcessRepeatedFrameOnDelayedCadence(int frame_id, Timestamp now, std::vector<Output>& out) {
  ZeroHertz& zh = *zero_hertz_;
  if (frame_id != zh.current_frame_id || zh.queued_frames.empty() || !zh.scheduled_repeat) return;  // Cancelled.
  QueuedFrame frame = zh.queued_frames.front();
  // The repeat's timestamp moves on by the time since repeating started.
  TimeDelta total_delay = now - zh.scheduled_repeat->origin;
  if (frame.capture_time > Timestamp::Millis(0)) frame.capture_time = zh.scheduled_repeat->origin_capture_time + total_delay;
  ++repeats_;
  if (zh.scheduled_repeat->idle) ++idle_repeats_;  // This repeat came on the idle period.
  // The next repeat is scheduled before the frame goes to the encoder.
  ScheduleRepeat(frame_id, HasQualityConverged(), now);
  SendFrameNow(frame, /*is_repeat=*/true, now, out);
}

void FrameCadenceAdapter::SendFrameNow(const QueuedFrame& f, bool is_repeat, Timestamp now, std::vector<Output>& out) {
  Output o;
  o.frame = f.frame;
  o.capture_time = f.capture_time;
  o.post_time = now;
  o.is_repeat = is_repeat;
  out.push_back(o);
  ++frames_out_;
}

TimeDelta FrameCadenceAdapter::FrameDuration() const {
  const ZeroHertz& zh = *zero_hertz_;
  return std::max(zh.frame_delay, zh.restricted_frame_delay.value_or(zh.frame_delay));
}

TimeDelta FrameCadenceAdapter::RepeatDuration(bool idle_repeat) const {
  return idle_repeat ? kZeroHertzIdleRepeatRatePeriod : FrameDuration();
}

// ZeroHertzAdapterMode::MaybeStartRefreshFrameRequester (frame_cadence_adapter.cc:752-766).
void FrameCadenceAdapter::MaybeStartRefreshFrameRequester(Timestamp now) {
  ZeroHertz& zh = *zero_hertz_;
  if (zh.refresh_running) return;
  zh.refresh_running = true;
  ++zh.refresh_generation;
  Task t{};
  t.due = now + kOnDiscardedFrameRefreshFramePeriod * zh.frame_delay;
  t.kind = TaskKind::kRefresh;
  t.adapter = zh.id;
  t.frame_id = static_cast<int>(zh.refresh_generation);
  Post(t);
}

void FrameCadenceAdapter::StopRefreshFrameRequester() { zero_hertz_->refresh_running = false; }

void FrameCadenceAdapter::ExportMetrics(MetricsWriter& w) const {
  w.Counter("frames_in", frames_in_);
  w.Counter("frames_out", frames_out_);
  w.Counter("repeats", repeats_);
  w.Counter("idle_repeats", idle_repeats_);
  w.Counter("refresh_requests", refresh_requests_);
  w.Gauge("zero_hertz", zero_hertz_ ? 1.0 : 0.0);
}

}  // namespace sm::video
