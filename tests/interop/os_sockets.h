// POSIX sockets for the interop tests (issue #1037, C4 part 5): the library's
// packet and stream socket interfaces (sm/p2p/async_packet_socket.h,
// sm/p2p/socket.h) over non-blocking OS sockets, so a TurnPort can talk to a
// real TURN server (coturn) on loopback or on a real host.
//
// The library reads no clock and starts no thread; the test's event loop
// plays libwebrtc's PhysicalSocketServer: Poll(now, wait) waits on every open
// socket with poll(2) and raises the read, write, connect and close events
// (PhysicalSocket's dispatcher), passing the `now` the loop read from its
// clock. TCP sockets are made by the library's CreateClientTcpSocket, which
// adds the STUN framing and, for TLS, the SSL adapter, as
// BasicPacketSocketFactory does over PhysicalSocket.
//
// Test code only: IPv4, no DSCP or ECN, and no listening UDP ports for peers.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/async_packet_socket.h"
#include "sm/p2p/socket.h"
#include "sm/p2p/socket_address.h"

namespace sm::interop {

class OsSocketServer;

// The errno of the last call, as the POSIX numbers sm/p2p uses (macOS's).
int MapErrno(int e);

class OsUdpSocket final : public p2p::AsyncPacketSocket {
 public:
  ~OsUdpSocket() override;
  p2p::SocketAddress GetLocalAddress() const override { return local_; }
  p2p::SocketAddress GetRemoteAddress() const override { return {}; }
  int Send(Timestamp now, std::span<const uint8_t>, const p2p::PacketOptions& options) override;
  int SendTo(Timestamp now, std::span<const uint8_t> data, const p2p::SocketAddress& addr,
             const p2p::PacketOptions& options) override;
  int Close() override;
  State GetState() const override { return fd_ >= 0 ? State::kBound : State::kClosed; }
  int GetOption(p2p::SocketOption opt, int* value) override;
  int SetOption(p2p::SocketOption opt, int value) override;
  int GetError() const override { return error_; }
  void SetError(int error) override { error_ = error; }

 private:
  friend class OsSocketServer;
  OsUdpSocket(OsSocketServer* server, int fd, p2p::SocketAddress local) : server_(server), fd_(fd), local_(local) {}
  void OnReadable(Timestamp now);

  OsSocketServer* server_;
  int fd_;
  p2p::SocketAddress local_;
  std::map<p2p::SocketOption, int> options_;
  int error_ = 0;
};

class OsTcpSocket final : public p2p::Socket {
 public:
  ~OsTcpSocket() override;
  p2p::SocketAddress GetLocalAddress() const override;
  p2p::SocketAddress GetRemoteAddress() const override { return remote_; }
  int Bind(const p2p::SocketAddress& addr) override;
  int Connect(Timestamp now, const p2p::SocketAddress& addr) override;
  int Send(Timestamp now, std::span<const uint8_t> data) override;
  int Recv(Timestamp now, std::span<uint8_t> buffer) override;
  // Listening is not needed by the interop tests.
  int Listen(int) override {
    error_ = p2p::kSocketEInval;
    return -1;
  }
  std::unique_ptr<p2p::Socket> Accept(Timestamp, p2p::SocketAddress*) override {
    error_ = p2p::kSocketEInval;
    return nullptr;
  }
  int Close() override;
  int GetError() const override { return error_; }
  void SetError(int error) override { error_ = error; }
  ConnState GetState() const override { return state_; }
  int GetOption(p2p::SocketOption opt, int* value) override;
  int SetOption(p2p::SocketOption opt, int value) override;

 private:
  friend class OsSocketServer;
  OsTcpSocket(OsSocketServer* server, int fd) : server_(server), fd_(fd) {}
  short Events() const;
  void OnEvents(Timestamp now, short revents);

  OsSocketServer* server_;
  int fd_;
  ConnState state_ = ConnState::kClosed;
  p2p::SocketAddress remote_;
  bool want_write_ = false;  // A Send blocked: raise the write event when writable.
  int error_ = 0;
};

class OsSocketServer final : public p2p::PacketSocketFactory, public p2p::StreamSocketFactory {
 public:
  OsSocketServer() = default;
  ~OsSocketServer() override;

  std::unique_ptr<p2p::AsyncPacketSocket> CreateUdpSocket(const p2p::SocketAddress& address, uint16_t min_port,
                                                          uint16_t max_port) override;
  std::unique_ptr<p2p::AsyncPacketSocket> CreateClientTcpSocket(Timestamp now, const p2p::SocketAddress& local_address,
                                                                const p2p::SocketAddress& remote_address,
                                                                const p2p::PacketSocketTcpOptions& options) override;
  std::unique_ptr<p2p::Socket> CreateStreamSocket(p2p::IpFamily family) override;

  // Waits up to `wait` for socket events and dispatches them with `now`.
  void Poll(Timestamp now, TimeDelta wait);

  // Every packet socket the factory hands out is wrapped so that a test can
  // see the messages each one sends and receives (whole STUN or ChannelData
  // messages, after the TCP framing and TLS).
  using Tap = std::function<void(bool sent, std::span<const uint8_t> message)>;
  void set_tap(Tap tap) { tap_ = std::move(tap); }

 private:
  friend class OsUdpSocket;
  friend class OsTcpSocket;
  std::unique_ptr<p2p::AsyncPacketSocket> Wrap(std::unique_ptr<p2p::AsyncPacketSocket> socket);

  std::set<OsUdpSocket*> udp_;
  std::set<OsTcpSocket*> tcp_;
  Tap tap_;
};

// sockaddr_in conversions.
p2p::SocketAddress FromSockaddr(const void* sockaddr_in);
void ToSockaddr(const p2p::SocketAddress& a, void* sockaddr_in);

}  // namespace sm::interop
