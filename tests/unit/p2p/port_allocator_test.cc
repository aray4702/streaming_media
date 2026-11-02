// Port allocation (issue #1001): the UDP and STUN cases of libwebrtc M157's
// p2p/base/port_allocator_unittest.cc and
// p2p/client/basic_port_allocator_unittest.cc, with the library's time model
// in place of the simulated clock. libwebrtc's fixture has a host TCP port
// on every network; there are no TCP ports until C4, so each expected count
// here is libwebrtc's less its TCP ports (noted at each case). The STUN
// cases run over the emulated network (#985) with a STUN server; the others
// on a socket factory that binds any address, as libwebrtc's virtual socket
// server does.
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/basic_port_allocator.h"
#include "sm/p2p/candidate.h"
#include "sm/p2p/network_manager.h"
#include "sm/p2p/port_allocator.h"
#include "sm/p2p/stun_port.h"
#include "sm/sim/emulated_sockets.h"
#include "sm/sim/stun_server.h"
#include "sm_test.h"
#include "test_crypto.h"

using namespace sm;
using namespace sm::p2p;

namespace {

IpAddress Ip(std::string_view s) { return *IpAddress::FromString(s); }

const IpAddress kClientIp = Ip("11.11.11.11");
const IpAddress kClientIp2 = Ip("22.22.22.22");
const IpAddress kPrivateIp = Ip("192.168.1.11");
const IpAddress kLoopbackIp = Ip("127.0.0.1");
const IpAddress kNatIp = Ip("77.77.77.77");
const IpAddress kClientIPv6 = Ip("2401:fa00:4:1000:be30:5bff:fee5:c3");
const IpAddress kClientIPv6_2 = Ip("2401:fa00:4:2000:be30:5bff:fee5:c3");
const IpAddress kClientIPv6_3 = Ip("2401:fa00:4:3000:be30:5bff:fee5:c3");
const SocketAddress kStunAddr(Ip("99.99.99.1"), 3478);  // STUN_SERVER_PORT.

constexpr int kMinPort = 10000;
constexpr int kMaxPort = 10099;
const char kIceUfrag0[] = "UF00";
const char kIcePwd0[] = "TESTICEPWD00000000000000";
const char kContentName[] = "test content";

// A socket that sends nowhere.
class FakeSocket final : public AsyncPacketSocket {
 public:
  explicit FakeSocket(SocketAddress local) : local_(local) {}
  SocketAddress GetLocalAddress() const override { return local_; }
  SocketAddress GetRemoteAddress() const override { return {}; }
  int Send(Timestamp, std::span<const uint8_t>, const PacketOptions&) override { return -1; }
  int SendTo(Timestamp, std::span<const uint8_t> data, const SocketAddress&, const PacketOptions&) override {
    return static_cast<int>(data.size());
  }
  int Close() override { return 0; }
  State GetState() const override { return State::kBound; }
  int GetOption(SocketOption, int*) override { return -1; }
  int SetOption(SocketOption, int) override { return 0; }
  int GetError() const override { return 0; }
  void SetError(int) override {}

 private:
  SocketAddress local_;
};

// Binds any address, in the range if one is given (VirtualSocketServer).
class FakeFactory final : public PacketSocketFactory {
 public:
  std::unique_ptr<AsyncPacketSocket> CreateUdpSocket(const SocketAddress& a, uint16_t min_port,
                                                     uint16_t max_port) override {
    if (!udp_enabled) return nullptr;
    uint16_t port = min_port == 0 && max_port == 0 ? next_++ : static_cast<uint16_t>(min_port + in_range_++);
    if (max_port != 0 && port > max_port) return nullptr;
    ++created;
    return std::make_unique<FakeSocket>(SocketAddress(a.ipaddr(), port));
  }
  bool udp_enabled = true;
  int created = 0;

 private:
  uint16_t next_ = 49152;
  uint16_t in_range_ = 0;
};

// Lets a test turn socket creation off (VirtualSocketServer's
// set_udp_sockets_enabled) on any factory.
class GatedFactory final : public PacketSocketFactory {
 public:
  explicit GatedFactory(PacketSocketFactory* inner) : inner_(inner) {}
  std::unique_ptr<AsyncPacketSocket> CreateUdpSocket(const SocketAddress& a, uint16_t min_port,
                                                     uint16_t max_port) override {
    return enabled ? inner_->CreateUdpSocket(a, min_port, max_port) : nullptr;
  }
  bool enabled = true;

 private:
  PacketSocketFactory* inner_;
};

sim::LinkConfig Link(int delay_ms) {
  sim::LinkConfig c;
  c.base_delay = TimeDelta::Millis(delay_ms);
  return c;
}

uint32_t V4(const IpAddress& ip) { return ip.v4(); }

// BasicPortAllocatorTest. kFake: any-address sockets that reach nothing;
// kPublic / kNat: one client host on the emulated network (public, or behind
// a port-restricted NAT at 77.77.77.77) with a STUN server at kStunAddr.
struct Fixture {
  enum Mode { kFake, kPublic, kNat };

  sim::ByteNetwork net;
  std::unique_ptr<sim::EmulatedSocketServer> client;
  std::unique_ptr<sim::EmulatedSocketServer> stun_host;
  std::unique_ptr<sim::StunServer> stun;
  FakeFactory fake;
  std::unique_ptr<GatedFactory> gate;
  StaticNetworkManager network_manager;
  std::unique_ptr<BasicPortAllocator> allocator;
  std::unique_ptr<PortAllocatorSession> session;
  Timestamp now = Timestamp::Seconds(10);

  std::vector<Candidate> candidates;
  std::vector<Port*> ports;
  bool done = false;
  int regathering = 0;
  std::vector<Candidate> removed;

  explicit Fixture(Mode mode = kFake, bool with_stun_server = true) {
    testing::InstallTestCrypto();
    PacketSocketFactory* factory = &fake;
    if (mode != kFake) {
      int realm = sim::ByteNetwork::kPublicRealm;
      if (mode == kNat) {
        sim::NatConfig cfg;
        cfg.type = sim::NatType::kPortRestricted;
        cfg.external_ip = V4(kNatIp);
        realm = net.NatRealm(net.AddNat(cfg));
      }
      const uint32_t client_ip = mode == kNat ? V4(kPrivateIp) : V4(kClientIp);
      client = std::make_unique<sim::EmulatedSocketServer>(net, net.AddHost(client_ip, realm, Link(10), Link(10)));
      stun_host = std::make_unique<sim::EmulatedSocketServer>(
          net, net.AddHost(kStunAddr.ip(), sim::ByteNetwork::kPublicRealm, Link(5), Link(5)));
      stun = std::make_unique<sim::StunServer>(stun_host->CreateUdpSocket(kStunAddr, 0, 0));
      factory = client.get();
    }
    gate = std::make_unique<GatedFactory>(factory);
    allocator = std::make_unique<BasicPortAllocator>(&network_manager, gate.get());
    allocator->SetConfiguration(now, with_stun_server ? ServerAddresses{kStunAddr} : ServerAddresses{}, {}, 0,
                                NO_PRUNE);
    allocator->set_step_delay(kMinimumStepDelay);
  }

  void AddInterface(const IpAddress& ip) { network_manager.AddInterface(ip); }
  void AddInterface(const IpAddress& ip, std::string name, AdapterType type = ADAPTER_TYPE_UNKNOWN) {
    network_manager.AddInterface(ip, std::move(name), type);
  }

  std::unique_ptr<PortAllocatorSession> MakeSession(std::string_view content = "test content") {
    auto s = allocator->CreateSession(content, ICE_CANDIDATE_COMPONENT_RTP, kIceUfrag0, kIcePwd0);
    Subscribe(s.get());
    return s;
  }
  void Subscribe(PortAllocatorSession* s) {
    s->SubscribePortReady(this, [this](PortAllocatorSession*, Port* p) {
      if (std::find(ports.begin(), ports.end(), p) == ports.end()) ports.push_back(p);
    });
    s->SubscribeCandidatesReady(this, [this](PortAllocatorSession*, const std::vector<Candidate>& c) {
      candidates.insert(candidates.end(), c.begin(), c.end());
    });
    s->SubscribeCandidatesAllocationDone(this, [this](PortAllocatorSession*) { done = true; });
    s->SubscribeIceRegathering(this, [this](PortAllocatorSession*, IceRegatheringReason) { ++regathering; });
    s->SubscribeCandidatesRemoved(this, [this](PortAllocatorSession*, const std::vector<Candidate>& c) {
      removed.insert(removed.end(), c.begin(), c.end());
    });
    s->SubscribePortsPruned(this, [this](PortAllocatorSession*, const std::vector<Port*>& pruned) {
      for (Port* p : pruned) std::erase(ports, p);
    });
  }
  void CreateSession() { session = MakeSession(); }

