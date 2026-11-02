// TURN over TCP and TLS (issue #1036, C4 part 4): the TCP and TLS cases of
// libwebrtc M157's p2p/base/turn_port_unittest.cc, written once for both
// protocols. turn_port_tcp_test.cc runs them over TCP and
// turn_port_tls_test.cc over TLS, whose fixture sets the server's TLS
// wrapper (an adapter in server role with a self-signed certificate).
#pragma once

#include <string>
#include <vector>

#include "turn_fixture.h"

namespace turn_stream {

inline const char* Transport(ProtocolType proto) { return proto == PROTO_TLS ? "tls" : "tcp"; }

// TestTurnServer::AddInternalSocket(addr, proto) for TCP or TLS.
inline void AddServer(TurnFixture& f, sim::EmulatedSocketServer& host, const SocketAddress& addr,
                      ProtocolType proto) {
  f.AddTcpServerSocket(host, addr, proto, proto == PROTO_TLS ? f.tls_wrapper : nullptr);
}

// TestTurnTcpAllocate, TestReconstructedServerUrlForTcp/Tls: the Allocate
// waits for the connection; the relay candidate is udp relayed over the
// first hop's protocol.
inline void Allocate(TurnFixture& f, ProtocolType proto) {
  AddServer(f, f.turn_tcp, kTurnTcpIntAddr, proto);
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnTcpIntAddr, proto));
  SM_ASSERT_TRUE(f.turn_port != nullptr);
  SM_EXPECT_TRUE(f.turn_port->GetProtocol() == proto);
  SM_EXPECT_EQ(f.turn_port->SetOption(SocketOption::kSndBuf, 10 * 1024), 0);
  f.turn_port->PrepareAddress(f.now);
  // Connecting: nothing sent before the connection is up.
  SM_ASSERT_TRUE(f.turn_port->socket() != nullptr);
  SM_EXPECT_TRUE(f.turn_port->state() == TurnPort::STATE_CONNECTING);
  SM_EXPECT_FALSE(f.turn_port->HasRequests());
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_ready; }, TimeToGetTurnCandidate(proto)));
  SM_ASSERT_EQ(f.turn_port->Candidates().size(), size_t{1});
  const Candidate& c = f.turn_port->Candidates()[0];
  SM_EXPECT_TRUE(c.is_relay());
  SM_EXPECT_TRUE(c.address().ipaddr() == IpAddress(kTurnExtIp));
  SM_EXPECT_NE(c.address().port(), 0);
  SM_EXPECT_EQ(c.protocol(), std::string("udp"));
  SM_EXPECT_EQ(c.relay_protocol(), std::string(Transport(proto)));
  SM_EXPECT_EQ(c.url(), std::string(proto == PROTO_TLS ? "turns:99.99.99.4:3478?transport=tcp"
                                                       : "turn:99.99.99.4:3478?transport=tcp"));
  SM_EXPECT_EQ(c.priority() >> 24, static_cast<uint32_t>(proto == PROTO_TLS ? ICE_TYPE_PREFERENCE_RELAY_TLS
                                                                            : ICE_TYPE_PREFERENCE_RELAY_TCP));
  SM_EXPECT_TRUE(c.related_address() == f.turn_port->GetLocalAddress());
  SM_EXPECT_EQ(f.server.allocation_count(), size_t{1});
  // 401, then the allocation, over the one connection.
  SM_ASSERT_EQ(f.server.received_message_types().size(), size_t{2});
  SM_EXPECT_EQ(f.server.received_message_types()[1], STUN_ALLOCATE_REQUEST);
}

