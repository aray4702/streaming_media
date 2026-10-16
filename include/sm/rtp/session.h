// RTP session facade (docs/design/rtp-session.md, sections 4 and 5): one
// endpoint of a one-to-one call. It composes rtp::RtcpEngine, rtp::RtpSender,
// rtp::SendSideController and rtp::RtpReceiver, so an application passes
// encoded frames and datagram bytes instead of wiring the building blocks.
//
//   egress   SendAudio / SendVideo -> RtpSender -> SendSideController (pacer,
//            transport-wide sequence number, GCC) -> TakeOutgoing()
//   ingress  OnPacket: RFC 5761 demux; RTCP goes to RtcpEngine and is
//            dispatched by type (reports and RTT to the receivers and GCC,
//            NACK to the sender's RTX, transport-cc and REMB to GCC, PLI and
//            FIR to the encoder as events); RTP is routed by MID, SSRC or
//            payload type (sync::RtpDemuxer) to an RtpReceiver
//   drive    Process(now) releases paced packets, runs the receivers, queues
//            NACK, keyframe requests and transport-cc feedback, and emits
//            RTCP reports
//
// Pull, not callbacks: every output is a Take*() call, so the order of
// effects is fixed and a test is a plain loop. Takes `now`, reads no clock,
// starts no thread, and never allocates after setup beyond the packets it
// returns.
//
// Video payloads are opaque in this layer, as in the test bench today: the
// app supplies a VideoPayloadParser that tells the receiver where a frame
// starts and whether it is a keyframe, and SendVideo() splits the frame into
// packets. The H.264 packetizer (plan PR 7) replaces both.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sm/cc/transport_feedback.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"
#include "sm/loss/fec.h"
#include "sm/rtp/rtcp_engine.h"
#include "sm/rtp/rtp_receiver.h"
#include "sm/rtp/rtp_sender.h"
#include "sm/rtp/send_side_controller.h"
#include "sm/sync/header_extensions.h"

