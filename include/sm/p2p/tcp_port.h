// ICE-TCP host candidates (issue #1151, milestone 3): libwebrtc M157's
// p2p/base/tcp_port.{h,cc} (compare/webrtc/reference/p2p/base/).
//
// A TCPPort gathers one TCP host candidate on its network (RFC 6544): with a
// listen socket (the allocator's allow_tcp_listen, true by default) a passive
// candidate at the listener's address; without one an active candidate at the
// network's best IP and the discard port 9. It makes a TCPConnection per
// remote candidate:
// - Outgoing (a remote passive candidate, or a remote candidate without a
//   tcptype and a real port): the connection opens a client socket to it
//   (AsyncTcpSocket, the RFC 4571 2-byte length framing RFC 6544 uses) and is
//   not connected until the socket's connect event. A remote active candidate
//   (not peer-reflexive) gets no connection: it connects to us.
// - Incoming: the listener's accepted sockets wait in the port, which answers
//   the STUN traffic on them as for an unknown address (Port::OnReadPacket
//   with PROTO_TCP); the ICE agent then makes a peer-reflexive active remote
//   candidate and a connection that takes the accepted socket over.
// - A connection whose socket closes is "pretending to be writable"
//   (tcp_port.cc's state diagram): an outgoing one reconnects on its next send
//   or check, and the connection is destroyed if it is still pretending
//   kConnectionWriteConnectTimeout (5 s) after the close. Data is refused
//   until the first STUN response on the new socket. A socket bound to an
//   address of another network is dropped (OnConnect), except loopback or the
//   any address.
// - Options set on the port (TCP_NODELAY, set by the constructor) go to the
//   accepted sockets; client sockets get TCP_NODELAY from the factory.
//
// The library's time model replaces libwebrtc's network thread: calls that
// may send take `now`; the owner calls Process(now) at NextProcessTime(),
// which runs what libwebrtc posts (the deferred FailAndPrune of a connection
// whose socket could not be made) and the reconnection timeouts. Socket close
// events carry no time, so a close is timed at the latest time the port has
// seen (`now` of its last call or packet), as TurnPort does.
//
// Not ported: SSLTCP's fake TLS (OPT_TLS_FAKE; the factory makes no such
// socket, 10.8), and the RTC_DCHECKs on threads.
#pragma once

#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/async_packet_socket.h"
#include "sm/p2p/connection.h"
#include "sm/p2p/port.h"

namespace sm::p2p {

class TCPConnection;

class TCPPort : public Port {
 public:
  static std::unique_ptr<TCPPort> Create(Timestamp now, const Parameters& args, uint16_t min_port, uint16_t max_port,
                                         bool allow_listen);
  ~TCPPort() override;

  Connection* CreateConnection(Timestamp now, const Candidate& address, CandidateOrigin origin) override;
  void PrepareAddress(Timestamp now) override;

  // Options apply to accepted sockets.
  int GetOption(SocketOption opt, int* value) override;
  int SetOption(SocketOption opt, int value) override;
  int GetError() override { return error_; }
  bool SupportsProtocol(std::string_view protocol) const override;
  ProtocolType GetProtocol() const override { return PROTO_TCP; }

  int SendTo(Timestamp now, std::span<const uint8_t> data, const SocketAddress& addr, const PacketOptions& options,
             bool payload) override;

  // Also runs the connections' posted tasks (theirs, not the port's: CancelPendingTasks leaves them, as the
  // connection's own task safety flag does in libwebrtc).
  void Process(Timestamp now) override;
  Timestamp NextProcessTime() const override;

  // The listen socket's address, nil without one (tests).
  SocketAddress ListenAddressForTesting() const;
  // Accepted sockets no connection has taken yet.
  size_t incoming_count_for_testing() const { return incoming_.size(); }

 protected:
  TCPPort(Timestamp now, const Parameters& args, uint16_t min_port, uint16_t max_port, bool allow_listen);

 private:
  friend class TCPConnection;

  struct Incoming {
    SocketAddress addr;
    std::unique_ptr<AsyncPacketSocket> socket;
  };

