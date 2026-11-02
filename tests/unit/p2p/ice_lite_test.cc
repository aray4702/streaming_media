// ICE-lite for the AI server (issue #1003, C3 part 7): a P2PTransportChannel run as an ICE-lite agent with
// sm::p2p::IceLiteAgent (libwebrtc M157's p2p/test/fake_ice_lite_agent.h), as libwebrtc's DTLS-over-ICE fixture
// (p2p/dtls/dtls_ice_integration_fixture.h, SetupIceAndDtls) sets it up: the server is controlled, gathers host
// candidates only and takes the lite agent as its active ICE controller; the client is a full agent, controlling,
// told SetRemoteIceMode(ICEMODE_LITE), with a STUN server and continual gathering as the fixture's IceConfig. Over
// the emulated network (#985): the server on a public address, the client public or behind each NAT type, and a
// client whose network changes (one interface goes away and another appears) while the call runs.
#include <algorithm>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/basic_port_allocator.h"
#include "sm/p2p/candidate.h"
#include "sm/p2p/ice_lite_agent.h"
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

const IpAddress kServerIp = Ip("22.22.22.22");
const IpAddress kClientPublicIp[2] = {Ip("11.11.11.11"), Ip("11.11.11.12")};
const IpAddress kClientPrivateIp[2] = {Ip("192.168.1.11"), Ip("192.168.1.12")};
const IpAddress kNatIp = Ip("77.77.77.77");
const SocketAddress kStunAddr(Ip("99.99.99.1"), 3478);

const IceParameters kClientIce{"UF00", "TESTICEPWD00000000000000", false};
const IceParameters kServerIce{"UF01", "TESTICEPWD00000000000001", false};

constexpr TimeDelta kMediumTimeout = TimeDelta::Seconds(3);
constexpr TimeDelta kDefaultTimeout = TimeDelta::Seconds(10);

enum Config { OPEN, NAT_FULL_CONE, NAT_ADDR_RESTRICTED, NAT_PORT_RESTRICTED, NAT_SYMMETRIC };
constexpr Config kAllConfigs[] = {OPEN, NAT_FULL_CONE, NAT_ADDR_RESTRICTED, NAT_PORT_RESTRICTED, NAT_SYMMETRIC};

sim::LinkConfig Link(int delay_ms) {
  sim::LinkConfig c;
  c.base_delay = TimeDelta::Millis(delay_ms);
  return c;
}

// The client's socket layer over several emulated hosts, one per interface: a socket bound to an interface's address
// lives on that interface's host (a multihomed machine; the any address takes the first).
class MultiHostSocketFactory final : public PacketSocketFactory {
 public:
  void Add(sim::EmulatedSocketServer* host) { hosts_.push_back(host); }
  std::unique_ptr<AsyncPacketSocket> CreateUdpSocket(const SocketAddress& address, uint16_t min_port,
                                                     uint16_t max_port) override {
    for (sim::EmulatedSocketServer* h : hosts_) {
      if (h->ip() == address.ipaddr()) return h->CreateUdpSocket(address, min_port, max_port);
    }
    return hosts_.empty() ? nullptr : hosts_.front()->CreateUdpSocket(address, min_port, max_port);
  }

 private:
  std::vector<sim::EmulatedSocketServer*> hosts_;
};

// Keeps the agent it makes, to read what the channel reported to it.
class RecordingIceLiteAgentFactory final : public ActiveIceControllerFactoryInterface {
 public:
  std::unique_ptr<ActiveIceControllerInterface> Create(const ActiveIceControllerFactoryArgs& args) override {
    auto agent = std::make_unique<IceLiteAgent>(args);
    agent_ = agent.get();
    return agent;
  }
  IceLiteAgent* agent() const { return agent_; }

 private:
  IceLiteAgent* agent_ = nullptr;
};

