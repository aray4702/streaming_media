// 5.5 Packet pacer with priority queues.
//
// Encoders emit a whole frame at once; sending it as a line-rate burst
// fills router queues (bufferbloat) and causes self-inflicted loss. The
// pacer releases packets at the pacing rate (usually 2.5 x the target) using
// a "media debt" leaky bucket. Packets are queued per priority:
//   audio > retransmission > video > FEC > padding
// so audio is never stuck behind a keyframe, and video degrades first.
// Large queues are drained faster so no packet waits longer than
// max_queue_time. Probe clusters are sent at their own rate and tagged so
// the probe estimator can find them; padding fills a probe (or a requested
// padding rate) when there is no media to send.
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include "sm/cc/probing.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::cc {

enum class PacketPriority : int { kAudio = 0, kRetransmission = 1, kVideo = 2, kFec = 3, kPadding = 4 };

struct PacedPacket {
  uint64_t id = 0;  // Caller's handle.
  PacketPriority priority = PacketPriority::kVideo;
  DataSize size;
  Timestamp enqueue_time;
  int probe_cluster = -1;  // Set by the pacer when sent as part of a probe.
  bool is_padding = false;
};

struct PacerConfig {
  TimeDelta max_queue_time = TimeDelta::Millis(2000);
  bool bypass_audio = true;  // Audio is sent immediately, never queued behind budget.
  DataSize padding_packet = DataSize::Bytes(224);
  EventSink* events = nullptr;
};

struct PacerStats {
  std::array<uint64_t, 5> sent_packets{};
  std::array<int64_t, 5> sent_bytes{};
  uint64_t probe_packets = 0;
  uint64_t drain_boosts = 0;
  std::array<Histogram, 5> queue_delay_ms;
};

class Pacer : public MetricsSource {
 public:
  explicit Pacer(PacerConfig cfg = {});
  void SetPacingRates(DataRate pacing, DataRate padding);
  void Enqueue(PacedPacket p);
  void AddProbeCluster(const ProbeCluster& c);
  // Sends everything that is due by `now`. Call every few ms or at
  // NextSendTime().
  std::vector<PacedPacket> Process(Timestamp now);
  Timestamp NextSendTime(Timestamp now) const;
  void Pause() { paused_ = true; }
  void Resume() { paused_ = false; }
  bool paused() const { return paused_; }

  DataSize QueueSize() const { return queue_bytes_; }
  size_t QueuePackets() const;
  TimeDelta OldestQueueTime(Timestamp now) const;
  const PacerStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  bool PopNext(Timestamp now, PacedPacket* out);
  void Drain(Timestamp now);
  DataRate EffectiveRate(Timestamp now);

  PacerConfig cfg_;
  std::array<std::deque<PacedPacket>, 5> queues_;
  DataSize queue_bytes_;
  DataRate pacing_ = DataRate::KilobitsPerSec(750);
  DataRate padding_rate_;
  double media_debt_bytes_ = 0;
  double padding_debt_bytes_ = 0;
  Timestamp last_process_ = Timestamp::MinusInfinity();
  bool paused_ = false;
  struct ActiveProbe {
    ProbeCluster c;
    int sent_probes = 0;
    int64_t sent_bytes = 0;
  };
  std::deque<ActiveProbe> probes_;
  uint64_t next_padding_id_ = 1ull << 62;
  PacerStats stats_;
};

}  // namespace sm::cc
