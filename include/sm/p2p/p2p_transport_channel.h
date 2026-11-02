// The ICE agent (issue #1002 part 2, C3 part 6, ICE #990, milestone 3): libwebrtc M157's
// p2p/base/p2p_transport_channel.{h,cc} (compare/webrtc/reference/p2p/base/).
//
// P2PTransportChannel is one ICE transport (one component of one m-section). It gathers local candidates through a
// PortAllocator session, pairs them with the remote candidates it is given (and with peer-reflexive ones learned from
// incoming checks), and lets its ICE controller (WrappingActiveIceController over BasicIceController, #1028) choose
// which pairs to ping, which one to select, and which to prune, as libwebrtc does:
// - MaybeStartGathering takes a pooled session with the local credentials, or creates one; new credentials (an ICE
//   restart) stop the old sessions, prune their ports, and gather again. The gathering state follows the session
//   (complete when allocation is done, unless gathering continually).
// - Every ready port is paired with every remembered remote candidate of the current generation (ports of older
//   sessions are pruned and pair only with candidates that arrived through them). A remote candidate of an older
//   generation (by ufrag) is dropped; one with an unknown ufrag is held for the next generation.
// - Pings carry USE-CANDIDATE by the controller's nomination rule (semi-aggressive by default) or, with renomination
//   on both sides, NOMINATION with a counter that grows with each switch. The controlled side switches to a pair the
//   remote nominated, or that data arrived on.
// - State: INIT until a connection exists, FAILED when none is active, COMPLETED when each network has at most one
//   active connection, CONNECTING otherwise; the standard RTCIceTransportState (new, checking, connected,
//   disconnected, failed) beside it. Writable follows the selected connection (or a presumed-writable TURN-TURN
//   pair); receiving is any connection receiving. When every connection has timed out they are all destroyed.
// - With continual gathering, networks without a connection are regathered every
//   regather_on_failed_networks_interval (5 min) once pinging has started.
// - SendPacket sends on the selected connection while it is writable, unreliable or presumed writable (ENOTCONN
//   otherwise); received data is handed up whichever connection it came on.
// Same constants and defaults as libwebrtc: IceConfig with the receiving timeout (2.5 s), backup ping interval
// (25 s), stable writable ping interval (2.5 s), regather interval (5 min), receiving switching delay (1 s),
// presume_writable_when_fully_relayed on, gather once, and the WebRTC-IceFieldTrials defaults.
//
// The library's time model replaces libwebrtc's network thread and clock. Calls that read the clock there take `now`
// here; the channel runs its allocator sessions (and through them the ports and their connections' STUN
// transactions), its controller's queued tasks and the regathering timer in Process(now), at NextProcessTime(). The
// calls that take no `now` (SetRemoteIceParameters, SetIceConfig, and the signals of sockets and connections) use
// the time of the last call that had one (a connection's signal: the later of that and its last receive), so the
// owner calls Process(now) before handing the sockets their packets at `now`, as the test bench's loops do.
//
// Not here (see docs/design/libwebrtc-parity.md 10.9): hostname (mDNS) candidates and their DNS resolution (the
// library's addresses carry no hostname, as 10.3), the local network access permission (always granted), the STUN
// dictionary (GOOG-DELTA; no library API writes it), the DTLS-in-STUN piggyback (off by default in M157), the RTC
// event log, the field trial strings (WebRTC-IceFieldTrials and the others are at their defaults; a caller may pass
// IceFieldTrials), socket options from field trials (WebRTC-DscpFieldTrial, -SetSocketReceiveBuffer,
// -RFC8888CongestionControlFeedback: off by default), and candidate stats in GetStats.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/active_ice_controller_interface.h"
#include "sm/p2p/candidate.h"
#include "sm/p2p/connection.h"
#include "sm/p2p/ice_agent_interface.h"
#include "sm/p2p/ice_controller_interface.h"
#include "sm/p2p/ice_switch_reason.h"
#include "sm/p2p/ice_transport_internal.h"
#include "sm/p2p/network_route.h"
#include "sm/p2p/p2p_transport_channel_ice_field_trials.h"
#include "sm/p2p/port.h"
#include "sm/p2p/port_allocator.h"
#include "sm/p2p/transport_description.h"

