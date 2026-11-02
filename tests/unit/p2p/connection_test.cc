// ICE candidate pairs (issue #1000): the cases of libwebrtc M157's p2p/base/connection_unittest.cc and the
// connection cases of port_unittest.cc (compare/webrtc/reference/p2p/base/), with the library's time model in place
// of the simulated clock, plus checks between two UDP ports over the emulated network (#985), with and without loss,
// through an outage (consent freshness) and on a NAT (peer-reflexive candidates).
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sm/core/random.h"
#include "sm/core/units.h"
#include "sm/p2p/byte_buffer.h"
#include "sm/p2p/candidate.h"
#include "sm/p2p/connection.h"
#include "sm/p2p/network.h"
#include "sm/p2p/port.h"
#include "sm/p2p/stun.h"
#include "sm/p2p/stun_port.h"
#include "sm/sim/emulated_sockets.h"
#include "sm_test.h"
#include "test_crypto.h"

using namespace sm;
using namespace sm::p2p;

namespace {

SocketAddress Addr(std::string_view s) { return *SocketAddress::FromString(s); }

const SocketAddress kLocalAddr1 = Addr("192.168.1.2:0");
const SocketAddress kLocalAddr2 = Addr("192.168.1.3:0");
constexpr uint64_t kTiebreaker1 = 11111;
constexpr uint64_t kTiebreaker2 = 22222;
const TimeDelta kEpsilon = TimeDelta::Millis(1);
const Timestamp kStart = Timestamp::Seconds(100);

Network MakeNetwork(const SocketAddress& addr) {
  Network n("unittest", addr.ipaddr(), 32, ADAPTER_TYPE_UNKNOWN);
  n.AddIp(addr.ipaddr());
  return n;
}

class NullFactory final : public PacketSocketFactory {
 public:
  std::unique_ptr<AsyncPacketSocket> CreateUdpSocket(const SocketAddress&, uint16_t, uint16_t) override {
    return nullptr;
  }
};

// p2p/test/test_port.h: a host port whose STUN sends are parsed and kept (the last one), and whose connections send
// USE-CANDIDATE.
class TestPort final : public Port {
 public:
  TestPort(Timestamp now, const Parameters& args) : Port(args, IceCandidateType::kHost, 0, 0, false, now) {}
  using Port::GetStunMessage;

  void set_type_preference(uint32_t p) { type_preference_ = p; }
  void AddCandidateAddress(const SocketAddress& addr) {
    AddAddress(addr, addr, SocketAddress(), "udp", "", "", Type(), type_preference_, 0, "", false);
  }
  bool SupportsProtocol(std::string_view protocol) const override { return protocol == UDP_PROTOCOL_NAME; }
  ProtocolType GetProtocol() const override { return PROTO_UDP; }
  void PrepareAddress(Timestamp) override {
    const SocketAddress addr(network()->GetBestIP(), 0);
    AddAddress(addr, addr, SocketAddress(), "udp", "", "", IceCandidateType::kHost, ICE_TYPE_PREFERENCE_HOST, 0, "",
               true);
  }
  Connection* CreateConnection(Timestamp now, const Candidate& remote_candidate, CandidateOrigin) override {
    auto conn = std::make_unique<ProxyConnection>(now, this, 0, remote_candidate);
    Connection* raw = conn.get();
    AddOrReplaceConnection(std::move(conn));
    // Set use-candidate attribute flag as this will add USE-CANDIDATE attribute in STUN binding requests.
    raw->set_use_candidate_attr(true);
    return raw;
  }
  int SendTo(Timestamp, std::span<const uint8_t> data, const SocketAddress&, const PacketOptions&,
             bool payload) override {
    if (!payload) {
      auto msg = std::make_unique<IceMessage>();
      ByteBufferReader buf(data);
      if (!msg->Read(&buf)) return -1;
      last_stun_buf.assign(data.begin(), data.end());
      last_stun_msg = std::move(msg);
    }
    return static_cast<int>(data.size());
  }
  int SetOption(SocketOption, int) override { return 0; }
  int GetOption(SocketOption, int*) override { return -1; }
  int GetError() override { return 0; }
  void Reset() {
    last_stun_buf.clear();
    last_stun_msg.reset();
  }

  std::vector<uint8_t> last_stun_buf;
  std::unique_ptr<IceMessage> last_stun_msg;

 private:
  uint32_t type_preference_ = 0;
};

ReceivedPacket Packet(const std::vector<uint8_t>& data, Timestamp now) {
  return ReceivedPacket{std::span<const uint8_t>(data), SocketAddress(), now};
}

// ConnectionTest: two test ports, controlling and controlled.
struct ConnectionFixture {
  Network net1 = MakeNetwork(kLocalAddr1);
  Network net2 = MakeNetwork(kLocalAddr2);
  NullFactory factory;
  Timestamp now = kStart;
  int num_state_changes = 0;
  bool role_conflict = false;
  std::unique_ptr<TestPort> lport;
  std::unique_ptr<TestPort> rport;

  explicit ConnectionFixture(Timestamp start = kStart) : now(start) {
    testing::InstallTestCrypto();
    lport = CreateTestPort(&net1, "lfrag", "lpass", kTiebreaker1);
    rport = CreateTestPort(&net2, "rfrag", "rpass", kTiebreaker2);
    lport->SetIceRole(ICEROLE_CONTROLLING);
    rport->SetIceRole(ICEROLE_CONTROLLED);
    lport->PrepareAddress(now);
    rport->PrepareAddress(now);
  }

  std::unique_ptr<TestPort> CreateTestPort(Network* network, std::string ufrag, std::string pwd,
                                           uint64_t tiebreaker) {
    Port::Parameters args;
    args.network = network;
    args.socket_factory = &factory;
    args.ice_username_fragment = std::move(ufrag);
    args.ice_password = std::move(pwd);
    args.ice_tiebreaker = tiebreaker;
    auto port = std::make_unique<TestPort>(now, args);
    port->SubscribeRoleConflict([this] { role_conflict = true; });
    return port;
  }

  Connection* CreateConnection(IceRole role) {
    Connection* conn = role == ICEROLE_CONTROLLING
                           ? lport->CreateConnection(now, rport->Candidates()[0], Port::ORIGIN_MESSAGE)
                           : rport->CreateConnection(now, lport->Candidates()[0], Port::ORIGIN_MESSAGE);
    conn->SubscribeStateChange(this, [this](Connection*) { num_state_changes++; });
    return conn;
  }

  TestPort* PortOf(Connection* conn) { return conn->PortForTest() == lport.get() ? lport.get() : rport.get(); }

  // Pings from `lconn`, delivers the check to `rconn` and returns its answer `ms` later.
  std::vector<uint8_t> SendPingAndCaptureReply(Connection* lconn, Connection* rconn, int64_t ms) {
    TestPort* lp = PortOf(lconn);
    TestPort* rp = PortOf(rconn);
    lp->Reset();
    rp->Reset();
    lconn->Ping(now);
    if (lp->last_stun_buf.empty()) return {};
    rconn->OnReadPacket(Packet(lp->last_stun_buf, now));
    now += TimeDelta::Millis(ms);
    return rp->last_stun_buf;
  }
  void SendPingAndReceiveResponse(Connection* lconn, Connection* rconn, int64_t ms) {
    const std::vector<uint8_t> reply = SendPingAndCaptureReply(lconn, rconn, ms);
    lconn->OnReadPacket(Packet(reply, now));
  }
};

}  // namespace

// ------------------------------------------------------------ connection_unittest.cc

SM_TEST(P2pConnection, ForgetLearnedState) {
  ConnectionFixture f;
  Connection* lconn = f.CreateConnection(ICEROLE_CONTROLLING);
  Connection* rconn = f.CreateConnection(ICEROLE_CONTROLLED);

  SM_EXPECT_FALSE(lconn->writable());
  SM_EXPECT_FALSE(lconn->receiving());
  SM_EXPECT_TRUE(std::isnan(lconn->GetRttEstimate().GetAverage()));
  SM_EXPECT_EQ(lconn->GetRttEstimate().GetVariance(), std::numeric_limits<double>::infinity());

  f.SendPingAndReceiveResponse(lconn, rconn, 10);
  SM_EXPECT_TRUE(lconn->writable());
  SM_EXPECT_TRUE(lconn->receiving());
  SM_EXPECT_EQ(lconn->GetRttEstimate().GetAverage(), 10.0);
  SM_EXPECT_EQ(lconn->GetRttEstimate().GetVariance(), std::numeric_limits<double>::infinity());

  f.SendPingAndReceiveResponse(lconn, rconn, 11);
  SM_EXPECT_TRUE(lconn->writable());
  SM_EXPECT_TRUE(lconn->receiving());
  SM_EXPECT_NEAR(lconn->GetRttEstimate().GetAverage(), 10.0, 0.5);
  SM_EXPECT_LT(lconn->GetRttEstimate().GetVariance(), std::numeric_limits<double>::infinity());

  lconn->ForgetLearnedState();
  SM_EXPECT_FALSE(lconn->writable());
  SM_EXPECT_FALSE(lconn->receiving());
  SM_EXPECT_TRUE(std::isnan(lconn->GetRttEstimate().GetAverage()));
  SM_EXPECT_EQ(lconn->GetRttEstimate().GetVariance(), std::numeric_limits<double>::infinity());
}

