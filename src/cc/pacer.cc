#include "sm/cc/pacer.h"

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

Pacer::Pacer(PacerConfig cfg) : v2_on_(cfg.version == PacerVersion::kV2) {
  if (v2_on_) v2_ = std::make_unique<PacingController>(V2(cfg));
  else v1_ = std::make_unique<PacerV1>(V1(cfg));
}

void Pacer::SetPacingRates(DataRate pacing, DataRate padding) {
  if (v2_on_) v2_->SetPacingRates(pacing, padding);
  else v1_->SetPacingRates(pacing, padding);
}
void Pacer::Enqueue(PacedPacket p) {
  if (v2_on_) v2_->Enqueue(std::move(p));
  else v1_->Enqueue(std::move(p));
}
void Pacer::AddProbeCluster(const ProbeCluster& c) {
  if (v2_on_) v2_->AddProbeCluster(c);
  else v1_->AddProbeCluster(c);
}
std::vector<PacedPacket> Pacer::Process(Timestamp now) { return v2_on_ ? v2_->Process(now) : v1_->Process(now); }
Timestamp Pacer::NextSendTime(Timestamp now) const { return v2_on_ ? v2_->NextSendTime(now) : v1_->NextSendTime(now); }
void Pacer::Pause() {
  if (v2_on_) v2_->Pause();
  else v1_->Pause();
}
void Pacer::Resume() {
  if (v2_on_) v2_->Resume();
  else v1_->Resume();
}
bool Pacer::paused() const { return v2_on_ ? v2_->paused() : v1_->paused(); }
void Pacer::SetCongested(bool congested) {
  if (v2_on_) v2_->SetCongested(congested);
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
