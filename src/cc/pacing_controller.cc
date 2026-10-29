#include "sm/cc/pacing_controller.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace sm::cc {

namespace {
const char* kPrioName[5] = {"audio", "rtx", "video", "fec", "padding"};

// modules/pacing/pacing_controller.cc constants.
constexpr TimeDelta kCongestedPacketInterval = TimeDelta::Millis(500);  // Also kPausedProcessInterval.
constexpr TimeDelta kMaxDebtInTime = TimeDelta::Millis(500);
constexpr TimeDelta kMaxElapsedTime = TimeDelta::Seconds(2);
constexpr TimeDelta kTargetPaddingDuration = TimeDelta::Millis(5);
constexpr TimeDelta kMaxPaddingReplayDuration = TimeDelta::Millis(50);
constexpr TimeDelta kMaxEarlyProbeProcessing = TimeDelta::Millis(1);
constexpr DataSize kMaxBurstSize = DataSize::Bytes(63 * 1000);

// libwebrtc's unit arithmetic (api/units/data_rate.h): DataRate * TimeDelta rounds to nearest;
// DataSize / DataRate and DataSize / TimeDelta truncate (#166).
DataSize Mul(DataRate r, TimeDelta t) {
  return DataSize::Bytes(static_cast<int64_t>(std::llround(static_cast<double>(r.bps()) * static_cast<double>(t.us()) / 8e6)));
}
TimeDelta Div(DataSize s, DataRate r) {
  if (r.bps() <= 0) return TimeDelta::PlusInfinity();
  return TimeDelta::Micros(s.bytes() * 8000000 / r.bps());
}
DataRate Div(DataSize s, TimeDelta t) {
  return DataRate::BitsPerSec(s.bytes() * 8000000 / t.us());
}
DataSize Min(DataSize a, DataSize b) { return a < b ? a : b; }
}  // namespace

PacingController::PacingController(PacingControllerConfig cfg)
    : cfg_(cfg), send_burst_interval_(cfg.send_burst_interval), queue_time_limit_(cfg.queue_time_limit), prober_(cfg.prober) {}

void PacingController::Start(Timestamp now) {
  started_ = true;
  last_timestamp_ = now;
  last_process_time_ = now;
  last_send_time_ = now;
  queue_.UpdateAverageQueueTime(now);
}

Timestamp PacingController::Cur(Timestamp now) {
  if (!started_) Start(now);
  if (now < last_timestamp_) now = last_timestamp_;  // Non-monotonic input is clamped.
  last_timestamp_ = now;
  return now;
}

// --- Configuration ---------------------------------------------------------

void PacingController::SetPacingRates(DataRate pacing, DataRate padding) {
  padding_rate_ = std::min(padding, pacing);  // SetPacerConfig caps padding at the pacing rate.
  pacing_rate_ = pacing;
  if (started_) MaybeUpdateMediaRateDueToLongQueue(last_timestamp_);
  else adjusted_media_rate_ = pacing_rate_;
}

void PacingController::Pause() {
  paused_ = true;
  queue_.SetPauseState(paused_ || congested_, last_timestamp_);
}

void PacingController::Resume() {
  paused_ = false;
  queue_.SetPauseState(paused_ || congested_, last_timestamp_);
}

void PacingController::SetCongested(bool congested) {
  if (started_ && congested_ && !congested) UpdateBudgetWithElapsedTime(UpdateTimeAndGetElapsed(last_timestamp_));
  congested_ = congested;
  queue_.SetPauseState(paused_ || congested_, last_timestamp_);
}

void PacingController::AddProbeCluster(const ProbeCluster& c) { prober_.CreateProbeCluster(c); }

// --- Prioritized packet queue ----------------------------------------------

int PrioritizedPacketQueue::Level(PacketPriority p) {
  switch (p) {
    case PacketPriority::kAudio: return 0;
    case PacketPriority::kRetransmission: return 1;
    case PacketPriority::kVideo:
    case PacketPriority::kFec: return 3;  // Redundancy is sent with the video it protects.
    case PacketPriority::kPadding: return 4;
  }
  return 3;
}

