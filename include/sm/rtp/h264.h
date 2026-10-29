// H.264 RTP payload format (RFC 6184), packetization-mode 1 (docs/design/
// rtp-session.md, sections 6.1 to 6.4). Follows libwebrtc's RtpPacketizerH264
// (modules/rtp_rtcp/source/rtp_format_h264.cc) and VideoRtpDepacketizerH264:
// single NAL unit packets, STAP-A aggregation and FU-A fragmentation. STAP-B,
// MTAP and FU-B belong to mode 2, which libwebrtc does not negotiate: its
// depacketizer takes them (and types 0 and 30-31) as single NAL units it reads
// nothing from, and so does ours.
//
// An access unit is handed over as an Annex-B byte stream (start codes
// 00 00 01 or 00 00 00 01). The packetizer removes the start codes, the
// depacketizer puts one back per NAL unit. The RTP marker bit goes on the last
// packet of the access unit (the caller sets it; RtpSender::SendVideoFrame
// does). No clock, no threads, no allocation outside the returned vectors.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "sm/core/rtp_packet.h"
#include "sm/rtp/rtp_receiver.h"

namespace sm::rtp {

// nal_unit_type values (RFC 6184 table 1 and H.264 table 7-1).
inline constexpr uint8_t kH264NalSlice = 1;
inline constexpr uint8_t kH264NalIdr = 5;
inline constexpr uint8_t kH264NalSei = 6;
inline constexpr uint8_t kH264NalSps = 7;
inline constexpr uint8_t kH264NalPps = 8;
inline constexpr uint8_t kH264NalAud = 9;
inline constexpr uint8_t kH264NalStapA = 24;
inline constexpr uint8_t kH264NalFuA = 28;

// NAL units of an Annex-B stream, start codes removed, as views into `annexb`.
// Zero-length units (back-to-back start codes) are skipped; bytes before the
// first start code are ignored. A stream with no start code yields one unit
// holding all of it, so a bare NAL unit is accepted too.
std::vector<std::span<const uint8_t>> SplitAnnexB(std::span<const uint8_t> annexb);

// libwebrtc's RtpPacketizer::PayloadSizeLimits (rtp_format.h): the payload
// room of a middle packet, and how much less the first, the last and a
// single packet of the frame have, because their headers carry more
// extensions.
struct PayloadSizeLimits {
  int max_payload_len = 1200 - 12;
  int first_packet_reduction_len = 0;
  int last_packet_reduction_len = 0;
  int single_packet_reduction_len = 0;
};

// libwebrtc's H264PacketizationMode (RFC 6184 packetization-mode), which
// RtpPacketizerH264 takes from RTPVideoHeaderH264 (rtp_format.cc:64-69).
enum class H264PacketizationMode {
  kNonInterleaved,  // Mode 1: single NAL unit, STAP-A and FU-A packets.
  kSingleNalUnit,   // Mode 0: one NAL unit per packet, no aggregation or fragmentation.
};

class H264Packetizer {
 public:
  // RtpPacketizerH264 (rtp_format_h264.cc) with the reductions of `limits`.
  // Mode 1 (GeneratePackets, l.70-100): a unit larger than the room of its
  // packet is split by FU-A with SplitAboutEqually, the rest are aggregated by
  // STAP-A. Mode 0 (PacketizeSingleNalu, l.204-226): each unit is one packet.
  // Empty when the frame cannot be packetized (max_payload_len of 2 or less for
  // an FU-A, no room for a byte in the first or last packet, or in mode 0 a unit
  // larger than the room of its packet).
  static std::vector<std::vector<uint8_t>> Packetize(std::span<const uint8_t> annexb, const PayloadSizeLimits& limits,
                                                     H264PacketizationMode mode = H264PacketizationMode::kNonInterleaved);

  // The payloads of the RTP packets of one access unit, in order, each at most
  // `max_payload` bytes (at least 3 is needed for FU-A; smaller is raised).
  // Empty input gives no payloads.
  static std::vector<std::vector<uint8_t>> Packetize(std::span<const uint8_t> annexb, size_t max_payload);
};

// What one RTP payload holds: VideoRtpDepacketizerH264::Parse's video header
// (video_rtp_depacketizer_h264.cc).
struct H264PayloadInfo {
  bool valid = false;  // False: Parse returns nullopt there (the packet is dropped).
  // is_first_packet_in_frame: an SPS, PPS, AUD or SEI unit, or a slice whose
  // first_mb_in_slice is 0 (in an FU-A, the first fragment of a slice only).
  bool first_packet_in_frame = false;
  bool idr = false;  // An IDR slice (nal_unit_type 5), or its first FU-A fragment.
  // frame_type kVideoFrameKey: an SPS or an IDR slice in a single NAL unit or
  // STAP-A, or any FU-A fragment of an IDR slice (ParseFuaNalu reads the
  // original NAL type from every fragment).
  bool keyframe = false;
  bool sps = false;  // An SPS that parsed (single NAL unit or STAP-A).
  bool pps = false;  // A PPS whose ids parsed (single NAL unit or STAP-A).
  // The SPS's resolution (video_header.width/height), 0 without an SPS.
  uint32_t width = 0;
  uint32_t height = 0;
  bool fu_start = false;  // An FU-A fragment with the start bit.
  bool fu_end = false;    // An FU-A fragment with the end bit.
  bool fu_continuation = false;  // An FU-A fragment without the start bit.
  // RTPVideoHeaderH264::nalus: each non-empty unit of a single NAL unit or
  // STAP-A with the ids it carries (SPS: its id; PPS: both; slice: its PPS
  // id), and an FU-A's first fragment with its slice's PPS id (-1 for an SPS
  // or PPS, whose ids an FU-A doesn't give). Other fragments have none.
  std::vector<VideoNaluInfo> nalus;
};

class H264Depacketizer {
 public:
  // VideoRtpDepacketizerH264::Parse without keeping state. A single NAL unit
  // or STAP-A is dropped when a unit is an SPS that SpsVuiRewriter cannot
  // parse, a PPS or slice whose ids don't parse, or a nested STAP-A or FU-A; an
  // FU-A when it is shorter than its two header bytes. Empty units are
  // skipped. The FU-A start and end bits are not checked, as in libwebrtc.
  static H264PayloadInfo Parse(std::span<const uint8_t> payload);

  // Rebuilds the Annex-B access unit from the payloads of one frame's packets,
  // in sequence order, as H264SpsPpsTracker::CopyAndFixBitstream: a start
  // code before each unit and before the first fragment of an FU-A. An SPS is
  // rewritten as the depacketizer rewrites it on receive (SpsVuiRewriter,
  // kIncoming). False if a payload is one Parse drops; `out` is left with
  // what was appended.
  static bool Reassemble(std::span<const RtpPacket> packets, std::vector<uint8_t>& out);
};

// VideoPayloadParser for H.264: tells the receiver where frames start and
// whether they are keyframes. By default an IDR slice alone makes a keyframe
// (libwebrtc's default); the receiver's `keyframe_needs_parameter_sets`
// requires SPS and PPS as well (libwebrtc's sps-pps-idr-in-keyframe mode).
class H264PayloadParser : public VideoPayloadParser {
 public:
  VideoPayloadInfo Parse(const RtpPacket& p) const override;
  bool Reassemble(std::span<const RtpPacket> packets, std::vector<uint8_t>& out) const override {
    return H264Depacketizer::Reassemble(packets, out);
  }
};

}  // namespace sm::rtp
