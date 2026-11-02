// sim::Network: addresses, routing and NATs (issue #985). The NAT tests port
// libwebrtc's p2p/test/nat_unittest.cc (TestBindings, TestFilters) to the
// emulated network; the timeout, hairpinning and nesting tests cover what
// the issue adds beyond libwebrtc's test NAT.
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/sim/network.h"
#include "sm_test.h"

using namespace sm;
using sim::NatType;
using sim::SocketAddress;

namespace {

using Net = sim::Network<std::string>;

constexpr uint32_t kInternalIp = SocketAddress::Ip(192, 168, 0, 2);
constexpr uint32_t kNatIp = SocketAddress::Ip(10, 0, 0, 1);
constexpr uint32_t kExtIp1 = SocketAddress::Ip(1, 1, 1, 1);
constexpr uint32_t kExtIp2 = SocketAddress::Ip(2, 2, 2, 2);

// No delay on any link, so a datagram arrives at the time it is sent.
sim::LinkConfig Instant() {
  sim::LinkConfig c;
  c.base_delay = TimeDelta::Zero();
  return c;
}

// libwebrtc's nat_unittest layout: one internal host behind the NAT and
// four external addresses, ext[2] on ext[0]'s IP and ext[3] on ext[1]'s IP
// with other ports (TestVirtualInternal).
struct Setup {
  Net net;
  Net::NatId nat;
  Net::HostId in, ext1, ext2;
  SocketAddress ext[4] = {{kExtIp1, 1000}, {kExtIp2, 1000}, {kExtIp1, 2000}, {kExtIp2, 2000}};
  explicit Setup(sim::NatConfig cfg) {
    cfg.external_ip = kNatIp;
    nat = net.AddNat(cfg);
    in = net.AddHost(kInternalIp, net.NatRealm(nat), Instant(), Instant());
    ext1 = net.AddHost(kExtIp1, Net::kPublicRealm, Instant(), Instant());
    ext2 = net.AddHost(kExtIp2, Net::kPublicRealm, Instant(), Instant());
  }
  Net::HostId HostOf(const SocketAddress& a) const { return a.ip == kExtIp1 ? ext1 : ext2; }
  // Sends from ext[i] to `to` at `t`; returns what the internal host got.
  std::vector<sim::Datagram<std::string>> FromExt(int i, SocketAddress to, Timestamp t) {
    net.Send(t, HostOf(ext[i]), ext[i].port, to, "filter_test", DataSize::Bytes(100));
    return net.Receive(t, in);
  }
};

sim::NatConfig Type(NatType t) {
  sim::NatConfig c;
  c.type = t;
  return c;
}

// TestSend: the internal host sends to all four external addresses; every
// one receives it, from the same mapped address unless the NAT is symmetric.
void CheckBindings(NatType type, bool exp_same) {
  Setup s(Type(type));
  const Timestamp t = Timestamp::Millis(0);
  SocketAddress first;
  for (int i = 0; i < 4; ++i) {
    s.net.Send(t, s.in, 5000, s.ext[i], "filter_test", DataSize::Bytes(100));
    auto got = s.net.Receive(t, s.HostOf(s.ext[i]));
    SM_ASSERT_EQ(got.size(), 1u);
    SM_EXPECT_EQ(got[0].dst, s.ext[i]);
    SM_EXPECT_EQ(got[0].src.ip, kNatIp);
    SM_EXPECT_EQ(got[0].payload, std::string("filter_test"));
    if (i == 0) {
      first = got[0].src;
    } else {
      SM_EXPECT_EQ(got[0].src == first, exp_same);
    }
  }
  SM_EXPECT_EQ(s.net.stats().bindings_created, exp_same ? 1u : 4u);
}

// TestRecv: after the internal host sends to ext[0], ext[1] (other IP),
// ext[2] (same IP, other port) and ext[3] (other IP and port) send to its
// mapped address; each gets through unless the NAT filters on what differs.
void CheckFilters(NatType type, bool filter_ip, bool filter_port) {
  Setup s(Type(type));
  const Timestamp t = Timestamp::Millis(0);
  s.net.Send(t, s.in, 5000, s.ext[0], "filter_test", DataSize::Bytes(100));
  auto out = s.net.Receive(t, s.ext1);
  SM_ASSERT_EQ(out.size(), 1u);
  const SocketAddress trans = out[0].src;

  auto r1 = s.FromExt(1, trans, t);
  SM_EXPECT_EQ(r1.size(), filter_ip ? 0u : 1u);
  auto r2 = s.FromExt(2, trans, t);
  SM_EXPECT_EQ(r2.size(), filter_port ? 0u : 1u);
  auto r3 = s.FromExt(3, trans, t);
  SM_EXPECT_EQ(r3.size(), (!filter_ip && !filter_port) ? 1u : 0u);
  // What gets through is rewritten to the internal address, from the real source.
  if (!r1.empty()) {
    SM_EXPECT_EQ(r1[0].dst, (SocketAddress{kInternalIp, 5000}));
    SM_EXPECT_EQ(r1[0].src, s.ext[1]);
  }
  // ext[0] itself is always allowed back.
  SM_EXPECT_EQ(s.FromExt(0, trans, t).size(), 1u);
}

}  // namespace