void PrioritizedPacketQueue::UpdateAverageQueueTime(Timestamp now) {
  if (!last_update_time_.IsFinite()) last_update_time_ = now;
  if (now <= last_update_time_) return;
  const TimeDelta delta = now - last_update_time_;
  if (paused_) {
    pause_time_sum_ += delta;
  } else {
    queue_time_sum_ += delta * static_cast<double>(size_packets_);
  }
  last_update_time_ = now;
}

void PrioritizedPacketQueue::SetPauseState(bool paused, Timestamp now) {
  UpdateAverageQueueTime(now);
  paused_ = paused;
}

TimeDelta PrioritizedPacketQueue::AverageQueueTime() const {
  if (size_packets_ == 0) return TimeDelta::Zero();
  return queue_time_sum_ / static_cast<int64_t>(size_packets_);
}

Timestamp PrioritizedPacketQueue::LeadingPacketEnqueueTime(PacketPriority p) const {
  const auto& level = levels_[static_cast<size_t>(Level(p))];
  return level.empty() ? Timestamp::MinusInfinity() : level.front().enqueue_time;
}

Timestamp PrioritizedPacketQueue::OldestEnqueueTime() const {
  return enqueue_times_.empty() ? Timestamp::MinusInfinity() : *enqueue_times_.begin();
}

void PrioritizedPacketQueue::Push(Timestamp now, PacedPacket p) {
  UpdateAverageQueueTime(now);
  Queued q;
  q.oldest_it = enqueue_times_.insert(now);
  // The time spent paused is subtracted from every packet's queue time: take
  // the pause total so far off its enqueue time (as PrioritizedPacketQueue does).
  q.enqueue_time = now - pause_time_sum_;
  size_bytes_ += p.size;
  size_payload_ += p.size - p.header_size;
  ++size_packets_;
  ++size_per_priority_[static_cast<size_t>(p.priority)];
  if (p.is_key_frame) ++num_keyframe_packets_;
  p.enqueue_time = now;
  q.p = p;
  levels_[static_cast<size_t>(Level(p.priority))].push_back(std::move(q));
}

void PrioritizedPacketQueue::DequeueInternal(Queued& q) {
  size_bytes_ -= q.p.size;
  size_payload_ -= q.p.size - q.p.header_size;
  --size_packets_;
  --size_per_priority_[static_cast<size_t>(q.p.priority)];
  if (q.p.is_key_frame) --num_keyframe_packets_;
  queue_time_sum_ -= last_update_time_ - q.enqueue_time - pause_time_sum_;
  enqueue_times_.erase(q.oldest_it);
}

std::optional<PacedPacket> PrioritizedPacketQueue::Pop() {
  for (auto& level : levels_) {
    if (level.empty()) continue;
    Queued q = std::move(level.front());
    level.pop_front();
    DequeueInternal(q);
    return q.p;
  }
  return std::nullopt;
}

std::vector<PacedPacket> PrioritizedPacketQueue::RemoveVideoStreamPackets(bool include_padding) {
  // The levels that hold the video stream's packets: retransmission (1) and
  // video and FEC (3), and padding (4), which libwebrtc sends on the video
  // stream's RTX SSRC. Audio is another stream.
  std::vector<PacedPacket> removed;
  for (PacketPriority prio : {PacketPriority::kRetransmission, PacketPriority::kVideo, PacketPriority::kPadding}) {
    if (prio == PacketPriority::kPadding && !include_padding) continue;
    auto& level = levels_[static_cast<size_t>(Level(prio))];
    for (Queued& q : level) {
      DequeueInternal(q);
      removed.push_back(q.p);
    }
    level.clear();
  }
  return removed;
}

std::vector<PacedPacket> PacingController::TakeFlushed() {
  std::vector<PacedPacket> out;
  out.swap(flushed_);
  return out;
}

