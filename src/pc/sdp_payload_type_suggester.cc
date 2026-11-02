// libwebrtc M157 pc/sdp_payload_type_suggester.cc, with call/payload_type_picker's pickers and recorders.
#include "sm/pc/sdp_payload_type_suggester.h"

#include <cctype>

namespace sm::pc {

namespace {

constexpr int kFirstDynamicPayloadTypeUpperRange = 96;
constexpr int kLastDynamicPayloadTypeUpperRange = 127;
constexpr int kFirstDynamicPayloadTypeLowerRange = 35;
constexpr int kLastDynamicPayloadTypeLowerRange = 63;

bool SameName(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

// PayloadTypeRecorder's match: the same codec for SDP, by name, and for RTX and RED the same associated payload type.
bool SameForRecorder(const Codec& a, const Codec& b) {
  if (!SameName(a.name, b.name) || !MatchesWithCodecRules(a, b)) return false;
  if (a.IsResiliencyCodec()) {
    std::string apt_a, apt_b;
    a.GetParam(kCodecParamAssociatedPayloadType, &apt_a);
    b.GetParam(kCodecParamAssociatedPayloadType, &apt_b);
    return apt_a == apt_b;
  }
  return true;
}

const Codec* LookupCodec(const std::map<int, Codec>& map, int payload_type) {
  auto it = map.find(payload_type);
  return it == map.end() ? nullptr : &it->second;
}

std::optional<int> LookupPayloadType(const std::map<int, Codec>& map, const Codec& codec) {
  for (const auto& [pt, c] : map) {
    if (SameForRecorder(c, codec)) return pt;
  }
  return std::nullopt;
}

}  // namespace

std::string SdpPayloadTypeSuggester::RecorderName(std::string_view mid) const {
  const auto& groups = has_pending_ ? pending_bundle_groups_ : bundle_groups_;
  for (const ContentGroup& group : groups) {
    if (group.HasContentName(mid) && group.FirstContentName()) return *group.FirstContentName();
  }
  return std::string(mid);
}

SdpPayloadTypeSuggester::Recorder& SdpPayloadTypeSuggester::LookupRecorder(std::string_view mid) {
  return recorders_[RecorderName(mid)];
}

std::optional<int> SdpPayloadTypeSuggester::SuggestPayloadType(std::string_view mid, const Codec& codec) {
  Recorder& recorder = LookupRecorder(mid);
  if (auto local = LookupPayloadType(recorder.local_payload_types, codec)) return local;
  if (auto remote = LookupPayloadType(recorder.remote_payload_types, codec)) {
    if (!LookupCodec(recorder.local_payload_types, *remote)) {
      AddLocalMapping(mid, *remote, codec);
      return remote;
    }
    // In use locally, possibly for something else: pick a new one.
  }
  auto is_free = [&](int pt) {
    return !LookupCodec(recorder.local_payload_types, pt) && !LookupCodec(recorder.remote_payload_types, pt);
  };
  std::optional<int> result;
  if (IsValidRtpPayloadType(codec.id) && is_free(codec.id)) {
    result = codec.id;
  } else {
    for (int pt = kFirstDynamicPayloadTypeUpperRange; !result && pt <= kLastDynamicPayloadTypeUpperRange; ++pt) {
      if (is_free(pt)) result = pt;
    }
    for (int pt = kFirstDynamicPayloadTypeLowerRange; !result && pt <= kLastDynamicPayloadTypeLowerRange; ++pt) {
      if (is_free(pt)) result = pt;
    }
  }
  if (result) {
    Codec mapped = codec;
    mapped.id = *result;
    recorder.local_payload_types[*result] = mapped;
    seen_payload_types_.insert(*result);
  }
  return result;
}

bool SdpPayloadTypeSuggester::AddLocalMapping(std::string_view mid, int payload_type, const Codec& codec) {
  Recorder& recorder = LookupRecorder(mid);
  const Codec* existing = LookupCodec(recorder.local_payload_types, payload_type);
  if (existing && !SameForRecorder(*existing, codec)) return false;
  recorder.local_payload_types[payload_type] = codec;
  seen_payload_types_.insert(payload_type);
  return true;
}

std::optional<int> SdpPayloadTypeSuggester::SuggestRtpHeaderExtensionId(std::string_view mid,
                                                                       const RtpExtension& extension,
                                                                       RtpTransceiverIdDomain id_domain) {
  Recorder& recorder = LookupRecorder(mid);
  const ExtensionKey key(extension.uri, extension.encrypt);
  for (const auto& [id, k] : recorder.header_extensions) {
    if (k == key) return id;
  }
  const int max_id = id_domain == RtpTransceiverIdDomain::kTwoByteAllowed ? RtpExtension::kMaxId
                                                                           : RtpExtension::kOneByteHeaderExtensionMaxId;
  auto is_free = [&](int id) {
    auto it = extension_ids_.find(id);
    if (it != extension_ids_.end() && it->second != key) return false;
    auto rit = recorder.header_extensions.find(id);
    return rit == recorder.header_extensions.end() || rit->second == key;
  };
  std::optional<int> result;
  if (extension.id >= RtpExtension::kMinId && extension.id <= max_id && is_free(extension.id)) {
    result = extension.id;
  } else {
    for (int id = RtpExtension::kOneByteHeaderExtensionMaxId; !result && id >= RtpExtension::kMinId; --id) {
      if (is_free(id)) result = id;
    }
    for (int id = RtpExtension::kOneByteHeaderExtensionMaxId + 1; !result && id <= max_id; ++id) {
      if (is_free(id)) result = id;
    }
  }
  if (result) {
    recorder.header_extensions[*result] = key;
    extension_ids_[*result] = key;
  }
  return result;
}

bool SdpPayloadTypeSuggester::AddRtpHeaderExtensionMapping(std::string_view mid, const RtpExtension& extension,
                                                          bool /*local*/) {
  Recorder& recorder = LookupRecorder(mid);
  const ExtensionKey key(extension.uri, extension.encrypt);
  extension_ids_[extension.id] = key;
  auto it = recorder.header_extensions.find(extension.id);
  if (it != recorder.header_extensions.end() && it->second != key) {
    it->second = key;
    return false;
  }
  recorder.header_extensions[extension.id] = key;
  return true;
}

bool SdpPayloadTypeSuggester::Update(const SessionDescription* description, bool local, p2p::SdpType type) {
  if (!description) return false;
  // BundleManager::Update / Commit.
  pending_bundle_groups_.clear();
  for (const ContentGroup* group : description->GetGroupsByName(GROUP_TYPE_BUNDLE)) {
    if (group->FirstContentName()) pending_bundle_groups_.push_back(*group);
  }
  has_pending_ = true;
  if (type == p2p::SdpType::kAnswer) {
    bundle_groups_ = pending_bundle_groups_;
    has_pending_ = false;
  }
  std::map<std::string, std::set<int>> in_use;
  bool ok = true;
  for (const ContentInfo& content : description->contents()) {
    if (content.rejected) continue;
    const std::string name = RecorderName(content.mid());
    Recorder& recorder = recorders_[name];
    auto& map = local ? recorder.local_payload_types : recorder.remote_payload_types;
    for (const Codec& codec : content.media_description()->codecs()) {
      const Codec* existing = LookupCodec(map, codec.id);
      // Redefinition within one description is a conflict; across descriptions the newer one wins.
      if (existing && in_use[name].count(codec.id) && !SameForRecorder(*existing, codec)) ok = false;
      map[codec.id] = codec;
      in_use[name].insert(codec.id);
      seen_payload_types_.insert(codec.id);
    }
    for (const RtpExtension& ext : content.media_description()->rtp_header_extensions()) {
      recorder.header_extensions[ext.id] = ExtensionKey(ext.uri, ext.encrypt);
      extension_ids_[ext.id] = ExtensionKey(ext.uri, ext.encrypt);
    }
  }
  // Payload types the description does not use are available again.
  for (auto& [name, pts] : in_use) {
    auto& map = local ? recorders_[name].local_payload_types : recorders_[name].remote_payload_types;
    for (auto it = map.begin(); it != map.end();) it = pts.count(it->first) ? std::next(it) : map.erase(it);
  }
  // Drop the recorders the description has no section for.
  for (auto it = recorders_.begin(); it != recorders_.end();) {
    it = in_use.count(it->first) ? std::next(it) : recorders_.erase(it);
  }
  seen_payload_types_.clear();
  for (const auto& [name, r] : recorders_) {
    for (const auto& [pt, c] : r.local_payload_types) seen_payload_types_.insert(pt);
    for (const auto& [pt, c] : r.remote_payload_types) seen_payload_types_.insert(pt);
  }
  return ok;
}

}  // namespace sm::pc
