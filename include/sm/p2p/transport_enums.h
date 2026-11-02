// libwebrtc M157's api/transport/enums.h (compare/webrtc/reference/api/transport/): the port prune policy and the VPN
// preference that the port allocator (#1001) and the ICE controller (#1002) share.
#pragma once

namespace sm::p2p {

enum PortPrunePolicy {
  NO_PRUNE,                 // Do not prune.
  PRUNE_BASED_ON_PRIORITY,  // Prune lower-priority ports on the same network.
  KEEP_FIRST_READY          // Keep the first ready port and prune the rest on the same network.
};

enum class VpnPreference {
  kDefault,      // No VPN preference.
  kOnlyUseVpn,   // only use VPN connections.
  kNeverUseVpn,  // never use VPN connections
  kPreferVpn,    // use a VPN connection if possible, i.e VPN connections sorts first.
  kAvoidVpn,     // only use VPN if there is no other connections, i.e VPN connections sorts last.
};

}  // namespace sm::p2p
