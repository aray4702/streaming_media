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
constexpr DataSize kMaxBurstSize = DataSize::Bytes(63 * 1000);

// libwebrtc's unit arithmetic: DataRate * TimeDelta rounds to nearest;
// DataSize / DataRate rounds up.
DataSize Mul(DataRate r, TimeDelta t) {
  return DataSize::Bytes(static_cast<int64_t>(std::llround(static_cast<double>(r.bps()) * static_cast<double>(t.us()) / 8e6)));
}
TimeDelta Div(DataSize s, DataRate r) {
  if (r.bps() <= 0) return TimeDelta::PlusInfinity();
  return TimeDelta::Micros(static_cast<int64_t>(std::ceil(static_cast<double>(s.bytes()) * 8e6 / static_cast<double>(r.bps()))));
}
DataRate Div(DataSize s, TimeDelta t) {
  return DataRate::BitsPerSec(static_cast<int64_t>(std::ceil(static_cast<double>(s.bytes()) * 8e6 / static_cast<double>(t.us()))));
}
DataSize Min(DataSize a, DataSize b) { return a < b ? a : b; }
}  // namespace

PacingController::PacingController(PacingControllerConfig cfg)
    : cfg_(cfg), send_burst_interval_(cfg.send_burst_interval), queue_time_limit_(cfg.queue_time_limit) {}

void PacingController::Start(Timestamp now) {
  started_ = true;
  last_timestamp_ = now;
  last_process_time_ = now;
  last_send_time_ = now;
  queue_update_time_ = now;
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
  if (started_) SetQueuePauseState(paused_ || congested_, last_timestamp_);
  else queue_paused_ = true;
}

void PacingController::Resume() {
  paused_ = false;
  if (started_) SetQueuePauseState(paused_ || congested_, last_timestamp_);
  else queue_paused_ = congested_;
}

void PacingController::SetCongested(bool congested) {
  if (started_) {
    if (congested_ && !congested) UpdateBudgetWithElapsedTime(UpdateTimeAndGetElapsed(last_timestamp_));
    congested_ = congested;
    SetQueuePauseState(paused_ || congested_, last_timestamp_);
  } else {
    congested_ = congested;
    queue_paused_ = paused_ || congested_;
  }
}

void PacingController::AddProbeCluster(const ProbeCluster& c) { probes_.push_back(ActiveProbe{c}); }

// --- Prioritized packet queue ----------------------------------------------

int PacingController::Level(PacketPriority p) {
  switch (p) {
    case PacketPriority::kAudio: return 0;
    case PacketPriority::kRetransmission: return 1;
    case PacketPriority::kVideo:
    case PacketPriority::kFec: return 3;  // Redundancy is sent with the video it protects.
    case PacketPriority::kPadding: return 4;
  }
  return 3;
}

void PacingController::UpdateAverageQueueTime(Timestamp now) {
  if (!queue_update_time_.IsFinite()) queue_update_time_ = now;
  if (now <= queue_update_time_) return;
  const TimeDelta delta = now - queue_update_time_;
  if (queue_paused_) {
    pause_time_sum_ += delta;
  } else {
    queue_time_sum_ += delta * static_cast<double>(queue_packets_);
  }
  queue_update_time_ = now;
}

void PacingController::SetQueuePauseState(bool paused, Timestamp now) {
  UpdateAverageQueueTime(now);
  queue_paused_ = paused;
}

TimeDelta PacingController::AverageQueueTime() const {
  if (queue_packets_ == 0) return TimeDelta::Zero();
  return queue_time_sum_ / static_cast<int64_t>(queue_packets_);
}

void PacingController::Push(Timestamp now, PacedPacket p) {
  UpdateAverageQueueTime(now);
  Queued q;
  q.oldest_it = enqueue_times_.insert(now);
  // The time spent paused is subtracted from every packet's queue time: take
  // the pause total so far off its enqueue time (as PrioritizedPacketQueue does).
  q.enqueue_time = now - pause_time_sum_;
  queue_bytes_ += p.size;
  ++queue_packets_;
  p.enqueue_time = now;
  q.p = p;
  levels_[static_cast<size_t>(Level(p.priority))].push_back(std::move(q));
}

bool PacingController::Pop(PacedPacket* out, Timestamp now) {
  (void)now;
  for (auto& level : levels_) {
    if (level.empty()) continue;
    Queued q = std::move(level.front());
    level.pop_front();
    queue_bytes_ -= q.p.size;
    --queue_packets_;
    const TimeDelta in_queue = queue_update_time_ - q.enqueue_time - pause_time_sum_;
    queue_time_sum_ -= in_queue;
    enqueue_times_.erase(q.oldest_it);
    *out = q.p;
    return true;
  }
  return false;
}

