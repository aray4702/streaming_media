// libwebrtc M157's p2p/base/p2p_transport_channel.cc (compare/webrtc/reference/p2p/base/), with the library's time
// model (see sm/p2p/p2p_transport_channel.h). Function order and comments follow the reference.
#include "sm/p2p/p2p_transport_channel.h"

#include <algorithm>
#include <cerrno>
#include <set>
#include <utility>

#include "sm/p2p/network.h"
#include "sm/p2p/p2p_constants.h"
#include "sm/p2p/stun.h"
#include "sm/p2p/wrapping_active_ice_controller.h"

namespace sm::p2p {
namespace {

Port::CandidateOrigin GetOrigin(Port* port, Port* origin_port) {
  if (!origin_port)
    return Port::ORIGIN_MESSAGE;
  else if (port == origin_port)
    return Port::ORIGIN_THIS_PORT;
  else
    return Port::ORIGIN_OTHER_PORT;
}

// Network::GuessAdapterFromNetworkCost (rtc_base/network.cc), the adapter type only (no network slices in the
// library; the slice costs map to their adapter as there).
AdapterType GuessAdapterFromNetworkCost(int network_cost) {
  constexpr int kSlice = -2;  // kNetworkCostSlice
  switch (network_cost) {
    case kNetworkCostMin:
    case kNetworkCostMin + kNetworkCostVpn:
      return ADAPTER_TYPE_ETHERNET;
    case kNetworkCostLow:
    case kNetworkCostLow + kNetworkCostVpn:
      return ADAPTER_TYPE_WIFI;
    case kNetworkCostCellular:
    case kNetworkCostCellular + kNetworkCostVpn:
    case kNetworkCostCellular + kSlice:
    case kNetworkCostCellular + kNetworkCostVpn + kSlice:
      return ADAPTER_TYPE_CELLULAR;
    case kNetworkCostCellular2G:
    case kNetworkCostCellular2G + kNetworkCostVpn:
      return ADAPTER_TYPE_CELLULAR_2G;
    case kNetworkCostCellular3G:
    case kNetworkCostCellular3G + kNetworkCostVpn:
      return ADAPTER_TYPE_CELLULAR_3G;
    case kNetworkCostCellular4G:
    case kNetworkCostCellular4G + kNetworkCostVpn:
      return ADAPTER_TYPE_CELLULAR_4G;
    case kNetworkCostCellular5G:
    case kNetworkCostCellular5G + kNetworkCostVpn:
    case kNetworkCostCellular5G + kSlice:
    case kNetworkCostCellular5G + kNetworkCostVpn + kSlice:
      return ADAPTER_TYPE_CELLULAR_5G;
    case kNetworkCostUnknown:
    case kNetworkCostUnknown + kNetworkCostVpn:
      return ADAPTER_TYPE_UNKNOWN;
    case kNetworkCostMax:
    case kNetworkCostMax + kNetworkCostVpn:
      return ADAPTER_TYPE_ANY;
  }
  return ADAPTER_TYPE_UNKNOWN;
}

RouteEndpoint CreateRouteEndpointFromCandidate(bool local, const Candidate& candidate, bool uses_turn) {
  auto adapter_type = candidate.network_type();
  if (!local && adapter_type == ADAPTER_TYPE_UNKNOWN) {
    adapter_type = GuessAdapterFromNetworkCost(candidate.network_cost());
  }
  // TODO(bugs.webrtc.org/9446) : Rewrite if information about remote network adapter becomes available. The
  // implication of this implementation is that we will only ever report 1 adapter per type. In practice this is
  // probably fine, since the endpoint also contains network-id.
  uint16_t adapter_id = static_cast<uint16_t>(adapter_type);
  return RouteEndpoint(adapter_type, adapter_id, candidate.network_id(), uses_turn);
}

// GetProtocolOverhead (rtc_base/net_helper.cc, not in reference/): the transport header, 8 bytes for UDP and 20 for
// TCP.
int GetProtocolOverhead(std::string_view protocol) { return protocol == "tcp" || protocol == "ssltcp" ? 20 : 8; }

// The P2PTransportChannel's IceConfig (its constructor's arguments).
IceConfig DefaultChannelConfig() {
  IceConfig config;
  config.receiving_timeout = kReceivingTimeout;
  config.backup_connection_ping_interval = kBackupConnectionPingInterval;
  config.continual_gathering_policy = ContinualGatheringPolicy::kGatherOnce;
  config.prioritize_most_likely_candidate_pairs = false;
  config.stable_writable_connection_ping_interval = kStrongAndStableWritableConnectionPingInterval;
  config.presume_writable_when_fully_relayed = true;
  config.regather_on_failed_networks_interval = kRegatherOnFailedNetworksInterval;
  config.receiving_switching_delay = kReceivingSwitchingDelay;
  return config;
}

}  // namespace

bool IceCredentialsChanged(std::string_view old_ufrag, std::string_view old_pwd, std::string_view new_ufrag,
                           std::string_view new_pwd) {
  // The standard (RFC 5245 Section 9.1.1.1) says that ICE restarts MUST change both the ufrag and password.
  // However, section 9.2.1.1 says changing the ufrag OR password indicates an ICE restart. So, to keep
  // compatibility with endpoints that only change one, we'll treat this as an ICE restart.
  return (old_ufrag != new_ufrag) || (old_pwd != new_pwd);
}

P2PTransportChannel::P2PTransportChannel(Timestamp now, std::string_view transport_name, int component,
                                         PortAllocator* allocator)
    : P2PTransportChannel(now, transport_name, component, allocator, Options{}) {}

P2PTransportChannel::P2PTransportChannel(Timestamp now, std::string_view transport_name, int component,
                                         PortAllocator* allocator, const Options& options)
    : transport_name_(transport_name),
      component_(component),
      allocator_(allocator),
      config_(DefaultChannelConfig()),
      ice_field_trials_(options.field_trials),
      now_(now) {
  // We populate the change in the candidate filter to the session taken by the transport.
  allocator_->SubscribeCandidateFilterChanged(
      this, [this](uint32_t prev_filter, uint32_t cur_filter) { OnCandidateFilterChanged(prev_filter, cur_filter); });

  // ParseFieldTrials: dead_connection_timeout_ms is at least 30 s.
  if (ice_field_trials_.dead_connection_timeout_ms < 30000) ice_field_trials_.dead_connection_timeout_ms = 30000;
  if (ice_field_trials_.override_dscp) SetOption(SocketOption::kDscp, *ice_field_trials_.override_dscp);

  IceControllerFactoryArgs args;
  args.ice_transport_state_func = [this] { return GetState(); };
  args.ice_role_func = [this] { return GetIceRole(); };
  args.is_connection_pruned_func = [this](const Connection* connection) {
    return IsPortPruned(connection->port()) || IsRemoteCandidatePruned(connection->remote_candidate());
  };
  args.ice_field_trials = &ice_field_trials_;

  if (options.active_ice_controller_factory) {
    ActiveIceControllerFactoryArgs active_args{args, this};
    ice_controller_ = options.active_ice_controller_factory->Create(active_args);
  } else {
    ice_controller_ = std::make_unique<WrappingActiveIceController>(this, options.ice_controller_factory, args);
  }
}

P2PTransportChannel::~P2PTransportChannel() {
  next_regather_ = Timestamp::PlusInfinity();
  std::vector<Connection*> copy(connections_.begin(), connections_.end());
  for (Connection* connection : copy) {
    connection->UnsubscribeDestroyed(this);
    RemoveConnection(connection);
    connection->Destroy(now_);
  }
  allocator_->UnsubscribeCandidateFilterChanged(this);
  // The ports of the sessions go with them; their signals hold `this`, so drop the sessions first.
  allocator_sessions_.clear();
}

Timestamp P2PTransportChannel::EventTime(const Connection* connection) {
  if (connection) now_ = std::max(now_, connection->LastReceived());
  return now_;
}

// Add the allocator session to our list so that we know which sessions are still active.
void P2PTransportChannel::AddAllocatorSession(Timestamp now, std::unique_ptr<PortAllocatorSession> session) {
  session->set_generation(static_cast<uint32_t>(allocator_sessions_.size()));
  session->SubscribePortReady(this, [this](PortAllocatorSession* s, Port* port) { OnPortReady(s, port); });
  session->SubscribePortsPruned(
      this, [this](PortAllocatorSession* s, const std::vector<Port*>& ports) { OnPortsPruned(s, ports); });
  session->SubscribeCandidatesReady(this, [this](PortAllocatorSession* s, const std::vector<Candidate>& candidates) {
    OnCandidatesReady(s, candidates);
  });
  session->SubscribeCandidateError(
      this, [this](PortAllocatorSession* s, const IceCandidateErrorEvent& event) { OnCandidateError(s, event); });
  session->SubscribeCandidatesRemoved(this,
                                      [this](PortAllocatorSession* s, const std::vector<Candidate>& candidates) {
                                        OnCandidatesRemoved(s, candidates);
                                      });
  session->SubscribeCandidatesAllocationDone(this, [this](PortAllocatorSession* s) { OnCandidatesAllocationDone(s); });
  if (!allocator_sessions_.empty()) {
    allocator_session()->PruneAllPorts(now);
  }
  allocator_sessions_.push_back(std::move(session));

  // We now only want to apply new candidates that we receive to the ports created by this new session because these
  // are replacing those of the previous sessions.
  PruneAllPorts();
}

void P2PTransportChannel::AddConnection(Connection* connection) {
  connection->SetReceivingTimeout(config_.receiving_timeout);
  connection->SetUnwritableTimeout(config_.ice_unwritable_timeout);
  connection->set_unwritable_min_checks(config_.ice_unwritable_min_checks);
  connection->SetInactiveTimeout(config_.ice_inactive_timeout);
  connection->RegisterReceivedPacketCallback(
      [this](Connection* c, const ReceivedPacket& packet) { OnReadPacket(c, packet); });
  connection->SubscribeReadyToSend(this, [this](Connection* c) { OnReadyToSend(c); });
  connection->SubscribeStateChange(this, [this](Connection* c) { OnConnectionStateChange(c); });
  connection->SubscribeDestroyed(this, [this](Connection* c) { OnConnectionDestroyed(c); });
  connection->SubscribeNominated(this, [this](Connection* c) { OnNominated(c); });

  had_connection_ = true;

  connection->SetIceFieldTrials(&ice_field_trials_);

  connections_.push_back(connection);
  ice_controller_->OnConnectionAdded(connection);
}

void P2PTransportChannel::ForgetLearnedStateForConnections(std::span<const Connection* const> connections) {
  for (const Connection* con : connections) {
    FromIceController(con)->ForgetLearnedState();
  }
}

void P2PTransportChannel::SetIceRole(IceRole ice_role) {
  if (ice_role_ != ice_role) {
    ice_role_ = ice_role;
    for (Port* port : ports_) {
      port->SetIceRole(ice_role);
    }
    // Update role on pruned ports as well, because they may still have connections alive that should be using the
    // correct role.
    for (Port* port : pruned_ports_) {
      port->SetIceRole(ice_role);
    }
  }
}

std::optional<int> P2PTransportChannel::GetRttEstimate() {
  if (selected_connection_ != nullptr && selected_connection_->rtt_samples() > 0) {
    return static_cast<int>(selected_connection_->Rtt().ms());
  }
  return std::nullopt;
}

std::optional<const CandidatePair> P2PTransportChannel::GetSelectedCandidatePair() const {
  if (selected_connection_ == nullptr) {
    return std::nullopt;
  }
  CandidatePair pair;
  pair.local = SanitizeLocalCandidate(selected_connection_->local_candidate());
  pair.remote = SanitizeRemoteCandidate(selected_connection_->remote_candidate());
  return pair;
}

// A channel is considered ICE completed once there is at most one active connection per network and at least one
// active connection.
IceTransportStateInternal P2PTransportChannel::ComputeState() const {
  if (!had_connection_) {
    return IceTransportStateInternal::kInit;
  }

  std::vector<Connection*> active_connections;
  for (Connection* connection : connections_) {
    if (connection->active()) {
      active_connections.push_back(connection);
    }
  }
  if (active_connections.empty()) {
    return IceTransportStateInternal::kFailed;
  }

  std::set<const Network*> networks;
  for (Connection* connection : active_connections) {
    const Network* network = connection->network();
    if (networks.find(network) == networks.end()) {
      networks.insert(network);
    } else {
      return IceTransportStateInternal::kConnecting;
    }
  }
  return IceTransportStateInternal::kCompleted;
}

// Compute the current RTCIceTransportState as described in https://www.w3.org/TR/webrtc/#dom-rtcicetransportstate
// TODO(bugs.webrtc.org/9218): Start signaling kCompleted once we have implemented end-of-candidates signalling.
IceTransportState P2PTransportChannel::ComputeIceTransportState() const {
  bool has_connection = false;
  for (Connection* connection : connections_) {
    if (connection->active()) {
      has_connection = true;
      break;
    }
  }

  if (had_connection_ && !has_connection) {
    return IceTransportState::kFailed;
  }

  if (!writable() && has_been_writable_) {
    return IceTransportState::kDisconnected;
  }

  if (!had_connection_ && !has_connection) {
    return IceTransportState::kNew;
  }

  if (has_connection && !writable()) {
    // A candidate pair has been formed by adding a remote candidate and gathering a local candidate.
    return IceTransportState::kChecking;
  }

  return IceTransportState::kConnected;
}

void P2PTransportChannel::SetIceParameters(const IceParameters& ice_params) {
  ice_parameters_ = ice_params;
  // Note: Candidate gathering will restart when MaybeStartGathering is next called.
}

void P2PTransportChannel::SetRemoteIceParameters(const IceParameters& ice_params) {
  const IceParameters* current_ice = remote_ice_parameters();
  if (!current_ice || *current_ice != ice_params) {
    // Keep the ICE credentials so that newer connections are prioritized over the older ones.
    remote_ice_parameters_.push_back(ice_params);
  }

  // Update the pwd of remote candidate if needed.
  for (RemoteCandidate& candidate : remote_candidates_) {
    if (candidate.username() == ice_params.ufrag && candidate.password().empty()) {
      candidate.set_password(ice_params.pwd);
    }
  }
  // We need to update the credentials and generation for any peer reflexive candidates.
  for (Connection* conn : connections_) {
    conn->MaybeSetRemoteIceParametersAndGeneration(ice_params, static_cast<int>(remote_ice_parameters_.size() - 1));
  }
  // Updating the remote ICE candidate generation could change the sort order.
  ice_controller_->OnSortAndSwitchRequest(now_, IceSwitchReason::REMOTE_CANDIDATE_GENERATION_CHANGE);
}

// TODO(qingsi): We apply the convention that setting a std::optional parameter to null restores its default value
// in the implementation. However, some std::optional parameters are only processed below if non-null, e.g.,
// regather_on_failed_networks_interval, and thus there is no way to restore the defaults. Fix this issue later for
// consistency.
void P2PTransportChannel::SetIceConfig(const IceConfig& config) {
  if (config_.continual_gathering_policy != config.continual_gathering_policy) {
    if (allocator_sessions_.empty()) {
      config_.continual_gathering_policy = config.continual_gathering_policy;
    }
    // Otherwise: "Trying to change continual gathering policy when gathering has already started!"
  }

  if (config_.backup_connection_ping_interval != config.backup_connection_ping_interval) {
    config_.backup_connection_ping_interval = config.backup_connection_ping_interval;
  }
  if (config_.receiving_timeout != config.receiving_timeout) {
    config_.receiving_timeout = config.receiving_timeout;
    for (Connection* connection : connections_) {
      connection->SetReceivingTimeout(config_.receiving_timeout);
    }
  }

  config_.prioritize_most_likely_candidate_pairs = config.prioritize_most_likely_candidate_pairs;

  if (config_.stable_writable_connection_ping_interval != config.stable_writable_connection_ping_interval) {
    config_.stable_writable_connection_ping_interval = config.stable_writable_connection_ping_interval;
  }

  if (config_.presume_writable_when_fully_relayed != config.presume_writable_when_fully_relayed) {
    if (connections_.empty()) {
      config_.presume_writable_when_fully_relayed = config.presume_writable_when_fully_relayed;
    }
    // Otherwise: "Trying to change 'presume writable' while connections already exist!"
  }

  config_.surface_ice_candidates_on_ice_transport_type_changed =
      config.surface_ice_candidates_on_ice_transport_type_changed;

  if (config_.regather_on_failed_networks_interval != config.regather_on_failed_networks_interval) {
    config_.regather_on_failed_networks_interval = config.regather_on_failed_networks_interval;
    if (next_regather_.IsFinite()) {
      OnStartedPinging();  // Restart the regathering timer.
    }
  }

  if (config_.receiving_switching_delay != config.receiving_switching_delay) {
    config_.receiving_switching_delay = config.receiving_switching_delay;
  }

  if (config_.default_nomination_mode != config.default_nomination_mode) {
    config_.default_nomination_mode = config.default_nomination_mode;
  }

  if (config_.ice_check_interval_strong_connectivity != config.ice_check_interval_strong_connectivity) {
    config_.ice_check_interval_strong_connectivity = config.ice_check_interval_strong_connectivity;
  }

  if (config_.ice_check_interval_weak_connectivity != config.ice_check_interval_weak_connectivity) {
    config_.ice_check_interval_weak_connectivity = config.ice_check_interval_weak_connectivity;
  }

  if (config_.ice_check_min_interval != config.ice_check_min_interval) {
    config_.ice_check_min_interval = config.ice_check_min_interval;
  }

  if (config_.ice_unwritable_timeout != config.ice_unwritable_timeout) {
    config_.ice_unwritable_timeout = config.ice_unwritable_timeout;
    for (Connection* conn : connections_) {
      conn->SetUnwritableTimeout(config_.ice_unwritable_timeout);
    }
  }

  if (config_.ice_unwritable_min_checks != config.ice_unwritable_min_checks) {
    config_.ice_unwritable_min_checks = config.ice_unwritable_min_checks;
    for (Connection* conn : connections_) {
      conn->set_unwritable_min_checks(config_.ice_unwritable_min_checks);
    }
  }

  if (config_.ice_inactive_timeout != config.ice_inactive_timeout) {
    config_.ice_inactive_timeout = config.ice_inactive_timeout;
    for (Connection* conn : connections_) {
      conn->SetInactiveTimeout(config_.ice_inactive_timeout);
    }
  }

  if (config_.network_preference != config.network_preference) {
    config_.network_preference = config.network_preference;
    ice_controller_->OnSortAndSwitchRequest(now_, IceSwitchReason::NETWORK_PREFERENCE_CHANGE);
  }

  // TODO(qingsi): Resolve the naming conflict of stun_keepalive_delay in UDPPort and stun_keepalive_interval.
  if (config_.stun_keepalive_interval != config.stun_keepalive_interval) {
    config_.stun_keepalive_interval = config.stun_keepalive_interval;
    // libwebrtc dereferences allocator_session() here; a config set before gathering has no session yet.
    if (allocator_session()) allocator_session()->SetStunKeepaliveIntervalForReadyPorts(config_.stun_keepalive_interval);
  }

  config_.vpn_preference = config.vpn_preference;
  allocator_->SetVpnPreference(config_.vpn_preference);

  ice_controller_->SetIceConfig(config_);
  if (config_.dtls_handshake_in_stun != config.dtls_handshake_in_stun) {
    config_.dtls_handshake_in_stun = config.dtls_handshake_in_stun;
  }
}

TimeDelta P2PTransportChannel::check_receiving_interval() const {
  return std::max(kMinCheckReceivingInterval, config_.receiving_timeout_or_default() / 10);
}

void P2PTransportChannel::MaybeStartGathering(Timestamp now) {
  now_ = std::max(now_, now);
  if (ice_parameters_.ufrag.empty() || ice_parameters_.pwd.empty()) {
    // "Cannot gather candidates because ICE parameters are empty"
    return;
  }
  // Start gathering if we never started before, or if an ICE restart occurred.
  if (allocator_sessions_.empty() ||
      IceCredentialsChanged(allocator_sessions_.back()->ice_ufrag(), allocator_sessions_.back()->ice_pwd(),
                            ice_parameters_.ufrag, ice_parameters_.pwd)) {
    if (gathering_state_ != kIceGatheringGathering) {
      gathering_state_ = kIceGatheringGathering;
      SendGatheringStateEvent();
    }

    for (const auto& session : allocator_sessions_) {
      if (session->IsStopped()) {
        continue;
      }
      session->StopGettingPorts(now_);
    }

    // Time for a new allocator.
    std::unique_ptr<PortAllocatorSession> pooled_session =
        allocator_->TakePooledSession(transport_name(), component(), ice_parameters_.ufrag, ice_parameters_.pwd);
    if (pooled_session) {
      PortAllocatorSession* raw_session = pooled_session.get();
      AddAllocatorSession(now_, std::move(pooled_session));
      // Process the pooled session's existing candidates/ports, if they exist.
      OnCandidatesReady(raw_session, raw_session->ReadyCandidates());
      for (Port* port : raw_session->ReadyPorts()) {
        OnPortReady(raw_session, port);
      }
      if (raw_session->CandidatesAllocationDone()) {
        OnCandidatesAllocationDone(raw_session);
      }
    } else {
      AddAllocatorSession(now_, allocator_->CreateSession(transport_name(), component(), ice_parameters_.ufrag,
                                                          ice_parameters_.pwd));
      allocator_sessions_.back()->StartGettingPorts(now_);
    }
  }
}

// A new port is available, attempt to make connections for it
void P2PTransportChannel::OnPortReady(PortAllocatorSession* /*session*/, Port* port) {
  // Set in-effect options on the new port
  for (const auto& [opt, value] : options_) {
    port->SetOption(opt, value);  // Errors are frequent (bugs.webrtc.org/9221); libwebrtc only logs them.
  }

  // Remember the ports and candidates, and signal that candidates are ready. The session will handle this, and send
  // an initiate/accept/modify message if one is pending.
  port->SetIceRole(ice_role_);
  ports_.push_back(port);
  port->SubscribeUnknownAddress(this, [this](Timestamp now, Port* p, const SocketAddress& address, ProtocolType proto,
                                             IceMessage* stun_msg, const std::string& remote_username,
                                             bool port_muxed) {
    OnUnknownAddress(now, p, address, proto, stun_msg, remote_username, port_muxed);
  });
  port->SubscribeSentPacket(this, [this](const SentPacketInfo& sent_packet) { OnSentPacket(sent_packet); });
  port->SubscribePortDestroyed(this, [this](Port* p) { OnPortDestroyed(p); });
  port->SubscribeRoleConflict([this] { NotifyRoleConflict(this); });

  // Attempt to create a connection from this new port to all of the remote candidates that we were given so far.
  // (Indexed: CreateConnection never changes remote_candidates_.)
  for (size_t i = 0; i < remote_candidates_.size(); ++i) {
    const RemoteCandidate candidate = remote_candidates_[i];
    CreateConnection(now_, port, candidate, candidate.origin_port());
  }

  ice_controller_->OnImmediateSortAndSwitchRequest(now_, IceSwitchReason::NEW_CONNECTION_FROM_LOCAL_CANDIDATE);
}

// A new candidate is available, let listeners know
void P2PTransportChannel::OnCandidatesReady(PortAllocatorSession* /*session*/,
                                            const std::vector<Candidate>& candidates) {
  for (const Candidate& candidate : candidates) {
    NotifyCandidateGathered(this, candidate);
  }
}

void P2PTransportChannel::OnCandidateError(PortAllocatorSession* /*session*/, const IceCandidateErrorEvent& event) {
  if (candidate_error_callback_) {
    candidate_error_callback_(this, event);
  }
}

void P2PTransportChannel::OnCandidatesAllocationDone(PortAllocatorSession* /*session*/) {
  if (config_.gather_continually()) {
    // Gathering complete, but using continual gathering so not changing gathering state.
    return;
  }
  gathering_state_ = kIceGatheringComplete;
  SendGatheringStateEvent();
}

// Handle stun packets
void P2PTransportChannel::OnUnknownAddress(Timestamp now, Port* port, const SocketAddress& address,
                                           ProtocolType proto, IceMessage* stun_msg,
                                           const std::string& remote_username, bool port_muxed) {
  now_ = std::max(now_, now);
  // Port has received a valid stun packet from an address that no Connection is currently available for. See if we
  // already have a candidate with the address. If it isn't we need to create new candidate for it.
  const Candidate* candidate = nullptr;
  for (const Candidate& c : remote_candidates_) {
    if (c.username() == remote_username && c.address() == address && c.protocol() == ProtoToString(proto)) {
      candidate = &c;
      break;
    }
  }

  uint32_t remote_generation = 0;
  std::string remote_password;
  // The STUN binding request may arrive after setRemoteDescription and before adding remote candidate, so we need to
  // set the password to the shared password and set the generation if the user name matches.
  const IceParameters* ice_param = FindRemoteIceFromUfrag(remote_username, &remote_generation);
  // Note: if not found, the remote_generation will still be 0.
  if (ice_param != nullptr) {
    remote_password = ice_param->pwd;
  }

  Candidate remote_candidate;
  bool remote_candidate_is_new = (candidate == nullptr);
  if (!remote_candidate_is_new) {
    remote_candidate = *candidate;
  } else {
    // Create a new candidate with this address. The priority of the candidate is set to the PRIORITY attribute from
    // the request.
    const StunUInt32Attribute* priority_attr = stun_msg->GetUInt32(STUN_ATTR_PRIORITY);
    if (!priority_attr) {
      // "No STUN_ATTR_PRIORITY found in the stun request message"
      port->SendBindingErrorResponse(now, stun_msg, address, STUN_ERROR_BAD_REQUEST, STUN_ERROR_REASON_BAD_REQUEST);
      return;
    }
    uint32_t remote_candidate_priority = priority_attr->value();

    uint16_t network_id = 0;
    uint16_t network_cost = 0;
    const StunUInt32Attribute* network_attr = stun_msg->GetUInt32(STUN_ATTR_GOOG_NETWORK_INFO);
    if (network_attr) {
      uint32_t network_info = network_attr->value();
      network_id = static_cast<uint16_t>(network_info >> 16);
      network_cost = static_cast<uint16_t>(network_info);
    }

    // RFC 5245: If the source transport address of the request does not match any existing remote candidates, it
    // represents a new peer reflexive remote candidate.
    remote_candidate = Candidate(component(), ProtoToString(proto), address, remote_candidate_priority,
                                 remote_username, remote_password, IceCandidateType::kPrflx, remote_generation, "",
                                 network_id, network_cost);
    if (proto == PROTO_TCP) {
      remote_candidate.set_tcptype(TCPTYPE_ACTIVE_STR);
    }

    // From RFC 5245, section-7.2.1.3: The foundation of the candidate is set to an arbitrary value, different from
    // the foundation for all other remote candidates.
    remote_candidate.ComputePrflxFoundation();
  }

  // RFC5245, the agent constructs a pair whose local candidate is equal to the transport address on which the STUN
  // request was received, and a remote candidate equal to the source transport address where the request came from.

  // There shouldn't be an existing connection with this remote address. When ports are muxed, this channel might get
  // multiple unknown address signals. In that case if the connection is already exists, we should simply ignore the
  // signal otherwise send server error.
  if (port->GetConnection(remote_candidate.address())) {
    if (!port_muxed) {
      port->SendBindingErrorResponse(now, stun_msg, address, STUN_ERROR_SERVER_ERROR,
                                     STUN_ERROR_REASON_SERVER_ERROR);
    }
    return;
  }

  Connection* connection = port->CreateConnection(now, remote_candidate, Port::ORIGIN_THIS_PORT);
  if (!connection) {
    // This could happen in some scenarios. For example, a TurnPort may have had a refresh request timeout, so it
    // won't create connections.
    port->SendBindingErrorResponse(now, stun_msg, address, STUN_ERROR_SERVER_ERROR, STUN_ERROR_REASON_SERVER_ERROR);
    return;
  }

  AddConnection(connection);
  connection->HandleStunBindingOrGoogPingRequest(now, stun_msg);

  // Update the list of connections since we just added another. We do this after sending the response since it could
  // (in principle) delete the connection in question.
  ice_controller_->OnImmediateSortAndSwitchRequest(now_, IceSwitchReason::NEW_CONNECTION_FROM_UNKNOWN_REMOTE_ADDRESS);
}

void P2PTransportChannel::OnCandidateFilterChanged(uint32_t prev_filter, uint32_t cur_filter) {
  if (prev_filter == cur_filter || allocator_session() == nullptr) {
    return;
  }
  if (config_.surface_ice_candidates_on_ice_transport_type_changed) {
    allocator_session()->SetCandidateFilter(cur_filter);
  }
}

const IceParameters* P2PTransportChannel::FindRemoteIceFromUfrag(std::string_view ufrag,
                                                                 uint32_t* generation) const {
  const auto& params = remote_ice_parameters_;
  auto it = std::find_if(params.rbegin(), params.rend(),
                         [ufrag](const IceParameters& param) { return param.ufrag == ufrag; });
  if (it == params.rend()) {
    // Not found.
    return nullptr;
  }
  *generation = static_cast<uint32_t>(params.rend() - it - 1);
  return &(*it);
}

void P2PTransportChannel::OnNominated(Connection* conn) {
  const Timestamp now = EventTime(conn);
  if (selected_connection_ == conn) {
    return;
  }

  if (ice_field_trials_.send_ping_on_nomination_ice_controlled && conn != nullptr) {
    SendPingRequestInternal(now, conn);
  }

  // TODO(qingsi): RequestSortAndStateUpdate will eventually call MaybeSwitchSelectedConnection again. Rewrite this
  // logic.
  if (ice_controller_->OnImmediateSwitchRequest(now, IceSwitchReason::NOMINATION_ON_CONTROLLED_SIDE, conn)) {
    // Now that we have selected a connection, it is time to prune other connections and update the read/write state
    // of the channel.
    ice_controller_->OnSortAndSwitchRequest(now, IceSwitchReason::NOMINATION_ON_CONTROLLED_SIDE);
  }
  // Otherwise: "Not switching the selected connection on controlled side yet".
}

void P2PTransportChannel::AddRemoteCandidate(Timestamp now, const Candidate& candidate) {
  now_ = std::max(now_, now);
  uint32_t generation = GetRemoteCandidateGeneration(candidate);
  // If a remote candidate with a previous generation arrives, drop it.
  if (generation < remote_ice_generation()) {
    return;
  }

  Candidate new_remote_candidate(candidate);
  new_remote_candidate.set_generation(generation);
  // ICE candidates don't need to have username and password set, but the code below this (specifically,
  // ConnectionRequest::Prepare in port.cc) uses the remote candidates's username. So, we set it here.
  if (remote_ice_parameters()) {
    if (candidate.username().empty()) {
      new_remote_candidate.set_username(remote_ice_parameters()->ufrag);
    }
    if (new_remote_candidate.username() == remote_ice_parameters()->ufrag) {
      if (candidate.password().empty()) {
        new_remote_candidate.set_password(remote_ice_parameters()->pwd);
      }
    }
    // Otherwise the candidate belongs to the next generation. Its pwd will be set when the new remote ICE credentials
    // arrive.
  }

  // libwebrtc resolves a hostname candidate here (IsUnresolvedIP); the library's addresses carry no hostname. With no
  // local network access permission factory, CheckLocalNetworkAccessPermission finishes adding at once.
  FinishAddingRemoteCandidate(now_, new_remote_candidate);
}

void P2PTransportChannel::FinishAddingRemoteCandidate(Timestamp now, const Candidate& new_remote_candidate) {
  // If this candidate matches what was thought to be a peer reflexive candidate, we need to update the candidate
  // priority/etc.
  for (Connection* conn : connections_) {
    conn->MaybeUpdatePeerReflexiveCandidate(new_remote_candidate);
  }

  // Create connections to this remote candidate.
  CreateConnections(now, new_remote_candidate, nullptr);

  // Resort the connections list, which may have new elements.
  ice_controller_->OnImmediateSortAndSwitchRequest(now, IceSwitchReason::NEW_CONNECTION_FROM_REMOTE_CANDIDATE);
}

void P2PTransportChannel::RemoveRemoteCandidate(const Candidate& cand_to_remove) {
  std::erase_if(remote_candidates_,
                [&cand_to_remove](const Candidate& candidate) { return cand_to_remove.MatchesForRemoval(candidate); });
}

// Creates connections from all of the ports that we care about to the given remote candidate. The return value is
// true if we created a connection from the origin port.
bool P2PTransportChannel::CreateConnections(Timestamp now, const Candidate& remote_candidate, Port* origin_port) {
  // If we've already seen the new remote candidate (in the current candidate generation), then we shouldn't try
  // creating connections for it. We either already have a connection for it, or we previously created one and then
  // later pruned it. If we don't return, the channel will again re-create any connections that were previously
  // pruned, which will then immediately be re-pruned, churning the network for no purpose. This only applies to
  // candidates received over signaling (i.e. origin_port is NULL).
  if (!origin_port && IsDuplicateRemoteCandidate(remote_candidate)) {
    // return true to indicate success, without creating any new connections.
    return true;
  }

  // Add a new connection for this candidate to every port that allows such a connection (i.e., if they have
  // compatible protocols) and that does not already have a connection to an equivalent candidate. We must be careful
  // to make sure that the origin port is included, even if it was pruned, since that may be the only port that can
  // create this connection.
  bool created = false;
  const std::vector<Port*> ports = ports_;
  for (auto it = ports.rbegin(); it != ports.rend(); ++it) {
    if (CreateConnection(now, *it, remote_candidate, origin_port)) {
      if (*it == origin_port) created = true;
    }
  }

  if ((origin_port != nullptr) && std::find(ports_.begin(), ports_.end(), origin_port) == ports_.end()) {
    if (CreateConnection(now, origin_port, remote_candidate, origin_port)) created = true;
  }

  // Remember this remote candidate so that we can add it to future ports.
  RememberRemoteCandidate(remote_candidate, origin_port);

  return created;
}

// Setup a connection object for the local and remote candidate combination. And then listen to connection object for
// changes.
bool P2PTransportChannel::CreateConnection(Timestamp now, Port* port, const Candidate& remote_candidate,
                                           Port* origin_port) {
  if (!port->SupportsProtocol(remote_candidate.protocol())) {
    return false;
  }

  if (ice_field_trials_.skip_relay_to_non_relay_connections) {
    IceCandidateType port_type = port->Type();
    if ((port_type != remote_candidate.type()) &&
        (port_type == IceCandidateType::kRelay || remote_candidate.is_relay())) {
      return false;
    }
  }

  // Look for an existing connection with this remote address. If one is not found or a connection exists with an
  // older generation remote candidate and is not selected, then we can create a new connection for this address. Any
  // selected connection is kept so that it can be used to send data during ICE restart per RFC 8445. The skipped
  // connection would be pruned later when a new generation connection is selected at the end of ICE restart.
  Connection* connection = port->GetConnection(remote_candidate.address());
  if (connection == nullptr ||
      (!connection->selected() && connection->remote_candidate().generation() < remote_candidate.generation())) {
    // Don't create a connection if this is a candidate we received in a message and we are not allowed to make
    // outgoing connections.
    Port::CandidateOrigin origin = GetOrigin(port, origin_port);
    if (origin == Port::ORIGIN_MESSAGE && incoming_only_) {
      return false;
    }
    Connection* new_connection = port->CreateConnection(now, remote_candidate, origin);
    if (!new_connection) {
      return false;
    }
    AddConnection(new_connection);
    return true;
  }

  // No new connection was created. libwebrtc logs a selected connection that exists, or an attempt to change a remote
  // candidate (it is not legal to change the parameters of an existing connection; the other side can send a
  // duplicate candidate).
  return false;
}

bool P2PTransportChannel::FindConnection(const Connection* connection) const {
  return std::find(connections_.begin(), connections_.end(), connection) != connections_.end();
}

uint32_t P2PTransportChannel::GetRemoteCandidateGeneration(const Candidate& candidate) const {
  // If the candidate has a ufrag, use it to find the generation.
  if (!candidate.username().empty()) {
    uint32_t generation = 0;
    if (!FindRemoteIceFromUfrag(candidate.username(), &generation)) {
      // If the ufrag is not found, assume the next/future generation.
      generation = static_cast<uint32_t>(remote_ice_parameters_.size());
    }
    return generation;
  }
  // If candidate generation is set, use that.
  if (candidate.generation() > 0) {
    return candidate.generation();
  }
  // Otherwise, assume the generation from remote ice parameters.
  return remote_ice_generation();
}

// Check if remote candidate is already cached.
bool P2PTransportChannel::IsDuplicateRemoteCandidate(const Candidate& candidate) const {
  for (const RemoteCandidate& c : remote_candidates_) {
    if (c.IsEquivalent(candidate)) {
      return true;
    }
  }
  return false;
}

// Maintain our remote candidate list, adding this new remote one.
void P2PTransportChannel::RememberRemoteCandidate(const Candidate& remote_candidate, Port* origin_port) {
  // Remove any candidates whose generation is older than this one. The presence of a new generation indicates that
  // the old ones are not useful.
  std::erase_if(remote_candidates_,
                [&](const RemoteCandidate& c) { return c.generation() < remote_candidate.generation(); });

  // Make sure this candidate is not a duplicate.
  if (IsDuplicateRemoteCandidate(remote_candidate)) {
    return;
  }

  // Try this candidate for all future ports.
  remote_candidates_.push_back(RemoteCandidate(remote_candidate, origin_port));
}

// Set options on ourselves is simply setting options on all of our available port objects.
int P2PTransportChannel::SetOption(SocketOption opt, int value) {
  if (ice_field_trials_.override_dscp && opt == SocketOption::kDscp) {
    value = *ice_field_trials_.override_dscp;
  }

  auto it = options_.find(opt);
  if (it == options_.end()) {
    options_.insert(std::make_pair(opt, value));
  } else if (it->second == value) {
    return 0;
  } else {
    it->second = value;
  }

  for (Port* port : ports_) {
    // Because this also occurs deferred, probably no point in reporting an error.
    port->SetOption(opt, value);
  }
  return 0;
}

bool P2PTransportChannel::GetOption(SocketOption opt, int* value) {
  const auto found = options_.find(opt);
  if (found == options_.end()) {
    return false;
  }
  *value = found->second;
  return true;
}

// Send data to the other side, using our selected connection.
int P2PTransportChannel::SendPacket(Timestamp now, std::span<const uint8_t> data, const PacketOptions& options,
                                    int flags) {
  now_ = std::max(now_, now);
  if (flags != 0) {
    error_ = EINVAL;
    return -1;
  }

  // If we don't think the connection is working yet, return ENOTCONN instead of sending a packet that will probably
  // be dropped.
  if (!ReadyToSend(selected_connection_)) {
    error_ = ENOTCONN;
    return -1;
  }

  packets_sent_++;
  last_sent_packet_id_ = static_cast<int>(options.packet_id);
  // libwebrtc marks the packet kData in info_signaled_after_sent; PacketOptions carries no packet type.
  int sent = selected_connection_->Send(now, data, options);
  if (sent <= 0) {
    error_ = selected_connection_->GetError();
    return sent;
  }

  bytes_sent_ += static_cast<uint64_t>(sent);
  return sent;
}

bool P2PTransportChannel::GetStats(Timestamp now, IceTransportStats* ice_transport_stats) {
  now_ = std::max(now_, now);
  // Gather candidate pair stats (candidate stats are not ported).
  ice_transport_stats->connection_infos.clear();

  // TODO(qingsi): Remove naming inconsistency for candidate pair/connection.
  for (Connection* connection : connections_) {
    ConnectionInfo stats = connection->stats(now);
    stats.local_candidate = SanitizeLocalCandidate(stats.local_candidate);
    stats.remote_candidate = SanitizeRemoteCandidate(stats.remote_candidate);
    stats.best_connection = (selected_connection_ == connection);
    ice_transport_stats->connection_infos.push_back(std::move(stats));
  }

  ice_transport_stats->selected_candidate_pair_changes = selected_candidate_pair_changes_;

  ice_transport_stats->bytes_sent = bytes_sent_;
  ice_transport_stats->bytes_received = bytes_received_;
  ice_transport_stats->packets_sent = packets_sent_;
  ice_transport_stats->packets_received = packets_received_;

  ice_transport_stats->ice_role = GetIceRole();
  ice_transport_stats->ice_local_username_fragment = ice_parameters_.ufrag;
  ice_transport_stats->ice_state = ComputeIceTransportState();

  return true;
}

Dscp P2PTransportChannel::DefaultDscpValue() const {
  auto it = options_.find(SocketOption::kDscp);
  if (it == options_.end()) {
    return Dscp::kNoChange;
  }
  return static_cast<Dscp>(it->second);
}

void P2PTransportChannel::RemoveConnectionForTest(Timestamp now, Connection* connection) {
  connection->UnsubscribeDestroyed(this);
  RemoveConnection(connection);
  if (selected_connection_ == connection) selected_connection_ = nullptr;
  connection->Destroy(now);
}

// Monitor connection states.
void P2PTransportChannel::UpdateConnectionStates(Timestamp now) {
  now_ = std::max(now_, now);
  // We need to copy the list of connections since some may delete themselves when we call UpdateState.
  std::vector<Connection*> copy(connections_.begin(), connections_.end());
  for (Connection* c : copy) {
    if (FindConnection(c)) c->UpdateState(now);
  }
}

void P2PTransportChannel::OnStartedPinging() {
  // "Have a pingable connection for the first time; starting to ping." The regathering task (a RepeatingTaskHandle
  // there) first runs one interval from now, then every interval.
  next_regather_ = now_ + config_.regather_on_failed_networks_interval_or_default();
}

bool P2PTransportChannel::IsPortPruned(const Port* port) const {
  return std::find(ports_.begin(), ports_.end(), port) == ports_.end();
}

bool P2PTransportChannel::IsRemoteCandidatePruned(const Candidate& cand) const {
  return std::find_if(remote_candidates_.begin(), remote_candidates_.end(),
                      [&cand](const RemoteCandidate& c) { return static_cast<const Candidate&>(c) == cand; }) ==
         remote_candidates_.end();
}

bool P2PTransportChannel::PresumedWritable(const Connection* conn) const {
  return (conn->write_state() == Connection::STATE_WRITE_INIT && config_.presume_writable_when_fully_relayed &&
          conn->local_candidate().is_relay() &&
          (conn->remote_candidate().is_relay() || conn->remote_candidate().is_prflx()));
}

void P2PTransportChannel::UpdateState(Timestamp now) {
  now_ = std::max(now_, now);
  // Check if all connections are timedout.
  bool all_connections_timedout = true;
  for (const Connection* conn : connections_) {
    if (conn->write_state() != Connection::STATE_WRITE_TIMEOUT) {
      all_connections_timedout = false;
      break;
    }
  }

  // Now update the writable state of the channel with the information we have so far.
  if (all_connections_timedout) {
    HandleAllTimedOut(now);
  }

  // Update the state of this channel.
  UpdateTransportState();
}

bool P2PTransportChannel::AllowedToPruneConnections() const {
  return ice_role_ == ICEROLE_CONTROLLING || (selected_connection_ && selected_connection_->nominated());
}

bool P2PTransportChannel::PruneConnections(Timestamp now, std::span<const Connection* const> connections) {
  now_ = std::max(now_, now);
  if (!AllowedToPruneConnections()) {
    // "Not allowed to prune connections"
    return false;
  }
  for (const Connection* conn : connections) {
    FromIceController(conn)->Prune();
  }
  return true;
}

NetworkRoute P2PTransportChannel::ConfigureNetworkRoute(const Connection* conn) const {
  NetworkRoute route;
  route.connected = ReadyToSend(conn);
  route.local = CreateRouteEndpointFromCandidate(/*local=*/true, conn->local_candidate(),
                                                 /*uses_turn=*/conn->port()->Type() == IceCandidateType::kRelay);
  route.remote = CreateRouteEndpointFromCandidate(/*local=*/false, conn->remote_candidate(),
                                                  /*uses_turn=*/conn->remote_candidate().is_relay());
  route.last_sent_packet_id = last_sent_packet_id_;
  route.packet_overhead =
      conn->local_candidate().address().ipaddr().overhead() + GetProtocolOverhead(conn->local_candidate().protocol());
  return route;
}

void P2PTransportChannel::SwitchSelectedConnection(Timestamp now, const Connection* new_connection,
                                                   IceSwitchReason reason) {
  now_ = std::max(now_, now);
  SwitchSelectedConnectionInternal(now, FromIceController(new_connection), reason);
}

// Change the selected connection, and let listeners know.
void P2PTransportChannel::SwitchSelectedConnectionInternal(Timestamp now, Connection* conn, IceSwitchReason reason) {
  // Note: if conn is NULL, the previous `selected_connection_` has been destroyed, so don't use it.
  Connection* old_selected_connection = selected_connection_;
  selected_connection_ = conn;
  network_route_.reset();
  if (old_selected_connection) {
    old_selected_connection->set_selected(false);
  }
  if (selected_connection_) {
    ++nomination_;
    selected_connection_->set_selected(true);
    // This is a temporary, but safe fix to webrtc issue 5705.
    // TODO(honghaiz): Make all ENOTCONN error routed through the transport channel so that it knows whether the media
    // channel is allowed to send; then it will only signal ready-to-send if the media channel has been disallowed to
    // send.
    if (selected_connection_->writable() || PresumedWritable(selected_connection_)) {
      NotifyReadyToSend(this, now);
    }

    network_route_.emplace(ConfigureNetworkRoute(selected_connection_));
  }
  // Otherwise: "No selected connection".

  if (conn != nullptr && ice_role_ == ICEROLE_CONTROLLING &&
      ((ice_field_trials_.send_ping_on_switch_ice_controlling && old_selected_connection != nullptr) ||
       ice_field_trials_.send_ping_on_selected_ice_controlling)) {
    SendPingRequestInternal(now, conn);
  }

  NotifyNetworkRouteChanged(network_route_);

  // Create event for candidate pair change.
  if (selected_connection_ && candidate_pair_change_callback_) {
    CandidatePairChangeEvent pair_change;
    pair_change.transport_name = transport_name();
    pair_change.selected_candidate_pair = *GetSelectedCandidatePair();
    // "Never" is at or before Timestamp 0 here and 0 there.
    pair_change.last_data_received_ms = std::max(selected_connection_->LastDataReceived(), Timestamp::Millis(0)).ms();
    pair_change.reason = IceSwitchReasonToString(reason);
    pair_change.estimated_disconnected_time_ms =
        old_selected_connection != nullptr ? ComputeEstimatedDisconnectedTime(now, old_selected_connection).ms() : 0;
    candidate_pair_change_callback_(pair_change);
  }

  ++selected_candidate_pair_changes_;

  ice_controller_->OnConnectionSwitched(selected_connection_);
}

TimeDelta P2PTransportChannel::ComputeEstimatedDisconnectedTime(Timestamp now,
                                                                const Connection* old_connection) const {
  // TODO(jonaso): nicer keeps estimate of how frequently data _should_ be received, this could be used to give better
  // estimate (if needed).
  // "Never" is at or before Timestamp 0 here and 0 there.
  Timestamp last_data_or_old_ping =
      std::max({old_connection->LastReceived(), last_data_received_, Timestamp::Millis(0)});
  return now - last_data_or_old_ping;
}

// Warning: UpdateTransportState should eventually be called whenever a connection is added, deleted, or the write
// state of any connection changes so that the transport controller will get the up-to-date channel state. However it
// should not be called too often; in the case that multiple connection states change, it should be called after all
// the connection states have changed. For example, we call this at the end of SortConnectionsAndUpdateState.
void P2PTransportChannel::UpdateTransportState() {
  // If our selected connection is "presumed writable" (TURN-TURN with no CreatePermission required), act like we're
  // already writable to the upper layers, so they can start media quicker.
  bool writable =
      selected_connection_ && (selected_connection_->writable() || PresumedWritable(selected_connection_));
  SetWritable(writable);

  bool receiving = false;
  for (const Connection* connection : connections_) {
    if (connection->receiving()) {
      receiving = true;
      break;
    }
  }
  SetReceiving(receiving);

  IceTransportStateInternal state = ComputeState();
  IceTransportState current_standardized_state = ComputeIceTransportState();

  // libwebrtc DCHECKs the allowed transitions of the internal state here (any state but INIT may follow any other;
  // INIT only goes forward).
  if (standardized_state_ != current_standardized_state || state_ != state) {
    standardized_state_ = current_standardized_state;
    state_ = state;
    // Unconditionally signal change, no matter what changed.
    NotifyIceTransportStateChanged(this);
  }
}

void P2PTransportChannel::MaybeStopPortAllocatorSessions(Timestamp now) {
  if (!IsGettingPorts()) {
    return;
  }

  for (const auto& session : allocator_sessions_) {
    if (session->IsStopped()) {
      continue;
    }
    // If gathering continually, keep the last session running so that it can gather candidates if the networks
    // change.
    if (config_.gather_continually() && session == allocator_sessions_.back()) {
      session->ClearGettingPorts(now);
    } else {
      session->StopGettingPorts(now);
    }
  }
}

void P2PTransportChannel::OnSelectedConnectionDestroyed(Timestamp now) {
  // "Selected connection destroyed. Will choose a new one."
  IceSwitchReason reason = IceSwitchReason::SELECTED_CONNECTION_DESTROYED;
  SwitchSelectedConnectionInternal(now, nullptr, reason);
  ice_controller_->OnSortAndSwitchRequest(now, reason);
}

// If all connections timed out, delete them all.
void P2PTransportChannel::HandleAllTimedOut(Timestamp now) {
  bool update_selected_connection = false;
  std::vector<Connection*> copy(connections_.begin(), connections_.end());
  for (Connection* connection : copy) {
    if (selected_connection_ == connection) {
      selected_connection_ = nullptr;
      update_selected_connection = true;
    }
    connection->UnsubscribeDestroyed(this);
    RemoveConnection(connection);
    connection->Destroy(now);
  }

  if (update_selected_connection) OnSelectedConnectionDestroyed(now);
}

bool P2PTransportChannel::ReadyToSend(const Connection* connection) const {
  // Note that we allow sending on an unreliable connection, because it's possible that it became unreliable simply
  // due to bad chance. So this shouldn't prevent attempting to send media.
  return connection != nullptr &&
         (connection->writable() || connection->write_state() == Connection::STATE_WRITE_UNRELIABLE ||
          PresumedWritable(connection));
}

// This method is only for unit testing.
Connection* P2PTransportChannel::FindNextPingableConnection(Timestamp now) {
  now_ = std::max(now_, now);
  const Connection* conn = ice_controller_->FindNextPingableConnection(now);
  return conn ? FromIceController(conn) : nullptr;
}

void P2PTransportChannel::SendPingRequest(Timestamp now, const Connection* connection) {
  now_ = std::max(now_, now);
  SendPingRequestInternal(now, FromIceController(connection));
}

void P2PTransportChannel::SendPingRequestInternal(Timestamp now, Connection* connection) {
  PingConnection(now, connection);
  MarkConnectionPinged(connection);
}

// A connection is considered a backup connection if the channel state is completed, the connection is not the
// selected connection and it is active.
void P2PTransportChannel::MarkConnectionPinged(Connection* conn) { ice_controller_->OnConnectionPinged(conn); }

// Apart from sending ping from `conn` this method also updates `use_candidate_attr` and `nomination` flags. One of
// the flags is set to nominate `conn` if this channel is in CONTROLLING.
void P2PTransportChannel::PingConnection(Timestamp now, Connection* conn) {
  bool use_candidate_attr = false;
  uint32_t nomination = 0;
  if (ice_role_ == ICEROLE_CONTROLLING) {
    bool renomination_supported = ice_parameters_.renomination && !remote_ice_parameters_.empty() &&
                                  remote_ice_parameters_.back().renomination;
    if (renomination_supported) {
      nomination = GetNominationAttr(conn);
    } else {
      use_candidate_attr = GetUseCandidateAttr(conn);
    }
  }
  conn->set_nomination(nomination);
  conn->set_use_candidate_attr(use_candidate_attr);
  last_ping_sent_ = now;
  // The STUN dictionary is not ported, so there is never a GOOG-DELTA to send (StunDictionaryWriter::CreateDelta
  // returns null when nothing is pending).
  conn->Ping(last_ping_sent_);
}

uint32_t P2PTransportChannel::GetNominationAttr(Connection* conn) const {
  return (conn == selected_connection_) ? nomination_ : 0;
}

// Nominate a connection based on the NominationMode.
bool P2PTransportChannel::GetUseCandidateAttr(Connection* conn) const {
  return ice_controller_->GetUseCandidateAttribute(conn, config_.default_nomination_mode, remote_ice_mode_);
}

// When a connection's state changes, we need to figure out who to use as the selected connection again. It could
// have become usable, or become unusable.
void P2PTransportChannel::OnConnectionStateChange(Connection* connection) {
  const Timestamp now = EventTime(connection);
  // May stop the allocator session when at least one connection becomes strongly connected after starting to get
  // ports and the local candidate of the connection is at the latest generation. It is not enough to check that the
  // connection becomes weakly connected because the connection may be changing from (writable, receiving) to
  // (writable, not receiving).
  if (ice_field_trials_.stop_gather_on_strongly_connected) {
    bool strongly_connected = !connection->weak();
    bool latest_generation = connection->local_candidate().generation() >= allocator_session()->generation();
    if (strongly_connected && latest_generation) {
      MaybeStopPortAllocatorSessions(now);
    }
  }
  // We have to unroll the stack before doing this because we may be changing the state of connections while sorting.
  ice_controller_->OnSortAndSwitchRequest(now, IceSwitchReason::CONNECT_STATE_CHANGE);  // "candidate pair state
                                                                                        // changed");

  ice_controller_->OnConnectionUpdated(connection);
}

// When a connection is removed, edit it out, and then update our best connection.
void P2PTransportChannel::OnConnectionDestroyed(Connection* connection) {
  // Note: the previous selected_connection_ may be destroyed by now, so don't use it.

  // Remove this connection from the list.
  RemoveConnection(connection);

  // If this is currently the selected connection, then we need to pick a new one. The call to
  // SortConnectionsAndUpdateState will pick a new one. It looks at the current selected connection in order to avoid
  // switching between fairly similar ones. Since this connection is no longer an option, we can just set selected to
  // nullptr and re-choose a best assuming that there was no selected connection.
  if (selected_connection_ == connection) {
    OnSelectedConnectionDestroyed(now_);
  } else {
    // If a non-selected connection was destroyed, we don't need to re-sort but we do need to update state, because we
    // could be switching to "failed" or "completed".
    UpdateTransportState();
  }
}

void P2PTransportChannel::RemoveConnection(Connection* connection) {
  auto it = std::find(connections_.begin(), connections_.end(), connection);
  if (it == connections_.end()) return;  // libwebrtc DCHECKs that it is there.
  connection->DeregisterReceivedPacketCallback();
  connections_.erase(it);
  ice_controller_->OnConnectionDestroyed(connection);
}

// When a port is destroyed, remove it from our list of ports to use for connection attempts.
void P2PTransportChannel::OnPortDestroyed(Port* port) {
  std::erase(ports_, port);
  std::erase(pruned_ports_, port);
}

void P2PTransportChannel::OnPortsPruned(PortAllocatorSession* /*session*/, const std::vector<Port*>& ports) {
  for (Port* port : ports) {
    PrunePort(port);
  }
}

void P2PTransportChannel::OnCandidatesRemoved(PortAllocatorSession* session,
                                              const std::vector<Candidate>& candidates) {
  // Do not signal candidate removals if continual gathering is not enabled, or if this is not the last session
  // because an ICE restart would have signaled the remote side to remove all candidates in previous sessions.
  if (!config_.gather_continually() || session != allocator_session()) {
    return;
  }
  if (candidates_removed_callback_) {
    candidates_removed_callback_(this, candidates);
  }
}

void P2PTransportChannel::PruneAllPorts() {
  pruned_ports_.insert(pruned_ports_.end(), ports_.begin(), ports_.end());
  ports_.clear();
}

bool P2PTransportChannel::PrunePort(Port* port) {
  auto it = std::find(ports_.begin(), ports_.end(), port);
  // Don't need to do anything if the port has been deleted from the port list.
  if (it == ports_.end()) {
    return false;
  }
  ports_.erase(it);
  pruned_ports_.push_back(port);
  return true;
}

// We data is available, let listeners know
void P2PTransportChannel::OnReadPacket(Connection* connection, const ReceivedPacket& packet) {
  now_ = std::max(now_, packet.arrival_time);
  if (connection != selected_connection_ && !FindConnection(connection)) {
    // Do not deliver, if packet doesn't belong to the correct transport channel.
    return;
  }

  // Let the client know of an incoming packet
  packets_received_++;
  bytes_received_ += packet.payload.size();
  last_data_received_ = std::max(last_data_received_, connection->LastDataReceived());

  NotifyPacketReceived(packet);

  // May need to switch the sending connection based on the receiving media path if this is the controlled side.
  if (ice_role_ == ICEROLE_CONTROLLED && connection != selected_connection_) {
    ice_controller_->OnImmediateSwitchRequest(now_, IceSwitchReason::DATA_RECEIVED, connection);
  }
}

void P2PTransportChannel::OnSentPacket(const SentPacketInfo& sent_packet) { NotifySentPacket(this, sent_packet); }

void P2PTransportChannel::OnReadyToSend(Connection* connection) {
  if (connection == selected_connection_ && writable()) {
    NotifyReadyToSend(this, EventTime(connection));
  }
}

void P2PTransportChannel::SetWritable(bool writable) {
  if (writable_ == writable) {
    return;
  }
  writable_ = writable;
  if (writable_) {
    has_been_writable_ = true;
    NotifyReadyToSend(this, now_);
  }
  NotifyWritableState(this, now_);
}

void P2PTransportChannel::SetReceiving(bool receiving) {
  if (receiving_ == receiving) {
    return;
  }
  receiving_ = receiving;
  NotifyReceivingState(this, now_);
}

Candidate P2PTransportChannel::SanitizeLocalCandidate(const Candidate& c) const {
  // Delegates to the port allocator.
  return allocator_->SanitizeCandidate(c);
}

Candidate P2PTransportChannel::SanitizeRemoteCandidate(const Candidate& c) const {
  // libwebrtc also sanitizes an mDNS candidate the remote signaled; the library has none. Remove the address for
  // prflx remote candidates. See https://w3c.github.io/webrtc-stats/#dom-rtcicecandidatestats. (ToSanitizedCopy with
  // use_hostname_address and an empty hostname: the address keeps only its port.)
  Candidate copy = c;
  if (c.is_prflx()) {
    copy.set_address(SocketAddress(IpAddress(), c.address().port()));
    // Filter remote ufrag of peer-reflexive candidates before any ICE parameters are known.
    uint32_t remote_generation = 0;
    if (FindRemoteIceFromUfrag(c.username(), &remote_generation) == nullptr) copy.set_username("");
  }
  return copy;
}

Connection* P2PTransportChannel::FromIceController(const Connection* conn) const {
  return const_cast<Connection*>(conn);
}

std::string P2PTransportChannel::ToString() const {
  std::string s = "Channel[" + transport_name_ + "|" + std::to_string(component_) + "|";
  s += receiving_ ? "R" : "_";
  s += writable_ ? "W" : "_";
  s += "]";
  return s;
}

void P2PTransportChannel::Process(Timestamp now) {
  now_ = std::max(now_, now);
  // Each session runs its tasks and its ports (and their connections' STUN transactions). A session added while
  // processing (an ICE restart from a callback) runs on the next call.
  const size_t sessions = allocator_sessions_.size();
  for (size_t i = 0; i < sessions && i < allocator_sessions_.size(); ++i) {
    if (allocator_sessions_[i]->NextProcessTime() <= now) allocator_sessions_[i]->Process(now);
  }
  ice_controller_->Process(now);
  while (next_regather_ <= now) {
    const Timestamp due = next_regather_;
    now_ = std::max(now_, due);
    if (allocator_session() && allocator_session()->IsCleared()) {
      // Only regather when the current session is in the CLEARED state (i.e., not running or stopped). It is only
      // possible to enter this state when we gather continually, so there is an implicit check on continual gathering
      // here.
      allocator_session()->RegatherOnFailedNetworks(now);
    }
    next_regather_ = due + config_.regather_on_failed_networks_interval_or_default();
  }
}

Timestamp P2PTransportChannel::NextProcessTime() const {
  Timestamp next = std::min(ice_controller_->NextProcessTime(), next_regather_);
  for (const auto& session : allocator_sessions_) next = std::min(next, session->NextProcessTime());
  return next;
}

}  // namespace sm::p2p
