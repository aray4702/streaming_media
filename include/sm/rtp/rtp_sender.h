// RTP sender of one media stream (docs/design/rtp-session.md, sections 4.3 and
// 7): turns encoded frames into RTP packets. It assigns sequence numbers,
// SSRC, payload type, timestamps and the marker bit, writes the MID header
// extension, wraps audio in RED (RFC 2198), keeps the packet history and
// answers NACKs with RTX (RFC 4588), and feeds the optional ULPFEC generator. It
// does not send anything: every packet is handed out through TakePackets()
// with the pacer priority it needs, and rtp::SendSideController paces, numbers
// (transport-wide sequence number) and serializes it.
//
// Payloads are opaque chunks here, as in the test bench today. A codec
// packetizer (H.264, plan PR 7) supplies the chunks. Takes `now`, reads no
// clock and starts no thread.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sm/cc/pacer.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/rate_statistics.h"
#include "sm/audio/audio_level.h"
#include "sm/core/rms_level.h"
#include "sm/core/rtp_packet.h"
#include "sm/core/units.h"
#include "sm/loss/red.h"
#include "sm/loss/ulpfec.h"
#include "sm/loss/rtx.h"
#include "sm/rtp/rtcp_engine.h"
#include "sm/sync/capture_time.h"
#include "sm/sync/header_extensions.h"
#include "sm/video/orientation.h"

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
  // ModuleRtpRtcpImpl2's default 1460 bytes, rtp_rtcp_impl2.cc:146-150; issue
  // #580). Clamped so the whole packet (with
  // the 12-byte fixed header) is 100 to 1500 bytes, the range libwebrtc's
  // RTPSender::SetMaxRtpPacketSize accepts: a smaller value would leave no
  // room for padding or media (issue #257).
  size_t max_payload_size = 1460 - 12;

  // RTX (RFC 4588). Without an RTX SSRC, NACKs cannot be answered.
  std::optional<uint32_t> rtx_ssrc;
  uint8_t rtx_payload_type = 97;
  loss::RtxConfig rtx;  // History size; its SSRC and payload type are overwritten.

  // RED (RFC 2198), audio. `red_distance` previous frames are repeated.
  std::optional<uint8_t> red_payload_type;
  int red_distance = 1;

  // Video RED (RFC 2198) + ULPFEC (RFC 5109), as libwebrtc's RTPSenderVideo
  // and RtpSenderEgress with a UlpfecGenerator: every media packet is wrapped
  // in RED, and the FEC packets go on the media SSRC.
  bool fec = false;
  loss::ulpfec::UlpfecConfig ulpfec;

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
  uint64_t video_orientation_packets = 0;  // Packets carrying the CVO extension.
  uint64_t abs_capture_time_packets = 0;   // Packets carrying the absolute capture time extension.
};

// Splits `bytes` into the fewest chunks of at most `max_payload` bytes, as
// even as possible (the first chunks take the remainder), like the test
// bench's frame packetization.
std::vector<size_t> SplitEvenly(size_t bytes, size_t max_payload);

class RtpSender : public MetricsSource {
 public:
  explicit RtpSender(RtpSenderConfig cfg = {});