  Timestamp NextEvent() const {
    Timestamp t = Timestamp::PlusInfinity();
    if (session) t = std::min(t, session->NextProcessTime());
    t = std::min(t, allocator->NextPooledSessionsProcessTime());
    if (client) t = std::min(t, client->NextEvent());
    return t;
  }
  void Step(Timestamp t) {
    now = std::max(now, t);
    if (session) session->Process(now);
    allocator->ProcessPooledSessions(now);
    if (client) {
      client->Deliver(now);
      stun_host->Deliver(now);
    }
  }
  // Runs until `stop` holds, at most `limit` of emulated time.
  template <class F>
  bool RunUntil(F stop, TimeDelta limit = TimeDelta::Seconds(60)) {
    const Timestamp until = now + limit;
    for (;;) {
      if (stop()) return true;
      const Timestamp t = NextEvent();
      if (t > until) {
        now = until;
        return stop();
      }
      Step(t);
    }
  }
  bool RunUntilDone() {
    return RunUntil([this] { return done; });
  }
  // Advances the clock by `d`, running everything due (AdvanceTime).
  void Advance(TimeDelta d) {
    RunUntil([] { return false; }, d);
    Step(now);
  }

  bool HasCandidate(IceCandidateType type, std::string_view proto, const IpAddress& ip) const {
    return std::any_of(candidates.begin(), candidates.end(), [&](const Candidate& c) {
      return c.type() == type && c.protocol() == proto && c.address().ipaddr() == ip;
    });
  }
};

}  // namespace

// --------------------------------------------------------- PortAllocatorTest

SM_TEST(P2pPortAllocator, Defaults) {
  StaticNetworkManager nm;
  FakeFactory f;
  BasicPortAllocator allocator(&nm, &f);
  SM_EXPECT_EQ(allocator.stun_servers().size(), size_t{0});
  SM_EXPECT_EQ(allocator.turn_servers().size(), size_t{0});
  SM_EXPECT_EQ(allocator.candidate_pool_size(), 0);
  SM_EXPECT_TRUE(allocator.GetPooledSession() == nullptr);
  // libwebrtc's defaults.
  SM_EXPECT_EQ(allocator.flags(), kDefaultPortAllocatorFlags);
  SM_EXPECT_EQ(allocator.step_delay(), kDefaultStepDelay);
  SM_EXPECT_EQ(allocator.step_delay(), 1000u);
  SM_EXPECT_EQ(allocator.max_ipv6_networks(), 5);
  SM_EXPECT_EQ(allocator.candidate_filter(), uint32_t{CF_ALL});
  SM_EXPECT_EQ(allocator.min_port(), 0);
  SM_EXPECT_EQ(allocator.max_port(), 0);
  SM_EXPECT_TRUE(allocator.allow_tcp_listen());
  SM_EXPECT_EQ(allocator.GetNetworkIgnoreMask(), int{ADAPTER_TYPE_LOOPBACK});
  SM_EXPECT_EQ(kMinimumStepDelay, 50u);
}

SM_TEST(P2pPortAllocator, CreateSession) {
  Fixture f;
  f.allocator->SetCandidateFilter(CF_RELAY);
  auto session = f.allocator->CreateSession(kContentName, 1, "ufrag", "password");
  SM_ASSERT_TRUE(session != nullptr);
  SM_EXPECT_EQ(session->content_name(), std::string(kContentName));
  SM_EXPECT_EQ(session->component(), 1);
  SM_EXPECT_EQ(session->ice_ufrag(), std::string("ufrag"));
  SM_EXPECT_EQ(session->ice_pwd(), std::string("password"));
  SM_EXPECT_FALSE(session->pooled());
}

SM_TEST(P2pPortAllocator, SetConfigurationUpdatesIceServersAndPool) {
  Fixture f;
  const ServerAddresses stun1 = {SocketAddress(Ip("11.11.11.11"), 10)};
  const ServerAddresses stun2 = {SocketAddress(Ip("22.22.22.22"), 10)};
  const std::vector<RelayServerConfig> turn1 = {
      RelayServerConfig(SocketAddress(Ip("11.11.11.11"), 3478), "test", "test", PROTO_UDP, false)};
  const std::vector<RelayServerConfig> turn2 = {
      RelayServerConfig(SocketAddress(Ip("22.22.22.22"), 3478), "test", "test", PROTO_UDP, false)};
  SM_EXPECT_TRUE(f.allocator->SetConfiguration(f.now, stun1, turn1, 0, NO_PRUNE));
  SM_EXPECT_TRUE(f.allocator->stun_servers() == stun1);
  SM_EXPECT_TRUE(f.allocator->turn_servers() == turn1);
  SM_EXPECT_TRUE(f.allocator->SetConfiguration(f.now, stun2, turn2, 0, NO_PRUNE));
  SM_EXPECT_TRUE(f.allocator->stun_servers() == stun2);
  SM_EXPECT_TRUE(f.allocator->turn_servers() == turn2);
  // SetConfigurationUpdatesCandidatePoolSize, ...CreatesMore / Destroys.
  for (int size : {2, 3, 1, 4}) {
    f.allocator->SetConfiguration(f.now, stun2, turn2, size, NO_PRUNE);
    SM_EXPECT_EQ(f.allocator->candidate_pool_size(), size);
    SM_EXPECT_EQ(f.allocator->GetPooledIceCredentials().size(), static_cast<size_t>(size));
  }
  // SetConfigurationRecreatesPooledSessionsWhenIceServersChange.
  // The recreated sessions have new ICE credentials. (Comparing session pointers is not enough: the new session can
  // be allocated at the freed one's address, as macOS malloc does.)
  const std::vector<IceCredentials> before = f.allocator->GetPooledIceCredentials();
  f.allocator->SetConfiguration(f.now, stun1, turn1, 4, NO_PRUNE);
  for (const IceCredentials& now_pooled : f.allocator->GetPooledIceCredentials()) {
    for (const IceCredentials& old : before) SM_EXPECT_TRUE(now_pooled.ufrag != old.ufrag || now_pooled.pwd != old.pwd);
  }
  SM_EXPECT_EQ(f.allocator->GetPooledIceCredentials().size(), size_t{4});
  // DiscardCandidatePool.
  f.allocator->DiscardCandidatePool();
  SM_EXPECT_TRUE(f.allocator->GetPooledSession() == nullptr);
  SM_EXPECT_FALSE(f.allocator->SetConfiguration(f.now, stun1, turn1, -1, NO_PRUNE));
}

// GetPooledSessionReturnsNextSession, TakePooledSessionUpdatesIceParameters,
// TakePooledSessionUpdatesCandidateFilter.
SM_TEST(P2pPortAllocator, TakePooledSession) {
  Fixture f;
  f.AddInterface(kClientIp);
  f.allocator->SetConfiguration(f.now, f.allocator->stun_servers(), {}, 2, NO_PRUNE);
  const PortAllocatorSession* peeked1 = f.allocator->GetPooledSession();
  SM_ASSERT_TRUE(peeked1 != nullptr);
  SM_EXPECT_TRUE(peeked1->pooled());
  SM_EXPECT_EQ(peeked1->content_name(), std::string());
  SM_EXPECT_EQ(peeked1->ice_ufrag().size(), size_t{ICE_UFRAG_LENGTH});
  SM_EXPECT_EQ(peeked1->ice_pwd().size(), size_t{ICE_PWD_LENGTH});
  f.allocator->SetCandidateFilter(CF_RELAY);
  auto s1 = f.allocator->TakePooledSession(kContentName, 1, kIceUfrag0, kIcePwd0);
  SM_EXPECT_TRUE(s1.get() == peeked1);
  SM_EXPECT_FALSE(s1->pooled());
  SM_EXPECT_EQ(s1->content_name(), std::string(kContentName));
  SM_EXPECT_EQ(s1->component(), 1);
  SM_EXPECT_EQ(s1->ice_ufrag(), std::string(kIceUfrag0));
  SM_EXPECT_EQ(s1->ice_pwd(), std::string(kIcePwd0));
  const PortAllocatorSession* peeked2 = f.allocator->GetPooledSession();
  auto s2 = f.allocator->TakePooledSession(kContentName, 1, kIceUfrag0, kIcePwd0);
  SM_EXPECT_TRUE(s2.get() == peeked2);
  SM_EXPECT_TRUE(f.allocator->TakePooledSession(kContentName, 1, kIceUfrag0, kIcePwd0) == nullptr);
}

SM_TEST(P2pPortAllocator, RestrictIceCredentialsChange) {
  Fixture f;
  f.allocator->set_restrict_ice_credentials_change(true);
  f.allocator->SetConfiguration(f.now, f.allocator->stun_servers(), {}, 1, NO_PRUNE);
  const auto credentials = f.allocator->GetPooledIceCredentials();
  SM_ASSERT_EQ(credentials.size(), size_t{1});
  SM_EXPECT_TRUE(f.allocator->TakePooledSession(kContentName, 0, kIceUfrag0, kIcePwd0) == nullptr);
  SM_EXPECT_TRUE(f.allocator->TakePooledSession(kContentName, 0, credentials[0].ufrag, credentials[0].pwd) !=
                 nullptr);
  SM_EXPECT_TRUE(f.allocator->TakePooledSession(kContentName, 0, credentials[0].ufrag, credentials[0].pwd) ==
                 nullptr);
}

