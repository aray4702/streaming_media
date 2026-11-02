// UDP and STUN ports (issue #999, part of ICE #990, milestone 3): libwebrtc
// M157's p2p/base/stun_port.{h,cc} (compare/webrtc/reference/p2p/base/).
//
// A UDPPort binds one UDP socket on its network (or shares the allocator's)
// and gathers a host candidate for the socket's address, then a
// server-reflexive candidate from each STUN server (STUN binding requests,
// RFC 5389). A StunPort gathers only the server-reflexive candidates. As in
// libwebrtc:
// - Binding requests use STUN's retransmission schedule (stun_request.h);
//   after a response the port sends the next one kStunKeepaliveInterval
//   (10 s, or the configured interval) later to keep the NAT binding open,
//   for as long as the keepalive lifetime allows: forever, or 2 minutes on a
//   high-cost (cellular) network. After an error response it retries on the
//   same interval for up to 50 s.
// - A server that times out, answers with an error or has an incompatible
//   address family counts as done; the port is complete when every server
//   is done and at least one answered (or there were none, or the socket is
//   shared), and fails otherwise. Errors are signalled with the server's URL.
// - A shared socket's reflexive address equal to its local address adds no
//   candidate (the host candidate covers it), nor does an address another
//   server already gave.
//
// Differences from libwebrtc: STUN servers are given by IP address (no DNS
// resolution: the library's addresses carry no hostnames), there is no mDNS
// obfuscation and no local network access permission (granted, as with no
// permission factory). The reflexive candidate's URL is "stun:<ip>:<port>",
// as libwebrtc writes it for a server configured by its IP literal (which
// becomes the address's hostname there).
#pragma once

#include <memory>
#include <optional>
#include <set>

#include "sm/p2p/port.h"
#include "sm/p2p/stun_request.h"

namespace sm::p2p {

// The keepalive lifetime of a STUN port on a high-cost network.
inline constexpr TimeDelta kHighCostPortKeepaliveLifetime = TimeDelta::Seconds(2 * 60);

class UDPPort : public Port {
 public:
  // A host port on `socket`, which the caller owns and demultiplexes
  // (HandleIncomingPacket).
  static std::unique_ptr<UDPPort> Create(Timestamp now, const Parameters& args, AsyncPacketSocket* socket,
                                         bool emit_local_for_anyaddress,
                                         std::optional<TimeDelta> stun_keepalive_interval);
  // A host port on a socket of its own in [min_port, max_port] (0, 0: any).
  // nullptr when no socket can be bound.
  static std::unique_ptr<UDPPort> Create(Timestamp now, const Parameters& args, uint16_t min_port,
                                         uint16_t max_port, bool emit_local_for_anyaddress,
                                         std::optional<TimeDelta> stun_keepalive_interval);
  ~UDPPort() override;

  SocketAddress GetLocalAddress() const { return socket_->GetLocalAddress(); }
  const ServerAddresses& server_addresses() const { return server_addresses_; }
  void set_server_addresses(const ServerAddresses& addresses) { server_addresses_ = addresses; }

  void PrepareAddress(Timestamp now) override;
  Connection* CreateConnection(Timestamp now, const Candidate& address, CandidateOrigin origin) override;
  int SendTo(Timestamp now, std::span<const uint8_t> data, const SocketAddress& addr, const PacketOptions& options,
             bool payload) override;
  int SetOption(SocketOption opt, int value) override;
  int GetOption(SocketOption opt, int* value) override;
  int GetError() override { return error_; }
  bool SupportsProtocol(std::string_view protocol) const override { return protocol == UDP_PROTOCOL_NAME; }
  ProtocolType GetProtocol() const override { return PROTO_UDP; }
  void GetStunStats(std::optional<StunStats>* stats) override { *stats = stats_; }
  bool HandleIncomingPacket(AsyncPacketSocket* socket, const ReceivedPacket& packet) override;
  void UpdateNetworkCost() override;

  void CancelPendingTasks() override;
  void Process(Timestamp now) override;
  Timestamp NextProcessTime() const override;

