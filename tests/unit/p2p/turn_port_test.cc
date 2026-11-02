// TURN client port and test TURN server (issue #1031, C4 part 1): the UDP
// cases of libwebrtc M157's p2p/base/turn_port_unittest.cc and
// p2p/test/turn_server_unittest.cc, on the emulated network (#985) with the
// library's time model in place of the simulated clock, plus a relay
// candidate gathered by BasicPortAllocator behind a symmetric NAT. Times are
// the emulated links' (10 ms one way to each host, so the client-server RTT
// is 40 ms where libwebrtc's kSimulatedRtt is 50 ms).
#include "turn_fixture.h"

// TestTurnPortType, TestReconstructedServerUrlForUdpIPv4, TestTurnAllocate:
// one relay candidate on the server's external address, after a 401.
SM_TEST(P2pTurnPort, Allocate) {
  TurnFixture f;
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnUdpIntAddr, PROTO_UDP));
  SM_ASSERT_TRUE(f.turn_port != nullptr);
  SM_EXPECT_TRUE(f.turn_port->Type() == IceCandidateType::kRelay);
  // Options before the socket exists are kept and applied to it.
  SM_EXPECT_EQ(f.turn_port->SetOption(SocketOption::kSndBuf, 10 * 1024), 0);
  int value = 0;
  SM_EXPECT_EQ(f.turn_port->GetOption(SocketOption::kSndBuf, &value), 0);
  SM_EXPECT_EQ(value, 10 * 1024);
  f.turn_port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_ready; }, kSimulatedRtt * 2));
  SM_ASSERT_EQ(f.turn_port->Candidates().size(), size_t{1});
  const Candidate& c = f.turn_port->Candidates()[0];
  SM_EXPECT_TRUE(c.is_relay());
  SM_EXPECT_TRUE(c.address().ipaddr() == IpAddress(kTurnExtIp));
  SM_EXPECT_NE(c.address().port(), 0);
  SM_EXPECT_EQ(c.url(), std::string("turn:99.99.99.3:3478?transport=udp"));
  SM_EXPECT_EQ(c.protocol(), std::string("udp"));
  SM_EXPECT_EQ(c.relay_protocol(), std::string("udp"));
  // The related address is the server-reflexive one: this host's own.
  SM_EXPECT_TRUE(c.related_address() == f.turn_port->GetLocalAddress());
  SM_EXPECT_EQ(c.priority() >> 24, static_cast<uint32_t>(ICE_TYPE_PREFERENCE_RELAY_UDP));
  SM_EXPECT_EQ(f.server.allocation_count(), size_t{1});
  SM_EXPECT_TRUE(f.server.relayed_address(f.turn_port->GetLocalAddress()) == c.address());
  // Allocate without credentials (401), then with them.
  SM_ASSERT_EQ(f.server.received_message_types().size(), size_t{2});
  SM_EXPECT_EQ(f.server.received_message_types()[0], STUN_ALLOCATE_REQUEST);
  SM_EXPECT_EQ(f.server.received_message_types()[1], STUN_ALLOCATE_REQUEST);
  SM_EXPECT_FALSE(f.turn_port->hash().empty());
  SM_EXPECT_EQ(f.turn_port->realm(), std::string(kTestRealm));
  int sndbuf = 0;
  SM_EXPECT_EQ(f.turn_port->socket()->GetOption(SocketOption::kSndBuf, &sndbuf), 0);
  SM_EXPECT_EQ(sndbuf, 10 * 1024);
}

// TestTurnAllocateWithLoggingId, TestTurnAllocateWithoutLoggingId.
SM_TEST(P2pTurnPort, AllocateLoggingId) {
  for (const char* id : {"KESO", static_cast<const char*>(nullptr)}) {
    TurnFixture f;
    f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnUdpIntAddr, PROTO_UDP));
    if (id) f.turn_port->SetTurnLoggingId(id);
    int allocates = 0;
    bool ok = true;
    f.server.SetStunMessageObserver([&](const TurnMessage& msg) {
      if (msg.type() != STUN_ALLOCATE_REQUEST) return;
      ++allocates;
      const StunByteStringAttribute* attr = msg.GetByteString(STUN_ATTR_TURN_LOGGING_ID);
      if (id) {
        ok = ok && attr != nullptr && attr->string_view() == id;
      } else {
        ok = ok && attr == nullptr;
      }
    });
    f.turn_port->PrepareAddress(f.now);
    SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_ready; }, kSimulatedRtt * 2));
    SM_EXPECT_EQ(allocates, 2);
    SM_EXPECT_TRUE(ok);
  }
}

