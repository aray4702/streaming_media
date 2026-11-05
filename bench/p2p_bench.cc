// Connectivity (milestone 3) benchmarks, sections 1.1-1.9 of docs/algorithms/ (sm::p2p, so every build, Wasm
// included). STUN (#986): an ICE agent parses and builds one of these per connectivity check, so they stay well under
// a microsecond-scale budget. The others run the library on the test bench's emulated network (sm/sim/), as the unit
// tests in tests/unit/p2p/ do: virtual time passed in as `now`, no OS sockets, no threads. Where a benchmark needs a
// fresh world per iteration (a port, an allocator session, two ICE agents), building and tearing it down is outside
// the timed part. Budgets are about 5-10x the Release time on an Apple M4.
#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/async_tcp_socket.h"
#include "sm/p2p/basic_ice_controller.h"
#include "sm/p2p/basic_port_allocator.h"
#include "sm/p2p/candidate.h"
#include "sm/p2p/connection.h"
#include "sm/p2p/ice_lite_agent.h"
#include "sm/p2p/network.h"
#include "sm/p2p/network_manager.h"
#include "sm/p2p/p2p_transport_channel.h"
#include "sm/p2p/port.h"
#include "sm/p2p/port_allocator.h"
#include "sm/p2p/stun.h"
#include "sm/p2p/stun_port.h"
#include "sm/p2p/turn_port.h"
#include "sm/sim/emulated_sockets.h"
#include "sm/sim/stun_server.h"
#include "sm/sim/turn_server.h"
#include "sm_bench.h"
#if SM_BENCH_STUN_CRYPTO_TLS
#include "sm/p2p/stun_crypto_tls.h"
#else
#include "../tests/unit/p2p/test_crypto.h"
#endif

using namespace sm;
using namespace sm::p2p;

namespace {

// RFC 5769 2.1 sample request (from libwebrtc's stun_unittest.cc).
const uint8_t kRfc5769SampleRequest[] = {
    0x00, 0x01, 0x00, 0x58, 0x21, 0x12, 0xa4, 0x42, 0xb7, 0xe7, 0xa7, 0x01, 0xbc, 0x34, 0xd6, 0x86, 0xfa, 0x87,
    0xdf, 0xae, 0x80, 0x22, 0x00, 0x10, 0x53, 0x54, 0x55, 0x4e, 0x20, 0x74, 0x65, 0x73, 0x74, 0x20, 0x63, 0x6c,
    0x69, 0x65, 0x6e, 0x74, 0x00, 0x24, 0x00, 0x04, 0x6e, 0x00, 0x01, 0xff, 0x80, 0x29, 0x00, 0x08, 0x93, 0x2f,
    0xf9, 0xb1, 0x51, 0x26, 0x3b, 0x36, 0x00, 0x06, 0x00, 0x09, 0x65, 0x76, 0x74, 0x6a, 0x3a, 0x68, 0x36, 0x76,
    0x59, 0x20, 0x20, 0x20, 0x00, 0x08, 0x00, 0x14, 0x9a, 0xea, 0xa7, 0x0c, 0xbf, 0xd8, 0xcb, 0x56, 0x78, 0x1e,
    0xf2, 0xb5, 0xb2, 0xd3, 0xf2, 0x49, 0xc1, 0xb5, 0x71, 0xa2, 0x80, 0x28, 0x00, 0x04, 0xe5, 0x7a, 0x3b, 0xcf};

}  // namespace

SM_BENCH(p2p, Stun_ParseIceRequest) {
  for (auto _ : st) {
    IceMessage msg;
    ByteBufferReader r(kRfc5769SampleRequest);
    sm::bench::DoNotOptimize(msg.Read(&r));
    sm::bench::DoNotOptimize(StunMessage::ValidateFingerprint(kRfc5769SampleRequest));
  }
  st.BudgetNsPerItem(5000);
}

SM_BENCH(p2p, Stun_BuildBindingResponse) {
  const SocketAddress mapped(IpAddress::V4(192, 0, 2, 1), 32853);
  for (auto _ : st) {
    StunMessage msg(STUN_BINDING_RESPONSE, "0123456789ab");
    msg.AddAttribute(std::make_unique<StunXorAddressAttribute>(STUN_ATTR_XOR_MAPPED_ADDRESS, mapped));
    msg.AddAttribute(std::make_unique<StunByteStringAttribute>(STUN_ATTR_SOFTWARE, std::string_view("test vector")));
    msg.AddFingerprint();
    ByteBufferWriter out;
    sm::bench::DoNotOptimize(msg.Write(&out));
    sm::bench::DoNotOptimize(out.Length());
  }
  st.BudgetNsPerItem(5000);
}

// ------------------------------------------------------------------------------------------- shared set-up (1.2-1.9)

