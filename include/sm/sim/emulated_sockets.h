// UDP and TCP sockets over the emulated network (UDP: issue #996, part of
// ICE #990; TCP: issue #1034, C4 part 2; milestone 3).
//
// `EmulatedSocketServer` is one host's socket layer on `sim::Network`
// (network.h): a `p2p::PacketSocketFactory` whose sockets bind ports on the
// host and send and receive datagrams through its access links and the NATs
// between it and the peer. It is libwebrtc's VirtualSocketServer
// (compare/webrtc/reference/rtc_base/virtual_socket_server.cc) for one host,
// with libwebrtc's port rules:
//
// - **Binding** (VirtualSocket::Bind, VirtualSocketServer::AssignBindAddress):
//   port 0 takes the next free ephemeral port, counting up from 49152 and
//   wrapping after 65535; a port in use fails with EADDRINUSE. The factory
//   tries [min_port, max_port] in order when given a range
//   (BasicPacketSocketFactory::BindSocket). A socket may bind the host's
//   address or the any address. Unlike the virtual server, which has no
//   routes, any other address fails with EADDRNOTAVAIL: the emulated network
//   only delivers to the host's address.
// - **Sending** (VirtualSocketServer::SendUdp): from a socket bound to the
//   any address, the datagram's source is the host's address (the default
//   route). The wire size is the payload plus the IPv4 and UDP headers
//   (28 bytes), which the access links charge.
// - **Receiving**: `Deliver(now)` hands each datagram that has reached the
//   host by `now` to the socket bound to its destination port (the host's
//   address or any), stamped with `now`. Datagrams to a port with no socket
//   are dropped and counted. Drive it at `NextEvent()` for exact arrival
//   times.
//
// **TCP** (#1034) is VirtualSocketServer's stream model
// (VirtualSocket::SendTcp, VirtualSocketServer::SendTcp, Connect, Accept,
// Disconnect) carried as segments over the same hosts, links and NATs:
//
// - **Ports**: TCP and UDP share the host's port table, as the virtual
//   server's one bindings map does, so a TCP and a UDP socket never hold the
//   same port. A client binds as UDP does (or the any address with an
//   ephemeral port on Connect); accepted sockets share the listener's port
//   and are found by (local port, remote address), as `connections_` there.
// - **Buffers and window** (kDefaultTcpBufferSize, TCP_MSS,
//   TCP_HEADER_SIZE): a 32 KiB send buffer of bytes not yet on the wire and
//   a 32 KiB receive buffer; the sender keeps at most the receiver's free
//   space in flight; segments carry at most 1400 - 40 = 1360 bytes, and the
//   links charge each one its payload plus 40 header bytes. A full send
//   buffer fails Send with EWOULDBLOCK, and a write event follows when it
//   has room again. Reading a receive buffer that was full reopens the
//   window (the virtual server's `was_full`).
// - **Connect and accept**: a SYN goes to the destination; a listening
//   socket queues it and raises a read event, and Accept answers with a
//   SYN-ACK whose arrival completes the client's connect (a connect event).
//   The accepted socket is connected at once, as the virtual server's is. A
//   SYN to a port with no listener is answered with a reset, and the
//   client's connect fails with ECONNREFUSED ("No one listening").
// - **Close**: Close sends a FIN, and the peer closes with error 0 when it
//   arrives (PostDisconnect); data not yet on the wire is dropped with the
//   socket, as the virtual server drops the send buffer. A segment for a
//   connection that no longer exists is answered with a reset.
// - **NATs**: segments cross the NATs as datagrams do, with the same
//   mapping and filtering rules (network.h). A client behind any of the test
//   NAT types connects out; a connection into a NAT from outside finds no
//   binding and times out, as with libwebrtc's NAT proxy, which only makes
//   outbound TCP connections.
//
// One addition the virtual server does not need: its TCP never loses a
// segment, while the emulated links do. So the stream recovers: cumulative
// acknowledgements, retransmission after RFC 6298's timeout (1 s at first
// and at least, doubled on each retry up to 60 s, from smoothed RTT samples
// that skip retransmitted segments), go-back-N from the first unacknowledged
// byte on a timeout or the third duplicate ACK, and a window probe while the
// receiver's window is closed. A SYN is retried 6 times and data 15 (Linux's
// tcp_syn_retries and tcp_retries2) before the socket closes with
// ETIMEDOUT. The receiver keeps only in-order data. On a link without loss
// none of this runs. Close's FIN is sent once.
//
// Like every module, it takes `now`, reads no clock and starts no thread. A
// test bench building block: the media test bench does not use it yet.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/async_packet_socket.h"
#include "sm/p2p/async_tcp_socket.h"
#include "sm/p2p/socket.h"
#include "sm/p2p/socket_address.h"
#include "sm/sim/network.h"

