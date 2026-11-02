// sm/p2p/stun_port.h: libwebrtc M157's p2p/base/stun_port.cc.
#include "sm/p2p/stun_port.h"

#include <algorithm>
#include <utility>

namespace sm::p2p {
namespace {

// After an error response, retry for this long from the first request.
constexpr TimeDelta kRetryTimeout = TimeDelta::Seconds(50);
// UDPPort::SendTo stops logging after this many failures in a row.
constexpr int kSendErrorLogLimit = 5;

}  // namespace

// A binding request to a STUN server, and its keepalive successors.
class StunBindingRequest : public StunRequest {
 public:
  StunBindingRequest(UDPPort* port, const SocketAddress& addr, Timestamp start_time)
      : StunRequest(port->request_manager(), std::make_unique<StunMessage>(STUN_BINDING_REQUEST)),
        port_(port),
        server_addr_(addr),
        start_time_(start_time) {
    SetAuthenticationRequired(false);
  }
  const SocketAddress& server_addr() const { return server_addr_; }

 private:
  void OnResponse(Timestamp now, StunMessage* response) override {
    const StunAddressAttribute* addr_attr = response->GetAddress(STUN_ATTR_MAPPED_ADDRESS);
    if (addr_attr != nullptr && (addr_attr->family() == STUN_ADDRESS_IPV4 || addr_attr->family() == STUN_ADDRESS_IPV6)) {
      port_->OnStunBindingRequestSucceeded(Elapsed(now), server_addr_, addr_attr->GetAddress());
    }
    // Keepalives stop once the lifetime has passed.
    if (WithinLifetime(now)) {
      port_->request_manager_.Send(now, std::make_unique<StunBindingRequest>(port_, server_addr_, start_time_),
                                   port_->stun_keepalive_delay());
    }
  }
  void OnErrorResponse(Timestamp now, StunMessage* response) override {
    const StunErrorCodeAttribute* attr = response->GetErrorCode();
    port_->OnStunBindingOrResolveRequestFailed(
        server_addr_, attr ? attr->number() : STUN_ERROR_GLOBAL_FAILURE,
        attr ? std::string_view(attr->reason()) : std::string_view("STUN binding response with no error code attribute."));
    if (WithinLifetime(now) && now - start_time_ < kRetryTimeout) {
      port_->request_manager_.Send(now, std::make_unique<StunBindingRequest>(port_, server_addr_, start_time_),
                                   port_->stun_keepalive_delay());
    }
  }
  void OnTimeout(Timestamp) override {
    port_->OnStunBindingOrResolveRequestFailed(server_addr_, STUN_ERROR_SERVER_NOT_REACHABLE,
                                               "STUN binding request timed out.");
  }
  bool WithinLifetime(Timestamp now) const { return now - start_time_ <= port_->stun_keepalive_lifetime(); }

