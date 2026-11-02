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
#include <execinfo.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "api/candidate.h"
#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/ice_transport_interface.h"
#include "api/packet_socket_factory.h"
#include "api/task_queue/task_queue_base.h"
#include "api/units/timestamp.h"
#include "ice_scenarios.h"
#include "p2p/base/active_ice_controller_factory_interface.h"
#include "p2p/base/basic_ice_controller.h"
#include "p2p/base/connection.h"
#include "p2p/base/ice_transport_internal.h"
#include "p2p/base/p2p_transport_channel.h"
#include "p2p/base/port_allocator.h"
#include "p2p/base/transport_description.h"
#include "p2p/base/wrapping_active_ice_controller.h"
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
  // Endpoint i's sockets live on the host of the interface they bind to (Topology::HostFor), as ours.
  SocketFactory(SimTime* sim, const Topology* topo, int i) : sim_(sim), topo_(topo), i_(i) {}
  std::unique_ptr<w::AsyncPacketSocket> CreateUdpSocket(const w::Environment&, const w::SocketAddress& address,
                                                        uint16_t min_port, uint16_t max_port) override {
    auto socket = topo_->HostFor(i_, address.ipaddr().v4AddressAsHostOrderInteger())
                      ->CreateUdpSocket(FromW(address), min_port, max_port);
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
  const Topology* topo_;
  int i_;
};

// rtc_base/fake_network.h's FakeNetworkManager, posting to a TaskQueueBase instead of a Thread.
class NetworkManager final : public w::NetworkManagerBase {
 public:
  explicit NetworkManager(w::TaskQueueBase* network_thread) : network_thread_(network_thread) {}

