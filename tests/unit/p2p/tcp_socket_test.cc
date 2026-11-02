// TCP packet sockets and TCP in the emulated network (issue #1034, C4 part 2).
//
// - AsyncTcpSocket: libwebrtc's rtc_base/async_tcp_socket_unittest.cc
//   (OnWriteEvent), plus the 2-byte framing both ways through the factory.
// - AsyncStunTcpSocket: every case of p2p/base/async_stun_tcp_socket_unittest.cc
//   at M157, on two hosts of the emulated network in place of the
//   VirtualSocketServer (its 11.11.11.11 and 22.22.22.22).
// - EmulatedTcp: the stream model (virtual_socket_server.cc's buffers,
//   window, MSS, connect, accept, close and refusal), a TCP echo
//   (rtc_base/test_echo_server.h) from behind each of libwebrtc's test NAT
//   types, and the recovery the emulated links need because they lose
//   segments.
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/async_packet_socket.h"
#include "sm/p2p/async_tcp_socket.h"
#include "sm/p2p/socket.h"
#include "sm/p2p/socket_address.h"
#include "sm/sim/emulated_sockets.h"
#include "sm_test.h"

using namespace sm;
using p2p::AsyncPacketSocket;
using p2p::IpAddress;
using p2p::ReceivedPacket;

namespace {

constexpr uint32_t kClientIp = sim::SocketAddress::Ip(11, 11, 11, 11);
constexpr uint32_t kServerIp = sim::SocketAddress::Ip(22, 22, 22, 22);

sim::LinkConfig Link(int delay_ms, double loss = 0) {
  sim::LinkConfig c;
  c.base_delay = TimeDelta::Millis(delay_ms);
  c.loss_rate = loss;
  return c;
}

using Packet = std::vector<uint8_t>;

// Runs every server's deliveries and timers in time order until nothing is
// left to do, or for `horizon` after the first event (window probes never
// stop); returns the time of the last event it ran.
Timestamp Run(const std::vector<sim::EmulatedSocketServer*>& servers,
              TimeDelta horizon = TimeDelta::Seconds(1000)) {
  Timestamp last = Timestamp::Micros(0);
  Timestamp until = Timestamp::PlusInfinity();
  for (;;) {
    Timestamp t = Timestamp::PlusInfinity();
    for (auto* s : servers) t = std::min(t, s->NextEvent());
    if (!t.IsFinite() || t > until) return last;
    if (!until.IsFinite()) until = t + horizon;
    for (auto* s : servers) s->Deliver(t);
    last = t;
  }
}

// AsyncStunTCPSocketTest's AsyncStunServerTCPSocket.
class StunListenSocket final : public p2p::AsyncTcpListenSocket {
 public:
  using AsyncTcpListenSocket::AsyncTcpListenSocket;

 protected:
  std::unique_ptr<AsyncPacketSocket> WrapIncomingConnection(std::unique_ptr<p2p::Socket> socket) override {
    return std::make_unique<p2p::AsyncStunTcpSocket>(std::move(socket));
  }
};

// AsyncStunTCPSocketTest's fixture: a STUN TCP client on 11.11.11.11
// connected to a STUN TCP listener on 22.22.22.22.
struct StunFixture {
  sim::ByteNetwork net;
  sim::EmulatedSocketServer client_host{net, net.AddHost(kClientIp, 0, Link(5), Link(5))};
  sim::EmulatedSocketServer server_host{net, net.AddHost(kServerIp, 0, Link(5), Link(5))};
  std::unique_ptr<p2p::AsyncListenSocket> listen_socket;
  std::unique_ptr<p2p::AsyncStunTcpSocket> send_socket;
  std::unique_ptr<AsyncPacketSocket> recv_socket;
  std::vector<Packet> recv_packets;
  int sent_packets = 0;
  Timestamp now = Timestamp::Micros(0);

