// The ICE transport interface the DTLS transport runs over (#1006): the declarations of libwebrtc's
// p2p/base/ice_transport_internal.h at M157 (compare/webrtc/reference/). P2PTransportChannel implements it with
// #1002; until then the tests use a fake (tests/unit/p2p_dtls/fake_ice_transport.h, after p2p/test/
// fake_ice_transport.h).
//
// IceConfig carries libwebrtc's defaults (*_or_default) and IsValid since #1002's ICE controller part.
//
// Left out until P2PTransportChannel (#1002) brings their types: the candidate methods and signals (AddRemoteCandidate, gathering,
// candidate pairs, GetStats), the STUN dictionary, and the DTLS-in-STUN piggyback callbacks, which M157 has behind a
// field trial that is off by default (IceConfig::dtls_handshake_in_stun is kept, false).
#pragma once
#include <optional>
#include <string>

#include "sm/core/units.h"
#include "sm/p2p/network.h"
#include "sm/p2p/p2p_constants.h"
#include "sm/p2p/packet_transport_internal.h"
#include "sm/p2p/transport_description.h"
#include "sm/p2p/transport_enums.h"

namespace sm::p2p {

// IceTransportStateInternal: the state P2PTransportChannel computes itself.
enum class IceTransportStateInternal { kInit, kConnecting, kCompleted, kFailed };
// api/transport/enums.h's IceTransportState: the standard RTCIceTransportState.
enum class IceTransportState { kNew, kChecking, kConnected, kCompleted, kFailed, kDisconnected, kClosed };
enum class ContinualGatheringPolicy { kGatherOnce, kGatherContinually };

enum IceGatheringState { kIceGatheringNew = 0, kIceGatheringGathering, kIceGatheringComplete };

// ICE Nomination mode.
enum class NominationMode {
  REGULAR,         // Nominate once per ICE restart (Not implemented yet).
  AGGRESSIVE,      // Nominate every connection except that it will behave as if REGULAR when the remote is an
                   // ICE-LITE endpoint.
  SEMI_AGGRESSIVE  // Our current implementation of the nomination algorithm. The details are described in
                   // P2PTransportChannel.
};

// IceConfig (#1002): unset fields take the *_or_default values, libwebrtc's constants in sm/p2p/p2p_constants.h.
// The RTCConfiguration constructor comes with PeerConnection's allocator setup.
struct IceConfig {
  // The ICE connection receiving timeout value.
  std::optional<TimeDelta> receiving_timeout;
  // Time interval to ping a backup connection when the ICE channel is strongly connected.
  std::optional<TimeDelta> backup_connection_ping_interval;
  ContinualGatheringPolicy continual_gathering_policy = ContinualGatheringPolicy::kGatherOnce;
  bool gather_continually() const { return continual_gathering_policy == ContinualGatheringPolicy::kGatherContinually; }
  // Whether we should prioritize Relay/Relay candidate when nothing is writable yet.
  bool prioritize_most_likely_candidate_pairs = false;
  // Writable connections are pinged at a slower rate once stablized.
  std::optional<TimeDelta> stable_writable_connection_ping_interval;
  // Presume TURN-to-TURN candidate pairs will succeed, even before a binding response is received.
  bool presume_writable_when_fully_relayed = false;
  // Surface candidates gathered but filtered out when the candidate filter is widened.
  bool surface_ice_candidates_on_ice_transport_type_changed = false;
  // Interval to check on all networks and to perform ICE regathering on any active network having no connection.
  std::optional<TimeDelta> regather_on_failed_networks_interval;
  // The time period in which we will not switch the selected connection when a new connection becomes receiving but
  // the selected connection is not, in case the selected connection may become receiving soon.
  std::optional<TimeDelta> receiving_switching_delay;
  // Default nomination mode if the remote does not support renomination.
  NominationMode default_nomination_mode = NominationMode::SEMI_AGGRESSIVE;
  // The ping interval of a pair that is writable and receiving (strong connectivity); kStrongPingInterval if unset.
  std::optional<TimeDelta> ice_check_interval_strong_connectivity;
  // The ping interval of a pair that is not writable or not receiving (weak connectivity); kWeakPingInterval.
  std::optional<TimeDelta> ice_check_interval_weak_connectivity;
  // ICE checks are not sent at a higher rate (lower interval) than this, no matter what other settings there are.
  std::optional<TimeDelta> ice_check_min_interval;
  // The min time a pair waits for responses before it becomes unwritable (kConnectionWriteConnectTimeout).
  std::optional<TimeDelta> ice_unwritable_timeout;
  // The min number of unanswered checks before a pair becomes unwritable (kConnectionWriteConnectFailures).
  std::optional<int> ice_unwritable_min_checks;
  // The min time a pair waits for responses before it becomes inactive (kConnectionWriteTimeout).
  std::optional<TimeDelta> ice_inactive_timeout;
  // The interval at which STUN candidates resend STUN binding requests to keep NAT bindings open.
  std::optional<TimeDelta> stun_keepalive_interval;
  std::optional<AdapterType> network_preference;
  VpnPreference vpn_preference = VpnPreference::kDefault;
  // DTLS handshake in STUN (SPED): off, as M157's default; not ported (see the file comment).
  bool dtls_handshake_in_stun = false;