TimeDelta PacingController::OldestQueueTime(Timestamp now) const {
  return queue_.Empty() ? TimeDelta::Zero() : now - queue_.OldestEnqueueTime();
}

TimeDelta PacingController::ExpectedQueueTime() const { return Div(QueueSizeData(), adjusted_media_rate_); }

// --- PacingController -------------------------------------------------------

TimeDelta PacingController::UpdateTimeAndGetElapsed(Timestamp now) {
  // No previous processing, or the last process was "in the future".
  if (!last_process_time_.IsFinite() || now < last_process_time_) return TimeDelta::Zero();
  TimeDelta elapsed = now - last_process_time_;
  last_process_time_ = now;
  return std::min(elapsed, kMaxElapsedTime);
}

bool PacingController::ShouldSendKeepalive(Timestamp now) const {
  // A padding packet every 500 ms keeps feedback flowing while nothing else is sent.
  if (paused_ || congested_ || !seen_first_packet_) return now - last_send_time_ >= kCongestedPacketInterval;
  return false;
}

void PacingController::UpdateBudgetWithElapsedTime(TimeDelta delta) {
  media_debt_ -= Min(media_debt_, Mul(adjusted_media_rate_, delta));
  padding_debt_ -= Min(padding_debt_, Mul(padding_rate_, delta));
}

void PacingController::UpdateBudgetWithSentData(DataSize size) {
  media_debt_ += size;
  media_debt_ = Min(media_debt_, Mul(adjusted_media_rate_, kMaxDebtInTime));
  UpdatePaddingBudgetWithSentData(size);
}

void PacingController::UpdatePaddingBudgetWithSentData(DataSize size) {
  padding_debt_ += size;
  padding_debt_ = Min(padding_debt_, Mul(padding_rate_, kMaxDebtInTime));
}

void PacingController::OnPacketSent(PacketPriority prio, DataSize size, Timestamp send_time) {
  const bool audio = prio == PacketPriority::kAudio;
  if ((!audio || cfg_.account_for_audio) && size > DataSize::Zero()) UpdateBudgetWithSentData(size);
  last_send_time_ = send_time;
}

void PacingController::MaybeUpdateMediaRateDueToLongQueue(Timestamp now) {
  adjusted_media_rate_ = pacing_rate_;
  bool boost = false;
  const DataSize queue_size_data = QueueSizeData();
  if (cfg_.drain_large_queues && queue_size_data > DataSize::Zero()) {
    // Assuming equal size packets and input/output rate, the average packet has
    // avg_time_left to get the queue out if the limit is to be met.
    queue_.UpdateAverageQueueTime(now);
    const TimeDelta avg_time_left = std::max(TimeDelta::Millis(1), queue_time_limit_ - queue_.AverageQueueTime());
    const DataRate min_rate_needed = Div(queue_size_data, avg_time_left);
    if (min_rate_needed > pacing_rate_) {
      adjusted_media_rate_ = min_rate_needed;
      boost = true;
    }
  }
  if (boost && !boosted_) {
    ++stats_.drain_boosts;
    SM_EVENT(cfg_.events, now, "pacer", "drain_boost", adjusted_media_rate_.bps(), pacing_rate_.bps());
  }
  boosted_ = boost;
}

Timestamp PacingController::NextUnpacedSendTime() const {
  if (!cfg_.pace_audio) return queue_.LeadingPacketEnqueueTime(PacketPriority::kAudio);
  return Timestamp::MinusInfinity();
}

