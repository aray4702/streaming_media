#include "sm/video/codec_negotiation.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace sm::video {

namespace {
std::string Upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
std::string Param(const CodecCapability& c, const std::string& key, const std::string& def) {
  auto it = c.params.find(key);
  return it == c.params.end() ? def : it->second;
}
}  // namespace

std::optional<H264ProfileLevel> ParseH264ProfileLevelId(const std::string& s) {
  if (s.size() != 6) return std::nullopt;
  unsigned v = 0;
  for (char c : s) {
    v <<= 4;
    if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
    else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
    else return std::nullopt;
  }
  unsigned profile_idc = v >> 16, iop = (v >> 8) & 0xff, level = v & 0xff;
  H264ProfileLevel pl;
  pl.level_idc = static_cast<int>(level);
  bool cs1 = iop & 0x40, cs3 = iop & 0x10;
  // RFC 6184 Table 5 (simplified to the profiles WebRTC uses).
  if (profile_idc == 0x42) pl.profile = cs1 ? H264Profile::kConstrainedBaseline : H264Profile::kBaseline;
  else if (profile_idc == 0x4d) pl.profile = (iop & 0x80) ? H264Profile::kConstrainedBaseline : H264Profile::kMain;
  else if (profile_idc == 0x58) pl.profile = cs1 ? H264Profile::kConstrainedBaseline : H264Profile::kUnknown;
  else if (profile_idc == 0x64) pl.profile = (iop & 0x0c) == 0x0c ? H264Profile::kConstrainedHigh : H264Profile::kHigh;
  else return std::nullopt;
  (void)cs3;
  if (pl.profile == H264Profile::kUnknown) return std::nullopt;
  return pl;
}

std::string H264ProfileLevelIdString(const H264ProfileLevel& pl) {
  unsigned pidc = 0x42, iop = 0;
  switch (pl.profile) {
    case H264Profile::kConstrainedBaseline: pidc = 0x42; iop = 0xe0; break;
    case H264Profile::kBaseline: pidc = 0x42; iop = 0x00; break;
    case H264Profile::kMain: pidc = 0x4d; iop = 0x00; break;
    case H264Profile::kHigh: pidc = 0x64; iop = 0x00; break;
    case H264Profile::kConstrainedHigh: pidc = 0x64; iop = 0x0c; break;
    case H264Profile::kUnknown: break;
  }
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%02x%02x%02x", pidc, iop, static_cast<unsigned>(pl.level_idc));
  return buf;
}

bool IsSameCodec(const CodecCapability& a, const CodecCapability& b) {
  if (Upper(a.name) != Upper(b.name)) return false;
  const std::string n = Upper(a.name);
  if (n == "H264") {
    auto pa = ParseH264ProfileLevelId(Param(a, "profile-level-id", "42e01f"));
    auto pb = ParseH264ProfileLevelId(Param(b, "profile-level-id", "42e01f"));
    if (!pa || !pb || pa->profile != pb->profile) return false;
    return Param(a, "packetization-mode", "0") == Param(b, "packetization-mode", "0");
  }
  if (n == "VP9") return Param(a, "profile-id", "0") == Param(b, "profile-id", "0");
  if (n == "AV1") return Param(a, "profile", "0") == Param(b, "profile", "0");
  return true;
}

std::vector<CodecCapability> NegotiateCodecs(const std::vector<CodecCapability>& offer,
                                             const std::vector<CodecCapability>& local,
                                             NegotiationOptions opts) {
  // Local preference order; hardware first if requested (stable).
  std::vector<const CodecCapability*> prefs;
  for (const auto& c : local) prefs.push_back(&c);
  if (opts.prefer_hardware) {
    std::stable_sort(prefs.begin(), prefs.end(), [](auto* a, auto* b) { return a->hardware && !b->hardware; });
  }
  std::vector<CodecCapability> answer;
  for (const CodecCapability* l : prefs) {
    if (Upper(l->name) == "RTX") continue;
    for (const CodecCapability& o : offer) {
      if (Upper(o.name) == "RTX" || !IsSameCodec(o, *l)) continue;
      bool dup = std::any_of(answer.begin(), answer.end(), [&](const auto& a) { return a.payload_type == o.payload_type; });
      if (dup) continue;
      CodecCapability a = *l;
      a.payload_type = o.payload_type;
      a.params = o.params;
      if (Upper(a.name) == "H264") {
        auto po = ParseH264ProfileLevelId(Param(o, "profile-level-id", "42e01f"));
        auto pl = ParseH264ProfileLevelId(Param(*l, "profile-level-id", "42e01f"));
        H264ProfileLevel r = *po;
        r.level_idc = std::min(po->level_idc, pl->level_idc);
        a.params["profile-level-id"] = H264ProfileLevelIdString(r);
      }
      a.feedback.clear();
      for (const auto& f : o.feedback) {
        if (std::find(l->feedback.begin(), l->feedback.end(), f) != l->feedback.end()) a.feedback.push_back(f);
      }
      answer.push_back(a);
      break;  // One offered entry per local codec.
    }
  }
  // RTX for each kept codec, using the offer's RTX payload type.
  std::vector<CodecCapability> rtx;
  for (const CodecCapability& o : offer) {
    if (Upper(o.name) != "RTX") continue;
    int apt = std::atoi(Param(o, "apt", "-1").c_str());
    bool kept = std::any_of(answer.begin(), answer.end(), [&](const auto& a) { return a.payload_type == apt; });
    if (kept) rtx.push_back(o);
  }
  answer.insert(answer.end(), rtx.begin(), rtx.end());
  return answer;
}

}  // namespace sm::video
