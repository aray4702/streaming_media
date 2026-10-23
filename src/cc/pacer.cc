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

void Pacer::SetPacingRates(DataRate pacing, DataRate padding) {
  pc_->SetPacingRates(pacing, padding);
  kick_ = true;
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
std::vector<PacedPacket> Pacer::Process(Timestamp now) { return task_queue_ ? ProcessScheduled(now) : pc_->Process(now); }

// TaskQueuePacedSender::MaybeProcessPackets (modules/pacing/task_queue_paced_sender.cc), called with
// -inf for a change (`kick_`) and with the scheduled time for the timer.
std::vector<PacedPacket> Pacer::ProcessScheduled(Timestamp now) {
  const bool timer_due = !next_process_time_.IsFinite() || now >= next_process_time_;
  if (!kick_ && !timer_due) return {};
  kick_ = false;
  std::vector<PacedPacket> out;
  auto margin = [&] { return pc_->probing() ? TimeDelta::Millis(1) : TimeDelta::Zero(); };
  TimeDelta early_execute_margin = margin();
  Timestamp next_send_time = pc_->NextSendTime(now);
  for (int i = 0; i < 1024 && next_send_time <= now + early_execute_margin; ++i) {
    for (PacedPacket& p : pc_->Process(now)) out.push_back(std::move(p));
    next_send_time = pc_->NextSendTime(now);
    early_execute_margin = margin();
  }
  if (timer_due) next_process_time_ = Timestamp::MinusInfinity();  // This call is the timer: it is spent.

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
  // If no timer is set or the one set is later, schedule a new one.
  if (!next_process_time_.IsFinite() || next_process_time_ > next) {
    const int64_t us = time_to_next_process.us();
    next_process_time_ = now + TimeDelta::Micros((us + 999) / 1000 * 1000);
  }
  return out;
}
Timestamp Pacer::NextSendTime(Timestamp now) const {
  if (!task_queue_) return pc_->NextSendTime(now);
  if (kick_) return now;
  return next_process_time_.IsFinite() ? std::max(now, next_process_time_) : pc_->NextSendTime(now);
}
std::vector<PacedPacket> Pacer::TakeFlushed() { return pc_->TakeFlushed(); }
void Pacer::Pause() {
  pc_->Pause();
  kick_ = true;
}
void Pacer::Resume() {
  pc_->Resume();
  kick_ = true;
}
bool Pacer::paused() const { return pc_->paused(); }
void Pacer::SetCongested(bool congested) {
  pc_->SetCongested(congested);
  kick_ = true;
}
DataSize Pacer::QueueSize() const { return pc_->QueueSize(); }
TimeDelta Pacer::ExpectedQueueTime() const { return pc_->ExpectedQueueTime(); }
size_t Pacer::QueuePackets() const { return pc_->QueuePackets(); }
TimeDelta Pacer::OldestQueueTime(Timestamp now) const { return pc_->OldestQueueTime(now); }
const PacerStats& Pacer::stats() const { return pc_->stats(); }
void Pacer::ExportMetrics(MetricsWriter& w) const { pc_->ExportMetrics(w); }

}  // namespace sm::cc
