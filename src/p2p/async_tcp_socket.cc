// libwebrtc's rtc_base/async_tcp_socket.cc, p2p/base/async_stun_tcp_socket.cc
// and the TCP half of p2p/base/basic_packet_socket_factory.cc at M157 (see
// the header).
#include "sm/p2p/async_tcp_socket.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <utility>

#include "sm/p2p/stun.h"

namespace sm::p2p {
namespace {

constexpr size_t kMinimumRecvSize = 128;
constexpr size_t kPacketLenSize = 2;     // sizeof(PacketLength).
constexpr size_t kPacketLenOffset = 2;   // Where STUN and ChannelData keep their length.
constexpr size_t kTurnChannelDataHdrSize = 4;

uint16_t GetBe16(std::span<const uint8_t> d) { return static_cast<uint16_t>((d[0] << 8) | d[1]); }

// The first two bits of a ChannelData message are 0b01.
bool IsStunMessage(uint16_t msg_type) { return (msg_type & 0xC000) == 0; }

}  // namespace

AsyncTcpSocketBase::AsyncTcpSocketBase(std::unique_ptr<Socket> socket, size_t max_packet_size)
    : socket_(std::move(socket)), max_insize_(max_packet_size), max_outsize_(max_packet_size) {
  assert(socket_);
  inbuf_.resize(kMinimumRecvSize);  // EnsureCapacity(kMinimumRecvSize).
  socket_->SubscribeConnectEvent(this, [this](Socket*, Timestamp) { NotifyConnect(); });
  socket_->SubscribeReadEvent(this, [this](Socket* s, Timestamp now) { OnReadEvent(s, now); });
  socket_->SubscribeWriteEvent(this, [this](Socket* s, Timestamp now) { OnWriteEvent(s, now); });
  socket_->SubscribeCloseEvent(this, [this](Socket*, int error) { NotifyClosed(error); });
}

AsyncTcpSocketBase::~AsyncTcpSocketBase() { socket_->UnsubscribeAll(this); }

AsyncPacketSocket::State AsyncTcpSocketBase::GetState() const {
  switch (socket_->GetState()) {
    case Socket::ConnState::kClosed: return State::kClosed;
    case Socket::ConnState::kConnecting: return State::kConnecting;
    case Socket::ConnState::kConnected: return State::kConnected;
  }
  return State::kClosed;
}

int AsyncTcpSocketBase::SendTo(Timestamp now, std::span<const uint8_t> data, const SocketAddress& addr,
                               const PacketOptions& options) {
  const SocketAddress remote = GetRemoteAddress();
  if (addr == remote) return Send(now, data, options);
  socket_->SetError(kSocketENotConn);
  return -1;
}

int AsyncTcpSocketBase::FlushOutBuffer(Timestamp now) {
  assert(!outbuf_.empty());
  std::span<const uint8_t> view(outbuf_);
  int res = 0;
  while (!view.empty()) {
    res = socket_->Send(now, view);
    if (res <= 0) break;
    if (static_cast<size_t>(res) > view.size()) {
      res = -1;
      break;
    }
    view = view.subspan(static_cast<size_t>(res));
  }
  if (res > 0) {
    res = static_cast<int>(outbuf_.size());
    outbuf_.clear();
  } else {
    // A blocked socket made progress worth reporting; keep what is left.
    if (socket_->GetError() == kSocketEWouldBlock) res = static_cast<int>(outbuf_.size() - view.size());
    if (view.size() < outbuf_.size()) outbuf_.erase(outbuf_.begin(), outbuf_.end() - static_cast<long>(view.size()));
  }
  return res;
}

void AsyncTcpSocketBase::AppendToOutBuffer(std::span<const uint8_t> data) {
  assert(outbuf_.size() + data.size() <= max_outsize_);
  outbuf_.insert(outbuf_.end(), data.begin(), data.end());
}

void AsyncTcpSocketBase::OnReadEvent(Socket* socket, Timestamp now) {
  assert(socket_.get() == socket);
  (void)socket;
  size_t total_recv = 0;
  for (;;) {
    size_t free_size = inbuf_.size() - inbuf_size_;
    if (free_size < kMinimumRecvSize && inbuf_.size() < max_insize_) {
      inbuf_.resize(std::min(max_insize_, inbuf_.size() * 2));
      free_size = inbuf_.size() - inbuf_size_;
    }
    const int len = socket_->Recv(now, std::span<uint8_t>(inbuf_.data() + inbuf_size_, free_size));
    if (len < 0) break;
    total_recv += static_cast<size_t>(len);
    inbuf_size_ += static_cast<size_t>(len);
    if (len == 0 || static_cast<size_t>(len) < free_size) break;
  }
  if (total_recv == 0) return;

  const size_t processed = ProcessInput(now, std::span<const uint8_t>(inbuf_.data(), inbuf_size_));
  if (processed > inbuf_size_) {
    inbuf_size_ = 0;  // "input buffer overflow".
  } else if (processed > 0) {
    std::memmove(inbuf_.data(), inbuf_.data() + processed, inbuf_size_ - processed);
    inbuf_size_ -= processed;
  }
}

void AsyncTcpSocketBase::OnWriteEvent(Socket* socket, Timestamp now) {
  assert(socket_.get() == socket);
  (void)socket;
  if (!outbuf_.empty()) FlushOutBuffer(now);
  if (outbuf_.empty()) NotifyReadyToSend();
}

int AsyncTcpSocket::Send(Timestamp now, std::span<const uint8_t> data, const PacketOptions& options) {
  if (data.size() > kBufSize) {
    SetError(kSocketEMsgSize);
    return -1;
  }
  // Blocked on an earlier packet: report success and drop this one.
  if (!IsOutBufferEmpty()) return static_cast<int>(data.size());

  const uint16_t len = static_cast<uint16_t>(data.size());  // PacketLength, as libwebrtc truncates it.
  const uint8_t header[kPacketLenSize] = {static_cast<uint8_t>(len >> 8), static_cast<uint8_t>(len)};
  AppendToOutBuffer(header);
  AppendToOutBuffer(data);

  const int res = FlushOutBuffer(now);
  if (res <= 0) {
    ClearOutBuffer();
    return res;
  }
  // CopySocketInformationToPacketInfo.
  NotifySentPacket(SentPacketInfo{options.packet_id, now, data.size(),
                                  static_cast<size_t>(GetLocalAddress().ipaddr().overhead())});
  return static_cast<int>(data.size());
}

size_t AsyncTcpSocket::ProcessInput(Timestamp now, std::span<const uint8_t> data) {
  const SocketAddress remote = GetRemoteAddress();
  size_t processed = 0;
  for (;;) {
    const size_t left = data.size() - processed;
    if (left < kPacketLenSize) return processed;
    const size_t pkt_len = GetBe16(data.subspan(processed, kPacketLenSize));
    if (left < kPacketLenSize + pkt_len) return processed;
    NotifyPacketReceived(ReceivedPacket{data.subspan(processed + kPacketLenSize, pkt_len), remote, now,
                                        ReceivedPacket::kNotDecrypted});
    processed += kPacketLenSize + pkt_len;
  }
}

size_t AsyncStunTcpSocket::GetExpectedLength(std::span<const uint8_t> data, int* pad_bytes) {
  *pad_bytes = 0;
  const size_t pkt_len = GetBe16(data.subspan(kPacketLenOffset, 2));
  if (IsStunMessage(GetBe16(data))) return kStunHeaderSize + pkt_len;
  // TURN ChannelData: padded to a multiple of 4 over TCP (RFC 5766 11.5),
  // and the padding is not in the length field.
  const size_t expected = kTurnChannelDataHdrSize + pkt_len;
  if (expected % 4) *pad_bytes = static_cast<int>(4 - expected % 4);
  return expected;
}

int AsyncStunTcpSocket::Send(Timestamp now, std::span<const uint8_t> data, const PacketOptions& options) {
  if (data.size() > kBufSize || data.size() < kPacketLenSize + kPacketLenOffset) {
    SetError(kSocketEMsgSize);
    return -1;
  }
  // Blocked on an earlier packet: report success and drop this one.
  if (!IsOutBufferEmpty()) return static_cast<int>(data.size());

  int pad_bytes = 0;
  const size_t expected = GetExpectedLength(data, &pad_bytes);
  if (data.size() != expected) return -1;  // Only whole STUN or ChannelData messages.

  AppendToOutBuffer(data);
  assert(pad_bytes < 4);
  const uint8_t padding[4] = {0, 0, 0, 0};
  AppendToOutBuffer(std::span<const uint8_t>(padding, static_cast<size_t>(pad_bytes)));

  const int res = FlushOutBuffer(now);
  if (res <= 0) {
    ClearOutBuffer();  // No progress: drop the packet.
    return res;
  }
  // SentPacketInfo(packet_id, time) without the packet info, as libwebrtc.
  NotifySentPacket(SentPacketInfo{options.packet_id, now, 0, 0});
  // Claims the whole packet even after a partial write.
  return static_cast<int>(data.size());
}

size_t AsyncStunTcpSocket::ProcessInput(Timestamp now, std::span<const uint8_t> data) {
  const SocketAddress remote = GetRemoteAddress();
  size_t processed = 0;
  for (;;) {
    const size_t left = data.size() - processed;
    if (left < kPacketLenOffset + kPacketLenSize) return processed;
    int pad_bytes = 0;
    const size_t expected = GetExpectedLength(data.subspan(processed), &pad_bytes);
    const size_t actual = expected + static_cast<size_t>(pad_bytes);
    if (left < actual) return processed;
    NotifyPacketReceived(
        ReceivedPacket{data.subspan(processed, expected), remote, now, ReceivedPacket::kNotDecrypted});
    processed += actual;
  }
}

AsyncTcpListenSocket::AsyncTcpListenSocket(std::unique_ptr<Socket> socket) : socket_(std::move(socket)) {
  assert(socket_);
  socket_->SubscribeReadEvent(this, [this](Socket* s, Timestamp now) { OnReadEvent(s, now); });
  socket_->Listen(kListenBacklog);  // libwebrtc logs a failure and carries on.
}

AsyncTcpListenSocket::~AsyncTcpListenSocket() { socket_->UnsubscribeAll(this); }

AsyncListenSocket::State AsyncTcpListenSocket::GetState() const {
  // A listening socket reports CS_CONNECTING.
  return socket_->GetState() == Socket::ConnState::kConnecting ? State::kBound : State::kClosed;
}

void AsyncTcpListenSocket::OnReadEvent(Socket* socket, Timestamp now) {
  assert(socket_.get() == socket);
  SocketAddress address;
  std::unique_ptr<Socket> accepted = socket->Accept(now, &address);
  if (!accepted) return;  // "TCP accept failed".
  Socket* raw = accepted.get();
  std::unique_ptr<AsyncPacketSocket> packet_socket = WrapIncomingConnection(std::move(accepted));
  NotifyNewConnection(packet_socket);
  // Data that came with the connection; only if a subscriber kept the socket.
  if (!packet_socket) raw->NotifyReadEvent(now);
}

std::unique_ptr<AsyncListenSocket> CreateServerTcpSocket(StreamSocketFactory& sockets,
                                                         const SocketAddress& local_address, uint16_t min_port,
                                                         uint16_t max_port, int opts) {
  // TLS is not available on a server socket, real or fake.
  if (opts & (PacketSocketFactory::kOptTls | PacketSocketFactory::kOptTlsFake)) return nullptr;
  std::unique_ptr<Socket> socket = sockets.CreateStreamSocket(local_address.family());
  if (!socket) return nullptr;
  // BindSocket.
  int ret = -1;
  if (min_port == 0 && max_port == 0) {
    ret = socket->Bind(local_address);
  } else {
    for (uint32_t port = min_port; ret < 0 && port <= max_port; ++port) {
      ret = socket->Bind(SocketAddress(local_address.ipaddr(), static_cast<uint16_t>(port)));
    }
  }
  if (ret < 0) return nullptr;
  // libwebrtc RTC_CHECKs that OPT_STUN is not asked for.
  assert(!(opts & PacketSocketFactory::kOptStun));
  if (opts & PacketSocketFactory::kOptStun) return nullptr;
  return std::make_unique<AsyncTcpListenSocket>(std::move(socket));
}

std::unique_ptr<AsyncPacketSocket> CreateClientTcpSocket(StreamSocketFactory& sockets, Timestamp now,
                                                         const SocketAddress& local_address,
                                                         const SocketAddress& remote_address,
                                                         const PacketSocketTcpOptions& options) {
  std::unique_ptr<Socket> socket = sockets.CreateStreamSocket(local_address.family());
  if (!socket) return nullptr;
  // A failed bind to the any address is fine: Connect binds.
  if (socket->Bind(local_address) < 0 && !local_address.IsAnyIp()) return nullptr;
  // TCP_NODELAY before Connect, so small media packets go at once.
  socket->SetOption(SocketOption::kNoDelay, 1);
  const int tls = options.opts & (PacketSocketFactory::kOptTls | PacketSocketFactory::kOptTlsFake |
                                  PacketSocketFactory::kOptTlsInsecure);
  assert((tls & (tls - 1)) == 0 && "at most one TLS option");
  if (tls) return nullptr;  // TLS: C4 part 3 (#1035).
  if (socket->Connect(now, remote_address) < 0) return nullptr;
  if (options.opts & PacketSocketFactory::kOptStun) return std::make_unique<AsyncStunTcpSocket>(std::move(socket));
  return std::make_unique<AsyncTcpSocket>(std::move(socket));
}

}  // namespace sm::p2p