Timestamp PacingController::NextSendTimeAt(Timestamp now) const {
  if (paused_) return last_send_time_ + kCongestedPacketInterval;

  // If probing is active, that always takes priority.
  if (prober_.is_probing() && !probing_send_failure_) {
    const Timestamp probe_time = prober_.NextProbeTime(now);
    if (probe_time != Timestamp::PlusInfinity()) return probe_time == Timestamp::MinusInfinity() ? now : probe_time;
  }

  // A packet that is not paced leaves at the time it was enqueued.
  const Timestamp unpaced = NextUnpacedSendTime();
  if (unpaced.IsFinite()) return unpaced;

  if (congested_ || !seen_first_packet_) return last_send_time_ + kCongestedPacketInterval;

  Timestamp next = Timestamp::PlusInfinity();
  if (adjusted_media_rate_ > DataRate::Zero() && !queue_.Empty()) {
    // The debt may grow to one packet more than what the burst interval allows.
    const TimeDelta drain_time = Div(media_debt_, adjusted_media_rate_);
    // A burst is not larger than kMaxBurstSize, so as not to overfill socket buffers.
    const TimeDelta burst = std::min(send_burst_interval_, Div(kMaxBurstSize, adjusted_media_rate_));
    next = last_process_time_ + ((burst > drain_time) ? TimeDelta::Zero() : drain_time);
  } else if (padding_rate_ > DataRate::Zero() && queue_.Empty()) {
    // No packets: wait until both debts have drained to send padding.
    TimeDelta drain_time = std::max(Div(media_debt_, adjusted_media_rate_), Div(padding_debt_, padding_rate_));
    if (drain_time.IsZero() && (media_debt_ > DataSize::Zero() || padding_debt_ > DataSize::Zero())) {
      drain_time = TimeDelta::Micros(1);
    }
    next = last_process_time_ + drain_time;
  } else {
    next = last_process_time_ + kCongestedPacketInterval;
  }
  (void)now;
  return next;
}

Timestamp PacingController::NextSendTime(Timestamp now) const {
  if (!started_) return now + kCongestedPacketInterval;
  return NextSendTimeAt(now);
}

void PacingController::Enqueue(PacedPacket p) {
  const Timestamp now = Cur(p.enqueue_time);
  if (p.is_key_frame && p.first_packet_of_frame && p.priority == PacketPriority::kVideo && !queue_.HasKeyframePackets() &&
      !queue_.Empty()) {
    const size_t before = flushed_.size();
    queue_.UpdateAverageQueueTime(now);
    for (PacedPacket& f : queue_.RemoveVideoStreamPackets(cfg_.keyframe_flush_padding)) flushed_.push_back(f);
    ++stats_.keyframe_flushes;
    stats_.flushed_packets += flushed_.size() - before;
    SM_EVENT(cfg_.events, now, "pacer", "keyframe_flush", static_cast<double>(flushed_.size() - before), 0);
  }
  prober_.OnIncomingPacket(std::max(DataSize::Zero(), p.size - transport_overhead_));
  if (queue_.Empty()) {
    // Fast-forward the last process time, so that time that has passed is not
    // used as budget for the first new packet.
    Timestamp target = now;
    const Timestamp next = NextSendTimeAt(now);
    if (next.IsFinite()) target = std::min(now, next);
    UpdateBudgetWithElapsedTime(UpdateTimeAndGetElapsed(target));
  }
  queue_.Push(now, std::move(p));
  seen_first_packet_ = true;
  // The queue got longer: the pacing rate may need to change.
  MaybeUpdateMediaRateDueToLongQueue(now);
}

DataSize PacingController::PaddingToAdd(DataSize recommended_probe_size, DataSize data_sent) const {
  if (!queue_.Empty()) return DataSize::Zero();  // Real payload is available.
  if (congested_) return DataSize::Zero();  // Not even for a probe.
  if (recommended_probe_size > DataSize::Zero()) {
    return recommended_probe_size > data_sent ? recommended_probe_size - data_sent : DataSize::Zero();
  }
  if (padding_rate_ > DataRate::Zero() && padding_debt_ == DataSize::Zero()) return Mul(padding_rate_, kTargetPaddingDuration);
  return DataSize::Zero();
}

