#include "sm/timing/audio_jitter_buffer.h"

#include <algorithm>

namespace sm::timing {

const char* ToString(AudioOperation op) {
  switch (op) {
    case AudioOperation::kSilence: return "silence";
    case AudioOperation::kNormal: return "normal";
    case AudioOperation::kAccelerate: return "accelerate";
    case AudioOperation::kPreemptiveExpand: return "preemptive_expand";
    case AudioOperation::kExpand: return "expand";
    case AudioOperation::kMerge: return "merge";
  }
  return "?";
}

namespace {
loss::PlcConfig PlcFor(int rate) {
  loss::PlcConfig c;
  c.sample_rate = rate;
  return c;
}
TimeStretchConfig StretchFor(int rate) {
  TimeStretchConfig c;
  c.sample_rate = rate;
  return c;
}
}  // namespace

AudioJitterBuffer::AudioJitterBuffer(AudioJitterBufferConfig cfg)
    : cfg_(cfg), delay_(cfg.delay), stretcher_(StretchFor(cfg.sample_rate)), plc_(PlcFor(cfg.sample_rate)) {
  delay_.SetPacketDurationMs(cfg_.packet_ms);
}

int AudioJitterBuffer::TargetDelayMs() const { return std::max(delay_.TargetDelayMs(), min_delay_ms_); }

int64_t AudioJitterBuffer::SpanSamples() const {
  if (queue_.empty()) return 0;
  const auto& last = *queue_.rbegin();
  return last.first + static_cast<int64_t>(last.second.pcm.size()) - next_ts_;
}

double AudioJitterBuffer::CurrentLevelMs() const {
  return static_cast<double>(static_cast<int64_t>(sync_.size()) + std::max<int64_t>(0, SpanSamples())) * 1000.0 /
         cfg_.sample_rate;
}

void AudioJitterBuffer::InsertPacket(uint32_t rtp_ts, std::vector<float> pcm, Timestamp arrival) {
  int64_t ts = unwrapper_.Unwrap(rtp_ts);
  ++stats_.packets_received;
  delay_.Update(rtp_ts, cfg_.sample_rate, arrival);
  if (started_ && ts < next_ts_) {
    ++stats_.packets_discarded_late;
    SM_EVENT(cfg_.events, arrival, "neteq", "late", static_cast<double>(next_ts_ - ts), ts);
    return;
  }
  if (queue_.count(ts)) {
    ++stats_.packets_duplicate;
    return;
  }
  if (!started_ && queue_.empty()) next_ts_ = ts;
  if (!started_) next_ts_ = std::min(next_ts_, ts);
  queue_.emplace(ts, Packet{std::move(pcm), arrival});
  if (queue_.size() > cfg_.max_packets) {
    // Far too much audio: drop everything but the newest few packets.
    while (queue_.size() > 3) queue_.erase(queue_.begin());
    next_ts_ = queue_.begin()->first;
    sync_.clear();
    ++stats_.buffer_flushes;
    SM_EVENT(cfg_.events, arrival, "neteq", "flush", 0, ts);
  }
}

AudioOperation AudioJitterBuffer::DecodeNext(Timestamp now) {
  auto it = queue_.begin();
  Packet pkt = std::move(it->second);
  queue_.erase(it);
  next_ts_ += static_cast<int64_t>(pkt.pcm.size());
  stats_.jitter_buffer_delay_ms.Add((now - pkt.arrival).ms_f() + sync_.size() * 1000.0 / cfg_.sample_rate);
  plc_.OnGoodFrame(pkt.pcm);  // Keeps PLC history; crossfades after an expand.
  AudioOperation op = AudioOperation::kNormal;
  if (expanding_) {
    expanding_ = false;
    expanded_samples_ = 0;
    op = AudioOperation::kMerge;
  }
  sync_.insert(sync_.end(), pkt.pcm.begin(), pkt.pcm.end());
  return op;
}

bool AudioJitterBuffer::NextPacketReady() const {
  return !queue_.empty() && queue_.begin()->first == next_ts_;
}

AudioOperation AudioJitterBuffer::MaybeStretch(Timestamp now) {
  double level = CurrentLevelMs();
  filtered_level_ms_ = 0.8 * filtered_level_ms_ + 0.2 * level;
  if (!cfg_.time_stretch) return AudioOperation::kNormal;
  const double target = TargetDelayMs();
  const double low = target * 0.75;
  const double high = std::max(target, low + cfg_.packet_ms);
  const bool accelerate = filtered_level_ms_ > high;
  const bool expand = filtered_level_ms_ < low;
  if (!accelerate && !expand) return AudioOperation::kNormal;
  // WSOLA needs 30 ms of decoded audio: decode ahead when packets allow.
  while (sync_.size() < stretcher_.min_input() && NextPacketReady()) DecodeNext(now);
  if (sync_.size() < stretcher_.min_input()) return AudioOperation::kNormal;
  size_t changed = accelerate ? stretcher_.Accelerate(sync_, scratch_) : stretcher_.PreemptiveExpand(sync_, scratch_);
  if (!changed) return AudioOperation::kNormal;
  sync_.swap(scratch_);
  double ms = changed * 1000.0 / cfg_.sample_rate;
  if (accelerate) {
    stats_.removed_samples_for_acceleration += changed;
    filtered_level_ms_ -= ms;
    return AudioOperation::kAccelerate;
  }
  stats_.inserted_samples_for_deceleration += changed;
  filtered_level_ms_ += ms;
  return AudioOperation::kPreemptiveExpand;
}

AudioOperation AudioJitterBuffer::GetAudio(std::span<float> out, Timestamp now) {
  const size_t need = out.size();
  if (!started_) {
    if (!queue_.empty() && CurrentLevelMs() >= TargetDelayMs()) {
      started_ = true;
    } else {
      std::fill(out.begin(), out.end(), 0.0f);
      ++stats_.ops[static_cast<int>(AudioOperation::kSilence)];
      return AudioOperation::kSilence;
    }
  }
  AudioOperation result = AudioOperation::kNormal;
  bool decoded = false;
  while (sync_.size() < need) {
    while (!queue_.empty() && queue_.begin()->first < next_ts_) {
      queue_.erase(queue_.begin());
      ++stats_.packets_discarded_late;
    }
    if (NextPacketReady()) {
      if (DecodeNext(now) == AudioOperation::kMerge) result = AudioOperation::kMerge;
      decoded = true;
      continue;
    }
    if (!queue_.empty() && expanded_samples_ >= queue_.begin()->first - next_ts_) {
      // The gap has been concealed for its full duration: the missing
      // packet(s) are treated as lost and playout jumps ahead.
      stats_.packets_lost += static_cast<uint64_t>((queue_.begin()->first - next_ts_) /
                                                   std::max<int64_t>(1, cfg_.sample_rate * cfg_.packet_ms / 1000));
      next_ts_ = queue_.begin()->first;
      continue;
    }
    // Expand: conceal one output block.
    if (!expanding_) {
      ++stats_.concealment_events;
      SM_EVENT(cfg_.events, now, "neteq", "expand", CurrentLevelMs(), next_ts_);
    }
    scratch_.assign(need, 0.0f);
    plc_.Conceal(scratch_);
    expanding_ = true;
    expanded_samples_ += static_cast<int64_t>(need);
    stats_.concealed_samples += need;
    sync_.insert(sync_.end(), scratch_.begin(), scratch_.end());
    result = AudioOperation::kExpand;
  }
  if (decoded && result == AudioOperation::kNormal) result = MaybeStretch(now);
  std::copy(sync_.begin(), sync_.begin() + static_cast<std::ptrdiff_t>(need), out.begin());
  sync_.erase(sync_.begin(), sync_.begin() + static_cast<std::ptrdiff_t>(need));
  stats_.total_samples_received += need;
  ++stats_.ops[static_cast<int>(result)];
  return result;
}

void AudioJitterBuffer::ExportMetrics(MetricsWriter& w) const {
  w.Counter("packets_received", stats_.packets_received);
  w.Counter("packets_discarded_late", stats_.packets_discarded_late);
  w.Counter("packets_duplicate", stats_.packets_duplicate);
  w.Counter("packets_lost", stats_.packets_lost);
  w.Counter("buffer_flushes", stats_.buffer_flushes);
  w.Counter("total_samples_received", stats_.total_samples_received);
  w.Counter("concealed_samples", stats_.concealed_samples);
  w.Counter("concealment_events", stats_.concealment_events);
  w.Counter("inserted_samples_for_deceleration", stats_.inserted_samples_for_deceleration);
  w.Counter("removed_samples_for_acceleration", stats_.removed_samples_for_acceleration);
  for (int i = 0; i < 6; ++i) {
    w.Counter(std::string("op_") + ToString(static_cast<AudioOperation>(i)), stats_.ops[i]);
  }
  w.Gauge("target_delay_ms", TargetDelayMs());
  w.Gauge("current_level_ms", CurrentLevelMs());
  w.Hist("jitter_buffer_delay_ms", stats_.jitter_buffer_delay_ms);
}

}  // namespace sm::timing
