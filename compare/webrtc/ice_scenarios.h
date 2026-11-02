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
// Recovery after a network change (an interface goes away and another appears) needs the multi-interface fixture
// of #1002 part 3; it is the next part of this item.
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

struct Result {
  bool connected = false;
  double connect_ms = -1;
  std::string pair;  // "host/srflx": local/remote candidate types of the controlling side's selected connection.
  int checks_at_connect = 0;
  int checks_total = 0;
};

// The run: until `kRunTime` after gathering starts. libwebrtc's unit tests wait up to 10 s (kDefaultTimeout) for a
// connection.
inline constexpr TimeDelta kRunTime = TimeDelta::Seconds(10);

// Addresses of the libwebrtc fixture (p2p_transport_channel_unittest.cc kPublicAddrs, kPrivateAddrs, kNatAddrs,
// kStunAddr with STUN_SERVER_PORT).
inline constexpr uint32_t kPublicIp[2] = {sim::SocketAddress::Ip(11, 11, 11, 11), sim::SocketAddress::Ip(22, 22, 22, 22)};
inline constexpr uint32_t kPrivateIp[2] = {sim::SocketAddress::Ip(192, 168, 1, 11),
                                           sim::SocketAddress::Ip(192, 168, 2, 22)};
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
      sim::LinkConfig link;
      link.base_delay = TimeDelta::Millis(s.delay_ms);
      link.loss_rate = s.loss;
      host[i] = std::make_unique<sim::EmulatedSocketServer>(net, net.AddHost(ip[i], realm, link, link));
    }
  }

  static p2p::SocketAddress StunAddress() { return p2p::SocketAddress(p2p::IpAddress(kStunIp), kStunPort); }

  // Hands every datagram that has arrived by `now` to its socket.
  void Deliver(Timestamp now) {
    host[0]->Deliver(now);
    host[1]->Deliver(now);
    stun_host->Deliver(now);
  }
};

// This library's side of one run.
inline Result RunSm(const Scenario& s, uint64_t seed) {
  using namespace sm::p2p;
  Topology topo(s, seed);
  Timestamp now = Timestamp::Seconds(10);
  const Timestamp start = now;

  struct Endpoint {
    StaticNetworkManager network_manager;
    std::unique_ptr<BasicPortAllocator> allocator;
    std::unique_ptr<P2PTransportChannel> ch;
  };
  Endpoint ep[2];
  std::deque<Candidate> signaling[2];  // Candidates for endpoint i.
  PeerConnectionAllocatorConfig pc_config;
  pc_config.tcp_candidates_enabled = false;  // See the file comment.
  for (int i = 0; i < 2; ++i) {
    ep[i].network_manager.AddInterface(IpAddress(topo.ip[i]));
    ep[i].allocator = std::make_unique<BasicPortAllocator>(&ep[i].network_manager, topo.host[i].get());
    ConfigureAsPeerConnection(now, ep[i].allocator.get(), ServerAddresses{Topology::StunAddress()}, {}, pc_config);
    ep[i].ch = std::make_unique<P2PTransportChannel>(now, "audio", ICE_CANDIDATE_COMPONENT_RTP, ep[i].allocator.get());
    ep[i].ch->SubscribeCandidateGathered(&ep[i], [&signaling, i](IceTransportInternal*, const Candidate& c) {
      signaling[1 - i].push_back(c);
    });
    ep[i].ch->SetIceConfig(IceConfig{});
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

  Result r;
  const Timestamp until = start + kRunTime;
  for (int guard = 0; guard < 5000000; ++guard) {
    if (!r.connected && connected()) {
      r.connected = true;
      r.connect_ms = (now - start).us() / 1000.0;
      const Connection* sel = ep[0].ch->selected_connection();
      r.pair = std::string(sel->local_candidate().type_name()) + "/" + std::string(sel->remote_candidate().type_name());
      r.checks_at_connect = checks();
    }
    Timestamp t = topo.net.NextEvent();
    for (Endpoint& e : ep) {
      t = std::min(t, e.ch->NextProcessTime());
      t = std::min(t, e.allocator->NextPooledSessionsProcessTime());
    }
    if (!signaling[0].empty() || !signaling[1].empty()) t = std::min(t, now);
    if (t > until) break;
    now = std::max(now, t);
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
