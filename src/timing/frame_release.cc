#include "sm/timing/frame_release.h"

#include <algorithm>
#include <cstdlib>

namespace sm::timing {
namespace {
constexpr TimeDelta kMaxAllowedFrameDelay = TimeDelta::Millis(5);       // FrameDecodeTiming.
constexpr TimeDelta kZeroPlayoutDelayMinPacing = TimeDelta::Millis(8);  // FrameDecodeTiming.
constexpr size_t kZeroPlayoutDelayMaxDecodeQueueSize = 8;               // video_stream_buffer_controller.cc.
constexpr TimeDelta kMaxVideoDelay = TimeDelta::Seconds(10);            // frame_helpers.cc.
}  // namespace

FrameReleaseController::FrameReleaseController(VcmTiming* timing, FrameReleaseConfig cfg)
    : timing_(timing), cfg_(cfg) {}

void FrameReleaseController::Insert(const ReleaseFrame& f) {
  ++stats_.frames_in;
  if (!queue_.empty() && f.id <= queue_.back().id) return;
  timing_->OnCompleteFrame(f.rtp_timestamp, f.receive_time, f.retransmitted);
  queue_.push_back(f);
}

int64_t FrameReleaseController::LastDecodableId() const {
  // FrameBuffer::FindNextAndLastDecodableTemporalUnit: a delta frame needs the
  // frame before it decoded, so past the front only a keyframe is decodable.
  for (auto it = queue_.rbegin(); it != queue_.rend(); ++it) {
    if (it->keyframe) return it->id;
  }
  return queue_.front().id;
}

TimeDelta FrameReleaseController::MaxWaitingTime(Timestamp render_time, Timestamp now,
                                                 bool too_many_frames_queued) const {
  if ((render_time.us() == 0) && timing_->min_playout_delay().IsZero() &&
      timing_->max_playout_delay() > TimeDelta::Zero()) {
    if (too_many_frames_queued) return TimeDelta::Zero();
    const Timestamp earliest_next_decode_start_time = last_decode_scheduled_ + kZeroPlayoutDelayMinPacing;
    return now >= earliest_next_decode_start_time ? TimeDelta::Zero() : earliest_next_decode_start_time - now;
  }
  return render_time - now - timing_->estimated_max_decode_time() - timing_->render_delay();
}

void FrameReleaseController::Drop(Timestamp now, bool fast_forward, std::vector<ReleaseAction>* out) {
  const ReleaseFrame& f = queue_.front();
  if (fast_forward) {
    ++stats_.fast_forwarded;
    SM_EVENT(cfg_.events, now, "frame_release", "fast_forward", static_cast<double>(queue_.size()), f.id);
  } else {
    ++stats_.dropped_for_keyframe;
  }
  out->push_back({f.id, false, now, TimeDelta::Zero()});
  queue_.pop_front();
}

void FrameReleaseController::Release(Timestamp render_time, Timestamp now, TimeDelta decode_time,
                                     std::vector<ReleaseAction>* out) {
  const ReleaseFrame f = queue_.front();
  queue_.pop_front();
  scheduled_.reset();
  if (f.keyframe) keyframe_required_ = false;
  // VideoStreamBufferController::OnFrameReady.
  const bool bad_render_timing =
      !(render_time.us() == 0) && (render_time < Timestamp::Micros(0) ||
                                TimeDelta::Micros(std::llabs((render_time - now).us())) > kMaxVideoDelay);
  if (bad_render_timing || timing_->TargetDelay() > kMaxVideoDelay) {
    ++stats_.timing_resets;
    SM_EVENT(cfg_.events, now, "frame_release", "timing_reset", (render_time - now).ms_f(), f.id);
    timing_->Reset(now);
    render_time = timing_->RenderTime(f.rtp_timestamp, now);
  }
  timing_->OnDecodableTemporalUnit(f.rtp_timestamp, f.size, f.receive_time, f.retransmitted, now);
  if (!f.retransmitted) timing_->UpdateCurrentDelay(render_time, now);
  last_decode_scheduled_ = now;
  timeout_ = now + cfg_.max_wait_for_frame;  // OnEncodedFrameReleased.
  const TimeDelta late = now - (render_time - timing_->estimated_max_decode_time() - timing_->render_delay());
  ++stats_.released;
  stats_.lateness_ms.Add(std::max(0.0, late.ms_f()));
  out->push_back({f.id, true, render_time, late});
  // VideoReceiveStream2 reports the decoder's time after decoding.
  timing_->UpdateDecodeTimeEstimate(decode_time, now);
}

std::vector<ReleaseAction> FrameReleaseController::Process(Timestamp now, TimeDelta decode_time) {
  std::vector<ReleaseAction> out;
  // VideoReceiveStreamTimeoutTracker: armed at the first StartNextDecode and
  // re-armed when it fires (the receiver's time-out handling restarts it).
  if (!timeout_ || now >= *timeout_) {
    timeout_ = now + (keyframe_required_ ? cfg_.max_wait_for_keyframe : cfg_.max_wait_for_frame);
  }
  while (!queue_.empty()) {
    if (keyframe_required_) {
      // ForceKeyFrameReleaseImmediately.
      while (!queue_.empty() && !queue_.front().keyframe) Drop(now, false, &out);
      if (queue_.empty()) break;
      Release(timing_->RenderTime(queue_.front().rtp_timestamp, now), now, decode_time, &out);
      continue;
    }
    if (scheduled_ && scheduled_->id == queue_.front().id) {
      if (now < scheduled_->latest_decode_time) break;
      Release(scheduled_->render_time, now, decode_time, &out);
      continue;
    }
    // MaybeScheduleFrameForRelease + FrameDecodeTiming::OnFrameBufferUpdated.
    const ReleaseFrame& next = queue_.front();
    const Timestamp render_time = timing_->RenderTime(next.rtp_timestamp, now);
    TimeDelta max_wait =
        MaxWaitingTime(render_time, now, queue_.size() > kZeroPlayoutDelayMaxDecodeQueueSize);
    if (max_wait <= -kMaxAllowedFrameDelay && next.id != LastDecodableId()) {
      // DropNextDecodableTemporalUnit; the delta frames after it are not
      // decodable until the next keyframe, which becomes the next decodable unit.
      Drop(now, true, &out);
      while (!queue_.empty() && !queue_.front().keyframe) Drop(now, true, &out);
      continue;
    }
    TimeDelta max_wait_for_frame = std::max(*timeout_ - now, TimeDelta::Zero());
    max_wait_for_frame = std::max(max_wait_for_frame - TimeDelta::Millis(1), TimeDelta::Zero());
    max_wait = Clamp(max_wait, TimeDelta::Zero(), max_wait_for_frame);
    scheduled_ = Schedule{next.id, now + max_wait, render_time};
  }
  return out;
}

Timestamp FrameReleaseController::NextWakeup() const {
  if (queue_.empty()) return Timestamp::PlusInfinity();
  if (scheduled_ && scheduled_->id == queue_.front().id) return scheduled_->latest_decode_time;
  return Timestamp::MinusInfinity();
}

void FrameReleaseController::ExportMetrics(MetricsWriter& w) const {
  w.Counter("frames_in", stats_.frames_in);
  w.Counter("released", stats_.released);
  w.Counter("fast_forwarded", stats_.fast_forwarded);
  w.Counter("dropped_for_keyframe", stats_.dropped_for_keyframe);
  w.Counter("timing_resets", stats_.timing_resets);
  w.Gauge("queued", static_cast<double>(queue_.size()));
  w.Hist("lateness_ms", stats_.lateness_ms);
}

}  // namespace sm::timing
