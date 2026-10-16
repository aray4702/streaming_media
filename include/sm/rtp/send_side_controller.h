// Send-side controller of the RTP session layer (docs/design/rtp-session.md,
// sections 5.1 and 7): one per transport, shared by every stream. It owns the
// pacer, the GCC network controller and the outage monitor, and ties them to
// the packets of rtp::RtpSender:
//   - Enqueue() hands a packet to the pacer with its priority;
//   - Process() releases what is due, writes the transport-wide sequence
//     number (and abs-send-time when negotiated), tells GCC about the send,
//     counts the bytes of the 500 ms send-rate window, and returns datagrams
//     ready for the socket;
//   - transport feedback, RTT and REMB go to GCC, and the new target and
//     pacing rate come back as `target()` and `rates()`;
//   - the GCC process interval (25 ms) and the outage monitor (10 ms) run
//     from Process(), and the outage monitor's decisions come out of
//     TakeActions().
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

#include "sm/cc/congestion_window_pushback.h"
#include "sm/cc/outage_monitor.h"
#include "sm/cc/pacer.h"
#include "sm/cc/send_side_bwe.h"
#include "sm/cc/transport_feedback.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"
#include "sm/rtp/rtp_sender.h"
#include "sm/sync/header_extensions.h"

namespace sm::rtp {

struct SendSideConfig {
  cc::GccConfig gcc;
  cc::PacerConfig pacer;
  cc::OutageConfig outage;
  cc::CongestionWindowConfig cwnd;
  // Bytes the transport adds to every datagram (UDP + IPv4), counted in the
  // pacer budget and in GCC's send history.
  int64_t transport_overhead_bytes = 28;
  TimeDelta gcc_interval = TimeDelta::Millis(25);
  TimeDelta outage_interval = TimeDelta::Millis(10);
  TimeDelta send_rate_window = TimeDelta::Millis(500);
  // Largest share of the sending rate repair traffic may take away from the
  // video encoder (libwebrtc FecControllerDefault::overhead_threshold_).
  double max_repair_share = 0.5;
  TimeDelta repair_window = TimeDelta::Seconds(1);
  DataRate video_min = DataRate::KilobitsPerSec(30);
  DataRate video_max = DataRate::KilobitsPerSec(10000);
  EventSink* events = nullptr;
};

// The split of the target the encoders need (design 7.3).
struct SendRates {
  DataRate target;       // GCC target for everything the session sends.
  DataRate audio_rate;   // Audio wire rate taken off first.
  DataRate video_target; // What the video encoder may produce (after congestion-window pushback).
  // Congestion-window pushback (issue #53): drop every N-th captured frame
  // (0 = none); `video_target` is already lowered to match. See
  // SendSideController::DropCapturedFrame().
  int frame_drop_interval = 0;
};

// A datagram released by the pacer.
struct ReleasedPacket {
  std::vector<uint8_t> bytes;  // RTP, ready for the socket (SRTP is the host's job).
  PacketKind kind = PacketKind::kVideo;
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
  int64_t released_bytes = 0;
};

class SendSideController : public MetricsSource {
 public:
  // `extensions` (not owned, may be null) maps the transport-wide sequence
  // number and abs-send-time; without them the packets carry neither.
  explicit SendSideController(SendSideConfig cfg = {}, const sync::ExtensionMap* extensions = nullptr);

  void Start(Timestamp now);

  // The stream whose SSRC pads and probes. Not owned.
  void SetPaddingSender(RtpSender* sender) { padding_sender_ = sender; }

  void Enqueue(SenderPacket p, Timestamp now);
  // Releases due packets, and runs the GCC and outage timers.
  std::vector<ReleasedPacket> Process(Timestamp now);
  Timestamp NextProcessTime(Timestamp now) const;

  // Feedback from the peer.
  void OnTransportFeedback(const cc::TransportFeedback& fb, Timestamp now);
  void OnRtt(TimeDelta rtt) { gcc_.OnRttUpdate(rtt); }
  void OnRemb(DataRate remb) { gcc_.OnRemb(remb); }

  // Decisions the outage monitor made since the last call.
  ControllerActions TakeActions();

  // Encoder side of congestion-window pushback: call once per captured frame
  // before encoding it; true means skip the frame (libwebrtc
  // VideoStreamEncoder::OnFrame, cwnd_frame_drop).
  bool DropCapturedFrame();

  DataRate target() const { return target_; }
  DataRate send_rate(Timestamp now);
  SendRates ComputeRates(DataRate audio_wire_rate, double fec_protection) const;
  // As above, but the share taken off is what the sender actually spent on
  // RTX and FEC in the last second, exactly as libwebrtc's FecControllerDefault::
  // UpdateFecRates does: (sent_nack + sent_fec) / (video + nack + fec), capped at
  // `max_repair_share` (0.5).
  SendRates ComputeMeasuredRates(DataRate audio_wire_rate) const;
  // Share of the non-audio bytes released in the last `repair_window` (as of the last Process()) that were RTX or FEC.
  double repair_share() const;

  cc::GccController& gcc() { return gcc_; }
  cc::Pacer& pacer() { return pacer_; }
  cc::OutageMonitor& outage() { return outage_; }
  const cc::CongestionWindowPushback& congestion_window() const { return cwnd_; }
  const SendSideStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  void Apply(const cc::NetworkControlUpdate& u);
  void UpdatePushback(Timestamp now, bool window);
  void ApplyPushback(SendRates& out) const;
  void WriteSendExtensions(RtpPacket& p, uint16_t transport_seq, Timestamp now) const;

  SendSideConfig cfg_;
  const sync::ExtensionMap* extensions_;
  cc::GccController gcc_;
  cc::Pacer pacer_;
  cc::OutageMonitor outage_;
  cc::CongestionWindowPushback cwnd_;
  uint64_t cwnd_frame_counter_ = 0;
  uint64_t cwnd_frames_dropped_ = 0;
  RtpSender* padding_sender_ = nullptr;
  std::unordered_map<uint64_t, SenderPacket> queued_;
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
  DataRate target_;
  Timestamp last_gcc_ = Timestamp::MinusInfinity();
  Timestamp last_outage_ = Timestamp::MinusInfinity();
  std::deque<std::pair<Timestamp, int64_t>> window_;
  int64_t window_bytes_ = 0;
  ControllerActions actions_;
  SendSideStats stats_;
};

}  // namespace sm::rtp