  // Audio capture, before the encoder: one 10 ms frame of int16 samples (all
  // channels), after muting, as ChannelSend::ProcessAndEncodeAudio
  // (channel_send.cc:909-924). With the audio-level extension (RFC 6464)
  // mapped it feeds the stream's RmsLevel: `AnalyzeMuted` for a muted frame
  // after a muted one, `Analyze` otherwise. Without it, it does nothing.
  void AnalyzeAudioCapture(std::span<const int16_t> frame, bool muted);
  // Audio: one frame becomes one packet (RED-wrapped when configured).
  // With the audio-level extension (RFC 6464) mapped, every packet carries
  // the level, as ChannelSend::SendData and RTPSenderAudio::SendAudio
  // (channel_send.cc:389-396, rtp_sender_audio.cc:248-250): `level` when
  // given (the override; it also sets the voice bit), otherwise the
  // RmsLevel average of the capture frames analyzed since the last packet
  // (127 when none were), with the voice bit set unless the frame is an
  // Opus DTX frame (at most 2 bytes; AudioEncoderOpusImpl's `speech`,
  // opus_interface.cc:179-192).
  // With `capture_time` (the frame's capture time on the `now` clock), the
  // AbsoluteCaptureTimeSender decides per packet whether it carries the
  // absolute capture time extension: the capture time in full NTP precision,
  // offset 0, at the stream's clock rate, as RTPSenderAudio::SendAudio
  // (rtp_sender_audio.cc:142-157, 252-256).
  void SendAudio(std::span<const uint8_t> frame, uint32_t rtp_timestamp, bool marker, Timestamp now,
                 std::optional<audio::AudioLevel> level = std::nullopt,
                 std::optional<Timestamp> capture_time = std::nullopt);
  // Header bytes of the next media packet with the extensions it will carry
  // (transport-cc, abs-send-time and, while it is repeated, MID): what
  // libwebrtc's RtpSenderVideo reads from the packet templates to size the
  // payload (rtp_sender_video.cc, `max_payload_len`).
  size_t MediaHeaderSize() const;
  // Video: one packetized frame. The marker is set on the last chunk. With
  // FEC each packet is wrapped in RED (rtp_sender_video.cc BuildRedPayload).
  // `keyframe` marks its media packets for the pacer (keyframe flush). With
  // the video-orientation extension (CVO) mapped, the last packet carries
  // `rotation` on a keyframe, when it changed since the last frame, or when
  // it is not 0, as RTPSenderVideo::AddRtpHeaderExtensions
  // (rtp_sender_video.cc:388-408); the byte is ConvertVideoRotationToCVOByte
  // (rtp_cvo.h): the rotation alone, camera and flip bits 0.
  // `abs_capture_time` (from VideoAbsoluteCaptureTime) goes on the first
  // packet when the extension is mapped (rtp_sender_video.cc:425-428).
  void SendVideoFrame(std::span<const std::vector<uint8_t>> chunks, uint32_t rtp_timestamp, Timestamp now,
                      bool keyframe = false, video::Rotation rotation = video::Rotation::k0,
                      const std::optional<sync::AbsoluteCaptureTime>& abs_capture_time = std::nullopt);
  // RTPSenderVideo::SendVideo (rtp_sender_video.cc:690-707), once per frame
  // before packetization: the extension for a frame captured at
  // `capture_time` (on the `now` clock), whole milliseconds of NTP time and
  // offset 0, if AbsoluteCaptureTimeSender decides to send it at 90 kHz;
  // nullopt without a capture time. The sender's state moves whether or not
  // the extension is mapped, as libwebrtc's does.
  std::optional<sync::AbsoluteCaptureTime> VideoAbsoluteCaptureTime(uint32_t rtp_timestamp,
                                                                    std::optional<Timestamp> capture_time,
                                                                    Timestamp now);
  // How much larger the last packet's header of such a frame is than a middle
  // packet's: the packetizer's last_packet_reduction_len in
  // RTPSenderVideo::SendVideo (rtp_sender_video.cc:741-758). 0 unless the
  // frame carries CVO.
  size_t LastPacketReduction(bool keyframe, video::Rotation rotation) const;
  // The same for the first packet (first_packet_reduction_len): 0 unless it
  // carries the absolute capture time.
  size_t FirstPacketReduction(const std::optional<sync::AbsoluteCaptureTime>& abs_capture_time) const;
  // And for a frame that fits one packet, which carries both
  // (single_packet_reduction_len).
  size_t SinglePacketReduction(bool keyframe, video::Rotation rotation,
                               const std::optional<sync::AbsoluteCaptureTime>& abs_capture_time) const;

