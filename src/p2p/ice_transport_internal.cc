// libwebrtc M157's p2p/base/ice_transport_internal.cc (compare/webrtc/reference/p2p/base/): IceConfig::IsValid.
#include "sm/p2p/ice_transport_internal.h"

#include <algorithm>

namespace sm::p2p {

std::optional<std::string> IceConfig::IsValid() const {
  if (ice_check_interval_strong_connectivity_or_default() <
      ice_check_interval_weak_connectivity.value_or(kWeakPingInterval)) {
    return "Ping interval of candidate pairs is shorter when ICE is strongly connected than that when ICE is weakly "
           "connected";
  }
  if (receiving_timeout_or_default() <
      std::max(ice_check_interval_strong_connectivity_or_default(), ice_check_min_interval_or_default())) {
    return "Receiving timeout is shorter than the minimal ping interval.";
  }
  if (backup_connection_ping_interval_or_default() < ice_check_interval_strong_connectivity_or_default()) {
    return "Ping interval of backup candidate pairs is shorter than that of general candidate pairs when ICE is "
           "strongly connected";
  }
  if (stable_writable_connection_ping_interval_or_default() < ice_check_interval_strong_connectivity_or_default()) {
    return "Ping interval of stable and writable candidate pairs is shorter than that of general candidate pairs "
           "when ICE is strongly connected";
  }
  if (ice_unwritable_timeout_or_default() > ice_inactive_timeout_or_default()) {
    return "The timeout period for the writability state to become UNRELIABLE is longer than that to become TIMEOUT.";
  }
  return std::nullopt;
}

}  // namespace sm::p2p
