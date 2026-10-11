// 4.5 Adaptive audio jitter buffer (a compact NetEq).
//
// Packets (decoded PCM) are inserted on arrival; the audio device pulls a
// fixed block (10 ms) at a time. On each pull the buffer chooses one
// operation:
//   normal            next packet is present and the buffer level is on target
//   accelerate        level well above target: drop one pitch period (WSOLA)
//   preemptive expand level below target: add one pitch period (WSOLA)
//   expand            next packet missing: synthesize audio with PLC
//   merge             packet arrives after an expand: crossfade back in
// The target delay comes from DelayManager (95th percentile of relative
// arrival delay), so the buffer tracks the network's jitter instead of
// using a fixed size.
#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <vector>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/seq_num.h"
#include "sm/loss/plc.h"
#include "sm/timing/delay_manager.h"
#include "sm/timing/time_stretch.h"

namespace sm::timing {

enum class AudioOperation { kSilence, kNormal, kAccelerate, kPreemptiveExpand, kExpand, kMerge };
const char* ToString(AudioOperation op);

struct AudioJitterBufferConfig {
  int sample_rate = 16000;  // RTP clock rate is assumed equal.
  int packet_ms = 20;
  size_t max_packets = 100;
  bool time_stretch = true;
  DelayManagerConfig delay;
  EventSink* events = nullptr;
};

// Field names follow WebRTC getStats() (RTCInboundRtpStreamStats).
struct AudioJitterBufferStats {
  uint64_t packets_received = 0;
  uint64_t packets_discarded_late = 0;
  uint64_t packets_duplicate = 0;
  uint64_t packets_lost = 0;                 // Skipped after concealment.
  uint64_t buffer_flushes = 0;
  uint64_t total_samples_received = 0;       // Samples output.
  uint64_t concealed_samples = 0;
  uint64_t concealment_events = 0;
  uint64_t inserted_samples_for_deceleration = 0;
  uint64_t removed_samples_for_acceleration = 0;
  uint64_t ops[6] = {0, 0, 0, 0, 0, 0};      // Indexed by AudioOperation.
  Histogram jitter_buffer_delay_ms;          // Per decoded packet.
};

class AudioJitterBuffer : public MetricsSource {
 public:
  explicit AudioJitterBuffer(AudioJitterBufferConfig cfg = {});

  void InsertPacket(uint32_t rtp_ts, std::vector<float> pcm, Timestamp arrival);
  // Fills `out` (typically 10 ms) and reports the operation performed.
  AudioOperation GetAudio(std::span<float> out, Timestamp now);

  int TargetDelayMs() const;
  double CurrentLevelMs() const;
  // Extra minimum delay, e.g. requested by audio/video synchronization.
  void SetMinimumDelayMs(int ms) { min_delay_ms_ = ms; }
  const AudioJitterBufferStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  int64_t SpanSamples() const;
  AudioOperation DecodeNext(Timestamp now);
  bool NextPacketReady() const;
  AudioOperation MaybeStretch(Timestamp now);

  AudioJitterBufferConfig cfg_;
  DelayManager delay_;
  TimeStretcher stretcher_;
  loss::PacketLossConcealer plc_;
  SeqNumUnwrapper<uint32_t> unwrapper_;
  struct Packet {
    std::vector<float> pcm;
    Timestamp arrival;
  };
  std::map<int64_t, Packet> queue_;
  std::vector<float> sync_;          // Decoded samples waiting for playout.
  int64_t next_ts_ = 0;              // RTP time of the first sample after sync_.
  bool started_ = false;
  bool expanding_ = false;
  int64_t expanded_samples_ = 0;
  double filtered_level_ms_ = 0;
  int min_delay_ms_ = 0;
  AudioJitterBufferStats stats_;
  std::vector<float> scratch_;
};

}  // namespace sm::timing