bool PacingController::GetPendingPacket(const ProbeSendInfo& pacing_info, Timestamp target_send_time, Timestamp now, PacedPacket* out) {
  const bool is_probe = pacing_info.cluster_id >= 0;
  // The first packet of a probe is a small padding packet, so the estimate has a
  // more reliable start window.
  if (is_probe && pacing_info.bytes_sent == 0) {
    if (padding_generator_) {
      std::vector<PacedPacket> padding = padding_generator_(DataSize::Bytes(1), now);
      if (!padding.empty()) {
        *out = std::move(padding.front());
        return true;
      }
    } else {
      *out = MakePadding(cfg_.probe_start_padding, now);
      return true;
    }
  }
  if (queue_.Empty()) return false;
  // Unpaced packets and probes are exempt from the send checks.
  if (!NextUnpacedSendTime().IsFinite() && !is_probe) {
    if (congested_) return false;
    if (now <= target_send_time && send_burst_interval_.IsZero()) {
      // Sending slightly early is allowed if the debt would have drained by the target time.
      const TimeDelta flush_time = Div(media_debt_, adjusted_media_rate_);
      if (now + flush_time > target_send_time) return false;
    }
  }
  std::optional<PacedPacket> p = queue_.Pop();
  if (!p) return false;
  *out = std::move(*p);
  return true;
}

void PacingController::Account(PacedPacket& p, Timestamp now, const ProbeSendInfo& pacing_info, std::vector<PacedPacket>& sent) {
  const size_t i = static_cast<size_t>(p.priority);
  ++stats_.sent_packets[i];
  stats_.sent_bytes[i] += p.size.bytes();
  if (p.priority != PacketPriority::kPadding) stats_.queue_delay_ms[i].Add((now - p.enqueue_time).ms_f());
  if (pacing_info.cluster_id >= 0) {
    p.probe_cluster = pacing_info.cluster_id;
    ++stats_.probe_packets;
  }
  sent.push_back(p);
}

PacedPacket PacingController::MakePadding(DataSize size, Timestamp now) {
  PacedPacket p;
  p.id = next_padding_id_++;
  p.priority = PacketPriority::kPadding;
  p.size = size;
  p.enqueue_time = now;
  p.is_padding = true;
  return p;
}

