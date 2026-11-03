// POSIX sockets for the interop tests (issue #1037); see os_sockets.h.
#include "os_sockets.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <utility>

#include "sm/p2p/async_tcp_socket.h"

// macOS has no MSG_NOSIGNAL; its sockets take SO_NOSIGPIPE instead.
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace sm::interop {

using p2p::SocketAddress;

int MapErrno(int e) {
  if (e == EWOULDBLOCK || e == EAGAIN) return p2p::kSocketEWouldBlock;
  switch (e) {
    case EINPROGRESS: return p2p::kSocketEInProgress;
    case EINVAL: return p2p::kSocketEInval;
    case EMSGSIZE: return p2p::kSocketEMsgSize;
    case EADDRINUSE: return p2p::kSocketEAddrInUse;
    case EADDRNOTAVAIL: return p2p::kSocketEAddrNotAvail;
    case ENOTCONN: return p2p::kSocketENotConn;
    case EBADF: return p2p::kSocketEBadF;
    case ECONNRESET: return p2p::kSocketEConnReset;
    case EISCONN: return p2p::kSocketEIsConn;
    case ETIMEDOUT: return p2p::kSocketETimedOut;
    case ECONNREFUSED: return p2p::kSocketEConnRefused;
    case EHOSTUNREACH: return p2p::kSocketEHostUnreach;
    default: return e;
  }
}

SocketAddress FromSockaddr(const void* p) {
  const auto* sin = static_cast<const sockaddr_in*>(p);
  return SocketAddress(ntohl(sin->sin_addr.s_addr), ntohs(sin->sin_port));
}

void ToSockaddr(const SocketAddress& a, void* p) {
  auto* sin = static_cast<sockaddr_in*>(p);
  std::memset(sin, 0, sizeof(*sin));
  sin->sin_family = AF_INET;
  sin->sin_addr.s_addr = htonl(a.ip());
  sin->sin_port = htons(a.port());
}

namespace {

int NonBlocking(int fd) { return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }

SocketAddress LocalOf(int fd) {
  sockaddr_in sin{};
  socklen_t len = sizeof(sin);
  if (getsockname(fd, reinterpret_cast<sockaddr*>(&sin), &len) != 0) return {};
  return FromSockaddr(&sin);
}

// Forwards everything to the socket it wraps and shows each message to the
// server's tap.
class TapSocket final : public p2p::AsyncPacketSocket {
 public:
  TapSocket(std::unique_ptr<p2p::AsyncPacketSocket> inner, OsSocketServer::Tap tap)
      : inner_(std::move(inner)), tap_(std::move(tap)) {
    inner_->RegisterReceivedPacketCallback([this](p2p::AsyncPacketSocket*, const p2p::ReceivedPacket& p) {
      tap_(false, p.payload);
      NotifyPacketReceived(p);
    });
    inner_->SubscribeSentPacket(this, [this](p2p::AsyncPacketSocket*, const p2p::SentPacketInfo& i) {
      NotifySentPacket(i);
    });
    inner_->SubscribeCloseEvent(this, [this](p2p::AsyncPacketSocket*, int err) { NotifyClosed(err); });
    inner_->SubscribeReadyToSend(this, [this](p2p::AsyncPacketSocket*) { NotifyReadyToSend(); });
    inner_->SubscribeConnect(this, [this](p2p::AsyncPacketSocket*, Timestamp now) { NotifyConnect(now); });
  }
  SocketAddress GetLocalAddress() const override { return inner_->GetLocalAddress(); }
  SocketAddress GetRemoteAddress() const override { return inner_->GetRemoteAddress(); }
  int Send(Timestamp now, std::span<const uint8_t> data, const p2p::PacketOptions& options) override {
    tap_(true, data);
    return inner_->Send(now, data, options);
  }
  int SendTo(Timestamp now, std::span<const uint8_t> data, const SocketAddress& addr,
             const p2p::PacketOptions& options) override {
    tap_(true, data);
    return inner_->SendTo(now, data, addr, options);
  }
  int Close() override { return inner_->Close(); }
  State GetState() const override { return inner_->GetState(); }
  int GetOption(p2p::SocketOption opt, int* value) override { return inner_->GetOption(opt, value); }
  int SetOption(p2p::SocketOption opt, int value) override { return inner_->SetOption(opt, value); }
  int GetError() const override { return inner_->GetError(); }
  void SetError(int error) override { inner_->SetError(error); }

 private:
  std::unique_ptr<p2p::AsyncPacketSocket> inner_;
  OsSocketServer::Tap tap_;
};

}  // namespace

// --- UDP ---

OsUdpSocket::~OsUdpSocket() { Close(); }

int OsUdpSocket::Send(Timestamp now, std::span<const uint8_t>, const p2p::PacketOptions& options) {
  NotifySentPacket(p2p::SentPacketInfo{options.packet_id, now, 0, 20});
  error_ = p2p::kSocketENotConn;
  return -1;
}

int OsUdpSocket::SendTo(Timestamp now, std::span<const uint8_t> data, const SocketAddress& addr,
                        const p2p::PacketOptions& options) {
  NotifySentPacket(p2p::SentPacketInfo{options.packet_id, now, data.size(), 20});
  if (fd_ < 0) {
    error_ = p2p::kSocketEBadF;
    return -1;
  }
  sockaddr_in sin;
  ToSockaddr(addr, &sin);
  const ssize_t n = ::sendto(fd_, data.data(), data.size(), 0, reinterpret_cast<sockaddr*>(&sin), sizeof(sin));
  if (n < 0) {
    error_ = MapErrno(errno);
    return -1;
  }
  return static_cast<int>(n);
}

int OsUdpSocket::Close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  if (server_) server_->udp_.erase(this);
  server_ = nullptr;
  return 0;
}

