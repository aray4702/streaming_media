// sm/p2p/turn_port.h: libwebrtc M157's p2p/base/turn_port.cc and
// p2p/client/turn_port_factory.cc.
#include "sm/p2p/turn_port.h"

#include <algorithm>
#include <utility>

#include "sm/p2p/byte_buffer.h"

namespace sm::p2p {
namespace {

constexpr int TURN_ALLOCATE_REQUEST = STUN_ALLOCATE_REQUEST;
constexpr uint32_t kIpProtoUdp = 17;  // IPPROTO_UDP.
constexpr int kSocketError = -1;

bool IsTurnChannelData(uint16_t msg_type) { return (msg_type & 0xC000) == 0x4000; }  // MSB are 0b01.

uint16_t GetBE16(std::span<const uint8_t> p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

int GetRelayPreference(ProtocolType proto) {
  switch (proto) {
    case PROTO_TCP: return ICE_TYPE_PREFERENCE_RELAY_TCP;
    case PROTO_TLS: return ICE_TYPE_PREFERENCE_RELAY_TLS;
    default: return ICE_TYPE_PREFERENCE_RELAY_UDP;
  }
}

std::string_view ErrorReason(const StunMessage* response) {
  const StunErrorCodeAttribute* attr = response->GetErrorCode();
  return attr ? std::string_view(attr->reason()) : std::string_view();
}

}  // namespace

// ----------------------------------------------------------------- requests

class TurnAllocateRequest : public StunRequest {
 public:
  explicit TurnAllocateRequest(TurnPort* port);

 private:
  void OnResponse(Timestamp now, StunMessage* response) override;
  void OnErrorResponse(Timestamp now, StunMessage* response) override;
  void OnTimeout(Timestamp now) override;
  void OnAuthChallenge(Timestamp now, StunMessage* response, int code);
  void OnTryAlternate(StunMessage* response, int code);

  TurnPort* port_;
};

class TurnRefreshRequest : public StunRequest {
 public:
  explicit TurnRefreshRequest(TurnPort* port, int lifetime = -1);

 private:
  void OnResponse(Timestamp now, StunMessage* response) override;
  void OnErrorResponse(Timestamp now, StunMessage* response) override;
  void OnTimeout(Timestamp now) override;

  TurnPort* port_;
};

// A request about one TurnEntry, which may be deleted before it completes
// (libwebrtc's destroyed_callback_list_): the entry clears entry_ then.
class TurnEntryRequest : public StunRequest {
 public:
  TurnEntryRequest(TurnPort* port, TurnEntry* entry, int type);
  ~TurnEntryRequest() override;
  void EntryDestroyed() { entry_ = nullptr; }

 protected:
  TurnPort* const port_;
  TurnEntry* entry_;
};

class TurnCreatePermissionRequest : public TurnEntryRequest {
 public:
  TurnCreatePermissionRequest(TurnPort* port, TurnEntry* entry, const SocketAddress& ext_addr);

 private:
  void OnResponse(Timestamp now, StunMessage* response) override;
  void OnErrorResponse(Timestamp now, StunMessage* response) override;
  void OnTimeout(Timestamp now) override;
};

class TurnChannelBindRequest : public TurnEntryRequest {
 public:
  TurnChannelBindRequest(TurnPort* port, TurnEntry* entry, uint16_t channel_id, const SocketAddress& ext_addr);

 private:
  void OnResponse(Timestamp now, StunMessage* response) override;
  void OnErrorResponse(Timestamp now, StunMessage* response) override;
  void OnTimeout(Timestamp now) override;
};

// ------------------------------------------------------------------- entry

// A "connection" to one remote address: its permission and channel.
class TurnEntry {
 public:
  enum BindState { STATE_UNBOUND, STATE_BINDING, STATE_BOUND };
  TurnEntry(Timestamp now, TurnPort* port, Connection* conn, int channel_id)
      : port_(port), channel_id_(static_cast<uint16_t>(channel_id)), ext_addr_(conn->remote_candidate().address()),
        connections_({conn}) {
    SendCreatePermissionRequest(now, 0);  // Permission for ext_addr_.
  }
  ~TurnEntry() {
    for (TurnEntryRequest* r : requests_) r->EntryDestroyed();
  }

  uint16_t channel_id() const { return channel_id_; }
  const SocketAddress& address() const { return ext_addr_; }
  BindState state() const { return state_; }

  // Returns true if the entry was waiting to be deleted (no connections).
  bool TrackConnection(Connection* conn) {
    const bool was_empty = connections_.empty();
    connections_.push_back(conn);
    return was_empty;
  }
  // True if `conn` was the last connection.
  bool UntrackConnection(Connection* conn) {
    auto it = std::find(connections_.begin(), connections_.end(), conn);
    if (it != connections_.end()) connections_.erase(it);
    return connections_.empty();
  }

  void SendCreatePermissionRequest(Timestamp now, int delay) {
    port_->SendRequest(now, std::make_unique<TurnCreatePermissionRequest>(port_, this, ext_addr_), delay);
  }
  void SendChannelBindRequest(Timestamp now, int delay) {
    port_->SendRequest(now, std::make_unique<TurnChannelBindRequest>(port_, this, channel_id_, ext_addr_), delay);
  }

  // Sends `data` to ext_addr_, in a Send indication until the channel is bound.
  int Send(Timestamp now, std::span<const uint8_t> data, bool payload, const PacketOptions& options) {
    ByteBufferWriter buf;
    if (state_ != STATE_BOUND) {
      TurnMessage msg(TURN_SEND_INDICATION);
      msg.AddAttribute(std::make_unique<StunXorAddressAttribute>(STUN_ATTR_XOR_PEER_ADDRESS, ext_addr_));
      msg.AddAttribute(std::make_unique<StunByteStringAttribute>(STUN_ATTR_DATA, data));
      msg.Write(&buf);
      // Real data: ask for a channel to use later.
      if (state_ == STATE_UNBOUND && payload) {
        SendChannelBindRequest(now, 0);
        state_ = STATE_BINDING;
      }
    } else {
      buf.WriteUInt16(channel_id_);
      buf.WriteUInt16(static_cast<uint16_t>(data.size()));
      buf.Write(data);
    }
    return port_->Send(now, buf.DataView(), options);
  }