// TestTurnTcpAllocateMismatch: a new connection from the address of an
// allocation the server still holds gets 437 and moves to a new socket.
inline void AllocateMismatch(TurnFixture& f, ProtocolType proto) {
  AddServer(f, f.turn_tcp, kTurnTcpIntAddr, proto);
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnTcpIntAddr, proto));
  f.turn_port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_ready; }, TimeToGetTurnCandidate(proto)));
  const SocketAddress first_addr = f.turn_port->socket()->GetLocalAddress();
  // Suppresses the release of the allocation.
  f.turn_port->OnSocketClose(f.turn_port->socket(), 0);
  f.local1.SetNextPortForTesting(first_addr.port());
  f.turn_ready = false;
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnTcpIntAddr, proto));
  f.turn_port->PrepareAddress(f.now);
  SM_EXPECT_TRUE(f.turn_port->socket()->GetLocalAddress() == first_addr);
  // Two more round trips and a new connection for the mismatch.
  SM_ASSERT_TRUE(
      f.WaitUntil([&] { return f.turn_ready; }, TimeToGetTurnCandidate(proto) * 2 + kSimulatedRtt * 2));
  SM_EXPECT_FALSE(f.turn_port->socket()->GetLocalAddress() == first_addr);
  int mismatches = 0;
  for (uint16_t t : f.server.received_message_types()) mismatches += t == STUN_ALLOCATE_REQUEST;
  SM_EXPECT_GE(mismatches, 5);  // 401 + success, then 401, 437, 401 + success on the new socket.
}

// TestPrivateIpTurnCandidateErrorAddressScrubbedTcp/Tls and
// TestPublicIpTurnCandidateErrorAddressNotScrubbedTcp/Tls: a connection that
// fails before the allocation reports the local address only for a public
// server.
inline void ErrorAddressScrubbing(TurnFixture& f, ProtocolType proto, bool private_server) {
  const SocketAddress private_addr(sim::SocketAddress::Ip(192, 168, 1, 1), 3478);
  const SocketAddress server = private_server ? private_addr : kTurnTcpIntAddr;
  AddServer(f, private_server ? f.AddHost(private_addr.ip()) : f.turn_tcp, server, proto);
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(server, proto));
  f.turn_port->PrepareAddress(f.now);
  SM_ASSERT_FALSE(f.turn_ready);
  SM_ASSERT_TRUE(f.turn_port->socket() != nullptr);
  f.turn_port->OnSocketClose(f.turn_port->socket(), 1);
  SM_EXPECT_EQ(f.error_event.error_code, static_cast<int>(STUN_ERROR_SERVER_NOT_REACHABLE));
  SM_EXPECT_EQ(f.error_event.error_text, std::string("Failed to establish connection"));
  if (private_server) {
    SM_EXPECT_EQ(f.error_event.address, std::string());
    SM_EXPECT_EQ(f.error_event.port, 0);
  } else {
    SM_EXPECT_FALSE(f.error_event.address.empty());
    SM_EXPECT_NE(f.error_event.port, 0);
  }
  f.AdvanceTime(TimeDelta::Zero());
  SM_EXPECT_TRUE(f.turn_error);
}

// The emulated TCP refuses a connection to a port without a listener
// (ECONNREFUSED): the port fails with 701 before any request.
inline void ConnectionRefused(TurnFixture& f, ProtocolType proto) {
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnTcpIntAddr, proto));
  f.turn_port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_error; }, TimeToConnect(proto) + kSimulatedRtt));
  SM_EXPECT_FALSE(f.turn_ready);
  SM_EXPECT_EQ(f.error_event.error_code, static_cast<int>(STUN_ERROR_SERVER_NOT_REACHABLE));
  SM_EXPECT_EQ(f.error_event.error_text, std::string("Failed to establish connection"));
  SM_EXPECT_TRUE(f.turn_port->state() == TurnPort::STATE_DISCONNECTED);
  SM_EXPECT_TRUE(f.server.received_message_types().empty());
}

// TestTurnAlternateServerTCP/TLS: a 300 drops the connection and allocates
// on a new connection to the alternate server.
inline void AlternateServer(TurnFixture& f, ProtocolType proto) {
  TestRedirector redirector({kTurnAlternateIntAddr});
  AddServer(f, f.turn_tcp, kTurnTcpIntAddr, proto);
  AddServer(f, f.turn_alt, kTurnAlternateIntAddr, proto);
  f.server.set_redirect_hook(&redirector);
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnTcpIntAddr, proto));
  const SocketAddress old_addr = f.turn_port->server_address().address;
  f.turn_port->PrepareAddress(f.now);
  SM_EXPECT_TRUE(f.turn_port->socket()->GetRemoteAddress() == kTurnTcpIntAddr);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_ready; }, TimeToGetAlternateTurnCandidate(proto)));
  SM_EXPECT_FALSE(f.turn_port->server_address().address == old_addr);
  SM_EXPECT_TRUE(f.turn_port->server_address().address == kTurnAlternateIntAddr);
  SM_EXPECT_TRUE(f.turn_port->server_address().proto == proto);
  SM_EXPECT_TRUE(f.turn_port->socket()->GetRemoteAddress() == kTurnAlternateIntAddr);
  SM_ASSERT_EQ(f.turn_port->Candidates().size(), size_t{1});
  SM_EXPECT_TRUE(f.turn_port->Candidates()[0].address().ipaddr() == IpAddress(kTurnExtIp));
  SM_EXPECT_NE(f.turn_port->Candidates()[0].address().port(), 0);
}