  // The pacer is releasing `p` (SendSideController::Process, before OnPacketReleased). A media or
  // FEC packet gets its RTP sequence number here, as libwebrtc's PacketRouter does
  // (modules/pacing/packet_router.cc:217, rtp_rtcp_impl2.cc:352); until then it carries 0.
  void AssignSequenceNumber(SenderPacket& p);
  // The pacer sent `p` (SendSideController::Process). The RTX history takes media packets
  // here (the packet as it goes out, RED-wrapped when RED is in use, so a resend is identical
  // apart from the RTX header) and learns that a resend left the queue. With FEC a video
  // packet goes to the ULPFEC generator without its RED header (RtpSenderEgress::SendPacket,
  // rtp_sender_egress.cc:186-216), so the FEC covers the numbered packet.
  void OnPacketReleased(const SenderPacket& p, Timestamp now);
  // The FEC packets the last OnPacketReleased generated, for the pacer to enqueue
  // (PacketRouter::FetchFec, PacingController::ProcessPackets). Their sequence numbers are
  // assigned when the pacer releases them.
  std::vector<SenderPacket> TakeFecPackets();
  // Bytes a video packet's payload leaves for FEC: RTPSenderVideo::FecPacketOverhead
  // (rtp_sender_video.cc:263-280), the ULPFEC header, the RED header and the header
  // extensions an FEC packet can carry (kFecOrPaddingExtensionSizes). 0 without FEC.
  size_t FecPacketOverhead() const;
  // RTPSender's max_padding_fec_packet_header_ (rtp_sender.cc:736-742): the fixed header plus
  // the registered extensions of kFecOrPaddingExtensionSizes (transport-cc, abs-send-time, MID
  // at its largest). A padding packet's padding is at most the whole-packet limit less this.
  size_t PaddingFecPacketHeaderSize() const;
  // RTPSender::RtxPacketOverhead (rtp_sender.cc:564-595): the room a media packet leaves for its
  // RTX resend, 0 without an RTX SSRC. kRtxHeaderSize (2, the original sequence number), plus,
  // while the media SSRC has been acked and the RTX SSRC not yet, the MID extension's room
  // (2 + its length; this sender has no RID) and 3 bytes for rounding the extensions up to 4.
  size_t RtxPacketOverhead() const;
  // RTPSender::ExpectedPerPacketOverhead, the max_media_packet_header_ of UpdateHeaderSizes
  // (rtp_sender.cc:537-540, :736-786): the fixed header plus the registered non-volatile extensions of
  // kVideoExtensionSizes or kAudioExtensionSizes (IsNonVolatile, :116-140): transport-cc,
  // abs-send-time, the audio level on audio, and MID at its largest (RtpMid::kMaxValueSizeBytes, 16)
  // while the media SSRC, or the RTX SSRC, has not been acked; plus kRtxHeaderSize (2) with an RTX
  // SSRC. RtpVideoSender::OnBitrateUpdated takes it as the packet overhead of its overhead rate
  // (rtp_video_sender.cc:869-882; issue #577).
  size_t ExpectedPerPacketOverhead() const;
  // RTPSender::OnReceivedAckOnSsrc / OnReceivedAckOnRtxSsrc (rtp_sender.cc:367-385): a report
  // block for the media or the RTX SSRC arrived (ModuleRtpRtcpImpl2::OnReceivedRtcpReportBlocks,
  // rtp_rtcp_impl2.cc:694-713).
  void OnReceivedAckOnSsrc() { ssrc_has_acked_ = true; }
  void OnReceivedAckOnRtxSsrc() { rtx_ssrc_has_acked_ = true; }
  bool ssrc_has_acked() const { return ssrc_has_acked_; }
  bool rtx_ssrc_has_acked() const { return rtx_ssrc_has_acked_; }
  // A measured RTT (history retention).
  void SetRtt(TimeDelta rtt, Timestamp now) { rtx_.SetRtt(rtt, now); }
  // Packets the peer acknowledged by transport-wide feedback leave the history.
  void OnAcknowledged(std::span<const uint16_t> seqs) { rtx_.CullAcknowledged(seqs); }

  // NACK from the peer (RtcpIncoming::nacks). The RTX packets are queued for
  // the pacer at retransmission priority.
  void OnNack(std::span<const uint16_t> seqs, Timestamp now, TimeDelta rtt);
  // Transport feedback reported these media packets lost with no later packet of
  // the stream received (RtpVideoSender::OnPacketFeedbackVector, early loss
  // detection): RTPSender::ReSendPacket for each, queued like a NACK's RTX.
  void OnEarlyLoss(std::span<const uint16_t> seqs, Timestamp now);

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

