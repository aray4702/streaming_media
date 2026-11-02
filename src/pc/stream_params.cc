// sm/pc/stream_params.h: libwebrtc M157's media/base/stream_params.cc (compare/webrtc/reference/media/base/), #1062.
#include "sm/pc/stream_params.h"

#include <span>

namespace sm::pc {
namespace {

void AppendSsrcs(std::span<const uint32_t> ssrcs, std::string* sb) {
  *sb += "ssrcs:[";
  const char* delimiter = "";
  for (uint32_t ssrc : ssrcs) {
    *sb += delimiter + std::to_string(ssrc);
    delimiter = ",";
  }
  *sb += "]";
}

}  // namespace

std::string SsrcGroup::ToString() const {
  std::string sb = "{semantics:" + semantics + ";";
  AppendSsrcs(ssrcs, &sb);
  return sb + "}";
}

const SsrcGroup* StreamParams::get_ssrc_group(std::string_view semantics) const {
  for (const SsrcGroup& ssrc_group : ssrc_groups) {
    if (ssrc_group.has_semantics(semantics)) return &ssrc_group;
  }
  return nullptr;
}

bool StreamParams::operator==(const StreamParams& other) const {
  return id == other.id && ssrcs == other.ssrcs && ssrc_groups == other.ssrc_groups && cname == other.cname &&
         stream_ids_ == other.stream_ids_ && std::is_permutation(rids_.begin(), rids_.end(), other.rids_.begin(),
                                                                 other.rids_.end());
}

std::string StreamParams::ToString() const {
  std::string sb = "{";
  if (!id.empty()) sb += "id:" + id + ";";
  AppendSsrcs(ssrcs, &sb);
  sb += ";ssrc_groups:";
  const char* delimiter = "";
  for (const SsrcGroup& ssrc_group : ssrc_groups) {
    sb += delimiter + ssrc_group.ToString();
    delimiter = ",";
  }
  sb += ";";
  if (!cname.empty()) sb += "cname:" + cname + ";";
  sb += "stream_ids:";
  delimiter = "";
  for (const std::string& stream_id : stream_ids_) {
    sb += delimiter + stream_id;
    delimiter = ",";
  }
  sb += ";";
  if (!rids_.empty()) {
    sb += "rids:[";
    delimiter = "";
    for (const RidDescription& rid : rids_) {
      sb += delimiter + rid.rid;
      delimiter = ",";
    }
    sb += "];";
  }
  return sb + "}";
}

void StreamParams::GenerateSsrcs(int num_layers, bool generate_fid, bool generate_fec_fr,
                                 const std::function<uint32_t()>& generate_id) {
  std::vector<uint32_t> primary_ssrcs;
  for (int i = 0; i < num_layers; ++i) {
    uint32_t ssrc = generate_id();
    primary_ssrcs.push_back(ssrc);
    add_ssrc(ssrc);
  }
  if (num_layers > 1) ssrc_groups.push_back(SsrcGroup(kSimSsrcGroupSemantics, primary_ssrcs));
  if (generate_fid) {
    for (uint32_t ssrc : primary_ssrcs) AddFidSsrc(ssrc, generate_id());
  }
  if (generate_fec_fr) {
    for (uint32_t ssrc : primary_ssrcs) AddFecFrSsrc(ssrc, generate_id());
  }
}

void StreamParams::GetPrimarySsrcs(std::vector<uint32_t>* primary_ssrcs) const {
  const SsrcGroup* sim_group = get_ssrc_group(kSimSsrcGroupSemantics);
  if (sim_group == nullptr) {
    primary_ssrcs->push_back(first_ssrc());
  } else {
    primary_ssrcs->insert(primary_ssrcs->end(), sim_group->ssrcs.begin(), sim_group->ssrcs.end());
  }
}

void StreamParams::GetSecondarySsrcs(std::string_view semantics, const std::vector<uint32_t>& primary_ssrcs,
                                     std::vector<uint32_t>* secondary_ssrcs) const {
  for (uint32_t primary_ssrc : primary_ssrcs) {
    uint32_t secondary_ssrc;
    if (GetSecondarySsrc(semantics, primary_ssrc, &secondary_ssrc)) secondary_ssrcs->push_back(secondary_ssrc);
  }
}

bool StreamParams::AddSecondarySsrc(std::string_view semantics, uint32_t primary_ssrc, uint32_t secondary_ssrc) {
  if (!has_ssrc(primary_ssrc)) return false;
  ssrcs.push_back(secondary_ssrc);
  ssrc_groups.push_back(SsrcGroup(semantics, {primary_ssrc, secondary_ssrc}));
  return true;
}

bool StreamParams::GetSecondarySsrc(std::string_view semantics, uint32_t primary_ssrc,
                                    uint32_t* secondary_ssrc) const {
  for (const SsrcGroup& ssrc_group : ssrc_groups) {
    if (ssrc_group.has_semantics(semantics) && ssrc_group.ssrcs.size() >= 2 && ssrc_group.ssrcs[0] == primary_ssrc) {
      *secondary_ssrc = ssrc_group.ssrcs[1];
      return true;
    }
  }
  return false;
}

}  // namespace sm::pc