struct Fixture {
  sim::ByteNetwork net;
  std::unique_ptr<sim::EmulatedSocketServer> stun_host;
  std::unique_ptr<sim::StunServer> stun;
  // The client: one host per interface (the second appears on a network change).
  std::unique_ptr<sim::EmulatedSocketServer> client_host[2];
  int client_realm = sim::ByteNetwork::kPublicRealm;
  Config client_config = OPEN;
  MultiHostSocketFactory client_factory;
  StaticNetworkManager client_networks;
  std::unique_ptr<BasicPortAllocator> client_allocator;
  std::unique_ptr<P2PTransportChannel> client;
  // The server.
  std::unique_ptr<sim::EmulatedSocketServer> server_host;
  StaticNetworkManager server_networks;
  std::unique_ptr<BasicPortAllocator> server_allocator;
  RecordingIceLiteAgentFactory lite_factory;
  std::unique_ptr<P2PTransportChannel> server;
  // Signaling: candidates for the client [0] and for the server [1], and every candidate the server signaled.
  std::deque<Candidate> signaling[2];
  std::vector<Candidate> server_candidates;
  std::vector<std::string> received[2];
  Timestamp now = Timestamp::Seconds(10);

  Fixture() {
    testing::InstallTestCrypto();
    stun_host = std::make_unique<sim::EmulatedSocketServer>(
        net, net.AddHost(kStunAddr.ip(), sim::ByteNetwork::kPublicRealm, Link(5), Link(5)));
    stun = std::make_unique<sim::StunServer>(stun_host->CreateUdpSocket(kStunAddr, 0, 0));
  }
  ~Fixture() {
    client.reset();
    server.reset();
  }

  IpAddress ClientIp(int iface) const {
    return client_config == OPEN ? kClientPublicIp[iface] : kClientPrivateIp[iface];
  }
  void AddClientHost(int iface) {
    client_host[iface] = std::make_unique<sim::EmulatedSocketServer>(
        net, net.AddHost(ClientIp(iface).v4(), client_realm, Link(10), Link(10)));
    client_factory.Add(client_host[iface].get());
  }

  void Configure(Config c) {
    client_config = c;
    if (c != OPEN) {
      sim::NatConfig cfg;
      cfg.type = c == NAT_FULL_CONE         ? sim::NatType::kOpenCone
                 : c == NAT_ADDR_RESTRICTED ? sim::NatType::kAddrRestricted
                 : c == NAT_PORT_RESTRICTED ? sim::NatType::kPortRestricted
                                            : sim::NatType::kSymmetric;
      cfg.external_ip = kNatIp.v4();
      client_realm = net.NatRealm(net.AddNat(cfg));
    }
    AddClientHost(0);
    client_networks.AddInterface(ClientIp(0));
    client_allocator = std::make_unique<BasicPortAllocator>(&client_networks, &client_factory);
    client_allocator->SetConfiguration(now, ServerAddresses{kStunAddr}, {}, 0, NO_PRUNE);
    client_allocator->set_flags(kDefaultPortAllocatorFlags | PORTALLOCATOR_DISABLE_TCP);
    client_allocator->set_step_delay(kMinimumStepDelay);

    // The lite server: host candidates only (no STUN or TURN server).
    server_host = std::make_unique<sim::EmulatedSocketServer>(
        net, net.AddHost(kServerIp.v4(), sim::ByteNetwork::kPublicRealm, Link(10), Link(10)));
    server_networks.AddInterface(kServerIp);
    server_allocator = std::make_unique<BasicPortAllocator>(&server_networks, server_host.get());
    server_allocator->set_flags(kDefaultPortAllocatorFlags | PORTALLOCATOR_DISABLE_TCP);
    server_allocator->set_step_delay(kMinimumStepDelay);
  }

  void Hook(P2PTransportChannel* ch, int side) {
    ch->SubscribeCandidateGathered(this, [this, side](IceTransportInternal*, const Candidate& c) {
      if (side == 1) server_candidates.push_back(c);
      signaling[1 - side].push_back(c);
    });
    ch->RegisterReceivedPacketCallback(this, [this, side](PacketTransportInternal*, const ReceivedPacket& p) {
      received[side].emplace_back(p.payload.begin(), p.payload.end());
    });
  }

  void CreateChannels() {
    IceConfig config;
    config.continual_gathering_policy = ContinualGatheringPolicy::kGatherContinually;

    client = std::make_unique<P2PTransportChannel>(now, "client_transport", ICE_CANDIDATE_COMPONENT_RTP,
                                                   client_allocator.get());
    Hook(client.get(), 0);
    client->SetIceConfig(config);
    client->SetRemoteIceMode(ICEMODE_LITE);  // Is peer using ice-lite.
    client->SetIceParameters(kClientIce);
    client->SetRemoteIceParameters(kServerIce);
    client->SetIceRole(ICEROLE_CONTROLLING);

    P2PTransportChannel::Options options;
    options.active_ice_controller_factory = &lite_factory;
    server = std::make_unique<P2PTransportChannel>(now, "server_transport", ICE_CANDIDATE_COMPONENT_RTP,
                                                   server_allocator.get(), options);
    Hook(server.get(), 1);
    server->SetIceConfig(config);
    server->SetIceParameters(kServerIce);
    server->SetRemoteIceParameters(kClientIce);
    server->SetIceRole(ICEROLE_CONTROLLED);

    client->MaybeStartGathering(now);
    server->MaybeStartGathering(now);
  }