// SanitizeEmptyCandidateDefaultConfig, SanitizeIpv4CandidateDefaultConfig,
// and the related-address rules of SanitizeCandidate.
SM_TEST(P2pPortAllocator, SanitizeCandidate) {
  Fixture f;
  SM_EXPECT_EQ(f.allocator->SanitizeCandidate(Candidate()).address().ipaddr().ToString(), std::string());
  Candidate host(1, "udp", SocketAddress(Ip("12.34.56.78"), 443), 1, "username", "password",
                 IceCandidateType::kHost, 1, "foundation", 1, 1);
  SM_EXPECT_EQ(f.allocator->SanitizeCandidate(host).address().ToString(), std::string("12.34.56.78:443"));
  Candidate srflx = host;
  srflx.set_type(IceCandidateType::kSrflx);
  srflx.set_related_address(SocketAddress(kPrivateIp, 1000));
  // Kept by default; removed when host candidates are filtered out.
  SM_EXPECT_TRUE(f.allocator->SanitizeCandidate(srflx).related_address() == SocketAddress(kPrivateIp, 1000));
  f.allocator->SetCandidateFilter(CF_REFLEXIVE);
  SM_EXPECT_TRUE(f.allocator->SanitizeCandidate(srflx).related_address() ==
                 SocketAddress(GetAnyIp(IpFamily::kV4), 0));
  // A relay candidate's related address goes when reflexive ones are filtered.
  Candidate relay = srflx;
  relay.set_type(IceCandidateType::kRelay);
  SM_EXPECT_TRUE(f.allocator->SanitizeCandidate(relay).related_address() == SocketAddress(kPrivateIp, 1000));
  f.allocator->SetCandidateFilter(CF_RELAY);
  SM_EXPECT_TRUE(f.allocator->SanitizeCandidate(relay).related_address() ==
                 SocketAddress(GetAnyIp(IpFamily::kV4), 0));
}

// ---------------------------------------------------- BasicPortAllocatorTest

SM_TEST(P2pBasicPortAllocator, Basic) {
  Fixture f;
  SM_EXPECT_TRUE(f.allocator->network_manager() == &f.network_manager);
  SM_EXPECT_TRUE(*f.allocator->stun_servers().begin() == kStunAddr);
  SM_EXPECT_EQ(f.allocator->turn_servers().size(), size_t{0});
  f.CreateSession();
  SM_EXPECT_FALSE(f.session->CandidatesAllocationDone());
}

SM_TEST(P2pBasicPortAllocator, IgnoreOnlyLoopbackNetworkByDefault) {
  Fixture f;
  f.AddInterface(IpAddress(0x12345600u), "test_eth0", ADAPTER_TYPE_ETHERNET);
  f.AddInterface(IpAddress(0x12345601u), "test_wlan0", ADAPTER_TYPE_WIFI);
  f.AddInterface(IpAddress(0x12345602u), "test_cell0", ADAPTER_TYPE_CELLULAR);
  f.AddInterface(IpAddress(0x12345603u), "test_vpn0", ADAPTER_TYPE_VPN);
  f.AddInterface(IpAddress(0x12345604u), "test_lo", ADAPTER_TYPE_LOOPBACK);
  f.CreateSession();
  f.session->set_flags(PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY | PORTALLOCATOR_DISABLE_TCP);
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_EXPECT_EQ(f.candidates.size(), size_t{4});
  for (const Candidate& c : f.candidates) SM_EXPECT_TRUE(c.address().ip() < 0x12345604u);
}

SM_TEST(P2pBasicPortAllocator, IgnoreNetworksAccordingToIgnoreMask) {
  Fixture f;
  f.AddInterface(IpAddress(0x12345600u), "test_eth0", ADAPTER_TYPE_ETHERNET);
  f.AddInterface(IpAddress(0x12345601u), "test_wlan0", ADAPTER_TYPE_WIFI);
  f.AddInterface(IpAddress(0x12345602u), "test_cell0", ADAPTER_TYPE_CELLULAR);
  f.allocator->SetNetworkIgnoreMask(ADAPTER_TYPE_ETHERNET | ADAPTER_TYPE_LOOPBACK | ADAPTER_TYPE_WIFI);
  f.CreateSession();
  f.session->set_flags(PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY | PORTALLOCATOR_DISABLE_TCP);
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_ASSERT_EQ(f.candidates.size(), size_t{1});
  SM_EXPECT_EQ(f.candidates[0].address().ip(), 0x12345602u);
}

// The VPN preference (GetNetworkIgnoreMask).
SM_TEST(P2pBasicPortAllocator, VpnPreference) {
  for (VpnPreference pref : {VpnPreference::kOnlyUseVpn, VpnPreference::kNeverUseVpn}) {
    Fixture f;
    f.AddInterface(IpAddress(0x12345600u), "test_eth0", ADAPTER_TYPE_ETHERNET);
    f.AddInterface(IpAddress(0x12345603u), "test_vpn0", ADAPTER_TYPE_VPN);
    f.allocator->SetVpnPreference(pref);
    f.CreateSession();
    f.session->set_flags(PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY | PORTALLOCATOR_DISABLE_TCP);
    f.session->StartGettingPorts(f.now);
    SM_ASSERT_TRUE(f.RunUntilDone());
    SM_ASSERT_EQ(f.candidates.size(), size_t{1});
    SM_EXPECT_EQ(f.candidates[0].address().ip(), pref == VpnPreference::kOnlyUseVpn ? 0x12345603u : 0x12345600u);
  }
}

// WifiUsedInsteadOfCellWhenCostlyNetworksDisabled and the other costly-network cases.
SM_TEST(P2pBasicPortAllocator, CostlyNetworksDisabled) {
  struct Iface {
    IpAddress ip;
    const char* name;
    AdapterType type;
  };
  const IpAddress wifi(0x12345600u), cell(0x12345601u), unknown1(0x12345602u), unknown2(0x12345603u);
  const IpAddress wifi_link_local = Ip("169.254.0.1");
  struct Case {
    std::vector<Iface> ifaces;
    std::vector<IpAddress> expected;
  };
  const std::vector<Case> cases = {
      // WifiUsedInsteadOfCellWhenCostlyNetworksDisabled.
      {{{wifi, "test_wlan0", ADAPTER_TYPE_WIFI}, {cell, "test_cell0", ADAPTER_TYPE_CELLULAR}}, {wifi}},
      // UnknownInterfaceUsedInsteadOfCellWhenCostlyNetworksDisabled.
      {{{cell, "test_cell0", ADAPTER_TYPE_CELLULAR},
        {unknown1, "test_unknown0", ADAPTER_TYPE_UNKNOWN},
        {unknown2, "test_unknown1", ADAPTER_TYPE_UNKNOWN}},
       {unknown1, unknown2}},
      // WifiUsedInsteadOfUnknownOrCellWhenCostlyNetworksDisabled.
      {{{wifi, "test_wlan0", ADAPTER_TYPE_WIFI},
        {cell, "test_cell0", ADAPTER_TYPE_CELLULAR},
        {unknown1, "test_unknown0", ADAPTER_TYPE_UNKNOWN},
        {unknown2, "test_unknown1", ADAPTER_TYPE_UNKNOWN}},
       {wifi}},
      // CellUsedWhenCostlyNetworksDisabledButThereAreNoOtherInterfaces.
      {{{cell, "test_cell0", ADAPTER_TYPE_CELLULAR}}, {cell}},
      // CellNotRemovedWhenCostlyNetworksDisabledAndWifiIsLinkLocal.
      {{{wifi_link_local, "test_wlan0", ADAPTER_TYPE_WIFI}, {cell, "test_cell0", ADAPTER_TYPE_CELLULAR}},
       {wifi_link_local, cell}},
      // CellRemovedWhenCostlyNetworksDisabledAndBothWifisPresent.
      {{{wifi, "test_wlan0", ADAPTER_TYPE_WIFI},
        {wifi_link_local, "test_wlan1", ADAPTER_TYPE_WIFI},
        {cell, "test_cell0", ADAPTER_TYPE_CELLULAR}},
       {wifi, wifi_link_local}},
  };
  for (const Case& c : cases) {
    Fixture f;
    for (const Iface& i : c.ifaces) f.AddInterface(i.ip, i.name, i.type);
    f.allocator->set_flags(PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY | PORTALLOCATOR_DISABLE_TCP |
                           PORTALLOCATOR_DISABLE_COSTLY_NETWORKS);
    f.CreateSession();
    f.session->StartGettingPorts(f.now);
    SM_ASSERT_TRUE(f.RunUntilDone());
    SM_EXPECT_EQ(f.candidates.size(), c.expected.size());
    for (const IpAddress& ip : c.expected) SM_EXPECT_TRUE(f.HasCandidate(IceCandidateType::kHost, "udp", ip));
  }
}

// EthernetIsNotFilteredOutWhenCostlyNetworksDisabledAndVpnPresent: the VPN
// (cost 50 + its unknown underlying type) is the costly one.
SM_TEST(P2pBasicPortAllocator, EthernetKeptWhenCostlyNetworksDisabledAndVpnPresent) {
  Fixture f(Fixture::kFake, /*with_stun_server=*/false);
  f.AddInterface(kClientIp, "eth0", ADAPTER_TYPE_ETHERNET);
  f.AddInterface(kClientIp2, "tap0", ADAPTER_TYPE_VPN);
  f.allocator->set_flags(PORTALLOCATOR_DISABLE_COSTLY_NETWORKS | PORTALLOCATOR_DISABLE_RELAY |
                         PORTALLOCATOR_DISABLE_TCP);
  f.CreateSession();
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  // libwebrtc: 2 ports on eth0 (UDP and STUN); without a STUN server here, 1.
  SM_ASSERT_EQ(f.ports.size(), size_t{1});
  SM_EXPECT_EQ(f.ports[0]->network()->name(), std::string("eth0"));
}

