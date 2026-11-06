// POSIX sockets for the interop tests and the apps (issue #1037, C4 part 5; a library
// of its own since #1103, for the AI server): the library's
// packet and stream socket interfaces (sm/p2p/async_packet_socket.h,
// sm/p2p/socket.h) over non-blocking OS sockets, so a TurnPort can talk to a
// real TURN server (coturn) on loopback or on a real host.
//
// The library reads no clock and starts no thread; the test's event loop
// plays libwebrtc's PhysicalSocketServer: Poll(now, wait) waits on every open
// socket with poll(2) and raises the read, write, connect and close events
// (PhysicalSocket's dispatcher). `now` is the loop's clock when it calls Poll;
// an event gets the time Poll handles it, `now` plus the time since the call
// on the steady clock (#1168), and a datagram the kernel's receive time moved
// onto that clock, as libwebrtc's PhysicalSocket and AsyncUDPSocket do (#1190;
// see Poll). TCP sockets are made by the library's CreateClientTcpSocket, which
// adds the STUN framing and, for TLS, the SSL adapter, as
// BasicPacketSocketFactory does over PhysicalSocket.
//
// IPv4 only, no DSCP or ECN, and no listening TCP sockets (a TCP candidate's
// passive side).
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
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
  void OnReadable();

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

  // Waits up to `wait` for socket events and dispatches them. `now` is the caller's clock at this call (read it
  // just before); each event is stamped with `now` plus the steady-clock time Poll has run until it is handled.
  // The sender's congestion control reads datagram arrival times back from transport-wide feedback, and its
  // probes (15-20 ms clusters) measure the link rate from them, so a datagram gets the time it arrived:
  //  * the kernel's receive time (SO_TIMESTAMP), as libwebrtc's PhysicalSocket reads it, moved onto the caller's
  //    clock, so a datagram that waited in the socket while this thread was not running keeps its own time (#1190:
  //    on a loaded host, a probe cluster read in one batch measured 0 ms or 20-30 Mbps and Chrome discarded it);
  //  * never after the read, and never before `earliest`: the caller's clock at its last call into whatever
  //    receives the datagrams (default `now`; it must not be later than `now`). A datagram that arrived during the
  //    caller's work since then keeps that time, not the time of this call;
  //  * where the kernel gives no time, the time it is read (#1168: stamping every datagram with `now`, the time
  //    before the wait, put a packet that arrived 8 ms into a 10 ms wait 8 ms early).
  void Poll(Timestamp now, TimeDelta wait, std::optional<Timestamp> earliest = std::nullopt);

  // Every packet socket the factory hands out is wrapped so that a test can
  // see the messages each one sends and receives (whole STUN or ChannelData
  // messages, after the TCP framing and TLS).
  using Tap = std::function<void(bool sent, std::span<const uint8_t> message)>;
  void set_tap(Tap tap) { tap_ = std::move(tap); }

 private:
  friend class OsUdpSocket;
  friend class OsTcpSocket;
  std::unique_ptr<p2p::AsyncPacketSocket> Wrap(std::unique_ptr<p2p::AsyncPacketSocket> socket);

  // The caller's clock now: the `now` Poll was given plus the steady time since.
  Timestamp ReadTime() const;
  // The time of an event handled now, or of a datagram read now: its kernel receive time (SO_TIMESTAMP, wall-clock
  // microseconds) moved onto the caller's clock, else ReadTime(). Within [poll_floor_, ReadTime()], and not before
  // the previous one.
  Timestamp ArrivalTime(std::optional<int64_t> kernel_us);
  static int64_t SteadyMicros();
  static int64_t WallMicros();

  std::set<OsUdpSocket*> udp_;
  std::set<OsTcpSocket*> tcp_;
  Tap tap_;
  Timestamp poll_now_ = Timestamp::Micros(0);
  Timestamp poll_floor_ = Timestamp::Micros(0);  // Poll's `earliest`.
  Timestamp last_arrival_ = Timestamp::MinusInfinity();
  int64_t poll_start_us_ = 0;
};

// sockaddr_in conversions.
p2p::SocketAddress FromSockaddr(const void* sockaddr_in);
void ToSockaddr(const p2p::SocketAddress& a, void* sockaddr_in);

}  // namespace sm::interop
