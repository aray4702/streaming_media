// RTP sender of one media stream (docs/design/rtp-session.md, sections 4.3 and
// 7): turns encoded frames into RTP packets. It assigns sequence numbers,
// SSRC, payload type, timestamps and the marker bit, writes the MID header
// extension, wraps audio in RED (RFC 2198), keeps the packet history and
// answers NACKs with RTX (RFC 4588), and feeds the optional FEC encoder. It
// does not send anything: every packet is handed out through TakePackets()
// with the pacer priority it needs, and rtp::SendSideController paces, numbers
// (transport-wide sequence number) and serializes it.
//
// Payloads are opaque chunks here, as in the test bench today. A codec
// packetizer (H.264, plan PR 7) supplies the chunks. Takes `now`, reads no
// clock and starts no thread.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sm/cc/pacer.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/audio/audio_level.h"
#include "sm/core/rtp_packet.h"
#include "sm/core/units.h"
#include "sm/loss/fec.h"
#include "sm/loss/red.h"
#include "sm/loss/rtx.h"
#include "sm/rtp/rtcp_engine.h"
#include "sm/sync/header_extensions.h"

namespace sm::rtp {

enum class PacketKind : uint8_t { kAudio, kVideo, kRtx, kFec, kPadding };

class RtpSender;

// One RTP packet on its way to the pacer.
struct SenderPacket {
  RtpPacket packet;
  PacketKind kind = PacketKind::kVideo;
  cc::PacketPriority priority = cc::PacketPriority::kVideo;
  bool is_key_frame = false;  // Video media packet of a keyframe.
  bool first_packet_of_frame = false;
  RtpSender* sender = nullptr;  // The stream that made it, told when the pacer sends it.
};

struct RtpSenderConfig {
  uint32_t ssrc = 1;
  uint8_t payload_type = 96;
  int clock_rate = 90000;  // 90000 video, 48000 Opus.
  bool video = true;       // Picks the packet kind and pacer priority.
  uint16_t initial_seq = 0;
  // Largest payload of one video packet (the whole packet stays within
  // libwebrtc's kVideoMtu of 1200 bytes). Clamped so the whole packet (with
  // the 12-byte fixed header) is 100 to 1500 bytes, the range libwebrtc's
  // RTPSender::SetMaxRtpPacketSize accepts: a smaller value would leave no
  // room for padding or media (issue #257).
  size_t max_payload_size = 1200 - 12;

  // RTX (RFC 4588). Without an RTX SSRC, NACKs cannot be answered.
  std::optional<uint32_t> rtx_ssrc;
  uint8_t rtx_payload_type = 97;
  loss::RtxConfig rtx;  // History size; its SSRC and payload type are overwritten.

  // RED (RFC 2198), audio. `red_distance` previous frames are repeated.
  std::optional<uint8_t> red_payload_type;
  int red_distance = 1;

  // Forward error correction over the media packets (video).
  bool fec = false;
  loss::FecConfig fec_config;

  // MID (RFC 8843) is written on the first `mid_packets` packets, or until
  // AcknowledgeMid() when the peer has answered. Needs the extension mapped.
  std::string mid;
  int mid_packets = 0;
  const sync::ExtensionMap* extensions = nullptr;  // Not owned; must outlive the sender.

  EventSink* events = nullptr;
};

struct RtpSenderStats {
  uint64_t media_packets = 0;
  int64_t media_payload_bytes = 0;  // Payload octets of media packets, what the SR counts.
  uint64_t frames = 0;
  uint64_t fec_packets = 0;
  int64_t fec_payload_bytes = 0;
  uint64_t rtx_packets = 0;
  uint64_t padding_packets = 0;
  uint64_t payload_padding_packets = 0;  // RTX payload padding (GeneratePadding), a subset of padding_packets.
  uint64_t nacks_without_rtx = 0;
  uint64_t mid_packets = 0;
  uint64_t audio_level_packets = 0;
};

// Splits `bytes` into the fewest chunks of at most `max_payload` bytes, as
// even as possible (the first chunks take the remainder), like the test
// bench's frame packetization.
std::vector<size_t> SplitEvenly(size_t bytes, size_t max_payload);

class RtpSender : public MetricsSource {
 public:
  explicit RtpSender(RtpSenderConfig cfg = {});

  // Audio: one frame becomes one packet (RED-wrapped when configured).
  // With `level` and the audio-level extension (RFC 6464) mapped, the packet
  // carries it, as libwebrtc's ChannelSend does when the level id is set.
  void SendAudio(std::span<const uint8_t> frame, uint32_t rtp_timestamp, bool marker, Timestamp now,
                 std::optional<audio::AudioLevel> level = std::nullopt);
  // Header bytes of the next media packet with the extensions it will carry
  // (transport-cc, abs-send-time and, while it is repeated, MID): what
  // libwebrtc's RtpSenderVideo reads from the packet templates to size the
  // payload (rtp_sender_video.cc, `max_payload_len`).
  size_t MediaHeaderSize() const;
  // Video: one packetized frame. The marker is set on the last chunk. FEC
  // groups end at the frame boundary.
  // `keyframe` marks its media packets for the pacer (keyframe flush).
  void SendVideoFrame(std::span<const std::vector<uint8_t>> chunks, uint32_t rtp_timestamp, Timestamp now,
                      bool keyframe = false);

