// The ICE agent (issue #1002 part 2): cases of libwebrtc M157's p2p/base/p2p_transport_channel_unittest.cc
// (P2PTransportChannelTest, the two-endpoint tests) without TURN or TCP, over the emulated network (#985) with its
// NATs and a STUN server, with the library's time model in place of the simulated clock. Each test names the
// libwebrtc case it follows. Candidates go to the other side through the test's signaling queue, delivered on the
// next step (libwebrtc posts them). The hostname (mDNS) cases (#1152) give an endpoint the deterministic resolver
// (FakeAsyncDnsResolverFactory, answered on the next step as libwebrtc's resolver answers later) and stand in for
// the gathering side's mDNS responder by signaling its host candidates under a `.local` name.
#include <algorithm>
#include <cerrno>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/async_dns_resolver.h"
#include "sm/p2p/basic_port_allocator.h"
#include "sm/p2p/candidate.h"
#include "sm/p2p/network_manager.h"
#include "sm/p2p/p2p_constants.h"
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

// kPublicAddrs and kPrivateAddrs of the libwebrtc fixture.
const IpAddress kPublicIp[2] = {Ip("11.11.11.11"), Ip("22.22.22.22")};
const IpAddress kPrivateIp[2] = {Ip("192.168.1.11"), Ip("192.168.2.22")};
const IpAddress kNatIp[2] = {Ip("77.77.77.77"), Ip("88.88.88.88")};
const SocketAddress kStunAddr(Ip("99.99.99.1"), 3478);  // STUN_SERVER_PORT.

const char* kIceUfrag[4] = {"UF00", "UF01", "UF02", "UF03"};
const char* kIcePwd[4] = {"TESTICEPWD00000000000000", "TESTICEPWD00000000000001", "TESTICEPWD00000000000002",
                          "TESTICEPWD00000000000003"};
const IceParameters kIceParams[4] = {{kIceUfrag[0], kIcePwd[0], false},
                                     {kIceUfrag[1], kIcePwd[1], false},
                                     {kIceUfrag[2], kIcePwd[2], false},
                                     {kIceUfrag[3], kIcePwd[3], false}};

constexpr TimeDelta kShortTimeout = TimeDelta::Seconds(1);
constexpr TimeDelta kMediumTimeout = TimeDelta::Seconds(3);
constexpr TimeDelta kDefaultTimeout = TimeDelta::Seconds(10);

enum Config { OPEN, NAT_FULL_CONE, NAT_ADDR_RESTRICTED, NAT_PORT_RESTRICTED, NAT_SYMMETRIC };
enum RemoteIceParameterSource { FROM_CANDIDATE, FROM_SETICEPARAMETERS };

// kOnlyLocalPorts: host candidates only.
constexpr uint32_t kOnlyLocalPorts = PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY | PORTALLOCATOR_DISABLE_TCP;

sim::LinkConfig Link(int delay_ms) {
  sim::LinkConfig c;
  c.base_delay = TimeDelta::Millis(delay_ms);
  return c;
}

struct Endpoint {
  std::unique_ptr<sim::EmulatedSocketServer> host;
  sim::ByteNetwork::HostId host_id = 0;
  StaticNetworkManager network_manager;
  std::unique_ptr<BasicPortAllocator> allocator;
  std::unique_ptr<P2PTransportChannel> ch;
  IceRole role = ICEROLE_UNKNOWN;
  bool role_conflict = false;
  bool ready_to_send = false;
  bool save_candidates = false;
  std::vector<Candidate> saved_candidates;
  std::vector<std::string> received;
  std::vector<std::optional<NetworkRoute>> routes;
  std::vector<IceGatheringState> gathering;
  std::vector<CandidatePairChangeEvent> pair_changes;
  // set_async_dns_resolver_factory: the channel resolves remote hostname candidates with it.
  AsyncDnsResolverFactoryInterface* resolver_factory = nullptr;
  // The FakeMdnsResponder's part: host candidates are signaled under a .local name (the IP it stands for kept here).
  bool mdns_names = false;
  std::vector<std::pair<std::string, IpAddress>> mdns_hosts;
};

struct Fixture {
  sim::ByteNetwork net;
  std::unique_ptr<sim::EmulatedSocketServer> stun_host;
  std::unique_ptr<sim::StunServer> stun;
  Endpoint ep[2];
  RemoteIceParameterSource remote_ice_parameter_source = FROM_CANDIDATE;
  // The signaling channel: candidates for endpoint i.
  std::deque<Candidate> signaling[2];
  // ResolverFactoryFixture: the resolver of the tests that resolve hostname candidates.
  FakeAsyncDnsResolverFactory resolver;
  Timestamp now = Timestamp::Seconds(10);

  Fixture() {
    testing::InstallTestCrypto();
    stun_host = std::make_unique<sim::EmulatedSocketServer>(
        net, net.AddHost(kStunAddr.ip(), sim::ByteNetwork::kPublicRealm, Link(5), Link(5)));
    stun = std::make_unique<sim::StunServer>(stun_host->CreateUdpSocket(kStunAddr, 0, 0));
    ep[0].role = ICEROLE_CONTROLLING;
    ep[1].role = ICEROLE_CONTROLLED;
  }
  ~Fixture() { DestroyChannels(); }

  // ConfigureEndpoints: each endpoint public or behind its own NAT, with the allocator's flags.
  void ConfigureEndpoints(Config c0, Config c1, uint32_t flags0 = kDefaultPortAllocatorFlags,
                          uint32_t flags1 = kDefaultPortAllocatorFlags) {
    const Config configs[2] = {c0, c1};
    const uint32_t flags[2] = {flags0, flags1};
    for (int i = 0; i < 2; ++i) {
      int realm = sim::ByteNetwork::kPublicRealm;
      IpAddress ip = kPublicIp[i];
      if (configs[i] != OPEN) {
        sim::NatConfig cfg;
        cfg.type = configs[i] == NAT_FULL_CONE         ? sim::NatType::kOpenCone
                   : configs[i] == NAT_ADDR_RESTRICTED ? sim::NatType::kAddrRestricted
                   : configs[i] == NAT_PORT_RESTRICTED ? sim::NatType::kPortRestricted
                                                       : sim::NatType::kSymmetric;
        cfg.external_ip = kNatIp[i].v4();
        realm = net.NatRealm(net.AddNat(cfg));
        ip = kPrivateIp[i];
      }
      Endpoint& e = ep[i];
      e.host_id = net.AddHost(ip.v4(), realm, Link(10), Link(10));
      e.host = std::make_unique<sim::EmulatedSocketServer>(net, e.host_id);
      e.network_manager.AddInterface(ip);
      e.allocator = std::make_unique<BasicPortAllocator>(&e.network_manager, e.host.get());
      e.allocator->SetConfiguration(now, ServerAddresses{kStunAddr}, {}, 0, NO_PRUNE);
      e.allocator->set_flags(flags[i]);
      e.allocator->set_step_delay(kMinimumStepDelay);
    }
    remote_ice_parameter_source = FROM_SETICEPARAMETERS;
  }