  // Checks that the values are consistent (IceConfig::IsValid); the reason if not.
  std::optional<std::string> IsValid() const;

  TimeDelta receiving_timeout_or_default() const { return receiving_timeout.value_or(kReceivingTimeout); }
  TimeDelta backup_connection_ping_interval_or_default() const {
    return backup_connection_ping_interval.value_or(kBackupConnectionPingInterval);
  }
  TimeDelta stable_writable_connection_ping_interval_or_default() const {
    return stable_writable_connection_ping_interval.value_or(kStrongAndStableWritableConnectionPingInterval);
  }
  TimeDelta regather_on_failed_networks_interval_or_default() const {
    return regather_on_failed_networks_interval.value_or(kRegatherOnFailedNetworksInterval);
  }
  TimeDelta receiving_switching_delay_or_default() const {
    return receiving_switching_delay.value_or(kReceivingSwitchingDelay);
  }
  TimeDelta ice_check_interval_strong_connectivity_or_default() const {
    return ice_check_interval_strong_connectivity.value_or(kStrongPingInterval);
  }
  TimeDelta ice_check_interval_weak_connectivity_or_default() const {
    return ice_check_interval_weak_connectivity.value_or(kWeakPingInterval);
  }
  TimeDelta ice_check_min_interval_or_default() const { return ice_check_min_interval.value_or(TimeDelta::Millis(-1)); }
  TimeDelta ice_unwritable_timeout_or_default() const {
    return ice_unwritable_timeout.value_or(kConnectionWriteConnectTimeout);
  }
  int ice_unwritable_min_checks_or_default() const {
    return ice_unwritable_min_checks.value_or(kConnectionWriteConnectFailures);
  }
  TimeDelta ice_inactive_timeout_or_default() const { return ice_inactive_timeout.value_or(kConnectionWriteTimeout); }
  TimeDelta stun_keepalive_interval_or_default() const {
    return stun_keepalive_interval.value_or(kStunKeepaliveInterval);
  }
};

class IceTransportInternal : public PacketTransportInternal {
 public:
  using IceCallback = std::function<void(IceTransportInternal*)>;

  virtual IceTransportStateInternal GetState() const = 0;
  virtual IceTransportState GetIceTransportState() const = 0;
  // The RTP (1) or RTCP (2) component.
  virtual int component() const = 0;
  virtual IceRole GetIceRole() const = 0;
  virtual void SetIceRole(IceRole role) = 0;
  // The local ICE credentials; a change starts an ICE restart.
  virtual void SetIceParameters(const IceParameters& ice_params) = 0;
  virtual void SetRemoteIceParameters(const IceParameters& ice_params) = 0;
  virtual void SetRemoteIceMode(IceMode mode) = 0;
  virtual void SetIceConfig(const IceConfig& config) = 0;
  virtual const IceConfig& config() const = 0;
  // Starts gathering candidates if not yet started, or after an ICE restart.
  virtual void MaybeStartGathering(Timestamp now) = 0;
  // The RTT of the selected connection, in milliseconds, if known.
  virtual std::optional<int> GetRttEstimate() = 0;

  void SubscribeIceTransportStateChanged(const void* tag, IceCallback cb) { state_changed_.Add(tag, std::move(cb)); }
  void UnsubscribeIceTransportStateChanged(const void* tag) { state_changed_.Remove(tag); }
  void NotifyIceTransportStateChanged(IceTransportInternal* t) { state_changed_.Send(t); }
  void SubscribeRoleConflict(const void* tag, IceCallback cb) { role_conflict_.Add(tag, std::move(cb)); }
  void NotifyRoleConflict(IceTransportInternal* t) { role_conflict_.Send(t); }

 private:
  TaggedCallbackList<IceTransportInternal*> state_changed_, role_conflict_;
};

}  // namespace sm::p2p
