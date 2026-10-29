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
    : cfg_(cfg),
      delay_(cfg.delay),
      neteq_delay_(cfg.neteq_delay),
      stretcher_(StretchFor(cfg.sample_rate)),
      plc_(PlcFor(cfg.sample_rate)),
      expand_(cfg.sample_rate),
      merge_(cfg.sample_rate),
      time_stretch_(cfg.sample_rate) {
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

int AudioJitterBuffer::FilteredCurrentDelayMs() const {
  if (cfg_.version < 2) return static_cast<int>(filtered_level_ms_);
  const int64_t delay_samples = decision_.GetFilteredBufferLevel() + static_cast<int64_t>(sync_.size());
  return static_cast<int>(delay_samples / (cfg_.sample_rate / 1000));
}

double AudioJitterBuffer::CurrentLevelMs() const {
  return static_cast<double>(static_cast<int64_t>(sync_.size()) + std::max<int64_t>(0, SpanSamples())) * 1000.0 /
         cfg_.sample_rate;
}

void AudioJitterBuffer::InsertPacket(uint32_t rtp_ts, std::vector<float> pcm, Timestamp arrival,
                                     AudioPacketPriority priority) {
  if (cfg_.version >= 2 && !NetEqSupportsSampleRate(cfg_.sample_rate)) {
    // As libwebrtc, where no decoder exists for such a rate (#256): NetEq's
    // expand, merge and time stretch assume one of the four rates.
    ++stats_.packets_discarded_unsupported_rate;
    SM_EVENT(cfg_.events, arrival, "neteq", "unsupported_rate", cfg_.sample_rate, rtp_ts);
    return;
  }
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
    if (priority.codec_level > 0) return;
    ++stats_.packets_discarded_late;
    SM_EVENT(cfg_.events, arrival, "neteq", "late", static_cast<double>(next_ts_ - ts), ts);
    return;
  }
  if (auto it = queue_.find(ts); it != queue_.end()) {
    ++stats_.packets_duplicate;
    // PacketBuffer::InsertPacket: one packet per timestamp, the higher priority.
    if (cfg_.version >= 2 && priority < it->second.priority) it->second = Packet{std::move(pcm), arrival, ticks_, priority};
    return;
  }
  if (cfg_.version >= 2 && queue_.size() >= static_cast<size_t>(cfg_.neteq_delay.max_packets_in_buffer)) {
    // PacketBuffer::InsertPacket (packet_buffer.cc): a full buffer (NetEq::
    // Config::max_packets_in_buffer, the capacity DelayConstraints assumes)
    // is flushed completely before the new packet goes in (issue #331).
    queue_.clear();
    next_ts_ = ts;
    sync_.assign(expand_.overlap_length(), 0.0f);  // NetEqImpl::FlushBuffers.
    ++stats_.buffer_flushes;
    decision_.NotifyBufferFlush();
    SM_EVENT(cfg_.events, arrival, "neteq", "flush", 0, ts);
  }
  if (!started_ && queue_.empty()) next_ts_ = ts;
  if (!started_) next_ts_ = std::min(next_ts_, ts);
  queue_.emplace(ts, Packet{std::move(pcm), arrival, ticks_, priority});
  if (cfg_.version < 2 && queue_.size() > cfg_.max_packets) {
    // Far too much audio: drop everything but the newest few packets.
    while (queue_.size() > 3) queue_.erase(queue_.begin());
    next_ts_ = queue_.begin()->first;
    sync_.clear();
    ++stats_.buffer_flushes;
    decision_.NotifyBufferFlush();
    SM_EVENT(cfg_.events, arrival, "neteq", "flush", 0, ts);
  }
}

