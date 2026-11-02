// libwebrtc M157's p2p/base/wrapping_active_ice_controller.{h,cc} (compare/webrtc/reference/p2p/base/): a legacy
// (passive) ICE controller, BasicIceController by default, packaged as an active one (#1002). P2PTransportChannel uses
// it: it pings the pair the wrapped controller picks every recheck delay once a pair is pingable, sorts and switches
// on request (at once, or as the next task), rechecks after the delay a switch result asks for, prunes after each
// sort (the controlled side only once its selected pair is nominated), and forgets the learned state it is told to.
//
// The library's time model replaces the network thread: PostTask and PostDelayedTask queue the task with its due
// time, and Process(now) runs the due ones in order (a task posted while it runs runs in the same call if it is due).
// Destroying the controller drops its tasks, as libwebrtc's ScopedTaskSafety does.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/active_ice_controller_interface.h"
#include "sm/p2p/ice_agent_interface.h"
#include "sm/p2p/ice_controller_interface.h"

namespace sm::p2p {

class WrappingActiveIceController : public ActiveIceControllerInterface {
 public:
  // Wraps an already constructed legacy ICE controller. Does not take ownership of the ICE agent, which must outlive
  // the ICE controller.
  WrappingActiveIceController(IceAgentInterface* ice_agent, std::unique_ptr<IceControllerInterface> wrapped);
  // Wraps a legacy ICE controller made by `wrapped_factory`, or a BasicIceController without one.
  WrappingActiveIceController(IceAgentInterface* ice_agent, IceControllerFactoryInterface* wrapped_factory,
                              const IceControllerFactoryArgs& wrapped_factory_args);
  ~WrappingActiveIceController() override;

  void SetIceConfig(const IceConfig& config) override;
  bool GetUseCandidateAttribute(const Connection* connection, NominationMode mode,
                                IceMode remote_ice_mode) const override;

  void OnConnectionAdded(const Connection* connection) override;
  void OnConnectionPinged(const Connection* connection) override;
  void OnConnectionUpdated(const Connection* connection) override;
  void OnConnectionSwitched(const Connection* connection) override;
  void OnConnectionDestroyed(const Connection* connection) override;

  void OnSortAndSwitchRequest(Timestamp now, IceSwitchReason reason) override;
  void OnImmediateSortAndSwitchRequest(Timestamp now, IceSwitchReason reason) override;
  bool OnImmediateSwitchRequest(Timestamp now, IceSwitchReason reason, const Connection* selected) override;

  void Process(Timestamp now) override;
  Timestamp NextProcessTime() const override;

  // Only for unit tests
  const Connection* FindNextPingableConnection(Timestamp now) override;

 private:
  struct Task {
    Timestamp due;
    uint64_t seq;
    std::function<void(Timestamp)> run;
  };
  void PostDelayedTask(Timestamp now, TimeDelta delay, std::function<void(Timestamp)> run);

  void MaybeStartPinging(Timestamp now);
  void SelectAndPingConnection(Timestamp now);
  void HandlePingResult(Timestamp now, IceControllerInterface::PingResult result);

  void SortAndSwitchToBestConnection(Timestamp now, IceSwitchReason reason);
  void HandleSwitchResult(Timestamp now, IceSwitchReason reason_for_switch,
                          IceControllerInterface::SwitchResult result);
  void UpdateStateOnConnectionsResorted(Timestamp now);

  void PruneConnections(Timestamp now);

  bool started_pinging_ = false;
  bool sort_pending_ = false;
  const Connection* selected_connection_ = nullptr;

  std::unique_ptr<IceControllerInterface> wrapped_;
  IceAgentInterface& agent_;

  std::vector<Task> tasks_;
  uint64_t next_seq_ = 0;
};

}  // namespace sm::p2p