  void TryCreateServerSocket();
  void OnNewConnection(AsyncListenSocket* socket, std::unique_ptr<AsyncPacketSocket>& new_socket);
  // The accepted socket from `addr` (GetIncoming), or the same handed over to
  // a connection (TakeIncoming, libwebrtc's GetIncoming with `remove`).
  AsyncPacketSocket* GetIncoming(const SocketAddress& addr);
  std::unique_ptr<AsyncPacketSocket> TakeIncoming(const SocketAddress& addr);
  void OnReadPacket(AsyncPacketSocket* socket, const ReceivedPacket& packet);
  void OnSentPacket(const SentPacketInfo& sent_packet) { NotifySentPacket(sent_packet); }
  void Touch(Timestamp now) {
    if (now > last_now_) last_now_ = now;
  }
  Timestamp last_now() const { return last_now_; }

  bool allow_listen_;
  std::unique_ptr<AsyncListenSocket> listen_socket_;
  // Options to be applied to accepted sockets.
  std::map<SocketOption, int> socket_options_;
  int error_ = 0;
  std::list<Incoming> incoming_;
  Timestamp last_now_;
};

class TCPConnection : public Connection {
 public:
  // Connection is outgoing unless `socket` is given (an accepted one).
  TCPConnection(Timestamp now, TCPPort* port, const Candidate& candidate,
                std::unique_ptr<AsyncPacketSocket> socket = nullptr);
  ~TCPConnection() override;

  int Send(Timestamp now, std::span<const uint8_t> data, const PacketOptions& options) override;
  int GetError() override { return error_; }

  AsyncPacketSocket* socket() { return socket_.get(); }
  bool outgoing() const { return outgoing_; }
  bool pretending_to_be_writable() const { return pretending_to_be_writable_; }

  TimeDelta reconnection_timeout() const { return reconnection_timeout_; }
  void SetReconnectionTimeoutForTesting(TimeDelta timeout) { reconnection_timeout_ = timeout; }

 protected:
  // Clears pretending_to_be_writable_ to allow data packets again, in addition
  // to what Connection::OnConnectionRequestResponse does.
  void OnConnectionRequestResponse(Timestamp now, StunRequest* req, StunMessage* response) override;

 private:
  friend class TCPPort;  // For MaybeReconnect() and the posted tasks.

  // When a send or check finds the socket closed: reconnects an outgoing
  // connection that has no connect pending.
  void MaybeReconnect(Timestamp now);
  void CreateOutgoingTcpSocket(Timestamp now);
  void ConnectSocketSignals(AsyncPacketSocket* socket);
  void DisconnectSocketSignals(AsyncPacketSocket* socket);

  void OnConnect(AsyncPacketSocket* socket, Timestamp now);
  void OnClose(AsyncPacketSocket* socket, int error);
  void OnSentPacket(const SentPacketInfo& sent_packet);
  void OnReadPacket(AsyncPacketSocket* socket, const ReceivedPacket& packet);
  void OnDestroyed();

  TCPPort* tcp_port() { return static_cast<TCPPort*>(port()); }

  // The tasks libwebrtc posts, run by TCPPort::Process.
  Timestamp NextTaskTime(Timestamp port_now) const;
  // Runs the tasks due by `now`. False if the connection was destroyed.
  bool RunTasks(Timestamp now);

  std::unique_ptr<AsyncPacketSocket> socket_;
  int error_ = 0;
  const bool outgoing_;
  // Guard against multiple outgoing tcp connection during a reconnect.
  bool connection_pending_ = false;
  // Guard against data packets sent when we reconnect a TCP connection. During
  // reconnecting, when a new tcp connection has being made, we can't send data
  // packets out until the STUN binding is completed (i.e. the write state is
  // set to WRITABLE again by Connection::OnConnectionRequestResponse).
  bool pretending_to_be_writable_ = false;
  TimeDelta reconnection_timeout_;
  // The posted FailAndPrune (the socket could not be made).
  bool fail_and_prune_posted_ = false;
  // The delayed tasks OnClose posts: destroy if still pretending.
  std::vector<Timestamp> reconnection_deadlines_;
};

}  // namespace sm::p2p