AudioOperation AudioJitterBuffer::DecodeNext(Timestamp now, std::vector<float>* dst) {
  auto it = queue_.begin();
  const int64_t ts = it->first;
  Packet pkt = std::move(it->second);
  queue_.erase(it);
  // NetEqImpl::DecodeLoop: the packet's RtpPacketInfo, which only the primary payload carries
  // (neteq_impl.cc:316-318, 1352-1354).
  if (pkt.priority.codec_level == 0 && pkt.priority.red_level == 0) {
    last_decoded_timestamps_.push_back(static_cast<uint32_t>(ts));
  }
  next_ts_ += static_cast<int64_t>(pkt.pcm.size());
  const size_t ahead = sync_.size() + (dst ? dst->size() : 0);
  stats_.jitter_buffer_delay_ms.Add((now - pkt.arrival).ms_f() + ahead * 1000.0 / cfg_.sample_rate);
  const uint64_t samples = pkt.pcm.size();
  stats_.jitter_buffer_delay_ms_samples += static_cast<uint64_t>((ticks_ - pkt.insert_tick) * kMsPerTick) * samples;
  stats_.jitter_buffer_emitted_count += samples;
  if (cfg_.version < 2) plc_.OnGoodFrame(pkt.pcm);  // Keeps PLC history; crossfades after an expand.
  AudioOperation op = AudioOperation::kNormal;
  if (expanding_) {
    expanding_ = false;
    expanded_samples_ = 0;
    op = AudioOperation::kMerge;
  }
  std::vector<float>& to = dst ? *dst : sync_;
  to.insert(to.end(), pkt.pcm.begin(), pkt.pcm.end());
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
  ++ticks_;  // NetEqImpl::GetAudioInternal: tick_timer_->Increment().
  last_decoded_timestamps_.clear();  // NetEqImpl::GetAudioInternal (neteq_impl.cc:674).
  if (cfg_.version >= 2 && !neteq_reset_done_ && stats_.packets_received > 0) {
    neteq_delay_.SoftReset(ticks_ * kMsPerTick);
    decision_.SoftReset(ticks_);
    neteq_reset_done_ = true;
  }
  const AudioOperation op = cfg_.version >= 2 ? GetAudioV2(out, now) : GetAudioV1(out, now);
  // NetEqImpl::GetAudioInternal: after an expand the playout timestamp moves on
  // by the output (dead reckoning); otherwise it is the sync buffer's end minus
  // its future length, never moved backwards.
  // Not after a sample underrun (kSilence once started), which returns first.
  if (started_ && op != AudioOperation::kSilence) {
    const int64_t played = next_ts_ - static_cast<int64_t>(sync_.size());
    if (op == AudioOperation::kExpand && playout_ts_) {
      *playout_ts_ += static_cast<int64_t>(out.size());
    } else if (!playout_ts_ || played > *playout_ts_) {
      playout_ts_ = played;
    }
  }
  return op;
}

