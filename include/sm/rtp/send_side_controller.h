// Send-side controller of the RTP session layer (docs/design/rtp-session.md,
// sections 5.1 and 7): one per transport, shared by every stream. It owns the
// pacer, the GCC network controller and the outage monitor, and ties them to
// the packets of rtp::RtpSender:
//   - Enqueue() hands a packet to the pacer with its priority;
//   - Process() releases what is due, writes the transport-wide sequence
//     number (and abs-send-time when negotiated), tells GCC about the send
//     (libwebrtc's PacketRouter::SendPacket),
//     counts the bytes of the 500 ms send-rate window, and returns datagrams
//     ready for the socket;
//   - transport feedback, RTT and REMB go to GCC, and the new target and
//     pacing rate come back as `target()` and `rates()`;
//   - the GCC process interval (25 ms) runs from Process(). As in libwebrtc,
//     nothing pauses the encoder on a feedback time-out: the outage monitor
//     only follows the link (#151), and TakeActions() stays empty.
// Padding and probes are real padding-only RTP packets made by the padding
// stream registered with SetPaddingSender().
//
// ComputeRates() is the test bench's original video_rate() arithmetic: target
// minus the audio wire rate, divided by (1 + FEC protection) and clamped.
// ComputeMeasuredRates() is what the session uses (issue #21): the share taken
// off is what RTX and FEC actually used in the last second, as in libwebrtc's
// FecControllerDefault. Both are then lowered by congestion-window pushback
// (cc/congestion_window_pushback.h). Takes `now`, reads no clock and starts no thread.
#pragma once

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

#include "sm/cc/bitrate_allocator_v2.h"
#include "sm/cc/congestion_window_pushback.h"
#include "sm/cc/control_handler.h"
#include "sm/cc/outage_monitor.h"
#include "sm/cc/pacer.h"
#include "sm/cc/send_side_bwe.h"
#include "sm/cc/transport_feedback.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/rate_statistics.h"
#include "sm/core/units.h"
#include "sm/rtp/rtp_sender.h"
#include "sm/sync/header_extensions.h"