  StunFixture() {
    auto server = server_host.CreateStreamSocket(p2p::IpFamily::kV4);
    server->Bind(p2p::SocketAddress(server_host.ip(), 0));
    listen_socket = std::make_unique<StunListenSocket>(std::move(server));
    listen_socket->SubscribeNewConnection(this, [this](p2p::AsyncListenSocket*, std::unique_ptr<AsyncPacketSocket>& s) {
      recv_socket = std::move(s);
      recv_socket->RegisterReceivedPacketCallback([this](AsyncPacketSocket*, const ReceivedPacket& p) {
        recv_packets.emplace_back(p.payload.begin(), p.payload.end());
      });
    });
    auto client = client_host.CreateStreamSocket(p2p::IpFamily::kV4);
    SM_EXPECT_EQ(client->Bind(p2p::SocketAddress(client_host.ip(), 0)), 0);
    SM_EXPECT_EQ(client->Connect(now, listen_socket->GetLocalAddress()), 0);
    send_socket = std::make_unique<p2p::AsyncStunTcpSocket>(std::move(client));
    send_socket->SubscribeSentPacket(this, [this](AsyncPacketSocket*, const p2p::SentPacketInfo&) { ++sent_packets; });
    now = Run({&client_host, &server_host});
  }
  bool Send(std::span<const uint8_t> data) {
    const int ret = send_socket->Send(now, data, {});
    now = Run({&client_host, &server_host});
    return ret == static_cast<int>(data.size());
  }
};

constexpr auto kStunMessageWithZeroLength = std::to_array<uint8_t>({
    0x00, 0x01, 0x00, 0x00,  // Length 0 (the last 2 bytes).
    0x21, 0x12, 0xA4, 0x42, '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b',
});
constexpr auto kTurnChannelDataMessageWithZeroLength = std::to_array<uint8_t>({0x40, 0x00, 0x00, 0x00});
constexpr auto kTurnChannelDataMessage = std::to_array<uint8_t>({
    0x40, 0x00, 0x00, 0x10, 0x21, 0x12, 0xA4, 0x42, '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b',
});
constexpr auto kStunMessageWithInvalidLength = std::to_array<uint8_t>({
    0x00, 0x01, 0x00, 0x10, 0x21, 0x12, 0xA4, 0x42, '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b',
});
constexpr auto kTurnChannelDataMessageWithInvalidLength = std::to_array<uint8_t>({
    0x80, 0x00, 0x00, 0x20, 0x21, 0x12, 0xA4, 0x42, '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b',
});
constexpr auto kTurnChannelDataMessageWithOddLength =
    std::to_array<uint8_t>({0x40, 0x00, 0x00, 0x05, 0x21, 0x12, 0xA4, 0x42, '0'});
constexpr uint8_t kTurnChannelDataMarker = 0x40;
constexpr size_t kMaxTurnPacketSize = 65539;
constexpr size_t kMaxStunPacketSize = 65552;

bool Equal(const Packet& p, std::span<const uint8_t> want) { return std::equal(p.begin(), p.end(), want.begin(), want.end()); }

Packet Bytes(const std::string& s) { return Packet(s.begin(), s.end()); }

}  // namespace

// --- rtc_base/async_tcp_socket_unittest.cc ---

SM_TEST(AsyncTcpSocket, OnWriteEvent) {
  sim::ByteNetwork net;
  sim::EmulatedSocketServer host(net, net.AddHost(kClientIp));
  std::unique_ptr<p2p::Socket> socket = host.CreateStreamSocket(p2p::IpFamily::kV4);
  SM_ASSERT_TRUE(socket != nullptr);
  p2p::Socket& socket_ref = *socket;
  p2p::AsyncTcpSocket tcp_socket(std::move(socket));
  bool ready_to_send = false;
  tcp_socket.SubscribeReadyToSend(&ready_to_send, [&](AsyncPacketSocket*) { ready_to_send = true; });
  SM_EXPECT_FALSE(ready_to_send);
  socket_ref.NotifyWriteEvent(Timestamp::Micros(0));
  SM_EXPECT_TRUE(ready_to_send);
}

