// sm/pc/jsep.h: libwebrtc M157's api/jsep_ice_candidate.cc (compare/webrtc/reference/api/) and the
// SessionDescriptionInterface implementation (api/jsep_session_description.cc, pc/jsep_session_description.cc; not in
// compare/webrtc/reference/, ported from M157's behaviour), #1062.
#include "sm/pc/jsep.h"

#include <algorithm>
#include <limits>

#include "sm/p2p/p2p_constants.h"
#include "sm/pc/webrtc_sdp.h"

namespace sm::pc {
namespace {

int EnsureValidMLineIndex(int sdp_mline_index) {
  if (sdp_mline_index < 0 || sdp_mline_index > std::numeric_limits<uint16_t>::max()) return -1;
  return sdp_mline_index;
}

constexpr char kDummyAddress[] = "0.0.0.0";
constexpr int kDummyPort = 9;

// Update the connection address for the MediaContentDescription based on the candidates: the default destination
// is the most preferred RTP (component 1) UDP candidate, IPv4 before IPv6.
void UpdateConnectionAddress(const IceCandidateCollection& candidate_collection,
                             MediaContentDescription* media_desc) {
  int port = kDummyPort;
  p2p::IpAddress ip = *p2p::IpAddress::FromString(kDummyAddress);
  std::string hostname;
  int current_preference = 0;  // Start with lowest preference.
  p2p::IpFamily current_family = p2p::IpFamily::kUnspec;
  for (size_t i = 0; i < candidate_collection.count(); ++i) {
    const IceCandidate* jsep_candidate = candidate_collection.at(i);
    if (jsep_candidate->candidate().component() != p2p::ICE_CANDIDATE_COMPONENT_RTP) continue;
    // Default destination should be UDP only.
    if (jsep_candidate->candidate().protocol() != p2p::UDP_PROTOCOL_NAME) continue;
    const int preference = jsep_candidate->candidate().type_preference();
    const p2p::IpFamily family = jsep_candidate->candidate().address().ipaddr().family();
    // See if this candidate is more preferable then the current one if it's the same family. Or if the current
    // family is IPv4 already so we could safely ignore all IPv6 ones. WebRTC bug 4269.
    if ((preference <= current_preference && current_family == family) ||
        (current_family == p2p::IpFamily::kV4 && family == p2p::IpFamily::kV6)) {
      continue;
    }
    current_preference = preference;
    current_family = family;
    const p2p::SocketAddress& candidate_addr = jsep_candidate->candidate().address();
    port = candidate_addr.port();
    ip = candidate_addr.ipaddr();
    hostname = candidate_addr.hostname();
  }
  p2p::SocketAddress connection_addr(ip, static_cast<uint16_t>(port));
  if (p2p::IpIsUnspec(connection_addr.ipaddr()) && !hostname.empty()) {
    // When a hostname candidate becomes the (default) connection address, we use the dummy address 0.0.0.0 and port 9
    // in the c= and the m= lines: with a FQDN in a c= line, SDP parsing could fail in other JSEP implementations, and
    // 0.0.0.0 with port 9 is the widely deployed wildcard for trickle ICE (draft-ietf-mmusic-trickle-ice-sip).
    connection_addr = p2p::SocketAddress(*p2p::IpAddress::FromString(kDummyAddress), kDummyPort);
  }
  media_desc->set_connection_address(connection_addr);
}

}  // namespace

IceCandidate::IceCandidate(std::string_view sdp_mid, int sdp_mline_index, const Candidate& candidate)
    : sdp_mid_(sdp_mid), sdp_mline_index_(EnsureValidMLineIndex(sdp_mline_index)), candidate_(candidate) {}

std::unique_ptr<IceCandidate> IceCandidate::Create(std::string_view mid, int sdp_mline_index, std::string_view sdp,
                                                   SdpParseError* error) {
  std::string reason;
  std::optional<Candidate> c = Candidate::ParseCandidateString(sdp, &reason);
  if (!c) {
    if (error) {
      error->line = std::string(sdp);
      error->description = reason;
    }
    return nullptr;
  }
  return std::make_unique<IceCandidate>(mid, sdp_mline_index, *c);
}

std::unique_ptr<IceCandidate> CreateIceCandidate(std::string_view sdp_mid, int sdp_mline_index,
                                                 const Candidate& candidate) {
  return std::make_unique<IceCandidate>(sdp_mid, sdp_mline_index, candidate);
}

void IceCandidateCollection::Append(IceCandidateCollection collection) {
  candidates_.insert(candidates_.end(), std::make_move_iterator(collection.candidates_.begin()),
                     std::make_move_iterator(collection.candidates_.end()));
}

size_t IceCandidateCollection::remove(const IceCandidate* candidate) {
  auto iter = std::find_if(candidates_.begin(), candidates_.end(), [&](const std::unique_ptr<IceCandidate>& c) {
    return c->candidate().MatchesForRemoval(candidate->candidate());
  });
  if (iter == candidates_.end()) return 0u;
  candidates_.erase(iter);
  return 1u;
}

bool IceCandidateCollection::HasCandidate(const IceCandidate* candidate) const {
  const std::string sdp_mid = candidate->sdp_mid();
  return std::any_of(candidates_.begin(), candidates_.end(), [&](const std::unique_ptr<IceCandidate>& entry) {
    if (!entry->candidate().IsEquivalent(candidate->candidate())) return false;
    if (!sdp_mid.empty()) return sdp_mid == entry->sdp_mid();
    return candidate->sdp_mline_index() == entry->sdp_mline_index();
  });
}

IceCandidateCollection IceCandidateCollection::Clone() const {
  IceCandidateCollection new_collection;
  new_collection.candidates_.reserve(candidates_.size());
  for (const auto& candidate : candidates_) {
    new_collection.candidates_.push_back(
        std::make_unique<IceCandidate>(candidate->sdp_mid(), candidate->sdp_mline_index(), candidate->candidate()));
  }
  return new_collection;
}

const char* SdpTypeToString(SdpType type) {
  switch (type) {
    case SdpType::kOffer:
      return SessionDescriptionInterface::kOffer;
    case SdpType::kPrAnswer:
      return SessionDescriptionInterface::kPrAnswer;
    case SdpType::kAnswer:
      return SessionDescriptionInterface::kAnswer;
    case SdpType::kRollback:
      return SessionDescriptionInterface::kRollback;
  }
  return "";
}

std::optional<SdpType> SdpTypeFromString(std::string_view type_str) {
  if (type_str == SessionDescriptionInterface::kOffer) return SdpType::kOffer;
  if (type_str == SessionDescriptionInterface::kPrAnswer) return SdpType::kPrAnswer;
  if (type_str == SessionDescriptionInterface::kAnswer) return SdpType::kAnswer;
  if (type_str == SessionDescriptionInterface::kRollback) return SdpType::kRollback;
  return std::nullopt;
}

SessionDescriptionInterface::SessionDescriptionInterface(SdpType type, std::unique_ptr<SessionDescription> description,
                                                         std::string_view id, std::string_view version,
                                                         std::vector<IceCandidateCollection> candidates,
                                                         EncodingOptions encoding_options)
    : sdp_type_(type),
      id_(id),
      version_(version),
      description_(std::move(description)),
      candidate_collection_(std::move(candidates)),
      encoding_options_(encoding_options) {
  const size_t mediasection_count = number_of_mediasections();
  if (candidate_collection_.size() < mediasection_count) candidate_collection_.resize(mediasection_count);
}

SessionDescriptionInterface::~SessionDescriptionInterface() = default;

std::unique_ptr<SessionDescriptionInterface> SessionDescriptionInterface::Create(
    SdpType type, std::unique_ptr<SessionDescription> description, std::string_view id, std::string_view version,
    std::vector<IceCandidateCollection> candidates, EncodingOptions encoding_options) {
  return std::unique_ptr<SessionDescriptionInterface>(new SessionDescriptionInterface(
      type, std::move(description), id, version, std::move(candidates), encoding_options));
}

std::unique_ptr<SessionDescriptionInterface> SessionDescriptionInterface::Clone() const {
  std::vector<IceCandidateCollection> candidates;
  for (const IceCandidateCollection& collection : candidate_collection_) candidates.push_back(collection.Clone());
  return Create(sdp_type_, description_ ? description_->Clone() : nullptr, id_, version_, std::move(candidates),
                encoding_options_);
}

bool SessionDescriptionInterface::AddCandidate(const IceCandidate* candidate) {
  if (!candidate) return false;
  size_t mediasection_index = 0;
  if (!GetMediasectionIndex(candidate, &mediasection_index)) return false;
  if (mediasection_index >= number_of_mediasections()) return false;
  const ContentInfo& content = description_->contents()[mediasection_index];
  const std::string& content_name = content.mid();
  const TransportInfo* transport_info = description_->GetTransportInfoByName(content_name);
  if (!transport_info) return false;

  Candidate updated_candidate = candidate->candidate();
  if (updated_candidate.username().empty()) updated_candidate.set_username(transport_info->description.ice_ufrag);
  if (updated_candidate.password().empty()) updated_candidate.set_password(transport_info->description.ice_pwd);

  auto updated_candidate_wrapper =
      std::make_unique<IceCandidate>(content_name, static_cast<int>(mediasection_index), updated_candidate);
  IceCandidateCollection& candidates = candidate_collection_[mediasection_index];
  if (!candidates.HasCandidate(updated_candidate_wrapper.get())) {
    candidates.add(std::move(updated_candidate_wrapper));
    UpdateConnectionAddress(candidates, description_->contents()[mediasection_index].media_description());
  }
  return true;
}

bool SessionDescriptionInterface::RemoveCandidate(const IceCandidate* candidate) {
  size_t mediasection_index = 0u;
  if (!GetMediasectionIndex(candidate, &mediasection_index)) return false;
  if (mediasection_index >= candidate_collection_.size()) return false;
  IceCandidateCollection& candidates = candidate_collection_[mediasection_index];
  if (candidates.remove(candidate) == 0) return false;
  UpdateConnectionAddress(candidates, description_->contents()[mediasection_index].media_description());
  return true;
}

size_t SessionDescriptionInterface::number_of_mediasections() const {
  return description_ ? description_->contents().size() : 0;
}

const IceCandidateCollection* SessionDescriptionInterface::candidates(size_t mediasection_index) const {
  if (mediasection_index >= candidate_collection_.size()) return nullptr;
  return &candidate_collection_[mediasection_index];
}

std::string SessionDescriptionInterface::ToString() const { return SdpSerialize(*this); }

bool SessionDescriptionInterface::GetMediasectionIndex(const IceCandidate* candidate, size_t* index) const {
  if (!candidate || !index || !description_) return false;
  // If the candidate has no valid mline index or sdp_mid, it is impossible to find a match.
  if (candidate->sdp_mid().empty() &&
      (candidate->sdp_mline_index() < 0 ||
       static_cast<size_t>(candidate->sdp_mline_index()) >= description_->contents().size())) {
    return false;
  }
  if (candidate->sdp_mline_index() >= 0) *index = static_cast<size_t>(candidate->sdp_mline_index());
  if (!candidate->sdp_mid().empty()) {
    bool found = false;
    // Try to match the sdp_mid with content name.
    for (size_t i = 0; i < description_->contents().size(); ++i) {
      if (candidate->sdp_mid() == description_->contents().at(i).mid()) {
        *index = i;
        found = true;
        break;
      }
    }
    // If the sdp_mid is presented but we can't find a match, we consider this as an error.
    if (!found) return false;
  }
  return true;
}

std::unique_ptr<SessionDescriptionInterface> CreateSessionDescription(SdpType type, std::string_view sdp,
                                                                      SdpParseError* error_out) {
  if (type == SdpType::kRollback) return CreateRollbackSessionDescription();
  return SdpDeserialize(type, sdp, error_out);
}

std::unique_ptr<SessionDescriptionInterface> CreateSessionDescription(SdpType type, std::string_view session_id,
                                                                      std::string_view session_version,
                                                                      std::unique_ptr<SessionDescription> description) {
  if (!description) return nullptr;
  return SessionDescriptionInterface::Create(type, std::move(description), session_id, session_version);
}

std::unique_ptr<SessionDescriptionInterface> CreateRollbackSessionDescription(std::string_view session_id,
                                                                              std::string_view session_version) {
  return SessionDescriptionInterface::Create(SdpType::kRollback, nullptr, session_id, session_version);
}

}  // namespace sm::pc
