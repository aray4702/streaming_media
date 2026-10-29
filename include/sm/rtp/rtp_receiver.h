// RTP receiver of one media stream (docs/design/rtp-session.md, sections 4.3,
// 5 and 6.4): takes datagrams as bytes and does what the test bench's
// receiver glue does today.
//
// Video: parse the RTP packet, restore RTX (RFC 4588), take RED apart and
// recover with ULPFEC (RFC 2198, RFC 5109) as RtpVideoStreamReceiver2, track
// gaps for NACK, assemble frames in the packet buffer, request keyframes
// (PLI, FIR, LNTF), and time and schedule decodable frames with the jitter
// estimator, the timing model and the frame scheduler. Audio: unwrap RED
// (RFC 2198), decode through the application's AudioDecoder and insert into
// the jitter buffer.
//
// Nothing is sent from here. Feedback comes out through TakeFeedback() for
// the caller to queue on rtp::RtcpEngine, and received frames through
// TakeFrames(). A VideoPayloadParser tells the receiver where a frame starts
// and what its packets hold (rtp::H264PayloadParser for H.264). Frame ids and
// references come from sequence numbers, not from the bitstream (design 6.4):
// a frame's id is the unwrapped sequence number of its last packet, and a
// delta frame depends on the frame whose last packet precedes its first.
// Takes `now`, reads no clock and starts no thread.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <map>
#include <optional>
#include <span>
#include <unordered_set>
#include <vector>

#include "sm/cc/transport_feedback.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/rtp_packet.h"
#include "sm/core/seq_num.h"
#include "sm/core/units.h"
#include "sm/loss/keyframe_request.h"
#include "sm/loss/nack_tracker.h"
#include "sm/loss/red.h"
#include "sm/loss/ulpfec.h"
#include "sm/rtp/sps_vui_rewriter.h"
#include "sm/sync/header_extensions.h"
#include "sm/video/orientation.h"
#include "sm/sync/stream_sync.h"
#include "sm/timing/audio_jitter_buffer.h"
#include "sm/timing/frame_release.h"
#include "sm/timing/packet_buffer.h"
#include "sm/timing/rtcp_stats.h"
#include "sm/timing/vcm_timing.h"
#include "sm/timing/video_jitter_estimator.h"
#include "sm/video/frame_scheduler.h"
#include "sm/video/render_frames.h"

namespace sm::rtp {

// One NAL unit of an H.264 payload: libwebrtc's NaluInfo
// (RTPVideoHeaderH264::nalus). -1 is an id the unit doesn't carry or that
// wasn't parsed (an FU-A fragment of an SPS or PPS).
struct VideoNaluInfo {
  uint8_t type = 0;
  int32_t sps_id = -1;
  int32_t pps_id = -1;
};

// What the receiver needs to know about one video RTP packet.
struct VideoPayloadInfo {
  bool valid = false;        // False: malformed payload, the packet is dropped.
  bool frame_start = false;  // The packet can start a frame (see PacketBuffer).
  bool idr = false;          // Carries (the start of) an IDR slice: picture data of a keyframe.
  bool sps = false;          // Carries a sequence parameter set.
  bool pps = false;          // Carries a picture parameter set.
  bool keyframe = false;     // The depacketizer's frame type is key (the packet belongs to a keyframe).
  std::vector<VideoNaluInfo> nalus;  // H.264: the units the depacketizer read, in order (empty for others).
};

// libwebrtc's H264SpsPpsTracker::CopyAndFixBitstream
// (modules/video_coding/h264_sps_pps_tracker.cc:37-110), the decision part:
// SPS and PPS are recorded by id (a PPS with the SPS id it refers to), and the
// first packet of a frame that holds an IDR slice is let through only when the
// slice's PPS and that PPS's SPS have been received. Units are handled in
// order, so a STAP-A [SPS, PPS, IDR] passes. The rebuilt bitstream is
// H264Depacketizer::Reassemble's. Parameter sets received out of band
// (InsertSpsPpsNalus) are not supported, so the SPS resolution and bitstream
// libwebrtc keeps per id are not kept.
class H264SpsPpsTracker {
 public:
  enum class Action { kInsert, kDrop, kRequestKeyframe };
  // kDrop: an SPS or PPS whose ids are missing or out of bounds (an FU-A
  // fragment of a parameter set). kRequestKeyframe: an IDR at the start of a
  // frame whose parameter sets are unknown.
  Action CopyAndFixBitstream(std::span<const VideoNaluInfo> nalus, bool first_packet_in_frame);