SM_TEST(Nat, RulesMatchLibwebrtcNatTypes) {
  // nat_types.cc: IsSymmetric, FiltersIP, FiltersPort.
  constexpr auto cone = sim::NatRules::Of(NatType::kOpenCone);
  constexpr auto addr = sim::NatRules::Of(NatType::kAddrRestricted);
  constexpr auto port = sim::NatRules::Of(NatType::kPortRestricted);
  constexpr auto sym = sim::NatRules::Of(NatType::kSymmetric);
  SM_EXPECT_TRUE(!cone.symmetric && !cone.filters_ip && !cone.filters_port);
  SM_EXPECT_TRUE(!addr.symmetric && addr.filters_ip && !addr.filters_port);
  SM_EXPECT_TRUE(!port.symmetric && port.filters_ip && port.filters_port);
  SM_EXPECT_TRUE(sym.symmetric && sym.filters_ip && sym.filters_port);
}

SM_TEST(Nat, Bindings) {
  CheckBindings(NatType::kOpenCone, true);
  CheckBindings(NatType::kAddrRestricted, true);
  CheckBindings(NatType::kPortRestricted, true);
  CheckBindings(NatType::kSymmetric, false);
}

SM_TEST(Nat, Filters) {
  CheckFilters(NatType::kOpenCone, false, false);
  CheckFilters(NatType::kAddrRestricted, true, false);
  CheckFilters(NatType::kPortRestricted, true, true);
  CheckFilters(NatType::kSymmetric, true, true);
}

SM_TEST(Nat, UnsolicitedInboundIsDropped) {
  for (NatType type : {NatType::kOpenCone, NatType::kSymmetric}) {
    Setup s(Type(type));
    // No binding yet: even an open cone NAT has nowhere to forward it.
    SM_EXPECT_EQ(s.FromExt(0, {kNatIp, 49152}, Timestamp::Millis(0)).size(), 0u);
    SM_EXPECT_EQ(s.net.stats().nat_no_binding, 1u);
  }
}

SM_TEST(Nat, PortsAreHandedOutInOrder) {
  sim::NatConfig cfg = Type(NatType::kSymmetric);
  cfg.first_port = 40000;
  Setup s(cfg);
  for (int i = 0; i < 3; ++i) s.net.Send(Timestamp::Millis(0), s.in, 5000, s.ext[i], "x", DataSize::Bytes(10));
  // A binding is made when the datagram reaches the NAT, at the end of the uplink.
  s.net.Process(Timestamp::Millis(0));
  SM_EXPECT_EQ(s.net.MappedAddress(s.nat, {kInternalIp, 5000}, s.ext[0])->port, 40000);
  SM_EXPECT_EQ(s.net.MappedAddress(s.nat, {kInternalIp, 5000}, s.ext[2])->port, 40002);
  SM_EXPECT_FALSE(s.net.MappedAddress(s.nat, {kInternalIp, 5000}, s.ext[3]).has_value());
  SM_EXPECT_EQ(s.net.BindingCount(s.nat), 3u);
}

SM_TEST(Nat, BindingsNeverExpireByDefault) {
  Setup s(Type(NatType::kPortRestricted));
  s.net.Send(Timestamp::Millis(0), s.in, 5000, s.ext[0], "x", DataSize::Bytes(10));
  s.net.Process(Timestamp::Millis(0));
  const SocketAddress mapped = *s.net.MappedAddress(s.nat, {kInternalIp, 5000});
  // An hour later the reply still gets in, as with libwebrtc's NATServer.
  SM_EXPECT_EQ(s.FromExt(0, mapped, Timestamp::Seconds(3600)).size(), 1u);
  SM_EXPECT_EQ(s.net.stats().bindings_expired, 0u);
}