// TestTurnAlternateServerPingPong, ...DetectRepetition, ...LoopbackTcpIpv4,
// ...AnyAddressTcpIpv4 (TCP and TLS): refused redirects fail the port with
// 300 after every redirect was used.
inline void AlternateServerRefused(ProtocolType proto, const std::vector<SocketAddress>& redirects,
                                   void (*setup)(TurnFixture&)) {
  TurnFixture f;
  if (setup) setup(f);
  TestRedirector redirector(redirects);
  AddServer(f, f.turn_tcp, kTurnTcpIntAddr, proto);
  AddServer(f, f.turn_alt, kTurnAlternateIntAddr, proto);
  f.server.set_redirect_hook(&redirector);
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnTcpIntAddr, proto));
  f.turn_port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_error; }, TimeToGetAlternateTurnCandidate(proto)));
  SM_EXPECT_FALSE(f.turn_ready);
  SM_EXPECT_EQ(f.turn_port->Candidates().size(), size_t{0});
  SM_EXPECT_EQ(f.error_event.error_code, static_cast<int>(STUN_ERROR_TRY_ALTERNATE));
  SocketAddress unused;
  SM_EXPECT_FALSE(redirector.ShouldRedirect(unused, &unused));
}
inline void AlternateServerRefusedCases(ProtocolType proto, void (*setup)(TurnFixture&)) {
  AlternateServerRefused(proto, {kTurnAlternateIntAddr, kTurnTcpIntAddr}, setup);        // Ping-pong.
  AlternateServerRefused(proto, {kTurnAlternateIntAddr, kTurnAlternateIntAddr}, setup);  // Repetition.
  AlternateServerRefused(proto, {*SocketAddress::FromString("127.0.0.1:3478")}, setup);  // Loopback.
  AlternateServerRefused(proto, {*SocketAddress::FromString("0.0.0.0:3478")}, setup);    // The any address.
}

// TestTurnTcpConnection, TestTurnTlsConnection (TestTurnConnection): pings
// both ways between the relay candidate and a UDP port.
inline void Connection(TurnFixture& f, ProtocolType proto) {
  AddServer(f, f.turn_tcp, kTurnTcpIntAddr, proto);
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnTcpIntAddr, proto));
  f.PrepareTurnAndUdpPorts(proto);
  SM_ASSERT_GE(f.turn_port->Candidates().size(), size_t{1});
  // UDP to TURN: no permission yet, so the server drops it.
  sm::p2p::Connection* conn1 =
      f.udp_port->CreateConnection(f.now, f.turn_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  SM_ASSERT_TRUE(conn1 != nullptr);
  conn1->Ping(f.now);
  f.AdvanceTime(kSimulatedRtt * 2);
  SM_EXPECT_FALSE(f.turn_unknown_address);
  SM_EXPECT_FALSE(conn1->receiving());
  SM_EXPECT_TRUE(conn1->write_state() == sm::p2p::Connection::STATE_WRITE_INIT);
  // TURN to UDP, once the permission is in.
  sm::p2p::Connection* conn2 =
      f.turn_port->CreateConnection(f.now, f.udp_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  SM_ASSERT_TRUE(conn2 != nullptr);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_create_permission_success; }, kSimulatedRtt));
  conn2->Ping(f.now);
  SM_EXPECT_TRUE(f.WaitUntil(
      [&] { return conn2->write_state() == sm::p2p::Connection::STATE_WRITABLE; }, kSimulatedRtt * 2));
  SM_EXPECT_TRUE(conn1->receiving());
  SM_EXPECT_TRUE(conn2->receiving());
  SM_EXPECT_TRUE(conn1->write_state() == sm::p2p::Connection::STATE_WRITE_INIT);
  // UDP to TURN again: through now.
  conn1->Ping(f.now);
  SM_EXPECT_TRUE(f.WaitUntil(
      [&] { return conn1->write_state() == sm::p2p::Connection::STATE_WRITABLE; }, kSimulatedRtt * 2));
  SM_EXPECT_TRUE(conn2->receiving());
}

