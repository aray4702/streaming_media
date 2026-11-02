// JSEP's session description and ICE candidate objects (#1062, C6 part 1): libwebrtc M157's api/jsep.h,
// api/jsep_ice_candidate.cc (compare/webrtc/reference/api/) and the SessionDescriptionInterface implementation
// (api/jsep_session_description.cc / pc/jsep_session_description.cc, not in compare/webrtc/reference/; this follows
// M157's behaviour): a SessionDescription with its type (offer, pranswer, answer, rollback), session id and version,
// and the ICE candidates of each m= section, which also set that section's default address (c= and the m= port).
//
// Differences from libwebrtc: no observers (CreateSessionDescriptionObserver, SetSessionDescriptionObserver) and no
// sequence checker, since the library starts no threads.
#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sm/p2p/candidate.h"
#include "sm/p2p/transport_description.h"
#include "sm/pc/session_description.h"

namespace sm::pc {

using p2p::Candidate;
using p2p::SdpType;

// The line that failed to parse and why.
struct SdpParseError {
  std::string line;
  std::string description;
};

// A candidate with the m= section it belongs to (by mid, or by index when the mid is empty).
class IceCandidate final {
 public:
  IceCandidate(std::string_view sdp_mid, int sdp_mline_index, const Candidate& candidate);
  IceCandidate(const IceCandidate&) = delete;
  IceCandidate& operator=(const IceCandidate&) = delete;

  // Parses `sdp` (an a=candidate line or its value); null, with the reason in `error`, if it is malformed.
  static std::unique_ptr<IceCandidate> Create(std::string_view mid, int sdp_mline_index, std::string_view sdp,
                                              SdpParseError* error = nullptr);

  std::string sdp_mid() const { return sdp_mid_; }
  // -1 when out of range.
  int sdp_mline_index() const { return sdp_mline_index_; }
  const Candidate& candidate() const { return candidate_; }
  std::string server_url() const { return candidate_.url(); }
  // The candidate-attribute, with the ufrag.
  std::string ToString() const { return candidate_.ToCandidateAttribute(true); }

 private:
  const std::string sdp_mid_;
  const int sdp_mline_index_;
  const Candidate candidate_;
};

std::unique_ptr<IceCandidate> CreateIceCandidate(std::string_view sdp_mid, int sdp_mline_index,
                                                 const Candidate& candidate);

// The candidates of one m= section.
class IceCandidateCollection final {
 public:
  IceCandidateCollection() = default;
  explicit IceCandidateCollection(std::vector<std::unique_ptr<IceCandidate>>&& candidates)
      : candidates_(std::move(candidates)) {}
  IceCandidateCollection(IceCandidateCollection&& o) = default;
  IceCandidateCollection& operator=(IceCandidateCollection&& o) = default;
  IceCandidateCollection(const IceCandidateCollection&) = delete;
  IceCandidateCollection& operator=(const IceCandidateCollection&) = delete;

  size_t count() const { return candidates_.size(); }
  bool empty() const { return candidates_.empty(); }
  const IceCandidate* at(size_t index) const { return candidates_[index].get(); }
  void add(std::unique_ptr<IceCandidate> candidate) { candidates_.push_back(std::move(candidate)); }
  void Append(IceCandidateCollection collection);
  // Removes the first candidate that MatchesForRemoval; returns how many were removed (0 or 1).
  size_t remove(const IceCandidate* candidate);
  const std::vector<std::unique_ptr<IceCandidate>>& candidates() const { return candidates_; }
  // An equivalent candidate (Candidate::IsEquivalent) for the same mid (or index, without a mid).
  bool HasCandidate(const IceCandidate* candidate) const;
  IceCandidateCollection Clone() const;

 private:
  std::vector<std::unique_ptr<IceCandidate>> candidates_;
};

// "offer", "pranswer", "answer", "rollback".
const char* SdpTypeToString(SdpType type);
std::optional<SdpType> SdpTypeFromString(std::string_view type_str);

struct EncodingOptions {
  // Write the rtcp-fb lines that every codec shares once, as a=rtcp-fb:* (off by default, as libwebrtc).
  bool use_wildcard = false;
};

class SessionDescriptionInterface final {
 public:
  static std::unique_ptr<SessionDescriptionInterface> Create(SdpType type,
                                                             std::unique_ptr<SessionDescription> description,
                                                             std::string_view id, std::string_view version,
                                                             std::vector<IceCandidateCollection> candidates = {},
                                                             EncodingOptions encoding_options = {});
  SessionDescriptionInterface(const SessionDescriptionInterface&) = delete;
  SessionDescriptionInterface& operator=(const SessionDescriptionInterface&) = delete;
  ~SessionDescriptionInterface();

  static constexpr char kOffer[] = "offer";
  static constexpr char kPrAnswer[] = "pranswer";
  static constexpr char kAnswer[] = "answer";
  static constexpr char kRollback[] = "rollback";

  std::unique_ptr<SessionDescriptionInterface> Clone() const;

  std::string_view id() const { return id_; }
  std::string_view version() const { return version_; }
  const SessionDescription* description() const { return description_.get(); }
  SessionDescription* description() { return description_.get(); }
  std::string session_id() const { return id_; }
  std::string session_version() const { return version_; }
  SdpType GetType() const { return sdp_type_; }
  std::string type() const { return SdpTypeToString(sdp_type_); }

  // Adds the candidate to its m= section (by mid, else by index), filling an empty ufrag and password from that
  // section's transport, unless an equivalent one is there already, and updates the section's default address.
  // False if no m= section matches.
  bool AddCandidate(const IceCandidate* candidate);
  bool RemoveCandidate(const IceCandidate* candidate);
  size_t number_of_mediasections() const;
  // Null for an index past the last m= section.
  const IceCandidateCollection* candidates(size_t mediasection_index) const;
  EncodingOptions encoding_options() const { return encoding_options_; }

  // The SDP (SdpSerialize).
  std::string ToString() const;
  bool ToString(std::string* out) const {
    if (!out) return false;
    *out = ToString();
    return !out->empty();
  }

 private:
  SessionDescriptionInterface(SdpType type, std::unique_ptr<SessionDescription> description, std::string_view id,
                              std::string_view version, std::vector<IceCandidateCollection> candidates,
                              EncodingOptions encoding_options);

  bool GetMediasectionIndex(const IceCandidate* candidate, size_t* index) const;

  const SdpType sdp_type_;
  const std::string id_;
  const std::string version_;
  const std::unique_ptr<SessionDescription> description_;
  std::vector<IceCandidateCollection> candidate_collection_;
  const EncodingOptions encoding_options_;
};

// Parses `sdp` (SdpDeserialize); a rollback needs no SDP. Null, with the reason in `error_out`, if it fails.
std::unique_ptr<SessionDescriptionInterface> CreateSessionDescription(SdpType type, std::string_view sdp,
                                                                      SdpParseError* error_out = nullptr);
// Wraps an already built description; null for a null description.
std::unique_ptr<SessionDescriptionInterface> CreateSessionDescription(SdpType type, std::string_view session_id,
                                                                      std::string_view session_version,
                                                                      std::unique_ptr<SessionDescription> description);
std::unique_ptr<SessionDescriptionInterface> CreateRollbackSessionDescription(std::string_view session_id = "",
                                                                              std::string_view session_version = "");

}  // namespace sm::pc