SM_TEST(P2pConnection, ForgetLearnedStateDiscardsPendingPings) {
  ConnectionFixture f;
  Connection* lconn = f.CreateConnection(ICEROLE_CONTROLLING);
  Connection* rconn = f.CreateConnection(ICEROLE_CONTROLLED);
  f.SendPingAndReceiveResponse(lconn, rconn, 10);
  SM_EXPECT_TRUE(lconn->writable());
  SM_EXPECT_TRUE(lconn->receiving());

  const std::vector<uint8_t> reply = f.SendPingAndCaptureReply(lconn, rconn, 10);
  lconn->ForgetLearnedState();
  SM_EXPECT_FALSE(lconn->writable());
  SM_EXPECT_FALSE(lconn->receiving());

  lconn->OnReadPacket(Packet(reply, f.now));
  // That reply was discarded due to the ForgetLearnedState() while it was outstanding.
  SM_EXPECT_FALSE(lconn->writable());
  SM_EXPECT_FALSE(lconn->receiving());

  // But sending a new ping and getting a reply works.
  f.SendPingAndReceiveResponse(lconn, rconn, 11);
  SM_EXPECT_TRUE(lconn->writable());
  SM_EXPECT_TRUE(lconn->receiving());
}

SM_TEST(P2pConnection, ForgetLearnedStateDoesNotTriggerStateChange) {
  // libwebrtc's simulated clock starts at 0, which the connection reads as "never": rconn's check arrives at 0 and
  // doesn't make it receiving, so only lconn's two changes count. Start there too.
  ConnectionFixture f(Timestamp::Millis(0));
  Connection* lconn = f.CreateConnection(ICEROLE_CONTROLLING);
  Connection* rconn = f.CreateConnection(ICEROLE_CONTROLLED);
  SM_EXPECT_EQ(f.num_state_changes, 0);
  f.SendPingAndReceiveResponse(lconn, rconn, 10);
  SM_EXPECT_TRUE(lconn->writable());
  SM_EXPECT_TRUE(lconn->receiving());
  SM_EXPECT_EQ(f.num_state_changes, 2);
  lconn->ForgetLearnedState();
  SM_EXPECT_FALSE(lconn->writable());
  SM_EXPECT_FALSE(lconn->receiving());
  SM_EXPECT_EQ(f.num_state_changes, 2);
}

// Sending a delta and getting a delta ack in response.
SM_TEST(P2pConnection, SendReceiveGoogDelta) {
  ConnectionFixture f;
  Connection* lconn = f.CreateConnection(ICEROLE_CONTROLLING);
  Connection* rconn = f.CreateConnection(ICEROLE_CONTROLLED);
  auto delta = std::make_unique<StunByteStringAttribute>(STUN_ATTR_GOOG_DELTA);
  delta->CopyBytes("DELTA");

  bool received_goog_delta = false;
  bool received_goog_delta_ack = false;
  lconn->SetStunDictConsumer([](const StunByteStringAttribute*) -> std::unique_ptr<StunAttribute> { return nullptr; },
                             [&](const StunUInt64Attribute* ack, GoogDeltaAckError error) {
                               received_goog_delta_ack = true;
                               SM_EXPECT_TRUE(error == GoogDeltaAckError::kNone);
                               SM_EXPECT_TRUE(ack != nullptr && ack->value() == 133u);
                             });
  rconn->SetStunDictConsumer(
      [&](const StunByteStringAttribute* d) -> std::unique_ptr<StunAttribute> {
        received_goog_delta = true;
        SM_EXPECT_EQ(std::string(d->string_view()), std::string("DELTA"));
        return std::make_unique<StunUInt64Attribute>(STUN_ATTR_GOOG_DELTA_ACK, 133);
      },
      [](const StunUInt64Attribute*, GoogDeltaAckError) {});

  lconn->Ping(f.now, std::move(delta));
  SM_ASSERT_TRUE(!f.lport->last_stun_buf.empty());
  rconn->OnReadPacket(Packet(f.lport->last_stun_buf, f.now));
  SM_EXPECT_TRUE(received_goog_delta);
  f.now += TimeDelta::Millis(10);
  SM_ASSERT_TRUE(!f.rport->last_stun_buf.empty());
  lconn->OnReadPacket(Packet(f.rport->last_stun_buf, f.now));
  SM_EXPECT_TRUE(received_goog_delta_ack);
}

// Sending a goog delta and not getting a delta ack in reply gives an error callback.
SM_TEST(P2pConnection, SendGoogDeltaNoReply) {
  ConnectionFixture f;
  Connection* lconn = f.CreateConnection(ICEROLE_CONTROLLING);
  Connection* rconn = f.CreateConnection(ICEROLE_CONTROLLED);
  auto delta = std::make_unique<StunByteStringAttribute>(STUN_ATTR_GOOG_DELTA);
  delta->CopyBytes("DELTA");
  bool received_goog_delta_ack_error = false;
  lconn->SetStunDictConsumer([](const StunByteStringAttribute*) -> std::unique_ptr<StunAttribute> { return nullptr; },
                             [&](const StunUInt64Attribute* ack, GoogDeltaAckError error) {
                               received_goog_delta_ack_error = true;
                               SM_EXPECT_TRUE(ack == nullptr);
                               SM_EXPECT_TRUE(error == GoogDeltaAckError::kUnsupported);
                             });
  lconn->Ping(f.now, std::move(delta));
  rconn->OnReadPacket(Packet(f.lport->last_stun_buf, f.now));
  f.now += TimeDelta::Millis(10);
  lconn->OnReadPacket(Packet(f.rport->last_stun_buf, f.now));
  SM_EXPECT_TRUE(received_goog_delta_ack_error);
}

// If the STUN binding is sufficiently full, aka DELTA is too big, it is not sent.
SM_TEST(P2pConnection, TooBigDeltaIsNotSent) {
  ConnectionFixture f;
  Connection* lconn = f.CreateConnection(ICEROLE_CONTROLLING);
  Connection* rconn = f.CreateConnection(ICEROLE_CONTROLLED);
  auto delta = std::make_unique<StunByteStringAttribute>(STUN_ATTR_GOOG_DELTA);
  delta->CopyBytes(std::string(1200, 'a'));
  bool received_goog_delta = false;
  bool received_goog_delta_ack = false;
  lconn->SetStunDictConsumer([](const StunByteStringAttribute*) -> std::unique_ptr<StunAttribute> { return nullptr; },
                             [&](const StunUInt64Attribute*, GoogDeltaAckError) { received_goog_delta_ack = true; });
  rconn->SetStunDictConsumer(
      [&](const StunByteStringAttribute*) -> std::unique_ptr<StunAttribute> {
        received_goog_delta = true;
        return std::make_unique<StunUInt64Attribute>(STUN_ATTR_GOOG_DELTA_ACK, 133);
      },
      [](const StunUInt64Attribute*, GoogDeltaAckError) {});
  lconn->Ping(f.now, std::move(delta));
  rconn->OnReadPacket(Packet(f.lport->last_stun_buf, f.now));
  SM_EXPECT_FALSE(received_goog_delta);
  f.now += TimeDelta::Millis(10);
  lconn->OnReadPacket(Packet(f.rport->last_stun_buf, f.now));
  SM_EXPECT_FALSE(received_goog_delta_ack);
}

// ------------------------------------------------------------ port_unittest.cc: checks and their attributes

// TestNomination.
SM_TEST(P2pConnection, Nomination) {
  ConnectionFixture f;
  Connection* lconn = f.CreateConnection(ICEROLE_CONTROLLING);
  Connection* rconn = f.CreateConnection(ICEROLE_CONTROLLED);
  const uint32_t nomination = 1234;
  lconn->set_nomination(nomination);
  SM_EXPECT_FALSE(lconn->nominated());
  SM_EXPECT_FALSE(rconn->nominated());
  SM_EXPECT_EQ(lconn->nominated(), lconn->stats(f.now).nominated);
  SM_EXPECT_EQ(rconn->nominated(), rconn->stats(f.now).nominated);
  int nominated_signals = 0;
  rconn->SubscribeNominated(nullptr, [&](Connection*) { ++nominated_signals; });

  // Send ping (including the nomination value) from `lconn` to `rconn`. This should set the remote nomination of
  // `rconn`.
  lconn->Ping(f.now);
  SM_ASSERT_TRUE(f.lport->last_stun_msg != nullptr);
  const StunUInt32Attribute* nom = f.lport->last_stun_msg->GetUInt32(STUN_ATTR_NOMINATION);
  SM_ASSERT_TRUE(nom != nullptr);
  SM_EXPECT_EQ(nom->value(), nomination);
  rconn->OnReadPacket(Packet(f.lport->last_stun_buf, f.now));
  SM_EXPECT_EQ(rconn->remote_nomination(), nomination);
  SM_EXPECT_FALSE(lconn->nominated());
  SM_EXPECT_TRUE(rconn->nominated());
  SM_EXPECT_EQ(nominated_signals, 1);

  // This should result in an acknowledgment sent back from `rconn` to `lconn`, updating the acknowledged
  // nomination of `lconn`.
  lconn->OnReadPacket(Packet(f.rport->last_stun_buf, f.now));
  SM_EXPECT_EQ(lconn->acked_nomination(), nomination);
  SM_EXPECT_TRUE(lconn->nominated());
  SM_EXPECT_TRUE(rconn->nominated());
  SM_EXPECT_EQ(lconn->nominated(), lconn->stats(f.now).nominated);
  SM_EXPECT_EQ(rconn->nominated(), rconn->stats(f.now).nominated);

  // An acknowledged nomination is not sent again; USE-CANDIDATE still is.
  lconn->Ping(f.now);
  SM_EXPECT_TRUE(f.lport->last_stun_msg->GetUInt32(STUN_ATTR_NOMINATION) == nullptr);
  SM_EXPECT_TRUE(f.lport->last_stun_msg->GetByteString(STUN_ATTR_USE_CANDIDATE) != nullptr);
}