  std::unique_ptr<P2PTransportChannel> CreateChannel(int i, const IceParameters& local_ice,
                                                     const IceParameters& remote_ice) {
    P2PTransportChannel::Options options;
    options.async_dns_resolver_factory = ep[i].resolver_factory;
    auto channel = std::make_unique<P2PTransportChannel>(now, "test content name", ICE_CANDIDATE_COMPONENT_RTP,
                                                         ep[i].allocator.get(), options);
    Endpoint& e = ep[i];
    channel->SubscribeReadyToSend(this, [&e](PacketTransportInternal*, Timestamp) { e.ready_to_send = true; });
    channel->SubscribeCandidateGathered(this, [this, i](IceTransportInternal*, const Candidate& gathered) {
      Candidate c = gathered;
      if (ep[i].mdns_names && c.is_local()) {
        // MaybeObfuscateAddress: the address becomes a random name in .local; the port is kept.
        const std::string name = "ep" + std::to_string(i) + "-" + std::to_string(ep[i].mdns_hosts.size()) +
                                 "-1f2e3d4c-5b6a-7980.local";
        ep[i].mdns_hosts.emplace_back(name, c.address().ipaddr());
        c.set_address(SocketAddress(name, c.address().port()));
      }
      if (ep[i].save_candidates) {
        ep[i].saved_candidates.push_back(c);
      } else {
        signaling[1 - i].push_back(c);
      }
    });
    channel->RegisterReceivedPacketCallback(this, [&e](PacketTransportInternal*, const ReceivedPacket& p) {
      e.received.emplace_back(p.payload.begin(), p.payload.end());
    });
    channel->SubscribeRoleConflict(this, [&e](IceTransportInternal* t) {
      e.role_conflict = true;
      e.role = e.role == ICEROLE_CONTROLLING ? ICEROLE_CONTROLLED : ICEROLE_CONTROLLING;
      static_cast<P2PTransportChannel*>(t)->SetIceRole(e.role);
    });
    channel->SubscribeNetworkRouteChanged(this, [&e](std::optional<NetworkRoute> r) { e.routes.push_back(r); });
    channel->AddGatheringStateCallback(
        this, [&e](IceTransportInternal* t) { e.gathering.push_back(t->gathering_state()); });
    channel->SetCandidatePairChangeCallback([&e](const CandidatePairChangeEvent& ev) { e.pair_changes.push_back(ev); });
    channel->SetIceParameters(local_ice);
    if (remote_ice_parameter_source == FROM_SETICEPARAMETERS) channel->SetRemoteIceParameters(remote_ice);
    channel->SetIceRole(e.role);
    return channel;
  }

  void CreateChannels() {
    ep[0].ch = CreateChannel(0, kIceParams[0], kIceParams[1]);
    ep[1].ch = CreateChannel(1, kIceParams[1], kIceParams[0]);
    ep[0].ch->MaybeStartGathering(now);
    ep[1].ch->MaybeStartGathering(now);
  }
  void DestroyChannels() {
    ep[0].ch.reset();
    ep[1].ch.reset();
  }
  P2PTransportChannel* ch(int i) { return ep[i].ch.get(); }

  // PauseCandidates / ResumeCandidates.
  void PauseCandidates(int i) { ep[i].save_candidates = true; }
  void ResumeCandidates(int i) {
    for (const Candidate& c : ep[i].saved_candidates) signaling[1 - i].push_back(c);
    ep[i].saved_candidates.clear();
    ep[i].save_candidates = false;
  }

  void DeliverCandidates() {
    for (int i = 0; i < 2; ++i) {
      while (!signaling[i].empty()) {
        Candidate c = signaling[i].front();
        signaling[i].pop_front();
        if (!ep[i].ch) continue;
        if (remote_ice_parameter_source != FROM_CANDIDATE) {
          c.set_username("");
          c.set_password("");
        }
        ep[i].ch->AddRemoteCandidate(now, c);
      }
    }
  }

  Timestamp NextEvent() const {
    Timestamp t = net.NextEvent();
    for (const Endpoint& e : ep) {
      if (e.ch) t = std::min(t, e.ch->NextProcessTime());
      if (e.allocator) t = std::min(t, e.allocator->NextPooledSessionsProcessTime());
    }
    if (!signaling[0].empty() || !signaling[1].empty()) t = std::min(t, now);
    t = std::min(t, resolver.NextProcessTime(now));
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
    resolver.Process(now);
  }
  // Runs until `stop` holds, at most `limit` of emulated time.
  template <class F>
  bool RunUntil(F stop, TimeDelta limit) {
    const Timestamp until = now + limit;
    for (int guard = 0; guard < 2000000; ++guard) {
      if (stop()) return true;
      const Timestamp t = NextEvent();
      if (t > until) {
        now = until;
        return stop();
      }
      Step(t);
    }
    return stop();
  }
  void Run(TimeDelta d) {
    RunUntil([] { return false; }, d);
  }

  bool CheckConnected(P2PTransportChannel* a, P2PTransportChannel* b) const {
    return a->receiving() && a->writable() && b->receiving() && b->writable() && a->selected_connection() &&
           b->selected_connection();
  }
  bool Connected() { return CheckConnected(ch(0), ch(1)); }

  const Candidate* LocalCandidate(P2PTransportChannel* c) const {
    return c->selected_connection() ? &c->selected_connection()->local_candidate() : nullptr;
  }
  const Candidate* RemoteCandidate(P2PTransportChannel* c) const {
    return c->selected_connection() ? &c->selected_connection()->remote_candidate() : nullptr;
  }

  int SendData(int i, std::string_view data) {
    return ep[i].ch->SendPacket(now, std::span(reinterpret_cast<const uint8_t*>(data.data()), data.size()),
                                PacketOptions{}, 0);
  }
  bool CheckDataOnChannel(int i, std::string_view data) {
    return std::find(ep[i].received.begin(), ep[i].received.end(), data) != ep[i].received.end();
  }
  // TestSendRecv: 10 packets each way.
  bool TestSendRecv() {
    for (int i = 0; i < 10; ++i) {
      const std::string data = "ABCDEFGHIJKLMNOPQRSTUVWXYZ" + std::to_string(i);
      for (int s = 0; s < 2; ++s) {
        if (SendData(s, data) != static_cast<int>(data.size())) return false;
        if (!RunUntil([&] { return CheckDataOnChannel(1 - s, data); }, kMediumTimeout)) return false;
      }
    }
    return true;
  }
};

}  // namespace

// IceCredentialsChanged: a change of either the ufrag or the password is an ICE restart.
SM_TEST(P2pTransportChannel, IceCredentialsChanged) {
  SM_EXPECT_FALSE(IceCredentialsChanged("u", "p", "u", "p"));
  SM_EXPECT_TRUE(IceCredentialsChanged("u", "p", "v", "p"));
  SM_EXPECT_TRUE(IceCredentialsChanged("u", "p", "u", "q"));
}

// The constructor's IceConfig (p2p_transport_channel.cc:177-184) and check_receiving_interval.
SM_TEST(P2pTransportChannel, DefaultConfig) {
  Fixture f;
  f.ConfigureEndpoints(OPEN, OPEN);
  P2PTransportChannel channel(f.now, "test", 1, f.ep[0].allocator.get());
  const IceConfig& c = channel.config();
  SM_EXPECT_TRUE(c.receiving_timeout == kReceivingTimeout);
  SM_EXPECT_TRUE(c.backup_connection_ping_interval == kBackupConnectionPingInterval);
  SM_EXPECT_FALSE(c.gather_continually());
  SM_EXPECT_FALSE(c.prioritize_most_likely_candidate_pairs);
  SM_EXPECT_TRUE(c.stable_writable_connection_ping_interval == kStrongAndStableWritableConnectionPingInterval);
  SM_EXPECT_TRUE(c.presume_writable_when_fully_relayed);
  SM_EXPECT_TRUE(c.regather_on_failed_networks_interval == kRegatherOnFailedNetworksInterval);
  SM_EXPECT_TRUE(c.receiving_switching_delay == kReceivingSwitchingDelay);
  SM_EXPECT_TRUE(c.default_nomination_mode == NominationMode::SEMI_AGGRESSIVE);
  SM_EXPECT_FALSE(c.IsValid().has_value());
  SM_EXPECT_EQ(channel.check_receiving_interval().ms(), int64_t{250});  // 2500 / 10.
  SM_EXPECT_TRUE(channel.GetState() == IceTransportStateInternal::kInit);
  SM_EXPECT_TRUE(channel.GetIceTransportState() == IceTransportState::kNew);
  SM_EXPECT_TRUE(channel.gathering_state() == kIceGatheringNew);
  SM_EXPECT_EQ(channel.ice_field_trials().dead_connection_timeout_ms, 30000);
  // MaybeStartGathering without ICE parameters does nothing.
  channel.MaybeStartGathering(f.now);
  SM_EXPECT_TRUE(channel.allocator_session() == nullptr);
  // ReadyToSend: no selected connection, so ENOTCONN.
  const uint8_t data[1] = {0};
  SM_EXPECT_EQ(channel.SendPacket(f.now, data, PacketOptions{}, 0), -1);
  SM_EXPECT_EQ(channel.GetError(), ENOTCONN);
  SM_EXPECT_EQ(channel.SendPacket(f.now, data, PacketOptions{}, 1), -1);
  SM_EXPECT_EQ(channel.GetError(), EINVAL);
  SM_EXPECT_FALSE(channel.GetRttEstimate().has_value());
}

