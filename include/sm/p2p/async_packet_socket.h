// The packet socket the ICE ports send and receive through (issue #996, part
// of ICE #990, milestone 3).
//
// libwebrtc's rtc_base/async_packet_socket.{h,cc}, rtc_base/network/
// received_packet.h, rtc_base/network/sent_packet.h and
// api/packet_socket_factory.h at M157 (compare/webrtc/reference/), with the
// library's time model: nothing reads a clock or starts a thread, so `Send`
// and `SendTo` take `now`, and a socket's owner delivers received datagrams
// by calling into it (the emulated network does in sm/sim/emulated_sockets.h;
// an OS socket's event loop will, C11 and C12). Callbacks run on the caller's
// thread, inside the call that caused them.
//
// TCP packet sockets (AsyncTCPSocket, AsyncStunTCPSocket, the listen socket)
// came with C4 part 2 (#1034, sm/p2p/async_tcp_socket.h) and keep this
// interface; `AsyncListenSocket` and the factory's TCP methods are here.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/socket_address.h"

namespace sm::p2p {

// rtc_base/dscp.h's DiffServCodePoint values the library uses.
enum class Dscp : int {
  kNoChange = -1,
  kDefault = 0,
  kCs1 = 8,
  kAf41 = 34,
  kAf42 = 36,
  kEf = 46,
};

// AsyncSocketPacketOptions, without the RTP send-time rewriting fields
// (PacketTimeUpdateParams), which only Chromium's socket layer uses.
struct PacketOptions {
  Dscp dscp = Dscp::kNoChange;
  bool ect_1 = false;     // Send with ECN ECT(1) (L4S).
  int64_t packet_id = -1;  // Echoed in SentPacketInfo (transport-wide sequence number).
  bool batchable = false;
  bool last_packet_in_batch = false;
};

// SentPacketInfo with the PacketInfo fields CopySocketInformationToPacketInfo
// fills.
struct SentPacketInfo {
  int64_t packet_id = -1;
  Timestamp send_time = Timestamp::MinusInfinity();
  size_t packet_size_bytes = 0;  // UDP payload.
  size_t ip_overhead_bytes = 0;  // IP header: 20 for IPv4, 40 for IPv6 (IPAddress::overhead()).
};

// ReceivedIpPacket: a datagram as the socket hands it up. `payload` is valid
// only during the callback.
struct ReceivedPacket {
  // ReceivedIpPacket::DecryptionInfo: what the DTLS transport did with the packet (#1006).
  enum DecryptionInfo {
    kNotDecrypted,   // Not yet decrypted, or not encrypted at all.
    kDtlsDecrypted,  // Application data that came out of DTLS.
    kSrtpEncrypted,  // SRTP passed through DTLS untouched (bypass); SRTP decrypts it.
  };
  std::span<const uint8_t> payload;
  SocketAddress source_address;
  Timestamp arrival_time = Timestamp::MinusInfinity();
  DecryptionInfo decryption_info = kNotDecrypted;
};

// Socket::Option.
enum class SocketOption {
  kDontFragment,
  kRcvBuf,
  kSndBuf,
  kNoDelay,
  kIpv6V6Only,
  kDscp,
  kRtpSendTimeExtnId,
  kSendEcn,
  kRecvEcn,
};

// The errno values the sockets report through GetError(), as POSIX numbers
// so they mean the same on every platform.
inline constexpr int kSocketEInval = 22;
inline constexpr int kSocketEWouldBlock = 35;  // EWOULDBLOCK / EAGAIN as macOS numbers them.
inline constexpr int kSocketEMsgSize = 40;
inline constexpr int kSocketEAddrInUse = 48;
inline constexpr int kSocketEAddrNotAvail = 49;
inline constexpr int kSocketENotConn = 57;
inline constexpr int kSocketEBadF = 9;
inline constexpr int kSocketEInProgress = 36;
inline constexpr int kSocketEConnReset = 54;
inline constexpr int kSocketEIsConn = 56;
inline constexpr int kSocketETimedOut = 60;
inline constexpr int kSocketEConnRefused = 61;
inline constexpr int kSocketEHostUnreach = 65;

class AsyncPacketSocket {
 public:
  enum class State { kClosed, kBinding, kBound, kConnecting, kConnected };

