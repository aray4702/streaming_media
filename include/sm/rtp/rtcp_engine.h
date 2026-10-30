// RTCP engine of the RTP session layer (docs/design/rtp-session.md, section 8):
// schedules sender and receiver reports, builds the compound datagrams that
// carry them and the early feedback (NACK, PLI, FIR, loss notification,
// transport-cc, REMB), and splits incoming compounds into typed results for
// the caller to dispatch. Follows libwebrtc's RTCPSender and RTCPReceiver:
// reports every 1 s with video and 5 s without (randomized 0.5 to 1.5x with
// libwebrtc's webrtc::Random draws, #517), a first report at about half the
// interval, SR/RR first and SDES CNAME after it, and early feedback sent at
// once.
//
// The engine owns no media state. Report blocks come from the registered
// timing::ReceiveStatistics, sender counters are pushed in with
// SetSenderState, and the caller decides when feedback is wanted (the NACK,
// keyframe and transport-cc modules keep their own throttles). Takes `now`,
// reads no clock and starts no thread.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sm/cc/transport_feedback.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/random.h"
#include "sm/core/units.h"
#include "sm/loss/rtcp_feedback.h"
#include "sm/rtp/rtcp_sdes_bye.h"
#include "sm/timing/rtcp_stats.h"

namespace sm::rtp {

struct RtcpConfig {
  uint32_t local_ssrc = 1;  // RTCP sender SSRC of reports and feedback.
  // SDES CNAME. Every SR carries SDES, with an empty CNAME if this is empty;
  // an RR carries it when this is set (RTCPSender::PrepareReport,
  // rtcp_sender.cc:713).
  std::string cname;
  // Whether an RR carries the CNAME. rtp::Session turns it off: its RR stands
  // for the video receive stream's receiver-only RtpRtcp module, which gets no
  // CNAME (CreateRtpRtcpModule, video/rtp_video_stream_receiver2.cc:124-158;
  // only call/rtp_video_sender.cc:271 and audio/channel_send.cc:804 call
  // SetCNAME), so libwebrtc's receiver sends no SDES (issue #820).
  bool cname_in_receiver_report = true;
  bool has_video = true;    // Picks the default report interval.
  // Nominal interval between reports; zero = 1 s with video, 5 s audio only
  // (libwebrtc's rtcp_report_interval_ms).
  TimeDelta report_interval = TimeDelta::Zero();
  // Time from the first Process call to the first report; zero = half the
  // nominal interval with no random draw, like RTCPSender's constructor. Lets the test bench keep its report phases.
  TimeDelta first_report_delay = TimeDelta::Zero();
  // Intervals follow libwebrtc's `RTCPSender::ComputeTimeUntilNextReport`
  // (#76): every interval is a whole number of milliseconds drawn from
  // [0.5, 1.5] x the minimum, at least 1 ms, and a video sender above
  // 72 kbit/s uses min(360 / kbit/s s, interval) (RFC 3550 6.2).
  // Reduced-size RTCP (RFC 5506): early feedback is sent without the leading
  // SR/RR. Only when the peer negotiated it.
  bool reduced_size = false;
  // Receiver reference time reports (RFC 3611 RRTR), libwebrtc's
  // `rtcp_xr.receiver_reference_time_report`: while the endpoint sends no
  // media, every report carries an RRTR, and the peer's DLRR echo gives the
  // RTT a receive-only stream measures (RTCPReceiver::HandleXrDlrrReportBlock).
  // Received RRTRs are echoed in a DLRR whatever this says, as RTCPSender does.
  bool xr_rrtr = false;
  // Datagrams are split before they pass this size: RTCPSender's
  // max_packet_size_, which ModuleRtpRtcpImpl2 sets to IP_PACKET_SIZE -
  // kTcpOverIpv4HeaderSize = 1460 (rtp_rtcp_impl2.cc:146-150, 463). A
  // video-sending rtp::Session lowers it to min(max_packet_size, 1500 -
  // transport overhead), as RtpVideoSender (rtp_video_sender.cc:272, 860-865).
  size_t max_datagram_bytes = 1460;
  // Seeds the interval draws. libwebrtc seeds each RTCPSender with its
  // clock's TimeInMicroseconds() when it is built (rtcp_sender.cc:130); the
  // caller passes that time, as the library reads no clock (#517).
  uint64_t seed = 1;
  EventSink* events = nullptr;
};

// What the caller knows about the media it sends, for the SR.
struct RtcpSenderState {
  uint32_t ssrc = 0;
  uint32_t packet_count = 0;
  uint32_t octet_count = 0;
  // The RTP timestamp that corresponded to `reference_time`; the SR carries
  // this extrapolated to the report time with `clock_rate`.
  uint32_t rtp_timestamp = 0;
  Timestamp reference_time = Timestamp::Millis(0);
  int clock_rate = 90000;
  // The sender's rate over the last second, every packet type with its RTP
  // header (ModuleRtpRtcpImpl2::GetFeedbackState: RtpSenderEgress::GetSendRates(now).Sum()),
  // for the video report interval. Empty: derived from `octet_count`.
  std::optional<DataRate> send_bitrate;
};

enum class RtcpOutKind { kReport, kFeedback };

struct RtcpOutgoing {
  std::vector<uint8_t> bytes;  // One datagram, ready for the socket.
  RtcpOutKind kind = RtcpOutKind::kReport;
};

struct RtcpRemb {
  DataRate bitrate = DataRate::Zero();
  std::vector<uint32_t> ssrcs;
};

// What one incoming datagram contained. Nothing is dispatched by the engine;
// the caller routes each list (design section 8.3):
//   sender_reports      -> ReceiveStatistics::OnSenderReport, sync::RtpToNtp
//   report_blocks, rtts -> GCC OnRttUpdate, NackTracker, KeyframeRequester,
//                          RtxSender, FecControllerDefault
//   transport_feedback  -> GccController::OnTransportFeedback, OutageMonitor
//   nacks               -> RtxSender::OnNack
//   keyframe_requests   -> encoder (PLI, and FIR once per seq_nr)
//   remb                -> GccController::OnRemb
struct RtcpIncoming {
  bool valid = false;  // False when the datagram was malformed.
  std::vector<timing::SenderReport> sender_reports;
  std::vector<timing::ReportBlock> report_blocks;  // From SR and RR, all senders.
  std::vector<TimeDelta> rtts;                     // One per usable report block.
  std::vector<uint32_t> rtt_ssrcs;                 // The reported (local) SSRC of each entry in rtts.
  std::vector<TimeDelta> xr_rtts;  // One per DLRR item that echoes our RRTR (with xr_rrtr).
  std::vector<loss::Nack> nacks;
  std::vector<cc::TransportFeedback> transport_feedback;
  std::vector<uint32_t> keyframe_requests;  // Media SSRC of each PLI / new FIR.
  std::vector<loss::LossNotification> loss_notifications;  // Informational only.
  std::vector<RtcpRemb> remb;
  std::vector<SdesChunk> sdes;
  std::vector<Bye> byes;
  size_t ignored = 0;  // Packets of a type the library does not interpret.
};

struct RtcpStats {
  uint64_t reports_sent = 0, sr_sent = 0, rr_sent = 0;
  uint64_t feedback_datagrams = 0;
  uint64_t nack_sent = 0, nack_seqs_sent = 0, pli_sent = 0, fir_sent = 0, lntf_sent = 0;
  uint64_t transport_cc_sent = 0, remb_sent = 0;
  uint64_t bytes_sent = 0;
  uint64_t datagrams_received = 0, malformed = 0, ignored = 0;
  uint64_t sr_received = 0, rr_received = 0, report_blocks_received = 0;
  uint64_t nack_received = 0, pli_received = 0, fir_received = 0, fir_duplicates = 0;
  uint64_t lntf_received = 0, transport_cc_received = 0, remb_received = 0;
  uint64_t sdes_received = 0, bye_received = 0;
  uint64_t xr_sent = 0, rrtr_sent = 0, dlrr_items_sent = 0;
  uint64_t xr_received = 0, rrtr_received = 0, dlrr_items_received = 0;
};

class RtcpEngine : public MetricsSource {
 public:
  // Incoming FIR sequence numbers are remembered for at most this many
  // (sender, media) pairs; the table is cleared when it fills.
  static constexpr size_t kMaxFirTracked = 64;
  // RTCPReceiver's kMaxNumberOfStoredRrtrs: RRTR senders remembered until the
  // next report echoes them.
  static constexpr size_t kMaxStoredRrtrs = 300;

