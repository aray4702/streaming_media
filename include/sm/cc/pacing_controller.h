// 5.5 Packet pacer with priority queues, v2: libwebrtc's PacingController.
//
// A port of modules/pacing/pacing_controller.cc, prioritized_packet_queue.cc
// (M157, compare/webrtc/reference/), behind a `Timestamp now` API with no
// task queue. Encoders emit a whole frame at once; sending it as a line-rate
// burst fills router queues (bufferbloat) and causes self-inflicted loss.
// The pacer releases packets at the pacing rate (usually 2.5 x the target)
// using libwebrtc's leaky bucket:
//   - `media_debt` grows with every packet sent and drains at the (adjusted)
//     pacing rate; packets may leave in a burst of `send_burst_interval`
//     (40 ms) at a time, and the debt is capped at 500 ms of the rate;
//   - the time that is credited since the last send is capped at 2 s;
//   - a queue that cannot drain within `queue_time_limit` (2 s, judged on the
//     average queue time, not the oldest packet) raises the rate;
//   - the first packet of a keyframe flushes the queued packets of the video
//     stream (video, FEC, retransmissions), as EnqueuePacket does;
//   - padding fills a requested padding rate, 5 ms of it at a time.
// Packets are queued per priority level, as PrioritizedPacketQueue does:
//   0 audio, 1 retransmission, 3 video and FEC (one FIFO), 4 padding
// (level 2, video retransmission, is only used when audio retransmission is
// prioritised, which libwebrtc does not do by default). Audio is not paced
// (it leaves at its enqueue time) but is counted in the debt, as a call with
// audio in the congestion-control allocation does. Probe clusters are sent at
// their own rate and tagged so the probe estimator can find them; padding
// fills a probe when there is no media. BitrateProber is #48's.
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <set>
#include <vector>

#include "sm/cc/pacer_types.h"
#include "sm/cc/probing.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::cc {

struct PacingControllerConfig {
  // PacingController::Configuration defaults.
  TimeDelta queue_time_limit = TimeDelta::Millis(2000);  // kMaxExpectedQueueLength
  bool drain_large_queues = true;
  TimeDelta send_burst_interval = TimeDelta::Millis(40);  // PacerConfig::kDefaultTimeInterval
  // Audio waits for the budget only with WebRTC-Pacer-BlockAudio (off by default).
  bool pace_audio = false;
  // AccountForAudioPacketsInPacedSender(true), what AudioSendStream does when audio is
  // part of the congestion-control allocation.
  bool account_for_audio = true;
  // Wire size of one padding packet (RTPSender::GeneratePadding, kMaxPaddingLength).
  DataSize padding_packet = DataSize::Bytes(224);
  int circuit_breaker_iterations = 1 << 16;
  EventSink* events = nullptr;
};

class PacingController : public MetricsSource {
 public:
  explicit PacingController(PacingControllerConfig cfg = {});
  void SetPacingRates(DataRate pacing, DataRate padding);
  void Enqueue(PacedPacket p);
  void AddProbeCluster(const ProbeCluster& c);
  // Sends everything that is due by `now`. Call at NextSendTime() or sooner.
  std::vector<PacedPacket> Process(Timestamp now);
  // Packets that Enqueue() discarded from the queue since the last call: the
  // first packet of a keyframe flushes the video stream's queued packets
  // (PacingController::EnqueuePacket), so the keyframe leaves as soon as it can.
  // The caller drops whatever it kept for them; they are never sent.
  std::vector<PacedPacket> TakeFlushed();
  Timestamp NextSendTime(Timestamp now) const;
  void Pause();
  void Resume();
  bool paused() const { return paused_; }
  // PacingController::SetCongested: nothing is sent while congested except a
  // keep-alive padding packet every 500 ms.
  void SetCongested(bool congested);
  bool congested() const { return congested_; }

  DataSize QueueSize() const { return queue_bytes_; }
  size_t QueuePackets() const { return queue_packets_; }
  // Age of the oldest queued packet (zero when empty).
  TimeDelta OldestQueueTime(Timestamp now) const;
  // QueueSizeData() / adjusted rate.
  TimeDelta ExpectedQueueTime() const;
  DataRate pacing_rate() const { return adjusted_media_rate_; }
  const PacerStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  static constexpr int kLevels = 5;
  struct Queued {
    PacedPacket p;
    Timestamp enqueue_time;  // Minus the time the queue spent paused.
    std::multiset<Timestamp>::iterator oldest_it;
  };

  void Start(Timestamp now);
  Timestamp Cur(Timestamp now);
  TimeDelta UpdateTimeAndGetElapsed(Timestamp now);
  bool ShouldSendKeepalive(Timestamp now) const;
  void UpdateBudgetWithElapsedTime(TimeDelta delta);
  void UpdateBudgetWithSentData(DataSize size);
  void UpdatePaddingBudgetWithSentData(DataSize size);
  DataSize PaddingToAdd(DataSize data_sent) const;
  void OnPacketSent(PacketPriority prio, DataSize size, Timestamp send_time);
  void MaybeUpdateMediaRateDueToLongQueue(Timestamp now);
  Timestamp NextUnpacedSendTime() const;
  Timestamp NextSendTimeAt(Timestamp now) const;
  bool GetPendingPacket(Timestamp target_send_time, Timestamp now, PacedPacket* out);
  void ProcessProbe(Timestamp now, std::vector<PacedPacket>& sent);
  void Account(PacedPacket& p, Timestamp now, std::vector<PacedPacket>& sent);

  // PrioritizedPacketQueue.
  static int Level(PacketPriority p);
  void Push(Timestamp now, PacedPacket p);
  bool Pop(PacedPacket* out, Timestamp now);
  // PrioritizedPacketQueue::RemovePacketsForSsrc for the video stream: the
  // video, FEC and retransmission levels.
  void FlushVideo();
  bool HasKeyframePackets() const;
  void UpdateAverageQueueTime(Timestamp now);
  void SetQueuePauseState(bool paused, Timestamp now);
  TimeDelta AverageQueueTime() const;

  PacingControllerConfig cfg_;
  bool started_ = false;
  Timestamp last_timestamp_ = Timestamp::MinusInfinity();
  bool paused_ = false;
  bool congested_ = false;

  DataSize media_debt_, padding_debt_;
  DataRate pacing_rate_ = DataRate::KilobitsPerSec(750);
  DataRate adjusted_media_rate_ = DataRate::KilobitsPerSec(750);
  DataRate padding_rate_;
  TimeDelta send_burst_interval_;
  TimeDelta queue_time_limit_;
  Timestamp last_process_time_ = Timestamp::MinusInfinity();
  Timestamp last_send_time_ = Timestamp::MinusInfinity();
  bool seen_first_packet_ = false;
  bool boosted_ = false;

  std::array<std::deque<Queued>, kLevels> levels_;
  std::multiset<Timestamp> enqueue_times_;
  DataSize queue_bytes_;
  size_t queue_packets_ = 0;
  TimeDelta queue_time_sum_, pause_time_sum_;
  Timestamp queue_update_time_ = Timestamp::MinusInfinity();
  bool queue_paused_ = false;

  struct ActiveProbe {
    ProbeCluster c;
    int sent_probes = 0;
    int64_t sent_bytes = 0;
  };
  std::deque<ActiveProbe> probes_;
  DataSize probe_debt_;
  uint64_t next_padding_id_ = 1ull << 62;
  std::vector<PacedPacket> flushed_;
  PacerStats stats_;
};

}  // namespace sm::cc
