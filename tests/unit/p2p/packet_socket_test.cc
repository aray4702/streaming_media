// IP and socket addresses, the packet socket interface, and UDP sockets over
// the emulated network (issue #996). The address cases follow libwebrtc's
// rtc_base/ip_address_unittest.cc and socket_address_unittest.cc; the socket
// cases follow VirtualSocketServer's binding rules and AsyncUDPSocket's sent
// notifications; the NAT cases check that a binding through each of
// libwebrtc's test NAT types gives the mapped address it should.
#include <array>
#include <memory>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/async_packet_socket.h"
#include "sm/p2p/socket_address.h"
#include "sm/sim/emulated_sockets.h"
#include "sm_test.h"

using namespace sm;
using p2p::IpAddress;
using p2p::IpFamily;

namespace {

IpAddress Ip(const char* s) { return *IpAddress::FromString(s); }

}  // namespace

SM_TEST(P2pIpAddress, ParsesAndPrintsV4) {
  const IpAddress a = Ip("1.2.3.4");
  SM_EXPECT_TRUE(a.family() == IpFamily::kV4);
  SM_EXPECT_EQ(a.v4(), 0x01020304u);
  SM_EXPECT_EQ(a.ToString(), std::string("1.2.3.4"));
  SM_EXPECT_EQ(a.ToSensitiveString(), std::string("1.2.3.x"));
  SM_EXPECT_EQ(a.Size(), 4u);
  SM_EXPECT_EQ(a.overhead(), 20);
  SM_EXPECT_TRUE(IpAddress::V4(1, 2, 3, 4) == a);
  for (const char* bad : {"", "1.2.3", "1.2.3.4.5", "256.1.1.1", "1.2.3.a", "1..2.3", "1.2.3.4 "}) {
    SM_EXPECT_FALSE(IpAddress::FromString(bad).has_value());
  }
}

SM_TEST(P2pIpAddress, ParsesAndPrintsV6) {
  // inet_ntop's forms (ip_address_unittest.cc's kIPv6* strings).
  const char* canonical[] = {"::", "::1", "fe80::be30:5bff:fee5:c3", "2620:0:1008:1201:2ad1:1b2e:c3f2:1", "1::",
                             "1:0:2::3", "::ffff:1.2.3.4", "2001:db8::1:0:0:1"};
  for (const char* s : canonical) {
    auto ip = IpAddress::FromString(s);
    SM_ASSERT_TRUE(ip.has_value());
    SM_EXPECT_TRUE(ip->family() == IpFamily::kV6);
    SM_EXPECT_EQ(ip->ToString(), std::string(s));
  }
  // Non-canonical input prints canonically: lower case, longest zero run.
  SM_EXPECT_EQ(Ip("2001:DB8:0:0:1:0:0:1").ToString(), std::string("2001:db8::1:0:0:1"));
  SM_EXPECT_EQ(Ip("0:0:0:0:0:0:0:1").ToString(), std::string("::1"));
  SM_EXPECT_EQ(Ip("2620:0:1008:1201:2ad1:1b2e:c3f2:1").ToSensitiveString(), std::string("2620:0:1008:x:x:x:x:x"));
  for (const char* bad : {":::", "1:2:3:4:5:6:7:8:9", "1::2::3", "12345::", "g::", "1:2:3:4:5:6:7"}) {
    SM_EXPECT_FALSE(IpAddress::FromString(bad).has_value());
  }
}