// TestBasic (the P2PTransportChannelTest matrix's OPEN-OPEN cell): both sides connect on host candidates, the
// controlling side nominates, both reach COMPLETED, and data goes both ways.
SM_TEST(P2pTransportChannel, OpenToOpenConnectsAndSendsData) {
  Fixture f;
  f.ConfigureEndpoints(OPEN, OPEN);
  f.CreateChannels();
  SM_EXPECT_TRUE(f.ch(0)->gathering_state() == kIceGatheringGathering);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected(); }, kMediumTimeout));
  SM_EXPECT_TRUE(f.LocalCandidate(f.ch(0))->is_local());
  SM_EXPECT_TRUE(f.RemoteCandidate(f.ch(0))->is_local());
  SM_EXPECT_TRUE(f.RemoteCandidate(f.ch(0))->address().ipaddr() == kPublicIp[1]);
  SM_EXPECT_TRUE(f.RunUntil(
      [&] {
        return f.ch(0)->GetState() == IceTransportStateInternal::kCompleted &&
               f.ch(1)->GetState() == IceTransportStateInternal::kCompleted;
      },
      kDefaultTimeout));
  SM_EXPECT_TRUE(f.ch(0)->GetIceTransportState() == IceTransportState::kConnected);
  SM_EXPECT_TRUE(f.ch(1)->selected_connection()->nominated());
  SM_EXPECT_TRUE(f.ep[0].ready_to_send && f.ep[1].ready_to_send);
  SM_EXPECT_TRUE(f.TestSendRecv());
  // Gathering completes (gather once).
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.ch(0)->gathering_state() == kIceGatheringComplete; }, kDefaultTimeout));
  SM_EXPECT_TRUE(f.ep[0].gathering.size() >= 2 && f.ep[0].gathering.front() == kIceGatheringGathering);
  // The RTT estimate of the selected connection: twice the 20 ms one-way path, roughly.
  SM_ASSERT_TRUE(f.ch(0)->GetRttEstimate().has_value());
  SM_EXPECT_GE(*f.ch(0)->GetRttEstimate(), 40);
  SM_EXPECT_LE(*f.ch(0)->GetRttEstimate(), 3000);
  // The network route: connected, UDP over IPv4 (20 + 8 bytes).
  SM_ASSERT_TRUE(f.ch(0)->network_route().has_value());
  SM_EXPECT_TRUE(f.ch(0)->network_route()->connected);
  SM_EXPECT_EQ(f.ch(0)->network_route()->packet_overhead, 28);
  SM_EXPECT_FALSE(f.ch(0)->network_route()->local.uses_turn);
  SM_ASSERT_TRUE(!f.ep[0].routes.empty());
  SM_EXPECT_TRUE(f.ep[0].routes.back() == f.ch(0)->network_route());
  SM_ASSERT_TRUE(!f.ep[0].pair_changes.empty());
  SM_EXPECT_EQ(f.ep[0].pair_changes.back().transport_name, std::string("test content name"));
}

// GetStats: the selected pair is the best connection, and the counters follow what was sent and received.
SM_TEST(P2pTransportChannel, GetStats) {
  Fixture f;
  f.ConfigureEndpoints(OPEN, OPEN);
  f.CreateChannels();
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected(); }, kMediumTimeout));
  SM_ASSERT_TRUE(f.TestSendRecv());
  IceTransportStats stats;
  SM_ASSERT_TRUE(f.ch(0)->GetStats(f.now, &stats));
  SM_ASSERT_GE(stats.connection_infos.size(), size_t{1});
  int best = 0;
  for (const ConnectionInfo& info : stats.connection_infos) {
    if (!info.best_connection) continue;
    ++best;
    SM_EXPECT_TRUE(info.local_candidate.address() == f.LocalCandidate(f.ch(0))->address());
    SM_EXPECT_TRUE(info.remote_candidate.address() == f.RemoteCandidate(f.ch(0))->address());
    SM_EXPECT_TRUE(info.receiving && info.writable);
  }
  SM_EXPECT_EQ(best, 1);
  SM_EXPECT_EQ(stats.selected_candidate_pair_changes, uint32_t{1});
  SM_EXPECT_EQ(stats.packets_sent, uint64_t{10});
  SM_EXPECT_EQ(stats.packets_received, uint64_t{10});
  SM_EXPECT_EQ(stats.bytes_sent, uint64_t{10 * 27});
  SM_EXPECT_EQ(stats.bytes_received, uint64_t{10 * 27});
  SM_EXPECT_TRUE(stats.ice_role == ICEROLE_CONTROLLING);
  SM_EXPECT_EQ(stats.ice_local_username_fragment, std::string(kIceUfrag[0]));
  SM_EXPECT_TRUE(stats.ice_state == IceTransportState::kConnected);
}

// The matrix's NAT cells without TURN: through a port-restricted NAT (srflx candidate) and a symmetric NAT (the
// open side learns a prflx candidate).
SM_TEST(P2pTransportChannel, ConnectsThroughNats) {
  for (Config c : {NAT_FULL_CONE, NAT_ADDR_RESTRICTED, NAT_PORT_RESTRICTED, NAT_SYMMETRIC}) {
    Fixture f;
    f.ConfigureEndpoints(c, OPEN);
    f.CreateChannels();
    SM_EXPECT_TRUE(f.RunUntil([&] { return f.Connected(); }, kDefaultTimeout));
    SM_EXPECT_TRUE(f.TestSendRecv());
    if (c == NAT_SYMMETRIC) {
      SM_ASSERT_TRUE(f.RemoteCandidate(f.ch(1)) != nullptr);
      SM_EXPECT_TRUE(f.RemoteCandidate(f.ch(1))->is_prflx());
    }
  }
  // Both behind cone NATs: the srflx candidates connect.
  Fixture f;
  f.ConfigureEndpoints(NAT_FULL_CONE, NAT_PORT_RESTRICTED);
  f.CreateChannels();
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.Connected(); }, kDefaultTimeout));
  SM_EXPECT_TRUE(f.TestSendRecv());
}

// PeerReflexiveCandidateBeforeSignaling: ep1 gets a check before it knows ep2's credentials or candidates, pairs with
// a prflx candidate of generation 1 (the second set of remote credentials), and keeps that connection when the
// signaled host candidate turns out to have the same address (it becomes the host candidate).
SM_TEST(P2pTransportChannel, PeerReflexiveCandidateBeforeSignaling) {
  Fixture f;
  f.ConfigureEndpoints(OPEN, OPEN);
  f.remote_ice_parameter_source = FROM_CANDIDATE;
  f.CreateChannels();
  // Only have remote parameters come in for ep2, not ep1.
  f.ch(1)->SetRemoteIceParameters(kIceParams[0]);
  // Pause sending ep2's candidates to ep1 until ep1 receives the peer reflexive candidate.
  f.PauseCandidates(1);
  // Wait until the callee becomes writable to make sure that a ping request is received by the caller before their
  // remote ICE credentials are set.
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.ch(1)->selected_connection() != nullptr; }, TimeDelta::Seconds(11)));
  // Add two sets of remote ICE credentials, so that the ones used by the candidate will be generation 1 instead of 0.
  f.ch(0)->SetRemoteIceParameters(kIceParams[3]);
  f.ch(0)->SetRemoteIceParameters(kIceParams[1]);
  // The caller should have the selected connection connected to the peer reflexive candidate.
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.ch(0)->selected_connection() != nullptr; }, kMediumTimeout));
  const Connection* selected_connection = f.ch(0)->selected_connection();
  SM_EXPECT_TRUE(selected_connection->remote_candidate().is_prflx());
  SM_EXPECT_EQ(selected_connection->remote_candidate().username(), std::string(kIceUfrag[1]));
  SM_EXPECT_EQ(selected_connection->remote_candidate().password(), std::string(kIcePwd[1]));
  SM_EXPECT_EQ(selected_connection->remote_candidate().generation(), uint32_t{1});

  f.ResumeCandidates(1);
  // Verify ep1's selected connection is updated to use the 'local' candidate.
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.ch(0)->selected_connection()->remote_candidate().is_local(); },
                            kMediumTimeout));
  SM_EXPECT_TRUE(f.ch(0)->selected_connection() == selected_connection);
}

