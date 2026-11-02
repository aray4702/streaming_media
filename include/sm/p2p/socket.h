// The stream socket under the TCP packet sockets (issue #1034, C4 part 2,
// milestone 3).
//
// libwebrtc's rtc_base/socket.h `Socket` at M157
// (compare/webrtc/reference/rtc_base/socket.h), for the stream (SOCK_STREAM)
// sockets that AsyncTCPSocketBase wraps, with the library's time model:
// nothing reads a clock or starts a thread, so the calls that can put
// segments on the wire take `now`, and the events carry the time of the
// call that raised them. A socket's owner drives it: the emulated network
// does (sm/sim/emulated_sockets.h); an OS socket's event loop will (C11, C12).
//
// Differences from libwebrtc's interface, none of them behavioural:
// - `Accept` returns a `std::unique_ptr` (libwebrtc returns a raw pointer
//   the caller owns).
// - `RecvFrom`, `SendTo` and ECN, which only datagram sockets use, are left
//   out: the UDP side keeps `AsyncPacketSocket` directly.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/async_packet_socket.h"
#include "sm/p2p/socket_address.h"

namespace sm::p2p {

// IsBlockingError (rtc_base/socket.h): EWOULDBLOCK, EAGAIN (the same number
// on macOS) and EINPROGRESS.
inline bool IsBlockingError(int e) { return e == kSocketEWouldBlock || e == kSocketEInProgress; }

class Socket {
 public:
  enum class ConnState { kClosed, kConnecting, kConnected };

  using EventCallback = std::function<void(Socket*, Timestamp now)>;
  using CloseCallback = std::function<void(Socket*, int error)>;

  Socket() = default;
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;
  virtual ~Socket() = default;

  virtual SocketAddress GetLocalAddress() const = 0;
  virtual SocketAddress GetRemoteAddress() const = 0;

  virtual int Bind(const SocketAddress& addr) = 0;
  virtual int Connect(Timestamp now, const SocketAddress& addr) = 0;
  // Returns the bytes taken (possibly fewer than `data`), or -1 with
  // GetError() set (EWOULDBLOCK when the send buffer is full).
  virtual int Send(Timestamp now, std::span<const uint8_t> data) = 0;
  // Returns the bytes read into `buffer`, or -1 with GetError() set
  // (EWOULDBLOCK when nothing is buffered).
  virtual int Recv(Timestamp now, std::span<uint8_t> buffer) = 0;
  virtual int Listen(int backlog) = 0;
  // A connection from the listen queue, or nullptr with GetError() set
  // (EWOULDBLOCK when the queue is empty, EINVAL when not listening).
  virtual std::unique_ptr<Socket> Accept(Timestamp now, SocketAddress* paddr) = 0;
  virtual int Close() = 0;
  virtual int GetError() const = 0;
  virtual void SetError(int error) = 0;
  bool IsBlocking() const { return IsBlockingError(GetError()); }

  // A listening socket reports kConnecting, as libwebrtc's sockets do.
  virtual ConnState GetState() const = 0;

  virtual int GetOption(SocketOption opt, int* value) = 0;
  virtual int SetOption(SocketOption opt, int value) = 0;

  // Any number of subscribers per event, removed by tag (CallbackList).
  void SubscribeReadEvent(const void* tag, EventCallback cb) { read_.emplace_back(tag, std::move(cb)); }
  void SubscribeWriteEvent(const void* tag, EventCallback cb) { write_.emplace_back(tag, std::move(cb)); }
  void SubscribeConnectEvent(const void* tag, EventCallback cb) { connect_.emplace_back(tag, std::move(cb)); }
  void SubscribeCloseEvent(const void* tag, CloseCallback cb) { close_.emplace_back(tag, std::move(cb)); }
  void UnsubscribeAll(const void* tag) {
    Remove(read_, tag);
    Remove(write_, tag);
    Remove(connect_, tag);
    Remove(close_, tag);
  }

  // Callbacks may unsubscribe while they run, so each walks a copy.
  void NotifyReadEvent(Timestamp now) { Fire(read_, now); }
  void NotifyWriteEvent(Timestamp now) { Fire(write_, now); }
  void NotifyConnectEvent(Timestamp now) { Fire(connect_, now); }
  void NotifyCloseEvent(int error) {
    auto subscribers = close_;
    for (auto& [tag, cb] : subscribers) cb(this, error);
  }

 private:
  template <class F>
  static void Remove(std::vector<std::pair<const void*, F>>& v, const void* tag) {
    std::erase_if(v, [tag](const auto& e) { return e.first == tag; });
  }
  void Fire(const std::vector<std::pair<const void*, EventCallback>>& v, Timestamp now) {
    auto subscribers = v;
    for (auto& [tag, cb] : subscribers) cb(this, now);
  }

  std::vector<std::pair<const void*, EventCallback>> read_;
  std::vector<std::pair<const void*, EventCallback>> write_;
  std::vector<std::pair<const void*, EventCallback>> connect_;
  std::vector<std::pair<const void*, CloseCallback>> close_;
};

// Where BasicPacketSocketFactory gets its stream sockets
// (rtc_base/socket_factory.h's SocketFactory::Create with SOCK_STREAM).
class StreamSocketFactory {
 public:
  virtual ~StreamSocketFactory() = default;
  virtual std::unique_ptr<Socket> CreateStreamSocket(IpFamily family) = 0;
};

}  // namespace sm::p2p