// TestTurnBadCredentials, TestTurnAllocateBadPassword: a 401 after the
// credentials were sent fails the port.
SM_TEST(P2pTurnPort, BadCredentials) {
  TurnFixture f;
  f.CreateTurnPort(kTurnUsername, "bad", ProtocolAddress(kTurnUdpIntAddr, PROTO_UDP));
  f.turn_port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_error; }, kSimulatedRtt * 3));
  SM_EXPECT_EQ(f.turn_port->Candidates().size(), size_t{0});
  SM_EXPECT_EQ(f.error_event.error_code, static_cast<int>(STUN_ERROR_UNAUTHORIZED));
  SM_EXPECT_EQ(f.error_event.error_text, std::string("Unauthorized."));
  SM_EXPECT_EQ(f.error_event.url, std::string("turn:99.99.99.3:3478?transport=udp"));
  // Missing credentials fail at once.
  TurnFixture g;
  g.CreateTurnPort(kTurnUsername, "", ProtocolAddress(kTurnUdpIntAddr, PROTO_UDP));
  g.turn_port->PrepareAddress(g.now);
  SM_EXPECT_EQ(g.error_event.error_code, static_cast<int>(STUN_ERROR_UNAUTHORIZED));
  SM_EXPECT_EQ(g.error_event.error_text, std::string("Missing TURN server credentials."));
  SM_EXPECT_FALSE(g.turn_error);  // The port error is posted.
  g.AdvanceTime(TimeDelta::Zero());
  SM_EXPECT_TRUE(g.turn_error);
}

// TestServerAddressFamilyMismatch: IPv4 local, IPv6 server: an error without
// an error event.
SM_TEST(P2pTurnPort, ServerAddressFamilyMismatch) {
  TurnFixture f;
  f.CreateTurnPort(kTurnUsername, kTurnPassword,
                   ProtocolAddress(*SocketAddress::FromString("[2400:4030:1:2c00:be30:abcd:efab:cdef]:3478"),
                                   PROTO_UDP));
  f.turn_port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_error; }, kSimulatedRtt * 3));
  SM_EXPECT_EQ(f.turn_port->Candidates().size(), size_t{0});
  SM_EXPECT_EQ(f.error_event.error_code, 0);
}

// TestTurnAllocateMismatch and TestTurnAllocateNonceResetAfterAllocateMismatch:
// a new port on the old port's address gets 437 and moves to a new socket,
// and the nonce it ends with is a newer one.
SM_TEST(P2pTurnPort, AllocateMismatch) {
  TurnFixture f;
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnUdpIntAddr, PROTO_UDP));
  f.turn_port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_ready; }, kSimulatedRtt * 2));
  const SocketAddress first_addr = f.turn_port->socket()->GetLocalAddress();
  // Closing the socket keeps the allocation at the server (no release).
  f.turn_port->OnSocketClose(f.turn_port->socket(), 0);
  SM_EXPECT_TRUE(f.turn_port_closed);
  f.local1.SetNextPortForTesting(first_addr.port());
  f.turn_ready = false;
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnUdpIntAddr, PROTO_UDP));
  const std::string first_nonce = f.server.SetTimestampForNextNonce(f.now.ms() - 1);
  f.turn_port->PrepareAddress(f.now);
  SM_EXPECT_TRUE(f.turn_port->socket()->GetLocalAddress() == first_addr);
  // 401 (with the old-timestamp nonce), 437, 401, then success.
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_ready; }, kSimulatedRtt * 4));
  SM_EXPECT_FALSE(f.turn_port->socket()->GetLocalAddress() == first_addr);
  SM_EXPECT_NE(f.turn_port->nonce(), first_nonce);
  SM_EXPECT_EQ(f.server.allocation_count(), size_t{2});
}

// TestSharedSocketAllocateMismatch: a shared-socket port moves to a socket of
// its own after 437, and ignores the shared socket from then on.
SM_TEST(P2pTurnPort, SharedSocketAllocateMismatch) {
  TurnFixture f;
  f.CreateSharedTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnUdpIntAddr, PROTO_UDP));
  f.turn_port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_ready; }, kSimulatedRtt * 2));
  const SocketAddress first_addr = f.turn_port->socket()->GetLocalAddress();
  f.turn_port->OnSocketClose(f.turn_port->socket(), 0);
  f.turn_ready = false;
  f.CreateSharedTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnUdpIntAddr, PROTO_UDP));
  SM_EXPECT_TRUE(f.turn_port->SharedSocket());
  f.turn_port->PrepareAddress(f.now);
  SM_EXPECT_TRUE(f.turn_port->socket()->GetLocalAddress() == first_addr);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_ready; }, kSimulatedRtt * 4));
  SM_EXPECT_FALSE(f.turn_port->socket()->GetLocalAddress() == first_addr);
  SM_EXPECT_FALSE(f.turn_port->SharedSocket());
  const uint8_t junk[] = {'T', 'e', 's', 't', ' ', 'p', 'k', 't'};
  SM_EXPECT_FALSE(f.turn_port->HandleIncomingPacket(f.shared_socket.get(),
                                                     ReceivedPacket{junk, kTurnUdpIntAddr, f.now}));
}