AudioOperation AudioJitterBuffer::GetAudioV1(std::span<float> out, Timestamp now) {
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
    // NetEqImpl::FlushBuffers: Expand's overlap of zeros as lookahead.
    sync_.assign(expand_.overlap_length(), 0.0f);
  }
  // NetEqImpl::GetAudioInternal (neteq_impl.cc:682-698): the expand has faded
  // to silence and the packet buffer is empty, so the frame is muted without
  // a decision or an expand. Nothing in the sync buffer or the decision state
  // moves; the playout timestamp moves on by dead reckoning (GetAudio, as
  // after an expand), the noise stopwatch keeps running, and the samples are
  // expanded noise that starts no concealment event. NetEqController::
  // NotifyMutedState is a no-op for DecisionLogic, which does not override it.
  if (cfg_.enable_muted_state && expand_.Muted() && queue_.empty()) {
    std::fill(out.begin(), out.end(), 0.0f);
    if (!muted_) {
      SM_EVENT(cfg_.events, now, "neteq", "muted", CurrentLevelMs(), next_ts_);
    }
    muted_ = true;
    ++stats_.muted_frames;
    AddConcealedSamples(need);
    stats_.total_samples_received += static_cast<uint64_t>(need);
    ++stats_.ops[static_cast<int>(AudioOperation::kExpand)];
    return AudioOperation::kExpand;
  }
  muted_ = false;
  // PacketBuffer::DiscardOldPackets.
  while (!queue_.empty() && queue_.begin()->first < next_ts_) {
    queue_.erase(queue_.begin());
    ++stats_.packets_discarded_late;
  }
  // The sync buffer always keeps Expand's overlap as lookahead.
  const int64_t future = static_cast<int64_t>(sync_.size());
  const int64_t samples_left = future - static_cast<int64_t>(expand_.overlap_length());
  if (IsTimestretch(last_mode_)) decision_.AddSampleMemory(-static_cast<int>(samples_left + need));

  NetEqStatus st;
  st.last_mode = last_mode_;
  st.target_timestamp = next_ts_;
  if (!queue_.empty()) st.next_packet_timestamp = queue_.begin()->first;
  st.span_samples = PacketSpanSamples(false);
  st.span_samples_wait_time = PacketSpanSamples(true);
  st.generated_noise_samples = noise_start_tick_ ? (ticks_ - *noise_start_tick_) * need : 0;
  st.sync_buffer_samples = future;
  st.expand_mutefactor = expand_.MuteFactor();
  st.target_level_ms = neteq_delay_.TargetLevelMs();
  st.playout_delay_ms = neteq_delay_.GetDelayMs(static_cast<uint32_t>(next_ts_ - future), ticks_ * kMsPerTick);
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
  decoded_.clear();
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
      DecodeNext(now, &decoded_);
      extracted = static_cast<int64_t>(decoded_.size());
    }
  }
  // NetEqImpl::DoNormal / DoMerge: the first decode after an expand.
  if (!decoded_.empty() && last_mode_ == NetEqMode::kExpand && op == NetEqOperation::kMerge) {
    MergeAfterExpand(decoded_, now);
  } else if (!decoded_.empty() && last_mode_ == NetEqMode::kExpand && op == NetEqOperation::kNormal) {
    JoinAfterExpand(decoded_);
  }
  sync_.insert(sync_.end(), decoded_.begin(), decoded_.end());
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
      // NetEqImpl::DoExpand: whole periods until the sync buffer holds the
      // output plus Expand's overlap.
      if (last_mode_ != NetEqMode::kExpand) {
        ++stats_.concealment_events;
        SM_EVENT(cfg_.events, now, "neteq", "expand", CurrentLevelMs(), next_ts_);
      }
      while (sync_.size() < static_cast<size_t>(need) + expand_.overlap_length()) {
        expanded_.clear();
        AddConcealedSamples(static_cast<int64_t>(ExpandOnce(expanded_)));
        sync_.insert(sync_.end(), expanded_.begin(), expanded_.end());
      }
      expanding_ = true;
      last_mode_ = NetEqMode::kExpand;
      if (!noise_start_tick_) noise_start_tick_ = ticks_;
      result = AudioOperation::kExpand;
      break;
    }
    case NetEqOperation::kMerge:
      last_mode_ = NetEqMode::kMerge;
      expand_.Reset();
      result = AudioOperation::kMerge;
      break;
    case NetEqOperation::kAccelerate:
    case NetEqOperation::kPreemptiveExpand: {
      const bool accelerate = op == NetEqOperation::kAccelerate;
      // NetEqImpl::DoAccelerate / DoPreemptiveExpand: the decoded audio, or
      // 30 ms with the missing samples borrowed from the end of the sync
      // buffer, its future part first and then played samples (which
      // preemptive expand leaves unchanged; accelerate never reaches them,
      // as it needs 30 ms past the overlap).
      const size_t decoded = static_cast<size_t>(extracted);
      const size_t input_length = std::max(decoded, static_cast<size_t>(samples_30_ms));
      const size_t borrowed = input_length - decoded;
      const size_t future_before = sync_.size() - decoded;
      const size_t old = borrowed > future_before ? borrowed - future_before : 0;
      // NetEq's sync buffer starts out as zeros.
      const size_t real = played_.size() + sync_.size();
      if (real < input_length) played_.insert(played_.begin(), input_length - real, 0.0f);
      const size_t from_played = input_length > sync_.size() ? input_length - sync_.size() : 0;
      const size_t from_sync = input_length - from_played;
      sync_scratch_.assign(played_.end() - static_cast<std::ptrdiff_t>(from_played), played_.end());
      sync_scratch_.insert(sync_scratch_.end(), sync_.end() - static_cast<std::ptrdiff_t>(from_sync), sync_.end());
      scratch_.clear();
      size_t changed = 0;
      const NetEqTimeStretch::Result r =
          accelerate ? time_stretch_.Accelerate(sync_scratch_, false, expand_.background_noise(), scratch_, &changed)
                     : time_stretch_.PreemptiveExpand(sync_scratch_, old, expand_.overlap_length(),
                                                      expand_.background_noise(), scratch_, &changed);
      // The stretched audio replaces the input and the play position stays.
      // (On kError libwebrtc's GetAudioInternal fails the call; here the
      // input is kept as it is.)
      played_.resize(played_.size() - from_played);
      sync_.resize(sync_.size() - from_sync);
      const size_t back = std::min(from_played, scratch_.size());
      played_.insert(played_.end(), scratch_.begin(), scratch_.begin() + static_cast<std::ptrdiff_t>(back));
      sync_.insert(sync_.end(), scratch_.begin() + static_cast<std::ptrdiff_t>(back), scratch_.end());
      const bool success = r == NetEqTimeStretch::Result::kSuccess;
      const bool low_energy = r == NetEqTimeStretch::Result::kSuccessLowEnergy;
      if (accelerate) {
        stats_.removed_samples_for_acceleration += changed;
        last_mode_ = success      ? NetEqMode::kAccelerateSuccess
                     : low_energy ? NetEqMode::kAccelerateLowEnergy
                                  : NetEqMode::kAccelerateFail;
        if (changed) result = AudioOperation::kAccelerate;
      } else {
        stats_.inserted_samples_for_deceleration += changed;
        last_mode_ = success      ? NetEqMode::kPreemptiveExpandSuccess
                     : low_energy ? NetEqMode::kPreemptiveExpandLowEnergy
                                  : NetEqMode::kPreemptiveExpandFail;
        if (changed) result = AudioOperation::kPreemptiveExpand;
      }
      expand_.Reset();  // DoAccelerate and DoPreemptiveExpand.
      expanding_ = false;
      break;
    }
    default:
      // NetEqImpl::DoNormal: the mode changes only when something was decoded.
      if (extracted > 0) last_mode_ = NetEqMode::kNormal;
      break;
  }
  if (last_mode_ != NetEqMode::kExpand) {
    expanding_ = false;
    expanded_samples_ = 0;
  }
  if (static_cast<int64_t>(sync_.size()) < need) {
    // NetEqImpl::GetAudioInternal: SyncBuffer::GetNextAudioInterleaved reads
    // nothing when it holds less than the output, so the frame is muted and
    // kSampleUnderrun returned before the background noise, playout
    // timestamp and noise stopwatch updates; NetEqImpl::GetAudio then skips
    // StatisticsCalculator::IncreaseCounter. Not reached with whole packets
    // of 10 ms or more.
    std::fill(out.begin(), out.end(), 0.0f);
    SM_EVENT(cfg_.events, now, "neteq", "underrun", static_cast<double>(sync_.size()), next_ts_);
    ++stats_.ops[static_cast<int>(AudioOperation::kSilence)];
    return AudioOperation::kSilence;
  }
  if (last_mode_ != NetEqMode::kExpand) noise_start_tick_.reset();
  std::copy(sync_.begin(), sync_.begin() + static_cast<std::ptrdiff_t>(need), out.begin());
  sync_.erase(sync_.begin(), sync_.begin() + static_cast<std::ptrdiff_t>(need));
  played_.insert(played_.end(), out.begin(), out.end());
  if (sync_.size() < expand_.overlap_length()) {
    // NetEqImpl::GetAudioInternal reinstalls the lookahead by moving the
    // sync buffer's index back over samples just played.
    const size_t missing = std::min(expand_.overlap_length() - sync_.size(), played_.size());
    sync_.insert(sync_.begin(), played_.end() - static_cast<std::ptrdiff_t>(missing), played_.end());
    played_.erase(played_.end() - static_cast<std::ptrdiff_t>(missing), played_.end());
  }
  const size_t keep = std::max(static_cast<size_t>(samples_30_ms), expand_.history_length());
  if (played_.size() > keep) played_.erase(played_.begin(), played_.end() - static_cast<std::ptrdiff_t>(keep));
  // NetEqImpl::GetAudioInternal: the background noise estimate is updated
  // only when the sync buffer holds decoded audio unchanged.
  if (last_mode_ == NetEqMode::kNormal || last_mode_ == NetEqMode::kAccelerateFail ||
      last_mode_ == NetEqMode::kPreemptiveExpandFail) {
    const size_t tail = std::min(NetEqBackgroundNoise::kVecLen, played_.size() + sync_.size());
    sync_scratch_.clear();
    if (tail > sync_.size()) sync_scratch_.assign(played_.end() - static_cast<std::ptrdiff_t>(tail - sync_.size()), played_.end());
    sync_scratch_.insert(sync_scratch_.end(), sync_.end() - static_cast<std::ptrdiff_t>(std::min(tail, sync_.size())), sync_.end());
    expand_.background_noise().Update(sync_scratch_);
  }
  stats_.total_samples_received += static_cast<uint64_t>(need);
  ++stats_.ops[static_cast<int>(result)];
  return result;
}

