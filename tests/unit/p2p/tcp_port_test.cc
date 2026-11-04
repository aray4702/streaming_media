// ICE-TCP ports (issue #1151): the cases of libwebrtc M157's p2p/base/tcp_port_unittest.cc
// (compare/webrtc/reference/p2p/base/) on the emulated network's TCP (sim::EmulatedSocketServer), with the library's
// time model in place of the simulated clock, plus the candidates TCPPort gathers, the remote candidates it makes
// connections to, and the reconnection timeout.
//
// libwebrtc's VirtualSocketServer::SetAlternativeLocalAddress (a socket bound to one address ends up on another) is a
// socket factory here that reports the alternative address for the sockets it makes, on the same emulated hosts.
// TCPPortNotDiscardedIfBoundToTemporaryIP is not ported: it needs IPv6 and InterfaceAddress's temporary flag, which
// the emulated network (IPv4) and sm::p2p::Network (plain addresses) don't have. NotifyClosedForTest is not in the
// library's socket interface, so ReconnectTimeoutAfterShutdownDoesNotDestroy closes the socket from the far end.
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/async_packet_socket.h"
#include "sm/p2p/candidate.h"
#include "sm/p2p/connection.h"
#include "sm/p2p/network.h"
#include "sm/p2p/p2p_constants.h"
#include "sm/p2p/port.h"
#include "sm/p2p/tcp_port.h"
#include "sm/sim/emulated_sockets.h"
#include "sm_test.h"
#include "test_crypto.h"

using namespace sm;
using namespace sm::p2p;

namespace {

IpAddress Ip(std::string_view s) { return *IpAddress::FromString(s); }

const IpAddress kLocalIp = Ip("11.11.11.11");
const IpAddress kAlternateLocalIp = Ip("1.2.3.4");
const IpAddress kRemoteIp = Ip("22.22.22.22");
const IpAddress kLoopbackIp = Ip("127.0.0.1");
constexpr uint64_t kTiebreakerDefault = 44444;
// tcp_port_unittest.cc's kTimeout.
constexpr TimeDelta kTimeout = TimeDelta::Millis(1000);

// A socket that reports another local address (VirtualSocketServer::SetAlternativeLocalAddress), passing everything
// else through.
class AltAddressSocket final : public AsyncPacketSocket {
 public:
  AltAddressSocket(std::unique_ptr<AsyncPacketSocket> inner, IpAddress alt) : inner_(std::move(inner)), alt_(alt) {
    inner_->RegisterReceivedPacketCallback(
        [this](AsyncPacketSocket*, const ReceivedPacket& p) { NotifyPacketReceived(p); });
    inner_->SubscribeSentPacket(this, [this](AsyncPacketSocket*, const SentPacketInfo& i) { NotifySentPacket(i); });
    inner_->SubscribeCloseEvent(this, [this](AsyncPacketSocket*, int e) { NotifyClosed(e); });
    inner_->SubscribeReadyToSend(this, [this](AsyncPacketSocket*) { NotifyReadyToSend(); });
    inner_->SubscribeConnect(this, [this](AsyncPacketSocket*, Timestamp t) { NotifyConnect(t); });
  }
  SocketAddress GetLocalAddress() const override { return SocketAddress(alt_, inner_->GetLocalAddress().port()); }
  SocketAddress GetRemoteAddress() const override { return inner_->GetRemoteAddress(); }
  int Send(Timestamp now, std::span<const uint8_t> data, const PacketOptions& options) override {
    return inner_->Send(now, data, options);
  }
  int SendTo(Timestamp now, std::span<const uint8_t> data, const SocketAddress& addr,
             const PacketOptions& options) override {
    return inner_->SendTo(now, data, addr, options);
  }
  int Close() override { return inner_->Close(); }
  State GetState() const override { return inner_->GetState(); }
  int GetOption(SocketOption opt, int* value) override { return inner_->GetOption(opt, value); }
  int SetOption(SocketOption opt, int value) override { return inner_->SetOption(opt, value); }
  int GetError() const override { return inner_->GetError(); }
  void SetError(int error) override { inner_->SetError(error); }

