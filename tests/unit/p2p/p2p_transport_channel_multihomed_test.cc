// The ICE agent with several interfaces per endpoint (issue #1002 part 4): the cases of libwebrtc M157's
// p2p/base/p2p_transport_channel_unittest.cc P2PTransportChannelMultihomedTest, over the emulated network (#985).
//
// The fixture gives each endpoint a `sim::EmulatedMultiHomedHost`: every address `AddAddress` adds is its own host
// on the network, with its own access links, and its own interface in the endpoint's `StaticNetworkManager` (with
// the name and adapter type the libwebrtc case gives). `fw()->AddRule(false, FP_ANY, FD_ANY, addr)` is
// `sim::Network::BlockAddress`, and `RemoveAddress` removes the interface and blocks the address, as libwebrtc's
// fixture does. Candidates go to the other side through the test's signaling queue on the next step.
//
// As in libwebrtc's fixture, the links have no delay (the virtual server's default), and candidate removals go to
// the other side unpaused (OnCandidatesRemoved). Differences: no TURN server (the cases here gather host candidates,
// or host and STUN candidates, and assert on host pairs), the library's time model in place of the simulated clock,
// and the session's network-change handling is posted with no delay (basic_port_allocator.cc), so
// TestNetworkBecomesInactive steps once before its immediate check. Not ported: TestFailoverWithManyConnections (IPv6
// interfaces and a TCP TURN server; the emulated network is IPv4 only) and StunDictionaryPerformsSync (the STUN
// dictionary is not ported).
#include <algorithm>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/basic_port_allocator.h"
#include "sm/p2p/candidate.h"
#include "sm/p2p/network_manager.h"
#include "sm/p2p/p2p_transport_channel.h"
#include "sm/p2p/port_allocator.h"
#include "sm/sim/emulated_sockets.h"
#include "sm/sim/stun_server.h"
#include "sm_test.h"
#include "test_crypto.h"

using namespace sm;
using namespace sm::p2p;

namespace {

IpAddress Ip(std::string_view s) { return *IpAddress::FromString(s); }

// kPublicAddrs and kAlternateAddrs of the libwebrtc fixture.
const IpAddress kPublicAddrs[2] = {Ip("11.11.11.11"), Ip("22.22.22.22")};
const IpAddress kAlternateAddrs[2] = {Ip("101.101.101.101"), Ip("202.202.202.202")};
const SocketAddress kStunAddr(Ip("99.99.99.1"), 3478);  // STUN_SERVER_PORT.

const char* kIceUfrag[2] = {"UF00", "UF01"};
const char* kIcePwd[2] = {"TESTICEPWD00000000000000", "TESTICEPWD00000000000001"};
const IceParameters kIceParams[2] = {{kIceUfrag[0], kIcePwd[0], false}, {kIceUfrag[1], kIcePwd[1], false}};

// ShortWait, MediumWait and DefaultWait.
constexpr TimeDelta kShortTimeout = TimeDelta::Seconds(1);
constexpr TimeDelta kMediumTimeout = TimeDelta::Seconds(3);
constexpr TimeDelta kDefaultTimeout = TimeDelta::Seconds(10);

constexpr uint32_t kOnlyLocalPorts = PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY | PORTALLOCATOR_DISABLE_TCP;
constexpr auto GATHER_ONCE = ContinualGatheringPolicy::kGatherOnce;
constexpr auto GATHER_CONTINUALLY = ContinualGatheringPolicy::kGatherContinually;

IceConfig CreateIceConfig(TimeDelta receiving_timeout, ContinualGatheringPolicy policy,
                          std::optional<TimeDelta> backup_ping_interval = std::nullopt) {
  IceConfig config;
  config.receiving_timeout = receiving_timeout;
  config.continual_gathering_policy = policy;
  config.backup_connection_ping_interval = backup_ping_interval;
  return config;
}

sim::LinkConfig Link(int delay_ms) {
  sim::LinkConfig c;
  c.base_delay = TimeDelta::Millis(delay_ms);
  return c;
}

struct Endpoint {
  std::unique_ptr<sim::EmulatedMultiHomedHost> host;
  StaticNetworkManager network_manager;
  std::unique_ptr<BasicPortAllocator> allocator;
  std::unique_ptr<P2PTransportChannel> ch;
  IceRole role = ICEROLE_UNKNOWN;
};

struct Fixture {
  sim::ByteNetwork net;
  std::unique_ptr<sim::EmulatedSocketServer> stun_host;
  std::unique_ptr<sim::StunServer> stun;
  Endpoint ep[2];
  std::deque<Candidate> signaling[2];  // Candidates for endpoint i.
  std::deque<Candidate> removals[2];   // Candidates endpoint i removes from its remote candidates (not paused).
  int selected_candidate_pair_switches = 0;
  bool nominated = false;
  Timestamp now = Timestamp::Seconds(10);

  Fixture() {
    testing::InstallTestCrypto();
    stun_host = std::make_unique<sim::EmulatedSocketServer>(
        net, net.AddHost(kStunAddr.ip(), sim::ByteNetwork::kPublicRealm, Link(0), Link(0)));
    stun = std::make_unique<sim::StunServer>(stun_host->CreateUdpSocket(kStunAddr, 0, 0));
    ep[0].role = ICEROLE_CONTROLLING;
    ep[1].role = ICEROLE_CONTROLLED;
  }
  ~Fixture() { DestroyChannels(); }