 private:
  std::array<bool, kH264MaxSpsId + 1> sps_{};  // sps_data_: the SPS ids received.
  std::array<int16_t, kH264MaxPpsId + 1> pps_sps_id_ = MakeNoPps();  // pps_data_: PPS id -> SPS id, -1 none.
  static std::array<int16_t, kH264MaxPpsId + 1> MakeNoPps() {
    std::array<int16_t, kH264MaxPpsId + 1> a{};
    a.fill(-1);
    return a;
  }
};

class VideoPayloadParser {
 public:
  virtual ~VideoPayloadParser() = default;
  virtual VideoPayloadInfo Parse(const RtpPacket& p) const = 0;
  // Appends the bitstream of a frame from its packets, in order. False if it
  // cannot (not supported, or the packets are inconsistent): the frame is still
  // delivered, without a bitstream.
  virtual bool Reassemble(std::span<const RtpPacket>, std::vector<uint8_t>&) const { return false; }
};

// Told of every video media packet the receiver accepts: the original, an RTX
// restore or an FEC recovery. The test bench uses it to measure residual loss.
class VideoMediaObserver {
 public:
  virtual ~VideoMediaObserver() = default;
  virtual void OnVideoMediaPacket(uint16_t seq, Timestamp now) = 0;
};

// Audio decoding stays with the application (design section 4.2): the jitter
// buffer holds decoded samples.
class AudioDecoder {
 public:
  virtual ~AudioDecoder() = default;
  // One frame (an Opus packet, or the primary or redundant block of a RED
  // packet) to samples at the jitter buffer's sample rate. Nullopt drops it.
  virtual std::optional<std::vector<float>> Decode(std::span<const uint8_t> frame, uint32_t rtp_timestamp) = 0;
  // In-band FEC (Opus LBRR), as AudioDecoderOpusImpl::ParsePayload reads it
  // (modules/audio_coding/codecs/opus/audio_decoder_opus.cc): the RTP
  // timestamp units of the redundant copy of the previous frame that `frame`
  // carries, 0 when it carries none (PacketHasFec / PacketDurationRedundant).
  // Used only with the jitter buffer's version 2 (`neteq v2`).
  virtual uint32_t FecDuration(std::span<const uint8_t> /*frame*/) const { return 0; }
  // DecodeRedundantInternal: that copy to samples, for `rtp_timestamp` (the
  // frame's own timestamp less FecDuration). Nullopt drops it.
  virtual std::optional<std::vector<float>> DecodeFec(std::span<const uint8_t> /*frame*/, uint32_t /*rtp_timestamp*/) {
    return std::nullopt;
  }
};

struct RtpReceiverConfig {
  uint32_t ssrc = 1;  // Media SSRC to receive.
  uint8_t payload_type = 96;
  int clock_rate = 90000;  // 90000 video, the jitter buffer's sample rate for audio.
  bool video = true;

  // Reads the transport-wide sequence number of every packet (RTX, FEC and
  // padding included) into `feedback`. Both not owned; either may be null.
  const sync::ExtensionMap* extensions = nullptr;
  cc::TransportFeedbackBuilder* feedback = nullptr;

  // Receive statistics as libwebrtc's StreamStatisticianImpl (timing::ReceiveStatistics v2,
  // issue #575): the max reordering threshold (450, kMaxPacketAgeToNack, for video with NACK;
  // 50 otherwise), the stream-restart rule, 255-scaled fraction lost and the clamp of a
  // negative cumulative loss. Off by default until the flip is judged on the parity report.
  bool receive_statistics_v2 = false;

