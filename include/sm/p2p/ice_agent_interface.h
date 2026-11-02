// libwebrtc M157's p2p/base/ice_agent_interface.h (compare/webrtc/reference/p2p/base/): what an active ICE controller
// asks of the ICE transport (P2PTransportChannel implements it, #1002). Calls that send, switch or update state take
// `now` (the library's time model).
#pragma once

#include <span>

#include "sm/core/units.h"
#include "sm/p2p/connection.h"
#include "sm/p2p/ice_switch_reason.h"
#include "sm/p2p/transport_description.h"

namespace sm::p2p {

class IceAgentInterface {
 public:
  virtual ~IceAgentInterface() = default;

  // Get the time when the last ping was sent.
  virtual Timestamp GetLastPingSent() const = 0;
  // Get the ICE role of this ICE agent.
  virtual IceRole GetIceRole() const = 0;
  // Called when a pingable connection first becomes available.
  virtual void OnStartedPinging() = 0;
  // Update the state of all available connections.
  virtual void UpdateConnectionStates(Timestamp now) = 0;
  // Update the internal state of the ICE agent, at the end of a sequence of actions.
  virtual void UpdateState(Timestamp now) = 0;
  // Reset the given connections to a state of newly connected connections (STATE_WRITE_INIT, not receiving, no pending
  // requests, a reset RTT estimate), keeping connected, the remote candidate and the statistics. SignalStateChange is
  // not triggered.
  virtual void ForgetLearnedStateForConnections(std::span<const Connection* const> connections) = 0;
  // Send a STUN ping request for the given connection.
  virtual void SendPingRequest(Timestamp now, const Connection* connection) = 0;
  // Switch the transport to use the given connection.
  virtual void SwitchSelectedConnection(Timestamp now, const Connection* new_connection, IceSwitchReason reason) = 0;
  // Prune away the given connections. Returns true if pruning is permitted and successfully performed.
  virtual bool PruneConnections(Timestamp now, std::span<const Connection* const> connections) = 0;
};

}  // namespace sm::p2p
