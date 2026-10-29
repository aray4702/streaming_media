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
  // BaseRtpStringExtension::Parse (rtp_header_extensions.cc:753-763): an
  // empty value, or one starting with \0, is not a string; a \0 inside
  // ends it.
  auto d = GetRaw(p, u);
  if (!d || d->empty() || (*d)[0] == 0) return std::nullopt;
  return std::string(d->begin(), std::find(d->begin(), d->end(), uint8_t{0}));
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

bool RtpDemuxerCriteria::IsValid() const {
  // RtpDemuxerCriteria::IsValid (rtp_demuxer.cc:91-110); the string limit is
  // BaseRtpStringExtension::kMaxValueSizeBytes.
  if (!match_any && payload_types.empty() && ssrcs.empty() && mid.empty() && rsid.empty()) return false;
  return mid.size() <= kOneByteHeaderExtensionMaxValueSize && rsid.size() <= kOneByteHeaderExtensionMaxValueSize;
}

bool RtpDemuxer::IsEmpty() const {
  return sink_by_mid_.empty() && sink_by_ssrc_.empty() && sinks_by_pt_.empty() && sink_by_mid_and_rsid_.empty() &&
         sink_by_rsid_.empty() && !match_any_sink_;
}

bool RtpDemuxer::AddSink(int sink, const RtpDemuxerCriteria& c) {
  // RtpDemuxer::AddSink (rtp_demuxer.cc:140-196).
  if (!c.IsValid() || CriteriaWouldConflict(c)) return false;
  if (c.match_any) match_any_sink_ = sink;
  if (!c.mid.empty()) {
    if (c.rsid.empty()) {
      sink_by_mid_.emplace(c.mid, sink);
    } else {
      sink_by_mid_and_rsid_.emplace(std::make_pair(c.mid, c.rsid), sink);
    }
  } else if (!c.rsid.empty()) {
    sink_by_rsid_.emplace(c.rsid, sink);
  }
  for (uint32_t ssrc : c.ssrcs) {
    // A binding learned from packets gives way to a signalled SSRC.
    auto [it, inserted] = sink_by_ssrc_.emplace(ssrc, sink);
    if (!inserted && !signaled_ssrcs_.contains(ssrc)) it->second = sink;
    signaled_ssrcs_.insert(ssrc);
  }
  for (uint8_t pt : c.payload_types) sinks_by_pt_.emplace(pt, sink);
  RefreshKnownMids();
  return true;
}

bool RtpDemuxer::AddSink(int sink, const std::string& mid, const std::string& rid, std::vector<uint8_t> pts) {
  RtpDemuxerCriteria c;
  c.mid = mid;
  c.rsid = rid;
  c.payload_types = std::move(pts);
  return AddSink(sink, c);
}

bool RtpDemuxer::CriteriaWouldConflict(const RtpDemuxerCriteria& c) const {
  // RtpDemuxer::CriteriaWouldConflict (rtp_demuxer.cc:198-257).
  if (match_any_sink_) return true;
  if (c.match_any) return !IsEmpty();
  if (!c.mid.empty()) {
    if (c.rsid.empty()) {
      // A sink for this MID, or for this MID with some RSID, would be shadowed.
      if (known_mids_.contains(c.mid)) return true;
    } else {
      if (sink_by_mid_and_rsid_.contains(std::make_pair(c.mid, c.rsid))) return true;
      // The bare MID's sink would take every packet of this pair.
      if (sink_by_mid_.contains(c.mid)) return true;
    }
  }
  for (uint32_t ssrc : c.ssrcs) {
    if (sink_by_ssrc_.contains(ssrc) && signaled_ssrcs_.contains(ssrc)) return true;
  }
  return false;
}

void RtpDemuxer::RefreshKnownMids() {
  known_mids_.clear();
  for (const auto& [mid, sink] : sink_by_mid_) known_mids_.insert(mid);
  for (const auto& [key, sink] : sink_by_mid_and_rsid_) known_mids_.insert(key.first);
}

bool RtpDemuxer::RemoveSink(int sink) {
  // RtpDemuxer::RemoveSink (rtp_demuxer.cc:290-307). The MIDs and RSIDs
  // learned per SSRC are kept.
  for (uint32_t ssrc : GetSsrcsForSink(sink)) signaled_ssrcs_.erase(ssrc);
  auto by_value = [sink](const auto& e) { return e.second == sink; };
  size_t removed = std::erase_if(sink_by_mid_, by_value) + std::erase_if(sink_by_ssrc_, by_value) +
                   std::erase_if(sinks_by_pt_, by_value) + std::erase_if(sink_by_mid_and_rsid_, by_value) +
                   std::erase_if(sink_by_rsid_, by_value);
  if (match_any_sink_ == sink) {
    match_any_sink_.reset();
    ++removed;
  }
  RefreshKnownMids();
  return removed > 0;
}

