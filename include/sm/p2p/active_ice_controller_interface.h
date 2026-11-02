// libwebrtc M157's p2p/base/active_ice_controller_interface.h and active_ice_controller_factory_interface.h
// (compare/webrtc/reference/p2p/base/): a module that actively manages the connection an ICE transport uses (#1002).
//
// The library's time model: what libwebrtc posts to the network thread runs in Process(now), at NextProcessTime(), and
// the requests that post or switch take `now`.
#pragma once

#include <memory>

#include "sm/core/units.h"
#include "sm/p2p/connection.h"
#include "sm/p2p/ice_agent_interface.h"
#include "sm/p2p/ice_controller_interface.h"
#include "sm/p2p/ice_switch_reason.h"
#include "sm/p2p/ice_transport_internal.h"

namespace sm::p2p {

class ActiveIceControllerInterface {
 public:
  virtual ~ActiveIceControllerInterface() = default;

  // Sets the current ICE configuration.
  virtual void SetIceConfig(const IceConfig& config) = 0;
  // Called when a new connection is added to the ICE transport.
  virtual void OnConnectionAdded(const Connection* connection) = 0;
  // Called when the transport switches that connection in active use.
  virtual void OnConnectionSwitched(const Connection* connection) = 0;
  // Called when a connection is destroyed.
  virtual void OnConnectionDestroyed(const Connection* connection) = 0;
  // Called when a STUN ping has been sent on a connection (not that a response has been received).
  virtual void OnConnectionPinged(const Connection* connection) = 0;
  // Called when the rtt estimate, write state, receiving, connected or nominated of a connection changes.
  virtual void OnConnectionUpdated(const Connection* connection) = 0;
  // Compute "STUN_ATTR_USE_CANDIDATE" for a STUN ping on the given connection.
  virtual bool GetUseCandidateAttribute(const Connection* connection, NominationMode mode,
                                        IceMode remote_ice_mode) const = 0;
  // Called to enque a request to pick and switch to the best available connection.
  virtual void OnSortAndSwitchRequest(Timestamp now, IceSwitchReason reason) = 0;
  // Called to pick and switch to the best available connection immediately.
  virtual void OnImmediateSortAndSwitchRequest(Timestamp now, IceSwitchReason reason) = 0;
  // Called to switch to the given connection immediately without checking for the best available connection.
  virtual bool OnImmediateSwitchRequest(Timestamp now, IceSwitchReason reason, const Connection* selected) = 0;

  // Runs the tasks due at `now` (libwebrtc posts them to the network thread).
  virtual void Process(Timestamp now) = 0;
  // When Process should next run; PlusInfinity if nothing is pending.
  virtual Timestamp NextProcessTime() const = 0;

  // Only for unit tests
  virtual const Connection* FindNextPingableConnection(Timestamp now) = 0;
};

// An active ICE controller may be constructed with the same arguments as a legacy ICE controller, plus the ICE agent
// it interacts with.
struct ActiveIceControllerFactoryArgs {
  IceControllerFactoryArgs legacy_args;
  IceAgentInterface* ice_agent = nullptr;
};

class ActiveIceControllerFactoryInterface {
 public:
  virtual ~ActiveIceControllerFactoryInterface() = default;
  virtual std::unique_ptr<ActiveIceControllerInterface> Create(const ActiveIceControllerFactoryArgs&) = 0;
};

}  // namespace sm::p2p
