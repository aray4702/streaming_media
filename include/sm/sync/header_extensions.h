// 8.4 Stream identification: RTP header extensions (RFC 8285) and BUNDLE
// demultiplexing (RFC 8843 / RFC 8851).
//
// Extension elements are written in the one-byte form (profile 0xBEDE,
// ids 1-14, 1-16 bytes) when they fit, otherwise the two-byte form
// (profile 0x100x, ids 1-255, 0-255 bytes). Typed helpers cover the
// extensions WebRTC relies on: MID and RID/repaired-RID (strings),
// transport-wide sequence number, abs-send-time, audio level, video
// orientation and absolute capture time.
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

struct HeaderExtension {
  uint8_t id = 0;
  std::vector<uint8_t> data;
};

// Parses the extension block of a packet (profile + body).
std::optional<std::vector<HeaderExtension>> ParseHeaderExtensions(uint16_t profile, std::span<const uint8_t> body);
// Writes elements into the packet's extension block, choosing the
// one-byte form when possible. Returns false if an element cannot be
// encoded (id 0/15 in one-byte form is never used; id > 255 or > 255 bytes).
bool WriteHeaderExtensions(const std::vector<HeaderExtension>& ext, RtpPacket& p);

// id <-> URI mapping negotiated in SDP (a=extmap).
class ExtensionMap {
 public:
  bool Register(uint8_t id, const std::string& uri);
  std::optional<uint8_t> IdOf(const std::string& uri) const;
  std::optional<std::string> UriOf(uint8_t id) const;

 private:
  std::map<uint8_t, std::string> by_id_;
};

// Typed accessors on a packet using an ExtensionMap.
class ExtensionAccessor {
 public:
  explicit ExtensionAccessor(const ExtensionMap& map) : map_(map) {}
  void SetString(RtpPacket& p, const char* uri, const std::string& v) const;
  std::optional<std::string> GetString(const RtpPacket& p, const char* uri) const;
  void SetTransportSeq(RtpPacket& p, uint16_t seq) const;
  std::optional<uint16_t> GetTransportSeq(const RtpPacket& p) const;
  // abs-send-time: 24-bit 6.18 fixed-point seconds.
  void SetAbsSendTime(RtpPacket& p, Timestamp send_time) const;
  std::optional<uint32_t> GetAbsSendTime24(const RtpPacket& p) const;
  void SetRaw(RtpPacket& p, const char* uri, std::vector<uint8_t> data) const;
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