// PeerReflexiveRemoteCandidateIsSanitized: a prflx remote candidate's address is hidden from the selected candidate
// pair and the stats until signaling brings the same address.
SM_TEST(P2pTransportChannel, PeerReflexiveRemoteCandidateIsSanitized) {
  Fixture f;
  f.ConfigureEndpoints(OPEN, OPEN, kOnlyLocalPorts, kOnlyLocalPorts);
  // Emulate no remote parameters coming in.
  f.remote_ice_parameter_source = FROM_CANDIDATE;
  f.CreateChannels();
  // Only have remote parameters come in for ep2, not ep1.
  f.ch(1)->SetRemoteIceParameters(kIceParams[0]);
  // Pause sending ep2's candidates to ep1 until ep1 receives the peer reflexive candidate.
  f.PauseCandidates(1);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.ch(1)->selected_connection() != nullptr; }, kMediumTimeout));
  f.ch(0)->SetRemoteIceParameters(kIceParams[1]);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.ch(0)->selected_connection() != nullptr; }, kMediumTimeout));

  // Check the selected candidate pair.
  const auto pair_ep1 = f.ch(0)->GetSelectedCandidatePair();
  SM_ASSERT_TRUE(pair_ep1.has_value());
  SM_EXPECT_TRUE(pair_ep1->remote_candidate().is_prflx());
  SM_EXPECT_TRUE(pair_ep1->remote_candidate().address().ipaddr().family() == IpFamily::kUnspec);

  IceTransportStats stats;
  f.ch(0)->GetStats(f.now, &stats);
  // Check the candidate pair stats.
  SM_ASSERT_EQ(stats.connection_infos.size(), size_t{1});
  SM_EXPECT_TRUE(stats.connection_infos[0].remote_candidate.is_prflx());
  SM_EXPECT_TRUE(stats.connection_infos[0].remote_candidate.address().ipaddr().family() == IpFamily::kUnspec);

  // Let ep1 receive the remote candidate to update its type from prflx to host.
  f.ResumeCandidates(1);
  SM_ASSERT_TRUE(f.RunUntil(
      [&] {
        return f.ch(0)->selected_connection() != nullptr &&
               f.ch(0)->selected_connection()->remote_candidate().is_local();
      },
      kMediumTimeout));

  // We should be able to reveal the address after it is learnt via AddIceCandidate.
  const auto updated_pair_ep1 = f.ch(0)->GetSelectedCandidatePair();
  SM_ASSERT_TRUE(updated_pair_ep1.has_value());
  SM_EXPECT_TRUE(updated_pair_ep1->remote_candidate().is_local());
  SM_EXPECT_TRUE(updated_pair_ep1->remote_candidate().address().ipaddr() == kPublicIp[1]);
  f.ch(0)->GetStats(f.now, &stats);
  SM_ASSERT_EQ(stats.connection_infos.size(), size_t{1});
  SM_EXPECT_TRUE(stats.connection_infos[0].remote_candidate.address().ipaddr() == kPublicIp[1]);
}

// RemoteCandidatesWithoutUfragPwd: candidates without credentials take the remote ICE parameters; the selected
// connection does not change once made.
SM_TEST(P2pTransportChannel, RemoteCandidatesWithoutUfragPwd) {
  Fixture f;
  f.remote_ice_parameter_source = FROM_SETICEPARAMETERS;
  f.ConfigureEndpoints(OPEN, OPEN);
  f.CreateChannels();
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.ch(1)->selected_connection() != nullptr; }, kDefaultTimeout));
  const Connection* selected_connection = f.ch(1)->selected_connection();
  // Wait to make sure the selected connection is not changed.
  f.RunUntil([&] { return f.ch(1)->selected_connection() != selected_connection; }, kMediumTimeout);
  SM_EXPECT_TRUE(f.ch(1)->selected_connection() == selected_connection);
  SM_EXPECT_EQ(f.ch(1)->remote_candidates().front().username(), std::string(kIceUfrag[0]));
  SM_EXPECT_EQ(f.ch(1)->remote_candidates().front().password(), std::string(kIcePwd[0]));
}

// HandleUfragPwdChange and HandleUfragPwdChangeSymmetricNat (without TURN: prflx on the open side): an ICE restart on
// both sides moves both to generation-1 candidates.
SM_TEST(P2pTransportChannel, HandleUfragPwdChange) {
  for (Config c : {OPEN, NAT_SYMMETRIC}) {
    Fixture f;
    f.ConfigureEndpoints(c, OPEN);
    f.CreateChannels();
    f.ch(0)->SetRemoteIceParameters(kIceParams[1]);
    f.ch(1)->SetRemoteIceParameters(kIceParams[0]);
    SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected(); }, kDefaultTimeout));

    const uint32_t old_local1 = f.LocalCandidate(f.ch(0))->generation();
    const uint32_t old_local2 = f.LocalCandidate(f.ch(1))->generation();

    f.ch(0)->SetIceParameters(kIceParams[2]);
    f.ch(0)->SetRemoteIceParameters(kIceParams[3]);
    f.ch(0)->MaybeStartGathering(f.now);
    f.ch(1)->SetIceParameters(kIceParams[3]);
    f.ch(1)->SetRemoteIceParameters(kIceParams[2]);
    f.ch(1)->MaybeStartGathering(f.now);

    SM_EXPECT_TRUE(f.RunUntil([&] { return f.LocalCandidate(f.ch(0))->generation() != old_local1; }, kMediumTimeout));
    SM_EXPECT_TRUE(f.RunUntil([&] { return f.LocalCandidate(f.ch(1))->generation() != old_local2; }, kMediumTimeout));
    SM_EXPECT_TRUE(f.RunUntil([&] { return f.RemoteCandidate(f.ch(0))->generation() == 1; }, kMediumTimeout));
    SM_EXPECT_TRUE(f.RunUntil([&] { return f.RemoteCandidate(f.ch(1))->generation() == 1; }, kMediumTimeout));
    SM_EXPECT_EQ(f.RemoteCandidate(f.ch(1))->generation(), uint32_t{1});
    SM_EXPECT_EQ(f.RemoteCandidate(f.ch(0))->generation(), uint32_t{1});
    // The old session's ports are pruned; the new session's are in use.
    SM_EXPECT_GE(f.ch(0)->pruned_ports().size(), size_t{1});
    SM_EXPECT_EQ(f.ch(0)->allocator_session()->generation(), uint32_t{1});
    SM_EXPECT_TRUE(f.TestSendRecv());
  }
}

// IncomingOnlyBlocked: a side behind a NAT that may only take incoming connections can't be reached.
SM_TEST(P2pTransportChannel, IncomingOnlyBlocked) {
  Fixture f;
  f.ConfigureEndpoints(NAT_FULL_CONE, OPEN);
  f.ep[0].allocator->set_flags(kOnlyLocalPorts);
  f.CreateChannels();
  f.ch(0)->set_incoming_only(true);
  // Pump for 1 second and verify that the channels are not connected.
  f.Run(kShortTimeout);
  SM_EXPECT_FALSE(f.ch(0)->receiving());
  SM_EXPECT_FALSE(f.ch(0)->writable());
  SM_EXPECT_FALSE(f.ch(1)->receiving());
  SM_EXPECT_FALSE(f.ch(1)->writable());
}