int OsUdpSocket::GetOption(p2p::SocketOption opt, int* value) {
  auto it = options_.find(opt);
  if (it == options_.end()) return -1;
  *value = it->second;
  return 0;
}

int OsUdpSocket::SetOption(p2p::SocketOption opt, int value) {
  options_[opt] = value;
  return 0;
}

void OsUdpSocket::OnReadable(Timestamp now) {
  std::array<uint8_t, 65536> buf;
  while (fd_ >= 0) {
    sockaddr_in from{};
    socklen_t len = sizeof(from);
    const ssize_t n = ::recvfrom(fd_, buf.data(), buf.size(), 0, reinterpret_cast<sockaddr*>(&from), &len);
    if (n < 0) return;
    p2p::ReceivedPacket p;
    p.payload = std::span<const uint8_t>(buf.data(), static_cast<size_t>(n));
    p.source_address = FromSockaddr(&from);
    p.arrival_time = now;
    NotifyPacketReceived(p);
  }
}

// --- TCP ---

OsTcpSocket::~OsTcpSocket() { Close(); }

SocketAddress OsTcpSocket::GetLocalAddress() const { return fd_ >= 0 ? LocalOf(fd_) : SocketAddress(); }

int OsTcpSocket::Bind(const SocketAddress& addr) {
  sockaddr_in sin;
  ToSockaddr(addr, &sin);
  if (::bind(fd_, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) != 0) {
    error_ = MapErrno(errno);
    return -1;
  }
  return 0;
}

int OsTcpSocket::Connect(Timestamp, const SocketAddress& addr) {
  if (state_ != ConnState::kClosed) {
    error_ = p2p::kSocketEAddrInUse;
    return -1;
  }
  sockaddr_in sin;
  ToSockaddr(addr, &sin);
  remote_ = addr;
  if (::connect(fd_, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) != 0 && errno != EINPROGRESS) {
    error_ = MapErrno(errno);
    return -1;
  }
  // Connected or not, the poll loop raises the connect event.
  state_ = ConnState::kConnecting;
  return 0;
}

int OsTcpSocket::Send(Timestamp, std::span<const uint8_t> data) {
  const ssize_t n = ::send(fd_, data.data(), data.size(), MSG_NOSIGNAL);
  if (n < 0) {
    error_ = MapErrno(errno);
    if (IsBlocking()) want_write_ = true;
    return -1;
  }
  if (static_cast<size_t>(n) < data.size()) want_write_ = true;
  return static_cast<int>(n);
}

int OsTcpSocket::Recv(Timestamp, std::span<uint8_t> buffer) {
  const ssize_t n = ::recv(fd_, buffer.data(), buffer.size(), 0);
  if (n < 0) {
    error_ = MapErrno(errno);
    return -1;
  }
  if (n == 0 && !buffer.empty()) {
    // PhysicalSocket::Recv: the close comes as an event, not as a read of 0.
    error_ = p2p::kSocketEWouldBlock;
    return -1;
  }
  return static_cast<int>(n);
}

int OsTcpSocket::Close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  state_ = ConnState::kClosed;
  if (server_) server_->tcp_.erase(this);
  server_ = nullptr;
  return 0;
}

int OsTcpSocket::GetOption(p2p::SocketOption opt, int* value) {
  if (opt != p2p::SocketOption::kNoDelay) return -1;
  socklen_t len = sizeof(*value);
  return ::getsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, value, &len);
}

int OsTcpSocket::SetOption(p2p::SocketOption opt, int value) {
  if (opt != p2p::SocketOption::kNoDelay) return -1;
  return ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value));
}

short OsTcpSocket::Events() const {
  switch (state_) {
    case ConnState::kConnecting: return POLLOUT;
    case ConnState::kConnected: return static_cast<short>(POLLIN | (want_write_ ? POLLOUT : 0));
    case ConnState::kClosed: return 0;
  }
  return 0;
}

