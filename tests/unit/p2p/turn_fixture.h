// The TURN test fixture (issues #1031, #1036): libwebrtc M157's TurnPortTest
// (p2p/base/turn_port_unittest.cc) on the emulated network, shared by the UDP
// cases (turn_port_test.cc), the TCP cases (turn_port_tcp_test.cc) and the
// TLS cases (turn_port_tls_test.cc). Times are the emulated links' (10 ms one
// way to each host, so the client-server RTT is 40 ms where libwebrtc's
// kSimulatedRtt is 50 ms).
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/basic_port_allocator.h"
#include "sm/p2p/connection.h"
#include "sm/p2p/network.h"
#include "sm/p2p/network_manager.h"
#include "sm/p2p/port.h"
#include "sm/p2p/socket.h"
#include "sm/p2p/stun.h"
#include "sm/p2p/stun_port.h"
#include "sm/p2p/turn_port.h"
#include "sm/sim/emulated_sockets.h"
#include "sm/sim/turn_server.h"
#include "sm_test.h"
#include "test_crypto.h"

using namespace sm;
using namespace sm::p2p;

namespace {

constexpr uint32_t kLocalIp1 = sim::SocketAddress::Ip(11, 11, 11, 11);
constexpr uint32_t kLocalIp2 = sim::SocketAddress::Ip(22, 22, 22, 22);
constexpr uint32_t kTurnIntIp = sim::SocketAddress::Ip(99, 99, 99, 3);
constexpr uint32_t kTurnExtIp = sim::SocketAddress::Ip(99, 99, 99, 5);
constexpr uint32_t kTurnAltIp = sim::SocketAddress::Ip(99, 99, 99, 6);
// The TCP and TLS server (kTurnTcpIntAddr): TCP and UDP share a host's port
// table in the emulated network, so it has a host of its own, as libwebrtc's
// test puts it on 99.99.99.4.
constexpr uint32_t kTurnTcpIp = sim::SocketAddress::Ip(99, 99, 99, 4);
constexpr uint32_t kNatIp = sim::SocketAddress::Ip(77, 77, 77, 77);
constexpr uint32_t kPrivateIp = sim::SocketAddress::Ip(192, 168, 1, 2);
const SocketAddress kTurnUdpIntAddr(kTurnIntIp, 3478);
const SocketAddress kTurnAlternateIntAddr(kTurnAltIp, 3478);
const SocketAddress kTurnTcpIntAddr(kTurnTcpIp, 3478);
const SocketAddress kTurnUdpExtAddr(kTurnExtIp, 0);
const char kTurnUsername[] = "test";
const char kTurnPassword[] = "test";
const char kTestRealm[] = "example.org";
constexpr TimeDelta kSimulatedRtt = TimeDelta::Millis(40);

// TimeToConnect: the emulated TCP connects in one RTT (SYN, SYN-ACK), where
// libwebrtc's virtual server takes half of one; TLS adds the two handshake
// round trips libwebrtc's test allows.
TimeDelta TimeToConnect(ProtocolType proto) {
  switch (proto) {
    case PROTO_TCP: return kSimulatedRtt;
    case PROTO_TLS: return 2 * kSimulatedRtt + TimeToConnect(PROTO_TCP);
    default: return TimeDelta::Zero();
  }
}
// TimeToGetTurnCandidate: a 401, then the allocation.
TimeDelta TimeToGetTurnCandidate(ProtocolType proto) { return 2 * kSimulatedRtt + TimeToConnect(proto); }
// TimeToGetAlternateTurnCandidate: connect, 300, connect to the alternate,
// 401, then the allocation.
TimeDelta TimeToGetAlternateTurnCandidate(ProtocolType proto) {
  return 3 * kSimulatedRtt + 2 * TimeToConnect(proto);
}

sim::LinkConfig Link(int delay_ms) {
  sim::LinkConfig c;
  c.base_delay = TimeDelta::Millis(delay_ms);
  return c;
}

// test_crypto's CounterBytes repeats its 12-byte transaction ids after 64 requests, which a TURN port with refreshes
// and permissions reaches, so these tests use the non-repeating ids.
void InstallTurnTestCrypto() { testing::InstallTestCryptoWithUniqueIds(); }

// test_turn_server.h's auth: the password is the username.
class TestAuth final : public sim::TurnAuthInterface {
 public:
  bool GetKey(std::string_view username, std::string_view realm, std::string* key) override {
    return ComputeStunCredentialHash(std::string(username), std::string(realm), std::string(username), key);
  }
};

// TestTurnRedirector: redirects to each address in turn, once.
class TestRedirector final : public sim::TurnRedirectInterface {
 public:
  explicit TestRedirector(std::vector<SocketAddress> addresses) : addresses_(std::move(addresses)) {}
  bool ShouldRedirect(const SocketAddress&, SocketAddress* out) override {
    if (next_ >= addresses_.size()) return false;
    *out = addresses_[next_++];
    return true;
  }

