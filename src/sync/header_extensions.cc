#include "sm/sync/header_extensions.h"

#include <algorithm>

namespace sm::sync {

std::optional<std::vector<HeaderExtension>> ParseHeaderExtensions(uint16_t profile, std::span<const uint8_t> b) {
  std::vector<HeaderExtension> out;
  if (profile == 0xBEDE) {
    size_t i = 0;
    while (i < b.size()) {
      uint8_t h = b[i];
      if (h == 0) {  // Padding.
        ++i;
        continue;
      }
      uint8_t id = h >> 4;
      size_t len = static_cast<size_t>(h & 0x0f) + 1;
      if (id == 15) break;  // Reserved: stop parsing (RFC 8285 4.2).
      if (i + 1 + len > b.size()) return std::nullopt;
      out.push_back({id, std::vector<uint8_t>(b.begin() + static_cast<std::ptrdiff_t>(i + 1),
                                              b.begin() + static_cast<std::ptrdiff_t>(i + 1 + len))});
      i += 1 + len;
    }
    return out;
  }
  if ((profile & 0xFFF0) == 0x1000) {
    size_t i = 0;
    while (i < b.size()) {
      if (b[i] == 0) {
        ++i;
        continue;
      }
      if (i + 2 > b.size()) return std::nullopt;
      uint8_t id = b[i];
      size_t len = b[i + 1];
      if (i + 2 + len > b.size()) return std::nullopt;
      out.push_back({id, std::vector<uint8_t>(b.begin() + static_cast<std::ptrdiff_t>(i + 2),
                                              b.begin() + static_cast<std::ptrdiff_t>(i + 2 + len))});
      i += 2 + len;
    }
    return out;
  }
  return std::nullopt;  // Unknown profile.
}

bool WriteHeaderExtensions(const std::vector<HeaderExtension>& ext, RtpPacket& p) {
  bool one_byte = std::all_of(ext.begin(), ext.end(), [](const HeaderExtension& e) {
    return e.id >= 1 && e.id <= 14 && !e.data.empty() && e.data.size() <= 16;
  });
  std::vector<uint8_t> body;
  if (one_byte) {
    for (const auto& e : ext) {
      body.push_back(static_cast<uint8_t>((e.id << 4) | (e.data.size() - 1)));
      body.insert(body.end(), e.data.begin(), e.data.end());
    }
    p.extension_profile = 0xBEDE;
  } else {
    for (const auto& e : ext) {
      if (e.id == 0 || e.data.size() > 255) return false;
      body.push_back(e.id);
      body.push_back(static_cast<uint8_t>(e.data.size()));
      body.insert(body.end(), e.data.begin(), e.data.end());
    }
    p.extension_profile = 0x1000;
  }
  while (body.size() % 4) body.push_back(0);
  p.extension = std::move(body);
  return true;
}

bool ExtensionMap::Register(uint8_t id, const std::string& uri) {
  if (id == 0) return false;
  for (const auto& [i, u] : by_id_) {
    if (u == uri && i != id) return false;
  }
  by_id_[id] = uri;
  return true;
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

void ExtensionAccessor::SetRaw(RtpPacket& p, const char* u, std::vector<uint8_t> data) const {
  auto id = map_.IdOf(u);
  if (!id) return;
  auto cur = ParseHeaderExtensions(p.extension_profile, p.extension).value_or(std::vector<HeaderExtension>{});
  std::erase_if(cur, [&](const HeaderExtension& e) { return e.id == *id; });
  cur.push_back({*id, std::move(data)});
  WriteHeaderExtensions(cur, p);
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

void ExtensionAccessor::SetString(RtpPacket& p, const char* u, const std::string& v) const {
  SetRaw(p, u, std::vector<uint8_t>(v.begin(), v.end()));
}

std::optional<std::string> ExtensionAccessor::GetString(const RtpPacket& p, const char* u) const {
  auto d = GetRaw(p, u);
  if (!d) return std::nullopt;
  return std::string(d->begin(), d->end());
}

void ExtensionAccessor::SetTransportSeq(RtpPacket& p, uint16_t seq) const {
  SetRaw(p, uri::kTransportCc, {static_cast<uint8_t>(seq >> 8), static_cast<uint8_t>(seq)});
}

std::optional<uint16_t> ExtensionAccessor::GetTransportSeq(const RtpPacket& p) const {
  auto d = GetRaw(p, uri::kTransportCc);
  if (!d || d->size() != 2) return std::nullopt;
  return static_cast<uint16_t>(((*d)[0] << 8) | (*d)[1]);
}

void ExtensionAccessor::SetAbsSendTime(RtpPacket& p, Timestamp t) const {
  // 6.18 fixed point seconds, wrapping every 64 s.
  uint32_t v = static_cast<uint32_t>(((t.us() << 18) + 500000) / 1000000) & 0xFFFFFF;
  SetRaw(p, uri::kAbsSendTime, {static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)});
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
