// 8.4 Stream identification: RTP header extensions (RFC 8285) and BUNDLE
// demultiplexing (RFC 8843 / RFC 8851).
//
// The id <-> URI map and the extension elements follow libwebrtc M157's
// RtpHeaderExtensionMap (modules/rtp_rtcp/source/rtp_header_extension_map.cc)
// and RtpPacket (rtp_packet.cc):
//  - Only the URIs libwebrtc knows can be registered, at ids 1-255; an id
//    already used by another URI, or a URI already at another id, is refused.
//  - Without extmap-allow-mixed (the default, RFC 8285 section 6) only the
//    one-byte form is written: ids 1-14, 1-16 bytes. With it, an element
//    that needs the two-byte form (id > 14 or > 16 bytes) promotes the
//    packet's elements to it (profile 0x1000).
//  - Setting an element that is already in the packet rewrites it in place,
//    and only with the same length.
//  - Parsing skips padding, stops at the one-byte reserved id 15 (or a
//    non-zero byte with id 0), keeps the elements before an oversized one,
//    and lets a repeated id overwrite the earlier value.
// Typed helpers cover the extensions WebRTC relies on: MID and
// RID/repaired-RID (strings), transport-wide sequence number, abs-send-time,
// audio level, video orientation and absolute capture time.
//
// With BUNDLE every stream shares one 5-tuple, so incoming packets are
// routed to the right receiver by, in order: an SSRC already bound to a
// sink; the MID (+ RID for simulcast) header extension, which also binds
// the SSRC for later packets that omit the extension; and finally a payload
// type that only one sink uses. Anything else is counted as unroutable.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sm/core/metrics.h"
#include "sm/core/rtp_packet.h"
#include "sm/core/units.h"

namespace sm::sync {

namespace uri {
inline constexpr const char* kMid = "urn:ietf:params:rtp-hdrext:sdes:mid";
inline constexpr const char* kRid = "urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id";
inline constexpr const char* kRepairedRid = "urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id";
inline constexpr const char* kAudioLevel = "urn:ietf:params:rtp-hdrext:ssrc-audio-level";
inline constexpr const char* kVideoOrientation = "urn:3gpp:video-orientation";
inline constexpr const char* kTransportCc = "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01";
inline constexpr const char* kAbsSendTime = "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time";
inline constexpr const char* kAbsCaptureTime = "http://www.webrtc.org/experiments/rtp-hdrext/abs-capture-time";
}  // namespace uri

// RtpHeaderExtensionId and RtpExtension limits (api/rtp_header_extension_id.h,
// api/rtp_parameters.h).
inline constexpr uint8_t kOneByteHeaderExtensionMaxId = 14;
inline constexpr size_t kOneByteHeaderExtensionMaxValueSize = 16;
inline constexpr size_t kMaxHeaderExtensionValueSize = 255;
inline constexpr uint16_t kOneByteExtensionProfileId = 0xBEDE;
inline constexpr uint16_t kTwoByteExtensionProfileId = 0x1000;

struct HeaderExtension {
  uint8_t id = 0;
  std::vector<uint8_t> data;
};

// Parses the extension block of a packet (profile + body) as
// RtpPacket::ParseBuffer does. nullopt for a profile that is neither form
// (libwebrtc then reads no elements).
std::optional<std::vector<HeaderExtension>> ParseHeaderExtensions(uint16_t profile, std::span<const uint8_t> body);
// Writes elements into the packet's extension block, choosing the
// one-byte form when every element fits it. Returns false if an element
// cannot be encoded (id 0, or more than 255 bytes).
bool WriteHeaderExtensions(const std::vector<HeaderExtension>& ext, RtpPacket& p);

// id <-> URI mapping negotiated in SDP (a=extmap), as RtpHeaderExtensionMap.
class ExtensionMap {
 public:
  ExtensionMap() = default;
  explicit ExtensionMap(bool extmap_allow_mixed) : extmap_allow_mixed_(extmap_allow_mixed) {}
  // RegisterByUri: false for an unknown URI, id 0, an id already used by
  // another URI, or a URI already registered at another id. Registering the
  // same pair again succeeds.
  bool Register(uint8_t id, const std::string& uri);
  void Deregister(const std::string& uri);
  std::optional<uint8_t> IdOf(const std::string& uri) const;
  std::optional<std::string> UriOf(uint8_t id) const;
  // a=extmap-allow-mixed: the two-byte form may be written (default off).
  bool extmap_allow_mixed() const { return extmap_allow_mixed_; }
  void SetExtmapAllowMixed(bool allow) { extmap_allow_mixed_ = allow; }
  // A URI RtpHeaderExtensionMap knows (its kExtensions table).
  static bool IsKnownUri(const std::string& uri);

 private:
  std::map<uint8_t, std::string> by_id_;
  bool extmap_allow_mixed_ = false;
};

// Typed accessors on a packet using an ExtensionMap.
class ExtensionAccessor {
 public:
  explicit ExtensionAccessor(const ExtensionMap& map) : map_(map) {}
  // Each setter returns false when the element can't be written (see
  // SetRaw); the packet is then unchanged.
  bool SetString(RtpPacket& p, const char* uri, const std::string& v) const;
  std::optional<std::string> GetString(const RtpPacket& p, const char* uri) const;
  bool SetTransportSeq(RtpPacket& p, uint16_t seq) const;
  std::optional<uint16_t> GetTransportSeq(const RtpPacket& p) const;
  // abs-send-time: 24-bit 6.18 fixed-point seconds.
  bool SetAbsSendTime(RtpPacket& p, Timestamp send_time) const;
  std::optional<uint32_t> GetAbsSendTime24(const RtpPacket& p) const;
  // RtpPacket::AllocateExtension: false if the URI is not registered, the
  // value is empty or too long for the map's form, the id needs the two-byte
  // form without extmap-allow-mixed, or the element is already in the packet
  // with another length.
  bool SetRaw(RtpPacket& p, const char* uri, std::vector<uint8_t> data) const;
  std::optional<std::vector<uint8_t>> GetRaw(const RtpPacket& p, const char* uri) const;

 private:
  const ExtensionMap& map_;
};

struct DemuxStats {
  uint64_t by_ssrc = 0, by_mid = 0, by_payload_type = 0, unroutable = 0, ssrc_bindings = 0;
};

class RtpDemuxer : public MetricsSource {
 public:
  explicit RtpDemuxer(const ExtensionMap& map) : access_(map) {}
  // A receiver for `mid` (and optionally a simulcast `rid`), with the
  // payload types it accepts.
  void AddSink(int sink, const std::string& mid, const std::string& rid, std::vector<uint8_t> payload_types);
  std::optional<int> Demux(const RtpPacket& p);
  const DemuxStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  struct Sink {
    int id;
    std::string mid, rid;
    std::vector<uint8_t> pts;
  };
  ExtensionAccessor access_;
  std::vector<Sink> sinks_;
  std::map<uint32_t, int> ssrc_to_sink_;
  DemuxStats stats_;
};

}  // namespace sm::sync