 private:
  std::vector<SocketAddress> addresses_;
  size_t next_ = 0;
};

struct TurnFixture : public TurnPort::CallbacksForTest {
  sim::ByteNetwork net;
  sim::EmulatedSocketServer local1;
  sim::EmulatedSocketServer local2;
  sim::EmulatedSocketServer turn_int;
  sim::EmulatedSocketServer turn_ext;
  sim::EmulatedSocketServer turn_alt;
  sim::EmulatedSocketServer turn_tcp;
  // Wraps the server's accepted TLS connections (turn_port_tls_test.cc).
  sim::TurnServer::SslWrapper tls_wrapper;
  // Hosts a test adds (AddHost).
  std::vector<std::unique_ptr<sim::EmulatedSocketServer>> extra_hosts;
  TestAuth auth;
  sim::TurnServer server;
  Network network1;
  Network network2;
  Timestamp now = Timestamp::Seconds(10);

  std::unique_ptr<AsyncPacketSocket> shared_socket;
  std::unique_ptr<TurnPort> turn_port;
  std::unique_ptr<UDPPort> udp_port;
  bool turn_ready = false;
  bool turn_error = false;
  bool udp_ready = false;
  bool turn_unknown_address = false;
  bool turn_create_permission_success = false;
  bool turn_port_closed = false;
  bool turn_refresh_success = false;
  IceCandidateErrorEvent error_event;
  std::vector<std::vector<uint8_t>> turn_packets;
  std::vector<std::vector<uint8_t>> udp_packets;

  TurnFixture()
      : local1(net, net.AddHost(kLocalIp1, sim::ByteNetwork::kPublicRealm, Link(5), Link(5))),
        local2(net, net.AddHost(kLocalIp2, sim::ByteNetwork::kPublicRealm, Link(5), Link(5))),
        turn_int(net, net.AddHost(kTurnIntIp, sim::ByteNetwork::kPublicRealm, Link(15), Link(15))),
        turn_ext(net, net.AddHost(kTurnExtIp, sim::ByteNetwork::kPublicRealm, Link(15), Link(15))),
        turn_alt(net, net.AddHost(kTurnAltIp, sim::ByteNetwork::kPublicRealm, Link(15), Link(15))),
        turn_tcp(net, net.AddHost(kTurnTcpIp, sim::ByteNetwork::kPublicRealm, Link(15), Link(15))),
        network1("eth0", IpAddress(kLocalIp1), 24),
        network2("eth1", IpAddress(kLocalIp2), 24) {
    InstallTurnTestCrypto();
    network1.AddIp(IpAddress(kLocalIp1));
    network2.AddIp(IpAddress(kLocalIp2));
    server.set_realm(kTestRealm);
    server.set_auth_hook(&auth);
    server.AddInternalSocket(turn_int.CreateUdpSocket(kTurnUdpIntAddr, 0, 0), PROTO_UDP);
    server.SetExternalSocketFactory(&turn_ext, kTurnUdpExtAddr);
  }
  ~TurnFixture() override {
    turn_port.reset();
    udp_port.reset();
  }

  void OnTurnCreatePermissionResult(int code) override { turn_create_permission_success = (code == 0); }
  void OnTurnRefreshResult(int code) override { turn_refresh_success = (code == 0); }
  void OnTurnPortClosed() override { turn_port_closed = true; }

  RelayServerConfig config;
  ProtocolAddress server_address{kTurnUdpIntAddr, PROTO_UDP};
  CreateRelayPortArgs Args(std::string_view username, std::string_view password, const ProtocolAddress& addr,
                           Network* network = nullptr) {
    server_address = addr;
    config = RelayServerConfig(addr.address, username, password, addr.proto);
    CreateRelayPortArgs args;
    args.port.network = network ? network : &network1;
    args.port.socket_factory = &local1;
    args.port.ice_tiebreaker = 1;
    args.server_address = &server_address;
    args.config = &config;
    args.username = "lfrag";
    args.password = "lpasswordlpasswordlpassw";
    return args;
  }
  // A public host at `ip`, 15 ms one way, as the server's.
  sim::EmulatedSocketServer& AddHost(uint32_t ip) {
    extra_hosts.push_back(std::make_unique<sim::EmulatedSocketServer>(
        net, net.AddHost(ip, sim::ByteNetwork::kPublicRealm, Link(15), Link(15))));
    return *extra_hosts.back();
  }
  // TestTurnServer::AddInternalSocket for TCP and TLS: a listening stream
  // socket on `addr` of `host`; TLS connections are wrapped by `ssl`.
  void AddTcpServerSocket(sim::EmulatedSocketServer& host, const SocketAddress& addr, ProtocolType proto,
                          sim::TurnServer::SslWrapper ssl = nullptr) {
    std::unique_ptr<Socket> socket = host.CreateStreamSocket(addr.family());
    SM_ASSERT_EQ(socket->Bind(addr), 0);
    SM_ASSERT_EQ(socket->Listen(5), 0);
    server.AddInternalServerSocket(std::move(socket), proto, std::move(ssl));
  }

