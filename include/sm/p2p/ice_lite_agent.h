// ICE-lite for the AI server (issue #1003, C3 part 7, ICE #990, milestone 3): libwebrtc M157's
// p2p/test/fake_ice_lite_agent.h (compare/webrtc/reference/p2p/test/), the active ICE controller with which libwebrtc
// runs a P2PTransportChannel as an ICE-lite agent (RFC 8445 section 2.5; `ICEMODE_LITE`). libwebrtc has no other
// lite agent: its DTLS-over-ICE integration fixture (p2p/dtls/dtls_ice_integration_fixture.h) plugs this controller
// into the controlled side through IceTransportInit::set_active_ice_controller_factory, and the full agent is told
// with SetRemoteIceMode(ICEMODE_LITE) (pc/jsep_transport.cc does that from the remote description's `a=ice-lite`).
//
// What the agent does, as libwebrtc's:
// - It never sends a check: it chooses no pair to ping (FindNextPingableConnection is null, so the channel never
//   starts pinging), and a ping reported to it is a bug (libwebrtc's RTC_CHECK; here a debug assert, and
//   pings_reported() counts them).
// - Every update of a connection (a check received, receiving, write state, nomination) is looked at once the call
//   that reported it returns (libwebrtc posts a task): a connection that is still there and receiving is made
//   writable (Connection::set_writable_for_fake_ice_lite: receiving, writable, candidate pair SUCCEEDED), since a lite
//   agent's pair is valid once a check from the peer arrived, and the channel's connection states are updated.
// - It never sorts or switches by itself. It switches only to a pair the full agent nominated
//   (NOMINATION_ON_CONTROLLED_SIDE) or that data arrived on (DATA_RECEIVED), then updates the connection states;
//   every other switch request is ignored. On a switch it updates the channel's state.
// - USE-CANDIDATE is never set (it sends no checks to set it on).
// The rest of the lite role is set up by the owner, as in the fixture: the controlled role, host candidates only
// (an allocator without STUN or TURN servers, or PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY), and
// the full agent's SetRemoteIceMode(ICEMODE_LITE), with which BasicIceController nominates (regular nomination
// against a lite peer, libwebrtc's GetUseCandidateAttr).
//
// The library's time model replaces the network thread: the task libwebrtc posts runs in Process(now) at
// NextProcessTime(), which is the time of the last call that carried one (the channel's switch and sort requests,
// which it makes before reporting the update, or Process). Destroying the agent drops the pending tasks, as
// ScopedTaskSafety does.
#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/active_ice_controller_interface.h"

namespace sm::p2p {

class IceLiteAgent : public ActiveIceControllerInterface {
 public:
  explicit IceLiteAgent(const ActiveIceControllerFactoryArgs& args) : args_(args) {}

  void SetIceConfig(const IceConfig&) override {}
  void OnConnectionAdded(const Connection*) override {}
  void OnConnectionSwitched(const Connection* connection) override;
  void OnConnectionDestroyed(const Connection* connection) override { connections_in_use_.erase(connection); }
  void OnConnectionPinged(const Connection* connection) override;
  void OnConnectionUpdated(const Connection* connection) override;
  bool GetUseCandidateAttribute(const Connection*, NominationMode, IceMode) const override { return false; }
  void OnSortAndSwitchRequest(Timestamp now, IceSwitchReason) override { Advance(now); }
  void OnImmediateSortAndSwitchRequest(Timestamp now, IceSwitchReason) override { Advance(now); }
  bool OnImmediateSwitchRequest(Timestamp now, IceSwitchReason reason, const Connection* selected) override;

  void Process(Timestamp now) override;
  Timestamp NextProcessTime() const override;

  // Only for unit tests
  const Connection* FindNextPingableConnection(Timestamp) override { return nullptr; }

  // Pings the channel reported (always 0 for a lite agent; libwebrtc's RTC_CHECK fails on the first).
  size_t pings_reported() const { return pings_reported_; }

 private:
  void Advance(Timestamp now) { now_ = std::max(now_, now); }
  void MarkConnectionInUse(const Connection* con) { connections_in_use_[con]++; }
  // Unmark a connection and see if it is still valid.
  bool UnmarkConnection(const Connection* con);

  ActiveIceControllerFactoryArgs args_;
  std::map<const Connection*, int> connections_in_use_;
  // The tasks OnConnectionUpdated posts, in order, due at the time they were posted.
  struct Task {
    Timestamp due;
    const Connection* connection;
  };
  std::vector<Task> tasks_;
  Timestamp now_ = Timestamp::MinusInfinity();
  size_t pings_reported_ = 0;
};

// FakeIceLiteAgentIceControllerFactory: pass it as P2PTransportChannel::Options::active_ice_controller_factory.
class IceLiteAgentFactory : public ActiveIceControllerFactoryInterface {
 public:
  std::unique_ptr<ActiveIceControllerInterface> Create(const ActiveIceControllerFactoryArgs& args) override {
    return std::make_unique<IceLiteAgent>(args);
  }
};

}  // namespace sm::p2p
