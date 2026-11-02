// libwebrtc M157's p2p/base/basic_ice_controller.cc (compare/webrtc/reference/p2p/base/); see
// sm/p2p/basic_ice_controller.h.
#include "sm/p2p/basic_ice_controller.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include "sm/p2p/p2p_constants.h"
#include "sm/p2p/socket_address.h"

namespace sm::p2p {
namespace {

// The minimum improvement in RTT that justifies a switch.
constexpr TimeDelta kMinImprovement = TimeDelta::Millis(10);

bool IsRelayRelay(const Connection* conn) {
  return conn->local_candidate().is_relay() && conn->remote_candidate().is_relay();
}

bool IsUdp(const Connection* conn) { return conn->local_candidate().relay_protocol() == UDP_PROTOCOL_NAME; }

constexpr int a_is_better = 1;
constexpr int b_is_better = -1;
constexpr int a_and_b_equal = 0;

bool LocalCandidateUsesPreferredNetwork(const Connection* conn, std::optional<AdapterType> network_preference) {
  AdapterType network_type = conn->network()->type();
  return network_preference.has_value() && (network_type == network_preference);
}

int CompareCandidatePairsByNetworkPreference(const Connection* a, const Connection* b,
                                             std::optional<AdapterType> network_preference) {
  bool a_uses_preferred_network = LocalCandidateUsesPreferredNetwork(a, network_preference);
  bool b_uses_preferred_network = LocalCandidateUsesPreferredNetwork(b, network_preference);
  if (a_uses_preferred_network && !b_uses_preferred_network) {
    return a_is_better;
  } else if (!a_uses_preferred_network && b_uses_preferred_network) {
    return b_is_better;
  }
  return a_and_b_equal;
}

}  // namespace

BasicIceController::BasicIceController(const IceControllerFactoryArgs& args)
    : ice_transport_state_func_(args.ice_transport_state_func),
      ice_role_func_(args.ice_role_func),
      is_connection_pruned_func_(args.is_connection_pruned_func),
      field_trials_(args.ice_field_trials) {}

BasicIceController::~BasicIceController() = default;

void BasicIceController::SetIceConfig(const IceConfig& config) { config_ = config; }

void BasicIceController::SetSelectedConnection(const Connection* selected_connection) {
  selected_connection_ = selected_connection;
}

void BasicIceController::AddConnection(const Connection* connection) {
  connections_.push_back(connection);
  unpinged_connections_.insert(connection);
}

void BasicIceController::OnConnectionDestroyed(const Connection* connection) {
  pinged_connections_.erase(connection);
  unpinged_connections_.erase(connection);
  auto it = std::find(connections_.begin(), connections_.end(), connection);
  if (it != connections_.end()) connections_.erase(it);
  if (selected_connection_ == connection) selected_connection_ = nullptr;
}

bool BasicIceController::HasPingableConnection(Timestamp now) const {
  return std::any_of(connections_.begin(), connections_.end(),
                     [this, now](const Connection* c) { return IsPingable(c, now); });
}

IceControllerInterface::PingResult BasicIceController::GetConnectionToPing(Timestamp now, Timestamp last_ping_sent) {
  // When the selected connection is not receiving or not writable, or any active connection has not been pinged
  // enough times, use the weak ping interval.
  bool need_more_pings_at_weak_interval = std::any_of(connections_.begin(), connections_.end(), [](const Connection* c) {
    return c->active() && c->num_pings_sent() < kMinPingsAtWeakPingInterval;
  });
  TimeDelta ping_interval =
      (weak() || need_more_pings_at_weak_interval) ? weak_ping_interval() : strong_ping_interval();

  const Connection* conn = nullptr;
  if (now >= last_ping_sent + ping_interval) conn = FindNextPingableConnection(now);
  return PingResult(conn, std::min(ping_interval, check_receiving_interval()));
}

void BasicIceController::MarkConnectionPinged(const Connection* conn) {
  if (conn && pinged_connections_.insert(conn).second) unpinged_connections_.erase(conn);
}

// Returns the next pingable connection to ping.
const Connection* BasicIceController::FindNextPingableConnection(Timestamp now) {
  // Rule 1: Selected connection takes priority over non-selected ones.
  if (selected_connection_ && selected_connection_->connected() && selected_connection_->writable() &&
      WritableConnectionPastPingInterval(selected_connection_, now)) {
    return selected_connection_;
  }

  // Rule 2: If the channel is weak, we need to find a new writable and receiving connection, probably on a different
  // network. Make sure at least one connection per network is pinged frequently enough in order for it to be
  // selectable. Rule 2.1: Among such connections, pick the one with the earliest last-ping-sent time.
  if (weak()) {
    std::vector<const Connection*> pingable_selectable_connections;
    const std::vector<const Connection*> best = GetBestWritableConnectionPerNetwork();
    std::copy_if(best.begin(), best.end(), std::back_inserter(pingable_selectable_connections),
                 [this, now](const Connection* conn) { return WritableConnectionPastPingInterval(conn, now); });
    auto iter = std::min_element(
        pingable_selectable_connections.begin(), pingable_selectable_connections.end(),
        [](const Connection* conn1, const Connection* conn2) { return conn1->LastPingSent() < conn2->LastPingSent(); });
    if (iter != pingable_selectable_connections.end()) return *iter;
  }

  // Rule 3: Triggered checks have priority over non-triggered connections.
  // Rule 3.1: Among triggered checks, oldest takes precedence.
  const Connection* oldest_triggered_check = FindOldestConnectionNeedingTriggeredCheck(now);
  if (oldest_triggered_check) return oldest_triggered_check;

  // Rule 4: Unpinged connections have priority over pinged ones. If there are unpinged and pingable connections,
  // only ping those. Otherwise, treat everything as unpinged.
  if (std::none_of(unpinged_connections_.begin(), unpinged_connections_.end(),
                   [this, now](const Connection* conn) { return IsPingable(conn, now); })) {
    unpinged_connections_.insert(pinged_connections_.begin(), pinged_connections_.end());
    pinged_connections_.clear();
  }

  // Among un-pinged pingable connections, "more pingable" takes precedence.
  std::vector<const Connection*> pingable_connections;
  std::copy_if(unpinged_connections_.begin(), unpinged_connections_.end(), std::back_inserter(pingable_connections),
               [this, now](const Connection* conn) { return IsPingable(conn, now); });
  auto iter = std::max_element(pingable_connections.begin(), pingable_connections.end(),
                               [this](const Connection* conn1, const Connection* conn2) {
                                 // Some implementations of max_element compare an element with itself.
                                 if (conn1 == conn2) return false;
                                 return MorePingable(conn1, conn2) == conn2;
                               });
  if (iter != pingable_connections.end()) return *iter;
  return nullptr;
}

// Find "triggered checks": connections that have received a ping but have not sent a ping since receiving it
// (last_ping_received > last_ping_sent), unless already writable.
const Connection* BasicIceController::FindOldestConnectionNeedingTriggeredCheck(Timestamp now) {
  const Connection* oldest_needing_triggered_check = nullptr;
  for (const Connection* conn : connections_) {
    if (!IsPingable(conn, now)) continue;
    bool needs_triggered_check = (!conn->writable() && conn->LastPingReceived() > conn->LastPingSent());
    if (needs_triggered_check &&
        (!oldest_needing_triggered_check ||
         (conn->LastPingReceived() < oldest_needing_triggered_check->LastPingReceived()))) {
      oldest_needing_triggered_check = conn;
    }
  }
  return oldest_needing_triggered_check;
}

bool BasicIceController::WritableConnectionPastPingInterval(const Connection* conn, Timestamp now) const {
  TimeDelta interval = CalculateActiveWritablePingInterval(conn, now);
  return conn->LastPingSent() + interval <= now;
}

TimeDelta BasicIceController::CalculateActiveWritablePingInterval(const Connection* conn, Timestamp now) const {
  // Ping each connection at a higher rate at least kMinPingsAtWeakPingInterval times.
  if (conn->num_pings_sent() < kMinPingsAtWeakPingInterval) return weak_ping_interval();

  TimeDelta stable_interval = config_.stable_writable_connection_ping_interval_or_default();
  TimeDelta weak_or_stablizing_interval = std::min(stable_interval, kWeakOrStabilizingWritableConnectionPingInterval);
  // If the channel is weak or the connection is not stable yet, use the weak_or_stablizing_interval.
  return (!weak() && conn->stable(now)) ? stable_interval : weak_or_stablizing_interval;
}

// Is the connection in a state for us to even consider pinging the other side? A connection is pingable even if it's
// not connected because that's how a TCP connection is kicked into reconnecting on the active side.
bool BasicIceController::IsPingable(const Connection* conn, Timestamp now) const {
  const Candidate& remote = conn->remote_candidate();
  // If we don't have an ICE ufrag and pwd, there's no way we can ping.
  if (remote.username().empty() || remote.password().empty()) return false;
  // A failed connection will not be pinged.
  if (conn->state() == IceCandidatePairState::FAILED) return false;
  // A never connected connection cannot be written to at all, so pinging is out of the question. However, if it has
  // become WRITABLE, it is in the reconnecting state so ping is needed.
  if (!conn->connected() && !conn->writable()) return false;
  // If we sent a number of pings wo/ reply, skip sending more until we get one.
  if (conn->TooManyOutstandingPings(field_trials_->max_outstanding_pings)) return false;
  // If the channel is weakly connected, ping all connections.
  if (weak()) return true;
  // Always ping active connections regardless whether the channel is completed or not, but backup connections are
  // pinged at a slower rate.
  if (IsBackupConnection(conn)) {
    return conn->rtt_samples() == 0 ||
           (now >= conn->LastPingResponseReceived() + config_.backup_connection_ping_interval_or_default());
  }
  // Don't ping inactive non-backup connections.
  if (!conn->active()) return false;
  // Do ping unwritable, active connections.
  if (!conn->writable()) return true;
  // Ping writable, active connections if it's been long enough since the last ping.
  return WritableConnectionPastPingInterval(conn, now);
}

// A connection is a backup connection if the channel state is completed, the connection is not the selected
// connection and it is active.
bool BasicIceController::IsBackupConnection(const Connection* conn) const {
  return ice_transport_state_func_() == IceTransportStateInternal::kCompleted && conn != selected_connection_ &&
         conn->active();
}

const Connection* BasicIceController::MorePingable(const Connection* conn1, const Connection* conn2) {
  if (config_.prioritize_most_likely_candidate_pairs) {
    const Connection* most_likely_to_work_conn = MostLikelyToWork(conn1, conn2);
    if (most_likely_to_work_conn) return most_likely_to_work_conn;
  }
  const Connection* least_recently_pinged_conn = LeastRecentlyPinged(conn1, conn2);
  if (least_recently_pinged_conn) return least_recently_pinged_conn;
  // During the initial state when nothing has been pinged yet, return the first one in the ordered `connections_`.
  return *std::find_if(connections_.begin(), connections_.end(),
                       [conn1, conn2](const Connection* conn) { return conn == conn1 || conn == conn2; });
}

const Connection* BasicIceController::MostLikelyToWork(const Connection* conn1, const Connection* conn2) {
  bool rr1 = IsRelayRelay(conn1);
  bool rr2 = IsRelayRelay(conn2);
  if (rr1 && !rr2) {
    return conn1;
  } else if (rr2 && !rr1) {
    return conn2;
  } else if (rr1 && rr2) {
    bool udp1 = IsUdp(conn1);
    bool udp2 = IsUdp(conn2);
    if (udp1 && !udp2) {
      return conn1;
    } else if (udp2 && udp1) {  // As libwebrtc (it reads `udp2 && udp1`, not `udp2 && !udp1`).
      return conn2;
    }
  }
  return nullptr;
}

const Connection* BasicIceController::LeastRecentlyPinged(const Connection* conn1, const Connection* conn2) {
  if (conn1->LastPingSent() < conn2->LastPingSent()) return conn1;
  if (conn1->LastPingSent() > conn2->LastPingSent()) return conn2;
  return nullptr;
}

std::map<const Network*, const Connection*> BasicIceController::GetBestConnectionByNetwork() const {
  // `connections_` has been sorted, so the first one in the list on a given network is the best connection on the
  // network, except that the selected connection is always the best connection on the network.
  std::map<const Network*, const Connection*> best_connection_by_network;
  if (selected_connection_) best_connection_by_network[selected_connection_->network()] = selected_connection_;
  for (const Connection* conn : connections_) {
    // This only inserts when the network does not exist in the map.
    best_connection_by_network.insert(std::make_pair(conn->network(), conn));
  }
  return best_connection_by_network;
}

std::vector<const Connection*> BasicIceController::GetBestWritableConnectionPerNetwork() const {
  std::vector<const Connection*> connections;
  for (const auto& kv : GetBestConnectionByNetwork()) {
    const Connection* conn = kv.second;
    if (conn->writable() && conn->connected()) connections.push_back(conn);
  }
  return connections;
}

IceControllerInterface::SwitchResult BasicIceController::HandleInitialSelectDampening(
    Timestamp now, IceSwitchReason /*reason*/, const Connection* new_connection) {
  if (!field_trials_->initial_select_dampening.has_value() &&
      !field_trials_->initial_select_dampening_ping_received.has_value()) {
    // experiment not enabled => select connection.
    return {.connection = new_connection, .recheck_event = std::nullopt, .connections_to_forget_state_on = {}};
  }

  int64_t max_delay = 0;
  if (new_connection->LastPingReceived() > Timestamp::Millis(0) &&
      field_trials_->initial_select_dampening_ping_received.has_value()) {
    max_delay = *field_trials_->initial_select_dampening_ping_received;
  } else if (field_trials_->initial_select_dampening.has_value()) {
    max_delay = *field_trials_->initial_select_dampening;
  }

  Timestamp start_wait = initial_select_timestamp_.value_or(now);
  Timestamp max_wait_until = start_wait + TimeDelta::Millis(max_delay);

  if (now >= max_wait_until) {
    initial_select_timestamp_ = std::nullopt;
    return {.connection = new_connection, .recheck_event = std::nullopt, .connections_to_forget_state_on = {}};
  }

  // We are not yet ready to select first connection... Set timestamp on first time, but run the delayed invokation
  // everytime to avoid possibility that we miss it.
  if (!initial_select_timestamp_.has_value()) initial_select_timestamp_ = now;

  int min_delay = static_cast<int>(max_delay);
  if (field_trials_->initial_select_dampening.has_value()) {
    min_delay = std::min(min_delay, *field_trials_->initial_select_dampening);
  }
  if (field_trials_->initial_select_dampening_ping_received.has_value()) {
    min_delay = std::min(min_delay, *field_trials_->initial_select_dampening_ping_received);
  }
  return {.connection = std::nullopt,
          .recheck_event = IceRecheckEvent(IceSwitchReason::ICE_CONTROLLER_RECHECK, min_delay),
          .connections_to_forget_state_on = {}};
}

IceControllerInterface::SwitchResult BasicIceController::ShouldSwitchConnection(Timestamp now,
                                                                                IceSwitchReason reason,
                                                                                const Connection* new_connection) {
  if (!ReadyToSend(new_connection) || selected_connection_ == new_connection) return {};

  if (selected_connection_ == nullptr) return HandleInitialSelectDampening(now, reason, new_connection);

  // Do not switch to a connection that is not receiving if it is not on a preferred network or it has higher cost
  // because it may be just spuriously better.
  int compare_a_b_by_networks =
      CompareCandidatePairNetworks(new_connection, selected_connection_, config_.network_preference);
  if (compare_a_b_by_networks == b_is_better && !new_connection->receiving()) return {};

  bool missed_receiving_unchanged_threshold = false;
  int64_t receiving_unchanged_threshold_ms = now.ms() - config_.receiving_switching_delay_or_default().ms();
  int cmp = CompareConnections(selected_connection_, new_connection, receiving_unchanged_threshold_ms,
                               &missed_receiving_unchanged_threshold);

  std::optional<IceRecheckEvent> recheck_event;
  if (missed_receiving_unchanged_threshold && config_.receiving_switching_delay_or_default() > TimeDelta::Zero()) {
    // If we do not switch to the connection because it missed the receiving threshold, the new connection is in a
    // better receiving state than the currently selected connection. So we need to re-check whether it needs to be
    // switched at a later time.
    recheck_event.emplace(reason, static_cast<int>(config_.receiving_switching_delay_or_default().ms()));
  }

  if (cmp < 0) {
    return {.connection = new_connection, .recheck_event = std::nullopt, .connections_to_forget_state_on = {}};
  } else if (cmp > 0) {
    return {.connection = std::nullopt, .recheck_event = recheck_event, .connections_to_forget_state_on = {}};
  }

  // If everything else is the same, switch only if rtt has improved by a margin.
  if (new_connection->Rtt() <= selected_connection_->Rtt() - kMinImprovement) {
    return {.connection = new_connection, .recheck_event = std::nullopt, .connections_to_forget_state_on = {}};
  }
  return {.connection = std::nullopt, .recheck_event = recheck_event, .connections_to_forget_state_on = {}};
}

IceControllerInterface::SwitchResult BasicIceController::SortAndSwitchConnection(Timestamp now,
                                                                                 IceSwitchReason reason) {
  // Find the best alternative connection by sorting. Amongst equal preference, writable connections, this chooses the
  // one whose estimated latency is lowest, so it is the only one that we need to consider switching to.
  std::stable_sort(connections_.begin(), connections_.end(), [this](const Connection* a, const Connection* b) {
    int cmp = CompareConnections(a, b, std::nullopt, nullptr);
    if (cmp != 0) return cmp > 0;
    // Otherwise, sort based on latency estimate.
    return a->Rtt() < b->Rtt();
  });
  const Connection* top_connection = (!connections_.empty()) ? connections_[0] : nullptr;
  return ShouldSwitchConnection(now, reason, top_connection);
}

bool BasicIceController::ReadyToSend(const Connection* connection) const {
  // Sending is allowed on an unreliable connection, because it may have become unreliable simply due to bad chance.
  return connection != nullptr && (connection->writable() ||
                                   connection->write_state() == Connection::STATE_WRITE_UNRELIABLE ||
                                   PresumedWritable(connection));
}

bool BasicIceController::PresumedWritable(const Connection* conn) const {
  return (conn->write_state() == Connection::STATE_WRITE_INIT && config_.presume_writable_when_fully_relayed &&
          conn->local_candidate().is_relay() &&
          (conn->remote_candidate().is_relay() || conn->remote_candidate().is_prflx()));
}

// Compare two connections based on their writing, receiving, and connected states.
int BasicIceController::CompareConnectionStates(const Connection* a, const Connection* b,
                                                std::optional<int64_t> receiving_unchanged_threshold,
                                                bool* missed_receiving_unchanged_threshold) const {
  // First, prefer a connection that's writable or presumed writable over one that's not writable.
  bool a_writable = a->writable() || PresumedWritable(a);
  bool b_writable = b->writable() || PresumedWritable(b);
  if (a_writable && !b_writable) return a_is_better;
  if (!a_writable && b_writable) return b_is_better;

  // Sort based on write-state. Better states have lower values.
  if (a->write_state() < b->write_state()) return a_is_better;
  if (b->write_state() < a->write_state()) return b_is_better;

  // We prefer a receiving connection to a non-receiving, higher-priority connection when sorting connections and
  // choosing which connection to switch to.
  if (a->receiving() && !b->receiving()) return a_is_better;
  if (!a->receiving() && b->receiving()) {
    if (!receiving_unchanged_threshold ||
        (a->ReceivingUnchangedSince().ms() <= *receiving_unchanged_threshold &&
         b->ReceivingUnchangedSince().ms() <= *receiving_unchanged_threshold)) {
      return b_is_better;
    }
    *missed_receiving_unchanged_threshold = true;
  }

  // TCP reconnecting: a disconnected connection that still pretends to be writable is worse than a connected one
  // (see libwebrtc's comment).
  if (a->write_state() == Connection::STATE_WRITABLE && b->write_state() == Connection::STATE_WRITABLE) {
    if (a->connected() && !b->connected()) return a_is_better;
    if (!a->connected() && b->connected()) return b_is_better;
  }
  return 0;
}

// Compares two connections based only on the candidate and network information. Positive if `a` is better than `b`.
int BasicIceController::CompareConnectionCandidates(const Connection* a, const Connection* b) const {
  int compare_a_b_by_networks = CompareCandidatePairNetworks(a, b, config_.network_preference);
  if (compare_a_b_by_networks != a_and_b_equal) return compare_a_b_by_networks;

  // Compare connection priority. Lower values get sorted last.
  if (a->priority() > b->priority()) return a_is_better;
  if (a->priority() < b->priority()) return b_is_better;

  // If we're still tied at this point, prefer a younger generation (a larger generation number).
  int cmp = static_cast<int>(a->remote_candidate().generation() + static_cast<uint32_t>(a->generation())) -
            static_cast<int>(b->remote_candidate().generation() + static_cast<uint32_t>(b->generation()));
  if (cmp != 0) return cmp;

  // A periodic regather produces candidates that appear the same but use a new port; the old ports get pruned
  // immediately, so rank the candidates with an active port higher.
  bool a_pruned = is_connection_pruned_func_(a);
  bool b_pruned = is_connection_pruned_func_(b);
  if (!a_pruned && b_pruned) return a_is_better;
  if (a_pruned && !b_pruned) return b_is_better;

  // Otherwise, must be equal
  return 0;
}

int BasicIceController::CompareConnections(const Connection* a, const Connection* b,
                                           std::optional<int64_t> receiving_unchanged_threshold,
                                           bool* missed_receiving_unchanged_threshold) const {
  // We prefer to switch to a writable and receiving connection over a non-writable or non-receiving connection, even
  // if the latter has been nominated by the controlling side.
  int state_cmp = CompareConnectionStates(a, b, receiving_unchanged_threshold, missed_receiving_unchanged_threshold);
  if (state_cmp != 0) return state_cmp;

  if (ice_role_func_() == ICEROLE_CONTROLLED) {
    // Compare the connections based on the nomination states and the last data received time if this is on the
    // controlled side.
    if (a->remote_nomination() > b->remote_nomination()) return a_is_better;
    if (a->remote_nomination() < b->remote_nomination()) return b_is_better;
    if (a->LastDataReceived() > b->LastDataReceived()) return a_is_better;
    if (a->LastDataReceived() < b->LastDataReceived()) return b_is_better;
  }

  // Compare the network cost and priority.
  return CompareConnectionCandidates(a, b);
}

int BasicIceController::CompareCandidatePairNetworks(const Connection* a, const Connection* b,
                                                     std::optional<AdapterType> /*network_preference*/) const {
  int compare_a_b_by_network_preference = CompareCandidatePairsByNetworkPreference(a, b, config_.network_preference);
  // The network preference has a higher precedence than the network cost.
  if (compare_a_b_by_network_preference != a_and_b_equal) return compare_a_b_by_network_preference;

  bool a_vpn = a->network()->IsVpn();
  bool b_vpn = b->network()->IsVpn();
  switch (config_.vpn_preference) {
    case VpnPreference::kDefault:
      break;
    case VpnPreference::kOnlyUseVpn:
    case VpnPreference::kPreferVpn:
      if (a_vpn && !b_vpn) {
        return a_is_better;
      } else if (!a_vpn && b_vpn) {
        return b_is_better;
      }
      break;
    case VpnPreference::kNeverUseVpn:
    case VpnPreference::kAvoidVpn:
      if (a_vpn && !b_vpn) {
        return b_is_better;
      } else if (!a_vpn && b_vpn) {
        return a_is_better;
      }
      break;
  }

  uint32_t a_cost = a->ComputeNetworkCost();
  uint32_t b_cost = b->ComputeNetworkCost();
  // Prefer lower network cost.
  if (a_cost < b_cost) return a_is_better;
  if (a_cost > b_cost) return b_is_better;
  return a_and_b_equal;
}

std::vector<const Connection*> BasicIceController::PruneConnections() {
  // Prune any connection for which there is a connected, writable connection on the same network with better or
  // equal priority. Connections on other networks are kept: they may represent very distinct paths. An "any address"
  // network is compared against the selected connection instead, since it could be using the same interface.
  std::vector<const Connection*> connections_to_prune;
  auto best_connection_by_network = GetBestConnectionByNetwork();
  for (const Connection* conn : connections_) {
    const Connection* best_conn = selected_connection_;
    if (!IpIsAny(conn->network()->GetBestIP())) {
      // Compare against the best connection for that network interface, so at least one connection per network is
      // left unpruned.
      best_conn = best_connection_by_network[conn->network()];
    }
    // Do not prune connections if the connection being compared against is weak. Otherwise, it may delete
    // connections prematurely.
    if (best_conn && conn != best_conn && !best_conn->weak() && CompareConnectionCandidates(best_conn, conn) >= 0) {
      connections_to_prune.push_back(conn);
    }
  }
  return connections_to_prune;
}

bool BasicIceController::GetUseCandidateAttr(const Connection* conn, NominationMode mode,
                                             IceMode remote_ice_mode) const {
  switch (mode) {
    case NominationMode::REGULAR:
      // TODO(honghaiz): Implement regular nomination.
      return false;
    case NominationMode::AGGRESSIVE:
      if (remote_ice_mode == ICEMODE_LITE) return GetUseCandidateAttr(conn, NominationMode::REGULAR, remote_ice_mode);
      return true;
    case NominationMode::SEMI_AGGRESSIVE: {
      // Nominate if
      // a) Remote is in FULL ICE AND
      //    a.1) `conn` is the selected connection OR
      //    a.2) there is no selected connection OR
      //    a.3) the selected connection is unwritable OR
      //    a.4) `conn` has higher priority than selected_connection.
      // b) Remote is in LITE ICE AND
      //    b.1) `conn` is the selected_connection AND
      //    b.2) `conn` is writable.
      bool selected = conn == selected_connection_;
      if (remote_ice_mode == ICEMODE_LITE) return selected && conn->writable();
      bool better_than_selected = !selected_connection_ || !selected_connection_->writable() ||
                                  CompareConnectionCandidates(selected_connection_, conn) < 0;
      return selected || better_than_selected;
    }
  }
  return false;
}

}  // namespace sm::p2p
