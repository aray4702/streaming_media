// 4.5 Adaptive audio jitter buffer: libwebrtc M157's NetEq (issue #62).
//
// Packets (decoded PCM, or encoded frames that are decoded when played, as
// NetEq's DecodeLoop does) are inserted on arrival; the audio device pulls a
// fixed block (10 ms) at a time. On each pull the buffer chooses one
// operation:
//   normal            next packet is present and the buffer level is on target
//   accelerate        level well above target: drop one pitch period
//   preemptive expand level below target: add one pitch period
//   expand            next packet missing: synthesize audio with NetEq's Expand
//   merge             packet arrives after an expand: join it with NetEq's Merge
// The target delay comes from NetEqDelayManager, libwebrtc M157's NetEq
// DelayManager and its packet-arrival history, and the operation is chosen
// as NetEq's DecisionLogic and NetEqImpl::GetDecision do
// (NetEqDecisionLogic): playout starts with the first packet; accelerate
// when the playout delay of the arrival history reaches the target plus
// its largest arrival delay plus 20 ms, preemptive expand below the target,
// at most one of them every 5 ticks; decoding is postponed after an expand
// while the buffer holds less than half the target; and an expand ends in
// a merge once it has covered the gap to the next packet. Concealment is
// NetEq's Expand (NetEqExpand): whole pitch periods until the sync buffer
// holds the output plus Expand's overlap, the mute factor that
// DecisionLogic reads, and Normal's unmute and 1 ms crossfade on the first
// decode after it. A merge (the packet after a lost one) is joined with
// NetEq's Merge (NetEqMerge): the decoded audio goes in where it correlates
// best with one more expansion, scaled to the expansion's energy and
// crossfaded in. When the sync buffer can't fill the output (packets under
// 10 ms), the buffer plays silence, reads nothing from it and doesn't count
// the frame, as NetEq's kSampleUnderrun (#568). With `enable_muted_state`
// (NetEq::Config's, which libwebrtc's ChannelReceive turns on), once the
// expand has faded to silence (NetEqExpand::Muted) and no packet is waiting,
// the buffer plays a muted frame without running the decision logic or the
// expand: the playout timestamp moves on by the output, the samples count as
// concealed noise, and the sync buffer and decision state stay as they were
// (NetEqImpl::GetAudioInternal's muted branch, #574).
//
// The library's own buffer (`version = 1`: DelayManager, WSOLA and
// loss::PacketLossConcealer) was removed in #817; no default path used it
// since #579.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <vector>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/seq_num.h"
#include "sm/timing/neteq_delay.h"
#include "sm/timing/neteq_expand.h"

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
  // RTP clock rate is assumed equal. The buffer runs only at the rates
  // NetEqSupportsSampleRate accepts, and discards the packets of any other.
  int sample_rate = 16000;
  int packet_ms = 20;
  // Off: no accelerate or preemptive expand (tests that pin the level).
  bool time_stretch = true;
  // NetEq::Config::enable_muted_state (off by default, as there; the
  // session's audio receive turns it on, as ChannelReceive does).
  bool enable_muted_state = false;
  NetEqDelayConfig neteq_delay;
  EventSink* events = nullptr;
};

// Field names follow WebRTC getStats() (RTCInboundRtpStreamStats).
struct AudioJitterBufferStats {
  uint64_t packets_received = 0;
  uint64_t packets_discarded_late = 0;
  uint64_t packets_duplicate = 0;
  // At a sample rate NetEq does not run at (NetEqSupportsSampleRate).
  uint64_t packets_discarded_unsupported_rate = 0;
  uint64_t packets_lost = 0;                 // Skipped after concealment.
  uint64_t buffer_flushes = 0;
  uint64_t flush_buffers_calls = 0;  // FlushBuffers calls (the app's flush, e.g. on barge-in).
  uint64_t total_samples_received = 0;       // Samples output.
  uint64_t concealed_samples = 0;
  uint64_t concealment_events = 0;
  uint64_t muted_frames = 0;                 // The muted state (enable_muted_state).
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

  // A packet at a timestamp already held replaces it if its priority is
  // higher, and is discarded otherwise (PacketBuffer::InsertPacket). A late
  // in-band FEC frame is not counted as a late discard (NetEq counts
  // secondary discards apart).
  void InsertPacket(uint32_t rtp_ts, std::vector<float> pcm, Timestamp arrival, AudioPacketPriority priority = {});
  // A frame still encoded, `samples` long (AudioDecoder::PacketDuration), that
  // the buffer decodes only when it plays it, as NetEq's DecodeLoop decodes
  // each packet it extracts, in timestamp order (neteq_impl.cc). A stateful
  // decoder (Opus) then sees the frames NetEq's sees, in NetEq's order: the
  // copies a higher priority replaced and the late packets discarded are
  // never decoded (#783). `decode` returns the frame's samples, empty on a
  // decode error, which plays `samples` of silence.
  using FrameDecoder = std::function<std::vector<float>()>;
  void InsertPacket(uint32_t rtp_ts, size_t samples, FrameDecoder decode, Timestamp arrival,
                    AudioPacketPriority priority = {});
  // Fills `out` (typically 10 ms) and reports the operation performed.
  AudioOperation GetAudio(std::span<float> out, Timestamp now);
  // NetEq::FlushBuffers (api/neteq/neteq.h:327-328; NetEqImpl::FlushBuffers,
  // neteq_impl.cc:616-626): drops the queued packets (PacketBuffer::Flush,
  // counted as a packet buffer flush) and the decoded audio not yet played
  // (SyncBuffer::Flush: history and future zeroed, Expand's overlap of zeros
  // left as lookahead), and waits for a new first packet. Until it arrives
  // GetAudio expands the zeroed history (silence) and there is no playout
  // timestamp; that packet then restarts playout at its own timestamp, as
  // after a full-buffer flush (new_codec_ at the next decision).
  void FlushBuffers(Timestamp now);

