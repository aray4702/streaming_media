// The ICE controller interface (#1002, part of ICE #990): libwebrtc M157's p2p/base/ice_controller_interface.{h,cc}
// and ice_controller_factory_interface.h (compare/webrtc/reference/p2p/base/).
//
// An ICE controller decides which candidate pair (Connection) to ping, which to use, which to prune and which to
// forget the learned state of. P2PTransportChannel owns the connections and hands the controller const pointers; the
// controller asks for the mutations (Ping, Prune, ForgetLearnedState) through its results.
//
// The library's time model replaces libwebrtc's clock: the calls that read Environment's clock there
// (HasPingableConnection, GetConnectionToPing, FindNextPingableConnection, ShouldSwitchConnection,
// SortAndSwitchConnection) take `now`, and IceControllerFactoryArgs has no Environment.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/connection.h"
#include "sm/p2p/ice_switch_reason.h"
#include "sm/p2p/ice_transport_internal.h"
#include "sm/p2p/p2p_transport_channel_ice_field_trials.h"
#include "sm/p2p/transport_description.h"

namespace sm::p2p {

struct IceRecheckEvent {
  IceRecheckEvent(IceSwitchReason _reason, int _recheck_delay_ms)
      : reason(_reason), recheck_delay_ms(_recheck_delay_ms) {}
  std::string ToString() const;

  IceSwitchReason reason;
  int recheck_delay_ms;
};

class IceControllerInterface {
 public:
  // This represents the result of a switch call.
  struct SwitchResult {
    // Connection that we should (optionally) switch to.
    std::optional<const Connection*> connection;
    // An optional recheck event for when a Switch() should be attempted again.
    std::optional<IceRecheckEvent> recheck_event;
    // A vector with connection to run ForgetLearnedState on.
    std::vector<const Connection*> connections_to_forget_state_on;
  };

  // This represents the result of a call to GetConnectionToPing.
  struct PingResult {
    PingResult(const Connection* conn, int _recheck_delay_ms)
        : connection(conn ? std::optional<const Connection*>(conn) : std::nullopt),
          recheck_delay_ms(_recheck_delay_ms) {}
    PingResult(const Connection* conn, TimeDelta recheck_delay)
        : connection(conn ? std::optional<const Connection*>(conn) : std::nullopt),
          recheck_delay_ms(static_cast<int>(recheck_delay.ms())) {}

    TimeDelta recheck_delay() const { return TimeDelta::Millis(recheck_delay_ms); }

    // Connection that we should (optionally) ping.
    const std::optional<const Connection*> connection;
    // The delay before P2PTransportChannel shall call GetConnectionToPing() again.
    const int recheck_delay_ms = 0;
  };

  virtual ~IceControllerInterface() = default;

  // These setters are called when the state of P2PTransportChannel is mutated.
  virtual void SetIceConfig(const IceConfig& config) = 0;
  virtual void SetSelectedConnection(const Connection* selected_connection) = 0;
  virtual void AddConnection(const Connection* connection) = 0;
  virtual void OnConnectionDestroyed(const Connection* connection) = 0;

  // These are all connections that has been added and not destroyed.
  virtual std::span<const Connection* const> GetConnections() const = 0;

  // Is there a pingable connection? Boot-straps pinging: after this returns true GetConnectionToPing() is called
  // periodically.
  virtual bool HasPingableConnection(Timestamp now) const = 0;

  // Selects a connection to Ping, or nullptr if none.
  virtual PingResult GetConnectionToPing(Timestamp now, Timestamp last_ping_sent) = 0;

  // Compute the "STUN_ATTR_USE_CANDIDATE" for `conn`.
  virtual bool GetUseCandidateAttr(const Connection* conn, NominationMode mode, IceMode remote_ice_mode) const = 0;

  // Only to let unit tests simulate pinging by marking a connection pinged.
  virtual const Connection* FindNextPingableConnection(Timestamp now) = 0;
  virtual void MarkConnectionPinged(const Connection* con) = 0;

  // Check if we should switch to `connection`, for IceSwitchReasons that can switch directly i.e without resorting.
  virtual SwitchResult ShouldSwitchConnection(Timestamp now, IceSwitchReason reason, const Connection* connection) = 0;

  // Sort connections and check if we should switch.
  virtual SwitchResult SortAndSwitchConnection(Timestamp now, IceSwitchReason reason) = 0;

  // Prune connections.
  virtual std::vector<const Connection*> PruneConnections() = 0;
};

// Arguments to IceControllerFactoryInterface::Create.
struct IceControllerFactoryArgs {
  std::function<IceTransportStateInternal()> ice_transport_state_func;
  std::function<IceRole()> ice_role_func;
  std::function<bool(const Connection*)> is_connection_pruned_func;
  const IceFieldTrials* ice_field_trials = nullptr;
  std::string ice_controller_field_trials;
};

class IceControllerFactoryInterface {
 public:
  virtual ~IceControllerFactoryInterface() = default;
  virtual std::unique_ptr<IceControllerInterface> Create(const IceControllerFactoryArgs& args) = 0;
};

}  // namespace sm::p2p