  using ReceivedCallback = std::function<void(AsyncPacketSocket*, const ReceivedPacket&)>;
  using SentCallback = std::function<void(AsyncPacketSocket*, const SentPacketInfo&)>;
  using CloseCallback = std::function<void(AsyncPacketSocket*, int)>;
  using ReadyToSendCallback = std::function<void(AsyncPacketSocket*)>;
  // With the time of the connect event (the library passes `now` where
  // libwebrtc reads its clock).
  using ConnectCallback = std::function<void(AsyncPacketSocket*, Timestamp)>;

  AsyncPacketSocket() = default;
  AsyncPacketSocket(const AsyncPacketSocket&) = delete;
  AsyncPacketSocket& operator=(const AsyncPacketSocket&) = delete;
  virtual ~AsyncPacketSocket() = default;

  // The address the socket is bound to. A socket bound to the any address
  // reports it (0.0.0.0 or ::); its datagrams leave from the host's address.
  virtual SocketAddress GetLocalAddress() const = 0;
  // The connected peer, nil for an unconnected UDP socket.
  virtual SocketAddress GetRemoteAddress() const = 0;

  // Sends to the connected peer. Returns the bytes sent, or -1 with
  // GetError() set.
  virtual int Send(Timestamp now, std::span<const uint8_t> data, const PacketOptions& options) = 0;
  // Sends a datagram to `addr`. Returns the bytes sent, or -1 with GetError()
  // set. Every call, sent or not, reports SentPacketInfo, as AsyncUDPSocket
  // does.
  virtual int SendTo(Timestamp now, std::span<const uint8_t> data, const SocketAddress& addr,
                     const PacketOptions& options) = 0;
  virtual int Close() = 0;
  virtual State GetState() const = 0;
  virtual int GetOption(SocketOption opt, int* value) = 0;
  virtual int SetOption(SocketOption opt, int value) = 0;
  virtual int GetError() const = 0;
  virtual void SetError(int error) = 0;

  // One receiver for datagrams (RegisterReceivedPacketCallback, which
  // RTC_CHECKs there is only one). Without one, datagrams are dropped.
  void RegisterReceivedPacketCallback(ReceivedCallback cb);
  void DeregisterReceivedPacketCallback() { received_ = nullptr; }

  // Any number of subscribers, removed by tag (CallbackList).
  void SubscribeSentPacket(const void* tag, SentCallback cb) { sent_.emplace_back(tag, std::move(cb)); }
  void UnsubscribeSentPacket(const void* tag) { Remove(sent_, tag); }
  void SubscribeCloseEvent(const void* tag, CloseCallback cb) { closed_.emplace_back(tag, std::move(cb)); }
  void UnsubscribeCloseEvent(const void* tag) { Remove(closed_, tag); }
  void SubscribeReadyToSend(const void* tag, ReadyToSendCallback cb) { ready_.emplace_back(tag, std::move(cb)); }
  void UnsubscribeReadyToSend(const void* tag) { Remove(ready_, tag); }
  // A TCP socket's connection completed (SubscribeConnect).
  void SubscribeConnect(const void* tag, ConnectCallback cb) { connect_.emplace_back(tag, std::move(cb)); }
  void UnsubscribeConnect(const void* tag) { Remove(connect_, tag); }

  void NotifySentPacket(const SentPacketInfo& info);
  void NotifyReadyToSend();
  void NotifyConnect(Timestamp now);

 protected:
  void NotifyPacketReceived(const ReceivedPacket& packet) {
    if (received_) received_(this, packet);
  }
  void NotifyClosed(int err);

 private:
  template <class F>
  static void Remove(std::vector<std::pair<const void*, F>>& v, const void* tag) {
    std::erase_if(v, [tag](const auto& e) { return e.first == tag; });
  }

  ReceivedCallback received_;
  std::vector<std::pair<const void*, SentCallback>> sent_;
  std::vector<std::pair<const void*, CloseCallback>> closed_;
  std::vector<std::pair<const void*, ReadyToSendCallback>> ready_;
  std::vector<std::pair<const void*, ConnectCallback>> connect_;
};

// AsyncListenSocket: a listening TCP socket that hands out a packet socket
// for each peer that connects. libwebrtc passes the new socket as a raw
// pointer the subscriber takes; here the subscriber takes it by moving the
// `std::unique_ptr` out, and a socket nobody takes is closed.
class AsyncListenSocket {
 public:
  enum class State { kClosed, kBound };
  using NewConnectionCallback = std::function<void(AsyncListenSocket*, std::unique_ptr<AsyncPacketSocket>&)>;

