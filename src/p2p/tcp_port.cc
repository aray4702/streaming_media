// sm/p2p/tcp_port.h: libwebrtc M157's p2p/base/tcp_port.cc.
//
// tcp_port.cc's diagram of how TCP reconnect works for the active side (the
// passive side just waits for an incoming connection):
//
// - Connected: whether the TCP socket is connected.
// - Writable: whether the STUN binding is completed. Sending a data packet
//   before it would make the socket layer shut the connection down.
// - PendingTCP: `connection_pending_`, an outstanding TCP connect.
// - PretendWri: `pretending_to_be_writable_`. Marking the connection as
//   WRITE_TIMEOUT would delete it; instead it "pretends" to be writable for a
//   while, so that a reconnect can work.
//
// Data can only be sent in state 3. Sending in states 2 and 6 gets
// EWOULDBLOCK, in 4 and 5 EPIPE.
//
//         OS Timeout         7 -------------+
//   +----------------------->|Connected: N  |
//   |                        |Writable:  N  |     Timeout
//   |       Timeout          |Connection is |<----------------+
//   |   +------------------->|Dead          |                 |
//   |   |                    +--------------+                 |
//   |   |                               ^                     |
//   |   |            OnClose            |                     |
//   |   |    +-----------------------+  |                     |
//   |   |    |                       |  |Timeout              |
//   |   |    v                       |  |                     |
//   | 4 +----------+          5 -----+--+--+           6 -----+-----+
//   | |Connected: N|Send() or |Connected: N|           |Connected: Y|
//   | |Writable:  Y|Ping()    |Writable:  Y|OnConnect  |Writable:  Y|
//   | |PendingTCP:N+--------> |PendingTCP:Y+---------> |PendingTCP:N|
//   | |PretendWri:Y|          |PretendWri:Y|           |PretendWri:Y|
//   | +-----+------+          +------------+           +---+--+-----+
//   |   ^   ^                                              |  |
//   |   |   |                     OnClose                  |  |
//   |   |   +----------------------------------------------+  |
//   |   |                                                     |
//   |   |                              Stun Binding Completed |
//   |   |                                                     |
//   |   |                    OnClose                          |
//   |   +------------------------------------------------+    |
//   |                                                    |    v
//  1 -----------+           2 -----------+Stun      3 -----------+
//  |Connected: N|           |Connected: Y|Binding   |Connected: Y|
//  |Writable:  N|OnConnect  |Writable:  N|Completed |Writable:  Y|
//  |PendingTCP:Y+---------> |PendingTCP:N+--------> |PendingTCP:N|
//  |PretendWri:N|           |PretendWri:N|          |PretendWri:N|
//  +------------+           +------------+          +------------+
#include "sm/p2p/tcp_port.h"

#include <algorithm>
#include <utility>

#include "sm/p2p/candidate.h"
#include "sm/p2p/p2p_constants.h"

