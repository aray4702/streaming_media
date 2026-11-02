// libwebrtc M157's p2p/base/ice_switch_reason.{h,cc} (compare/webrtc/reference/p2p/base/): why the ICE transport
// re-sorts its candidate pairs or switches the selected one (#1002).
#pragma once

#include <string>

namespace sm::p2p {

enum class IceSwitchReason {
  UNKNOWN,
  REMOTE_CANDIDATE_GENERATION_CHANGE,
  NETWORK_PREFERENCE_CHANGE,
  NEW_CONNECTION_FROM_LOCAL_CANDIDATE,
  NEW_CONNECTION_FROM_REMOTE_CANDIDATE,
  NEW_CONNECTION_FROM_UNKNOWN_REMOTE_ADDRESS,
  NOMINATION_ON_CONTROLLED_SIDE,
  DATA_RECEIVED,
  CONNECT_STATE_CHANGE,
  SELECTED_CONNECTION_DESTROYED,
  // Lets an IceController request P2PTransportChannel to recheck a switch periodically without an event taking place.
  ICE_CONTROLLER_RECHECK,
  // The webrtc application requested a connection switch.
  APPLICATION_REQUESTED,
};

std::string IceSwitchReasonToString(IceSwitchReason reason);

}  // namespace sm::p2p
