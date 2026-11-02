// ICE ports (issue #999, part of ICE #990, milestone 3): libwebrtc M157's
// p2p/base/port.{h,cc} and port_interface.h (compare/webrtc/reference/
// p2p/base/).
//
// A Port gathers the candidates of one transport (UDP for now) on one
// network interface and answers the STUN traffic that reaches it from
// addresses it has no connection to:
// - AddAddress makes a candidate with libwebrtc's priority (type preference,
//   network preference, RFC 3484 address precedence, component) and
//   foundation, and signals it (candidate ready, then port complete when
//   final).
// - Incoming STUN is checked as Port::GetStunMessage does: a binding request
//   without USERNAME or MESSAGE-INTEGRITY gets 400, a wrong local ufrag or a
//   bad MESSAGE-INTEGRITY 401, unknown comprehension-required attributes 420
//   with UNKNOWN-ATTRIBUTES. A good request from an unknown address raises
//   the unknown-address signal, which creates a peer-reflexive candidate and
//   a connection (#1000), then the ICE role check: a conflicting role answers
//   487 or raises the role-conflict signal, by tie-breaker (RFC 5245 7.2.1.1).
//   A GOOG-PING to an address without a connection gets 400.
// - A port no ICE agent keeps (KeepAliveUntilPruned) is destroyed
//   kPortTimeoutDelay (STUN_TOTAL_TIMEOUT + 5 s) after it was made, or as
//   soon as it is pruned, once it has no connections.
//
// The library's time model replaces libwebrtc's network thread: nothing
// reads a clock or posts a task. Calls that may send take `now`; the owner
// calls Process(now) at NextProcessTime(). libwebrtc's port deletes itself
// when it dies; here it marks itself dead (dead()) and raises the destroyed
// signal, and the owner (the allocator, #1001) deletes it.
//
// Not here yet: Connection and the calls that make and destroy connections
// (CreateConnection, GetConnection, DestroyConnection; #1000), TCP and TURN
// ports (C4), mDNS name registration and the local network access
// permission (Chromium's; with no permission factory libwebrtc grants
// every address, as here).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/async_packet_socket.h"
#include "sm/p2p/candidate.h"
#include "sm/p2p/network.h"
#include "sm/p2p/p2p_constants.h"
#include "sm/p2p/socket_address.h"
#include "sm/p2p/stun.h"
#include "sm/p2p/stun_request.h"

namespace sm::p2p {

// p2p/base/transport_description.h.
enum IceRole { ICEROLE_CONTROLLING = 0, ICEROLE_CONTROLLED, ICEROLE_UNKNOWN };

// p2p/base/port_interface.h.
enum ProtocolType { PROTO_UDP, PROTO_TCP, PROTO_SSLTCP, PROTO_TLS, PROTO_LAST = PROTO_TLS };
const char* ProtoToString(ProtocolType proto);

// Port's timeout: a little more than a whole STUN transaction.
inline constexpr int kPortTimeoutDelay = STUN_TOTAL_TIMEOUT + 5000;  // ms

struct StunStats {
  int stun_binding_requests_sent = 0;
  int stun_binding_responses_received = 0;
  double stun_binding_rtt_ms_total = 0;
  double stun_binding_rtt_ms_squared_total = 0;
};

struct IceCandidateErrorEvent {
  std::string address;
  int port = 0;
  std::string url;
  int error_code = 0;
  std::string error_text;
};

using ServerAddresses = std::set<SocketAddress>;

// A CallbackList: any number of receivers, removed by tag.
template <class... Args>
class PortCallbacks {
 public:
  void Add(const void* tag, std::function<void(Args...)> cb) { cbs_.emplace_back(tag, std::move(cb)); }
  void Remove(const void* tag) {
    std::erase_if(cbs_, [tag](const auto& e) { return e.first == tag; });
  }
  void Send(Args... args) {
    // Copy, so a receiver may subscribe or unsubscribe while it runs.
    auto cbs = cbs_;
    for (auto& [tag, cb] : cbs) cb(args...);
  }

 private:
  std::vector<std::pair<const void*, std::function<void(Args...)>>> cbs_;
};

class Port {
 public:
  // PortParametersRef. `network` and `socket_factory` must outlive the port.
  struct Parameters {
    const Network* network = nullptr;
    PacketSocketFactory* socket_factory = nullptr;
    std::string ice_username_fragment;
    std::string ice_password;
    std::string content_name;
    uint64_t ice_tiebreaker = 0;
    // WebRTC-IncreaseIceCandidatePriorityHostSrflx (off by default).
    bool increase_host_srflx_priority = false;
  };

  virtual ~Port();
  Port(const Port&) = delete;
  Port& operator=(const Port&) = delete;

  IceCandidateType Type() const { return type_; }
  const Network* network() const { return network_; }
  IceRole GetIceRole() const { return ice_role_; }
  void SetIceRole(IceRole role) { ice_role_ = role; }
  uint64_t IceTiebreaker() const { return ice_tiebreaker_; }
  bool SharedSocket() const { return shared_socket_; }
  void ResetSharedSocket() { shared_socket_ = false; }

