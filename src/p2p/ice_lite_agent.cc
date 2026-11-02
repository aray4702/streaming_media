// libwebrtc M157's p2p/test/fake_ice_lite_agent.h (compare/webrtc/reference/p2p/test/); see sm/p2p/ice_lite_agent.h.
#include "sm/p2p/ice_lite_agent.h"

#include <algorithm>
#include <cassert>

#include "sm/p2p/connection.h"

namespace sm::p2p {

void IceLiteAgent::OnConnectionSwitched(const Connection*) { args_.ice_agent->UpdateState(now_); }

void IceLiteAgent::OnConnectionPinged(const Connection*) {
  // "We never send any STUN_BINDING_REQUEST !!"
  ++pings_reported_;
  assert(false && "an ICE-lite agent never sends a STUN binding request");
}

void IceLiteAgent::OnConnectionUpdated(const Connection* connection) {
  // NOTE: we don't know which field has been updated...so we need to do this every time.
  MarkConnectionInUse(connection);
  tasks_.push_back(Task{now_, connection});
}

bool IceLiteAgent::OnImmediateSwitchRequest(Timestamp now, IceSwitchReason reason, const Connection* selected) {
  Advance(now);
  switch (reason) {
    case IceSwitchReason::NOMINATION_ON_CONTROLLED_SIDE:
    case IceSwitchReason::DATA_RECEIVED:
      if (selected) {
        args_.ice_agent->SwitchSelectedConnection(now, selected, reason);
        args_.ice_agent->UpdateConnectionStates(now);
        return true;
      }
      break;
    default:
      // All the other can happen, which is ok: we simply ignore them.
      break;
  }
  return false;
}

bool IceLiteAgent::UnmarkConnection(const Connection* con) {
  auto c = connections_in_use_.find(con);
  if (c == connections_in_use_.end()) return false;
  if (c->second == 1) connections_in_use_.erase(c);
  return true;
}

void IceLiteAgent::Process(Timestamp now) {
  Advance(now);
  // Tasks posted while these run (an update reported by UpdateConnectionStates) run in the same call, as they are
  // due at once.
  while (!tasks_.empty() && tasks_.front().due <= now) {
    const Connection* connection = tasks_.front().connection;
    tasks_.erase(tasks_.begin());
    if (UnmarkConnection(connection)) {
      if (connection->receiving()) {
        if (const_cast<Connection*>(connection)->set_writable_for_fake_ice_lite(now)) {
          args_.ice_agent->UpdateConnectionStates(now);
        }
      }
    }
  }
}

Timestamp IceLiteAgent::NextProcessTime() const {
  return tasks_.empty() ? Timestamp::PlusInfinity() : tasks_.front().due;
}

}  // namespace sm::p2p