namespace sm::rtp {

struct SendSideConfig {
  // libwebrtc's GoogCC and PacingController (#61; their v1 alternatives were deleted in #167).
  cc::GccConfig gcc;
  cc::PacerConfig pacer;
  cc::OutageConfig outage;
  cc::CongestionWindowConfig cwnd;
  // Bytes the transport adds to every datagram (UDP + IPv4), counted in the
  // pacer budget and in GCC's send history.
  int64_t transport_overhead_bytes = 28;
  TimeDelta gcc_interval = TimeDelta::Millis(25);
  TimeDelta send_rate_window = TimeDelta::Millis(500);
  // Largest share of the sending rate repair traffic may take away from the
  // video encoder (libwebrtc FecControllerDefault::overhead_threshold_).
  double max_repair_share = 0.5;
  TimeDelta repair_window = TimeDelta::Seconds(1);
  // How the repair share is measured (issue #224). On: one RateStatistics per
  // media type, as RtpSenderEgress::GetSendRates, so a repair stream that has
  // just started (after 1.5 windows without one) counts at its rate since it
  // started. Off: the bytes of each type over the last `repair_window`. On by
  // default since #224, together with `control_handler`.
  bool send_rate_stats = true;
  // The encoder bitrate adjuster's bandwidth allocation (issue #326). On: VideoSendStreamImpl's
  // link_allocation, the video allocation less overhead and protection before the encoder target
  // is capped at `video_max`, so an allocation above the maximum gives the adjuster headroom, as
  // libwebrtc does. Off: the capped encoder target, no headroom.
  bool link_headroom = true;
  // The transport's CongestionControlHandler between GoogCC and the allocator (cc/control_handler.h,
  // issue #204): the allocator, and with it the encoder target and its protection share, changes only
  // when the handler passes an update on, and the encoder pauses while the pacer queue is above 2 s.
  // Off: every GoogCC update reallocates and the encoder reads the live protection share. On by
  // default since #224/#204, as libwebrtc's RtpTransportControllerSend.
  bool control_handler = true;
  DataRate video_min = DataRate::KilobitsPerSec(30);
  DataRate video_max = DataRate::KilobitsPerSec(10000);
  // With the allocator (RtpVideoSender::OnBitrateUpdated): the packet size
  // the overhead rate is computed for (RTP max_packet_size; the transport
  // overhead is added) and the RTP header bytes of a media packet (12 plus the
  // transport-wide sequence number and abs-send-time extensions; libwebrtc's
  // ExpectedPerPacketOverhead is the header the sender actually builds).
  int64_t max_packet_size = 1200;
  int64_t rtp_overhead_bytes = 24;
  EventSink* events = nullptr;
};

// The split of the target the encoders need (design 7.3).
struct SendRates {
  DataRate target;       // GCC target for everything the session sends.
  DataRate audio_rate;   // Audio wire rate taken off first.
  DataRate video_target; // What the video encoder may produce (after congestion-window pushback).
  // VideoSendStreamImpl's link_allocation, given to the encoder bitrate adjuster as the bandwidth
  // allocation: the video allocation less overhead and protection, before the target is capped at
  // the encoder maximum (and before pushback). Zero without a configured allocation.
  DataRate video_link_allocation;
  // Congestion-window pushback (issue #53): drop every N-th captured frame
  // (0 = none); `video_target` is already lowered to match. See
  // SendSideController::DropCapturedFrame().
  int frame_drop_interval = 0;
  // `control_handler`: the last update passed on had a zero target (the pacer queue above 2 s), so the
  // encoder drops every captured frame (VideoStreamEncoder::EncoderPaused).
  bool encoder_paused = false;
};

// A datagram released by the pacer.
struct ReleasedPacket {
  std::vector<uint8_t> bytes;  // RTP, ready for the socket (SRTP is the host's job).
  PacketKind kind = PacketKind::kVideo;
  // Transport-wide sequence number. Numbering starts at 1, as libwebrtc's
  // PacketRouter, and a packet without the extension mapped has none (0) and is not reported to GCC.
  uint16_t transport_seq = 0;
  DataSize wire_size;          // Size the pacer and GCC accounted, with transport overhead.
  int probe_cluster = -1;
  Timestamp send_time;
};

struct ControllerActions {
  bool pause_encoder = false;
  bool resume_encoder = false;
  bool request_keyframe = false;
};

struct SendSideStats {
  uint64_t released = 0;
  uint64_t padding = 0;
  uint64_t unknown_ids = 0;
  uint64_t flushed = 0;  // Packets a keyframe flushed from the pacer queue.
  int64_t released_bytes = 0;
};

class SendSideController : public MetricsSource {
 public:
  // `extensions` (not owned, may be null) maps the transport-wide sequence
  // number and abs-send-time; without them the packets carry neither.
  explicit SendSideController(SendSideConfig cfg = {}, const sync::ExtensionMap* extensions = nullptr);

  void Start(Timestamp now);

  // Registers the streams with libwebrtc's BitrateAllocator
  // (call/bitrate_allocator.cc, issue #104), as
  // AudioSendStream::ConfigureBitrateObserver and VideoSendStreamImpl::
  // GetAllocationConfig do, and lifts the network controller's lower bound to the
  // sum of the enforced minimums. Call before Start(). `audio_wire_rate` is the
  // fixed audio rate (min = max = priority rate, enforce_min true, as for a
  // constant-bitrate Opus stream); zero means no audio stream. The video stream
  // (min `video_min`, max `video_max`, enforce_min true: suspend_below_min_bitrate
  // is off by default) is registered when `video` is true.
  void ConfigureAllocation(DataRate audio_wire_rate, bool video);

  // VideoSendStreamImpl's encoder activity check (video_send_stream_impl.cc:84,
  // 677-760, 846-875): every kEncoderTimeOut (2 s) from Start(), a video stream
  // that encoded no frame since the last check and has a non-zero encoder target
  // leaves the BitrateAllocator (SignalEncoderTimedOut); the first check that
  // sees a frame again, or the frame itself after a time-out (OnEncodedImage,
  // disable_padding_), registers it again (SignalEncoderActive), and so does an
  // encoder reconfiguration while the check runs (OnEncoderConfigurationChanged).
  // Each change of the allocation limits goes to GoogCC as OnStreamsConfig
  // (BitrateAllocator::UpdateAllocationLimits -> OnAllocationLimitsChanged), so a
  // stream that comes back raises max_total_allocated_bitrate and may start an
  // allocation probe (ProbeController::OnMaxTotalAllocatedBitrate). The video
  // stream's pad-up rate is 0 (CalculateMaxPadBitrateBps for one stream without
  // min_transmit_bitrate), so disable_padding_ changes no limit.
  void OnVideoFrameEncoded(Timestamp now);
  void OnVideoEncoderReconfigured(Timestamp now);
  bool video_registered() const { return video_registered_; }