namespace sm::rtp {

struct SessionConfig {
  uint32_t local_ssrc = 1;  // RTCP sender SSRC of reports and feedback.
  std::string cname;        // SDES CNAME; empty = none.
  std::string mid_audio, mid_video;  // BUNDLE MIDs (RFC 8843); empty = no MID.
  // MID is written on this many first packets of each stream, or until the
  // peer's first packet carrying a MID acknowledges it.
  int mid_packets = 8;
  sync::ExtensionMap extensions;  // Negotiated id <-> URI (SDP stays with the host).
  RtcpConfig rtcp;                // local_ssrc, cname and seed are filled in; has_video stays as set.
  SendSideConfig send_side;
  // Interval between transport-cc feedback messages; zero turns it off.
  TimeDelta transport_feedback_interval = TimeDelta::Millis(50);
  // 1: the interval above, each arrival reported once. 2: libwebrtc's
  // TransportSequenceNumberFeedbackGenenerator: the interval follows the local
  // send estimate (5% of it, 50-250 ms, 100 ms before the first), arrivals are
  // kept 500 ms and late ones reported again. Zero interval still turns it off.
  int transport_feedback_version = 1;
  uint64_t seed = 1;
  EventSink* events = nullptr;
};

struct AudioStreamConfig {
  uint32_t ssrc = 1;
  uint8_t payload_type = 111;
  std::optional<uint8_t> red_payload_type;  // RFC 2198 redundancy (audio/red).
  int red_distance = 1;
  int clock_rate = 48000;  // RTP clock; Opus is 48000 (RFC 7587 4.1).
  // Send side: nominal wire rate taken off the target before video is
  // allocated (rates().audio_rate).
  DataRate nominal_rate = DataRate::Zero();
  // Receive side.
  timing::AudioJitterBufferConfig jitter_buffer;
};

// Payload formats libwebrtc tells apart when it decides about video FEC.
enum class VideoCodecType : uint8_t { kH264, kVP8, kVP9, kGeneric };

struct VideoStreamConfig {
  VideoCodecType codec = VideoCodecType::kH264;
  uint32_t ssrc = 2;
  uint8_t payload_type = 96;
  std::optional<uint32_t> rtx_ssrc;
  uint8_t rtx_payload_type = 97;
  loss::RtxConfig rtx;
  // Video RED+ULPFEC (here: the FEC stream) requested. Whether it runs is
  // VideoFecEnabled(): it is off beside NACK for a codec without a picture id.
  bool fec = false;
  loss::FecConfig fec_config;
  size_t max_payload_size = 1200 - 12;  // Whole packet <= 1200 bytes (libwebrtc's kVideoMtu).
  int clock_rate = 90000;
  // Receive side. The parser is not owned and must outlive the session.
  const VideoPayloadParser* parser = nullptr;  // rtp::H264PayloadParser for H.264.
  bool keyframe_needs_parameter_sets = false;  // See RtpReceiverConfig.
  bool deliver_bitstream = true;               // See RtpReceiverConfig.
  VideoMediaObserver* media_observer = nullptr;  // Optional, not owned.
  bool nack = true;
  loss::NackConfig nack_config;
  timing::PacketBufferConfig packet_buffer;
  loss::KeyframeRequestConfig keyframe;
  video::FrameSchedulerConfig scheduler;
  timing::VideoTimingConfig timing;
  TimeDelta decode_time = TimeDelta::Millis(5);
};

// libwebrtc call/rtp_video_sender.cc PayloadTypeSupportsSkippingFecPackets:
// VP8 and VP9, and Generic only with the WebRTC-GenericPictureId trial.
bool PayloadTypeSupportsSkippingFecPackets(VideoCodecType codec, bool generic_picture_id_trial = false);

// libwebrtc call/rtp_video_sender.cc ShouldDisableRedAndUlpfec, with the
// field trial WebRTC-DisableUlpFecExperiment as a parameter. True when video
// RED and ULPFEC must not be sent: FlexFEC is on, the experiment is on, NACK is
// on and the codec cannot skip FEC packets (ULPFEC would be retransmitted
// too), or only one of RED and ULPFEC is configured.
struct RedUlpfecDecisionInput {
  bool nack_enabled = false;
  bool red_enabled = false;
  bool ulpfec_enabled = false;
  bool flexfec_enabled = false;
  bool disable_ulpfec_experiment = false;
  bool generic_picture_id_trial = false;
  VideoCodecType codec = VideoCodecType::kH264;
};
bool ShouldDisableRedAndUlpfec(const RedUlpfecDecisionInput& in);

// Whether a video stream configured with `v` sends and expects FEC: v.fec
// (both RED and ULPFEC) unless ShouldDisableRedAndUlpfec turns it off.
bool VideoFecEnabled(const VideoStreamConfig& v);

struct AudioSendId { int index = -1; };
struct VideoSendId { int index = -1; };
struct AudioRecvId { int index = -1; };
struct VideoRecvId { int index = -1; };

struct EncodedAudioFrame {  // One Opus packet.
  uint32_t rtp_timestamp = 0;
  std::span<const uint8_t> data;
  bool start_of_talkspurt = false;  // RTP marker (DTX resume).
};

struct EncodedVideoFrame {
  uint32_t rtp_timestamp = 0;  // 90 kHz.
  // One H.264 access unit as an Annex-B byte stream (SPS and PPS ahead of an
  // IDR slice on a keyframe). It is packetized as RFC 6184 mode 1: single NAL
  // unit, STAP-A and FU-A packets of at most max_payload_size bytes, marker on
  // the last. An empty frame sends nothing and is counted.
  std::span<const uint8_t> data;
  // An IDR frame: the pacer flushes the stream's queued packets when it starts
  // (libwebrtc PacingController::EnqueuePacket).
  bool keyframe = false;
};

enum class OutgoingKind : uint8_t { kAudio, kVideo, kRtx, kFec, kPadding, kRtcp };

struct OutgoingPacket {
  std::vector<uint8_t> bytes;  // RTP or RTCP, ready for the socket (SRTP is the host's job).
  OutgoingKind kind = OutgoingKind::kRtcp;
  // Size the pacer accounted for RTP, including transport overhead; the
  // datagram size plus the overhead for RTCP.
  DataSize wire_size;
};

using ReceivedVideoFrame = ReceivedFrame;

enum class SessionEventType : uint8_t {
  kKeyframeRequested,  // PLI or FIR arrived, or the outage monitor wants one: emit an IDR.
  kEncoderPause,       // Outage monitor: stop encoding (and resume below).
  kEncoderResume,
  kTargetRateChanged,  // `rate` is the new target.
  kRttUpdated,         // `rtt` is the latest round-trip time.
  kReportBlock,        // A report block about `ssrc` arrived; `loss_fraction` is its fraction lost (0 to 1).
};

struct SessionEvent {
  SessionEventType type = SessionEventType::kKeyframeRequested;
  Timestamp time;
  uint32_t ssrc = 0;  // Media SSRC for a keyframe request.
  DataRate rate = DataRate::Zero();
  TimeDelta rtt = TimeDelta::Zero();
  double loss_fraction = 0;
};

struct SessionStats {
  uint64_t datagrams_in = 0;
  uint64_t malformed = 0;
  uint64_t rtcp_in = 0;
  uint64_t rtp_in = 0;
  uint64_t unroutable = 0;
  uint64_t empty_video_frames = 0;
  uint64_t nacks_unmatched = 0;  // NACK for an SSRC with no sender.
  uint64_t datagrams_out = 0;
};

class Session : public MetricsSource {
 public:
  explicit Session(SessionConfig cfg = {});
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  // --- Setup (before the first packet) ----------------------------------------
  AudioSendId AddAudioSend(const AudioStreamConfig& cfg);
  VideoSendId AddVideoSend(const VideoStreamConfig& cfg);
  // The decoder is not owned and must outlive the session.
  AudioRecvId AddAudioReceive(const AudioStreamConfig& cfg, AudioDecoder* decoder);
  VideoRecvId AddVideoReceive(const VideoStreamConfig& cfg);

