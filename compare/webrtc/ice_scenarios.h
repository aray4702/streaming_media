// ICE comparison (issue #1004, C3 part 8): the scenarios, the emulated network they run on, the metrics, and this
// library's side. ice_compare.cc runs libwebrtc's side on the same network and prints both.
//
// Both stacks run the same scenario on their own copy of the same emulated network (sm::sim::Network with the same
// seed, hosts, NATs, links and STUN server), through the same sockets (sim::EmulatedSocketServer: libwebrtc's side
// wraps them as webrtc::AsyncPacketSocket), so both bind the same ports, cross the same NATs and see the same loss
// draws for the same packet sequence. Only the ICE stack differs. Each side is set up as PeerConnection sets it up:
// - the allocator with PeerConnection::InitializePortAllocator_n (pc/peer_connection.cc) and RTCConfiguration's
//   defaults (api/peer_connection_interface.h): ours with p2p::ConfigureAsPeerConnection, libwebrtc's with the same
//   calls in ice_compare.cc;
// - the channel with IceConfig(RTCConfiguration()) (p2p/base/ice_transport_internal.cc), which for the default
//   RTCConfiguration leaves every optional unset and gathers once: IceConfig{} on our side;
// - the endpoints' interfaces as rtc_base/fake_network.h's FakeNetworkManager names them ("test0", /24).
// Endpoint 0 is controlling and endpoint 1 controlled, with the ICE credentials set before gathering, as after an
// offer/answer; candidates go to the other side through a signaling queue with no delay (each side delivers them on
// its next step).
//
// One setting differs from RTCConfiguration's defaults on both sides: TCP candidates are off
// (kTcpCandidatePolicyDisabled), because this library has no TCPPort yet (C4); libwebrtc would add TCP host
// candidates and pairs. Relay (TURN) is left out until C4, as the issue says, so the symmetric-to-symmetric pair fails
// on both sides.
//
// Metrics per run (both endpoints together unless named):
// - connected: both endpoints writable and receiving with a selected connection (the unit test's CheckConnected);
// - connect_ms: time from gathering start to that;
// - pair: the candidate types of the controlling side's selected connection (local/remote) when it connected;
// - checks: STUN binding requests on the connections of both channels (ConnectionInfo::sent_ping_requests_total
//   summed over IceTransportStats::connection_infos), when connected and at the end of the run.
//
// Network change (part 2): one endpoint's interface goes away and another appears, as in libwebrtc's
// P2PTransportChannelMultihomedTest (TestFailoverControllingSide, TestFailoverControlledSide,
// TestContinualGatheringOnNewInterface, TestNetworkBecomesInactive): `kChangeAt` after gathering starts, the
// endpoint's second interface (kAlternateIp, or kAlternatePrivateIp behind the same NAT) is added to its network
// manager, the first removed, and the first interface's links go down. Its sockets on the new interface live on a
// second emulated host (a multihomed machine). Candidates the endpoint removes are not signaled to the other side, on
// either stack (the fixture signals only new candidates as well). Metrics:
// - recovered: both endpoints connected again, the changed endpoint's selected connection on the new interface and the
//   other's selected connection to a new remote address;
// - recover_ms: time from the change to that;
// - pair_after: the controlling side's selected candidate types then;
// - checks: as above, at the end of the run, so only on the connections both stacks still keep then.
// With PeerConnection's default gathering (gather once), neither stack gathers on the new interface, so the call
// does not recover without an ICE restart; with continual gathering (RTCConfiguration::continual_gathering_policy =
// GATHER_CONTINUALLY, IceConfig::continual_gathering_policy) both should.
// Two variants, as the multihomed tests name their interfaces:
// - Wi-Fi to cellular: the first interface is Wi-Fi ("eth0", ADAPTER_TYPE_WIFI), the one that appears cellular
//   ("wlan0", ADAPTER_TYPE_CELLULAR);
// - a link going dead (TestFailoverControllingSide, TestFailoverControlledSide): the endpoint has both of those
//   interfaces from the start, and at kChangeAt only the Wi-Fi one's links go down, with no change in the network
//   manager; the backup connection on cellular takes over. PeerConnection's default gathering (once), as those tests.
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/basic_port_allocator.h"
#include "sm/p2p/candidate.h"
#include "sm/p2p/ice_transport_internal.h"
#include "sm/p2p/network_manager.h"
#include "sm/p2p/p2p_transport_channel.h"
#include "sm/p2p/port_allocator.h"
#include "sm/sim/emulated_sockets.h"
#include "sm/sim/stun_server.h"

