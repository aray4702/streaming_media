// The port allocator ICE uses (issue #1001, part of ICE #990, milestone 3):
// libwebrtc M157's p2p/client/basic_port_allocator.{h,cc}
// (compare/webrtc/reference/p2p/client/).
//
// A BasicPortAllocatorSession gathers on every network the NetworkManager
// lists, after filtering them as libwebrtc does (the ignore mask, by default
// loopback; the VPN preference; link-local and costly networks when the flags
// say so; at most max_ipv6_networks IPv6 networks, picked by adapter type),
// with one AllocationSequence per network. A sequence runs its phases a step
// delay apart (1 s by default): UDP (a UDPPort for the host candidate, and a
// StunPort for the server-reflexive ones, or the UDPPort itself gathers both
// on a shared socket with PORTALLOCATOR_ENABLE_SHARED_SOCKET), then relay
// (TURN), then TCP, and completes. IPv6 networks are used only with
// PORTALLOCATOR_ENABLE_IPV6 (and IPv6 Wi-Fi only with ..._IPV6_ON_WIFI).
// Phases another sequence already covers on the same network and best IP are
// skipped. Each port's candidates are signalled through the candidate filter
// (a public host address passes as reflexive), the port is "ready" once it
// has a pairable candidate (port ready, then KeepAliveUntilPruned), and the
// session reports allocation done when every sequence and port has finished.
//
// When the networks change, sequences on networks that went away are
// stopped and their ports pruned (candidates removed), and new networks are
// gathered on (continual gathering: ICE regathering is signalled).
// RegatherOnFailedNetworks does the same for networks whose ports have no
// connection. ClearGettingPorts stops the current gathering (ports still
// gathering count as failed), StopGettingPorts stops for good.
//
// The library's time model replaces libwebrtc's network thread: the session
// queues what libwebrtc posts and runs it, and its ports, in Process(now);
// the owner calls Process at NextProcessTime(). Ports are owned by the
// session; a port that destroys itself is deleted at the end of the Process
// call that destroyed it.
//
// Differences from libwebrtc:
// - No TCP ports and no TURN ports yet (C4): the TCP phase makes nothing, and
//   relay ports come from a RelayPortFactoryInterface the caller may give
//   (none by default, where libwebrtc defaults to its TurnPortFactory).
//   PortConfiguration does not add DTLS TURN servers as STUN servers
//   (ProtocolType has no PROTO_DTLS until C4).
// - No mDNS (host candidates are never obfuscated), no local network access
//   permission, no VPN list, no field trials (WebRTC-UseTurnServerAsStunServer
//   is at its default, enabled).
// - Ports have no connections until #1000, so RegatherOnFailedNetworks
//   treats every network as failed.
// - pc/peer_connection.cc (the flags and step delay PeerConnection sets) is
//   not in reference/; the allocator's own defaults apply.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/async_packet_socket.h"
#include "sm/p2p/network_manager.h"
#include "sm/p2p/port.h"
#include "sm/p2p/port_allocator.h"
#include "sm/p2p/stun_port.h"

namespace sm::p2p {

// p2p/client/relay_port_factory_interface.h: makes TURN ports (C4).
struct CreateRelayPortArgs {
  Port::Parameters port;
  const ProtocolAddress* server_address = nullptr;
  const RelayServerConfig* config = nullptr;
  std::string username;
  std::string password;
  int relative_priority = 0;
};
class RelayPortFactoryInterface {
 public:
  virtual ~RelayPortFactoryInterface() = default;
  // On the allocator's shared UDP socket.
  virtual std::unique_ptr<Port> Create(Timestamp now, const CreateRelayPortArgs& args,
                                       AsyncPacketSocket* udp_socket) = 0;
  // On a socket of its own in [min_port, max_port].
  virtual std::unique_ptr<Port> Create(Timestamp now, const CreateRelayPortArgs& args, int min_port,
                                       int max_port) = 0;
};

class BasicPortAllocator final : public PortAllocator {
 public:
  // `network_manager`, `socket_factory` and `relay_port_factory` (may be
  // null) must outlive the allocator and its sessions.
  BasicPortAllocator(NetworkManager* network_manager, PacketSocketFactory* socket_factory,
                     RelayPortFactoryInterface* relay_port_factory = nullptr);
  ~BasicPortAllocator() override;

  void SetNetworkIgnoreMask(int network_ignore_mask) override { network_ignore_mask_ = network_ignore_mask; }
  // The ignore mask with the VPN preference applied.
  int GetNetworkIgnoreMask() const;
  NetworkManager* network_manager() const { return network_manager_; }
  PacketSocketFactory* socket_factory() const { return socket_factory_; }
  RelayPortFactoryInterface* relay_port_factory() const { return relay_port_factory_; }
  void AddTurnServerForTesting(Timestamp now, const RelayServerConfig& turn_server);