// After the third 437 in a row the port gives up (MAX_ALLOCATE_MISMATCH_RETRIES).
SM_TEST(P2pTurnPort, AllocateMismatchGivesUpAfterTwoRetries) {
  TurnFixture f;
  // Three allocations on three consecutive ports, kept at the server.
  std::vector<SocketAddress> addrs;
  for (int i = 0; i < 3; ++i) {
    f.turn_ready = false;
    f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnUdpIntAddr, PROTO_UDP));
    f.turn_port->PrepareAddress(f.now);
    SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_ready; }, kSimulatedRtt * 2));
    addrs.push_back(f.turn_port->socket()->GetLocalAddress());
    f.turn_port->OnSocketClose(f.turn_port->socket(), 0);
    f.turn_port.reset();
  }
  // The next ports tried are those three again.
  f.local1.SetNextPortForTesting(addrs[0].port());
  f.turn_ready = false;
  f.turn_error = false;
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnUdpIntAddr, PROTO_UDP));
  f.turn_port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_error; }, kSimulatedRtt * 10));
  SM_EXPECT_FALSE(f.turn_ready);
  SM_EXPECT_EQ(f.error_event.error_code, static_cast<int>(STUN_ERROR_ALLOCATION_MISMATCH));
  SM_EXPECT_EQ(f.error_event.error_text, std::string("Maximum retries reached for allocation mismatch."));
}

// TestTurnAlternateServerUDP: 300 with ALTERNATE-SERVER moves the allocation.
SM_TEST(P2pTurnPort, AlternateServer) {
  TurnFixture f;
  TestRedirector redirector({kTurnAlternateIntAddr});
  f.server.AddInternalSocket(f.turn_alt.CreateUdpSocket(kTurnAlternateIntAddr, 0, 0), PROTO_UDP);
  f.server.set_redirect_hook(&redirector);
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnUdpIntAddr, PROTO_UDP));
  const SocketAddress old_addr = f.turn_port->server_address().address;
  f.turn_port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_ready; }, kSimulatedRtt * 3));
  SM_EXPECT_FALSE(f.turn_port->server_address().address == old_addr);
  SM_EXPECT_TRUE(f.turn_port->server_address().address == kTurnAlternateIntAddr);
  SM_ASSERT_EQ(f.turn_port->Candidates().size(), size_t{1});
  SM_EXPECT_TRUE(f.turn_port->Candidates()[0].address().ipaddr() == IpAddress(kTurnExtIp));
}

// TestTurnAlternateServerPingPongUDP, ...DetectRepetitionUDP, ...LoopbackUdpIpv4,
// ...AnyAddressUdpIpv4, TestTurnDangerousAlternateServerUDP: redirects that are
// refused fail the port with 300.
SM_TEST(P2pTurnPort, AlternateServerRefused) {
  const std::vector<std::vector<SocketAddress>> cases = {
      {kTurnAlternateIntAddr, kTurnUdpIntAddr},        // Ping-pong.
      {kTurnAlternateIntAddr, kTurnAlternateIntAddr},  // Repetition.
      {*SocketAddress::FromString("127.0.0.1:3478")},  // Loopback.
      {*SocketAddress::FromString("0.0.0.0:3478")},    // The any address.
      {SocketAddress(kTurnAltIp, 25)},                 // A dangerous port.
  };
  for (const auto& redirects : cases) {
    TurnFixture f;
    TestRedirector redirector(redirects);
    f.server.AddInternalSocket(f.turn_alt.CreateUdpSocket(kTurnAlternateIntAddr, 0, 0), PROTO_UDP);
    f.server.set_redirect_hook(&redirector);
    f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnUdpIntAddr, PROTO_UDP));
    f.turn_port->PrepareAddress(f.now);
    SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_error; }, kSimulatedRtt * 4));
    SM_EXPECT_FALSE(f.turn_ready);
    SM_EXPECT_EQ(f.turn_port->Candidates().size(), size_t{0});
    SM_EXPECT_EQ(f.error_event.error_code, static_cast<int>(STUN_ERROR_TRY_ALTERNATE));
    // Every redirect was used: the failure is the refusal, not another error.
    SocketAddress unused;
    SM_EXPECT_FALSE(redirector.ShouldRedirect(unused, &unused));
  }
}

// TestTurnDangerousServer(Permits53/80/443), TestOverlongUsername.
SM_TEST(P2pTurnPort, ValidateServerPortAndUsername) {
  TurnFixture f;
  for (int port : {25, 1023}) {
    f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(SocketAddress(kTurnIntIp, port), PROTO_UDP));
    SM_EXPECT_TRUE(f.turn_port == nullptr);
  }
  for (int port : {53, 80, 443, 1024}) {
    f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(SocketAddress(kTurnIntIp, port), PROTO_UDP));
    SM_EXPECT_TRUE(f.turn_port != nullptr);
  }
  const std::string overlong(kMaxTurnUsernameLength + 1, 'x');
  f.CreateTurnPort(overlong, kTurnPassword, ProtocolAddress(kTurnUdpIntAddr, PROTO_UDP));
  SM_EXPECT_TRUE(f.turn_port == nullptr);
}