  // FEC protection for the next frames, for delta and key frames alike; the
  // generator applies them from the next protected packet.
  void SetFecParams(const loss::ulpfec::FecProtectionParams& p) { SetFecParams(p, p); }
  // RtpRtcp::SetFecProtectionParams: what loss::FecControllerDefault asks for each frame type.
  void SetFecParams(const loss::ulpfec::FecProtectionParams& delta, const loss::ulpfec::FecProtectionParams& key);
  // The delta-frame and key-frame parameters last set.
  const loss::ulpfec::FecProtectionParams& fec_params() const { return fec_params_; }
  const loss::ulpfec::FecProtectionParams& key_fec_params() const { return key_fec_params_; }
  // The ULPFEC generator, when FEC is on.
  const loss::ulpfec::UlpfecGenerator* ulpfec() const { return ulpfec_.get(); }

  // Everything queued since the last call, in the order to enqueue it.
  std::vector<SenderPacket> TakePackets();

  // Counters of the media stream for the RTCP sender report, and the send rate for its interval.
  RtcpSenderState SenderState(Timestamp now) const;
  // RtpSenderEgress::GetSendRates(now).Sum(): one BitrateTracker (1 s window) per packet kind,
  // fed with each released packet's RTP size (rtp_sender_egress.cc:49, 129, 495).
  DataRate SendRate(Timestamp now) const;
  // RTP timestamp of the last media packet; padding carries it, like libwebrtc.
  uint32_t last_rtp_timestamp() const { return last_rtp_timestamp_; }
  const RtpSenderConfig& config() const { return cfg_; }
  const RtpSenderStats& stats() const { return stats_; }
  const loss::RtxSender& rtx() const { return rtx_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  RtpPacket NewPacket(uint8_t pt, uint32_t rtp_timestamp, bool marker);
  void Queue(RtpPacket p, PacketKind kind, cc::PacketPriority prio);
  void StoreAndQueueMedia(RtpPacket p, Timestamp now, bool keyframe = false, bool first = false);
  bool SetsVideoRotation(bool keyframe, video::Rotation rotation) const;
  bool SetsAbsoluteCaptureTime(const std::optional<sync::AbsoluteCaptureTime>& a) const;
  void SetAbsoluteCaptureTime(RtpPacket& p, const sync::AbsoluteCaptureTime& a);
  // Header size of a media packet with the extensions every packet carries, plus CVO and the
  // absolute capture time when given (rtp_sender_video.cc:717, "set them").
  size_t HeaderSizeWith(std::optional<video::Rotation> rotation,
                        const std::optional<sync::AbsoluteCaptureTime>& abs_capture_time) const;

  RtpSenderConfig cfg_;
  loss::RtxSender rtx_;
  loss::RedEncoder red_;
  std::unique_ptr<loss::ulpfec::UlpfecGenerator> ulpfec_;
  loss::ulpfec::FecProtectionParams fec_params_;
  loss::ulpfec::FecProtectionParams key_fec_params_;
  std::vector<SenderPacket> fec_out_;
  uint16_t next_seq_;
  int mid_remaining_;
  video::Rotation last_rotation_ = video::Rotation::k0;  // RTPSenderVideo::last_rotation_.
  sync::AbsoluteCaptureTimeSender abs_capture_time_sender_;
  dsp::RmsLevel rms_level_;            // ChannelSend::rms_level_.
  bool previous_frame_muted_ = false;  // ChannelSend::previous_frame_muted_.
  bool ssrc_has_acked_ = false;
  bool rtx_ssrc_has_acked_ = false;
  uint32_t last_rtp_timestamp_ = 0;
  Timestamp last_media_time_ = Timestamp::MinusInfinity();
  uint32_t sr_packets_ = 0, sr_octets_ = 0;
  // Indexed by PacketKind (kAudio, kVideo, kRtx, kFec, kPadding).
  mutable std::array<RateStatistics, 5> send_rates_{RateStatistics(1000, 8000.0f), RateStatistics(1000, 8000.0f),
                                                    RateStatistics(1000, 8000.0f), RateStatistics(1000, 8000.0f),
                                                    RateStatistics(1000, 8000.0f)};
  std::vector<SenderPacket> out_;
  RtpSenderStats stats_;
};

}  // namespace sm::rtp