SM_TEST(P2pBasicPortAllocator, MaxIpv6NetworksLimit) {
  {
    // MaxIpv6NetworksLimitEnforced.
    Fixture f;
    f.allocator->set_max_ipv6_networks(2);
    f.AddInterface(kClientIPv6, "eth0", ADAPTER_TYPE_ETHERNET);
    f.AddInterface(kClientIPv6_2, "eth1", ADAPTER_TYPE_ETHERNET);
    f.AddInterface(kClientIPv6_3, "eth2", ADAPTER_TYPE_ETHERNET);
    f.allocator->set_flags(PORTALLOCATOR_ENABLE_IPV6 | PORTALLOCATOR_DISABLE_TCP | PORTALLOCATOR_DISABLE_STUN |
                           PORTALLOCATOR_DISABLE_RELAY);
    f.CreateSession();
    f.session->StartGettingPorts(f.now);
    SM_ASSERT_TRUE(f.RunUntilDone());
    SM_EXPECT_EQ(f.candidates.size(), size_t{2});
    SM_EXPECT_TRUE(f.HasCandidate(IceCandidateType::kHost, "udp", kClientIPv6));
    SM_EXPECT_TRUE(f.HasCandidate(IceCandidateType::kHost, "udp", kClientIPv6_2));
  }
  {
    // MaxIpv6NetworksLimitDoesNotImpactIpv4Networks.
    Fixture f;
    f.allocator->set_max_ipv6_networks(1);
    f.AddInterface(kClientIPv6, "eth0", ADAPTER_TYPE_ETHERNET);
    f.AddInterface(kClientIPv6_2, "eth1", ADAPTER_TYPE_ETHERNET);
    f.AddInterface(kClientIp, "eth2", ADAPTER_TYPE_ETHERNET);
    f.AddInterface(kClientIp2, "eth3", ADAPTER_TYPE_ETHERNET);
    f.allocator->set_flags(PORTALLOCATOR_ENABLE_IPV6 | PORTALLOCATOR_DISABLE_TCP | PORTALLOCATOR_DISABLE_STUN |
                           PORTALLOCATOR_DISABLE_RELAY);
    f.CreateSession();
    f.session->StartGettingPorts(f.now);
    SM_ASSERT_TRUE(f.RunUntilDone());
    SM_EXPECT_EQ(f.candidates.size(), size_t{3});
    SM_EXPECT_TRUE(f.HasCandidate(IceCandidateType::kHost, "udp", kClientIPv6));
    SM_EXPECT_TRUE(f.HasCandidate(IceCandidateType::kHost, "udp", kClientIp));
    SM_EXPECT_TRUE(f.HasCandidate(IceCandidateType::kHost, "udp", kClientIp2));
  }
}

// IPv6 only with PORTALLOCATOR_ENABLE_IPV6 (TestEnableIPv6Addresses, less
// its two TCP candidates).
SM_TEST(P2pBasicPortAllocator, EnableIPv6Addresses) {
  for (bool enable : {false, true}) {
    Fixture f(Fixture::kFake, /*with_stun_server=*/false);
    f.allocator->set_flags(PORTALLOCATOR_DISABLE_RELAY | PORTALLOCATOR_ENABLE_SHARED_SOCKET |
                           (enable ? PORTALLOCATOR_ENABLE_IPV6 : 0));
    f.AddInterface(kClientIPv6);
    f.AddInterface(kClientIp);
    f.CreateSession();
    f.session->StartGettingPorts(f.now);
    SM_ASSERT_TRUE(f.RunUntilDone());
    SM_EXPECT_EQ(f.ports.size(), size_t{enable ? 2u : 1u});
    SM_EXPECT_EQ(f.candidates.size(), size_t{enable ? 2u : 1u});
    SM_EXPECT_EQ(f.HasCandidate(IceCandidateType::kHost, "udp", kClientIPv6), enable);
    SM_EXPECT_TRUE(f.HasCandidate(IceCandidateType::kHost, "udp", kClientIp));
  }
}

SM_TEST(P2pBasicPortAllocator, LoopbackNetworkInterface) {
  Fixture f;
  f.AddInterface(kLoopbackIp, "test_loopback", ADAPTER_TYPE_LOOPBACK);
  f.allocator->SetNetworkIgnoreMask(0);
  f.CreateSession();
  f.session->set_flags(PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY | PORTALLOCATOR_DISABLE_TCP);
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_EXPECT_EQ(f.candidates.size(), size_t{1});
}

// TestGetAllPortsWithMinimumStepDelay: host and srflx over UDP (libwebrtc
// also a TCP host candidate: 3 candidates, 3 ports). No NAT, so the srflx
// address is the host's.
SM_TEST(P2pBasicPortAllocator, GetAllPortsWithMinimumStepDelay) {
  Fixture f(Fixture::kPublic);
  f.AddInterface(kClientIp);
  f.CreateSession();
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_EXPECT_EQ(f.candidates.size(), size_t{2});
  SM_EXPECT_EQ(f.ports.size(), size_t{2});
  SM_EXPECT_TRUE(f.HasCandidate(IceCandidateType::kHost, "udp", kClientIp));
  SM_EXPECT_TRUE(f.HasCandidate(IceCandidateType::kSrflx, "udp", kClientIp));
}

// TestGetAllPortsWithOneSecondStepDelay: the UDP phase at once, the relay
// phase a second later, TCP a second after that, then done.
SM_TEST(P2pBasicPortAllocator, GetAllPortsWithOneSecondStepDelay) {
  Fixture f(Fixture::kPublic);
  f.AddInterface(kClientIp);
  f.allocator->set_step_delay(kDefaultStepDelay);
  f.CreateSession();
  const Timestamp start = f.now;
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.candidates.size() == 2; }));
  SM_EXPECT_EQ(f.ports.size(), size_t{2});
  SM_EXPECT_TRUE(f.now - start < TimeDelta::Millis(100));  // One STUN round trip.
  SM_EXPECT_FALSE(f.done);
  SM_ASSERT_TRUE(f.RunUntilDone());
  // Done when the last (TCP) phase ran, two step delays in.
  SM_EXPECT_EQ((f.now - start).ms(), 2000);
  SM_EXPECT_EQ(f.candidates.size(), size_t{2});
  f.session->StopGettingPorts(f.now);
}

// ConcurrentSessionsDoNotDuplicatePorts: each session has its own host port.
SM_TEST(P2pBasicPortAllocator, ConcurrentSessionsDoNotDuplicatePorts) {
  Fixture f(Fixture::kFake, /*with_stun_server=*/false);
  f.AddInterface(kClientIp);
  std::vector<std::unique_ptr<PortAllocatorSession>> sessions;
  for (int i = 0; i < 10; ++i) {
    sessions.push_back(f.allocator->CreateSession("session", ICE_CANDIDATE_COMPONENT_RTP, kIceUfrag0, kIcePwd0));
    sessions.back()->StartGettingPorts(f.now);
    for (auto& s : sessions) s->Process(f.now);
  }
  for (int step = 0; step < 10; ++step) {
    f.now = f.now + TimeDelta::Millis(kMinimumStepDelay);
    for (auto& s : sessions) s->Process(f.now);
  }
  for (const auto& s : sessions) {
    SM_EXPECT_TRUE(s->CandidatesAllocationDone());
    const std::vector<Port*> ports = s->ReadyPorts();
    SM_EXPECT_EQ(std::count_if(ports.begin(), ports.end(),
                               [](Port* p) {
                                 return p->Type() == IceCandidateType::kHost && p->GetProtocol() == PROTO_UDP;
                               }),
                 std::ptrdiff_t{1});
  }
}

// TestSameNetworkDownAndUpWhenSessionNotStopped: the network goes away (its
// ports are pruned and its candidates removed), comes back, and is gathered
// on again (ICE regathering), as continual gathering does.
SM_TEST(P2pBasicPortAllocator, SameNetworkDownAndUpWhenSessionNotStopped) {
  Fixture f(Fixture::kPublic);
  f.AddInterface(kClientIp, "test_net0");
  f.CreateSession();
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_EXPECT_EQ(f.candidates.size(), size_t{2});
  SM_EXPECT_EQ(f.ports.size(), size_t{2});
  f.done = false;
  f.candidates.clear();
  // No sockets while the network is down: the any-address fallback gathers nothing.
  f.gate->enabled = false;
  f.network_manager.RemoveInterface(kClientIp);
  f.Advance(TimeDelta::Seconds(1));
  SM_EXPECT_EQ(f.candidates.size(), size_t{0});
  SM_EXPECT_EQ(f.ports.size(), size_t{0});  // Pruned.
  SM_EXPECT_EQ(f.removed.size(), size_t{2});
  f.gate->enabled = true;
  f.done = false;
  f.AddInterface(kClientIp, "test_net0");
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_EXPECT_EQ(f.candidates.size(), size_t{2});
  SM_EXPECT_EQ(f.ports.size(), size_t{2});
  SM_EXPECT_EQ(f.regathering, 2);  // Down, then up.
}