size_t AudioJitterBuffer::ExpandOnce(std::vector<float>& out) {
  // NetEq's sync buffer starts out as zeros.
  const size_t real = played_.size() + sync_.size();
  const size_t pad = real < expand_.history_length() ? expand_.history_length() - real : 0;
  sync_scratch_.assign(pad, 0.0f);
  sync_scratch_.insert(sync_scratch_.end(), played_.begin(), played_.end());
  sync_scratch_.insert(sync_scratch_.end(), sync_.begin(), sync_.end());
  const size_t n = expand_.Process(sync_scratch_, out);
  // Write back the overlap-add.
  auto it = sync_scratch_.begin() + static_cast<std::ptrdiff_t>(pad);
  std::copy(it, it + static_cast<std::ptrdiff_t>(played_.size()), played_.begin());
  std::copy(it + static_cast<std::ptrdiff_t>(played_.size()), sync_scratch_.end(), sync_.begin());
  return n;
}

void AudioJitterBuffer::JoinAfterExpand(std::vector<float>& decoded) {
  // Normal::Process: one more expansion to crossfade from, without muting.
  expand_.SetParametersForNormalAfterExpand();
  expanded_.clear();
  ExpandOnce(expanded_);
  expand_.Reset();
  // The mute factor is raised to the background noise level.
  const int fs_mult = cfg_.sample_rate / 8000;
  int mute_factor =
      NetEqNormalMuteFactor(decoded, cfg_.sample_rate, expand_.MuteFactor(), expand_.background_noise().Energy());
  // Back to full gain by 0.64 per 20 ms, or within the decoded audio.
  const int increment = std::max(64 / fs_mult, (16384 - mute_factor) / std::max<int>(1, static_cast<int>(decoded.size())));
  for (float& v : decoded) {
    v = v * static_cast<float>(mute_factor) / 16384.0f;
    mute_factor = std::min(mute_factor + increment, 16384);
  }
  // Crossfade from the expansion over 1 ms.
  const size_t win = std::min({static_cast<size_t>(cfg_.sample_rate / 1000), expanded_.size(), decoded.size()});
  if (win == 0) return;
  const int slope = 16384 / static_cast<int>(win);
  int up = 0;
  for (size_t i = 0; i < win; ++i) {
    up += slope;
    decoded[i] = (static_cast<float>(up) * decoded[i] + static_cast<float>(16384 - up) * expanded_[i]) / 16384.0f;
  }
}