namespace sm::sim {

using Bytes = std::vector<uint8_t>;

// What the emulated network carries: a UDP payload, or a TCP segment with
// the fields the stream model needs (byte sequence numbers from 0, so they
// never wrap).
struct Wire {
  enum class Proto : uint8_t { kUdp, kTcp };
  enum Flags : uint8_t { kSyn = 1, kAck = 2, kFin = 4, kRst = 8, kProbe = 16 };
  Proto proto = Proto::kUdp;
  uint8_t flags = 0;
  uint64_t seq = 0;
  uint64_t ack = 0;
  uint32_t window = 0;
  Bytes data;
};
using ByteNetwork = Network<Wire>;

// Between the emulated network's IPv4 addresses and the library's.
inline p2p::SocketAddress ToP2p(const SocketAddress& a) { return p2p::SocketAddress(a.ip, a.port); }
inline SocketAddress FromP2p(const p2p::SocketAddress& a) { return SocketAddress{a.ip(), a.port()}; }

class EmulatedSocketServer;
class EmulatedTcpSocket;

class EmulatedUdpSocket final : public p2p::AsyncPacketSocket {
 public:
  ~EmulatedUdpSocket() override;

  p2p::SocketAddress GetLocalAddress() const override { return local_; }
  p2p::SocketAddress GetRemoteAddress() const override { return {}; }
  // UDP sockets from the factory are never connected (AsyncUDPSocket::Send
  // on an unconnected socket fails with ENOTCONN).
  int Send(Timestamp now, std::span<const uint8_t>, const p2p::PacketOptions& options) override {
    Sent(now, 0, options);
    error_ = p2p::kSocketENotConn;
    return -1;
  }
  int SendTo(Timestamp now, std::span<const uint8_t> data, const p2p::SocketAddress& addr,
             const p2p::PacketOptions& options) override;
  int Close() override;
  State GetState() const override { return server_ ? State::kBound : State::kClosed; }
  int GetOption(p2p::SocketOption opt, int* value) override {
    auto it = options_.find(opt);
    if (it == options_.end()) return -1;
    *value = it->second;
    return 0;
  }
  int SetOption(p2p::SocketOption opt, int value) override {
    options_[opt] = value;
    return 0;
  }
  int GetError() const override { return error_; }
  void SetError(int error) override { error_ = error; }

 private:
  friend class EmulatedSocketServer;
  EmulatedUdpSocket(EmulatedSocketServer* server, p2p::SocketAddress local) : server_(server), local_(local) {}
  void Receive(const p2p::ReceivedPacket& p) { NotifyPacketReceived(p); }
  // AsyncUDPSocket::SendTo reports every send, before the result is known.
  void Sent(Timestamp now, size_t bytes, const p2p::PacketOptions& options) {
    NotifySentPacket(
        p2p::SentPacketInfo{options.packet_id, now, bytes, static_cast<size_t>(local_.ipaddr().overhead())});
  }

  EmulatedSocketServer* server_;  // nullptr once closed.
  p2p::SocketAddress local_;
  std::map<p2p::SocketOption, int> options_;
  int error_ = 0;
};

// A TCP socket on the emulated network (VirtualSocket with SOCK_STREAM; see
// the file comment).
class EmulatedTcpSocket final : public p2p::Socket {
 public:
  ~EmulatedTcpSocket() override;

  p2p::SocketAddress GetLocalAddress() const override { return local_; }
  p2p::SocketAddress GetRemoteAddress() const override { return remote_; }
  int Bind(const p2p::SocketAddress& addr) override;
  int Connect(Timestamp now, const p2p::SocketAddress& addr) override;
  int Send(Timestamp now, std::span<const uint8_t> data) override;
  int Recv(Timestamp now, std::span<uint8_t> buffer) override;
  int Listen(int backlog) override;
  std::unique_ptr<p2p::Socket> Accept(Timestamp now, p2p::SocketAddress* paddr) override;
  int Close() override;
  int GetError() const override { return error_; }
  void SetError(int error) override { error_ = error; }
  ConnState GetState() const override {
    switch (state_) {
      case St::kClosed: return ConnState::kClosed;
      case St::kListening:
      case St::kSynSent: return ConnState::kConnecting;
      case St::kConnected: return ConnState::kConnected;
    }
    return ConnState::kClosed;
  }
  int GetOption(p2p::SocketOption opt, int* value) override {
    auto it = options_.find(opt);
    if (it == options_.end()) return -1;
    *value = it->second;
    return 0;
  }
  int SetOption(p2p::SocketOption opt, int value) override {
    options_[opt] = value;
    return 0;
  }

  // For tests: bytes written but not yet on the wire, on the wire and not
  // yet acknowledged, and received but not yet read.
  size_t unsent_bytes() const { return sndbuf_.size() - InFlight(); }
  size_t in_flight_bytes() const { return InFlight(); }
  size_t unread_bytes() const { return recv_.size(); }
  TimeDelta rto() const { return rto_; }

 private:
  friend class EmulatedSocketServer;
  enum class St { kClosed, kListening, kSynSent, kConnected };

  explicit EmulatedTcpSocket(EmulatedSocketServer* server) : server_(server) {}

