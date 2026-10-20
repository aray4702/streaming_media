// RTP receiver of one media stream (docs/design/rtp-session.md, sections 4.3,
// 5 and 6.4): takes datagrams as bytes and does what the test bench's
// receiver glue does today.
//
// Video: parse the RTP packet, restore RTX (RFC 4588), recover with FEC, track
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

#include <cstdint>
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
#include "sm/loss/fec.h"
#include "sm/loss/keyframe_request.h"
#include "sm/loss/nack_tracker.h"
#include "sm/loss/red.h"
#include "sm/sync/header_extensions.h"
#include "sm/timing/audio_jitter_buffer.h"
#include "sm/timing/frame_release.h"
#include "sm/timing/packet_buffer.h"
#include "sm/timing/rtcp_stats.h"
#include "sm/timing/vcm_timing.h"
#include "sm/timing/video_jitter_estimator.h"
#include "sm/video/frame_scheduler.h"

namespace sm::rtp {

// What the receiver needs to know about one video RTP packet.
struct VideoPayloadInfo {
  bool valid = false;        // False: malformed payload, the packet is dropped.
  bool frame_start = false;  // The packet can start a frame (see PacketBuffer).
  bool idr = false;          // Carries (the start of) an IDR slice: picture data of a keyframe.
  bool sps = false;          // Carries a sequence parameter set.
  bool pps = false;          // Carries a picture parameter set.
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

  // --- Video ---
  const VideoPayloadParser* parser = nullptr;  // Required for video. Not owned.
  VideoMediaObserver* media_observer = nullptr;  // Optional. Not owned.
  std::optional<uint32_t> rtx_ssrc;
  bool nack = true;  // Track gaps and ask for retransmissions.
  // A keyframe needs SPS and PPS as well as the IDR slice (libwebrtc's
  // sps-pps-idr-in-keyframe mode). Off: an IDR alone is a keyframe, which is
  // libwebrtc's default.
  bool keyframe_needs_parameter_sets = false;
  bool deliver_bitstream = true;  // Rebuild each frame's bitstream (ReceivedFrame::bitstream).
  bool fec = false;
  loss::FecConfig fec_config;  // fec_config.fec_ssrc is the SSRC of the FEC packets.
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
};

// Feedback due toward the sender, for rtp::RtcpEngine.
struct ReceiverFeedback {
  std::vector<uint16_t> nacks;                  // One NACK batch.
  std::vector<loss::FeedbackMessage> keyframe;  // PLI, FIR or LNTF from the keyframe requester.
  bool pli = false;                             // The NACK list overflowed: ask for a keyframe.
  bool empty() const { return nacks.empty() && keyframe.empty() && !pli; }
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
  // Gaps in the media sequence numbers as they arrive (recovered packets
  // included), for the protection controller (a real receiver reports this in
  // RTCP XR): runs of missing packets, and the packets in them.
  uint64_t gaps = 0;
  uint64_t missing = 0;
  uint64_t audio_frames = 0;
  uint64_t audio_decode_failures = 0;
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
  // Sender report from RtcpEngine (RtcpIncoming::sender_reports).
  void OnSenderReport(const timing::SenderReport& sr, Timestamp now) { stats_rtp_.OnSenderReport(sr, now); }
  void OnRtt(TimeDelta rtt);
  // The decoder failed: ask for a keyframe.
  void OnDecodeFailure(Timestamp now) { keyframes_.OnDecoderError(now); }

  std::vector<ReceivedFrame> TakeFrames();
  ReceiverFeedback TakeFeedback();
  // Audio: the device pulls 10 ms.
  timing::AudioOperation GetAudio(std::span<float> out, Timestamp now) { return jitter_buffer_.GetAudio(out, now); }

  // Report block source for rtp::RtcpEngine::AddReceiveStatistics.
  timing::ReceiveStatistics* receive_statistics() { return &stats_rtp_; }

  const RtpReceiverConfig& config() const { return cfg_; }
  const RtpReceiverStats& stats() const { return stats_; }
  const loss::NackTracker& nack() const { return nack_; }
  const loss::FecDecoder& fec() const { return fec_; }
  const loss::RedReceiver& red() const { return red_; }
  const timing::PacketBuffer& packet_buffer() const { return packet_buffer_; }
  const loss::KeyframeRequester& keyframes() const { return keyframes_; }
  const video::FrameScheduler& scheduler() const { return scheduler_; }
  const timing::VideoJitterEstimator& video_jitter() const { return video_jitter_; }
  const timing::VideoTiming& video_timing() const { return video_timing_; }
  const timing::VcmTiming& vcm_timing() const { return vcm_timing_; }
  const timing::FrameReleaseController& frame_release() const { return release_; }
  const timing::AudioJitterBuffer& jitter_buffer() const { return jitter_buffer_; }
  timing::AudioJitterBuffer& jitter_buffer() { return jitter_buffer_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  // A video media packet: original, retransmitted or FEC-recovered.
  void OnVideoMedia(const RtpPacket& p, Timestamp now, bool feed_fec);
  void TrackSequence(uint16_t seq);  // Gap bookkeeping for the protection controller.
  void OnAudioMedia(const RtpPacket& p, Timestamp now);
  void AssembleFrames(Timestamp now);

  RtpReceiverConfig cfg_;
  loss::NackTracker nack_;
  loss::FecDecoder fec_;
  loss::RedReceiver red_;
  timing::PacketBuffer packet_buffer_;
  loss::KeyframeRequester keyframes_;
  timing::VideoJitterEstimator video_jitter_;
  timing::VideoTiming video_timing_;
  video::FrameScheduler scheduler_;
  timing::VcmTiming vcm_timing_;            // timing v2.
  timing::FrameReleaseController release_;  // timing v2.
  timing::AudioJitterBuffer jitter_buffer_;
  timing::ReceiveStatistics stats_rtp_;
  SeqNumUnwrapper<uint16_t> unwrap_;
  int64_t highest_seq_ = -1;
  TimeDelta rtt_ = TimeDelta::Millis(100);
  bool have_rtt_ = false;
  Timestamp last_rtt_update_ = Timestamp::MinusInfinity();  // timing v2: CallStats' 1 s update.
  std::unordered_set<uint16_t> nacked_seqs_;  // timing v2: received packets that had been NACKed.
  struct FrameMeta {
    uint32_t rtp_timestamp;
    bool keyframe;
    size_t size_bytes;
    std::vector<uint8_t> bitstream;
    Timestamp receive_time;      // Last packet's arrival.
    bool retransmitted = false;  // A packet of it was NACKed (timing v2).
  };
  std::map<int64_t, FrameMeta> frames_;  // Frame id -> what the scheduler needs; bounded.
  std::vector<ReceivedFrame> out_frames_;
  ReceiverFeedback feedback_;
  RtpReceiverStats stats_;
};

}  // namespace sm::rtp