void AudioJitterBuffer::MergeAfterExpand(std::vector<float>& decoded, Timestamp now) {
  // NetEqImpl::DoMerge: Merge::Process on the sync buffer (played_ then
  // sync_, zeros before its start as ExpandOnce).
  const size_t real = played_.size() + sync_.size();
  const size_t pad = real < expand_.history_length() ? expand_.history_length() - real : 0;
  sync_scratch_.assign(pad, 0.0f);
  sync_scratch_.insert(sync_scratch_.end(), played_.begin(), played_.end());
  sync_scratch_.insert(sync_scratch_.end(), sync_.begin(), sync_.end());
  expanded_.clear();
  merge_.Process(decoded, sync_scratch_, sync_.size(), expand_, expanded_);
  // Write back: the unplayed part is now the start of the merged signal (cut
  // to Merge's maximum, with zeros put before it), and the played part keeps
  // its length.
  const size_t old_length = merge_.old_length();
  const auto future_begin = sync_scratch_.end() - static_cast<std::ptrdiff_t>(old_length);
  sync_.assign(future_begin, sync_scratch_.end());
  const size_t history = std::min(played_.size(), static_cast<size_t>(future_begin - sync_scratch_.begin()));
  played_.assign(future_begin - static_cast<std::ptrdiff_t>(history), future_begin);
  // The merge shifts the decoded audio by the alignment: the samples it adds
  // or takes away count as concealed (StatisticsCalculator's
  // Expanded*SamplesCorrection, a negative one held against later additions).
  AddConcealedSamples(static_cast<int64_t>(expanded_.size()) - static_cast<int64_t>(decoded.size()));
  SM_EVENT(cfg_.events, now, "neteq", "merge", static_cast<double>(merge_.best_correlation_index()), merge_.mute_factor());
  decoded.swap(expanded_);
}