namespace sm::p2p {

// RFC 5245 9.1.1.1 says an ICE restart MUST change both the ufrag and the password, but 9.2.1.1 says changing
// either indicates a restart; libwebrtc treats a change of either as a restart.
bool IceCredentialsChanged(std::string_view old_ufrag, std::string_view old_pwd, std::string_view new_ufrag,
                           std::string_view new_pwd);

// A remote candidate and the port it arrived through (nullptr: from signaling).
class RemoteCandidate : public Candidate {
 public:
  RemoteCandidate(const Candidate& c, Port* origin_port) : Candidate(c), origin_port_(origin_port) {}
  Port* origin_port() const { return origin_port_; }

 private:
  Port* origin_port_;
};

class P2PTransportChannel : public IceTransportInternal, public IceAgentInterface {
 public:
  // IceTransportInit's optional parts. `field_trials` stands for WebRTC-IceFieldTrials (defaults when not given).
  struct Options {
    IceControllerFactoryInterface* ice_controller_factory = nullptr;
    ActiveIceControllerFactoryInterface* active_ice_controller_factory = nullptr;
    IceFieldTrials field_trials;
  };

  // `allocator` must outlive the channel. `now` starts the channel's clock.
  P2PTransportChannel(Timestamp now, std::string_view transport_name, int component, PortAllocator* allocator);
  P2PTransportChannel(Timestamp now, std::string_view transport_name, int component, PortAllocator* allocator,
                      const Options& options);
  ~P2PTransportChannel() override;

  // IceTransportInternal.
  IceTransportStateInternal GetState() const override { return state_; }
  IceTransportState GetIceTransportState() const override { return standardized_state_; }
  const std::string& transport_name() const override { return transport_name_; }
  int component() const override { return component_; }
  bool writable() const override { return writable_; }
  bool receiving() const override { return receiving_; }
  void SetIceRole(IceRole role) override;
  IceRole GetIceRole() const override { return ice_role_; }
  void SetIceParameters(const IceParameters& ice_params) override;
  void SetRemoteIceParameters(const IceParameters& ice_params) override;
  void SetRemoteIceMode(IceMode mode) override { remote_ice_mode_ = mode; }
  // Starts gathering if it never started, or if the local credentials changed (an ICE restart).
  void MaybeStartGathering(Timestamp now) override;
  IceGatheringState gathering_state() const override { return gathering_state_; }
  void AddRemoteCandidate(Timestamp now, const Candidate& candidate) override;
  void RemoveRemoteCandidate(const Candidate& candidate) override;
  void RemoveAllRemoteCandidates() override { remote_candidates_.clear(); }
  // Settings that would change gathering once it started (continual gathering) or connections once they exist
  // (presume writable) are kept as they were, as libwebrtc does.
  void SetIceConfig(const IceConfig& config) override;
  const IceConfig& config() const override { return config_; }
  int SendPacket(Timestamp now, std::span<const uint8_t> data, const PacketOptions& options,
                 int flags = kPfNormal) override;
  int SetOption(SocketOption opt, int value) override;
  bool GetOption(SocketOption opt, int* value) override;
  int GetError() override { return error_; }
  bool GetStats(Timestamp now, IceTransportStats* ice_transport_stats) override;
  std::optional<int> GetRttEstimate() override;
  std::optional<const CandidatePair> GetSelectedCandidatePair() const override;
  std::optional<NetworkRoute> network_route() const override { return network_route_; }

  // IceAgentInterface.
  Timestamp GetLastPingSent() const override { return last_ping_sent_; }
  void OnStartedPinging() override;
  void UpdateConnectionStates(Timestamp now) override;
  void UpdateState(Timestamp now) override;
  void ForgetLearnedStateForConnections(std::span<const Connection* const> connections) override;
  void SendPingRequest(Timestamp now, const Connection* connection) override;
  void SwitchSelectedConnection(Timestamp now, const Connection* new_connection, IceSwitchReason reason) override;
  bool PruneConnections(Timestamp now, std::span<const Connection* const> connections) override;