  // CreatePortAllocators: a STUN server, no TURN server (see the file comment).
  void CreatePortAllocators() {
    for (Endpoint& e : ep) {
      e.host = std::make_unique<sim::EmulatedMultiHomedHost>(net);
      e.allocator = std::make_unique<BasicPortAllocator>(&e.network_manager, e.host.get());
      e.allocator->SetConfiguration(now, ServerAddresses{kStunAddr}, {}, 0, NO_PRUNE);
    }
  }
  // AddAddress: the interface on the network (once per address) and in the network manager.
  void AddAddress(int i, const IpAddress& ip) {
    AddHost(i, ip);
    ep[i].network_manager.AddInterface(ip);
  }
  void AddAddress(int i, const IpAddress& ip, std::string name, AdapterType type,
                  AdapterType underlying_vpn_type = ADAPTER_TYPE_UNKNOWN) {
    AddHost(i, ip);
    ep[i].network_manager.AddInterface(ip, std::move(name), type, underlying_vpn_type);
  }
  void AddHost(int i, const IpAddress& ip) {
    if (!ep[i].host->server(ip)) ep[i].host->AddInterface(ip, sim::ByteNetwork::kPublicRealm, Link(0), Link(0));
  }
  // RemoveAddress: the interface goes away and its traffic is blackholed.
  void RemoveAddress(int i, const IpAddress& ip) {
    ep[i].network_manager.RemoveInterface(ip);
    net.BlockAddress(ip.v4());
  }
  // fw()->AddRule(false, FP_ANY, FD_ANY, addr) and fw()->ClearRules().
  void Blackhole(const IpAddress& ip) { net.BlockAddress(ip.v4()); }
  void ClearRules() { net.ClearBlockedAddresses(); }
  void SetAllocatorFlags(int i, uint32_t flags) { ep[i].allocator->set_flags(flags); }

  std::unique_ptr<P2PTransportChannel> CreateChannel(int i, const IceParameters& local_ice,
                                                     const IceParameters& remote_ice) {
    auto channel = std::make_unique<P2PTransportChannel>(now, "test content name", ICE_CANDIDATE_COMPONENT_RTP,
                                                         ep[i].allocator.get());
    channel->SubscribeCandidateGathered(
        this, [this, i](IceTransportInternal*, const Candidate& c) { signaling[1 - i].push_back(c); });
    // OnCandidatesRemoved: the other side removes them from its remote candidates, on the next step.
    channel->SetCandidatesRemovedCallback([this, i](IceTransportInternal*, const Candidates& candidates) {
      for (const Candidate& c : candidates) removals[1 - i].push_back(c);
    });
    // OnNetworkRouteChanged: an unset route (the remote side's network is down) is not a switch.
    channel->SubscribeNetworkRouteChanged(this, [this](std::optional<NetworkRoute> r) {
      if (r) ++selected_candidate_pair_switches;
    });
    channel->SetIceParameters(local_ice);
    channel->SetRemoteIceParameters(remote_ice);
    channel->SetIceRole(ep[i].role);
    return channel;
  }
  static IceParameters WithRenomination(IceParameters p, bool renomination) {
    p.renomination = renomination;
    return p;
  }
  void CreateChannels(const IceConfig& c0, const IceConfig& c1, bool renomination = false) {
    ep[0].ch = CreateChannel(0, WithRenomination(kIceParams[0], renomination),
                             WithRenomination(kIceParams[1], renomination));
    ep[1].ch = CreateChannel(1, WithRenomination(kIceParams[1], renomination),
                             WithRenomination(kIceParams[0], renomination));
    ep[0].ch->SetIceConfig(c0);
    ep[1].ch->SetIceConfig(c1);
    ep[0].ch->MaybeStartGathering(now);
    ep[1].ch->MaybeStartGathering(now);
  }
  void CreateChannels() { CreateChannels(IceConfig{}, IceConfig{}); }
  void DestroyChannels() {
    ep[0].ch.reset();
    ep[1].ch.reset();
  }
  P2PTransportChannel* ch(int i) { return ep[i].ch.get(); }

  int reset_selected_candidate_pair_switches() {
    const int n = selected_candidate_pair_switches;
    selected_candidate_pair_switches = 0;
    return n;
  }
  void ConnectSignalNominated(Connection* conn) {
    conn->SubscribeNominated(this, [this](Connection*) { nominated = true; });
  }

  void DeliverCandidates() {
    for (int i = 0; i < 2; ++i) {
      while (!signaling[i].empty()) {
        Candidate c = signaling[i].front();
        signaling[i].pop_front();
        if (!ep[i].ch) continue;
        c.set_username("");  // FROM_SETICEPARAMETERS.
        c.set_password("");
        ep[i].ch->AddRemoteCandidate(now, c);
      }
      while (!removals[i].empty()) {
        const Candidate c = removals[i].front();
        removals[i].pop_front();
        if (ep[i].ch) ep[i].ch->RemoveRemoteCandidate(c);
      }
    }
  }
  Timestamp NextEvent() const {
    Timestamp t = net.NextEvent();
    for (const Endpoint& e : ep) {
      if (e.host) t = std::min(t, e.host->NextEvent());
      if (e.ch) t = std::min(t, e.ch->NextProcessTime());
      if (e.allocator) t = std::min(t, e.allocator->NextPooledSessionsProcessTime());
    }
    for (int i = 0; i < 2; ++i) {
      if (!signaling[i].empty() || !removals[i].empty()) t = std::min(t, now);
    }
    return t;
  }
  void Step(Timestamp t) {
    now = std::max(now, t);
    for (Endpoint& e : ep) {
      if (e.ch) e.ch->Process(now);
      if (e.allocator) e.allocator->ProcessPooledSessions(now);
    }
    for (Endpoint& e : ep) {
      if (e.host) e.host->Deliver(now);
    }
    stun_host->Deliver(now);
    DeliverCandidates();
  }
  // Runs until `stop` holds, at most `limit` of emulated time (WaitUntil).
  template <class F>
  bool RunUntil(F stop, TimeDelta limit) {
    const Timestamp until = now + limit;
    for (int guard = 0; guard < 2000000; ++guard) {
      if (stop()) return true;
      const Timestamp t = NextEvent();
      if (t > until) {
        Step(until);
        return stop();
      }
      Step(t);
    }
    return stop();
  }
  // time_controller_.AdvanceTime.
  void Run(TimeDelta d) {
    RunUntil([] { return false; }, d);
  }

