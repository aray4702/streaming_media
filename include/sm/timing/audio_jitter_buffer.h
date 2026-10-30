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
// decode after it. A merge (the packet after a lost one) is joined with
// NetEq's Merge (NetEqMerge): the decoded audio goes in where it correlates
// best with one more expansion, scaled to the expansion's energy and
// crossfaded in. When the sync buffer can't fill the output (packets under
// 10 ms), version 2 plays silence, reads nothing from it and doesn't count
// the frame, as NetEq's kSampleUnderrun (#568). With `enable_muted_state`
// (NetEq::Config's, which libwebrtc's ChannelReceive turns on), once the
// expand has faded to silence (NetEqExpand::Muted) and no packet is waiting,
// version 2 plays a muted frame without running the decision logic or the
// expand: the playout timestamp moves on by the output, the samples count as
// concealed noise, and the sync buffer and decision state stay as they were
// (NetEqImpl::GetAudioInternal's muted branch, #574).
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

// 8000, 16000, 32000 or 48000 Hz, the only rates libwebrtc's NetEq runs at
// (NetEqImpl::SetSampleRateAndChannels RTC_CHECKs them). libwebrtc never
// gives NetEq another rate: the decoder factories refuse such a format
// (AudioDecoderOpus needs a 48 kHz clock, AudioDecoderL16 one of these four),
// so no decoder is registered and NetEqImpl::InsertPacketInternal drops the
// payload type's packets as kUnknownRtpPayloadType.
constexpr bool NetEqSupportsSampleRate(int hz) { return hz == 8000 || hz == 16000 || hz == 32000 || hz == 48000; }

struct AudioJitterBufferConfig {
  // RTP clock rate is assumed equal. Version 2 runs only at the rates
  // NetEqSupportsSampleRate accepts, and discards the packets of any other.
  int sample_rate = 16000;
  int packet_ms = 20;
  // Version 1 only: above this many packets, all but the newest 3 are
  // dropped. Version 2 holds neteq_delay.max_packets_in_buffer, as NetEq.
  size_t max_packets = 100;
  bool time_stretch = true;
  DelayManagerConfig delay;
  // 2 (default, #579): the target delay and the operations as libwebrtc's
  // NetEq. 1: the library's own buffer, kept for tests that pin it.
  int version = 2;
  // Version 2: NetEq::Config::enable_muted_state (off by default, as there;
  // the session's audio receive turns it on, as ChannelReceive does).
  bool enable_muted_state = false;
  NetEqDelayConfig neteq_delay;
  EventSink* events = nullptr;
};

// Field names follow WebRTC getStats() (RTCInboundRtpStreamStats).
struct AudioJitterBufferStats {
  uint64_t packets_received = 0;
  uint64_t packets_discarded_late = 0;
  uint64_t packets_duplicate = 0;
  // Version 2 at a sample rate NetEq does not run at (NetEqSupportsSampleRate).
  uint64_t packets_discarded_unsupported_rate = 0;
  uint64_t packets_lost = 0;                 // Skipped after concealment.
  uint64_t buffer_flushes = 0;
  uint64_t total_samples_received = 0;       // Samples output.
  uint64_t concealed_samples = 0;
  uint64_t concealment_events = 0;
  uint64_t muted_frames = 0;                 // Version 2's muted state (enable_muted_state).
  uint64_t inserted_samples_for_deceleration = 0;
  uint64_t removed_samples_for_acceleration = 0;
  uint64_t ops[6] = {0, 0, 0, 0, 0, 0};      // Indexed by AudioOperation.
  Histogram jitter_buffer_delay_ms;          // Per decoded packet.
  // StatisticsCalculator::JitterBufferDelay: each extracted packet's waiting
  // time in the packet buffer (its tick stopwatch, so whole 10 ms ticks),
  // weighted by its samples, and the samples emitted (jitterBufferDelay and
  // jitterBufferEmittedCount; the sync buffer is not counted).
  uint64_t jitter_buffer_delay_ms_samples = 0;
  uint64_t jitter_buffer_emitted_count = 0;
};

// Packet::Priority (modules/audio_coding/neteq/packet.h): lower is kept,
// first by the codec's level (0 the frame itself, 1 Opus in-band FEC), then
// by the RED level (0 the primary block, 1 the redundant one).
struct AudioPacketPriority {
  int codec_level = 0;
  int red_level = 0;
  bool operator<(const AudioPacketPriority& b) const {
    return codec_level != b.codec_level ? codec_level < b.codec_level : red_level < b.red_level;
  }
};

class AudioJitterBuffer : public MetricsSource {
 public:
  explicit AudioJitterBuffer(AudioJitterBufferConfig cfg = {});

