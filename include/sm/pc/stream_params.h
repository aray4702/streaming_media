// The streams an m= section sends (#1062, C6 part 1): libwebrtc M157's media/base/stream_params.{h,cc} (SsrcGroup,
// StreamParams, the stream lookups) and media/base/rid_description.{h,cc} (compare/webrtc/reference/media/base/).
//
// Difference from libwebrtc: GenerateSsrcs takes the id generator as a function (UniqueRandomIdGenerator comes with
// offer generation, C6 part 2).
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "sm/pc/codec.h"

namespace sm::pc {

inline constexpr char kFecSsrcGroupSemantics[] = "FEC";
inline constexpr char kFecFrSsrcGroupSemantics[] = "FEC-FR";
inline constexpr char kFidSsrcGroupSemantics[] = "FID";
inline constexpr char kSimSsrcGroupSemantics[] = "SIM";

// media/base/rid_description.h: an RFC 8851 a=rid line.
enum class RidDirection { kSend, kReceive };

struct RidDescription final {
  RidDescription() = default;
  RidDescription(std::string_view rid, RidDirection direction) : rid(rid), direction(direction) {}
  bool operator==(const RidDescription& other) const {
    return rid == other.rid && direction == other.direction && codecs == other.codecs &&
           restrictions == other.restrictions;
  }
  bool operator!=(const RidDescription& other) const { return !(*this == other); }

  // The RID (RFC 8852): 1-16 alphanumeric characters.
  std::string rid;
  RidDirection direction = RidDirection::kSend;
  // The codecs of the m= section's payload types that the line's pt= lists, in its order.
  std::vector<Codec> codecs;
  // The other restrictions (max-width, max-fps, ...), by name; an empty value for a restriction without one.
  std::map<std::string, std::string> restrictions;
};

// RFC 5576's a=ssrc-group.
struct SsrcGroup {
  SsrcGroup(std::string_view usage, const std::vector<uint32_t>& ssrcs) : semantics(usage), ssrcs(ssrcs) {}
  bool operator==(const SsrcGroup& other) const { return semantics == other.semantics && ssrcs == other.ssrcs; }
  bool operator!=(const SsrcGroup& other) const { return !(*this == other); }

  bool has_semantics(std::string_view semantics_in) const { return semantics == semantics_in && !ssrcs.empty(); }
  std::string ToString() const;

  std::string semantics;        // e.g FID, FEC-FR, SIM.
  std::vector<uint32_t> ssrcs;  // SSRCs of this type.
};

// One track's SSRCs, CNAME, stream ids and RIDs.
struct StreamParams {
  static StreamParams CreateLegacy(uint32_t ssrc) {
    StreamParams stream;
    stream.ssrcs.push_back(ssrc);
    return stream;
  }

  bool operator==(const StreamParams& other) const;
  bool operator!=(const StreamParams& other) const { return !(*this == other); }

  uint32_t first_ssrc() const { return ssrcs.empty() ? 0 : ssrcs[0]; }
  bool has_ssrcs() const { return !ssrcs.empty(); }
  bool has_ssrc(uint32_t ssrc) const { return std::find(ssrcs.begin(), ssrcs.end(), ssrc) != ssrcs.end(); }
  void add_ssrc(uint32_t ssrc) { ssrcs.push_back(ssrc); }
  bool has_ssrc_groups() const { return !ssrc_groups.empty(); }
  bool has_ssrc_group(std::string_view semantics) const { return get_ssrc_group(semantics) != nullptr; }
  const SsrcGroup* get_ssrc_group(std::string_view semantics) const;

  // Convenience to get all the FID SSRCs (RTX) of a track.
  bool AddFidSsrc(uint32_t primary_ssrc, uint32_t fid_ssrc) {
    return AddSecondarySsrc(kFidSsrcGroupSemantics, primary_ssrc, fid_ssrc);
  }
  bool GetFidSsrc(uint32_t primary_ssrc, uint32_t* fid_ssrc) const {
    return GetSecondarySsrc(kFidSsrcGroupSemantics, primary_ssrc, fid_ssrc);
  }
  bool AddFecFrSsrc(uint32_t primary_ssrc, uint32_t fecfr_ssrc) {
    return AddSecondarySsrc(kFecFrSsrcGroupSemantics, primary_ssrc, fecfr_ssrc);
  }
  bool GetFecFrSsrc(uint32_t primary_ssrc, uint32_t* fecfr_ssrc) const {
    return GetSecondarySsrc(kFecFrSsrcGroupSemantics, primary_ssrc, fecfr_ssrc);
  }

  // `num_layers` primary SSRCs (a SIM group if more than one), plus an RTX (FID) and a FlexFEC (FEC-FR) SSRC for
  // each if asked, all from `generate_id`.
  void GenerateSsrcs(int num_layers, bool generate_fid, bool generate_fec_fr,
                     const std::function<uint32_t()>& generate_id);
  // The SIM group's SSRCs, or the first SSRC without one.
  void GetPrimarySsrcs(std::vector<uint32_t>* primary_ssrcs) const;
  void GetSecondarySsrcs(std::string_view semantic, const std::vector<uint32_t>& primary_ssrcs,
                         std::vector<uint32_t>* secondary_ssrcs) const;
  void GetFidSsrcs(const std::vector<uint32_t>& primary_ssrcs, std::vector<uint32_t>* fid_ssrcs) const {
    GetSecondarySsrcs(kFidSsrcGroupSemantics, primary_ssrcs, fid_ssrcs);
  }

  std::vector<std::string> stream_ids() const { return stream_ids_; }
  void set_stream_ids(const std::vector<std::string>& stream_ids) { stream_ids_ = stream_ids; }
  std::string_view first_stream_id() const {
    return stream_ids_.empty() ? std::string_view() : std::string_view(stream_ids_[0]);
  }
  std::string ToString() const;

  // The track id (a=msid's appdata).
  std::string id;
  std::vector<uint32_t> ssrcs;         // All SSRCs for this source
  std::vector<SsrcGroup> ssrc_groups;  // e.g. FID, FEC, SIM
  std::string cname;                   // RTCP CNAME

  bool has_rids() const { return !rids_.empty(); }
  const std::vector<RidDescription>& rids() const { return rids_; }
  void set_rids(const std::vector<RidDescription>& rids) { rids_ = rids; }

 private:
  bool AddSecondarySsrc(std::string_view semantics, uint32_t primary_ssrc, uint32_t secondary_ssrc);
  bool GetSecondarySsrc(std::string_view semantics, uint32_t primary_ssrc, uint32_t* secondary_ssrc) const;

  std::vector<std::string> stream_ids_;
  std::vector<RidDescription> rids_;
};
using StreamParamsVec = std::vector<StreamParams>;

template <class Condition>
const StreamParams* GetStream(const StreamParamsVec& streams, Condition condition) {
  auto found = std::find_if(streams.begin(), streams.end(), condition);
  return found == streams.end() ? nullptr : &(*found);
}
inline bool HasStreamWithNoSsrcs(const StreamParamsVec& streams) {
  return GetStream(streams, [](const StreamParams& sp) { return !sp.has_ssrcs(); }) != nullptr;
}
inline const StreamParams* GetStreamBySsrc(const StreamParamsVec& streams, uint32_t ssrc) {
  return GetStream(streams, [&ssrc](const StreamParams& sp) { return sp.has_ssrc(ssrc); });
}
inline const StreamParams* GetStreamByIds(const StreamParamsVec& streams, std::string_view id) {
  return GetStream(streams, [&id](const StreamParams& sp) { return sp.id == id; });
}

}  // namespace sm::pc