// TestTurnConnection (UDP), and with one-time nonces
// (TestTurnConnectionUsingOTUNonce): pings both ways through the relay.
SM_TEST(P2pTurnPort, Connection) {
  for (bool otu : {false, true}) {
    TurnFixture f;
    f.server.set_enable_otu_nonce(otu);
    f.PrepareTurnAndUdpPorts();
    SM_ASSERT_EQ(f.turn_port->Candidates().size(), size_t{1});
    // UDP to TURN: no permission yet, so the server drops it.
    Connection* conn1 = f.udp_port->CreateConnection(f.now, f.turn_port->Candidates()[0], Port::ORIGIN_MESSAGE);
    SM_ASSERT_TRUE(conn1 != nullptr);
    conn1->Ping(f.now);
    f.AdvanceTime(kSimulatedRtt * 2);
    SM_EXPECT_FALSE(f.turn_unknown_address);
    SM_EXPECT_FALSE(conn1->receiving());
    SM_EXPECT_TRUE(conn1->write_state() == Connection::STATE_WRITE_INIT);
    // TURN to UDP, once the permission is there.
    Connection* conn2 = f.turn_port->CreateConnection(f.now, f.udp_port->Candidates()[0], Port::ORIGIN_MESSAGE);
    SM_ASSERT_TRUE(conn2 != nullptr);
    SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_create_permission_success; }, kSimulatedRtt * (otu ? 2 : 1)));
    conn2->Ping(f.now);
    SM_EXPECT_TRUE(f.WaitUntil([&] { return conn2->write_state() == Connection::STATE_WRITABLE; }, kSimulatedRtt * 2));
    SM_EXPECT_TRUE(conn1->receiving());
    SM_EXPECT_TRUE(conn2->receiving());
    SM_EXPECT_TRUE(conn1->write_state() == Connection::STATE_WRITE_INIT);
    // UDP to TURN again: now through.
    conn1->Ping(f.now);
    SM_EXPECT_TRUE(f.WaitUntil([&] { return conn1->write_state() == Connection::STATE_WRITABLE; }, kSimulatedRtt * 2));
    SM_EXPECT_TRUE(conn2->receiving());
  }
}

// TestDestroyTurnConnection: the entry outlives its connection by the
// permission timeout; a new connection takes it again.
SM_TEST(P2pTurnPort, DestroyConnection) {
  TurnFixture f;
  f.PrepareTurnAndUdpPorts();
  Connection* conn1 = f.udp_port->CreateConnection(f.now, f.turn_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  Connection* conn2 = f.turn_port->CreateConnection(f.now, f.udp_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  // So the entry times out before the port.
  f.turn_port->set_timeout_delay(10 * 60 * 1000);
  SM_ASSERT_TRUE(conn2 != nullptr);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_create_permission_success; }, kSimulatedRtt));
  conn1->Ping(f.now);
  SM_EXPECT_TRUE(f.WaitUntil([&] { return conn1->write_state() == Connection::STATE_WRITABLE; }, kSimulatedRtt * 2));
  SM_EXPECT_FALSE(f.turn_unknown_address);
  // The entry stays: a ping from the peer is an unknown address now.
  f.turn_port->DestroyConnection(f.now, conn2);
  SM_EXPECT_EQ(f.turn_port->entry_count_for_test(), size_t{1});
  conn1->Ping(f.now);
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.turn_unknown_address; }, kSimulatedRtt));
  // A request with a bad password from an address without a connection gets
  // no error response through the relay (SendBindingErrorResponse) and
  // doesn't crash.
  const std::string pwd = conn1->remote_password_for_test();
  conn1->set_remote_password_for_test("bad");
  auto msg = conn1->BuildPingRequestForTest();
  ByteBufferWriter buf;
  msg->Write(&buf);
  conn1->Send(f.now, buf.DataView(), {});
  conn1->set_remote_password_for_test(pwd);
  f.AdvanceTime(kSimulatedRtt);
  // Five minutes later the entry is gone.
  f.turn_unknown_address = false;
  f.AdvanceTime(kTurnPermissionTimeout);
  SM_EXPECT_EQ(f.turn_port->entry_count_for_test(), size_t{0});
  // A new connection: a new entry, and it receives.
  f.turn_create_permission_success = false;
  conn2 = f.turn_port->CreateConnection(f.now, f.udp_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  SM_EXPECT_EQ(f.turn_port->entry_count_for_test(), size_t{1});
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_create_permission_success; }, kSimulatedRtt));
  conn1->Ping(f.now);
  SM_EXPECT_TRUE(f.WaitUntil([&] { return conn2->receiving(); }, kSimulatedRtt));
}

