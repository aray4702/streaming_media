// libwebrtc M157's p2p/base/wrapping_active_ice_controller.cc (compare/webrtc/reference/p2p/base/); see
// sm/p2p/wrapping_active_ice_controller.h.
#include "sm/p2p/wrapping_active_ice_controller.h"

#include <algorithm>
#include <utility>

#include "sm/p2p/basic_ice_controller.h"

namespace sm::p2p {

WrappingActiveIceController::WrappingActiveIceController(IceAgentInterface* ice_agent,
                                                         std::unique_ptr<IceControllerInterface> wrapped)
    : wrapped_(std::move(wrapped)), agent_(*ice_agent) {}

WrappingActiveIceController::WrappingActiveIceController(IceAgentInterface* ice_agent,
                                                         IceControllerFactoryInterface* wrapped_factory,
                                                         const IceControllerFactoryArgs& wrapped_factory_args)
    : agent_(*ice_agent) {
  if (wrapped_factory) {
    wrapped_ = wrapped_factory->Create(wrapped_factory_args);
  } else {
    wrapped_ = std::make_unique<BasicIceController>(wrapped_factory_args);
  }
}

WrappingActiveIceController::~WrappingActiveIceController() = default;

void WrappingActiveIceController::PostDelayedTask(Timestamp now, TimeDelta delay,
                                                  std::function<void(Timestamp)> run) {
  tasks_.push_back(Task{now + std::max(delay, TimeDelta::Zero()), next_seq_++, std::move(run)});
}

void WrappingActiveIceController::Process(Timestamp now) {
  while (true) {
    auto it = std::min_element(tasks_.begin(), tasks_.end(), [](const Task& a, const Task& b) {
      return a.due != b.due ? a.due < b.due : a.seq < b.seq;
    });
    if (it == tasks_.end() || it->due > now) return;
    std::function<void(Timestamp)> run = std::move(it->run);
    tasks_.erase(it);
    run(now);
  }
}

Timestamp WrappingActiveIceController::NextProcessTime() const {
  Timestamp next = Timestamp::PlusInfinity();
  for (const Task& t : tasks_) next = std::min(next, t.due);
  return next;
}

void WrappingActiveIceController::SetIceConfig(const IceConfig& config) { wrapped_->SetIceConfig(config); }

bool WrappingActiveIceController::GetUseCandidateAttribute(const Connection* connection, NominationMode mode,
                                                           IceMode remote_ice_mode) const {
  return wrapped_->GetUseCandidateAttr(connection, mode, remote_ice_mode);
}

void WrappingActiveIceController::OnConnectionAdded(const Connection* connection) {
  wrapped_->AddConnection(connection);
}

void WrappingActiveIceController::OnConnectionPinged(const Connection* connection) {
  wrapped_->MarkConnectionPinged(connection);
}

void WrappingActiveIceController::OnConnectionUpdated(const Connection* /*connection*/) {
  // Do nothing. Native ICE controllers have direct access to Connection, so no need to update connection state
  // separately.
}

void WrappingActiveIceController::OnConnectionSwitched(const Connection* connection) {
  selected_connection_ = connection;
  wrapped_->SetSelectedConnection(connection);
}

void WrappingActiveIceController::OnConnectionDestroyed(const Connection* connection) {
  wrapped_->OnConnectionDestroyed(connection);
}

void WrappingActiveIceController::MaybeStartPinging(Timestamp now) {
  if (started_pinging_) return;
  if (wrapped_->HasPingableConnection(now)) {
    PostDelayedTask(now, TimeDelta::Zero(), [this](Timestamp t) { SelectAndPingConnection(t); });
    agent_.OnStartedPinging();
    started_pinging_ = true;
  }
}

void WrappingActiveIceController::SelectAndPingConnection(Timestamp now) {
  agent_.UpdateConnectionStates(now);
  IceControllerInterface::PingResult result = wrapped_->GetConnectionToPing(now, agent_.GetLastPingSent());
  HandlePingResult(now, result);
}

void WrappingActiveIceController::HandlePingResult(Timestamp now, IceControllerInterface::PingResult result) {
  if (result.connection.has_value()) agent_.SendPingRequest(now, result.connection.value());
  PostDelayedTask(now, TimeDelta::Millis(result.recheck_delay_ms),
                  [this](Timestamp t) { SelectAndPingConnection(t); });
}

void WrappingActiveIceController::OnSortAndSwitchRequest(Timestamp now, IceSwitchReason reason) {
  if (!sort_pending_) {
    PostDelayedTask(now, TimeDelta::Zero(), [this, reason](Timestamp t) { SortAndSwitchToBestConnection(t, reason); });
    sort_pending_ = true;
  }
}

void WrappingActiveIceController::OnImmediateSortAndSwitchRequest(Timestamp now, IceSwitchReason reason) {
  SortAndSwitchToBestConnection(now, reason);
}

void WrappingActiveIceController::SortAndSwitchToBestConnection(Timestamp now, IceSwitchReason reason) {
  // Make sure the connection states are up-to-date since this affects how they will be sorted.
  agent_.UpdateConnectionStates(now);
  // Any changes after this point will require a re-sort.
  sort_pending_ = false;
  IceControllerInterface::SwitchResult result = wrapped_->SortAndSwitchConnection(now, reason);
  HandleSwitchResult(now, reason, result);
  UpdateStateOnConnectionsResorted(now);
}

bool WrappingActiveIceController::OnImmediateSwitchRequest(Timestamp now, IceSwitchReason reason,
                                                           const Connection* selected) {
  IceControllerInterface::SwitchResult result = wrapped_->ShouldSwitchConnection(now, reason, selected);
  HandleSwitchResult(now, reason, result);
  return result.connection.has_value();
}

void WrappingActiveIceController::HandleSwitchResult(Timestamp now, IceSwitchReason reason_for_switch,
                                                     IceControllerInterface::SwitchResult result) {
  if (result.connection.has_value()) agent_.SwitchSelectedConnection(now, result.connection.value(), reason_for_switch);

  if (result.recheck_event.has_value()) {
    // If we do not switch to the connection because it missed the receiving threshold, the new connection is in a
    // better receiving state than the currently selected connection. So we need to re-check whether it needs to be
    // switched at a later time.
    PostDelayedTask(now, TimeDelta::Millis(result.recheck_event->recheck_delay_ms),
                    [this, recheck_reason = result.recheck_event->reason](Timestamp t) {
                      SortAndSwitchToBestConnection(t, recheck_reason);
                    });
  }

  agent_.ForgetLearnedStateForConnections(result.connections_to_forget_state_on);
}

void WrappingActiveIceController::UpdateStateOnConnectionsResorted(Timestamp now) {
  PruneConnections(now);
  // Update the internal state of the ICE agent.
  agent_.UpdateState(now);
  // Also possibly start pinging: the first connection was created, ICE credentials were provided, or a TCP connection
  // became connected.
  MaybeStartPinging(now);
}

void WrappingActiveIceController::PruneConnections(Timestamp now) {
  // The controlled side can prune only if the selected connection has been nominated because otherwise it may prune
  // the connection that will be selected by the controlling side.
  if (agent_.GetIceRole() == ICEROLE_CONTROLLING || (selected_connection_ && selected_connection_->nominated())) {
    std::vector<const Connection*> connections_to_prune = wrapped_->PruneConnections();
    agent_.PruneConnections(now, connections_to_prune);
  }
}

// Only for unit tests
const Connection* WrappingActiveIceController::FindNextPingableConnection(Timestamp now) {
  return wrapped_->FindNextPingableConnection(now);
}

}  // namespace sm::p2p