  void AddInterface(uint32_t ip) { AddInterface(ip, "test" + std::to_string(next_index_++)); }
  void AddInterface(uint32_t ip, std::string name, w::AdapterType type = w::ADAPTER_TYPE_UNKNOWN) {
    ifaces_.push_back({w::IPAddress(ip), std::move(name), type});
    DoUpdateNetworks();
  }
  void RemoveInterface(uint32_t ip) {
    for (auto it = ifaces_.begin(); it != ifaces_.end(); ++it) {
      if (it->ip == w::IPAddress(ip)) {
        ifaces_.erase(it);
        break;
      }
    }
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
    w::AdapterType type;
  };
  void DoUpdateNetworks() {
    if (start_count_ == 0) return;
    constexpr int kFakeIPv4NetworkPrefixLength = 24;
    std::vector<std::unique_ptr<w::Network>> networks;
    for (const Iface& it : ifaces_) {
      auto net = std::make_unique<w::Network>(it.name, it.name, w::TruncateIP(it.ip, kFakeIPv4NetworkPrefixLength),
                                              kFakeIPv4NetworkPrefixLength, it.type);
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

// The ICE controller P2PTransportChannel makes without a factory: a BasicIceController wrapped by
// WrappingActiveIceController. Made here through the constructor that takes the network thread from
// TaskQueueBase::Current(), because the one the channel uses reads Thread::Current()
// (p2p/base/wrapping_active_ice_controller.cc), which is null on SimTime's queues; the controller is otherwise the
// same.
class IceControllerFactory final : public w::ActiveIceControllerFactoryInterface {
 public:
  std::unique_ptr<w::ActiveIceControllerInterface> Create(const w::ActiveIceControllerFactoryArgs& args) override {
    return std::make_unique<w::WrappingActiveIceController>(
        args.ice_agent, std::make_unique<w::BasicIceController>(args.legacy_args));
  }
};

// Set by --trace: prints each setup step of libwebrtc's side to stderr, to find a crash.
bool g_trace = false;
void Stage(const char* what) {
  if (g_trace) std::fprintf(stderr, "  libwebrtc: %s\n", what);
}

// PeerConnection::InitializePortAllocator_n (pc/peer_connection.cc) with RTCConfiguration's defaults
// (api/peer_connection_interface.h), as sm::p2p::ConfigureAsPeerConnection does on our side, and TCP candidates off
// (ice_scenarios.h). The defaults are written out rather than read from a default-constructed RTCConfiguration, whose
// out-of-line constructor in libwebrtc.a this build would have to match field for field:
// disable_ipv6_on_wifi = false, candidate_network_policy = kCandidateNetworkPolicyAll,
// disable_link_local_networks = false, type = kAll (CF_ALL), max_ipv6_networks = kDefaultMaxIPv6Networks,
// ice_candidate_pool_size = 0, prune_turn_ports = false and turn_port_prune_policy = NO_PRUNE (GetTurnPortPrunePolicy),
// turn_customizer = nullptr, stun_candidate_keepalive_interval unset.
void ConfigureAsPeerConnection(w::PortAllocator* allocator, const w::ServerAddresses& stun_servers) {
  Stage("initialize");
  allocator->Initialize();
  uint32_t flags = allocator->flags();
  flags |= w::PORTALLOCATOR_ENABLE_SHARED_SOCKET | w::PORTALLOCATOR_ENABLE_IPV6 | w::PORTALLOCATOR_ENABLE_IPV6_ON_WIFI;
  // WebRTC-IPv6Default is not disabled with the default field trials; disable_ipv6_on_wifi is false.
  flags |= w::PORTALLOCATOR_DISABLE_TCP;  // kTcpCandidatePolicyDisabled; RTCConfiguration's default enables TCP.
  allocator->set_flags(flags);
  allocator->set_step_delay(w::kMinimumStepDelay);
  allocator->SetCandidateFilter(w::CF_ALL);  // ConvertIceTransportTypeToCandidateFilter(kAll).
  allocator->set_max_ipv6_networks(w::kDefaultMaxIPv6Networks);
  Stage("set configuration");
  allocator->SetConfiguration(stun_servers, {}, /*candidate_pool_size=*/0, w::NO_PRUNE, /*turn_customizer=*/nullptr,
                              /*stun_candidate_keepalive_interval=*/std::nullopt);
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
  Stage("environment");
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
  IceControllerFactory ice_controller_factory;

  run_on_network([&] {
    w::ServerAddresses stun_servers = {ToW(Topology::StunAddress())};
    for (int i = 0; i < 2; ++i) {
      Endpoint& e = ep[i];
      Stage("network manager");
      e.network_manager = std::make_unique<NetworkManager>(queue.get());
      // As RunSm: the interfaces' names and types.
      if (s.wifi_to_cellular && i == s.change) {
        e.network_manager->AddInterface(topo.ip[i], "eth0", w::ADAPTER_TYPE_WIFI);
      } else {
        e.network_manager->AddInterface(topo.ip[i]);
      }
      if (s.dead && i == s.change) {
        topo.AddAlternate(i);
        e.network_manager->AddInterface(topo.alt_ip[i], "wlan0", w::ADAPTER_TYPE_CELLULAR);
      }
      e.socket_factory = std::make_unique<SocketFactory>(&sim, &topo, i);
      Stage("allocator");
      e.allocator = std::make_unique<w::BasicPortAllocator>(env, e.network_manager.get(), e.socket_factory.get());
      Stage("configure");
      ConfigureAsPeerConnection(e.allocator.get(), stun_servers);
      Stage("channel");
      // Create allocates the channel inside libwebrtc.a, at the library's own sizeof.
      w::IceTransportInit init(env);
      init.set_port_allocator(e.allocator.get());
      init.set_active_ice_controller_factory(&ice_controller_factory);
      e.ch = w::P2PTransportChannel::Create("audio", w::ICE_CANDIDATE_COMPONENT_RTP, std::move(init));
      e.ch->SubscribeCandidateGathered(&e, [&signaling, i](w::IceTransportInternal*, const w::Candidate& c) {
        signaling[1 - i].push_back(c);
      });
      Stage("ice config");
      // IceConfig(RTCConfiguration()) leaves every optional unset and gathers once (p2p/base/ice_transport_internal.cc):
      // the default IceConfig; continual_gathering_policy = GATHER_CONTINUALLY carries over.
      w::IceConfig config;
      if (s.continual) config.continual_gathering_policy = w::GATHER_CONTINUALLY;
      e.ch->SetIceConfig(config);
      Stage("ice role");
      e.ch->SetIceRole(i == 0 ? w::ICEROLE_CONTROLLING : w::ICEROLE_CONTROLLED);
      Stage("ice parameters");
      e.ch->SetIceParameters(w::IceParameters(kUfrag[i], kPwd[i], false));
      Stage("remote ice parameters");
      e.ch->SetRemoteIceParameters(w::IceParameters(kUfrag[1 - i], kPwd[1 - i], false));
    }
    Stage("gathering");
    for (Endpoint& e : ep) e.ch->MaybeStartGathering();
    Stage("started");
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

  auto pair = [&] {
    const w::Connection* sel = ep[0].ch->selected_connection();
    return std::string(sel->local_candidate().type_name()) + "/" + std::string(sel->remote_candidate().type_name());
  };

  Result r;
  const w::Timestamp until = start + w::TimeDelta::Micros(RunTime(s).us());
  const w::Timestamp change_at =
      s.change < 0 ? w::Timestamp::PlusInfinity() : start + w::TimeDelta::Micros(kChangeAt.us());
  bool changed = false;
  std::optional<w::SocketAddress> old_remote;  // As RunSm.
  auto recovered = [&] {
    if (!connected()) return false;
    const w::Connection* own = ep[s.change].ch->selected_connection();
    const w::Connection* other = ep[1 - s.change].ch->selected_connection();
    return own->network()->GetBestIP() == w::IPAddress(topo.alt_ip[s.change]) &&
           (!old_remote || other->remote_candidate().address() != *old_remote);
  };
  for (int guard = 0; guard < 5000000; ++guard) {
    run_on_network([&] {
      if (!r.connected && connected()) {
        r.connected = true;
        r.connect_ms = (sim.Now() - start).us() / 1000.0;
        r.pair = pair();
        r.checks_at_connect = checks();
      }
      if (changed && !r.recovered && recovered()) {
        r.recovered = true;
        r.recover_ms = (sim.Now() - change_at).us() / 1000.0;
        r.pair_after = pair();
      }
    });
    w::Timestamp t = std::min(sim.NextRunTime(), ToW(topo.net.NextEvent()));
    if (!changed) t = std::min(t, change_at);
    if (!signaling[0].empty() || !signaling[1].empty()) t = std::min(t, sim.Now());
    if (t > until) break;
    sim.RunUntil(t);
    run_on_network([&] {
      if (!changed && sim.Now() >= change_at) {
        // The network change, as RunSm.
        changed = true;
        if (const w::Connection* other = ep[1 - s.change].ch->selected_connection()) {
          old_remote = other->remote_candidate().address();
        }
        if (s.dead) {
          topo.KillPrimary(s.change);
        } else {
          topo.ChangeNetwork(s.change);
          if (s.wifi_to_cellular) {
            ep[s.change].network_manager->AddInterface(topo.alt_ip[s.change], "wlan0", w::ADAPTER_TYPE_CELLULAR);
          } else {
            ep[s.change].network_manager->AddInterface(topo.alt_ip[s.change]);
          }
          ep[s.change].network_manager->RemoveInterface(topo.ip[s.change]);
        }
      }
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
  int recovered = 0;
  double recover_ms = 0;  // Mean over the recovered runs.
  std::map<std::string, int> pairs_after;

  void Add(const Result& r) {
    ++runs;
    checks_total += r.checks_total;
    if (r.recovered) {
      ++recovered;
      recover_ms += r.recover_ms;
      ++pairs_after[r.pair_after];
    }
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
    if (recovered) recover_ms /= recovered;
    if (runs) checks_total /= runs;
  }
  static std::string PairList(const std::map<std::string, int>& m, int count) {
    if (m.empty()) return "-";
    std::string out;
    for (const auto& [p, n] : m) {
      if (!out.empty()) out += " ";
      out += p;
      if (n != count) out += "x" + std::to_string(n);
    }
    return out;
  }
  static std::string MsOf(int count, double ms) {
    if (!count) return "-";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f", ms);
    return buf;
  }
  std::string Pairs() const { return PairList(pairs, connected); }
  std::string PairsAfter() const { return PairList(pairs_after, recovered); }
  std::string Ms() const { return MsOf(connected, connect_ms); }
  std::string RecoverMs() const { return MsOf(recovered, recover_ms); }
};

// The metrics differ by more than the parity band (sm_compare's tie tolerance: 5% relative, with an absolute floor).
// A crash prints its stack (the binary is linked with -rdynamic), so the compare job's log shows where it failed.
void OnCrash(int sig) {
  void* frames[64];
  const int n = backtrace(frames, 64);
  const char msg[] = "sm_ice_compare: crashed, stack:\n";
  (void)!write(2, msg, sizeof msg - 1);
  backtrace_symbols_fd(frames, n, 2);
  std::signal(sig, SIG_DFL);
  std::raise(sig);
}

bool Differs(double a, double b, double floor) { return std::fabs(a - b) > std::max(floor, 0.05 * std::max(a, b)); }

}  // namespace

int main(int argc, char** argv) {
  int seeds = 5;
  bool verbose = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strncmp(argv[i], "--seeds=", 8) == 0) seeds = std::atoi(argv[i] + 8);
    else if (std::strcmp(argv[i], "--verbose") == 0) verbose = true;
    else if (std::strcmp(argv[i], "--trace") == 0) g_trace = verbose = true;
    else {
      std::fprintf(stderr, "usage: %s [--seeds=N] [--verbose] [--trace]\n", argv[0]);
      return 2;
    }
  }
  std::signal(SIGSEGV, OnCrash);
  std::signal(SIGABRT, OnCrash);
  std::signal(SIGILL, OnCrash);
  std::signal(SIGTRAP, OnCrash);
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  sm::p2p::testing::InstallTestCrypto();
  if (g_trace) {
    // libwebrtc.a's own sizes (operator new in P2PTransportChannel::Create, the stores of IceConfig::IceConfig()), to
    // check this build lays the classes out the same way.
    std::fprintf(stderr, "sizeof P2PTransportChannel %zu (libwebrtc.a 1616), IceConfig %zu (224), BasicPortAllocator %zu (512)\n",
                 sizeof(w::P2PTransportChannel), sizeof(w::IceConfig), sizeof(w::BasicPortAllocator));
  }

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
      if (verbose) std::fprintf(stderr, "%s seed %d: ours...\n", s.name.c_str(), seed);
      const Result a = RunSm(s, seed);
      if (verbose) std::fprintf(stderr, "%s seed %d: libwebrtc...\n", s.name.c_str(), seed);
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
  std::printf("\n%d of %zu scenarios differ.\n", gaps, Scenarios().size());

  // Network changes (#1004 part 2).
  std::printf("\nRecovery after a network change: endpoint changeN's interface goes away and another appears %d s "
              "after gathering starts; recovery within %d s. Ours / libwebrtc\n\n",
              static_cast<int>(kChangeAt.us() / 1000000), static_cast<int>(kRecoveryTime.us() / 1000000));
  std::printf("| scenario | connected | recovered | recover ms | pair after (local/remote) | checks in %d s | gap |\n",
              static_cast<int>((kChangeAt + kRecoveryTime).us() / 1000000));
  std::printf("|---|---|---|---|---|---|---|\n");
  int change_gaps = 0;
  for (const Scenario& s : ChangeScenarios()) {
    Summary ours, theirs;
    for (int seed = 1; seed <= seeds; ++seed) {
      if (verbose) std::fprintf(stderr, "%s seed %d: ours...\n", s.name.c_str(), seed);
      const Result a = RunSm(s, seed);
      if (verbose) std::fprintf(stderr, "%s seed %d: libwebrtc...\n", s.name.c_str(), seed);
      const Result b = RunWebrtc(s, seed);
      ours.Add(a);
      theirs.Add(b);
      if (verbose) {
        std::fprintf(stderr, "%s seed %d: ours %d %d %.1f ms %s %d; libwebrtc %d %d %.1f ms %s %d\n", s.name.c_str(),
                     seed, a.connected, a.recovered, a.recover_ms, a.pair_after.c_str(), a.checks_total, b.connected,
                     b.recovered, b.recover_ms, b.pair_after.c_str(), b.checks_total);
      }
    }
    ours.Finish();
    theirs.Finish();
    std::string gap;
    if (ours.connected != theirs.connected) gap += " connected";
    if (ours.recovered != theirs.recovered) gap += " recovered";
    if (ours.recovered && theirs.recovered && Differs(ours.recover_ms, theirs.recover_ms, 10)) gap += " recover_ms";
    if (ours.PairsAfter() != theirs.PairsAfter()) gap += " pair_after";
    if (Differs(ours.checks_total, theirs.checks_total, 1)) gap += " checks_total";
    if (!gap.empty()) ++change_gaps;
    std::printf("| %s | %d/%d / %d/%d | %d/%d / %d/%d | %s / %s | %s / %s | %.1f / %.1f | %s |\n", s.name.c_str(),
                ours.connected, ours.runs, theirs.connected, theirs.runs, ours.recovered, ours.runs, theirs.recovered,
                theirs.runs, ours.RecoverMs().c_str(), theirs.RecoverMs().c_str(), ours.PairsAfter().c_str(),
                theirs.PairsAfter().c_str(), ours.checks_total, theirs.checks_total, gap.empty() ? "" : gap.c_str() + 1);
  }
  std::printf("\n%d of %zu network-change scenarios differ. Not run yet: TCP and relay candidates (C4).\n",
              change_gaps, ChangeScenarios().size());
  return 0;
}
