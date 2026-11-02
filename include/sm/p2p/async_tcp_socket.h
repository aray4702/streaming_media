// TCP packet sockets (issue #1034, C4 part 2, milestone 3): the base TURN
// over TCP and TLS stands on.
//
// libwebrtc M157 (compare/webrtc/reference/):
// - rtc_base/async_tcp_socket.{h,cc}: `AsyncTcpSocketBase` turns a stream
//   socket into a packet socket (an input buffer that grows from 128 bytes
//   to the largest packet, an output buffer flushed on write events),
//   `AsyncTcpSocket` frames each packet with a 2-byte big-endian length, and
//   `AsyncTcpListenSocket` accepts connections (backlog 5);
// - p2p/base/async_stun_tcp_socket.{h,cc}: `AsyncStunTcpSocket` frames by
//   the STUN header's length (20 + length) or TURN ChannelData's (4 +
//   length, padded to a multiple of 4 on the wire, RFC 5766 section 11.5),
//   and sends only whole messages;
// - p2p/base/basic_packet_socket_factory.cc: `CreateServerTcpSocket` and
//   `CreateClientTcpSocket` over a `StreamSocketFactory`.
//
// The behaviour is libwebrtc's, quirks included: a Send while the output
// buffer still holds an earlier packet reports success and drops the packet;
// a Send that made no progress drops it and returns the socket's result; a
// partial write keeps the rest for the next write event and reports the
// whole packet as sent. The library's time model replaces the clock: `Send`
// takes `now`, which stamps SentPacketInfo, and packets are stamped with the
// time of the read event that delivered them.
//
// Not here: TLS (OPT_TLS, OPT_TLS_INSECURE, OPT_TLS_FAKE), which comes with
// C4 part 3 (#1035); the factory returns nullptr for it, as libwebrtc's does
// for a server socket.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/async_packet_socket.h"
#include "sm/p2p/socket.h"
#include "sm/p2p/socket_address.h"

namespace sm::p2p {

class AsyncTcpSocketBase : public AsyncPacketSocket {
 public:
  AsyncTcpSocketBase(std::unique_ptr<Socket> socket, size_t max_packet_size);
  ~AsyncTcpSocketBase() override;

  int Send(Timestamp now, std::span<const uint8_t> data, const PacketOptions& options) override = 0;
  // Hands every whole packet at the start of `data` up; returns the bytes it
  // used.
  virtual size_t ProcessInput(Timestamp now, std::span<const uint8_t> data) = 0;

  SocketAddress GetLocalAddress() const override { return socket_->GetLocalAddress(); }
  SocketAddress GetRemoteAddress() const override { return socket_->GetRemoteAddress(); }
  // Only to the connected peer; anything else fails with ENOTCONN.
  int SendTo(Timestamp now, std::span<const uint8_t> data, const SocketAddress& addr,
             const PacketOptions& options) override;
  int Close() override { return socket_->Close(); }
  State GetState() const override;
  int GetOption(SocketOption opt, int* value) override { return socket_->GetOption(opt, value); }
  int SetOption(SocketOption opt, int value) override { return socket_->SetOption(opt, value); }
  int GetError() const override { return socket_->GetError(); }
  void SetError(int error) override { socket_->SetError(error); }

 protected:
  int FlushOutBuffer(Timestamp now);
  void AppendToOutBuffer(std::span<const uint8_t> data);
  bool IsOutBufferEmpty() const { return outbuf_.empty(); }
  void ClearOutBuffer() { outbuf_.clear(); }

 private:
  void OnReadEvent(Socket* socket, Timestamp now);
  void OnWriteEvent(Socket* socket, Timestamp now);

  std::unique_ptr<Socket> socket_;
  std::vector<uint8_t> inbuf_;  // Its size() is rtc::Buffer's capacity; inbuf_size_ the data in it.
  size_t inbuf_size_ = 0;
  std::vector<uint8_t> outbuf_;
  size_t max_insize_;
  size_t max_outsize_;
};

class AsyncTcpSocket : public AsyncTcpSocketBase {
 public:
  // kBufSize: 64 KiB plus the 2-byte length.
  static constexpr size_t kBufSize = 64 * 1024 + 2;

  explicit AsyncTcpSocket(std::unique_ptr<Socket> socket) : AsyncTcpSocketBase(std::move(socket), kBufSize) {}

  int Send(Timestamp now, std::span<const uint8_t> data, const PacketOptions& options) override;
  size_t ProcessInput(Timestamp now, std::span<const uint8_t> data) override;
};

class AsyncStunTcpSocket : public AsyncTcpSocketBase {
 public:
  // kBufSize: 64 KiB plus the STUN header.
  static constexpr size_t kBufSize = 64 * 1024 + 20;

  explicit AsyncStunTcpSocket(std::unique_ptr<Socket> socket) : AsyncTcpSocketBase(std::move(socket), kBufSize) {}

  int Send(Timestamp now, std::span<const uint8_t> data, const PacketOptions& options) override;
  size_t ProcessInput(Timestamp now, std::span<const uint8_t> data) override;

  // The message length the header at the start of `data` (at least 4 bytes)
  // announces, and the padding that follows it on the wire.
  static size_t GetExpectedLength(std::span<const uint8_t> data, int* pad_bytes);
};

class AsyncTcpListenSocket : public AsyncListenSocket {
 public:
  static constexpr int kListenBacklog = 5;

  explicit AsyncTcpListenSocket(std::unique_ptr<Socket> socket);
  ~AsyncTcpListenSocket() override;

  State GetState() const override;
  SocketAddress GetLocalAddress() const override { return socket_->GetLocalAddress(); }

 protected:
  // Wraps an accepted socket; AsyncTcpSocket here, as libwebrtc's.
  virtual std::unique_ptr<AsyncPacketSocket> WrapIncomingConnection(std::unique_ptr<Socket> socket) {
    return std::make_unique<AsyncTcpSocket>(std::move(socket));
  }

 private:
  void OnReadEvent(Socket* socket, Timestamp now);

  std::unique_ptr<Socket> socket_;
};

// BasicPacketSocketFactory's TCP methods over `sockets`.
std::unique_ptr<AsyncListenSocket> CreateServerTcpSocket(StreamSocketFactory& sockets,
                                                         const SocketAddress& local_address, uint16_t min_port,
                                                         uint16_t max_port, int opts);
std::unique_ptr<AsyncPacketSocket> CreateClientTcpSocket(StreamSocketFactory& sockets, Timestamp now,
                                                         const SocketAddress& local_address,
                                                         const SocketAddress& remote_address,
                                                         const PacketSocketTcpOptions& options);

}  // namespace sm::p2p