  // The time model: runs the allocator sessions, the controller's tasks and the regathering timer that are due.
  void Process(Timestamp now);
  Timestamp NextProcessTime() const;

  const Connection* selected_connection() const { return selected_connection_; }
  void set_incoming_only(bool value) { incoming_only_ = value; }
  const std::vector<Port*>& ports() const { return ports_; }
  const std::vector<Port*>& pruned_ports() const { return pruned_ports_; }
  IceMode remote_ice_mode() const { return remote_ice_mode_; }
  void PruneAllPorts();
  // max(kMinCheckReceivingInterval, receiving timeout / 10).
  TimeDelta check_receiving_interval() const;
  void RemoveConnection(Connection* connection);
  Dscp DefaultDscpValue() const;
  // For tests: the controller's next connection to ping.
  Connection* FindNextPingableConnection(Timestamp now);
  void MarkConnectionPinged(Connection* conn);
  std::span<Connection* const> connections() const { return connections_; }
  void RemoveConnectionForTest(Timestamp now, Connection* connection);
  PortAllocatorSession* allocator_session() const {
    return allocator_sessions_.empty() ? nullptr : allocator_sessions_.back().get();
  }
  const std::vector<RemoteCandidate>& remote_candidates() const { return remote_candidates_; }
  const IceParameters* remote_ice_parameters() const {
    return remote_ice_parameters_.empty() ? nullptr : &remote_ice_parameters_.back();
  }
  const IceFieldTrials& ice_field_trials() const { return ice_field_trials_; }
  std::string ToString() const;

 private:
  bool IsGettingPorts() const { return allocator_session()->IsGettingPorts(); }
  bool ReadyToSend(const Connection* connection) const;
  bool PresumedWritable(const Connection* conn) const;
  void SendPingRequestInternal(Timestamp now, Connection* connection);
  NetworkRoute ConfigureNetworkRoute(const Connection* conn) const;
  void SwitchSelectedConnectionInternal(Timestamp now, Connection* conn, IceSwitchReason reason);
  void UpdateTransportState();
  void HandleAllTimedOut(Timestamp now);
  void MaybeStopPortAllocatorSessions(Timestamp now);
  void OnSelectedConnectionDestroyed(Timestamp now);
  IceTransportStateInternal ComputeState() const;
  IceTransportState ComputeIceTransportState() const;
  bool CreateConnections(Timestamp now, const Candidate& remote_candidate, Port* origin_port);
  bool CreateConnection(Timestamp now, Port* port, const Candidate& remote_candidate, Port* origin_port);
  bool FindConnection(const Connection* connection) const;
  uint32_t GetRemoteCandidateGeneration(const Candidate& candidate) const;
  bool IsDuplicateRemoteCandidate(const Candidate& candidate) const;
  void RememberRemoteCandidate(const Candidate& remote_candidate, Port* origin_port);
  void PingConnection(Timestamp now, Connection* conn);
  void AddAllocatorSession(Timestamp now, std::unique_ptr<PortAllocatorSession> session);
  void AddConnection(Connection* connection);
  void OnPortReady(PortAllocatorSession* session, Port* port);
  void OnPortsPruned(PortAllocatorSession* session, const std::vector<Port*>& ports);
  void OnCandidatesReady(PortAllocatorSession* session, const std::vector<Candidate>& candidates);
  void OnCandidateError(PortAllocatorSession* session, const IceCandidateErrorEvent& event);
  void OnCandidatesRemoved(PortAllocatorSession* session, const std::vector<Candidate>& candidates);
  void OnCandidatesAllocationDone(PortAllocatorSession* session);
  void OnUnknownAddress(Timestamp now, Port* port, const SocketAddress& addr, ProtocolType proto,
                        IceMessage* stun_msg, const std::string& remote_username, bool port_muxed);
  void OnCandidateFilterChanged(uint32_t prev_filter, uint32_t cur_filter);
  void OnPortDestroyed(Port* port);
  bool PrunePort(Port* port);
  void OnConnectionStateChange(Connection* connection);
  void OnReadPacket(Connection* connection, const ReceivedPacket& packet);
  void OnSentPacket(const SentPacketInfo& sent_packet);
  void OnReadyToSend(Connection* connection);
  void OnConnectionDestroyed(Connection* connection);
  void OnNominated(Connection* conn);
  uint32_t GetNominationAttr(Connection* conn) const;
  bool GetUseCandidateAttr(Connection* conn) const;
  bool AllowedToPruneConnections() const;
  const IceParameters* FindRemoteIceFromUfrag(std::string_view ufrag, uint32_t* generation) const;
  uint32_t remote_ice_generation() const {
    return remote_ice_parameters_.empty() ? 0 : static_cast<uint32_t>(remote_ice_parameters_.size() - 1);
  }
  bool IsPortPruned(const Port* port) const;
  bool IsRemoteCandidatePruned(const Candidate& cand) const;
  void SetWritable(bool writable);
  void SetReceiving(bool receiving);
  Candidate SanitizeLocalCandidate(const Candidate& c) const;
  Candidate SanitizeRemoteCandidate(const Candidate& c) const;
  Connection* FromIceController(const Connection* conn) const;
  TimeDelta ComputeEstimatedDisconnectedTime(Timestamp now, const Connection* old_connection) const;
  void FinishAddingRemoteCandidate(Timestamp now, const Candidate& new_remote_candidate);
  // The time of a signal that carries none (see the file comment).
  Timestamp EventTime(const Connection* connection);

