#include "sm/cc/pacer_v1.h"

#include <algorithm>
#include <string>

namespace sm::cc {

namespace {
const char* kPrioName[5] = {"audio", "rtx", "video", "fec", "padding"};
}

PacerV1::PacerV1(PacerV1Config cfg) : cfg_(cfg) {}

void PacerV1::SetPacingRates(DataRate pacing, DataRate padding) {
  pacing_ = pacing;
  padding_rate_ = padding;
}

void PacerV1::Enqueue(PacedPacket p) {
  queue_bytes_ += p.size;
  queues_[static_cast<size_t>(p.priority)].push_back(p);
}

void PacerV1::AddProbeCluster(const ProbeCluster& c) { probes_.push_back(ActiveProbe{c}); }

size_t PacerV1::QueuePackets() const {
  size_t n = 0;
  for (const auto& q : queues_) n += q.size();
  return n;
}

TimeDelta PacerV1::OldestQueueTime(Timestamp now) const {
  Timestamp oldest = Timestamp::PlusInfinity();
  for (const auto& q : queues_) {
    if (!q.empty()) oldest = std::min(oldest, q.front().enqueue_time);
  }
  return oldest.IsFinite() ? now - oldest : TimeDelta::Zero();
}

DataRate PacerV1::EffectiveRate(Timestamp now) {
  if (!probes_.empty()) return probes_.front().c.rate;
  DataRate rate = pacing_;
  // Drain: make sure the queue empties within max_queue_time.
  if (queue_bytes_.bytes() > 0) {
    TimeDelta left = cfg_.max_queue_time - OldestQueueTime(now);
    if (left < TimeDelta::Millis(1)) left = TimeDelta::Millis(1);
    DataRate needed = DataRate::FromSizeAndTime(queue_bytes_, left);
    if (needed > rate) {
      ++stats_.drain_boosts;
      rate = needed;
    }
  }
  return rate;
}

void PacerV1::Drain(Timestamp now) {
  if (!last_process_.IsFinite()) last_process_ = now;
  TimeDelta elapsed = std::min(now - last_process_, TimeDelta::Millis(30));
  last_process_ = now;
  DataRate rate = EffectiveRate(now);
  media_debt_bytes_ = std::max(0.0, media_debt_bytes_ - rate.bps() / 8.0 * elapsed.seconds());
  padding_debt_bytes_ = std::max(0.0, padding_debt_bytes_ - padding_rate_.bps() / 8.0 * elapsed.seconds());
}

bool PacerV1::PopNext(Timestamp now, PacedPacket* out) {
  for (size_t prio = 0; prio < queues_.size(); ++prio) {
    auto& q = queues_[prio];
    if (q.empty()) continue;
    *out = q.front();
    q.pop_front();
    queue_bytes_ -= out->size;
    (void)now;
    return true;
  }
  return false;
}

std::vector<PacedPacket> PacerV1::Process(Timestamp now) {
  std::vector<PacedPacket> sent;
  Drain(now);
  auto account = [&](PacedPacket& p) {
    size_t i = static_cast<size_t>(p.priority);
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
  };
  // Audio bypasses the budget (it is small and latency-critical).
  auto& audio = queues_[static_cast<size_t>(PacketPriority::kAudio)];
  if (cfg_.bypass_audio && !paused_) {
    while (!audio.empty()) {
      PacedPacket p = audio.front();
      audio.pop_front();
      queue_bytes_ -= p.size;
      media_debt_bytes_ += static_cast<double>(p.size.bytes());
      account(p);
    }
  }
  if (paused_) return sent;
  // Allow at most ~5 ms worth of burst per call.
  const DataRate rate = EffectiveRate(now);
  const double burst_bytes = std::max(1500.0, rate.bps() / 8.0 * 0.005);
  for (int guard = 0; guard < 10000; ++guard) {
    if (media_debt_bytes_ > burst_bytes) break;
    PacedPacket p;
    if (PopNext(now, &p)) {
      media_debt_bytes_ += static_cast<double>(p.size.bytes());
      if (p.priority != PacketPriority::kAudio) {
        padding_debt_bytes_ += static_cast<double>(p.size.bytes());
      }
      account(p);
      continue;
    }
    // Queue empty: padding for an active probe, or to hold a padding rate.
    bool probing = !probes_.empty();
    if (probing || (padding_rate_.bps() > 0 && padding_debt_bytes_ <= 0)) {
      PacedPacket pad;
      pad.id = next_padding_id_++;
      pad.priority = PacketPriority::kPadding;
      pad.size = cfg_.padding_packet;
      pad.enqueue_time = now;
      pad.is_padding = true;
      media_debt_bytes_ += static_cast<double>(pad.size.bytes());
      padding_debt_bytes_ += static_cast<double>(pad.size.bytes());
      account(pad);
      continue;
    }
    break;
  }
  return sent;
}

Timestamp PacerV1::NextSendTime(Timestamp now) const {
  if (QueuePackets() == 0 && probes_.empty()) return now + TimeDelta::Millis(5);
  if (media_debt_bytes_ <= 0) return now;
  double secs = media_debt_bytes_ * 8.0 / std::max<double>(1.0, static_cast<double>(pacing_.bps()));
  return now + TimeDelta::SecondsF(secs);
}

void PacerV1::ExportMetrics(MetricsWriter& w) const {
  for (size_t i = 0; i < 5; ++i) {
    std::string n = kPrioName[i];
    w.Counter("sent_packets_" + n, stats_.sent_packets[i]);
    w.Counter("sent_bytes_" + n, static_cast<uint64_t>(stats_.sent_bytes[i]));
    if (i != 4) w.Hist("queue_delay_ms_" + n, stats_.queue_delay_ms[i]);
  }
  w.Counter("probe_packets", stats_.probe_packets);
  w.Counter("drain_boosts", stats_.drain_boosts);
  w.Gauge("queue_bytes", static_cast<double>(queue_bytes_.bytes()));
  w.Gauge("pacing_kbps", pacing_.kbps());
  w.Gauge("paused", paused_ ? 1 : 0);
}

}  // namespace sm::cc