  static bool CheckConnected(P2PTransportChannel* a, P2PTransportChannel* b) {
    return a->receiving() && a->writable() && b->receiving() && b->writable() && a->selected_connection() &&
           b->selected_connection();
  }
  bool Connected() { return CheckConnected(ch(0), ch(1)); }
  static bool HasLocalAddress(const Connection* c, const IpAddress& ip) {
    return c->local_candidate().address().ipaddr() == ip;
  }
  static bool HasRemoteAddress(const Connection* c, const IpAddress& ip) {
    return c->remote_candidate().address().ipaddr() == ip;
  }
  // CheckCandidatePair: each side's selected pair runs from `from` to `to`, seen from its own end.
  static bool CheckCandidatePair(P2PTransportChannel* a, P2PTransportChannel* b, const IpAddress& from,
                                 const IpAddress& to) {
    const Connection* ca = a->selected_connection();
    const Connection* cb = b->selected_connection();
    if (!ca || !cb) return false;
    return HasLocalAddress(ca, from) && HasRemoteAddress(ca, to) && HasLocalAddress(cb, to) &&
           HasRemoteAddress(cb, from);
  }
  bool CheckCandidatePairAndConnected(const IpAddress& from, const IpAddress& to) {
    return Connected() && CheckCandidatePair(ch(0), ch(1), from, to);
  }

  Connection* GetConnectionWithLocalAddress(P2PTransportChannel* c, const IpAddress& ip) {
    for (Connection* conn : c->connections()) {
      if (HasLocalAddress(conn, ip)) return conn;
    }
    return nullptr;
  }
  Connection* GetBestConnection(P2PTransportChannel* c) {
    for (Connection* conn : c->connections()) {
      if (conn == c->selected_connection()) return conn;
    }
    return nullptr;
  }
  Connection* GetBackupConnection(P2PTransportChannel* c) {
    for (Connection* conn : c->connections()) {
      if (conn != c->selected_connection()) return conn;
    }
    return nullptr;
  }
  void DestroyAllButBestConnection(P2PTransportChannel* c) {
    const Connection* selected = c->selected_connection();
    std::vector<Connection*> connections(c->connections().begin(), c->connections().end());
    for (Connection* conn : connections) {
      if (conn != selected) c->RemoveConnectionForTest(now, conn);
    }
  }
};

}  // namespace

// TestBasic: both peers multihomed connect on host candidates (kLocalUdpToLocalUdp).
SM_TEST(P2pTransportChannelMultihomed, TestBasic) {
  Fixture f;
  f.CreatePortAllocators();
  f.AddAddress(0, kPublicAddrs[0]);
  f.AddAddress(0, kAlternateAddrs[0]);
  f.AddAddress(1, kPublicAddrs[1]);
  f.AddAddress(1, kAlternateAddrs[1]);
  f.CreateChannels();
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected(); }, kMediumTimeout));
  SM_EXPECT_TRUE(f.RunUntil(
      [&] {
        return f.ch(0)->selected_connection()->local_candidate().is_local() &&
               f.ch(0)->selected_connection()->remote_candidate().is_local() &&
               f.ch(1)->selected_connection()->local_candidate().is_local() &&
               f.ch(1)->selected_connection()->remote_candidate().is_local();
      },
      kShortTimeout));
  SM_EXPECT_EQ(f.ch(0)->selected_connection()->local_candidate().protocol(), std::string("udp"));
  SM_EXPECT_EQ(f.ch(1)->selected_connection()->local_candidate().protocol(), std::string("udp"));
}

// TestFailoverControlledSide: the controlled side's Wi-Fi interface dies; both sides move to its cellular one.
SM_TEST(P2pTransportChannelMultihomed, TestFailoverControlledSide) {
  Fixture f;
  f.CreatePortAllocators();
  f.AddAddress(0, kPublicAddrs[0]);
  f.AddAddress(1, kPublicAddrs[1], "eth0", ADAPTER_TYPE_WIFI);
  f.AddAddress(1, kAlternateAddrs[1], "wlan0", ADAPTER_TYPE_CELLULAR);
  f.SetAllocatorFlags(0, kOnlyLocalPorts);
  f.SetAllocatorFlags(1, kOnlyLocalPorts);
  const IceConfig config = CreateIceConfig(TimeDelta::Seconds(1), GATHER_ONCE);
  f.CreateChannels(config, config);
  SM_ASSERT_TRUE(
      f.RunUntil([&] { return f.CheckCandidatePairAndConnected(kPublicAddrs[0], kPublicAddrs[1]); }, kDefaultTimeout));

  f.Blackhole(kPublicAddrs[1]);
  const Connection* selected_connection1 = f.ch(0)->selected_connection();
  SM_EXPECT_TRUE(f.RunUntil([&] { return !selected_connection1->receiving(); }, kMediumTimeout));
  SM_EXPECT_TRUE(f.RunUntil(
      [&] { return f.ch(0)->selected_connection()->receiving() && f.ch(1)->selected_connection()->receiving(); },
      kMediumTimeout));
  SM_EXPECT_TRUE(f.ch(0)->selected_connection()->local_candidate().address().ipaddr() == kPublicAddrs[0]);
  SM_EXPECT_TRUE(f.ch(0)->selected_connection()->remote_candidate().address().ipaddr() == kAlternateAddrs[1]);
  SM_EXPECT_TRUE(f.ch(1)->selected_connection()->local_candidate().address().ipaddr() == kAlternateAddrs[1]);
}

// TestFailoverControllingSide: the controlling side's Wi-Fi interface dies.
SM_TEST(P2pTransportChannelMultihomed, TestFailoverControllingSide) {
  Fixture f;
  f.CreatePortAllocators();
  f.AddAddress(0, kPublicAddrs[0], "eth0", ADAPTER_TYPE_WIFI);
  f.AddAddress(0, kAlternateAddrs[0], "wlan0", ADAPTER_TYPE_CELLULAR);
  f.AddAddress(1, kPublicAddrs[1]);
  f.SetAllocatorFlags(0, kOnlyLocalPorts);
  f.SetAllocatorFlags(1, kOnlyLocalPorts);
  const IceConfig config = CreateIceConfig(TimeDelta::Seconds(1), GATHER_ONCE);
  f.CreateChannels(config, config);
  SM_ASSERT_TRUE(
      f.RunUntil([&] { return f.CheckCandidatePairAndConnected(kPublicAddrs[0], kPublicAddrs[1]); }, kMediumTimeout));

  f.Blackhole(kPublicAddrs[0]);
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.CheckCandidatePairAndConnected(kAlternateAddrs[0], kPublicAddrs[1]); },
                            kMediumTimeout));
}