namespace {

IpAddress Ip(std::string_view s) { return *IpAddress::FromString(s); }

const IpAddress kPrivateIp[2] = {Ip("192.168.1.11"), Ip("192.168.2.22")};
const IpAddress kPublicIp[2] = {Ip("11.11.11.11"), Ip("22.22.22.22")};
const IpAddress kNatIp[2] = {Ip("77.77.77.77"), Ip("88.88.88.88")};
const SocketAddress kStunAddr(Ip("99.99.99.1"), 3478);
const IceParameters kIce[2] = {{"UF00", "TESTICEPWD00000000000000", false}, {"UF01", "TESTICEPWD00000000000001", false}};

// STUN's HMAC-SHA1, MD5 and random bytes: sm::tls's where it is built (what an application installs), else the
// test-only implementations (the Wasm build has no TLS).
void InstallBenchCrypto() {
#if SM_BENCH_STUN_CRYPTO_TLS
  InstallStunCryptoFromTls();
#else
  testing::InstallTestCryptoWithUniqueIds();
#endif
}

sim::LinkConfig Link(int delay_ms) {
  sim::LinkConfig c;
  c.base_delay = TimeDelta::Millis(delay_ms);
  return c;
}

// A host on the emulated network, public or behind its own port-restricted NAT (the most common home NAT).
std::unique_ptr<sim::EmulatedSocketServer> AddHost(sim::ByteNetwork& net, const IpAddress& ip, int delay_ms,
                                                   const IpAddress* nat_ip = nullptr) {
  int realm = sim::ByteNetwork::kPublicRealm;
  if (nat_ip) {
    sim::NatConfig cfg;
    cfg.type = sim::NatType::kPortRestricted;
    cfg.external_ip = nat_ip->v4();
    realm = net.NatRealm(net.AddNat(cfg));
  }
  return std::make_unique<sim::EmulatedSocketServer>(net, net.AddHost(ip.v4(), realm, Link(delay_ms), Link(delay_ms)));
}

// Runs `step` at each `next()` until `stop()` holds or `limit` of virtual time has passed.
template <class Next, class Step, class Stop>
bool RunLoop(Timestamp& now, Next next, Step step, Stop stop, TimeDelta limit) {
  const Timestamp until = now + limit;
  for (int guard = 0; guard < 1000000; ++guard) {
    if (stop()) return true;
    const Timestamp t = next();
    if (t > until) {
      now = until;
      return stop();
    }
    now = std::max(now, t);
    step();
  }
  return stop();
}

}  // namespace

// ------------------------------------------------------------------------------------- 1.2 ports and candidates

// A UDP port behind a NAT gathers its host candidate and asks the STUN server for its server-reflexive one.
SM_BENCH(p2p, UdpPort_GatherHostAndSrflx) {
  InstallBenchCrypto();
  sim::ByteNetwork net;
  auto client = AddHost(net, kPrivateIp[0], 10, &kNatIp[0]);
  auto stun_host = AddHost(net, Ip("99.99.99.1"), 5);
  sim::StunServer stun(stun_host->CreateUdpSocket(kStunAddr, 0, 0));
  Network network("eth0", kPrivateIp[0], 24);
  network.AddIp(kPrivateIp[0]);
  Port::Parameters args;
  args.network = &network;
  args.socket_factory = client.get();
  args.ice_username_fragment = "ufrag";
  args.ice_password = "passwordpasswordpassword";
  args.ice_tiebreaker = 77;
  Timestamp now = Timestamp::Seconds(10);
  int gathered = 0;
  for (auto _ : st) {
    std::unique_ptr<UDPPort> port = UDPPort::Create(now, args, 0, 0, false, std::nullopt);
    port->set_server_addresses({kStunAddr});
    bool done = false;
    port->SubscribePortComplete(nullptr, [&](Port*) { done = true; });
    port->SubscribePortError(nullptr, [&](Port*) { done = true; });
    port->PrepareAddress(now);
    RunLoop(
        now, [&] { return std::min(port->NextProcessTime(), net.NextEvent()); },
        [&] {
          port->Process(now);
          client->Deliver(now);
          stun_host->Deliver(now);
        },
        [&] { return done; }, TimeDelta::Seconds(10));
    gathered += static_cast<int>(port->Candidates().size());
    st.PauseTiming();
    port.reset();
    st.ResumeTiming();
  }
  // Host and srflx each time.
  st.Require("candidates_per_port", static_cast<double>(gathered) / static_cast<double>(st.iterations()), ">=", 2);
  // One port: socket bind, host candidate, one STUN binding round trip, srflx candidate (3.5 us on an M4).
  st.BudgetNsPerItem(25000);
}

// ----------------------------------------------------------------------------- 1.3 connections (candidate pairs)

