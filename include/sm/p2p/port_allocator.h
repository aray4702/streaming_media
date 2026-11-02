// The port allocator interface (issue #1001, part of ICE #990, milestone 3):
// libwebrtc M157's p2p/base/port_allocator.{h,cc} (compare/webrtc/reference/
// p2p/base/).
//
// A PortAllocator holds the ICE configuration shared by the transports of a
// call (STUN and TURN servers, the flags, the port range, the step delay,
// the candidate filter, the ICE tie-breaker) and makes a
// PortAllocatorSession per transport component; the session gathers ports
// and candidates and signals them. A pool of sessions can start gathering
// before the transports exist (candidate_pool_size): TakePooledSession hands
// one over with the transport's ICE credentials. Same constants and defaults
// as libwebrtc: flags 0, the step delay 1 s (kDefaultStepDelay), at most 5
// IPv6 networks, the candidate filter CF_ALL, no port range.
//
// Differences from libwebrtc:
// - No thread checks: the library's calls run on the caller's thread.
// - No mDNS obfuscation (MdnsObfuscationEnabled is false), so
//   SanitizeCandidate only removes related addresses, as libwebrtc without an
//   mDNS responder; no TurnCustomizer, TLS certificate verifier or TURN
//   logging id (C4).
// - PortPrunePolicy and VpnPreference are api/transport/enums.h's
//   (sm/p2p/transport_enums.h, shared with the ICE controller).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/candidate.h"
#include "sm/p2p/port.h"
#include "sm/p2p/socket_address.h"
#include "sm/p2p/transport_description.h"
#include "sm/p2p/transport_enums.h"