// TestSameNetworkDownAndUpWhenSessionStopped: a stopped session gathers no more.
SM_TEST(P2pBasicPortAllocator, SameNetworkDownAndUpWhenSessionStopped) {
  Fixture f(Fixture::kPublic);
  f.AddInterface(kClientIp, "test_net0");
  f.CreateSession();
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_EXPECT_EQ(f.candidates.size(), size_t{2});
  f.session->StopGettingPorts(f.now);
  f.candidates.clear();
  f.ports.clear();
  f.network_manager.RemoveInterface(kClientIp);
  f.Advance(TimeDelta::Seconds(1));
  SM_EXPECT_EQ(f.candidates.size(), size_t{0});
  f.AddInterface(kClientIp, "test_net0");
  f.Advance(TimeDelta::Seconds(1));
  SM_EXPECT_EQ(f.candidates.size(), size_t{0});
  SM_EXPECT_EQ(f.ports.size(), size_t{0});
}

// TestStopGetAllPorts and TestStopGettingPorts: stopping signals done, and a
// new network after that is not gathered on.
SM_TEST(P2pBasicPortAllocator, StopGettingPorts) {
  Fixture f(Fixture::kPublic);
  f.AddInterface(kClientIp);
  f.allocator->set_step_delay(kDefaultStepDelay);
  f.CreateSession();
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.candidates.size() == 2; }));
  SM_EXPECT_EQ(f.ports.size(), size_t{2});
  f.session->StopGettingPorts(f.now);
  SM_EXPECT_TRUE(f.session->IsStopped());
  SM_ASSERT_TRUE(f.RunUntilDone());
  f.candidates.clear();
  f.ports.clear();
  f.done = false;
  f.network_manager.AddInterface(kClientIp2);
  f.Advance(TimeDelta::Seconds(1));
  SM_EXPECT_EQ(f.candidates.size(), size_t{0});
  SM_EXPECT_EQ(f.ports.size(), size_t{0});
}

// TestClearGettingPorts: a cleared session gathers on a new network.
SM_TEST(P2pBasicPortAllocator, ClearGettingPorts) {
  Fixture f(Fixture::kFake, /*with_stun_server=*/false);
  f.AddInterface(kClientIp);
  f.allocator->set_step_delay(kDefaultStepDelay);
  f.CreateSession();
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.candidates.size() == 1; }));
  f.session->ClearGettingPorts(f.now);
  SM_EXPECT_TRUE(f.session->IsCleared());
  SM_ASSERT_TRUE(f.RunUntilDone());
  f.candidates.clear();
  f.ports.clear();
  f.done = false;
  f.network_manager.AddInterface(kClientIp2);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.candidates.size() == 1; }));
  SM_EXPECT_TRUE(f.candidates[0].address().ipaddr() == kClientIp2);
  SM_EXPECT_EQ(f.ports.size(), size_t{1});
  SM_ASSERT_TRUE(f.RunUntilDone());
}

// TestGetAllPortsPortRange.
SM_TEST(P2pBasicPortAllocator, GetAllPortsPortRange) {
  Fixture f(Fixture::kFake, /*with_stun_server=*/false);
  f.AddInterface(kClientIp);
  SM_EXPECT_FALSE(f.allocator->SetPortRange(kMaxPort, kMinPort));
  SM_EXPECT_TRUE(f.allocator->SetPortRange(0, 0));
  SM_EXPECT_TRUE(f.allocator->SetPortRange(kMinPort, kMaxPort));
  f.CreateSession();
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_ASSERT_EQ(f.candidates.size(), size_t{1});
  for (const Candidate& c : f.candidates) {
    SM_EXPECT_TRUE(c.address().port() >= kMinPort && c.address().port() <= kMaxPort);
  }
}

// TestGetAllPortsNoAdapters: with no networks, gather on the any address.
// Its UDP host candidate is the any address, which is not signalled, but
// the port is ready (pairable on a shared socket... here a STUN port on its
// own socket gives the srflx candidate). libwebrtc also makes TCP and TURN
// ports (4 ports, 3 candidates).
SM_TEST(P2pBasicPortAllocator, GetAllPortsNoAdapters) {
  Fixture f(Fixture::kFake, /*with_stun_server=*/false);
  f.CreateSession();
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  // No host candidate for 0.0.0.0, and no default local address to report.
  SM_EXPECT_EQ(f.candidates.size(), size_t{0});
  const auto* bpas = static_cast<BasicPortAllocatorSession*>(f.session.get());
  // The IPv4 any network (the IPv6 one needs PORTALLOCATOR_ENABLE_IPV6).
  SM_EXPECT_EQ(bpas->sequence_count(), size_t{1});
}

// TestDisableAllPorts.
SM_TEST(P2pBasicPortAllocator, DisableAllPorts) {
  Fixture f;
  f.AddInterface(kClientIp);
  f.CreateSession();
  f.session->set_flags(PORTALLOCATOR_DISABLE_UDP | PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY |
                       PORTALLOCATOR_DISABLE_TCP);
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_EXPECT_EQ(f.candidates.size(), size_t{0});
}

// TestGetAllPortsNoUdpSockets: no UDP socket can be made (libwebrtc then
// has only its TCP candidate); done with none.
SM_TEST(P2pBasicPortAllocator, GetAllPortsNoUdpSockets) {
  Fixture f;
  f.AddInterface(kClientIp);
  f.fake.udp_enabled = false;
  f.CreateSession();
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_EXPECT_EQ(f.candidates.size(), size_t{0});
  SM_EXPECT_EQ(f.ports.size(), size_t{0});
}

// TestCandidatePriorityOfMultipleInterfaces: the networks' preferences
// differ, so the priorities do.
SM_TEST(P2pBasicPortAllocator, CandidatePriorityOfMultipleInterfaces) {
  Fixture f;
  f.AddInterface(kClientIp);
  f.AddInterface(kClientIp2);
  f.allocator->set_flags(PORTALLOCATOR_DISABLE_TCP | PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY);
  f.CreateSession();
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_ASSERT_EQ(f.candidates.size(), size_t{2});
  SM_EXPECT_EQ(f.ports.size(), size_t{2});
  SM_EXPECT_NE(f.candidates[0].priority(), f.candidates[1].priority());
}

// TestSessionUsesOwnCandidateFilter: the allocator's filter applies to new
// sessions only.
SM_TEST(P2pBasicPortAllocator, SessionUsesOwnCandidateFilter) {
  Fixture f(Fixture::kPublic);
  f.AddInterface(kClientIp);
  f.CreateSession();
  f.allocator->SetCandidateFilter(CF_RELAY);
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_EXPECT_EQ(f.candidates.size(), size_t{2});  // libwebrtc 3, with TCP.
  SM_EXPECT_EQ(f.ports.size(), size_t{2});
}

