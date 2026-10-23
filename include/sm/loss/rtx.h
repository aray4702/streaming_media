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
  uint32_t rtx_ssrc = 0;
  uint8_t rtx_payload_type = 97;
  // RTPSender's rtx_payload_type_map_: a packet with `red_payload_type` (video
  // RED, when ULPFEC is on) goes out with `rtx_red_payload_type` instead.
  std::optional<uint8_t> red_payload_type;
  uint8_t rtx_red_payload_type = 99;
  // libwebrtc's RtpPacketHistory (modules/rtp_rtcp/source/rtp_packet_history.cc, #76 group 2): a
  // packet enters when the pacer sends it (OnPacketSent), is kept for max(3 x RTT, 1 s) and
  // culled after three times that (or once `number_to_store` packets are held and the oldest
  // has passed the retention), never more than kMaxCapacity; a packet already in the pacer
  // queue as a resend is not resent again; a repeat request within one RTT of the last send
  // is ignored. (The v1 ring, filled when a packet was queued, was deleted in #167.)
  size_t number_to_store = 600;  // kMinSendSidePacketHistorySize (call/rtp_video_sender.cc).
  EventSink* events = nullptr;
};

struct RtxStats {
  uint64_t packets_stored = 0;
  uint64_t nack_requests = 0;
  uint64_t retransmitted = 0;
  uint64_t not_found = 0;   // Evicted from history or too old.
  uint64_t throttled = 0;   // Requested again within an RTT of the last send, or already pending in the pacer.
  uint64_t culled_acked = 0;  // Removed because the peer acknowledged them.
  uint64_t payload_padding = 0;  // RTX packets handed out as payload padding.
  int64_t bytes_retransmitted = 0;
};

class RtxSender : public MetricsSource {
 public:
  explicit RtxSender(RtxConfig cfg = {});
  // Called when the pacer sends the packet (RtpPacketHistory::PutRtpPacket from RtpSenderEgress).
  void OnPacketSent(const RtpPacket& p, Timestamp now);
  // Returns ready-to-send RTX packets for the requested sequence numbers. `rtt` is ignored: the
  // history uses the one given to SetRtt() (its own, -inf until a measurement).
  std::vector<RtpPacket> OnNack(std::span<const uint16_t> seqs, Timestamp now, TimeDelta rtt);
  // A measured RTT (RtpPacketHistory::SetRtt): lengthens the retention and culls.
  void SetRtt(TimeDelta rtt, Timestamp now);
  // The pacer sent the RTX packet for `original_seq` (RtpPacketHistory::MarkPacketAsSent).
  void MarkPacketAsSent(uint16_t original_seq, Timestamp now);
  // The peer acknowledged these sequence numbers (RtpPacketHistory::CullAcknowledgedPackets).
  void CullAcknowledged(std::span<const uint16_t> seqs);
  // RtpPacketHistory::GetPayloadPaddingPacket in PaddingMode::kRecentLargePacket (the
  // mode ModuleRtpRtcpImpl2 builds its history with). The packet is the most recent large
  // payload seen by OnPacketSent(), kept for padding even after it has left the history. It is
  // RTX-encapsulated only if `accept` takes the original (RTPSender::AppendPayloadPadding
  // refuses one that would overshoot the padding target), so a refused packet uses no RTX
  // sequence number.
  std::optional<RtpPacket> GetPayloadPaddingPacket(const std::function<bool(const RtpPacket&)>& accept);
  size_t history_size() const { return history_.size(); }
  // Next sequence number of the RTX stream. Padding sent on the RTX SSRC
  // (rtp::RtpSender) takes its numbers here so they never collide with resends.
  uint16_t AllocateSeq() { return rtx_seq_++; }
  const RtxStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  uint8_t RtxPayloadType(const RtpPacket& original) const;
  struct Stored {
    std::optional<RtpPacket> packet;  // Empty: a hole left by a removed packet.
    Timestamp send_time = Timestamp::MinusInfinity();
    bool pending = false;             // A resend went to the pacer and has not been sent (a flushed one stays pending).
    int times_retransmitted = 0;
  };
  int Index(uint16_t seq) const;
  Stored* Get(uint16_t seq);
  void Cull(Timestamp now);
  void Remove(size_t index);
  bool VerifyRtt(const Stored& s, Timestamp now) const;

  RtxConfig cfg_;
  std::deque<Stored> history_;
  std::optional<RtpPacket> large_payload_packet_;  // RtpPacketHistory::large_payload_packet_.
  TimeDelta rtt_ = TimeDelta::MinusInfinity();
  uint16_t rtx_seq_ = 0;
  RtxStats stats_;
};

}  // namespace sm::loss