// IncomingOnlyOpen: a side behind a NAT reaches an open side that only takes incoming connections.
SM_TEST(P2pTransportChannel, IncomingOnlyOpen) {
  Fixture f;
  f.ConfigureEndpoints(OPEN, NAT_FULL_CONE);
  f.ep[0].allocator->set_flags(kOnlyLocalPorts);
  f.CreateChannels();
  f.ch(0)->set_incoming_only(true);
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.Connected(); }, kDefaultTimeout));
}

// TestIceRoleConflict: both start controlling; the one with the lower tie-breaker gets the role conflict, switches,
// and they connect.
SM_TEST(P2pTransportChannel, TestIceRoleConflict) {
  Fixture f;
  f.ConfigureEndpoints(OPEN, OPEN);
  f.ep[0].role = ICEROLE_CONTROLLING;
  f.ep[1].role = ICEROLE_CONTROLLING;
  f.CreateChannels();
  const bool first_lower = f.ep[0].allocator->ice_tiebreaker() < f.ep[1].allocator->ice_tiebreaker();
  // Since both the channels initiated with controlling state, the channel with the lower tiebreaker should receive
  // SignalRoleConflict.
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.ep[first_lower ? 0 : 1].role_conflict; }, kMediumTimeout));
  SM_EXPECT_FALSE(f.ep[first_lower ? 1 : 0].role_conflict);
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.Connected(); }, kDefaultTimeout));
  SM_EXPECT_TRUE(f.ch(0)->selected_connection() && f.ch(1)->selected_connection());
  SM_EXPECT_TRUE(f.TestSendRecv());
}

// TestIceConfigWillPassDownToPort: the role reaches every port, pruned ones included.
SM_TEST(P2pTransportChannel, TestIceConfigWillPassDownToPort) {
  Fixture f;
  f.ConfigureEndpoints(OPEN, OPEN);
  f.ep[0].role = ICEROLE_CONTROLLING;
  f.ep[1].role = ICEROLE_CONTROLLING;
  f.CreateChannels();
  SM_ASSERT_TRUE(f.RunUntil([&] { return !f.ch(0)->ports().empty() && !f.ch(1)->ports().empty(); }, kMediumTimeout));
  for (Port* p : f.ch(0)->ports()) SM_EXPECT_TRUE(p->GetIceRole() == ICEROLE_CONTROLLING);
  f.ch(1)->SetIceRole(ICEROLE_CONTROLLED);
  for (Port* p : f.ch(1)->ports()) SM_EXPECT_TRUE(p->GetIceRole() == ICEROLE_CONTROLLED);
  f.ep[1].role = ICEROLE_CONTROLLED;
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.Connected(); }, kDefaultTimeout));
}

// TestDefaultDscpValue: options reach the ports and DefaultDscpValue reads them back.
SM_TEST(P2pTransportChannel, TestDefaultDscpValue) {
  Fixture f;
  f.ConfigureEndpoints(OPEN, OPEN);
  f.CreateChannels();
  SM_EXPECT_TRUE(f.ch(0)->DefaultDscpValue() == Dscp::kNoChange);
  f.ch(0)->SetOption(SocketOption::kDscp, static_cast<int>(Dscp::kCs1));
  SM_EXPECT_TRUE(f.ch(0)->DefaultDscpValue() == Dscp::kCs1);
  int value = 0;
  SM_EXPECT_TRUE(f.ch(0)->GetOption(SocketOption::kDscp, &value));
  SM_EXPECT_EQ(value, static_cast<int>(Dscp::kCs1));
  SM_EXPECT_FALSE(f.ch(0)->GetOption(SocketOption::kRcvBuf, &value));
  f.ch(0)->SetOption(SocketOption::kDscp, static_cast<int>(Dscp::kAf41));
  SM_EXPECT_TRUE(f.ch(0)->DefaultDscpValue() == Dscp::kAf41);
}

// TestUsingPooledSessionAfterDoneGathering: the channels take the pooled sessions, whose ports carry the connection.
SM_TEST(P2pTransportChannel, TestUsingPooledSessionAfterDoneGathering) {
  Fixture f;
  f.ConfigureEndpoints(OPEN, OPEN);
  // First create a pooled session for each endpoint.
  for (Endpoint& e : f.ep) e.allocator->SetConfiguration(f.now, e.allocator->stun_servers(), {}, 1, NO_PRUNE);
  const PortAllocatorSession* pooled_1 = f.ep[0].allocator->GetPooledSession();
  const PortAllocatorSession* pooled_2 = f.ep[1].allocator->GetPooledSession();
  SM_ASSERT_TRUE(pooled_1 != nullptr && pooled_2 != nullptr);
  // Wait for the pooled sessions to finish gathering before the channels try to use them.
  SM_EXPECT_TRUE(f.RunUntil([&] { return pooled_1->CandidatesAllocationDone() && pooled_2->CandidatesAllocationDone(); },
                            kMediumTimeout));
  // Now let the endpoints connect and try exchanging some data.
  f.CreateChannels();
  // Taken with its candidates, and already done.
  SM_EXPECT_TRUE(f.ch(0)->allocator_session() == pooled_1);
  SM_EXPECT_TRUE(f.ch(0)->gathering_state() == kIceGatheringComplete);
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.Connected(); }, kDefaultTimeout));
  SM_EXPECT_TRUE(f.TestSendRecv());
  // Make sure the channels are actually using ports from the pooled sessions.
  const auto ports_1 = pooled_1->ReadyPorts();
  SM_EXPECT_TRUE(std::find(ports_1.begin(), ports_1.end(), f.ch(0)->selected_connection()->port()) != ports_1.end());
}

// TestUsingPooledSessionBeforeDoneGathering: a pooled session still gathering is taken and finishes in the channel.
SM_TEST(P2pTransportChannel, TestUsingPooledSessionBeforeDoneGathering) {
  Fixture f;
  f.ConfigureEndpoints(OPEN, OPEN);
  for (Endpoint& e : f.ep) e.allocator->SetConfiguration(f.now, e.allocator->stun_servers(), {}, 1, NO_PRUNE);
  const PortAllocatorSession* pooled_1 = f.ep[0].allocator->GetPooledSession();
  SM_ASSERT_TRUE(pooled_1 != nullptr);
  SM_EXPECT_FALSE(pooled_1->CandidatesAllocationDone());
  f.CreateChannels();
  SM_EXPECT_TRUE(f.ch(0)->allocator_session() == pooled_1);
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.Connected(); }, kDefaultTimeout));
  SM_EXPECT_TRUE(f.TestSendRecv());
}

// TestDeleteConnectionsIfAllWriteTimedout (end to end): when the link goes down, the connections time out
// (kConnectionWriteTimeout, 15 s, after the last response), the channel destroys them all and fails.
SM_TEST(P2pTransportChannel, DeleteConnectionsIfAllWriteTimedout) {
  Fixture f;
  f.ConfigureEndpoints(OPEN, OPEN);
  f.CreateChannels();
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected(); }, kMediumTimeout));
  sim::LinkConfig down = Link(10);
  down.down = true;
  f.net.uplink(f.ep[1].host_id).SetConfig(down);
  f.net.downlink(f.ep[1].host_id).SetConfig(down);
  // Not writable once the checks go unanswered: the standard state says disconnected.
  SM_EXPECT_TRUE(f.RunUntil([&] { return !f.ch(0)->writable(); }, TimeDelta::Seconds(10)));
  SM_EXPECT_TRUE(f.ch(0)->GetIceTransportState() == IceTransportState::kDisconnected);
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.ch(0)->connections().empty(); }, TimeDelta::Seconds(40)));
  SM_EXPECT_TRUE(f.ch(0)->selected_connection() == nullptr);
  SM_EXPECT_TRUE(f.ch(0)->GetState() == IceTransportStateInternal::kFailed);
  SM_EXPECT_TRUE(f.ch(0)->GetIceTransportState() == IceTransportState::kFailed);
  SM_EXPECT_FALSE(f.ch(0)->network_route().has_value());
}