// The factory's sockets: 2-byte framing both ways, the connect event, the
// peer's address on received packets, SentPacketInfo as
// CopySocketInformationToPacketInfo fills it.
SM_TEST(AsyncTcpSocket, FramesPacketsBothWaysThroughTheFactory) {
  sim::ByteNetwork net;
  sim::EmulatedSocketServer a(net, net.AddHost(kClientIp, 0, Link(10), Link(10)));
  sim::EmulatedSocketServer b(net, net.AddHost(kServerIp, 0, Link(7), Link(7)));
  auto listen = b.CreateServerTcpSocket(p2p::SocketAddress(b.ip(), 0), 0, 0, 0);
  SM_ASSERT_TRUE(listen != nullptr);
  SM_EXPECT_TRUE(listen->GetState() == p2p::AsyncListenSocket::State::kBound);
  std::unique_ptr<AsyncPacketSocket> accepted;
  std::vector<Packet> at_b, at_a;
  std::vector<p2p::SocketAddress> b_from;
  listen->SubscribeNewConnection(nullptr, [&](p2p::AsyncListenSocket*, std::unique_ptr<AsyncPacketSocket>& s) {
    accepted = std::move(s);
    accepted->RegisterReceivedPacketCallback([&](AsyncPacketSocket*, const ReceivedPacket& p) {
      at_b.emplace_back(p.payload.begin(), p.payload.end());
      b_from.push_back(p.source_address);
    });
  });
  auto client = a.CreateClientTcpSocket(Timestamp::Micros(0), p2p::SocketAddress(IpAddress(0u), 0),
                                        listen->GetLocalAddress(), {});
  SM_ASSERT_TRUE(client != nullptr);
  SM_EXPECT_TRUE(client->GetState() == AsyncPacketSocket::State::kConnecting);
  int no_delay = 0;
  SM_EXPECT_EQ(client->GetOption(p2p::SocketOption::kNoDelay, &no_delay), 0);
  SM_EXPECT_EQ(no_delay, 1);  // TCP_NODELAY before Connect.
  bool connected = false;
  client->SubscribeConnect(nullptr, [&](AsyncPacketSocket*) { connected = true; });
  client->RegisterReceivedPacketCallback(
      [&](AsyncPacketSocket*, const ReceivedPacket& p) { at_a.emplace_back(p.payload.begin(), p.payload.end()); });
  std::vector<p2p::SentPacketInfo> sent;
  client->SubscribeSentPacket(nullptr, [&](AsyncPacketSocket*, const p2p::SentPacketInfo& i) { sent.push_back(i); });

  Timestamp now = Run({&a, &b});
  SM_ASSERT_TRUE(connected);
  // The SYN-ACK came back after one round trip: 10 + 7 out, 7 + 10 back.
  SM_EXPECT_EQ(now.ms(), 34);
  SM_ASSERT_TRUE(accepted != nullptr);
  SM_EXPECT_TRUE(client->GetState() == AsyncPacketSocket::State::kConnected);
  SM_EXPECT_TRUE(accepted->GetRemoteAddress() == p2p::SocketAddress(a.ip(), client->GetLocalAddress().port()));
  // Sending to anyone but the peer: ENOTCONN.
  SM_EXPECT_EQ(client->SendTo(now, Bytes("x"), p2p::SocketAddress(b.ip(), 1), {}), -1);
  SM_EXPECT_EQ(client->GetError(), p2p::kSocketENotConn);

  p2p::PacketOptions opts;
  opts.packet_id = 7;
  SM_EXPECT_EQ(client->Send(now, Bytes("hello"), opts), 5);
  SM_EXPECT_EQ(client->SendTo(now, Bytes(""), listen->GetLocalAddress(), {}), 0);  // An empty packet frames too.
  SM_EXPECT_EQ(client->Send(now, Packet(3000, 0x5a), {}), 3000);
  SM_ASSERT_EQ(sent.size(), 3u);
  SM_EXPECT_EQ(sent[0].packet_id, 7);
  SM_EXPECT_EQ(sent[0].packet_size_bytes, 5u);
  SM_EXPECT_EQ(sent[0].ip_overhead_bytes, 20u);
  SM_EXPECT_EQ(sent[0].send_time.ms(), now.ms());
  now = Run({&a, &b});
  SM_ASSERT_EQ(at_b.size(), 3u);
  SM_EXPECT_TRUE(at_b[0] == Bytes("hello"));
  SM_EXPECT_TRUE(at_b[1].empty());
  SM_EXPECT_TRUE(at_b[2] == Packet(3000, 0x5a));
  SM_EXPECT_TRUE(b_from[0] == accepted->GetRemoteAddress());
  SM_EXPECT_EQ(accepted->Send(now, Bytes("world"), {}), 5);
  Run({&a, &b});
  SM_ASSERT_EQ(at_a.size(), 1u);
  SM_EXPECT_TRUE(at_a[0] == Bytes("world"));
  // Too large for the 2-byte length plus 64 KiB: EMSGSIZE.
  SM_EXPECT_EQ(client->Send(now, Packet(p2p::AsyncTcpSocket::kBufSize + 1), {}), -1);
  SM_EXPECT_EQ(client->GetError(), p2p::kSocketEMsgSize);
}

SM_TEST(AsyncTcpSocket, FactoryRefusesTlsAndStunListeners) {
  sim::ByteNetwork net;
  sim::EmulatedSocketServer a(net, net.AddHost(kClientIp));
  const p2p::SocketAddress any(IpAddress(0u), 0);
  SM_EXPECT_TRUE(a.CreateServerTcpSocket(any, 0, 0, p2p::PacketSocketFactory::kOptTls) == nullptr);
  SM_EXPECT_TRUE(a.CreateServerTcpSocket(any, 0, 0, p2p::PacketSocketFactory::kOptTlsFake) == nullptr);
  p2p::PacketSocketTcpOptions tls;
  tls.opts = p2p::PacketSocketFactory::kOptTls;
  SM_EXPECT_TRUE(a.CreateClientTcpSocket(Timestamp::Micros(0), any, p2p::SocketAddress(IpAddress(kServerIp), 443), tls) ==
                 nullptr);
  // A port range, as BindSocket.
  auto l1 = a.CreateServerTcpSocket(any, 7000, 7001, 0);
  auto l2 = a.CreateServerTcpSocket(any, 7000, 7001, 0);
  SM_ASSERT_TRUE(l1 && l2);
  SM_EXPECT_EQ(l1->GetLocalAddress().port(), 7000);
  SM_EXPECT_EQ(l2->GetLocalAddress().port(), 7001);
  SM_EXPECT_TRUE(a.CreateServerTcpSocket(any, 7000, 7001, 0) == nullptr);
  // A non-any local address that can't bind fails the client socket.
  SM_EXPECT_TRUE(a.CreateClientTcpSocket(Timestamp::Micros(0), p2p::SocketAddress(a.ip(), 7000),
                                         p2p::SocketAddress(IpAddress(kServerIp), 80), {}) == nullptr);
}

