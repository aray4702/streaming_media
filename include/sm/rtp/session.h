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
// Clock contract: every `now` passed to a Session must come from one
// monotonic clock (never decreasing; the host's steady clock, not wall-clock
// time, which NTP and DST can step back). libwebrtc requires the same: its
// PacingController::CurrentTime (pacing_controller.cc) logs "Non-monotonic
// clock behavior observed", DCHECKs and holds the previous time, and this
// pacer clamps the same way. So after a backward step the session sends
// nothing, media or RTCP, until `now` passes its old high-water mark: a 60 s
// step is a 60 s silent call (issue #440). A forward step is absorbed.
//
// Video payloads are opaque in this layer, as in the test bench today: the
// app supplies a VideoPayloadParser that tells the receiver where a frame
// starts and whether it is a keyframe, and SendVideo() splits the frame into
// packets. The H.264 packetizer (plan PR 7) replaces both.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sm/cc/transport_feedback.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"
#include "sm/loss/fec_controller.h"
#include "sm/rtp/call_stats.h"
#include "sm/rtp/rtcp_engine.h"
#include "sm/rtp/rtp_receiver.h"
#include "sm/rtp/rtp_sender.h"
#include "sm/rtp/send_side_controller.h"
#include "sm/rtp/sps_vui_rewriter.h"
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
  // local_ssrc, cname and seed are filled in; has_video stays as set.
  RtcpConfig rtcp;
  SendSideConfig send_side;
  // Zero turns transport-cc feedback off. Otherwise the feedback follows
  // libwebrtc's TransportSequenceNumberFeedbackGenenerator: the interval
  // follows the local send estimate (5% of it, 50-250 ms, 100 ms before the
  // first), arrivals are kept 500 ms and late ones reported again.
  TimeDelta transport_feedback_interval = TimeDelta::Millis(50);
  uint64_t seed = 1;
  // Lip sync (issue #77): the first audio and the first video receive stream
  // are one sync group, as libwebrtc's Call::ConfigureSync pairs streams that
  // share a sync_group, and a sync::RtpStreamsSynchronizer sets both streams'
  // minimum playout delays every second (needs video timing version 2).
  // Off: each stream plays out on its own delay.
  bool av_sync = false;
  // The audio send stream sends its own RTCP sender reports beside video's,
  // as libwebrtc's audio RtpRtcp module does (SSRC the audio SSRC, the 5 s
  // audio report interval), so a receiver can map audio RTP time to NTP for
  // lip sync (issue #77). Off: only the first video stream's SR is sent when
  // the session sends video.
  bool audio_rtcp = false;
  // RTT aggregation as libwebrtc's per-module RTCPReceiver::RttStats and ModuleRtpRtcpImpl2 (issue
  // #568): each send stream keeps the RTT samples of its media SSRC's report block only, runs its
  // own 1 s periodic update to CallStats and its packet history, and takes its NACK RTT from them;
  // GoogCC gets each stream's packet RTT. Off: one session-wide value, the last report block of
  // any SSRC (or SetRtt), feeds every stream and one CallStats report a second. Off by default
  // until the flip is judged on the parity report.
  bool per_stream_rtt = false;
  // Receive statistics as libwebrtc's StreamStatisticianImpl for every receive stream
  // (RtpReceiverConfig::receive_statistics_v2, issue #575): max reordering threshold, stream
  // restart, 255-scaled fraction lost. Off by default until the flip is judged on the parity report.
  bool receive_statistics_v2 = false;
  // Each audio receive stream reports in its own RTCP, as libwebrtc's audio
  // ChannelReceive does (issue #484): one ReceiveStatistics per receive stream
  // (audio/channel_receive.cc:608, video/video_receive_stream2.cc:255) and a
  // receiver-only RtpRtcp module per stream (channel_receive.cc:115-139) that
  // sends an RR with the audio block alone at the 5 s audio report interval
  // (rtcp_receiver.cc:85), first at half of it (rtcp_sender.cc:171), with no
  // SDES (the audio receive module has no CNAME) and no RRTR
  // (enable_non_sender_rtt is off). The video compounds then carry only the
  // video and RTX blocks. Off: the audio block rides in every compound the
  // session sends (video RR, NACK, PLI). On by default, as libwebrtc (#484).
  bool audio_receive_rtcp = true;
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
  // Send side: the rate the stream registers with the bitrate
  // allocator as both its minimum and maximum. libwebrtc registers the codec's
  // payload rate plus the per-packet overhead at the packet interval
  // (AudioSendStream::GetMinMaxBitrateConstraints); redundancy is not counted.
  // Zero means the range below or, without one, `nominal_rate`.
  DataRate allocation_rate = DataRate::Zero();
  // Send side, used when `max_bitrate` is set and `allocation_rate` is not
  // (issue #569): the codec's payload rate range (AudioSendStream::Config::
  // min_bitrate_bps / max_bitrate_bps) and the encoder's frame length range
  // (AudioEncoder::GetFrameLengthRange; Opus without its network adaptor
  // reports its frame size for both). The session registers the stream as
  // AudioSendStream::GetMinMaxBitrateConstraints and ConfigureBitrateObserver
  // do (audio_send_stream.cc:808-901): min + overhead / longest frame,
  // max + overhead / shortest frame, and a priority rate of `priority_bitrate`
  // + overhead / longest frame (WebRTC-Audio-PriorityBitrate, on by default),
  // where the overhead is the transport's bytes plus the media packet header
  // (ExpectedPerPacketOverhead). A max below the min is raised to the min
  // (libwebrtc rejects such a config).
  DataRate min_bitrate = DataRate::Zero();
  DataRate max_bitrate = DataRate::Zero();
  TimeDelta min_frame_length = TimeDelta::Millis(20);
  TimeDelta max_frame_length = TimeDelta::Millis(20);
  // AudioAllocationConfig::priority_bitrate (WebRTC-Audio-Allocation): 0 by default.
  DataRate priority_bitrate = DataRate::Zero();
  // Receive side. libwebrtc's NetEq (jitter buffer version 2) is the session default (#62).
  timing::AudioJitterBufferConfig jitter_buffer = [] { timing::AudioJitterBufferConfig c; c.version = 2; return c; }();
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
  // Video RED+ULPFEC (RFC 2198, RFC 5109) requested, on the media SSRC with
  // `ulpfec`'s payload types. Whether it runs is VideoFecEnabled(): it is off
  // beside NACK for a codec without a picture id.
  bool fec = false;
  loss::ulpfec::UlpfecConfig ulpfec;
  // Whole packet <= 1200 bytes (libwebrtc's kVideoMtu). As libwebrtc's
  // RtpSenderVideo, the payload room of each frame is this packet capacity less
  // the header (with the extensions the next packet will carry), and the
  // packetizer applies the first, last and single packet reductions. The sender
  // clamps it to a 100-1500 byte packet (RtpSenderConfig::max_payload_size).
  size_t max_payload_size = 1200 - 12;
  int clock_rate = 90000;
  // Receive side. The parser is not owned and must outlive the session.
  const VideoPayloadParser* parser = nullptr;  // rtp::H264PayloadParser for H.264.
  bool keyframe_needs_parameter_sets = false;  // See RtpReceiverConfig.
  bool sps_pps_tracker = true;                 // See RtpReceiverConfig (issue #453). On, as libwebrtc; H.264 only.
  bool render_smoothing = false;               // See RtpReceiverConfig (issue #437).
  bool deliver_bitstream = true;               // See RtpReceiverConfig.
  VideoMediaObserver* media_observer = nullptr;  // Optional, not owned.
  bool nack = true;
  loss::NackConfig nack_config;
  // libwebrtc's H.264 `missing_packets_` hold is on (issue #453); the session applies it to H.264 only.
  timing::PacketBufferConfig packet_buffer = [] { timing::PacketBufferConfig c; c.missing_packets = true; return c; }();
  loss::KeyframeRequestConfig keyframe;
  video::FrameSchedulerConfig scheduler;
  // libwebrtc's video receive timing (VCMTiming and frame release, version 2) is the session default (#24).
  timing::VideoTimingConfig timing = [] { timing::VideoTimingConfig c; c.version = 2; return c; }();
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
  // The capture rotation (VideoFrame::rotation), sent in the video-orientation
  // extension (CVO) when the session maps it (RtpSender::SendVideoFrame).
  video::Rotation rotation = video::Rotation::k0;
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
  SpsRewriteStats sps;  // Keyframe SPS units: VUI rewritten, already fine, or not parsed (#342).
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
  // With the audio level of the frame (RFC 6464), written when the session maps the extension.
  void SendAudio(AudioSendId id, const EncodedAudioFrame& f, std::optional<audio::AudioLevel> level, Timestamp now);
  void SendVideo(VideoSendId id, const EncodedVideoFrame& f, Timestamp now);
  // A frame already split into packet payloads: for another codec's
  // packetizer, or for tests of malformed payloads.
  void SendVideoChunks(VideoSendId id, std::span<const std::vector<uint8_t>> chunks, uint32_t rtp_timestamp,
                       Timestamp now, bool keyframe = false);
  // FEC protection level for the next frames of a video stream, for delta and
  // key frames alike. With FEC on, the session sets it itself on each network
  // estimate change (loss::FecControllerDefault, issue #29), which replaces this.
  void SetFecParams(VideoSendId id, const loss::ulpfec::FecProtectionParams& p);
  // RtpVideoSender::SetEncodingData, on each encoder (re)configuration
  // (VideoSendStreamImpl::OnEncoderConfigurationChanged): the resolution and
  // temporal layers FecControllerDefault sizes the FEC for. Until the first
  // call it assumes 704x576 and one layer.
  void SetVideoEncodingData(VideoSendId id, int width, int height, int num_temporal_layers = 1);

  // --- Ingress ----------------------------------------------------------------
  // One datagram, RTP or RTCP (RFC 5761 demux by the payload-type byte),
  // already decrypted by the host.
  void OnPacket(std::span<const uint8_t> bytes, Timestamp now);
  // A new round-trip time measurement learned outside RTCP, for an endpoint
  // that only receives (its reports carry no RTT; the host may know it from
  // ICE connectivity checks or from the peer). Call it once per measurement.
  // It takes the place of a receive-only stream's XR DLRR RTT: the next 1 s
  // periodic update hands it to CallStats, whose maximum feeds the video
  // receivers' NACK and jitter estimate (VideoReceiveStream2::OnRttUpdate).
  // With `rtcp.xr_rrtr` the session measures that RTT itself, and this one
  // does not go to CallStats.
  // The congestion controller and the audio receivers take it at once. It
  // raises no event.
  void SetRtt(TimeDelta rtt);
  // The decoder failed on a frame: ask the sender for a keyframe.
  void OnDecodeFailure(VideoRecvId id, Timestamp now);

  // --- Drive ------------------------------------------------------------------
  // Cheap to call every tick. The first call starts the congestion controller.
  // `now` must never decrease across calls to any method (the clock contract
  // above); a backward step stalls sending until `now` catches up.
  // A non-finite `now` (PlusInfinity, MinusInfinity) is ignored.
  void Process(Timestamp now);
  // When Process must run next: the earliest timer due (pacer, RTCP, transport
  // feedback, CallStats, A/V sync, and each video receiver's frame release,
  // render, decode time-out and NACK tick), or `now` after a packet arrived.
  // A host that calls Process at these times runs every timer at its due
  // microsecond, as libwebrtc's delayed tasks run.
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
  // The audio receive streams' own RTCP; null unless SessionConfig::audio_receive_rtcp and an audio receive stream exist.
  const RtcpEngine* audio_receive_rtcp() const { return audio_recv_rtcp_.get(); }
  const SendSideController& send_side() const { return send_; }
  SendSideController& send_side() { return send_; }
  const RtpSender& video_sender(VideoSendId id) const { return *video_send_[static_cast<size_t>(id.index)].sender; }
  const loss::FecControllerDefault& fec_controller(VideoSendId id) const { return *video_send_[static_cast<size_t>(id.index)].fec; }
  const RtpSender& audio_sender(AudioSendId id) const { return *audio_send_[static_cast<size_t>(id.index)].sender; }
  const RtpReceiver& video_receiver(VideoRecvId id) const { return *video_recv_[static_cast<size_t>(id.index)].receiver; }
  const RtpReceiver& audio_receiver(AudioRecvId id) const { return *audio_recv_[static_cast<size_t>(id.index)].receiver; }
  // The A/V synchronizer; null unless SessionConfig::av_sync and both an audio and a video receive stream exist.
  const sync::RtpStreamsSynchronizer* av_sync() const { return av_sync_.get(); }
  const cc::TransportFeedbackBuilder& transport_feedback() const { return tcc_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  // Starts the session before the first encode decision (VideoSendController::OnCapturedFrame).
  friend class VideoSendController;

  struct SendStream {
    std::unique_ptr<RtpSender> sender;
    DataRate nominal_rate = DataRate::Zero();
    DataRate allocation_rate = DataRate::Zero();
    // Audio: AudioStreamConfig's codec and frame length ranges (zero `max_bitrate`: the fixed
    // `allocation_rate`).
    DataRate min_bitrate = DataRate::Zero(), max_bitrate = DataRate::Zero(), priority_bitrate = DataRate::Zero();
    TimeDelta min_frame_length = TimeDelta::Zero(), max_frame_length = TimeDelta::Zero();
    // Video: RtpVideoSender's FecControllerDefault, and the times of the frames
    // sent in the last second for the sent frame rate it takes.
    std::unique_ptr<loss::FecControllerDefault> fec;
    std::deque<Timestamp> frame_times;
    Timestamp first_frame = Timestamp::MinusInfinity();
    bool nack = false;  // Video: VideoStreamConfig::nack, for the RTX reservation (SendVideo).
    // The stream's own ModuleRtpRtcpImpl2 and RTCPReceiver (one per send stream in libwebrtc):
    // RttStats (rtcp_receiver.cc:211-229), fed only by the report block for the local media SSRC
    // (rtcp_receiver.cc:567-569); last_received_rb_, set by a block for the media or RTX SSRC
    // (rtcp_receiver.cc:536); and rtt_ms_, refreshed by PeriodicUpdate (rtp_rtcp_impl2.cc:740-751).
    TimeDelta last_rtt = TimeDelta::Zero();
    TimeDelta sum_rtt = TimeDelta::Zero();
    int64_t num_rtts = 0;
    Timestamp last_received_rb = Timestamp::PlusInfinity();
    int64_t rtt_ms = 0;
  };
  // The allocator registration of one audio stream (AudioStreamConfig::max_bitrate).
  SendSideController::AudioAllocation AudioAllocationOf(const SendStream& a) const;
  struct RecvStream {
    std::unique_ptr<RtpReceiver> receiver;
    uint32_t ssrc = 0;
    uint32_t rtx_ssrc = 0;
  };
  // A receive stream as call/syncable.h's Syncable.
  class SyncableStream : public sync::Syncable {
   public:
    SyncableStream(RtpReceiver* r, uint32_t ssrc) : r_(r), ssrc_(ssrc) {}
    uint32_t id() const override { return ssrc_; }
    std::optional<Info> GetInfo() const override { return r_->SyncInfo(); }
    std::optional<PlayoutInfo> GetPlayoutRtpTimestamp() const override { return r_->PlayoutRtpTimestamp(); }
    bool SetMinimumPlayoutDelay(TimeDelta delay) override { return r_->SetSyncMinimumPlayoutDelay(delay); }
    void SetEstimatedPlayoutNtpTimestamp(timing::NtpTime, Timestamp) override {}

   private:
    RtpReceiver* r_;
    uint32_t ssrc_;
  };

  void HandleRtcp(std::span<const uint8_t> bytes, Timestamp now);
  void HandleRtp(std::span<const uint8_t> bytes, Timestamp now);
  void EnqueueSenderPackets(RtpSender& s, Timestamp now);
  // SendVideoChunks with the encoded frame's size, for FecControllerDefault::UpdateWithEncodedData.
  void SendVideoChunks(VideoSendId id, std::span<const std::vector<uint8_t>> chunks, uint32_t rtp_timestamp,
                       Timestamp now, bool keyframe, size_t encoded_size,
                       video::Rotation rotation = video::Rotation::k0);
  // RtpVideoSender::OnBitrateUpdated's FEC half: on each network estimate change
  // (CongestionControlHandler::GetUpdate: the target, the loss or the RTT), each
  // video stream's FecControllerDefault sets the protection of the next frames.
  void MaybeUpdateFecRates(Timestamp now);
  // SendStatisticsProxy::GetSendFrameRate: frames sent in the last second.
  static int SentFrameRate(SendStream& v, Timestamp now);
  void UpdateSenderState(Timestamp now);
  void Emit(SessionEvent e) { events_.push_back(e); }
  RtpSender* SenderForSsrc(uint32_t ssrc);
  RtpSenderConfig SenderConfig(uint32_t ssrc, uint8_t pt, int clock, bool video, const std::string& mid) const;
  void Start(Timestamp now);
  void MaybeCreateSync();  // The synchronizer, once both an audio and a video receive stream exist.
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
  bool packet_since_process_ = false;  // OnPacket queued work (frames to assemble, RTCP) for the next Process.
  bool peer_mid_seen_ = false;
  bool send_has_padding_ = false;
  Timestamp last_tcc_ = Timestamp::MinusInfinity();
  // When the RepeatingTask that runs ReceiveSideCongestionController::MaybeProcess wakes next.
  Timestamp tcc_wake_ = Timestamp::MinusInfinity();
  DataRate last_tcc_estimate_ = DataRate::Zero();
  void UpdateTransportFeedbackRate();
  void ProcessTransportFeedback(Timestamp now);
  DataRate last_target_ = DataRate::Zero();
  // The network estimate the FEC controllers last took (target, fraction lost 0-255, RTT).
  struct FecEstimate {
    DataRate target = DataRate::Zero();
    uint8_t fraction_loss = 0;
    TimeDelta rtt = TimeDelta::Zero();
  };
  std::optional<FecEstimate> last_fec_estimate_;
  TimeDelta rtt_ = TimeDelta::Millis(100);
  // Last report block per reported SSRC, for the loss between two reports (RtpTransportControllerSend::
  // OnReceivedRtcpReceiverReportBlocks).
  struct LastReportBlock {
    uint32_t extended_highest_seq = 0;
    int32_t cumulative_lost = 0;
  };
  std::map<uint32_t, LastReportBlock> last_report_blocks_;
  bool have_rtt_ = false;  // Whether any RTT sample has arrived (rtt_ is a default until then).
  // Each send stream's ModuleRtpRtcpImpl2::PeriodicUpdate (kRttUpdateInterval). The streams'
  // tasks are started together, so one schedule runs them all.
  static constexpr TimeDelta kRttUpdateInterval = TimeDelta::Millis(1000);
  void PeriodicRttUpdate(Timestamp now);
  Timestamp next_rtt_update_ = Timestamp::MinusInfinity();
  SendStream* StreamForSsrc(uint32_t ssrc);
  // per_stream_rtt off: the session-wide RTT the periodic update and NACKs take (rtt_ms_), and
  // the samples' sum for AverageRtt.
  Timestamp last_report_block_ = Timestamp::MinusInfinity();
  TimeDelta periodic_rtt_ = TimeDelta::Zero();
  TimeDelta rtt_sum_ = TimeDelta::Zero();
  int64_t rtt_count_ = 0;
  // CallStats for the video receivers, fed by the periodic update: the
  // sender's periodic RTT, and a receive-only stream's RTT from the XR DLRR
  // (rtcp.xr_rrtr) or else SetRtt (RTCPReceiver::GetAndResetXrRrRtt: each
  // measurement once).
  CallStats call_stats_;
  std::optional<TimeDelta> pending_rr_rtt_;
  void OnCallStatsUpdate(const std::optional<CallStats::Update>& u);
  SessionStats stats_;
  std::unique_ptr<SyncableStream> sync_audio_, sync_video_;
  std::unique_ptr<RtcpEngine> audio_rtcp_;  // SessionConfig::audio_rtcp.
  std::unique_ptr<RtcpEngine> audio_recv_rtcp_;  // SessionConfig::audio_receive_rtcp.
  std::unique_ptr<sync::RtpStreamsSynchronizer> av_sync_;
};

}  // namespace sm::rtp
