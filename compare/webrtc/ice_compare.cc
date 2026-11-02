// sm_ice_compare (issue #1004, C3 part 8): this library's ICE agent and libwebrtc's P2PTransportChannel, set up as
// PeerConnection sets them up, on the same emulated network with NATs, side by side. ice_scenarios.h has the
// scenarios, the network, the metrics and this library's side; this file has libwebrtc's side and the report.
//
//   out/compare/sm_ice_compare [--seeds=N] [--verbose]
//
// libwebrtc's side runs on SimTime (sim_time.h): its network thread is one SimQueue, and the loop below runs
// libwebrtc's tasks and the emulated network's arrivals in time order, delivering each arrival on that queue. Its
// sockets are this library's emulated sockets (sim::EmulatedSocketServer) behind webrtc::AsyncPacketSocket, in the
// role AsyncUDPSocket over a VirtualSocketServer plays in libwebrtc's p2p_transport_channel_unittest.cc, so both
// stacks bind the same ports and cross the same NATs and links. Its NetworkManager is rtc_base/fake_network.h's
// FakeNetworkManager on a TaskQueueBase (the prebuilt library leaves test code out).
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "api/candidate.h"
#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/packet_socket_factory.h"
#include "api/peer_connection_interface.h"
#include "api/task_queue/task_queue_base.h"
#include "api/units/timestamp.h"
#include "ice_scenarios.h"
#include "p2p/base/connection.h"
#include "p2p/base/ice_transport_internal.h"
#include "p2p/base/p2p_transport_channel.h"
#include "p2p/base/port_allocator.h"
#include "p2p/base/transport_description.h"
#include "p2p/client/basic_port_allocator.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/network.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_address.h"
#include "sim_time.h"
#include "test_crypto.h"

namespace w = webrtc;
using sm::compare::SimTime;
using namespace sm::ice_compare;

namespace {

w::SocketAddress ToW(const sm::p2p::SocketAddress& a) { return w::SocketAddress(w::IPAddress(a.ip()), a.port()); }
sm::p2p::SocketAddress FromW(const w::SocketAddress& a) {
  return sm::p2p::SocketAddress(sm::p2p::IpAddress(a.ipaddr().v4AddressAsHostOrderInteger()), a.port());
}
sm::Timestamp ToSm(w::Timestamp t) { return sm::Timestamp::Micros(t.us()); }
w::Timestamp ToW(sm::Timestamp t) { return t.IsFinite() ? w::Timestamp::Micros(t.us()) : w::Timestamp::PlusInfinity(); }

// An emulated UDP socket as libwebrtc's AsyncUDPSocket (rtc_base/async_udp_socket.cc): SendTo reports the sent
// packet after the send, and received datagrams are stamped with the arrival time.
class UdpSocket final : public w::AsyncPacketSocket {
 public:
  UdpSocket(SimTime* sim, std::unique_ptr<sm::p2p::AsyncPacketSocket> socket) : sim_(sim), socket_(std::move(socket)) {
    socket_->RegisterReceivedPacketCallback([this](sm::p2p::AsyncPacketSocket*, const sm::p2p::ReceivedPacket& p) {
      NotifyPacketReceived(w::ReceivedIpPacket(p.payload, ToW(p.source_address), sim_->Now()));
    });
  }
  ~UdpSocket() override { socket_->DeregisterReceivedPacketCallback(); }