SM_TEST(P2pIpAddress, Classes) {
  SM_EXPECT_TRUE(p2p::IpIsAny(Ip("0.0.0.0")));
  SM_EXPECT_TRUE(p2p::IpIsAny(Ip("::")));
  SM_EXPECT_TRUE(p2p::IpIsAny(Ip("::ffff:0.0.0.0")));
  SM_EXPECT_FALSE(p2p::IpIsAny(IpAddress()));
  SM_EXPECT_TRUE(p2p::IpIsLoopback(Ip("127.0.0.1")));
  SM_EXPECT_TRUE(p2p::IpIsLoopback(Ip("127.255.0.9")));
  SM_EXPECT_TRUE(p2p::IpIsLoopback(Ip("::1")));
  SM_EXPECT_TRUE(p2p::IpIsLoopback(Ip("::ffff:127.0.0.1")));
  SM_EXPECT_FALSE(p2p::IpIsLoopback(Ip("::2")));
  SM_EXPECT_TRUE(p2p::IpIsLinkLocal(Ip("169.254.10.1")));
  SM_EXPECT_TRUE(p2p::IpIsLinkLocal(Ip("fe80::1")));
  SM_EXPECT_FALSE(p2p::IpIsLinkLocal(Ip("fec0::1")));
  for (const char* s : {"10.0.0.1", "172.16.0.1", "172.31.255.255", "192.168.1.1", "fd00::1"}) {
    SM_EXPECT_TRUE(p2p::IpIsPrivateNetwork(Ip(s)));
  }
  for (const char* s : {"172.32.0.1", "11.0.0.1", "192.169.0.1", "fc00::1", "8.8.8.8"}) {
    SM_EXPECT_FALSE(p2p::IpIsPrivateNetwork(Ip(s)));
  }
  SM_EXPECT_TRUE(p2p::IpIsSharedNetwork(Ip("100.64.0.1")));
  SM_EXPECT_TRUE(p2p::IpIsSharedNetwork(Ip("100.127.255.255")));
  SM_EXPECT_FALSE(p2p::IpIsSharedNetwork(Ip("100.128.0.1")));
  // IPIsPrivate: link-local, loopback, private or shared.
  for (const char* s : {"169.254.0.1", "127.0.0.1", "10.1.1.1", "100.64.1.1", "::1", "fe80::1"}) {
    SM_EXPECT_TRUE(p2p::IpIsPrivate(Ip(s)));
  }
  SM_EXPECT_FALSE(p2p::IpIsPrivate(Ip("1.2.3.4")));
  SM_EXPECT_FALSE(p2p::IpIsPrivate(Ip("2620::1")));
}

SM_TEST(P2pIpAddress, Normalizing) {
  SM_EXPECT_TRUE(Ip("::ffff:1.2.3.4").Normalized() == Ip("1.2.3.4"));
  SM_EXPECT_TRUE(Ip("::1.2.3.4").Normalized() == Ip("::1.2.3.4"));  // Normalized() takes only mapped.
  const IpAddress n = Ip("::1.2.3.4").NormalizeWithCheckForEmbeddedIPv4Address();
  SM_EXPECT_TRUE(n == Ip("1.2.3.4"));
  SM_EXPECT_TRUE(Ip("::1").NormalizeWithCheckForEmbeddedIPv4Address() == Ip("::1"));
  SM_EXPECT_TRUE(Ip("64:ff9b::1.2.3.4").NormalizeWithCheckForEmbeddedIPv4Address() == Ip("1.2.3.4"));
  // Teredo carries the client's address inverted.
  SM_EXPECT_TRUE(Ip("2001:0:1:2:3:4:fefd:fcfb").NormalizeWithCheckForEmbeddedIPv4Address() == Ip("1.2.3.4"));
  // libwebrtc leaves 6to4 alone.
  SM_EXPECT_TRUE(Ip("2002:102:304::1").NormalizeWithCheckForEmbeddedIPv4Address() == Ip("2002:102:304::1"));
  SM_EXPECT_TRUE(Ip("1.2.3.4").AsIPv6Address() == Ip("::ffff:1.2.3.4"));
}

SM_TEST(P2pIpAddress, Ordering) {
  // IPv4 sorts before IPv6, unspecified before both.
  SM_EXPECT_TRUE(IpAddress() < Ip("0.0.0.0"));
  SM_EXPECT_TRUE(Ip("255.255.255.255") < Ip("::"));
  SM_EXPECT_FALSE(Ip("::") < Ip("1.1.1.1"));
  SM_EXPECT_TRUE(Ip("1.2.3.4") < Ip("1.2.3.5"));
  SM_EXPECT_TRUE(Ip("::1") < Ip("::2"));
  SM_EXPECT_TRUE(Ip("1.2.3.4") != Ip("::ffff:1.2.3.4"));
}