  void DeliverCandidates() {
    P2PTransportChannel* ch[2] = {client.get(), server.get()};
    for (int i = 0; i < 2; ++i) {
      while (!signaling[i].empty()) {
        Candidate c = signaling[i].front();
        signaling[i].pop_front();
        ch[i]->AddRemoteCandidate(now, c);
      }
    }
  }

  Timestamp NextEvent() const {
    Timestamp t = net.NextEvent();
    t = std::min({t, client->NextProcessTime(), server->NextProcessTime()});
    if (!signaling[0].empty() || !signaling[1].empty()) t = std::min(t, now);
    return t;
  }
  void Step(Timestamp t) {
    now = std::max(now, t);
    client->Process(now);
    server->Process(now);
    for (auto& h : client_host) {
      if (h) h->Deliver(now);
    }
    server_host->Deliver(now);
    stun_host->Deliver(now);
    DeliverCandidates();
  }
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

  bool Connected() const {
    return client->receiving() && client->writable() && server->receiving() && server->writable() &&
           client->selected_connection() && server->selected_connection();
  }
  // The server selected the pair the client nominated: the same two addresses, seen from each side.
  bool SamePair() const {
    const Connection* c = client->selected_connection();
    const Connection* s = server->selected_connection();
    return c && s && c->nominated() && s->nominated() &&
           c->remote_candidate().address() == s->local_candidate().address() &&
           (client_config == OPEN ? s->remote_candidate().address() == c->local_candidate().address()
                                  : s->remote_candidate().address().ipaddr() == kNatIp);
  }

  int Send(int side, std::string_view data) {
    P2PTransportChannel* ch = side == 0 ? client.get() : server.get();
    return ch->SendPacket(now, std::span(reinterpret_cast<const uint8_t*>(data.data()), data.size()), PacketOptions{},
                          0);
  }
  bool Received(int side, std::string_view data) const {
    return std::find(received[side].begin(), received[side].end(), data) != received[side].end();
  }
  // TestSendRecv: 10 packets each way.
  bool TestSendRecv(std::string_view tag = "") {
    for (int i = 0; i < 10; ++i) {
      const std::string data = "ABCDEFGHIJKLMNOPQRSTUVWXYZ" + std::string(tag) + std::to_string(i);
      for (int s = 0; s < 2; ++s) {
        if (Send(s, data) != static_cast<int>(data.size())) return false;
        if (!RunUntil([&] { return Received(1 - s, data); }, kMediumTimeout)) return false;
      }
    }
    return true;
  }

  // The lite side never sends a check: no ping on any of its connections, none reported to its agent.
  bool ServerSentNoChecks() {
    for (Connection* c : server->connections()) {
      if (c->stats(now).sent_ping_requests_total != 0) return false;
    }
    return lite_factory.agent() != nullptr && lite_factory.agent()->pings_reported() == 0;
  }
};

}  // namespace

// The lite agent's settings: it pings nothing and never sets USE-CANDIDATE.
SM_TEST(P2pIceLite, AgentNeverPingsOrNominates) {
  Fixture f;
  f.Configure(OPEN);
  f.CreateChannels();
  IceLiteAgent* agent = f.lite_factory.agent();
  SM_ASSERT_TRUE(agent != nullptr);
  SM_EXPECT_TRUE(agent->FindNextPingableConnection(f.now) == nullptr);
  SM_EXPECT_TRUE(f.server->FindNextPingableConnection(f.now) == nullptr);
  SM_EXPECT_TRUE(f.server->remote_ice_mode() == ICEMODE_FULL);
  SM_EXPECT_TRUE(f.client->remote_ice_mode() == ICEMODE_LITE);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected(); }, kDefaultTimeout));
  for (Connection* c : f.server->connections()) {
    SM_EXPECT_FALSE(agent->GetUseCandidateAttribute(c, NominationMode::AGGRESSIVE, ICEMODE_FULL));
    SM_EXPECT_FALSE(agent->GetUseCandidateAttribute(c, NominationMode::SEMI_AGGRESSIVE, ICEMODE_FULL));
  }
}