// TestIceRenomination: when the controlling side switches, the controlled side's selected connection is nominated
// again with a larger nomination.
SM_TEST(P2pTransportChannelMultihomed, TestIceRenomination) {
  Fixture f;
  f.CreatePortAllocators();
  f.AddAddress(0, kPublicAddrs[0], "eth0", ADAPTER_TYPE_WIFI);
  f.AddAddress(0, kAlternateAddrs[0], "wlan0", ADAPTER_TYPE_CELLULAR);
  f.AddAddress(1, kPublicAddrs[1]);
  f.SetAllocatorFlags(0, kOnlyLocalPorts);
  f.SetAllocatorFlags(1, kOnlyLocalPorts);
  const IceConfig config = CreateIceConfig(TimeDelta::Seconds(1), GATHER_ONCE);
  f.CreateChannels(config, config, /*renomination=*/true);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected(); }, kMediumTimeout));
  SM_EXPECT_TRUE(f.RunUntil(
      [&] {
        return f.ch(1)->selected_connection()->remote_nomination() > 0 &&
               f.ch(0)->selected_connection()->acked_nomination() > 0;
      },
      kMediumTimeout));
  const Connection* selected_connection1 = f.ch(0)->selected_connection();
  Connection* selected_connection2 = const_cast<Connection*>(f.ch(1)->selected_connection());
  const uint32_t remote_nomination2 = selected_connection2->remote_nomination();
  // Not nominated again: the previous nomination was acknowledged.
  f.ConnectSignalNominated(selected_connection2);
  SM_EXPECT_FALSE(f.RunUntil([&] { return f.nominated; }, kDefaultTimeout));

  f.Blackhole(kPublicAddrs[0]);
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.ch(0)->selected_connection() != selected_connection1; }, kMediumTimeout));
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.ch(1)->selected_connection()->remote_nomination() > remote_nomination2; },
                            kMediumTimeout));
}

// TestConnectionSwitchDampeningControlledSide: an interface that fails briefly and recovers is not switched away from.
SM_TEST(P2pTransportChannelMultihomed, TestConnectionSwitchDampeningControlledSide) {
  Fixture f;
  f.CreatePortAllocators();
  f.AddAddress(0, kPublicAddrs[0]);
  f.AddAddress(1, kPublicAddrs[1], "eth0", ADAPTER_TYPE_WIFI);
  f.AddAddress(1, kAlternateAddrs[1], "wlan0", ADAPTER_TYPE_CELLULAR);
  f.SetAllocatorFlags(0, kOnlyLocalPorts);
  f.SetAllocatorFlags(1, kOnlyLocalPorts);
  f.CreateChannels();
  SM_ASSERT_TRUE(
      f.RunUntil([&] { return f.CheckCandidatePairAndConnected(kPublicAddrs[0], kPublicAddrs[1]); }, kDefaultTimeout));
  const IceConfig config = CreateIceConfig(TimeDelta::Seconds(1), GATHER_ONCE);
  f.ch(0)->SetIceConfig(config);
  f.ch(1)->SetIceConfig(config);
  f.reset_selected_candidate_pair_switches();

  f.Blackhole(kPublicAddrs[1]);
  const Connection* selected_connection1 = f.ch(0)->selected_connection();
  SM_EXPECT_TRUE(f.RunUntil([&] { return !selected_connection1->receiving(); }, kMediumTimeout));
  f.Run(TimeDelta::Millis(10));
  f.ClearRules();

  SM_EXPECT_TRUE(f.RunUntil(
      [&] { return f.ch(0)->selected_connection()->receiving() && f.ch(1)->selected_connection()->receiving(); },
      kMediumTimeout));
  SM_EXPECT_TRUE(f.ch(0)->selected_connection()->remote_candidate().address().ipaddr() == kPublicAddrs[1]);
  SM_EXPECT_TRUE(f.ch(1)->selected_connection()->local_candidate().address().ipaddr() == kPublicAddrs[1]);
  SM_EXPECT_EQ(f.reset_selected_candidate_pair_switches(), 0);
}

// TestConnectionSwitchDampeningControllingSide.
SM_TEST(P2pTransportChannelMultihomed, TestConnectionSwitchDampeningControllingSide) {
  Fixture f;
  f.CreatePortAllocators();
  f.AddAddress(0, kPublicAddrs[0], "eth0", ADAPTER_TYPE_WIFI);
  f.AddAddress(0, kAlternateAddrs[0], "wlan0", ADAPTER_TYPE_CELLULAR);
  f.AddAddress(1, kPublicAddrs[1]);
  f.SetAllocatorFlags(0, kOnlyLocalPorts);
  f.SetAllocatorFlags(1, kOnlyLocalPorts);
  f.CreateChannels();
  SM_ASSERT_TRUE(
      f.RunUntil([&] { return f.CheckCandidatePairAndConnected(kPublicAddrs[0], kPublicAddrs[1]); }, kMediumTimeout));
  const IceConfig config = CreateIceConfig(TimeDelta::Seconds(1), GATHER_ONCE);
  f.ch(0)->SetIceConfig(config);
  f.ch(1)->SetIceConfig(config);
  f.reset_selected_candidate_pair_switches();

  f.Blackhole(kPublicAddrs[0]);
  const Connection* selected_connection1 = f.ch(0)->selected_connection();
  SM_EXPECT_TRUE(f.RunUntil([&] { return !selected_connection1->receiving(); }, kMediumTimeout));
  f.Run(TimeDelta::Millis(10));
  f.ClearRules();

  SM_EXPECT_TRUE(
      f.RunUntil([&] { return f.CheckCandidatePairAndConnected(kPublicAddrs[0], kPublicAddrs[1]); }, kMediumTimeout));
  SM_EXPECT_EQ(f.reset_selected_candidate_pair_switches(), 0);
}