  virtual bool SupportsProtocol(std::string_view protocol) const = 0;
  virtual ProtocolType GetProtocol() const = 0;
  // Starts gathering. Candidates are signalled as they are found.
  virtual void PrepareAddress(Timestamp now) = 0;

  // Sends to `addr`. Returns the bytes sent, or -1 (GetError()).
  virtual int SendTo(Timestamp now, std::span<const uint8_t> data, const SocketAddress& addr,
                     const PacketOptions& options, bool payload) = 0;
  virtual int SetOption(SocketOption opt, int value) = 0;
  virtual int GetOption(SocketOption opt, int* value) = 0;
  virtual int GetError() = 0;
  virtual void GetStunStats(std::optional<StunStats>* /*stats*/) {}

  // Kept until Prune() even with no connections (the allocator calls it when
  // the port is handed to the ICE transport).
  void KeepAliveUntilPruned();
  // Destroyed at the next Process once it has no connections.
  void Prune(Timestamp now);
  // Drops pending work (the destroy checks; a subclass's STUN requests).
  virtual void CancelPendingTasks();
  // Runs what is due by `now`.
  virtual void Process(Timestamp now);
  // When Process next has something to do; PlusInfinity when idle.
  virtual Timestamp NextProcessTime() const;
  // True once the port has destroyed itself; the owner deletes it.
  bool dead() const { return dead_; }

  PacketSocketFactory* socket_factory() const { return factory_; }
  const std::string& content_name() const { return content_name_; }
  void set_content_name(std::string_view name) { content_name_.assign(name); }
  int component() const { return component_; }
  void set_component(int component) { component_ = component; }
  bool send_retransmit_count_attribute() const { return send_retransmit_count_attribute_; }
  void set_send_retransmit_count_attribute(bool enable) { send_retransmit_count_attribute_ = enable; }
  uint32_t generation() const { return generation_; }
  void set_generation(uint32_t generation) { generation_ = generation; }
  const std::string& username_fragment() const { return ice_username_fragment_; }
  const std::string& password() const { return password_; }
  // Updates the port's and its candidates' component and credentials.
  void SetIceParameters(int component, std::string_view username_fragment, std::string_view password);
  const std::vector<Candidate>& Candidates() const { return candidates_; }
  uint16_t min_port() const { return min_port_; }
  uint16_t max_port() const { return max_port_; }
  void set_timeout_delay(int delay_ms) { timeout_delay_ = delay_ms; }
  int16_t network_cost() const { return network_cost_; }
  // OnNetworkTypeChanged: re-reads the network's cost and stamps it on the
  // candidates. The owner calls it when the network's type changes.
  virtual void UpdateNetworkCost();

  // Signals.
  void SubscribeCandidateReady(const void* tag, std::function<void(Port*, const Candidate&)> cb) {
    candidate_ready_.Add(tag, std::move(cb));
  }
  void SubscribeCandidateError(const void* tag, std::function<void(Port*, const IceCandidateErrorEvent&)> cb) {
    candidate_error_.Add(tag, std::move(cb));
  }
  void SubscribePortComplete(const void* tag, std::function<void(Port*)> cb) { port_complete_.Add(tag, std::move(cb)); }
  void SubscribePortError(const void* tag, std::function<void(Port*)> cb) { port_error_.Add(tag, std::move(cb)); }
  void SubscribePortDestroyed(const void* tag, std::function<void(Port*)> cb) {
    port_destroyed_.Add(tag, std::move(cb));
  }
  void SubscribeRoleConflict(std::function<void()> cb) { role_conflict_ = std::move(cb); }
  // A good binding request from an address without a connection: the port,
  // the address, the protocol, the request and the remote ufrag (the last
  // argument, port_muxed, is false here as in libwebrtc).
  using UnknownAddressFn =
      std::function<void(Timestamp, Port*, const SocketAddress&, ProtocolType, IceMessage*, const std::string&, bool)>;
  void SubscribeUnknownAddress(const void* tag, UnknownAddressFn cb) { unknown_address_.Add(tag, std::move(cb)); }
  // With EnablePortPackets(), every datagram from an unknown address.
  void SubscribeReadPacket(const void* tag,
                           std::function<void(Port*, std::span<const uint8_t>, const SocketAddress&)> cb) {
    read_packet_.Add(tag, std::move(cb));
  }
  void SubscribeSentPacket(const void* tag, std::function<void(const SentPacketInfo&)> cb) {
    sent_packet_.Add(tag, std::move(cb));
  }
  void EnablePortPackets() { enable_port_packets_ = true; }
  // Raises the candidate-ready signal again for a candidate the port has
  // (the allocator's SetCandidateFilter resurfaces candidates with it).
  void NotifyCandidateReady(const Candidate& c) { candidate_ready_.Send(this, c); }

