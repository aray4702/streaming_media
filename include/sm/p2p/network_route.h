// libwebrtc M157's rtc_base/network_route.h, as P2PTransportChannel::ConfigureNetworkRoute
// (p2p/base/p2p_transport_channel.cc) fills it (#1002). The header itself is not in compare/webrtc/reference/
// (asked for on #57); the fields here are the ones the channel sets, with their meaning there.
#pragma once

#include <cstdint>

#include "sm/p2p/network.h"

namespace sm::p2p {

// One end of a route: its adapter, the network id the candidate carries, and whether it goes through TURN.
struct RouteEndpoint {
  RouteEndpoint() = default;
  RouteEndpoint(AdapterType type, uint16_t adapter, uint16_t network, bool turn)
      : adapter_type(type), adapter_id(adapter), network_id(network), uses_turn(turn) {}
  AdapterType adapter_type = ADAPTER_TYPE_UNKNOWN;
  uint16_t adapter_id = 0;
  uint16_t network_id = 0;
  bool uses_turn = false;
  friend bool operator==(const RouteEndpoint&, const RouteEndpoint&) = default;
};

struct NetworkRoute {
  bool connected = false;
  RouteEndpoint local;
  RouteEndpoint remote;
  // Last packet id sent on the previous route.
  int64_t last_sent_packet_id = -1;
  // The overhead in bytes from IP layer and above (IP header and transport header).
  int packet_overhead = 0;
  friend bool operator==(const NetworkRoute&, const NetworkRoute&) = default;
};

}  // namespace sm::p2p