// --- p2p/base/async_stun_tcp_socket_unittest.cc ---

SM_TEST(AsyncStunTcpSocket, TestSingleStunPacket) {
  StunFixture f;
  SM_EXPECT_TRUE(f.Send(kStunMessageWithZeroLength));
  SM_ASSERT_EQ(f.recv_packets.size(), 1u);
  SM_EXPECT_TRUE(Equal(f.recv_packets.front(), kStunMessageWithZeroLength));
}

SM_TEST(AsyncStunTcpSocket, TestMultipleStunPackets) {
  StunFixture f;
  for (int i = 0; i < 4; ++i) SM_EXPECT_TRUE(f.Send(kStunMessageWithZeroLength));
  SM_EXPECT_EQ(f.recv_packets.size(), 4u);
}

SM_TEST(AsyncStunTcpSocket, ProcessInputHandlesMultiplePackets) {
  StunFixture f;
  std::vector<Packet> got;
  f.send_socket->RegisterReceivedPacketCallback(
      [&](AsyncPacketSocket*, const ReceivedPacket& p) { got.emplace_back(p.payload.begin(), p.payload.end()); });
  // A ChannelData message MUST be padded to a multiple of four bytes.
  constexpr auto kTurnChannelData = std::to_array<uint8_t>({0x40, 0x00, 0x00, 0x04, 0x21, 0x12, 0xA4, 0x42});
  Packet buffer(kStunMessageWithZeroLength.begin(), kStunMessageWithZeroLength.end());
  buffer.insert(buffer.end(), kTurnChannelData.begin(), kTurnChannelData.end());
  f.send_socket->ProcessInput(f.now, buffer);
  SM_ASSERT_EQ(got.size(), 2u);
  SM_EXPECT_TRUE(Equal(got[0], kStunMessageWithZeroLength));
  SM_EXPECT_TRUE(Equal(got[1], kTurnChannelData));
}

SM_TEST(AsyncStunTcpSocket, TestTurnChannelDataWithZeroLength) {
  StunFixture f;
  SM_EXPECT_TRUE(f.Send(kTurnChannelDataMessageWithZeroLength));
  SM_ASSERT_EQ(f.recv_packets.size(), 1u);
  SM_EXPECT_TRUE(Equal(f.recv_packets.front(), kTurnChannelDataMessageWithZeroLength));
}

SM_TEST(AsyncStunTcpSocket, TestTurnChannelData) {
  StunFixture f;
  SM_EXPECT_TRUE(f.Send(kTurnChannelDataMessage));
  SM_ASSERT_EQ(f.recv_packets.size(), 1u);
  SM_EXPECT_TRUE(Equal(f.recv_packets.front(), kTurnChannelDataMessage));
}

SM_TEST(AsyncStunTcpSocket, TestTurnChannelDataPadding) {
  StunFixture f;
  SM_EXPECT_TRUE(f.Send(kTurnChannelDataMessageWithOddLength));
  SM_ASSERT_EQ(f.recv_packets.size(), 1u);
  SM_EXPECT_TRUE(Equal(f.recv_packets.front(), kTurnChannelDataMessageWithOddLength));
}

SM_TEST(AsyncStunTcpSocket, TestStunInvalidLength) {
  StunFixture f;
  auto msg = kStunMessageWithInvalidLength;
  SM_EXPECT_FALSE(f.Send(msg));
  SM_EXPECT_TRUE(f.recv_packets.empty());
  msg[2] = 0xFF;  // A larger length.
  msg[3] = 0xFF;
  SM_EXPECT_FALSE(f.Send(msg));
  msg[2] = 0x00;  // A smaller one.
  msg[3] = 0x01;
  SM_EXPECT_FALSE(f.Send(msg));
}

SM_TEST(AsyncStunTcpSocket, TestTurnChannelDataWithInvalidLength) {
  StunFixture f;
  auto msg = kTurnChannelDataMessageWithInvalidLength;
  SM_EXPECT_FALSE(f.Send(msg));
  msg[2] = 0xFF;
  msg[3] = 0xF0;
  SM_EXPECT_FALSE(f.Send(msg));
  msg[2] = 0x00;
  msg[3] = 0x00;
  SM_EXPECT_FALSE(f.Send(msg));
}

SM_TEST(AsyncStunTcpSocket, TestTooSmallMessageBuffer) {
  StunFixture f;
  const auto data = std::to_array<uint8_t>({0});
  SM_EXPECT_FALSE(f.Send(data));
}

SM_TEST(AsyncStunTcpSocket, TestMaximumSizeTurnPacket) {
  StunFixture f;
  Packet packet(kMaxTurnPacketSize, 0);
  packet[0] = kTurnChannelDataMarker;
  packet[1] = 0x00;
  packet[2] = 0xFF;
  packet[3] = 0xFF;
  SM_EXPECT_TRUE(f.Send(packet));
  // More than the send buffer and the window together: the rest went on
  // write events, and the whole message arrived.
  SM_ASSERT_EQ(f.recv_packets.size(), 1u);
  SM_EXPECT_TRUE(f.recv_packets[0] == packet);
}

