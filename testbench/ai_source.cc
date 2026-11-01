#include "ai_source.h"

#include <algorithm>
#include <cmath>

namespace sm::testbench {
namespace {

// `media` generated at `speed` x real time takes media / speed.
TimeDelta AtSpeed(TimeDelta media, double speed) {
  return TimeDelta::Micros(static_cast<int64_t>(std::llround(static_cast<double>(media.us()) / speed)));
}

}  // namespace

TimeDelta AiGenerationTime(const AiSourceConfig& config, TimeDelta media) {
  if (media <= TimeDelta::Zero()) return config.first_chunk;
  // b * (burst / speed + gap) + (media - b * burst) / speed = media / speed + b * gap.
  int64_t bursts_before = 0;
  if (config.burst > TimeDelta::Zero()) bursts_before = (media.us() - 1) / config.burst.us();
  return config.first_chunk + AtSpeed(media, config.speed) + config.gap * bursts_before;
}

AiMediaSource::AiMediaSource(const AiSourceConfig& config) : config_(config) {}

void AiMediaSource::StartTurn(Timestamp now, TimeDelta length) {
  turn_start_ = now;
  frames_.clear();
  next_ = 0;
  generated_ = 0;
  ++stats_.turns;
  stats_.first_frame_delay.reset();
  const TimeDelta frame = config_.frame;
  if (length <= TimeDelta::Zero() || frame <= TimeDelta::Zero()) return;
  const int64_t count = (length.us() + frame.us() - 1) / frame.us();
  frames_.reserve(static_cast<size_t>(count));
  for (int64_t k = 0; k < count; ++k) {
    AiFrame f;
    f.index = k;
    f.media_offset = frame * k;
    const TimeDelta end = std::min(frame * (k + 1), length);
    f.length = end - f.media_offset;
    f.generated = now + AiGenerationTime(config_, end);
    if (k == 0) {
      f.send = f.generated;
    } else {
      const AiFrame& prev = frames_.back();
      const Timestamp on_time = prev.send + prev.length;
      f.late = f.generated > on_time;
      f.send = f.late ? f.generated : on_time;
    }
    frames_.push_back(f);
  }
}

void AiMediaSource::Cancel(Timestamp now) {
  (void)now;
  if (next_ >= frames_.size()) return;
  ++stats_.cancels;
  for (size_t i = next_; i < frames_.size(); ++i) stats_.cancelled += frames_[i].length;
  frames_.resize(next_);
  if (generated_ > next_) generated_ = next_;
}

std::vector<AiFrame> AiMediaSource::Poll(Timestamp now) {
  // The queue is deepest just as a frame is generated, so its maximum is read at each generation time.
  while (generated_ < frames_.size() && frames_[generated_].generated <= now) {
    ++stats_.frames_generated;
    stats_.max_queue_ahead = std::max(stats_.max_queue_ahead, QueueAhead(frames_[generated_].generated));
    ++generated_;
  }
  std::vector<AiFrame> out;
  while (next_ < frames_.size() && frames_[next_].send <= now) {
    const AiFrame& f = frames_[next_];
    if (f.index == 0) stats_.first_frame_delay = f.send - turn_start_;
    ++stats_.frames_sent;
    if (f.late) ++stats_.underruns;
    out.push_back(f);
    ++next_;
  }
  return out;
}

TimeDelta AiMediaSource::QueueAhead(Timestamp now) const {
  TimeDelta ahead = TimeDelta::Zero();
  for (const AiFrame& f : frames_) {
    if (f.generated > now) break;  // Generation times only increase.
    if (f.send > now) ahead += f.length;
  }
  return ahead;
}

std::optional<Timestamp> AiMediaSource::NextSendTime() const {
  if (next_ >= frames_.size()) return std::nullopt;
  return frames_[next_].send;
}

}  // namespace sm::testbench
