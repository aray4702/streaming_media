// UDP sockets over the emulated network (issue #996, part of ICE #990,
// milestone 3).
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
// Like every module, it takes `now`, reads no clock and starts no thread. A
// test bench building block: the media test bench does not use it yet.
#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/async_packet_socket.h"
#include "sm/p2p/socket_address.h"
#include "sm/sim/network.h"

namespace sm::sim {

using Bytes = std::vector<uint8_t>;
using ByteNetwork = Network<Bytes>;

// Between the emulated network's IPv4 addresses and the library's.
inline p2p::SocketAddress ToP2p(const SocketAddress& a) { return p2p::SocketAddress(a.ip, a.port); }
inline SocketAddress FromP2p(const p2p::SocketAddress& a) { return SocketAddress{a.ip(), a.port()}; }

class EmulatedSocketServer;

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

class EmulatedSocketServer final : public p2p::PacketSocketFactory {
 public:
  static constexpr uint16_t kFirstEphemeralPort = 49152;
  static constexpr uint16_t kLastEphemeralPort = 65535;
  static constexpr int kUdpIpv4HeaderBytes = 28;  // IPv4 20 + UDP 8.
  // The largest UDP payload in one IPv4 datagram.
  static constexpr size_t kMaxUdpPayload = 65507;

  EmulatedSocketServer(ByteNetwork& net, ByteNetwork::HostId host) : net_(net), host_(host) {}
  ~EmulatedSocketServer() override {
    for (auto& [port, s] : bound_) s->server_ = nullptr;
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
    if (min_port == 0 && max_port == 0) return Bind(address);
    for (uint32_t port = min_port; port <= max_port; ++port) {
      if (auto s = Bind(p2p::SocketAddress(address.ipaddr(), static_cast<uint16_t>(port)))) return s;
    }
    return nullptr;
  }
  // Why the last CreateUdpSocket failed (EADDRINUSE, EADDRNOTAVAIL).
  int last_error() const { return last_error_; }

  // Hands datagrams that have reached this host by `now` to their sockets.
  void Deliver(Timestamp now) {
    for (Datagram<Bytes>& d : net_.Receive(now, host_)) {
      auto it = bound_.find(d.dst.port);
      if (it == bound_.end()) {
        ++dropped_no_socket_;
        continue;
      }
      it->second->Receive(p2p::ReceivedPacket{std::span<const uint8_t>(d.payload), ToP2p(d.src), now,
                                                  p2p::ReceivedPacket::kNotDecrypted});
    }
  }
  // When the network next moves anything (any host's links).
  Timestamp NextEvent() const { return net_.NextEvent(); }
  uint64_t dropped_no_socket() const { return dropped_no_socket_; }
  size_t bound_count() const { return bound_.size(); }

 private:
  friend class EmulatedUdpSocket;

  // VirtualSocket::Bind with AssignBindAddress. One socket per port: the
  // host has one address, so the any address and the host's collide.
  std::unique_ptr<EmulatedUdpSocket> Bind(const p2p::SocketAddress& requested) {
    const p2p::IpAddress ip = requested.ipaddr().Normalized();
    if (ip != this->ip() && !(p2p::IpIsAny(ip) && ip.family() == p2p::IpFamily::kV4)) {
      last_error_ = p2p::kSocketEAddrNotAvail;
      return nullptr;
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
      return nullptr;
    }
    auto s = std::unique_ptr<EmulatedUdpSocket>(new EmulatedUdpSocket(this, p2p::SocketAddress(ip, port)));
    bound_[port] = s.get();
    return s;
  }
  uint16_t NextPort() {  // GetNextPort.
    const uint16_t p = next_port_;
    next_port_ = next_port_ < kLastEphemeralPort ? static_cast<uint16_t>(next_port_ + 1) : kFirstEphemeralPort;
    return p;
  }
  void Unbind(EmulatedUdpSocket* s) {
    auto it = bound_.find(s->local_.port());
    if (it != bound_.end() && it->second == s) bound_.erase(it);
  }

  ByteNetwork& net_;
  ByteNetwork::HostId host_;
  std::map<uint16_t, EmulatedUdpSocket*> bound_;
  uint16_t next_port_ = kFirstEphemeralPort;
  int last_error_ = 0;
  uint64_t dropped_no_socket_ = 0;
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
  server_->net_.Send(now, server_->host_, local_.port(), FromP2p(addr), Bytes(data.begin(), data.end()),
                     DataSize::Bytes(static_cast<int64_t>(data.size()) + EmulatedSocketServer::kUdpIpv4HeaderBytes));
  return static_cast<int>(data.size());
}

}  // namespace sm::sim
