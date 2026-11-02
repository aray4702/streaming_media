// TURN over TCP (issue #1036, C4 part 4): the TCP cases of libwebrtc M157's
// p2p/base/turn_port_unittest.cc (turn_stream_cases.h), the TCP listener of
// p2p/test/turn_server.cc, and the TURN REST credentials helper.
#include "turn_stream_cases.h"

namespace {
constexpr ProtocolType kProto = PROTO_TCP;
}  // namespace

// TestTurnTcpAllocate, TestReconstructedServerUrlForTcp.
SM_TEST(P2pTurnPortTcp, Allocate) {
  TurnFixture f;
  turn_stream::Allocate(f, kProto);
}

// TestTurnTcpAllocateMismatch.
SM_TEST(P2pTurnPortTcp, AllocateMismatch) {
  TurnFixture f;
  turn_stream::AllocateMismatch(f, kProto);
}

// TestPrivateIpTurnCandidateErrorAddressScrubbedTcp.
SM_TEST(P2pTurnPortTcp, PrivateServerErrorAddressScrubbed) {
  TurnFixture f;
  turn_stream::ErrorAddressScrubbing(f, kProto, /*private_server=*/true);
}

// TestPublicIpTurnCandidateErrorAddressNotScrubbedTcp.
SM_TEST(P2pTurnPortTcp, PublicServerErrorAddressNotScrubbed) {
  TurnFixture f;
  turn_stream::ErrorAddressScrubbing(f, kProto, /*private_server=*/false);
}

SM_TEST(P2pTurnPortTcp, ConnectionRefused) {
  TurnFixture f;
  turn_stream::ConnectionRefused(f, kProto);
}

// TestTurnAlternateServerTCP.
SM_TEST(P2pTurnPortTcp, AlternateServer) {
  TurnFixture f;
  turn_stream::AlternateServer(f, kProto);
}

// TestTurnAlternateServerPingPongTCP, ...DetectRepetitionTCP,
// ...LoopbackTcpIpv4, ...AnyAddressTcpIpv4.
SM_TEST(P2pTurnPortTcp, AlternateServerRefused) { turn_stream::AlternateServerRefusedCases(kProto, nullptr); }

// TestTurnTcpConnection.
SM_TEST(P2pTurnPortTcp, Connection) {
  TurnFixture f;
  turn_stream::Connection(f, kProto);
}

// TestTurnSendDataTurnTcpToUdp.
SM_TEST(P2pTurnPortTcp, SendData) {
  TurnFixture f;
  turn_stream::SendData(f, kProto);
}

// TestTurnTCPReleaseAllocation.
SM_TEST(P2pTurnPortTcp, ReleaseAllocation) {
  TurnFixture f;
  turn_stream::ReleaseAllocation(f, kProto);
}

// TestTurnTCPGracefulReleaseAllocation.
SM_TEST(P2pTurnPortTcp, GracefulReleaseAllocation) {
  TurnFixture f;
  turn_stream::GracefulReleaseAllocation(f, kProto);
}

SM_TEST(P2pTurnPortTcp, ConnectionClosedAfterAllocation) {
  TurnFixture f;
  turn_stream::ConnectionClosedAfterAllocation(f, kProto);
}

SM_TEST(P2pTurnPortTcp, AllocatorGathersRelayWithUdpBlocked) {
  TurnFixture f;
  turn_stream::AllocatorGathersRelayWithUdpBlocked(f, kProto);
}

// The REST API's credentials: username "<expiry>:<user>", password
// base64(HMAC-SHA1(secret, username)). The expected passwords are Python's
// base64.b64encode(hmac.new(secret, username, hashlib.sha1).digest()).
SM_TEST(P2pTurnRestCredentials, UsernameAndPassword) {
  InstallTurnTestCrypto();
  const RelayCredentials c = CreateTurnRestCredentials("north", "ai-server", 1700000000);
  SM_EXPECT_EQ(c.username, std::string("1700000000:ai-server"));
  SM_EXPECT_EQ(c.password, std::string("kjPjQRM31dMMKA7SOSzPS102/8c="));
  // Without a user, the username is the expiry alone.
  const RelayCredentials e = CreateTurnRestCredentials("north", "", 1700000000);
  SM_EXPECT_EQ(e.username, std::string("1700000000"));
  SM_EXPECT_EQ(e.password, std::string("CWyHi3zCeWqXBir9thl4m+iZPRY="));
}

// The server checks them as long-term credentials: a TURN port with REST
// credentials allocates when the server's key is the REST password's.
SM_TEST(P2pTurnRestCredentials, AllocateOverTcp) {
  TurnFixture f;
  const RelayCredentials c = CreateTurnRestCredentials("north", "ai-server", 1700000000);
  class RestAuth final : public sim::TurnAuthInterface {
   public:
    explicit RestAuth(std::string secret) : secret_(std::move(secret)) {}
    bool GetKey(std::string_view username, std::string_view realm, std::string* key) override {
      // coturn's use-auth-secret: the password follows from the username.
      return ComputeStunCredentialHash(std::string(username), std::string(realm), DerivePassword(username), key);
    }
    std::string DerivePassword(std::string_view username) const {
      const size_t colon = username.find(':');
      const int64_t expiry = std::stoll(std::string(username.substr(0, colon)));
      const std::string user = colon == std::string_view::npos ? "" : std::string(username.substr(colon + 1));
      return CreateTurnRestCredentials(secret_, user, expiry).password;
    }

   private:
    std::string secret_;
  };
  RestAuth auth("north");
  f.server.set_auth_hook(&auth);
  turn_stream::AddServer(f, f.turn_tcp, kTurnTcpIntAddr, kProto);
  f.CreateTurnPort(c.username, c.password, ProtocolAddress(kTurnTcpIntAddr, kProto));
  f.turn_port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.turn_ready; }, TimeToGetTurnCandidate(kProto)));
  SM_EXPECT_EQ(f.server.allocation_count(), size_t{1});
  // A wrong secret is refused.
  TurnFixture g;
  RestAuth other("south");
  g.server.set_auth_hook(&other);
  turn_stream::AddServer(g, g.turn_tcp, kTurnTcpIntAddr, kProto);
  g.CreateTurnPort(c.username, c.password, ProtocolAddress(kTurnTcpIntAddr, kProto));
  g.turn_port->PrepareAddress(g.now);
  SM_ASSERT_TRUE(g.WaitUntil([&] { return g.turn_error; }, TimeToGetTurnCandidate(kProto)));
  SM_EXPECT_EQ(g.error_event.error_code, static_cast<int>(STUN_ERROR_UNAUTHORIZED));
}