  size_t InFlight() const { return static_cast<size_t>(snd_nxt_ - snd_una_); }
  uint32_t AdvertisedWindow() const;
  void SendSegment(Timestamp now, uint8_t flags, uint64_t seq, std::span<const uint8_t> data);
  void SendAck(Timestamp now) { SendSegment(now, Wire::kAck, snd_nxt_, {}); }
  void TrySend(Timestamp now);
  void OnSegment(Timestamp now, const Wire& w);
  void OnTimer(Timestamp now);
  void Enqueue(Timestamp now, const p2p::SocketAddress& remote);
  void Closed(int error);  // The peer closed or reset the connection, or it timed out.
  void Sample(TimeDelta rtt);

  EmulatedSocketServer* server_;  // nullptr once the server is gone.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  St state_ = St::kClosed;
  p2p::SocketAddress local_;
  p2p::SocketAddress remote_;
  bool bound_ = false;  // Holds local_'s port in the host's table.
  int error_ = 0;
  std::map<p2p::SocketOption, int> options_;
  std::deque<p2p::SocketAddress> listen_queue_;
  // Send side: sndbuf_ holds the bytes from snd_una_ on; those before
  // snd_nxt_ are on the wire.
  std::deque<uint8_t> sndbuf_;
  uint64_t snd_una_ = 0;
  uint64_t snd_nxt_ = 0;
  size_t peer_window_ = 0;
  bool ready_to_send_ = true;
  int dup_acks_ = 0;
  // Receive side.
  std::deque<uint8_t> recv_;
  uint64_t rcv_nxt_ = 0;
  uint32_t last_adv_window_ = 0;
  // Retransmission timer (RFC 6298).
  Timestamp rto_at_ = Timestamp::PlusInfinity();
  TimeDelta rto_ = TimeDelta::Seconds(1);
  TimeDelta base_rto_ = TimeDelta::Seconds(1);  // rto_ without the backoff.
  int retries_ = 0;
  bool have_srtt_ = false;
  TimeDelta srtt_ = TimeDelta::Zero();
  TimeDelta rttvar_ = TimeDelta::Zero();
  bool timing_ = false;  // A segment is being timed (Karn: never a retransmitted one).
  uint64_t timed_end_ = 0;
  Timestamp timed_at_ = Timestamp::MinusInfinity();
};

class EmulatedSocketServer final : public p2p::PacketSocketFactory, public p2p::StreamSocketFactory {
 public:
  static constexpr uint16_t kFirstEphemeralPort = 49152;
  static constexpr uint16_t kLastEphemeralPort = 65535;
  static constexpr int kUdpIpv4HeaderBytes = 28;  // IPv4 20 + UDP 8.
  // The largest UDP payload in one IPv4 datagram.
  static constexpr size_t kMaxUdpPayload = 65507;
  // virtual_socket_server.cc's TCP constants.
  static constexpr size_t kDefaultTcpBufferSize = 32 * 1024;
  static constexpr int kTcpHeaderBytes = 40;  // TCP_HEADER_SIZE: IP + TCP headers.
  static constexpr size_t kTcpMss = 1400;     // TCP_MSS.
  static constexpr size_t kTcpMaxSegmentPayload = kTcpMss - kTcpHeaderBytes;
  // Recovery from loss (not in the virtual server, whose TCP is lossless):
  // RFC 6298's timer and Linux's retry counts.
  static constexpr TimeDelta kTcpInitialRto = TimeDelta::Seconds(1);
  static constexpr TimeDelta kTcpMinRto = TimeDelta::Seconds(1);
  static constexpr TimeDelta kTcpMaxRto = TimeDelta::Seconds(60);
  static constexpr int kTcpSynRetries = 6;
  static constexpr int kTcpDataRetries = 15;

  EmulatedSocketServer(ByteNetwork& net, ByteNetwork::HostId host) : net_(net), host_(host) {}
  ~EmulatedSocketServer() override {
    for (auto& [port, b] : bound_) {
      if (b.udp) b.udp->server_ = nullptr;
    }
    for (EmulatedTcpSocket* s : tcp_sockets_) s->server_ = nullptr;
  }

  p2p::IpAddress ip() const { return p2p::IpAddress(net_.host_ip(host_)); }
  ByteNetwork::HostId host() const { return host_; }

  std::unique_ptr<p2p::AsyncPacketSocket> CreateUdpSocket(const p2p::SocketAddress& address, uint16_t min_port,
                                                          uint16_t max_port) override {
    return CreateEmulatedUdpSocket(address, min_port, max_port);
  }
  std::unique_ptr<EmulatedUdpSocket> CreateEmulatedUdpSocket(const p2p::SocketAddress& address, uint16_t min_port,
                                                             uint16_t max_port) {
    last_error_ = 0;
    if (min_port == 0 && max_port == 0) return BindUdp(address);
    for (uint32_t port = min_port; port <= max_port; ++port) {
      if (auto s = BindUdp(p2p::SocketAddress(address.ipaddr(), static_cast<uint16_t>(port)))) return s;
    }
    return nullptr;
  }
  // Why the last bind failed (EADDRINUSE, EADDRNOTAVAIL).
  int last_error() const { return last_error_; }