  explicit RtcpEngine(RtcpConfig cfg = {});

  // --- Setup (before traffic) -------------------------------------------------
  // Sender side: the SR is sent instead of an RR once this is set.
  void SetSenderState(const RtcpSenderState& s);
  // Receive statistics whose report block goes into every report. Not owned.
  void AddReceiveStatistics(timing::ReceiveStatistics* stats);
  // Local media SSRCs, to pick the report blocks that describe our own
  // streams for the RTT. With none registered every block counts.
  void AddLocalMediaSsrc(uint32_t ssrc);
  // RTCPSender::SetMaxRtpPacketSize: the size datagrams are split at.
  void SetMaxDatagramBytes(size_t bytes) { cfg_.max_datagram_bytes = bytes; }
  size_t max_datagram_bytes() const { return cfg_.max_datagram_bytes; }

  // --- Egress -----------------------------------------------------------------
  // Early feedback; sent by the next Process. Several in one Process share one
  // compound datagram (split at max_datagram_bytes). The sender SSRC of every
  // message is set to cfg.local_ssrc.
  void QueueNack(uint32_t media_ssrc, std::span<const uint16_t> seqs);
  void QueuePli(uint32_t media_ssrc);
  void QueueFir(uint32_t media_ssrc);  // The engine numbers the requests.
  void QueueLossNotification(loss::LossNotification l);
  void QueueTransportFeedback(cc::TransportFeedback fb);
  void QueueRemb(DataRate bitrate, std::span<const uint32_t> ssrcs);
  // Announces that the local sources leave the session (RFC 3550 6.6). It goes
  // out with the full report and last in its compound, as RTCPSender's BYE
  // (rtcp_sender.cc:525-530, 631-650), which carries the local SSRC and the
  // CSRCs and no reason. libwebrtc M157 never sends one: nothing sets
  // kRtcpBye, and RTCPSender::SetSendingStatus(false) only clears the sending
  // flag (rtcp_sender.cc:199-204) despite the comments in rtp_rtcp_impl2.cc
  // and channel_send.cc. rtp::Session doesn't call it either (issue #820).
  void QueueBye(std::span<const uint32_t> ssrcs, std::string reason = {});