namespace sm::p2p {

// PortAllocator::flags().
enum : uint32_t {
  PORTALLOCATOR_DISABLE_UDP = 0x01,
  PORTALLOCATOR_DISABLE_STUN = 0x02,
  PORTALLOCATOR_DISABLE_RELAY = 0x04,
  PORTALLOCATOR_DISABLE_TCP = 0x08,
  PORTALLOCATOR_ENABLE_IPV6 = 0x40,
  PORTALLOCATOR_ENABLE_SHARED_SOCKET = 0x100,
  PORTALLOCATOR_ENABLE_STUN_RETRANSMIT_ATTRIBUTE = 0x200,
  // Bind to the any address instead of each adapter's (no local IPs leak).
  PORTALLOCATOR_DISABLE_ADAPTER_ENUMERATION = 0x400,
  // With adapter enumeration off, no host candidate for the default route.
  PORTALLOCATOR_DISABLE_DEFAULT_LOCAL_CANDIDATE = 0x800,
  PORTALLOCATOR_DISABLE_UDP_RELAY = 0x1000,
  // Only the lowest-cost networks (within kNetworkCostLow).
  PORTALLOCATOR_DISABLE_COSTLY_NETWORKS = 0x2000,
  PORTALLOCATOR_ENABLE_IPV6_ON_WIFI = 0x4000,
  // Also gather on the any address when adapters are enumerated.
  PORTALLOCATOR_ENABLE_ANY_ADDRESS_PORTS = 0x8000,
  PORTALLOCATOR_DISABLE_LINK_LOCAL_NETWORKS = 0x10000,
};

enum class IceRegatheringReason { NETWORK_CHANGE, NETWORK_FAILURE, OCCASIONAL_REFRESH, MAX_VALUE };

inline constexpr uint32_t kDefaultPortAllocatorFlags = 0;
inline constexpr uint32_t kDefaultStepDelay = 1000;  // ms
inline constexpr uint32_t kMinimumStepDelay = 50;    // ms
inline constexpr int kDefaultMaxIPv6Networks = 5;

// Candidate filter: which candidate types are signalled.
enum : uint32_t {
  CF_NONE = 0x0,
  CF_HOST = 0x1,
  CF_REFLEXIVE = 0x2,
  CF_RELAY = 0x4,
  CF_ALL = 0x7,
};

enum class TlsCertPolicy { TLS_CERT_POLICY_SECURE, TLS_CERT_POLICY_INSECURE_NO_CHECK };

// p2p/base/port.h.
struct ProtocolAddress {
  SocketAddress address;
  ProtocolType proto;
  ProtocolAddress(const SocketAddress& a, ProtocolType p) : address(a), proto(p) {}
  friend bool operator==(const ProtocolAddress& a, const ProtocolAddress& b) {
    return a.address == b.address && a.proto == b.proto;
  }
};
using PortList = std::vector<ProtocolAddress>;

struct RelayCredentials {
  RelayCredentials() = default;
  RelayCredentials(std::string_view u, std::string_view p) : username(u), password(p) {}
  friend bool operator==(const RelayCredentials&, const RelayCredentials&) = default;
  std::string username;
  std::string password;
};

// A TURN server (C4 makes the ports; here it is configuration).
struct RelayServerConfig {
  RelayServerConfig() = default;
  RelayServerConfig(const SocketAddress& address, std::string_view username, std::string_view password,
                    ProtocolType proto)
      : credentials(username, password) {
    ports.emplace_back(address, proto);
  }
  // Legacy: PROTO_TCP and `secure` mean PROTO_TLS.
  RelayServerConfig(const SocketAddress& address, std::string_view username, std::string_view password,
                    ProtocolType proto, bool secure)
      : RelayServerConfig(address, username, password, proto == PROTO_TCP && secure ? PROTO_TLS : proto) {}
  friend bool operator==(const RelayServerConfig& a, const RelayServerConfig& b) {
    return a.ports == b.ports && a.credentials == b.credentials;
  }
  PortList ports;
  RelayCredentials credentials;
  TlsCertPolicy tls_cert_policy = TlsCertPolicy::TLS_CERT_POLICY_SECURE;
  std::vector<std::string> tls_alpn_protocols;
  std::vector<std::string> tls_elliptic_curves;
};

// IceCredentialsIterator::CreateRandomIceCredentials: a 4-character ufrag
// and a 24-character password (ICE_UFRAG_LENGTH, ICE_PWD_LENGTH).
IceParameters CreateRandomIceCredentials();
// rtc_base/crypto_random CreateRandomId64, from the STUN random hook (a
// fixed-seed generator without it: tests only).
uint64_t CreateRandomId64();

template <class... Args>
using AllocatorCallbacks = PortCallbacks<Args...>;

class PortAllocatorSession {
 public:
  PortAllocatorSession(std::string_view content_name, int component, std::string_view ice_ufrag,
                       std::string_view ice_pwd, uint32_t flags);
  virtual ~PortAllocatorSession();
  PortAllocatorSession(const PortAllocatorSession&) = delete;
  PortAllocatorSession& operator=(const PortAllocatorSession&) = delete;

  uint32_t flags() const { return flags_; }
  void set_flags(uint32_t flags) { flags_ = flags; }
  const std::string& content_name() const { return content_name_; }
  int component() const { return component_; }
  const std::string& ice_ufrag() const { return ice_ufrag_; }
  const std::string& ice_pwd() const { return ice_pwd_; }
  bool pooled() const { return pooled_; }

  // Signals only the candidate types the filter allows; candidates held back
  // by an earlier filter surface when a new one allows them.
  virtual void SetCandidateFilter(uint32_t filter) = 0;
  // Starts gathering. The session runs on Process(now) (see the subclass).
  virtual void StartGettingPorts(Timestamp now) = 0;
  // Stops gathering for good; ports already made stay.
  virtual void StopGettingPorts(Timestamp now) = 0;
  virtual bool IsGettingPorts() = 0;
  // Stops the current gathering; continual gathering may start another.
  virtual void ClearGettingPorts(Timestamp now) = 0;
  virtual bool IsCleared() const { return false; }
  virtual bool IsStopped() const { return false; }
  virtual void RegatherOnFailedNetworks(Timestamp /*now*/) {}
  virtual void SetStunKeepaliveIntervalForReadyPorts(const std::optional<TimeDelta>& /*interval*/) {}
  // The ports that have a pairable candidate and are neither failed nor
  // pruned, and their (filtered, sanitized) candidates.
  virtual std::vector<Port*> ReadyPorts() const = 0;
  virtual std::vector<Candidate> ReadyCandidates() const = 0;
  virtual bool CandidatesAllocationDone() const = 0;
  virtual void PruneAllPorts(Timestamp /*now*/) {}