// An entry whose last connection goes is kept if another connection takes it
// within the permission timeout (TrackConnection cancels the delete).
SM_TEST(P2pTurnPort, EntryReusedBeforeTimeout) {
  TurnFixture f;
  f.PrepareTurnAndUdpPorts();
  f.turn_port->set_timeout_delay(10 * 60 * 1000);
  Connection* conn = f.turn_port->CreateConnection(f.now, f.udp_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_create_permission_success; }, kSimulatedRtt));
  f.turn_port->DestroyConnection(f.now, conn);
  f.AdvanceTime(TimeDelta::Seconds(60));
  f.turn_port->CreateConnection(f.now, f.udp_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  f.AdvanceTime(kTurnPermissionTimeout);
  SM_EXPECT_EQ(f.turn_port->entry_count_for_test(), size_t{1});
}

// TestTurnSendDataTurnUdpToUdp: data both ways; the first payload from the
// TURN side goes in a Send indication and binds a channel, later ones as
// ChannelData.
SM_TEST(P2pTurnPort, SendData) {
  TurnFixture f;
  f.PrepareTurnAndUdpPorts();
  Connection* conn1 = f.turn_port->CreateConnection(f.now, f.udp_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  Connection* conn2 = f.udp_port->CreateConnection(f.now, f.turn_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  SM_ASSERT_TRUE(conn1 != nullptr);
  SM_ASSERT_TRUE(conn2 != nullptr);
  conn1->RegisterReceivedPacketCallback([&](Connection*, const ReceivedPacket& p) {
    f.turn_packets.emplace_back(p.payload.begin(), p.payload.end());
  });
  conn2->RegisterReceivedPacketCallback([&](Connection*, const ReceivedPacket& p) {
    f.udp_packets.emplace_back(p.payload.begin(), p.payload.end());
  });
  conn1->Ping(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return conn1->write_state() == Connection::STATE_WRITABLE; }, kSimulatedRtt * 2));
  conn2->Ping(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return conn2->write_state() == Connection::STATE_WRITABLE; }, kSimulatedRtt * 2));
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
  const TurnEntry* entry = f.turn_port->FindEntryForTest(f.udp_port->Candidates()[0].address());
  SM_ASSERT_TRUE(entry != nullptr);
  // One channel bind; channel data from both sides once bound.
  int binds = 0;
  for (uint16_t t : f.server.received_message_types()) binds += t == TURN_CHANNEL_BIND_REQUEST;
  SM_EXPECT_EQ(binds, 1);
  SM_EXPECT_GE(f.server.received_channel_data(), static_cast<int>(num_packets) - 2);
}

// TestRefreshCreatePermissionRequest: a CreatePermission refused (bad
// credentials) fails and prunes the connection.
SM_TEST(P2pTurnPort, RefreshCreatePermissionRequest) {
  TurnFixture f;
  f.PrepareTurnAndUdpPorts();
  Connection* conn = f.turn_port->CreateConnection(f.now, f.udp_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  SM_ASSERT_TRUE(conn != nullptr);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_create_permission_success; }, kSimulatedRtt));
  f.turn_create_permission_success = false;
  // The pending refresh succeeds and schedules one with the bad credentials.
  f.turn_port->set_credentials(RelayCredentials("bad_user", "bad_pwd"));
  f.turn_port->request_manager().FlushForTest(f.now, kAllRequestsForTest);
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.turn_create_permission_success; }, kSimulatedRtt));
  f.turn_port->request_manager().FlushForTest(f.now, kAllRequestsForTest);
  SM_EXPECT_TRUE(f.WaitUntil([&] { return !f.turn_create_permission_success; }, kSimulatedRtt));
  SM_EXPECT_TRUE(TurnFixture::CheckConnectionFailedAndPruned(conn));
}

// TestRefreshRequestGetsErrorResponse: a refused Refresh makes the port
// receive-only and fails and prunes its connections.
SM_TEST(P2pTurnPort, RefreshRequestGetsErrorResponse) {
  TurnFixture f;
  f.PrepareTurnAndUdpPorts();
  f.turn_port->CreateConnection(f.now, f.udp_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  f.turn_port->set_credentials(RelayCredentials("bad_user", "bad_pwd"));
  f.turn_refresh_success = false;
  f.turn_port->request_manager().FlushForTest(f.now, TURN_REFRESH_REQUEST);
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.turn_refresh_success; }, kSimulatedRtt));
  f.turn_port->request_manager().FlushForTest(f.now, TURN_REFRESH_REQUEST);
  SM_EXPECT_TRUE(f.WaitUntil([&] { return !f.turn_refresh_success; }, kSimulatedRtt));
  f.AdvanceTime(TimeDelta::Zero());
  SM_EXPECT_FALSE(f.turn_port->connected());
  SM_EXPECT_TRUE(f.turn_port->state() == TurnPort::STATE_RECEIVEONLY);
  SM_EXPECT_TRUE(f.CheckAllConnectionsFailedAndPruned());
  SM_EXPECT_FALSE(f.turn_port->HasRequests());
  // No new connections while receive-only.
  SM_EXPECT_TRUE(f.turn_port->CreateConnection(f.now, f.udp_port->Candidates()[0], Port::ORIGIN_MESSAGE) == nullptr);
}