// TestCandidateFilterWithHostOnly (shared socket: one UDP port).
SM_TEST(P2pBasicPortAllocator, CandidateFilterWithHostOnly) {
  Fixture f(Fixture::kPublic);
  f.AddInterface(kClientIp);
  f.allocator->set_flags(PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  f.allocator->SetCandidateFilter(CF_HOST);
  f.CreateSession();
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_EXPECT_EQ(f.candidates.size(), size_t{1});  // libwebrtc 2: UDP and TCP host.
  SM_EXPECT_EQ(f.ports.size(), size_t{1});
  for (const Candidate& c : f.candidates) SM_EXPECT_TRUE(c.is_local());
}

// TestCandidateFilterWithReflexiveOnly: behind a NAT, only the srflx
// candidate, without its related address.
SM_TEST(P2pBasicPortAllocator, CandidateFilterWithReflexiveOnly) {
  Fixture f(Fixture::kNat);
  f.AddInterface(kPrivateIp);
  f.allocator->set_flags(PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  f.allocator->SetCandidateFilter(CF_REFLEXIVE);
  f.CreateSession();
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_ASSERT_EQ(f.candidates.size(), size_t{1});
  SM_EXPECT_EQ(f.ports.size(), size_t{1});
  SM_EXPECT_TRUE(f.candidates[0].is_stun());
  SM_EXPECT_TRUE(f.candidates[0].address().ipaddr() == kNatIp);
  SM_EXPECT_TRUE(f.candidates[0].related_address() == SocketAddress(GetAnyIp(IpFamily::kV4), 0));
}

// TestCandidateFilterWithReflexiveOnlyAndNoNAT: a public host address passes
// a reflexive-only filter (no srflx candidate equals it on a shared socket).
SM_TEST(P2pBasicPortAllocator, CandidateFilterWithReflexiveOnlyAndNoNat) {
  Fixture f(Fixture::kPublic);
  f.AddInterface(kClientIp);
  f.allocator->set_flags(PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  f.allocator->SetCandidateFilter(CF_REFLEXIVE);
  f.CreateSession();
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_EXPECT_EQ(f.candidates.size(), size_t{1});  // libwebrtc 2: UDP and TCP host.
  SM_EXPECT_EQ(f.ports.size(), size_t{1});
  for (const Candidate& c : f.candidates) SM_EXPECT_TRUE(c.is_local());
}

// TestSharedSocketWithoutNat: one UDP port, one candidate (the srflx address
// equals the host's on a shared socket).
SM_TEST(P2pBasicPortAllocator, SharedSocketWithoutNat) {
  Fixture f(Fixture::kPublic);
  f.AddInterface(kClientIp);
  f.allocator->set_flags(f.allocator->flags() | PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  f.CreateSession();
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_EXPECT_EQ(f.candidates.size(), size_t{1});  // libwebrtc 2, with TCP.
  SM_EXPECT_EQ(f.ports.size(), size_t{1});
  SM_EXPECT_TRUE(f.HasCandidate(IceCandidateType::kHost, "udp", kClientIp));
}

// TestSharedSocketWithNat: host and srflx from the one UDP port.
SM_TEST(P2pBasicPortAllocator, SharedSocketWithNat) {
  Fixture f(Fixture::kNat);
  f.AddInterface(kPrivateIp);
  f.allocator->set_flags(f.allocator->flags() | PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  f.CreateSession();
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_EXPECT_EQ(f.candidates.size(), size_t{2});  // libwebrtc 3, with TCP.
  SM_EXPECT_EQ(f.ports.size(), size_t{1});       // libwebrtc 2, with TCP.
  SM_EXPECT_TRUE(f.HasCandidate(IceCandidateType::kHost, "udp", kPrivateIp));
  SM_EXPECT_TRUE(f.HasCandidate(IceCandidateType::kSrflx, "udp", kNatIp));
  const Candidate& srflx = *std::find_if(f.candidates.begin(), f.candidates.end(),
                                         [](const Candidate& c) { return c.is_stun(); });
  // The related address is the host candidate's.
  SM_EXPECT_TRUE(srflx.related_address().ipaddr() == kPrivateIp);
}

// Without a shared socket a NAT gives a UDP port and a STUN port.
SM_TEST(P2pBasicPortAllocator, NonSharedSocketWithNat) {
  Fixture f(Fixture::kNat);
  f.AddInterface(kPrivateIp);
  f.CreateSession();
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_EXPECT_EQ(f.candidates.size(), size_t{2});
  SM_EXPECT_EQ(f.ports.size(), size_t{2});
  SM_EXPECT_TRUE(f.HasCandidate(IceCandidateType::kHost, "udp", kPrivateIp));
  SM_EXPECT_TRUE(f.HasCandidate(IceCandidateType::kSrflx, "udp", kNatIp));
}

// TestNetworkPermissionBlocked: gather on the any address, reporting the
// default local address.
SM_TEST(P2pBasicPortAllocator, NetworkPermissionBlocked) {
  Fixture f(Fixture::kFake, /*with_stun_server=*/false);
  f.network_manager.set_default_local_addresses(kPrivateIp, IpAddress());
  f.network_manager.set_enumeration_permission(NetworkManager::ENUMERATION_BLOCKED);
  f.allocator->set_flags(f.allocator->flags() | PORTALLOCATOR_DISABLE_RELAY | PORTALLOCATOR_DISABLE_TCP |
                         PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  SM_EXPECT_EQ(f.allocator->flags() & PORTALLOCATOR_DISABLE_ADAPTER_ENUMERATION, 0u);
  f.CreateSession();
  SM_EXPECT_EQ(f.session->flags() & PORTALLOCATOR_DISABLE_ADAPTER_ENUMERATION, 0u);
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.ports.size() == 1; }));
  SM_EXPECT_EQ(f.candidates.size(), size_t{1});
  SM_EXPECT_TRUE(f.HasCandidate(IceCandidateType::kHost, "udp", kPrivateIp));
  SM_EXPECT_NE(f.session->flags() & PORTALLOCATOR_DISABLE_ADAPTER_ENUMERATION, 0u);
}

// TestSetCandidateFilterAfterCandidatesGathered (no relay candidates: none
// survive CF_RELAY), and the candidates held back by a filter resurfacing
// when it widens.
SM_TEST(P2pBasicPortAllocator, SetCandidateFilterAfterCandidatesGathered) {
  Fixture f(Fixture::kNat);
  f.AddInterface(kPrivateIp);
  f.allocator->SetConfiguration(f.now, f.allocator->stun_servers(), {}, 1, NO_PRUNE);
  const PortAllocatorSession* peeked = f.allocator->GetPooledSession();
  SM_ASSERT_TRUE(peeked != nullptr);
  SM_ASSERT_TRUE(f.RunUntil([&] { return peeked->CandidatesAllocationDone(); }));
  const size_t initial_candidates = peeked->ReadyCandidates().size();
  const size_t initial_ports = peeked->ReadyPorts().size();
  SM_EXPECT_EQ(initial_candidates, size_t{2});
  SM_EXPECT_EQ(initial_ports, size_t{2});
  f.allocator->SetCandidateFilter(CF_RELAY);
  f.session = f.allocator->TakePooledSession(kContentName, 1, kIceUfrag0, kIcePwd0);
  SM_ASSERT_TRUE(f.session != nullptr);
  SM_EXPECT_EQ(f.session->ReadyCandidates().size(), size_t{0});
  SM_EXPECT_EQ(f.session->ReadyPorts().size(), size_t{0});
  // The ports took the transport's credentials.
  for (Port* p : static_cast<BasicPortAllocatorSession*>(f.session.get())->AllPortsForTesting()) {
    SM_EXPECT_EQ(p->username_fragment(), std::string(kIceUfrag0));
    SM_EXPECT_EQ(p->content_name(), std::string(kContentName));
  }
}

// SetCandidateFilter widening after gathering (the filter-change half of
// TestSetCandidateFilterAfterCandidatesGathered's neighbours): candidates
// held back surface while the session still gathers.
SM_TEST(P2pBasicPortAllocator, WiderFilterResurfacesCandidates) {
  Fixture f(Fixture::kNat);
  f.AddInterface(kPrivateIp);
  f.allocator->SetCandidateFilter(CF_HOST);
  f.allocator->set_flags(PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  f.CreateSession();
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_ASSERT_EQ(f.candidates.size(), size_t{1});
  SM_EXPECT_TRUE(f.candidates[0].is_local());
  f.session->SetCandidateFilter(CF_ALL);
  SM_ASSERT_EQ(f.candidates.size(), size_t{2});
  SM_EXPECT_TRUE(f.candidates[1].is_stun());
  SM_EXPECT_EQ(f.session->ReadyCandidates().size(), size_t{2});
}

// SetStunKeepaliveIntervalForPorts.
SM_TEST(P2pBasicPortAllocator, SetStunKeepaliveIntervalForPorts) {
  Fixture f(Fixture::kNat);
  f.AddInterface(kPrivateIp);
  f.allocator->SetConfiguration(f.now, f.allocator->stun_servers(), {}, 1, NO_PRUNE, 123);
  const PortAllocatorSession* pooled = f.allocator->GetPooledSession();
  SM_ASSERT_TRUE(pooled != nullptr);
  SM_ASSERT_TRUE(f.RunUntil([&] { return pooled->CandidatesAllocationDone(); }));
  const auto ports = pooled->ReadyPorts();
  SM_EXPECT_EQ(ports.size(), size_t{2});
  for (Port* p : ports) {
    SM_EXPECT_TRUE(static_cast<const UDPPort*>(p)->stun_keepalive_delay() == TimeDelta::Millis(123));
  }
  // A new interval reaches the ready ports.
  f.allocator->SetConfiguration(f.now, f.allocator->stun_servers(), {}, 1, NO_PRUNE, 456);
  for (Port* p : pooled->ReadyPorts()) {
    SM_EXPECT_TRUE(static_cast<const UDPPort*>(p)->stun_keepalive_delay() == TimeDelta::Millis(456));
  }
}

// TestUseTurnServerAsStunSever / TestDoNotUseTurnServerAsStunSever.
SM_TEST(P2pBasicPortAllocator, TurnServerAsStunServer) {
  const SocketAddress turn_udp(Ip("99.99.99.4"), 3478);
  const SocketAddress turn_tcp(Ip("99.99.99.5"), 3478);
  ServerAddresses stun = {kStunAddr};
  PortConfiguration config(stun, "", "");
  config.AddRelay(RelayServerConfig(turn_udp, "test", "test", PROTO_UDP));
  config.AddRelay(RelayServerConfig(turn_tcp, "test", "test", PROTO_TCP));
  ServerAddresses expected = {kStunAddr, turn_udp};
  SM_EXPECT_TRUE(config.StunServers() == expected);
  PortConfiguration disabled(stun, "", "");
  disabled.use_turn_server_as_stun_server_disabled = true;
  disabled.AddRelay(RelayServerConfig(turn_udp, "test", "test", PROTO_UDP));
  SM_EXPECT_TRUE(disabled.StunServers() == stun);
}

// The SelectIPv6Networks cases.
SM_TEST(P2pBasicPortAllocator, SelectIPv6Networks) {
  Network wifi1("wifi1", kClientIPv6, 64, ADAPTER_TYPE_WIFI);
  Network ethe1("ethe1", kClientIPv6_2, 64, ADAPTER_TYPE_ETHERNET);
  Network wifi2("wifi2", kClientIPv6_3, 64, ADAPTER_TYPE_WIFI);
  Network ethe2("ethe2", kClientIPv6_3, 64, ADAPTER_TYPE_ETHERNET);
  Network unknown1("unknown1", kClientIPv6_2, 64, ADAPTER_TYPE_UNKNOWN);
  Network cell1("cell1", kClientIPv6_3, 64, ADAPTER_TYPE_CELLULAR_4G);
  auto has = [](const std::vector<const Network*>& v, const Network& n) {
    return std::find(v.begin(), v.end(), &n) != v.end();
  };
  {
    // TwoIPv6AreSelectedBecauseOfMaxIpv6Limit.
    std::vector<const Network*> networks = {&wifi1, &ethe1, &wifi2};
    SM_EXPECT_EQ(BasicPortAllocatorSession::SelectIPv6Networks(networks, 2).size(), size_t{2});
  }
  {
    // AllIPv6AreSelected.
    std::vector<const Network*> networks = {&wifi1, &ethe1};
    SM_EXPECT_EQ(BasicPortAllocatorSession::SelectIPv6Networks(networks, 3).size(), size_t{2});
  }
  {
    // TwoIPv6WifiAreSelectedIfThereAreTwo (the max-4 pick of five).
    std::vector<const Network*> networks = {&wifi1, &ethe1, &ethe2, &unknown1, &cell1};
    networks = BasicPortAllocatorSession::SelectIPv6Networks(networks, 4);
    SM_EXPECT_EQ(networks.size(), size_t{4});
    SM_EXPECT_TRUE(has(networks, wifi1));
    SM_EXPECT_TRUE(has(networks, ethe1));
    SM_EXPECT_TRUE(has(networks, cell1));
    SM_EXPECT_TRUE(has(networks, unknown1));
  }
  {
    // IPv6EthernetHasHigherPriorityThanWifi.
    std::vector<const Network*> networks = {&wifi1, &ethe1, &wifi2};
    networks = BasicPortAllocatorSession::SelectIPv6Networks(networks, 1);
    SM_EXPECT_EQ(networks.size(), size_t{1});
    SM_EXPECT_TRUE(has(networks, ethe1));
  }
  {
    // IPv6WithSameTypeAreSelectedIfNoOtherOption.
    std::vector<const Network*> networks = {&wifi1, &wifi2};
    networks = BasicPortAllocatorSession::SelectIPv6Networks(networks, 1);
    SM_EXPECT_EQ(networks.size(), size_t{1});
    SM_EXPECT_TRUE(has(networks, wifi1));
  }
}

// The STUN retransmit attribute flag reaches the ports, and the session's
// component and generation do.
SM_TEST(P2pBasicPortAllocator, PortsTakeSessionSettings) {
  Fixture f(Fixture::kFake, /*with_stun_server=*/false);
  f.AddInterface(kClientIp);
  f.allocator->set_flags(PORTALLOCATOR_ENABLE_STUN_RETRANSMIT_ATTRIBUTE);
  f.CreateSession();
  f.session->set_generation(3);
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_ASSERT_EQ(f.ports.size(), size_t{1});
  SM_EXPECT_TRUE(f.ports[0]->send_retransmit_count_attribute());
  SM_EXPECT_EQ(f.ports[0]->generation(), 3u);
  SM_EXPECT_EQ(f.ports[0]->component(), ICE_CANDIDATE_COMPONENT_RTP);
  SM_EXPECT_EQ(f.ports[0]->content_name(), std::string("test content"));
}

// RegatherOnFailedNetworks: a network whose ports have no connection has
// failed: its ports are pruned and it is gathered on again.
SM_TEST(P2pBasicPortAllocator, RegatherOnFailedNetworks) {
  Fixture f(Fixture::kFake, /*with_stun_server=*/false);
  f.AddInterface(kClientIp);
  f.CreateSession();
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_ASSERT_EQ(f.ports.size(), size_t{1});
  Port* first = f.ports[0];
  f.candidates.clear();
  f.session->RegatherOnFailedNetworks(f.now);
  SM_EXPECT_EQ(f.regathering, 1);
  SM_EXPECT_EQ(f.removed.size(), size_t{1});
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.candidates.size() == 1; }));
  SM_ASSERT_EQ(f.ports.size(), size_t{1});
  SM_EXPECT_TRUE(f.ports[0] != first);
}

// RegatherOnFailedNetworks as BasicPortAllocatorSession::GetFailedNetworks: a
// network interface with a connection on any of its ports has not failed, so
// nothing is regathered; the network without one is.
SM_TEST(P2pBasicPortAllocator, RegatherOnFailedNetworksKeepsNetworksWithConnections) {
  Fixture f(Fixture::kFake, /*with_stun_server=*/false);
  f.AddInterface(IpAddress(0x12345600u), "test_eth0", ADAPTER_TYPE_ETHERNET);
  f.AddInterface(IpAddress(0x12345601u), "test_wlan0", ADAPTER_TYPE_WIFI);
  f.CreateSession();
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_ASSERT_EQ(f.ports.size(), size_t{2});
  Port* with_connection = f.ports[0];
  Candidate remote;
  remote.set_address(*SocketAddress::FromString("1.2.3.4:5000"));
  remote.set_username("rfrag");
  remote.set_password("rpassword_rpassword_");
  remote.set_component(ICE_CANDIDATE_COMPONENT_RTP);
  remote.set_protocol(UDP_PROTOCOL_NAME);
  SM_ASSERT_TRUE(with_connection->CreateConnection(f.now, remote, Port::ORIGIN_MESSAGE) != nullptr);
  f.candidates.clear();
  f.session->RegatherOnFailedNetworks(f.now);
  SM_EXPECT_EQ(f.regathering, 1);
  // Only the other network's candidate is removed.
  SM_ASSERT_EQ(f.removed.size(), size_t{1});
  SM_EXPECT_TRUE(f.removed[0].network_name() != with_connection->network()->name());

  // With a connection on every network, nothing has failed.
  f.regathering = 0;
  f.removed.clear();
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.candidates.size() == 1; }));
  for (Port* port : f.session->ReadyPorts()) {
    if (port->connections().empty()) port->CreateConnection(f.now, remote, Port::ORIGIN_MESSAGE);
  }
  f.session->RegatherOnFailedNetworks(f.now);
  SM_EXPECT_EQ(f.regathering, 0);
  SM_EXPECT_EQ(f.removed.size(), size_t{0});
}