// TestRemoteFailover: the remote side's network failing does not make the local side switch.
SM_TEST(P2pTransportChannelMultihomed, TestRemoteFailover) {
  Fixture f;
  f.CreatePortAllocators();
  const IpAddress* wifi = kPublicAddrs;
  const IpAddress* cellular = kAlternateAddrs;
  f.AddAddress(0, wifi[0], "wifi0", ADAPTER_TYPE_WIFI);
  f.AddAddress(0, cellular[0], "cellular0", ADAPTER_TYPE_CELLULAR);
  f.AddAddress(1, wifi[1], "wifi0", ADAPTER_TYPE_WIFI);
  f.SetAllocatorFlags(0, kOnlyLocalPorts);
  f.SetAllocatorFlags(1, kOnlyLocalPorts);
  f.CreateChannels();
  IceConfig config = CreateIceConfig(TimeDelta::Seconds(1), GATHER_ONCE, TimeDelta::Seconds(25));
  config.stable_writable_connection_ping_interval = TimeDelta::Millis(900);
  f.ch(0)->SetIceConfig(config);
  f.ch(1)->SetIceConfig(config);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.CheckCandidatePairAndConnected(wifi[0], wifi[1]); }, kMediumTimeout));
  Connection* backup_conn = f.GetConnectionWithLocalAddress(f.ch(0), cellular[0]);
  SM_ASSERT_TRUE(backup_conn != nullptr);
  // The backup connection is pinged slowly: writable, not receiving.
  SM_EXPECT_TRUE(f.RunUntil([&] { return backup_conn->writable() && !backup_conn->receiving(); }, kDefaultTimeout));
  f.reset_selected_candidate_pair_switches();

  f.Blackhole(wifi[1]);
  int num_switches = 0;
  (void)f.RunUntil([&] { return (num_switches = f.reset_selected_candidate_pair_switches()) > 0; }, kDefaultTimeout);
  SM_EXPECT_EQ(num_switches, 0);
}

// TestPreferWifiToWifiConnection: Wi-Fi to Wi-Fi wins over the cellular pairs, whose names sort first.
SM_TEST(P2pTransportChannelMultihomed, TestPreferWifiToWifiConnection) {
  Fixture f;
  f.CreatePortAllocators();
  const IpAddress* wifi = kAlternateAddrs;
  const IpAddress* cellular = kPublicAddrs;
  f.AddAddress(0, wifi[0], "test0", ADAPTER_TYPE_WIFI);
  f.AddAddress(0, cellular[0], "test1", ADAPTER_TYPE_CELLULAR);
  f.AddAddress(1, wifi[1], "test0", ADAPTER_TYPE_WIFI);
  f.AddAddress(1, cellular[1], "test1", ADAPTER_TYPE_CELLULAR);
  f.SetAllocatorFlags(0, kOnlyLocalPorts);
  f.SetAllocatorFlags(1, kOnlyLocalPorts);
  f.CreateChannels();
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.Connected(); }, kDefaultTimeout));
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.CheckCandidatePairAndConnected(wifi[0], wifi[1]); }, kDefaultTimeout));
}

// TestPreferWifiOverCellularNetwork: cellular to Wi-Fi wins over cellular to cellular.
SM_TEST(P2pTransportChannelMultihomed, TestPreferWifiOverCellularNetwork) {
  Fixture f;
  f.CreatePortAllocators();
  const IpAddress* wifi = kAlternateAddrs;
  const IpAddress* cellular = kPublicAddrs;
  f.AddAddress(0, cellular[0], "test1", ADAPTER_TYPE_CELLULAR);
  f.AddAddress(1, wifi[1], "test0", ADAPTER_TYPE_WIFI);
  f.AddAddress(1, cellular[1], "test1", ADAPTER_TYPE_CELLULAR);
  f.SetAllocatorFlags(0, kOnlyLocalPorts);
  f.SetAllocatorFlags(1, kOnlyLocalPorts);
  f.CreateChannels();
  SM_EXPECT_TRUE(
      f.RunUntil([&] { return f.CheckCandidatePairAndConnected(cellular[0], wifi[1]); }, kDefaultTimeout));
}

// TestPingBackupConnectionRate: the backup connection is pinged no faster than the configured interval.
SM_TEST(P2pTransportChannelMultihomed, TestPingBackupConnectionRate) {
  Fixture f;
  f.CreatePortAllocators();
  f.AddAddress(0, kPublicAddrs[0]);
  f.AddAddress(1, kAlternateAddrs[1]);
  f.AddAddress(1, kPublicAddrs[1]);
  f.SetAllocatorFlags(0, kOnlyLocalPorts);
  f.SetAllocatorFlags(1, kOnlyLocalPorts);
  f.CreateChannels();
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected(); }, kDefaultTimeout));
  const TimeDelta backup_ping_interval = TimeDelta::Seconds(2);
  f.ch(1)->SetIceConfig(CreateIceConfig(TimeDelta::Seconds(2), GATHER_ONCE, backup_ping_interval));
  SM_ASSERT_TRUE(
      f.RunUntil([&] { return f.ch(1)->GetState() == IceTransportStateInternal::kCompleted; }, kDefaultTimeout));
  SM_ASSERT_EQ(f.ch(1)->connections().size(), size_t{2});
  Connection* backup_conn = f.GetBackupConnection(f.ch(1));
  SM_EXPECT_TRUE(f.RunUntil([&] { return backup_conn->writable(); }, kDefaultTimeout));
  const Timestamp last_ping_response = backup_conn->LastPingResponseReceived();
  SM_EXPECT_TRUE(
      f.RunUntil([&] { return backup_conn->LastPingResponseReceived() > last_ping_response; }, kMediumTimeout));
  const TimeDelta time_elapsed = backup_conn->LastPingResponseReceived() - last_ping_response;
  SM_EXPECT_TRUE(time_elapsed >= backup_ping_interval);
}