std::vector<PacedPacket> PacingController::Process(Timestamp now_in) {
  std::vector<PacedPacket> sent;
  const Timestamp now = Cur(now_in);

  if (ShouldSendKeepalive(now)) {
    DataSize keepalive;
    if (seen_first_packet_) {
      // GeneratePadding(1 byte), sent at once.
      for (PacedPacket& p : padding_generator_ ? padding_generator_(DataSize::Bytes(1), now)
                                               : std::vector<PacedPacket>{MakePadding(cfg_.padding_packet, now)}) {
        keepalive += p.size;
        Account(p, now, ProbeSendInfo{}, sent);
        ++stats_.keepalives;
      }
    }
    OnPacketSent(PacketPriority::kPadding, keepalive, now);
  }
  if (paused_) return sent;

  // Probing is allowed to be processed up to kMaxEarlyProbeProcessing early.
  const TimeDelta early_execute_margin = prober_.is_probing() ? kMaxEarlyProbeProcessing : TimeDelta::Zero();
  Timestamp target_send_time = NextSendTimeAt(now);
  if (now + early_execute_margin < target_send_time) {
    // Too early, but if the queue is empty still let the debt drain.
    UpdateBudgetWithElapsedTime(UpdateTimeAndGetElapsed(now));
    return sent;
  }
  const TimeDelta elapsed = UpdateTimeAndGetElapsed(target_send_time);
  if (elapsed > TimeDelta::Zero()) UpdateBudgetWithElapsedTime(elapsed);

  ProbeSendInfo pacing_info;
  DataSize recommended_probe_size;
  bool is_probing = prober_.is_probing();
  if (is_probing) {
    // Probe timing is sensitive and handled by BitrateProber, so the actual
    // send time is used rather than the target.
    if (auto c = prober_.CurrentCluster(now)) {
      pacing_info = *c;
      recommended_probe_size = prober_.RecommendedMinProbeSize();
    } else {
      // No valid probe cluster returned, the probe might have timed out.
      is_probing = false;
    }
  }

  DataSize data_sent;
  int iteration = 0;
  for (; iteration < cfg_.circuit_breaker_iterations; ++iteration) {
    PacedPacket p;
    if (!GetPendingPacket(pacing_info, target_send_time, now, &p)) {
      // No packet to send: see if padding is due.
      if (now - target_send_time > kMaxPaddingReplayDuration) {
        // The target is far behind the clock: replay at most 50 ms of padding.
        target_send_time = now - kMaxPaddingReplayDuration;
        last_process_time_ = std::max(last_process_time_, target_send_time);
      }
      const DataSize pad = PaddingToAdd(recommended_probe_size, data_sent);
      if (pad > DataSize::Zero() && padding_generator_) {
        std::vector<PacedPacket> padding = padding_generator_(pad, now);
        if (!padding.empty()) {
          for (PacedPacket& g : padding) queue_.Push(now, std::move(g));
          continue;  // Send the padding that was just queued.
        }
        UpdatePaddingBudgetWithSentData(pad);  // Can't generate padding: still charge the budget.
      } else if (pad > DataSize::Zero()) {
        const int64_t n = (pad.bytes() + cfg_.padding_packet.bytes() - 1) / cfg_.padding_packet.bytes();
        for (int64_t k = 0; k < n; ++k) queue_.Push(now, MakePadding(cfg_.padding_packet, now));
        continue;  // Send the padding that was just queued.
      }
      break;
    }
    data_sent += p.size;
    OnPacketSent(p.priority, p.size, now);
    Account(p, now, is_probing ? pacing_info : ProbeSendInfo{}, sent);

    if (is_probing) {
      pacing_info.bytes_sent += p.size.bytes();
      // Stop the send loop when the burst has reached its target size.
      if (data_sent >= recommended_probe_size) break;
    }

    // More packets may be late: look at the new target time.
    target_send_time = NextSendTimeAt(now);
    if (target_send_time > now) {
      if (!is_probing) break;
      target_send_time = now;
    }
    UpdateBudgetWithElapsedTime(UpdateTimeAndGetElapsed(target_send_time));
  }
  if (iteration >= cfg_.circuit_breaker_iterations) {
    ++stats_.circuit_breaks;
    last_send_time_ = now;
    last_process_time_ = now;
    return sent;
  }
  if (is_probing) {
    probing_send_failure_ = data_sent == DataSize::Zero();
    if (!probing_send_failure_) prober_.ProbeSent(Cur(now), data_sent);
  }
  MaybeUpdateMediaRateDueToLongQueue(Cur(now));
  return sent;
}

void PacingController::ExportMetrics(MetricsWriter& w) const {
  for (size_t i = 0; i < 5; ++i) {
    std::string n = kPrioName[i];
    w.Counter("sent_packets_" + n, stats_.sent_packets[i]);
    w.Counter("sent_bytes_" + n, static_cast<uint64_t>(stats_.sent_bytes[i]));
    if (i != 4) w.Hist("queue_delay_ms_" + n, stats_.queue_delay_ms[i]);
  }
  w.Counter("probe_packets", stats_.probe_packets);
  w.Counter("drain_boosts", stats_.drain_boosts);
  w.Counter("keepalives", stats_.keepalives);
  w.Counter("circuit_breaks", stats_.circuit_breaks);
  w.Counter("keyframe_flushes", stats_.keyframe_flushes);
  w.Counter("flushed_packets", stats_.flushed_packets);
  w.Gauge("queue_bytes", static_cast<double>(queue_.SizeInBytes().bytes()));
  w.Gauge("media_debt_bytes", static_cast<double>(media_debt_.bytes()));
  w.Gauge("avg_queue_ms", queue_.AverageQueueTime().ms_f());
  w.Gauge("pacing_kbps", pacing_rate_.kbps());
  w.Gauge("adjusted_pacing_kbps", adjusted_media_rate_.kbps());
  w.Gauge("paused", paused_ ? 1 : 0);
}

}  // namespace sm::cc