namespace sm::ice_compare {

enum class Nat { kOpen, kFullCone, kAddrRestricted, kPortRestricted, kSymmetric };

inline const char* NatName(Nat n) {
  switch (n) {
    case Nat::kOpen: return "open";
    case Nat::kFullCone: return "cone";
    case Nat::kAddrRestricted: return "addr";
    case Nat::kPortRestricted: return "port";
    case Nat::kSymmetric: return "sym";
  }
  return "?";
}

struct Scenario {
  std::string name;
  Nat nat[2] = {Nat::kOpen, Nat::kOpen};
  double loss = 0;    // Random loss on every access link, each direction.
  int delay_ms = 10;  // One-way delay of each endpoint's access links (the STUN server's are 5 ms).
  int change = -1;            // The endpoint whose network changes at kChangeAt, or -1 for none.
  bool continual = false;     // IceConfig::continual_gathering_policy = kGatherContinually on both endpoints.
  bool wifi_to_cellular = false;  // The changing endpoint's first interface is Wi-Fi, its second cellular.
  bool dead = false;  // The changing endpoint has both interfaces from the start, and the first one's links die.
};

// Each pair of NAT types (libwebrtc's P2PTransportChannelTest matrix without the relay and TCP columns), then loss
// on the path for a direct, a cone-to-port-restricted and a port-restricted pair.
inline std::vector<Scenario> Scenarios() {
  const Nat kNats[] = {Nat::kOpen, Nat::kFullCone, Nat::kAddrRestricted, Nat::kPortRestricted, Nat::kSymmetric};
  std::vector<Scenario> out;
  for (Nat a : kNats) {
    for (Nat b : kNats) {
      Scenario s;
      s.name = std::string(NatName(a)) + "-" + NatName(b);
      s.nat[0] = a;
      s.nat[1] = b;
      out.push_back(s);
    }
  }
  const std::pair<Nat, Nat> kLossy[] = {{Nat::kOpen, Nat::kOpen},
                                        {Nat::kFullCone, Nat::kPortRestricted},
                                        {Nat::kPortRestricted, Nat::kPortRestricted}};
  for (double loss : {0.05, 0.20}) {
    for (auto [a, b] : kLossy) {
      Scenario s;
      s.name = std::string(NatName(a)) + "-" + NatName(b) + "-loss" + std::to_string(static_cast<int>(loss * 100));
      s.nat[0] = a;
      s.nat[1] = b;
      s.loss = loss;
      out.push_back(s);
    }
  }
  return out;
}

// Network changes: on the controlling side (0) and on the controlled side (1), for a direct pair, a cone-to-port-
// restricted pair and a port-restricted pair, with continual gathering; and the direct pair with PeerConnection's
// default gathering (once).
inline std::vector<Scenario> ChangeScenarios() {
  std::vector<Scenario> out;
  const std::pair<Nat, Nat> kPairs[] = {{Nat::kOpen, Nat::kOpen},
                                        {Nat::kFullCone, Nat::kPortRestricted},
                                        {Nat::kPortRestricted, Nat::kPortRestricted}};
  for (auto [a, b] : kPairs) {
    for (int change : {0, 1}) {
      Scenario s;
      s.name = std::string(NatName(a)) + "-" + NatName(b) + "-change" + std::to_string(change) + "-continual";
      s.nat[0] = a;
      s.nat[1] = b;
      s.change = change;
      s.continual = true;
      out.push_back(s);
    }
  }
  Scenario once;
  once.name = "open-open-change0-once";
  once.change = 0;
  out.push_back(once);
  // Wi-Fi to cellular, with continual gathering.
  for (auto [a, b] : {kPairs[0], kPairs[2]}) {
    for (int change : {0, 1}) {
      Scenario s;
      s.name = std::string(NatName(a)) + "-" + NatName(b) + "-wifi2cell" + std::to_string(change) + "-continual";
      s.nat[0] = a;
      s.nat[1] = b;
      s.change = change;
      s.continual = true;
      s.wifi_to_cellular = true;
      out.push_back(s);
    }
  }
  // The Wi-Fi link going dead with cellular up, gathering once.
  for (auto [a, b] : {kPairs[0], kPairs[2]}) {
    for (int change : {0, 1}) {
      Scenario s;
      s.name = std::string(NatName(a)) + "-" + NatName(b) + "-dead" + std::to_string(change) + "-once";
      s.nat[0] = a;
      s.nat[1] = b;
      s.change = change;
      s.wifi_to_cellular = true;
      s.dead = true;
      out.push_back(s);
    }
  }
  return out;
}

struct Result {
  bool connected = false;
  double connect_ms = -1;
  std::string pair;  // "host/srflx": local/remote candidate types of the controlling side's selected connection.
  int checks_at_connect = 0;
  int checks_total = 0;
  // Network-change scenarios only.
  bool recovered = false;
  double recover_ms = -1;
  std::string pair_after;
};

// The run: until `kRunTime` after gathering starts. libwebrtc's unit tests wait up to 10 s (kDefaultTimeout) for a
// connection.
inline constexpr TimeDelta kRunTime = TimeDelta::Seconds(10);
// A network change comes `kChangeAt` after gathering starts, and the run then lasts `kRecoveryTime` more (the 30 s the
// libwebrtc fixture's multihomed tests and ours wait for a failover).
inline constexpr TimeDelta kChangeAt = TimeDelta::Seconds(5);
inline constexpr TimeDelta kRecoveryTime = TimeDelta::Seconds(30);
inline TimeDelta RunTime(const Scenario& s) { return s.change < 0 ? kRunTime : kChangeAt + kRecoveryTime; }

// Addresses of the libwebrtc fixture (p2p_transport_channel_unittest.cc kPublicAddrs, kPrivateAddrs, kNatAddrs,
// kStunAddr with STUN_SERVER_PORT).
inline constexpr uint32_t kPublicIp[2] = {sim::SocketAddress::Ip(11, 11, 11, 11), sim::SocketAddress::Ip(22, 22, 22, 22)};
inline constexpr uint32_t kPrivateIp[2] = {sim::SocketAddress::Ip(192, 168, 1, 11),
                                           sim::SocketAddress::Ip(192, 168, 2, 22)};
// The second interface of a network change: libwebrtc's kAlternateAddrs, or the next private address behind the same
// NAT.
inline constexpr uint32_t kAlternateIp[2] = {sim::SocketAddress::Ip(101, 101, 101, 101),
                                             sim::SocketAddress::Ip(202, 202, 202, 202)};
inline constexpr uint32_t kAlternatePrivateIp[2] = {sim::SocketAddress::Ip(192, 168, 1, 12),
                                                    sim::SocketAddress::Ip(192, 168, 2, 23)};
inline constexpr uint32_t kNatIp[2] = {sim::SocketAddress::Ip(77, 77, 77, 77), sim::SocketAddress::Ip(88, 88, 88, 88)};
inline constexpr uint32_t kStunIp = sim::SocketAddress::Ip(99, 99, 99, 1);
inline constexpr uint16_t kStunPort = 3478;

// ICE credentials of endpoints 0 and 1 (the fixture's kIceUfrag / kIcePwd).
inline const char* const kUfrag[2] = {"UF00", "UF01"};
inline const char* const kPwd[2] = {"TESTICEPWD00000000000000", "TESTICEPWD00000000000001"};

// The emulated network of one run, built the same way for each stack.
struct Topology {
  sim::ByteNetwork net;
  std::unique_ptr<sim::EmulatedSocketServer> stun_host;
  std::unique_ptr<sim::StunServer> stun;
  std::unique_ptr<sim::EmulatedSocketServer> host[2];
  uint32_t ip[2] = {0, 0};
  // The second interface of a network change (ChangeNetwork).
  std::unique_ptr<sim::EmulatedSocketServer> alt_host[2];
  uint32_t alt_ip[2] = {0, 0};
  int realm_of[2] = {sim::ByteNetwork::kPublicRealm, sim::ByteNetwork::kPublicRealm};
  sim::LinkConfig link;