// A pruned port with no connections is destroyed and deleted (Port's
// destroy check), leaving the session.
SM_TEST(P2pBasicPortAllocator, PrunedPortsAreDestroyed) {
  Fixture f(Fixture::kFake, /*with_stun_server=*/false);
  f.AddInterface(kClientIp);
  f.CreateSession();
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  auto* bpas = static_cast<BasicPortAllocatorSession*>(f.session.get());
  SM_EXPECT_EQ(bpas->AllPortsForTesting().size(), size_t{1});
  f.session->PruneAllPorts(f.now);
  f.Advance(TimeDelta::Millis(1));
  SM_EXPECT_EQ(bpas->AllPortsForTesting().size(), size_t{0});
  SM_EXPECT_EQ(f.session->ReadyPorts().size(), size_t{0});
}

// ------------------------------------------------------- NetworkManagerBase

SM_TEST(P2pNetworkManager, MergeSortAndPreferences) {
  StaticNetworkManager nm;
  int changes = 0;
  nm.SubscribeNetworksChanged(&changes, [&] { ++changes; });
  nm.AddInterface(Ip("1.1.1.1"), "wlan0", ADAPTER_TYPE_WIFI);
  SM_EXPECT_EQ(changes, 0);  // Not started.
  nm.StartUpdating();
  SM_EXPECT_EQ(changes, 1);
  nm.AddInterface(Ip("2.2.2.2"), "eth0", ADAPTER_TYPE_ETHERNET);
  SM_EXPECT_EQ(changes, 2);
  const auto networks = nm.GetNetworks();
  SM_ASSERT_EQ(networks.size(), size_t{2});
  // Sorted by type: Ethernet first, preference 127, then 126.
  SM_EXPECT_EQ(networks[0]->name(), std::string("eth0"));
  SM_EXPECT_EQ(networks[0]->preference(), 127);
  SM_EXPECT_EQ(networks[1]->preference(), 126);
  SM_EXPECT_TRUE(networks[0]->prefix() == Ip("2.2.2.0"));
  SM_EXPECT_EQ(networks[0]->prefix_length(), 24);
  // Ids in creation order, from 1.
  SM_EXPECT_EQ(networks[1]->id(), 1);
  SM_EXPECT_EQ(networks[0]->id(), 2);
  // A second address of the same name and prefix joins the network.
  nm.AddInterface(Ip("2.2.2.3"), "eth0", ADAPTER_TYPE_ETHERNET);
  SM_EXPECT_EQ(nm.GetNetworks().size(), size_t{2});
  SM_EXPECT_EQ(nm.GetNetworks()[0]->GetIps().size(), size_t{2});
  // Removing keeps the Network object, inactive; adding it back reuses it.
  const Network* wlan = networks[1];
  nm.RemoveInterface(Ip("1.1.1.1"));
  SM_EXPECT_EQ(nm.GetNetworks().size(), size_t{1});
  SM_EXPECT_FALSE(wlan->active());
  nm.AddInterface(Ip("1.1.1.1"), "wlan0", ADAPTER_TYPE_WIFI);
  SM_EXPECT_TRUE(nm.GetNetworks()[1] == wlan);
  SM_EXPECT_TRUE(wlan->active());
  // A change from one cellular type to another is not a network change.
  nm.AddInterface(Ip("3.3.3.3"), "rmnet0", ADAPTER_TYPE_CELLULAR_4G);
  const int before = changes;
  nm.ChangeInterfaceType(Ip("3.3.3.3"), ADAPTER_TYPE_CELLULAR_5G);
  SM_EXPECT_EQ(changes, before);
  nm.ChangeInterfaceType(Ip("3.3.3.3"), ADAPTER_TYPE_WIFI);
  SM_EXPECT_EQ(changes, before + 1);
}

