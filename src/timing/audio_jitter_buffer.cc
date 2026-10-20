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
    : cfg_(cfg), delay_(cfg.delay), neteq_delay_(cfg.neteq_delay), stretcher_(StretchFor(cfg.sample_rate)), plc_(PlcFor(cfg.sample_rate)) {
  delay_.SetPacketDurationMs(cfg_.packet_ms);
  neteq_delay_.SetSampleRate(cfg_.sample_rate);
  decision_.SetSampleRate(cfg_.sample_rate);
}

int AudioJitterBuffer::TargetDelayMs() const {
  // DecisionLogic::TargetLevelMs applies the minimum delay itself.
  if (cfg_.version >= 2) return neteq_delay_.TargetLevelMs();
  return std::max(delay_.TargetDelayMs(), min_delay_ms_);
}

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
  if (cfg_.version >= 2) {
    // NetEqImpl::InsertPacketInternal: the packets that arrive before the
    // first GetAudio are dropped from the statistics by the SoftReset that
    // the new codec triggers there.
    if (neteq_reset_done_) {
      neteq_delay_.PacketArrived(rtp_ts, static_cast<int>(pcm.size()), cfg_.sample_rate, ticks_ * kMsPerTick);
    }
  } else {
    delay_.Update(rtp_ts, cfg_.sample_rate, arrival);
  }
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
  queue_.emplace(ts, Packet{std::move(pcm), arrival, ticks_});
  if (queue_.size() > cfg_.max_packets) {
    // Far too much audio: drop everything but the newest few packets.
    while (queue_.size() > 3) queue_.erase(queue_.begin());
    next_ts_ = queue_.begin()->first;
    sync_.clear();
    ++stats_.buffer_flushes;
    decision_.NotifyBufferFlush();
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
  ++ticks_;  // NetEqImpl::GetAudioInternal: tick_timer_->Increment().
  if (cfg_.version >= 2 && !neteq_reset_done_ && stats_.packets_received > 0) {
    neteq_delay_.SoftReset();
    decision_.SoftReset(ticks_);
    neteq_reset_done_ = true;
  }
  if (cfg_.version >= 2) return GetAudioV2(out, now);
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

int64_t AudioJitterBuffer::PacketSpanSamples(bool count_waiting_time) const {
  if (queue_.empty()) return 0;
  const auto& front = *queue_.begin();
  const auto& back = *queue_.rbegin();
  int64_t span = back.first - front.first;
  if (count_waiting_time) {
    span += (ticks_ - back.second.insert_tick) * kMsPerTick * (cfg_.sample_rate / 1000);
  } else {
    span += static_cast<int64_t>(back.second.pcm.size());
  }
  return span;
}

AudioOperation AudioJitterBuffer::GetAudioV2(std::span<float> out, Timestamp now) {
  const int64_t need = static_cast<int64_t>(out.size());
  const int64_t samples_10_ms = cfg_.sample_rate / 100;
  const int64_t samples_20_ms = 2 * samples_10_ms;
  const int64_t samples_30_ms = 3 * samples_10_ms;
  if (!started_) {
    if (queue_.empty()) {
      std::fill(out.begin(), out.end(), 0.0f);
      ++stats_.ops[static_cast<int>(AudioOperation::kSilence)];
      return AudioOperation::kSilence;
    }
    // The first packet (NetEqImpl's new_codec_) starts playout at once with a
    // normal decode: the target is reached by preemptive expand, not by waiting.
    started_ = true;
    next_ts_ = queue_.begin()->first;
  }
  // PacketBuffer::DiscardOldPackets.
  while (!queue_.empty() && queue_.begin()->first < next_ts_) {
    queue_.erase(queue_.begin());
    ++stats_.packets_discarded_late;
  }
  const int64_t samples_left = static_cast<int64_t>(sync_.size());
  if (IsTimestretch(last_mode_)) decision_.AddSampleMemory(-static_cast<int>(samples_left + need));

  NetEqStatus st;
  st.last_mode = last_mode_;
  st.target_timestamp = next_ts_;
  if (!queue_.empty()) st.next_packet_timestamp = queue_.begin()->first;
  st.span_samples = PacketSpanSamples(false);
  st.span_samples_wait_time = PacketSpanSamples(true);
  st.generated_noise_samples = noise_start_tick_ ? (ticks_ - *noise_start_tick_) * need : 0;
  st.sync_buffer_samples = samples_left;
  st.expand_mutefactor = static_cast<int>(plc_.gain() * 16384.0f);
  st.target_level_ms = neteq_delay_.TargetLevelMs();
  st.playout_delay_ms = neteq_delay_.GetDelayMs(static_cast<uint32_t>(next_ts_ - samples_left), ticks_ * kMsPerTick);
  st.max_delay_ms = neteq_delay_.GetMaxDelayMs();
  NetEqOperation op = decision_.GetDecision(st, ticks_);
  if (op == NetEqOperation::kUndefined) op = NetEqOperation::kNormal;  // Old packets were discarded above.
  // Without fast accelerate (NetEq::Config's default) it is a plain accelerate.
  if (op == NetEqOperation::kFastAccelerate) op = NetEqOperation::kAccelerate;
  if (!cfg_.time_stretch && (op == NetEqOperation::kAccelerate || op == NetEqOperation::kPreemptiveExpand)) {
    op = NetEqOperation::kNormal;
  }

  // NetEqImpl::GetDecision: what to extract from the packet buffer.
  bool extract = true;
  int64_t required = need;
  if (samples_left >= need && op != NetEqOperation::kMerge && op != NetEqOperation::kAccelerate &&
      op != NetEqOperation::kPreemptiveExpand) {
    op = NetEqOperation::kNormal;  // Enough decoded audio already.
    extract = false;
  } else {
    switch (op) {
      case NetEqOperation::kExpand:
        extract = false;
        break;
      case NetEqOperation::kAccelerate:
        if (samples_left >= samples_30_ms) {
          decision_.set_sample_memory(static_cast<int>(samples_left));
          decision_.set_prev_time_scale(true);
          extract = false;
        } else if (samples_left < samples_20_ms) {
          // Build up 20 ms of decoded audio first (frames are shorter than 30 ms).
          required = 2 * need;
          op = NetEqOperation::kNormal;
        }
        break;
      case NetEqOperation::kPreemptiveExpand:
        if (samples_left >= samples_30_ms) {
          decision_.set_sample_memory(static_cast<int>(samples_left));
          decision_.set_prev_time_scale(true);
          extract = false;
        } else if (samples_left < samples_20_ms) {
          required = 2 * need;
        }
        break;
      case NetEqOperation::kMerge:
        required = std::max(samples_10_ms, need);  // Merge::RequiredFutureSamples.
        break;
      default:
        break;
    }
  }
  int64_t extracted = 0;
  if (extract && !queue_.empty()) {
    // The sync buffer's end timestamp jumps to the packet (a merge after an
    // expand that covered the gap).
    const int64_t jump = queue_.begin()->first - next_ts_;
    if (jump > 0) {
      stats_.packets_lost += static_cast<uint64_t>(jump / std::max<int64_t>(1, cfg_.sample_rate * cfg_.packet_ms / 1000));
      SM_EVENT(cfg_.events, now, "neteq", "merge_jump", static_cast<double>(jump), next_ts_);
      next_ts_ = queue_.begin()->first;
    }
    // NetEqImpl::ExtractPackets: consecutive packets until `required` samples.
    while (extracted < required && NextPacketReady()) {
      const int64_t before = static_cast<int64_t>(sync_.size());
      DecodeNext(now);
      extracted += static_cast<int64_t>(sync_.size()) - before;
    }
  }
  if (op == NetEqOperation::kAccelerate || op == NetEqOperation::kPreemptiveExpand) {
    if (extract) {
      decision_.set_sample_memory(static_cast<int>(samples_left + extracted));
      decision_.set_prev_time_scale(true);
    }
    if (op == NetEqOperation::kAccelerate && extracted + samples_left < samples_30_ms) op = NetEqOperation::kNormal;
  }

  AudioOperation result = AudioOperation::kNormal;
  switch (op) {
    case NetEqOperation::kExpand: {
      // NetEqImpl::DoExpand: conceal until the sync buffer covers the output.
      if (last_mode_ != NetEqMode::kExpand) {
        ++stats_.concealment_events;
        SM_EVENT(cfg_.events, now, "neteq", "expand", CurrentLevelMs(), next_ts_);
      }
      scratch_.assign(static_cast<size_t>(need - samples_left), 0.0f);
      plc_.Conceal(scratch_);
      expanding_ = true;
      stats_.concealed_samples += scratch_.size();
      sync_.insert(sync_.end(), scratch_.begin(), scratch_.end());
      last_mode_ = NetEqMode::kExpand;
      if (!noise_start_tick_) noise_start_tick_ = ticks_;
      result = AudioOperation::kExpand;
      break;
    }
    case NetEqOperation::kMerge:
      last_mode_ = NetEqMode::kMerge;
      result = AudioOperation::kMerge;
      break;
    case NetEqOperation::kAccelerate:
    case NetEqOperation::kPreemptiveExpand: {
      const bool accelerate = op == NetEqOperation::kAccelerate;
      size_t changed = 0;
      // Accelerate and PreemptiveExpand work on 30 ms of audio.
      const int64_t have = static_cast<int64_t>(sync_.size());
      if (have >= samples_30_ms) {
        changed = accelerate ? stretcher_.Accelerate(sync_, scratch_) : stretcher_.PreemptiveExpand(sync_, scratch_);
        if (changed) sync_.swap(scratch_);
      } else if (!accelerate && static_cast<int64_t>(played_.size()) >= samples_30_ms - have) {
        // NetEqImpl::DoPreemptiveExpand borrows the missing samples from the
        // sync buffer's played part, and inserts the period after them.
        const size_t old = static_cast<size_t>(samples_30_ms - have);
        std::vector<float> in(played_.end() - static_cast<std::ptrdiff_t>(old), played_.end());
        in.insert(in.end(), sync_.begin(), sync_.end());
        changed = stretcher_.PreemptiveExpand(in, scratch_, old);
        if (changed) sync_.assign(scratch_.begin() + static_cast<std::ptrdiff_t>(old), scratch_.end());
      }
      if (accelerate) {
        stats_.removed_samples_for_acceleration += changed;
        last_mode_ = changed ? NetEqMode::kAccelerateSuccess : NetEqMode::kAccelerateFail;
        if (changed) result = AudioOperation::kAccelerate;
      } else {
        stats_.inserted_samples_for_deceleration += changed;
        last_mode_ = changed ? NetEqMode::kPreemptiveExpandSuccess : NetEqMode::kPreemptiveExpandFail;
        if (changed) result = AudioOperation::kPreemptiveExpand;
      }
      expanding_ = false;
      break;
    }
    default:
      // NetEqImpl::DoNormal: the mode changes only when something was decoded.
      if (extracted > 0) last_mode_ = NetEqMode::kNormal;
      break;
  }
  if (last_mode_ != NetEqMode::kExpand) {
    noise_start_tick_.reset();
    expanding_ = false;
    expanded_samples_ = 0;
  }
  if (static_cast<int64_t>(sync_.size()) < need) {
    // Not reached with whole packets of 10 ms or more; conceal the rest.
    scratch_.assign(static_cast<size_t>(need) - sync_.size(), 0.0f);
    plc_.Conceal(scratch_);
    stats_.concealed_samples += scratch_.size();
    sync_.insert(sync_.end(), scratch_.begin(), scratch_.end());
  }
  std::copy(sync_.begin(), sync_.begin() + static_cast<std::ptrdiff_t>(need), out.begin());
  sync_.erase(sync_.begin(), sync_.begin() + static_cast<std::ptrdiff_t>(need));
  played_.insert(played_.end(), out.begin(), out.end());
  if (static_cast<int64_t>(played_.size()) > samples_30_ms) {
    played_.erase(played_.begin(), played_.end() - static_cast<std::ptrdiff_t>(samples_30_ms));
  }
  stats_.total_samples_received += static_cast<uint64_t>(need);
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
  if (cfg_.version >= 2) w.Gauge("filtered_buffer_level_samples", decision_.GetFilteredBufferLevel());
  w.Hist("jitter_buffer_delay_ms", stats_.jitter_buffer_delay_ms);
}

}  // namespace sm::timing