// TestStableWritableRate: a stable, writable connection is pinged no faster than the configured interval.
SM_TEST(P2pTransportChannelMultihomed, TestStableWritableRate) {
  Fixture f;
  f.CreatePortAllocators();
  f.AddAddress(0, kPublicAddrs[0]);
  f.AddAddress(1, kAlternateAddrs[1]);
  f.AddAddress(1, kPublicAddrs[1]);
  f.SetAllocatorFlags(0, kOnlyLocalPorts);
  f.SetAllocatorFlags(1, kOnlyLocalPorts);
  f.CreateChannels();
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected(); }, kDefaultTimeout));
  const TimeDelta ping_interval = TimeDelta::Millis(3'456);  // Above the default of 2500 ms.
  IceConfig config = CreateIceConfig(ping_interval * int64_t{2}, GATHER_ONCE);
  config.stable_writable_connection_ping_interval = ping_interval;
  f.ch(1)->SetIceConfig(config);
  SM_ASSERT_TRUE(
      f.RunUntil([&] { return f.ch(1)->GetState() == IceTransportStateInternal::kCompleted; }, kDefaultTimeout));
  SM_ASSERT_EQ(f.ch(1)->connections().size(), size_t{2});
  Connection* conn = f.GetBestConnection(f.ch(1));
  SM_ASSERT_TRUE(conn != nullptr);
  SM_EXPECT_TRUE(f.RunUntil([&] { return conn->writable(); }, kDefaultTimeout));
  Timestamp last_ping_response = Timestamp::Millis(0);
  for (int i = 0; i < 5; ++i) {  // Burn through some pings so the connection is stable.
    last_ping_response = conn->LastPingResponseReceived();
    SM_EXPECT_TRUE(f.RunUntil([&] { return conn->LastPingResponseReceived() > last_ping_response; }, kDefaultTimeout));
  }
  SM_EXPECT_TRUE(conn->stable(last_ping_response));
  const TimeDelta time_elapsed = conn->LastPingResponseReceived() - last_ping_response;
  SM_EXPECT_TRUE(time_elapsed >= ping_interval);
}

// TestGetState: both channels reach COMPLETED quickly.
SM_TEST(P2pTransportChannelMultihomed, TestGetState) {
  Fixture f;
  f.CreatePortAllocators();
  f.AddAddress(0, kAlternateAddrs[0]);
  f.AddAddress(0, kPublicAddrs[0]);
  f.AddAddress(1, kPublicAddrs[1]);
  f.CreateChannels();
  SM_EXPECT_TRUE(
      f.RunUntil([&] { return f.ch(0)->GetState() == IceTransportStateInternal::kCompleted; }, kDefaultTimeout));
  SM_EXPECT_TRUE(
      f.RunUntil([&] { return f.ch(1)->GetState() == IceTransportStateInternal::kCompleted; }, kShortTimeout));
}

// TestNetworkBecomesInactive: with continual gathering, the ports of an interface that goes away are removed and the
// other side drops their candidates; without it, the ports go but the other side keeps the candidates.
SM_TEST(P2pTransportChannelMultihomed, TestNetworkBecomesInactive) {
  Fixture f;
  f.CreatePortAllocators();
  f.AddAddress(0, kPublicAddrs[0]);
  f.AddAddress(1, kPublicAddrs[1]);
  f.CreateChannels(CreateIceConfig(TimeDelta::Seconds(2), GATHER_CONTINUALLY),
                   CreateIceConfig(TimeDelta::Seconds(2), GATHER_ONCE));
  f.SetAllocatorFlags(0, kOnlyLocalPorts);
  f.SetAllocatorFlags(1, kOnlyLocalPorts);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected(); }, kShortTimeout));
  SM_EXPECT_TRUE(f.ch(0)->ports().size() >= 1u);
  f.RemoveAddress(0, kPublicAddrs[0]);
  // The session posts OnNetworksChanged with no delay (basic_port_allocator.cc); libwebrtc's runs it in the call.
  f.Step(f.now);
  SM_EXPECT_TRUE(f.ch(0)->ports().empty());
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.ch(1)->remote_candidates().empty(); }, kDefaultTimeout));

  SM_EXPECT_TRUE(f.ch(1)->ports().size() >= 1u);
  const size_t num_remote_candidates = f.ch(0)->remote_candidates().size();
  f.RemoveAddress(1, kPublicAddrs[1]);
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.ch(1)->ports().empty(); }, kDefaultTimeout));
  (void)f.RunUntil([&] { return f.ch(0)->remote_candidates().empty(); }, kDefaultTimeout);
  SM_EXPECT_EQ(f.ch(0)->remote_candidates().size(), num_remote_candidates);
}

// TestContinualGatheringOnNewInterface: continual gathering makes connections on an interface added mid-call; a new
// Wi-Fi interface becomes the selected one, a new cellular one a writable backup.
SM_TEST(P2pTransportChannelMultihomed, TestContinualGatheringOnNewInterface) {
  Fixture f;
  f.CreatePortAllocators();
  const IpAddress* wifi = kAlternateAddrs;
  const IpAddress* cellular = kPublicAddrs;
  f.AddAddress(0, wifi[0], "test_wifi0", ADAPTER_TYPE_WIFI);
  f.AddAddress(1, cellular[1], "test_cell1", ADAPTER_TYPE_CELLULAR);
  const IceConfig config = CreateIceConfig(TimeDelta::Seconds(1), GATHER_CONTINUALLY);
  f.CreateChannels(config, config);
  f.SetAllocatorFlags(0, kOnlyLocalPorts);
  f.SetAllocatorFlags(1, kOnlyLocalPorts);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected(); }, kDefaultTimeout));

  f.AddAddress(1, wifi[1], "test_wifi1", ADAPTER_TYPE_WIFI);
  SM_EXPECT_TRUE(f.RunUntil(
      [&] {
        const Connection* c = f.ch(0)->selected_connection();
        return c && Fixture::HasRemoteAddress(c, wifi[1]);
      },
      kDefaultTimeout));
  SM_EXPECT_TRUE(f.RunUntil(
      [&] {
        const Connection* c = f.ch(1)->selected_connection();
        return c && Fixture::HasLocalAddress(c, wifi[1]);
      },
      kDefaultTimeout));

  f.AddAddress(0, cellular[0], "test_cellular0", ADAPTER_TYPE_CELLULAR);
  SM_EXPECT_TRUE(f.RunUntil(
      [&] {
        if (f.ch(0)->GetState() != IceTransportStateInternal::kCompleted) return false;
        for (const Connection* c : f.ch(0)->connections()) {
          if (Fixture::HasLocalAddress(c, cellular[0]) && c != f.ch(0)->selected_connection() && c->writable()) {
            return true;
          }
        }
        return false;
      },
      kDefaultTimeout));
  SM_EXPECT_TRUE(f.RunUntil(
      [&] {
        if (f.ch(1)->GetState() != IceTransportStateInternal::kCompleted) return false;
        for (const Connection* c : f.ch(1)->connections()) {
          if (Fixture::HasRemoteAddress(c, cellular[0]) && c != f.ch(1)->selected_connection() && c->receiving()) {
            return true;
          }
        }
        return false;
      },
      kDefaultTimeout));
}

