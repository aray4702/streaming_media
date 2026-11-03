// libwebrtc M157's pc/jsep_transport.cc and pc/jsep_transport_controller.cc (compare/webrtc/reference/pc/), for
// C6's subset (#1064); see sm/pc/jsep_transport_controller.h.
#include "sm/pc/jsep_transport_controller.h"

#include <algorithm>
#include <set>
#include <utility>

#include "sm/p2p/p2p_constants.h"
#include "sm/tls/ssl_fingerprint.h"

namespace sm::pc {

namespace {

bool Fail(std::string* error, std::string message) {
  if (error) *error = std::move(message);
  return false;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// JsepTransport

JsepTransport::JsepTransport(std::string mid, std::shared_ptr<const tls::SslIdentity> certificate,
                             std::unique_ptr<p2p::P2PTransportChannel> ice, std::unique_ptr<p2p::DtlsTransport> dtls,
                             std::unique_ptr<DtlsSrtpTransport> rtp_transport)
    : mid_(std::move(mid)),
      local_certificate_(std::move(certificate)),
      ice_(std::move(ice)),
      dtls_(std::move(dtls)),
      rtp_transport_(std::move(rtp_transport)) {}

JsepTransport::~JsepTransport() = default;

bool JsepTransport::SetLocalJsepTransportDescription(const JsepTransportDescription& d, p2p::SdpType type,
                                                     Timestamp now, std::string* error) {
  const p2p::IceParameters ice_parameters = d.transport_desc.GetIceParameters();
  if (const std::string e = ice_parameters.Validate(); !e.empty()) {
    return Fail(error, "Invalid ICE parameters: " + e);
  }
  // SetRtcpMux: with kRtcpMuxPolicyRequire there is no RTCP transport and every section has rtcp-mux.
  if (!d.rtcp_mux_enabled) return Fail(error, "Failed to setup RTCP mux.");
  rtp_transport_->UpdateRecvEncryptedHeaderExtensionIds(d.encrypted_header_extension_ids);
  local_description_ = std::make_unique<JsepTransportDescription>(d);

  // VerifyCertificateFingerprint.
  if (const tls::SslFingerprint* local_fp = local_description_->transport_desc.identity_fingerprint.get()) {
    if (!local_certificate_) {
      local_description_.reset();
      return Fail(error, "Fingerprint provided but no identity available.");
    }
    auto fp_tmp = tls::SslFingerprint::Create(local_fp->algorithm, *local_certificate_);
    if (!fp_tmp || !(*fp_tmp == *local_fp)) {
      local_description_.reset();
      return Fail(error, "Local fingerprint does not match identity.");
    }
  } else {
    local_certificate_ = nullptr;
  }
  ice_->SetIceParameters(ice_parameters);
  if (type == p2p::SdpType::kPrAnswer || type == p2p::SdpType::kAnswer) {
    if (!NegotiateAndSetDtlsParameters(type, now, error)) {
      local_description_.reset();
      return false;
    }
  }
  return true;
}

bool JsepTransport::SetRemoteJsepTransportDescription(const JsepTransportDescription& d, p2p::SdpType type,
                                                      Timestamp now, std::string* error) {
  const p2p::IceParameters ice_parameters = d.transport_desc.GetIceParameters();
  if (const std::string e = ice_parameters.Validate(); !e.empty()) {
    remote_description_.reset();
    return Fail(error, "Invalid ICE parameters: " + e);
  }
  if (!d.rtcp_mux_enabled) return Fail(error, "Failed to setup RTCP mux.");
  rtp_transport_->UpdateSendEncryptedHeaderExtensionIds(d.encrypted_header_extension_ids);
  remote_description_ = std::make_unique<JsepTransportDescription>(d);
  // SetRemoteIceParameters.
  ice_->SetRemoteIceParameters(ice_parameters);
  if (type == p2p::SdpType::kPrAnswer || type == p2p::SdpType::kAnswer) {
    if (!NegotiateAndSetDtlsParameters(p2p::SdpType::kOffer, now, error)) {
      remote_description_.reset();
      return false;
    }
  }
  return true;
}

bool JsepTransport::AddRemoteCandidates(const std::vector<p2p::Candidate>& candidates, Timestamp now,
                                        std::string* error) {
  if (!remote_description_) {
    return Fail(error, mid_ + " is not ready to use the remote candidate because the remote description is not set.");
  }
  for (const p2p::Candidate& candidate : candidates) {
    if (candidate.component() != p2p::ICE_CANDIDATE_COMPONENT_RTP) {
      return Fail(error, "Candidate has an unknown component: " + candidate.ToString() + " for mid " + mid_);
    }
    ice_->AddRemoteCandidate(now, candidate);
  }
  return true;
}

std::optional<p2p::SslRole> JsepTransport::GetDtlsRole() const {
  p2p::SslRole role;
  if (!dtls_->GetDtlsRole(&role)) return std::nullopt;
  return role;
}

bool JsepTransport::NegotiateAndSetDtlsParameters(p2p::SdpType local_description_type, Timestamp now,
                                                  std::string* error) {
  if (!local_description_ || !remote_description_) {
    return Fail(error, "Applying an answer transport description without applying any offer.");
  }
  std::optional<p2p::SslRole> negotiated_dtls_role;
  const tls::SslFingerprint* local_fp = local_description_->transport_desc.identity_fingerprint.get();
  const tls::SslFingerprint* remote_fp = remote_description_->transport_desc.identity_fingerprint.get();
  std::string algorithm;
  std::vector<uint8_t> digest;
  if (remote_fp && local_fp) {
    algorithm = remote_fp->algorithm;
    digest = remote_fp->digest;
    if (!p2p::NegotiateDtlsRole(local_description_type, local_description_->transport_desc.connection_role,
                                remote_description_->transport_desc.connection_role, GetDtlsRole(),
                                negotiated_dtls_role)) {
      return Fail(error, "Failed to negotiate the DTLS role.");
    }
  } else if (local_fp && local_description_type == p2p::SdpType::kAnswer) {
    return Fail(error, "Local fingerprint supplied when caller didn't offer DTLS.");
  }
  // SetNegotiatedDtlsParameters.
  if (!dtls_->SetRemoteParameters(algorithm, digest, negotiated_dtls_role, now)) {
    return Fail(error, "Failed to apply remote fingerprint.");
  }
  return true;
}

// ---------------------------------------------------------------------------------------------------------------
// JsepTransportController

JsepTransportController::JsepTransportController(p2p::PortAllocator* allocator, Config config)
    : allocator_(allocator), config_(std::move(config)) {}

JsepTransportController::~JsepTransportController() {
  mid_to_transport_.clear();
  transports_.clear();
}

bool JsepTransportController::SetLocalCertificate(std::shared_ptr<const tls::SslIdentity> certificate) {
  // Can't change a certificate, or set a null certificate.
  if (certificate_ || !certificate) return false;
  certificate_ = std::move(certificate);
  for (auto& [name, t] : transports_) t->rtp_dtls_transport()->SetLocalCertificate(certificate_);
  return true;
}

bool JsepTransportController::SetLocalDescription(p2p::SdpType type, const SessionDescription* local_desc,
                                                  const SessionDescription* remote_desc, Timestamp now,
                                                  std::string* error) {
  // RFC 8445 section 6.1.1: the role is set by the first local description (controlling for an offer).
  if (!ice_role_initialized_) {
    ice_role_initialized_ = true;
    SetIceRole(type == p2p::SdpType::kOffer ? p2p::ICEROLE_CONTROLLING : p2p::ICEROLE_CONTROLLED);
  }
  return ApplyDescription(/*local=*/true, type, local_desc, remote_desc, now, error);
}

bool JsepTransportController::SetRemoteDescription(p2p::SdpType type, const SessionDescription* local_desc,
                                                   const SessionDescription* remote_desc, Timestamp now,
                                                   std::string* error) {
  return ApplyDescription(/*local=*/false, type, local_desc, remote_desc, now, error);
}

bool JsepTransportController::ApplyDescription(bool local, p2p::SdpType type, const SessionDescription* local_desc,
                                               const SessionDescription* remote_desc, Timestamp now,
                                               std::string* error) {
  const SessionDescription* description = local ? local_desc : remote_desc;
  if (!description) return Fail(error, "No description.");
  if (!ValidateAndMaybeUpdateBundleGroups(local, type, local_desc, remote_desc, error)) return false;

  // MergeEncryptedHeaderExtensionIdsForBundles.
  std::map<const ContentGroup*, std::vector<int>> merged_ids_by_bundle;
  for (const ContentInfo& content : description->contents()) {
    const ContentGroup* group = LookupGroupByMid(content.mid());
    if (!group) continue;
    std::vector<int>& merged = merged_ids_by_bundle[group];
    for (int id : GetEncryptedHeaderExtensionIds(content)) {
      if (std::find(merged.begin(), merged.end(), id) == merged.end()) merged.push_back(id);
    }
  }

  for (const ContentInfo& content : description->contents()) {
    if (content.rejected || !IsFirstMidInGroup(content.mid())) continue;
    MaybeCreateJsepTransport(content, now);
  }

  if (description->contents().size() != description->transport_infos().size()) {
    return Fail(error, "The description has a transport for each m= section.");
  }
  for (size_t i = 0; i < description->contents().size(); ++i) {
    const ContentInfo& content = description->contents()[i];
    const TransportInfo& transport_info = description->transport_infos()[i];
    if (content.rejected) {
      HandleRejectedContent(content);
      continue;
    }
    const ContentGroup* established_bundle_group = LookupGroupByMid(content.mid());
    const bool bundled_not_first =
        established_bundle_group && content.mid() != *established_bundle_group->FirstContentName();
    if (bundled_not_first) {
      // HandleBundledContent.
      JsepTransport* bundle_transport = GetJsepTransportByName(*established_bundle_group->FirstContentName());
      if (!bundle_transport) {
        return Fail(error, "Failed to process the bundled m= section with mid='" + content.mid() + "'.");
      }
      mid_to_transport_[content.mid()] = bundle_transport;
    }
    JsepTransport* transport = GetJsepTransportForMid(content.mid());
    if (!transport) return Fail(error, "Could not find transport for m= section with mid='" + content.mid() + "'");
    if (bundled_not_first) continue;
    if (!ValidateContent(content, error)) return false;

    const std::vector<int> extension_ids = established_bundle_group ? merged_ids_by_bundle[established_bundle_group]
                                                                    : GetEncryptedHeaderExtensionIds(content);
    SetIceRole(DetermineIceRole(transport, transport_info, type, local));

    // CreateJsepTransportDescription.
    JsepTransportDescription jsep_description;
    jsep_description.rtcp_mux_enabled =
        content.type == MediaProtocolType::kSctp ? true : content.media_description()->rtcp_mux();
    jsep_description.encrypted_header_extension_ids = extension_ids;
    jsep_description.transport_desc = transport_info.description;
    std::string e;
    const bool ok = local ? transport->SetLocalJsepTransportDescription(jsep_description, type, now, &e)
                          : transport->SetRemoteJsepTransportDescription(jsep_description, type, now, &e);
    if (!ok) {
      return Fail(error, "Failed to apply the description for m= section with mid='" + content.mid() + "': " + e);
    }
  }
  // CommitTransports: an answer settles which transports the mids use; the others go.
  if (type == p2p::SdpType::kAnswer) DestroyUnusedTransports();
  return true;
}

bool JsepTransportController::ValidateAndMaybeUpdateBundleGroups(bool local, p2p::SdpType type,
                                                                 const SessionDescription* local_desc,
                                                                 const SessionDescription* remote_desc,
                                                                 std::string* error) {
  const SessionDescription* description = local ? local_desc : remote_desc;
  const std::vector<const ContentGroup*> new_bundle_groups = description->GetGroupsByName(GROUP_TYPE_BUNDLE);
  std::map<std::string, const ContentGroup*> new_bundle_groups_by_mid;
  for (const ContentGroup* group : new_bundle_groups) {
    for (const std::string& name : group->content_names()) {
      if (new_bundle_groups_by_mid.count(name)) {
        return Fail(error, "A BUNDLE group contains a MID='" + name + "' that is already in a BUNDLE group.");
      }
      new_bundle_groups_by_mid[name] = group;
      if (!description->GetContentByName(name)) {
        return Fail(error, "A BUNDLE group contains a MID='" + name + "' matching no m= section.");
      }
    }
  }
  if (type == p2p::SdpType::kOffer) {
    // A mid of an established group may not move to another group.
    std::map<const ContentGroup*, const ContentGroup*> new_by_existing, existing_by_new;
    for (const ContentGroup* group : new_bundle_groups) {
      for (const std::string& mid : group->content_names()) {
        const ContentGroup* existing = LookupGroupByMid(mid);
        if (!existing) continue;
        auto it = new_by_existing.find(existing);
        if (it != new_by_existing.end() && it->second != group) {
          return Fail(error, "MID " + mid + " in the offer has changed group.");
        }
        new_by_existing[existing] = group;
        it = existing_by_new.find(group);
        if (it != existing_by_new.end() && it->second != existing) {
          return Fail(error, "MID " + mid + " in the offer has changed group.");
        }
        existing_by_new[group] = existing;
      }
    }
  } else if (type == p2p::SdpType::kAnswer && ((local && remote_desc) || (!local && local_desc))) {
    const SessionDescription* offer = local ? remote_desc : local_desc;
    std::map<std::string, const ContentGroup*> offered_by_mid;
    for (const ContentGroup* group : offer->GetGroupsByName(GROUP_TYPE_BUNDLE)) {
      for (const std::string& name : group->content_names()) offered_by_mid[name] = group;
    }
    std::map<const ContentGroup*, const ContentGroup*> new_by_offered;
    for (const ContentGroup* group : new_bundle_groups) {
      if (!group->FirstContentName()) continue;
      auto it = offered_by_mid.find(*group->FirstContentName());
      if (it == offered_by_mid.end()) {
        return Fail(error, "A BUNDLE group was added in the answer that did not exist in the offer.");
      }
      const ContentGroup* offered = it->second;
      if (new_by_offered.count(offered)) return Fail(error, "A MID in the answer has changed group.");
      new_by_offered[offered] = group;
      for (const std::string& name : group->content_names()) {
        it = offered_by_mid.find(name);
        if (it == offered_by_mid.end() || it->second != offered) {
          return Fail(error, "A BUNDLE group in answer contains a MID='" + name + "' that was not in the offered group.");
        }
      }
    }
    for (const ContentGroup& group : bundle_groups_) {
      for (const std::string& name : group.content_names()) {
        if (new_bundle_groups_by_mid.count(name)) continue;
        const ContentInfo* content = description->GetContentByName(name);
        if (!content || !content->rejected) {
          return Fail(error, "Answer cannot remove m= section with mid='" + name +
                                 "' from already-established BUNDLE group.");
        }
      }
    }
  }
  if (config_.bundle_policy == BundlePolicy::kMaxBundle && !description->HasGroup(GROUP_TYPE_BUNDLE) &&
      description->contents().size() > 1) {
    return Fail(error, "max-bundle is used but no bundle group found.");
  }
  UpdateBundles(description, type);
  for (const ContentGroup& group : bundle_groups_) {
    if (!group.FirstContentName()) continue;
    const ContentInfo* bundled = description->GetContentByName(*group.FirstContentName());
    if (!bundled) return Fail(error, "An m= section associated with the BUNDLE-tag doesn't exist.");
    if (bundled->rejected) {
      for (const std::string& name : group.content_names()) {
        const ContentInfo* other = description->GetContentByName(name);
        if (!other) return Fail(error, "A BUNDLE group contains a MID='" + name + "' matching no m= section.");
        if (!other->rejected) return Fail(error, "The m= section with mid='" + name + "' should be rejected.");
      }
    }
  }
  return true;
}

// BundleManager::Update: max-bundle or an answer replaces every group; an offer only updates the groups already
// established (RFC 8843 section 7.3.2: the answerer cannot take a section out of one without rejecting it).
void JsepTransportController::UpdateBundles(const SessionDescription* description, p2p::SdpType type) {
  const std::vector<const ContentGroup*> groups = description->GetGroupsByName(GROUP_TYPE_BUNDLE);
  if (config_.bundle_policy == BundlePolicy::kMaxBundle || type == p2p::SdpType::kAnswer) {
    bundle_groups_.clear();
    for (const ContentGroup* g : groups) bundle_groups_.push_back(*g);
  } else if (type == p2p::SdpType::kOffer) {
    for (const ContentGroup* g : groups) {
      for (const std::string& mid : g->content_names()) {
        auto it = std::find_if(bundle_groups_.begin(), bundle_groups_.end(),
                               [&](const ContentGroup& e) { return e.HasContentName(mid); });
        if (it != bundle_groups_.end()) {
          *it = *g;
          break;
        }
      }
    }
  }
}

const ContentGroup* JsepTransportController::LookupGroupByMid(std::string_view mid) const {
  for (const ContentGroup& g : bundle_groups_) {
    if (g.HasContentName(mid)) return &g;
  }
  return nullptr;
}

bool JsepTransportController::IsFirstMidInGroup(std::string_view mid) const {
  const ContentGroup* group = LookupGroupByMid(mid);
  return !group || (group->FirstContentName() && *group->FirstContentName() == mid);
}

bool JsepTransportController::ValidateContent(const ContentInfo& content, std::string* error) const {
  // kRtcpMuxPolicyRequire.
  if (content.type == MediaProtocolType::kRtp && !content.bundle_only && !content.media_description()->rtcp_mux()) {
    return Fail(error, "The m= section with mid='" + content.mid() +
                           "' is invalid. RTCP-MUX is not enabled when it is required.");
  }
  return true;
}

void JsepTransportController::HandleRejectedContent(const ContentInfo& content) {
  auto group = std::find_if(bundle_groups_.begin(), bundle_groups_.end(),
                            [&](const ContentGroup& g) { return g.HasContentName(content.mid()); });
  if (group != bundle_groups_.end() && !group->content_names().empty() &&
      content.mid() == *group->FirstContentName()) {
    // The group's first section is rejected: so is every section in it.
    for (const std::string& name : group->content_names()) mid_to_transport_.erase(name);
    bundle_groups_.erase(group);
  } else {
    mid_to_transport_.erase(content.mid());
    if (group != bundle_groups_.end()) group->RemoveContentName(content.mid());
  }
  DestroyUnusedTransports();
}

JsepTransport* JsepTransportController::GetJsepTransportByName(std::string_view name) const {
  auto it = transports_.find(name);
  return it == transports_.end() ? nullptr : it->second.get();
}

JsepTransport* JsepTransportController::GetJsepTransportForMid(std::string_view mid) const {
  auto it = mid_to_transport_.find(mid);
  return it == mid_to_transport_.end() ? nullptr : it->second;
}

DtlsSrtpTransport* JsepTransportController::GetRtpTransport(std::string_view mid) const {
  JsepTransport* t = GetJsepTransportForMid(mid);
  return t ? t->rtp_transport() : nullptr;
}

p2p::DtlsTransport* JsepTransportController::GetDtlsTransport(std::string_view mid) const {
  JsepTransport* t = GetJsepTransportForMid(mid);
  return t ? t->rtp_dtls_transport() : nullptr;
}

std::optional<p2p::SslRole> JsepTransportController::GetDtlsRole(std::string_view mid) const {
  JsepTransport* t = GetJsepTransportForMid(mid);
  return t ? t->GetDtlsRole() : std::nullopt;
}

// MaybeCreateJsepTransport, CreateDtlsTransport and CreateRtpTransport (the DTLS-SRTP one, with rtcp-mux).
void JsepTransportController::MaybeCreateJsepTransport(const ContentInfo& content, Timestamp now) {
  if (GetJsepTransportByName(content.mid())) return;
  auto ice = std::make_unique<p2p::P2PTransportChannel>(now, content.mid(), p2p::ICE_CANDIDATE_COMPONENT_RTP,
                                                        allocator_);
  if (config_.ice_config) ice->SetIceConfig(*config_.ice_config);
  ice->SetIceRole(ice_role_);
  const std::string name = content.mid();
  ice->SubscribeCandidateGathered(this, [this, name](p2p::IceTransportInternal*, const p2p::Candidate& c) {
    if (on_candidate_) on_candidate_(name, c);
  });
  auto dtls = std::make_unique<p2p::DtlsTransport>(ice.get());
  if (certificate_) dtls->SetLocalCertificate(certificate_);
  auto rtp_transport = std::make_unique<DtlsSrtpTransport>(/*rtcp_mux_enabled=*/true);
  rtp_transport->SetDtlsTransports(dtls.get(), nullptr);
  auto transport = std::make_unique<JsepTransport>(name, certificate_, std::move(ice), std::move(dtls),
                                                   std::move(rtp_transport));
  mid_to_transport_[name] = transport.get();
  transports_[name] = std::move(transport);
}

void JsepTransportController::DestroyUnusedTransports() {
  std::set<const JsepTransport*> used;
  for (const auto& [mid, t] : mid_to_transport_) used.insert(t);
  for (auto it = transports_.begin(); it != transports_.end();) {
    it = used.count(it->second.get()) ? std::next(it) : transports_.erase(it);
  }
}

void JsepTransportController::SetIceRole(p2p::IceRole role) {
  ice_role_ = role;
  for (auto& [name, t] : transports_) t->ice_transport()->SetIceRole(ice_role_);
}

p2p::IceRole JsepTransportController::DetermineIceRole(const JsepTransport* transport,
                                                       const TransportInfo& transport_info, p2p::SdpType /*type*/,
                                                       bool local) const {
  p2p::IceRole ice_role = ice_role_;
  const p2p::TransportDescription& tdesc = transport_info.description;
  if (local) {
    // The remote end is ICE-lite and this one full: this end must be controlling.
    if (transport->remote_description() &&
        transport->remote_description()->transport_desc.ice_mode == p2p::ICEMODE_LITE &&
        ice_role_ == p2p::ICEROLE_CONTROLLED && tdesc.ice_mode == p2p::ICEMODE_FULL) {
      ice_role = p2p::ICEROLE_CONTROLLING;
    }
  } else {
    if (ice_role_ == p2p::ICEROLE_CONTROLLED && tdesc.ice_mode == p2p::ICEMODE_LITE) {
      ice_role = p2p::ICEROLE_CONTROLLING;
    }
    // This end is ICE-lite and the remote full: the remote end controls.
    if (transport->local_description() &&
        transport->local_description()->transport_desc.ice_mode == p2p::ICEMODE_LITE &&
        ice_role_ == p2p::ICEROLE_CONTROLLING && tdesc.ice_mode == p2p::ICEMODE_FULL) {
      ice_role = p2p::ICEROLE_CONTROLLED;
    }
  }
  return ice_role;
}

std::vector<int> JsepTransportController::GetEncryptedHeaderExtensionIds(const ContentInfo& content) const {
  std::vector<int> ids;
  if (!config_.enable_encrypted_rtp_header_extensions || !content.media_description()) return ids;
  for (const RtpExtension& e : content.media_description()->rtp_header_extensions()) {
    if (!e.encrypt) continue;
    if (std::find(ids.begin(), ids.end(), e.id) == ids.end()) ids.push_back(e.id);
  }
  return ids;
}

bool JsepTransportController::AddRemoteCandidates(std::string_view mid, const std::vector<p2p::Candidate>& candidates,
                                                  Timestamp now, std::string* error) {
  JsepTransport* t = GetJsepTransportForMid(mid);
  if (!t) return Fail(error, "The transport doesn't exist for mid " + std::string(mid));
  return t->AddRemoteCandidates(candidates, now, error);
}

void JsepTransportController::MaybeStartGathering(Timestamp now) {
  for (auto& [name, t] : transports_) t->ice_transport()->MaybeStartGathering(now);
}

void JsepTransportController::Process(Timestamp now) {
  for (auto& [name, t] : transports_) {
    t->ice_transport()->Process(now);
    if (auto due = t->rtp_dtls_transport()->NextTimeout(); due && *due <= now) t->rtp_dtls_transport()->OnTimeout(now);
  }
}

Timestamp JsepTransportController::NextProcessTime() const {
  Timestamp next = Timestamp::PlusInfinity();
  for (const auto& [name, t] : transports_) {
    next = std::min(next, t->ice_transport()->NextProcessTime());
    if (auto due = t->rtp_dtls_transport()->NextTimeout()) next = std::min(next, *due);
  }
  return next;
}

}  // namespace sm::pc