  void OnCreatePermissionSuccess(Timestamp now) {
    if (port_->callbacks_for_test_) port_->callbacks_for_test_->OnTurnCreatePermissionResult(TURN_SUCCESS_RESULT_CODE);
    // A bound channel's ChannelBind refreshes the permission.
    if (state_ != STATE_BOUND) {
      SendCreatePermissionRequest(now, static_cast<int>((kTurnPermissionTimeout - TimeDelta::Seconds(60)).ms()));
    }
  }
  void OnCreatePermissionError(Timestamp now, StunMessage* response, int code) {
    if (code == STUN_ERROR_STALE_NONCE) {
      if (port_->UpdateNonce(response)) SendCreatePermissionRequest(now, 0);
    } else {
      port_->FailAndPruneConnection(ext_addr_);
    }
    if (port_->callbacks_for_test_) port_->callbacks_for_test_->OnTurnCreatePermissionResult(code);
  }
  void OnCreatePermissionTimeout() { port_->FailAndPruneConnection(ext_addr_); }
  void OnChannelBindSuccess() { state_ = STATE_BOUND; }
  void OnChannelBindError(Timestamp now, StunMessage* response, int code) {
    // Other errors than a stale nonce fail and prune the connection; an ICE
    // restart re-establishes one if needed.
    if (code == STUN_ERROR_STALE_NONCE) {
      if (port_->UpdateNonce(response)) SendChannelBindRequest(now, 0);
    } else {
      state_ = STATE_UNBOUND;
      port_->FailAndPruneConnection(ext_addr_);
    }
  }
  void OnChannelBindTimeout() {
    state_ = STATE_UNBOUND;
    port_->FailAndPruneConnection(ext_addr_);
  }

  void AddRequest(TurnEntryRequest* r) { requests_.push_back(r); }
  void RemoveRequest(TurnEntryRequest* r) { std::erase(requests_, r); }

