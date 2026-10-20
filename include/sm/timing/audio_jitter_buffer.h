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
// using a fixed size. With `version = 2` (the test bench's `neteq v2`,
// issue #62) it comes from NetEqDelayManager, libwebrtc M157's NetEq
// DelayManager and its packet-arrival history, instead, and the operation
// is chosen as NetEq's DecisionLogic and NetEqImpl::GetDecision do
// (NetEqDecisionLogic): playout starts with the first packet; accelerate
// when the playout delay of the arrival history reaches the target plus
// its largest arrival delay plus 20 ms, preemptive expand below the target,
// at most one of them every 5 ticks; decoding is postponed after an expand
// while the buffer holds less than half the target; and an expand ends in
// a merge once it has covered the gap to the next packet. Version 2
// conceals with NetEq's Expand (NetEqExpand): whole pitch periods until the
// sync buffer holds the output plus Expand's overlap, the mute factor that
// DecisionLogic reads, and Normal's unmute and 1 ms crossfade on the first
// decode after it. Merge's alignment search is not ported yet: a merge is
// joined the way Normal joins a decode after an expand.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/seq_num.h"
#include "sm/loss/plc.h"
#include "sm/timing/delay_manager.h"
#include "sm/timing/neteq_delay.h"
#include "sm/timing/neteq_expand.h"
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
  int version = 1;  // 2: the target delay and the operations as libwebrtc's NetEq.
  NetEqDelayConfig neteq_delay;
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
  void SetMinimumDelayMs(int ms) {
    min_delay_ms_ = ms;
    neteq_delay_.SetMinimumDelay(ms);
  }
  const AudioJitterBufferStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  int64_t SpanSamples() const;
  // Decodes the next packet into `dst` (the sync buffer when null).
  AudioOperation DecodeNext(Timestamp now, std::vector<float>* dst = nullptr);
  bool NextPacketReady() const;
  AudioOperation MaybeStretch(Timestamp now);
  // NetEqImpl::GetAudioInternal and GetDecision (version 2).
  AudioOperation GetAudioV2(std::span<float> out, Timestamp now);
  // Expand::Process on the sync buffer (played_ then sync_); appends to `out`.
  size_t ExpandOnce(std::vector<float>& out);
  // Normal::Process after an expand (version 2): one more expansion to
  // crossfade from, and the decoded audio unmuted from the mute factor.
  void JoinAfterExpand(std::vector<float>& decoded, bool merge);
  // PacketBuffer::GetSpanSamples.
  int64_t PacketSpanSamples(bool count_waiting_time) const;

  AudioJitterBufferConfig cfg_;
  DelayManager delay_;
  NetEqDelayManager neteq_delay_;
  NetEqDecisionLogic decision_;
  NetEqMode last_mode_ = NetEqMode::kNormal;
  std::optional<int64_t> noise_start_tick_;  // NetEqImpl::generated_noise_stopwatch_.
  // NetEq's TickTimer: 10 ms per GetAudio call (version 2).
  static constexpr int64_t kMsPerTick = 10;
  int64_t ticks_ = 0;
  bool neteq_reset_done_ = false;
  TimeStretcher stretcher_;
  loss::PacketLossConcealer plc_;
  NetEqExpand expand_;
  SeqNumUnwrapper<uint32_t> unwrapper_;
  struct Packet {
    std::vector<float> pcm;
    Timestamp arrival;
    int64_t insert_tick = 0;  // Packet::waiting_time.
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
  // The played part of the sync buffer (version 2): 30 ms for preemptive
  // expand to borrow, and Expand's history.
  std::vector<float> played_;
  std::vector<float> sync_scratch_, decoded_, expanded_;
};

}  // namespace sm::timing