  // The session's time model (no network thread): Process runs what is due.
  virtual void Process(Timestamp now) = 0;
  virtual Timestamp NextProcessTime() const = 0;

  virtual uint32_t generation() { return generation_; }
  virtual void set_generation(uint32_t generation) { generation_ = generation; }

  // Signals.
  void SubscribePortReady(const void* tag, std::function<void(PortAllocatorSession*, Port*)> cb) {
    port_ready_.Add(tag, std::move(cb));
  }
  void SubscribePortsPruned(const void* tag,
                            std::function<void(PortAllocatorSession*, const std::vector<Port*>&)> cb) {
    ports_pruned_.Add(tag, std::move(cb));
  }
  void SubscribeCandidatesReady(const void* tag,
                                std::function<void(PortAllocatorSession*, const std::vector<Candidate>&)> cb) {
    candidates_ready_.Add(tag, std::move(cb));
  }
  void SubscribeCandidateError(const void* tag,
                               std::function<void(PortAllocatorSession*, const IceCandidateErrorEvent&)> cb) {
    candidate_error_.Add(tag, std::move(cb));
  }
  void SubscribeCandidatesRemoved(const void* tag,
                                  std::function<void(PortAllocatorSession*, const std::vector<Candidate>&)> cb) {
    candidates_removed_.Add(tag, std::move(cb));
  }
  void SubscribeCandidatesAllocationDone(const void* tag, std::function<void(PortAllocatorSession*)> cb) {
    candidates_allocation_done_.Add(tag, std::move(cb));
  }
  void SubscribeIceRegathering(const void* tag,
                               std::function<void(PortAllocatorSession*, IceRegatheringReason)> cb) {
    ice_regathering_.Add(tag, std::move(cb));
  }

  // Public as in libwebrtc (P2PTransportChannelPingTest.TestIceRoleUpdatedOnRemovedPort raises it).
  void NotifyPortsPruned(const std::vector<Port*>& ports) { ports_pruned_.Send(this, ports); }

 protected:
  void NotifyPortReady(Port* port) { port_ready_.Send(this, port); }
  void NotifyCandidatesReady(const std::vector<Candidate>& c) { candidates_ready_.Send(this, c); }
  void NotifyCandidateError(const IceCandidateErrorEvent& e) { candidate_error_.Send(this, e); }
  void NotifyCandidatesRemoved(const std::vector<Candidate>& c) { candidates_removed_.Send(this, c); }
  void NotifyCandidatesAllocationDone() { candidates_allocation_done_.Send(this); }
  void NotifyIceRegathering(IceRegatheringReason reason) { ice_regathering_.Send(this, reason); }

  // A pooled session's ports take the transport's credentials.
  virtual void UpdateIceParametersInternal() {}
  const std::string& username() const { return ice_ufrag_; }
  const std::string& password() const { return ice_pwd_; }

 private:
  friend class PortAllocator;
  void SetIceParameters(std::string_view content_name, int component, std::string_view ice_ufrag,
                        std::string_view ice_pwd);
  void set_pooled(bool value) {
    pooled_ = value;
    if (pooled_) content_name_.clear();
  }

  uint32_t flags_;
  uint32_t generation_ = 0;
  std::string content_name_;
  int component_;
  std::string ice_ufrag_;
  std::string ice_pwd_;
  bool pooled_ = false;

  AllocatorCallbacks<PortAllocatorSession*, Port*> port_ready_;
  AllocatorCallbacks<PortAllocatorSession*, const std::vector<Port*>&> ports_pruned_;
  AllocatorCallbacks<PortAllocatorSession*, const std::vector<Candidate>&> candidates_ready_;
  AllocatorCallbacks<PortAllocatorSession*, const IceCandidateErrorEvent&> candidate_error_;
  AllocatorCallbacks<PortAllocatorSession*, const std::vector<Candidate>&> candidates_removed_;
  AllocatorCallbacks<PortAllocatorSession*> candidates_allocation_done_;
  AllocatorCallbacks<PortAllocatorSession*, IceRegatheringReason> ice_regathering_;
};

class PortAllocator {
 public:
  PortAllocator();
  virtual ~PortAllocator();
  PortAllocator(const PortAllocator&) = delete;
  PortAllocator& operator=(const PortAllocator&) = delete;

