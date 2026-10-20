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
// their own rate and tagged so the probe estimator can find them (#48): the
// BitrateProber times each burst by the cluster rate, a burst starts with a
// small padding packet, and padding fills a burst when there is no media.
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <set>
#include <vector>

#include "sm/cc/bitrate_prober.h"
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
  // Wire size of the small padding packet that starts every probe burst
  // (GeneratePadding(1 byte): the RTP sender pads at least 50 bytes; plus the
  // 12-byte RTP header, the header extensions and the 28 bytes of UDP/IPv4).
  DataSize probe_start_padding = DataSize::Bytes(102);
  BitrateProberConfig prober;
  int circuit_breaker_iterations = 1 << 16;
  EventSink* events = nullptr;
};

// PrioritizedPacketQueue (modules/pacing/prioritized_packet_queue.cc, M157),
// the pacer's queue. One FIFO per priority level, as GetPriorityForType sets
// them: 0 audio, 1 retransmission, 3 video and FEC (one FIFO), 4 padding.
// Level 2 (video retransmission) is used only with
// prioritize_audio_retransmission, which is off by default, and the
// time-to-live per level (PacketQueueTTL) is infinite by default, so neither
// is ported. A one-to-one call has one stream of each kind, so the round robin
// between the SSRCs of a level is not needed either. Sizes are the packets'
// wire sizes (libwebrtc's SizeInPayloadBytes counts payload and padding only).
// Enqueue times are kept minus the time the queue has spent paused, so that
// AverageQueueTime counts only the time spent unpaused.
class PrioritizedPacketQueue {
 public:
  static constexpr int kNumPriorityLevels = 5;

  void Push(Timestamp enqueue_time, PacedPacket p);
  // The first packet of the highest-priority level; nullopt when empty.
  std::optional<PacedPacket> Pop();
  size_t SizeInPackets() const { return size_packets_; }
  DataSize SizeInBytes() const { return size_bytes_; }
  bool Empty() const { return size_packets_ == 0; }
  // Queued packets per PacketPriority (SizeInPacketsPerRtpPacketMediaType).
  const std::array<size_t, 5>& SizeInPacketsPerPriority() const { return size_per_priority_; }
  // Enqueue time (minus the pause time before it) of the first packet at the
  // level of `p`; MinusInfinity when that level is empty.
  Timestamp LeadingPacketEnqueueTime(PacketPriority p) const;
  // MinusInfinity when empty.
  Timestamp OldestEnqueueTime() const;
  // Mean time the queued packets have spent in the queue while unpaused, as of
  // the last UpdateAverageQueueTime (Push and SetPauseState call it).
  TimeDelta AverageQueueTime() const;
  void UpdateAverageQueueTime(Timestamp now);
  void SetPauseState(bool paused, Timestamp now);
  // RemovePacketsForSsrc for the video stream and its RTX stream, as
  // PacingController::EnqueuePacket calls it on a keyframe: the
  // retransmission, video and FEC levels.
  std::vector<PacedPacket> RemoveVideoStreamPackets();
  bool HasKeyframePackets() const { return num_keyframe_packets_ > 0; }

  static int Level(PacketPriority p);

 private:
  // `enqueue_time` is minus the time the queue spent paused before it.
  struct Queued { PacedPacket p; Timestamp enqueue_time; std::multiset<Timestamp>::iterator oldest_it; };
  void DequeueInternal(Queued& q);

  std::array<std::deque<Queued>, kNumPriorityLevels> levels_;
  std::multiset<Timestamp> enqueue_times_;
  DataSize size_bytes_;
  size_t size_packets_ = 0;
  std::array<size_t, 5> size_per_priority_{};
  int num_keyframe_packets_ = 0;
  TimeDelta queue_time_sum_, pause_time_sum_;
  Timestamp last_update_time_ = Timestamp::MinusInfinity();
  bool paused_ = false;
};

class PacingController : public MetricsSource {
 public:
  explicit PacingController(PacingControllerConfig cfg = {});
  void SetPacingRates(DataRate pacing, DataRate padding);
  // Where padding comes from (PacketRouter::GeneratePadding): keep-alives, the first packet of
  // a probe and the padding rate ask it for packets, and the pacer accounts their real sizes.
  // Unset: fixed-size placeholders. A generator that returns nothing for the padding rate
  // only charges the padding budget (PacingController::ProcessPackets).
  void SetPaddingGenerator(PaddingGenerator g) { padding_generator_ = std::move(g); }
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

  DataSize QueueSize() const { return queue_.SizeInBytes(); }
  size_t QueuePackets() const { return queue_.SizeInPackets(); }
  // Age of the oldest queued packet (zero when empty).
  TimeDelta OldestQueueTime(Timestamp now) const;
  // QueueSizeData() / adjusted rate.
  TimeDelta ExpectedQueueTime() const;
  // PacingController::pacing_rate(): the adjusted rate, after the long-queue boost.
  DataRate pacing_rate() const { return adjusted_media_rate_; }
  // The rate set by SetPacingRates, before the long-queue boost.
  DataRate configured_pacing_rate() const { return pacing_rate_; }
  bool probing() const { return prober_.is_probing(); }
  const PacerStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  void Start(Timestamp now);
  Timestamp Cur(Timestamp now);
  TimeDelta UpdateTimeAndGetElapsed(Timestamp now);
  bool ShouldSendKeepalive(Timestamp now) const;
  void UpdateBudgetWithElapsedTime(TimeDelta delta);
  void UpdateBudgetWithSentData(DataSize size);
  void UpdatePaddingBudgetWithSentData(DataSize size);
  DataSize PaddingToAdd(DataSize recommended_probe_size, DataSize data_sent) const;
  void OnPacketSent(PacketPriority prio, DataSize size, Timestamp send_time);
  void MaybeUpdateMediaRateDueToLongQueue(Timestamp now);
  Timestamp NextUnpacedSendTime() const;
  Timestamp NextSendTimeAt(Timestamp now) const;
  bool GetPendingPacket(const ProbeSendInfo& pacing_info, Timestamp target_send_time, Timestamp now, PacedPacket* out);
  void Account(PacedPacket& p, Timestamp now, const ProbeSendInfo& pacing_info, std::vector<PacedPacket>& sent);
  PacedPacket MakePadding(DataSize size, Timestamp now);

  PacingControllerConfig cfg_;
  PaddingGenerator padding_generator_;
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

  PrioritizedPacketQueue queue_;

  BitrateProber prober_;
  bool probing_send_failure_ = false;
  uint64_t next_padding_id_ = 1ull << 62;
  std::vector<PacedPacket> flushed_;
  PacerStats stats_;
};

}  // namespace sm::cc