SM_TEST(Nat, BindingTimesOutWithoutOutboundTraffic) {
  sim::NatConfig cfg = Type(NatType::kPortRestricted);
  cfg.binding_timeout = TimeDelta::Seconds(30);
  Setup s(cfg);
  s.net.Send(Timestamp::Seconds(0), s.in, 5000, s.ext[0], "x", DataSize::Bytes(10));
  s.net.Process(Timestamp::Seconds(0));
  const SocketAddress mapped = *s.net.MappedAddress(s.nat, {kInternalIp, 5000});
  // Inbound traffic does not refresh the binding (RFC 4787 REQ-6 makes that optional).
  SM_EXPECT_EQ(s.FromExt(0, mapped, Timestamp::Seconds(20)).size(), 1u);
  SM_EXPECT_EQ(s.FromExt(0, mapped, Timestamp::Seconds(31)).size(), 0u);
  SM_EXPECT_EQ(s.net.stats().bindings_expired, 1u);
  SM_EXPECT_EQ(s.net.stats().nat_no_binding, 1u);
  // The next outbound datagram gets a new binding on a new port.
  s.net.Send(Timestamp::Seconds(32), s.in, 5000, s.ext[0], "x", DataSize::Bytes(10));
  s.net.Process(Timestamp::Seconds(32));
  const SocketAddress again = *s.net.MappedAddress(s.nat, {kInternalIp, 5000});
  SM_EXPECT_NE(again.port, mapped.port);
  SM_EXPECT_EQ(s.net.stats().bindings_created, 2u);

  // Outbound keepalives inside the timeout keep it.
  Setup k(cfg);
  for (int sec = 0; sec <= 100; sec += 25) {
    k.net.Send(Timestamp::Seconds(sec), k.in, 5000, k.ext[0], "keepalive", DataSize::Bytes(10));
  }
  SM_EXPECT_EQ(k.net.stats().bindings_created, 1u);
  SM_EXPECT_EQ(k.FromExt(0, *k.net.MappedAddress(k.nat, {kInternalIp, 5000}), Timestamp::Seconds(120)).size(), 1u);
}

SM_TEST(Nat, InboundRefreshIsOptional) {
  sim::NatConfig cfg = Type(NatType::kPortRestricted);
  cfg.binding_timeout = TimeDelta::Seconds(30);
  cfg.inbound_refreshes = true;
  Setup s(cfg);
  s.net.Send(Timestamp::Seconds(0), s.in, 5000, s.ext[0], "x", DataSize::Bytes(10));
  s.net.Process(Timestamp::Seconds(0));
  const SocketAddress mapped = *s.net.MappedAddress(s.nat, {kInternalIp, 5000});
  for (int sec = 20; sec <= 100; sec += 20) SM_EXPECT_EQ(s.FromExt(0, mapped, Timestamp::Seconds(sec)).size(), 1u);
  SM_EXPECT_EQ(s.net.stats().bindings_expired, 0u);
}

SM_TEST(Nat, Hairpinning) {
  for (bool hairpin : {true, false}) {
    sim::NatConfig cfg = Type(NatType::kPortRestricted);
    cfg.external_ip = kNatIp;
    cfg.hairpinning = hairpin;
    Net net;
    const auto nat = net.AddNat(cfg);
    const auto a = net.AddHost(SocketAddress::Ip(192, 168, 0, 2), net.NatRealm(nat), Instant(), Instant());
    const auto b = net.AddHost(SocketAddress::Ip(192, 168, 0, 3), net.NatRealm(nat), Instant(), Instant());
    net.AddHost(kExtIp1, Net::kPublicRealm, Instant(), Instant());
    // Both learn their mapped addresses from a public server (as STUN would).
    const Timestamp t = Timestamp::Millis(0);
    net.Send(t, a, 5000, {kExtIp1, 3478}, "binding", DataSize::Bytes(20));
    net.Send(t, b, 6000, {kExtIp1, 3478}, "binding", DataSize::Bytes(20));
    net.Process(t);
    const SocketAddress ma = *net.MappedAddress(nat, {net.host_ip(a), 5000});
    const SocketAddress mb = *net.MappedAddress(nat, {net.host_ip(b), 6000});
    // a's first datagram to b's mapped address opens a's binding to it but is
    // filtered at b's binding (b has not sent to a's mapped address yet).
    net.Send(t, a, 5000, mb, "a->b 1", DataSize::Bytes(20));
    SM_EXPECT_EQ(net.Receive(t, b).size(), 0u);
    // b answers to a's mapped address: it gets in, from b's mapped address.
    net.Send(t, b, 6000, ma, "b->a", DataSize::Bytes(20));
    auto at_a = net.Receive(t, a);
    // Then a's next datagram reaches b.
    net.Send(t, a, 5000, mb, "a->b 2", DataSize::Bytes(20));
    auto at_b = net.Receive(t, b);
    if (hairpin) {
      SM_ASSERT_EQ(at_a.size(), 1u);
      SM_EXPECT_EQ(at_a[0].src, mb);
      SM_EXPECT_EQ(at_a[0].dst, (SocketAddress{net.host_ip(a), 5000}));
      SM_ASSERT_EQ(at_b.size(), 1u);
      SM_EXPECT_EQ(at_b[0].src, ma);
      SM_EXPECT_EQ(at_b[0].payload, std::string("a->b 2"));
      SM_EXPECT_EQ(net.stats().hairpinned, 3u);
      SM_EXPECT_EQ(net.stats().nat_filtered, 1u);
    } else {
      SM_EXPECT_EQ(at_a.size(), 0u);
      SM_EXPECT_EQ(at_b.size(), 0u);
      SM_EXPECT_EQ(net.stats().hairpin_dropped, 3u);
    }
  }
}