namespace {

class NullSocketFactory final : public PacketSocketFactory {
 public:
  std::unique_ptr<AsyncPacketSocket> CreateUdpSocket(const SocketAddress&, uint16_t, uint16_t) override {
    return nullptr;
  }
};

// p2p/test/test_port.h: a host port that keeps the last STUN message it sends (connection_test.cc's TestPort).
class CapturePort final : public Port {
 public:
  CapturePort(Timestamp now, const Parameters& args, uint16_t port_number)
      : Port(args, IceCandidateType::kHost, 0, 0, false, now), port_number_(port_number) {}
  bool SupportsProtocol(std::string_view protocol) const override { return protocol == UDP_PROTOCOL_NAME; }
  ProtocolType GetProtocol() const override { return PROTO_UDP; }
  void PrepareAddress(Timestamp) override {
    const SocketAddress addr(network()->GetBestIP(), port_number_);
    AddAddress(addr, addr, SocketAddress(), "udp", "", "", IceCandidateType::kHost, ICE_TYPE_PREFERENCE_HOST, 0, "",
               true);
  }
  Connection* CreateConnection(Timestamp now, const Candidate& remote, CandidateOrigin) override {
    auto conn = std::make_unique<ProxyConnection>(now, this, 0, remote);
    Connection* raw = conn.get();
    AddOrReplaceConnection(std::move(conn));
    raw->set_use_candidate_attr(true);
    return raw;
  }
  int SendTo(Timestamp, std::span<const uint8_t> data, const SocketAddress&, const PacketOptions&,
             bool payload) override {
    if (!payload) last_stun.assign(data.begin(), data.end());
    return static_cast<int>(data.size());
  }
  int SetOption(SocketOption, int) override { return 0; }
  int GetOption(SocketOption, int*) override { return -1; }
  int GetError() override { return 0; }

  std::vector<uint8_t> last_stun;

 private:
  uint16_t port_number_;
};

std::unique_ptr<CapturePort> MakeCapturePort(Timestamp now, const Network* network, PacketSocketFactory* factory,
                                             std::string ufrag, std::string pwd, uint64_t tiebreaker, IceRole role,
                                             uint16_t port_number) {
  Port::Parameters args;
  args.network = network;
  args.socket_factory = factory;
  args.ice_username_fragment = std::move(ufrag);
  args.ice_password = std::move(pwd);
  args.ice_tiebreaker = tiebreaker;
  auto port = std::make_unique<CapturePort>(now, args, port_number);
  port->SetIceRole(role);
  port->PrepareAddress(now);
  return port;
}

Network MakeNetwork(std::string name, const IpAddress& ip, AdapterType type = ADAPTER_TYPE_UNKNOWN) {
  Network n(std::move(name), ip, 32, type);
  n.AddIp(ip);
  return n;
}

}  // namespace

