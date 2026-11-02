// PortAllocator and PortAllocatorSession (issue #1001): libwebrtc M157's
// p2p/base/port_allocator.cc, and IceCredentialsIterator::
// CreateRandomIceCredentials (p2p/base/ice_credentials_iterator.cc).
#include "sm/p2p/port_allocator.h"

#include <algorithm>
#include <iterator>

#include "sm/core/random.h"
#include "sm/p2p/p2p_constants.h"
#include "sm/p2p/stun.h"

namespace sm::p2p {

IceParameters CreateRandomIceCredentials() {
  return IceParameters{CreateRandomString(ICE_UFRAG_LENGTH), CreateRandomString(ICE_PWD_LENGTH), false};
}

uint64_t CreateRandomId64() {
  uint8_t bytes[8] = {};
  const auto& crypto = GetStunCrypto();
  if (crypto.random_bytes == nullptr || !crypto.random_bytes(std::span<uint8_t>(bytes))) {
    static Random fallback(0x64u);  // No secure source installed: tests only.
    for (auto& b : bytes) b = static_cast<uint8_t>(fallback.Next());
  }
  uint64_t id = 0;
  for (uint8_t b : bytes) id = (id << 8) | b;
  return id;
}

PortAllocatorSession::PortAllocatorSession(std::string_view content_name, int component,
                                           std::string_view ice_ufrag, std::string_view ice_pwd, uint32_t flags)
    : flags_(flags),
      content_name_(content_name),
      component_(component),
      ice_ufrag_(ice_ufrag),
      ice_pwd_(ice_pwd) {}

PortAllocatorSession::~PortAllocatorSession() = default;

void PortAllocatorSession::SetIceParameters(std::string_view content_name, int component,
                                            std::string_view ice_ufrag, std::string_view ice_pwd) {
  content_name_.assign(content_name);
  component_ = component;
  ice_ufrag_.assign(ice_ufrag);
  ice_pwd_.assign(ice_pwd);
  UpdateIceParametersInternal();
}

PortAllocator::PortAllocator() : tiebreaker_(CreateRandomId64()) {}

PortAllocator::~PortAllocator() = default;

bool PortAllocator::SetConfiguration(Timestamp now, const ServerAddresses& stun_servers,
                                     const std::vector<RelayServerConfig>& turn_servers, int candidate_pool_size,
                                     PortPrunePolicy turn_port_prune_policy,
                                     const std::optional<int>& stun_candidate_keepalive_interval_ms) {
  if (candidate_pool_size < 0 || candidate_pool_size > 0xFFFF) return false;
  const bool ice_servers_changed = stun_servers != stun_servers_ || turn_servers != turn_servers_;
  stun_servers_ = stun_servers;
  turn_servers_ = turn_servers;
  turn_port_prune_policy_ = turn_port_prune_policy;
  candidate_pool_size_ = candidate_pool_size;
  // A pooled session gathered with the old servers: discard it.
  if (ice_servers_changed) pooled_sessions_.clear();
  while (candidate_pool_size_ < static_cast<int>(pooled_sessions_.size())) pooled_sessions_.pop_back();
  stun_candidate_keepalive_interval_ =
      stun_candidate_keepalive_interval_ms
          ? std::optional<TimeDelta>(TimeDelta::Millis(*stun_candidate_keepalive_interval_ms))
          : std::nullopt;
  for (const auto& session : pooled_sessions_) {
    session->SetStunKeepaliveIntervalForReadyPorts(stun_candidate_keepalive_interval_);
  }
  while (static_cast<int>(pooled_sessions_.size()) < candidate_pool_size_) {
    const IceParameters credentials = CreateRandomIceCredentials();
    std::unique_ptr<PortAllocatorSession> pooled = CreateSessionInternal("", 0, credentials.ufrag, credentials.pwd);
    pooled->set_pooled(true);
    pooled->StartGettingPorts(now);
    pooled_sessions_.push_back(std::move(pooled));
  }
  return true;
}

std::unique_ptr<PortAllocatorSession> PortAllocator::CreateSession(std::string_view content_name, int component,
                                                                   std::string_view ice_ufrag,
                                                                   std::string_view ice_pwd) {
  auto session = CreateSessionInternal(content_name, component, ice_ufrag, ice_pwd);
  session->SetCandidateFilter(candidate_filter());
  return session;
}

std::unique_ptr<PortAllocatorSession> PortAllocator::TakePooledSession(std::string_view content_name,
                                                                       int component, std::string_view ice_ufrag,
                                                                       std::string_view ice_pwd) {
  if (pooled_sessions_.empty() || ice_ufrag.empty() || ice_pwd.empty()) return nullptr;
  const IceParameters credentials{std::string(ice_ufrag), std::string(ice_pwd), false};
  auto cit = FindPooledSession(restrict_ice_credentials_change_ ? &credentials : nullptr);
  if (cit == pooled_sessions_.end()) return nullptr;
  auto it = pooled_sessions_.begin() + std::distance(pooled_sessions_.cbegin(), cit);
  std::unique_ptr<PortAllocatorSession> ret = std::move(*it);
  ret->SetIceParameters(content_name, component, ice_ufrag, ice_pwd);
  ret->set_pooled(false);
  // According to JSEP, a pooled session should filter candidates only after
  // it's taken out of the pool.
  ret->SetCandidateFilter(candidate_filter());
  pooled_sessions_.erase(it);
  return ret;
}

const PortAllocatorSession* PortAllocator::GetPooledSession(const IceParameters* ice_credentials) const {
  auto it = FindPooledSession(ice_credentials);
  return it == pooled_sessions_.end() ? nullptr : it->get();
}

std::vector<std::unique_ptr<PortAllocatorSession>>::const_iterator PortAllocator::FindPooledSession(
    const IceParameters* ice_credentials) const {
  for (auto it = pooled_sessions_.begin(); it != pooled_sessions_.end(); ++it) {
    if (ice_credentials == nullptr ||
        ((*it)->ice_ufrag() == ice_credentials->ufrag && (*it)->ice_pwd() == ice_credentials->pwd)) {
      return it;
    }
  }
  return pooled_sessions_.end();
}

std::vector<IceParameters> PortAllocator::GetPooledIceCredentials() const {
  std::vector<IceParameters> list;
  for (const auto& session : pooled_sessions_) list.push_back({session->ice_ufrag(), session->ice_pwd(), false});
  return list;
}

void PortAllocator::ProcessPooledSessions(Timestamp now) {
  for (const auto& session : pooled_sessions_) session->Process(now);
}

Timestamp PortAllocator::NextPooledSessionsProcessTime() const {
  Timestamp next = Timestamp::PlusInfinity();
  for (const auto& session : pooled_sessions_) next = std::min(next, session->NextProcessTime());
  return next;
}

void PortAllocator::SetCandidateFilter(uint32_t filter) {
  if (candidate_filter_ == filter) return;
  const uint32_t prev_filter = candidate_filter_;
  candidate_filter_ = filter;
  candidate_filter_changed_.Send(prev_filter, filter);
}

Candidate PortAllocator::SanitizeCandidate(const Candidate& c) const {
  // With mDNS off (MdnsObfuscationEnabled() is false here), only the related
  // address of srflx and relay candidates can be filtered.
  const bool filter_stun_related_address =
      ((flags() & PORTALLOCATOR_DISABLE_ADAPTER_ENUMERATION) &&
       (flags() & PORTALLOCATOR_DISABLE_DEFAULT_LOCAL_CANDIDATE)) ||
      !(candidate_filter_ & CF_HOST) || MdnsObfuscationEnabled();
  const bool filter_turn_related_address = !(candidate_filter_ & CF_REFLEXIVE);
  const bool filter_prflx_related_address = MdnsObfuscationEnabled();
  const bool filter_related_address = (c.is_stun() && filter_stun_related_address) ||
                                      (c.is_relay() && filter_turn_related_address) ||
                                      (c.is_prflx() && filter_prflx_related_address);
  // Candidate::ToSanitizedCopy(false, filter_related_address, false).
  Candidate copy(c);
  if (filter_related_address) {
    // EmptySocketAddressWithFamily.
    const IpFamily family = copy.address().family();
    copy.set_related_address(family == IpFamily::kUnspec ? SocketAddress() : SocketAddress(GetAnyIp(family), 0));
  }
  return copy;
}

void ConfigureAsPeerConnection(Timestamp now, PortAllocator* allocator, const ServerAddresses& stun_servers,
                               const std::vector<RelayServerConfig>& turn_servers,
                               const PeerConnectionAllocatorConfig& config) {
  uint32_t flags = allocator->flags();
  flags |= PORTALLOCATOR_ENABLE_SHARED_SOCKET | PORTALLOCATOR_ENABLE_IPV6 | PORTALLOCATOR_ENABLE_IPV6_ON_WIFI;
  if (!config.ipv6_default) flags &= ~uint32_t{PORTALLOCATOR_ENABLE_IPV6};
  if (config.disable_ipv6_on_wifi) flags &= ~uint32_t{PORTALLOCATOR_ENABLE_IPV6_ON_WIFI};
  if (!config.tcp_candidates_enabled) flags |= PORTALLOCATOR_DISABLE_TCP;
  if (config.low_cost_networks_only) flags |= PORTALLOCATOR_DISABLE_COSTLY_NETWORKS;
  if (config.disable_link_local_networks) flags |= PORTALLOCATOR_DISABLE_LINK_LOCAL_NETWORKS;
  allocator->set_flags(flags);
  allocator->set_step_delay(kMinimumStepDelay);
  allocator->SetCandidateFilter(config.candidate_filter);
  allocator->set_max_ipv6_networks(config.max_ipv6_networks);
  // Last: it may create pooled sessions with the settings above.
  allocator->SetConfiguration(now, stun_servers, turn_servers, config.ice_candidate_pool_size,
                              config.prune_turn_ports ? PRUNE_BASED_ON_PRIORITY : config.turn_port_prune_policy,
                              config.stun_candidate_keepalive_interval_ms);
}

}  // namespace sm::p2p