// TestTurnSendDataTurnTcpToUdp, TestTurnSendDataTurnTlsToUdp: 256 packets
// of 1 to 256 bytes each way, as ChannelData (padded to 4 bytes on the
// stream) once the channel is bound.
inline void SendData(TurnFixture& f, ProtocolType proto) {
  AddServer(f, f.turn_tcp, kTurnTcpIntAddr, proto);
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnTcpIntAddr, proto));
  f.PrepareTurnAndUdpPorts(proto);
  sm::p2p::Connection* conn1 =
      f.turn_port->CreateConnection(f.now, f.udp_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  sm::p2p::Connection* conn2 =
      f.udp_port->CreateConnection(f.now, f.turn_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  SM_ASSERT_TRUE(conn1 != nullptr);
  SM_ASSERT_TRUE(conn2 != nullptr);
  conn1->RegisterReceivedPacketCallback([&](sm::p2p::Connection*, const ReceivedPacket& p) {
    f.turn_packets.emplace_back(p.payload.begin(), p.payload.end());
  });
  conn2->RegisterReceivedPacketCallback([&](sm::p2p::Connection*, const ReceivedPacket& p) {
    f.udp_packets.emplace_back(p.payload.begin(), p.payload.end());
  });
  conn1->Ping(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return conn1->writable(); }, kSimulatedRtt * 2));
  conn2->Ping(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return conn2->writable(); }, kSimulatedRtt * 2));
  const size_t num_packets = 256;
  for (size_t i = 0; i < num_packets; ++i) {
    std::vector<uint8_t> buf(i + 1);
    uint8_t val = 0xFF;
    for (auto& b : buf) b = val--;
    conn1->Send(f.now, buf, {});
    conn2->Send(f.now, buf, {});
    f.AdvanceTime(kSimulatedRtt);
  }
  SM_ASSERT_EQ(f.turn_packets.size(), num_packets);
  SM_ASSERT_EQ(f.udp_packets.size(), num_packets);
  for (size_t i = 0; i < num_packets; ++i) {
    SM_EXPECT_EQ(f.turn_packets[i].size(), i + 1);
    SM_EXPECT_TRUE(f.turn_packets[i] == f.udp_packets[i]);
  }
  int binds = 0;
  for (uint16_t t : f.server.received_message_types()) binds += t == TURN_CHANNEL_BIND_REQUEST;
  SM_EXPECT_EQ(binds, 1);
  SM_EXPECT_GE(f.server.received_channel_data(), static_cast<int>(num_packets) - 2);
}

// TestTurnTCPReleaseAllocation, TestTurnTLSReleaseAllocation: deleting the
// port releases the allocation.
inline void ReleaseAllocation(TurnFixture& f, ProtocolType proto) {
  AddServer(f, f.turn_tcp, kTurnTcpIntAddr, proto);
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnTcpIntAddr, proto));
  f.PrepareTurnAndUdpPorts(proto);
  SM_EXPECT_EQ(f.server.allocation_count(), size_t{1});
  f.turn_port.reset();
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.server.allocation_count() == 0; }, kSimulatedRtt));
}

// TestTurnTCPGracefulReleaseAllocation, ...TLS...: Release sends a Refresh
// with lifetime 0; the port closes on the answer.
inline void GracefulReleaseAllocation(TurnFixture& f, ProtocolType proto) {
  AddServer(f, f.turn_tcp, kTurnTcpIntAddr, proto);
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnTcpIntAddr, proto));
  f.PrepareTurnAndUdpPorts(proto);
  sm::p2p::Connection* conn1 =
      f.turn_port->CreateConnection(f.now, f.udp_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  sm::p2p::Connection* conn2 =
      f.udp_port->CreateConnection(f.now, f.turn_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  conn1->Ping(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return conn1->writable(); }, kSimulatedRtt * 2));
  conn2->Ping(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return conn2->writable(); }, kSimulatedRtt * 2));
  f.turn_port->Release(f.now);
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.turn_port_closed; }, kSimulatedRtt * 2));
  SM_EXPECT_EQ(f.server.allocation_count(), size_t{0});
  SM_EXPECT_TRUE(f.turn_port->state() == TurnPort::STATE_DISCONNECTED);
  SM_EXPECT_TRUE(f.turn_port->connections().empty());
}