void RtpDemuxer::RemoveAllSinks() {
  sink_by_mid_.clear();
  sink_by_ssrc_.clear();
  sinks_by_pt_.clear();
  sink_by_mid_and_rsid_.clear();
  sink_by_rsid_.clear();
  signaled_ssrcs_.clear();
  known_mids_.clear();
  match_any_sink_.reset();
  mid_by_ssrc_.clear();
  rsid_by_ssrc_.clear();
}

std::vector<uint32_t> RtpDemuxer::GetSsrcsForSink(int sink) const {
  std::vector<uint32_t> ssrcs;
  for (const auto& [ssrc, s] : sink_by_ssrc_) {
    if (s == sink) ssrcs.push_back(ssrc);
  }
  return ssrcs;
}

std::optional<int> RtpDemuxer::Demux(const RtpPacket& p) {
  // RtpDemuxer::ResolveSink (rtp_demuxer.cc:318-428).
  if (match_any_sink_) return match_any_sink_;
  // A repaired RSID wins over an RSID on the same packet.
  std::optional<std::string> packet_mid = use_mid_ ? access_.GetString(p, uri::kMid) : std::nullopt;
  std::optional<std::string> packet_rsid = access_.GetString(p, uri::kRepairedRid);
  if (!packet_rsid) packet_rsid = access_.GetString(p, uri::kRid);
  const uint32_t ssrc = p.ssrc;
  // RFC 8843: a packet with an unknown MID is dropped, even if its SSRC is bound.
  if (packet_mid && !known_mids_.contains(*packet_mid)) {
    ++stats_.unroutable;
    return std::nullopt;
  }
  // Learn the SSRC's MID and RSID even without a sink for them yet, so a
  // sink added later finds them.
  const std::string* mid = nullptr;
  if (packet_mid) {
    mid = &(mid_by_ssrc_[ssrc] = *packet_mid);
  } else if (auto it = mid_by_ssrc_.find(ssrc); it != mid_by_ssrc_.end()) {
    mid = &it->second;
  }
  const std::string* rsid = nullptr;
  if (packet_rsid) {
    rsid = &(rsid_by_ssrc_[ssrc] = *packet_rsid);
  } else if (auto it = rsid_by_ssrc_.find(ssrc); it != rsid_by_ssrc_.end()) {
    rsid = &it->second;
  }
  if (mid) {
    std::optional<int> sink;
    if (auto it = sink_by_mid_.find(*mid); it != sink_by_mid_.end()) {
      sink = it->second;
    } else if (rsid) {  // An RSID is scoped to its MID.
      if (auto it2 = sink_by_mid_and_rsid_.find(std::make_pair(*mid, *rsid)); it2 != sink_by_mid_and_rsid_.end()) {
        sink = it2->second;
      }
    }
    if (!sink) {
      // The MID has sinks only for other RSIDs: outside BUNDLE, dropped.
      ++stats_.unroutable;
      return std::nullopt;
    }
    AddSsrcSinkBinding(ssrc, *sink);
    ++(packet_mid ? stats_.by_mid : stats_.by_ssrc);
    return sink;
  }
  if (rsid) {
    if (auto it = sink_by_rsid_.find(*rsid); it != sink_by_rsid_.end()) {
      AddSsrcSinkBinding(ssrc, it->second);
      ++(packet_rsid ? stats_.by_rsid : stats_.by_ssrc);
      return it->second;
    }
  }
  // A signalled SSRC is trusted more than a payload type.
  if (auto it = sink_by_ssrc_.find(ssrc); it != sink_by_ssrc_.end()) {
    ++stats_.by_ssrc;
    return it->second;
  }
  if (use_payload_type_demuxing_) {
    // Legacy senders signal only the payload type; it routes only if one
    // sink has it.
    auto [first, last] = sinks_by_pt_.equal_range(p.payload_type);
    if (first != last && std::next(first) == last) {
      AddSsrcSinkBinding(ssrc, first->second);
      ++stats_.by_payload_type;
      return first->second;
    }
  }
  ++stats_.unroutable;
  return std::nullopt;
}

void RtpDemuxer::AddSsrcSinkBinding(uint32_t ssrc, int sink) {
  // RtpDemuxer::AddSsrcSinkBinding (rtp_demuxer.cc:487-507): at most
  // kMaxSsrcBindings, against a peer sending many SSRCs.
  if (sink_by_ssrc_.size() >= kMaxSsrcBindings) {
    ++stats_.ssrc_binding_limit;
    return;
  }
  auto [it, inserted] = sink_by_ssrc_.emplace(ssrc, sink);
  if (inserted) {
    ++stats_.ssrc_bindings;
  } else {
    it->second = sink;
  }
}

void RtpDemuxer::ExportMetrics(MetricsWriter& w) const {
  w.Counter("by_ssrc", stats_.by_ssrc);
  w.Counter("by_mid", stats_.by_mid);
  w.Counter("by_rsid", stats_.by_rsid);
  w.Counter("by_payload_type", stats_.by_payload_type);
  w.Counter("unroutable", stats_.unroutable);
  w.Counter("ssrc_bindings", stats_.ssrc_bindings);
  w.Counter("ssrc_binding_limit", stats_.ssrc_binding_limit);
}

}  // namespace sm::sync