SM_TEST(AsyncStunTcpSocket, TestMaximumSizeStunPacket) {
  StunFixture f;
  Packet packet(kMaxStunPacketSize, 0);
  packet[0] = 0x00;
  packet[1] = 0x01;
  packet[2] = 0xFF;
  packet[3] = 0xFC;
  SM_EXPECT_TRUE(f.Send(packet));
  SM_ASSERT_EQ(f.recv_packets.size(), 1u);
  SM_EXPECT_TRUE(f.recv_packets[0] == packet);
}

// A message is sent whole even when it is larger than the send buffer.
SM_TEST(AsyncStunTcpSocket, TestWithSmallSendBuffer) {
  StunFixture f;
  f.client_host.set_send_buffer_capacity(1);
  f.Send(kTurnChannelDataMessageWithOddLength);
  SM_ASSERT_EQ(f.recv_packets.size(), 1u);
  SM_EXPECT_TRUE(Equal(f.recv_packets.front(), kTurnChannelDataMessageWithOddLength));
}

SM_TEST(AsyncStunTcpSocket, SignalSentPacketFiredWhenPacketSent) {
  StunFixture f;
  SM_ASSERT_TRUE(f.Send(kStunMessageWithZeroLength));
  SM_EXPECT_EQ(f.sent_packets, 1);
  SM_ASSERT_TRUE(f.Send(kStunMessageWithZeroLength));
  SM_EXPECT_EQ(f.sent_packets, 2);
}

SM_TEST(AsyncStunTcpSocket, SignalSentPacketNotFiredWhenPacketNotSent) {
  StunFixture f;
  const auto data = std::to_array<uint8_t>({0});
  SM_ASSERT_FALSE(f.Send(data));
  SM_EXPECT_EQ(f.sent_packets, 0);
}

// --- The stream model ---

namespace {

// Two raw stream sockets connected through a listener.
struct RawPair {
  sim::ByteNetwork net;
  sim::EmulatedSocketServer a;
  sim::EmulatedSocketServer b;
  std::unique_ptr<p2p::Socket> listener;
  std::unique_ptr<p2p::Socket> client;
  std::unique_ptr<p2p::Socket> server;
  Timestamp now = Timestamp::Micros(0);

  explicit RawPair(int delay_ms = 10, double loss = 0)
      : a(net, net.AddHost(kClientIp, 0, Link(delay_ms, loss), Link(delay_ms, loss))),
        b(net, net.AddHost(kServerIp, 0, Link(delay_ms, loss), Link(delay_ms, loss))) {
    listener = b.CreateStreamSocket(p2p::IpFamily::kV4);
    listener->Bind(p2p::SocketAddress(b.ip(), 80));
    listener->Listen(5);
    listener->SubscribeReadEvent(nullptr, [this](p2p::Socket* l, Timestamp t) {
      p2p::SocketAddress from;
      server = l->Accept(t, &from);
    });
    client = a.CreateStreamSocket(p2p::IpFamily::kV4);
    client->Connect(now, p2p::SocketAddress(b.ip(), 80));
    now = Run({&a, &b});
  }
  Timestamp Step() { return now = Run({&a, &b}); }
};

std::string Drain(p2p::Socket& s, Timestamp now) {
  std::string out;
  uint8_t buf[4096];
  int n;
  while ((n = s.Recv(now, buf)) > 0) out.append(reinterpret_cast<char*>(buf), static_cast<size_t>(n));
  return out;
}

}  // namespace

SM_TEST(EmulatedTcp, ConnectAcceptAndMss) {
  RawPair p;
  SM_ASSERT_TRUE(p.server != nullptr);
  SM_EXPECT_TRUE(p.client->GetState() == p2p::Socket::ConnState::kConnected);
  SM_EXPECT_TRUE(p.server->GetState() == p2p::Socket::ConnState::kConnected);
  SM_EXPECT_TRUE(p.listener->GetState() == p2p::Socket::ConnState::kConnecting);  // Listening, as CS_CONNECTING.
  // The accepted socket has the listener's address; the client an ephemeral port.
  SM_EXPECT_TRUE(p.server->GetLocalAddress() == p2p::SocketAddress(p.b.ip(), 80));
  SM_EXPECT_EQ(p.client->GetLocalAddress().port(), sim::EmulatedSocketServer::kFirstEphemeralPort);
  SM_EXPECT_TRUE(p.server->GetRemoteAddress() == p2p::SocketAddress(p.a.ip(), p.client->GetLocalAddress().port()));
  // A second connect: EISCONN.
  SM_EXPECT_EQ(p.client->Connect(p.now, p2p::SocketAddress(p.b.ip(), 80)), -1);
  SM_EXPECT_EQ(p.client->GetError(), p2p::kSocketEIsConn);

  // 3000 bytes go as segments of at most TCP_MSS - TCP_HEADER_SIZE = 1360,
  // each charged 40 header bytes, each acknowledged.
  const uint64_t sent0 = p.net.stats().sent;
  const int64_t bytes0 = p.net.uplink(0).stats().of(sim::LinkKind::kOther).bytes_sent;
  std::string data(3000, 'z');
  SM_EXPECT_EQ(p.client->Send(p.now, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(data.data()), 3000)),
               3000);
  p.Step();
  SM_EXPECT_EQ(p.net.stats().sent - sent0, 6u);  // 1360 + 1360 + 280, and three ACKs.
  SM_EXPECT_EQ(p.net.uplink(0).stats().of(sim::LinkKind::kOther).bytes_sent - bytes0, 3000 + 3 * 40);
  SM_EXPECT_EQ(Drain(*p.server, p.now), data);
  SM_EXPECT_EQ(p.server->Recv(p.now, std::span<uint8_t>()), -1);
  SM_EXPECT_EQ(p.server->GetError(), p2p::kSocketEWouldBlock);
}