SM_TEST(P2pSocketAddress, ParsePrintCompare) {
  const p2p::SocketAddress a = *p2p::SocketAddress::FromString("1.2.3.4:5678");
  SM_EXPECT_EQ(a.ToString(), std::string("1.2.3.4:5678"));
  SM_EXPECT_EQ(a.ToSensitiveString(), std::string("1.2.3.x:5678"));
  SM_EXPECT_EQ(a.port(), 5678);
  SM_EXPECT_EQ(a.ip(), 0x01020304u);
  SM_EXPECT_TRUE(a.IsComplete());
  const p2p::SocketAddress b = *p2p::SocketAddress::FromString("[2620:0:1008:1201:2ad1:1b2e:c3f2:1]:5678");
  SM_EXPECT_EQ(b.ToString(), std::string("[2620:0:1008:1201:2ad1:1b2e:c3f2:1]:5678"));
  SM_EXPECT_TRUE(b.family() == IpFamily::kV6);
  SM_EXPECT_EQ(b.ip(), 0u);
  for (const char* bad : {"1.2.3.4", "1.2.3.4:", "1.2.3.4:65536", "[::1]5678", "[::1:5678", "host:80"}) {
    SM_EXPECT_FALSE(p2p::SocketAddress::FromString(bad).has_value());
  }
  SM_EXPECT_TRUE(p2p::SocketAddress().IsNil());
  SM_EXPECT_FALSE(p2p::SocketAddress(Ip("0.0.0.0"), 80).IsComplete());
  SM_EXPECT_FALSE(p2p::SocketAddress(Ip("1.2.3.4"), 0).IsComplete());
  // By IP, then port.
  SM_EXPECT_TRUE(p2p::SocketAddress(Ip("1.2.3.4"), 9) < p2p::SocketAddress(Ip("1.2.3.5"), 1));
  SM_EXPECT_TRUE(p2p::SocketAddress(Ip("1.2.3.4"), 1) < p2p::SocketAddress(Ip("1.2.3.4"), 2));
  SM_EXPECT_TRUE(a.EqualIps(p2p::SocketAddress(Ip("1.2.3.4"), 1)));
  SM_EXPECT_FALSE(a.EqualPorts(p2p::SocketAddress(Ip("1.2.3.4"), 1)));
}

