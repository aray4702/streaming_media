// libwebrtc M157's p2p/base/p2p_transport_channel_ice_field_trials.h (compare/webrtc/reference/p2p/base/): the ICE
// field trials with their defaults. P2PTransportChannel (#1002) parses them; the connection (#1000) reads them.
#pragma once

#include <optional>

namespace sm::p2p {

struct IceFieldTrials {
  bool skip_relay_to_non_relay_connections = false;
  std::optional<int> max_outstanding_pings;
  // Wait X ms before selecting a connection when having none.
  std::optional<int> initial_select_dampening;
  // If the connection has received a ping request, delay by at most this.
  std::optional<int> initial_select_dampening_ping_received;
  // Announce GOOG_PING support in STUN_BINDING_RESPONSE if requested by the peer.
  bool announce_goog_ping = true;
  // Send GOOG_PING if the remote announces it.
  bool enable_goog_ping = false;
  // Halving time of the RTT estimate (EventBasedExponentialMovingAverage).
  int rtt_estimate_halftime_ms = 500;
  // Send a PING directly after a switch on the ICE_CONTROLLING side.
  bool send_ping_on_switch_ice_controlling = false;
  // Send a PING directly after selecting a connection (a switch or the initial selection).
  bool send_ping_on_selected_ice_controlling = false;
  // Send a PING directly after a nomination on the ICE_CONTROLLED side.
  bool send_ping_on_nomination_ice_controlled = false;
  // A connection that has received nothing for this long is dead.
  int dead_connection_timeout_ms = 30000;
  // Stop gathering when having a strong connection.
  bool stop_gather_on_strongly_connected = true;
  // DSCP tagging.
  std::optional<int> override_dscp;
  bool piggyback_ice_check_acknowledgement = false;
  bool extra_ice_ping = false;
  // Announce/enable GOOG_DELTA.
  bool enable_goog_delta = true;  // send GOOG DELTA
  bool answer_goog_delta = true;  // answer GOOG DELTA
};

}  // namespace sm::p2p