  // The stream whose SSRC pads and probes. Not owned.
  void SetPaddingSender(RtpSender* sender);

  // RtpTransportControllerSend::SetPacingFactor: the pacing factor of the
  // streams (StreamsConfig::pacing_factor), which GoogCC uses in place of its
  // defaults (cc::GccController::SetPacingFactor). The session sets 1.1 when it
  // adds a video stream with transport-cc, as VideoSendStreamImpl does (#187).
  void SetPacingFactor(double factor);

  // RtpTransportControllerSend::EnablePeriodicAlrProbing (StreamsConfig::
  // requests_alr_probing): ALR probes every 5 s while application limited. The
  // session requests it for an audio stream with transport-cc, as
  // AudioSendStream::ConfigureStream does unless WebRTC-Audio-AlrProbing is
  // disabled (audio_send_stream.cc:158-159, 297-308; issue #62).
  void EnablePeriodicAlrProbing(bool enable);

  void Enqueue(SenderPacket p, Timestamp now);
  // Releases due packets, and runs the GCC timer.
  std::vector<ReleasedPacket> Process(Timestamp now);
  Timestamp NextProcessTime(Timestamp now) const;

  // Feedback from the peer.
  void OnTransportFeedback(const cc::TransportFeedback& fb, Timestamp now);
  void OnRtt(TimeDelta rtt) { gcc_.OnRttUpdate(rtt); }
  void OnRemb(DataRate remb) { gcc_.OnRemb(remb); }
  // Loss between two receiver reports (GoogCC OnTransportLossReport).
  void OnLossReport(int64_t packets_lost_delta, int64_t packets_delta, Timestamp now);

  // Encoder pause/resume and keyframe decisions since the last call. Nothing makes
  // them since the v1 outage handling was deleted (#167): libwebrtc has none.
  ControllerActions TakeActions();

  // Encoder side of congestion-window pushback: call once per captured frame
  // before encoding it; true means skip the frame (libwebrtc
  // VideoStreamEncoder::OnFrame, cwnd_frame_drop).
  bool DropCapturedFrame();

  DataRate target() const { return target_; }
  DataRate send_rate(Timestamp now);
  // RtpVideoSender::OnBitrateUpdated's payload rate: the video allocation less
  // the packet overhead rate, which FecControllerDefault::UpdateFecRates takes.
  // Zero without an allocation (ConfigureAllocation).
  DataRate VideoPayloadRate() const;
  SendRates ComputeRates(DataRate audio_wire_rate, double fec_protection) const;
  // As above, but the share taken off is what the sender actually spent on
  // RTX and FEC in the last second, exactly as libwebrtc's FecControllerDefault::
  // UpdateFecRates does: (sent_nack + sent_fec) / (video + nack + fec), capped at
  // `max_repair_share` (0.5).
  SendRates ComputeMeasuredRates(DataRate audio_wire_rate) const;
  // Share of the video stream's send rate that was RTX or FEC, as of the last Process(): of the bytes
  // released in the last `repair_window`, or with `send_rate_stats` of RtpSenderEgress::GetSendRates'
  // rates (RateStatistics over `repair_window`), where a repair stream that has just started counts at
  // its rate since it started.
  double repair_share() const;