 private:
  TurnPort* const port_;
  const uint16_t channel_id_;
  const SocketAddress ext_addr_;
  BindState state_ = STATE_UNBOUND;
  std::vector<Connection*> connections_;
  std::vector<TurnEntryRequest*> requests_;
};

// ---------------------------------------------------------------- TurnPort

bool TurnPort::Validate(const CreateRelayPortArgs& args) {
  if (args.config->credentials.username.size() > static_cast<size_t>(kMaxTurnUsernameLength)) return false;
  if (!AllowedTurnPort(args.server_address->address.port())) return false;
  return true;
}

namespace {
Port::Parameters TurnParams(const CreateRelayPortArgs& args) {
  Port::Parameters p = args.port;
  p.ice_username_fragment = args.username;
  p.ice_password = args.password;
  return p;
}
}  // namespace

std::unique_ptr<TurnPort> TurnPort::Create(Timestamp now, const CreateRelayPortArgs& args,
                                           AsyncPacketSocket* socket) {
  if (!Validate(args)) return nullptr;
  return std::unique_ptr<TurnPort>(new TurnPort(now, TurnParams(args), socket, *args.server_address,
                                                args.config->credentials, args.relative_priority,
                                                args.config->tls_alpn_protocols, args.config->tls_elliptic_curves));
}

std::unique_ptr<TurnPort> TurnPort::Create(Timestamp now, const CreateRelayPortArgs& args, int min_port,
                                           int max_port) {
  if (!Validate(args)) return nullptr;
  Port::Parameters params = TurnParams(args);
  // libwebrtc's second Create passes no tiebreaker (zero).
  params.ice_tiebreaker = 0;
  return std::unique_ptr<TurnPort>(new TurnPort(
      now, params, static_cast<uint16_t>(min_port), static_cast<uint16_t>(max_port), *args.server_address,
      args.config->credentials, args.relative_priority, args.config->tls_alpn_protocols,
      args.config->tls_elliptic_curves));
}

TurnPort::TurnPort(Timestamp now, const Parameters& args, AsyncPacketSocket* socket,
                   const ProtocolAddress& server_address, const RelayCredentials& credentials, int server_priority,
                   const std::vector<std::string>& tls_alpn_protocols,
                   const std::vector<std::string>& tls_elliptic_curves)
    : Port(args, IceCandidateType::kRelay, now),
      server_address_(server_address),
      server_url_(ReconstructServerUrl()),
      tls_alpn_protocols_(tls_alpn_protocols),
      tls_elliptic_curves_(tls_elliptic_curves),
      credentials_(credentials),
      socket_(socket),
      request_manager_([this](Timestamp t, std::span<const uint8_t> data, StunRequest* request) {
        OnSendStunPacket(t, data, request);
      }),
      server_priority_(server_priority),
      last_now_(now) {}

TurnPort::TurnPort(Timestamp now, const Parameters& args, uint16_t min_port, uint16_t max_port,
                   const ProtocolAddress& server_address, const RelayCredentials& credentials, int server_priority,
                   const std::vector<std::string>& tls_alpn_protocols,
                   const std::vector<std::string>& tls_elliptic_curves)
    : Port(args, IceCandidateType::kRelay, min_port, max_port, /*shared_socket=*/false, now),
      server_address_(server_address),
      server_url_(ReconstructServerUrl()),
      tls_alpn_protocols_(tls_alpn_protocols),
      tls_elliptic_curves_(tls_elliptic_curves),
      credentials_(credentials),
      socket_(nullptr),
      request_manager_([this](Timestamp t, std::span<const uint8_t> data, StunRequest* request) {
        OnSendStunPacket(t, data, request);
      }),
      server_priority_(server_priority),
      last_now_(now) {}

TurnPort::~TurnPort() {
  // Release the allocation with a Refresh of lifetime 0.
  if (ready()) Release(last_now_);
  // The requests refer to the entries; drop them first.
  request_manager_.Clear();
  entries_.clear();
  if (socket_) {
    socket_->UnsubscribeSentPacket(this);
    socket_->UnsubscribeReadyToSend(this);
    socket_->UnsubscribeCloseEvent(this);
    socket_->UnsubscribeConnect(this);
    if (owned_socket_) owned_socket_->DeregisterReceivedPacketCallback();
  }
}

void TurnPort::set_realm(std::string_view realm) {
  // An empty realm is not supported (it reduces the hash's entropy): ignored.
  if (realm.empty()) return;
  if (realm != realm_) {
    realm_.assign(realm);
    UpdateHash();
  }
}

SocketAddress TurnPort::GetLocalAddress() const { return socket_ ? socket_->GetLocalAddress() : SocketAddress(); }

void TurnPort::PrepareAddress(Timestamp now) {
  Touch(now);
  if (credentials_.username.empty() || credentials_.password.empty()) {
    OnAllocateError(STUN_ERROR_UNAUTHORIZED, "Missing TURN server credentials.");
    return;
  }
  // The default TURN port if the address has none.
  if (!server_address_.address.port()) server_address_.address.SetPort(TURN_DEFAULT_PORT);
  if (!AllowedTurnPort(server_address_.address.port())) {
    // Disallowed ports are refused at creation and in SetAlternateServer.
    OnAllocateError(STUN_ERROR_SERVER_ERROR, "Attempt to start allocation to a disallowed port.");
    return;
  }
  // No DNS: the library's addresses are IP literals.
  if (!IsCompatibleAddress(server_address_.address)) {
    OnAllocateError(STUN_ERROR_NOT_AN_ERROR, "TURN server address is incompatible.");
    return;
  }
  // MaybeRequestLocalNetworkAccessPermissionForServer: granted.
  OnLocalNetworkAccessPermissionGranted(now);
}

void TurnPort::OnLocalNetworkAccessPermissionGranted(Timestamp now) {
  // The current address, against redirection ping-pong.
  attempted_server_addresses_.insert(server_address_.address);
  if (!CreateTurnClientSocket(now)) {
    OnAllocateError(STUN_ERROR_SERVER_NOT_REACHABLE, "Failed to create TURN client socket.");
    return;
  }
  if (server_address_.proto == PROTO_UDP) {
    // UDP: allocate now; TCP and TLS allocate in OnSocketConnect.
    SendRequest(now, std::make_unique<TurnAllocateRequest>(this), 0);
  }
}

bool TurnPort::CreateTurnClientSocket(Timestamp now) {
  if (server_address_.proto == PROTO_UDP && !SharedSocket()) {
    owned_socket_ = socket_factory()->CreateUdpSocket(SocketAddress(network()->GetBestIP(), 0), min_port(), max_port());
    socket_ = owned_socket_.get();
  } else if (server_address_.proto == PROTO_TCP || server_address_.proto == PROTO_TLS) {
    // Never on a shared socket (the allocator shares only UDP).
    int opts = PacketSocketFactory::kOptStun;
    // The server address's TLS and insecure bits.
    if (server_address_.proto == PROTO_TLS) {
      opts |= tls_cert_policy_ == TlsCertPolicy::TLS_CERT_POLICY_INSECURE_NO_CHECK ? PacketSocketFactory::kOptTlsInsecure
                                                                                  : PacketSocketFactory::kOptTls;
    }
    PacketSocketTcpOptions tcp_options;
    tcp_options.opts = opts;
    tcp_options.tls_alpn_protocols = tls_alpn_protocols_;
    tcp_options.tls_elliptic_curves = tls_elliptic_curves_;
    owned_socket_ = socket_factory()->CreateClientTcpSocket(now, SocketAddress(network()->GetBestIP(), 0),
                                                            server_address_.address, tcp_options);
    socket_ = owned_socket_.get();
  } else if (server_address_.proto != PROTO_UDP) {
    // PROTO_SSLTCP (and DTLS, which isn't ported): no socket.
    owned_socket_ = nullptr;
    socket_ = nullptr;
  }
  if (!socket_) {
    error_ = kSocketError;
    return false;
  }
  for (const auto& [opt, value] : socket_options_) socket_->SetOption(opt, value);
  if (!SharedSocket()) {
    // A shared socket's packets come through the allocation sequence.
    socket_->RegisterReceivedPacketCallback(
        [this](AsyncPacketSocket* s, const ReceivedPacket& packet) { OnReadPacket(s, packet); });
  }
  socket_->SubscribeReadyToSend(this, [this](AsyncPacketSocket* s) { OnReadyToSend(s); });
  socket_->SubscribeSentPacket(this,
                               [this](AsyncPacketSocket*, const SentPacketInfo& info) { NotifySentPacket(info); });
  // A closed socket is fatal for every protocol.
  socket_->SubscribeCloseEvent(this, [this](AsyncPacketSocket* s, int err) { OnSocketClose(s, err); });
  // TCP and TLS are ready to send STUN requests once connected, UDP once
  // the socket exists.
  if (server_address_.proto == PROTO_TCP || server_address_.proto == PROTO_TLS) {
    socket_->SubscribeConnect(this, [this](AsyncPacketSocket* s, Timestamp t) { OnSocketConnect(s, t); });
  } else {
    state_ = STATE_CONNECTED;
  }
  return true;
}

void TurnPort::OnSocketConnect(AsyncPacketSocket* socket, Timestamp now) {
  Touch(now);
  // Not on a socket bound to an address outside the network (a platform that
  // can't bind TCP picks the local address), except loopback (a proxy that
  // binds to localhost, webrtc issue 3927) and the any address (multiple
  // routes disabled, webrtc issue 4780). The same rule as TcpPort's.
  const SocketAddress socket_address = socket->GetLocalAddress();
  const auto& ips = network()->GetIps();
  if (std::none_of(ips.begin(), ips.end(), [&](const IpAddress& ip) { return socket_address.ipaddr() == ip; })) {
    if (socket_address.IsLoopbackIp()) {
      // Still allowed: localhost.
    } else if (IpIsAny(network()->GetBestIP())) {
      // Still allowed: the any address.
    } else {
      OnAllocateError(STUN_ERROR_SERVER_NOT_REACHABLE, "Address not associated with the desired network interface.");
      return;
    }
  }
  state_ = STATE_CONNECTED;  // Ready to send STUN requests.
  // (An unresolved server address takes the socket's remote address; the
  // library's addresses are always resolved.)
  SendRequest(now, std::make_unique<TurnAllocateRequest>(this), 0);
}

void TurnPort::OnSocketClose(AsyncPacketSocket* /*socket*/, int /*error*/) { Close(last_now_); }

void TurnPort::OnAllocateMismatch(Timestamp now) {
  if (allocate_mismatch_retries_ >= MAX_ALLOCATE_MISMATCH_RETRIES) {
    OnAllocateError(STUN_ERROR_ALLOCATION_MISMATCH, "Maximum retries reached for allocation mismatch.");
    return;
  }
  socket_->UnsubscribeCloseEvent(this);
  socket_->UnsubscribeReadyToSend(this);
  socket_->UnsubscribeSentPacket(this);
  socket_->UnsubscribeConnect(this);
  if (SharedSocket()) {
    ResetSharedSocket();
  } else if (owned_socket_) {
    owned_socket_->DeregisterReceivedPacketCallback();
  }
  owned_socket_ = nullptr;
  socket_ = nullptr;
  ResetNonce();
  PrepareAddress(now);
  ++allocate_mismatch_retries_;
}

Connection* TurnPort::CreateConnection(Timestamp now, const Candidate& remote_candidate, CandidateOrigin /*origin*/) {
  Touch(now);
  // TURN-UDP connects only to UDP candidates.
  if (!SupportsProtocol(remote_candidate.protocol())) return nullptr;
  if (state_ == STATE_DISCONNECTED || state_ == STATE_RECEIVEONLY) return nullptr;
  // (No mDNS candidates here: addresses carry no hostnames.)
  // The relay candidate of the remote candidate's family.
  for (size_t index = 0; index < Candidates().size(); ++index) {
    const Candidate& local_candidate = Candidates()[index];
    if (local_candidate.is_relay() && local_candidate.address().family() == remote_candidate.address().family()) {
      auto conn = std::make_unique<ProxyConnection>(now, this, index, remote_candidate);
      Connection* raw = conn.get();
      // An entry, so the permission is set up.
      if (CreateOrRefreshEntry(now, raw, next_channel_number_)) next_channel_number_++;
      AddOrReplaceConnection(std::move(conn));
      return raw;
    }
  }
  return nullptr;
}

bool TurnPort::FailAndPruneConnection(const SocketAddress& address) {
  if (Connection* conn = GetConnection(address)) {
    conn->FailAndPrune();
    return true;
  }
  return false;
}

int TurnPort::SetOption(SocketOption opt, int value) {
  // The latest DSCP, for STUN traffic.
  if (opt == SocketOption::kDscp) stun_dscp_value_ = static_cast<Dscp>(value);
  if (!socket_) {
    // Applied when the socket is made.
    socket_options_[opt] = value;
    return 0;
  }
  return socket_->SetOption(opt, value);
}

int TurnPort::GetOption(SocketOption opt, int* value) {
  if (!socket_) {
    auto it = socket_options_.find(opt);
    if (it == socket_options_.end()) return -1;
    *value = it->second;
    return 0;
  }
  return socket_->GetOption(opt, value);
}

int TurnPort::SendTo(Timestamp now, std::span<const uint8_t> data, const SocketAddress& addr,
                     const PacketOptions& options, bool payload) {
  Touch(now);
  TurnEntry* entry = FindEntry(addr);
  if (!ready() || entry == nullptr) {
    error_ = kSocketENotConn;
    return kSocketError;
  }
  const int sent = entry->Send(now, data, payload, options);
  if (sent <= 0) {
    error_ = socket_->GetError();
    return kSocketError;
  }
  // The caller expects the user data bytes, not the packet size.
  return static_cast<int>(data.size());
}

void TurnPort::SendBindingErrorResponse(Timestamp now, StunMessage* message, const SocketAddress& addr,
                                        int error_code, std::string_view reason) {
  if (!GetConnection(addr)) return;
  Port::SendBindingErrorResponse(now, message, addr, error_code, reason);
}

bool TurnPort::HandleIncomingPacket(AsyncPacketSocket* socket, const ReceivedPacket& packet) {
  // From a shared socket after this port moved to a socket of its own.
  if (socket != socket_) return false;
  Touch(packet.arrival_time);
  // A response from a previous server after a redirect.
  if (packet.source_address != server_address_.address) return false;
  // At least a channel header.
  if (packet.payload.size() < TURN_CHANNEL_HEADER_SIZE) return false;
  if (state_ == STATE_DISCONNECTED) return false;
  // Channel data, a data indication, or a response.
  const uint16_t msg_type = GetBE16(packet.payload);
  if (IsTurnChannelData(msg_type)) {
    HandleChannelData(msg_type, packet);
    return true;
  }
  if (msg_type == TURN_DATA_INDICATION) {
    HandleDataIndication(packet);
    return true;
  }
  if (SharedSocket() && (msg_type == STUN_BINDING_RESPONSE || msg_type == STUN_BINDING_ERROR_RESPONSE)) {
    // The shared socket's UDPPort takes binding responses.
    return false;
  }
  request_manager_.CheckResponse(packet.arrival_time, packet.payload);
  return true;
}

void TurnPort::OnReadPacket(AsyncPacketSocket* socket, const ReceivedPacket& packet) {
  HandleIncomingPacket(socket, packet);
}

void TurnPort::OnReadyToSend(AsyncPacketSocket* /*socket*/) {
  if (ready()) Port::OnReadyToSend();
}

bool TurnPort::SetAlternateServer(const SocketAddress& address) {
  // An address seen before is refused.
  if (attempted_server_addresses_.count(address)) return false;
  if (!IsCompatibleAddress(address)) return false;
  // No redirects to loopback (crbug.com/649118), to the any address (it
  // reaches the local host on some platforms), or to disallowed ports
  // (UDP redirects don't go through PrepareAddress).
  if (address.IsLoopbackIp()) return false;
  if (address.IsAnyIp()) return false;
  if (!AllowedTurnPort(address.port())) return false;
  server_address_ = ProtocolAddress(address, server_address_.proto);
  attempted_server_addresses_.insert(server_address_.address);
  return true;
}

void TurnPort::OnSendStunPacket(Timestamp now, std::span<const uint8_t> data, StunRequest* /*request*/) {
  PacketOptions options;
  options.dscp = StunDscpValue();
  Send(now, data, options);
}

void TurnPort::OnAllocateSuccess(const SocketAddress& address, const SocketAddress& stun_address) {
  state_ = STATE_READY;
  // A relay candidate's base is the candidate itself; the related address is
  // the server-reflexive one.
  AddAddress(address, address, stun_address, UDP_PROTOCOL_NAME, ProtoToString(server_address_.proto), "",
             IceCandidateType::kRelay, static_cast<uint32_t>(GetRelayPreference(server_address_.proto)),
             static_cast<uint32_t>(server_priority_), server_url_, true);
}

void TurnPort::OnAllocateError(int error_code, std::string_view reason) {
  // The port error goes asynchronously: this may run while the port is
  // being set up.
  Post([this](Timestamp) { NotifyPortError(); });
  const SocketAddress local = GetLocalAddress();
  std::string address = local.ipaddr().ToSensitiveString();
  int port = local.port();
  if (server_address_.proto != PROTO_UDP && server_address_.address.IsPrivateIp()) {
    address.clear();
    port = 0;
  }
  if (error_code != STUN_ERROR_NOT_AN_ERROR) {
    SendCandidateError(IceCandidateErrorEvent{address, port, server_url_, error_code, std::string(reason)});
  }
}

void TurnPort::OnRefreshError() {
  // Asynchronous, or the refresh request would be deleted twice.
  Post([this](Timestamp) { HandleRefreshError(); });
}

void TurnPort::HandleRefreshError() {
  request_manager_.Clear();
  state_ = STATE_RECEIVEONLY;
  // Fail and prune every connection; stop sending.
  std::vector<Connection*> conns;
  for (const auto& kv : connections()) conns.push_back(kv.second.get());
  for (Connection* c : conns) c->FailAndPrune();
}

void TurnPort::Release(Timestamp now) {
  Touch(now);
  request_manager_.Clear();
  SendRequest(now, std::make_unique<TurnRefreshRequest>(this, 0), 0);
  state_ = STATE_RECEIVEONLY;
}

void TurnPort::Close(Timestamp /*now*/) {
  if (!ready()) {
    OnAllocateError(STUN_ERROR_SERVER_NOT_REACHABLE,
                    GetProtocol() != PROTO_UDP ? "Failed to establish connection" : "Socket was closed");
  }
  request_manager_.Clear();
  // No new connections from now on.
  state_ = STATE_DISCONNECTED;
  // Delete the connections; stop sending.
  DestroyAllConnections();
  if (callbacks_for_test_) callbacks_for_test_->OnTurnPortClosed();
}

bool TurnPort::AllowedTurnPort(int port) {
  // 53, 80 and 443 are used by existing deployments; above 1024 is fine.
  return port == 53 || port == 80 || port == 443 || port >= 1024;
}

void TurnPort::TryAlternateServer(Timestamp now) {
  if (server_address_.proto == PROTO_UDP) {
    // UDP reuses the socket for the alternate server (PrepareAddress isn't
    // called; the permission it would request is granted).
    if (!connected()) return;  // The socket may have closed meanwhile.
    SendRequest(now, std::make_unique<TurnAllocateRequest>(this), 0);
    return;
  }
  // TCP and TLS: drop the connection and connect to the alternate server;
  // PrepareAddress sends the Allocate once the new socket connects. (Posted:
  // the old socket's handler may still be running.)
  if (socket_) {
    socket_->UnsubscribeCloseEvent(this);
    socket_->UnsubscribeReadyToSend(this);
    socket_->UnsubscribeSentPacket(this);
    socket_->UnsubscribeConnect(this);
    if (owned_socket_) owned_socket_->DeregisterReceivedPacketCallback();
  }
  owned_socket_ = nullptr;
  socket_ = nullptr;
  PrepareAddress(now);
}

void TurnPort::OnAllocateRequestTimeout() {
  OnAllocateError(STUN_ERROR_SERVER_NOT_REACHABLE, "TURN allocate request timed out.");
}

void TurnPort::HandleDataIndication(const ReceivedPacket& packet) {
  // RFC 5766 section 10.4.
  ByteBufferReader buf(packet.payload);
  TurnMessage msg;
  if (!msg.Read(&buf)) return;
  const StunAddressAttribute* addr_attr = msg.GetAddress(STUN_ATTR_XOR_PEER_ADDRESS);
  if (!addr_attr) return;
  const StunByteStringAttribute* data_attr = msg.GetByteString(STUN_ATTR_DATA);
  if (!data_attr) return;
  // From an address without a permission: libwebrtc logs it and still
  // dispatches.
  const SocketAddress ext_addr(addr_attr->GetAddress());
  ReceivedPacket unwrapped{data_attr->array_view(), ext_addr, packet.arrival_time, packet.decryption_info};
  DispatchPacket(unwrapped, PROTO_UDP);
}

void TurnPort::HandleChannelData(uint16_t channel_id, const ReceivedPacket& packet) {
  // RFC 5766 section 11.6: channel number, length, application data.
  const std::span<const uint8_t> payload = packet.payload;
  const uint16_t len = GetBE16(payload.subspan(2, 2));
  if (len > payload.size() - TURN_CHANNEL_HEADER_SIZE) return;
  // Longer than `len` is allowed: ChannelData can be padded.
  TurnEntry* entry = FindEntry(channel_id);
  if (!entry) return;
  ReceivedPacket unwrapped{payload.subspan(TURN_CHANNEL_HEADER_SIZE, len), entry->address(), packet.arrival_time,
                           packet.decryption_info};
  DispatchPacket(unwrapped, PROTO_UDP);
}

void TurnPort::DispatchPacket(const ReceivedPacket& packet, ProtocolType proto) {
  if (Connection* conn = GetConnection(packet.source_address)) {
    conn->OnReadPacket(packet);
  } else {
    Port::OnReadPacket(packet, proto);
  }
}

bool TurnPort::ScheduleRefresh(Timestamp now, uint32_t lifetime) {
  // Lifetime in seconds, delay in milliseconds.
  int delay = 1 * 60 * 1000;
  constexpr uint32_t max_lifetime = 60 * 60;  // Cut off above 1 h.
  if (lifetime < 2 * 60) {
    // The RFC sets no lower limit: refresh at half the lifetime.
    delay = static_cast<int>((lifetime * 1000) / 2);
  } else if (lifetime > max_lifetime) {
    // At most an hour: one minute less than that.
    delay = static_cast<int>((max_lifetime - 60) * 1000);
  } else {
    // One minute less than the lifetime.
    delay = static_cast<int>((lifetime - 60) * 1000);
  }
  SendRequest(now, std::make_unique<TurnRefreshRequest>(this), delay);
  return true;
}

void TurnPort::SendRequest(Timestamp now, std::unique_ptr<StunRequest> req, int delay_ms) {
  request_manager_.Send(now, std::move(req), TimeDelta::Millis(delay_ms));
}

void TurnPort::AddRequestAuthInfo(StunMessage* msg) {
  // The credentials the server gave us (realm, nonce) and the key.
  msg->AddAttribute(std::make_unique<StunByteStringAttribute>(STUN_ATTR_USERNAME, credentials_.username));
  msg->AddAttribute(std::make_unique<StunByteStringAttribute>(STUN_ATTR_REALM, realm_));
  msg->AddAttribute(std::make_unique<StunByteStringAttribute>(STUN_ATTR_NONCE, nonce_));
  msg->AddMessageIntegrity(hash_);
}

int TurnPort::Send(Timestamp now, std::span<const uint8_t> data, const PacketOptions& options) {
  if (!socket_) return kSocketError;
  return socket_->SendTo(now, data, server_address_.address, options);
}

void TurnPort::UpdateHash() {
  ComputeStunCredentialHash(credentials_.username, realm_, credentials_.password, &hash_);
}

bool TurnPort::UpdateNonce(StunMessage* response) {
  // A stale nonce: take the realm and nonce, and update the hash.
  const StunByteStringAttribute* realm_attr = response->GetByteString(STUN_ATTR_REALM);
  if (!realm_attr) return false;
  set_realm(realm_attr->string_view());
  const StunByteStringAttribute* nonce_attr = response->GetByteString(STUN_ATTR_NONCE);
  if (!nonce_attr) return false;
  set_nonce(nonce_attr->string_view());
  return true;
}

void TurnPort::ResetNonce() {
  hash_.clear();
  nonce_.clear();
  realm_.clear();
}

bool TurnPort::HasPermission(const IpAddress& ipaddr) const {
  return std::any_of(entries_.begin(), entries_.end(), [&](const auto& e) { return e->address().ipaddr() == ipaddr; });
}

TurnEntry* TurnPort::FindEntry(const SocketAddress& addr) const {
  auto it = std::find_if(entries_.begin(), entries_.end(), [&](const auto& e) { return e->address() == addr; });
  return it != entries_.end() ? it->get() : nullptr;
}

TurnEntry* TurnPort::FindEntry(uint16_t channel_id) const {
  auto it =
      std::find_if(entries_.begin(), entries_.end(), [&](const auto& e) { return e->channel_id() == channel_id; });
  return it != entries_.end() ? it->get() : nullptr;
}

bool TurnPort::CreateOrRefreshEntry(Timestamp now, Connection* conn, int channel_number) {
  TurnEntry* entry = FindEntry(conn->remote_candidate().address());
  if (entry == nullptr) {
    entries_.push_back(std::make_unique<TurnEntry>(now, this, conn, channel_number));
    return true;
  }
  // An entry waiting to be deleted is kept again.
  if (entry->TrackConnection(conn)) entry_deletions_.erase(entry);
  return false;
}

void TurnPort::HandleConnectionDestroyed(Connection* conn) {
  // The entry goes kTurnPermissionTimeout after its last connection, unless
  // a new connection takes it meanwhile.
  TurnEntry* entry = FindEntry(conn->remote_candidate().address());
  if (entry == nullptr) return;
  if (entry->UntrackConnection(conn)) entry_deletions_[entry] = last_now_ + kTurnPermissionTimeout;
}

void TurnPort::Post(std::function<void(Timestamp)> task) { posted_.push_back(std::move(task)); }

void TurnPort::CancelPendingTasks() {
  Port::CancelPendingTasks();
  request_manager_.Clear();
  posted_.clear();
  entry_deletions_.clear();
}

void TurnPort::Process(Timestamp now) {
  Touch(now);
  // Posted tasks first (they were due at once), then what may post more.
  for (int guard = 0; guard < 8 && !posted_.empty(); ++guard) {
    auto tasks = std::move(posted_);
    posted_.clear();
    for (auto& t : tasks) t(now);
  }
  for (auto it = entry_deletions_.begin(); it != entry_deletions_.end();) {
    if (it->second <= now) {
      TurnEntry* entry = it->first;
      it = entry_deletions_.erase(it);
      std::erase_if(entries_, [entry](const auto& e) { return e.get() == entry; });
    } else {
      ++it;
    }
  }
  request_manager_.Process(now);
  Port::Process(now);
}

Timestamp TurnPort::NextProcessTime() const {
  if (dead()) return Timestamp::PlusInfinity();
  Timestamp next = std::min(request_manager_.NextProcessTime(), Port::NextProcessTime());
  if (!posted_.empty()) next = std::min(next, last_now_);
  for (const auto& [entry, at] : entry_deletions_) next = std::min(next, at);
  return next;
}

std::string TurnPort::ReconstructServerUrl() const {
  // RFC 7065 section 3.1: scheme ":" host [ ":" port ] [ "?transport=" transport ].
  std::string scheme = "turn";
  std::string transport = "tcp";
  switch (server_address_.proto) {
    case PROTO_SSLTCP:
    case PROTO_TLS: scheme = "turns"; break;
    case PROTO_UDP: transport = "udp"; break;
    case PROTO_TCP: break;
  }
  const IpAddress& ip = server_address_.address.ipaddr();
  const std::string host = ip.family() == IpFamily::kV6 ? "[" + ip.ToString() + "]" : ip.ToString();
  return scheme + ":" + host + ":" + std::to_string(server_address_.address.port()) + "?transport=" + transport;
}

void TurnPort::MaybeAddTurnLoggingId(StunMessage* msg) {
  if (!turn_logging_id_.empty()) {
    msg->AddAttribute(
        std::make_unique<StunByteStringAttribute>(static_cast<uint16_t>(STUN_ATTR_TURN_LOGGING_ID), turn_logging_id_));
  }
}

// -------------------------------------------------------- request methods

TurnAllocateRequest::TurnAllocateRequest(TurnPort* port)
    : StunRequest(port->request_manager(), std::make_unique<TurnMessage>(TURN_ALLOCATE_REQUEST)), port_(port) {
  // RFC 5766 section 6.1.
  StunMessage* message = mutable_msg();
  auto transport_attr = StunAttribute::CreateUInt32(STUN_ATTR_REQUESTED_TRANSPORT);
  transport_attr->SetValue(kIpProtoUdp << 24);
  message->AddAttribute(std::move(transport_attr));
  port_->MaybeAddTurnLoggingId(message);
  if (!port_->hash().empty()) {
    port_->AddRequestAuthInfo(message);
  } else {
    SetAuthenticationRequired(false);
  }
}

void TurnAllocateRequest::OnResponse(Timestamp now, StunMessage* response) {
  // RFC 5766 section 6.3: the mandatory attributes.
  const StunAddressAttribute* mapped_attr = response->GetAddress(STUN_ATTR_XOR_MAPPED_ADDRESS);
  if (!mapped_attr) return;
  // (OnStunAddress does nothing in libwebrtc.)
  const StunAddressAttribute* relayed_attr = response->GetAddress(STUN_ATTR_XOR_RELAYED_ADDRESS);
  if (!relayed_attr) return;
  const StunUInt32Attribute* lifetime_attr = response->GetUInt32(STUN_ATTR_LIFETIME);
  if (!lifetime_attr) return;
  port_->OnAllocateSuccess(relayed_attr->GetAddress(), mapped_attr->GetAddress());
  port_->ScheduleRefresh(now, lifetime_attr->value());
}

void TurnAllocateRequest::OnErrorResponse(Timestamp now, StunMessage* response) {
  // RFC 5766 section 6.4.
  const int error_code = response->GetErrorCodeValue();
  switch (error_code) {
    case STUN_ERROR_UNAUTHORIZED: OnAuthChallenge(now, response, error_code); break;
    case STUN_ERROR_TRY_ALTERNATE: OnTryAlternate(response, error_code); break;
    case STUN_ERROR_ALLOCATION_MISMATCH: {
      // Asynchronous: the socket can't be deleted inside its own callback.
      TurnPort* port = port_;
      port->Post([port](Timestamp t) { port->OnAllocateMismatch(t); });
    } break;
    default: port_->OnAllocateError(error_code, "TURN allocate error."); break;
  }
}

void TurnAllocateRequest::OnTimeout(Timestamp /*now*/) { port_->OnAllocateRequestTimeout(); }

void TurnAllocateRequest::OnAuthChallenge(Timestamp now, StunMessage* response, int code) {
  // Unauthorized after sending our credentials: fail hard.
  if (code == STUN_ERROR_UNAUTHORIZED && !port_->hash().empty()) {
    (void)ErrorReason(response);
    port_->OnAllocateError(STUN_ERROR_UNAUTHORIZED, "Unauthorized.");
    return;
  }
  const StunByteStringAttribute* realm_attr = response->GetByteString(STUN_ATTR_REALM);
  if (!realm_attr) return;
  port_->set_realm(realm_attr->string_view());
  const StunByteStringAttribute* nonce_attr = response->GetByteString(STUN_ATTR_NONCE);
  if (!nonce_attr) return;
  port_->set_nonce(nonce_attr->string_view());
  // Again, with the realm and nonce.
  port_->SendRequest(now, std::make_unique<TurnAllocateRequest>(port_), 0);
}

void TurnAllocateRequest::OnTryAlternate(StunMessage* response, int /*code*/) {
  // RFC 5389 section 11: the response can't always be authenticated, so its
  // integrity is not checked.
  const StunAddressAttribute* alternate_server_attr = response->GetAddress(STUN_ATTR_ALTERNATE_SERVER);
  if (!alternate_server_attr) {
    port_->OnAllocateError(STUN_ERROR_TRY_ALTERNATE, "Missing alternate server attribute.");
    return;
  }
  if (!port_->SetAlternateServer(alternate_server_attr->GetAddress())) {
    port_->OnAllocateError(STUN_ERROR_TRY_ALTERNATE, "Failed to set alternate server.");
    return;
  }
  if (const StunByteStringAttribute* realm_attr = response->GetByteString(STUN_ATTR_REALM)) {
    port_->set_realm(realm_attr->string_view());
  }
  if (const StunByteStringAttribute* nonce_attr = response->GetByteString(STUN_ATTR_NONCE)) {
    port_->set_nonce(nonce_attr->string_view());
  }
  TurnPort* port = port_;
  port->Post([port](Timestamp t) { port->TryAlternateServer(t); });
}

TurnRefreshRequest::TurnRefreshRequest(TurnPort* port, int lifetime)
    : StunRequest(port->request_manager(), std::make_unique<TurnMessage>(TURN_REFRESH_REQUEST)), port_(port) {
  // RFC 5766 section 7.1.
  StunMessage* message = mutable_msg();
  if (lifetime > -1) {
    message->AddAttribute(std::make_unique<StunUInt32Attribute>(STUN_ATTR_LIFETIME, static_cast<uint32_t>(lifetime)));
  }
  port_->AddRequestAuthInfo(message);
}

void TurnRefreshRequest::OnResponse(Timestamp now, StunMessage* response) {
  // RFC 5766 section 7.3.
  const StunUInt32Attribute* lifetime_attr = response->GetUInt32(STUN_ATTR_LIFETIME);
  if (!lifetime_attr) return;
  if (lifetime_attr->value() > 0) {
    port_->ScheduleRefresh(now, lifetime_attr->value());
  } else {
    // A refresh with lifetime 0 releases the allocation (Release).
    TurnPort* port = port_;
    port->Post([port](Timestamp t) { port->Close(t); });
  }
  if (port_->callbacks_for_test_) port_->callbacks_for_test_->OnTurnRefreshResult(TURN_SUCCESS_RESULT_CODE);
}

void TurnRefreshRequest::OnErrorResponse(Timestamp now, StunMessage* response) {
  const int error_code = response->GetErrorCodeValue();
  if (error_code == STUN_ERROR_STALE_NONCE) {
    if (port_->UpdateNonce(response)) port_->SendRequest(now, std::make_unique<TurnRefreshRequest>(port_), 0);
  } else {
    port_->OnRefreshError();
    if (port_->callbacks_for_test_) port_->callbacks_for_test_->OnTurnRefreshResult(error_code);
  }
}

void TurnRefreshRequest::OnTimeout(Timestamp /*now*/) { port_->OnRefreshError(); }

TurnEntryRequest::TurnEntryRequest(TurnPort* port, TurnEntry* entry, int type)
    : StunRequest(port->request_manager(), std::make_unique<TurnMessage>(static_cast<uint16_t>(type))),
      port_(port),
      entry_(entry) {
  entry_->AddRequest(this);
}

TurnEntryRequest::~TurnEntryRequest() {
  if (entry_) entry_->RemoveRequest(this);
}

TurnCreatePermissionRequest::TurnCreatePermissionRequest(TurnPort* port, TurnEntry* entry,
                                                         const SocketAddress& ext_addr)
    : TurnEntryRequest(port, entry, TURN_CREATE_PERMISSION_REQUEST) {
  // RFC 5766 section 9.1.
  StunMessage* message = mutable_msg();
  message->AddAttribute(std::make_unique<StunXorAddressAttribute>(STUN_ATTR_XOR_PEER_ADDRESS, ext_addr));
  port_->AddRequestAuthInfo(message);
}

void TurnCreatePermissionRequest::OnResponse(Timestamp now, StunMessage* /*response*/) {
  if (entry_) entry_->OnCreatePermissionSuccess(now);
}

void TurnCreatePermissionRequest::OnErrorResponse(Timestamp now, StunMessage* response) {
  if (entry_) entry_->OnCreatePermissionError(now, response, response->GetErrorCodeValue());
}

void TurnCreatePermissionRequest::OnTimeout(Timestamp /*now*/) {
  if (entry_) entry_->OnCreatePermissionTimeout();
}

TurnChannelBindRequest::TurnChannelBindRequest(TurnPort* port, TurnEntry* entry, uint16_t channel_id,
                                               const SocketAddress& ext_addr)
    : TurnEntryRequest(port, entry, TURN_CHANNEL_BIND_REQUEST) {
  // RFC 5766 section 11.1.
  StunMessage* message = mutable_msg();
  message->AddAttribute(
      std::make_unique<StunUInt32Attribute>(STUN_ATTR_CHANNEL_NUMBER, static_cast<uint32_t>(channel_id) << 16));
  message->AddAttribute(std::make_unique<StunXorAddressAttribute>(STUN_ATTR_XOR_PEER_ADDRESS, ext_addr));
  port_->AddRequestAuthInfo(message);
}

void TurnChannelBindRequest::OnResponse(Timestamp now, StunMessage* /*response*/) {
  if (entry_) {
    entry_->OnChannelBindSuccess();
    // Refresh just under the permission timeout: the binding lives longer,
    // but this keeps both the channel and the permission.
    entry_->SendChannelBindRequest(now, static_cast<int>((kTurnPermissionTimeout - TimeDelta::Seconds(60)).ms()));
  }
}

void TurnChannelBindRequest::OnErrorResponse(Timestamp now, StunMessage* response) {
  if (entry_) entry_->OnChannelBindError(now, response, response->GetErrorCodeValue());
}

void TurnChannelBindRequest::OnTimeout(Timestamp /*now*/) {
  if (entry_) entry_->OnChannelBindTimeout();
}

// ------------------------------------------------------ REST credentials

namespace {
// RFC 4648 base64 with padding.
std::string Base64Encode(std::span<const uint8_t> in) {
  static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((in.size() + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 3 <= in.size(); i += 3) {
    const uint32_t v = (uint32_t{in[i]} << 16) | (uint32_t{in[i + 1]} << 8) | in[i + 2];
    out.push_back(kAlphabet[(v >> 18) & 63]);
    out.push_back(kAlphabet[(v >> 12) & 63]);
    out.push_back(kAlphabet[(v >> 6) & 63]);
    out.push_back(kAlphabet[v & 63]);
  }
  if (i < in.size()) {
    const bool two = i + 1 < in.size();
    const uint32_t v = (uint32_t{in[i]} << 16) | (two ? uint32_t{in[i + 1]} << 8 : 0u);
    out.push_back(kAlphabet[(v >> 18) & 63]);
    out.push_back(kAlphabet[(v >> 12) & 63]);
    out.push_back(two ? kAlphabet[(v >> 6) & 63] : '=');
    out.push_back('=');
  }
  return out;
}
}  // namespace

RelayCredentials CreateTurnRestCredentials(std::string_view shared_secret, std::string_view user,
                                           int64_t expiry_unix_seconds) {
  std::string username = std::to_string(expiry_unix_seconds);
  if (!user.empty()) {
    username += ':';
    username += user;
  }
  std::string password;
  uint8_t mac[20] = {};
  const auto hmac_sha1 = GetStunCrypto().hmac_sha1;
  if (hmac_sha1 != nullptr &&
      hmac_sha1(std::span(reinterpret_cast<const uint8_t*>(shared_secret.data()), shared_secret.size()),
                std::span(reinterpret_cast<const uint8_t*>(username.data()), username.size()), mac)) {
    password = Base64Encode(mac);
  }
  return RelayCredentials(username, password);
}

// --------------------------------------------------------- TurnPortFactory

std::unique_ptr<Port> TurnPortFactory::Create(Timestamp now, const CreateRelayPortArgs& args,
                                              AsyncPacketSocket* udp_socket) {
  auto port = TurnPort::Create(now, args, udp_socket);
  if (!port) return nullptr;
  port->SetTlsCertPolicy(args.config->tls_cert_policy);
  port->SetTurnLoggingId(args.config->turn_logging_id);
  return port;
}

std::unique_ptr<Port> TurnPortFactory::Create(Timestamp now, const CreateRelayPortArgs& args, int min_port,
                                              int max_port) {
  auto port = TurnPort::Create(now, args, min_port, max_port);
  if (!port) return nullptr;
  port->SetTlsCertPolicy(args.config->tls_cert_policy);
  port->SetTurnLoggingId(args.config->turn_logging_id);
  return port;
}

}  // namespace sm::p2p