void AudioJitterBuffer::AddConcealedSamples(int64_t n) {
  if (n < 0) {
    concealed_samples_correction_ += static_cast<uint64_t>(-n);
    return;
  }
  const uint64_t canceled_out = std::min(static_cast<uint64_t>(n), concealed_samples_correction_);
  concealed_samples_correction_ -= canceled_out;
  stats_.concealed_samples += static_cast<uint64_t>(n) - canceled_out;
}

void AudioJitterBuffer::ExportMetrics(MetricsWriter& w) const {
  w.Counter("packets_received", stats_.packets_received);
  w.Counter("packets_discarded_late", stats_.packets_discarded_late);
  w.Counter("packets_duplicate", stats_.packets_duplicate);
  w.Counter("packets_discarded_unsupported_rate", stats_.packets_discarded_unsupported_rate);
  w.Counter("packets_lost", stats_.packets_lost);
  w.Counter("buffer_flushes", stats_.buffer_flushes);
  w.Counter("total_samples_received", stats_.total_samples_received);
  w.Counter("concealed_samples", stats_.concealed_samples);
  w.Counter("concealment_events", stats_.concealment_events);
  w.Counter("muted_frames", stats_.muted_frames);
  w.Counter("inserted_samples_for_deceleration", stats_.inserted_samples_for_deceleration);
  w.Counter("removed_samples_for_acceleration", stats_.removed_samples_for_acceleration);
  for (int i = 0; i < 6; ++i) {
    w.Counter(std::string("op_") + ToString(static_cast<AudioOperation>(i)), stats_.ops[i]);
  }
  w.Gauge("target_delay_ms", TargetDelayMs());
  w.Gauge("current_level_ms", CurrentLevelMs());
  if (cfg_.version >= 2) w.Gauge("filtered_buffer_level_samples", decision_.GetFilteredBufferLevel());
  w.Hist("jitter_buffer_delay_ms", stats_.jitter_buffer_delay_ms);
  w.Counter("jitter_buffer_delay_ms_samples", stats_.jitter_buffer_delay_ms_samples);
  w.Counter("jitter_buffer_emitted_count", stats_.jitter_buffer_emitted_count);
}

}  // namespace sm::timing
