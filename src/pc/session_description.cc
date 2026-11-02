// sm/pc/session_description.h: libwebrtc M157's pc/session_description.cc (compare/webrtc/reference/pc/), and
// api/rtp_parameters.cc's RtpExtension::ToString, #1062.
#include "sm/pc/session_description.h"

#include <algorithm>

namespace sm::pc {
namespace {

template <typename Contents>
auto* FindContentInfoByName(Contents& contents, std::string_view name) {
  for (auto& content : contents) {
    if (content.mid() == name) return &content;
  }
  return static_cast<decltype(&contents[0])>(nullptr);
}

}  // namespace

std::string RtpExtension::ToString() const {
  std::string s = "{uri: " + uri + ", id: " + std::to_string(id);
  if (encrypt) s += ", encrypt";
  return s + "}";
}

const std::string* ContentGroup::FirstContentName() const {
  return !content_names_.empty() ? &content_names_.front() : nullptr;
}

bool ContentGroup::HasContentName(std::string_view content_name) const {
  return std::find(content_names_.begin(), content_names_.end(), content_name) != content_names_.end();
}

void ContentGroup::AddContentName(std::string_view content_name) {
  if (!HasContentName(content_name)) content_names_.emplace_back(content_name);
}

bool ContentGroup::RemoveContentName(std::string_view content_name) {
  auto iter = std::find(content_names_.begin(), content_names_.end(), content_name);
  if (iter == content_names_.end()) return false;
  content_names_.erase(iter);
  return true;
}

std::string ContentGroup::ToString() const {
  std::string acc = semantics_ + "(";
  for (size_t i = 0; i < content_names_.size(); ++i) {
    if (i > 0) acc += " ";
    acc += content_names_[i];
  }
  return acc + ")";
}

const ContentInfo* SessionDescription::GetContentByName(std::string_view name) const {
  return FindContentInfoByName(contents_, name);
}

ContentInfo* SessionDescription::GetContentByName(std::string_view name) {
  return FindContentInfoByName(contents_, name);
}

const MediaContentDescription* SessionDescription::GetContentDescriptionByName(std::string_view name) const {
  const ContentInfo* cinfo = GetContentByName(name);
  return cinfo ? cinfo->media_description() : nullptr;
}

MediaContentDescription* SessionDescription::GetContentDescriptionByName(std::string_view name) {
  ContentInfo* cinfo = GetContentByName(name);
  return cinfo ? cinfo->media_description() : nullptr;
}

const ContentInfo* SessionDescription::FirstContentByType(MediaProtocolType type) const {
  for (const auto& content : contents_) {
    if (content.type == type) return &content;
  }
  return nullptr;
}

const ContentInfo* SessionDescription::FirstContent() const { return contents_.empty() ? nullptr : &contents_.front(); }

void SessionDescription::AddContent(std::string_view name, MediaProtocolType type,
                                    std::unique_ptr<MediaContentDescription> description) {
  AddContent(ContentInfo(type, name, std::move(description)));
}

void SessionDescription::AddContent(std::string_view name, MediaProtocolType type, bool rejected,
                                    std::unique_ptr<MediaContentDescription> description) {
  AddContent(ContentInfo(type, name, std::move(description), rejected));
}

void SessionDescription::AddContent(std::string_view name, MediaProtocolType type, bool rejected, bool bundle_only,
                                    std::unique_ptr<MediaContentDescription> description) {
  AddContent(ContentInfo(type, name, std::move(description), rejected, bundle_only));
}

void SessionDescription::AddContent(ContentInfo&& content) {
  if (extmap_allow_mixed()) {
    // Mixed support on session level overrides setting on media level.
    content.media_description()->set_extmap_allow_mixed_level(MediaContentDescription::AttributeLevel::kSession);
  }
  if (cryptex()) {
    content.media_description()->set_cryptex_level(MediaContentDescription::AttributeLevel::kSession);
  }
  contents_.push_back(std::move(content));
}

bool SessionDescription::RemoveContentByName(std::string_view name) {
  for (auto content = contents_.begin(); content != contents_.end(); ++content) {
    if (content->mid() == name) {
      contents_.erase(content);
      return true;
    }
  }
  return false;
}

bool SessionDescription::RemoveTransportInfoByName(std::string_view name) {
  for (auto it = transport_infos_.begin(); it != transport_infos_.end(); ++it) {
    if (it->content_name == name) {
      transport_infos_.erase(it);
      return true;
    }
  }
  return false;
}

const TransportInfo* SessionDescription::GetTransportInfoByName(std::string_view name) const {
  for (const auto& info : transport_infos_) {
    if (info.content_name == name) return &info;
  }
  return nullptr;
}

TransportInfo* SessionDescription::GetTransportInfoByName(std::string_view name) {
  for (auto& info : transport_infos_) {
    if (info.content_name == name) return &info;
  }
  return nullptr;
}

void SessionDescription::RemoveGroupByName(std::string_view name) {
  for (auto it = content_groups_.begin(); it != content_groups_.end(); ++it) {
    if (it->semantics() == name) {
      content_groups_.erase(it);
      break;
    }
  }
}

bool SessionDescription::HasGroup(std::string_view name) const { return GetGroupByName(name) != nullptr; }

const ContentGroup* SessionDescription::GetGroupByName(std::string_view name) const {
  for (const auto& group : content_groups_) {
    if (group.semantics() == name) return &group;
  }
  return nullptr;
}

std::vector<const ContentGroup*> SessionDescription::GetGroupsByName(std::string_view name) const {
  std::vector<const ContentGroup*> content_groups;
  for (const ContentGroup& content_group : content_groups_) {
    if (content_group.semantics() == name) content_groups.push_back(&content_group);
  }
  return content_groups;
}

}  // namespace sm::pc