// TestSwitchLinksViaContinualGathering: an address added and the old one removed (and the other way round) moves the
// selected pair to the new address.
SM_TEST(P2pTransportChannelMultihomed, TestSwitchLinksViaContinualGathering) {
  Fixture f;
  f.CreatePortAllocators();
  f.AddAddress(0, kPublicAddrs[0]);
  f.AddAddress(1, kPublicAddrs[1]);
  f.SetAllocatorFlags(0, kOnlyLocalPorts);
  f.SetAllocatorFlags(1, kOnlyLocalPorts);
  const IceConfig config = CreateIceConfig(TimeDelta::Seconds(1), GATHER_CONTINUALLY);
  f.CreateChannels(config, config);
  SM_ASSERT_TRUE(
      f.RunUntil([&] { return f.CheckCandidatePairAndConnected(kPublicAddrs[0], kPublicAddrs[1]); }, kDefaultTimeout));

  // Add the new address first and then remove the other one.
  f.AddAddress(1, kAlternateAddrs[1]);
  f.RemoveAddress(1, kPublicAddrs[1]);
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.CheckCandidatePairAndConnected(kPublicAddrs[0], kAlternateAddrs[1]); },
                            kMediumTimeout));

  // Remove one address first and then add another address.
  f.RemoveAddress(1, kAlternateAddrs[1]);
  f.AddAddress(1, kAlternateAddrs[0]);
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.CheckCandidatePairAndConnected(kPublicAddrs[0], kAlternateAddrs[0]); },
                            kMediumTimeout));
}

// TestRestoreBackupConnection: a destroyed backup connection is made again (regathering on failed networks).
SM_TEST(P2pTransportChannelMultihomed, TestRestoreBackupConnection) {
  Fixture f;
  f.CreatePortAllocators();
  const IpAddress* wifi = kAlternateAddrs;
  const IpAddress* cellular = kPublicAddrs;
  f.AddAddress(0, wifi[0], "test_wifi0", ADAPTER_TYPE_WIFI);
  f.AddAddress(0, cellular[0], "test_cell0", ADAPTER_TYPE_CELLULAR);
  f.AddAddress(1, wifi[1], "test_wifi1", ADAPTER_TYPE_WIFI);
  f.AddAddress(1, cellular[1], "test_cell1", ADAPTER_TYPE_CELLULAR);
  f.SetAllocatorFlags(0, kOnlyLocalPorts);
  f.SetAllocatorFlags(1, kOnlyLocalPorts);
  IceConfig config = CreateIceConfig(TimeDelta::Seconds(1), GATHER_CONTINUALLY);
  config.regather_on_failed_networks_interval = TimeDelta::Seconds(2);
  f.CreateChannels(config, config);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.CheckCandidatePairAndConnected(wifi[0], wifi[1]); }, kMediumTimeout));

  f.DestroyAllButBestConnection(f.ch(0));
  SM_EXPECT_TRUE(
      f.RunUntil([&] { return f.GetConnectionWithLocalAddress(f.ch(0), cellular[0]) == nullptr; }, kMediumTimeout));
  SM_EXPECT_TRUE(f.RunUntil(
      [&] {
        const Connection* c = f.GetConnectionWithLocalAddress(f.ch(0), cellular[0]);
        return c && c != f.ch(0)->selected_connection() && c->writable();
      },
      kDefaultTimeout));
}

// TestVpnDefault: by default the non-VPN interface is chosen.
SM_TEST(P2pTransportChannelMultihomed, TestVpnDefault) {
  Fixture f;
  f.CreatePortAllocators();
  f.AddAddress(0, kPublicAddrs[0], "eth0", ADAPTER_TYPE_ETHERNET);
  f.AddAddress(0, kAlternateAddrs[0], "vpn0", ADAPTER_TYPE_VPN);
  f.AddAddress(1, kPublicAddrs[1]);
  f.CreateChannels();
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.Connected() && !f.ch(0)->selected_connection()->network()->IsVpn(); },
                            kDefaultTimeout));
}

// TestVpnPreferVpn: the VPN is chosen, and left when it is blocked.
SM_TEST(P2pTransportChannelMultihomed, TestVpnPreferVpn) {
  Fixture f;
  f.CreatePortAllocators();
  f.AddAddress(0, kPublicAddrs[0], "eth0", ADAPTER_TYPE_ETHERNET);
  f.AddAddress(0, kAlternateAddrs[0], "vpn0", ADAPTER_TYPE_VPN, ADAPTER_TYPE_CELLULAR);
  f.AddAddress(1, kPublicAddrs[1]);
  IceConfig config;
  config.vpn_preference = VpnPreference::kPreferVpn;
  f.CreateChannels(config, config);
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.Connected() && f.ch(0)->selected_connection()->network()->IsVpn(); },
                            kDefaultTimeout));
  f.Blackhole(kAlternateAddrs[0]);
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.Connected() && !f.ch(0)->selected_connection()->network()->IsVpn(); },
                            kDefaultTimeout));
}

// TestVpnAvoidVpn: the non-VPN interface is chosen, and the VPN taken when it is blocked.
SM_TEST(P2pTransportChannelMultihomed, TestVpnAvoidVpn) {
  Fixture f;
  f.CreatePortAllocators();
  f.AddAddress(0, kPublicAddrs[0], "eth0", ADAPTER_TYPE_CELLULAR);
  f.AddAddress(0, kAlternateAddrs[0], "vpn0", ADAPTER_TYPE_VPN, ADAPTER_TYPE_ETHERNET);
  f.AddAddress(1, kPublicAddrs[1]);
  IceConfig config;
  config.vpn_preference = VpnPreference::kAvoidVpn;
  f.CreateChannels(config, config);
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.Connected() && !f.ch(0)->selected_connection()->network()->IsVpn(); },
                            kDefaultTimeout));
  f.Blackhole(kPublicAddrs[0]);
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.Connected() && f.ch(0)->selected_connection()->network()->IsVpn(); },
                            kDefaultTimeout));
}