// TestRoundTripTime, plus the smoothed RTT (3:1) and its 3 s default.
SM_TEST(P2pConnection, RoundTripTime) {
  ConnectionFixture f;
  Connection* lconn = f.CreateConnection(ICEROLE_CONTROLLING);
  Connection* rconn = f.CreateConnection(ICEROLE_CONTROLLED);
  SM_EXPECT_EQ(lconn->stats(f.now).total_round_trip_time_ms, uint64_t{0});
  SM_EXPECT_FALSE(lconn->stats(f.now).current_round_trip_time_ms.has_value());
  SM_EXPECT_EQ(lconn->Rtt().ms(), int64_t{3000});

  f.SendPingAndReceiveResponse(lconn, rconn, 10);
  SM_EXPECT_EQ(lconn->stats(f.now).total_round_trip_time_ms, uint64_t{10});
  SM_EXPECT_EQ(*lconn->stats(f.now).current_round_trip_time_ms, uint32_t{10});
  SM_EXPECT_EQ(lconn->Rtt().ms(), int64_t{10});

  f.SendPingAndReceiveResponse(lconn, rconn, 20);
  SM_EXPECT_EQ(lconn->stats(f.now).total_round_trip_time_ms, uint64_t{30});
  SM_EXPECT_EQ(*lconn->stats(f.now).current_round_trip_time_ms, uint32_t{20});
  SM_EXPECT_EQ(lconn->Rtt().us(), int64_t{12500});  // (3 * 10 + 20) / 4

  f.SendPingAndReceiveResponse(lconn, rconn, 30);
  SM_EXPECT_EQ(lconn->stats(f.now).total_round_trip_time_ms, uint64_t{60});
  SM_EXPECT_EQ(*lconn->stats(f.now).current_round_trip_time_ms, uint32_t{30});
  SM_EXPECT_EQ(lconn->rtt_samples(), 3);
  SM_EXPECT_EQ(lconn->stats(f.now).recv_ping_responses, size_t{3});
  SM_EXPECT_EQ(rconn->stats(f.now).recv_ping_requests, size_t{3});
  SM_EXPECT_EQ(rconn->stats(f.now).sent_ping_responses, size_t{3});
  SM_EXPECT_EQ(lconn->stats(f.now).sent_ping_requests_total, size_t{3});
  SM_EXPECT_EQ(lconn->stats(f.now).sent_ping_requests_before_first_response, size_t{1});
  // RTT converges after more than kRttRatio + 1 samples.
  SM_EXPECT_FALSE(lconn->stable(f.now));
  f.SendPingAndReceiveResponse(lconn, rconn, 30);
  f.SendPingAndReceiveResponse(lconn, rconn, 30);
  SM_EXPECT_TRUE(lconn->stable(f.now));
}

// TestUseCandidateAttribute, TestLocalToLocalStandard: the check's attributes.
SM_TEST(P2pConnection, BindingRequestAttributes) {
  ConnectionFixture f;
  Connection* lconn = f.CreateConnection(ICEROLE_CONTROLLING);
  lconn->Ping(f.now);
  SM_ASSERT_TRUE(f.lport->last_stun_msg != nullptr);
  IceMessage* msg = f.lport->last_stun_msg.get();
  SM_EXPECT_EQ(msg->type(), int{STUN_BINDING_REQUEST});
  SM_EXPECT_EQ(std::string(msg->GetByteString(STUN_ATTR_USERNAME)->string_view()), std::string("rfrag:lfrag"));
  const StunUInt64Attribute* controlling = msg->GetUInt64(STUN_ATTR_ICE_CONTROLLING);
  SM_ASSERT_TRUE(controlling != nullptr);
  SM_EXPECT_EQ(controlling->value(), kTiebreaker1);
  SM_EXPECT_TRUE(msg->GetUInt64(STUN_ATTR_ICE_CONTROLLED) == nullptr);
  SM_EXPECT_TRUE(msg->GetByteString(STUN_ATTR_USE_CANDIDATE) != nullptr);
  // PRIORITY: the local priority with the peer-reflexive type preference (110).
  const uint32_t local_priority = f.lport->Candidates()[0].priority();
  SM_EXPECT_EQ(msg->GetUInt32(STUN_ATTR_PRIORITY)->value(), (110u << 24) | (local_priority & 0xFFFFFF));
  SM_EXPECT_EQ(lconn->prflx_priority(), (110u << 24) | (local_priority & 0xFFFFFF));
  SM_EXPECT_TRUE(msg->GetByteString(STUN_ATTR_MESSAGE_INTEGRITY) != nullptr);
  SM_EXPECT_TRUE(msg->GetUInt32(STUN_ATTR_FINGERPRINT) != nullptr);
  SM_EXPECT_TRUE(msg->ValidateMessageIntegrity("rpass") == StunMessage::IntegrityStatus::kIntegrityOk);
  // Without announce (enable_goog_ping is off by default), no GOOG-MISC-INFO; no RETRANSMIT-COUNT unless asked.
  SM_EXPECT_TRUE(msg->GetUInt16List(STUN_ATTR_GOOG_MISC_INFO) == nullptr);
  SM_EXPECT_TRUE(msg->GetUInt32(STUN_ATTR_RETRANSMIT_COUNT) == nullptr);
  SM_EXPECT_TRUE(lconn->state() == IceCandidatePairState::IN_PROGRESS);
  SM_EXPECT_EQ(lconn->num_pings_sent(), 1);

  // The controlled side sends ICE-CONTROLLED and no USE-CANDIDATE.
  Connection* rconn = f.CreateConnection(ICEROLE_CONTROLLED);
  rconn->set_use_candidate_attr(true);
  rconn->Ping(f.now);
  msg = f.rport->last_stun_msg.get();
  SM_EXPECT_TRUE(msg->GetUInt64(STUN_ATTR_ICE_CONTROLLED) != nullptr);
  SM_EXPECT_TRUE(msg->GetByteString(STUN_ATTR_USE_CANDIDATE) == nullptr);

  // RETRANSMIT-COUNT counts the checks without a response, and the response echoes it.
  f.lport->set_send_retransmit_count_attribute(true);
  lconn->Ping(f.now);
  SM_EXPECT_EQ(f.lport->last_stun_msg->GetUInt32(STUN_ATTR_RETRANSMIT_COUNT)->value(), 1u);
  rconn->OnReadPacket(Packet(f.lport->last_stun_buf, f.now));
  SM_EXPECT_EQ(f.rport->last_stun_msg->type(), int{STUN_BINDING_RESPONSE});
  SM_EXPECT_EQ(f.rport->last_stun_msg->GetUInt32(STUN_ATTR_RETRANSMIT_COUNT)->value(), 1u);
  // The response carries XOR-MAPPED-ADDRESS (the address it came from) and is signed with our password.
  const StunAddressAttribute* mapped = f.rport->last_stun_msg->GetAddress(STUN_ATTR_XOR_MAPPED_ADDRESS);
  SM_ASSERT_TRUE(mapped != nullptr);
  SM_EXPECT_TRUE(mapped->GetAddress() == f.lport->Candidates()[0].address());
  SM_EXPECT_TRUE(f.rport->last_stun_msg->ValidateMessageIntegrity("rpass") ==
                 StunMessage::IntegrityStatus::kIntegrityOk);
}