// The allocation is refreshed one minute before its lifetime ends.
SM_TEST(P2pTurnPort, RefreshScheduledBeforeLifetimeEnds) {
  TurnFixture f;
  f.PrepareTurnAndUdpPorts();
  // Kept without connections (the allocator does this when it hands the port on).
  f.turn_port->KeepAliveUntilPruned();
  const Timestamp allocated = f.now;
  int refreshes = 0;
  int stale_nonce_retries = 0;
  Timestamp last_refresh = Timestamp::MinusInfinity();
  f.server.SetStunMessageObserver([&](const TurnMessage& m) {
    if (m.type() != TURN_REFRESH_REQUEST) return;
    if (f.now - last_refresh < TimeDelta::Seconds(1)) ++stale_nonce_retries;
    last_refresh = f.now;
    ++refreshes;
  });
  // The server's default lifetime is 10 minutes: the refresh goes at 9
  // (the allocate response arrived at `allocated`), and reaches the server
  // half an RTT later.
  f.AdvanceTime(TimeDelta::Seconds(9 * 60) - TimeDelta::Millis(1));
  SM_EXPECT_EQ(refreshes, 0);
  f.AdvanceTime(TimeDelta::Millis(1) + kSimulatedRtt);
  SM_EXPECT_EQ(refreshes, 1);
  SM_EXPECT_TRUE(last_refresh - allocated == TimeDelta::Seconds(9 * 60) + kSimulatedRtt / 2);
  // Still allocated an hour later. The server's nonce expires after 60
  // minutes: that refresh gets 438 and goes again with the new nonce.
  f.AdvanceTime(TimeDelta::Seconds(60 * 60));
  SM_EXPECT_EQ(f.server.allocation_count(), size_t{1});
  SM_EXPECT_TRUE(f.turn_port->ready());
  SM_EXPECT_EQ(stale_nonce_retries, 1);
  SM_EXPECT_EQ(refreshes, 8);
}

// TestChannelBindGetErrorResponse: a refused ChannelBind fails and prunes the
// connection, and data still goes in Send indications.
SM_TEST(P2pTurnPort, ChannelBindGetErrorResponse) {
  TurnFixture f;
  f.PrepareTurnAndUdpPorts();
  Connection* conn1 = f.turn_port->CreateConnection(f.now, f.udp_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  Connection* conn2 = f.udp_port->CreateConnection(f.now, f.turn_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  SM_ASSERT_TRUE(conn1 != nullptr);
  SM_ASSERT_TRUE(conn2 != nullptr);
  conn1->Ping(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return conn1->writable(); }, kSimulatedRtt * 2));
  f.server.set_reject_bind_requests(true);
  const uint8_t data[] = {'A', 'B', 'C'};
  conn1->Send(f.now, data, {});
  SM_EXPECT_TRUE(f.WaitUntil([&] { return TurnFixture::CheckConnectionFailedAndPruned(conn1); }, kSimulatedRtt));
  conn2->RegisterReceivedPacketCallback([&](Connection*, const ReceivedPacket& p) {
    f.udp_packets.emplace_back(p.payload.begin(), p.payload.end());
  });
  conn1->Send(f.now, data, {});
  SM_EXPECT_TRUE(f.WaitUntil([&] { return !f.udp_packets.empty(); }, kSimulatedRtt));
}

// TestConnectionFailedAndPrunedOnCreatePermissionFailure (over UDP here): a
// 403 to CreatePermission fails and prunes the connection, which is not
// deleted.
SM_TEST(P2pTurnPort, ConnectionFailedAndPrunedOnCreatePermissionFailure) {
  TurnFixture f;
  f.server.set_reject_private_addresses(true);
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnUdpIntAddr, PROTO_UDP));
  f.turn_port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_ready; }, kSimulatedRtt * 3));
  Candidate remote = f.turn_port->Candidates()[0];
  remote.set_type(IceCandidateType::kHost);
  remote.set_address(*SocketAddress::FromString("10.0.0.10:5000"));
  bool destroyed = false;
  Connection* conn = f.turn_port->CreateConnection(f.now, remote, Port::ORIGIN_MESSAGE);
  SM_ASSERT_TRUE(conn != nullptr);
  conn->SubscribeDestroyed(nullptr, [&](Connection*) { destroyed = true; });
  f.turn_create_permission_success = true;
  SM_EXPECT_TRUE(f.WaitUntil([&] { return TurnFixture::CheckConnectionFailedAndPruned(conn); }, kSimulatedRtt));
  SM_EXPECT_FALSE(f.turn_create_permission_success);
  f.AdvanceTime(TimeDelta::Seconds(1));
  SM_EXPECT_FALSE(destroyed);
}

// TestTurnReleaseAllocation: deleting the port releases the allocation.
SM_TEST(P2pTurnPort, ReleaseAllocation) {
  TurnFixture f;
  f.PrepareTurnAndUdpPorts();
  SM_EXPECT_EQ(f.server.allocation_count(), size_t{1});
  f.turn_port.reset();
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.server.allocation_count() == 0; }, kSimulatedRtt));
}

