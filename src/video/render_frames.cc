#include "sm/video/render_frames.h"

namespace sm::video {
namespace {
// video/render/video_render_frames.cc.
constexpr int64_t kOldRenderTimestampMs = 500;
constexpr int64_t kFutureRenderTimestampMs = 10000;
constexpr int64_t kMinRenderDelayMs = 10;
constexpr int64_t kMaxRenderDelayMs = 500;

int64_t EnsureValidRenderDelay(int64_t render_delay_ms) {
  return (render_delay_ms < kMinRenderDelayMs || render_delay_ms > kMaxRenderDelayMs) ? kMinRenderDelayMs
                                                                                     : render_delay_ms;
}
}  // namespace

RenderFrames::RenderFrames(RenderFramesConfig cfg)
    : cfg_(cfg), render_delay_ms_(EnsureValidRenderDelay(cfg.render_delay.ms())) {}

int RenderFrames::AddFrame(int64_t id, int64_t render_time_ms, int64_t now_ms) {
  // Drop old frames only when there are other frames in the queue, otherwise a
  // really slow system never renders any frames.
  if (!queue_.empty() && render_time_ms + kOldRenderTimestampMs < now_ms) {
    ++stats_.dropped_old;
    return -1;
  }
  if (render_time_ms > now_ms + kFutureRenderTimestampMs) {
    ++stats_.dropped_future;
    return -1;
  }
  if (render_time_ms < last_render_time_ms_) {
    ++stats_.dropped_out_of_order;
    return -1;
  }
  last_render_time_ms_ = render_time_ms;
  queue_.push_back({id, render_time_ms});
  return static_cast<int>(queue_.size());
}

int64_t RenderFrames::TimeToNextFrameReleaseMs(int64_t now_ms) const {
  if (queue_.empty()) return 200;  // kEventMaxWaitTimeMs.
  const int64_t t = queue_.front().render_time_ms - render_delay_ms_ - now_ms;
  return t < 0 ? 0 : t;
}

void RenderFrames::OnFrame(int64_t id, Timestamp render_time, Timestamp now) {
  ++stats_.frames_in;
  const int added = AddFrame(id, render_time.ms(), now.ms());
  if (added < 0) {
    SM_EVENT(cfg_.events, now, "render_frames", "drop", (render_time - now).ms_f(), id);
    out_.push_back({id, false});
    return;
  }
  if (added == 1) Dequeue(now);
}

void RenderFrames::Process(Timestamp now) {
  if (now >= next_dequeue_) Dequeue(now);
}

void RenderFrames::Dequeue(Timestamp now) {
  next_dequeue_ = Timestamp::PlusInfinity();
  const int64_t now_ms = now.ms();
  // VideoRenderFrames::FrameToRender: the newest frame that can be released.
  bool have = false;
  int64_t render_id = 0;
  while (!queue_.empty() && TimeToNextFrameReleaseMs(now_ms) <= 0) {
    if (have) {
      ++stats_.dropped_skipped;
      SM_EVENT(cfg_.events, now, "render_frames", "skip", 0.0, render_id);
      out_.push_back({render_id, false});
    }
    have = true;
    render_id = queue_.front().id;
    queue_.pop_front();
  }
  if (have) {
    ++stats_.rendered;
    out_.push_back({render_id, true});
  }
  if (!queue_.empty()) next_dequeue_ = now + TimeDelta::Millis(TimeToNextFrameReleaseMs(now_ms));
}

std::vector<RenderDecision> RenderFrames::TakeDecisions() {
  std::vector<RenderDecision> out;
  out.swap(out_);
  return out;
}

void RenderFrames::ExportMetrics(MetricsWriter& w) const {
  w.Counter("frames_in", stats_.frames_in);
  w.Counter("rendered", stats_.rendered);
  w.Counter("dropped_old", stats_.dropped_old);
  w.Counter("dropped_future", stats_.dropped_future);
  w.Counter("dropped_out_of_order", stats_.dropped_out_of_order);
  w.Counter("dropped_skipped", stats_.dropped_skipped);
  w.Gauge("queued", static_cast<double>(queue_.size()));
}

}  // namespace sm::video