  void Subscribe() {
    turn_port->SetIceRole(ICEROLE_CONTROLLING);
    turn_port->SetCallbacksForTest(this);
    turn_port->SubscribePortComplete(nullptr, [this](Port*) { turn_ready = true; });
    turn_port->SubscribePortError(nullptr, [this](Port*) { turn_error = true; });
    turn_port->SubscribeCandidateError(nullptr, [this](Port*, const IceCandidateErrorEvent& e) { error_event = e; });
    turn_port->SubscribeUnknownAddress(
        nullptr, [this](Timestamp, Port*, const SocketAddress&, ProtocolType, IceMessage*, const std::string&, bool) {
          turn_unknown_address = true;
        });
  }
  void CreateTurnPort(std::string_view username, std::string_view password, const ProtocolAddress& addr) {
    turn_port.reset();
    turn_port = TurnPort::Create(now, Args(username, password, addr), 0, 0);
    if (!turn_port) return;
    Subscribe();
    // The test server's certificate is self-signed (libwebrtc's test does
    // the same).
    if (addr.proto == PROTO_TLS) turn_port->SetTlsCertPolicy(TlsCertPolicy::TLS_CERT_POLICY_INSECURE_NO_CHECK);
  }
  void CreateSharedTurnPort(std::string_view username, std::string_view password, const ProtocolAddress& addr) {
    turn_port.reset();
    if (!shared_socket) {
      shared_socket = local1.CreateUdpSocket(SocketAddress(kLocalIp1, 0), 0, 0);
      shared_socket->RegisterReceivedPacketCallback([this](AsyncPacketSocket* s, const ReceivedPacket& p) {
        if (turn_port) turn_port->HandleIncomingPacket(s, p);
      });
    }
    turn_port = TurnPort::Create(now, Args(username, password, addr), shared_socket.get());
    Subscribe();
  }
  void CreateUdpPort() {
    Port::Parameters args;
    args.network = &network2;
    args.socket_factory = &local2;
    args.ice_username_fragment = "rfrag";
    args.ice_password = "rpasswordrpasswordrpassw";
    args.ice_tiebreaker = 2;
    udp_port = UDPPort::Create(now, args, 0, 0, false, std::nullopt);
    udp_port->SetIceRole(ICEROLE_CONTROLLED);
    udp_port->SubscribePortComplete(nullptr, [this](Port*) { udp_ready = true; });
  }

  Timestamp Next() const {
    Timestamp t = std::min(server.NextProcessTime(), net.NextEvent());
    if (turn_port) t = std::min(t, turn_port->NextProcessTime());
    if (udp_port) t = std::min(t, udp_port->NextProcessTime());
    return t;
  }
  void Step(Timestamp t) {
    now = std::max(now, t);
    if (turn_port) turn_port->Process(now);
    if (udp_port) udp_port->Process(now);
    server.Process(now);
    for (sim::EmulatedSocketServer* h : {&local1, &local2, &turn_int, &turn_ext, &turn_alt, &turn_tcp}) h->Deliver(now);
    for (auto& h : extra_hosts) h->Deliver(now);
  }
  // Runs everything until `stop()` holds or `timeout` passes.
  template <class F>
  bool WaitUntil(F stop, TimeDelta timeout) {
    const Timestamp until = now + timeout;
    for (;;) {
      if (stop()) return true;
      const Timestamp t = Next();
      if (t > until) {
        Step(until);
        return stop();
      }
      Step(t);
    }
  }
  void AdvanceTime(TimeDelta d) {
    WaitUntil([] { return false; }, d);
  }

  void PrepareTurnAndUdpPorts(ProtocolType proto = PROTO_UDP) {
    if (!turn_port) CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnUdpIntAddr, PROTO_UDP));
    turn_port->PrepareAddress(now);
    SM_ASSERT_TRUE(WaitUntil([&] { return turn_ready; }, TimeToGetTurnCandidate(proto)));
    CreateUdpPort();
    udp_port->PrepareAddress(now);
    SM_ASSERT_TRUE(WaitUntil([&] { return udp_ready; }, kSimulatedRtt));
  }

  static bool CheckConnectionFailedAndPruned(const Connection* conn) {
    return conn && !conn->active() && conn->state() == IceCandidatePairState::FAILED;
  }
  bool CheckAllConnectionsFailedAndPruned() const {
    for (const auto& kv : turn_port->connections()) {
      if (!CheckConnectionFailedAndPruned(kv.second.get())) return false;
    }
    return true;
  }
};

}  // namespace