  // The pacer is releasing `p` (SendSideController::Process, before OnPacketReleased). Without
  // FEC the media packet gets its RTP sequence number here, as
  // libwebrtc's PacketRouter does (modules/pacing/packet_router.cc:217,
  // rtp_rtcp_impl2.cc:352); until then it carries 0. Otherwise the number was set when packetized.
  void AssignSequenceNumber(SenderPacket& p);
  // The pacer sent `p` (SendSideController::Process). The RTX history takes media packets
  // here (the packet as it goes out, RED-wrapped when RED is in use, so a resend is identical
  // apart from the RTX header) and learns that a resend left the queue.
  void OnPacketReleased(const SenderPacket& p, Timestamp now);
  // A measured RTT (history retention).
  void SetRtt(TimeDelta rtt, Timestamp now) { rtx_.SetRtt(rtt, now); }
  // Packets the peer acknowledged by transport-wide feedback leave the history.
  void OnAcknowledged(std::span<const uint16_t> seqs) { rtx_.CullAcknowledged(seqs); }

  // NACK from the peer (RtcpIncoming::nacks). The RTX packets are queued for
  // the pacer at retransmission priority.
  void OnNack(std::span<const uint16_t> seqs, Timestamp now, TimeDelta rtt);

  // A padding-only packet whose whole size is as close to `total_bytes` as
  // padding allows (at least a header plus one byte, at most 255 bytes of
  // padding). On the RTX SSRC when RTX is configured, like libwebrtc's
  // RTPSender::GeneratePadding, else on the media SSRC.
  SenderPacket MakePadding(size_t total_bytes, uint32_t rtp_timestamp, Timestamp now);

  // RTPSender::GeneratePadding (rtp_sender.cc:418-459), for a sender with RTX:
  // `target_bytes` of padding for the pacer, as a list of packets with their real
  // sizes. A video sender first answers with RTX payload padding, the newest large packet of
  // the history resent on the RTX SSRC (AppendPayloadPadding: only while at least
  // kMinPayloadPaddingBytes (50) are left, and never a packet that takes the total over
  // kMaxPaddingSizeFactor (3.0) x the target); the remainder is empty padding on the RTX
  // SSRC, packets of kMaxPaddingLength (255) bytes (an audio sender: of the bytes left, at
  // least kMinAudioPaddingLength (50)). Like libwebrtc, with RTX the empty padding waits for
  // a media packet unless transport-cc or abs-send-time is registered. All of them are
  // PacketKind::kPadding at padding priority; nothing is queued, the caller enqueues them.
  std::vector<SenderPacket> GeneratePadding(size_t target_bytes, uint32_t rtp_timestamp, Timestamp now);

  // The peer acknowledged the MID: stop repeating it.
  void AcknowledgeMid() { mid_remaining_ = 0; }

  // FEC protection level for the next frames (loss::ProtectionController).
  void SetFecParams(const loss::FecParams& p) { fec_.SetParams(p); }
  const loss::FecParams& fec_params() const { return fec_.params(); }

  // Everything queued since the last call, in the order to enqueue it.
  std::vector<SenderPacket> TakePackets();

  // Counters of the media stream for the RTCP sender report.
  RtcpSenderState SenderState() const;
  // RTP timestamp of the last media packet; padding carries it, like libwebrtc.
  uint32_t last_rtp_timestamp() const { return last_rtp_timestamp_; }
  const RtpSenderConfig& config() const { return cfg_; }
  const RtpSenderStats& stats() const { return stats_; }
  const loss::RtxSender& rtx() const { return rtx_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  RtpPacket NewPacket(uint8_t pt, uint32_t rtp_timestamp, bool marker);
  bool DefersSequenceNumbers() const;
  void Queue(RtpPacket p, PacketKind kind, cc::PacketPriority prio);
  void StoreAndQueueMedia(RtpPacket p, bool end_of_group, Timestamp now, bool keyframe = false, bool first = false);

  RtpSenderConfig cfg_;
  loss::RtxSender rtx_;
  loss::RedEncoder red_;
  loss::FecEncoder fec_;
  uint16_t next_seq_;
  int mid_remaining_;
  uint32_t last_rtp_timestamp_ = 0;
  Timestamp last_media_time_ = Timestamp::MinusInfinity();
  uint32_t sr_packets_ = 0, sr_octets_ = 0;
  std::vector<SenderPacket> out_;
  RtpSenderStats stats_;
};

}  // namespace sm::rtp