SM_TEST(P2pNetworkManager, AnyAddressNetworksAndDefaultLocalAddress) {
  StaticNetworkManager nm;
  IpAddress ip;
  SM_EXPECT_FALSE(nm.GetDefaultLocalAddress(IpFamily::kV4, &ip));
  nm.set_default_local_addresses(kPrivateIp, IpAddress());
  SM_EXPECT_TRUE(nm.GetDefaultLocalAddress(IpFamily::kV4, &ip));
  SM_EXPECT_TRUE(ip == kPrivateIp);
  SM_EXPECT_FALSE(nm.GetDefaultLocalAddress(IpFamily::kV6, &ip));
  const auto any = nm.GetAnyAddressNetworks();
  SM_ASSERT_EQ(any.size(), size_t{2});
  SM_EXPECT_EQ(any[0]->type(), ADAPTER_TYPE_ANY);
  SM_EXPECT_TRUE(IpIsAny(any[0]->GetBestIP()));
  SM_EXPECT_TRUE(any[0]->family() == IpFamily::kV4);
  SM_EXPECT_TRUE(any[1]->family() == IpFamily::kV6);
  SM_EXPECT_TRUE(*any[0]->default_local_address() == kPrivateIp);
  SM_EXPECT_TRUE(IpIsUnspec(*any[1]->default_local_address()));
  // TruncateIP.
  SM_EXPECT_TRUE(TruncateIp(Ip("1.2.3.4"), 24) == Ip("1.2.3.0"));
  SM_EXPECT_TRUE(TruncateIp(Ip("1.2.3.4"), 0) == Ip("0.0.0.0"));
  SM_EXPECT_TRUE(TruncateIp(Ip("1.2.3.4"), 32) == Ip("1.2.3.4"));
  SM_EXPECT_TRUE(TruncateIp(kClientIPv6, 64) == Ip("2401:fa00:4:1000::"));
  SM_EXPECT_TRUE(TruncateIp(kClientIPv6, 52) == Ip("2401:fa00:4:1000::"));
  SM_EXPECT_TRUE(TruncateIp(kClientIPv6, 36) == Ip("2401:fa00::"));
}

// NetworkChangeDuringGatheringDoesNotDuplicateTcp (UDP here): networks-changed
// signals while the first sequence runs make no second host port on the
// same network; a network added while gathering gets its own sequence.
SM_TEST(P2pBasicPortAllocator, NetworkChangeDuringGathering) {
  Fixture f(Fixture::kFake, /*with_stun_server=*/false);
  f.AddInterface(kClientIp);
  f.CreateSession();
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.candidates.size() == 1; }));
  SM_ASSERT_EQ(f.candidates.front().protocol(), std::string(UDP_PROTOCOL_NAME));
  SM_ASSERT_FALSE(f.done);
  f.network_manager.NotifyNetworksChanged();
  f.network_manager.NotifyNetworksChanged();
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_EXPECT_EQ(f.candidates.size(), size_t{1});
  SM_EXPECT_EQ(f.ports.size(), size_t{1});
  SM_EXPECT_EQ(static_cast<BasicPortAllocatorSession*>(f.session.get())->sequence_count(), size_t{1});

  // A second network while the session gathers (default step delay).
  Fixture g(Fixture::kFake, /*with_stun_server=*/false);
  g.allocator->set_step_delay(kDefaultStepDelay);
  g.AddInterface(kClientIp, "eth0");
  g.CreateSession();
  g.session->StartGettingPorts(g.now);
  SM_ASSERT_TRUE(g.RunUntil([&] { return g.candidates.size() == 1; }));
  SM_ASSERT_FALSE(g.done);
  g.AddInterface(kClientIp2, "eth1");
  SM_ASSERT_TRUE(g.RunUntilDone());
  SM_EXPECT_EQ(g.candidates.size(), size_t{2});
  SM_EXPECT_EQ(g.ports.size(), size_t{2});
  SM_EXPECT_TRUE(g.HasCandidate(IceCandidateType::kHost, "udp", kClientIp2));
  SM_EXPECT_EQ(g.regathering, 1);
}

// RegatherOnFailedNetworks only regathers on networks whose ports have no
// connection (GetFailedNetworks).
SM_TEST(P2pBasicPortAllocator, NetworkWithConnectionIsNotFailed) {
  Fixture f(Fixture::kFake, /*with_stun_server=*/false);
  f.AddInterface(kClientIp, "eth0");
  f.AddInterface(kClientIp2, "eth1");
  f.CreateSession();
  f.session->StartGettingPorts(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_ASSERT_EQ(f.ports.size(), size_t{2});
  Port* eth0 = f.ports[0]->network()->name() == "eth0" ? f.ports[0] : f.ports[1];
  Port* eth1 = eth0 == f.ports[0] ? f.ports[1] : f.ports[0];
  eth0->SetIceRole(ICEROLE_CONTROLLING);
  Candidate remote(ICE_CANDIDATE_COMPONENT_RTP, "udp", SocketAddress(Ip("33.33.33.33"), 4000), 1, "rufrag",
                   "rpasswordrpasswordrpass", IceCandidateType::kHost, 0, "f");
  SM_ASSERT_TRUE(eth0->CreateConnection(f.now, remote, Port::ORIGIN_MESSAGE) != nullptr);
  f.candidates.clear();
  f.session->RegatherOnFailedNetworks(f.now);
  // Only eth1's port is pruned and regathered.
  SM_ASSERT_EQ(f.removed.size(), size_t{1});
  SM_EXPECT_TRUE(f.removed[0].address().ipaddr() == kClientIp2);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.candidates.size() == 1; }));
  SM_EXPECT_TRUE(f.candidates[0].address().ipaddr() == kClientIp2);
  SM_EXPECT_TRUE(std::find(f.ports.begin(), f.ports.end(), eth0) != f.ports.end());
  SM_EXPECT_TRUE(std::find(f.ports.begin(), f.ports.end(), eth1) == f.ports.end());
}

// PeerConnection::InitializePortAllocator_n with RTCConfiguration's defaults,
// and the settings that turn flags off or on.
SM_TEST(P2pPortAllocator, ConfigureAsPeerConnection) {
  Fixture f;
  ConfigureAsPeerConnection(f.now, f.allocator.get(), {kStunAddr}, {});
  SM_EXPECT_EQ(f.allocator->flags(), uint32_t{PORTALLOCATOR_ENABLE_SHARED_SOCKET | PORTALLOCATOR_ENABLE_IPV6 |
                                              PORTALLOCATOR_ENABLE_IPV6_ON_WIFI});
  SM_EXPECT_EQ(f.allocator->step_delay(), kMinimumStepDelay);
  SM_EXPECT_EQ(f.allocator->candidate_filter(), uint32_t{CF_ALL});
  SM_EXPECT_EQ(f.allocator->max_ipv6_networks(), kDefaultMaxIPv6Networks);
  SM_EXPECT_EQ(f.allocator->candidate_pool_size(), 0);
  SM_EXPECT_TRUE(f.allocator->turn_port_prune_policy() == NO_PRUNE);

  Fixture g;
  PeerConnectionAllocatorConfig config;
  config.candidate_filter = CF_ALL & ~CF_HOST;  // kNoHost.
  config.ice_candidate_pool_size = 1;
  config.disable_ipv6_on_wifi = true;
  config.tcp_candidates_enabled = false;
  config.low_cost_networks_only = true;
  config.disable_link_local_networks = true;
  config.prune_turn_ports = true;
  config.ipv6_default = false;
  config.stun_candidate_keepalive_interval_ms = 123;
  ConfigureAsPeerConnection(g.now, g.allocator.get(), {kStunAddr}, {}, config);
  SM_EXPECT_EQ(g.allocator->flags(), uint32_t{PORTALLOCATOR_ENABLE_SHARED_SOCKET | PORTALLOCATOR_DISABLE_TCP |
                                              PORTALLOCATOR_DISABLE_COSTLY_NETWORKS |
                                              PORTALLOCATOR_DISABLE_LINK_LOCAL_NETWORKS});
  SM_EXPECT_EQ(g.allocator->candidate_filter(), uint32_t{CF_REFLEXIVE | CF_RELAY});
  SM_EXPECT_TRUE(g.allocator->turn_port_prune_policy() == PRUNE_BASED_ON_PRIORITY);
  SM_EXPECT_TRUE(*g.allocator->stun_candidate_keepalive_interval() == TimeDelta::Millis(123));
  // The pooled session was made after the flags were set.
  const PortAllocatorSession* pooled = g.allocator->GetPooledSession();
  SM_ASSERT_TRUE(pooled != nullptr);
  SM_EXPECT_EQ(pooled->flags(), g.allocator->flags());
}