// A connection to the server that closes after the allocation closes the
// port (OnSocketClose -> Close): its connections go and no new ones are made.
inline void ConnectionClosedAfterAllocation(TurnFixture& f, ProtocolType proto) {
  AddServer(f, f.turn_tcp, kTurnTcpIntAddr, proto);
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnTcpIntAddr, proto));
  f.PrepareTurnAndUdpPorts(proto);
  SM_ASSERT_TRUE(f.turn_port->CreateConnection(f.now, f.udp_port->Candidates()[0], Port::ORIGIN_MESSAGE) !=
                 nullptr);
  f.error_event = IceCandidateErrorEvent{};
  f.turn_port->OnSocketClose(f.turn_port->socket(), 0);
  SM_EXPECT_TRUE(f.turn_port_closed);
  SM_EXPECT_TRUE(f.turn_port->state() == TurnPort::STATE_DISCONNECTED);
  // Ready, so no candidate error.
  SM_EXPECT_EQ(f.error_event.error_code, 0);
  f.AdvanceTime(TimeDelta::Zero());
  SM_EXPECT_TRUE(f.turn_port->connections().empty());
  SM_EXPECT_TRUE(f.turn_port->CreateConnection(f.now, f.udp_port->Candidates()[0], Port::ORIGIN_MESSAGE) == nullptr);
}

// BasicPortAllocator with a TURN server on UDP and on TCP or TLS, and UDP
// blocked to and from the client (FirewallSocketServer's FP_UDP rule): the
// relay candidate comes over the stream.
inline void AllocatorGathersRelayWithUdpBlocked(TurnFixture& f, ProtocolType proto) {
  AddServer(f, f.turn_tcp, kTurnTcpIntAddr, proto);
  f.net.SetFirewallRule([](const sim::Datagram<sim::Wire>& d) {
    return d.payload.proto == sim::Wire::Proto::kUdp && (d.src.ip == kLocalIp1 || d.dst.ip == kLocalIp1);
  });
  StaticNetworkManager network_manager;
  network_manager.AddInterface(IpAddress(kLocalIp1));
  BasicPortAllocator allocator(&network_manager, &f.local1);
  RelayServerConfig relay(kTurnUdpIntAddr, kTurnUsername, kTurnPassword, PROTO_UDP);
  relay.ports.emplace_back(kTurnTcpIntAddr, proto);
  // The test server's certificate is self-signed.
  relay.tls_cert_policy = TlsCertPolicy::TLS_CERT_POLICY_INSECURE_NO_CHECK;
  allocator.SetConfiguration(f.now, ServerAddresses(), {relay}, 0, NO_PRUNE);
  allocator.set_step_delay(kMinimumStepDelay);
  auto session = allocator.CreateSession("audio", ICE_CANDIDATE_COMPONENT_RTP, "ufrag", "passwordpasswordpassword");
  std::vector<Candidate> candidates;
  session->SubscribeCandidatesReady(nullptr, [&](PortAllocatorSession*, const std::vector<Candidate>& c) {
    candidates.insert(candidates.end(), c.begin(), c.end());
  });
  session->StartGettingPorts(f.now);
  const Timestamp until = f.now + TimeDelta::Seconds(10);
  const Candidate* relay_candidate = nullptr;
  while (f.now < until && relay_candidate == nullptr) {
    const Timestamp t = std::min({session->NextProcessTime(), f.Next(), until});
    f.now = std::max(f.now, t);
    session->Process(f.now);
    f.Step(f.now);
    for (const Candidate& c : candidates) {
      if (c.is_relay()) relay_candidate = &c;
    }
  }
  SM_ASSERT_TRUE(relay_candidate != nullptr);
  SM_EXPECT_TRUE(relay_candidate->address().ipaddr() == IpAddress(kTurnExtIp));
  SM_EXPECT_EQ(relay_candidate->relay_protocol(), std::string(Transport(proto)));
  SM_EXPECT_EQ(f.server.allocation_count(), size_t{1});
  // The UDP Allocate was dropped.
  SM_EXPECT_GT(f.net.stats().firewall_dropped, 0);
}

}  // namespace turn_stream