  w::SocketAddress GetLocalAddress() const override { return ToW(socket_->GetLocalAddress()); }
  w::SocketAddress GetRemoteAddress() const override { return w::SocketAddress(); }
  int Send(const void*, size_t, const w::AsyncSocketPacketOptions&) override {
    error_ = ENOTCONN;
    return -1;
  }
  int SendTo(const void* pv, size_t cb, const w::SocketAddress& addr,
             const w::AsyncSocketPacketOptions& options) override {
    w::SentPacketInfo sent(options.packet_id, sim_->Now().ms(), options.info_signaled_after_sent);
    w::CopySocketInformationToPacketInfo(cb, *this, &sent.info);
    const int ret = socket_->SendTo(ToSm(sim_->Now()), std::span(static_cast<const uint8_t*>(pv), cb), FromW(addr),
                                    sm::p2p::PacketOptions{});
    if (ret < 0) error_ = socket_->GetError();
    NotifySentPacket(this, sent);
    return ret;
  }
  int Close() override { return socket_->Close(); }
  State GetState() const override { return STATE_BOUND; }
  int GetOption(w::Socket::Option opt, int* value) override {
    auto it = options_.find(opt);
    if (it == options_.end()) return -1;
    *value = it->second;
    return 0;
  }
  int SetOption(w::Socket::Option opt, int value) override {
    options_[opt] = value;
    return 0;
  }
  int GetError() const override { return error_; }
  void SetError(int error) override { error_ = error; }

 private:
  SimTime* sim_;
  std::unique_ptr<sm::p2p::AsyncPacketSocket> socket_;
  std::map<w::Socket::Option, int> options_;
  int error_ = 0;
};

// UDP only, as the comparison runs with TCP candidates off (ice_scenarios.h).
class SocketFactory final : public w::PacketSocketFactory {
 public:
  SocketFactory(SimTime* sim, sm::sim::EmulatedSocketServer* host) : sim_(sim), host_(host) {}
  std::unique_ptr<w::AsyncPacketSocket> CreateUdpSocket(const w::Environment&, const w::SocketAddress& address,
                                                        uint16_t min_port, uint16_t max_port) override {
    auto socket = host_->CreateUdpSocket(FromW(address), min_port, max_port);
    if (!socket) return nullptr;
    return std::make_unique<UdpSocket>(sim_, std::move(socket));
  }
  std::unique_ptr<w::AsyncListenSocket> CreateServerTcpSocket(const w::Environment&, const w::SocketAddress&,
                                                              uint16_t, uint16_t, int) override {
    return nullptr;
  }
  std::unique_ptr<w::AsyncPacketSocket> CreateClientTcpSocket(const w::Environment&, const w::SocketAddress&,
                                                              const w::SocketAddress&,
                                                              const w::revive::ProxyInfo&, const std::string&,
                                                              const w::PacketSocketTcpOptions&) override {
    return nullptr;
  }
  std::unique_ptr<w::AsyncDnsResolverInterface> CreateAsyncDnsResolver() override { return nullptr; }

 private:
  SimTime* sim_;
  sm::sim::EmulatedSocketServer* host_;
};

// rtc_base/fake_network.h's FakeNetworkManager, posting to a TaskQueueBase instead of a Thread.
class NetworkManager final : public w::NetworkManagerBase {
 public:
  explicit NetworkManager(w::TaskQueueBase* network_thread) : network_thread_(network_thread) {}

  void AddInterface(uint32_t ip) {
    ifaces_.push_back({w::IPAddress(ip), "test" + std::to_string(next_index_++)});
    DoUpdateNetworks();
  }
  void StartUpdating() override {
    ++start_count_;
    if (start_count_ == 1) {
      sent_first_update_ = false;
      network_thread_->PostTask([this] { DoUpdateNetworks(); });
    } else if (sent_first_update_) {
      network_thread_->PostTask([this] { NotifyNetworksChanged(); });
    }
  }
  void StopUpdating() override { --start_count_; }