SM_TEST(SimNetwork, SameRealmNeedsNoNat) {
  Net net;
  const auto nat = net.AddNat({.type = NatType::kSymmetric, .external_ip = kNatIp});
  const auto a = net.AddHost(SocketAddress::Ip(192, 168, 0, 2), net.NatRealm(nat), Instant(), Instant());
  const auto b = net.AddHost(SocketAddress::Ip(192, 168, 0, 3), net.NatRealm(nat), Instant(), Instant());
  net.Send(Timestamp::Millis(0), a, 5000, {net.host_ip(b), 6000}, "lan", DataSize::Bytes(20));
  auto got = net.Receive(Timestamp::Millis(0), b);
  SM_ASSERT_EQ(got.size(), 1u);
  SM_EXPECT_EQ(got[0].src, (SocketAddress{net.host_ip(a), 5000}));
  SM_EXPECT_EQ(net.stats().bindings_created, 0u);
}

SM_TEST(SimNetwork, NestedNats) {
  // A host behind a home NAT behind a carrier-grade NAT talks to a public host.
  Net net;
  const uint32_t cgn_ip = SocketAddress::Ip(203, 0, 113, 1);
  const uint32_t home_ip = SocketAddress::Ip(100, 64, 0, 7);
  const auto cgn = net.AddNat({.type = NatType::kPortRestricted, .external_ip = cgn_ip});
  const auto home = net.AddNat({.type = NatType::kOpenCone, .external_ip = home_ip}, net.NatRealm(cgn));
  const auto in = net.AddHost(kInternalIp, net.NatRealm(home), Instant(), Instant());
  const auto pub = net.AddHost(kExtIp1, Net::kPublicRealm, Instant(), Instant());
  const Timestamp t = Timestamp::Millis(0);
  net.Send(t, in, 5000, {kExtIp1, 80}, "hi", DataSize::Bytes(20));
  auto at_pub = net.Receive(t, pub);
  SM_ASSERT_EQ(at_pub.size(), 1u);
  SM_EXPECT_EQ(at_pub[0].src.ip, cgn_ip);
  SM_EXPECT_EQ(*net.MappedAddress(home, {kInternalIp, 5000}), (SocketAddress{home_ip, 49152}));
  net.Send(t, pub, 80, at_pub[0].src, "back", DataSize::Bytes(20));
  auto at_in = net.Receive(t, in);
  SM_ASSERT_EQ(at_in.size(), 1u);
  SM_EXPECT_EQ(at_in[0].dst, (SocketAddress{kInternalIp, 5000}));
  SM_EXPECT_EQ(at_in[0].src, (SocketAddress{kExtIp1, 80}));
}