TimeDelta PacingController::OldestQueueTime(Timestamp now) const {
  return enqueue_times_.empty() ? TimeDelta::Zero() : now - *enqueue_times_.begin();
}

TimeDelta PacingController::ExpectedQueueTime() const { return Div(queue_bytes_, adjusted_media_rate_); }

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
  if (cfg_.drain_large_queues && queue_bytes_ > DataSize::Zero()) {
    // Assuming equal size packets and input/output rate, the average packet has
    // avg_time_left to get the queue out if the limit is to be met.
    UpdateAverageQueueTime(now);
    const TimeDelta avg_time_left = std::max(TimeDelta::Millis(1), queue_time_limit_ - AverageQueueTime());
    const DataRate min_rate_needed = Div(queue_bytes_, avg_time_left);
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
  if (!cfg_.pace_audio && !levels_[0].empty()) return levels_[0].front().enqueue_time;
  return Timestamp::MinusInfinity();
}

Timestamp PacingController::NextSendTimeAt(Timestamp now) const {
  if (paused_) return last_send_time_ + kCongestedPacketInterval;

  // A packet that is not paced leaves at the time it was enqueued.
  const Timestamp unpaced = NextUnpacedSendTime();
  if (unpaced.IsFinite()) return unpaced;

  if (congested_ || !seen_first_packet_) return last_send_time_ + kCongestedPacketInterval;

  Timestamp next = Timestamp::PlusInfinity();
  if (adjusted_media_rate_ > DataRate::Zero() && queue_packets_ > 0) {
    // The debt may grow to one packet more than what the burst interval allows.
    const TimeDelta drain_time = Div(media_debt_, adjusted_media_rate_);
    // A burst is not larger than kMaxBurstSize, so as not to overfill socket buffers.
    const TimeDelta burst = std::min(send_burst_interval_, Div(kMaxBurstSize, adjusted_media_rate_));
    next = last_process_time_ + ((burst > drain_time) ? TimeDelta::Zero() : drain_time);
  } else if (padding_rate_ > DataRate::Zero() && queue_packets_ == 0) {
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
  if (!probes_.empty() && !paused_) {
    const DataRate rate = probes_.front().c.rate;
    return std::max(now, last_process_time_ + Div(probe_debt_, rate));
  }
  return NextSendTimeAt(now);
}

void PacingController::Enqueue(PacedPacket p) {
  const Timestamp now = Cur(p.enqueue_time);
  if (queue_packets_ == 0) {
    // Fast-forward the last process time, so that time that has passed is not
    // used as budget for the first new packet.
    Timestamp target = now;
    const Timestamp next = NextSendTimeAt(now);
    if (next.IsFinite()) target = std::min(now, next);
    UpdateBudgetWithElapsedTime(UpdateTimeAndGetElapsed(target));
  }
  Push(now, std::move(p));
  seen_first_packet_ = true;
  // The queue got longer: the pacing rate may need to change.
  MaybeUpdateMediaRateDueToLongQueue(now);
}

DataSize PacingController::PaddingToAdd(DataSize data_sent) const {
  (void)data_sent;
  if (queue_packets_ > 0) return DataSize::Zero();  // Real payload is available.
  if (congested_) return DataSize::Zero();
  if (padding_rate_ > DataRate::Zero() && padding_debt_ == DataSize::Zero()) return Mul(padding_rate_, kTargetPaddingDuration);
  return DataSize::Zero();
}

bool PacingController::GetPendingPacket(Timestamp target_send_time, Timestamp now, PacedPacket* out) {
  if (queue_packets_ == 0) return false;
  // Unpaced packets are exempt from the send checks.
  if (!NextUnpacedSendTime().IsFinite()) {
    if (congested_) return false;
    if (now <= target_send_time && send_burst_interval_.IsZero()) {
      // Sending slightly early is allowed if the debt would have drained by the target time.
      const TimeDelta flush_time = Div(media_debt_, adjusted_media_rate_);
      if (now + flush_time > target_send_time) return false;
    }
  }
  return Pop(out, now);
}

void PacingController::Account(PacedPacket& p, Timestamp now, std::vector<PacedPacket>& sent) {
  const size_t i = static_cast<size_t>(p.priority);
  ++stats_.sent_packets[i];
  stats_.sent_bytes[i] += p.size.bytes();
  if (!p.is_padding) stats_.queue_delay_ms[i].Add((now - p.enqueue_time).ms_f());
  if (!probes_.empty()) {
    ActiveProbe& ap = probes_.front();
    p.probe_cluster = ap.c.id;
    ++ap.sent_probes;
    ap.sent_bytes += p.size.bytes();
    ++stats_.probe_packets;
    if (ap.sent_probes >= ap.c.min_probes && ap.sent_bytes >= ap.c.min_bytes.bytes()) probes_.pop_front();
  }
  sent.push_back(p);
}

void PacingController::ProcessProbe(Timestamp now, std::vector<PacedPacket>& sent) {
  const TimeDelta elapsed = UpdateTimeAndGetElapsed(now);
  if (elapsed > TimeDelta::Zero()) UpdateBudgetWithElapsedTime(elapsed);
  const DataRate rate = probes_.front().c.rate;
  probe_debt_ -= Min(probe_debt_, Mul(rate, std::min(elapsed, TimeDelta::Millis(30))));
  // At most ~5 ms worth of the probe rate per call; BitrateProber (#48) will replace this.
  const DataSize burst = DataSize::Bytes(std::max<int64_t>(1500, Mul(rate, TimeDelta::Millis(5)).bytes()));
  for (int guard = 0; guard < 10000 && !probes_.empty(); ++guard) {
    if (probe_debt_ > burst) break;
    PacedPacket p;
    if (!Pop(&p, now)) {
      p = PacedPacket{};
      p.id = next_padding_id_++;
      p.priority = PacketPriority::kPadding;
      p.size = cfg_.padding_packet;
      p.enqueue_time = now;
      p.is_padding = true;
    }
    probe_debt_ += p.size;
    OnPacketSent(p.priority, p.size, now);
    Account(p, now, sent);
  }
}

std::vector<PacedPacket> PacingController::Process(Timestamp now_in) {
  std::vector<PacedPacket> sent;
  const Timestamp now = Cur(now_in);

  if (ShouldSendKeepalive(now)) {
    DataSize keepalive;
    if (seen_first_packet_) {
      PacedPacket p;
      p.id = next_padding_id_++;
      p.priority = PacketPriority::kPadding;
      p.size = cfg_.padding_packet;
      p.enqueue_time = now;
      p.is_padding = true;
      keepalive = p.size;
      Account(p, now, sent);
      ++stats_.keepalives;
    }
    OnPacketSent(PacketPriority::kPadding, keepalive, now);
  }
  if (paused_) return sent;

  if (!probes_.empty()) {
    ProcessProbe(now, sent);
    MaybeUpdateMediaRateDueToLongQueue(Cur(now));
    return sent;
  }

  Timestamp target_send_time = NextSendTimeAt(now);
  if (now < target_send_time) {
    // Too early; still let the debt drain.
    UpdateBudgetWithElapsedTime(UpdateTimeAndGetElapsed(now));
    return sent;
  }
  const TimeDelta elapsed = UpdateTimeAndGetElapsed(target_send_time);
  if (elapsed > TimeDelta::Zero()) UpdateBudgetWithElapsedTime(elapsed);

  DataSize data_sent;
  int iteration = 0;
  for (; iteration < cfg_.circuit_breaker_iterations; ++iteration) {
    PacedPacket p;
    if (!GetPendingPacket(target_send_time, now, &p)) {
      // No packet to send: see if padding is due.
      if (now - target_send_time > kMaxPaddingReplayDuration) {
        // The target is far behind the clock: replay at most 50 ms of padding.
        target_send_time = now - kMaxPaddingReplayDuration;
        last_process_time_ = std::max(last_process_time_, target_send_time);
      }
      const DataSize pad = PaddingToAdd(data_sent);
      if (pad > DataSize::Zero()) {
        const int64_t n = (pad.bytes() + cfg_.padding_packet.bytes() - 1) / cfg_.padding_packet.bytes();
        for (int64_t k = 0; k < n; ++k) {
          PacedPacket pp;
          pp.id = next_padding_id_++;
          pp.priority = PacketPriority::kPadding;
          pp.size = cfg_.padding_packet;
          pp.is_padding = true;
          Push(now, pp);
        }
        continue;  // Send the padding that was just queued.
      }
      break;
    }
    data_sent += p.size;
    OnPacketSent(p.priority, p.size, now);
    Account(p, now, sent);

    // More packets may be late: look at the new target time.
    target_send_time = NextSendTimeAt(now);
    if (target_send_time > now) break;
    UpdateBudgetWithElapsedTime(UpdateTimeAndGetElapsed(target_send_time));
  }
  if (iteration >= cfg_.circuit_breaker_iterations) {
    ++stats_.circuit_breaks;
    last_send_time_ = now;
    last_process_time_ = now;
    return sent;
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
  w.Gauge("queue_bytes", static_cast<double>(queue_bytes_.bytes()));
  w.Gauge("media_debt_bytes", static_cast<double>(media_debt_.bytes()));
  w.Gauge("avg_queue_ms", AverageQueueTime().ms_f());
  w.Gauge("pacing_kbps", pacing_rate_.kbps());
  w.Gauge("adjusted_pacing_kbps", adjusted_media_rate_.kbps());
  w.Gauge("paused", paused_ ? 1 : 0);
}

}  // namespace sm::cc