  // --- Video ---
  const VideoPayloadParser* parser = nullptr;  // Required for video. Not owned.
  VideoMediaObserver* media_observer = nullptr;  // Optional. Not owned.
  std::optional<uint32_t> rtx_ssrc;
  bool nack = true;  // Track gaps and ask for retransmissions.
  // A keyframe needs SPS and PPS as well as the IDR slice (libwebrtc's
  // sps-pps-idr-in-keyframe mode). Off: an IDR alone is a keyframe, which is
  // libwebrtc's default.
  bool keyframe_needs_parameter_sets = false;
  // libwebrtc's H264SpsPpsTracker::CopyAndFixBitstream, as
  // RtpVideoStreamReceiver2::OnReceivedPayloadData runs it on every H.264
  // packet (video/rtp_video_stream_receiver2.cc:924-941): a packet that
  // starts an IDR slice before its PPS and that PPS's SPS have been received
  // is dropped and a keyframe is requested (kRequestKeyframe); a parameter set
  // without its ids (an FU-A fragment of an SPS or PPS) is dropped (kDrop). The NACK tracker has already counted
  // it as received, so it is not retransmitted. On by default, as libwebrtc
  // runs it for every H.264 packet (issues #453, #580); it acts only on the
  // IDR, SPS and PPS flags an H.264 parser sets, and rtp::Session applies it to
  // H.264 streams only.
  bool sps_pps_tracker = true;
  // libwebrtc's pre-render smoothing (IncomingVideoStream + VideoRenderFrames,
  // video::RenderFrames), which VideoReceiveStream2 runs between the decoder and
  // the renderer when `enable_prerenderer_smoothing` is on, its default: a decoded
  // frame waits until `timing.render_delay` before its render time, and of several
  // due at once only the newest is shown. Frames it drops come out with
  // `render` false. Off (today's behaviour) by default until it is judged against
  // libwebrtc (issue #437).
  bool render_smoothing = false;
  bool deliver_bitstream = true;  // Rebuild each frame's bitstream (ReceivedFrame::bitstream).
  // Video RED + ULPFEC on the media SSRC, with `ulpfec`'s payload types.
  bool fec = false;
  loss::ulpfec::UlpfecConfig ulpfec;
  loss::NackConfig nack_config;
  timing::PacketBufferConfig packet_buffer;
  loss::KeyframeRequestConfig keyframe;
  video::FrameSchedulerConfig scheduler;
  timing::VideoTimingConfig timing;  // timing.version 2: libwebrtc's VCMTiming and frame release.
  TimeDelta decode_time = TimeDelta::Millis(5);  // Fed to the timing model per frame.

  // --- Audio ---
  AudioDecoder* decoder = nullptr;  // Required for audio. Not owned.
  bool red = false;                 // The stream is wrapped in RED (audio/red).
  timing::AudioJitterBufferConfig jitter_buffer;  // sample_rate is overwritten with clock_rate.

  EventSink* events = nullptr;
};

// What OnPacket made of a datagram, for the caller's accounting.
enum class ReceivedKind : uint8_t { kMalformed, kMedia, kRtx, kFec, kPadding, kUnknown };

// A frame the scheduler decided on. `render` is false for frames it dropped
// as too late (timing v1: decoded, not shown; timing v2: not decoded).
struct ReceivedFrame {
  int64_t frame_id = 0;
  uint32_t rtp_timestamp = 0;
  bool keyframe = false;
  bool render = false;
  Timestamp render_time;
  TimeDelta lateness;  // Positive = late.
  size_t size_bytes = 0;  // RTP payload bytes of the frame's packets.
  std::vector<uint8_t> bitstream;  // The access unit (Annex-B for H.264); empty if not rebuilt.
  // The last packet's video-orientation extension (CVO), 0 without it, as libwebrtc's
  // RtpVideoStreamReceiver2 (rtp_video_stream_receiver2.cc:797-802, 1108).
  video::Rotation rotation = video::Rotation::k0;
};

// Feedback due toward the sender, for rtp::RtcpEngine.
struct ReceiverFeedback {
  std::vector<uint16_t> nacks;                  // One NACK batch.
  std::vector<loss::FeedbackMessage> keyframe;  // PLI, FIR or LNTF from the keyframe requester.
  bool empty() const { return nacks.empty() && keyframe.empty(); }
};

struct RtpReceiverStats {
  uint64_t packets = 0;  // Parsed datagrams.
  uint64_t malformed = 0;
  uint64_t media = 0;
  uint64_t rtx = 0;
  uint64_t rtx_unrestorable = 0;
  uint64_t fec = 0;
  uint64_t padding = 0;
  uint64_t unknown_ssrc = 0;
  uint64_t invalid_payload = 0;
  uint64_t frames_received = 0;   // Assembled frames.
  uint64_t frames_scheduled = 0;  // Decodable frames handed to the scheduler.
  uint64_t frame_meta_evicted = 0;  // Assembled frames that never became decodable.
  uint64_t reassembly_failures = 0;  // Frames whose packets did not rebuild into a bitstream.
  uint64_t idr_without_parameter_sets = 0;  // sps_pps_tracker: IDR starts dropped before their SPS and PPS.
  uint64_t parameter_sets_without_ids = 0;  // sps_pps_tracker: SPS/PPS units without ids dropped (kDrop).
  // Gaps in the media sequence numbers as they arrive (recovered packets
  // included), for the protection controller (a real receiver reports this in
  // RTCP XR): runs of missing packets, and the packets in them.
  uint64_t gaps = 0;
  uint64_t missing = 0;
  uint64_t audio_frames = 0;
  uint64_t audio_decode_failures = 0;
  uint64_t audio_fec_frames = 0;       // In-band FEC frames given to the jitter buffer (version 2).
};

class RtpReceiver : public MetricsSource {
 public:
  explicit RtpReceiver(RtpReceiverConfig cfg = {});