SM_TEST(SimNetwork, AccessLinksKeepNetworkLinkImpairments) {
  Net net(7);
  sim::LinkConfig up;
  up.base_delay = TimeDelta::Millis(20);
  up.capacity = DataRate::KilobitsPerSec(800);  // 1000 B take 10 ms.
  sim::LinkConfig down;
  down.base_delay = TimeDelta::Millis(30);
  const auto a = net.AddHost(kExtIp1, Net::kPublicRealm, up, down);
  const auto b = net.AddHost(kExtIp2, Net::kPublicRealm, up, down);
  net.Send(Timestamp::Millis(0), a, 1, {kExtIp2, 2}, "one", DataSize::Bytes(1000), sim::LinkKind::kVideo);
  net.Send(Timestamp::Millis(0), a, 1, {kExtIp2, 2}, "two", DataSize::Bytes(1000), sim::LinkKind::kVideo);
  // Uplink: 10 ms serialization + 20 ms; downlink: 30 ms (no capacity limit).
  SM_EXPECT_EQ(net.Receive(Timestamp::Millis(59), b).size(), 0u);
  SM_EXPECT_EQ(net.Receive(Timestamp::Millis(60), b).size(), 1u);
  SM_EXPECT_EQ(net.Receive(Timestamp::Millis(69), b).size(), 0u);
  SM_EXPECT_EQ(net.Receive(Timestamp::Millis(70), b).size(), 1u);
  // Both links count the datagram with its size and kind.
  SM_EXPECT_EQ(net.uplink(a).stats().of(sim::LinkKind::kVideo).bytes_sent, 2000);
  SM_EXPECT_EQ(net.downlink(b).stats().of(sim::LinkKind::kVideo).bytes_sent, 2000);

  // An outage on b's downlink loses what arrives during it.
  sim::LinkConfig out = down;
  out.down = true;
  net.downlink(b).SetConfig(out);
  net.Send(Timestamp::Millis(100), a, 1, {kExtIp2, 2}, "lost", DataSize::Bytes(100));
  SM_EXPECT_EQ(net.Receive(Timestamp::Millis(200), b).size(), 0u);
  SM_EXPECT_EQ(net.downlink(b).stats().lost, 1u);
  SM_EXPECT_EQ(net.NextEvent(), Timestamp::PlusInfinity());
}

SM_TEST(SimNetwork, RandomLossFollowsTheLinkConfig) {
  Net net(42);
  sim::LinkConfig lossy = Instant();
  lossy.loss_rate = 0.1;
  const auto a = net.AddHost(kExtIp1, Net::kPublicRealm, lossy, Instant());
  const auto b = net.AddHost(kExtIp2, Net::kPublicRealm, Instant(), Instant());
  size_t got = 0;
  for (int i = 0; i < 10000; ++i) {
    net.Send(Timestamp::Millis(i), a, 1, {kExtIp2, 2}, "x", DataSize::Bytes(100));
    got += net.Receive(Timestamp::Millis(i), b).size();
  }
  SM_EXPECT_NEAR(1.0 - static_cast<double>(got) / 10000, 0.1, 0.015);
  SM_EXPECT_EQ(net.stats().delivered, got);
}

SM_TEST(SimNetwork, UnknownAddressesAreUnroutable) {
  Net net;
  const auto a = net.AddHost(kExtIp1, Net::kPublicRealm, Instant(), Instant());
  net.Send(Timestamp::Millis(0), a, 1, {SocketAddress::Ip(9, 9, 9, 9), 2}, "x", DataSize::Bytes(10));
  net.Process(Timestamp::Millis(0));
  SM_EXPECT_EQ(net.stats().unroutable, 1u);
  // A private address is not reachable from the public realm.
  const auto nat = net.AddNat({.type = NatType::kOpenCone, .external_ip = kNatIp});
  net.AddHost(kInternalIp, net.NatRealm(nat), Instant(), Instant());
  net.Send(Timestamp::Millis(0), a, 1, {kInternalIp, 2}, "x", DataSize::Bytes(10));
  net.Process(Timestamp::Millis(0));
  SM_EXPECT_EQ(net.stats().unroutable, 2u);
  SM_EXPECT_EQ(SocketAddress({kInternalIp, 5000}).ToString(), std::string("192.168.0.2:5000"));
}

SM_TEST(SimNetwork, BindingIsMadeWhenTheDatagramReachesTheNat) {
  Net net;
  sim::LinkConfig up;
  up.base_delay = TimeDelta::Millis(20);
  const auto nat = net.AddNat({.type = NatType::kPortRestricted, .external_ip = kNatIp});
  const auto in = net.AddHost(kInternalIp, net.NatRealm(nat), up, Instant());
  const auto pub = net.AddHost(kExtIp1, Net::kPublicRealm, Instant(), Instant());
  net.Send(Timestamp::Millis(0), in, 5000, {kExtIp1, 80}, "x", DataSize::Bytes(20));
  net.Process(Timestamp::Millis(19));
  SM_EXPECT_FALSE(net.MappedAddress(nat, {kInternalIp, 5000}).has_value());
  SM_EXPECT_EQ(net.NextEvent(), Timestamp::Millis(20));
  SM_EXPECT_EQ(net.Receive(Timestamp::Millis(20), pub).size(), 1u);
  SM_EXPECT_TRUE(net.MappedAddress(nat, {kInternalIp, 5000}).has_value());
}
