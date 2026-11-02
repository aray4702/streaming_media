// libwebrtc M157's BasicIceController (p2p/base/basic_ice_controller.{h,cc}, compare/webrtc/reference/p2p/base/), the
// ICE controller P2PTransportChannel uses by default (#1002, part of ICE #990):
// - Pinging: a weak channel (the selected pair not writable and receiving, or none) pings at the weak interval
//   (48 ms), a strong one at 480 ms, and every active pair gets kMinPingsAtWeakPingInterval (3) pings at the weak
//   interval first. The next pair to ping: the selected one when past its ping interval (900 ms while weak or not
//   stable, 2.5 s once stable); on a weak channel the best writable pair per network, least recently pinged first;
//   then triggered checks (a check arrived after our last ping), oldest first; then unpinged pairs before pinged ones,
//   least recently pinged first, ties to the sort order. Backup pairs (state completed, not selected) are pinged
//   every 25 s.
// - Sorting: writable (or presumed writable) first, then write state, receiving, connected; on the controlled side
//   the remote nomination and the last data received; then the network preference, the VPN preference, the network
//   cost, the pair priority, the younger generation and unpruned before pruned; equal pairs by RTT.
// - Switching: only to a pair that is ready to send (writable, unreliable or presumed writable). The first selection
//   may be dampened (field trials, off by default). A pair on a worse network must be receiving. A pair that is
//   receiving while the selected one is not wins only once both states are older than the receiving switching delay
//   (1 s), else a recheck after that delay; equal pairs switch only for an RTT 10 ms lower.
// - Pruning: a pair is pruned when a strong pair on its network is at least as good by candidates.
// - Nomination (USE-CANDIDATE): semi-aggressive by default.
//
// The library's time model: the calls that read the clock there take `now` (sm/p2p/ice_controller_interface.h).
#pragma once

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/connection.h"
#include "sm/p2p/ice_controller_interface.h"
#include "sm/p2p/ice_transport_internal.h"
#include "sm/p2p/network.h"

namespace sm::p2p {

class BasicIceController : public IceControllerInterface {
 public:
  explicit BasicIceController(const IceControllerFactoryArgs& args);
  ~BasicIceController() override;

  void SetIceConfig(const IceConfig& config) override;
  void SetSelectedConnection(const Connection* selected_connection) override;
  void AddConnection(const Connection* connection) override;
  void OnConnectionDestroyed(const Connection* connection) override;
  std::span<const Connection* const> GetConnections() const override { return connections_; }

  bool HasPingableConnection(Timestamp now) const override;
  PingResult GetConnectionToPing(Timestamp now, Timestamp last_ping_sent) override;
  bool GetUseCandidateAttr(const Connection* conn, NominationMode mode, IceMode remote_ice_mode) const override;
  SwitchResult ShouldSwitchConnection(Timestamp now, IceSwitchReason reason, const Connection* connection) override;
  SwitchResult SortAndSwitchConnection(Timestamp now, IceSwitchReason reason) override;
  std::vector<const Connection*> PruneConnections() override;

  // These methods are only for tests.
  const Connection* FindNextPingableConnection(Timestamp now) override;
  void MarkConnectionPinged(const Connection* conn) override;

 private:
  // A transport channel is weak if the current best connection is either not receiving or not writable, or if there
  // is no best connection at all.
  bool weak() const { return !selected_connection_ || selected_connection_->weak(); }
  TimeDelta weak_ping_interval() const {
    return std::max(config_.ice_check_interval_weak_connectivity_or_default(),
                    config_.ice_check_min_interval_or_default());
  }
  TimeDelta strong_ping_interval() const {
    return std::max(config_.ice_check_interval_strong_connectivity_or_default(),
                    config_.ice_check_min_interval_or_default());
  }
  TimeDelta check_receiving_interval() const {
    return std::max(kMinCheckReceivingInterval, config_.receiving_timeout_or_default() / 10);
  }

  const Connection* FindOldestConnectionNeedingTriggeredCheck(Timestamp now);
  // Between `conn1` and `conn2`, the one which should be pinged first.
  const Connection* MorePingable(const Connection* conn1, const Connection* conn2);
  // Select the connection which is Relay/Relay. If both of them are, UDP relay protocol takes precedence.
  const Connection* MostLikelyToWork(const Connection* conn1, const Connection* conn2);
  // Compare the last_ping_sent time and return the one least recently pinged.
  const Connection* LeastRecentlyPinged(const Connection* conn1, const Connection* conn2);

  bool IsPingable(const Connection* conn, Timestamp now) const;
  bool IsBackupConnection(const Connection* conn) const;
  // Whether a writable connection is past its ping interval and needs to be pinged again.
  bool WritableConnectionPastPingInterval(const Connection* conn, Timestamp now) const;
  TimeDelta CalculateActiveWritablePingInterval(const Connection* conn, Timestamp now) const;

  std::map<const Network*, const Connection*> GetBestConnectionByNetwork() const;
  std::vector<const Connection*> GetBestWritableConnectionPerNetwork() const;

  bool ReadyToSend(const Connection* connection) const;
  bool PresumedWritable(const Connection* conn) const;

  int CompareCandidatePairNetworks(const Connection* a, const Connection* b,
                                   std::optional<AdapterType> network_preference) const;
  // These return a positive value if `a` is preferable to `b`, a negative value if `b` is preferable, and 0 if they're
  // equally preferable. With `receiving_unchanged_threshold`, when `b` is receiving and `a` is not, returns a negative
  // value only if both have been in their receiving state since the threshold, and sets
  // `missed_receiving_unchanged_threshold` otherwise.
  int CompareConnectionStates(const Connection* a, const Connection* b,
                              std::optional<int64_t> receiving_unchanged_threshold,
                              bool* missed_receiving_unchanged_threshold) const;
  int CompareConnectionCandidates(const Connection* a, const Connection* b) const;
  // The connection states, the nomination states, the last data received time and static preferences; not latency.
  int CompareConnections(const Connection* a, const Connection* b,
                         std::optional<int64_t> receiving_unchanged_threshold,
                         bool* missed_receiving_unchanged_threshold) const;

  SwitchResult HandleInitialSelectDampening(Timestamp now, IceSwitchReason reason, const Connection* new_connection);

  std::function<IceTransportStateInternal()> ice_transport_state_func_;
  std::function<IceRole()> ice_role_func_;
  std::function<bool(const Connection*)> is_connection_pruned_func_;

  IceConfig config_;
  const IceFieldTrials* field_trials_;

  // `connections_` is a sorted list with the first one always the `selected_connection_` when it's not nullptr.
  // `pinged_connections_` and `unpinged_connections_` together hold the same connections as `connections_`.
  const Connection* selected_connection_ = nullptr;
  std::vector<const Connection*> connections_;
  std::set<const Connection*> pinged_connections_;
  std::set<const Connection*> unpinged_connections_;

  // Timestamp for when we got the first selectable connection.
  std::optional<Timestamp> initial_select_timestamp_;
};

}  // namespace sm::p2p
