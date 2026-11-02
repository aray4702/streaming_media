// The ICE transport interface the DTLS transport runs over (#1006): the declarations of libwebrtc's
// p2p/base/ice_transport_internal.h at M157 (compare/webrtc/reference/). P2PTransportChannel implements it with
// #1002; until then the tests use a fake (tests/unit/p2p_dtls/fake_ice_transport.h, after p2p/test/
// fake_ice_transport.h).
//
// Left out until #1002 brings their types: the candidate methods and signals (AddRemoteCandidate, gathering,
// candidate pairs, GetStats), the STUN dictionary, and the DTLS-in-STUN piggyback callbacks, which M157 has behind a
// field trial that is off by default (IceConfig::dtls_handshake_in_stun is kept, false).
#pragma once
#include <optional>
#include <string>

#include "sm/core/units.h"
#include "sm/p2p/packet_transport_internal.h"

namespace sm::p2p {

// IceTransportStateInternal: the state P2PTransportChannel computes itself.
enum class IceTransportStateInternal { kInit, kConnecting, kCompleted, kFailed };
// api/transport/enums.h's IceTransportState: the standard RTCIceTransportState.
enum class IceTransportState { kNew, kChecking, kConnected, kCompleted, kFailed, kDisconnected, kClosed };
// p2p/base/transport_description.h.
enum class IceRole { kControlling, kControlled, kUnknown };
enum class IceMode { kFull, kLite };
struct IceParameters {
  std::string ufrag;
  std::string pwd;
  bool renomination = false;
};
enum class ContinualGatheringPolicy { kGatherOnce, kGatherContinually };

// IceConfig: unset fields take P2PTransportChannel's defaults (#1002 ports the *_or_default accessors).
struct IceConfig {
  std::optional<TimeDelta> receiving_timeout;
  std::optional<TimeDelta> backup_connection_ping_interval;
  ContinualGatheringPolicy continual_gathering_policy = ContinualGatheringPolicy::kGatherOnce;
  bool prioritize_most_likely_candidate_pairs = false;
  std::optional<TimeDelta> stable_writable_connection_ping_interval;
  bool presume_writable_when_fully_relayed = false;
  std::optional<TimeDelta> regather_on_failed_networks_interval;
  std::optional<TimeDelta> receiving_switching_delay;
  std::optional<TimeDelta> ice_check_interval_strong_connectivity;
  std::optional<TimeDelta> ice_check_interval_weak_connectivity;
  std::optional<TimeDelta> ice_check_min_interval;
  std::optional<TimeDelta> ice_unwritable_timeout;
  std::optional<int> ice_unwritable_min_checks;
  std::optional<TimeDelta> ice_inactive_timeout;
  std::optional<TimeDelta> stun_keepalive_interval;
  // DTLS handshake in STUN (SPED): off, as M157's default; not ported (see the file comment).
  bool dtls_handshake_in_stun = false;
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