// TestTurnUDPGracefulReleaseAllocation: Release sends a Refresh with lifetime
// 0, the server drops the allocation and the port closes on the answer.
SM_TEST(P2pTurnPort, GracefulReleaseAllocation) {
  TurnFixture f;
  f.PrepareTurnAndUdpPorts();
  Connection* conn1 = f.turn_port->CreateConnection(f.now, f.udp_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  Connection* conn2 = f.udp_port->CreateConnection(f.now, f.turn_port->Candidates()[0], Port::ORIGIN_MESSAGE);
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

// TestStopProcessingPacketsAfterClosed.
SM_TEST(P2pTurnPort, StopProcessingPacketsAfterClosed) {
  TurnFixture f;
  f.PrepareTurnAndUdpPorts();
  Connection* conn1 = f.turn_port->CreateConnection(f.now, f.udp_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  Connection* conn2 = f.udp_port->CreateConnection(f.now, f.turn_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  SM_ASSERT_TRUE(conn1 != nullptr);
  SM_ASSERT_TRUE(conn2 != nullptr);
  conn2->Ping(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return conn2->write_state() == Connection::STATE_WRITABLE; }, kSimulatedRtt * 2));
  f.turn_port->CloseForTest(f.now);
  f.AdvanceTime(kSimulatedRtt);
  f.turn_unknown_address = false;
  conn2->Ping(f.now);
  f.AdvanceTime(kSimulatedRtt);
  SM_EXPECT_FALSE(f.turn_unknown_address);
}

// TestUdpSocketCloseFailsPendingAllocateRequest.
SM_TEST(P2pTurnPort, UdpSocketCloseFailsPendingAllocateRequest) {
  TurnFixture f;
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnUdpIntAddr, PROTO_UDP));
  f.turn_port->PrepareAddress(f.now);
  SM_ASSERT_FALSE(f.turn_ready);
  f.turn_port->OnSocketClose(f.turn_port->socket(), 1);
  // At once, except the posted port error.
  SM_EXPECT_EQ(f.error_event.error_code, static_cast<int>(STUN_ERROR_SERVER_NOT_REACHABLE));
  SM_EXPECT_EQ(f.error_event.error_text, std::string("Socket was closed"));
  SM_EXPECT_FALSE(f.turn_error);
  f.AdvanceTime(TimeDelta::Zero());
  SM_EXPECT_TRUE(f.turn_error);
  SM_EXPECT_EQ(f.turn_port->Candidates().size(), size_t{0});
  SM_EXPECT_TRUE(f.turn_port->CreateConnection(f.now, f.turn_port->Candidates().empty()
                                                          ? Candidate()
                                                          : f.turn_port->Candidates()[0],
                                               Port::ORIGIN_MESSAGE) == nullptr);
}

// CanCreateTwoConnectionsToSameAddress.
SM_TEST(P2pTurnPort, CanCreateTwoConnectionsToSameAddress) {
  TurnFixture f;
  f.PrepareTurnAndUdpPorts();
  Connection* conn1 = f.turn_port->CreateConnection(f.now, f.udp_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  Connection* conn2 = f.turn_port->CreateConnection(f.now, f.udp_port->Candidates()[0], Port::ORIGIN_MESSAGE);
  SM_EXPECT_NE(conn1, conn2);
  SM_EXPECT_EQ(f.turn_port->entry_count_for_test(), size_t{1});
}

// TestUnauthenticatedAllocateSuccessRejected: a forged success without
// MESSAGE-INTEGRITY for an unauthenticated Allocate is refused.
SM_TEST(P2pTurnPort, UnauthenticatedAllocateSuccessRejected) {
  TurnFixture f;
  const SocketAddress fake_server_addr(kTurnAltIp, 3478);
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(fake_server_addr, PROTO_UDP));
  std::unique_ptr<AsyncPacketSocket> server_socket = f.turn_alt.CreateUdpSocket(fake_server_addr, 0, 0);
  std::string transaction_id;
  server_socket->RegisterReceivedPacketCallback([&](AsyncPacketSocket*, const ReceivedPacket& p) {
    ByteBufferReader reader(p.payload);
    TurnMessage msg;
    if (msg.Read(&reader) && msg.type() == STUN_ALLOCATE_REQUEST) transaction_id = msg.transaction_id();
  });
  f.turn_port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return !transaction_id.empty(); }, kSimulatedRtt));
  TurnMessage forged(STUN_ALLOCATE_RESPONSE, transaction_id);
  forged.AddAttribute(std::make_unique<StunXorAddressAttribute>(STUN_ATTR_XOR_RELAYED_ADDRESS,
                                                                *SocketAddress::FromString("198.51.100.99:6666")));
  forged.AddAttribute(std::make_unique<StunXorAddressAttribute>(STUN_ATTR_XOR_MAPPED_ADDRESS,
                                                                *SocketAddress::FromString("203.0.113.77:5555")));
  forged.AddAttribute(std::make_unique<StunUInt32Attribute>(STUN_ATTR_LIFETIME, 300));
  ByteBufferWriter buf;
  forged.Write(&buf);
  server_socket->SendTo(f.now, buf.DataView(), f.turn_port->socket()->GetLocalAddress(), {});
  f.AdvanceTime(kSimulatedRtt);
  SM_EXPECT_FALSE(f.turn_ready);
  server_socket->DeregisterReceivedPacketCallback();
}