  // --- Egress -----------------------------------------------------------------
  void SendAudio(AudioSendId id, const EncodedAudioFrame& f, Timestamp now);
  void SendVideo(VideoSendId id, const EncodedVideoFrame& f, Timestamp now);
  // A frame already split into packet payloads: for another codec's
  // packetizer, or for tests of malformed payloads.
  void SendVideoChunks(VideoSendId id, std::span<const std::vector<uint8_t>> chunks, uint32_t rtp_timestamp,
                       Timestamp now, bool keyframe = false);
  // FEC protection level for the next frames of a video stream.
  void SetFecParams(VideoSendId id, const loss::FecParams& p);

  // --- Ingress ----------------------------------------------------------------
  // One datagram, RTP or RTCP (RFC 5761 demux by the payload-type byte),
  // already decrypted by the host.
  void OnPacket(std::span<const uint8_t> bytes, Timestamp now);
  // The round-trip time as learned outside RTCP, for an endpoint that only
  // receives (its reports carry no RTT; the host may know it from ICE
  // connectivity checks or from the peer). Feeds the receivers' NACK and
  // keyframe throttles and the congestion controller, like an RTCP-measured
  // RTT, but raises no event.
  void SetRtt(TimeDelta rtt);
  // The decoder failed on a frame: ask the sender for a keyframe.
  void OnDecodeFailure(VideoRecvId id, Timestamp now);

  // --- Drive ------------------------------------------------------------------
  // Cheap to call every tick. The first call starts the congestion controller.
  void Process(Timestamp now);
  // When Process must run next. With a video receiver this is at most 1 ms
  // away, because frame scheduling is evaluated on every call.
  Timestamp NextProcessTime(Timestamp now) const;

  // --- Outputs ----------------------------------------------------------------
  std::vector<OutgoingPacket> TakeOutgoing();
  std::vector<ReceivedVideoFrame> TakeVideoFrames(VideoRecvId id);
  std::vector<SessionEvent> TakeEvents();
  // Audio: the device pulls 10 ms.
  timing::AudioOperation GetAudio(AudioRecvId id, std::span<float> out, Timestamp now);

  // --- Read-only snapshots ----------------------------------------------------
  // Target, audio rate and the video encoder's share (design 7.3).
  SendRates rates() const;
  TimeDelta rtt() const { return rtt_; }
  const SessionStats& stats() const { return stats_; }
  const RtcpEngine& rtcp() const { return rtcp_; }
  const SendSideController& send_side() const { return send_; }
  SendSideController& send_side() { return send_; }
  const RtpSender& video_sender(VideoSendId id) const { return *video_send_[static_cast<size_t>(id.index)].sender; }
  const RtpSender& audio_sender(AudioSendId id) const { return *audio_send_[static_cast<size_t>(id.index)].sender; }
  const RtpReceiver& video_receiver(VideoRecvId id) const { return *video_recv_[static_cast<size_t>(id.index)].receiver; }
  const RtpReceiver& audio_receiver(AudioRecvId id) const { return *audio_recv_[static_cast<size_t>(id.index)].receiver; }
  const cc::TransportFeedbackBuilder& transport_feedback() const { return tcc_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  struct SendStream {
    std::unique_ptr<RtpSender> sender;
    DataRate nominal_rate = DataRate::Zero();
  };
  struct RecvStream {
    std::unique_ptr<RtpReceiver> receiver;
    uint32_t ssrc = 0;
    uint32_t rtx_ssrc = 0;
  };

  void HandleRtcp(std::span<const uint8_t> bytes, Timestamp now);
  void HandleRtp(std::span<const uint8_t> bytes, Timestamp now);
  void EnqueueSenderPackets(RtpSender& s, Timestamp now);
  void UpdateSenderState();
  void Emit(SessionEvent e) { events_.push_back(e); }
  RtpSender* SenderForSsrc(uint32_t ssrc);
  RtpSenderConfig SenderConfig(uint32_t ssrc, uint8_t pt, int clock, bool video, const std::string& mid) const;
  void Start(Timestamp now);
  bool HasSendStreams() const { return !audio_send_.empty() || !video_send_.empty(); }

  SessionConfig cfg_;
  RtcpEngine rtcp_;
  SendSideController send_;
  cc::TransportFeedbackBuilder tcc_;
  sync::RtpDemuxer demux_;
  std::vector<SendStream> audio_send_, video_send_;
  std::vector<RecvStream> audio_recv_, video_recv_;
  // Demuxer sink id -> (is video, index).
  std::vector<std::pair<bool, int>> sinks_;
  std::vector<OutgoingPacket> out_;
  std::vector<SessionEvent> events_;
  bool started_ = false;
  bool peer_mid_seen_ = false;
  bool send_has_padding_ = false;
  Timestamp last_tcc_ = Timestamp::MinusInfinity();
  DataRate last_tcc_estimate_ = DataRate::Zero();
  TimeDelta TransportFeedbackInterval() const;
  void UpdateTransportFeedbackRate();
  DataRate last_target_ = DataRate::Zero();
  TimeDelta rtt_ = TimeDelta::Millis(100);
  bool have_rtt_ = false;  // Whether any RTT sample has arrived (rtt_ is a default until then).
  SessionStats stats_;
};

}  // namespace sm::rtp
