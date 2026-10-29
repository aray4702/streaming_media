#include "sm/sync/header_extensions.h"

#include <algorithm>
#include <iterator>
#include <string_view>

namespace sm::sync {

namespace {

// RtpHeaderExtensionMap's kExtensions (rtp_header_extension_map.cc:40-62),
// with the URIs of api/rtp_parameters.h and rtp_header_extensions.h.
constexpr std::string_view kKnownUris[] = {
    "urn:ietf:params:rtp-hdrext:toffset",                                        // TransmissionOffset
    uri::kAudioLevel,                                                            // AudioLevelExtension
    "urn:ietf:params:rtp-hdrext:csrc-audio-level",                               // CsrcAudioLevel
    uri::kAbsSendTime,                                                           // AbsoluteSendTime
    uri::kAbsCaptureTime,                                                        // AbsoluteCaptureTimeExtension
    uri::kVideoOrientation,                                                      // VideoOrientation
    uri::kTransportCc,                                                           // TransportSequenceNumber
    "http://www.webrtc.org/experiments/rtp-hdrext/transport-wide-cc-02",         // TransportSequenceNumberV2
    "http://www.webrtc.org/experiments/rtp-hdrext/playout-delay",                // PlayoutDelayLimits
    "http://www.webrtc.org/experiments/rtp-hdrext/video-content-type",           // VideoContentTypeExtension
    "http://www.webrtc.org/experiments/rtp-hdrext/video-layers-allocation00",    // RtpVideoLayersAllocationExtension
    "http://www.webrtc.org/experiments/rtp-hdrext/video-timing",                 // VideoTimingExtension
    uri::kRid,                                                                   // RtpStreamId
    uri::kRepairedRid,                                                           // RepairedRtpStreamId
    uri::kMid,                                                                   // RtpMid
    "http://www.webrtc.org/experiments/rtp-hdrext/generic-frame-descriptor-00",  // RtpGenericFrameDescriptorExtension00
    "https://aomediacodec.github.io/av1-rtp-spec/#dependency-descriptor-rtp-header-extension",  // RtpDependencyDescriptorExtension
    "http://www.webrtc.org/experiments/rtp-hdrext/color-space",                  // ColorSpaceExtension
    "http://www.webrtc.org/experiments/rtp-hdrext/inband-cn",                    // InbandComfortNoiseExtension
    "http://www.webrtc.org/experiments/rtp-hdrext/video-frame-tracking-id",      // VideoFrameTrackingIdExtension
    "http://www.webrtc.org/experiments/rtp-hdrext/corruption-detection",         // CorruptionDetectionExtension
};

bool NeedsTwoByte(uint8_t id, size_t length) {
  // RFC 8285 4.2-4.3, as RtpPacket::AllocateRawExtension (rtp_packet.cc:273-277).
  return id > kOneByteHeaderExtensionMaxId || length > kOneByteHeaderExtensionMaxValueSize || length == 0;
}

std::vector<uint8_t> Encode(const std::vector<HeaderExtension>& ext, bool two_byte) {
  std::vector<uint8_t> body;
  for (const auto& e : ext) {
    if (two_byte) {
      body.push_back(e.id);
      body.push_back(static_cast<uint8_t>(e.data.size()));
    } else {
      body.push_back(static_cast<uint8_t>((e.id << 4) | (e.data.size() - 1)));
    }
    body.insert(body.end(), e.data.begin(), e.data.end());
  }
  while (body.size() % 4) body.push_back(0);  // SetExtensionLengthMaybeAddZeroPadding.
  return body;
}

}  // namespace

std::optional<std::vector<HeaderExtension>> ParseHeaderExtensions(uint16_t profile, std::span<const uint8_t> b) {
  // RtpPacket::ParseBuffer (rtp_packet.cc:512-571).
  const bool one_byte = profile == kOneByteExtensionProfileId;
  if (!one_byte && (profile & 0xFFF0) != kTwoByteExtensionProfileId) return std::nullopt;  // Unsupported.
  const size_t header = one_byte ? 1 : 2;
  constexpr uint8_t kPaddingByte = 0;
  constexpr uint8_t kOneByteHeaderExtensionReservedId = 15;
  std::vector<HeaderExtension> out;
  size_t i = 0;
  while (i + header < b.size()) {
    if (b[i] == kPaddingByte) {
      ++i;
      continue;
    }
    uint8_t id;
    size_t length;
    if (one_byte) {
      id = b[i] >> 4;
      length = 1 + (b[i] & 0x0f);
      // A non-zero byte with id 0 is not padding.
      if (id == kOneByteHeaderExtensionReservedId || id == 0) break;
    } else {
      id = b[i];
      length = b[i + 1];
    }
    if (i + header + length > b.size()) break;  // Oversized: keep what came before.
    std::vector<uint8_t> data(b.begin() + static_cast<std::ptrdiff_t>(i + header),
                              b.begin() + static_cast<std::ptrdiff_t>(i + header + length));
    auto it = std::find_if(out.begin(), out.end(), [id](const HeaderExtension& e) { return e.id == id; });
    if (it != out.end()) {
      it->data = std::move(data);  // Duplicate id: the later value wins.
    } else {
      out.push_back({id, std::move(data)});
    }
    i += header + length;
  }
  return out;
}

bool WriteHeaderExtensions(const std::vector<HeaderExtension>& ext, RtpPacket& p) {
  bool two_byte = false;
  for (const auto& e : ext) {
    if (e.id == 0 || e.data.size() > kMaxHeaderExtensionValueSize) return false;
    two_byte = two_byte || NeedsTwoByte(e.id, e.data.size());
  }
  p.extension_profile = two_byte ? kTwoByteExtensionProfileId : kOneByteExtensionProfileId;
  p.extension = Encode(ext, two_byte);
  return true;
}

bool ExtensionMap::IsKnownUri(const std::string& u) {
  return std::find(std::begin(kKnownUris), std::end(kKnownUris), u) != std::end(kKnownUris);
}

bool ExtensionMap::Register(uint8_t id, const std::string& uri) {
  // RegisterByUri and Register (rtp_header_extension_map.cc:100-110, 133-170).
  if (!IsKnownUri(uri)) return false;
  if (id == 0) return false;  // RtpHeaderExtensionId::kMinId is 1; kMaxId is 255.
  auto it = by_id_.find(id);
  if (it != by_id_.end()) return it->second == uri;  // Same pair again, or id used by another URI.
  if (IdOf(uri)) return false;                       // Illegal reregistration at another id.
  by_id_[id] = uri;
  return true;
}

void ExtensionMap::Deregister(const std::string& uri) {
  if (auto id = IdOf(uri)) by_id_.erase(*id);
}

std::optional<uint8_t> ExtensionMap::IdOf(const std::string& uri) const {
  for (const auto& [i, u] : by_id_) {
    if (u == uri) return i;
  }
  return std::nullopt;
}

std::optional<std::string> ExtensionMap::UriOf(uint8_t id) const {
  auto it = by_id_.find(id);
  if (it == by_id_.end()) return std::nullopt;
  return it->second;
}

bool ExtensionAccessor::SetRaw(RtpPacket& p, const char* u, std::vector<uint8_t> data) const {
  // RtpPacket::AllocateExtension (rtp_packet.cc:620-639).
  const bool mixed = map_.extmap_allow_mixed();
  const size_t length = data.size();
  if (length == 0 || length > kMaxHeaderExtensionValueSize || (!mixed && length > kOneByteHeaderExtensionMaxValueSize))
    return false;
  auto id = map_.IdOf(u);
  if (!id) return false;
  if (!mixed && *id > kOneByteHeaderExtensionMaxId) return false;
  // AllocateRawExtension (rtp_packet.cc:242-353).
  const bool had_block = !p.extension.empty();
  std::vector<HeaderExtension> cur;
  if (had_block) cur = ParseHeaderExtensions(p.extension_profile, p.extension).value_or(cur);
  auto it = std::find_if(cur.begin(), cur.end(), [&](const HeaderExtension& e) { return e.id == *id; });
  if (it != cur.end()) {
    if (it->data.size() != length) return false;  // Length mismatch.
    it->data = std::move(data);                   // Rewritten in place.
  } else {
    cur.push_back({*id, std::move(data)});
  }
  // The first element sets the profile; a two-byte element later promotes
  // the one-byte elements already written (PromoteToTwoByteHeaderExtension).
  const bool was_two_byte = had_block && (p.extension_profile & 0xFFF0) == kTwoByteExtensionProfileId;
  const bool two_byte = was_two_byte || NeedsTwoByte(*id, length);
  if (!was_two_byte) p.extension_profile = two_byte ? kTwoByteExtensionProfileId : kOneByteExtensionProfileId;
  p.extension = Encode(cur, two_byte);
  return true;
}

std::optional<std::vector<uint8_t>> ExtensionAccessor::GetRaw(const RtpPacket& p, const char* u) const {
  auto id = map_.IdOf(u);
  if (!id || p.extension.empty()) return std::nullopt;
  auto all = ParseHeaderExtensions(p.extension_profile, p.extension);
  if (!all) return std::nullopt;
  for (auto& e : *all) {
    if (e.id == *id) return e.data;
  }
  return std::nullopt;
}

bool ExtensionAccessor::SetString(RtpPacket& p, const char* u, const std::string& v) const {
  return SetRaw(p, u, std::vector<uint8_t>(v.begin(), v.end()));
}

std::optional<std::string> ExtensionAccessor::GetString(const RtpPacket& p, const char* u) const {
  auto d = GetRaw(p, u);
  if (!d) return std::nullopt;
  return std::string(d->begin(), d->end());
}

bool ExtensionAccessor::SetTransportSeq(RtpPacket& p, uint16_t seq) const {
  return SetRaw(p, uri::kTransportCc, {static_cast<uint8_t>(seq >> 8), static_cast<uint8_t>(seq)});
}

std::optional<uint16_t> ExtensionAccessor::GetTransportSeq(const RtpPacket& p) const {
  auto d = GetRaw(p, uri::kTransportCc);
  if (!d || d->size() != 2) return std::nullopt;
  return static_cast<uint16_t>(((*d)[0] << 8) | (*d)[1]);
}

bool ExtensionAccessor::SetAbsSendTime(RtpPacket& p, Timestamp t) const {
  // 6.18 fixed point seconds, wrapping every 64 s.
  uint32_t v = static_cast<uint32_t>(((t.us() << 18) + 500000) / 1000000) & 0xFFFFFF;
  return SetRaw(p, uri::kAbsSendTime, {static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)});
}