 protected:
  std::unique_ptr<PortAllocatorSession> CreateSessionInternal(std::string_view content_name, int component,
                                                              std::string_view ice_ufrag,
                                                              std::string_view ice_pwd) override;

 private:
  NetworkManager* const network_manager_;
  PacketSocketFactory* const socket_factory_;
  RelayPortFactoryInterface* const relay_port_factory_;
  int network_ignore_mask_ = kDefaultNetworkIgnoreMask;
};

// What a sequence makes ports with.
struct PortConfiguration {
  SocketAddress stun_address;  // Legacy single STUN server; joins stun_servers.
  ServerAddresses stun_servers;
  std::string username;
  std::string password;
  bool use_turn_server_as_stun_server_disabled = false;
  using RelayList = std::vector<RelayServerConfig>;
  RelayList relays;

  PortConfiguration(const ServerAddresses& stun_servers, std::string_view username, std::string_view password);
  // The STUN servers, plus every UDP TURN server unless that is disabled.
  ServerAddresses StunServers();
  void AddRelay(const RelayServerConfig& config) { relays.push_back(config); }
  bool SupportsProtocol(const RelayServerConfig& relay, ProtocolType type) const;
  bool SupportsProtocol(ProtocolType type) const;
  ServerAddresses GetRelayServerAddresses(ProtocolType type) const;
  void InsertStunServersForProtocol(ProtocolType type);
};

enum class SessionState { GATHERING, CLEARED, STOPPED };

class AllocationSequence;

class BasicPortAllocatorSession final : public PortAllocatorSession {
 public:
  BasicPortAllocatorSession(BasicPortAllocator* allocator, std::string_view content_name, int component,
                            std::string_view ice_ufrag, std::string_view ice_pwd);
  ~BasicPortAllocatorSession() override;

  BasicPortAllocator* allocator() const { return allocator_; }
  PacketSocketFactory* socket_factory() const { return socket_factory_; }

  void SetCandidateFilter(uint32_t filter) override;
  void StartGettingPorts(Timestamp now) override;
  void StopGettingPorts(Timestamp now) override;
  void ClearGettingPorts(Timestamp now) override;
  bool IsGettingPorts() override { return state_ == SessionState::GATHERING; }
  bool IsCleared() const override { return state_ == SessionState::CLEARED; }
  bool IsStopped() const override { return state_ == SessionState::STOPPED; }
  std::vector<Port*> ReadyPorts() const override;
  std::vector<Candidate> ReadyCandidates() const override;
  bool CandidatesAllocationDone() const override;
  void RegatherOnFailedNetworks(Timestamp now) override;
  void SetStunKeepaliveIntervalForReadyPorts(const std::optional<TimeDelta>& interval) override;
  void PruneAllPorts(Timestamp now) override;

  void Process(Timestamp now) override;
  Timestamp NextProcessTime() const override;

  // Picks at most `max_ipv6_networks` IPv6 networks, one adapter type at a
  // time in libwebrtc's order (Ethernet, loopback, Wi-Fi, cellular, VPN,
  // unknown, any). Removes the picked ones from `all_ipv6_networks`.
  static std::vector<const Network*> SelectIPv6Networks(std::vector<const Network*>& all_ipv6_networks,
                                                        int max_ipv6_networks);

  // Every port the session holds (ready or not), for tests and the transport.
  std::vector<Port*> AllPortsForTesting() const;
  size_t sequence_count() const { return sequences_.size(); }

 protected:
  void UpdateIceParametersInternal() override;

 private:
  friend class AllocationSequence;

  class PortData {
   public:
    enum State { STATE_INPROGRESS, STATE_COMPLETE, STATE_ERROR, STATE_PRUNED };
    PortData(Port* port, AllocationSequence* seq) : port_(port), sequence_(seq) {}
    Port* port() const { return port_; }
    AllocationSequence* sequence() const { return sequence_; }
    bool has_pairable_candidate() const { return has_pairable_candidate_; }
    State state() const { return state_; }
    bool complete() const { return state_ == STATE_COMPLETE; }
    bool error() const { return state_ == STATE_ERROR; }
    bool pruned() const { return state_ == STATE_PRUNED; }
    bool inprogress() const { return state_ == STATE_INPROGRESS; }
    bool ready() const { return has_pairable_candidate_ && state_ != STATE_ERROR && state_ != STATE_PRUNED; }
    void Prune(Timestamp now) {
      state_ = STATE_PRUNED;
      if (port_) port_->Prune(now);
    }
    void set_has_pairable_candidate(bool v) { has_pairable_candidate_ = v; }
    void set_state(State state) { state_ = state; }

   private:
    Port* port_ = nullptr;
    AllocationSequence* sequence_ = nullptr;
    bool has_pairable_candidate_ = false;
    State state_ = STATE_INPROGRESS;
  };

  // The network thread's task queue: tasks run in time order, then in the
  // order they were posted.
  void Post(TimeDelta delay, std::function<void()> task);

