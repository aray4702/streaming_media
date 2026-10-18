// 3.1 (part 2) Sender-side retransmission: packet history plus RTX encapsulation
// (RFC 4588), where the original sequence number (OSN) is prepended to the
// payload and the packet is sent on a separate SSRC and payload type.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <span>
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
  // 1: the ring above, filled when a packet is queued for the pacer. 2: libwebrtc's
  // RtpPacketHistory (modules/rtp_rtcp/source/rtp_packet_history.cc, #76 group 2): a
  // packet enters when the pacer sends it (OnPacketSent), is kept for max(3 x RTT, 1 s)
  // and culled after three times that (or once `number_to_store` packets are held and the
  // oldest has passed the retention), never more than kMaxCapacity; a packet already in the
  // pacer queue as a resend is not resent again; a repeat request within one RTT of the
  // last send is ignored. `history_capacity`, `max_age` and `min_resend_rtt_factor` are v1 only.
  int version = 1;
  size_t number_to_store = 600;  // kMinSendSidePacketHistorySize (call/rtp_video_sender.cc).
  EventSink* events = nullptr;
};

struct RtxStats {
  uint64_t packets_stored = 0;
  uint64_t nack_requests = 0;
  uint64_t retransmitted = 0;
  uint64_t not_found = 0;   // Evicted from history or too old.
  uint64_t throttled = 0;   // Requested again before rtt elapsed (v2: or already pending in the pacer).
  uint64_t culled_acked = 0;  // v2: removed because the peer acknowledged them.
  uint64_t payload_padding = 0;  // v2: RTX packets handed out as payload padding.
  int64_t bytes_retransmitted = 0;
};

class RtxSender : public MetricsSource {
 public:
  explicit RtxSender(RtxConfig cfg = {});
  // v1: called when the packet is queued. v2: called when the pacer sends it
  // (RtpPacketHistory::PutRtpPacket from RtpSenderEgress).
  void OnPacketSent(const RtpPacket& p, Timestamp now);
  // Returns ready-to-send RTX packets for the requested sequence numbers. v2 ignores `rtt`
  // and uses the one given to SetRtt() (the history's own, -inf until a measurement).
  std::vector<RtpPacket> OnNack(std::span<const uint16_t> seqs, Timestamp now, TimeDelta rtt);
  // v2 only (no effect on v1).
  // A measured RTT (RtpPacketHistory::SetRtt): lengthens the retention and culls.
  void SetRtt(TimeDelta rtt, Timestamp now);
  // The pacer sent the RTX packet for `original_seq` (RtpPacketHistory::MarkPacketAsSent).
  void MarkPacketAsSent(uint16_t original_seq, Timestamp now);
  // The queued RTX packet for `original_seq` left the pacer without being sent (a keyframe flushed
  // it; libwebrtc has no such flush). Clears `pending` so the packet can be resent and culled.
  void ClearPending(uint16_t original_seq);
  // The peer acknowledged these sequence numbers (RtpPacketHistory::CullAcknowledgedPackets).
  void CullAcknowledged(std::span<const uint16_t> seqs);
  // v2 only: RtpPacketHistory::GetPayloadPaddingPacket in PaddingMode::kRecentLargePacket (the
  // mode ModuleRtpRtcpImpl2 builds its history with). The packet is the most recent large
  // payload seen by OnPacketSent(), kept for padding even after it has left the history. It is
  // RTX-encapsulated only if `accept` takes the original (RTPSender::AppendPayloadPadding
  // refuses one that would overshoot the padding target), so a refused packet uses no RTX
  // sequence number.
  std::optional<RtpPacket> GetPayloadPaddingPacket(const std::function<bool(const RtpPacket&)>& accept);
  size_t history_size() const { return v2_.size(); }
  // Next sequence number of the RTX stream. Padding sent on the RTX SSRC
  // (rtp::RtpSender) takes its numbers here so they never collide with resends.
  uint16_t AllocateSeq() { return rtx_seq_++; }
  const RtxStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  struct Slot {
    bool valid = false;
    RtpPacket packet;
    Timestamp sent;
    Timestamp last_resent = Timestamp::MinusInfinity();
  };
  struct Stored {
    std::optional<RtpPacket> packet;  // Empty: a hole left by a removed packet.
    Timestamp send_time = Timestamp::MinusInfinity();
    bool pending = false;             // A resend is in the pacer queue.
    int times_retransmitted = 0;
  };
  int V2Index(uint16_t seq) const;
  Stored* V2Get(uint16_t seq);
  void V2Cull(Timestamp now);
  void V2Remove(size_t index);
  bool V2VerifyRtt(const Stored& s, Timestamp now) const;

  RtxConfig cfg_;
  std::vector<Slot> slots_;
  std::deque<Stored> v2_;
  std::optional<RtpPacket> large_payload_packet_;  // v2: RtpPacketHistory::large_payload_packet_.
  TimeDelta rtt_ = TimeDelta::MinusInfinity();
  uint16_t rtx_seq_ = 0;
  RtxStats stats_;
};

}  // namespace sm::loss