// TestNetworkCostChange.
SM_TEST(P2pConnection, NetworkCostChange) {
  ConnectionFixture f;
  Network shared = MakeNetwork(kLocalAddr1);
  f.lport = f.CreateTestPort(&shared, "lfrag", "lpass", kTiebreaker1);
  f.rport = f.CreateTestPort(&shared, "rfrag", "rpass", kTiebreaker2);
  f.lport->SetIceRole(ICEROLE_CONTROLLING);
  f.rport->SetIceRole(ICEROLE_CONTROLLED);
  f.lport->PrepareAddress(f.now);
  f.rport->PrepareAddress(f.now);
  // Default local port cost is kNetworkCostUnknown.
  SM_EXPECT_EQ(f.lport->network_cost(), int16_t{kNetworkCostUnknown});

  // Add a connection and then change the network type (the owner tells the ports, OnNetworkTypeChanged).
  Connection* lconn = f.lport->CreateConnection(f.now, f.rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  shared.set_type(ADAPTER_TYPE_CELLULAR);
  f.lport->UpdateNetworkCost();
  f.rport->UpdateNetworkCost();
  SM_EXPECT_EQ(f.lport->network_cost(), int16_t{kNetworkCostHigh});
  SM_EXPECT_EQ(lconn->local_candidate().network_cost(), uint16_t{kNetworkCostHigh});

  shared.set_type(ADAPTER_TYPE_WIFI);
  f.lport->UpdateNetworkCost();
  f.rport->UpdateNetworkCost();
  Connection* rconn = f.rport->CreateConnection(f.now, f.lport->Candidates()[0], Port::ORIGIN_MESSAGE);
  shared.set_type(ADAPTER_TYPE_CELLULAR);
  f.lport->UpdateNetworkCost();
  int changes = 0;
  rconn->SubscribeStateChange(nullptr, [&](Connection*) { ++changes; });
  lconn->Ping(f.now);
  // The rconn's remote candidate cost is kNetworkCostLow, but the ping contains an attribute of network cost of
  // kNetworkCostHigh. Once the message is handled in rconn, the remote candidate will have cost kNetworkCostHigh.
  SM_EXPECT_EQ(rconn->remote_candidate().network_cost(), uint16_t{kNetworkCostLow});
  rconn->OnReadPacket(Packet(f.lport->last_stun_buf, f.now));
  SM_EXPECT_EQ(rconn->remote_candidate().network_cost(), uint16_t{kNetworkCostHigh});
  SM_EXPECT_GE(changes, 1);
  SM_EXPECT_EQ(rconn->ComputeNetworkCost(), uint32_t{kNetworkCostLow} + kNetworkCostHigh);
}

// TestNetworkInfoAttribute.
SM_TEST(P2pConnection, NetworkInfoAttribute) {
  ConnectionFixture f;
  Network shared = MakeNetwork(kLocalAddr1);
  f.lport = f.CreateTestPort(&shared, "lfrag", "lpass", kTiebreaker1);
  f.rport = f.CreateTestPort(&shared, "rfrag", "rpass", kTiebreaker2);
  f.lport->SetIceRole(ICEROLE_CONTROLLING);
  f.rport->SetIceRole(ICEROLE_CONTROLLED);
  shared.set_id(9);
  f.lport->PrepareAddress(f.now);
  f.rport->PrepareAddress(f.now);
  Connection* lconn = f.lport->CreateConnection(f.now, f.rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  lconn->Ping(f.now);
  uint32_t network_info = f.lport->last_stun_msg->GetUInt32(STUN_ATTR_GOOG_NETWORK_INFO)->value();
  SM_EXPECT_EQ(network_info >> 16, 9u);
  SM_EXPECT_EQ(network_info & 0xFFFF, uint32_t{kNetworkCostUnknown});

  shared.set_type(ADAPTER_TYPE_CELLULAR);
  f.rport->UpdateNetworkCost();
  shared.set_id(8);
  Connection* rconn = f.rport->CreateConnection(f.now, f.lport->Candidates()[0], Port::ORIGIN_MESSAGE);
  rconn->Ping(f.now);
  network_info = f.rport->last_stun_msg->GetUInt32(STUN_ATTR_GOOG_NETWORK_INFO)->value();
  SM_EXPECT_EQ(network_info >> 16, 8u);
  SM_EXPECT_EQ(network_info & 0xFFFF, uint32_t{kNetworkCostHigh});
}

// TestConnectionPriority.
SM_TEST(P2pConnection, Priority) {
  ConnectionFixture f;
  f.lport = f.CreateTestPort(&f.net1, "lfrag", "lpass", kTiebreaker1);
  f.rport = f.CreateTestPort(&f.net2, "rfrag", "rpass", kTiebreaker2);
  f.lport->set_type_preference(ICE_TYPE_PREFERENCE_HOST);
  f.rport->set_type_preference(ICE_TYPE_PREFERENCE_RELAY_UDP);
  f.lport->set_component(123);
  f.lport->AddCandidateAddress(Addr("192.168.1.4:1234"));
  f.rport->set_component(23);
  f.rport->AddCandidateAddress(Addr("10.1.1.100:1234"));
  f.rport->set_type_preference(ICE_TYPE_PREFERENCE_RELAY_DTLS);
  f.rport->AddCandidateAddress(Addr("10.1.1.100:1234"));
  SM_EXPECT_EQ(f.lport->Candidates()[0].priority(), 0x7E001E85u);
  SM_EXPECT_EQ(f.rport->Candidates()[0].priority(), 0x3001EE9u);
  SM_EXPECT_EQ(f.rport->Candidates()[1].priority(), 0x2001EE9u);

  // pair priority = 2^32*MIN(G,D) + 2*MAX(G,D) + (G>D?1:0)
  f.lport->SetIceRole(ICEROLE_CONTROLLING);
  f.rport->SetIceRole(ICEROLE_CONTROLLED);
  Connection* lconn = f.lport->CreateConnection(f.now, f.rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  SM_EXPECT_EQ(lconn->priority(), uint64_t{0x3001EE9FC003D0B});
  lconn = f.lport->CreateConnection(f.now, f.rport->Candidates()[1], Port::ORIGIN_MESSAGE);
  SM_EXPECT_EQ(lconn->priority(), uint64_t{0x2001EE9FC003D0B});

  f.lport->SetIceRole(ICEROLE_CONTROLLED);
  f.rport->SetIceRole(ICEROLE_CONTROLLING);
  Connection* rconn = f.rport->CreateConnection(f.now, f.lport->Candidates()[0], Port::ORIGIN_MESSAGE);
  SM_EXPECT_EQ(rconn->priority(), uint64_t{0x3001EE9FC003D0A});
  // No role, no priority.
  f.rport->SetIceRole(ICEROLE_UNKNOWN);
  SM_EXPECT_EQ(rconn->priority(), uint64_t{0});
}

// ------------------------------------------------------------ port_unittest.cc: role conflicts and errors

// TestLoopbackCall: our own ufrag and tie-breaker is no conflict; a lower tie-breaker gets 487.
SM_TEST(P2pConnection, LoopbackCall) {
  ConnectionFixture f;
  Network net = MakeNetwork(kLocalAddr1);
  auto lport = f.CreateTestPort(&net, "lfrag", "lpass", kTiebreaker1);
  lport->SetIceRole(ICEROLE_CONTROLLING);
  lport->PrepareAddress(f.now);
  Connection* conn = lport->CreateConnection(f.now, lport->Candidates()[0], Port::ORIGIN_MESSAGE);
  conn->Ping(f.now);
  SM_ASSERT_TRUE(lport->last_stun_msg != nullptr);
  SM_EXPECT_EQ(lport->last_stun_msg->type(), int{STUN_BINDING_REQUEST});
  conn->OnReadPacket(Packet(lport->last_stun_buf, f.now));
  SM_EXPECT_EQ(lport->last_stun_msg->type(), int{STUN_BINDING_RESPONSE});

  // If the tiebreaker value is different from port, we expect an error response.
  lport->Reset();
  lport->AddCandidateAddress(kLocalAddr2);
  // Creating a different connection as `conn` is receiving.
  Connection* conn1 = lport->CreateConnection(f.now, lport->Candidates()[1], Port::ORIGIN_MESSAGE);
  conn1->Ping(f.now);
  SM_ASSERT_TRUE(lport->last_stun_msg != nullptr);
  const std::string username(lport->last_stun_msg->GetByteString(STUN_ATTR_USERNAME)->string_view());
  IceMessage modified_req(STUN_BINDING_REQUEST);
  modified_req.AddAttribute(std::make_unique<StunByteStringAttribute>(STUN_ATTR_USERNAME, username));
  // To make sure we receive error response, adding tiebreaker less than what's present in request.
  modified_req.AddAttribute(
      std::make_unique<StunUInt64Attribute>(STUN_ATTR_ICE_CONTROLLING, lport->IceTiebreaker() - 1));
  modified_req.AddMessageIntegrity("lpass");
  modified_req.AddFingerprint();
  lport->Reset();
  ByteBufferWriter buf;
  modified_req.Write(&buf);
  const std::vector<uint8_t> bytes(buf.DataView().begin(), buf.DataView().end());
  conn1->OnReadPacket(Packet(bytes, f.now));
  SM_ASSERT_TRUE(lport->last_stun_msg != nullptr);
  SM_EXPECT_EQ(lport->last_stun_msg->type(), int{STUN_BINDING_ERROR_RESPONSE});
  SM_EXPECT_EQ(lport->last_stun_msg->GetErrorCodeValue(), int{STUN_ERROR_ROLE_CONFLICT});
  // The losing check is not counted as received.
  SM_EXPECT_EQ(conn1->stats(f.now).recv_ping_requests, size_t{0});
}

// TestIceRoleConflict: both controlling; the side with the lower tie-breaker answers and signals the conflict.
SM_TEST(P2pConnection, IceRoleConflict) {
  ConnectionFixture f;
  f.rport->SetIceRole(ICEROLE_CONTROLLING);
  Connection* lconn = f.lport->CreateConnection(f.now, f.rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  Connection* rconn = f.rport->CreateConnection(f.now, f.lport->Candidates()[0], Port::ORIGIN_MESSAGE);
  rconn->Ping(f.now);
  SM_EXPECT_EQ(f.rport->last_stun_msg->type(), int{STUN_BINDING_REQUEST});
  // Send rport binding request to lport.
  lconn->OnReadPacket(Packet(f.rport->last_stun_buf, f.now));
  SM_ASSERT_TRUE(f.lport->last_stun_msg != nullptr);
  SM_EXPECT_EQ(f.lport->last_stun_msg->type(), int{STUN_BINDING_RESPONSE});
  SM_EXPECT_TRUE(f.role_conflict);
}

// A 487 answer raises the role conflict signal and keeps the connection; other errors fail it and destroy it at
// the port's next Process; 401 is recoverable.
SM_TEST(P2pConnection, ErrorResponses) {
  ConnectionFixture f;
  Connection* lconn = f.CreateConnection(ICEROLE_CONTROLLING);
  int destroyed = 0;
  lconn->SubscribeDestroyed(nullptr, [&](Connection*) { ++destroyed; });
  auto answer = [&](int code) {
    lconn->Ping(f.now);
    const IceMessage& req = *f.lport->last_stun_msg;
    StunMessage response(STUN_BINDING_ERROR_RESPONSE, req.transaction_id());
    auto error = StunAttribute::CreateErrorCode();
    error->SetCode(code);
    response.AddAttribute(std::move(error));
    response.AddMessageIntegrity("rpass");
    response.AddFingerprint();
    ByteBufferWriter buf;
    response.Write(&buf);
    const std::vector<uint8_t> bytes(buf.DataView().begin(), buf.DataView().end());
    lconn->OnReadPacket(Packet(bytes, f.now));
  };
  answer(STUN_ERROR_UNAUTHORIZED);
  SM_EXPECT_FALSE(f.role_conflict);
  SM_EXPECT_EQ(destroyed, 0);
  answer(STUN_ERROR_ROLE_CONFLICT);
  SM_EXPECT_TRUE(f.role_conflict);
  SM_EXPECT_EQ(destroyed, 0);
  SM_EXPECT_TRUE(f.lport->GetConnection(lconn->remote_candidate().address()) == lconn);
  answer(STUN_ERROR_BAD_REQUEST);
  SM_EXPECT_EQ(destroyed, 1);
  SM_EXPECT_TRUE(f.lport->connections().empty());
  // Deleted at the port's next Process, which is due now.
  SM_EXPECT_TRUE(f.lport->NextProcessTime() <= f.now);
  f.lport->Process(f.now);
}

// A response signed with the wrong password, and a check from the wrong remote ufrag, are refused.
SM_TEST(P2pConnection, BadIntegrityAndRemoteUsername) {
  ConnectionFixture f;
  Connection* lconn = f.CreateConnection(ICEROLE_CONTROLLING);
  Connection* rconn = f.CreateConnection(ICEROLE_CONTROLLED);
  lconn->set_remote_password_for_test("wrong");
  f.SendPingAndReceiveResponse(lconn, rconn, 10);
  // The check was signed with "wrong", so rport refused it with 401 and lconn stays unwritable.
  SM_EXPECT_EQ(f.rport->last_stun_msg->type(), int{STUN_BINDING_ERROR_RESPONSE});
  SM_EXPECT_EQ(f.rport->last_stun_msg->GetErrorCodeValue(), int{STUN_ERROR_UNAUTHORIZED});
  SM_EXPECT_FALSE(lconn->writable());

  // Signed right, answered, but the answer is checked with the wrong remote password: dropped.
  lconn->set_remote_password_for_test("rpass");
  const std::vector<uint8_t> reply = f.SendPingAndCaptureReply(lconn, rconn, 10);
  lconn->set_remote_password_for_test("wrong");
  lconn->OnReadPacket(Packet(reply, f.now));
  SM_EXPECT_FALSE(lconn->writable());

  // A check whose remote ufrag is not the remote candidate's gets 401.
  ConnectionFixture g;
  Network other = MakeNetwork(kLocalAddr1);
  auto xport = g.CreateTestPort(&other, "xfrag", "lpass", kTiebreaker1);
  xport->SetIceRole(ICEROLE_CONTROLLING);
  xport->PrepareAddress(g.now);
  Connection* rc = g.rport->CreateConnection(g.now, g.lport->Candidates()[0], Port::ORIGIN_MESSAGE);
  Connection* xc = xport->CreateConnection(g.now, g.rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  xc->Ping(g.now);
  rc->OnReadPacket(Packet(xport->last_stun_buf, g.now));
  SM_EXPECT_EQ(g.rport->last_stun_msg->type(), int{STUN_BINDING_ERROR_RESPONSE});
  SM_EXPECT_EQ(g.rport->last_stun_msg->GetErrorCodeValue(), int{STUN_ERROR_UNAUTHORIZED});
  SM_EXPECT_FALSE(rc->receiving());
}

// A binding indication marks the pair receiving without an answer.
SM_TEST(P2pConnection, BindingIndication) {
  ConnectionFixture f;
  Connection* rconn = f.CreateConnection(ICEROLE_CONTROLLED);
  StunMessage ind(STUN_BINDING_INDICATION);
  ind.AddFingerprint();
  ByteBufferWriter buf;
  ind.Write(&buf);
  const std::vector<uint8_t> bytes(buf.DataView().begin(), buf.DataView().end());
  rconn->OnReadPacket(Packet(bytes, f.now));
  SM_EXPECT_TRUE(rconn->receiving());
  SM_EXPECT_TRUE(rconn->LastPingReceived() == f.now);
  SM_EXPECT_TRUE(f.rport->last_stun_msg == nullptr);
}

// ------------------------------------------------------------ port_unittest.cc: write states and death

// TestWritableState, with the checks delivered by hand: unreliable after 5 unanswered checks and 5 s, timed out after
// 15 s, and a response brings it back.
SM_TEST(P2pConnection, WritableState) {
  ConnectionFixture f;
  Connection* lconn = f.CreateConnection(ICEROLE_CONTROLLING);
  Connection* rconn = f.CreateConnection(ICEROLE_CONTROLLED);
  SM_EXPECT_TRUE(lconn->write_state() == Connection::STATE_WRITE_INIT);
  // Data can be sent before the connection is writable.
  const std::vector<uint8_t> data = {'a', 'b', 'c', 'd', 0};
  SM_EXPECT_EQ(lconn->Send(f.now, data, PacketOptions()), 5);
  f.SendPingAndReceiveResponse(lconn, rconn, 1);
  SM_EXPECT_TRUE(lconn->write_state() == Connection::STATE_WRITABLE);

  // 5 pings go unanswered; the estimate is 2 * RTT clamped to [100 ms, 60 s] = 100 ms.
  const Timestamp t0 = f.now;
  for (int i = 1; i <= kConnectionWriteConnectFailures; ++i) lconn->Ping(t0 + TimeDelta::Millis(i));
  lconn->UpdateState(t0 + kConnectionWriteConnectTimeout);
  SM_EXPECT_TRUE(lconn->write_state() == Connection::STATE_WRITABLE);  // Not past 5 s since the first.
  lconn->UpdateState(t0 + kConnectionWriteConnectTimeout + TimeDelta::Millis(2));
  SM_EXPECT_TRUE(lconn->write_state() == Connection::STATE_WRITE_UNRELIABLE);
  SM_EXPECT_EQ(lconn->Send(f.now, data, PacketOptions()), 5);

  // Timed out 15 s after the first unanswered check.
  lconn->UpdateState(t0 + kConnectionWriteTimeout);
  SM_EXPECT_TRUE(lconn->write_state() == Connection::STATE_WRITE_UNRELIABLE);
  lconn->UpdateState(t0 + kConnectionWriteTimeout + TimeDelta::Millis(2));
  SM_EXPECT_TRUE(lconn->write_state() == Connection::STATE_WRITE_TIMEOUT);
  SM_EXPECT_FALSE(lconn->active());
  // Even if the connection has timed out, the Connection shouldn't block the sending of data.
  SM_EXPECT_EQ(lconn->Send(f.now, data, PacketOptions()), 5);
  // A check from the other side restarts it (STATE_WRITE_INIT), and an answer makes it writable.
  f.now = t0 + kConnectionWriteTimeout + TimeDelta::Seconds(1);
  rconn->Ping(f.now);
  lconn->OnReadPacket(Packet(f.rport->last_stun_buf, f.now));
  SM_EXPECT_TRUE(lconn->write_state() == Connection::STATE_WRITE_INIT);
  f.SendPingAndReceiveResponse(lconn, rconn, 1);
  SM_EXPECT_TRUE(lconn->write_state() == Connection::STATE_WRITABLE);
}

// TestWritableStateWithConfiguredThreshold.
SM_TEST(P2pConnection, WritableStateWithConfiguredThreshold) {
  ConnectionFixture f;
  Connection* lconn = f.CreateConnection(ICEROLE_CONTROLLING);
  Connection* rconn = f.CreateConnection(ICEROLE_CONTROLLED);
  f.SendPingAndReceiveResponse(lconn, rconn, 1);
  SM_EXPECT_TRUE(lconn->write_state() == Connection::STATE_WRITABLE);
  lconn->SetUnwritableTimeout(TimeDelta::Seconds(1));
  lconn->set_unwritable_min_checks(3);
  const Timestamp t0 = f.now;
  // Send two checks.
  lconn->Ping(t0 + TimeDelta::Millis(1));
  lconn->Ping(t0 + TimeDelta::Millis(2));
  // We have not reached the timeout nor have we sent the minimum number of checks.
  lconn->UpdateState(t0 + TimeDelta::Seconds(1) - kEpsilon);
  SM_EXPECT_TRUE(lconn->write_state() == Connection::STATE_WRITABLE);
  // We have not sent the minimum number of checks without responses.
  lconn->UpdateState(t0 + TimeDelta::Seconds(1) + TimeDelta::Millis(200));
  SM_EXPECT_TRUE(lconn->write_state() == Connection::STATE_WRITABLE);
  // Last ping after which the candidate pair should become Unreliable after timeout.
  lconn->Ping(t0 + TimeDelta::Millis(3));
  lconn->UpdateState(t0 + TimeDelta::Seconds(1) - kEpsilon);
  SM_EXPECT_TRUE(lconn->write_state() == Connection::STATE_WRITABLE);
  lconn->UpdateState(t0 + TimeDelta::Seconds(1) + TimeDelta::Millis(200));
  SM_EXPECT_TRUE(lconn->write_state() == Connection::STATE_WRITE_UNRELIABLE);
}

// TestTimeoutForNeverWritable: never answered, it times out after 15 s and, never having received anything and no
// longer active, is dead past kMinConnectionLifetime.
SM_TEST(P2pConnection, TimeoutForNeverWritable) {
  ConnectionFixture f;
  Connection* lconn = f.CreateConnection(ICEROLE_CONTROLLING);
  for (int i = 1; i <= kConnectionWriteConnectFailures; ++i) {
    f.now += TimeDelta::Millis(1);
    lconn->Ping(f.now);
  }
  f.now += kConnectionWriteTimeout;
  lconn->UpdateState(f.now);
  // The connection should be destroyed because no data received before the write timeout.
  SM_EXPECT_TRUE(f.lport->connections().empty());
  f.lport->Process(f.now);
}

// TestConnectionDead.
SM_TEST(P2pConnection, ConnectionDead) {
  ConnectionFixture f;
  const SocketAddress raddr = f.rport->Candidates()[0].address();
  // Test case that the connection has never received anything.
  const Timestamp created = f.now;
  Connection* conn = f.lport->CreateConnection(created, f.rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  // It is not dead if it is after kMinConnectionLifetime but not pruned.
  conn->UpdateState(created + kMinConnectionLifetime + kEpsilon);
  f.lport->Process(created);
  SM_EXPECT_TRUE(f.lport->GetConnection(raddr) != nullptr);
  // It is not dead if it is before kMinConnectionLifetime and pruned.
  conn->UpdateState(created + kMinConnectionLifetime - kEpsilon);
  conn->Prune();
  f.lport->Process(created);
  SM_EXPECT_TRUE(f.lport->GetConnection(raddr) != nullptr);
  // It will be dead after kMinConnectionLifetime and pruned.
  conn->UpdateState(created + kMinConnectionLifetime + kEpsilon);
  SM_EXPECT_TRUE(f.lport->GetConnection(raddr) == nullptr);
  f.lport->Process(created + kMinConnectionLifetime + kEpsilon);

  // Test case that the connection has received something. Create a connection again and receive a ping.
  conn = f.lport->CreateConnection(f.now, f.rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  const Timestamp last_receiving = f.now;
  conn->ReceivedPing(last_receiving);
  // The connection will be dead after kDeadConnectionReceiveTimeout.
  conn->UpdateState(last_receiving + kDeadConnectionReceiveTimeout - kEpsilon);
  SM_EXPECT_TRUE(f.lport->GetConnection(raddr) != nullptr);
  conn->UpdateState(last_receiving + kDeadConnectionReceiveTimeout + kEpsilon);
  SM_EXPECT_TRUE(f.lport->GetConnection(raddr) == nullptr);
}

// TestConnectionDeadWithDeadConnectionTimeout.
SM_TEST(P2pConnection, ConnectionDeadWithDeadConnectionTimeout) {
  ConnectionFixture f;
  IceFieldTrials field_trials;
  field_trials.dead_connection_timeout_ms = 90000;
  const SocketAddress raddr = f.rport->Candidates()[0].address();
  Connection* conn = f.lport->CreateConnection(f.now, f.rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  conn->SetIceFieldTrials(&field_trials);
  conn->ReceivedPing(f.now);
  // The connection will be dead after 90s.
  conn->UpdateState(f.now + TimeDelta::Seconds(90) - kEpsilon);
  SM_EXPECT_TRUE(f.lport->GetConnection(raddr) != nullptr);
  conn->UpdateState(f.now + TimeDelta::Seconds(90) + kEpsilon);
  SM_EXPECT_TRUE(f.lport->GetConnection(raddr) == nullptr);
}

// TestConnectionDeadOutstandingPing.
SM_TEST(P2pConnection, ConnectionDeadOutstandingPing) {
  ConnectionFixture f;
  IceFieldTrials field_trials;
  field_trials.dead_connection_timeout_ms = 360000;
  const SocketAddress raddr = f.rport->Candidates()[0].address();
  Connection* conn = f.lport->CreateConnection(f.now, f.rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  conn->SetIceFieldTrials(&field_trials);
  conn->ReceivedPing(f.now);
  const Timestamp send_ping_timestamp = f.now + TimeDelta::Seconds(1);
  conn->Ping(send_ping_timestamp);
  // The connection will be dead 30s after the ping was sent.
  conn->UpdateState(send_ping_timestamp + kDeadConnectionReceiveTimeout - kEpsilon);
  SM_EXPECT_TRUE(f.lport->GetConnection(raddr) != nullptr);
  conn->UpdateState(send_ping_timestamp + kDeadConnectionReceiveTimeout + kEpsilon);
  SM_EXPECT_TRUE(f.lport->GetConnection(raddr) == nullptr);
}

// The receiving state: receiving within 2.5 s of the last packet, or while the last check is answered.
SM_TEST(P2pConnection, ReceivingTimeout) {
  ConnectionFixture f;
  Connection* rconn = f.CreateConnection(ICEROLE_CONTROLLED);
  const std::vector<uint8_t> data = {1, 2, 3, 4, 5};
  int data_packets = 0;
  rconn->RegisterReceivedPacketCallback([&](Connection*, const ReceivedPacket&) { ++data_packets; });
  rconn->OnReadPacket(Packet(data, f.now));
  SM_EXPECT_EQ(data_packets, 1);
  SM_EXPECT_TRUE(rconn->receiving());
  SM_EXPECT_TRUE(rconn->LastDataReceived() == f.now);
  rconn->UpdateState(f.now + kWeakConnectionReceiveTimeout);
  SM_EXPECT_TRUE(rconn->receiving());
  rconn->UpdateState(f.now + kWeakConnectionReceiveTimeout + kEpsilon);
  SM_EXPECT_FALSE(rconn->receiving());
  SM_EXPECT_TRUE(rconn->ReceivingUnchangedSince() == f.now + kWeakConnectionReceiveTimeout + kEpsilon);
  rconn->SetReceivingTimeout(TimeDelta::Seconds(10));
  rconn->UpdateState(f.now + TimeDelta::Seconds(5));
  SM_EXPECT_TRUE(rconn->receiving());
  const ConnectionInfo info = rconn->stats(f.now + TimeDelta::Seconds(5));
  SM_EXPECT_EQ(info.packets_received, size_t{1});
  SM_EXPECT_EQ(info.recv_total_bytes, size_t{5});
}

// TestPortTimeoutAfterNewConnectionCreatedAndDestroyed: the port's timeout restarts when its last connection goes.
SM_TEST(P2pConnection, PortTimeoutAfterConnectionDestroyed) {
  ConnectionFixture f;
  f.lport->set_timeout_delay(10);  // milliseconds
  Connection* conn = f.lport->CreateConnection(f.now, f.rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  // Not destroyed while it has a connection, past the first timeout.
  f.lport->Process(f.now + TimeDelta::Millis(20));
  SM_EXPECT_FALSE(f.lport->dead());
  const Timestamp destroyed_at = f.now + TimeDelta::Millis(30);
  conn->Destroy(destroyed_at);
  SM_EXPECT_TRUE(f.lport->connections().empty());
  f.lport->Process(destroyed_at + TimeDelta::Millis(9));
  SM_EXPECT_FALSE(f.lport->dead());
  f.lport->Process(destroyed_at + TimeDelta::Millis(10));
  SM_EXPECT_TRUE(f.lport->dead());
}

// TestAddConnectionWithSameAddress: a new connection to the same address replaces (and shuts down) the old one.
SM_TEST(P2pConnection, AddConnectionWithSameAddress) {
  ConnectionFixture f;
  Connection* conn1 = f.lport->CreateConnection(f.now, f.rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  int destroyed = 0;
  conn1->SubscribeDestroyed(nullptr, [&](Connection*) { ++destroyed; });
  Connection* conn2 = f.lport->CreateConnection(f.now, f.rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  SM_EXPECT_TRUE(conn1 != conn2);
  SM_EXPECT_EQ(destroyed, 1);
  SM_EXPECT_EQ(f.lport->connections().size(), size_t{1});
  SM_EXPECT_TRUE(f.lport->GetConnection(f.rport->Candidates()[0].address()) == conn2);
  // SetIceParameters reaches the connections' local candidates.
  f.lport->SetIceParameters(2, "newfrag", "newpass");
  SM_EXPECT_EQ(conn2->local_candidate().username(), std::string("newfrag"));
  SM_EXPECT_EQ(conn2->local_candidate().component(), 2);
}

// ------------------------------------------------------------ peer-reflexive candidates

// A response's XOR-MAPPED-ADDRESS that is no local candidate makes the local candidate peer-reflexive, with the
// check's PRIORITY (RFC 5245 7.1.3.2.1), and adds it to the port.
SM_TEST(P2pConnection, LocalPeerReflexiveFromResponse) {
  ConnectionFixture f;
  Connection* lconn = f.CreateConnection(ICEROLE_CONTROLLING);
  int changes = 0;
  lconn->SubscribeStateChange(nullptr, [&](Connection*) { ++changes; });
  lconn->Ping(f.now);
  const IceMessage& req = *f.lport->last_stun_msg;
  const uint32_t sent_priority = req.GetUInt32(STUN_ATTR_PRIORITY)->value();
  const SocketAddress mapped = Addr("5.5.5.5:7000");
  StunMessage response(STUN_BINDING_RESPONSE, req.transaction_id());
  response.AddAttribute(std::make_unique<StunXorAddressAttribute>(STUN_ATTR_XOR_MAPPED_ADDRESS, mapped));
  response.AddMessageIntegrity("rpass");
  response.AddFingerprint();
  ByteBufferWriter buf;
  response.Write(&buf);
  const std::vector<uint8_t> bytes(buf.DataView().begin(), buf.DataView().end());
  const SocketAddress host = f.lport->Candidates()[0].address();
  lconn->OnReadPacket(Packet(bytes, f.now + TimeDelta::Millis(20)));
  SM_EXPECT_TRUE(lconn->writable());
  SM_EXPECT_TRUE(lconn->local_candidate().is_prflx());
  SM_EXPECT_TRUE(lconn->local_candidate().address() == mapped);
  SM_EXPECT_TRUE(lconn->local_candidate().related_address() == host);
  SM_EXPECT_EQ(lconn->local_candidate().priority(), sent_priority);
  SM_ASSERT_EQ(f.lport->Candidates().size(), size_t{2});
  SM_EXPECT_TRUE(f.lport->Candidates()[1].is_prflx());
  SM_EXPECT_GE(changes, 1);
}

// A check from an address without a connection raises the port's unknown-address signal (with the remote ufrag), so
// the ICE transport can make a peer-reflexive remote candidate and a connection; that connection answers the next
// check. A signalled candidate later replaces the peer-reflexive one.
SM_TEST(P2pConnection, RemotePeerReflexiveFromCheck) {
  ConnectionFixture f;
  Connection* lconn = f.CreateConnection(ICEROLE_CONTROLLING);
  Connection* rconn = nullptr;
  f.rport->SubscribeUnknownAddress(nullptr, [&](Timestamp now, Port* port, const SocketAddress& addr, ProtocolType,
                                                IceMessage* msg, const std::string& remote_ufrag, bool) {
    SM_EXPECT_EQ(remote_ufrag, std::string("lfrag"));
    Candidate prflx;
    prflx.set_component(1);
    prflx.set_protocol("udp");
    prflx.set_address(addr);
    prflx.set_username(remote_ufrag);
    prflx.set_type(IceCandidateType::kPrflx);
    prflx.set_priority(msg->GetUInt32(STUN_ATTR_PRIORITY)->value());
    rconn = port->CreateConnection(now, prflx, Port::ORIGIN_MESSAGE);
    rconn->HandleStunBindingOrGoogPingRequest(now, msg);
  });
  lconn->Ping(f.now);
  // No connection to lport's address yet: it goes to the port.
  ReceivedPacket packet = Packet(f.lport->last_stun_buf, f.now);
  packet.source_address = f.lport->Candidates()[0].address();
  SM_EXPECT_TRUE(f.rport->GetConnection(packet.source_address) == nullptr);
  // TestPort exposes the protected Port::OnReadPacket through GetStunMessage's class; call it through a UDP-like path.
  struct Access : Port {
    static void Read(Port* p, const ReceivedPacket& pk) { (p->*(&Access::OnReadPacket))(pk, PROTO_UDP); }
  };
  Access::Read(f.rport.get(), packet);
  SM_ASSERT_TRUE(rconn != nullptr);
  SM_EXPECT_TRUE(rconn->remote_candidate().is_prflx());
  SM_EXPECT_TRUE(rconn->receiving());
  SM_EXPECT_EQ(f.rport->last_stun_msg->type(), int{STUN_BINDING_RESPONSE});
  // The prflx candidate has no password yet: MaybeSetRemoteIceParametersAndGeneration fills it in.
  rconn->MaybeSetRemoteIceParametersAndGeneration(IceParameters{"lfrag", "lpass", false}, 3);
  SM_EXPECT_EQ(rconn->remote_candidate().password(), std::string("lpass"));
  SM_EXPECT_EQ(rconn->remote_candidate().generation(), 3u);
  // The signalled host candidate replaces it.
  Candidate signalled = f.lport->Candidates()[0];
  signalled.set_password("lpass");
  signalled.set_generation(3);
  rconn->MaybeUpdatePeerReflexiveCandidate(signalled);
  SM_EXPECT_TRUE(rconn->remote_candidate().is_local());
}

// ------------------------------------------------------------ GOOG-PING (enable_goog_ping, off by default)

// TestGoogPingAnnounceEnable: with both sides announcing, the second unchanged check is a GOOG-PING, answered with
// GOOG-PING-RESPONSE; a changed check goes back to a binding request; an error drops the cache.
SM_TEST(P2pConnection, GoogPing) {
  ConnectionFixture f;
  IceFieldTrials trials;
  trials.enable_goog_ping = true;
  Connection* lconn = f.CreateConnection(ICEROLE_CONTROLLING);
  Connection* rconn = f.CreateConnection(ICEROLE_CONTROLLED);
  lconn->SetIceFieldTrials(&trials);
  rconn->SetIceFieldTrials(&trials);

  lconn->Ping(f.now);
  SM_ASSERT_TRUE(f.lport->last_stun_msg != nullptr);
  SM_EXPECT_EQ(f.lport->last_stun_msg->type(), int{STUN_BINDING_REQUEST});
  SM_ASSERT_TRUE(f.lport->last_stun_msg->GetUInt16List(STUN_ATTR_GOOG_MISC_INFO) != nullptr);
  rconn->OnReadPacket(Packet(f.lport->last_stun_buf, f.now));
  SM_EXPECT_EQ(f.rport->last_stun_msg->type(), int{STUN_BINDING_RESPONSE});
  const StunUInt16ListAttribute* misc = f.rport->last_stun_msg->GetUInt16List(STUN_ATTR_GOOG_MISC_INFO);
  SM_ASSERT_TRUE(misc != nullptr);
  lconn->OnReadPacket(Packet(f.rport->last_stun_buf, f.now));
  SM_EXPECT_TRUE(lconn->writable());

  // The next check is unchanged: GOOG-PING.
  lconn->Ping(f.now);
  SM_EXPECT_EQ(f.lport->last_stun_msg->type(), int{GOOG_PING_REQUEST});
  rconn->OnReadPacket(Packet(f.lport->last_stun_buf, f.now));
  SM_EXPECT_EQ(f.rport->last_stun_msg->type(), int{GOOG_PING_RESPONSE});
  lconn->OnReadPacket(Packet(f.rport->last_stun_buf, f.now + TimeDelta::Millis(5)));
  SM_EXPECT_EQ(lconn->rtt_samples(), 2);

  // A change in the attributes (a nomination) falls back to a binding request.
  lconn->set_nomination(2);
  lconn->Ping(f.now);
  SM_EXPECT_EQ(f.lport->last_stun_msg->type(), int{STUN_BINDING_REQUEST});

  // Without enable_goog_ping on the sender nothing is announced, and the answer carries no support.
  ConnectionFixture g;
  Connection* l2 = g.CreateConnection(ICEROLE_CONTROLLING);
  Connection* r2 = g.CreateConnection(ICEROLE_CONTROLLED);
  g.SendPingAndReceiveResponse(l2, r2, 5);
  l2->Ping(g.now);
  SM_EXPECT_EQ(g.lport->last_stun_msg->type(), int{STUN_BINDING_REQUEST});
}

// ------------------------------------------------------------ STUN transactions

// Each check is sent once and its transaction ends kConnectionResponseTimeout (60 s) later; a late answer after that
// is ignored, but the outstanding check still counts for the write state.
SM_TEST(P2pConnection, CheckSentOnceAndTimesOut) {
  ConnectionFixture f;
  Connection* lconn = f.CreateConnection(ICEROLE_CONTROLLING);
  Connection* rconn = f.CreateConnection(ICEROLE_CONTROLLED);
  lconn->Ping(f.now);
  const std::vector<uint8_t> check = f.lport->last_stun_buf;
  SM_EXPECT_TRUE(lconn->NextRequestTime() == f.now + kConnectionResponseTimeout);
  f.lport->Reset();
  f.lport->Process(f.now + kConnectionResponseTimeout - kEpsilon);
  SM_EXPECT_TRUE(f.lport->last_stun_msg == nullptr);  // No retransmission.
  f.lport->Process(f.now + kConnectionResponseTimeout);
  SM_EXPECT_TRUE(f.lport->last_stun_msg == nullptr);
  SM_EXPECT_TRUE(lconn->NextRequestTime() == Timestamp::PlusInfinity());
  rconn->OnReadPacket(Packet(check, f.now));
  lconn->OnReadPacket(Packet(f.rport->last_stun_buf, f.now + kConnectionResponseTimeout + kEpsilon));
  SM_EXPECT_FALSE(lconn->writable());
}

// Data goes through Send and the port; stats count it.
SM_TEST(P2pConnection, SendData) {
  ConnectionFixture f;
  Connection* lconn = f.CreateConnection(ICEROLE_CONTROLLING);
  const std::vector<uint8_t> data(100, 7);
  for (int i = 0; i < 10; ++i) SM_EXPECT_EQ(lconn->Send(f.now + TimeDelta::Millis(100 * i), data, PacketOptions()), 100);
  const ConnectionInfo info = lconn->stats(f.now + TimeDelta::Millis(999));
  SM_EXPECT_EQ(info.sent_total_packets, size_t{10});
  SM_EXPECT_EQ(info.sent_total_bytes, size_t{1000});
  SM_EXPECT_EQ(info.sent_bytes_second, size_t{8000});  // 1000 bytes in 1 s, in bits per second.
  SM_EXPECT_TRUE(info.last_data_sent.has_value());
  SM_EXPECT_TRUE(f.lport->last_stun_msg == nullptr);  // Payload, not STUN.
}

// ------------------------------------------------------------ two UDP ports over the emulated network

namespace {

constexpr uint32_t kIpA = sim::SocketAddress::Ip(10, 0, 0, 1);
constexpr uint32_t kIpB = sim::SocketAddress::Ip(10, 0, 0, 2);

sim::LinkConfig Link(int delay_ms, double loss = 0) {
  sim::LinkConfig c;
  c.base_delay = TimeDelta::Millis(delay_ms);
  c.loss_rate = loss;
  return c;
}

// Random transaction ids: the test crypto's counter repeats every 16 ids, fewer than the checks a pair keeps
// outstanding over 60 s, and a repeated id is refused (StunRequestManager::Send).
bool PseudoRandomBytes(std::span<uint8_t> out) {
  static Random rng(0x1000);
  for (auto& b : out) b = static_cast<uint8_t>(rng.Next());
  return true;
}

// Two endpoints, each a UDP port with one connection to the other, pinging at libwebrtc's strong ping interval
// (480 ms) and updating its state every 50 ms (kMinCheckReceivingInterval), as P2PTransportChannel would.
struct CallFixture {
  sim::ByteNetwork net;
  sim::EmulatedSocketServer host_a;
  sim::EmulatedSocketServer host_b;
  Network network_a{"eth0", IpAddress(kIpA), 24};
  Network network_b{"eth0", IpAddress(kIpB), 24};
  std::unique_ptr<UDPPort> port_a;
  std::unique_ptr<UDPPort> port_b;
  Connection* conn_a = nullptr;
  Connection* conn_b = nullptr;
  Timestamp now = Timestamp::Seconds(10);
  Timestamp next_ping = Timestamp::Seconds(10);
  Timestamp next_update = Timestamp::Seconds(10);
  bool pinging = true;
  int data_a = 0;

  CallFixture(double loss, uint64_t seed)
      : net(seed),
        host_a(net, net.AddHost(kIpA, sim::ByteNetwork::kPublicRealm, Link(20, loss), Link(20, loss))),
        host_b(net, net.AddHost(kIpB, sim::ByteNetwork::kPublicRealm, Link(20, loss), Link(20, loss))) {
    testing::InstallTestCrypto();
    StunCrypto crypto = GetStunCrypto();
    crypto.random_bytes = &PseudoRandomBytes;
    SetStunCrypto(crypto);
    network_a.AddIp(IpAddress(kIpA));
    network_b.AddIp(IpAddress(kIpB));
    port_a = MakePort(&network_a, &host_a, "afrag", "apasswordapasswordapassw", 1, ICEROLE_CONTROLLING);
    port_b = MakePort(&network_b, &host_b, "bfrag", "bpasswordbpasswordbpassw", 2, ICEROLE_CONTROLLED);
    Candidate cand_a = port_a->Candidates()[0];
    Candidate cand_b = port_b->Candidates()[0];
    conn_a = port_a->CreateConnection(now, cand_b, Port::ORIGIN_MESSAGE);
    conn_b = port_b->CreateConnection(now, cand_a, Port::ORIGIN_MESSAGE);
    conn_a->set_use_candidate_attr(true);
    conn_a->RegisterReceivedPacketCallback([this](Connection*, const ReceivedPacket&) { ++data_a; });
    conn_a->SubscribeDestroyed(this, [this](Connection*) { conn_a = nullptr; });
    conn_b->SubscribeDestroyed(this, [this](Connection*) { conn_b = nullptr; });
  }

  ~CallFixture() { testing::InstallTestCrypto(); }

  std::unique_ptr<UDPPort> MakePort(Network* network, sim::EmulatedSocketServer* host, std::string ufrag,
                                    std::string pwd, uint64_t tiebreaker, IceRole role) {
    Port::Parameters args;
    args.network = network;
    args.socket_factory = host;
    args.ice_username_fragment = std::move(ufrag);
    args.ice_password = std::move(pwd);
    args.ice_tiebreaker = tiebreaker;
    auto port = UDPPort::Create(now, args, 0, 0, false, std::nullopt);
    port->SetIceRole(role);
    port->KeepAliveUntilPruned();
    port->PrepareAddress(now);
    return port;
  }

  void Run(Timestamp until) {
    while (now < until) {
      Timestamp t = std::min({port_a->NextProcessTime(), port_b->NextProcessTime(), host_a.NextEvent(),
                              host_b.NextEvent(), next_update, pinging ? next_ping : Timestamp::PlusInfinity()});
      if (t > until) {
        now = until;
        break;
      }
      now = std::max(now, t);
      host_a.Deliver(now);
      host_b.Deliver(now);
      port_a->Process(now);
      port_b->Process(now);
      if (pinging && now >= next_ping) {
        if (conn_a) conn_a->Ping(now);
        if (conn_b) conn_b->Ping(now);
        next_ping = now + kStrongPingInterval;
      }
      if (now >= next_update) {
        if (conn_a) conn_a->UpdateState(now);
        if (conn_b) conn_b->UpdateState(now);
        next_update = now + kMinCheckReceivingInterval;
      }
    }
  }
};

}  // namespace

// Both sides become writable and receiving, the controlled side is nominated, the RTT is the path's 80 ms, and data
// flows; the candidate pair's state is SUCCEEDED.
SM_TEST(P2pConnectionCall, ConnectsOverEmulatedNetwork) {
  CallFixture c(0.0, 1);
  c.Run(c.now + TimeDelta::Seconds(2));
  SM_ASSERT_TRUE(c.conn_a != nullptr && c.conn_b != nullptr);
  SM_EXPECT_TRUE(c.conn_a->writable());
  SM_EXPECT_TRUE(c.conn_b->writable());
  SM_EXPECT_TRUE(c.conn_a->receiving());
  SM_EXPECT_TRUE(c.conn_b->receiving());
  SM_EXPECT_TRUE(c.conn_b->nominated());
  SM_EXPECT_TRUE(c.conn_a->nominated());  // The nomination was acknowledged.
  SM_EXPECT_TRUE(c.conn_a->state() == IceCandidatePairState::SUCCEEDED);
  SM_EXPECT_GE(c.conn_a->Rtt().ms(), int64_t{80});
  SM_EXPECT_LE(c.conn_a->Rtt().ms(), int64_t{81});
  SM_EXPECT_TRUE(c.conn_a->local_candidate().is_local());  // No NAT: no peer-reflexive candidate.
  const std::vector<uint8_t> data(200, 1);
  SM_EXPECT_EQ(c.conn_b->Send(c.now, data, PacketOptions()), 200);
  c.Run(c.now + TimeDelta::Millis(100));
  SM_EXPECT_EQ(c.data_a, 1);
}

// With 10 % loss each way, both sides stay writable over 30 s: a lost check makes a pair unreliable only after 5
// unanswered checks and 5 s.
SM_TEST(P2pConnectionCall, StaysWritableWithLoss) {
  for (uint64_t seed = 1; seed <= 5; ++seed) {
    CallFixture c(0.10, seed);
    int unwritable_after_connect = 0;
    c.Run(c.now + TimeDelta::Seconds(3));
    SM_ASSERT_TRUE(c.conn_a != nullptr && c.conn_b != nullptr);
    SM_EXPECT_TRUE(c.conn_a->writable());
    SM_EXPECT_TRUE(c.conn_b->writable());
    c.conn_a->SubscribeStateChange(nullptr, [&](Connection* conn) {
      if (!conn->writable()) ++unwritable_after_connect;
    });
    c.Run(c.now + TimeDelta::Seconds(30));
    SM_ASSERT_TRUE(c.conn_a != nullptr);
    SM_EXPECT_TRUE(c.conn_a->writable());
    SM_EXPECT_EQ(unwritable_after_connect, 0);
    const ConnectionInfo info = c.conn_a->stats(c.now);
    // About 19 % of the checks lose the check or its answer.
    SM_EXPECT_GT(info.sent_ping_requests_total, info.recv_ping_responses);
    SM_EXPECT_GT(info.recv_ping_responses, size_t{0});
  }
}

// Consent freshness: when the path goes down, a writable pair becomes unreliable once 5 checks and 5 s go
// unanswered, times out after 15 s, and is destroyed 30 s after it last received anything (the first unanswered
// check, 30 s later, at the latest).
SM_TEST(P2pConnectionCall, OutageLosesConsent) {
  CallFixture c(0.0, 1);
  c.Run(c.now + TimeDelta::Seconds(2));
  SM_ASSERT_TRUE(c.conn_a != nullptr && c.conn_a->writable());
  const Timestamp last_received = c.conn_a->LastReceived();
  sim::LinkConfig down = Link(20);
  down.down = true;
  c.net.uplink(c.host_a.host()).SetConfig(down);
  c.net.downlink(c.host_a.host()).SetConfig(down);
  std::optional<Timestamp> unreliable_at;
  std::optional<Timestamp> timeout_at;
  c.conn_a->SubscribeStateChange(nullptr, [&](Connection* conn) {
    if (conn->write_state() == Connection::STATE_WRITE_UNRELIABLE && !unreliable_at) unreliable_at = c.now;
    if (conn->write_state() == Connection::STATE_WRITE_TIMEOUT && !timeout_at) timeout_at = c.now;
  });
  c.Run(c.now + TimeDelta::Seconds(40));
  SM_ASSERT_TRUE(unreliable_at.has_value());
  SM_ASSERT_TRUE(timeout_at.has_value());
  // The first unanswered check went out within one ping interval of the last answer.
  SM_EXPECT_GE(*unreliable_at - last_received, kConnectionWriteConnectTimeout);
  SM_EXPECT_LE(*unreliable_at - last_received, kConnectionWriteConnectTimeout + TimeDelta::Seconds(1));
  SM_EXPECT_GE(*timeout_at - last_received, kConnectionWriteTimeout);
  SM_EXPECT_LE(*timeout_at - last_received, kConnectionWriteTimeout + TimeDelta::Seconds(1));
  // Dead 30 s after the first unanswered check: destroyed.
  SM_EXPECT_TRUE(c.conn_a == nullptr);
}
