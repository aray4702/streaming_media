// 3.1 (part 2) Sender-side retransmission: packet history plus RTX encapsulation
// (RFC 4588), where the original sequence number (OSN) is prepended to the
// payload and the packet is sent on a separate SSRC and payload type.
#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/rtp_packet.h"
#include "sm/core/units.h"

namespace sm::loss {

RtpPacket MakeRtxPacket(const RtpPacket& original, uint32_t rtx_ssrc, uint8_t rtx_payload_type,
                        uint16_t rtx_seq);
// Restores the original packet. Returns nullopt for padding-only or
// malformed RTX packets.
std::optional<RtpPacket> RestoreFromRtx(const RtpPacket& rtx, uint32_t media_ssrc,
                                        uint8_t media_payload_type);

struct RtxConfig {
  size_t history_capacity = 1024;              // Packets (power of two not required).
  TimeDelta max_age = TimeDelta::Seconds(2);   // Do not resend older packets.
  double min_resend_rtt_factor = 1.0;          // Ignore repeat NACKs within rtt*f.
  uint32_t rtx_ssrc = 0;
  uint8_t rtx_payload_type = 97;
  EventSink* events = nullptr;
};

struct RtxStats {
  uint64_t packets_stored = 0;
  uint64_t nack_requests = 0;
  uint64_t retransmitted = 0;
  uint64_t not_found = 0;   // Evicted from history or too old.
  uint64_t throttled = 0;   // Requested again before rtt elapsed.
  uint64_t rate_limited = 0;  // Not resent: the retransmission rate limit was reached.
  int64_t bytes_retransmitted = 0;
};

class RtxSender : public MetricsSource {
 public:
  explicit RtxSender(RtxConfig cfg = {});
  void OnPacketSent(const RtpPacket& p, Timestamp now);
  // Returns ready-to-send RTX packets for the requested sequence numbers.
  std::vector<RtpPacket> OnNack(std::span<const uint16_t> seqs, Timestamp now, TimeDelta rtt);
  // Next sequence number of the RTX stream. Padding sent on the RTX SSRC
  // (rtp::RtpSender) takes its numbers here so they never collide with resends.
  uint16_t AllocateSeq() { return rtx_seq_++; }
  // Caps the bytes resent per second (the 1 s window of libwebrtc's
  // RateLimiter, which bounded retransmissions by the target rate). A NACK
  // that would exceed it is not answered; the receiver asks again or falls back
  // to a keyframe. Unlimited until set. Issue #21.
  void SetMaxRate(DataRate max) { max_rate_ = max; }
  DataRate max_rate() const { return max_rate_; }
  // Bytes resent in the last second, as a rate.
  DataRate resent_rate(Timestamp now);
  const RtxStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  void Expire(Timestamp now);
  struct Slot {
    bool valid = false;
    RtpPacket packet;
    Timestamp sent;
    Timestamp last_resent = Timestamp::MinusInfinity();
  };
  RtxConfig cfg_;
  std::vector<Slot> slots_;
  uint16_t rtx_seq_ = 0;
  DataRate max_rate_ = DataRate::PlusInfinity();
  std::deque<std::pair<Timestamp, int64_t>> window_;  // (time, bytes) of recent resends.
  int64_t window_bytes_ = 0;
  RtxStats stats_;
};

}  // namespace sm::loss