  // `RTCPSender::TimeToSendRTCPReport(send_keyframe_before_rtp)`: a report is
  // due. With a keyframe about to be sent on a video stream a
  // report due within 100 ms counts, so it goes out before the large frame.
  bool ReportDue(Timestamp now, bool keyframe_before_rtp = false) const;
  // Sends the report now (`SendRTCP(kRtcpReport)`) and schedules the next one
  // an interval from `now`. Returns nothing before the first Process.
  std::vector<RtcpOutgoing> SendReport(Timestamp now);

  // Emits the report if one is due and all queued feedback.
  std::vector<RtcpOutgoing> Process(Timestamp now);
  // When Process must run next: now if feedback is queued, else the next report.
  Timestamp NextProcessTime(Timestamp now) const;

  // --- Ingress ----------------------------------------------------------------
  RtcpIncoming OnRtcp(std::span<const uint8_t> datagram, Timestamp now);

  const timing::RttEstimator& rtt() const { return rtt_; }
  const RtcpStats& stats() const { return stats_; }
  size_t fir_tracked() const { return fir_seq_in_.size(); }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  TimeDelta Interval();
  TimeDelta NominalInterval() const;
  std::vector<uint8_t> BuildReport(Timestamp now);
  void AppendReport(Timestamp now, std::vector<std::vector<uint8_t>>& packets);
  void EmitReport(Timestamp now, std::vector<RtcpOutgoing>& out);
  // RTCPSender::BuildExtendedReports for a report: an RRTR while sending no
  // media (xr_rrtr), and a DLRR item per RRTR received since the last report
  // (RTCPReceiver::ConsumeReceivedXrReferenceTimeInfo). Empty when neither.
  std::vector<uint8_t> BuildExtendedReports(Timestamp now);
  std::vector<uint8_t> LeadingReport() const;
  void Flush(std::vector<std::vector<uint8_t>>& packets, RtcpOutKind kind, std::vector<RtcpOutgoing>& out);

  RtcpConfig cfg_;
  XorshiftRandom rng_;
  std::optional<RtcpSenderState> sender_;
  std::vector<timing::ReceiveStatistics*> receive_stats_;
  std::vector<uint32_t> local_ssrcs_;
  Timestamp next_report_ = Timestamp::PlusInfinity();
  bool started_ = false;
  // Queued feedback, already serialized, in the order queued. The tag counts it.
  enum class Tag : uint8_t { kNack, kPli, kFir, kLntf, kTcc, kRemb, kBye };
  static bool CarriesReport(Tag tag);
  struct Queued {
    Tag tag;
    size_t units;  // Sequence numbers for a NACK, else 1.
    std::vector<uint8_t> bytes;
  };
  std::vector<Queued> queue_;
  std::map<uint32_t, uint8_t> fir_seq_out_;                       // media ssrc -> next seq_nr
  std::map<std::pair<uint32_t, uint32_t>, uint8_t> fir_seq_in_;   // (sender, media) -> last seq_nr
  void UpdateSendRate(Timestamp now);
  std::deque<std::pair<Timestamp, uint32_t>> rate_samples_;
  DataRate send_rate_ = DataRate::Zero();
  timing::RttEstimator rtt_;
  // RTCPReceiver::RrtrInformation, oldest first; one per sender SSRC.
  struct ReceivedRrtr {
    uint32_t ssrc;
    uint32_t remote_compact;  // The RRTR's NTP, compact.
    uint32_t local_compact;   // Our NTP when it arrived, compact.
  };
  std::deque<ReceivedRrtr> received_rrtrs_;
  RtcpStats stats_;
};

}  // namespace sm::rtp