  // TCP (BasicPacketSocketFactory's methods over this server's stream
  // sockets).
  std::unique_ptr<p2p::Socket> CreateStreamSocket(p2p::IpFamily family) override {
    if (family != p2p::IpFamily::kV4) return nullptr;  // The emulated network is IPv4 only.
    return CreateEmulatedTcpSocket();
  }
  std::unique_ptr<EmulatedTcpSocket> CreateEmulatedTcpSocket() {
    auto s = std::unique_ptr<EmulatedTcpSocket>(new EmulatedTcpSocket(this));
    tcp_sockets_.insert(s.get());
    return s;
  }
  std::unique_ptr<p2p::AsyncListenSocket> CreateServerTcpSocket(const p2p::SocketAddress& address,
                                                                uint16_t min_port, uint16_t max_port,
                                                                int opts) override {
    last_error_ = 0;
    return p2p::CreateServerTcpSocket(*this, address, min_port, max_port, opts);
  }
  std::unique_ptr<p2p::AsyncPacketSocket> CreateClientTcpSocket(Timestamp now,
                                                                const p2p::SocketAddress& local_address,
                                                                const p2p::SocketAddress& remote_address,
                                                                const p2p::PacketSocketTcpOptions& options) override {
    last_error_ = 0;
    return p2p::CreateClientTcpSocket(*this, now, local_address, remote_address, options);
  }
  // VirtualSocketServer::set_send_buffer_capacity / set_recv_buffer_capacity,
  // for sockets created afterwards and those already open.
  void set_send_buffer_capacity(size_t bytes) { send_buffer_capacity_ = bytes; }
  void set_recv_buffer_capacity(size_t bytes) { recv_buffer_capacity_ = bytes; }
  size_t send_buffer_capacity() const { return send_buffer_capacity_; }
  size_t recv_buffer_capacity() const { return recv_buffer_capacity_; }

  // Hands datagrams and TCP segments that have reached this host by `now` to
  // their sockets, then runs the TCP timers that are due.
  void Deliver(Timestamp now) {
    Advance(now);
    for (Datagram<Wire>& d : net_.Receive(now, host_)) {
      if (d.payload.proto == Wire::Proto::kTcp) {
        OnTcpSegment(now, d);
        continue;
      }
      auto it = bound_.find(d.dst.port);
      if (it == bound_.end() || !it->second.udp) {
        ++dropped_no_socket_;
        continue;
      }
      it->second.udp->Receive(p2p::ReceivedPacket{std::span<const uint8_t>(d.payload.data), ToP2p(d.src), now,
                                                  p2p::ReceivedPacket::kNotDecrypted});
    }
    std::vector<EmulatedTcpSocket*> due;
    for (EmulatedTcpSocket* s : tcp_sockets_) {
      if (s->rto_at_ <= now) due.push_back(s);
    }
    for (EmulatedTcpSocket* s : due) {
      if (tcp_sockets_.count(s) && s->rto_at_ <= now) s->OnTimer(now);  // An earlier callback may have closed it.
    }
  }
  // When anything next happens: the network moves (any host's links), or one
  // of this server's TCP timers fires.
  Timestamp NextEvent() const {
    Timestamp t = net_.NextEvent();
    for (const EmulatedTcpSocket* s : tcp_sockets_) t = std::min(t, s->rto_at_);
    return t;
  }
  uint64_t dropped_no_socket() const { return dropped_no_socket_; }
  uint64_t tcp_resets_sent() const { return tcp_resets_sent_; }
  size_t bound_count() const { return bound_.size(); }

 private:
  friend class EmulatedUdpSocket;
  friend class EmulatedTcpSocket;

  struct Binding {
    EmulatedUdpSocket* udp = nullptr;
    EmulatedTcpSocket* tcp = nullptr;
  };

  // VirtualSocket::Bind with AssignBindAddress: the address a socket gets,
  // or nullopt with last_error_ set. One socket per port, TCP or UDP: the
  // host has one address, so the any address and the host's collide.
  std::optional<p2p::SocketAddress> AssignPort(const p2p::SocketAddress& requested) {
    const p2p::IpAddress ip = requested.ipaddr().Normalized();
    if (ip != this->ip() && !(p2p::IpIsAny(ip) && ip.family() == p2p::IpFamily::kV4)) {
      last_error_ = p2p::kSocketEAddrNotAvail;
      return std::nullopt;
    }
    uint16_t port = requested.port();
    if (port == 0) {
      for (int i = 0; i < kLastEphemeralPort - kFirstEphemeralPort + 1; ++i) {
        port = NextPort();
        if (!bound_.count(port)) break;
      }
    }
    if (bound_.count(port)) {
      last_error_ = p2p::kSocketEAddrInUse;
      return std::nullopt;
    }
    return p2p::SocketAddress(ip, port);
  }
  std::unique_ptr<EmulatedUdpSocket> BindUdp(const p2p::SocketAddress& requested) {
    const std::optional<p2p::SocketAddress> addr = AssignPort(requested);
    if (!addr) return nullptr;
    auto s = std::unique_ptr<EmulatedUdpSocket>(new EmulatedUdpSocket(this, *addr));
    bound_[addr->port()].udp = s.get();
    return s;
  }
  uint16_t NextPort() {  // GetNextPort.
    const uint16_t p = next_port_;
    next_port_ = next_port_ < kLastEphemeralPort ? static_cast<uint16_t>(next_port_ + 1) : kFirstEphemeralPort;
    return p;
  }
  void Unbind(EmulatedUdpSocket* s) {
    auto it = bound_.find(s->local_.port());
    if (it != bound_.end() && it->second.udp == s) bound_.erase(it);
  }
  void Unbind(EmulatedTcpSocket* s) {
    auto it = bound_.find(s->local_.port());
    if (it != bound_.end() && it->second.tcp == s) bound_.erase(it);
    s->bound_ = false;
  }