  // With version 2 a packet at a timestamp already held replaces it if its
  // priority is higher, and is discarded otherwise (PacketBuffer::InsertPacket);
  // version 1 keeps the first. A late in-band FEC frame is not counted as a
  // late discard (NetEq counts secondary discards apart).
  void InsertPacket(uint32_t rtp_ts, std::vector<float> pcm, Timestamp arrival, AudioPacketPriority priority = {});
  // Fills `out` (typically 10 ms) and reports the operation performed.
  AudioOperation GetAudio(std::span<float> out, Timestamp now);

  // NetEq::GetPlayoutTimestamp: the RTP timestamp of the audio the last
  // GetAudio played out, unset until playout starts. A/V synchronization reads
  // it (libwebrtc's ChannelReceive::UpdatePlayoutTimestamp).
  std::optional<uint32_t> PlayoutTimestamp() const {
    if (!playout_ts_) return std::nullopt;
    return static_cast<uint32_t>(*playout_ts_);
  }
  // The RTP timestamps of the primary packets (not FEC or RED redundancy) the
  // last GetAudio decoded, in order: NetEqImpl::last_decoded_packet_infos_,
  // which becomes the frame's packet_infos_ (neteq_impl.cc:674, 800-822,
  // 1352-1354).
  const std::vector<uint32_t>& last_decoded_timestamps() const { return last_decoded_timestamps_; }
  int TargetDelayMs() const;
  double CurrentLevelMs() const;
  // Extra minimum delay, e.g. requested by audio/video synchronization.
  // Version 2 returns DelayConstraints::SetMinimumDelay's result.
  bool SetMinimumDelayMs(int ms) {
    min_delay_ms_ = ms;
    return neteq_delay_.SetMinimumDelay(ms) || cfg_.version < 2;
  }
  // NetEqImpl::FilteredCurrentDelayMs (version 2): the filtered packet
  // buffer level plus the decoded audio not yet played, truncated to ms.
  // Version 1 gives its own filtered level.
  int FilteredCurrentDelayMs() const;
  const AudioJitterBufferStats& stats() const { return stats_; }
  const NetEqDelayManager& neteq_delay() const { return neteq_delay_; }  // Version 2's DelayManager.
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  int64_t SpanSamples() const;
  // Decodes the next packet into `dst` (the sync buffer when null).
  AudioOperation DecodeNext(Timestamp now, std::vector<float>* dst = nullptr);
  bool NextPacketReady() const;
  AudioOperation MaybeStretch(Timestamp now);
  AudioOperation GetAudioV1(std::span<float> out, Timestamp now);
  // NetEqImpl::GetAudioInternal and GetDecision (version 2).
  AudioOperation GetAudioV2(std::span<float> out, Timestamp now);
  // Expand::Process on the sync buffer (played_ then sync_); appends to `out`.
  size_t ExpandOnce(std::vector<float>& out);
  // Normal::Process after an expand (version 2): one more expansion to
  // crossfade from, and the decoded audio unmuted from the mute factor.
  void JoinAfterExpand(std::vector<float>& decoded);
  // NetEqImpl::DoMerge (version 2): Merge::Process on the sync buffer;
  // `decoded` becomes the samples to append.
  void MergeAfterExpand(std::vector<float>& decoded, Timestamp now);
  // StatisticsCalculator::ConcealedSamplesCorrection (version 2).
  void AddConcealedSamples(int64_t n);
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
  loss::PacketLossConcealer plc_;  // Version 1's concealment.
  NetEqExpand expand_;
  NetEqMerge merge_;
  NetEqTimeStretch time_stretch_;  // Version 2's accelerate and preemptive expand.
  uint64_t concealed_samples_correction_ = 0;
  SeqNumUnwrapper<uint32_t> unwrapper_;
  struct Packet {
    std::vector<float> pcm;
    Timestamp arrival;
    int64_t insert_tick = 0;  // Packet::waiting_time.
    AudioPacketPriority priority;
  };
  std::map<int64_t, Packet> queue_;
  std::vector<float> sync_;          // Decoded samples waiting for playout.
  int64_t next_ts_ = 0;              // RTP time of the first sample after sync_.
  bool started_ = false;
  std::optional<int64_t> playout_ts_;  // Unwrapped; NetEqImpl::playout_timestamp_.
  bool expanding_ = false;
  bool muted_ = false;  // The last frame was muted (enable_muted_state).
  int64_t expanded_samples_ = 0;
  double filtered_level_ms_ = 0;
  int min_delay_ms_ = 0;
  AudioJitterBufferStats stats_;
  std::vector<float> scratch_;
  // The played part of the sync buffer (version 2): 30 ms for preemptive
  // expand to borrow, and Expand's history.
  std::vector<float> played_;
  std::vector<float> sync_scratch_, decoded_, expanded_;
  std::vector<uint32_t> last_decoded_timestamps_;
};

}  // namespace sm::timing