// TestAddRemoteCandidateWithVariousUfrags (the remembered candidates): a candidate of an older generation is dropped,
// one with an unknown ufrag is kept for the next generation and gets its password when the credentials arrive.
SM_TEST(P2pTransportChannel, AddRemoteCandidateWithVariousUfrags) {
  Fixture f;
  f.ConfigureEndpoints(OPEN, OPEN);
  P2PTransportChannel ch(f.now, "add candidate", 1, f.ep[0].allocator.get());
  ch.SetIceParameters(kIceParams[0]);
  ch.SetRemoteIceParameters(kIceParams[1]);
  // A candidate with a future ufrag.
  Candidate c(1, "udp", SocketAddress(Ip("1.1.1.1"), 1), 1, kIceUfrag[2], "", IceCandidateType::kHost, 0, "f1");
  ch.AddRemoteCandidate(f.now, c);
  SM_ASSERT_EQ(ch.remote_candidates().size(), size_t{1});
  SM_EXPECT_EQ(ch.remote_candidates()[0].generation(), uint32_t{1});
  SM_EXPECT_TRUE(ch.remote_candidates()[0].password().empty());
  // The credentials arrive: the candidate takes the password.
  ch.SetRemoteIceParameters(kIceParams[2]);
  SM_EXPECT_EQ(ch.remote_candidates()[0].password(), std::string(kIcePwd[2]));
  // A candidate of the now old generation (ufrag 1) is dropped.
  Candidate old(1, "udp", SocketAddress(Ip("2.2.2.2"), 2), 1, kIceUfrag[1], "", IceCandidateType::kHost, 0, "f2");
  ch.AddRemoteCandidate(f.now, old);
  SM_EXPECT_EQ(ch.remote_candidates().size(), size_t{1});
  // Without a ufrag, the current remote credentials are taken.
  Candidate bare(1, "udp", SocketAddress(Ip("3.3.3.3"), 3), 1, "", "", IceCandidateType::kHost, 0, "f3");
  ch.AddRemoteCandidate(f.now, bare);
  SM_ASSERT_EQ(ch.remote_candidates().size(), size_t{2});
  SM_EXPECT_EQ(ch.remote_candidates()[1].username(), std::string(kIceUfrag[2]));
  SM_EXPECT_EQ(ch.remote_candidates()[1].password(), std::string(kIcePwd[2]));
  // A duplicate is remembered once; RemoveRemoteCandidate and RemoveAllRemoteCandidates forget.
  ch.AddRemoteCandidate(f.now, bare);
  SM_EXPECT_EQ(ch.remote_candidates().size(), size_t{2});
  ch.RemoveRemoteCandidate(ch.remote_candidates()[1]);
  SM_EXPECT_EQ(ch.remote_candidates().size(), size_t{1});
  ch.RemoveAllRemoteCandidates();
  SM_EXPECT_TRUE(ch.remote_candidates().empty());
}

// TestContinualGathering: with continual gathering the session is cleared, not stopped, once a connection is strong,
// and the gathering state stays "gathering".
SM_TEST(P2pTransportChannel, TestContinualGathering) {
  Fixture f;
  f.ConfigureEndpoints(OPEN, OPEN);
  f.ep[0].ch = f.CreateChannel(0, kIceParams[0], kIceParams[1]);
  f.ep[1].ch = f.CreateChannel(1, kIceParams[1], kIceParams[0]);
  IceConfig config = f.ch(0)->config();
  config.continual_gathering_policy = ContinualGatheringPolicy::kGatherContinually;
  f.ch(0)->SetIceConfig(config);
  f.ch(1)->SetIceConfig(config);
  f.ch(0)->MaybeStartGathering(f.now);
  f.ch(1)->MaybeStartGathering(f.now);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected(); }, kDefaultTimeout));
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.ch(0)->allocator_session()->IsCleared(); }, kDefaultTimeout));
  SM_EXPECT_FALSE(f.ch(0)->allocator_session()->IsStopped());
  SM_EXPECT_TRUE(f.ch(0)->gathering_state() == kIceGatheringGathering);
  // Changing the policy once gathering started is refused.
  config.continual_gathering_policy = ContinualGatheringPolicy::kGatherOnce;
  f.ch(0)->SetIceConfig(config);
  SM_EXPECT_TRUE(f.ch(0)->config().gather_continually());
}

// Gather once: a strongly connected pair stops the session (stop_gather_on_strongly_connected, on by default).
SM_TEST(P2pTransportChannel, TestStopPortAllocatorSessions) {
  Fixture f;
  f.ConfigureEndpoints(OPEN, OPEN);
  f.CreateChannels();
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected(); }, kDefaultTimeout));
  SM_EXPECT_TRUE(f.RunUntil([&] { return f.ch(0)->allocator_session()->IsStopped(); }, kDefaultTimeout));
  SM_EXPECT_FALSE(f.ch(0)->allocator_session()->IsGettingPorts());
}

// --- Remote hostname (mDNS) candidates (#1152): P2PTransportChannelResolverTest and the mDNS cases of
// P2PTransportChannelTest (p2p_transport_channel_unittest.cc:5437-5990). -------------------------------------------

namespace {

Candidate HostnameCandidate(std::string_view name, uint16_t port, IceCandidateType type = IceCandidateType::kHost,
                            uint32_t priority = 0) {
  return Candidate(ICE_CANDIDATE_COMPONENT_RTP, UDP_PROTOCOL_NAME, SocketAddress(name, port), priority, "", "", type,
                   0, "foundation");
}

// One gathering channel of endpoint 0 (host ports only), for the cases that add remote candidates by hand.
std::unique_ptr<P2PTransportChannel> GatheredChannel(Fixture& f, AsyncDnsResolverFactoryInterface* factory,
                                                     uint32_t flags = kOnlyLocalPorts) {
  f.ConfigureEndpoints(OPEN, OPEN, flags, flags);
  f.ep[0].resolver_factory = factory;
  f.ep[0].ch = f.CreateChannel(0, kIceParams[0], kIceParams[1]);
  f.ch(0)->MaybeStartGathering(f.now);
  f.RunUntil([&] { return f.ch(0)->gathering_state() == kIceGatheringComplete; }, kMediumTimeout);
  return std::move(f.ep[0].ch);
}

}  // namespace

// HostnameCandidateIsResolved: a resolver is created, asked for a result, and destroyed when the address is a
// hostname; the candidate is added with the resolved address (and keeps its name).
SM_TEST(P2pTransportChannel, HostnameCandidateIsResolved) {
  Fixture f;
  auto channel = GatheredChannel(f, &f.resolver);
  f.resolver.SetAddresses("fake.test", {Ip("1.1.1.1")});
  channel->AddRemoteCandidate(f.now, HostnameCandidate("fake.test", 1000));
  // Not added until the resolver answers (libwebrtc's resolver answers asynchronously).
  SM_EXPECT_EQ(channel->remote_candidates().size(), 0u);
  SM_EXPECT_EQ(f.resolver.started(), 1);
  SM_EXPECT_EQ(channel->hostname_candidate_stats().resolving, 1);
  f.resolver.Process(f.now);
  SM_ASSERT_EQ(channel->remote_candidates().size(), 1u);
  const RemoteCandidate& candidate = channel->remote_candidates()[0];
  SM_EXPECT_FALSE(candidate.address().IsUnresolvedIP());
  SM_EXPECT_TRUE(candidate.address().ipaddr() == Ip("1.1.1.1"));
  SM_EXPECT_EQ(candidate.address().hostname(), std::string("fake.test"));
  SM_EXPECT_EQ(candidate.address().port(), 1000);
  // The candidate is paired with the channel's port.
  SM_EXPECT_EQ(channel->connections().size(), 1u);
  SM_EXPECT_EQ(f.resolver.pending(), 0u);
  SM_EXPECT_EQ(channel->hostname_candidate_stats().resolving, 0);
  SM_EXPECT_EQ(channel->hostname_candidate_stats().resolved, 1);
}

