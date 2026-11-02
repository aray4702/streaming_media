// A getStats-style snapshot of a Session (issue #988, requirement C8): what an
// app shows or uploads per stream (packets, bytes, loss, jitter, RTT, NACK and
// keyframe-request counts, jitter buffer and freeze stats), in the shape of the
// W3C webrtc-stats dictionaries that libwebrtc's RTCStatsCollector fills
// (outbound-rtp, inbound-rtp, remote-inbound-rtp, transport, candidate-pair).
// libwebrtc's api/stats/ and pc/rtc_stats_collector.cc are not in
// compare/webrtc/reference/, so the fields follow the W3C names, not that code.
//
// Computed only when the app calls Session::GetStats; nothing is kept for it
// in between. Built with SM_ENABLE_STATS=OFF, GetStats returns
// ObsStatus::kNotSupported (sm/core/observability.h).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sm/core/units.h"

namespace sm::rtp {

// RTCOutboundRtpStreamStats with the RTCRemoteInboundRtpStreamStats the peer's
// report blocks give for it.
struct OutboundRtpStats {
  uint32_t ssrc = 0;
  std::optional<uint32_t> rtx_ssrc;
  bool video = false;
  std::string mid;
  uint64_t packets_sent = 0;              // Media packets (RtpSenderStats::media_packets).
  uint64_t payload_bytes_sent = 0;        // Media payload octets.
  uint64_t frames_sent = 0;
  uint64_t retransmitted_packets_sent = 0;
  uint64_t fec_packets_sent = 0;
  uint64_t padding_packets_sent = 0;
  // From the latest report block about this SSRC (remote-inbound-rtp); absent before the first.
  std::optional<int64_t> remote_packets_lost;       // Cumulative.
  std::optional<double> remote_fraction_lost;       // 0 to 1, of the last report interval.
  std::optional<double> remote_jitter_ms;
  TimeDelta round_trip_time = TimeDelta::Zero();    // The stream's own RTT (its RTCP module); zero before one.
};

// RTCInboundRtpStreamStats.
struct InboundRtpStats {
  uint32_t ssrc = 0;
  bool video = false;
  uint64_t packets_received = 0;          // Media packets, RFC 3550 counting (receive statistics).
  int64_t packets_lost = 0;               // Cumulative, RFC 3550 (may be negative with duplicates).
  double fraction_lost = 0;               // Since the last report block built.
  double jitter_ms = 0;                   // RFC 3550 interarrival jitter.
  uint64_t rtx_packets_received = 0;
  uint64_t fec_packets_received = 0;
  uint64_t nack_count = 0;                // Sequence numbers asked for in NACKs (retries included).
  uint64_t pli_count = 0;                 // Video.
  uint64_t fir_count = 0;                 // Video.
  // Video.
  uint64_t frames_received = 0;           // Assembled.
  uint64_t frames_decodable = 0;          // Handed to the scheduler (decodable).
  uint64_t frames_rendered = 0;           // Render queue: shown.
  uint64_t frames_dropped = 0;            // Render queue: dropped without being shown.
  // Freeze stats (VideoQualityObserver's), from the app's video::FreezeDetector for this stream
  // (Session::SetFreezeDetector): the session decides which frames to render, the app shows them.
  std::optional<uint64_t> freeze_count;
  std::optional<double> total_freezes_duration_ms;
  std::optional<uint64_t> pause_count;
  std::optional<double> total_pauses_duration_ms;
  // Audio: the jitter buffer (NetEq's lifetime stats).
  uint64_t total_samples_received = 0;
  uint64_t concealed_samples = 0;
  uint64_t concealment_events = 0;
  uint64_t inserted_samples_for_deceleration = 0;
  uint64_t removed_samples_for_acceleration = 0;
  double jitter_buffer_delay_ms = 0;      // Sum over emitted samples (jitterBufferDelay), in ms.
  uint64_t jitter_buffer_emitted_count = 0;
  uint64_t packets_discarded = 0;         // Arrived too late or duplicate.
};

// RTCTransportStats with the session's counters. The ICE candidate pair and
// the DTLS state join it when the connectivity layer lands (#990, #991).
struct TransportStats {
  uint64_t datagrams_sent = 0;
  uint64_t datagrams_received = 0;
  uint64_t rtp_received = 0;
  uint64_t rtcp_received = 0;
  uint64_t malformed = 0;
  uint64_t unroutable = 0;
  TimeDelta round_trip_time = TimeDelta::Zero();  // The session's latest RTT.
  DataRate target_bitrate = DataRate::Zero();     // The congestion controller's target.
  uint64_t nack_sent = 0, pli_sent = 0, fir_sent = 0;
  uint64_t nack_received = 0, pli_received = 0, fir_received = 0;
};

struct StatsReport {
  Timestamp timestamp = Timestamp::MinusInfinity();  // The `now` of GetStats.
  TransportStats transport;
  std::vector<OutboundRtpStats> outbound;  // Audio streams first, then video, in the order added.
  std::vector<InboundRtpStats> inbound;    // Audio streams first, then video, in the order added.
};

}  // namespace sm::rtp