SM_TEST(EmulatedTcp, ConnectionRefusedWithoutListener) {
  sim::ByteNetwork net;
  sim::EmulatedSocketServer a(net, net.AddHost(kClientIp, 0, Link(10), Link(10)));
  sim::EmulatedSocketServer b(net, net.AddHost(kServerIp, 0, Link(10), Link(10)));
  auto udp = b.CreateUdpSocket(p2p::SocketAddress(b.ip(), 5000), 0, 0);  // A UDP port is no listener.
  for (uint16_t port : {uint16_t{4000}, uint16_t{5000}}) {
    auto c = a.CreateStreamSocket(p2p::IpFamily::kV4);
    int closed_with = -1;
    c->SubscribeCloseEvent(nullptr, [&](p2p::Socket*, int e) { closed_with = e; });
    SM_EXPECT_EQ(c->Connect(Timestamp::Micros(0), p2p::SocketAddress(b.ip(), port)), 0);
    SM_EXPECT_TRUE(c->GetState() == p2p::Socket::ConnState::kConnecting);
    Run({&a, &b});
    SM_EXPECT_EQ(closed_with, p2p::kSocketEConnRefused);
    SM_EXPECT_TRUE(c->GetState() == p2p::Socket::ConnState::kClosed);
  }
  SM_EXPECT_EQ(b.tcp_resets_sent(), 2u);
}

SM_TEST(EmulatedTcp, TcpAndUdpShareThePortTable) {
  sim::ByteNetwork net;
  sim::EmulatedSocketServer a(net, net.AddHost(kClientIp));
  auto udp = a.CreateUdpSocket(p2p::SocketAddress(a.ip(), 6000), 0, 0);
  auto tcp = a.CreateStreamSocket(p2p::IpFamily::kV4);
  // VirtualSocketServer's one bindings map: EADDRINUSE.
  SM_EXPECT_EQ(tcp->Bind(p2p::SocketAddress(a.ip(), 6000)), -1);
  SM_EXPECT_EQ(tcp->GetError(), p2p::kSocketEAddrInUse);
  SM_EXPECT_EQ(tcp->Bind(p2p::SocketAddress(a.ip(), 6001)), 0);
  SM_EXPECT_TRUE(a.CreateUdpSocket(p2p::SocketAddress(a.ip(), 6001), 0, 0) == nullptr);
  SM_EXPECT_EQ(a.last_error(), p2p::kSocketEAddrInUse);
  // Listen needs a bound socket.
  auto unbound = a.CreateStreamSocket(p2p::IpFamily::kV4);
  SM_EXPECT_EQ(unbound->Listen(5), -1);
  SM_EXPECT_EQ(unbound->GetError(), p2p::kSocketEInval);
  // Closing frees the port.
  tcp->Close();
  SM_EXPECT_TRUE(a.CreateUdpSocket(p2p::SocketAddress(a.ip(), 6001), 0, 0) != nullptr);
  SM_EXPECT_TRUE(a.CreateStreamSocket(p2p::IpFamily::kV6) == nullptr);  // IPv4 only.
}

// The send buffer and the receiver's window bound what a sender can queue;
// reading reopens the window, and a write event tells the sender.
SM_TEST(EmulatedTcp, FlowControlAndWriteEvent) {
  RawPair p;
  int writes = 0;
  p.client->SubscribeWriteEvent(nullptr, [&](p2p::Socket*, Timestamp) { ++writes; });
  const std::vector<uint8_t> chunk(8192, 0x11);
  size_t queued = 0;
  for (;;) {
    const int n = p.client->Send(p.now, chunk);
    if (n < 0) break;
    queued += static_cast<size_t>(n);
    p.Step();
  }
  SM_EXPECT_EQ(p.client->GetError(), p2p::kSocketEWouldBlock);
  // 32 KiB in the receiver's buffer, 32 KiB in the sender's.
  SM_EXPECT_EQ(queued, 2 * sim::EmulatedSocketServer::kDefaultTcpBufferSize);
  auto* raw = static_cast<sim::EmulatedTcpSocket*>(p.server.get());
  SM_EXPECT_EQ(raw->unread_bytes(), sim::EmulatedSocketServer::kDefaultTcpBufferSize);
  SM_EXPECT_EQ(writes, 0);
  const std::string got = Drain(*p.server, p.now);
  SM_EXPECT_EQ(got.size(), sim::EmulatedSocketServer::kDefaultTcpBufferSize);
  p.Step();
  SM_EXPECT_EQ(writes, 1);  // The sender's buffer drained into the reopened window.
  SM_EXPECT_EQ(Drain(*p.server, p.now).size(), sim::EmulatedSocketServer::kDefaultTcpBufferSize);
}