  Topology(const Scenario& s, uint64_t seed) : net(seed) {
    sim::LinkConfig stun_link;
    stun_link.base_delay = TimeDelta::Millis(5);
    stun_host = std::make_unique<sim::EmulatedSocketServer>(
        net, net.AddHost(kStunIp, sim::ByteNetwork::kPublicRealm, stun_link, stun_link));
    stun = std::make_unique<sim::StunServer>(stun_host->CreateUdpSocket(StunAddress(), 0, 0));
    for (int i = 0; i < 2; ++i) {
      int realm = sim::ByteNetwork::kPublicRealm;
      ip[i] = kPublicIp[i];
      if (s.nat[i] != Nat::kOpen) {
        sim::NatConfig cfg;
        cfg.type = s.nat[i] == Nat::kFullCone         ? sim::NatType::kOpenCone
                   : s.nat[i] == Nat::kAddrRestricted ? sim::NatType::kAddrRestricted
                   : s.nat[i] == Nat::kPortRestricted ? sim::NatType::kPortRestricted
                                                      : sim::NatType::kSymmetric;
        cfg.external_ip = kNatIp[i];
        realm = net.NatRealm(net.AddNat(cfg));
        ip[i] = kPrivateIp[i];
      }
      link.base_delay = TimeDelta::Millis(s.delay_ms);
      link.loss_rate = s.loss;
      realm_of[i] = realm;
      alt_ip[i] = s.nat[i] == Nat::kOpen ? kAlternateIp[i] : kAlternatePrivateIp[i];
      host[i] = std::make_unique<sim::EmulatedSocketServer>(net, net.AddHost(ip[i], realm, link, link));
    }
  }