  using ConnectionKey = std::pair<uint16_t, p2p::SocketAddress>;  // (local port, remote address).
  void AddConnection(EmulatedTcpSocket* s) { connections_[{s->local_.port(), s->remote_}] = s; }
  void RemoveConnection(EmulatedTcpSocket* s) {
    auto it = connections_.find({s->local_.port(), s->remote_});
    if (it != connections_.end() && it->second == s) connections_.erase(it);
  }

  // The latest time the server has been told; Close, which takes no time,
  // sends its FIN then.
  void Advance(Timestamp now) { last_now_ = std::max(last_now_, now); }

  void SendTcp(Timestamp now, uint16_t src_port, const p2p::SocketAddress& dst, Wire w) {
    Advance(now);
    const int64_t size = static_cast<int64_t>(w.data.size()) + kTcpHeaderBytes;
    net_.Send(last_now_, host_, src_port, FromP2p(dst), std::move(w), DataSize::Bytes(size));
  }
  void SendReset(Timestamp now, uint16_t src_port, const p2p::SocketAddress& dst) {
    Wire w;
    w.proto = Wire::Proto::kTcp;
    w.flags = Wire::kRst;
    ++tcp_resets_sent_;
    SendTcp(now, src_port, dst, std::move(w));
  }

  void OnTcpSegment(Timestamp now, const Datagram<Wire>& d) {
    const p2p::SocketAddress from = ToP2p(d.src);
    auto c = connections_.find({d.dst.port, from});
    if (c != connections_.end()) {
      c->second->OnSegment(now, d.payload);
      return;
    }
    auto b = bound_.find(d.dst.port);
    const uint8_t flags = d.payload.flags;
    if (b != bound_.end() && b->second.tcp && b->second.tcp->state_ == EmulatedTcpSocket::St::kListening &&
        (flags & Wire::kSyn) && !(flags & Wire::kAck)) {
      b->second.tcp->Enqueue(now, from);
      return;
    }
    if (!(flags & Wire::kRst)) SendReset(now, d.dst.port, from);  // No one listening, or a stale connection.
  }

  // What Close does on the wire: a FIN for a connection, a reset for one
  // still connecting and for each connection a listener has not accepted.
  void CloseTcp(EmulatedTcpSocket* s) {
    switch (s->state_) {
      case EmulatedTcpSocket::St::kListening:
        for (const p2p::SocketAddress& r : s->listen_queue_) SendReset(last_now_, s->local_.port(), r);
        s->listen_queue_.clear();
        break;
      case EmulatedTcpSocket::St::kSynSent: SendReset(last_now_, s->local_.port(), s->remote_); break;
      case EmulatedTcpSocket::St::kConnected: s->SendSegment(last_now_, Wire::kFin | Wire::kAck, s->snd_nxt_, {}); break;
      case EmulatedTcpSocket::St::kClosed: break;
    }
    RemoveConnection(s);
    if (s->bound_) Unbind(s);
  }