  AsyncListenSocket() = default;
  AsyncListenSocket(const AsyncListenSocket&) = delete;
  AsyncListenSocket& operator=(const AsyncListenSocket&) = delete;
  virtual ~AsyncListenSocket() = default;

  virtual State GetState() const = 0;
  virtual SocketAddress GetLocalAddress() const = 0;

  void SubscribeNewConnection(const void* tag, NewConnectionCallback cb) {
    new_connection_.emplace_back(tag, std::move(cb));
  }
  void UnsubscribeNewConnection(const void* tag) {
    std::erase_if(new_connection_, [tag](const auto& e) { return e.first == tag; });
  }
  void NotifyNewConnection(std::unique_ptr<AsyncPacketSocket>& socket) {
    auto subscribers = new_connection_;
    for (auto& [tag, cb] : subscribers) cb(this, socket);
  }

 private:
  std::vector<std::pair<const void*, NewConnectionCallback>> new_connection_;
};

class SslCertificateVerifier;  // sm/p2p/ssl_adapter.h

// api/packet_socket_factory.h's PacketSocketTcpOptions. `tls_trusted_roots`
// (DER) stands in for libwebrtc's built-in roots (#1035).
struct PacketSocketTcpOptions {
  int opts = 0;
  std::vector<std::string> tls_alpn_protocols;
  std::vector<std::string> tls_elliptic_curves;
  SslCertificateVerifier* tls_cert_verifier = nullptr;  // Not owned.
  std::vector<std::vector<uint8_t>> tls_trusted_roots;
};

// api/packet_socket_factory.h: where the ports get their sockets.
class PacketSocketFactory {
 public:
  enum Options {
    kOptStun = 0x04,
    kOptDtls = 0x20,          // Real and secure DTLS.
    kOptDtlsInsecure = 0x10,  // Insecure DTLS without certificate validation.
    kOptTls = 0x02,           // Real and secure TLS.
    kOptTlsFake = 0x01,       // Fake TLS with a dummy SSL handshake.
    kOptTlsInsecure = 0x08,   // Insecure TLS without certificate validation.
  };

  PacketSocketFactory() = default;
  PacketSocketFactory(const PacketSocketFactory&) = delete;
  PacketSocketFactory& operator=(const PacketSocketFactory&) = delete;
  virtual ~PacketSocketFactory() = default;

  // A UDP socket bound to `address`'s IP. With `min_port` and `max_port`
  // both 0 the system picks the port (`address`'s port when it isn't 0);
  // otherwise the first free port in [min_port, max_port], tried in order
  // (BasicPacketSocketFactory::BindSocket). nullptr when it can't bind.
  virtual std::unique_ptr<AsyncPacketSocket> CreateUdpSocket(const SocketAddress& address, uint16_t min_port,
                                                             uint16_t max_port) = 0;

  // A TCP listen socket bound to `address`'s IP, the port picked as for UDP;
  // nullptr when it can't bind or `opts` asks for TLS
  // (BasicPacketSocketFactory::CreateServerTcpSocket). libwebrtc's methods
  // are pure virtual; here a factory without TCP (the tests' fakes) returns
  // nullptr.
  virtual std::unique_ptr<AsyncListenSocket> CreateServerTcpSocket(const SocketAddress& /*address*/,
                                                                   uint16_t /*min_port*/, uint16_t /*max_port*/,
                                                                   int /*opts*/) {
    return nullptr;
  }
  // A TCP socket connecting from `local_address` to `remote_address`
  // (CreateClientTcpSocket); framed as STUN/ChannelData with kOptStun,
  // otherwise with the 2-byte length prefix. The connect starts at `now`.
  virtual std::unique_ptr<AsyncPacketSocket> CreateClientTcpSocket(Timestamp /*now*/,
                                                                   const SocketAddress& /*local_address*/,
                                                                   const SocketAddress& /*remote_address*/,
                                                                   const PacketSocketTcpOptions& /*options*/) {
    return nullptr;
  }
};

}  // namespace sm::p2p
