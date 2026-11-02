// BasicPortAllocator, BasicPortAllocatorSession, PortConfiguration and
// AllocationSequence (issue #1001): libwebrtc M157's
// p2p/client/basic_port_allocator.cc. See include/sm/p2p/basic_port_allocator.h.
#include "sm/p2p/basic_port_allocator.h"

#include <algorithm>
#include <set>

namespace sm::p2p {
namespace {

constexpr int PHASE_UDP = 0;
constexpr int PHASE_RELAY = 1;
constexpr int PHASE_TCP = 2;

constexpr uint32_t DISABLE_ALL_PHASES =
    PORTALLOCATOR_DISABLE_UDP | PORTALLOCATOR_DISABLE_TCP | PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY;

// UDP > TCP > SSLTCP == TLS.
int GetProtocolPriority(ProtocolType protocol) {
  switch (protocol) {
    case PROTO_UDP:
      return 2;
    case PROTO_TCP:
      return 1;
    case PROTO_SSLTCP:
    case PROTO_TLS:
      return 0;
  }
  return 0;
}

// IPv6 > IPv4 > unspecified.
int GetAddressFamilyPriority(IpFamily family) {
  switch (family) {
    case IpFamily::kV6:
      return 2;
    case IpFamily::kV4:
      return 1;
    default:
      return 0;
  }
}

// Positive if a is better, negative if b is better, 0 otherwise.
int ComparePort(const Port* a, const Port* b) {
  const int cmp_protocol = GetProtocolPriority(a->GetProtocol()) - GetProtocolPriority(b->GetProtocol());
  if (cmp_protocol != 0) return cmp_protocol;
  return GetAddressFamilyPriority(a->network()->GetBestIP().family()) -
         GetAddressFamilyPriority(b->network()->GetBestIP().family());
}

// Removes the networks `remove` holds for (std::partition, as FilterNetworks).
template <class Pred>
void FilterNetworks(std::vector<const Network*>* networks, Pred remove) {
  auto start_to_remove =
      std::partition(networks->begin(), networks->end(), [&](const Network* n) { return !remove(n); });
  networks->erase(start_to_remove, networks->end());
}

bool IsAllowedByCandidateFilter(const Candidate& c, uint32_t filter) {
  // An any-address candidate (bound before any packet was sent) is not a
  // valid ICE candidate.
  if (c.address().IsAnyIp()) return false;
  if (c.is_relay()) return (filter & CF_RELAY) != 0;
  if (c.is_stun()) return (filter & CF_REFLEXIVE) != 0;
  if (c.is_local()) {
    // A public host address is also its reflexive one (no srflx candidate is
    // made for it), so a reflexive-only filter lets it through.
    if ((filter & CF_REFLEXIVE) && !c.address().IsPrivateIp()) return true;
    return (filter & CF_HOST) != 0;
  }
  return false;
}

}  // namespace

// ------------------------------------------------------------ BasicPortAllocator

BasicPortAllocator::BasicPortAllocator(NetworkManager* network_manager, PacketSocketFactory* socket_factory,
                                       RelayPortFactoryInterface* relay_port_factory)
    : network_manager_(network_manager), socket_factory_(socket_factory), relay_port_factory_(relay_port_factory) {
  SetConfiguration(Timestamp::MinusInfinity(), ServerAddresses(), {}, 0, NO_PRUNE);
}

BasicPortAllocator::~BasicPortAllocator() {
  // The pooled sessions use this allocator: destroy them first.
  DiscardCandidatePool();
}

int BasicPortAllocator::GetNetworkIgnoreMask() const {
  int mask = network_ignore_mask_;
  switch (vpn_preference_) {
    case VpnPreference::kOnlyUseVpn:
      mask |= ~static_cast<int>(ADAPTER_TYPE_VPN);
      break;
    case VpnPreference::kNeverUseVpn:
      mask |= static_cast<int>(ADAPTER_TYPE_VPN);
      break;
    default:
      break;
  }
  return mask;
}

std::unique_ptr<PortAllocatorSession> BasicPortAllocator::CreateSessionInternal(std::string_view content_name,
                                                                                int component,
                                                                                std::string_view ice_ufrag,
                                                                                std::string_view ice_pwd) {
  return std::make_unique<BasicPortAllocatorSession>(this, content_name, component, ice_ufrag, ice_pwd);
}

void BasicPortAllocator::AddTurnServerForTesting(Timestamp now, const RelayServerConfig& turn_server) {
  std::vector<RelayServerConfig> servers = turn_servers();
  servers.push_back(turn_server);
  SetConfiguration(now, stun_servers(), servers, candidate_pool_size(), turn_port_prune_policy());
}

// ------------------------------------------------------ BasicPortAllocatorSession

BasicPortAllocatorSession::BasicPortAllocatorSession(BasicPortAllocator* allocator, std::string_view content_name,
                                                     int component, std::string_view ice_ufrag,
                                                     std::string_view ice_pwd)
    : PortAllocatorSession(content_name, component, ice_ufrag, ice_pwd, allocator->flags()),
      allocator_(allocator),
      socket_factory_(allocator->socket_factory()),
      turn_port_prune_policy_(allocator->turn_port_prune_policy()) {
  // libwebrtc runs OnNetworksChanged on the network thread; queue it, so it
  // runs in order with what StartGettingPorts posts.
  allocator_->network_manager()->SubscribeNetworksChanged(this, [this] {
    Post(TimeDelta::Zero(), [this] { OnNetworksChanged(); });
  });
  allocator_->network_manager()->StartUpdating();
}

BasicPortAllocatorSession::~BasicPortAllocatorSession() {
  allocator_->network_manager()->StopUpdating();
  allocator_->network_manager()->UnsubscribeNetworksChanged(this);
  for (auto& sequence : sequences_) sequence->Clear();
  // The ports go before the sequences: a shared socket outlives its ports.
  ports_.clear();
  owned_ports_.clear();
  destroyed_ports_.clear();
  configs_.clear();
  sequences_.clear();
}

void BasicPortAllocatorSession::Post(TimeDelta delay, std::function<void()> task) {
  tasks_.emplace(std::make_pair(now_ + delay, next_task_seq_++), std::move(task));
}

void BasicPortAllocatorSession::Process(Timestamp now) {
  if (processing_) return;
  processing_ = true;
  now_ = std::max(now_, now);
  // Each pass runs the due tasks in order, then each due port once (its STUN
  // retransmissions, keepalives and destroy checks); another pass follows
  // only if that queued more due tasks.
  for (;;) {
    while (!tasks_.empty() && tasks_.begin()->first.first <= now) {
      auto node = tasks_.extract(tasks_.begin());
      node.mapped()();
    }
    std::vector<Port*> due;
    for (const auto& [port, owned] : owned_ports_) {
      if (port->NextProcessTime() <= now) due.push_back(port);
    }
    for (Port* port : due) {
      if (owned_ports_.count(port) && port->NextProcessTime() <= now) port->Process(now);
    }
    if (tasks_.empty() || tasks_.begin()->first.first > now) break;
  }
  destroyed_ports_.clear();
  processing_ = false;
}

Timestamp BasicPortAllocatorSession::NextProcessTime() const {
  Timestamp next = tasks_.empty() ? Timestamp::PlusInfinity() : tasks_.begin()->first.first;
  for (const auto& [port, owned] : owned_ports_) next = std::min(next, port->NextProcessTime());
  return next;
}

void BasicPortAllocatorSession::SetCandidateFilter(uint32_t filter) {
  if (filter == candidate_filter_) return;
  const uint32_t prev_filter = candidate_filter_;
  candidate_filter_ = filter;
  for (size_t i = 0; i < ports_.size(); ++i) {
    PortData& port_data = ports_[i];
    if (port_data.error() || port_data.pruned()) continue;
    const PortData::State cur_state = port_data.state();
    bool found_signalable_candidate = false;
    bool found_pairable_candidate = false;
    Port* port = port_data.port();
    const std::vector<Candidate> candidates = port->Candidates();
    for (const Candidate& c : candidates) {
      if (!IsStopped() && !IsAllowedByCandidateFilter(c, prev_filter) && IsAllowedByCandidateFilter(c, filter)) {
        // Held back by the previous filter (OnCandidateReady): signal it
        // again, with the port gathering so that it is taken.
        if (!found_signalable_candidate) {
          found_signalable_candidate = true;
          port_data.set_state(PortData::STATE_INPROGRESS);
        }
        port->NotifyCandidateReady(c);
      }
      if (CandidatePairable(c, port)) found_pairable_candidate = true;
    }
    PortData* data = FindPort(port);  // The signal may not move ports_, but be safe.
    if (data == nullptr) continue;
    data->set_state(cur_state);
    // A filter can leave a ready port with no pairable candidate.
    if (!found_pairable_candidate) data->set_has_pairable_candidate(false);
  }
}

void BasicPortAllocatorSession::StartGettingPorts(Timestamp now) {
  now_ = std::max(now_, now);
  state_ = SessionState::GATHERING;
  Post(TimeDelta::Zero(), [this] { GetPortConfigurations(); });
}

void BasicPortAllocatorSession::StopGettingPorts(Timestamp now) {
  ClearGettingPorts(now);
  // After ClearGettingPorts, which sets CLEARED.
  state_ = SessionState::STOPPED;
}

void BasicPortAllocatorSession::ClearGettingPorts(Timestamp now) {
  now_ = std::max(now_, now);
  ++allocation_epoch_;
  for (auto& sequence : sequences_) sequence->Stop();
  Post(TimeDelta::Zero(), [this] { OnConfigStop(); });
  state_ = SessionState::CLEARED;
}

std::vector<const Network*> BasicPortAllocatorSession::GetFailedNetworks() {
  std::vector<const Network*> networks = GetNetworks();
  // An interface (IPv4 and IPv6 networks by name) has failed when none of
  // its ports has a connection. Ports have no connections until #1000.
  std::set<std::string> networks_with_connection;
  std::erase_if(networks,
                [&](const Network* network) { return networks_with_connection.contains(network->name()); });
  return networks;
}

void BasicPortAllocatorSession::RegatherOnFailedNetworks(Timestamp now) {
  now_ = std::max(now_, now);
  std::vector<const Network*> failed_networks = GetFailedNetworks();
  if (failed_networks.empty()) return;
  // A sequence on a failed network is not equivalent to a new one.
  for (auto& sequence : sequences_) {
    if (!sequence->network_failed() &&
        std::find(failed_networks.begin(), failed_networks.end(), sequence->network()) != failed_networks.end()) {
      sequence->set_network_failed();
    }
  }
  Regather(failed_networks, IceRegatheringReason::NETWORK_FAILURE);
}

void BasicPortAllocatorSession::Regather(const std::vector<const Network*>& networks, IceRegatheringReason reason) {
  std::vector<PortData*> ports_to_prune = GetUnprunedPorts(networks);
  if (!ports_to_prune.empty()) PrunePortsAndRemoveCandidates(ports_to_prune);
  if (allocation_started_ && network_manager_started_ && !IsStopped()) {
    NotifyIceRegathering(reason);
    DoAllocate();
  }
}

void BasicPortAllocatorSession::SetStunKeepaliveIntervalForReadyPorts(const std::optional<TimeDelta>& interval) {
  for (Port* port : ReadyPorts()) {
    // UDPPort and StunPort (a TCPPort is a host port over PROTO_TCP).
    if (port->Type() == IceCandidateType::kSrflx ||
        (port->Type() == IceCandidateType::kHost && port->GetProtocol() == PROTO_UDP)) {
      static_cast<UDPPort*>(port)->set_stun_keepalive_delay(interval);
    }
  }
}

std::vector<Port*> BasicPortAllocatorSession::ReadyPorts() const {
  std::vector<Port*> ret;
  for (const PortData& data : ports_) {
    if (data.ready()) ret.push_back(data.port());
  }
  return ret;
}

std::vector<Candidate> BasicPortAllocatorSession::ReadyCandidates() const {
  std::vector<Candidate> candidates;
  for (const PortData& data : ports_) {
    if (data.ready()) GetCandidatesFromPort(data, &candidates);
  }
  return candidates;
}

std::vector<Port*> BasicPortAllocatorSession::AllPortsForTesting() const {
  std::vector<Port*> ret;
  for (const PortData& data : ports_) ret.push_back(data.port());
  return ret;
}

void BasicPortAllocatorSession::GetCandidatesFromPort(const PortData& data,
                                                      std::vector<Candidate>* candidates) const {
  for (const Candidate& candidate : data.port()->Candidates()) {
    if (!CheckCandidateFilter(candidate)) continue;
    candidates->push_back(allocator_->SanitizeCandidate(candidate));
  }
}

bool BasicPortAllocatorSession::CandidatesAllocationDone() const {
  // Done only once every sequence is made, none still runs, and no port is
  // still gathering.
  if (!allocation_sequences_created_) return false;
  if (std::any_of(sequences_.begin(), sequences_.end(),
                  [](const auto& s) { return s->state() == AllocationSequence::kRunning; })) {
    return false;
  }
  return std::none_of(ports_.begin(), ports_.end(), [](const PortData& p) { return p.inprogress(); });
}

void BasicPortAllocatorSession::UpdateIceParametersInternal() {
  for (PortData& port : ports_) {
    port.port()->set_content_name(content_name());
    port.port()->SetIceParameters(component(), ice_ufrag(), ice_pwd());
  }
}

void BasicPortAllocatorSession::GetPortConfigurations() {
  auto config = std::make_unique<PortConfiguration>(allocator_->stun_servers(), username(), password());
  for (const RelayServerConfig& turn_server : allocator_->turn_servers()) config->AddRelay(turn_server);
  ConfigReady(std::move(config));
}

void BasicPortAllocatorSession::ConfigReady(std::unique_ptr<PortConfiguration> config) {
  auto shared = std::make_shared<std::unique_ptr<PortConfiguration>>(std::move(config));
  Post(TimeDelta::Zero(), [this, shared] { OnConfigReady(std::move(*shared)); });
}

void BasicPortAllocatorSession::OnConfigReady(std::unique_ptr<PortConfiguration> config) {
  if (config) configs_.push_back(std::move(config));
  AllocatePorts();
}

void BasicPortAllocatorSession::OnConfigStop() {
  // Ports still gathering will not finish: mark them failed and drop
  // whatever candidates they find later.
  bool send_signal = false;
  for (PortData& data : ports_) {
    if (data.inprogress()) {
      data.set_state(PortData::STATE_ERROR);
      send_signal = true;
    }
  }
  for (auto it = sequences_.begin(); it != sequences_.end() && !send_signal; ++it) {
    if ((*it)->state() == AllocationSequence::kStopped) send_signal = true;
  }
  if (send_signal) MaybeSignalCandidatesAllocationDone();
}

void BasicPortAllocatorSession::AllocatePorts() {
  const int epoch = allocation_epoch_;
  Post(TimeDelta::Zero(), [this, epoch] { OnAllocate(epoch); });
}

void BasicPortAllocatorSession::OnAllocate(int allocation_epoch) {
  if (allocation_epoch != allocation_epoch_) return;
  if (network_manager_started_ && !IsStopped()) DoAllocate();
  allocation_started_ = true;
}

std::vector<const Network*> BasicPortAllocatorSession::GetNetworks() {
  std::vector<const Network*> networks;
  NetworkManager* network_manager = allocator_->network_manager();
  // Blocked enumeration acts as the flag.
  if (network_manager->enumeration_permission() == NetworkManager::ENUMERATION_BLOCKED) {
    set_flags(flags() | PORTALLOCATOR_DISABLE_ADAPTER_ENUMERATION);
  }
  // Without adapter enumeration, bind to the any address, so the OS routes
  // STUN as it routes other traffic (no local or public IP leaks).
  if (flags() & PORTALLOCATOR_DISABLE_ADAPTER_ENUMERATION) {
    networks = network_manager->GetAnyAddressNetworks();
  } else {
    networks = network_manager->GetNetworks();
    // No networks: fall back on the any address (the OS's default route).
    if (networks.empty() || (flags() & PORTALLOCATOR_ENABLE_ANY_ADDRESS_PORTS)) {
      std::vector<const Network*> any = network_manager->GetAnyAddressNetworks();
      networks.insert(networks.end(), any.begin(), any.end());
    }
  }
  if (flags() & PORTALLOCATOR_DISABLE_LINK_LOCAL_NETWORKS) {
    FilterNetworks(&networks, [](const Network* n) { return IpIsLinkLocal(n->prefix()); });
  }
  const int ignore_mask = allocator_->GetNetworkIgnoreMask();
  FilterNetworks(&networks, [ignore_mask](const Network* n) { return (ignore_mask & n->type()) != 0; });
  if (flags() & PORTALLOCATOR_DISABLE_COSTLY_NETWORKS) {
    uint16_t lowest_cost = kNetworkCostMax;
    for (const Network* network : networks) {
      // A link-local network (an iOS device's link to its computer) can't
      // reach a peer: it doesn't set the lowest cost.
      if (IpIsLinkLocal(network->GetBestIP())) continue;
      lowest_cost = std::min<uint16_t>(lowest_cost, network->GetCost());
    }
    FilterNetworks(&networks,
                   [lowest_cost](const Network* n) { return n->GetCost() > lowest_cost + kNetworkCostLow; });
  }
  // At most max_ipv6_networks IPv6 networks, from different adapter types.
  std::vector<const Network*> ipv6_networks;
  for (auto it = networks.begin(); it != networks.end();) {
    if ((*it)->prefix().family() == IpFamily::kV6) {
      ipv6_networks.push_back(*it);
      it = networks.erase(it);
      continue;
    }
    ++it;
  }
  ipv6_networks = SelectIPv6Networks(ipv6_networks, allocator_->max_ipv6_networks());
  networks.insert(networks.end(), ipv6_networks.begin(), ipv6_networks.end());
  return networks;
}

std::vector<const Network*> BasicPortAllocatorSession::SelectIPv6Networks(
    std::vector<const Network*>& all_ipv6_networks, int max_ipv6_networks) {
  if (static_cast<int>(all_ipv6_networks.size()) <= max_ipv6_networks) return all_ipv6_networks;
  // In priority order; "cellular" covers the 2G-5G types.
  const AdapterType adapter_types[] = {ADAPTER_TYPE_ETHERNET, ADAPTER_TYPE_LOOPBACK, ADAPTER_TYPE_WIFI,
                                       ADAPTER_TYPE_CELLULAR, ADAPTER_TYPE_VPN,      ADAPTER_TYPE_UNKNOWN,
                                       ADAPTER_TYPE_ANY};
  const int adapter_types_cnt = static_cast<int>(std::size(adapter_types));
  std::vector<const Network*> selected;
  int adapter_types_pos = 0;
  while (static_cast<int>(selected.size()) < max_ipv6_networks &&
         adapter_types_pos < adapter_types_cnt * max_ipv6_networks) {
    const AdapterType type = adapter_types[adapter_types_pos % adapter_types_cnt];
    for (size_t pos = 0; pos < all_ipv6_networks.size(); ++pos) {
      const Network* n = all_ipv6_networks[pos];
      if (type == n->type() || (type == ADAPTER_TYPE_CELLULAR && n->IsCellular())) {
        selected.push_back(n);
        all_ipv6_networks.erase(all_ipv6_networks.begin() + static_cast<std::ptrdiff_t>(pos));
        break;
      }
    }
    adapter_types_pos++;
  }
  return selected;
}

void BasicPortAllocatorSession::DoAllocate() {
  bool done_signal_needed = false;
  std::vector<const Network*> networks = GetNetworks();
  if (networks.empty()) {
    done_signal_needed = true;  // "Machine has no networks; no ports will be allocated".
  } else {
    PortConfiguration* config = configs_.empty() ? nullptr : configs_.back().get();
    for (const Network* network : networks) {
      uint32_t sequence_flags = flags();
      if ((sequence_flags & DISABLE_ALL_PHASES) == DISABLE_ALL_PHASES) {
        // Every port type disabled: just signal done.
        done_signal_needed = true;
        break;
      }
      if (!config || config->relays.empty()) sequence_flags |= PORTALLOCATOR_DISABLE_RELAY;
      if (!(sequence_flags & PORTALLOCATOR_ENABLE_IPV6) && network->GetBestIP().family() == IpFamily::kV6) {
        continue;
      }
      if (!(sequence_flags & PORTALLOCATOR_ENABLE_IPV6_ON_WIFI) && network->GetBestIP().family() == IpFamily::kV6 &&
          network->type() == ADAPTER_TYPE_WIFI) {
        continue;
      }
      DisableEquivalentPhases(network, config, &sequence_flags);
      if ((sequence_flags & DISABLE_ALL_PHASES) == DISABLE_ALL_PHASES) continue;  // Nothing to do.
      auto sequence = std::make_unique<AllocationSequence>(this, network, config, sequence_flags,
                                                           [this] { OnPortAllocationComplete(); });
      sequence->Init();
      sequence->Start();
      sequences_.push_back(std::move(sequence));
      done_signal_needed = true;
    }
  }
  if (done_signal_needed) Post(TimeDelta::Zero(), [this] { OnAllocationSequenceObjectsCreated(); });
}

void BasicPortAllocatorSession::OnNetworksChanged() {
  std::vector<const Network*> networks = GetNetworks();
  std::vector<const Network*> failed_networks;
  for (auto& sequence : sequences_) {
    // A sequence whose network is gone has failed.
    if (!sequence->network_failed() &&
        std::find(networks.begin(), networks.end(), sequence->network()) == networks.end()) {
      sequence->OnNetworkFailed();
      failed_networks.push_back(sequence->network());
    }
  }
  std::vector<PortData*> ports_to_prune = GetUnprunedPorts(failed_networks);
  if (!ports_to_prune.empty()) PrunePortsAndRemoveCandidates(ports_to_prune);
  if (allocation_started_ && !IsStopped()) {
    // Once the network manager has started, a change means regathering.
    if (network_manager_started_) NotifyIceRegathering(IceRegatheringReason::NETWORK_CHANGE);
    DoAllocate();
  }
  network_manager_started_ = true;
}

void BasicPortAllocatorSession::DisableEquivalentPhases(const Network* network, PortConfiguration* config,
                                                        uint32_t* flags) {
  for (size_t i = 0; i < sequences_.size() && (*flags & DISABLE_ALL_PHASES) != DISABLE_ALL_PHASES; ++i) {
    sequences_[i]->DisableEquivalentPhases(network, config, flags);
  }
}

void BasicPortAllocatorSession::AddAllocatedPort(std::unique_ptr<Port> owned, AllocationSequence* seq) {
  Port* port = owned.get();
  port->set_component(component());
  port->set_generation(generation());
  port->set_send_retransmit_count_attribute((flags() & PORTALLOCATOR_ENABLE_STUN_RETRANSMIT_ATTRIBUTE) != 0);
  owned_ports_.emplace(port, std::move(owned));
  ports_.emplace_back(port, seq);
  port->SubscribeCandidateReady(this, [this](Port* p, const Candidate& c) { OnCandidateReady(p, c); });
  port->SubscribeCandidateError(this,
                                [this](Port* p, const IceCandidateErrorEvent& event) { OnCandidateError(p, event); });
  port->SubscribePortComplete(this, [this](Port* p) { OnPortComplete(p); });
  port->SubscribePortDestroyed(this, [this](Port* p) { OnPortDestroyed(p); });
  port->SubscribePortError(this, [this](Port* p) { OnPortError(p); });
  port->PrepareAddress(now_);
}

void BasicPortAllocatorSession::OnAllocationSequenceObjectsCreated() {
  allocation_sequences_created_ = true;
  // With no sequences, this is the only done signal.
  MaybeSignalCandidatesAllocationDone();
}

void BasicPortAllocatorSession::OnCandidateReady(Port* port, const Candidate& c) {
  PortData* data = FindPort(port);
  if (data == nullptr) return;
  // Gathering on this port is over: drop the candidate.
  if (!data->inprogress()) return;
  // The port has a pairable candidate (a usable one, or it is bound to the
  // any address and has no host candidate): it can make connections.
  bool pruned = false;
  if (CandidatePairable(c, port) && !data->has_pairable_candidate()) {
    data->set_has_pairable_candidate(true);
    if (port->Type() == IceCandidateType::kRelay) {
      if (turn_port_prune_policy_ == KEEP_FIRST_READY) {
        pruned = PruneNewlyPairableTurnPort(data);
      } else if (turn_port_prune_policy_ == PRUNE_BASED_ON_PRIORITY) {
        pruned = PruneTurnPorts(port);
      }
    }
    data = FindPort(port);
    if (data == nullptr) return;
    if (!data->pruned()) {
      NotifyPortReady(port);
      port->KeepAliveUntilPruned();
    }
  }
  data = FindPort(port);
  if (data == nullptr) return;
  if (data->ready() && CheckCandidateFilter(c)) {
    NotifyCandidatesReady({allocator_->SanitizeCandidate(c)});
  }
  if (pruned) MaybeSignalCandidatesAllocationDone();
}

void BasicPortAllocatorSession::OnCandidateError(Port* port, const IceCandidateErrorEvent& event) {
  if (FindPort(port) == nullptr) return;
  // Errors without an address wait for allocation done.
  if (event.address.empty()) {
    candidate_error_events_.push_back(event);
  } else {
    NotifyCandidateError(event);
  }
}

Port* BasicPortAllocatorSession::GetBestTurnPortForNetwork(std::string_view network_name) const {
  Port* best_turn_port = nullptr;
  for (const PortData& data : ports_) {
    if (data.port()->network()->name() == network_name && data.port()->Type() == IceCandidateType::kRelay &&
        data.ready() && (!best_turn_port || ComparePort(data.port(), best_turn_port) > 0)) {
      best_turn_port = data.port();
    }
  }
  return best_turn_port;
}

bool BasicPortAllocatorSession::PruneNewlyPairableTurnPort(PortData* newly_pairable_port_data) {
  // A TURN port already ready on the network: prune the new one.
  const std::string network_name = newly_pairable_port_data->port()->network()->name();
  for (PortData& data : ports_) {
    if (data.port()->network()->name() == network_name && data.port()->Type() == IceCandidateType::kRelay &&
        data.ready() && &data != newly_pairable_port_data) {
      newly_pairable_port_data->Prune(now_);
      return true;
    }
  }
  return false;
}

bool BasicPortAllocatorSession::PruneTurnPorts(Port* newly_pairable_turn_port) {
  // Same network means the same name, so an IPv4 and an IPv6 network of one
  // interface count as one.
  const std::string network_name = newly_pairable_turn_port->network()->name();
  Port* best_turn_port = GetBestTurnPortForNetwork(network_name);
  if (best_turn_port == nullptr) return false;
  bool pruned = false;
  std::vector<Port*> ports_to_prune;
  for (PortData& data : ports_) {
    if (data.port()->network()->name() == network_name && data.port()->Type() == IceCandidateType::kRelay &&
        !data.pruned() && ComparePort(data.port(), best_turn_port) < 0) {
      pruned = true;
      if (data.port() != newly_pairable_turn_port) {
        ports_to_prune.push_back(data.port());
      } else {
        data.Prune(now_);
      }
    }
  }
  if (!ports_to_prune.empty()) {
    std::vector<PortData*> list;
    for (Port* p : ports_to_prune) {
      if (PortData* d = FindPort(p)) list.push_back(d);
    }
    PrunePortsAndRemoveCandidates(list);
  }
  return pruned;
}

void BasicPortAllocatorSession::PruneAllPorts(Timestamp now) {
  now_ = std::max(now_, now);
  for (PortData& data : ports_) data.Prune(now_);
}

void BasicPortAllocatorSession::OnPortComplete(Port* port) {
  PortData* data = FindPort(port);
  if (data == nullptr || !data->inprogress()) return;  // A late signal.
  data->set_state(PortData::STATE_COMPLETE);
  MaybeSignalCandidatesAllocationDone();
}

void BasicPortAllocatorSession::OnPortError(Port* port) {
  PortData* data = FindPort(port);
  if (data == nullptr || !data->inprogress()) return;  // Already given up on.
  data->set_state(PortData::STATE_ERROR);
  MaybeSignalCandidatesAllocationDone();
}

bool BasicPortAllocatorSession::CheckCandidateFilter(const Candidate& c) const {
  return IsAllowedByCandidateFilter(c, candidate_filter_);
}

bool BasicPortAllocatorSession::CandidatePairable(const Candidate& c, const Port* port) const {
  const bool candidate_signalable = CheckCandidateFilter(c);
  // With adapter enumeration off, a port pings from candidates it doesn't
  // signal (the any address), unless host candidates are filtered out too.
  const bool network_enumeration_disabled = c.address().IsAnyIp();
  const bool can_ping_from_candidate = port->SharedSocket() || c.protocol() == TCP_PROTOCOL_NAME;
  const bool host_candidates_disabled = !(candidate_filter_ & CF_HOST);
  return candidate_signalable ||
         (network_enumeration_disabled && can_ping_from_candidate && !host_candidates_disabled);
}

void BasicPortAllocatorSession::MaybeSignalCandidatesAllocationDone() {
  if (!CandidatesAllocationDone()) return;
  std::vector<IceCandidateErrorEvent> events;
  events.swap(candidate_error_events_);
  for (const auto& event : events) NotifyCandidateError(event);
  NotifyCandidatesAllocationDone();
}

void BasicPortAllocatorSession::OnPortDestroyed(Port* port) {
  for (auto it = ports_.begin(); it != ports_.end(); ++it) {
    if (it->port() == port) {
      ports_.erase(it);
      break;
    }
  }
  auto owned = owned_ports_.find(port);
  if (owned != owned_ports_.end()) {
    // Deleted after the call that destroyed it returns.
    destroyed_ports_.push_back(std::move(owned->second));
    owned_ports_.erase(owned);
  }
}

BasicPortAllocatorSession::PortData* BasicPortAllocatorSession::FindPort(Port* port) {
  for (PortData& data : ports_) {
    if (data.port() == port) return &data;
  }
  return nullptr;
}

std::vector<BasicPortAllocatorSession::PortData*> BasicPortAllocatorSession::GetUnprunedPorts(
    const std::vector<const Network*>& networks) {
  std::vector<PortData*> unpruned;
  for (PortData& port : ports_) {
    if (!port.pruned() &&
        std::find(networks.begin(), networks.end(), port.sequence()->network()) != networks.end()) {
      unpruned.push_back(&port);
    }
  }
  return unpruned;
}

void BasicPortAllocatorSession::PrunePortsAndRemoveCandidates(const std::vector<PortData*>& port_data_list) {
  std::vector<Port*> pruned_ports;
  std::vector<Candidate> removed_candidates;
  for (PortData* data : port_data_list) {
    // Pruned, so that it may be destroyed.
    data->Prune(now_);
    pruned_ports.push_back(data->port());
    if (data->has_pairable_candidate()) {
      GetCandidatesFromPort(*data, &removed_candidates);
      // So that its candidates are removed only once.
      data->set_has_pairable_candidate(false);
    }
  }
  if (!pruned_ports.empty()) NotifyPortsPruned(pruned_ports);
  if (!removed_candidates.empty()) NotifyCandidatesRemoved(removed_candidates);
}

// ------------------------------------------------------------ AllocationSequence

AllocationSequence::AllocationSequence(BasicPortAllocatorSession* session, const Network* network,
                                       PortConfiguration* config, uint32_t flags,
                                       std::function<void()> port_allocation_complete_callback)
    : session_(session),
      network_(network),
      config_(config),
      flags_(flags),
      port_allocation_complete_callback_(std::move(port_allocation_complete_callback)) {}

AllocationSequence::~AllocationSequence() = default;

void AllocationSequence::Init() {
  if (IsFlagSet(PORTALLOCATOR_ENABLE_SHARED_SOCKET)) {
    BasicPortAllocator& allocator = *session_->allocator();
    udp_socket_ = session_->socket_factory()->CreateUdpSocket(SocketAddress(network_->GetBestIP(), 0),
                                                              static_cast<uint16_t>(allocator.min_port()),
                                                              static_cast<uint16_t>(allocator.max_port()));
    if (udp_socket_) {
      udp_socket_->RegisterReceivedPacketCallback(
          [this](AsyncPacketSocket* socket, const ReceivedPacket& packet) { OnReadPacket(socket, packet); });
    }
    // Without the socket, TCP and relay over TCP may still work: go on.
  }
}

void AllocationSequence::Clear() {
  udp_port_ = nullptr;
  relay_ports_.clear();
}

void AllocationSequence::OnNetworkFailed() {
  network_failed_ = true;
  Stop();
}

void AllocationSequence::DisableEquivalentPhases(const Network* network, PortConfiguration* config,
                                                 uint32_t* flags) {
  // A network that once failed is not equivalent to a new one.
  if (network_failed_) return;
  if (!(network == network_ && previous_best_ip_ == network->GetBestIP())) return;
  // Every config implies host ports: skip UDP if this sequence is making a
  // host UDP port on the network or one exists without an error. (A TURN
  // port over UDP doesn't count.)
  const auto& ports = session_->ports_;
  if ((state_ == kRunning && phase_ == PHASE_UDP && !IsFlagSet(PORTALLOCATOR_DISABLE_UDP)) ||
      std::any_of(ports.begin(), ports.end(), [this](const BasicPortAllocatorSession::PortData& p) {
        return !p.pruned() && p.port()->network() == network_ && p.port()->GetProtocol() == PROTO_UDP &&
               p.port()->Type() == IceCandidateType::kHost && !p.error();
      })) {
    *flags |= PORTALLOCATOR_DISABLE_UDP;
  }
  // TCP is the last phase: a running sequence with TCP on covers it before
  // its port exists.
  if ((state_ == kRunning && !IsFlagSet(PORTALLOCATOR_DISABLE_TCP)) ||
      std::any_of(ports.begin(), ports.end(), [this](const BasicPortAllocatorSession::PortData& p) {
        return !p.pruned() && p.port()->network() == network_ && p.port()->GetProtocol() == PROTO_TCP &&
               p.port()->Type() == IceCandidateType::kHost && !p.error();
      })) {
    *flags |= PORTALLOCATOR_DISABLE_TCP;
  }
  if (config_ && config) {
    // Regather srflx only with other STUN servers, or with new host ports
    // (new NAT bindings).
    if (config_->StunServers() == config->StunServers() && (*flags & PORTALLOCATOR_DISABLE_UDP)) {
      *flags |= PORTALLOCATOR_DISABLE_STUN;
    }
    // Relays covered (even a different set: libwebrtc never gives one).
    if (!config_->relays.empty()) *flags |= PORTALLOCATOR_DISABLE_RELAY;
  }
}

void AllocationSequence::Start() {
  state_ = kRunning;
  const int epoch = epoch_;
  session_->Post(TimeDelta::Zero(), [this, epoch] { Process(epoch); });
  // A later DisableEquivalentPhases enables every phase if the best IP changed.
  previous_best_ip_ = network_->GetBestIP();
}

void AllocationSequence::Stop() {
  // A completed sequence stays completed.
  if (state_ == kRunning) {
    state_ = kStopped;
    ++epoch_;  // Ignore the pending Process.
  }
}

void AllocationSequence::Process(int epoch) {
  if (epoch != epoch_) return;
  switch (phase_) {
    case PHASE_UDP:
      CreateUDPPorts();
      CreateStunPorts();
      break;
    case PHASE_RELAY:
      CreateRelayPorts();
      break;
    case PHASE_TCP:
      CreateTCPPorts();
      state_ = kCompleted;
      break;
    default:
      break;
  }
  if (state_ == kRunning) {
    ++phase_;
    const int e = epoch_;
    session_->Post(TimeDelta::Millis(session_->allocator()->step_delay()), [this, e] { Process(e); });
  } else {
    // Done: ignore any Process still queued for this epoch.
    ++epoch_;
    port_allocation_complete_callback_();
  }
}

Port::Parameters AllocationSequence::PortArgs() const {
  Port::Parameters args;
  args.network = network_;
  args.socket_factory = session_->socket_factory();
  args.ice_username_fragment = session_->username();
  args.ice_password = session_->password();
  args.content_name = session_->content_name();
  args.ice_tiebreaker = session_->allocator()->ice_tiebreaker();
  return args;
}

void AllocationSequence::CreateUDPPorts() {
  if (IsFlagSet(PORTALLOCATOR_DISABLE_UDP)) return;
  const BasicPortAllocator& allocator = *session_->allocator();
  const bool emit_local_candidate_for_anyaddress = !IsFlagSet(PORTALLOCATOR_DISABLE_DEFAULT_LOCAL_CANDIDATE);
  std::unique_ptr<UDPPort> port;
  if (IsFlagSet(PORTALLOCATOR_ENABLE_SHARED_SOCKET) && udp_socket_) {
    port = UDPPort::Create(session_->now_, PortArgs(), udp_socket_.get(), emit_local_candidate_for_anyaddress,
                           allocator.stun_candidate_keepalive_interval());
  } else {
    port = UDPPort::Create(session_->now_, PortArgs(), static_cast<uint16_t>(allocator.min_port()),
                           static_cast<uint16_t>(allocator.max_port()), emit_local_candidate_for_anyaddress,
                           allocator.stun_candidate_keepalive_interval());
  }
  if (!port) return;
  // On a shared socket, the UDPPort gathers the STUN candidates too.
  if (IsFlagSet(PORTALLOCATOR_ENABLE_SHARED_SOCKET)) {
    udp_port_ = port.get();
    port->SubscribePortDestroyed(this, [this](Port* p) { OnPortDestroyed(p); });
    if (!IsFlagSet(PORTALLOCATOR_DISABLE_STUN) && config_ && !config_->StunServers().empty()) {
      port->set_server_addresses(config_->StunServers());
    }
  }
  session_->AddAllocatedPort(std::move(port), this);
}

void AllocationSequence::CreateTCPPorts() {
  if (IsFlagSet(PORTALLOCATOR_DISABLE_TCP)) return;
  // TCPPort comes with C4 (TCP candidates); libwebrtc makes one here.
}

void AllocationSequence::CreateStunPorts() {
  if (IsFlagSet(PORTALLOCATOR_DISABLE_STUN)) return;
  // On a shared socket the UDPPort makes them.
  if (IsFlagSet(PORTALLOCATOR_ENABLE_SHARED_SOCKET)) return;
  if (!(config_ && !config_->StunServers().empty())) return;
  const BasicPortAllocator& allocator = *session_->allocator();
  std::unique_ptr<StunPort> port = StunPort::Create(
      session_->now_, PortArgs(), static_cast<uint16_t>(allocator.min_port()),
      static_cast<uint16_t>(allocator.max_port()), config_->StunServers(), allocator.stun_candidate_keepalive_interval());
  if (port) session_->AddAllocatedPort(std::move(port), this);
}

void AllocationSequence::CreateRelayPorts() {
  if (IsFlagSet(PORTALLOCATOR_DISABLE_RELAY)) return;
  if (!(config_ && !config_->relays.empty())) return;
  // Relative priority of each TURN server's candidates (ICE priorities must
  // be unique).
  int relative_priority = static_cast<int>(config_->relays.size());
  for (const RelayServerConfig& relay : config_->relays) CreateTurnPort(relay, relative_priority--);
}

void AllocationSequence::CreateTurnPort(const RelayServerConfig& config, int relative_priority) {
  RelayPortFactoryInterface* factory = session_->allocator()->relay_port_factory();
  if (factory == nullptr) return;  // No TURN ports until C4.
  for (const ProtocolAddress& relay_port : config.ports) {
    if (IsFlagSet(PORTALLOCATOR_DISABLE_UDP_RELAY) && relay_port.proto == PROTO_UDP) continue;
    // A server of a known, other address family can't be reached.
    const IpFamily server_ip_family = relay_port.address.ipaddr().family();
    const IpFamily local_ip_family = network_->GetBestIP().family();
    if (server_ip_family != IpFamily::kUnspec && server_ip_family != local_ip_family) continue;
    CreateRelayPortArgs args;
    args.port = PortArgs();
    args.server_address = &relay_port;
    args.config = &config;
    args.username = session_->username();
    args.password = session_->password();
    args.relative_priority = relative_priority;
    std::unique_ptr<Port> port;
    // Shared socket only for UDP TURN.
    if (IsFlagSet(PORTALLOCATOR_ENABLE_SHARED_SOCKET) && relay_port.proto == PROTO_UDP && udp_socket_) {
      port = factory->Create(session_->now_, args, udp_socket_.get());
      if (!port) continue;
      relay_ports_.push_back(port.get());
      port->SubscribePortDestroyed(this, [this](Port* p) { OnPortDestroyed(p); });
    } else {
      port = factory->Create(session_->now_, args, session_->allocator()->min_port(),
                             session_->allocator()->max_port());
      if (!port) continue;
    }
    session_->AddAllocatedPort(std::move(port), this);
  }
}

void AllocationSequence::OnReadPacket(AsyncPacketSocket* socket, const ReceivedPacket& packet) {
  // A TURN port for the source first (a STUN response from a TURN server
  // used as STUN server goes to it too; it ignores unknown transactions).
  bool turn_port_found = false;
  const std::vector<Port*> relay_ports = relay_ports_;
  for (Port* port : relay_ports) {
    if (port->CanHandleIncomingPacketsFrom(packet.source_address)) {
      if (port->HandleIncomingPacket(socket, packet)) return;
      turn_port_found = true;
    }
  }
  if (udp_port_) {
    const ServerAddresses& stun_servers = udp_port_->server_addresses();
    // The UDPPort takes it if no TURN port matched, or the TURN server is
    // also a STUN server.
    if (!turn_port_found || stun_servers.find(packet.source_address) != stun_servers.end()) {
      udp_port_->HandleIncomingPacket(socket, packet);
    }
  }
}

void AllocationSequence::OnPortDestroyed(Port* port) {
  if (udp_port_ == port) {
    udp_port_ = nullptr;
    return;
  }
  auto it = std::find(relay_ports_.begin(), relay_ports_.end(), port);
  if (it != relay_ports_.end()) relay_ports_.erase(it);
}

// ------------------------------------------------------------ PortConfiguration

PortConfiguration::PortConfiguration(const ServerAddresses& servers, std::string_view user, std::string_view pass)
    : stun_servers(servers), username(user), password(pass) {
  if (!stun_servers.empty()) stun_address = *stun_servers.begin();
}

ServerAddresses PortConfiguration::StunServers() {
  if (!stun_address.IsNil() && stun_servers.find(stun_address) == stun_servers.end()) {
    stun_servers.insert(stun_address);
  }
  if (!stun_servers.empty() && use_turn_server_as_stun_server_disabled) return stun_servers;
  // Every UDP TURN server is also a STUN server (and in libwebrtc every DTLS
  // one: no PROTO_DTLS until C4).
  InsertStunServersForProtocol(PROTO_UDP);
  return stun_servers;
}

void PortConfiguration::InsertStunServersForProtocol(ProtocolType type) {
  for (const SocketAddress& turn_server : GetRelayServerAddresses(type)) stun_servers.insert(turn_server);
}

bool PortConfiguration::SupportsProtocol(const RelayServerConfig& relay, ProtocolType type) const {
  return std::any_of(relay.ports.begin(), relay.ports.end(), [type](const ProtocolAddress& p) {
    return p.proto == type;
  });
}

bool PortConfiguration::SupportsProtocol(ProtocolType type) const {
  return std::any_of(relays.begin(), relays.end(), [&](const RelayServerConfig& r) { return SupportsProtocol(r, type); });
}

ServerAddresses PortConfiguration::GetRelayServerAddresses(ProtocolType type) const {
  ServerAddresses servers;
  for (const RelayServerConfig& relay : relays) {
    if (SupportsProtocol(relay, type)) servers.insert(relay.ports.front().address);
  }
  return servers;
}

}  // namespace sm::p2p