void OsTcpSocket::OnEvents(Timestamp now, short revents) {
  if (state_ == ConnState::kConnecting) {
    int err = 0;
    socklen_t len = sizeof(err);
    ::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len);
    if (err != 0) {
      state_ = ConnState::kClosed;
      error_ = MapErrno(err);
      NotifyCloseEvent(error_);
      return;
    }
    state_ = ConnState::kConnected;
    NotifyConnectEvent(now);
    return;
  }
  if (state_ != ConnState::kConnected) return;
  if (revents & (POLLIN | POLLHUP | POLLERR)) {
    uint8_t b;
    const ssize_t n = ::recv(fd_, &b, 1, MSG_PEEK);
    if (n == 0 || (n < 0 && errno != EWOULDBLOCK && errno != EAGAIN)) {
      const int err = n == 0 ? 0 : MapErrno(errno);
      state_ = ConnState::kClosed;
      NotifyCloseEvent(err);
      return;
    }
    NotifyReadEvent(now);
    if (state_ != ConnState::kConnected) return;
  }
  if ((revents & POLLOUT) && want_write_) {
    want_write_ = false;
    NotifyWriteEvent(now);
  }
}

// --- Server ---

OsSocketServer::~OsSocketServer() {
  for (OsUdpSocket* s : udp_) s->server_ = nullptr;
  for (OsTcpSocket* s : tcp_) s->server_ = nullptr;
}

std::unique_ptr<p2p::AsyncPacketSocket> OsSocketServer::Wrap(std::unique_ptr<p2p::AsyncPacketSocket> socket) {
  if (!socket || !tap_) return socket;
  return std::make_unique<TapSocket>(std::move(socket), tap_);
}

std::unique_ptr<p2p::AsyncPacketSocket> OsSocketServer::CreateUdpSocket(const SocketAddress& address,
                                                                       uint16_t min_port, uint16_t max_port) {
  if (address.family() != p2p::IpFamily::kV4) return nullptr;
  const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return nullptr;
  NonBlocking(fd);
  bool bound = false;
  if (min_port == 0 && max_port == 0) {
    sockaddr_in sin;
    ToSockaddr(address, &sin);
    bound = ::bind(fd, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) == 0;
  } else {
    for (uint32_t port = min_port; port <= max_port && !bound; ++port) {
      sockaddr_in sin;
      ToSockaddr(SocketAddress(address.ipaddr(), static_cast<uint16_t>(port)), &sin);
      bound = ::bind(fd, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) == 0;
    }
  }
  if (!bound) {
    ::close(fd);
    return nullptr;
  }
  std::unique_ptr<OsUdpSocket> s(new OsUdpSocket(this, fd, LocalOf(fd)));
  udp_.insert(s.get());
  return Wrap(std::move(s));
}

std::unique_ptr<p2p::Socket> OsSocketServer::CreateStreamSocket(p2p::IpFamily family) {
  if (family != p2p::IpFamily::kV4) return nullptr;
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return nullptr;
  NonBlocking(fd);
#ifdef SO_NOSIGPIPE
  const int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
  std::unique_ptr<OsTcpSocket> s(new OsTcpSocket(this, fd));
  tcp_.insert(s.get());
  return s;
}

std::unique_ptr<p2p::AsyncPacketSocket> OsSocketServer::CreateClientTcpSocket(
    Timestamp now, const SocketAddress& local_address, const SocketAddress& remote_address,
    const p2p::PacketSocketTcpOptions& options) {
  return Wrap(p2p::CreateClientTcpSocket(*this, now, local_address, remote_address, options));
}

void OsSocketServer::Poll(Timestamp now, TimeDelta wait) {
  std::vector<pollfd> fds;
  std::vector<std::pair<OsUdpSocket*, OsTcpSocket*>> owners;
  for (OsUdpSocket* s : udp_) {
    fds.push_back(pollfd{s->fd_, POLLIN, 0});
    owners.emplace_back(s, nullptr);
  }
  for (OsTcpSocket* s : tcp_) {
    const short ev = s->Events();
    if (ev == 0) continue;
    fds.push_back(pollfd{s->fd_, ev, 0});
    owners.emplace_back(nullptr, s);
  }
  const int timeout_ms = static_cast<int>(std::max<int64_t>(0, wait.ms()));
  if (::poll(fds.data(), fds.size(), timeout_ms) <= 0) return;
  for (size_t i = 0; i < fds.size(); ++i) {
    if (fds[i].revents == 0) continue;
    // A callback may have closed or destroyed a socket polled with this one.
    if (OsUdpSocket* u = owners[i].first) {
      if (udp_.count(u) && u->fd_ == fds[i].fd) u->OnReadable(now);
    } else if (OsTcpSocket* t = owners[i].second) {
      if (tcp_.count(t) && t->fd_ == fds[i].fd) t->OnEvents(now, fds[i].revents);
    }
  }
}

}  // namespace sm::interop
