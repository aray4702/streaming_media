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
// Two versions sit behind one interface (`PacerConfig::version`, issue #70):
//   v1  the library's own pacer (cc/pacer_v1.h): a media-debt leaky bucket
//       with a 5 ms burst allowance; the default.
//   v2  libwebrtc's PacingController and PrioritizedPacketQueue, ported as
//       they are (cc/pacing_controller.h). It is not the default because its
//       results are only meaningful next to the rest of libwebrtc's GoogCC
//       (#61 makes it the default together with the cc v2 controller).
#pragma once

#include "sm/cc/pacer_types.h"
#include "sm/cc/pacer_v1.h"
#include "sm/cc/pacing_controller.h"

namespace sm::cc {

enum class PacerVersion { kV1, kV2 };

struct PacerConfig {
  PacerVersion version = PacerVersion::kV1;
  // v1 (see PacerV1Config).
  TimeDelta max_queue_time = TimeDelta::Millis(2000);
  bool bypass_audio = true;  // Audio is sent immediately, never queued behind budget.
  // Both versions: wire size of one padding packet.
  DataSize padding_packet = DataSize::Bytes(224);
  EventSink* events = nullptr;
  // v2 (see PacingControllerConfig); its padding_packet and events are taken from above.
  PacingControllerConfig v2;
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
  void Pause();
  void Resume();
  bool paused() const;
  // v2 only (PacingController::SetCongested); ignored by v1.
  void SetCongested(bool congested);

  PacerVersion version() const { return v2_on_ ? PacerVersion::kV2 : PacerVersion::kV1; }
  DataSize QueueSize() const;
  size_t QueuePackets() const;
  TimeDelta OldestQueueTime(Timestamp now) const;
  const PacerStats& stats() const;
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  bool v2_on_;
  PacerV1 v1_;
  PacingController v2_;
};

}  // namespace sm::cc