// AddRemoteCandidateWithResult: an error, or a result with neither an IPv6 nor an IPv4 address, drops the candidate;
// IPv6 is preferred to IPv4 (RFC 5245 15.1).
SM_TEST(P2pTransportChannel, HostnameCandidateResolutionResults) {
  Fixture f;
  auto channel = GatheredChannel(f, &f.resolver);
  f.resolver.SetAddresses("empty.local", {});
  f.resolver.SetAddresses("both.local", {Ip("1.1.1.1"), Ip("2001:db8::1")});
  channel->AddRemoteCandidate(f.now, HostnameCandidate("unknown.local", 1001));
  channel->AddRemoteCandidate(f.now, HostnameCandidate("empty.local", 1002));
  channel->AddRemoteCandidate(f.now, HostnameCandidate("BOTH.local.", 1003));
  SM_EXPECT_EQ(f.resolver.started(), 3);
  f.resolver.Process(f.now);
  SM_ASSERT_EQ(channel->remote_candidates().size(), 1u);
  SM_EXPECT_TRUE(channel->remote_candidates()[0].address().ipaddr() == Ip("2001:db8::1"));
  SM_EXPECT_EQ(channel->hostname_candidate_stats().failed, 2);
  SM_EXPECT_EQ(channel->hostname_candidate_stats().resolved, 1);
  // The host's only port is IPv4: no pair with an IPv6 candidate (libwebrtc's TODO on an AAAA-only answer).
  SM_EXPECT_EQ(channel->connections().size(), 0u);
}

// ResolveHostnameCandidate without an AsyncResolverFactory: "Dropping ICE candidate with hostname address".
SM_TEST(P2pTransportChannel, HostnameCandidateDroppedWithoutResolverFactory) {
  Fixture f;
  auto channel = GatheredChannel(f, nullptr);
  channel->AddRemoteCandidate(f.now, HostnameCandidate("a.local", 1000));
  SM_EXPECT_EQ(channel->remote_candidates().size(), 0u);
  SM_EXPECT_EQ(channel->connections().size(), 0u);
  SM_EXPECT_EQ(channel->hostname_candidate_stats().dropped, 1);
  // An IP candidate is added as before.
  Candidate ip = HostnameCandidate("1.1.1.1", 1000);
  SM_EXPECT_FALSE(ip.address().IsUnresolvedIP());
  channel->AddRemoteCandidate(f.now, ip);
  SM_EXPECT_EQ(channel->remote_candidates().size(), 1u);
}

// "Don't do DNS lookups if the IceTransportPolicy is "none" or "relay"": the candidate filter shares neither host
// nor reflexive candidates, so no resolver is created.
SM_TEST(P2pTransportChannel, HostnameCandidateNotResolvedWhenRelayOnly) {
  Fixture f;
  auto channel = GatheredChannel(f, &f.resolver);
  f.ep[0].allocator->SetCandidateFilter(CF_RELAY);
  channel->AddRemoteCandidate(f.now, HostnameCandidate("a.local", 1000));
  SM_EXPECT_EQ(f.resolver.created(), 0);
  SM_EXPECT_EQ(channel->hostname_candidate_stats().dropped, 1);
  // CF_REFLEXIVE alone still resolves.
  f.ep[0].allocator->SetCandidateFilter(CF_REFLEXIVE | CF_RELAY);
  channel->AddRemoteCandidate(f.now, HostnameCandidate("a.local", 1000));
  SM_EXPECT_EQ(f.resolver.started(), 1);
}

// Destroying the channel destroys its resolvers, which cancels their callbacks.
SM_TEST(P2pTransportChannel, ChannelDestroyedWhileResolving) {
  Fixture f;
  auto channel = GatheredChannel(f, &f.resolver);
  f.resolver.SetAddresses("a.local", {Ip("1.1.1.1")});
  channel->AddRemoteCandidate(f.now, HostnameCandidate("a.local", 1000));
  SM_EXPECT_EQ(f.resolver.pending(), 1u);
  channel.reset();
  SM_EXPECT_EQ(f.resolver.pending(), 0u);
  f.resolver.Process(f.now);
  SM_EXPECT_EQ(f.resolver.answered(), 0);
}

// The candidate becomes usable once resolved, and is paired as a signaled one: with an IP candidate of lower
// priority already there, the resolved one (higher priority) is pinged first.
SM_TEST(P2pTransportChannel, ResolvedHostnameCandidateIsPairedByPriority) {
  Fixture f;
  auto channel = GatheredChannel(f, &f.resolver);
  channel->SetIceRole(ICEROLE_CONTROLLING);
  f.resolver.SetAddresses("high.local", {Ip("2.2.2.2")});
  channel->AddRemoteCandidate(f.now, HostnameCandidate("1.1.1.1", 1000, IceCandidateType::kHost, 100));
  channel->AddRemoteCandidate(f.now, HostnameCandidate("high.local", 2000, IceCandidateType::kHost, 200));
  SM_ASSERT_EQ(channel->connections().size(), 1u);
  f.resolver.Process(f.now);
  SM_ASSERT_EQ(channel->connections().size(), 2u);
  SM_EXPECT_EQ(channel->remote_candidates()[1].address().ToString(), std::string("high.local:2000"));
  Connection* first = channel->FindNextPingableConnection(f.now);
  SM_ASSERT_TRUE(first != nullptr);
  SM_EXPECT_TRUE(first->remote_candidate().address().ipaddr() == Ip("2.2.2.2"));
  SM_EXPECT_EQ(first->remote_candidate().priority(), 200u);
}

// CanConnectWithHostCandidateWithMdnsName: endpoint 0 signals only a host candidate under an mDNS name; endpoint 1
// resolves it and pings, and endpoint 0 answers through a peer-reflexive candidate.
SM_TEST(P2pTransportChannel, CanConnectWithHostCandidateWithMdnsName) {
  Fixture f;
  f.ConfigureEndpoints(OPEN, OPEN, kOnlyLocalPorts, kOnlyLocalPorts);
  f.ep[0].mdns_names = true;
  f.ep[1].resolver_factory = &f.resolver;
  f.CreateChannels();
  f.PauseCandidates(0);
  f.PauseCandidates(1);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.ep[0].saved_candidates.size() == 1u; }, kMediumTimeout));
  // The IP address of ep1's host candidate should be obfuscated.
  SM_EXPECT_TRUE(f.ep[0].saved_candidates[0].address().IsUnresolvedIP());
  f.resolver.SetAddresses(f.ep[0].mdns_hosts[0].first, {kPublicIp[0]});
  f.ResumeCandidates(0);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.ch(0)->selected_connection() != nullptr; }, kMediumTimeout));
  SM_EXPECT_TRUE(f.LocalCandidate(f.ch(0))->is_local());
  SM_EXPECT_TRUE(f.RemoteCandidate(f.ch(0))->is_prflx());
  // Endpoint 1 reached it through the resolved host candidate, which kept its name.
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.ch(1)->selected_connection() != nullptr; }, kMediumTimeout));
  SM_EXPECT_TRUE(f.RemoteCandidate(f.ch(1))->is_local());
  SM_EXPECT_TRUE(f.RemoteCandidate(f.ch(1))->address().ipaddr() == kPublicIp[0]);
  SM_EXPECT_EQ(f.RemoteCandidate(f.ch(1))->address().hostname(), f.ep[0].mdns_hosts[0].first);
  SM_EXPECT_EQ(f.ch(1)->hostname_candidate_stats().resolved, 1);
}

// Without a resolver the mDNS host candidate is dropped, so endpoint 1 never pings first: with endpoint 1's own
// candidates withheld as above, nothing connects (what the parser and the agent did before #1152).
SM_TEST(P2pTransportChannel, CannotConnectToMdnsHostCandidateWithoutResolver) {
  Fixture f;
  f.ConfigureEndpoints(OPEN, OPEN, kOnlyLocalPorts, kOnlyLocalPorts);
  f.ep[0].mdns_names = true;
  f.CreateChannels();
  f.PauseCandidates(1);
  SM_EXPECT_FALSE(f.RunUntil([&] { return f.ch(0)->selected_connection() || f.ch(1)->selected_connection(); },
                             kMediumTimeout));
  SM_EXPECT_EQ(f.ch(1)->hostname_candidate_stats().dropped, 1);
}