namespace sm::p2p {

// ------------------------------------------------------------------- TCPPort

std::unique_ptr<TCPPort> TCPPort::Create(Timestamp now, const Parameters& args, uint16_t min_port,
                                         uint16_t max_port, bool allow_listen) {
  return std::unique_ptr<TCPPort>(new TCPPort(now, args, min_port, max_port, allow_listen));
}

TCPPort::TCPPort(Timestamp now, const Parameters& args, uint16_t min_port, uint16_t max_port, bool allow_listen)
    : Port(args, IceCandidateType::kHost, min_port, max_port, /*shared_socket=*/false, now),
      allow_listen_(allow_listen),
      last_now_(now) {
  // libwebrtc: "TODO(mallinath) - Set preference value as per RFC 6544."
  if (allow_listen_) TryCreateServerSocket();
  // Set TCP_NODELAY (via OPT_NODELAY) for improved performance; this causes
  // small media packets to be sent immediately rather than being buffered up,
  // reducing latency.
  SetOption(SocketOption::kNoDelay, 1);
}

TCPPort::~TCPPort() {
  // The connections go first, while they can still reach this port (libwebrtc's
  // ~Port does it after ~TCPPort, where its connections hold weak pointers).
  DestroyAllConnections();
  if (listen_socket_) listen_socket_->UnsubscribeNewConnection(this);
  listen_socket_ = nullptr;
  incoming_.clear();
}

Connection* TCPPort::CreateConnection(Timestamp now, const Candidate& address, CandidateOrigin origin) {
  Touch(now);
  if (!SupportsProtocol(address.protocol())) return nullptr;

  if ((address.tcptype() == TCPTYPE_ACTIVE_STR && !address.is_prflx()) ||
      (address.tcptype().empty() && address.address().port() == 0)) {
    // It's active only candidate, we should not try to create connections
    // for these candidates.
    return nullptr;
  }

  // We can't accept TCP connections incoming on other ports.
  if (origin == ORIGIN_OTHER_PORT) return nullptr;

  // We don't know how to act as an ssl server yet.
  if (address.protocol() == SSLTCP_PROTOCOL_NAME && origin == ORIGIN_THIS_PORT) return nullptr;

  if (!IsCompatibleAddress(address.address())) return nullptr;

  std::unique_ptr<TCPConnection> conn;
  if (std::unique_ptr<AsyncPacketSocket> socket = TakeIncoming(address.address())) {
    // Incoming connection; we already created a socket and connected signals,
    // so we need to hand off the "read packet" responsibility to
    // TCPConnection.
    socket->DeregisterReceivedPacketCallback();
    conn = std::make_unique<TCPConnection>(now, this, address, std::move(socket));
  } else {
    // Outgoing connection, which will create a new socket.
    conn = std::make_unique<TCPConnection>(now, this, address);
  }
  TCPConnection* raw = conn.get();
  AddOrReplaceConnection(std::move(conn));
  return raw;
}

void TCPPort::PrepareAddress(Timestamp now) {
  Touch(now);
  if (listen_socket_) {
    // Socket may be in the CLOSED state if Listen() failed, we still want to
    // add the socket address.
    AddAddress(listen_socket_->GetLocalAddress(), listen_socket_->GetLocalAddress(), SocketAddress(),
               TCP_PROTOCOL_NAME, "", TCPTYPE_PASSIVE_STR, IceCandidateType::kHost, ICE_TYPE_PREFERENCE_HOST_TCP, 0, "",
               true);
  } else {
    // "Not listening due to firewall restrictions." We still add the address,
    // since otherwise the remote side won't recognize our incoming TCP
    // connections. According to RFC 6544 section 4.5, for an active candidate
    // the port must be set to the discard port, i.e. 9. We can't be 100% sure
    // which IP address will actually be used, so GetBestIP is as good as we can
    // do.
    AddAddress(SocketAddress(network()->GetBestIP(), DISCARD_PORT), SocketAddress(network()->GetBestIP(), 0),
               SocketAddress(), TCP_PROTOCOL_NAME, "", TCPTYPE_ACTIVE_STR, IceCandidateType::kHost,
               ICE_TYPE_PREFERENCE_HOST_TCP, 0, "", true);
  }
}

int TCPPort::SendTo(Timestamp now, std::span<const uint8_t> data, const SocketAddress& addr,
                    const PacketOptions& options, bool /*payload*/) {
  Touch(now);
  AsyncPacketSocket* socket = nullptr;
  TCPConnection* conn = static_cast<TCPConnection*>(GetConnection(addr));

  // For Connection, this is the code path used by Ping() to establish
  // WRITABLE. It has to send through the socket directly as
  // TCPConnection::Send checks writability.
  if (conn) {
    if (!conn->connected()) {
      conn->MaybeReconnect(now);
      return -1;  // SOCKET_ERROR
    }
    socket = conn->socket();
    if (!socket) {
      // "Attempted to send to an uninitialized socket."
      error_ = kSocketEHostUnreach;
      return -1;
    }
  } else {
    socket = GetIncoming(addr);
    if (!socket) {
      // "Attempted to send to an unknown destination."
      error_ = kSocketEHostUnreach;
      return -1;
    }
  }
  const int sent = socket->Send(now, data, options);
  if (sent < 0) {
    // Error from this code path for a Connection (instead of from a bare
    // socket) will not trigger reconnecting. In theory, this shouldn't matter
    // as OnClose should always be called and set connected to false.
    error_ = socket->GetError();
  }
  return sent;
}

int TCPPort::GetOption(SocketOption opt, int* value) {
  auto it = socket_options_.find(opt);
  if (it == socket_options_.end()) return -1;
  *value = it->second;
  return 0;
}

int TCPPort::SetOption(SocketOption opt, int value) {
  socket_options_[opt] = value;
  return 0;
}

bool TCPPort::SupportsProtocol(std::string_view protocol) const {
  return protocol == TCP_PROTOCOL_NAME || protocol == SSLTCP_PROTOCOL_NAME;
}

void TCPPort::OnNewConnection(AsyncListenSocket* /*socket*/, std::unique_ptr<AsyncPacketSocket>& new_socket) {
  if (!new_socket) return;  // Taken by another subscriber.
  for (const auto& [opt, value] : socket_options_) new_socket->SetOption(opt, value);
  Incoming incoming;
  incoming.addr = new_socket->GetRemoteAddress();
  incoming.socket = std::move(new_socket);
  incoming.socket->RegisterReceivedPacketCallback(
      [this](AsyncPacketSocket* s, const ReceivedPacket& packet) { OnReadPacket(s, packet); });
  incoming.socket->SubscribeReadyToSend(this, [this](AsyncPacketSocket*) { Port::OnReadyToSend(); });
  incoming.socket->SubscribeSentPacket(this,
                                       [this](AsyncPacketSocket*, const SentPacketInfo& info) { OnSentPacket(info); });
  // "Accepted connection from <addr>."
  incoming_.push_back(std::move(incoming));
}

void TCPPort::TryCreateServerSocket() {
  listen_socket_ = socket_factory()->CreateServerTcpSocket(SocketAddress(network()->GetBestIP(), 0), min_port(),
                                                           max_port(), /*opts=*/0);
  if (!listen_socket_) {
    // "TCP server socket creation failed; continuing anyway."
    return;
  }
  listen_socket_->SubscribeNewConnection(
      this, [this](AsyncListenSocket* listen_socket, std::unique_ptr<AsyncPacketSocket>& packet_socket) {
        OnNewConnection(listen_socket, packet_socket);
      });
}

AsyncPacketSocket* TCPPort::GetIncoming(const SocketAddress& addr) {
  for (Incoming& in : incoming_) {
    if (in.addr == addr) return in.socket.get();
  }
  return nullptr;
}

std::unique_ptr<AsyncPacketSocket> TCPPort::TakeIncoming(const SocketAddress& addr) {
  for (auto it = incoming_.begin(); it != incoming_.end(); ++it) {
    if (it->addr == addr) {
      std::unique_ptr<AsyncPacketSocket> socket = std::move(it->socket);
      incoming_.erase(it);
      return socket;
    }
  }
  return nullptr;
}

void TCPPort::OnReadPacket(AsyncPacketSocket* /*socket*/, const ReceivedPacket& packet) {
  Touch(packet.arrival_time);
  Port::OnReadPacket(packet, PROTO_TCP);
}

SocketAddress TCPPort::ListenAddressForTesting() const {
  return listen_socket_ ? listen_socket_->GetLocalAddress() : SocketAddress();
}

void TCPPort::Process(Timestamp now) {
  Touch(now);
  Port::Process(now);
  // The connections' posted tasks, earliest first. A task may destroy its
  // connection (now), so each round looks the next due one up again.
  for (int guard = 0; guard < 10000; ++guard) {
    TCPConnection* due = nullptr;
    Timestamp due_at = Timestamp::PlusInfinity();
    for (auto& [unused, conn] : connections()) {
      auto* tcp = static_cast<TCPConnection*>(conn.get());
      const Timestamp t = tcp->NextTaskTime(last_now_);
      if (t <= now && t < due_at) {
        due = tcp;
        due_at = t;
      }
    }
    if (due == nullptr) break;
    due->RunTasks(now);
  }
}

Timestamp TCPPort::NextProcessTime() const {
  if (dead()) return Timestamp::PlusInfinity();
  Timestamp t = Port::NextProcessTime();
  for (const auto& [unused, conn] : connections()) {
    t = std::min(t, static_cast<const TCPConnection*>(conn.get())->NextTaskTime(last_now_));
  }
  return t;
}

// ------------------------------------------------------------- TCPConnection

// libwebrtc: "TODO(qingsi): `kConnectionWriteConnectTimeout` is overriden by
// `ice_unwritable_timeout` in IceConfig when determining the writability
// state." The reconnection timeout stays this constant there too.
TCPConnection::TCPConnection(Timestamp now, TCPPort* port, const Candidate& candidate,
                             std::unique_ptr<AsyncPacketSocket> socket)
    : Connection(now, port, 0, candidate),
      socket_(std::move(socket)),
      outgoing_(socket_ == nullptr),
      reconnection_timeout_(kConnectionWriteConnectTimeout) {
  SubscribeDestroyed(this, [this](Connection*) { OnDestroyed(); });
  if (outgoing_) {
    CreateOutgoingTcpSocket(now);
  } else {
    // Incoming connections should match one of the network addresses (a
    // DCHECK in libwebrtc, the same check OnConnect makes).
    ConnectSocketSignals(socket_.get());
  }
}

TCPConnection::~TCPConnection() {
  if (socket_) DisconnectSocketSignals(socket_.get());
}

int TCPConnection::Send(Timestamp now, std::span<const uint8_t> data, const PacketOptions& options) {
  if (!socket_) {
    error_ = kSocketENotConn;
    return -1;  // SOCKET_ERROR
  }

  // Sending after OnClose on active side will trigger a reconnect for a
  // outgoing connection. Note that the write state is still WRITABLE as we want
  // to spend a few seconds attempting a reconnect before saying we're
  // unwritable.
  if (!connected()) {
    MaybeReconnect(now);
    return -1;
  }

  // Note that this is important to put this after the previous check to give
  // the connection a chance to reconnect.
  if (pretending_to_be_writable_ || write_state() != STATE_WRITABLE) {
    error_ = kSocketENotConn;
    return -1;
  }
  if (port() != nullptr) tcp_port()->Touch(now);
  mutable_stats().sent_total_packets++;
  const int sent = socket_->Send(now, data, options);
  if (sent < 0) {
    mutable_stats().sent_discarded_packets++;
    error_ = socket_->GetError();
  } else {
    AddSentBytesToStats(sent, now);
  }
  set_last_send_data(now);
  return sent;
}

void TCPConnection::OnSentPacket(const SentPacketInfo& sent_packet) {
  if (port() != nullptr) tcp_port()->OnSentPacket(sent_packet);
}

void TCPConnection::OnConnectionRequestResponse(Timestamp now, StunRequest* req, StunMessage* response) {
  // Process the STUN response before we inform upper layer ready to send.
  Connection::OnConnectionRequestResponse(now, req, response);

  // If we're in the state of pretending to be writeable, we should inform the
  // upper layer it's ready to send again as previous EWOULDLBLOCK from socket
  // would have stopped the outgoing stream.
  if (pretending_to_be_writable_) Connection::OnReadyToSend();
  pretending_to_be_writable_ = false;
}

void TCPConnection::OnConnect(AsyncPacketSocket* socket, Timestamp now) {
  if (port() == nullptr) return;  // "TCPConnection: Port has been deleted."
  tcp_port()->Touch(now);

  // Do not use this port if the socket bound to an address not associated with
  // the desired network interface. This is seen in Chrome, where TCP sockets
  // cannot be given a binding address, and the platform is expected to pick
  // the correct local address.
  //
  // However, there are two situations in which we allow the bound address to
  // not be one of the addresses of the requested interface:
  // 1. The bound address is the loopback address. This happens when a proxy
  // forces TCP to bind to only the localhost address (see issue 3927).
  // 2. The bound address is the "any address". This happens when
  // multiple_routes is disabled (see issue 4780).
  //
  // Note that, aside from minor differences in log statements, this logic is
  // identical to that in TurnPort.
  const SocketAddress socket_address = socket->GetLocalAddress();
  const std::vector<IpAddress>& ips = port()->network()->GetIps();
  if (std::find(ips.begin(), ips.end(), socket_address.ipaddr()) == ips.end() && !socket_address.IsLoopbackIp() &&
      !IpIsAny(port()->network()->GetBestIP())) {
    // "Dropping connection as TCP socket bound to IP ..., rather than an
    // address associated with network."
    OnClose(socket, 0);
    return;
  }

  // Connection is established successfully.
  set_connected(true);
  connection_pending_ = false;
}

void TCPConnection::OnClose(AsyncPacketSocket* /*socket*/, int /*error*/) {
  // "Connection closed with error <error>."
  if (port() == nullptr) return;  // "TCPConnection: Port has been deleted."
  const Timestamp now = tcp_port()->last_now();

  // Guard against the condition where IPC socket will call OnClose for every
  // packet it can't send.
  if (connected()) {
    set_connected(false);

    // Prevent the connection from being destroyed by redundant SignalClose
    // events.
    pretending_to_be_writable_ = true;

    // If this connection can't become connected and writable again in 5
    // seconds, it's time to tear this down. This is the case for the original
    // TCP connection on passive side during a reconnect.
    // We don't attempt reconnect right here. This is to avoid a case where the
    // shutdown is intentional and reconnect is not necessary. We only reconnect
    // when the connection is used to Send() or Ping().
    reconnection_deadlines_.push_back(now + reconnection_timeout());
  } else if (!pretending_to_be_writable_) {
    // OnClose could be called when the underneath socket times out during the
    // initial connect() (i.e. `pretending_to_be_writable_` is false) . We have
    // to manually destroy here as this connection, as never connected, will not
    // be scheduled for ping to trigger destroy.
    DisconnectSocketSignals(socket_.get());
    port()->DestroyConnectionAsync(now, this);
  }
}

void TCPConnection::MaybeReconnect(Timestamp now) {
  // Only reconnect for an outgoing TCPConnection when OnClose was signaled and
  // no outstanding reconnect is pending.
  if (connected() || connection_pending_ || !outgoing_) return;

  // "TCP Connection with remote is closed, trying to reconnect."
  CreateOutgoingTcpSocket(now);
  error_ = kSocketEPipe;
}

void TCPConnection::OnReadPacket(AsyncPacketSocket* /*socket*/, const ReceivedPacket& packet) {
  if (port() != nullptr) tcp_port()->Touch(packet.arrival_time);
  Connection::OnReadPacket(packet);
}

void TCPConnection::OnDestroyed() {
  if (socket_) DisconnectSocketSignals(socket_.get());
}

void TCPConnection::CreateOutgoingTcpSocket(Timestamp now) {
  if (port() == nullptr) return;
  tcp_port()->Touch(now);
  // SSLTCP would ask for fake TLS (OPT_TLS_FAKE), which the factory does not
  // make (sm/p2p/async_tcp_socket.h): the socket fails as below.
  const int opts =
      remote_candidate().protocol() == SSLTCP_PROTOCOL_NAME ? static_cast<int>(PacketSocketFactory::kOptTlsFake) : 0;

  if (socket_) DisconnectSocketSignals(socket_.get());

  PacketSocketTcpOptions tcp_opts;
  tcp_opts.opts = opts;
  socket_ = port()->socket_factory()->CreateClientTcpSocket(now, SocketAddress(port()->network()->GetBestIP(), 0),
                                                            remote_candidate().address(), tcp_opts);
  if (socket_) {
    // "Connecting from <local> to <remote>."
    set_connected(false);
    connection_pending_ = true;
    ConnectSocketSignals(socket_.get());
  } else {
    // "Failed to create connection to <remote>."
    set_state(IceCandidatePairState::FAILED);
    // We can't FailAndPrune directly here. FailAndPrune and deletes all
    // the StunRequests from the request_map_. And if this is in the stack
    // of Connection::Ping(), we are still using the request.
    // Unwind the stack and defer the FailAndPrune.
    fail_and_prune_posted_ = true;
  }
}

void TCPConnection::ConnectSocketSignals(AsyncPacketSocket* socket) {
  // Incoming connections register SignalSentPacket and SignalReadyToSend
  // directly on the port in TCPPort::OnNewConnection.
  if (outgoing_) {
    socket->SubscribeConnect(this, [this](AsyncPacketSocket* s, Timestamp now) { OnConnect(s, now); });
    socket->SubscribeSentPacket(this, [this](AsyncPacketSocket*, const SentPacketInfo& info) { OnSentPacket(info); });
    socket->SubscribeReadyToSend(this, [this](AsyncPacketSocket*) { Connection::OnReadyToSend(); });
  }

  // For incoming connections, this re-register ReceivedPacketCallback to the
  // connection instead of the port.
  socket->RegisterReceivedPacketCallback(
      [this](AsyncPacketSocket* s, const ReceivedPacket& packet) { OnReadPacket(s, packet); });
  socket->SubscribeCloseEvent(this, [this](AsyncPacketSocket* s, int err) { OnClose(s, err); });
}

void TCPConnection::DisconnectSocketSignals(AsyncPacketSocket* socket) {
  if (outgoing_) {
    // Incoming connections do not register these signals in TCPConnection.
    socket->UnsubscribeConnect(this);
    socket->UnsubscribeReadyToSend(this);
    socket->UnsubscribeSentPacket(this);
  }
  socket->DeregisterReceivedPacketCallback();
  socket->UnsubscribeCloseEvent(this);
}

Timestamp TCPConnection::NextTaskTime(Timestamp port_now) const {
  Timestamp t = Timestamp::PlusInfinity();
  if (fail_and_prune_posted_) t = port_now;
  for (Timestamp d : reconnection_deadlines_) t = std::min(t, d);
  return t;
}

bool TCPConnection::RunTasks(Timestamp now) {
  if (fail_and_prune_posted_) {
    fail_and_prune_posted_ = false;
    FailAndPrune();
  }
  // The reconnection timeouts that are due, earliest first.
  std::sort(reconnection_deadlines_.begin(), reconnection_deadlines_.end());
  while (!reconnection_deadlines_.empty() && reconnection_deadlines_.front() <= now) {
    const Timestamp at = reconnection_deadlines_.front();
    reconnection_deadlines_.erase(reconnection_deadlines_.begin());
    if (pretending_to_be_writable_ && port() != nullptr) {
      Destroy(at);  // Deletes this connection.
      return false;
    }
  }
  return true;
}

}  // namespace sm::p2p