  // Endpoint i's second interface appears (a new host in the same realm, behind the same NAT if any) and the first
  // one's links go down. The caller adds and removes the interfaces in its network manager.
  void ChangeNetwork(int i) {
    AddAlternate(i);
    KillPrimary(i);
  }
  // Endpoint i's second interface (a new host in the same realm, behind the same NAT if any).
  void AddAlternate(int i) {
    alt_host[i] = std::make_unique<sim::EmulatedSocketServer>(net, net.AddHost(alt_ip[i], realm_of[i], link, link));
  }
  // Endpoint i's first interface's links go down.
  void KillPrimary(int i) {
    sim::LinkConfig down = link;
    down.down = true;
    net.uplink(host[i]->host()).SetConfig(down);
    net.downlink(host[i]->host()).SetConfig(down);
  }

  // The host that owns endpoint i's address `ip_addr` (a multihomed machine; any other address, such as the any
  // address, takes the first interface's).
  sim::EmulatedSocketServer* HostFor(int i, uint32_t ip_addr) const {
    if (alt_host[i] && ip_addr == alt_ip[i]) return alt_host[i].get();
    return host[i].get();
  }

  static p2p::SocketAddress StunAddress() { return p2p::SocketAddress(p2p::IpAddress(kStunIp), kStunPort); }

  // Hands every datagram that has arrived by `now` to its socket.
  void Deliver(Timestamp now) {
    host[0]->Deliver(now);
    host[1]->Deliver(now);
    stun_host->Deliver(now);
    for (auto& h : alt_host) {
      if (h) h->Deliver(now);
    }
  }
};

// This library's socket factory for endpoint i: its sockets live on the host of the interface they bind to.
class EndpointSocketFactory final : public p2p::PacketSocketFactory {
 public:
  EndpointSocketFactory(const Topology* topo, int i) : topo_(topo), i_(i) {}
  std::unique_ptr<p2p::AsyncPacketSocket> CreateUdpSocket(const p2p::SocketAddress& address, uint16_t min_port,
                                                          uint16_t max_port) override {
    return topo_->HostFor(i_, address.ipaddr().v4())->CreateUdpSocket(address, min_port, max_port);
  }