  void set_stun_keepalive_delay(const std::optional<TimeDelta>& delay) {
    stun_keepalive_delay_ = delay.value_or(kStunKeepaliveInterval);
  }
  TimeDelta stun_keepalive_delay() const { return stun_keepalive_delay_; }
  TimeDelta stun_keepalive_lifetime() const { return stun_keepalive_lifetime_; }
  void set_stun_keepalive_lifetime(TimeDelta lifetime) { stun_keepalive_lifetime_ = lifetime; }

  StunRequestManager& request_manager() { return request_manager_; }
  // Tests: transaction ids from the fixed-seed generator when no crypto is
  // installed (StunRequestManager::set_allow_predictable_ids_for_testing).
  void set_allow_predictable_ids_for_testing(bool allow) {
    request_manager_.set_allow_predictable_ids_for_testing(allow);
  }

 protected:
  UDPPort(Timestamp now, const Parameters& args, IceCandidateType type, AsyncPacketSocket* socket,
          bool emit_local_for_anyaddress);
  UDPPort(Timestamp now, const Parameters& args, IceCandidateType type, uint16_t min_port, uint16_t max_port,
          bool emit_local_for_anyaddress);
  bool Init(Timestamp now);

  void PostAddAddress(bool is_final) override;
  Dscp StunDscpValue() const override { return dscp_; }

  void OnLocalAddressReady(Timestamp now, const SocketAddress& address);
  void OnReadPacket(const ReceivedPacket& packet);
  void OnSocketClose(int error);
  void SendStunBindingRequests(Timestamp now);

 private:
  friend class StunBindingRequest;

  void MaybePrepareStunCandidate(Timestamp now);
  void SendStunBindingRequest(Timestamp now, const SocketAddress& stun_addr);
  // When the socket is bound to the any address, the network's default local
  // address (if `emit_local_for_anyaddress`). False if it should but can't.
  bool MaybeSetDefaultLocalAddress(SocketAddress* addr) const;
  void OnStunBindingRequestSucceeded(TimeDelta rtt, const SocketAddress& stun_server_addr,
                                     const SocketAddress& stun_reflected_addr);
  void OnStunBindingOrResolveRequestFailed(const SocketAddress& stun_server_addr, int error_code,
                                           std::string_view reason);
  void MaybeSetPortCompleteOrError();
  void SendStunRequest(Timestamp now, std::span<const uint8_t> data, StunRequest* request);
  bool HasStunCandidateWithAddress(const SocketAddress& addr) const;
  TimeDelta GetStunKeepaliveLifetime() const {
    return network_cost() >= static_cast<int16_t>(kNetworkCostHigh) ? kHighCostPortKeepaliveLifetime
                                                                     : TimeDelta::PlusInfinity();
  }

  ServerAddresses server_addresses_;
  ServerAddresses bind_request_succeeded_servers_;
  ServerAddresses bind_request_failed_servers_;
  StunRequestManager request_manager_;
  std::unique_ptr<AsyncPacketSocket> owned_socket_;
  AsyncPacketSocket* socket_;
  int error_ = 0;
  int send_error_count_ = 0;
  StunStats stats_;
  bool ready_ = false;
  TimeDelta stun_keepalive_delay_ = kStunKeepaliveInterval;
  TimeDelta stun_keepalive_lifetime_ = TimeDelta::PlusInfinity();
  Dscp dscp_ = Dscp::kNoChange;
  const bool emit_local_for_anyaddress_;
};

class StunPort : public UDPPort {
 public:
  static std::unique_ptr<StunPort> Create(Timestamp now, const Parameters& args, uint16_t min_port,
                                          uint16_t max_port, const ServerAddresses& servers,
                                          std::optional<TimeDelta> stun_keepalive_interval);
  void PrepareAddress(Timestamp now) override;

 private:
  StunPort(Timestamp now, const Parameters& args, uint16_t min_port, uint16_t max_port,
           const ServerAddresses& servers);
};

}  // namespace sm::p2p