  // NetEq::GetPlayoutTimestamp: the RTP timestamp of the audio the last
  // GetAudio played out, unset until playout starts. A/V synchronization reads
  // it (libwebrtc's ChannelReceive::UpdatePlayoutTimestamp).
  std::optional<uint32_t> PlayoutTimestamp() const {
    if (!playout_ts_ || first_packet_) return std::nullopt;
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
  // Returns DelayConstraints::SetMinimumDelay's result.
  bool SetMinimumDelayMs(int ms) { return neteq_delay_.SetMinimumDelay(ms); }
  // NetEq::SetMaximumDelay (neteq_impl.cc:508): DelayConstraints::SetMaximumDelay's result.
  bool SetMaximumDelayMs(int ms) { return neteq_delay_.SetMaximumDelay(ms); }
  // NetEqImpl::FilteredCurrentDelayMs: the filtered packet buffer level plus
  // the decoded audio not yet played, truncated to ms.
  int FilteredCurrentDelayMs() const;
  const AudioJitterBufferStats& stats() const { return stats_; }
  const NetEqDelayManager& neteq_delay() const { return neteq_delay_; }  // NetEq's DelayManager.
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  int64_t SpanSamples() const;
  // Decodes the next packet into `dst` (the sync buffer when null).
  AudioOperation DecodeNext(Timestamp now, std::vector<float>* dst = nullptr);
  bool NextPacketReady() const;
  // PacketBuffer::DiscardOldPackets(limit, horizon).
  void DiscardOldPackets(int64_t limit, int64_t horizon_samples, Timestamp now);
  // NetEqImpl::GetAudioInternal and GetDecision.
  AudioOperation GetAudioInternal(std::span<float> out, Timestamp now);
  // Expand::Process on the sync buffer (played_ then sync_); appends to `out`.
  size_t ExpandOnce(std::vector<float>& out);
  // Normal::Process after an expand: one more expansion to
  // crossfade from, and the decoded audio unmuted from the mute factor.
  void JoinAfterExpand(std::vector<float>& decoded);
  // NetEqImpl::DoMerge: Merge::Process on the sync buffer;
  // `decoded` becomes the samples to append.
  void MergeAfterExpand(std::vector<float>& decoded, Timestamp now);
  // StatisticsCalculator::ConcealedSamplesCorrection.
  void AddConcealedSamples(int64_t n);
  // PacketBuffer::GetSpanSamples.
  int64_t PacketSpanSamples(bool count_waiting_time) const;

  AudioJitterBufferConfig cfg_;
  NetEqDelayManager neteq_delay_;
  NetEqDecisionLogic decision_;
  NetEqMode last_mode_ = NetEqMode::kNormal;
  std::optional<int64_t> noise_start_tick_;  // NetEqImpl::generated_noise_stopwatch_.
  // NetEq's TickTimer: 10 ms per GetAudio call.
  static constexpr int64_t kMsPerTick = 10;
  int64_t ticks_ = 0;
  bool neteq_reset_done_ = false;
  // NetEqImpl::new_codec_ after PacketBuffer::kFlushed: set by a
  // full-buffer flush, cleared by the soft reset at the next decision.
  bool neteq_flush_pending_ = false;
  // NetEqImpl::first_packet_ after FlushBuffers: the next inserted packet sets
  // new_codec_ (neteq_impl.cc:395-398). Not set at construction here: the
  // first packet of the call is handled by `started_` and the initial reset.
  bool first_packet_ = false;
  NetEqExpand expand_;
  NetEqMerge merge_;
  NetEqTimeStretch time_stretch_;  // Accelerate and preemptive expand.
  uint64_t concealed_samples_correction_ = 0;
  SeqNumUnwrapper<uint32_t> unwrapper_;
  struct Packet {
    std::vector<float> pcm;
    Timestamp arrival;
    int64_t insert_tick = 0;  // Packet::waiting_time.
    AudioPacketPriority priority;
    size_t samples = 0;   // The frame's duration: pcm.size(), or before `decode` runs.
    FrameDecoder decode;  // Set until the frame is decoded at playout.
  };
  void Insert(uint32_t rtp_ts, Packet pkt);
  std::map<int64_t, Packet> queue_;
  std::vector<float> sync_;          // Decoded samples waiting for playout.
  int64_t next_ts_ = 0;              // RTP time of the first sample after sync_.
  bool started_ = false;
  std::optional<int64_t> playout_ts_;  // Unwrapped; NetEqImpl::playout_timestamp_.
  bool expanding_ = false;
  bool muted_ = false;  // The last frame was muted (enable_muted_state).
  int64_t expanded_samples_ = 0;
  AudioJitterBufferStats stats_;
  std::vector<float> scratch_;
  // The played part of the sync buffer: 30 ms for preemptive
  // expand to borrow, and Expand's history.
  std::vector<float> played_;
  std::vector<float> sync_scratch_, decoded_, expanded_;
  std::vector<uint32_t> last_decoded_timestamps_;
};

}  // namespace sm::timing
