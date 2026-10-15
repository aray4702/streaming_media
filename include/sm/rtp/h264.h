// H.264 RTP payload format (RFC 6184), packetization-mode 1 (docs/design/
// rtp-session.md, sections 6.1 to 6.4). Follows libwebrtc's RtpPacketizerH264
// (modules/rtp_rtcp/source/rtp_format_h264.cc) and VideoRtpDepacketizerH264:
// single NAL unit packets, STAP-A aggregation and FU-A fragmentation. STAP-B,
// MTAP and FU-B belong to mode 2, which libwebrtc does not negotiate, and are
// rejected as malformed.
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

class H264Packetizer {
 public:
  // The payloads of the RTP packets of one access unit, in order, each at most
  // `max_payload` bytes (at least 3 is needed for FU-A; smaller is raised).
  // Empty input gives no payloads.
  static std::vector<std::vector<uint8_t>> Packetize(std::span<const uint8_t> annexb, size_t max_payload);
};

// What one RTP payload holds.
struct H264PayloadInfo {
  bool valid = false;  // False: malformed (the packet is dropped).
  bool first_packet_in_frame = false;
  bool idr = false;  // An IDR slice (nal_unit_type 5), or its first FU-A fragment.
  bool sps = false;
  bool pps = false;
  bool fu_start = false;  // An FU-A fragment with the start bit.
  bool fu_end = false;    // An FU-A fragment with the end bit.
  bool fu_continuation = false;  // An FU-A fragment without the start bit.
};

class H264Depacketizer {
 public:
  // Classifies one RTP payload without keeping state. A slice or IDR starts a
  // frame when first_mb_in_slice is 0, which in the first byte after the NAL
  // header is a leading 1 bit (ue(v) of 0); SPS, PPS, SEI and AUD always
  // start one, as they precede the slices of an access unit.
  static H264PayloadInfo Parse(std::span<const uint8_t> payload);

  // Rebuilds the Annex-B access unit from the payloads of one frame's packets,
  // in sequence order. False if a FU-A fragment sequence is inconsistent
  // (a fragment without its start, a start inside an unfinished unit, an end
  // missing) or a payload is malformed; `out` is left with what was appended.
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
