#include "sm/cc/pacer.h"

#include <algorithm>

namespace sm::cc {

namespace {
PacerV1Config V1(const PacerConfig& c) {
  PacerV1Config v;
  v.max_queue_time = c.max_queue_time;
  v.bypass_audio = c.bypass_audio;
  v.padding_packet = c.padding_packet;
  v.events = c.events;
  return v;
}
PacingControllerConfig V2(const PacerConfig& c) {
  PacingControllerConfig v = c.v2;
  v.padding_packet = c.padding_packet;
  v.events = c.events;
  return v;
}
}  // namespace

Pacer::Pacer(PacerConfig cfg)
    : v2_on_(cfg.version == PacerVersion::kV2),
      task_queue_(cfg.task_queue_scheduling),
      hold_back_window_(cfg.hold_back_window),
      hold_back_packets_(cfg.hold_back_packets) {
  if (v2_on_) v2_ = std::make_unique<PacingController>(V2(cfg));
  else v1_ = std::make_unique<PacerV1>(V1(cfg));
}

void Pacer::SetPacingRates(DataRate pacing, DataRate padding) {
  if (v2_on_) {
    v2_->SetPacingRates(pacing, padding);
    kick_ = true;
  } else {
    v1_->SetPacingRates(pacing, padding);
  }
}
void Pacer::Enqueue(PacedPacket p) {
  if (v2_on_) {
    // TaskQueuePacedSender::EnqueuePackets: ExpFilter(0.95).Apply(1, size), then MaybeProcessPackets.
    const double size = static_cast<double>(p.size.bytes());
    packet_size_ = packet_size_ < 0 ? size : 0.95 * packet_size_ + 0.05 * size;
    v2_->Enqueue(std::move(p));
    kick_ = true;
  } else {
    v1_->Enqueue(std::move(p));
  }
}
void Pacer::AddProbeCluster(const ProbeCluster& c) {
  if (v2_on_) {
    v2_->AddProbeCluster(c);
    kick_ = true;
  } else {
    v1_->AddProbeCluster(c);
  }
}
std::vector<PacedPacket> Pacer::Process(Timestamp now) {
  if (!v2_on_) return v1_->Process(now);
  return task_queue_ ? ProcessV2(now) : v2_->Process(now);
}

// TaskQueuePacedSender::MaybeProcessPackets (modules/pacing/task_queue_paced_sender.cc), called with
// -inf for a change (`kick_`) and with the scheduled time for the timer.
std::vector<PacedPacket> Pacer::ProcessV2(Timestamp now) {
  const bool timer_due = !next_process_time_.IsFinite() || now >= next_process_time_;
  if (!kick_ && !timer_due) return {};
  kick_ = false;
  std::vector<PacedPacket> out;
  auto margin = [&] { return v2_->probing() ? TimeDelta::Millis(1) : TimeDelta::Zero(); };
  TimeDelta early_execute_margin = margin();
  Timestamp next_send_time = v2_->NextSendTime(now);
  for (int i = 0; i < 1024 && next_send_time <= now + early_execute_margin; ++i) {
    for (PacedPacket& p : v2_->Process(now)) out.push_back(std::move(p));
    next_send_time = v2_->NextSendTime(now);
    early_execute_margin = margin();
  }
  if (timer_due) next_process_time_ = Timestamp::MinusInfinity();  // This call is the timer: it is spent.

  // Do not hold back in probing.
  TimeDelta hold_back = TimeDelta::Zero();
  if (!v2_->probing()) {
    hold_back = hold_back_window_;
    // pacing_controller_.pacing_rate(): the adjusted rate, after the long-queue boost (#166).
    const DataRate rate = v2_->pacing_rate();
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
  if (!v2_on_) return v1_->NextSendTime(now);
  if (!task_queue_) return v2_->NextSendTime(now);
  if (kick_) return now;
  return next_process_time_.IsFinite() ? std::max(now, next_process_time_) : v2_->NextSendTime(now);
}
std::vector<PacedPacket> Pacer::TakeFlushed() { return v2_on_ ? v2_->TakeFlushed() : std::vector<PacedPacket>{}; }
void Pacer::Pause() {
  if (v2_on_) {
    v2_->Pause();
    kick_ = true;
  } else {
    v1_->Pause();
  }
}
void Pacer::Resume() {
  if (v2_on_) {
    v2_->Resume();
    kick_ = true;
  } else {
    v1_->Resume();
  }
}
bool Pacer::paused() const { return v2_on_ ? v2_->paused() : v1_->paused(); }
void Pacer::SetCongested(bool congested) {
  if (v2_on_) {
    v2_->SetCongested(congested);
    kick_ = true;
  }
}
DataSize Pacer::QueueSize() const { return v2_on_ ? v2_->QueueSize() : v1_->QueueSize(); }
size_t Pacer::QueuePackets() const { return v2_on_ ? v2_->QueuePackets() : v1_->QueuePackets(); }
TimeDelta Pacer::OldestQueueTime(Timestamp now) const {
  return v2_on_ ? v2_->OldestQueueTime(now) : v1_->OldestQueueTime(now);
}
const PacerStats& Pacer::stats() const { return v2_on_ ? v2_->stats() : v1_->stats(); }
void Pacer::ExportMetrics(MetricsWriter& w) const {
  if (v2_on_) v2_->ExportMetrics(w);
  else v1_->ExportMetrics(w);
}

}  // namespace sm::cc