  void GetPortConfigurations();
  void ConfigReady(std::unique_ptr<PortConfiguration> config);
  void OnConfigReady(std::unique_ptr<PortConfiguration> config);
  void OnConfigStop();
  void AllocatePorts();
  void OnAllocate(int allocation_epoch);
  void DoAllocate();
  void OnNetworksChanged();
  void OnAllocationSequenceObjectsCreated();
  void DisableEquivalentPhases(const Network* network, PortConfiguration* config, uint32_t* flags);
  void AddAllocatedPort(std::unique_ptr<Port> port, AllocationSequence* seq);
  void OnCandidateReady(Port* port, const Candidate& c);
  void OnCandidateError(Port* port, const IceCandidateErrorEvent& event);
  void OnPortComplete(Port* port);
  void OnPortError(Port* port);
  void OnPortDestroyed(Port* port);
  void MaybeSignalCandidatesAllocationDone();
  void OnPortAllocationComplete() { MaybeSignalCandidatesAllocationDone(); }
  PortData* FindPort(Port* port);
  std::vector<const Network*> GetNetworks();
  std::vector<const Network*> GetFailedNetworks();
  void Regather(const std::vector<const Network*>& networks, IceRegatheringReason reason);
  bool CheckCandidateFilter(const Candidate& c) const;
  bool CandidatePairable(const Candidate& c, const Port* port) const;
  std::vector<PortData*> GetUnprunedPorts(const std::vector<const Network*>& networks);
  void PrunePortsAndRemoveCandidates(const std::vector<PortData*>& port_data_list);
  void GetCandidatesFromPort(const PortData& data, std::vector<Candidate>* candidates) const;
  Port* GetBestTurnPortForNetwork(std::string_view network_name) const;
  bool PruneTurnPorts(Port* newly_pairable_turn_port);
  bool PruneNewlyPairableTurnPort(PortData* newly_pairable_turn_port);

  BasicPortAllocator* const allocator_;
  PacketSocketFactory* const socket_factory_;
  bool allocation_started_ = false;
  bool network_manager_started_ = false;
  bool allocation_sequences_created_ = false;
  std::vector<std::unique_ptr<PortConfiguration>> configs_;
  std::vector<std::unique_ptr<AllocationSequence>> sequences_;
  std::vector<PortData> ports_;
  std::map<Port*, std::unique_ptr<Port>> owned_ports_;
  std::vector<std::unique_ptr<Port>> destroyed_ports_;  // Deleted at the end of Process.
  std::vector<IceCandidateErrorEvent> candidate_error_events_;
  uint32_t candidate_filter_ = CF_ALL;
  const PortPrunePolicy turn_port_prune_policy_;
  SessionState state_ = SessionState::CLEARED;
  int allocation_epoch_ = 0;

  // The latest time the session has been given; what it posts "now" runs at
  // the next Process.
  Timestamp now_ = Timestamp::MinusInfinity();
  std::map<std::pair<Timestamp, uint64_t>, std::function<void()>> tasks_;
  uint64_t next_task_seq_ = 0;
  bool processing_ = false;
};

// Allocates the ports of one network, a phase per step.
class AllocationSequence {
 public:
  enum State { kInit, kRunning, kStopped, kCompleted };

  AllocationSequence(BasicPortAllocatorSession* session, const Network* network, PortConfiguration* config,
                     uint32_t flags, std::function<void()> port_allocation_complete_callback);
  ~AllocationSequence();
  void Init();
  void Clear();
  void OnNetworkFailed();

  State state() const { return state_; }
  const Network* network() const { return network_; }
  bool network_failed() const { return network_failed_; }
  void set_network_failed() { network_failed_ = true; }
  uint32_t flags() const { return flags_; }
  void DisableEquivalentPhases(const Network* network, PortConfiguration* config, uint32_t* flags);
  void Start();
  void Stop();
  // The port bound to the shared socket was destroyed.
  void OnPortDestroyed(Port* port);

 private:
  void Process(int epoch);
  bool IsFlagSet(uint32_t flag) const { return (flags_ & flag) != 0; }
  Port::Parameters PortArgs() const;
  void CreateUDPPorts();
  void CreateTCPPorts();
  void CreateStunPorts();
  void CreateRelayPorts();
  void CreateTurnPort(const RelayServerConfig& config, int relative_priority);
  void OnReadPacket(AsyncPacketSocket* socket, const ReceivedPacket& packet);

  BasicPortAllocatorSession* const session_;
  bool network_failed_ = false;
  const Network* const network_;
  IpAddress previous_best_ip_;
  PortConfiguration* const config_;
  State state_ = kInit;
  uint32_t flags_;
  std::unique_ptr<AsyncPacketSocket> udp_socket_;
  UDPPort* udp_port_ = nullptr;
  std::vector<Port*> relay_ports_;
  int phase_ = 0;
  std::function<void()> port_allocation_complete_callback_;
  int epoch_ = 0;
};

}  // namespace sm::p2p
