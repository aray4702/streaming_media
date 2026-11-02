// libwebrtc M157's p2p/base/ice_transport_internal.cc (compare/webrtc/reference/p2p/base/): IceConfig::IsValid and
// IceConfig(const RTCConfiguration&) (#1002 part 3).
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

namespace {

// RTCConfiguration uses kUndefined (-1) to indicate unset optional parameters.
std::optional<TimeDelta> RtcConfigurationToIceConfigOptionalMillis(int rtc_configuration_parameter) {
  if (rtc_configuration_parameter == RtcIceConfiguration::kUndefined) return std::nullopt;
  return TimeDelta::Millis(rtc_configuration_parameter);
}

std::optional<TimeDelta> ToOptionalMillis(std::optional<int> ms) {
  if (!ms) return std::nullopt;
  return TimeDelta::Millis(*ms);
}

}  // namespace

IceConfig IceConfigFromRtcConfiguration(const RtcIceConfiguration& config) {
  IceConfig c;
  c.receiving_timeout = RtcConfigurationToIceConfigOptionalMillis(config.ice_connection_receiving_timeout);
  c.backup_connection_ping_interval =
      RtcConfigurationToIceConfigOptionalMillis(config.ice_backup_candidate_pair_ping_interval);
  c.continual_gathering_policy = config.continual_gathering_policy;
  c.prioritize_most_likely_candidate_pairs = config.prioritize_most_likely_ice_candidate_pairs;
  c.stable_writable_connection_ping_interval = ToOptionalMillis(config.stable_writable_connection_ping_interval_ms);
  c.presume_writable_when_fully_relayed = config.presume_writable_when_fully_relayed;
  c.surface_ice_candidates_on_ice_transport_type_changed = config.surface_ice_candidates_on_ice_transport_type_changed;
  c.ice_check_interval_strong_connectivity = ToOptionalMillis(config.ice_check_interval_strong_connectivity);
  c.ice_check_interval_weak_connectivity = ToOptionalMillis(config.ice_check_interval_weak_connectivity);
  c.ice_check_min_interval = ToOptionalMillis(config.ice_check_min_interval);
  c.ice_unwritable_timeout = ToOptionalMillis(config.ice_unwritable_timeout);
  c.ice_unwritable_min_checks = config.ice_unwritable_min_checks;
  c.ice_inactive_timeout = ToOptionalMillis(config.ice_inactive_timeout);
  c.stun_keepalive_interval = ToOptionalMillis(config.stun_candidate_keepalive_interval);
  c.network_preference = config.network_preference;
  return c;
}

}  // namespace sm::p2p