 private:
  std::unique_ptr<AsyncPacketSocket> inner_;
  IpAddress alt_;
};

class AltAddressListenSocket final : public AsyncListenSocket {
 public:
  AltAddressListenSocket(std::unique_ptr<AsyncListenSocket> inner, IpAddress alt)
      : inner_(std::move(inner)), alt_(alt) {
    inner_->SubscribeNewConnection(this, [this](AsyncListenSocket*, std::unique_ptr<AsyncPacketSocket>& s) {
      std::unique_ptr<AsyncPacketSocket> wrapped = std::make_unique<AltAddressSocket>(std::move(s), alt_);
      NotifyNewConnection(wrapped);
    });
  }
  State GetState() const override { return inner_->GetState(); }
  SocketAddress GetLocalAddress() const override { return SocketAddress(alt_, inner_->GetLocalAddress().port()); }

 private:
  std::unique_ptr<AsyncListenSocket> inner_;
  IpAddress alt_;
};

// The sockets of `inner`, bound to `alt` as far as they tell.
class AltAddressFactory final : public PacketSocketFactory {
 public:
  AltAddressFactory(PacketSocketFactory* inner, IpAddress alt) : inner_(inner), alt_(alt) {}
  std::unique_ptr<AsyncPacketSocket> CreateUdpSocket(const SocketAddress& a, uint16_t min_port,
                                                     uint16_t max_port) override {
    return inner_->CreateUdpSocket(a, min_port, max_port);
  }
  std::unique_ptr<AsyncListenSocket> CreateServerTcpSocket(const SocketAddress& a, uint16_t min_port, uint16_t max_port,
                                                           int opts) override {
    std::unique_ptr<AsyncListenSocket> s = inner_->CreateServerTcpSocket(a, min_port, max_port, opts);
    if (!s) return nullptr;
    return std::make_unique<AltAddressListenSocket>(std::move(s), alt_);
  }
  std::unique_ptr<AsyncPacketSocket> CreateClientTcpSocket(Timestamp now, const SocketAddress& local,
                                                           const SocketAddress& remote,
                                                           const PacketSocketTcpOptions& options) override {
    std::unique_ptr<AsyncPacketSocket> s = inner_->CreateClientTcpSocket(now, local, remote, options);
    if (!s) return nullptr;
    return std::make_unique<AltAddressSocket>(std::move(s), alt_);
  }

 private:
  PacketSocketFactory* inner_;
  IpAddress alt_;
};

// No sockets at all.
class NullFactory final : public PacketSocketFactory {
 public:
  std::unique_ptr<AsyncPacketSocket> CreateUdpSocket(const SocketAddress&, uint16_t, uint16_t) override {
    return nullptr;
  }
};

// tcp_port_unittest.cc's ConnectionObserver.
class ConnectionObserver {
 public:
  explicit ConnectionObserver(Connection* conn) : conn_(conn) {
    conn->SubscribeDestroyed(this, [this](Connection*) { connection_destroyed_ = true; });
  }
  ~ConnectionObserver() {
    if (!connection_destroyed_) conn_->UnsubscribeDestroyed(this);
  }
  bool connection_destroyed() const { return connection_destroyed_; }

 private:
  Connection* const conn_;
  bool connection_destroyed_ = false;
};

// tcp_port_unittest.cc's SentPacketCounter.
class SentPacketCounter {
 public:
  explicit SentPacketCounter(TCPPort* p) {
    p->SubscribeSentPacket(this, [this](const SentPacketInfo&) { ++sent_packets_; });
  }
  int sent_packets() const { return sent_packets_; }