  const std::string transport_name_;
  const int component_;
  PortAllocator* const allocator_;
  bool incoming_only_ = false;
  int error_ = 0;
  std::vector<std::unique_ptr<PortAllocatorSession>> allocator_sessions_;
  std::vector<Port*> ports_;
  std::vector<Port*> pruned_ports_;
  Connection* selected_connection_ = nullptr;
  std::vector<Connection*> connections_;
  std::vector<RemoteCandidate> remote_candidates_;
  bool had_connection_ = false;  // if connections_ has ever been nonempty
  std::map<SocketOption, int> options_;
  IceParameters ice_parameters_;
  std::vector<IceParameters> remote_ice_parameters_;
  IceMode remote_ice_mode_ = ICEMODE_FULL;
  IceRole ice_role_ = ICEROLE_UNKNOWN;
  IceGatheringState gathering_state_ = kIceGatheringNew;
  IceConfig config_;
  int last_sent_packet_id_ = -1;  // -1 indicates no packet was sent before.
  // Set when the controller starts pinging; the regathering timer (RepeatingTaskHandle there).
  Timestamp next_regather_ = Timestamp::PlusInfinity();
  IceTransportStateInternal state_ = IceTransportStateInternal::kInit;
  IceTransportState standardized_state_ = IceTransportState::kNew;
  bool has_been_writable_ = false;  // if writable_ has ever been true
  std::optional<NetworkRoute> network_route_;
  uint32_t selected_candidate_pair_changes_ = 0;
  uint64_t bytes_sent_ = 0;
  uint64_t bytes_received_ = 0;
  uint64_t packets_sent_ = 0;
  uint64_t packets_received_ = 0;
  Timestamp last_data_received_ = Timestamp::MinusInfinity();
  bool writable_ = false;
  bool receiving_ = false;
  uint32_t nomination_ = 0;
  Timestamp last_ping_sent_ = Timestamp::Millis(0);  // Timestamp::Zero() in libwebrtc
  IceFieldTrials ice_field_trials_;
  // The time of the last call that carried one (see the file comment).
  Timestamp now_;
  std::unique_ptr<ActiveIceControllerInterface> ice_controller_;
};

}  // namespace sm::p2p