// PeerReflexiveCandidateBeforeSignalingWithMdnsName: endpoint 1 first learns endpoint 0 as a prflx candidate; when
// the mDNS candidate arrives and resolves to the same address, the prflx candidate becomes the host candidate on
// the same selected connection.
SM_TEST(P2pTransportChannel, PeerReflexiveCandidateBeforeSignalingWithMdnsName) {
  Fixture f;
  f.ConfigureEndpoints(OPEN, OPEN, kOnlyLocalPorts, kOnlyLocalPorts);
  f.ep[0].mdns_names = true;
  f.ep[1].resolver_factory = &f.resolver;
  f.CreateChannels();
  f.PauseCandidates(0);
  f.PauseCandidates(1);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.ep[0].saved_candidates.size() == 1u; }, kMediumTimeout));
  SM_EXPECT_TRUE(f.ep[0].saved_candidates[0].address().IsUnresolvedIP());
  // Let ep2 signal its candidate to ep1. ep1 should form a candidate pair and start to ping. After receiving the
  // ping, ep2 discovers a prflx remote candidate and form a candidate pair as well.
  f.ResumeCandidates(1);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.ch(0)->selected_connection() != nullptr; }, kMediumTimeout));
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.ch(1)->selected_connection() != nullptr; }, kMediumTimeout));
  const Connection* selected_connection = f.ch(1)->selected_connection();
  SM_EXPECT_TRUE(selected_connection->remote_candidate().is_prflx());
  SM_EXPECT_EQ(selected_connection->remote_candidate().username(), std::string(kIceUfrag[0]));
  SM_EXPECT_EQ(selected_connection->remote_candidate().password(), std::string(kIcePwd[0]));
  // Set expectation before ep1 signals a hostname candidate.
  f.resolver.SetAddresses(f.ep[0].mdns_hosts[0].first, {kPublicIp[0]});
  f.ResumeCandidates(0);
  // Verify ep2's selected connection is updated to use the 'local' candidate.
  SM_EXPECT_TRUE(
      f.RunUntil([&] { return f.ch(1)->selected_connection()->remote_candidate().is_local(); }, kMediumTimeout));
  SM_EXPECT_TRUE(selected_connection == f.ch(1)->selected_connection());
}

// PeerReflexiveCandidateDuringResolvingHostCandidateWithMdnsName: a prflx candidate found while the hostname is
// being resolved is merged with the resolved host candidate.
SM_TEST(P2pTransportChannel, PeerReflexiveCandidateDuringResolvingHostCandidateWithMdnsName) {
  Fixture f;
  // Prevent resolution until triggered by FireDelayedResolution.
  f.resolver.set_hold(true);
  f.ConfigureEndpoints(OPEN, OPEN, kOnlyLocalPorts, kOnlyLocalPorts);
  f.ep[0].mdns_names = true;
  f.ep[1].resolver_factory = &f.resolver;
  f.CreateChannels();
  f.PauseCandidates(0);
  f.PauseCandidates(1);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.ep[0].saved_candidates.size() == 1u; }, kMediumTimeout));
  SM_ASSERT_TRUE(f.ep[0].saved_candidates[0].address().IsUnresolvedIP());
  // Let ep1 signal its hostname candidate to ep2; ep2 starts resolving it. Then ep2 signals its host candidate, so
  // ep1 can form a pair, select it and ping ep2.
  f.ResumeCandidates(0);
  f.ResumeCandidates(1);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.ch(0)->selected_connection() != nullptr; }, kMediumTimeout));
  f.resolver.SetAddresses(f.ep[0].mdns_hosts[0].first, {kPublicIp[0]});
  // Upon receiving a ping from ep1, ep2 adds a prflx candidate from the unknown address and establishes a
  // connection.
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.ch(1)->selected_connection() != nullptr; }, kMediumTimeout));
  SM_EXPECT_TRUE(f.ch(1)->selected_connection()->remote_candidate().is_prflx());
  SM_EXPECT_EQ(f.ch(1)->hostname_candidate_stats().resolving, 1);
  // ep2 should also be able resolve the hostname candidate. The resolved remote host candidate should be merged
  // with the prflx remote candidate.
  f.resolver.set_hold(false);
  SM_EXPECT_TRUE(
      f.RunUntil([&] { return f.ch(1)->selected_connection()->remote_candidate().is_local(); }, kMediumTimeout));
  SM_EXPECT_EQ(f.ch(1)->remote_candidates().size(), 1u);
}

// SelectedCandidatePairSanitizedWhenMdnsObfuscationEnabled and the remote half of
// CandidatesSanitizedInStatsWhenMdnsObfuscationEnabled: the endpoint that resolved the mDNS candidate shows only
// its name in GetSelectedCandidatePair and in its stats.
SM_TEST(P2pTransportChannel, ResolvedMdnsCandidateSanitizedInSelectedPairAndStats) {
  Fixture f;
  f.ConfigureEndpoints(OPEN, OPEN, kOnlyLocalPorts, kOnlyLocalPorts);
  f.ep[0].mdns_names = true;
  f.ep[1].resolver_factory = &f.resolver;
  f.CreateChannels();
  f.PauseCandidates(0);
  f.PauseCandidates(1);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.ep[0].saved_candidates.size() == 1u; }, kMediumTimeout));
  f.resolver.SetAddresses(f.ep[0].mdns_hosts[0].first, {kPublicIp[0]});
  f.ResumeCandidates(0);
  f.ResumeCandidates(1);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.ch(0)->selected_connection() && f.ch(1)->selected_connection(); },
                            kMediumTimeout));
  const auto pair_ep2 = f.ch(1)->GetSelectedCandidatePair();
  SM_ASSERT_TRUE(pair_ep2.has_value());
  SM_EXPECT_TRUE(pair_ep2->remote.is_local());
  SM_EXPECT_TRUE(pair_ep2->remote.address().IsUnresolvedIP());
  SM_EXPECT_EQ(pair_ep2->remote.address().ToString(),
               f.ep[0].mdns_hosts[0].first + ":" + std::to_string(f.RemoteCandidate(f.ch(1))->address().port()));
  IceTransportStats stats;
  SM_ASSERT_TRUE(f.ch(1)->GetStats(f.now, &stats));
  SM_ASSERT_FALSE(stats.connection_infos.empty());
  for (const auto& info : stats.connection_infos) {
    if (info.remote_candidate.is_local()) SM_EXPECT_TRUE(info.remote_candidate.address().IsUnresolvedIP());
  }
  // The connection itself has the resolved address.
  SM_EXPECT_TRUE(f.RemoteCandidate(f.ch(1))->address().ipaddr() == kPublicIp[0]);
}

// RemoteMdnsCandidatesSanitizedInStats: also when the name is fully qualified (a trailing dot) or in another case.
// A name outside .local is not hidden.
SM_TEST(P2pTransportChannel, RemoteMdnsCandidatesSanitizedInStats) {
  Fixture f;
  auto channel = GatheredChannel(f, nullptr);
  uint16_t port = 1;
  for (const char* name : {"example.local", "example.local.", "EXAMPLE.LOCAL", "Example.Local.", "example.test"}) {
    Candidate candidate = HostnameCandidate(name, port++);
    // Resolve the hostname to 1.1.1.1 and add the candidate with this address directly.
    SocketAddress resolved_address(candidate.address());
    resolved_address.SetResolvedIP(Ip("1.1.1.1"));
    candidate.set_address(resolved_address);
    channel->AddRemoteCandidate(f.now, candidate);
  }
  IceTransportStats stats;
  SM_ASSERT_TRUE(channel->GetStats(f.now, &stats));
  SM_ASSERT_EQ(stats.connection_infos.size(), 5u);
  for (const auto& info : stats.connection_infos) {
    const SocketAddress& a = info.remote_candidate.address();
    if (a.hostname() == "example.test") {
      SM_EXPECT_FALSE(a.IsUnresolvedIP());
    } else {
      SM_EXPECT_TRUE(a.IsUnresolvedIP());
      SM_EXPECT_TRUE(IsMdnsHostname(a.hostname()));
    }
  }
}