// BasicPortAllocator with a TURN server and its default TurnPortFactory:
// behind a symmetric NAT the relay candidate is the server's external
// address, related to the NAT's mapping.
SM_TEST(P2pTurnPort, AllocatorGathersRelayBehindSymmetricNat) {
  InstallTurnTestCrypto();
  sim::ByteNetwork net;
  sim::NatConfig cfg;
  cfg.type = sim::NatType::kSymmetric;
  cfg.external_ip = kNatIp;
  const int realm = net.NatRealm(net.AddNat(cfg));
  sim::EmulatedSocketServer client(net, net.AddHost(kPrivateIp, realm, Link(5), Link(5)));
  sim::EmulatedSocketServer turn_int(net, net.AddHost(kTurnIntIp, sim::ByteNetwork::kPublicRealm, Link(15), Link(15)));
  sim::EmulatedSocketServer turn_ext(net, net.AddHost(kTurnExtIp, sim::ByteNetwork::kPublicRealm, Link(15), Link(15)));
  TestAuth auth;
  sim::TurnServer server;
  server.set_realm(kTestRealm);
  server.set_auth_hook(&auth);
  server.AddInternalSocket(turn_int.CreateUdpSocket(kTurnUdpIntAddr, 0, 0), PROTO_UDP);
  server.SetExternalSocketFactory(&turn_ext, kTurnUdpExtAddr);

  StaticNetworkManager network_manager;
  network_manager.AddInterface(IpAddress(kPrivateIp));
  BasicPortAllocator allocator(&network_manager, &client);
  SM_EXPECT_TRUE(allocator.relay_port_factory() != nullptr);
  Timestamp now = Timestamp::Seconds(10);
  allocator.SetConfiguration(now, ServerAddresses(),
                             {RelayServerConfig(kTurnUdpIntAddr, kTurnUsername, kTurnPassword, PROTO_UDP)}, 0,
                             NO_PRUNE);
  allocator.set_step_delay(kMinimumStepDelay);
  auto session = allocator.CreateSession("audio", ICE_CANDIDATE_COMPONENT_RTP, "ufrag", "passwordpasswordpassword");
  std::vector<Candidate> candidates;
  bool done = false;
  session->SubscribeCandidatesReady(nullptr, [&](PortAllocatorSession*, const std::vector<Candidate>& c) {
    candidates.insert(candidates.end(), c.begin(), c.end());
  });
  session->SubscribeCandidatesAllocationDone(nullptr, [&](PortAllocatorSession*) { done = true; });
  session->StartGettingPorts(now);
  const Timestamp until = now + TimeDelta::Seconds(10);
  while (!done && now < until) {
    const Timestamp t = std::min({session->NextProcessTime(), net.NextEvent(), server.NextProcessTime(), until});
    now = std::max(now, t);
    session->Process(now);
    server.Process(now);
    client.Deliver(now);
    turn_int.Deliver(now);
    turn_ext.Deliver(now);
  }
  SM_EXPECT_TRUE(done);
  const Candidate* relay = nullptr;
  for (const Candidate& c : candidates) {
    if (c.is_relay()) relay = &c;
  }
  SM_ASSERT_TRUE(relay != nullptr);
  SM_EXPECT_TRUE(relay->address().ipaddr() == IpAddress(kTurnExtIp));
  SM_EXPECT_TRUE(relay->related_address().ipaddr() == IpAddress(kNatIp));
  SM_EXPECT_EQ(relay->url(), std::string("turn:99.99.99.3:3478?transport=udp"));
  SM_EXPECT_EQ(server.allocation_count(), size_t{1});
}

// TurnServerConnectionTest.ComparisonOperators is a property of the server's
// allocation key (source, socket's remote address, protocol): two clients
// from the same address on different protocols are different allocations.
// Here: different source ports are different allocations.
SM_TEST(P2pTurnServer, OneAllocationPerClientAddress) {
  TurnFixture f;
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnUdpIntAddr, PROTO_UDP));
  f.turn_port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_ready; }, kSimulatedRtt * 2));
  auto first = std::move(f.turn_port);
  f.turn_ready = false;
  f.CreateTurnPort(kTurnUsername, kTurnPassword, ProtocolAddress(kTurnUdpIntAddr, PROTO_UDP));
  f.turn_port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_ready; }, kSimulatedRtt * 2));
  SM_EXPECT_EQ(f.server.allocation_count(), size_t{2});
  SM_EXPECT_FALSE(first->Candidates()[0].address() == f.turn_port->Candidates()[0].address());
}