// Close sends a FIN; the peer closes with no error. A segment for a closed
// connection gets a reset.
SM_TEST(EmulatedTcp, CloseAndReset) {
  RawPair p;
  int server_closed = -1;
  p.server->SubscribeCloseEvent(nullptr, [&](p2p::Socket*, int e) { server_closed = e; });
  p.client->Close();
  SM_EXPECT_TRUE(p.client->GetState() == p2p::Socket::ConnState::kClosed);
  p.Step();
  SM_EXPECT_EQ(server_closed, 0);
  SM_EXPECT_TRUE(p.server->GetState() == p2p::Socket::ConnState::kClosed);
  SM_EXPECT_EQ(p.server->Send(p.now, Bytes("late")), -1);
  SM_EXPECT_EQ(p.server->GetError(), p2p::kSocketENotConn);

  // Destroying a connected socket resets nothing on its own: it FINs, and
  // the peer's next data finds no connection and is reset.
  RawPair q;
  int client_closed = -1;
  q.client->SubscribeCloseEvent(nullptr, [&](p2p::Socket*, int e) { client_closed = e; });
  q.server->SubscribeCloseEvent(nullptr, [](p2p::Socket*, int) {});
  // Lose the FIN: an outage on the client's uplink while it closes... then
  // the server sends and is reset.
  sim::LinkConfig down = Link(10);
  down.down = true;
  q.net.uplink(1).SetConfig(down);
  q.server->Close();
  q.Step();
  q.net.uplink(1).SetConfig(Link(10));
  SM_EXPECT_EQ(client_closed, -1);  // The FIN never arrived.
  SM_EXPECT_EQ(q.client->Send(q.now, Bytes("anyone?")), 7);
  q.Step();
  SM_EXPECT_EQ(client_closed, p2p::kSocketEConnReset);
}

// rtc_base/test_echo_server.h through each of libwebrtc's test NAT types:
// the client connects out, the server sees the NAT's mapped address, and the
// echo comes back through the NAT's filter.
SM_TEST(EmulatedTcp, EchoFromBehindEachNatType) {
  for (sim::NatType type : {sim::NatType::kOpenCone, sim::NatType::kAddrRestricted, sim::NatType::kPortRestricted,
                            sim::NatType::kSymmetric}) {
    sim::ByteNetwork net;
    sim::NatConfig nat_cfg;
    nat_cfg.type = type;
    nat_cfg.external_ip = sim::SocketAddress::Ip(33, 33, 33, 33);
    const auto nat = net.AddNat(nat_cfg);
    sim::EmulatedSocketServer client_host(
        net, net.AddHost(sim::SocketAddress::Ip(192, 168, 1, 2), net.NatRealm(nat), Link(10), Link(10)));
    sim::EmulatedSocketServer echo_host(net, net.AddHost(kServerIp, 0, Link(10), Link(10)));

    // TestEchoServer: echoes every packet on the connection it came on.
    auto listen = echo_host.CreateServerTcpSocket(p2p::SocketAddress(echo_host.ip(), 7), 0, 0, 0);
    SM_ASSERT_TRUE(listen != nullptr);
    std::vector<std::unique_ptr<AsyncPacketSocket>> clients;
    std::vector<p2p::SocketAddress> seen_from;
    Timestamp now = Timestamp::Micros(0);
    listen->SubscribeNewConnection(nullptr, [&](p2p::AsyncListenSocket*, std::unique_ptr<AsyncPacketSocket>& s) {
      seen_from.push_back(s->GetRemoteAddress());
      s->RegisterReceivedPacketCallback([](AsyncPacketSocket* sock, const ReceivedPacket& pk) {
        sock->Send(pk.arrival_time, pk.payload, {});
      });
      clients.push_back(std::move(s));
    });

    auto client = client_host.CreateClientTcpSocket(now, p2p::SocketAddress(IpAddress(0u), 0),
                                                    listen->GetLocalAddress(), {});
    SM_ASSERT_TRUE(client != nullptr);
    std::vector<Packet> echoed;
    client->RegisterReceivedPacketCallback(
        [&](AsyncPacketSocket*, const ReceivedPacket& pk) { echoed.emplace_back(pk.payload.begin(), pk.payload.end()); });
    now = Run({&client_host, &echo_host});
    SM_ASSERT_TRUE(client->GetState() == AsyncPacketSocket::State::kConnected);
    SM_ASSERT_EQ(seen_from.size(), 1u);
    SM_EXPECT_TRUE(seen_from[0].ipaddr() == IpAddress(nat_cfg.external_ip));
    for (const char* msg : {"one", "two", "three"}) client->Send(now, Bytes(msg), {});
    Run({&client_host, &echo_host});
    SM_ASSERT_EQ(echoed.size(), 3u);
    SM_EXPECT_TRUE(echoed[2] == Bytes("three"));
  }
}