// A full agent connects to the lite server from each NAT type of #985 (and from a public address): the server
// gathers host candidates only, never sends a check, stays controlled, and selects the pair the client nominated;
// data flows both ways.
SM_TEST(P2pIceLite, FullAgentConnectsThroughEachNat) {
  for (Config c : kAllConfigs) {
    Fixture f;
    f.Configure(c);
    f.CreateChannels();
    SM_EXPECT_TRUE(f.RunUntil([&] { return f.Connected() && f.SamePair(); }, kDefaultTimeout));
    SM_EXPECT_TRUE(f.TestSendRecv());
    SM_EXPECT_FALSE(f.server_candidates.empty());
    for (const Candidate& cand : f.server_candidates) SM_EXPECT_TRUE(cand.is_local());
    SM_EXPECT_TRUE(f.server->GetIceRole() == ICEROLE_CONTROLLED);
    SM_EXPECT_TRUE(f.ServerSentNoChecks());
    SM_ASSERT_TRUE(f.server->selected_connection() != nullptr);
    SM_EXPECT_TRUE(f.server->selected_connection()->remote_nomination() > 0);
    SM_EXPECT_TRUE(f.client->selected_connection()->acked_nomination() > 0);
    if (c == NAT_SYMMETRIC) {
      // The symmetric NAT maps the client's checks to the lite server to a port no STUN server saw: prflx.
      SM_EXPECT_TRUE(f.server->selected_connection()->remote_candidate().is_prflx());
    }
    // The lite agent prunes nothing, so its other pairs stay (CONNECTING, as libwebrtc's FakeIceLiteAgent);
    // the standard state is connected.
    SM_EXPECT_TRUE(f.server->GetIceTransportState() == IceTransportState::kConnected);
  }
}

// Without the client's checks the lite server's pairs never become writable: it waits for the full agent.
SM_TEST(P2pIceLite, ServerWaitsForTheClientsChecks) {
  Fixture f;
  f.Configure(OPEN);
  f.CreateChannels();
  // Take the client's links down before anything is sent: the server learns the client's candidates by signaling
  // only.
  sim::LinkConfig down = Link(10);
  down.down = true;
  f.net.uplink(f.client_host[0]->host()).SetConfig(down);
  f.RunUntil([] { return false; }, kDefaultTimeout);
  SM_EXPECT_FALSE(f.server->connections().empty());
  SM_EXPECT_FALSE(f.server->writable());
  SM_EXPECT_TRUE(f.server->selected_connection() == nullptr);
  SM_EXPECT_TRUE(f.ServerSentNoChecks());
}

// The client's network changes in the middle of the call (its interface goes away and another appears, behind the
// same NAT): the full agent gathers on the new interface, checks the lite server from it, and nominates the new
// pair; the server switches to it and data flows again.
SM_TEST(P2pIceLite, ReconnectsAfterClientNetworkChange) {
  for (Config c : kAllConfigs) {
    Fixture f;
    f.Configure(c);
    f.CreateChannels();
    SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected() && f.SamePair(); }, kDefaultTimeout));
    SM_EXPECT_TRUE(f.TestSendRecv("a"));
    const SocketAddress old_server_remote = f.server->selected_connection()->remote_candidate().address();

    // The network change: the second interface appears, the first goes away with its links.
    f.AddClientHost(1);
    f.client_networks.AddInterface(f.ClientIp(1));
    f.client_networks.RemoveInterface(f.ClientIp(0));
    sim::LinkConfig down = Link(10);
    down.down = true;
    f.net.uplink(f.client_host[0]->host()).SetConfig(down);
    f.net.downlink(f.client_host[0]->host()).SetConfig(down);

    SM_EXPECT_TRUE(f.RunUntil(
        [&] {
          const Connection* cc = f.client->selected_connection();
          return f.Connected() && f.SamePair() && cc->network()->GetBestIP() == f.ClientIp(1);
        },
        TimeDelta::Seconds(30)));
    SM_EXPECT_TRUE(f.server->selected_connection()->remote_candidate().address() != old_server_remote);
    SM_EXPECT_TRUE(f.TestSendRecv("b"));
    SM_EXPECT_TRUE(f.ServerSentNoChecks());
  }
}