 private:
  int sent_packets_ = 0;
};

// TCPPortTest: two hosts on the emulated network (kLocalIp and kRemoteIp), ports on networks made for their addresses.
struct TcpPortFixture {
  sim::ByteNetwork net{1};
  sim::EmulatedSocketServer local_host{net, net.AddHost(kLocalIp.v4())};
  sim::EmulatedSocketServer remote_host{net, net.AddHost(kRemoteIp.v4())};
  // A list, so that pointers to its networks stay valid as it grows.
  std::list<Network> networks;
  std::vector<TCPPort*> ports;  // Processed by Run; a test removes a port it deletes (Forget).
  // TCPPortTest's simulated clock starts at 1000 ms; the library's "never" is at or before 0.
  Timestamp now = Timestamp::Millis(1000);
  std::string username = "TESTUFRAG";
  std::string password = "TESTICEPWD0000000000000";

  TcpPortFixture() { testing::InstallTestCrypto(); }

  Network* MakeNetwork(const IpAddress& ip) {
    networks.emplace_back("unittest", ip, 32);
    networks.back().AddIp(ip);
    return &networks.back();
  }

  std::unique_ptr<TCPPort> CreateTCPPort(PacketSocketFactory* factory, const Network* network,
                                         bool allow_listen = true, uint16_t port_number = 0) {
    Port::Parameters args;
    args.network = network;
    args.socket_factory = factory;
    args.ice_username_fragment = username;
    args.ice_password = password;
    args.ice_tiebreaker = kTiebreakerDefault;
    std::unique_ptr<TCPPort> port = TCPPort::Create(now, args, port_number, port_number, allow_listen);
    ports.push_back(port.get());
    return port;
  }
  std::unique_ptr<TCPPort> CreateLocalPort(bool allow_listen = true) {
    return CreateTCPPort(&local_host, MakeNetwork(kLocalIp), allow_listen);
  }
  std::unique_ptr<TCPPort> CreateRemotePort(bool allow_listen = true, uint16_t port_number = 0) {
    return CreateTCPPort(&remote_host, MakeNetwork(kRemoteIp), allow_listen, port_number);
  }
  void Forget(TCPPort* port) { std::erase(ports, port); }

  Timestamp NextEvent() const {
    Timestamp t = std::min(local_host.NextEvent(), remote_host.NextEvent());
    for (const TCPPort* p : ports) t = std::min(t, p->NextProcessTime());
    return t;
  }
  void Step(Timestamp t) {
    now = std::max(now, t);
    for (TCPPort* p : ports) p->Process(now);
    local_host.Deliver(now);
    remote_host.Deliver(now);
  }
  // Runs until `stop` holds, at most `limit` (WaitUntil with kTimeout).
  template <class F>
  bool RunUntil(F stop, TimeDelta limit = kTimeout) {
    const Timestamp until = now + limit;
    for (;;) {
      if (stop()) return true;
      const Timestamp t = NextEvent();
      if (t > until) {
        now = until;
        Step(now);
        return stop();
      }
      Step(t);
    }
  }
  // Advances the clock by `d`, running everything due (AdvanceTime).
  void Advance(TimeDelta d) { RunUntil([] { return false; }, d); }
};

// The client candidate a server port makes its connection to: the address of the client's outgoing socket.
Candidate ClientCandidate(TCPPort* client, Connection* client_conn) {
  Candidate c = client->Candidates()[0];
  c.set_address(static_cast<TCPConnection*>(client_conn)->socket()->GetLocalAddress());
  return c;
}

}  // namespace