  const cc::BitrateAllocatorV2& allocator() const { return allocator_; }
  cc::GccController& gcc() { return gcc_; }
  cc::Pacer& pacer() { return pacer_; }
  cc::OutageMonitor& outage() { return outage_; }
  const cc::CongestionWindowPushback& congestion_window() const { return cwnd_; }
  const SendSideStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  void Apply(const cc::NetworkControlUpdate& u, Timestamp now);
  SendRates AllocatedRates() const;
  void UpdatePushback(Timestamp now, bool window);
  void PostTargetRate(Timestamp now);
  void UpdateControlState(Timestamp now);
  // The congestion-window reduce ratio the encoder takes: the one last passed on with the handler.
  double reduce_ratio() const { return cfg_.control_handler ? reported_reduce_ratio_ : cwnd_.reduce_ratio(); }
  void ApplyPushback(SendRates& out) const;
  // `allocator v2`: the video allocation less the packet-overhead rate and the
  // protection share, at most the video maximum: the encoder target before the
  // congestion-window frame dropping (VideoStreamEncoder::UpdateTargetBitrate).
  // RtpVideoSender::OnBitrateUpdated and VideoSendStreamImpl::OnBitrateUpdated for a video allocation.
  struct VideoAllocationSplit {
    DataRate encoder_target;   // Capped at the encoder maximum.
    DataRate link_allocation;  // At least encoder_target.
    DataRate protection;       // Returned to the allocator.
  };
  VideoAllocationSplit SplitVideoAllocation(DataRate alloc) const;
  DataRate AllocatedVideoTarget(DataRate alloc) const;
  // RtpVideoSender::CalculateOverheadRate (frame-rate option off).
  DataRate OverheadRate(DataRate rate, int64_t packet_size_bytes) const;
  void WriteSendExtensions(RtpPacket& p, uint16_t transport_seq, Timestamp now) const;
  // The encoder activity task due at `at` (check_encoder_activity_task_).
  void CheckVideoActivity(Timestamp at);
  // Runs each activity task due at or before `now`, at its due time.
  void RunDueVideoActivityChecks(Timestamp now);
  void RegisterVideo(Timestamp now);    // AddObserver(this, GetAllocationConfig()).
  void UnregisterVideo(Timestamp now);  // RemoveObserver(this).
  // RtpTransportControllerSend::OnAllocationLimitsChanged -> GoogCC OnStreamsConfig.
  void NotifyAllocationLimits(Timestamp now);

  SendSideConfig cfg_;
  const sync::ExtensionMap* extensions_;
  cc::GccController gcc_;
  cc::Pacer pacer_;
  cc::OutageMonitor outage_;
  cc::CongestionWindowPushback cwnd_;
  cc::BitrateAllocatorV2 allocator_;
  bool allocation_configured_ = false;
  cc::CongestionControlHandler control_handler_;
  // The reduce ratio of the last update passed on (BitrateAllocationUpdate::cwnd_reduce_ratio).
  double reported_reduce_ratio_ = 0;
  // AllocatedVideoTarget() as of the last allocation (RtpVideoSender::encoder_target_rate_bps_).
  DataRate video_encoder_target_;
  DataRate video_link_allocation_;  // VideoSendStreamImpl's link_allocation as of the last allocation.
  DataRate video_allocation_;       // The video stream's last allocation, kept while it is unregistered.
  cc::StreamAllocationConfig video_alloc_cfg_;
  cc::BitrateAllocatorV2::Observer video_observer_;
  bool video_registered_ = false;
  bool video_activity_ = false;    // activity_
  bool video_timed_out_ = false;   // timed_out_
  bool video_disable_padding_ = true;  // disable_padding_
  Timestamp next_video_activity_check_ = Timestamp::PlusInfinity();
  cc::AllocationLimits notified_limits_;
  uint64_t video_timeouts_ = 0;
  DataRate pushback_target_;  // GoogCC last_pushback_target_rate_.
  DataRate window_target_;    // GoogCC last_loss_based_target_rate_: the target at the last pushback update.
  uint64_t cwnd_frame_counter_ = 0;
  uint64_t cwnd_frames_dropped_ = 0;
  RtpSender* padding_sender_ = nullptr;
  std::unordered_map<uint64_t, SenderPacket> queued_;
  // Media packets (and the FEC on the media SSRC) by transport-wide sequence number, so that
  // feedback can cull the acknowledged ones and resend the trailing lost ones
  // (RtpVideoSender::OnPacketFeedbackVector).
  struct SentMedia {
    RtpSender* sender;
    uint16_t seq;
  };
  std::unordered_map<uint16_t, SentMedia> sent_media_;
  uint64_t next_id_ = 1;
  uint16_t transport_seq_ = 0;
  struct Released {
    Timestamp time;
    int64_t bytes;
    PacketKind kind;
  };
  void ExpireRepair(Timestamp now);
  std::deque<Released> repair_window_;
  int64_t repair_bytes_ = 0, video_bytes_ = 0;
  // `send_rate_stats`: RtpSenderEgress::send_rates_, one BitrateTracker per media
  // type over `repair_window` (kBitrateStatisticsWindow, 1 s), read as of the last Process().
  RateStatistics video_rate_, rtx_rate_, fec_rate_;
  Timestamp last_process_ = Timestamp::MinusInfinity();
  DataRate target_;
  Timestamp last_gcc_ = Timestamp::MinusInfinity();
  std::deque<std::pair<Timestamp, int64_t>> window_;
  int64_t window_bytes_ = 0;
  ControllerActions actions_;
  SendSideStats stats_;
};

}  // namespace sm::rtp