  UDPPort* port_;
  const SocketAddress server_addr_;
  const Timestamp start_time_;
};

std::unique_ptr<UDPPort> UDPPort::Create(Timestamp now, const Parameters& args, AsyncPacketSocket* socket,
                                         bool emit_local_for_anyaddress,
                                         std::optional<TimeDelta> stun_keepalive_interval) {
  auto port = std::unique_ptr<UDPPort>(
      new UDPPort(now, args, IceCandidateType::kHost, socket, emit_local_for_anyaddress));
  port->set_stun_keepalive_delay(stun_keepalive_interval);
  if (!port->Init(now)) return nullptr;
  return port;
}

std::unique_ptr<UDPPort> UDPPort::Create(Timestamp now, const Parameters& args, uint16_t min_port,
                                         uint16_t max_port, bool emit_local_for_anyaddress,
                                         std::optional<TimeDelta> stun_keepalive_interval) {
  auto port = std::unique_ptr<UDPPort>(
      new UDPPort(now, args, IceCandidateType::kHost, min_port, max_port, emit_local_for_anyaddress));
  port->set_stun_keepalive_delay(stun_keepalive_interval);
  if (!port->Init(now)) return nullptr;
  return port;
}

UDPPort::UDPPort(Timestamp now, const Parameters& args, IceCandidateType type, AsyncPacketSocket* socket,
                 bool emit_local_for_anyaddress)
    : Port(args, type, now),
      request_manager_([this](Timestamp t, std::span<const uint8_t> data, StunRequest* request) {
        SendStunRequest(t, data, request);
      }),
      socket_(socket),
      emit_local_for_anyaddress_(emit_local_for_anyaddress) {}

UDPPort::UDPPort(Timestamp now, const Parameters& args, IceCandidateType type, uint16_t min_port,
                 uint16_t max_port, bool emit_local_for_anyaddress)
    : Port(args, type, min_port, max_port, /*shared_socket=*/false, now),
      request_manager_([this](Timestamp t, std::span<const uint8_t> data, StunRequest* request) {
        SendStunRequest(t, data, request);
      }),
      socket_(nullptr),
      emit_local_for_anyaddress_(emit_local_for_anyaddress) {}

bool UDPPort::Init(Timestamp /*now*/) {
  stun_keepalive_lifetime_ = GetStunKeepaliveLifetime();
  if (!SharedSocket()) {
    owned_socket_ = socket_factory()->CreateUdpSocket(SocketAddress(network()->GetBestIP(), 0), min_port(), max_port());
    socket_ = owned_socket_.get();
    if (socket_ == nullptr) return false;
    socket_->RegisterReceivedPacketCallback(
        [this](AsyncPacketSocket*, const ReceivedPacket& packet) { OnReadPacket(packet); });
  }
  socket_->SubscribeSentPacket(this, [this](AsyncPacketSocket*, const SentPacketInfo& info) { NotifySentPacket(info); });
  // OnReadyToSend goes to the connections (#1000).
  socket_->SubscribeCloseEvent(this, [this](AsyncPacketSocket*, int error) { OnSocketClose(error); });
  return true;
}

UDPPort::~UDPPort() {
  if (socket_ == nullptr) return;
  socket_->UnsubscribeSentPacket(this);
  socket_->UnsubscribeCloseEvent(this);
  if (owned_socket_) owned_socket_->DeregisterReceivedPacketCallback();
}

void UDPPort::PrepareAddress(Timestamp now) {
  if (socket_->GetState() == AsyncPacketSocket::State::kBound) OnLocalAddressReady(now, socket_->GetLocalAddress());
}

void UDPPort::MaybePrepareStunCandidate(Timestamp now) {
  if (!server_addresses_.empty()) {
    SendStunBindingRequests(now);
  } else {
    MaybeSetPortCompleteOrError();  // No servers: done gathering.
  }
}

int UDPPort::SendTo(Timestamp now, std::span<const uint8_t> data, const SocketAddress& addr,
                    const PacketOptions& options, bool /*payload*/) {
  const int sent = socket_->SendTo(now, data, addr, options);
  if (sent < 0) {
    error_ = socket_->GetError();
    if (send_error_count_ < kSendErrorLogLimit) ++send_error_count_;
  } else {
    send_error_count_ = 0;
  }
  return sent;
}

void UDPPort::UpdateNetworkCost() {
  Port::UpdateNetworkCost();
  stun_keepalive_lifetime_ = GetStunKeepaliveLifetime();
}

int UDPPort::SetOption(SocketOption opt, int value) {
  if (opt == SocketOption::kDscp) dscp_ = static_cast<Dscp>(value);  // For the port's own packets.
  return socket_->SetOption(opt, value);
}

int UDPPort::GetOption(SocketOption opt, int* value) { return socket_->GetOption(opt, value); }

bool UDPPort::HandleIncomingPacket(AsyncPacketSocket* /*socket*/, const ReceivedPacket& packet) {
  // A UDP port takes every packet it is given.
  OnReadPacket(packet);
  return true;
}

void UDPPort::CancelPendingTasks() {
  Port::CancelPendingTasks();
  request_manager_.Clear();
}

void UDPPort::Process(Timestamp now) {
  request_manager_.Process(now);
  Port::Process(now);
}

Timestamp UDPPort::NextProcessTime() const {
  if (dead()) return Timestamp::PlusInfinity();
  return std::min(request_manager_.NextProcessTime(), Port::NextProcessTime());
}

void UDPPort::OnLocalAddressReady(Timestamp now, const SocketAddress& address) {
  // Bound to the any address, the default local address is the host
  // candidate when `emit_local_for_anyaddress`; if that fails the any address
  // stays, so at least the port listens.
  SocketAddress addr = address;
  MaybeSetDefaultLocalAddress(&addr);
  AddAddress(addr, addr, SocketAddress(), UDP_PROTOCOL_NAME, "", "", IceCandidateType::kHost,
             ICE_TYPE_PREFERENCE_HOST, 0, "", false);
  MaybePrepareStunCandidate(now);
}

void UDPPort::PostAddAddress(bool /*is_final*/) { MaybeSetPortCompleteOrError(); }

void UDPPort::OnReadPacket(const ReceivedPacket& packet) {
  // A packet from a STUN server is eaten even if it matches no request: it
  // may answer a retransmission whose first answer cleared the request.
  if (server_addresses_.count(packet.source_address)) {
    request_manager_.CheckResponse(packet.arrival_time, packet.payload);
    return;
  }
  // Packets for a connection go to it (#1000).
  Port::OnReadPacket(packet, PROTO_UDP);
}

void UDPPort::OnSocketClose(int error) {
  error_ = error;
  request_manager_.Clear();
  for (const SocketAddress& server : ServerAddresses(server_addresses_)) {
    if (!bind_request_succeeded_servers_.count(server) && !bind_request_failed_servers_.count(server)) {
      OnStunBindingOrResolveRequestFailed(server, STUN_ERROR_SERVER_NOT_REACHABLE, "UDP socket was closed.");
    }
  }
}

void UDPPort::SendStunBindingRequests(Timestamp now) {
  // Keeps pinging each server to hold the NAT binding open (see the lifetime).
  for (auto it = server_addresses_.begin(); it != server_addresses_.end();) {
    // A send may erase the current address; step past it first.
    const SocketAddress addr = *it++;
    SendStunBindingRequest(now, addr);
  }
}

void UDPPort::SendStunBindingRequest(Timestamp now, const SocketAddress& stun_addr) {
  if (socket_->GetState() != AsyncPacketSocket::State::kBound) return;
  if (!IsCompatibleAddress(stun_addr)) {
    // Not an error, like a family mismatch when pairing candidates: the
    // server is done.
    OnStunBindingOrResolveRequestFailed(stun_addr, STUN_ERROR_NOT_AN_ERROR, "STUN server address is incompatible.");
    return;
  }
  // MaybeRequestLocalNetworkAccessPermission: granted.
  request_manager_.Send(now, std::make_unique<StunBindingRequest>(this, stun_addr, now));
}

bool UDPPort::MaybeSetDefaultLocalAddress(SocketAddress* addr) const {
  if (!addr->IsAnyIp() || !emit_local_for_anyaddress_ || !network()->default_local_address().has_value()) {
    return true;
  }
  const IpAddress default_address = *network()->default_local_address();
  if (IpIsUnspec(default_address) || default_address.family() != addr->family()) return false;
  addr->SetIp(default_address);
  return true;
}

void UDPPort::OnStunBindingRequestSucceeded(TimeDelta rtt, const SocketAddress& stun_server_addr,
                                            const SocketAddress& stun_reflected_addr) {
  const int64_t rtt_ms = rtt.ms();
  stats_.stun_binding_responses_received++;
  stats_.stun_binding_rtt_ms_total += static_cast<double>(rtt_ms);
  stats_.stun_binding_rtt_ms_squared_total += static_cast<double>(rtt_ms * rtt_ms);
  if (bind_request_succeeded_servers_.count(stun_server_addr)) return;
  bind_request_succeeded_servers_.insert(stun_server_addr);
  // A shared socket's reflexive address equal to its local address is the
  // host candidate; an address another server gave is there already.
  if ((!SharedSocket() || stun_reflected_addr != socket_->GetLocalAddress()) &&
      !HasStunCandidateWithAddress(stun_reflected_addr)) {
    SocketAddress related_address = socket_->GetLocalAddress();
    // If the related address can't be stamped correctly, empty it rather
    // than leak the any address.
    if (!MaybeSetDefaultLocalAddress(&related_address)) {
      related_address = SocketAddress(GetAnyIp(related_address.family()), 0);
    }
    const std::string url = "stun:" + stun_server_addr.ipaddr().ToString() + ":" + std::to_string(stun_server_addr.port());
    AddAddress(stun_reflected_addr, socket_->GetLocalAddress(), related_address, UDP_PROTOCOL_NAME, "", "",
               IceCandidateType::kSrflx, ICE_TYPE_PREFERENCE_SRFLX, 0, url, false);
  }
  MaybeSetPortCompleteOrError();
}

void UDPPort::OnStunBindingOrResolveRequestFailed(const SocketAddress& stun_server_addr, int error_code,
                                                  std::string_view reason) {
  if (error_code != STUN_ERROR_NOT_AN_ERROR) {
    const SocketAddress local = GetLocalAddress();
    SendCandidateError(IceCandidateErrorEvent{local.ipaddr().ToSensitiveString(), local.port(),
                                              "stun:" + stun_server_addr.ToString(), error_code, std::string(reason)});
  }
  if (bind_request_failed_servers_.count(stun_server_addr)) return;
  bind_request_failed_servers_.insert(stun_server_addr);
  MaybeSetPortCompleteOrError();
}

void UDPPort::MaybeSetPortCompleteOrError() {
  if (ready_) return;
  // Still waiting for binding responses.
  if (server_addresses_.size() != bind_request_failed_servers_.size() + bind_request_succeeded_servers_.size()) {
    return;
  }
  ready_ = true;
  // Complete with no servers, a server that answered, or a shared socket.
  if (server_addresses_.empty() || !bind_request_succeeded_servers_.empty() || SharedSocket()) {
    NotifyPortComplete();
  } else {
    NotifyPortError();
  }
}

void UDPPort::SendStunRequest(Timestamp now, std::span<const uint8_t> data, StunRequest* req) {
  auto* sreq = static_cast<StunBindingRequest*>(req);
  PacketOptions options;
  options.dscp = StunDscpValue();
  SendTo(now, data, sreq->server_addr(), options, /*payload=*/true);
  stats_.stun_binding_requests_sent++;
}

bool UDPPort::HasStunCandidateWithAddress(const SocketAddress& addr) const {
  for (const Candidate& c : Candidates()) {
    if (c.is_stun() && c.address() == addr) return true;
  }
  return false;
}

std::unique_ptr<StunPort> StunPort::Create(Timestamp now, const Parameters& args, uint16_t min_port,
                                           uint16_t max_port, const ServerAddresses& servers,
                                           std::optional<TimeDelta> stun_keepalive_interval) {
  auto port = std::unique_ptr<StunPort>(new StunPort(now, args, min_port, max_port, servers));
  port->set_stun_keepalive_delay(stun_keepalive_interval);
  if (!port->Init(now)) return nullptr;
  return port;
}

StunPort::StunPort(Timestamp now, const Parameters& args, uint16_t min_port, uint16_t max_port,
                   const ServerAddresses& servers)
    : UDPPort(now, args, IceCandidateType::kSrflx, min_port, max_port, false) {
  set_server_addresses(servers);
}

void StunPort::PrepareAddress(Timestamp now) { SendStunBindingRequests(now); }

}  // namespace sm::p2p