 private:
  struct Iface {
    w::IPAddress ip;
    std::string name;
  };
  void DoUpdateNetworks() {
    if (start_count_ == 0) return;
    constexpr int kFakeIPv4NetworkPrefixLength = 24;
    std::vector<std::unique_ptr<w::Network>> networks;
    for (const Iface& it : ifaces_) {
      auto net = std::make_unique<w::Network>(it.name, it.name, w::TruncateIP(it.ip, kFakeIPv4NetworkPrefixLength),
                                              kFakeIPv4NetworkPrefixLength, w::ADAPTER_TYPE_UNKNOWN);
      net->set_default_local_address_provider(this);
      net->AddIP(it.ip);
      networks.push_back(std::move(net));
    }
    bool changed = false;
    MergeNetworkList(std::move(networks), &changed);
    if (changed || !sent_first_update_) {
      NotifyNetworksChanged();
      sent_first_update_ = true;
    }
  }

  w::TaskQueueBase* const network_thread_;
  std::vector<Iface> ifaces_;
  int next_index_ = 0;
  int start_count_ = 0;
  bool sent_first_update_ = false;
};

// PeerConnection::InitializePortAllocator_n (pc/peer_connection.cc) with RTCConfiguration's defaults, as
// sm::p2p::ConfigureAsPeerConnection does on our side, and TCP candidates off (ice_scenarios.h).
void ConfigureAsPeerConnection(w::PortAllocator* allocator, const w::ServerAddresses& stun_servers) {
  const w::PeerConnectionInterface::RTCConfiguration configuration;
  allocator->Initialize();
  uint32_t flags = allocator->flags();
  flags |= w::PORTALLOCATOR_ENABLE_SHARED_SOCKET | w::PORTALLOCATOR_ENABLE_IPV6 | w::PORTALLOCATOR_ENABLE_IPV6_ON_WIFI;
  // WebRTC-IPv6Default is not disabled with the default field trials.
  if (configuration.disable_ipv6_on_wifi) flags &= ~uint32_t{w::PORTALLOCATOR_ENABLE_IPV6_ON_WIFI};
  flags |= w::PORTALLOCATOR_DISABLE_TCP;  // kTcpCandidatePolicyDisabled; RTCConfiguration's default enables TCP.
  if (configuration.candidate_network_policy == w::PeerConnectionInterface::kCandidateNetworkPolicyLowCost) {
    flags |= w::PORTALLOCATOR_DISABLE_COSTLY_NETWORKS;
  }
  if (configuration.disable_link_local_networks) flags |= w::PORTALLOCATOR_DISABLE_LINK_LOCAL_NETWORKS;
  allocator->set_flags(flags);
  allocator->set_step_delay(w::kMinimumStepDelay);
  // ConvertIceTransportTypeToCandidateFilter(kAll), RTCConfiguration's default type.
  allocator->SetCandidateFilter(w::CF_ALL);
  allocator->set_max_ipv6_networks(configuration.max_ipv6_networks);
  allocator->SetConfiguration(stun_servers, {}, configuration.ice_candidate_pool_size,
                              configuration.GetTurnPortPrunePolicy(), configuration.turn_customizer,
                              configuration.stun_candidate_keepalive_interval);
}

Result RunWebrtc(const Scenario& s, uint64_t seed) {
  Topology topo(s, seed);
  const w::Timestamp start = w::Timestamp::Seconds(10);  // RunSm's start.
  SimTime sim(start);
  w::ScopedYieldPolicy yield_policy(&sim);
  w::EnvironmentFactory env_factory;
  env_factory.Set(sim.clock());
  env_factory.Set(static_cast<w::TaskQueueFactory*>(&sim));
  const w::Environment env = env_factory.Create();
  auto queue = sim.CreateTaskQueue("network", w::TaskQueueFactory::Priority::NORMAL);
  auto run_on_network = [&](auto f) {
    queue->PostTask(std::move(f));
    sim.RunReady();
  };

  struct Endpoint {
    std::unique_ptr<NetworkManager> network_manager;
    std::unique_ptr<SocketFactory> socket_factory;
    std::unique_ptr<w::BasicPortAllocator> allocator;
    std::unique_ptr<w::P2PTransportChannel> ch;
  };
  Endpoint ep[2];
  std::deque<w::Candidate> signaling[2];  // Candidates for endpoint i.

  run_on_network([&] {
    w::ServerAddresses stun_servers = {ToW(Topology::StunAddress())};
    for (int i = 0; i < 2; ++i) {
      Endpoint& e = ep[i];
      e.network_manager = std::make_unique<NetworkManager>(queue.get());
      e.network_manager->AddInterface(topo.ip[i]);
      e.socket_factory = std::make_unique<SocketFactory>(&sim, topo.host[i].get());
      e.allocator = std::make_unique<w::BasicPortAllocator>(env, e.network_manager.get(), e.socket_factory.get());
      ConfigureAsPeerConnection(e.allocator.get(), stun_servers);
      e.ch = std::make_unique<w::P2PTransportChannel>(env, "audio", w::ICE_CANDIDATE_COMPONENT_RTP, e.allocator.get());
      e.ch->SubscribeCandidateGathered(&e, [&signaling, i](w::IceTransportInternal*, const w::Candidate& c) {
        signaling[1 - i].push_back(c);
      });
      e.ch->SetIceConfig(w::IceConfig(w::PeerConnectionInterface::RTCConfiguration()));
      e.ch->SetIceRole(i == 0 ? w::ICEROLE_CONTROLLING : w::ICEROLE_CONTROLLED);
      e.ch->SetIceParameters(w::IceParameters(kUfrag[i], kPwd[i], false));
      e.ch->SetRemoteIceParameters(w::IceParameters(kUfrag[1 - i], kPwd[1 - i], false));
    }
    for (Endpoint& e : ep) e.ch->MaybeStartGathering();
  });

  auto checks = [&] {
    int n = 0;
    for (Endpoint& e : ep) {
      w::IceTransportStats stats;
      e.ch->GetStats(&stats);
      for (const w::ConnectionInfo& c : stats.connection_infos) n += static_cast<int>(c.sent_ping_requests_total);
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
  const w::Timestamp until = start + w::TimeDelta::Micros(kRunTime.us());
  for (int guard = 0; guard < 5000000; ++guard) {
    run_on_network([&] {
      if (!r.connected && connected()) {
        r.connected = true;
        r.connect_ms = (sim.Now() - start).us() / 1000.0;
        const w::Connection* sel = ep[0].ch->selected_connection();
        r.pair = std::string(sel->local_candidate().type_name()) + "/" +
                 std::string(sel->remote_candidate().type_name());
        r.checks_at_connect = checks();
      }
    });
    w::Timestamp t = std::min(sim.NextRunTime(), ToW(topo.net.NextEvent()));
    if (!signaling[0].empty() || !signaling[1].empty()) t = std::min(t, sim.Now());
    if (t > until) break;
    sim.RunUntil(t);
    run_on_network([&] {
      topo.Deliver(ToSm(sim.Now()));
      for (int i = 0; i < 2; ++i) {
        while (!signaling[i].empty()) {
          w::Candidate c = signaling[i].front();
          signaling[i].pop_front();
          ep[i].ch->AddRemoteCandidate(c);
        }
      }
    });
  }
  sim.RunUntil(until);
  run_on_network([&] {
    r.checks_total = checks();
    for (Endpoint& e : ep) {
      e.ch.reset();
      e.allocator.reset();
    }
  });
  return r;
}

struct Summary {
  int runs = 0;
  int connected = 0;
  double connect_ms = 0;  // Mean over the connected runs.
  double checks_at_connect = 0;
  double checks_total = 0;  // Mean over all runs.
  std::map<std::string, int> pairs;

  void Add(const Result& r) {
    ++runs;
    checks_total += r.checks_total;
    if (!r.connected) return;
    ++connected;
    connect_ms += r.connect_ms;
    checks_at_connect += r.checks_at_connect;
    ++pairs[r.pair];
  }
  void Finish() {
    if (connected) {
      connect_ms /= connected;
      checks_at_connect /= connected;
    }
    if (runs) checks_total /= runs;
  }
  std::string Pairs() const {
    if (pairs.empty()) return "-";
    std::string out;
    for (const auto& [p, n] : pairs) {
      if (!out.empty()) out += " ";
      out += p;
      if (n != connected) out += "x" + std::to_string(n);
    }
    return out;
  }
  std::string Ms() const {
    if (!connected) return "-";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f", connect_ms);
    return buf;
  }
};

// The metrics differ by more than the parity band (sm_compare's tie tolerance: 5% relative, with an absolute floor).
bool Differs(double a, double b, double floor) { return std::fabs(a - b) > std::max(floor, 0.05 * std::max(a, b)); }

}  // namespace

int main(int argc, char** argv) {
  int seeds = 5;
  bool verbose = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strncmp(argv[i], "--seeds=", 8) == 0) seeds = std::atoi(argv[i] + 8);
    else if (std::strcmp(argv[i], "--verbose") == 0) verbose = true;
    else {
      std::fprintf(stderr, "usage: %s [--seeds=N] [--verbose]\n", argv[0]);
      return 2;
    }
  }
  sm::p2p::testing::InstallTestCrypto();