  // Stream start: the keyframe requester treats the first frame as wanted.
  void Start(Timestamp now) { keyframes_.OnStreamStart(now); }

  // One datagram addressed to this stream (the session demultiplexes by
  // SSRC, MID and payload type).
  ReceivedKind OnPacket(std::span<const uint8_t> datagram, Timestamp now);

  // Video: assembles frames, runs the keyframe requester and NACK tracker,
  // and schedules decodable frames. Cheap to call every tick.
  void Process(Timestamp now);
  // When Process must run next (video): the earliest of the frame release's decode time, the render stage's
  // dequeue, the keyframe requester's decode time-out and the NACK tracker's tick, as libwebrtc posts each of
  // these as a delayed task for its own time. Never before `now`; PlusInfinity for audio.
  Timestamp NextProcessTime(Timestamp now) const;
  // Sender report from RtcpEngine (RtcpIncoming::sender_reports).
  void OnSenderReport(const timing::SenderReport& sr, Timestamp now) {
    stats_rtp_.OnSenderReport(sr, now);
    last_sr_ = sr;
  }
  // A/V synchronization (issue #77), what the stream gives
  // sync::RtpStreamsSynchronizer as a Syncable. Video is libwebrtc's
  // VideoReceiveStream2::GetInfo (RtpVideoStreamReceiver2::GetSyncInfo: the
  // last SR and the last media packet, with VCMTiming::TargetVideoDelay as
  // the current delay; timing v2 only). Audio is AudioReceiveStreamImpl::
  // GetInfo (ChannelReceive::GetSyncInfo, with GetDelayEstimate =
  // NetEq::FilteredCurrentDelayMs, no playout device delay). Unset until an
  // SR and a media packet have arrived.
  std::optional<sync::Syncable::Info> SyncInfo() const;
  // The synchronizer's minimum playout delay. Video is VideoReceiveStream2::
  // SetMinimumPlayoutDelay (the largest of it and the configured minimum goes
  // to VCMTiming); audio is ChannelReceive::SetMinimumPlayoutDelay (clamped
  // to 0-10 s, NetEq::SetMinimumDelay). False when the delay is not applied.
  bool SetSyncMinimumPlayoutDelay(TimeDelta delay);
  // ChannelReceive::GetPlayoutRtpTimestamp: the RTP timestamp of the audio
  // last played and when (audio only).
  std::optional<sync::Syncable::PlayoutInfo> PlayoutRtpTimestamp() const;
  void OnRtt(TimeDelta rtt);
  // The decoder failed: ask for a keyframe.
  void OnDecodeFailure(Timestamp now) { keyframes_.OnDecoderError(now); }

  std::vector<ReceivedFrame> TakeFrames();
  ReceiverFeedback TakeFeedback();
  // Audio: the device pulls 10 ms.
  timing::AudioOperation GetAudio(std::span<float> out, Timestamp now) {
    last_playout_time_ = now;
    return jitter_buffer_.GetAudio(out, now);
  }

  // Report block source for rtp::RtcpEngine::AddReceiveStatistics.
  timing::ReceiveStatistics* receive_statistics() { return &stats_rtp_; }
  // The RTX SSRC's own statistics (RtxReceiveStream counts every packet on it, padding included,
  // in the receive statistics, video_receive_stream2.cc:313-316); null without RTX.
  timing::ReceiveStatistics* rtx_receive_statistics() { return stats_rtx_ ? &*stats_rtx_ : nullptr; }