namespace {

constexpr uint32_t kHostA = sim::SocketAddress::Ip(1, 1, 1, 1);
constexpr uint32_t kHostB = sim::SocketAddress::Ip(2, 2, 2, 2);

sim::LinkConfig Delay(int ms) {
  sim::LinkConfig c;
  c.base_delay = TimeDelta::Millis(ms);
  return c;
}

struct Received {
  std::string payload;
  p2p::SocketAddress from;
  Timestamp at;
};

// Records what a socket receives.
void Collect(p2p::AsyncPacketSocket& s, std::vector<Received>& out) {
  s.RegisterReceivedPacketCallback([&out](p2p::AsyncPacketSocket*, const p2p::ReceivedPacket& p) {
    out.push_back({std::string(p.payload.begin(), p.payload.end()), p.source_address, p.arrival_time});
  });
}

std::span<const uint8_t> Span(const std::string& s) {
  return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

// Runs every server's delivery at each network event up to `until`.
void Run(std::vector<sim::EmulatedSocketServer*> servers, Timestamp until) {
  for (;;) {
    const Timestamp t = servers[0]->NextEvent();
    if (t > until) break;
    for (auto* s : servers) s->Deliver(t);
  }
}

}  // namespace

SM_TEST(EmulatedSockets, BindsEphemeralPortsInOrder) {
  sim::ByteNetwork net;
  const auto h = net.AddHost(kHostA);
  sim::EmulatedSocketServer server(net, h);
  const p2p::SocketAddress any(IpAddress(0u), 0);
  auto s1 = server.CreateUdpSocket(any, 0, 0);
  auto s2 = server.CreateUdpSocket(p2p::SocketAddress(server.ip(), 0), 0, 0);
  SM_ASSERT_TRUE(s1 && s2);
  // VirtualSocketServer: ephemeral ports from 49152, in order; any stays any.
  SM_EXPECT_EQ(s1->GetLocalAddress().port(), 49152);
  SM_EXPECT_TRUE(s1->GetLocalAddress().IsAnyIp());
  SM_EXPECT_EQ(s2->GetLocalAddress().port(), 49153);
  SM_EXPECT_TRUE(s2->GetLocalAddress().ipaddr() == server.ip());
  SM_EXPECT_TRUE(s1->GetState() == p2p::AsyncPacketSocket::State::kBound);
  SM_EXPECT_TRUE(s1->GetRemoteAddress().IsNil());
  // An explicit port.
  auto s3 = server.CreateUdpSocket(p2p::SocketAddress(server.ip(), 5000), 0, 0);
  SM_ASSERT_TRUE(s3 != nullptr);
  SM_EXPECT_EQ(s3->GetLocalAddress().port(), 5000);
  // In use: EADDRINUSE.
  SM_EXPECT_TRUE(server.CreateUdpSocket(p2p::SocketAddress(server.ip(), 5000), 0, 0) == nullptr);
  SM_EXPECT_EQ(server.last_error(), p2p::kSocketEAddrInUse);
  // Not this host's address: EADDRNOTAVAIL.
  SM_EXPECT_TRUE(server.CreateUdpSocket(p2p::SocketAddress(Ip("9.9.9.9"), 0), 0, 0) == nullptr);
  SM_EXPECT_EQ(server.last_error(), p2p::kSocketEAddrNotAvail);
  SM_EXPECT_EQ(server.bound_count(), 3u);
}

SM_TEST(EmulatedSockets, BindsTheFirstFreePortInARange) {
  sim::ByteNetwork net;
  sim::EmulatedSocketServer server(net, net.AddHost(kHostA));
  const p2p::SocketAddress addr(server.ip(), 0);
  // BasicPacketSocketFactory::BindSocket tries min..max in order.
  auto a = server.CreateUdpSocket(addr, 6000, 6002);
  auto b = server.CreateUdpSocket(addr, 6000, 6002);
  auto c = server.CreateUdpSocket(addr, 6000, 6002);
  SM_ASSERT_TRUE(a && b && c);
  SM_EXPECT_EQ(a->GetLocalAddress().port(), 6000);
  SM_EXPECT_EQ(b->GetLocalAddress().port(), 6001);
  SM_EXPECT_EQ(c->GetLocalAddress().port(), 6002);
  SM_EXPECT_TRUE(server.CreateUdpSocket(addr, 6000, 6002) == nullptr);
  // Closing frees the port for the next bind.
  b->Close();
  SM_EXPECT_TRUE(b->GetState() == p2p::AsyncPacketSocket::State::kClosed);
  auto d = server.CreateUdpSocket(addr, 6000, 6002);
  SM_ASSERT_TRUE(d != nullptr);
  SM_EXPECT_EQ(d->GetLocalAddress().port(), 6001);
  // Destroying a socket frees its port too.
  a.reset();
  SM_EXPECT_EQ(server.CreateUdpSocket(addr, 6000, 6000)->GetLocalAddress().port(), 6000);
}

SM_TEST(EmulatedSockets, SendsAndReceivesWithLinkDelay) {
  sim::ByteNetwork net;
  const auto ha = net.AddHost(kHostA, sim::ByteNetwork::kPublicRealm, Delay(10), Delay(5));
  const auto hb = net.AddHost(kHostB, sim::ByteNetwork::kPublicRealm, Delay(7), Delay(3));
  sim::EmulatedSocketServer a(net, ha), b(net, hb);
  auto sa = a.CreateUdpSocket(p2p::SocketAddress(IpAddress(0u), 0), 0, 0);  // Bound to any.
  auto sb = b.CreateUdpSocket(p2p::SocketAddress(b.ip(), 7000), 0, 0);
  std::vector<Received> at_a, at_b;
  Collect(*sa, at_a);
  Collect(*sb, at_b);
  std::vector<p2p::SentPacketInfo> sent;
  int tag = 0;
  sa->SubscribeSentPacket(&tag, [&sent](p2p::AsyncPacketSocket*, const p2p::SentPacketInfo& i) { sent.push_back(i); });

  p2p::PacketOptions opt;
  opt.packet_id = 42;
  SM_EXPECT_EQ(sa->SendTo(Timestamp::Millis(100), Span("hello"), sb->GetLocalAddress(), opt), 5);
  SM_ASSERT_EQ(sent.size(), 1u);
  SM_EXPECT_EQ(sent[0].packet_id, 42);
  SM_EXPECT_EQ(sent[0].send_time.ms(), 100);
  SM_EXPECT_EQ(sent[0].packet_size_bytes, 5u);
  SM_EXPECT_EQ(sent[0].ip_overhead_bytes, 20u);
  Run({&a, &b}, Timestamp::Seconds(1));
  SM_ASSERT_EQ(at_b.size(), 1u);
  SM_EXPECT_EQ(at_b[0].payload, std::string("hello"));
  // From a socket bound to any, the source is the host's address.
  SM_EXPECT_TRUE(at_b[0].from == p2p::SocketAddress(a.ip(), sa->GetLocalAddress().port()));
  // A's uplink (10 ms) and B's downlink (3 ms).
  SM_EXPECT_EQ(at_b[0].at.ms(), 113);

  // The reply goes back to the source address.
  sb->SendTo(Timestamp::Millis(200), Span("world"), at_b[0].from, {});
  Run({&a, &b}, Timestamp::Seconds(1));
  SM_ASSERT_EQ(at_a.size(), 1u);
  SM_EXPECT_EQ(at_a[0].payload, std::string("world"));
  SM_EXPECT_EQ(at_a[0].at.ms(), 212);
  SM_EXPECT_TRUE(at_a[0].from == sb->GetLocalAddress());

  // Unsubscribed: no more notifications.
  sa->UnsubscribeSentPacket(&tag);
  sa->SendTo(Timestamp::Millis(300), Span("x"), sb->GetLocalAddress(), {});
  SM_EXPECT_EQ(sent.size(), 1u);
}

SM_TEST(EmulatedSockets, AccessLinkSizeIncludesHeaders) {
  sim::ByteNetwork net;
  sim::LinkConfig slow = Delay(0);
  slow.capacity = DataRate::BitsPerSec(8000);  // 1 byte per ms.
  const auto ha = net.AddHost(kHostA, sim::ByteNetwork::kPublicRealm, slow, Delay(0));
  const auto hb = net.AddHost(kHostB, sim::ByteNetwork::kPublicRealm, Delay(0), Delay(0));
  sim::EmulatedSocketServer a(net, ha), b(net, hb);
  auto sa = a.CreateUdpSocket(p2p::SocketAddress(a.ip(), 0), 0, 0);
  auto sb = b.CreateUdpSocket(p2p::SocketAddress(b.ip(), 0), 0, 0);
  std::vector<Received> got;
  Collect(*sb, got);
  sa->SendTo(Timestamp::Millis(0), Span(std::string(72, 'x')), sb->GetLocalAddress(), {});
  Run({&a, &b}, Timestamp::Seconds(1));
  SM_ASSERT_EQ(got.size(), 1u);
  // 72 payload + 28 header bytes at 1 byte/ms.
  SM_EXPECT_EQ(got[0].at.ms(), 100);
}

SM_TEST(EmulatedSockets, ErrorsAndDrops) {
  sim::ByteNetwork net;
  const auto ha = net.AddHost(kHostA, sim::ByteNetwork::kPublicRealm, Delay(0), Delay(0));
  const auto hb = net.AddHost(kHostB, sim::ByteNetwork::kPublicRealm, Delay(0), Delay(0));
  sim::EmulatedSocketServer a(net, ha), b(net, hb);
  auto sa = a.CreateUdpSocket(p2p::SocketAddress(a.ip(), 0), 0, 0);
  // Unconnected Send: ENOTCONN.
  SM_EXPECT_EQ(sa->Send(Timestamp::Millis(0), Span("x"), {}), -1);
  SM_EXPECT_EQ(sa->GetError(), p2p::kSocketENotConn);
  // IPv6 destinations: the emulated network is IPv4 only.
  SM_EXPECT_EQ(sa->SendTo(Timestamp::Millis(0), Span("x"), p2p::SocketAddress(Ip("::1"), 9), {}), -1);
  SM_EXPECT_EQ(sa->GetError(), p2p::kSocketEInval);
  // Too large for one datagram.
  const std::string big(65508, 'x');
  SM_EXPECT_EQ(sa->SendTo(Timestamp::Millis(0), Span(big), p2p::SocketAddress(b.ip(), 9), {}), -1);
  SM_EXPECT_EQ(sa->GetError(), p2p::kSocketEMsgSize);
  // To a port nobody has bound: dropped at the host.
  SM_EXPECT_EQ(sa->SendTo(Timestamp::Millis(0), Span("x"), p2p::SocketAddress(b.ip(), 9), {}), 1);
  Run({&a, &b}, Timestamp::Seconds(1));
  SM_EXPECT_EQ(b.dropped_no_socket(), 1u);
  // Without a received-packet callback the socket drops what arrives.
  auto sb = b.CreateUdpSocket(p2p::SocketAddress(b.ip(), 9), 0, 0);
  sa->SendTo(Timestamp::Seconds(2), Span("x"), sb->GetLocalAddress(), {});
  Run({&a, &b}, Timestamp::Seconds(3));
  SM_EXPECT_EQ(b.dropped_no_socket(), 1u);
  // Closed: EBADF.
  sa->Close();
  SM_EXPECT_EQ(sa->SendTo(Timestamp::Seconds(4), Span("x"), sb->GetLocalAddress(), {}), -1);
  SM_EXPECT_EQ(sa->GetError(), p2p::kSocketEBadF);
  // Options are stored per socket.
  int v = 0;
  SM_EXPECT_EQ(sb->GetOption(p2p::SocketOption::kDscp, &v), -1);
  SM_EXPECT_EQ(sb->SetOption(p2p::SocketOption::kDscp, 46), 0);
  SM_EXPECT_EQ(sb->GetOption(p2p::SocketOption::kDscp, &v), 0);
  SM_EXPECT_EQ(v, 46);
}

namespace {

// A reflector on the internet: answers each datagram with the address it came
// from, as a STUN server's XOR-MAPPED-ADDRESS would (the STUN server itself
// comes with #997, after the STUN messages, #986).
struct Reflector {
  sim::EmulatedSocketServer server;
  std::unique_ptr<p2p::AsyncPacketSocket> socket;
  Reflector(sim::ByteNetwork& net, uint32_t ip, uint16_t port)
      : server(net, net.AddHost(ip, sim::ByteNetwork::kPublicRealm, Delay(5), Delay(5))) {
    socket = server.CreateUdpSocket(p2p::SocketAddress(server.ip(), port), 0, 0);
    socket->RegisterReceivedPacketCallback([](p2p::AsyncPacketSocket* s, const p2p::ReceivedPacket& p) {
      const std::string text = p.source_address.ToString();
      s->SendTo(p.arrival_time, Span(text), p.source_address, {});
    });
  }
  p2p::SocketAddress address() const { return socket->GetLocalAddress(); }
};

constexpr uint32_t kNatExternal = sim::SocketAddress::Ip(5, 5, 5, 5);

// What a host behind `type` learns from reflectors on two IPs and two ports,
// and whether an unsolicited datagram from each address gets in after it has
// talked to reflector 0 only.
struct Behind {
  std::vector<std::string> mapped;  // What reflectors 0, 1 and 2 saw.
  std::vector<bool> reachable_from;  // Address 1 (other IP), 2 (same IP, other port).
};

Behind ProbeThrough(sim::NatType type) {
  sim::ByteNetwork net;
  sim::NatConfig cfg;
  cfg.type = type;
  cfg.external_ip = kNatExternal;
  const auto nat = net.AddNat(cfg);
  sim::EmulatedSocketServer client(net, net.AddHost(sim::SocketAddress::Ip(192, 168, 1, 2), net.NatRealm(nat),
                                                    Delay(2), Delay(2)));
  Reflector r0(net, sim::SocketAddress::Ip(8, 8, 8, 8), 3478);
  Reflector r1(net, sim::SocketAddress::Ip(9, 9, 9, 9), 3478);
  // r2: r0's IP on another port, a second socket on r0's host.
  auto r2 = r0.server.CreateUdpSocket(p2p::SocketAddress(r0.server.ip(), 3479), 0, 0);
  r2->RegisterReceivedPacketCallback([](p2p::AsyncPacketSocket* s, const p2p::ReceivedPacket& p) {
    const std::string text = p.source_address.ToString();
    s->SendTo(p.arrival_time, Span(text), p.source_address, {});
  });
  std::vector<sim::EmulatedSocketServer*> all = {&client, &r0.server, &r1.server};

  auto sock = client.CreateUdpSocket(p2p::SocketAddress(client.ip(), 0), 0, 0);
  std::vector<Received> got;
  Collect(*sock, got);
  Behind out;

  // Talk to r0 only, and learn the mapped address from its answer.
  sock->SendTo(Timestamp::Millis(0), Span("bind"), r0.address(), {});
  Run(all, Timestamp::Millis(100));
  if (got.size() != 1u) return out;  // The test then fails on the sizes.
  const p2p::SocketAddress mapped0 = *p2p::SocketAddress::FromString(got[0].payload);
  // An unsolicited datagram to the mapped address from another IP, then from
  // r0's IP on another port.
  const size_t before = got.size();
  r1.socket->SendTo(Timestamp::Millis(200), Span("unsolicited-ip"), mapped0, {});
  Run(all, Timestamp::Millis(300));
  out.reachable_from.push_back(got.size() > before);
  const size_t before2 = got.size();
  r2->SendTo(Timestamp::Millis(400), Span("unsolicited-port"), mapped0, {});
  Run(all, Timestamp::Millis(500));
  out.reachable_from.push_back(got.size() > before2);

  // Now ask each reflector what it sees.
  got.clear();
  sock->SendTo(Timestamp::Millis(600), Span("q"), r0.address(), {});
  sock->SendTo(Timestamp::Millis(600), Span("q"), r1.address(), {});
  sock->SendTo(Timestamp::Millis(600), Span("q"), r2->GetLocalAddress(), {});
  Run(all, Timestamp::Millis(800));
  // In reflector order, matched by who answered.
  for (const p2p::SocketAddress& who : {r0.address(), r1.address(), r2->GetLocalAddress()}) {
    for (const Received& r : got) {
      if (r.from == who) out.mapped.push_back(r.payload);
    }
  }
  return out;
}

}  // namespace

SM_TEST(EmulatedSockets, BindingThroughEachNatType) {
  const std::string mapped0 = "5.5.5.5:49152";  // The NAT's first external port.
  {
    // Open cone: one mapping for all destinations, no filtering.
    const Behind b = ProbeThrough(sim::NatType::kOpenCone);
    SM_ASSERT_EQ(b.mapped.size(), 3u);
    for (const std::string& m : b.mapped) SM_EXPECT_EQ(m, mapped0);
    SM_EXPECT_TRUE(b.reachable_from[0]);
    SM_EXPECT_TRUE(b.reachable_from[1]);
  }
  {
    // Address-restricted: one mapping; filters by IP only.
    const Behind b = ProbeThrough(sim::NatType::kAddrRestricted);
    SM_ASSERT_EQ(b.mapped.size(), 3u);
    for (const std::string& m : b.mapped) SM_EXPECT_EQ(m, mapped0);
    SM_EXPECT_FALSE(b.reachable_from[0]);
    SM_EXPECT_TRUE(b.reachable_from[1]);
  }
  {
    // Port-restricted: one mapping; filters by IP and port.
    const Behind b = ProbeThrough(sim::NatType::kPortRestricted);
    SM_ASSERT_EQ(b.mapped.size(), 3u);
    for (const std::string& m : b.mapped) SM_EXPECT_EQ(m, mapped0);
    SM_EXPECT_FALSE(b.reachable_from[0]);
    SM_EXPECT_FALSE(b.reachable_from[1]);
  }
  {
    // Symmetric: a new mapping per destination, so the reflectors disagree;
    // r0 keeps the first one.
    const Behind b = ProbeThrough(sim::NatType::kSymmetric);
    SM_ASSERT_EQ(b.mapped.size(), 3u);
    SM_EXPECT_EQ(b.mapped[0], mapped0);
    SM_EXPECT_NE(b.mapped[1], mapped0);
    SM_EXPECT_NE(b.mapped[2], mapped0);
    SM_EXPECT_NE(b.mapped[1], b.mapped[2]);
    SM_EXPECT_FALSE(b.reachable_from[0]);
    SM_EXPECT_FALSE(b.reachable_from[1]);
  }
}