// TestVpnNeverVpn: the VPN is never used, so blocking the other interface disconnects.
SM_TEST(P2pTransportChannelMultihomed, TestVpnNeverVpn) {
  Fixture f;
  f.CreatePortAllocators();
  f.AddAddress(0, kPublicAddrs[0], "eth0", ADAPTER_TYPE_CELLULAR);
  f.AddAddress(0, kAlternateAddrs[0], "vpn0", ADAPTER_TYPE_VPN, ADAPTER_TYPE_ETHERNET);
  f.AddAddress(1, kPublicAddrs[1]);
  IceConfig config;
  config.vpn_preference = VpnPreference::kNeverUseVpn;
  f.CreateChannels(config, config);
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.Connected() && !f.ch(0)->selected_connection()->network()->IsVpn(); },
                            kDefaultTimeout));
  f.Blackhole(kPublicAddrs[0]);
  f.Run(kDefaultTimeout);
  SM_EXPECT_TRUE(f.RunUntil([&] { return !f.Connected(); }, kDefaultTimeout));
}

// TestVpnOnlyVpn: only the VPN is used, so blocking it disconnects.
SM_TEST(P2pTransportChannelMultihomed, TestVpnOnlyVpn) {
  Fixture f;
  f.CreatePortAllocators();
  f.AddAddress(0, kPublicAddrs[0], "eth0", ADAPTER_TYPE_CELLULAR);
  f.AddAddress(0, kAlternateAddrs[0], "vpn0", ADAPTER_TYPE_VPN, ADAPTER_TYPE_ETHERNET);
  f.AddAddress(1, kPublicAddrs[1]);
  IceConfig config;
  config.vpn_preference = VpnPreference::kOnlyUseVpn;
  f.CreateChannels(config, config);
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.Connected() && f.ch(0)->selected_connection()->network()->IsVpn(); },
                            kDefaultTimeout));
  f.Blackhole(kAlternateAddrs[0]);
  f.Run(kDefaultTimeout);
  SM_EXPECT_TRUE(f.RunUntil([&] { return !f.Connected(); }, kDefaultTimeout));
}

// The firewall and the multi-interface host themselves: a blocked address neither sends nor receives (datagrams in
// flight included), the others carry on, and ClearBlockedAddresses restores it.
SM_TEST(P2pTransportChannelMultihomed, FirewallBlackholesOneInterface) {
  sim::ByteNetwork net;
  sim::EmulatedMultiHomedHost a(net);
  sim::EmulatedMultiHomedHost b(net);
  a.AddInterface(kPublicAddrs[0], sim::ByteNetwork::kPublicRealm, Link(10), Link(10));
  a.AddInterface(kAlternateAddrs[0], sim::ByteNetwork::kPublicRealm, Link(10), Link(10));
  b.AddInterface(kPublicAddrs[1], sim::ByteNetwork::kPublicRealm, Link(10), Link(10));
  SM_EXPECT_EQ(a.interface_count(), size_t{2});
  auto s0 = a.CreateUdpSocket(SocketAddress(kPublicAddrs[0], 0), 0, 0);
  auto s1 = a.CreateUdpSocket(SocketAddress(kAlternateAddrs[0], 0), 0, 0);
  auto any = a.CreateUdpSocket(SocketAddress(Ip("0.0.0.0"), 0), 0, 0);
  auto none = a.CreateUdpSocket(SocketAddress(Ip("1.2.3.4"), 0), 0, 0);
  auto r = b.CreateUdpSocket(SocketAddress(kPublicAddrs[1], 5000), 0, 0);
  SM_ASSERT_TRUE(s0 && s1 && any && r);
  SM_EXPECT_TRUE(none == nullptr);
  SM_EXPECT_TRUE(s1->GetLocalAddress().ipaddr() == kAlternateAddrs[0]);
  std::vector<IpAddress> from;
  r->RegisterReceivedPacketCallback([&](AsyncPacketSocket*, const ReceivedPacket& p) {
    from.push_back(p.source_address.ipaddr());
  });
  const uint8_t data[4] = {1, 2, 3, 4};
  Timestamp now = Timestamp::Seconds(1);
  auto run = [&](TimeDelta d) {
    const Timestamp until = now + d;
    while (true) {
      const Timestamp t = std::min(a.NextEvent(), b.NextEvent());
      if (t > until) break;
      now = std::max(now, t);
      a.Deliver(now);
      b.Deliver(now);
    }
    now = until;
  };
  s0->SendTo(now, data, r->GetLocalAddress(), PacketOptions{});
  s1->SendTo(now, data, r->GetLocalAddress(), PacketOptions{});
  run(TimeDelta::Millis(5));  // Both are in flight now.
  net.BlockAddress(kPublicAddrs[0].v4());
  run(TimeDelta::Millis(100));
  SM_ASSERT_EQ(from.size(), size_t{1});
  SM_EXPECT_TRUE(from[0] == kAlternateAddrs[0]);
  s0->SendTo(now, data, r->GetLocalAddress(), PacketOptions{});
  run(TimeDelta::Millis(100));
  SM_EXPECT_EQ(from.size(), size_t{1});
  SM_EXPECT_EQ(net.stats().firewall_dropped, uint64_t{2});
  net.ClearBlockedAddresses();
  s0->SendTo(now, data, r->GetLocalAddress(), PacketOptions{});
  any->SendTo(now, data, r->GetLocalAddress(), PacketOptions{});
  run(TimeDelta::Millis(100));
  SM_ASSERT_EQ(from.size(), size_t{3});
  SM_EXPECT_TRUE(from[1] == kPublicAddrs[0]);
  SM_EXPECT_TRUE(from[2] == kPublicAddrs[0]);  // The any address sends from the first interface.
}