  // Answers a binding request (or GOOG-PING) with an error.
  void SendBindingErrorResponse(Timestamp now, StunMessage* message, const SocketAddress& addr, int error_code,
                                std::string_view reason);
  void SendUnknownAttributesErrorResponse(Timestamp now, StunMessage* message, const SocketAddress& addr,
                                          const std::vector<uint16_t>& unknown_types);

  // "RFRAG:LFRAG" of a request: false without USERNAME or the colon.
  bool ParseStunUsername(const StunMessage* stun_msg, std::string* local_username,
                         std::string* remote_username) const;
  std::string CreateStunUsername(std::string_view remote_username) const;
  // RFC 5245 7.2.1.1: false when the request lost the role conflict (487 sent).
  bool MaybeIceRoleConflict(Timestamp now, const SocketAddress& addr, IceMessage* stun_msg,
                            std::string_view remote_ufrag);

  // A packet from the shared socket's demultiplexer. False if not taken.
  virtual bool HandleIncomingPacket(AsyncPacketSocket* socket, const ReceivedPacket& packet);
  virtual bool CanHandleIncomingPacketsFrom(const SocketAddress& remote_addr) const;
  // Called when a Connection finds a local peer-reflexive candidate.
  void AddPrflxCandidate(const Candidate& local) { candidates_.push_back(local); }

  // "Port[ptr:content:component:generation:type:network]".
  std::string ToString() const;

 protected:
  Port(const Parameters& args, IceCandidateType type, Timestamp now);
  Port(const Parameters& args, IceCandidateType type, uint16_t min_port, uint16_t max_port, bool shared_socket,
       Timestamp now);

  void AddAddress(const SocketAddress& address, const SocketAddress& base_address,
                  const SocketAddress& related_address, std::string_view protocol, std::string_view relay_protocol,
                  std::string_view tcptype, IceCandidateType type, uint32_t type_preference,
                  uint32_t relay_preference, std::string_view url, bool is_final);
  virtual void PostAddAddress(bool is_final);
  void NotifyPortComplete() { port_complete_.Send(this); }
  void NotifyPortError() { port_error_.Send(this); }
  void SendCandidateError(const IceCandidateErrorEvent& event) { candidate_error_.Send(this, event); }
  void NotifySentPacket(const SentPacketInfo& info) { sent_packet_.Send(info); }

  // A packet from an address without a connection.
  void OnReadPacket(const ReceivedPacket& packet, ProtocolType proto);
  // True if `data` is a whole, correct STUN message. `out_msg` is set when
  // the caller should handle it (nullptr when the port already answered).
  bool GetStunMessage(Timestamp now, std::span<const uint8_t> data, const SocketAddress& addr,
                      std::unique_ptr<IceMessage>* out_msg, std::string* out_username);
  // Same family as the network; IPv6 link-local only with link-local.
  bool IsCompatibleAddress(const SocketAddress& addr) const;
  virtual Dscp StunDscpValue() const { return Dscp::kNoChange; }

 private:
  void FinishAddingAddress(const Candidate& c, bool is_final);
  void PostDestroyIfDead(Timestamp now, bool delayed);
  void DestroyIfDead(Timestamp now);
  void Destroy();

  PacketSocketFactory* const factory_;
  const IceCandidateType type_;
  bool send_retransmit_count_attribute_ = false;
  const Network* network_;
  const bool increase_host_srflx_priority_;
  uint16_t min_port_;
  uint16_t max_port_;
  std::string content_name_;
  int component_ = ICE_CANDIDATE_COMPONENT_DEFAULT;
  uint32_t generation_ = 0;
  std::string ice_username_fragment_;
  std::string password_;
  std::vector<Candidate> candidates_;
  int timeout_delay_ = kPortTimeoutDelay;
  bool enable_port_packets_ = false;
  IceRole ice_role_ = ICEROLE_UNKNOWN;
  const uint64_t ice_tiebreaker_;
  bool shared_socket_;
  int16_t network_cost_;
  enum class State { kInit, kKeepAliveUntilPruned, kPruned };
  State state_ = State::kInit;
  Timestamp last_time_all_connections_removed_ = Timestamp::MinusInfinity();
  std::multiset<Timestamp> destroy_checks_;  // The posted DestroyIfDead tasks.
  bool dead_ = false;

  PortCallbacks<Timestamp, Port*, const SocketAddress&, ProtocolType, IceMessage*, const std::string&, bool>
      unknown_address_;
  PortCallbacks<Port*, std::span<const uint8_t>, const SocketAddress&> read_packet_;
  PortCallbacks<const SentPacketInfo&> sent_packet_;
  PortCallbacks<Port*> port_destroyed_;
  PortCallbacks<Port*, const IceCandidateErrorEvent&> candidate_error_;
  PortCallbacks<Port*, const Candidate&> candidate_ready_;
  PortCallbacks<Port*> port_complete_;
  PortCallbacks<Port*> port_error_;
  std::function<void()> role_conflict_;
};

}  // namespace sm::p2p