  std::printf("ICE against libwebrtc M157 (#1004): P2PTransportChannel set up as PeerConnection, UDP and STUN, "
              "%d seeds, ours / libwebrtc\n\n",
              seeds);
  std::printf("| scenario | connected | connect ms | pair (local/remote) | checks at connect | checks in %d s | gap |\n",
              static_cast<int>(kRunTime.us() / 1000000));
  std::printf("|---|---|---|---|---|---|---|\n");
  int gaps = 0;
  for (const Scenario& s : Scenarios()) {
    Summary ours, theirs;
    for (int seed = 1; seed <= seeds; ++seed) {
      const Result a = RunSm(s, seed);
      const Result b = RunWebrtc(s, seed);
      ours.Add(a);
      theirs.Add(b);
      if (verbose) {
        std::fprintf(stderr, "%s seed %d: ours %d %.1f ms %s %d/%d; libwebrtc %d %.1f ms %s %d/%d\n", s.name.c_str(),
                     seed, a.connected, a.connect_ms, a.pair.c_str(), a.checks_at_connect, a.checks_total,
                     b.connected, b.connect_ms, b.pair.c_str(), b.checks_at_connect, b.checks_total);
      }
    }
    ours.Finish();
    theirs.Finish();
    std::string gap;
    if (ours.connected != theirs.connected) gap += " connected";
    if (ours.connected && theirs.connected && Differs(ours.connect_ms, theirs.connect_ms, 10)) gap += " connect_ms";
    if (ours.Pairs() != theirs.Pairs()) gap += " pair";
    if (ours.connected && theirs.connected && Differs(ours.checks_at_connect, theirs.checks_at_connect, 1)) {
      gap += " checks_at_connect";
    }
    if (Differs(ours.checks_total, theirs.checks_total, 1)) gap += " checks_total";
    if (!gap.empty()) ++gaps;
    std::printf("| %s | %d/%d / %d/%d | %s / %s | %s / %s | %.1f / %.1f | %.1f / %.1f | %s |\n", s.name.c_str(),
                ours.connected, ours.runs, theirs.connected, theirs.runs, ours.Ms().c_str(), theirs.Ms().c_str(),
                ours.Pairs().c_str(), theirs.Pairs().c_str(), ours.checks_at_connect, theirs.checks_at_connect,
                ours.checks_total, theirs.checks_total, gap.empty() ? "" : gap.c_str() + 1);
  }
  std::printf("\n%d of %zu scenarios differ. Not run yet: recovery after a network change (#1004 part 2, after "
              "#1002 part 3's multi-interface fixture); TCP and relay candidates (C4).\n",
              gaps, Scenarios().size());
  return 0;
}