  // Only the given credentials may take a pooled session.
  void set_restrict_ice_credentials_change(bool value) { restrict_ice_credentials_change_ = value; }

  // Sets the servers and the pool. A change of servers discards the pool; the
  // pool then grows or shrinks to `candidate_pool_size`, and new pooled
  // sessions start gathering at `now`.
  bool SetConfiguration(Timestamp now, const ServerAddresses& stun_servers,
                        const std::vector<RelayServerConfig>& turn_servers, int candidate_pool_size,
                        PortPrunePolicy turn_port_prune_policy,
                        const std::optional<int>& stun_candidate_keepalive_interval_ms = std::nullopt);
  bool SetConfiguration(Timestamp now, const ServerAddresses& stun_servers,
                        const std::vector<RelayServerConfig>& turn_servers, int candidate_pool_size,
                        bool prune_turn_ports,
                        const std::optional<int>& stun_candidate_keepalive_interval_ms = std::nullopt) {
    return SetConfiguration(now, stun_servers, turn_servers, candidate_pool_size,
                            prune_turn_ports ? PRUNE_BASED_ON_PRIORITY : NO_PRUNE,
                            stun_candidate_keepalive_interval_ms);
  }

  const ServerAddresses& stun_servers() const { return stun_servers_; }
  const std::vector<RelayServerConfig>& turn_servers() const { return turn_servers_; }
  int candidate_pool_size() const { return candidate_pool_size_; }
  const std::optional<TimeDelta>& stun_candidate_keepalive_interval() const {
    return stun_candidate_keepalive_interval_;
  }

  virtual void SetNetworkIgnoreMask(int network_ignore_mask) = 0;
  virtual void SetVpnPreference(VpnPreference preference) { vpn_preference_ = preference; }

  std::unique_ptr<PortAllocatorSession> CreateSession(std::string_view content_name, int component,
                                                      std::string_view ice_ufrag, std::string_view ice_pwd);
  // A pooled session (the first, or the one with these credentials when
  // restricted), given the transport's parameters. nullptr if none.
  std::unique_ptr<PortAllocatorSession> TakePooledSession(std::string_view content_name, int component,
                                                          std::string_view ice_ufrag, std::string_view ice_pwd);
  const PortAllocatorSession* GetPooledSession(const IceParameters* ice_credentials = nullptr) const;
  virtual void DiscardCandidatePool() { pooled_sessions_.clear(); }
  std::vector<IceParameters> GetPooledIceCredentials() const;
  // Runs the pooled sessions.
  void ProcessPooledSessions(Timestamp now);
  Timestamp NextPooledSessionsProcessTime() const;

  // The candidate as signalled: without the related address where it would
  // reveal what the flags or the filter hide.
  Candidate SanitizeCandidate(const Candidate& c) const;

  uint64_t ice_tiebreaker() const { return tiebreaker_; }
  uint32_t flags() const { return flags_; }
  void set_flags(uint32_t flags) { flags_ = flags; }
  int min_port() const { return min_port_; }
  int max_port() const { return max_port_; }
  bool SetPortRange(int min_port, int max_port) {
    if (min_port > max_port) return false;
    min_port_ = min_port;
    max_port_ = max_port;
    return true;
  }
  void set_max_ipv6_networks(int networks) { max_ipv6_networks_ = networks; }
  int max_ipv6_networks() const { return max_ipv6_networks_; }
  uint32_t step_delay() const { return step_delay_; }
  void set_step_delay(uint32_t delay) { step_delay_ = delay; }
  bool allow_tcp_listen() const { return allow_tcp_listen_; }
  void set_allow_tcp_listen(bool allow) { allow_tcp_listen_ = allow; }
  uint32_t candidate_filter() const { return candidate_filter_; }
  void SetCandidateFilter(uint32_t filter);
  void set_candidate_filter(uint32_t filter) { SetCandidateFilter(filter); }
  bool prune_turn_ports() const { return turn_port_prune_policy_ == PRUNE_BASED_ON_PRIORITY; }
  PortPrunePolicy turn_port_prune_policy() const { return turn_port_prune_policy_; }