// A passive host candidate at the listen socket's address, with libwebrtc's host TCP type preference (90).
SM_TEST(P2pTcpPort, PassiveCandidateOnListenSocket) {
  TcpPortFixture f;
  auto port = f.CreateLocalPort();
  bool complete = false;
  port->SubscribePortComplete(&f, [&](Port*) { complete = true; });
  port->PrepareAddress(f.now);
  SM_ASSERT_EQ(port->Candidates().size(), size_t{1});
  const Candidate& c = port->Candidates()[0];
  SM_EXPECT_TRUE(complete);
  SM_EXPECT_EQ(c.protocol(), std::string(TCP_PROTOCOL_NAME));
  SM_EXPECT_EQ(c.tcptype(), std::string(TCPTYPE_PASSIVE_STR));
  SM_EXPECT_TRUE(c.is_local());
  SM_EXPECT_TRUE(c.address() == port->ListenAddressForTesting());
  SM_EXPECT_TRUE(c.address().ipaddr() == kLocalIp);
  SM_EXPECT_NE(c.address().port(), 0);
  SM_EXPECT_EQ(c.priority() >> 24, uint32_t{ICE_TYPE_PREFERENCE_HOST_TCP});
  SM_EXPECT_EQ(port->GetProtocol(), PROTO_TCP);
  SM_EXPECT_FALSE(port->SharedSocket());
  int nodelay = 0;
  SM_EXPECT_EQ(port->GetOption(SocketOption::kNoDelay, &nodelay), 0);
  SM_EXPECT_EQ(nodelay, 1);
}

// Without a listen socket (allow_tcp_listen off, or the listen fails): an active candidate at the best IP and the
// discard port (RFC 6544 4.5), based on port 0.
SM_TEST(P2pTcpPort, ActiveCandidateWithoutListenSocket) {
  for (bool listen_fails : {false, true}) {
    TcpPortFixture f;
    NullFactory null_factory;
    auto port = listen_fails ? f.CreateTCPPort(&null_factory, f.MakeNetwork(kLocalIp))
                             : f.CreateLocalPort(/*allow_listen=*/false);
    port->PrepareAddress(f.now);
    SM_ASSERT_EQ(port->Candidates().size(), size_t{1});
    const Candidate& c = port->Candidates()[0];
    SM_EXPECT_EQ(c.tcptype(), std::string(TCPTYPE_ACTIVE_STR));
    SM_EXPECT_TRUE(c.address() == SocketAddress(kLocalIp, DISCARD_PORT));
    SM_EXPECT_TRUE(port->ListenAddressForTesting().IsNil());
  }
}

// CreateConnection: TCP and SSLTCP only; no connection to an active candidate (unless peer-reflexive), to a
// candidate without tcptype and port, from another port, or to SSLTCP from this port; otherwise an outgoing one.
SM_TEST(P2pTcpPort, CreateConnectionRules) {
  TcpPortFixture f;
  auto local = f.CreateLocalPort();
  auto remote = f.CreateRemotePort();
  local->PrepareAddress(f.now);
  remote->PrepareAddress(f.now);
  const Candidate passive = remote->Candidates()[0];

  Candidate udp = passive;
  udp.set_protocol(UDP_PROTOCOL_NAME);
  SM_EXPECT_TRUE(local->CreateConnection(f.now, udp, Port::ORIGIN_MESSAGE) == nullptr);
  Candidate active = passive;
  active.set_tcptype(TCPTYPE_ACTIVE_STR);
  SM_EXPECT_TRUE(local->CreateConnection(f.now, active, Port::ORIGIN_MESSAGE) == nullptr);
  Candidate no_type = passive;
  no_type.set_tcptype("");
  no_type.set_address(SocketAddress(kRemoteIp, 0));
  SM_EXPECT_TRUE(local->CreateConnection(f.now, no_type, Port::ORIGIN_MESSAGE) == nullptr);
  SM_EXPECT_TRUE(local->CreateConnection(f.now, passive, Port::ORIGIN_OTHER_PORT) == nullptr);
  Candidate ssltcp = passive;
  ssltcp.set_protocol(SSLTCP_PROTOCOL_NAME);
  SM_EXPECT_TRUE(local->CreateConnection(f.now, ssltcp, Port::ORIGIN_THIS_PORT) == nullptr);
  SM_EXPECT_TRUE(local->connections().empty());

  Connection* conn = local->CreateConnection(f.now, passive, Port::ORIGIN_MESSAGE);
  SM_ASSERT_TRUE(conn != nullptr);
  auto* tcp = static_cast<TCPConnection*>(conn);
  SM_EXPECT_TRUE(tcp->outgoing());
  SM_EXPECT_FALSE(conn->connected());  // Until the connect completes.
  SM_ASSERT_TRUE(f.RunUntil([&] { return conn->connected(); }));
  // The listener accepted it and keeps it until a connection takes it.
  SM_EXPECT_EQ(remote->incoming_count_for_testing(), size_t{1});
  Candidate prflx_active = local->Candidates()[0];
  prflx_active.set_type(IceCandidateType::kPrflx);
  prflx_active.set_address(tcp->socket()->GetLocalAddress());
  Connection* incoming = remote->CreateConnection(f.now, prflx_active, Port::ORIGIN_THIS_PORT);
  SM_ASSERT_TRUE(incoming != nullptr);
  SM_EXPECT_FALSE(static_cast<TCPConnection*>(incoming)->outgoing());
  SM_EXPECT_TRUE(incoming->connected());
  SM_EXPECT_EQ(remote->incoming_count_for_testing(), size_t{0});
}