// A connection into a NAT from outside finds no binding: the SYN is retried
// with RFC 6298's doubling timer (1, 2, ... 64 s) and the connect times out.
SM_TEST(EmulatedTcp, ConnectIntoANatTimesOut) {
  sim::ByteNetwork net;
  sim::NatConfig nat_cfg;
  nat_cfg.type = sim::NatType::kOpenCone;
  nat_cfg.external_ip = sim::SocketAddress::Ip(33, 33, 33, 33);
  const auto nat = net.AddNat(nat_cfg);
  sim::EmulatedSocketServer inside(net, net.AddHost(sim::SocketAddress::Ip(192, 168, 1, 2), net.NatRealm(nat)));
  sim::EmulatedSocketServer outside(net, net.AddHost(kClientIp));
  auto listen = inside.CreateServerTcpSocket(p2p::SocketAddress(inside.ip(), 80), 0, 0, 0);
  auto c = outside.CreateStreamSocket(p2p::IpFamily::kV4);
  int closed_with = -1;
  Timestamp closed_at = Timestamp::MinusInfinity();
  Timestamp now = Timestamp::Micros(0);
  c->SubscribeCloseEvent(nullptr, [&](p2p::Socket*, int e) { closed_with = e; });
  c->Connect(now, p2p::SocketAddress(IpAddress(nat_cfg.external_ip), 80));
  closed_at = Run({&inside, &outside});
  SM_EXPECT_EQ(closed_with, p2p::kSocketETimedOut);
  SM_EXPECT_EQ(closed_at.ms(), (1 + 2 + 4 + 8 + 16 + 32 + 60) * 1000);  // The 7th wait is capped at 60 s.
  SM_EXPECT_EQ(net.stats().nat_no_binding, 7u);
}

// The emulated links lose segments, which the virtual server's TCP never
// does: the stream still delivers every byte once, in order.
SM_TEST(EmulatedTcp, DeliversInOrderOverLossyLinks) {
  for (uint64_t seed : {1u, 2u, 3u}) {
    sim::ByteNetwork net(seed);
    sim::EmulatedSocketServer a(net, net.AddHost(kClientIp, 0, Link(20, 0.1), Link(20, 0.1)));
    sim::EmulatedSocketServer b(net, net.AddHost(kServerIp, 0, Link(20, 0.1), Link(20, 0.1)));
    auto listener = b.CreateStreamSocket(p2p::IpFamily::kV4);
    listener->Bind(p2p::SocketAddress(b.ip(), 80));
    listener->Listen(5);
    std::unique_ptr<p2p::Socket> server;
    std::string got;
    listener->SubscribeReadEvent(nullptr, [&](p2p::Socket* l, Timestamp t) {
      server = l->Accept(t, nullptr);
      server->SubscribeReadEvent(nullptr, [&](p2p::Socket* s, Timestamp t2) { got += Drain(*s, t2); });
    });
    auto client = a.CreateStreamSocket(p2p::IpFamily::kV4);
    client->Connect(Timestamp::Micros(0), p2p::SocketAddress(b.ip(), 80));
    // 200 KB, written whenever the socket takes more.
    std::string data(200000, 0);
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<char>(i * 7 + i / 251);
    size_t written = 0;
    auto write_more = [&](Timestamp t) {
      while (written < data.size()) {
        const int n = client->Send(
            t, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(data.data()) + written, data.size() - written));
        if (n <= 0) break;
        written += static_cast<size_t>(n);
      }
    };
    client->SubscribeConnectEvent(nullptr, [&](p2p::Socket*, Timestamp t) { write_more(t); });
    client->SubscribeWriteEvent(nullptr, [&](p2p::Socket*, Timestamp t) { write_more(t); });
    const Timestamp end = Run({&a, &b});
    SM_EXPECT_EQ(written, data.size());
    SM_EXPECT_EQ(got.size(), data.size());
    SM_EXPECT_TRUE(got == data);
    SM_EXPECT_TRUE(client->GetState() == p2p::Socket::ConnState::kConnected);
    SM_EXPECT_TRUE(end < Timestamp::Seconds(300));
    // Segments and ACKs were lost on the way, and the stream recovered.
    SM_EXPECT_TRUE(net.uplink(0).stats().of(sim::LinkKind::kOther).lost > 20);
    SM_EXPECT_TRUE(net.uplink(1).stats().of(sim::LinkKind::kOther).lost > 5);
  }
}