// A connectivity check on a candidate pair: the controlling side pings (USE-CANDIDATE, MESSAGE-INTEGRITY,
// FINGERPRINT), the controlled side checks it and answers, and the answer makes the pair writable and updates its RTT.
SM_BENCH(p2p, Connection_CheckRoundTrip) {
  InstallBenchCrypto();
  Network net1 = MakeNetwork("lnet", Ip("192.168.1.2"));
  Network net2 = MakeNetwork("rnet", Ip("192.168.1.3"));
  NullSocketFactory factory;
  Timestamp now = Timestamp::Seconds(100);
  auto lport = MakeCapturePort(now, &net1, &factory, "lfrag", "lpasswordlpasswordlpass", 11111, ICEROLE_CONTROLLING,
                               5000);
  auto rport = MakeCapturePort(now, &net2, &factory, "rfrag", "rpasswordrpasswordrpass", 22222, ICEROLE_CONTROLLED,
                               5001);
  Connection* lconn = lport->CreateConnection(now, rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  Connection* rconn = rport->CreateConnection(now, lport->Candidates()[0], Port::ORIGIN_MESSAGE);
  int64_t answered = 0;
  for (auto _ : st) {
    lport->last_stun.clear();
    rport->last_stun.clear();
    lconn->Ping(now);
    rconn->OnReadPacket(ReceivedPacket{std::span<const uint8_t>(lport->last_stun), SocketAddress(), now});
    now += TimeDelta::Millis(10);
    lconn->OnReadPacket(ReceivedPacket{std::span<const uint8_t>(rport->last_stun), SocketAddress(), now});
    answered += !rport->last_stun.empty();
  }
  st.Require("writable", lconn->writable() ? 1 : 0, ">=", 1);
  st.Require("answered_fraction", static_cast<double>(answered) / static_cast<double>(st.iterations()), ">=", 1);
  // One check: request built and signed, verified and answered, the answer verified (4.3 us on an M4).
  st.BudgetNsPerItem(30000);
}

// --------------------------------------------------------------------------------------- 1.4 port allocation

// A port allocator session on a host behind a NAT gathers UDP host, server-reflexive and TCP host candidates, phase
// by phase (kMinimumStepDelay), until it reports allocation done.
SM_BENCH(p2p, PortAllocator_GatherSession) {
  InstallBenchCrypto();
  sim::ByteNetwork net;
  auto client = AddHost(net, kPrivateIp[0], 10, &kNatIp[0]);
  auto stun_host = AddHost(net, Ip("99.99.99.1"), 5);
  sim::StunServer stun(stun_host->CreateUdpSocket(kStunAddr, 0, 0));
  StaticNetworkManager network_manager;
  network_manager.AddInterface(kPrivateIp[0]);
  Timestamp now = Timestamp::Seconds(10);
  BasicPortAllocator allocator(&network_manager, client.get());
  allocator.SetConfiguration(now, ServerAddresses{kStunAddr}, {}, 0, NO_PRUNE);
  allocator.set_step_delay(kMinimumStepDelay);
  size_t gathered = 0;
  for (auto _ : st) {
    std::unique_ptr<PortAllocatorSession> session =
        allocator.CreateSession("0", ICE_CANDIDATE_COMPONENT_RTP, kIce[0].ufrag, kIce[0].pwd);
    bool done = false;
    session->SubscribeCandidatesReady(
        nullptr, [&](PortAllocatorSession*, const std::vector<Candidate>& c) { gathered += c.size(); });
    session->SubscribeCandidatesAllocationDone(nullptr, [&](PortAllocatorSession*) { done = true; });
    session->StartGettingPorts(now);
    RunLoop(
        now, [&] { return std::min(session->NextProcessTime(), net.NextEvent()); },
        [&] {
          session->Process(now);
          client->Deliver(now);
          stun_host->Deliver(now);
        },
        [&] { return done; }, TimeDelta::Seconds(30));
    st.PauseTiming();
    session.reset();
    st.ResumeTiming();
  }
  // UDP host, srflx and TCP host each time.
  st.Require("candidates_per_session", static_cast<double>(gathered) / static_cast<double>(st.iterations()), ">=", 3);
  // One session: 3 ports over 3 phases, one STUN round trip (6.3 us on an M4).
  st.BudgetNsPerItem(50000);
}

// ----------------------------------------------------------------------------------------- 1.5 ICE controller

// BasicIceController over 12 candidate pairs (2 networks x 6 remote candidates, 8 of them writable with different
// RTTs): what P2PTransportChannel asks it on each check tick, a sort-and-switch, the next pair to ping and a prune pass.
SM_BENCH(p2p, IceController_SortPingPrune) {
  InstallBenchCrypto();
  Network wifi = MakeNetwork("wlan0", Ip("192.168.1.2"), ADAPTER_TYPE_WIFI);
  Network cell = MakeNetwork("cell0", Ip("10.0.0.2"), ADAPTER_TYPE_CELLULAR);
  NullSocketFactory factory;
  Timestamp now = Timestamp::Seconds(100);
  IceTransportStateInternal state = IceTransportStateInternal::kConnecting;
  IceFieldTrials trials;
  IceControllerFactoryArgs cargs;
  cargs.ice_transport_state_func = [&] { return state; };
  cargs.ice_role_func = [] { return ICEROLE_CONTROLLING; };
  cargs.is_connection_pruned_func = [](const Connection*) { return false; };
  cargs.ice_field_trials = &trials;
  BasicIceController controller(cargs);
  std::unique_ptr<CapturePort> ports[2] = {
      MakeCapturePort(now, &wifi, &factory, "lfrag", "lpasswordlpasswordlpass", 1111, ICEROLE_CONTROLLING, 5000),
      MakeCapturePort(now, &cell, &factory, "lfrag", "lpasswordlpasswordlpass", 1111, ICEROLE_CONTROLLING, 5002)};
  int n = 0;
  for (auto& port : ports) {
    for (int r = 0; r < 6; ++r, ++n) {
      Candidate c;
      c.set_component(ICE_CANDIDATE_COMPONENT_RTP);
      c.set_protocol(UDP_PROTOCOL_NAME);
      c.set_address(SocketAddress(Ip("1.1.1." + std::to_string(r + 1)), static_cast<uint16_t>(1000 + r)));
      c.set_priority(static_cast<uint32_t>(1000 + 100 * r));
      c.set_username("rfrag");
      c.set_password("rpasswordrpasswordrpass");
      c.set_type(IceCandidateType::kHost);
      Connection* conn = port->CreateConnection(now, c, Port::ORIGIN_MESSAGE);
      controller.AddConnection(conn);
      if (n % 3 != 2) conn->ReceivedPingResponse(now, TimeDelta::Millis(20 + 7 * n), "id");
    }
  }
  auto first = controller.SortAndSwitchConnection(now, IceSwitchReason::CONNECT_STATE_CHANGE);
  if (first.connection) controller.SetSelectedConnection(*first.connection);
  state = IceTransportStateInternal::kCompleted;
  Timestamp last_ping = now;
  int64_t pinged = 0;
  for (auto _ : st) {
    now += TimeDelta::Millis(1);
    auto sw = controller.SortAndSwitchConnection(now, IceSwitchReason::DATA_RECEIVED);
    sm::bench::DoNotOptimize(sw.connection);
    auto ping = controller.GetConnectionToPing(now, last_ping);
    if (ping.connection) {
      controller.MarkConnectionPinged(*ping.connection);
      last_ping = now;
      ++pinged;
    }
    sm::bench::DoNotOptimize(controller.PruneConnections());
  }
  st.Counter("pings_per_tick", static_cast<double>(pinged) / static_cast<double>(st.iterations()));
  // One tick over 12 pairs: sort-and-switch, ping selection, prune (0.4 us on an M4).
  st.BudgetNsPerItem(3000);
}

// ---------------------------------------------------------------------------- 1.6 ICE agent, 1.7 ICE-lite

namespace {

// Two P2PTransportChannels on the emulated network with a STUN server, candidates signaled on the next step
// (p2p_transport_channel_test.cc's and ice_lite_test.cc's fixtures). Endpoint 0 is the full, controlling agent;
// endpoint 1 is a full agent behind its own NAT, or (lite) an ICE-lite server on a public address.
class IceWorld {
 public:
  explicit IceWorld(bool lite) : lite_(lite) {
    stun_host_ = AddHost(net_, Ip("99.99.99.1"), 5);
    stun_ = std::make_unique<sim::StunServer>(stun_host_->CreateUdpSocket(kStunAddr, 0, 0));
    for (int i = 0; i < 2; ++i) {
      const bool nat = !lite || i == 0;
      const IpAddress ip = nat ? kPrivateIp[i] : kPublicIp[i];
      ep_[i].host = AddHost(net_, ip, 10, nat ? &kNatIp[i] : nullptr);
      ep_[i].networks.AddInterface(ip);
      ep_[i].allocator = std::make_unique<BasicPortAllocator>(&ep_[i].networks, ep_[i].host.get());
      if (!(lite && i == 1)) ep_[i].allocator->SetConfiguration(now, ServerAddresses{kStunAddr}, {}, 0, NO_PRUNE);
      if (lite) ep_[i].allocator->set_flags(kDefaultPortAllocatorFlags | PORTALLOCATOR_DISABLE_TCP);
      ep_[i].allocator->set_step_delay(kMinimumStepDelay);
    }
  }
  ~IceWorld() {
    for (auto& e : ep_) e.ch.reset();
  }

  // Creates both channels and starts gathering: the start of a call.
  void Start() {
    for (int i = 0; i < 2; ++i) {
      P2PTransportChannel::Options options;
      if (lite_ && i == 1) options.active_ice_controller_factory = &lite_factory_;
      auto ch = std::make_unique<P2PTransportChannel>(now, "0", ICE_CANDIDATE_COMPONENT_RTP, ep_[i].allocator.get(),
                                                      options);
      ch->SubscribeCandidateGathered(this, [this, i](IceTransportInternal*, const Candidate& c) {
        signaling_[1 - i].push_back(c);
      });
      ch->RegisterReceivedPacketCallback(this, [this, i](PacketTransportInternal*, const ReceivedPacket&) {
        ++ep_[i].received;
      });
      if (lite_) {
        IceConfig config;
        config.continual_gathering_policy = ContinualGatheringPolicy::kGatherContinually;
        ch->SetIceConfig(config);
        if (i == 0) ch->SetRemoteIceMode(ICEMODE_LITE);
      }
      ch->SetIceParameters(kIce[i]);
      ch->SetRemoteIceParameters(kIce[1 - i]);
      ch->SetIceRole(i == 0 ? ICEROLE_CONTROLLING : ICEROLE_CONTROLLED);
      ep_[i].ch = std::move(ch);
    }
    for (auto& e : ep_) e.ch->MaybeStartGathering(now);
  }

  // Both sides writable and receiving on a selected pair that the controlling side nominated.
  bool Connected() const {
    for (const auto& e : ep_) {
      if (!e.ch->receiving() || !e.ch->writable() || !e.ch->selected_connection()) return false;
    }
    return ep_[1].ch->selected_connection()->remote_nomination() > 0;
  }

  Timestamp NextEvent() const {
    Timestamp t = net_.NextEvent();
    for (const auto& e : ep_) {
      if (e.ch) t = std::min(t, e.ch->NextProcessTime());
    }
    if (!signaling_[0].empty() || !signaling_[1].empty()) t = std::min(t, now);
    return t;
  }
  void Step() {
    for (auto& e : ep_) e.ch->Process(now);
    for (auto& e : ep_) e.host->Deliver(now);
    stun_host_->Deliver(now);
    for (int i = 0; i < 2; ++i) {
      while (!signaling_[i].empty()) {
        ep_[i].ch->AddRemoteCandidate(now, signaling_[i].front());
        signaling_[i].pop_front();
      }
    }
  }
  template <class F>
  bool RunUntil(F stop, TimeDelta limit) {
    return RunLoop(now, [this] { return NextEvent(); }, [this] { Step(); }, stop, limit);
  }

  P2PTransportChannel* ch(int i) { return ep_[i].ch.get(); }
  int64_t received(int i) const { return ep_[i].received; }

  Timestamp now = Timestamp::Seconds(10);

 private:
  class LiteFactory final : public ActiveIceControllerFactoryInterface {
   public:
    std::unique_ptr<ActiveIceControllerInterface> Create(const ActiveIceControllerFactoryArgs& args) override {
      return std::make_unique<IceLiteAgent>(args);
    }
  };
  struct Endpoint {
    std::unique_ptr<sim::EmulatedSocketServer> host;
    StaticNetworkManager networks;
    std::unique_ptr<BasicPortAllocator> allocator;
    std::unique_ptr<P2PTransportChannel> ch;
    int64_t received = 0;
  };

  bool lite_;
  sim::ByteNetwork net_;
  std::unique_ptr<sim::EmulatedSocketServer> stun_host_;
  std::unique_ptr<sim::StunServer> stun_;
  LiteFactory lite_factory_;
  Endpoint ep_[2];
  std::deque<Candidate> signaling_[2];
};

// One connection set-up per iteration; the world is built and torn down outside the timed part.
void ConnectBench(sm::bench::State& st, bool lite, double budget_ns) {
  InstallBenchCrypto();
  int connected = 0;
  double virtual_ms = 0;
  for (auto _ : st) {
    st.PauseTiming();
    auto w = std::make_unique<IceWorld>(lite);
    const Timestamp start = w->now;
    st.ResumeTiming();
    w->Start();
    connected += w->RunUntil([&] { return w->Connected(); }, TimeDelta::Seconds(10));
    st.PauseTiming();
    virtual_ms += static_cast<double>((w->now - start).ms());
    w.reset();
    st.ResumeTiming();
  }
  st.Counter("connect_virtual_ms", virtual_ms / static_cast<double>(st.iterations()));
  st.Require("connected_fraction", static_cast<double>(connected) / static_cast<double>(st.iterations()), ">=", 1);
  st.BudgetNsPerItem(budget_ns);
}

}  // namespace

// Two full ICE agents, each behind its own port-restricted NAT with a STUN server: gather (UDP, srflx, TCP),
// trickle candidates, check, nominate and select a pair on both sides.
SM_BENCH(p2p, IceAgent_ConnectBehindNats) {
  // One call's ICE set-up, both agents, to connected and nominated (66 us on an M4).
  ConnectBench(st, /*lite=*/false, 500000);
}

// Media through a connected P2PTransportChannel: SendPacket on one agent, the emulated network, the other agent's
// connection and its received-packet callback (with the agents' own pings and timers interleaved).
SM_BENCH(p2p, IceAgent_SendPacket) {
  InstallBenchCrypto();
  IceWorld w(/*lite=*/false);
  w.Start();
  const bool ok = w.RunUntil([&] { return w.Connected(); }, TimeDelta::Seconds(10));
  std::vector<uint8_t> packet(1200, 0x5a);
  int64_t sent = 0;
  const int64_t base = w.received(1);
  for (auto _ : st) {
    for (int k = 0; k < 10; ++k) sent += w.ch(0)->SendPacket(w.now, packet, PacketOptions{}, 0) > 0;
    const int64_t want = base + sent;
    w.RunUntil([&] { return w.received(1) >= want; }, TimeDelta::Seconds(1));
  }
  st.SetItemsPerIteration(10);
  st.Require("connected", ok ? 1 : 0, ">=", 1);
  st.Require("delivered_fraction",
             static_cast<double>(w.received(1) - base) / static_cast<double>(10 * st.iterations()), ">=", 1);
  // One 1200-byte packet sent and received through the two channels (0.36 us on an M4).
  st.BudgetNsPerItem(3000);
}

// 1.7: a full agent behind a NAT connects to an ICE-lite server on a public address, which only answers checks.
SM_BENCH(p2p, IceLite_FullAgentConnects) {
  // One call's ICE set-up to the lite server, to connected and nominated (28 us on an M4).
  ConnectBench(st, /*lite=*/true, 200000);
}

// ------------------------------------------------------------------------------------------ 1.8 TURN client

namespace {

// turn_fixture.h's world: a client (11.11.11.11), a peer (22.22.22.22) and a TURN server (internal 99.99.99.3:3478,
// relayed addresses on 99.99.99.5) with long-term credentials; 5 ms links at the hosts, 15 ms at the server.
class TurnAuth final : public sim::TurnAuthInterface {
 public:
  bool GetKey(std::string_view username, std::string_view realm, std::string* key) override {
    return ComputeStunCredentialHash(std::string(username), std::string(realm), std::string(username), key);
  }
};

struct TurnWorld {
  sim::ByteNetwork net;
  std::unique_ptr<sim::EmulatedSocketServer> client = AddHost(net, kPublicIp[0], 5);
  std::unique_ptr<sim::EmulatedSocketServer> peer = AddHost(net, kPublicIp[1], 5);
  std::unique_ptr<sim::EmulatedSocketServer> turn_int = AddHost(net, Ip("99.99.99.3"), 15);
  std::unique_ptr<sim::EmulatedSocketServer> turn_ext = AddHost(net, Ip("99.99.99.5"), 15);
  TurnAuth auth;
  sim::TurnServer server;
  Network client_net = MakeNetwork("eth0", kPublicIp[0]);
  Network peer_net = MakeNetwork("eth1", kPublicIp[1]);
  ProtocolAddress server_address{SocketAddress(Ip("99.99.99.3"), 3478), PROTO_UDP};
  RelayServerConfig config{server_address.address, "test", "test", PROTO_UDP};
  Timestamp now = Timestamp::Seconds(10);
  std::unique_ptr<TurnPort> turn_port;
  std::unique_ptr<UDPPort> udp_port;
  bool turn_ready = false;

  TurnWorld() {
    server.set_realm("example.org");
    server.set_auth_hook(&auth);
    server.AddInternalSocket(turn_int->CreateUdpSocket(server_address.address, 0, 0), PROTO_UDP);
    server.SetExternalSocketFactory(turn_ext.get(), SocketAddress(Ip("99.99.99.5"), 0));
  }
  ~TurnWorld() {
    turn_port.reset();
    udp_port.reset();
  }
  void CreateTurnPort() {
    CreateRelayPortArgs args;
    args.port.network = &client_net;
    args.port.socket_factory = client.get();
    args.port.ice_tiebreaker = 1;
    args.server_address = &server_address;
    args.config = &config;
    args.username = "lfrag";
    args.password = "lpasswordlpasswordlpassw";
    turn_ready = false;
    turn_port = TurnPort::Create(now, args, 0, 0);
    turn_port->SetIceRole(ICEROLE_CONTROLLING);
    turn_port->SubscribePortComplete(nullptr, [this](Port*) { turn_ready = true; });
  }
  void CreateUdpPort() {
    Port::Parameters args;
    args.network = &peer_net;
    args.socket_factory = peer.get();
    args.ice_username_fragment = "rfrag";
    args.ice_password = "rpasswordrpasswordrpassw";
    args.ice_tiebreaker = 2;
    udp_port = UDPPort::Create(now, args, 0, 0, false, std::nullopt);
    udp_port->SetIceRole(ICEROLE_CONTROLLED);
  }
  Timestamp Next() const {
    Timestamp t = std::min(server.NextProcessTime(), net.NextEvent());
    if (turn_port) t = std::min(t, turn_port->NextProcessTime());
    if (udp_port) t = std::min(t, udp_port->NextProcessTime());
    return t;
  }
  void Step() {
    if (turn_port) turn_port->Process(now);
    if (udp_port) udp_port->Process(now);
    server.Process(now);
    for (auto* h : {client.get(), peer.get(), turn_int.get(), turn_ext.get()}) h->Deliver(now);
  }
  template <class F>
  bool RunUntil(F stop, TimeDelta limit) {
    return RunLoop(now, [this] { return Next(); }, [this] { Step(); }, stop, limit);
  }
};

}  // namespace

// A TURN allocation over UDP: Allocate, 401 with the realm and nonce, Allocate with MESSAGE-INTEGRITY, the relayed
// candidate; the port is then released (a Refresh with lifetime 0) outside the timed part.
SM_BENCH(p2p, TurnPort_AllocateUdp) {
  InstallBenchCrypto();
  TurnWorld w;
  int allocated = 0;
  for (auto _ : st) {
    w.CreateTurnPort();
    w.turn_port->PrepareAddress(w.now);
    allocated += w.RunUntil([&] { return w.turn_ready; }, TimeDelta::Seconds(5));
    st.PauseTiming();
    w.turn_port.reset();
    w.RunUntil([] { return false; }, TimeDelta::Millis(100));  // The release reaches the server.
    st.ResumeTiming();
  }
  st.Require("allocated_fraction", static_cast<double>(allocated) / static_cast<double>(st.iterations()), ">=", 1);
  st.Require("server_allocations_left", static_cast<double>(w.server.allocation_count()), "<=", 1);
  // One allocation: two request/response round trips with long-term credentials (9.5 us on an M4).
  st.BudgetNsPerItem(70000);
}

// Data relayed through a bound TURN channel: the client sends ChannelData to the server, which relays it to the peer
// as UDP, and the peer's packet comes back to the client as ChannelData.
SM_BENCH(p2p, TurnPort_ChannelDataRelay) {
  InstallBenchCrypto();
  TurnWorld w;
  w.CreateTurnPort();
  w.turn_port->PrepareAddress(w.now);
  bool ok = w.RunUntil([&] { return w.turn_ready; }, TimeDelta::Seconds(5));
  w.CreateUdpPort();
  w.udp_port->PrepareAddress(w.now);
  w.RunUntil([&] { return !w.udp_port->Candidates().empty(); }, TimeDelta::Seconds(1));
  Connection* conn1 = w.turn_port->CreateConnection(w.now, w.udp_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  Connection* conn2 = w.udp_port->CreateConnection(w.now, w.turn_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  int64_t at_turn = 0, at_peer = 0;
  conn1->RegisterReceivedPacketCallback([&](Connection*, const ReceivedPacket&) { ++at_turn; });
  conn2->RegisterReceivedPacketCallback([&](Connection*, const ReceivedPacket&) { ++at_peer; });
  conn1->Ping(w.now);
  ok = ok && w.RunUntil([&] { return conn1->writable(); }, TimeDelta::Seconds(1));
  conn2->Ping(w.now);
  ok = ok && w.RunUntil([&] { return conn2->writable(); }, TimeDelta::Seconds(1));
  std::vector<uint8_t> packet(1000, 0xa5);
  // Warm up: the first payload goes in a Send indication and binds the channel.
  for (int i = 0; i < 4; ++i) {
    conn1->Send(w.now, packet, {});
    w.RunUntil([] { return false; }, TimeDelta::Millis(100));
  }
  const int64_t base_turn = at_turn, base_peer = at_peer;
  for (auto _ : st) {
    conn1->Send(w.now, packet, {});
    conn2->Send(w.now, packet, {});
    const int64_t want_peer = at_peer + 1, want_turn = at_turn + 1;
    w.RunUntil([&] { return at_peer >= want_peer && at_turn >= want_turn; }, TimeDelta::Seconds(1));
  }
  st.SetItemsPerIteration(2);
  st.Require("set_up", ok ? 1 : 0, ">=", 1);
  st.Require("delivered_fraction",
             static_cast<double>(at_turn - base_turn + at_peer - base_peer) / static_cast<double>(2 * st.iterations()),
             ">=", 1);
  // One 1000-byte packet relayed one way: client, server, peer (0.8 us on an M4).
  st.BudgetNsPerItem(6000);
}

// ---------------------------------------------------------------------------------------- 1.9 TCP packet sockets

namespace {

template <class Framing>
class FramingListenSocket final : public AsyncTcpListenSocket {
 public:
  using AsyncTcpListenSocket::AsyncTcpListenSocket;

 protected:
  std::unique_ptr<AsyncPacketSocket> WrapIncomingConnection(std::unique_ptr<Socket> socket) override {
    return std::make_unique<Framing>(std::move(socket));
  }
};

// Delivers every event of both hosts in time order until none is left (the emulated TCP's segments and ACKs).
Timestamp RunIdle(sim::EmulatedSocketServer& a, sim::EmulatedSocketServer& b, Timestamp now) {
  for (int guard = 0; guard < 1000000; ++guard) {
    const Timestamp t = std::min(a.NextEvent(), b.NextEvent());
    if (!t.IsFinite()) break;
    now = std::max(now, t);
    a.Deliver(now);
    b.Deliver(now);
  }
  return now;
}

// Ten packets per iteration over one emulated TCP connection, framed by `Framing` on both ends.
template <class Framing>
void TcpFramingBench(sm::bench::State& st, const std::vector<uint8_t>& packet) {
  sim::ByteNetwork net;
  auto a = AddHost(net, kPublicIp[0], 5);
  auto b = AddHost(net, kPublicIp[1], 5);
  auto server = b->CreateStreamSocket(IpFamily::kV4);
  server->Bind(SocketAddress(b->ip(), 0));
  FramingListenSocket<Framing> listen(std::move(server));
  std::unique_ptr<AsyncPacketSocket> accepted;
  int64_t received = 0;
  size_t bytes = 0;
  listen.SubscribeNewConnection(nullptr, [&](AsyncListenSocket*, std::unique_ptr<AsyncPacketSocket>& s) {
    accepted = std::move(s);
    accepted->RegisterReceivedPacketCallback([&](AsyncPacketSocket*, const ReceivedPacket& p) {
      ++received;
      bytes += p.payload.size();
    });
  });
  Timestamp now = Timestamp::Seconds(1);
  auto client_socket = a->CreateStreamSocket(IpFamily::kV4);
  client_socket->Bind(SocketAddress(a->ip(), 0));
  client_socket->Connect(now, listen.GetLocalAddress());
  Framing client(std::move(client_socket));
  now = RunIdle(*a, *b, now);
  int64_t sent = 0;
  for (auto _ : st) {
    for (int k = 0; k < 10; ++k) sent += client.Send(now, packet, {}) == static_cast<int>(packet.size());
    now = RunIdle(*a, *b, now);
  }
  st.SetItemsPerIteration(10);
  st.Require("delivered_fraction", static_cast<double>(received) / static_cast<double>(10 * st.iterations()), ">=", 1);
  st.Require("sent_fraction", static_cast<double>(sent) / static_cast<double>(10 * st.iterations()), ">=", 1);
  sm::bench::DoNotOptimize(bytes);
}

}  // namespace

// RFC 4571 framing (a 2-byte length per packet, AsyncTcpSocket): ICE-TCP's framing, over the emulated TCP.
SM_BENCH(p2p, TcpSocket_Rfc4571Framing) {
  TcpFramingBench<AsyncTcpSocket>(st, std::vector<uint8_t>(1200, 0x5a));
  // One 1200-byte packet framed, carried in emulated TCP segments and cut out again (1.9 us on an M4).
  st.BudgetNsPerItem(15000);
}

// STUN/ChannelData framing (AsyncStunTcpSocket, TURN over TCP): packets cut by their own length fields.
SM_BENCH(p2p, TcpSocket_StunChannelDataFraming) {
  std::vector<uint8_t> channel_data(1200, 0x5a);
  channel_data[0] = 0x40;  // Channel number 0x4000.
  channel_data[1] = 0x00;
  channel_data[2] = static_cast<uint8_t>((1200 - 4) >> 8);
  channel_data[3] = static_cast<uint8_t>((1200 - 4) & 0xff);
  TcpFramingBench<AsyncStunTcpSocket>(st, channel_data);
  // One 1200-byte ChannelData message framed, carried and cut out again (1.9 us on an M4).
  st.BudgetNsPerItem(15000);
}
