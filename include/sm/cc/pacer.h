// 5.5 Packet pacer with priority queues.
//
// Encoders emit a whole frame at once; sending it as a line-rate burst fills
// router queues (bufferbloat) and causes self-inflicted loss. The pacer
// releases packets at the pacing rate (usually 2.5 x the target) and queues
// them per priority so audio is never stuck behind a keyframe and video
// degrades first. Probe clusters are sent at their own rate and tagged so the
// probe estimator can find them; padding fills a probe (or a requested
// padding rate) when there is no media to send.
//
// The pacer is libwebrtc's PacingController and PrioritizedPacketQueue, ported
// as they are (cc/pacing_controller.h, issue #70), run behind
// TaskQueuePacedSender's scheduling. The library's own leaky-bucket pacer
// (`pacer v1`) was deleted in #167.
#pragma once

#include <memory>
#include <vector>

#include "sm/cc/pacer_types.h"
#include "sm/cc/pacing_controller.h"

namespace sm::cc {

struct PacerConfig {
  // Wire size of one padding packet.
  DataSize padding_packet = DataSize::Bytes(224);
  EventSink* events = nullptr;
  // See PacingControllerConfig; its padding_packet and events are taken from above.
  PacingControllerConfig controller;
  // TaskQueuePacedSender's scheduling around the PacingController. It runs a pass only
  // when a packet, a rate or the pause state changed, or when a timer set by the last pass is
  // due, and only if NextSendTime() has come; consecutive timer passes are at least
  // `hold_back_window` apart. Without it every Process() call is a pass, and the early-return
  // branch of ProcessPackets then re-bases the leaky bucket on every call.
  bool task_queue_scheduling = true;
  TimeDelta hold_back_window = TimeDelta::Millis(5);  // RtpTransportControllerSend: 5 ms, 3 packets.
  int hold_back_packets = 3;
};

class Pacer : public MetricsSource {
 public:
  explicit Pacer(PacerConfig cfg = {});
  void SetPacingRates(DataRate pacing, DataRate padding);
  void Enqueue(PacedPacket p);
  // See PacingController::SetPaddingGenerator.
  void SetPaddingGenerator(PaddingGenerator g) { pc_->SetPaddingGenerator(std::move(g)); }
  // See PacingController::SetPacketSender.
  void SetPacketSender(PacketSender s) { pc_->SetPacketSender(std::move(s)); }
  // See PacingController::SetTransportOverhead.
  void SetTransportOverhead(DataSize overhead_per_packet) { pc_->SetTransportOverhead(overhead_per_packet); }
  void AddProbeCluster(const ProbeCluster& c);
  // Sends everything that is due by `now`. Call every few ms or at
  // NextSendTime(). With task_queue_scheduling it runs a pass only when TaskQueuePacedSender would.
  std::vector<PacedPacket> Process(Timestamp now);
  Timestamp NextSendTime(Timestamp now) const;
  // Packets a keyframe flushed from the queue (see PacingController).
  std::vector<PacedPacket> TakeFlushed();
  void Pause();
  void Resume();
  bool paused() const;
  // PacingController::SetCongested.
  void SetCongested(bool congested);

  DataSize QueueSize() const;
  // PacingController::QueueSizeData: the queue without its RTP headers.
  DataSize QueueSizeData() const;
  // PacingController::ExpectedQueueTime: the queue at the adjusted media rate.
  TimeDelta ExpectedQueueTime() const;
  size_t QueuePackets() const;
  TimeDelta OldestQueueTime(Timestamp now) const;
  const PacerStats& stats() const;
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  std::vector<PacedPacket> ProcessScheduled(Timestamp now);
  void MaybeProcessPackets(Timestamp now, Timestamp scheduled_process_time, std::vector<PacedPacket>& out);
  void PostMaybeProcessPackets();

  bool task_queue_;
  TimeDelta hold_back_window_;
  int hold_back_packets_;
  // On the heap: it holds about 4 KB of statistics, which the Session keeps off
  // a small (wasm) stack.
  std::unique_ptr<PacingController> pc_;
  // TaskQueuePacedSender state.
  bool kick_ = true;                                          // A MaybeProcessPackets(-inf) is pending.
  // The last scheduled process time, unrounded (TaskQueuePacedSender::next_process_time_).
  Timestamp next_process_time_ = Timestamp::MinusInfinity();
  // The delayed tasks in flight: when each fires (the delay rounded up to 1 ms) and the process time
  // it carries. A task whose time is no longer next_process_time_ is retired but still processes.
  struct Timer {
    Timestamp fire;
    Timestamp scheduled;
  };
  std::vector<Timer> timers_;  // In the order they fire.
  double packet_size_ = -1;                                   // ExpFilter(0.95) of the enqueued sizes.
};

}  // namespace sm::cc