std::optional<uint32_t> ExtensionAccessor::GetAbsSendTime24(const RtpPacket& p) const {
  auto d = GetRaw(p, uri::kAbsSendTime);
  if (!d || d->size() != 3) return std::nullopt;
  return (uint32_t{(*d)[0]} << 16) | (uint32_t{(*d)[1]} << 8) | (*d)[2];
}

void RtpDemuxer::AddSink(int sink, const std::string& mid, const std::string& rid, std::vector<uint8_t> pts) {
  sinks_.push_back({sink, mid, rid, std::move(pts)});
}

std::optional<int> RtpDemuxer::Demux(const RtpPacket& p) {
  auto mid = access_.GetString(p, uri::kMid);
  auto rid = access_.GetString(p, uri::kRid);
  if (!rid) rid = access_.GetString(p, uri::kRepairedRid);
  // 1. MID (+RID) present: authoritative, (re)binds the SSRC.
  if (mid) {
    for (const auto& s : sinks_) {
      if (s.mid != *mid) continue;
      if (!s.rid.empty() && (!rid || *rid != s.rid)) continue;
      auto [it, inserted] = ssrc_to_sink_.insert_or_assign(p.ssrc, s.id);
      (void)it;
      if (inserted) ++stats_.ssrc_bindings;
      ++stats_.by_mid;
      return s.id;
    }
  }
  // 2. Known SSRC.
  if (auto it = ssrc_to_sink_.find(p.ssrc); it != ssrc_to_sink_.end()) {
    ++stats_.by_ssrc;
    return it->second;
  }
  // 3. Payload type unique to one sink.
  std::optional<int> match;
  int count = 0;
  for (const auto& s : sinks_) {
    if (std::find(s.pts.begin(), s.pts.end(), p.payload_type) != s.pts.end()) {
      match = s.id;
      ++count;
    }
  }
  if (count == 1) {
    ssrc_to_sink_[p.ssrc] = *match;
    ++stats_.ssrc_bindings;
    ++stats_.by_payload_type;
    return match;
  }
  ++stats_.unroutable;
  return std::nullopt;
}

void RtpDemuxer::ExportMetrics(MetricsWriter& w) const {
  w.Counter("by_ssrc", stats_.by_ssrc);
  w.Counter("by_mid", stats_.by_mid);
  w.Counter("by_payload_type", stats_.by_payload_type);
  w.Counter("unroutable", stats_.unroutable);
  w.Counter("ssrc_bindings", stats_.ssrc_bindings);
}

}  // namespace sm::sync
