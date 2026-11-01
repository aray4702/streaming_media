#include "sm/cc/pacer.h"

#include <algorithm>

namespace sm::cc {

namespace {
PacingControllerConfig ControllerConfig(const PacerConfig& c) {
  PacingControllerConfig v = c.controller;
  v.padding_packet = c.padding_packet;
  v.events = c.events;
  return v;
}
}  // namespace

Pacer::Pacer(PacerConfig cfg)
    : task_queue_(cfg.task_queue_scheduling),
      hold_back_window_(cfg.hold_back_window),
      hold_back_packets_(cfg.hold_back_packets),
      pc_(std::make_unique<PacingController>(ControllerConfig(cfg))) {}

// TaskQueuePacedSender::PostMaybeProcessPackets (task_queue_paced_sender.cc:217-225): SetConfig, Resume and
// SetCongested post a pass only if the queue holds packets. A padding rate set on an empty queue waits for the
// next enqueue or the pending timer (issue #813).
void Pacer::PostMaybeProcessPackets() {
  if (pc_->QueuePackets() > 0) kick_ = true;
}

void Pacer::SetPacingRates(DataRate pacing, DataRate padding) {
  pc_->SetPacingRates(pacing, padding);
  PostMaybeProcessPackets();
}

void Pacer::SetPacingRates(DataRate pacing, DataRate padding, Timestamp now) {
  pc_->SetPacingRates(pacing, padding, now);
  PostMaybeProcessPackets();
}
void Pacer::Enqueue(PacedPacket p) {
  // TaskQueuePacedSender::EnqueuePackets: ExpFilter(0.95).Apply(1, size), then MaybeProcessPackets.
  const double size = static_cast<double>(p.size.bytes());
  packet_size_ = packet_size_ < 0 ? size : 0.95 * packet_size_ + 0.05 * size;
  pc_->Enqueue(std::move(p));
  kick_ = true;
}
void Pacer::AddProbeCluster(const ProbeCluster& c) {
  pc_->AddProbeCluster(c);
  kick_ = true;
}
void Pacer::BeginBatch(Timestamp now) {
  if (!task_queue_) return;
  if (last_batch_ == now && kick_) (void)ProcessScheduled(now);
  last_batch_ = now;
}
std::vector<PacedPacket> Pacer::Process(Timestamp now) { return task_queue_ ? ProcessScheduled(now) : pc_->Process(now); }

// TaskQueuePacedSender (modules/pacing/task_queue_paced_sender.cc): every delayed task it posted runs
// MaybeProcessPackets(its scheduled time) when it fires, retired or not, and a change posts
// MaybeProcessPackets(-inf). Timers due by `now` run first, in the order they fire (SimTime runs the
// sender queue's due tasks before the tick that enqueues the frames captured then), then the change.
std::vector<PacedPacket> Pacer::ProcessScheduled(Timestamp now) {
  std::vector<PacedPacket> out;
  while (!timers_.empty() && timers_.front().fire <= now) {
    const Timestamp scheduled = timers_.front().scheduled;
    timers_.erase(timers_.begin());
    MaybeProcessPackets(now, scheduled, out);
  }
  if (kick_) {
    kick_ = false;
    MaybeProcessPackets(now, Timestamp::MinusInfinity(), out);
  }
  return out;
}

// TaskQueuePacedSender::MaybeProcessPackets (task_queue_paced_sender.cc:233-311).
void Pacer::MaybeProcessPackets(Timestamp now, Timestamp scheduled_process_time, std::vector<PacedPacket>& out) {
  auto margin = [&] { return pc_->probing() ? TimeDelta::Millis(1) : TimeDelta::Zero(); };
  TimeDelta early_execute_margin = margin();
  Timestamp next_send_time = pc_->NextSendTime(now);
  for (int i = 0; i < 1024 && next_send_time <= now + early_execute_margin; ++i) {
    for (PacedPacket& p : pc_->Process(now)) out.push_back(std::move(p));
    next_send_time = pc_->NextSendTime(now);
    early_execute_margin = margin();
  }
  // Ignore a retired scheduled task (it still processed packets above), otherwise reset next_process_time_.
  if (scheduled_process_time.IsFinite()) {
    if (scheduled_process_time != next_process_time_) return;
    next_process_time_ = Timestamp::MinusInfinity();
  }

  // Do not hold back in probing.
  TimeDelta hold_back = TimeDelta::Zero();
  if (!pc_->probing()) {
    hold_back = hold_back_window_;
    // pacing_controller_.pacing_rate(): the adjusted rate, after the long-queue boost (#166).
    const DataRate rate = pc_->pacing_rate();
    if (hold_back_packets_ >= 0 && rate > DataRate::Zero() && packet_size_ >= 0) {
      const int64_t bits = static_cast<int64_t>(packet_size_) * 8;
      const TimeDelta avg_packet_send_time = TimeDelta::Micros(bits * 1000000 / rate.bps());
      hold_back = std::min(hold_back, avg_packet_send_time * hold_back_packets_);
    }
  }
  const TimeDelta wait = next_send_time.IsFinite() ? next_send_time - now - early_execute_margin : TimeDelta::Seconds(1);
  const TimeDelta time_to_next_process = std::max(hold_back, wait);
  const Timestamp next = now + time_to_next_process;
  // If no task is in flight or the one in flight is later than `next`, post a new one; the old one is
  // retired. next_process_time_ keeps the unrounded time; the task fires at the delay rounded up to
  // 1 ms (PostDelayedHighPrecisionTask(..., time_to_next_process.RoundUpTo(TimeDelta::Millis(1)))).
  if (!next_process_time_.IsFinite() || next_process_time_ > next) {
    const int64_t us = time_to_next_process.us();
    const Timer t{now + TimeDelta::Micros((us + 999) / 1000 * 1000), next};
    // Tasks due at the same time run in the order they were posted.
    auto it = std::upper_bound(timers_.begin(), timers_.end(), t,
                               [](const Timer& a, const Timer& b) { return a.fire < b.fire; });
    timers_.insert(it, t);
    next_process_time_ = next;
  }
}
Timestamp Pacer::NextSendTime(Timestamp now) const {
  if (!task_queue_) return pc_->NextSendTime(now);
  if (kick_) return now;
  return !timers_.empty() ? std::max(now, timers_.front().fire) : pc_->NextSendTime(now);
}
std::vector<PacedPacket> Pacer::TakeFlushed() { return pc_->TakeFlushed(); }
std::vector<PacedPacket> Pacer::RemoveStreamPackets(const std::function<bool(const PacedPacket&)>& is_stream,
                                                    Timestamp now) {
  std::vector<PacedPacket> removed = pc_->RemoveStreamPackets(is_stream, now);
  kick_ = true;  // The posted task ends with MaybeProcessPackets(Timestamp::MinusInfinity()).
  return removed;
}
// TaskQueuePacedSender::Pause posts nothing; Resume posts as SetConfig does.
void Pacer::Pause() { pc_->Pause(); }
void Pacer::Resume() {
  pc_->Resume();
  PostMaybeProcessPackets();
}
bool Pacer::paused() const { return pc_->paused(); }
void Pacer::SetCongested(bool congested) {
  pc_->SetCongested(congested);
  PostMaybeProcessPackets();
}
DataSize Pacer::QueueSize() const { return pc_->QueueSize(); }
DataSize Pacer::QueueSizeData() const { return pc_->QueueSizeData(); }
TimeDelta Pacer::ExpectedQueueTime() const { return pc_->ExpectedQueueTime(); }
size_t Pacer::QueuePackets() const { return pc_->QueuePackets(); }
TimeDelta Pacer::OldestQueueTime(Timestamp now) const { return pc_->OldestQueueTime(now); }
const PacerStats& Pacer::stats() const { return pc_->stats(); }
void Pacer::ExportMetrics(MetricsWriter& w) const { pc_->ExportMetrics(w); }

}  // namespace sm::cc