  const RtpReceiverConfig& config() const { return cfg_; }
  const RtpReceiverStats& stats() const { return stats_; }
  const loss::NackTracker& nack() const { return nack_; }
  // The ULPFEC receiver, when FEC is on.
  const loss::ulpfec::UlpfecReceiver* ulpfec() const { return ulpfec_.get(); }
  const loss::RedReceiver& red() const { return red_; }
  const timing::PacketBuffer& packet_buffer() const { return packet_buffer_; }
  const loss::KeyframeRequester& keyframes() const { return keyframes_; }
  const video::FrameScheduler& scheduler() const { return scheduler_; }
  const video::RenderFrames& render_frames() const { return render_frames_; }
  const timing::VideoJitterEstimator& video_jitter() const { return video_jitter_; }
  const timing::VideoTiming& video_timing() const { return video_timing_; }
  const timing::VcmTiming& vcm_timing() const { return vcm_timing_; }
  const timing::FrameReleaseController& frame_release() const { return release_; }
  const timing::AudioJitterBuffer& jitter_buffer() const { return jitter_buffer_; }
  timing::AudioJitterBuffer& jitter_buffer() { return jitter_buffer_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  // A video packet on the media SSRC, received or restored from RTX: RED goes to the ULPFEC
  // receiver (RtpVideoStreamReceiver2::ParseAndHandleEncapsulatingHeader). kFec for an FEC packet.
  ReceivedKind OnVideoRtp(const RtpPacket& p, Timestamp now, bool recovered);
  // A video media packet: original, retransmitted or FEC-recovered.
  // `recovered`: restored from RTX or recovered by FEC (RtpPacketReceived::recovered).
  void OnVideoMedia(const RtpPacket& p, Timestamp now, bool recovered);
  // A packet that holds a media sequence number but no media (padding, FEC):
  // RtpVideoStreamReceiver2::NotifyReceiverOfEmptyPacket.
  void OnEmptyPacket(uint16_t seq, Timestamp now);
  void SendSeqNumNacks(Timestamp now);  // NackRequester::OnReceivedPacket's kSeqNumOnly batch.
  void TrackSequence(uint16_t seq);  // Gap bookkeeping for the protection controller.
  void OnAudioMedia(const RtpPacket& p, Timestamp now);
  void AssembleFrames(Timestamp now);

  RtpReceiverConfig cfg_;
  loss::NackTracker nack_;
  std::unique_ptr<loss::ulpfec::UlpfecReceiver> ulpfec_;
  loss::RedReceiver red_;
  timing::PacketBuffer packet_buffer_;
  loss::KeyframeRequester keyframes_;
  timing::VideoJitterEstimator video_jitter_;
  timing::VideoTiming video_timing_;
  video::FrameScheduler scheduler_;
  timing::VcmTiming vcm_timing_;            // timing v2.
  timing::FrameReleaseController release_;  // timing v2.
  video::RenderFrames render_frames_;       // render_smoothing.
  std::map<int64_t, ReceivedFrame> render_pending_;  // render_smoothing: decoded frames in render_frames_.
  timing::AudioJitterBuffer jitter_buffer_;
  timing::ReceiveStatistics stats_rtp_;
  std::optional<timing::ReceiveStatistics> stats_rtx_;
  SeqNumUnwrapper<uint16_t> unwrap_;
  int64_t highest_seq_ = -1;
  H264SpsPpsTracker sps_pps_tracker_;  // sps_pps_tracker.
  TimeDelta rtt_ = TimeDelta::Millis(100);
  bool have_rtt_ = false;
  Timestamp last_rtt_update_ = Timestamp::MinusInfinity();  // timing v2: CallStats' 1 s update.
  std::unordered_set<uint16_t> nacked_seqs_;  // timing v2: received packets that had been NACKed.
  // A/V sync (issue #77): the last SR, the last media packet and the last playout.
  std::optional<timing::SenderReport> last_sr_;
  std::optional<uint32_t> last_received_rtp_timestamp_;
  Timestamp last_received_rtp_time_ = Timestamp::MinusInfinity();
  Timestamp last_playout_time_ = Timestamp::MinusInfinity();
  struct FrameMeta {
    uint32_t rtp_timestamp;
    bool keyframe;
    size_t size_bytes;
    std::vector<uint8_t> bitstream;
    Timestamp receive_time;      // Last packet's arrival.
    bool retransmitted = false;  // A packet of it was NACKed (timing v2).
    video::Rotation rotation = video::Rotation::k0;
  };
  std::map<int64_t, FrameMeta> frames_;  // Frame id -> what the scheduler needs; bounded.
  std::vector<ReceivedFrame> out_frames_;
  ReceiverFeedback feedback_;
  RtpReceiverStats stats_;
};

}  // namespace sm::rtp