 private:
  const Topology* topo_;
  int i_;
};

// This library's side of one run.
inline Result RunSm(const Scenario& s, uint64_t seed) {
  using namespace sm::p2p;
  Topology topo(s, seed);
  Timestamp now = Timestamp::Seconds(10);
  const Timestamp start = now;

  struct Endpoint {
    StaticNetworkManager network_manager;
    std::unique_ptr<EndpointSocketFactory> socket_factory;
    std::unique_ptr<BasicPortAllocator> allocator;
    std::unique_ptr<P2PTransportChannel> ch;
  };
  Endpoint ep[2];
  std::deque<Candidate> signaling[2];  // Candidates for endpoint i.
  PeerConnectionAllocatorConfig pc_config;
  pc_config.tcp_candidates_enabled = false;  // See the file comment.
  // The interfaces' names and types (see the file comment).
  const bool typed = s.wifi_to_cellular;
  auto add_primary = [&](int i) {
    if (typed && i == s.change) {
      ep[i].network_manager.AddInterface(IpAddress(topo.ip[i]), "eth0", ADAPTER_TYPE_WIFI);
    } else {
      ep[i].network_manager.AddInterface(IpAddress(topo.ip[i]));
    }
  };
  auto add_alternate = [&](int i) {
    if (typed) {
      ep[i].network_manager.AddInterface(IpAddress(topo.alt_ip[i]), "wlan0", ADAPTER_TYPE_CELLULAR);
    } else {
      ep[i].network_manager.AddInterface(IpAddress(topo.alt_ip[i]));
    }
  };
  for (int i = 0; i < 2; ++i) {
    add_primary(i);
    if (s.dead && i == s.change) {
      topo.AddAlternate(i);
      add_alternate(i);
    }
    ep[i].socket_factory = std::make_unique<EndpointSocketFactory>(&topo, i);
    ep[i].allocator = std::make_unique<BasicPortAllocator>(&ep[i].network_manager, ep[i].socket_factory.get());
    ConfigureAsPeerConnection(now, ep[i].allocator.get(), ServerAddresses{Topology::StunAddress()}, {}, pc_config);
    ep[i].ch = std::make_unique<P2PTransportChannel>(now, "audio", ICE_CANDIDATE_COMPONENT_RTP, ep[i].allocator.get());
    ep[i].ch->SubscribeCandidateGathered(&ep[i], [&signaling, i](IceTransportInternal*, const Candidate& c) {
      signaling[1 - i].push_back(c);
    });
    IceConfig config;
    if (s.continual) config.continual_gathering_policy = ContinualGatheringPolicy::kGatherContinually;
    ep[i].ch->SetIceConfig(config);
    ep[i].ch->SetIceRole(i == 0 ? ICEROLE_CONTROLLING : ICEROLE_CONTROLLED);
    ep[i].ch->SetIceParameters(IceParameters{kUfrag[i], kPwd[i], false});
    ep[i].ch->SetRemoteIceParameters(IceParameters{kUfrag[1 - i], kPwd[1 - i], false});
  }
  for (Endpoint& e : ep) e.ch->MaybeStartGathering(now);

  auto checks = [&] {
    int n = 0;
    for (Endpoint& e : ep) {
      IceTransportStats stats;
      e.ch->GetStats(now, &stats);
      for (const ConnectionInfo& c : stats.connection_infos) n += static_cast<int>(c.sent_ping_requests_total);
    }
    return n;
  };
  auto connected = [&] {
    for (Endpoint& e : ep) {
      if (!e.ch->writable() || !e.ch->receiving() || !e.ch->selected_connection()) return false;
    }
    return true;
  };
  auto pair = [&] {
    const Connection* sel = ep[0].ch->selected_connection();
    return std::string(sel->local_candidate().type_name()) + "/" + std::string(sel->remote_candidate().type_name());
  };

  Result r;
  const Timestamp until = start + RunTime(s);
  const Timestamp change_at = s.change < 0 ? Timestamp::PlusInfinity() : start + kChangeAt;
  bool changed = false;
  std::optional<SocketAddress> old_remote;  // The other endpoint's selected remote address before the change.
  auto recovered = [&] {
    if (!connected()) return false;
    const Connection* own = ep[s.change].ch->selected_connection();
    const Connection* other = ep[1 - s.change].ch->selected_connection();
    return own->network()->GetBestIP() == IpAddress(topo.alt_ip[s.change]) &&
           (!old_remote || other->remote_candidate().address() != *old_remote);
  };
  for (int guard = 0; guard < 5000000; ++guard) {
    if (!r.connected && connected()) {
      r.connected = true;
      r.connect_ms = (now - start).us() / 1000.0;
      r.pair = pair();
      r.checks_at_connect = checks();
    }
    if (changed && !r.recovered && recovered()) {
      r.recovered = true;
      r.recover_ms = (now - change_at).us() / 1000.0;
      r.pair_after = pair();
    }
    Timestamp t = std::min(topo.net.NextEvent(), changed ? Timestamp::PlusInfinity() : change_at);
    for (Endpoint& e : ep) {
      t = std::min(t, e.ch->NextProcessTime());
      t = std::min(t, e.allocator->NextPooledSessionsProcessTime());
    }
    if (!signaling[0].empty() || !signaling[1].empty()) t = std::min(t, now);
    if (t > until) break;
    now = std::max(now, t);
    if (!changed && now >= change_at) {
      // The network change, as P2PTransportChannelMultihomedTest's AddAddress and RemoveAddress.
      changed = true;
      if (const Connection* other = ep[1 - s.change].ch->selected_connection()) {
        old_remote = other->remote_candidate().address();
      }
      if (s.dead) {
        topo.KillPrimary(s.change);
      } else {
        topo.ChangeNetwork(s.change);
        add_alternate(s.change);
        ep[s.change].network_manager.RemoveInterface(IpAddress(topo.ip[s.change]));
      }
    }
    for (Endpoint& e : ep) {
      e.ch->Process(now);
      e.allocator->ProcessPooledSessions(now);
    }
    topo.Deliver(now);
    for (int i = 0; i < 2; ++i) {
      while (!signaling[i].empty()) {
        Candidate c = signaling[i].front();
        signaling[i].pop_front();
        ep[i].ch->AddRemoteCandidate(now, c);
      }
    }
  }
  now = until;
  r.checks_total = checks();
  return r;
}

}  // namespace sm::ice_compare