  void SubscribeCandidateFilterChanged(const void* tag, std::function<void(uint32_t, uint32_t)> cb) {
    candidate_filter_changed_.Add(tag, std::move(cb));
  }
  void UnsubscribeCandidateFilterChanged(const void* tag) { candidate_filter_changed_.Remove(tag); }

 protected:
  virtual std::unique_ptr<PortAllocatorSession> CreateSessionInternal(std::string_view content_name,
                                                                      int component, std::string_view ice_ufrag,
                                                                      std::string_view ice_pwd) = 0;
  const std::vector<std::unique_ptr<PortAllocatorSession>>& pooled_sessions() const { return pooled_sessions_; }
  virtual bool MdnsObfuscationEnabled() const { return false; }

  uint32_t flags_ = kDefaultPortAllocatorFlags;
  int min_port_ = 0;
  int max_port_ = 0;
  int max_ipv6_networks_ = kDefaultMaxIPv6Networks;
  uint32_t step_delay_ = kDefaultStepDelay;
  bool allow_tcp_listen_ = true;
  uint32_t candidate_filter_ = CF_ALL;
  VpnPreference vpn_preference_ = VpnPreference::kDefault;

 private:
  std::vector<std::unique_ptr<PortAllocatorSession>>::const_iterator FindPooledSession(
      const IceParameters* ice_credentials) const;

  ServerAddresses stun_servers_;
  std::vector<RelayServerConfig> turn_servers_;
  int candidate_pool_size_ = 0;
  std::vector<std::unique_ptr<PortAllocatorSession>> pooled_sessions_;
  PortPrunePolicy turn_port_prune_policy_ = NO_PRUNE;
  std::optional<TimeDelta> stun_candidate_keepalive_interval_;
  bool restrict_ice_credentials_change_ = false;
  AllocatorCallbacks<uint32_t, uint32_t> candidate_filter_changed_;
  uint64_t tiebreaker_;
};

// What PeerConnection::InitializePortAllocator_n (pc/peer_connection.cc)
// reads from the RTCConfiguration (api/peer_connection_interface.h), with its
// defaults.
struct PeerConnectionAllocatorConfig {
  // IceTransportsType: kAll -> CF_ALL, kNoHost -> CF_ALL & ~CF_HOST,
  // kRelay -> CF_RELAY, kNone -> CF_NONE.
  uint32_t candidate_filter = CF_ALL;
  int ice_candidate_pool_size = 0;
  int max_ipv6_networks = kDefaultMaxIPv6Networks;
  bool disable_ipv6_on_wifi = false;
  bool disable_link_local_networks = false;
  bool tcp_candidates_enabled = true;  // kTcpCandidatePolicyEnabled.
  bool low_cost_networks_only = false;  // kCandidateNetworkPolicyLowCost.
  bool prune_turn_ports = false;
  PortPrunePolicy turn_port_prune_policy = NO_PRUNE;
  std::optional<int> stun_candidate_keepalive_interval_ms;
  // The WebRTC-IPv6Default field trial (enabled unless disabled).
  bool ipv6_default = true;
};

// PeerConnection::InitializePortAllocator_n: adds the shared socket, IPv6 and
// IPv6-on-Wi-Fi flags (less what the config turns off), the minimum step
// delay (50 ms: "No step delay is used while allocating ports"), the
// candidate filter and IPv6 network limit, then SetConfiguration last (it
// may start pooled sessions with these settings).
void ConfigureAsPeerConnection(Timestamp now, PortAllocator* allocator, const ServerAddresses& stun_servers,
                               const std::vector<RelayServerConfig>& turn_servers,
                               const PeerConnectionAllocatorConfig& config = {});

}  // namespace sm::p2p