  ByteNetwork& net_;
  ByteNetwork::HostId host_;
  std::map<uint16_t, Binding> bound_;
  std::map<ConnectionKey, EmulatedTcpSocket*> connections_;
  std::set<EmulatedTcpSocket*> tcp_sockets_;
  uint16_t next_port_ = kFirstEphemeralPort;
  int last_error_ = 0;
  uint64_t dropped_no_socket_ = 0;
  uint64_t tcp_resets_sent_ = 0;
  size_t send_buffer_capacity_ = kDefaultTcpBufferSize;
  size_t recv_buffer_capacity_ = kDefaultTcpBufferSize;
  Timestamp last_now_ = Timestamp::Micros(0);
};

inline EmulatedUdpSocket::~EmulatedUdpSocket() {
  if (server_) server_->Unbind(this);
}

inline int EmulatedUdpSocket::Close() {
  if (server_) server_->Unbind(this);
  server_ = nullptr;
  return 0;
}

inline int EmulatedUdpSocket::SendTo(Timestamp now, std::span<const uint8_t> data, const p2p::SocketAddress& addr,
                                     const p2p::PacketOptions& options) {
  Sent(now, data.size(), options);
  if (!server_) {
    error_ = p2p::kSocketEBadF;
    return -1;
  }
  if (addr.family() != p2p::IpFamily::kV4 || addr.port() == 0) {
    error_ = p2p::kSocketEInval;  // The emulated network is IPv4 only.
    return -1;
  }
  if (data.size() > EmulatedSocketServer::kMaxUdpPayload) {
    error_ = p2p::kSocketEMsgSize;
    return -1;
  }
  server_->Advance(now);
  Wire w;
  w.data.assign(data.begin(), data.end());
  server_->net_.Send(now, server_->host_, local_.port(), FromP2p(addr), std::move(w),
                     DataSize::Bytes(static_cast<int64_t>(data.size()) + EmulatedSocketServer::kUdpIpv4HeaderBytes));
  return static_cast<int>(data.size());
}

// --- EmulatedTcpSocket ---

inline EmulatedTcpSocket::~EmulatedTcpSocket() {
  *alive_ = false;
  if (server_) {
    server_->CloseTcp(this);
    server_->tcp_sockets_.erase(this);
  }
}

inline int EmulatedTcpSocket::Bind(const p2p::SocketAddress& addr) {
  if (!server_) {
    error_ = p2p::kSocketEBadF;
    return -1;
  }
  if (bound_ || state_ != St::kClosed) {
    error_ = p2p::kSocketEInval;
    return -1;
  }
  const std::optional<p2p::SocketAddress> a = server_->AssignPort(addr);
  if (!a) {
    error_ = server_->last_error_;
    return -1;
  }
  local_ = *a;
  bound_ = true;
  server_->bound_[local_.port()].tcp = this;
  return 0;
}

inline int EmulatedTcpSocket::Connect(Timestamp now, const p2p::SocketAddress& addr) {
  if (!server_) {
    error_ = p2p::kSocketEBadF;
    return -1;
  }
  if (!remote_.IsNil()) {
    error_ = state_ == St::kConnected ? p2p::kSocketEIsConn : p2p::kSocketEInProgress;
    return -1;
  }
  if (state_ != St::kClosed || addr.family() != p2p::IpFamily::kV4 || addr.port() == 0) {
    error_ = p2p::kSocketEHostUnreach;
    return -1;
  }
  if (!bound_ && Bind(p2p::SocketAddress(p2p::IpAddress(0u), 0)) != 0) return -1;
  server_->Advance(now);
  remote_ = addr;
  state_ = St::kSynSent;
  server_->AddConnection(this);
  timing_ = true;  // Times the SYN: its SYN-ACK is the first RTT sample.
  timed_end_ = 0;
  timed_at_ = now;
  SendSegment(now, Wire::kSyn, 0, {});
  rto_at_ = now + rto_;
  return 0;
}

inline int EmulatedTcpSocket::Send(Timestamp now, std::span<const uint8_t> data) {
  if (!server_) {
    error_ = p2p::kSocketEBadF;
    return -1;
  }
  if (state_ != St::kConnected) {
    error_ = p2p::kSocketENotConn;
    return -1;
  }
  server_->Advance(now);
  const size_t unsent = unsent_bytes();
  const size_t cap = server_->send_buffer_capacity_;
  const size_t room = cap > unsent ? cap - unsent : 0;
  if (room == 0) {
    ready_to_send_ = false;
    error_ = p2p::kSocketEWouldBlock;
    return -1;
  }
  const size_t n = std::min(room, data.size());
  sndbuf_.insert(sndbuf_.end(), data.begin(), data.begin() + static_cast<long>(n));
  TrySend(now);
  return static_cast<int>(n);
}

inline int EmulatedTcpSocket::Recv(Timestamp now, std::span<uint8_t> buffer) {
  if (recv_.empty()) {
    error_ = p2p::kSocketEWouldBlock;
    return -1;
  }
  const size_t n = std::min(buffer.size(), recv_.size());
  std::copy_n(recv_.begin(), n, buffer.begin());
  recv_.erase(recv_.begin(), recv_.begin() + static_cast<long>(n));
  // The receive buffer was full: tell the sender it has room again.
  if (n > 0 && last_adv_window_ == 0 && state_ == St::kConnected && server_) SendAck(now);
  return static_cast<int>(n);
}

inline int EmulatedTcpSocket::Listen(int /*backlog*/) {
  if (!bound_ || state_ != St::kClosed) {
    error_ = p2p::kSocketEInval;
    return -1;
  }
  state_ = St::kListening;
  return 0;
}

inline std::unique_ptr<p2p::Socket> EmulatedTcpSocket::Accept(Timestamp now, p2p::SocketAddress* paddr) {
  if (!server_ || state_ != St::kListening) {
    error_ = p2p::kSocketEInval;
    return nullptr;
  }
  while (!listen_queue_.empty()) {
    const p2p::SocketAddress remote = listen_queue_.front();
    listen_queue_.pop_front();
    if (server_->connections_.count({local_.port(), remote})) continue;
    std::unique_ptr<EmulatedTcpSocket> s = server_->CreateEmulatedTcpSocket();
    s->local_ = local_;  // The listener's address; accepted sockets are not bound.
    s->remote_ = remote;
    s->state_ = St::kConnected;
    s->peer_window_ = server_->recv_buffer_capacity_;
    server_->AddConnection(s.get());
    s->SendSegment(now, Wire::kSyn | Wire::kAck, 0, {});
    if (paddr) *paddr = remote;
    return s;
  }
  error_ = p2p::kSocketEWouldBlock;
  return nullptr;
}

inline int EmulatedTcpSocket::Close() {
  if (server_) server_->CloseTcp(this);
  state_ = St::kClosed;
  bound_ = false;
  local_ = {};
  remote_ = {};
  rto_at_ = Timestamp::PlusInfinity();
  sndbuf_.clear();
  snd_una_ = snd_nxt_ = 0;
  listen_queue_.clear();
  return 0;
}

inline uint32_t EmulatedTcpSocket::AdvertisedWindow() const {
  const size_t cap = server_ ? server_->recv_buffer_capacity_ : 0;
  return static_cast<uint32_t>(cap > recv_.size() ? cap - recv_.size() : 0);
}

inline void EmulatedTcpSocket::SendSegment(Timestamp now, uint8_t flags, uint64_t seq,
                                           std::span<const uint8_t> data) {
  Wire w;
  w.proto = Wire::Proto::kTcp;
  w.flags = flags;
  w.seq = seq;
  w.ack = rcv_nxt_;
  w.window = AdvertisedWindow();
  w.data.assign(data.begin(), data.end());
  last_adv_window_ = w.window;
  server_->SendTcp(now, local_.port(), remote_, std::move(w));
}

// VirtualSocketServer::SendTcp: puts as much of the send buffer on the wire
// as the receiver's window allows, in segments of at most the MSS less the
// header, then signals a write event if a full buffer has room again.
inline void EmulatedTcpSocket::TrySend(Timestamp now) {
  if (state_ != St::kConnected) return;
  for (;;) {
    const size_t inflight = InFlight();
    const size_t unsent = sndbuf_.size() - inflight;
    const size_t room = peer_window_ > inflight ? peer_window_ - inflight : 0;
    const size_t n = std::min({unsent, EmulatedSocketServer::kTcpMaxSegmentPayload, room});
    if (n == 0) {
      // Data waits on a closed window with nothing in flight: probe it.
      if (unsent > 0 && inflight == 0 && !rto_at_.IsFinite()) rto_at_ = now + rto_;
      break;
    }
    std::vector<uint8_t> seg(sndbuf_.begin() + static_cast<long>(inflight),
                             sndbuf_.begin() + static_cast<long>(inflight + n));
    SendSegment(now, Wire::kAck, snd_nxt_, seg);
    snd_nxt_ += n;
    if (!timing_) {
      timing_ = true;
      timed_end_ = snd_nxt_;
      timed_at_ = now;
    }
    if (!rto_at_.IsFinite()) rto_at_ = now + rto_;
  }
  // MaybeSignalWriteEvent.
  if (!ready_to_send_ && unsent_bytes() < server_->send_buffer_capacity_) {
    ready_to_send_ = true;
    NotifyWriteEvent(now);
  }
}

// RFC 6298 section 2.
inline void EmulatedTcpSocket::Sample(TimeDelta rtt) {
  if (!have_srtt_) {
    srtt_ = rtt;
    rttvar_ = rtt / int64_t{2};
    have_srtt_ = true;
  } else {
    const TimeDelta err = srtt_ > rtt ? srtt_ - rtt : rtt - srtt_;
    rttvar_ = rttvar_ * int64_t{3} / int64_t{4} + err / int64_t{4};
    srtt_ = srtt_ * int64_t{7} / int64_t{8} + rtt / int64_t{8};
  }
  const TimeDelta k = std::max(TimeDelta::Millis(1), rttvar_ * int64_t{4});  // G = 1 ms.
  base_rto_ = std::clamp(srtt_ + k, EmulatedSocketServer::kTcpMinRto, EmulatedSocketServer::kTcpMaxRto);
}

inline void EmulatedTcpSocket::OnSegment(Timestamp now, const Wire& w) {
  if (w.flags & Wire::kRst) {
    if (state_ == St::kSynSent) {
      Closed(p2p::kSocketEConnRefused);
    } else if (state_ == St::kConnected) {
      Closed(p2p::kSocketEConnReset);
    }
    return;
  }
  if (state_ == St::kSynSent) {
    if ((w.flags & Wire::kSyn) && (w.flags & Wire::kAck)) {
      state_ = St::kConnected;
      peer_window_ = w.window;
      if (timing_ && retries_ == 0) Sample(now - timed_at_);
      timing_ = false;
      retries_ = 0;
      rto_ = base_rto_;
      rto_at_ = Timestamp::PlusInfinity();
      NotifyConnectEvent(now);
    }
    return;  // Anything else before the SYN-ACK is dropped and comes again.
  }
  if (state_ != St::kConnected) return;
  if (w.flags & Wire::kSyn) {
    // A repeated SYN (our SYN-ACK was lost) or SYN-ACK (our ACK was).
    if (w.flags & Wire::kAck) {
      SendAck(now);
    } else {
      SendSegment(now, Wire::kSyn | Wire::kAck, 0, {});
    }
    return;
  }

  // The acknowledgement.
  if (w.flags & Wire::kAck) {
    if (w.ack > snd_una_ && w.ack <= snd_nxt_) {
      const size_t acked = static_cast<size_t>(w.ack - snd_una_);
      sndbuf_.erase(sndbuf_.begin(), sndbuf_.begin() + static_cast<long>(acked));
      snd_una_ = w.ack;
      if (timing_ && w.ack >= timed_end_) {
        Sample(now - timed_at_);
        timing_ = false;
      }
      retries_ = 0;
      dup_acks_ = 0;
      rto_ = base_rto_;
      rto_at_ = snd_nxt_ > snd_una_ ? now + rto_ : Timestamp::PlusInfinity();
    } else if (w.ack == snd_una_ && snd_nxt_ > snd_una_ && w.data.empty() && !(w.flags & Wire::kFin) &&
               w.window == peer_window_) {
      if (++dup_acks_ == 3) {  // Fast retransmit, go-back-N.
        snd_nxt_ = snd_una_;
        timing_ = false;
      }
    }
    peer_window_ = w.window;
    if (peer_window_ > 0 && snd_nxt_ == snd_una_ && rto_at_.IsFinite() && sndbuf_.size() > 0) {
      rto_at_ = Timestamp::PlusInfinity();  // The window reopened: no probe needed.
    }
  }

  // The data: only in order, as far as the receive buffer has room.
  bool readable = false;
  if (!w.data.empty()) {
    const uint64_t end = w.seq + w.data.size();
    if (w.seq <= rcv_nxt_ && end > rcv_nxt_) {
      const size_t off = static_cast<size_t>(rcv_nxt_ - w.seq);
      const size_t n = std::min<size_t>(w.data.size() - off, AdvertisedWindow());
      recv_.insert(recv_.end(), w.data.begin() + static_cast<long>(off),
                   w.data.begin() + static_cast<long>(off + n));
      rcv_nxt_ += n;
      readable = n > 0;
    }
  }
  const bool fin = (w.flags & Wire::kFin) != 0;
  if (!w.data.empty() || (w.flags & Wire::kProbe)) SendAck(now);

  std::shared_ptr<bool> alive = alive_;
  if (readable) NotifyReadEvent(now);
  if (!*alive) return;
  if (fin) {
    Closed(0);  // PostDisconnect: a connected socket closes with no error.
    return;
  }
  TrySend(now);
}

inline void EmulatedTcpSocket::OnTimer(Timestamp now) {
  rto_at_ = Timestamp::PlusInfinity();
  if (state_ == St::kSynSent) {
    if (retries_ >= EmulatedSocketServer::kTcpSynRetries) {
      Closed(p2p::kSocketETimedOut);
      return;
    }
    ++retries_;
    rto_ = std::min(rto_ * int64_t{2}, EmulatedSocketServer::kTcpMaxRto);
    SendSegment(now, Wire::kSyn, 0, {});
    rto_at_ = now + rto_;
    return;
  }
  if (state_ != St::kConnected) return;
  if (InFlight() > 0) {
    if (retries_ >= EmulatedSocketServer::kTcpDataRetries) {
      Closed(p2p::kSocketETimedOut);
      return;
    }
    ++retries_;
    rto_ = std::min(rto_ * int64_t{2}, EmulatedSocketServer::kTcpMaxRto);
    snd_nxt_ = snd_una_;  // Go-back-N.
    timing_ = false;      // Karn: never time a retransmission.
    rto_at_ = now + rto_;
    TrySend(now);
    return;
  }
  if (unsent_bytes() > 0 && peer_window_ == 0) {  // Window probe.
    rto_ = std::min(rto_ * int64_t{2}, EmulatedSocketServer::kTcpMaxRto);
    SendSegment(now, Wire::kAck | Wire::kProbe, snd_nxt_, {});
    rto_at_ = now + rto_;
  }
}

inline void EmulatedTcpSocket::Enqueue(Timestamp now, const p2p::SocketAddress& remote) {
  if (std::find(listen_queue_.begin(), listen_queue_.end(), remote) != listen_queue_.end()) return;  // A repeated SYN.
  listen_queue_.push_back(remote);
  NotifyReadEvent(now);
}

inline void EmulatedTcpSocket::Closed(int error) {
  if (server_) server_->RemoveConnection(this);
  state_ = St::kClosed;
  remote_ = {};
  rto_at_ = Timestamp::PlusInfinity();
  sndbuf_.clear();
  snd_una_ = snd_nxt_ = 0;
  error_ = error;
  NotifyCloseEvent(error);
}

}  // namespace sm::sim