// TestTCPPortWithLocalhostAddress: a socket that ends up bound to localhost is still used.
SM_TEST(P2pTcpPort, TCPPortWithLocalhostAddress) {
  TcpPortFixture f;
  AltAddressFactory localhost(&f.local_host, kLoopbackIp);
  auto local_port = f.CreateTCPPort(&localhost, f.MakeNetwork(kLocalIp));
  auto remote_port = f.CreateRemotePort();
  local_port->PrepareAddress(f.now);
  remote_port->PrepareAddress(f.now);
  Connection* conn = local_port->CreateConnection(f.now, remote_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  SM_ASSERT_TRUE(conn != nullptr);
  SM_EXPECT_TRUE(f.RunUntil([&] { return conn->connected(); }));
  // Verify that the socket actually used localhost, otherwise this test isn't doing what it meant to.
  SM_ASSERT_TRUE(local_port->Candidates()[0].address().ipaddr() == kLoopbackIp);
}

// TCPPortDiscardedIfBoundAddressDoesNotMatchNetwork: a connection whose socket is bound to an address of no
// interface of its network is destroyed when the connect completes.
SM_TEST(P2pTcpPort, TCPPortDiscardedIfBoundAddressDoesNotMatchNetwork) {
  TcpPortFixture f;
  AltAddressFactory alternate(&f.local_host, kAlternateLocalIp);
  auto local_port = f.CreateTCPPort(&alternate, f.MakeNetwork(kLocalIp));
  auto remote_port = f.CreateRemotePort();
  local_port->PrepareAddress(f.now);
  remote_port->PrepareAddress(f.now);
  Connection* conn = local_port->CreateConnection(f.now, remote_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  SM_ASSERT_TRUE(conn != nullptr);
  ConnectionObserver observer(conn);
  SM_EXPECT_TRUE(f.RunUntil([&] { return observer.connection_destroyed(); }));
  SM_EXPECT_TRUE(local_port->connections().empty());
}

// TCPPortNotDiscardedIfNotBoundToBestIP: bound to one of the network's addresses that is not its best IP is fine.
SM_TEST(P2pTcpPort, TCPPortNotDiscardedIfNotBoundToBestIP) {
  TcpPortFixture f;
  AltAddressFactory alternate(&f.local_host, kAlternateLocalIp);
  Network* network = f.MakeNetwork(kLocalIp);
  network->AddIp(kAlternateLocalIp);
  SM_ASSERT_TRUE(network->GetBestIP() == kLocalIp);
  auto local_port = f.CreateTCPPort(&alternate, network);
  auto remote_port = f.CreateRemotePort();
  local_port->PrepareAddress(f.now);
  remote_port->PrepareAddress(f.now);
  Connection* conn = local_port->CreateConnection(f.now, remote_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  SM_ASSERT_TRUE(conn != nullptr);
  SM_EXPECT_TRUE(f.RunUntil([&] { return conn->connected(); }));
  SM_ASSERT_TRUE(local_port->Candidates()[0].address().ipaddr() == kAlternateLocalIp);
}

// SignalSentPacket: the sent-packet signal fires for each packet a connection sends, on the client (outgoing) and
// the server (accepted) socket.
SM_TEST(P2pTcpPort, SignalSentPacket) {
  TcpPortFixture f;
  auto client = f.CreateLocalPort();
  auto server = f.CreateRemotePort();
  client->SetIceRole(ICEROLE_CONTROLLING);
  server->SetIceRole(ICEROLE_CONTROLLED);
  client->PrepareAddress(f.now);
  server->PrepareAddress(f.now);

  Connection* client_conn = client->CreateConnection(f.now, server->Candidates()[0], Port::ORIGIN_MESSAGE);
  SM_ASSERT_TRUE(client_conn != nullptr);
  SM_ASSERT_TRUE(f.RunUntil([&] { return client_conn->connected(); }));

  // Need to get the port of the actual outgoing socket, not the server socket.
  Connection* server_conn =
      server->CreateConnection(f.now, ClientCandidate(client.get(), client_conn), Port::ORIGIN_THIS_PORT);
  SM_ASSERT_TRUE(server_conn != nullptr);
  SM_ASSERT_TRUE(f.RunUntil([&] { return server_conn->connected(); }));

  client_conn->Ping(f.now);
  server_conn->Ping(f.now);
  SM_ASSERT_TRUE(f.RunUntil([&] { return client_conn->writable(); }));
  SM_ASSERT_TRUE(f.RunUntil([&] { return server_conn->writable(); }));

  SentPacketCounter client_counter(client.get());
  SentPacketCounter server_counter(server.get());
  static constexpr uint8_t kData[] = {'h', 'e', 'l', 'l', 'o', '\0'};
  for (int i = 0; i < 10; ++i) {
    client_conn->Send(f.now, kData, PacketOptions());
    server_conn->Send(f.now, kData, PacketOptions());
  }
  SM_EXPECT_TRUE(f.RunUntil([&] { return client_counter.sent_packets() == 10; }));
  SM_EXPECT_TRUE(f.RunUntil([&] { return server_counter.sent_packets() == 10; }));
}

// SignalSentPacketAfterReconnect: after the server goes away and comes back on the same port, a send reconnects but
// is dropped, data is refused until the STUN binding on the new socket completes, and then the signal fires again.
SM_TEST(P2pTcpPort, SignalSentPacketAfterReconnect) {
  TcpPortFixture f;
  auto client = f.CreateLocalPort(/*allow_listen=*/false);
  constexpr uint16_t kServerPort = 123;
  auto server = f.CreateRemotePort(/*allow_listen=*/true, kServerPort);
  client->SetIceRole(ICEROLE_CONTROLLING);
  server->SetIceRole(ICEROLE_CONTROLLED);
  client->PrepareAddress(f.now);
  server->PrepareAddress(f.now);

  Connection* client_conn = client->CreateConnection(f.now, server->Candidates()[0], Port::ORIGIN_MESSAGE);
  SM_ASSERT_TRUE(client_conn != nullptr);
  SM_ASSERT_TRUE(f.RunUntil([&] { return client_conn->connected(); }));

  // Need to get the port of the actual outgoing socket.
  Candidate client_candidate = ClientCandidate(client.get(), client_conn);
  client_candidate.set_tcptype("");
  Connection* server_conn = server->CreateConnection(f.now, client_candidate, Port::ORIGIN_THIS_PORT);
  SM_ASSERT_TRUE(server_conn != nullptr);
  SM_ASSERT_TRUE(f.RunUntil([&] { return server_conn->connected(); }));
  SM_EXPECT_FALSE(client_conn->writable());
  client_conn->Ping(f.now);
  SM_ASSERT_TRUE(f.RunUntil([&] { return client_conn->writable(); }));

  SentPacketCounter client_counter(client.get());
  static constexpr uint8_t kData[] = {'h', 'e', 'l', 'l', 'o', '\0'};
  int result = client_conn->Send(f.now, kData, PacketOptions());
  SM_EXPECT_EQ(result, 6);

  // Deleting the server port should break the current connection.
  f.Forget(server.get());
  server = nullptr;
  server_conn = nullptr;
  SM_ASSERT_TRUE(f.RunUntil([&] { return !client_conn->connected(); }));

  // Recreate the server port with the same port number.
  server = f.CreateRemotePort(/*allow_listen=*/true, kServerPort);
  server->SetIceRole(ICEROLE_CONTROLLED);
  server->PrepareAddress(f.now);

  // Sending a packet from the client will trigger a reconnect attempt but the packet will be discarded.
  result = client_conn->Send(f.now, kData, PacketOptions());
  SM_EXPECT_EQ(result, -1);  // SOCKET_ERROR
  SM_EXPECT_EQ(client_conn->GetError(), kSocketEPipe);
  SM_ASSERT_TRUE(f.RunUntil([&] { return client_conn->connected(); }));
  // For unknown reasons, connection is still supposed to be writable....
  SM_EXPECT_TRUE(client_conn->writable());
  for (int i = 0; i < 10; ++i) {
    // All sent packets still fail to send.
    SM_EXPECT_EQ(client_conn->Send(f.now, kData, PacketOptions()), -1);
  }
  // And are not reported as sent.
  f.Advance(kTimeout);
  SM_EXPECT_EQ(client_counter.sent_packets(), 1);

  // Create the server connection again so server can reply to STUN pings. Client outgoing socket port will have
  // changed since the client create a new socket when it reconnect.
  client_candidate = ClientCandidate(client.get(), client_conn);
  client_candidate.set_tcptype("");
  server_conn = server->CreateConnection(f.now, client_candidate, Port::ORIGIN_THIS_PORT);
  SM_ASSERT_TRUE(server_conn != nullptr);
  SM_ASSERT_TRUE(f.RunUntil([&] { return server_conn->connected(); }));
  SM_EXPECT_EQ(client_counter.sent_packets(), 1);

  // Send Stun Binding request.
  client_conn->Ping(f.now);
  // The Stun Binding request is reported as sent.
  SM_EXPECT_TRUE(f.RunUntil([&] { return client_counter.sent_packets() == 2; }));
  // Wait a bit for the Stun response to be received.
  f.Advance(TimeDelta::Millis(100));

  // After the Stun Ping response has been received, packets can be sent again and SignalSentPacket should be
  // invoked.
  for (int i = 0; i < 5; ++i) SM_EXPECT_EQ(client_conn->Send(f.now, kData, PacketOptions()), 6);
  SM_EXPECT_TRUE(f.RunUntil([&] { return client_counter.sent_packets() == 2 + 5; }));
}

// The reconnection timeout: a connection whose socket closed and that does not get writable again is destroyed
// kConnectionWriteConnectTimeout (5 s) after the close, and not before.
SM_TEST(P2pTcpPort, ReconnectionTimeoutDestroysConnection) {
  TcpPortFixture f;
  auto client = f.CreateLocalPort(/*allow_listen=*/false);
  auto server = f.CreateRemotePort();
  client->SetIceRole(ICEROLE_CONTROLLING);
  server->SetIceRole(ICEROLE_CONTROLLED);
  client->PrepareAddress(f.now);
  server->PrepareAddress(f.now);
  auto* client_conn =
      static_cast<TCPConnection*>(client->CreateConnection(f.now, server->Candidates()[0], Port::ORIGIN_MESSAGE));
  SM_ASSERT_TRUE(client_conn != nullptr);
  SM_EXPECT_EQ(client_conn->reconnection_timeout().ms(), kConnectionWriteConnectTimeout.ms());
  SM_ASSERT_TRUE(f.RunUntil([&] { return client_conn->connected(); }));
  ConnectionObserver observer(client_conn);

  f.Forget(server.get());
  server = nullptr;  // The far end closes (FIN).
  SM_ASSERT_TRUE(f.RunUntil([&] { return !client_conn->connected(); }));
  SM_EXPECT_TRUE(client_conn->pretending_to_be_writable());
  const Timestamp closed_at = f.now;
  f.Advance(kConnectionWriteConnectTimeout - TimeDelta::Millis(1));
  SM_EXPECT_FALSE(observer.connection_destroyed());
  SM_EXPECT_TRUE(f.RunUntil([&] { return observer.connection_destroyed(); }, TimeDelta::Millis(10)));
  SM_EXPECT_EQ((f.now - closed_at).ms(), kConnectionWriteConnectTimeout.ms());
  SM_EXPECT_TRUE(client->connections().empty());
}

// ReconnectTimeoutAfterShutdownDoesNotDestroy: the reconnect timer of a connection that was destroyed
// asynchronously in the meantime does nothing (libwebrtc's SafeTask; here the connection is gone from the port).
SM_TEST(P2pTcpPort, ReconnectTimeoutAfterShutdownDoesNotDestroy) {
  TcpPortFixture f;
  auto client = f.CreateLocalPort(/*allow_listen=*/false);
  auto server = f.CreateRemotePort();
  client->SetIceRole(ICEROLE_CONTROLLING);
  server->SetIceRole(ICEROLE_CONTROLLED);
  client->PrepareAddress(f.now);
  server->PrepareAddress(f.now);
  auto* client_conn =
      static_cast<TCPConnection*>(client->CreateConnection(f.now, server->Candidates()[0], Port::ORIGIN_MESSAGE));
  SM_ASSERT_TRUE(client_conn != nullptr);
  SM_ASSERT_TRUE(f.RunUntil([&] { return client_conn->connected(); }));

  ConnectionObserver observer(client_conn);
  f.Forget(server.get());
  server = nullptr;
  SM_ASSERT_TRUE(f.RunUntil([&] { return !client_conn->connected(); }));

  // Runs the reconnect timer between Shutdown() and the deferred delete.
  client->DestroyConnectionAsync(f.now, client_conn);
  f.Advance(kConnectionWriteConnectTimeout);
  SM_EXPECT_TRUE(observer.connection_destroyed());
  SM_EXPECT_TRUE(client->connections().empty());
}

// A connect that fails before connecting (no one listening: a reset) destroys the connection at once, as it will
// never be pinged again; a client socket that can't be made fails and prunes the pair at the next Process.
SM_TEST(P2pTcpPort, ConnectFailureDestroysOrPrunes) {
  TcpPortFixture f;
  auto client = f.CreateLocalPort(/*allow_listen=*/false);
  auto server = f.CreateRemotePort();
  server->PrepareAddress(f.now);
  client->PrepareAddress(f.now);
  Candidate nobody = server->Candidates()[0];
  nobody.set_address(SocketAddress(kRemoteIp, static_cast<uint16_t>(nobody.address().port() + 1)));
  Connection* refused = client->CreateConnection(f.now, nobody, Port::ORIGIN_MESSAGE);
  SM_ASSERT_TRUE(refused != nullptr);
  ConnectionObserver observer(refused);
  SM_EXPECT_TRUE(f.RunUntil([&] { return observer.connection_destroyed(); }));

  NullFactory null_factory;
  auto no_sockets = f.CreateTCPPort(&null_factory, f.MakeNetwork(kLocalIp), /*allow_listen=*/false);
  no_sockets->PrepareAddress(f.now);
  Connection* failed = no_sockets->CreateConnection(f.now, server->Candidates()[0], Port::ORIGIN_MESSAGE);
  SM_ASSERT_TRUE(failed != nullptr);
  SM_EXPECT_TRUE(failed->state() == IceCandidatePairState::FAILED);
  SM_EXPECT_FALSE(failed->pruned());
  SM_EXPECT_EQ(failed->Send(f.now, std::span<const uint8_t>(), PacketOptions()), -1);
  SM_EXPECT_EQ(failed->GetError(), kSocketENotConn);
  SM_EXPECT_TRUE(no_sockets->NextProcessTime() <= f.now);
  no_sockets->Process(f.now);
  SM_EXPECT_TRUE(failed->pruned());
}
