// ICE ports and candidates (issue #999): the cases of libwebrtc M157's
// p2p/base/port_unittest.cc and stun_port_unittest.cc that apply to UDP (not
// TCP, TURN, connections or DNS), with the library's time model in place of
// the simulated clock, plus each NAT type of the emulated network (#985)
// giving the expected server-reflexive address.
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/byte_buffer.h"
#include "sm/p2p/candidate.h"
#include "sm/p2p/network.h"
#include "sm/p2p/port.h"
#include "sm/p2p/stun.h"
#include "sm/p2p/stun_port.h"
#include "sm/sim/emulated_sockets.h"
#include "sm/sim/stun_server.h"
#include "sm_test.h"
#include "test_crypto.h"

using namespace sm;
using namespace sm::p2p;

namespace {

IpAddress Ip(std::string_view s) { return *IpAddress::FromString(s); }
SocketAddress Addr(std::string_view s) { return *SocketAddress::FromString(s); }

Network MakeNetwork(std::string_view ip, AdapterType type = ADAPTER_TYPE_UNKNOWN) {
  const IpAddress a = Ip(ip);
  Network n("test", a, a.family() == IpFamily::kV4 ? 24 : 64, type);
  n.AddIp(a);
  return n;
}

// A socket that records what is sent and lets a test deliver packets and
// close it.
class FakeSocket final : public AsyncPacketSocket {
 public:
  explicit FakeSocket(SocketAddress local) : local_(local) {}
  SocketAddress GetLocalAddress() const override { return local_; }
  SocketAddress GetRemoteAddress() const override { return {}; }
  int Send(Timestamp, std::span<const uint8_t>, const PacketOptions&) override { return -1; }
  int SendTo(Timestamp now, std::span<const uint8_t> data, const SocketAddress& addr,
             const PacketOptions& options) override {
    sent.push_back({std::vector<uint8_t>(data.begin(), data.end()), addr, options});
    NotifySentPacket(SentPacketInfo{options.packet_id, now, data.size(), 20});
    return static_cast<int>(data.size());
  }
  int Close() override { return 0; }
  State GetState() const override { return state; }
  int GetOption(SocketOption, int*) override { return -1; }
  int SetOption(SocketOption, int) override { return 0; }
  int GetError() const override { return 0; }
  void SetError(int) override {}
  void Deliver(Timestamp now, std::span<const uint8_t> data, const SocketAddress& from) {
    NotifyPacketReceived(ReceivedPacket{data, from, now});
  }
  void NotifyClosedForTest(int err) { NotifyClosed(err); }

  struct Sent {
    std::vector<uint8_t> data;
    SocketAddress to;
    PacketOptions options;
  };
  std::vector<Sent> sent;
  State state = State::kBound;

 private:
  SocketAddress local_;
};

class NullFactory final : public PacketSocketFactory {
 public:
  std::unique_ptr<AsyncPacketSocket> CreateUdpSocket(const SocketAddress& a, uint16_t, uint16_t) override {
    return std::make_unique<FakeSocket>(SocketAddress(a.ipaddr(), 5000));
  }
};

// port_unittest.cc's TestPort: a host port whose sends are parsed and kept.
class TestPort final : public Port {
 public:
  TestPort(Timestamp now, const Parameters& args) : Port(args, IceCandidateType::kHost, 0, 0, false, now) {}
  using Port::GetStunMessage;
  using Port::IsCompatibleAddress;
  using Port::OnReadPacket;

  void set_type_preference(uint32_t p) { type_preference_ = p; }
  void AddCandidateAddress(const SocketAddress& addr) {
    AddAddress(addr, addr, SocketAddress(), "udp", "", "", Type(), type_preference_, 0, "", false);
  }
  void AddCandidateAddress(const SocketAddress& addr, const SocketAddress& base, IceCandidateType type,
                           uint32_t type_preference, bool is_final) {
    AddAddress(addr, base, SocketAddress(), "udp", "", "", type, type_preference, 0, "", is_final);
  }
  bool SupportsProtocol(std::string_view protocol) const override { return protocol == UDP_PROTOCOL_NAME; }
  ProtocolType GetProtocol() const override { return PROTO_UDP; }
  Connection* CreateConnection(Timestamp now, const Candidate& remote_candidate, CandidateOrigin) override {
    auto conn = std::make_unique<ProxyConnection>(now, this, 0, remote_candidate);
    Connection* raw = conn.get();
    AddOrReplaceConnection(std::move(conn));
    // Set use-candidate attribute flag as this will add USE-CANDIDATE attribute in STUN binding requests.
    raw->set_use_candidate_attr(true);
    return raw;
  }
  void PrepareAddress(Timestamp) override {
    AddAddress(SocketAddress(network()->GetBestIP(), 5000), SocketAddress(network()->GetBestIP(), 5000),
               SocketAddress(), "udp", "", "", IceCandidateType::kHost, ICE_TYPE_PREFERENCE_HOST, 0, "", true);
  }
  int SendTo(Timestamp, std::span<const uint8_t> data, const SocketAddress& addr, const PacketOptions& options,
             bool) override {
    last_stun_msg = std::make_unique<IceMessage>();
    ByteBufferReader buf(data);
    if (!last_stun_msg->Read(&buf)) last_stun_msg.reset();
    last_to = addr;
    last_options = options;
    ++sends;
    return static_cast<int>(data.size());
  }
  int SetOption(SocketOption, int) override { return 0; }
  int GetOption(SocketOption, int*) override { return -1; }
  int GetError() override { return 0; }

  int last_stun_error_code() const { return last_stun_msg ? last_stun_msg->GetErrorCodeValue() : 0; }

  std::unique_ptr<IceMessage> last_stun_msg;
  SocketAddress last_to;
  PacketOptions last_options;
  int sends = 0;

 private:
  uint32_t type_preference_ = 0;
};

const Timestamp kStart = Timestamp::Seconds(100);

struct PortFixture {
  Network network = MakeNetwork("192.168.1.2");
  NullFactory factory;
  PortFixture() { testing::InstallTestCrypto(); }
  std::unique_ptr<TestPort> CreateTestPort(std::string ufrag, std::string pwd, IceRole role = ICEROLE_UNKNOWN,
                                           bool adjust_priority = false) {
    Port::Parameters args;
    args.network = &network;
    args.socket_factory = &factory;
    args.ice_username_fragment = std::move(ufrag);
    args.ice_password = std::move(pwd);
    args.ice_tiebreaker = 1000;
    args.increase_host_srflx_priority = adjust_priority;
    auto port = std::make_unique<TestPort>(kStart, args);
    port->SetIceRole(role);
    return port;
  }
};

std::vector<uint8_t> Write(const StunMessage& msg) {
  ByteBufferWriter buf;
  msg.Write(&buf);
  return std::vector<uint8_t>(buf.DataView().begin(), buf.DataView().end());
}

std::unique_ptr<IceMessage> WithUsername(int type, std::string_view username) {
  auto msg = std::make_unique<IceMessage>(static_cast<uint16_t>(type), StunMessage::GenerateTransactionId());
  auto attr = StunAttribute::CreateByteString(STUN_ATTR_USERNAME);
  attr->CopyBytes(username);
  msg->AddAttribute(std::move(attr));
  return msg;
}

std::unique_ptr<IceMessage> Plain(int type) {
  return std::make_unique<IceMessage>(static_cast<uint16_t>(type), StunMessage::GenerateTransactionId());
}

}  // namespace

// ------------------------------------------------------------------ Candidate

SM_TEST(P2pCandidate, TypeNamesAndPreferences) {
  SM_EXPECT_EQ(std::string(IceCandidateTypeToString(IceCandidateType::kHost)), std::string("host"));
  SM_EXPECT_EQ(std::string(IceCandidateTypeToString(IceCandidateType::kSrflx)), std::string("srflx"));
  SM_EXPECT_EQ(std::string(IceCandidateTypeToString(IceCandidateType::kPrflx)), std::string("prflx"));
  SM_EXPECT_EQ(std::string(IceCandidateTypeToString(IceCandidateType::kRelay)), std::string("relay"));
  SM_EXPECT_TRUE(StringToIceCandidateType("relay") == IceCandidateType::kRelay);
  SM_EXPECT_FALSE(StringToIceCandidateType("bogus").has_value());
  Candidate c;
  SM_EXPECT_EQ(c.component(), ICE_CANDIDATE_COMPONENT_DEFAULT);
  SM_EXPECT_EQ(c.id().size(), size_t{8});
  SM_EXPECT_EQ(c.type_preference(), 1);
  c.set_type(IceCandidateType::kSrflx);
  SM_EXPECT_EQ(c.type_preference(), 2);
  c.set_type(IceCandidateType::kRelay);
  SM_EXPECT_EQ(c.type_preference(), 3);
  c.set_type(IceCandidateType::kPrflx);
  SM_EXPECT_EQ(c.type_preference(), 0);
  // TestCandidatePriority.
  Candidate a, b;
  a.set_priority(3);
  b.set_priority(1);
  SM_EXPECT_GT(a.priority(), b.priority());
}

SM_TEST(P2pCandidate, AttributeAndEquivalence) {
  testing::InstallTestCrypto();
  Candidate c(1, "udp", Addr("1.2.3.4:5000"), 2122260223u, "ufrg", "pass", IceCandidateType::kSrflx, 2, "123",
              7, 10);
  c.set_related_address(Addr("192.168.1.2:6000"));
  SM_EXPECT_EQ(c.ToCandidateAttribute(true),
               std::string("candidate:123 1 udp 2122260223 1.2.3.4 5000 typ srflx raddr 192.168.1.2 rport 6000 "
                           "generation 2 ufrag ufrg network-id 7 network-cost 10"));
  SM_EXPECT_EQ(c.ToCandidateAttribute(false),
               std::string("candidate:123 1 udp 2122260223 1.2.3.4 5000 typ srflx raddr 192.168.1.2 rport 6000 "
                           "generation 2 network-id 7 network-cost 10"));
  Candidate d = c;
  d.generate_id();
  d.set_priority(1);
  d.set_network_name("eth1");
  SM_EXPECT_TRUE(c.IsEquivalent(d));  // Ignores the priority and the debug fields.
  SM_EXPECT_TRUE(c != d);             // operator== compares the id.
  d.set_address(Addr("1.2.3.4:5001"));
  SM_EXPECT_FALSE(c.IsEquivalent(d));
  SM_EXPECT_FALSE(c.MatchesForRemoval(d));
  d.set_address(c.address());
  d.set_foundation("x");
  SM_EXPECT_TRUE(c.MatchesForRemoval(d));
  // The network cost is capped at kNetworkCostMax.
  d.set_network_cost(5000);
  SM_EXPECT_EQ(d.network_cost(), kNetworkCostMax);
}

SM_TEST(P2pCandidate, PrflxFoundationIsCrcOfId) {
  Candidate c;
  c.set_type(IceCandidateType::kPrflx);
  c.ComputePrflxFoundation();
  const std::string& id = c.id();
  SM_EXPECT_EQ(c.foundation(),
               std::to_string(ComputeCrc32(std::span<const uint8_t>(
                   reinterpret_cast<const uint8_t*>(id.data()), id.size()))));
}

SM_TEST(P2pCandidate, FoundationIsKeyedHashOfTypeBaseAndProtocol) {
  testing::InstallTestCrypto();
  auto foundation = [](IceCandidateType type, std::string_view base, std::string_view proto,
                       std::string_view relay_proto, uint64_t tie) {
    Candidate c(1, proto, Addr("9.9.9.9:1"), 0, "", "", type, 0, "");
    c.set_relay_protocol(relay_proto);
    c.ComputeFoundation(Addr(base), tie);
    return c.foundation();
  };
  const std::string f = foundation(IceCandidateType::kHost, "10.0.0.1:1000", "udp", "", 42);
  // The base port doesn't matter; the base IP, type, protocols and
  // tie-breaker do (RFC 5245 4.1.1.3).
  SM_EXPECT_EQ(foundation(IceCandidateType::kHost, "10.0.0.1:2000", "udp", "", 42), f);
  SM_EXPECT_NE(foundation(IceCandidateType::kHost, "10.0.0.2:1000", "udp", "", 42), f);
  SM_EXPECT_NE(foundation(IceCandidateType::kSrflx, "10.0.0.1:1000", "udp", "", 42), f);
  SM_EXPECT_NE(foundation(IceCandidateType::kHost, "10.0.0.1:1000", "tcp", "", 42), f);
  SM_EXPECT_NE(foundation(IceCandidateType::kHost, "10.0.0.1:1000", "udp", "tcp", 42), f);
  SM_EXPECT_NE(foundation(IceCandidateType::kHost, "10.0.0.1:1000", "udp", "", 43), f);
  // It is the decimal big-endian first word of HMAC-SHA256(key, input).
  for (char ch : f) SM_EXPECT_TRUE(ch >= '0' && ch <= '9');
}

SM_TEST(P2pNetwork, CostPrecedenceAndBestIp) {
  SM_EXPECT_EQ(ComputeNetworkCostByType(ADAPTER_TYPE_ETHERNET, false), kNetworkCostMin);
  SM_EXPECT_EQ(ComputeNetworkCostByType(ADAPTER_TYPE_LOOPBACK, false), kNetworkCostMin);
  SM_EXPECT_EQ(ComputeNetworkCostByType(ADAPTER_TYPE_WIFI, false), kNetworkCostLow);
  SM_EXPECT_EQ(ComputeNetworkCostByType(ADAPTER_TYPE_CELLULAR_4G, false), kNetworkCostCellular);
  SM_EXPECT_EQ(ComputeNetworkCostByType(ADAPTER_TYPE_ANY, false), kNetworkCostMax);
  SM_EXPECT_EQ(ComputeNetworkCostByType(ADAPTER_TYPE_UNKNOWN, false), kNetworkCostUnknown);
  Network vpn = MakeNetwork("10.8.0.2", ADAPTER_TYPE_VPN);
  vpn.set_underlying_type_for_vpn(ADAPTER_TYPE_CELLULAR);
  SM_EXPECT_EQ(vpn.GetCost(), kNetworkCostCellular);
  SM_EXPECT_EQ(IpAddressPrecedence(Ip("192.168.1.4")), 30);
  SM_EXPECT_EQ(IpAddressPrecedence(Ip("::1")), 60);
  SM_EXPECT_EQ(IpAddressPrecedence(Ip("fc12:3456::1234")), 50);
  SM_EXPECT_EQ(IpAddressPrecedence(Ip("2001:db8::1234")), 40);
  // IPv6: a global address beats link-local and ULA.
  Network v6("v6", Ip("2001:db8::"), 64);
  v6.AddIp(Ip("fe80::1"));
  v6.AddIp(Ip("fd00::1"));
  SM_EXPECT_EQ(v6.GetBestIP().ToString(), std::string("fe80::1"));
  v6.AddIp(Ip("2001:db8::5"));
  SM_EXPECT_EQ(v6.GetBestIP().ToString(), std::string("2001:db8::5"));
}

// ---------------------------------------------------------------- Port (base)

// TestComputeCandidatePriority and ...WithPriorityAdjustment.
SM_TEST(P2pPort, ComputeCandidatePriority) {
  for (bool adjust : {false, true}) {
    PortFixture f;
    auto port = f.CreateTestPort("name", "pass", ICEROLE_UNKNOWN, adjust);
    port->set_type_preference(90);
    port->set_component(177);
    for (const char* a : {"192.168.1.4:1234", "[2001:db8::1234]:1234", "[fc12:3456::1234]:1234",
                          "[::ffff:192.168.1.4]:1234", "[::192.168.1.4]:1234", "[2002::1234:5678]:1234",
                          "[2001::1234:5678]:1234", "[fecf::1234:5678]:1234", "[3ffe::1234:5678]:1234"}) {
      port->AddCandidateAddress(Addr(a));
    }
    const uint32_t add = adjust ? (kMaxTurnServers << 8) : 0;
    const uint32_t expected[] = {1509957199U, 1509959759U, 1509962319U, 1509957199U, 1509949775U,
                                 1509954639U, 1509952079U, 1509949775U, 1509949775U};
    SM_ASSERT_EQ(port->Candidates().size(), size_t{9});
    for (size_t i = 0; i < 9; ++i) SM_EXPECT_EQ(port->Candidates()[i].priority(), expected[i] + add);
  }
}

SM_TEST(P2pPort, CandidatesCarryPortAndNetworkFields) {
  PortFixture f;
  f.network.set_id(3);
  f.network.set_preference(kHighestNetworkPreference);
  f.network.set_type(ADAPTER_TYPE_WIFI);
  auto port = f.CreateTestPort("ufrg", "passwordpasswordpassword");
  port->set_generation(4);
  int ready = 0, complete = 0;
  port->SubscribeCandidateReady(nullptr, [&](Port*, const Candidate&) { ++ready; });
  port->SubscribePortComplete(nullptr, [&](Port*) { ++complete; });
  port->PrepareAddress(kStart);
  SM_EXPECT_EQ(ready, 1);
  SM_EXPECT_EQ(complete, 1);  // is_final.
  const Candidate& c = port->Candidates()[0];
  SM_EXPECT_EQ(c.username(), std::string("ufrg"));
  SM_EXPECT_EQ(c.generation(), 4u);
  SM_EXPECT_EQ(c.network_id(), 3);
  SM_EXPECT_EQ(c.network_cost(), kNetworkCostLow);
  SM_EXPECT_EQ(c.network_type(), ADAPTER_TYPE_WIFI);
  SM_EXPECT_EQ(c.protocol(), std::string("udp"));
  SM_EXPECT_TRUE(c.related_address().IsNil());
  // (126 << 24) | ((127 << 8 | 30) << 8) | (256 - 1).
  SM_EXPECT_EQ(c.priority(), (126u << 24) | (((127u << 8) | 30u) << 8) | 255u);
  // UpdateNetworkCost re-stamps the candidates.
  f.network.set_type(ADAPTER_TYPE_CELLULAR);
  port->UpdateNetworkCost();
  SM_EXPECT_EQ(port->network_cost(), static_cast<int16_t>(kNetworkCostCellular));
  SM_EXPECT_EQ(port->Candidates()[0].network_cost(), kNetworkCostCellular);
}

// TestFoundation.
SM_TEST(P2pPort, FoundationDiffersByType) {
  PortFixture f;
  auto port = f.CreateTestPort("name", "pass");
  port->AddCandidateAddress(Addr("192.168.1.2:1"), Addr("192.168.1.2:1"), IceCandidateType::kHost,
                            ICE_TYPE_PREFERENCE_HOST, false);
  port->AddCandidateAddress(Addr("192.168.1.3:1"), Addr("192.168.1.2:1"), IceCandidateType::kSrflx,
                            ICE_TYPE_PREFERENCE_SRFLX, true);
  SM_EXPECT_NE(port->Candidates()[0].foundation(), port->Candidates()[1].foundation());
}

// TestSetIceParameters.
SM_TEST(P2pPort, SetIceParameters) {
  PortFixture f;
  auto port = f.CreateTestPort("ufrag1", "password1");
  port->PrepareAddress(kStart);
  SM_ASSERT_EQ(port->Candidates().size(), size_t{1});
  port->SetIceParameters(1, "ufrag2", "password2");
  SM_EXPECT_EQ(port->component(), 1);
  SM_EXPECT_EQ(port->username_fragment(), std::string("ufrag2"));
  SM_EXPECT_EQ(port->password(), std::string("password2"));
  const Candidate& c = port->Candidates()[0];
  SM_EXPECT_EQ(c.component(), 1);
  SM_EXPECT_EQ(c.username(), std::string("ufrag2"));
  SM_EXPECT_EQ(c.password(), std::string("password2"));
}

SM_TEST(P2pPort, EmptyCredentialsGetRandomOnes) {
  PortFixture f;
  auto port = f.CreateTestPort("", "");
  SM_EXPECT_EQ(port->username_fragment().size(), size_t{ICE_UFRAG_LENGTH});
  SM_EXPECT_EQ(port->password().size(), size_t{ICE_PWD_LENGTH});
}

// TestSupportsProtocol (UDP).
SM_TEST(P2pPort, SupportsProtocol) {
  PortFixture f;
  auto port = f.CreateTestPort("a", "b");
  SM_EXPECT_TRUE(port->SupportsProtocol(UDP_PROTOCOL_NAME));
  SM_EXPECT_FALSE(port->SupportsProtocol(TCP_PROTOCOL_NAME));
  SM_EXPECT_FALSE(port->SupportsProtocol(SSLTCP_PROTOCOL_NAME));
}

// TestHandleStunMessage.
SM_TEST(P2pPort, HandleStunMessage) {
  PortFixture f;
  auto port = f.CreateTestPort("rfrag", "rpass");
  const SocketAddress addr = Addr("192.168.1.3:1000");
  std::unique_ptr<IceMessage> out;
  std::string username;

  auto in = WithUsername(STUN_BINDING_REQUEST, "rfrag:lfrag");
  in->AddMessageIntegrity("rpass");
  in->AddFingerprint();
  auto bytes = Write(*in);
  SM_EXPECT_TRUE(port->GetStunMessage(kStart, bytes, addr, &out, &username));
  SM_EXPECT_TRUE(out != nullptr);
  SM_EXPECT_EQ(username, std::string("lfrag"));

  in = Plain(STUN_BINDING_RESPONSE);
  auto mapped = StunAttribute::CreateXorAddress(STUN_ATTR_XOR_MAPPED_ADDRESS);
  mapped->SetAddress(Addr("192.168.1.2:5000"));
  in->AddAttribute(std::move(mapped));
  in->AddMessageIntegrity("rpass");
  in->AddFingerprint();
  bytes = Write(*in);
  SM_EXPECT_TRUE(port->GetStunMessage(kStart, bytes, addr, &out, &username));
  SM_EXPECT_TRUE(out != nullptr);
  SM_EXPECT_EQ(username, std::string(""));

  in = Plain(STUN_BINDING_ERROR_RESPONSE);
  auto code = StunAttribute::CreateErrorCode();
  code->SetCode(STUN_ERROR_SERVER_ERROR);
  code->SetReason(STUN_ERROR_REASON_SERVER_ERROR);
  in->AddAttribute(std::move(code));
  in->AddFingerprint();
  bytes = Write(*in);
  SM_EXPECT_TRUE(port->GetStunMessage(kStart, bytes, addr, &out, &username));
  SM_ASSERT_TRUE(out != nullptr);
  SM_EXPECT_EQ(username, std::string(""));
  SM_ASSERT_TRUE(out->GetErrorCode() != nullptr);
  SM_EXPECT_EQ(out->GetErrorCode()->code(), STUN_ERROR_SERVER_ERROR);
  SM_EXPECT_EQ(out->GetErrorCode()->reason(), std::string(STUN_ERROR_REASON_SERVER_ERROR));
  SM_EXPECT_EQ(port->sends, 0);  // Nothing answered.
}

// TestHandleStunMessageBadUsername.
SM_TEST(P2pPort, HandleStunMessageBadUsername) {
  PortFixture f;
  auto port = f.CreateTestPort("rfrag", "rpass");
  const SocketAddress addr = Addr("192.168.1.3:1000");
  std::unique_ptr<IceMessage> out;
  std::string username;

  auto in = Plain(STUN_BINDING_REQUEST);  // No username.
  in->AddMessageIntegrity("rpass");
  in->AddFingerprint();
  auto bytes = Write(*in);
  SM_EXPECT_TRUE(port->GetStunMessage(kStart, bytes, addr, &out, &username));
  SM_EXPECT_TRUE(out == nullptr);
  SM_EXPECT_EQ(username, std::string(""));
  SM_EXPECT_EQ(port->last_stun_error_code(), STUN_ERROR_BAD_REQUEST);
  // 400 carries no MESSAGE-INTEGRITY, and goes back to the sender.
  SM_EXPECT_TRUE(port->last_stun_msg->GetByteString(STUN_ATTR_MESSAGE_INTEGRITY) == nullptr);
  SM_EXPECT_TRUE(port->last_to == addr);
  SM_EXPECT_EQ(port->last_stun_msg->transaction_id(), in->transaction_id());

  for (const char* name : {"", "rfra", "lfrag:rfrag", "abcd:efgh"}) {
    in = WithUsername(STUN_BINDING_REQUEST, name);
    in->AddMessageIntegrity("rpass");
    in->AddFingerprint();
    bytes = Write(*in);
    SM_EXPECT_TRUE(port->GetStunMessage(kStart, bytes, addr, &out, &username));
    SM_EXPECT_TRUE(out == nullptr);
    SM_EXPECT_EQ(username, std::string(""));
    SM_EXPECT_EQ(port->last_stun_error_code(), STUN_ERROR_UNAUTHORIZED);
    SM_EXPECT_TRUE(port->last_stun_msg->GetByteString(STUN_ATTR_MESSAGE_INTEGRITY) == nullptr);
  }
}

// TestHandleStunMessageBadMessageIntegrity.
SM_TEST(P2pPort, HandleStunMessageBadMessageIntegrity) {
  PortFixture f;
  auto port = f.CreateTestPort("rfrag", "rpass");
  const SocketAddress addr = Addr("192.168.1.3:1000");
  std::unique_ptr<IceMessage> out;
  std::string username;
  auto in = WithUsername(STUN_BINDING_REQUEST, "rfrag:lfrag");
  in->AddFingerprint();
  auto bytes = Write(*in);
  SM_EXPECT_TRUE(port->GetStunMessage(kStart, bytes, addr, &out, &username));
  SM_EXPECT_TRUE(out == nullptr);
  SM_EXPECT_EQ(port->last_stun_error_code(), STUN_ERROR_BAD_REQUEST);

  in = WithUsername(STUN_BINDING_REQUEST, "rfrag:lfrag");
  in->AddMessageIntegrity("invalid");
  in->AddFingerprint();
  bytes = Write(*in);
  SM_EXPECT_TRUE(port->GetStunMessage(kStart, bytes, addr, &out, &username));
  SM_EXPECT_TRUE(out == nullptr);
  SM_EXPECT_EQ(port->last_stun_error_code(), STUN_ERROR_UNAUTHORIZED);
}

// TestHandleStunMessageBadFingerprint.
SM_TEST(P2pPort, HandleStunMessageBadFingerprint) {
  PortFixture f;
  auto port = f.CreateTestPort("rfrag", "rpass");
  const SocketAddress addr = Addr("192.168.1.3:1000");
  std::unique_ptr<IceMessage> out;
  std::string username;
  // No FINGERPRINT: not STUN to an ICE port.
  auto in = WithUsername(STUN_BINDING_REQUEST, "rfrag:lfrag");
  in->AddMessageIntegrity("rpass");
  auto bytes = Write(*in);
  SM_EXPECT_FALSE(port->GetStunMessage(kStart, bytes, addr, &out, &username));
  SM_EXPECT_EQ(port->sends, 0);
  // A corrupt FINGERPRINT.
  in->AddFingerprint();
  bytes = Write(*in);
  bytes.back() ^= 0x01;
  SM_EXPECT_FALSE(port->GetStunMessage(kStart, bytes, addr, &out, &username));
  // Same for responses and error responses.
  in = Plain(STUN_BINDING_RESPONSE);
  in->AddMessageIntegrity("rpass");
  bytes = Write(*in);
  SM_EXPECT_FALSE(port->GetStunMessage(kStart, bytes, addr, &out, &username));
  SM_EXPECT_EQ(port->sends, 0);
}

// TestHandleStunRequestWithUnknownComprehensionRequiredAttribute.
SM_TEST(P2pPort, UnknownComprehensionRequiredAttributeInRequest) {
  PortFixture f;
  auto port = f.CreateTestPort("rfrag", "rpass");
  std::unique_ptr<IceMessage> out;
  std::string username;
  auto in = WithUsername(STUN_BINDING_REQUEST, "rfrag:lfrag");
  in->AddAttribute(StunAttribute::CreateUInt32(0x7777));
  in->AddAttribute(StunAttribute::CreateUInt32(0x4567));
  in->AddAttribute(StunAttribute::CreateUInt32(0xdead));  // Comprehension-optional.
  in->AddMessageIntegrity("rpass");
  in->AddFingerprint();
  auto bytes = Write(*in);
  SM_ASSERT_TRUE(port->GetStunMessage(kStart, bytes, Addr("192.168.1.3:1000"), &out, &username));
  SM_EXPECT_TRUE(out == nullptr);
  SM_ASSERT_TRUE(port->last_stun_msg != nullptr);
  SM_EXPECT_EQ(port->last_stun_msg->GetErrorCodeValue(), STUN_ERROR_UNKNOWN_ATTRIBUTE);
  const StunUInt16ListAttribute* unknown = port->last_stun_msg->GetUnknownAttributes();
  SM_ASSERT_TRUE(unknown != nullptr);
  SM_ASSERT_EQ(unknown->Size(), size_t{2});
  SM_EXPECT_EQ(unknown->GetType(0), 0x7777);
  SM_EXPECT_EQ(unknown->GetType(1), 0x4567);
  // Signed with the port's password.
  SM_EXPECT_TRUE(port->last_stun_msg->ValidateMessageIntegrity("rpass") ==
                 StunMessage::IntegrityStatus::kIntegrityOk);
}

// A response or indication with one is discarded (RFC 5389 7.3.3, 7.3.2).
SM_TEST(P2pPort, UnknownComprehensionRequiredAttributeInResponseOrIndication) {
  PortFixture f;
  auto port = f.CreateTestPort("rfrag", "rpass");
  std::unique_ptr<IceMessage> out;
  std::string username;
  for (int type : {STUN_BINDING_RESPONSE, STUN_BINDING_INDICATION}) {
    auto in = Plain(type);
    in->AddAttribute(StunAttribute::CreateUInt32(0x7777));
    in->AddFingerprint();
    auto bytes = Write(*in);
    SM_EXPECT_TRUE(port->GetStunMessage(kStart, bytes, Addr("192.168.1.3:1000"), &out, &username));
    SM_EXPECT_TRUE(out == nullptr);
  }
  SM_EXPECT_EQ(port->sends, 0);
}

// TestHandleStunBindingIndication (the port half).
SM_TEST(P2pPort, HandleStunBindingIndication) {
  PortFixture f;
  auto port = f.CreateTestPort("lfrag", "lpass", ICEROLE_CONTROLLING);
  std::unique_ptr<IceMessage> out;
  std::string username;
  auto in = Plain(STUN_BINDING_INDICATION);
  in->AddFingerprint();
  auto bytes = Write(*in);
  SM_EXPECT_TRUE(port->GetStunMessage(kStart, bytes, Addr("192.168.1.3:1000"), &out, &username));
  SM_ASSERT_TRUE(out != nullptr);
  SM_EXPECT_EQ(out->type(), STUN_BINDING_INDICATION);
  SM_EXPECT_EQ(username, std::string(""));
}

SM_TEST(P2pPort, GoogPing) {
  PortFixture f;
  auto port = f.CreateTestPort("rfrag", "rpass", ICEROLE_CONTROLLED);
  const SocketAddress addr = Addr("192.168.1.3:1000");
  // A GOOG-PING with a bad GOOG-MESSAGE-INTEGRITY-32 gets 401.
  auto in = Plain(GOOG_PING_REQUEST);
  in->AddMessageIntegrity32("wrong");
  auto bytes = Write(*in);
  port->OnReadPacket(ReceivedPacket{bytes, addr, kStart}, PROTO_UDP);
  SM_EXPECT_EQ(port->last_stun_error_code(), STUN_ERROR_UNAUTHORIZED);
  SM_EXPECT_EQ(port->last_stun_msg->type(), GOOG_PING_ERROR_RESPONSE);
  // A good one to an address without a connection gets 400: the sender must
  // fall back to a full binding request.
  in = Plain(GOOG_PING_REQUEST);
  in->AddMessageIntegrity32("rpass");
  bytes = Write(*in);
  port->OnReadPacket(ReceivedPacket{bytes, addr, kStart}, PROTO_UDP);
  SM_EXPECT_EQ(port->last_stun_error_code(), STUN_ERROR_BAD_REQUEST);
  SM_EXPECT_EQ(port->last_stun_msg->type(), GOOG_PING_ERROR_RESPONSE);
  SM_EXPECT_TRUE(port->last_stun_msg->GetByteString(STUN_ATTR_FINGERPRINT) == nullptr);
  SM_EXPECT_TRUE(port->last_stun_msg->GetByteString(STUN_ATTR_GOOG_MESSAGE_INTEGRITY_32) == nullptr);
}

namespace {

std::vector<uint8_t> BindingRequest(std::string_view username, std::string_view pwd, std::optional<int> role_attr,
                                    uint64_t tiebreaker) {
  auto in = WithUsername(STUN_BINDING_REQUEST, username);
  if (role_attr) in->AddAttribute(std::make_unique<StunUInt64Attribute>(static_cast<uint16_t>(*role_attr), tiebreaker));
  in->AddMessageIntegrity(pwd);
  in->AddFingerprint();
  return Write(*in);
}

}  // namespace

// BindingRequestDroppedWithoutIceRole, and the unknown-address signal.
SM_TEST(P2pPort, UnknownAddressSignalNeedsIceRole) {
  PortFixture f;
  auto port = f.CreateTestPort("rfrag", "rpass");
  int unknown = 0;
  std::string remote_ufrag;
  SocketAddress from;
  port->SubscribeUnknownAddress(nullptr, [&](Timestamp, Port*, const SocketAddress& a, ProtocolType proto,
                                             IceMessage* msg, const std::string& rf, bool muxed) {
    ++unknown;
    from = a;
    remote_ufrag = rf;
    SM_EXPECT_EQ(proto, PROTO_UDP);
    SM_EXPECT_EQ(msg->type(), STUN_BINDING_REQUEST);
    SM_EXPECT_FALSE(muxed);
  });
  const auto bytes = BindingRequest("rfrag:lfrag", "rpass", STUN_ATTR_ICE_CONTROLLING, 1);
  const SocketAddress addr = Addr("192.168.1.3:1000");
  port->OnReadPacket(ReceivedPacket{bytes, addr, kStart}, PROTO_UDP);
  SM_EXPECT_EQ(unknown, 0);  // No role: dropped.
  SM_EXPECT_EQ(port->sends, 0);
  port->SetIceRole(ICEROLE_CONTROLLED);
  port->OnReadPacket(ReceivedPacket{bytes, addr, kStart}, PROTO_UDP);
  SM_EXPECT_EQ(unknown, 1);
  SM_EXPECT_TRUE(from == addr);
  SM_EXPECT_EQ(remote_ufrag, std::string("lfrag"));
  // A non-STUN packet from an unknown address is dropped.
  const uint8_t junk[] = {1, 2, 3, 4};
  port->OnReadPacket(ReceivedPacket{junk, addr, kStart}, PROTO_UDP);
  SM_EXPECT_EQ(unknown, 1);
  // With port packets on, everything goes to the read signal instead.
  int read = 0;
  port->SubscribeReadPacket(nullptr, [&](Port*, std::span<const uint8_t>, const SocketAddress&) { ++read; });
  port->EnablePortPackets();
  port->OnReadPacket(ReceivedPacket{junk, addr, kStart}, PROTO_UDP);
  port->OnReadPacket(ReceivedPacket{bytes, addr, kStart}, PROTO_UDP);
  SM_EXPECT_EQ(read, 2);
  SM_EXPECT_EQ(unknown, 1);
}

// RFC 5245 7.2.1.1 as MaybeIceRoleConflict (TestIceRoleConflict's port half).
SM_TEST(P2pPort, IceRoleConflict) {
  struct Case {
    IceRole ours;
    int theirs;
    uint64_t their_tiebreaker;
    bool expect_signal;
    bool expect_487;
  };
  // Our tie-breaker is 1000.
  const Case cases[] = {
      {ICEROLE_CONTROLLING, STUN_ATTR_ICE_CONTROLLING, 2000, true, false},  // They win: we switch.
      {ICEROLE_CONTROLLING, STUN_ATTR_ICE_CONTROLLING, 1000, true, false},  // >= wins.
      {ICEROLE_CONTROLLING, STUN_ATTR_ICE_CONTROLLING, 999, false, true},   // We win: 487.
      {ICEROLE_CONTROLLING, STUN_ATTR_ICE_CONTROLLED, 5, false, false},     // No conflict.
      {ICEROLE_CONTROLLED, STUN_ATTR_ICE_CONTROLLED, 999, true, false},     // They have less: we switch.
      {ICEROLE_CONTROLLED, STUN_ATTR_ICE_CONTROLLED, 1000, false, true},
      {ICEROLE_CONTROLLED, STUN_ATTR_ICE_CONTROLLING, 5, false, false},
  };
  for (const Case& c : cases) {
    PortFixture f;
    auto port = f.CreateTestPort("rfrag", "rpass", c.ours);
    int conflicts = 0;
    port->SubscribeRoleConflict([&] { ++conflicts; });
    port->OnReadPacket(ReceivedPacket{BindingRequest("rfrag:lfrag", "rpass", c.theirs, c.their_tiebreaker),
                                      Addr("192.168.1.3:1000"), kStart},
                       PROTO_UDP);
    SM_EXPECT_EQ(conflicts, c.expect_signal ? 1 : 0);
    if (c.expect_487) {
      SM_ASSERT_TRUE(port->last_stun_msg != nullptr);
      SM_EXPECT_EQ(port->last_stun_error_code(), STUN_ERROR_ROLE_CONFLICT);
      // 487 is signed, and has a FINGERPRINT.
      SM_EXPECT_TRUE(port->last_stun_msg->ValidateMessageIntegrity("rpass") ==
                     StunMessage::IntegrityStatus::kIntegrityOk);
      SM_EXPECT_TRUE(port->last_stun_msg->GetUInt32(STUN_ATTR_FINGERPRINT) != nullptr);
    } else {
      SM_EXPECT_EQ(port->sends, 0);
    }
  }
}

// TestLoopbackCall's port half: our own ufrag and tie-breaker is no conflict.
SM_TEST(P2pPort, LoopbackIsNoConflict) {
  PortFixture f;
  auto port = f.CreateTestPort("lfrag", "lpass", ICEROLE_CONTROLLING);
  int conflicts = 0;
  port->SubscribeRoleConflict([&] { ++conflicts; });
  port->OnReadPacket(
      ReceivedPacket{BindingRequest("lfrag:lfrag", "lpass", STUN_ATTR_ICE_CONTROLLING, 1000), Addr("1.1.1.1:1"),
                     kStart},
      PROTO_UDP);
  SM_EXPECT_EQ(conflicts, 0);
  SM_EXPECT_EQ(port->sends, 0);
}

SM_TEST(P2pPort, UsernameHelpers) {
  PortFixture f;
  auto port = f.CreateTestPort("lfrag", "lpass");
  SM_EXPECT_EQ(port->CreateStunUsername("rfrag"), std::string("rfrag:lfrag"));
  auto msg = WithUsername(STUN_BINDING_REQUEST, "a:b:c");
  std::string l, r;
  SM_EXPECT_TRUE(port->ParseStunUsername(msg.get(), &l, &r));
  SM_EXPECT_EQ(l, std::string("a"));
  SM_EXPECT_EQ(r, std::string("b:c"));
  msg = WithUsername(STUN_BINDING_REQUEST, "nocolon");
  SM_EXPECT_FALSE(port->ParseStunUsername(msg.get(), &l, &r));
  SM_EXPECT_FALSE(port->ParseStunUsername(Plain(STUN_BINDING_REQUEST).get(), &l, &r));
}

// TestSkipCrossFamilyUdp and the link-local rule.
SM_TEST(P2pPort, IsCompatibleAddress) {
  PortFixture f;
  auto port = f.CreateTestPort("a", "b");
  SM_EXPECT_TRUE(port->IsCompatibleAddress(Addr("8.8.8.8:1")));
  SM_EXPECT_FALSE(port->IsCompatibleAddress(Addr("[2001:db8::1]:1")));
  f.network = MakeNetwork("fe80::1");
  SM_EXPECT_TRUE(port->IsCompatibleAddress(Addr("[fe80::2]:1")));
  SM_EXPECT_FALSE(port->IsCompatibleAddress(Addr("[2001:db8::1]:1")));
  f.network = MakeNetwork("2001:db8::5");
  SM_EXPECT_TRUE(port->IsCompatibleAddress(Addr("[2001:db8::1]:1")));
  SM_EXPECT_FALSE(port->IsCompatibleAddress(Addr("[fe80::2]:1")));
}

namespace {

// Runs the port's timers until `end`.
void RunPort(Port& port, Timestamp end) {
  for (Timestamp t = port.NextProcessTime(); t <= end; t = port.NextProcessTime()) port.Process(t);
}

}  // namespace

// TestPortTimeoutIfNotKeptAlive.
SM_TEST(P2pPort, TimeoutIfNotKeptAlive) {
  PortFixture f;
  auto port = f.CreateTestPort("a", "b");
  int destroyed = 0;
  port->SubscribePortDestroyed(nullptr, [&](Port*) { ++destroyed; });
  port->set_timeout_delay(10);  // Before the first check is posted, it stays at kPortTimeoutDelay.
  SM_EXPECT_EQ((port->NextProcessTime() - kStart).ms(), kPortTimeoutDelay);
  RunPort(*port, kStart + TimeDelta::Millis(kPortTimeoutDelay - 1));
  SM_EXPECT_FALSE(port->dead());
  RunPort(*port, kStart + TimeDelta::Millis(kPortTimeoutDelay));
  SM_EXPECT_TRUE(port->dead());
  SM_EXPECT_EQ(destroyed, 1);
  SM_EXPECT_TRUE(port->NextProcessTime().IsPlusInfinity());
}

// TestPortNotTimeoutUntilPruned.
SM_TEST(P2pPort, NotTimeoutUntilPruned) {
  PortFixture f;
  auto port = f.CreateTestPort("a", "b");
  port->KeepAliveUntilPruned();
  RunPort(*port, kStart + TimeDelta::Seconds(300));
  SM_EXPECT_FALSE(port->dead());
  const Timestamp prune = kStart + TimeDelta::Seconds(301);
  port->Prune(prune);
  SM_EXPECT_EQ(port->NextProcessTime(), prune);  // At once (PostTask).
  RunPort(*port, prune);
  SM_EXPECT_TRUE(port->dead());
  // Once pruned, KeepAliveUntilPruned doesn't bring it back.
  PortFixture g;
  auto other = g.CreateTestPort("a", "b");
  other->Prune(kStart);
  other->KeepAliveUntilPruned();
  RunPort(*other, kStart);
  SM_EXPECT_TRUE(other->dead());
}

// ------------------------------------------------------------- UDP/STUN ports

namespace {

constexpr uint32_t kClientIp = sim::SocketAddress::Ip(192, 168, 1, 2);
constexpr uint32_t kPublicClientIp = sim::SocketAddress::Ip(1, 2, 3, 4);
constexpr uint32_t kNatIp = sim::SocketAddress::Ip(5, 5, 5, 5);
constexpr uint32_t kStun1Ip = sim::SocketAddress::Ip(8, 8, 8, 8);
constexpr uint32_t kStun2Ip = sim::SocketAddress::Ip(8, 8, 4, 4);
const SocketAddress kStunAddr1(kStun1Ip, 3478);
const SocketAddress kStunAddr2(kStun2Ip, 3478);
const SocketAddress kBadAddr(sim::SocketAddress::Ip(9, 9, 9, 9), 3478);  // Nobody there.

sim::LinkConfig Link(int delay_ms) {
  sim::LinkConfig c;
  c.base_delay = TimeDelta::Millis(delay_ms);
  return c;
}

// StunPortTest: a client (behind a NAT or not) and two STUN servers.
struct StunFixture {
  sim::ByteNetwork net;
  uint32_t client_ip;
  std::unique_ptr<sim::EmulatedSocketServer> client;
  sim::EmulatedSocketServer stun1_host;
  sim::EmulatedSocketServer stun2_host;
  sim::StunServer stun1;
  sim::StunServer stun2;
  Network network;
  Timestamp now = Timestamp::Seconds(10);

  std::unique_ptr<UDPPort> port;
  bool done = false;
  bool error = false;
  std::optional<IceCandidateErrorEvent> error_event;
  std::optional<TimeDelta> keepalive;

  explicit StunFixture(std::optional<sim::NatType> nat = sim::NatType::kPortRestricted)
      : client_ip(nat ? kClientIp : kPublicClientIp),
        client(MakeClient(nat)),
        stun1_host(net, net.AddHost(kStun1Ip, sim::ByteNetwork::kPublicRealm, Link(5), Link(5))),
        stun2_host(net, net.AddHost(kStun2Ip, sim::ByteNetwork::kPublicRealm, Link(5), Link(5))),
        stun1(stun1_host.CreateUdpSocket(kStunAddr1, 0, 0)),
        stun2(stun2_host.CreateUdpSocket(kStunAddr2, 0, 0)),
        network("eth0", IpAddress(client_ip), 24) {
    testing::InstallTestCrypto();
    network.AddIp(IpAddress(client_ip));
  }

  std::unique_ptr<sim::EmulatedSocketServer> MakeClient(std::optional<sim::NatType> nat) {
    int realm = sim::ByteNetwork::kPublicRealm;
    if (nat) {
      sim::NatConfig cfg;
      cfg.type = *nat;
      cfg.external_ip = kNatIp;
      realm = net.NatRealm(net.AddNat(cfg));
    }
    return std::make_unique<sim::EmulatedSocketServer>(net, net.AddHost(client_ip, realm, Link(10), Link(10)));
  }

  Port::Parameters Args(PacketSocketFactory* factory) {
    Port::Parameters args;
    args.network = &network;
    args.socket_factory = factory;
    args.ice_username_fragment = "ufrag";
    args.ice_password = "passwordpasswordpassword";
    args.ice_tiebreaker = 77;
    return args;
  }
  void Subscribe() {
    port->SubscribePortComplete(nullptr, [this](Port*) { done = true; });
    port->SubscribePortError(nullptr, [this](Port*) {
      done = true;
      error = true;
    });
    port->SubscribeCandidateError(nullptr, [this](Port*, const IceCandidateErrorEvent& e) { error_event = e; });
  }
  void CreateStunPort(const ServerAddresses& servers) {
    port = StunPort::Create(now, Args(client.get()), 0, 0, servers, keepalive);
    Subscribe();
  }
  void CreateUdpPort(const ServerAddresses& servers) {
    port = UDPPort::Create(now, Args(client.get()), 0, 0, false, keepalive);
    port->set_server_addresses(servers);
    Subscribe();
  }
  // Runs the port and the network until `until` or `stop()` holds.
  template <class F>
  bool RunUntil(Timestamp until, F stop) {
    for (;;) {
      if (stop()) return true;
      const Timestamp t = std::min(port->NextProcessTime(), client->NextEvent());
      if (t > until) {
        now = until;
        return stop();
      }
      now = std::max(now, t);
      port->Process(now);
      client->Deliver(now);
      stun1_host.Deliver(now);
      stun2_host.Deliver(now);
    }
  }
  bool RunUntilDone() {
    return RunUntil(now + TimeDelta::Seconds(60), [this] { return done; });
  }
};

}  // namespace

// TestCreateStunPort, TestPrepareAddress.
SM_TEST(P2pStunPort, PrepareAddress) {
  StunFixture f;
  f.CreateStunPort({kStunAddr1});
  SM_EXPECT_TRUE(f.port->Type() == IceCandidateType::kSrflx);
  SM_EXPECT_EQ(f.port->Candidates().size(), size_t{0});
  f.port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_EXPECT_FALSE(f.error);
  SM_ASSERT_EQ(f.port->Candidates().size(), size_t{1});
  const Candidate& c = f.port->Candidates()[0];
  SM_EXPECT_EQ(c.address().ToString(), std::string("5.5.5.5:49152"));
  SM_EXPECT_EQ(c.url(), "stun:" + kStunAddr1.ToString());
  SM_EXPECT_TRUE(c.is_stun());
  // TestCandidateRelatedAddress: the related address is the local socket.
  SM_EXPECT_TRUE(c.related_address() == f.port->GetLocalAddress());
  SM_EXPECT_EQ(c.related_address().ToString(), std::string("192.168.1.2:49152"));
  SM_EXPECT_EQ(c.priority(), (100u << 24) | (30u << 8) | 255u);
  // One request, one response: the RTT is the path's 30 ms.
  std::optional<StunStats> stats;
  f.port->GetStunStats(&stats);
  SM_ASSERT_TRUE(stats.has_value());
  SM_EXPECT_EQ(stats->stun_binding_requests_sent, 1);
  SM_EXPECT_EQ(stats->stun_binding_responses_received, 1);
  SM_EXPECT_EQ(stats->stun_binding_rtt_ms_total, 30.0);
  SM_EXPECT_EQ(stats->stun_binding_rtt_ms_squared_total, 900.0);
}

// Each NAT type gives the NAT's mapping as the server-reflexive address, and
// a public client's reflexive address is its own host address.
SM_TEST(P2pStunPort, ServerReflexiveThroughEachNatType) {
  for (sim::NatType type : {sim::NatType::kOpenCone, sim::NatType::kAddrRestricted, sim::NatType::kPortRestricted,
                            sim::NatType::kSymmetric}) {
    StunFixture f(type);
    f.CreateUdpPort({kStunAddr1});
    f.port->PrepareAddress(f.now);
    SM_ASSERT_TRUE(f.RunUntilDone());
    SM_ASSERT_EQ(f.port->Candidates().size(), size_t{2});
    SM_EXPECT_TRUE(f.port->Candidates()[0].is_local());
    SM_EXPECT_EQ(f.port->Candidates()[0].address().ToString(), std::string("192.168.1.2:49152"));
    SM_EXPECT_TRUE(f.port->Candidates()[1].is_stun());
    SM_EXPECT_EQ(f.port->Candidates()[1].address().ToString(), std::string("5.5.5.5:49152"));
  }
  // No NAT: the reflexive address equals the host address. The socket isn't
  // shared, so libwebrtc still adds it (with another foundation).
  StunFixture open(std::nullopt);
  open.CreateUdpPort({kStunAddr1});
  open.port->PrepareAddress(open.now);
  SM_ASSERT_TRUE(open.RunUntilDone());
  SM_ASSERT_EQ(open.port->Candidates().size(), size_t{2});
  SM_EXPECT_TRUE(open.port->Candidates()[1].address() == open.port->Candidates()[0].address());
  SM_EXPECT_NE(open.port->Candidates()[1].foundation(), open.port->Candidates()[0].foundation());
}

// A symmetric NAT maps each server to its own port: two reflexive
// candidates (TestTwoCandidatesWithTwoStunServersAcrossNat).
SM_TEST(P2pStunPort, TwoCandidatesWithTwoServersAcrossSymmetricNat) {
  StunFixture f(sim::NatType::kSymmetric);
  f.CreateStunPort({kStunAddr1, kStunAddr2});
  f.port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_ASSERT_EQ(f.port->Candidates().size(), size_t{2});
  SM_EXPECT_TRUE(f.port->Candidates()[0].address() != f.port->Candidates()[1].address());
  SM_EXPECT_EQ(f.port->Candidates()[0].relay_protocol(), std::string(""));
  SM_EXPECT_EQ(f.port->Candidates()[1].relay_protocol(), std::string(""));
  // Same base IP and type, so the same foundation (servers' IPs aren't in it).
  SM_EXPECT_EQ(f.port->Candidates()[0].foundation(), f.port->Candidates()[1].foundation());
}

// TestNoDuplicatedAddressWithTwoStunServers: a cone NAT gives both servers
// the same mapping, one candidate.
SM_TEST(P2pStunPort, NoDuplicatedAddressWithTwoServers) {
  StunFixture f(sim::NatType::kPortRestricted);
  f.CreateStunPort({kStunAddr1, kStunAddr2});
  f.port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_EXPECT_FALSE(f.error);
  SM_EXPECT_EQ(f.port->Candidates().size(), size_t{1});
}

// TestPrepareAddressFail: the server never answers; the port fails after the
// whole STUN transaction (39.75 s) with 701.
SM_TEST(P2pStunPort, PrepareAddressFail) {
  StunFixture f;
  f.CreateStunPort({kBadAddr});
  const Timestamp start = f.now;
  f.port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_EXPECT_TRUE(f.error);
  SM_EXPECT_EQ((f.now - start).ms(), STUN_TOTAL_TIMEOUT);
  SM_EXPECT_EQ(f.port->Candidates().size(), size_t{0});
  SM_ASSERT_TRUE(f.error_event.has_value());
  SM_EXPECT_EQ(f.error_event->error_code, STUN_ERROR_SERVER_NOT_REACHABLE);
  SM_EXPECT_NE(f.error_event->error_text.find('.'), std::string::npos);
  SM_EXPECT_NE(f.error_event->address.find(IpAddress(kClientIp).ToSensitiveString()), std::string::npos);
  SM_EXPECT_EQ(f.error_event->url, "stun:" + kBadAddr.ToString());
  std::optional<StunStats> stats;
  f.port->GetStunStats(&stats);
  SM_EXPECT_EQ(stats->stun_binding_requests_sent, 9);  // STUN_MAX_RETRANSMISSIONS + 1.
}

// TestMultipleStunServersWithBadServer: complete (not failed) once the bad
// server times out, with the good server's candidate.
SM_TEST(P2pStunPort, MultipleServersWithBadServer) {
  StunFixture f;
  f.CreateStunPort({kStunAddr1, kBadAddr});
  f.port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_EXPECT_FALSE(f.error);
  SM_EXPECT_EQ(f.port->Candidates().size(), size_t{1});
  SM_ASSERT_TRUE(f.error_event.has_value());
  SM_EXPECT_EQ(f.error_event->url, "stun:" + kBadAddr.ToString());
}

// TestServerAddressFamilyMismatch: an IPv6 server for an IPv4 port fails the
// port without a candidate error.
SM_TEST(P2pStunPort, ServerAddressFamilyMismatch) {
  StunFixture f;
  f.CreateStunPort({Addr("[2001:db8::1]:3478")});
  f.port->PrepareAddress(f.now);
  SM_EXPECT_TRUE(f.done);  // At once.
  SM_EXPECT_TRUE(f.error);
  SM_EXPECT_EQ(f.port->Candidates().size(), size_t{0});
  SM_EXPECT_FALSE(f.error_event.has_value());
}

// TestKeepAliveResponse: keepalives every interval, no new candidates.
SM_TEST(P2pStunPort, KeepAliveResponse) {
  StunFixture f;
  f.keepalive = TimeDelta::Millis(500);
  f.CreateStunPort({kStunAddr1});
  f.port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_ASSERT_EQ(f.port->Candidates().size(), size_t{1});
  const Timestamp done_at = f.now;
  f.RunUntil(done_at + TimeDelta::Millis(1000), [] { return false; });
  SM_EXPECT_EQ(f.port->Candidates().size(), size_t{1});
  // The first response at 30 ms, the next request 500 ms later (530 ms),
  // its response at 560 ms, the third request at 1060 ms: past the second.
  SM_EXPECT_EQ(f.stun1.binding_requests(), 2);
  SM_EXPECT_TRUE(f.port->request_manager().HasRequestForTest(STUN_BINDING_REQUEST));
}

// The default keepalive interval is kStunKeepaliveInterval (10 s).
SM_TEST(P2pStunPort, DefaultKeepaliveInterval) {
  StunFixture f;
  f.CreateStunPort({kStunAddr1});
  SM_EXPECT_EQ(f.port->stun_keepalive_delay(), kStunKeepaliveInterval);
  f.port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  const Timestamp done_at = f.now;
  f.RunUntil(done_at + TimeDelta::Millis(9999), [] { return false; });
  SM_EXPECT_EQ(f.stun1.binding_requests(), 1);
  f.RunUntil(done_at + TimeDelta::Millis(10100), [] { return false; });
  SM_EXPECT_EQ(f.stun1.binding_requests(), 2);
}

// TestStunPortGetStunKeepaliveLifetime and the UDP variant.
SM_TEST(P2pStunPort, KeepaliveLifetimeFollowsNetworkCost) {
  for (bool stun : {true, false}) {
    StunFixture f;
    if (stun) {
      f.CreateStunPort({kStunAddr1});
    } else {
      f.CreateUdpPort({kStunAddr1});
    }
    SM_EXPECT_TRUE(f.port->stun_keepalive_lifetime().IsPlusInfinity());
    f.network.set_type(ADAPTER_TYPE_CELLULAR);
    f.port->UpdateNetworkCost();
    SM_EXPECT_EQ(f.port->stun_keepalive_lifetime(), kHighCostPortKeepaliveLifetime);
    f.network.set_type(ADAPTER_TYPE_WIFI);
    f.port->UpdateNetworkCost();
    SM_EXPECT_TRUE(f.port->stun_keepalive_lifetime().IsPlusInfinity());
  }
  // A port made on a cellular network starts with the 2-minute lifetime.
  StunFixture g;
  g.network.set_type(ADAPTER_TYPE_CELLULAR_4G);
  g.CreateStunPort({kStunAddr1});
  SM_EXPECT_EQ(g.port->stun_keepalive_lifetime(), kHighCostPortKeepaliveLifetime);
}

// TestStunBindingRequestShortLifetime / LongLifetime.
SM_TEST(P2pStunPort, BindingRequestLifetime) {
  StunFixture f;
  f.keepalive = TimeDelta::Millis(101);
  f.CreateStunPort({kStunAddr1});
  f.port->set_stun_keepalive_lifetime(TimeDelta::Millis(100));
  f.port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_EXPECT_TRUE(f.RunUntil(f.now + TimeDelta::Seconds(5), [&] {
    return !f.port->request_manager().HasRequestForTest(STUN_BINDING_REQUEST);
  }));

  StunFixture g;
  g.keepalive = TimeDelta::Millis(101);
  g.CreateStunPort({kStunAddr1});
  g.port->PrepareAddress(g.now);
  SM_ASSERT_TRUE(g.RunUntilDone());
  g.RunUntil(g.now + TimeDelta::Seconds(5), [] { return false; });
  SM_EXPECT_TRUE(g.port->request_manager().HasRequestForTest(STUN_BINDING_REQUEST));
  // On a cellular network the keepalives stop after 2 minutes.
  StunFixture h;
  h.network.set_type(ADAPTER_TYPE_CELLULAR);
  h.CreateStunPort({kStunAddr1});
  h.port->KeepAliveUntilPruned();  // Or it dies at kPortTimeoutDelay (44.75 s).
  h.port->PrepareAddress(h.now);
  SM_ASSERT_TRUE(h.RunUntilDone());
  h.RunUntil(h.now + TimeDelta::Seconds(125), [] { return false; });
  SM_EXPECT_FALSE(h.port->request_manager().HasRequestForTest(STUN_BINDING_REQUEST));
  // Request k goes at k x 10.03 s; its response, 30 ms later, sends the next
  // while within 120 s of the start: responses 0-11 do (11 x 10.03 + 0.03 =
  // 110.36 s), response 12 at 120.39 s doesn't. 13 requests.
  SM_EXPECT_EQ(h.stun1.binding_requests(), 13);
}

// A server that answers with an error: the port fails (one server), and the
// request is retried on the keepalive interval for up to 50 s. The candidate
// error carries the error's number() (401 -> 1), as libwebrtc's
// StunBindingRequest::OnErrorResponse passes it; a x00 code reports 0, which
// is STUN_ERROR_NOT_AN_ERROR and raises no candidate error there either.
SM_TEST(P2pStunPort, ErrorResponseRetriesFor50Seconds) {
  StunFixture f(std::nullopt);
  // A fake server that answers every binding request with 500.
  auto server_socket = f.stun2_host.CreateUdpSocket(SocketAddress(IpAddress(kStun2Ip), 4000), 0, 0);
  int requests = 0;
  AsyncPacketSocket* raw = server_socket.get();
  raw->RegisterReceivedPacketCallback([&](AsyncPacketSocket*, const ReceivedPacket& p) {
    ByteBufferReader buf(p.payload);
    StunMessage req;
    if (!req.Read(&buf)) return;
    ++requests;
    StunMessage err(STUN_BINDING_ERROR_RESPONSE, req.transaction_id());
    auto code = StunAttribute::CreateErrorCode();
    code->SetCode(STUN_ERROR_UNAUTHORIZED);
    code->SetReason(STUN_ERROR_REASON_UNAUTHORIZED);
    err.AddAttribute(std::move(code));
    ByteBufferWriter out;
    err.Write(&out);
    raw->SendTo(p.arrival_time, out.DataView(), p.source_address, {});
  });
  const SocketAddress server(IpAddress(kStun2Ip), 4000);
  f.CreateStunPort({server});
  f.port->KeepAliveUntilPruned();
  const Timestamp start = f.now;
  f.port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.RunUntilDone());
  SM_EXPECT_TRUE(f.error);
  SM_ASSERT_TRUE(f.error_event.has_value());
  SM_EXPECT_EQ(f.error_event->error_code, 1);
  SM_EXPECT_EQ(f.error_event->error_text, std::string(STUN_ERROR_REASON_UNAUTHORIZED));
  f.RunUntil(start + TimeDelta::Seconds(80), [] { return false; });
  // At 0, then 10 s + 30 ms apart while under 50 s from the start: 0, 10.03,
  // 20.06, 30.09, 40.12 and 50.15 (the error at 40.15 is under 50 s).
  SM_EXPECT_EQ(requests, 6);
  SM_EXPECT_FALSE(f.port->request_manager().HasRequestForTest(STUN_BINDING_REQUEST));
}

// --------------------------------------------------------- shared socket port

namespace {

struct SharedFixture {
  Network network = MakeNetwork("192.168.1.2");
  NullFactory factory;
  FakeSocket socket;
  std::unique_ptr<UDPPort> port;
  bool done = false;
  bool error = false;
  std::optional<IceCandidateErrorEvent> error_event;
  const Timestamp now = Timestamp::Seconds(10);

  explicit SharedFixture(SocketAddress local = Addr("192.168.1.2:5000")) : socket(local) {
    testing::InstallTestCrypto();
  }
  void Create(const ServerAddresses& servers, bool emit_local_for_anyaddress = false) {
    Port::Parameters args;
    args.network = &network;
    args.socket_factory = &factory;
    args.ice_username_fragment = "ufrag";
    args.ice_password = "passwordpasswordpassword";
    port = UDPPort::Create(now, args, &socket, emit_local_for_anyaddress, std::nullopt);
    port->set_server_addresses(servers);
    port->SubscribePortComplete(nullptr, [this](Port*) { done = true; });
    port->SubscribePortError(nullptr, [this](Port*) {
      done = true;
      error = true;
    });
    port->SubscribeCandidateError(nullptr, [this](Port*, const IceCandidateErrorEvent& e) { error_event = e; });
  }
  // The shared socket's owner hands the port what arrives.
  void Deliver(Timestamp t, const StunMessage& msg, const SocketAddress& from) {
    ByteBufferWriter buf;
    msg.Write(&buf);
    port->HandleIncomingPacket(&socket, ReceivedPacket{buf.DataView(), from, t});
  }
  // Answers the last request sent to `server` with `mapped`.
  void Answer(Timestamp t, const SocketAddress& server, const SocketAddress& mapped) {
    for (auto it = socket.sent.rbegin(); it != socket.sent.rend(); ++it) {
      if (it->to != server) continue;
      ByteBufferReader buf(it->data);
      StunMessage req;
      SM_ASSERT_TRUE(req.Read(&buf));
      StunMessage resp(STUN_BINDING_RESPONSE, req.transaction_id());
      sim::StunServer::GetStunBindResponse(req, mapped, &resp);
      Deliver(t, resp, server);
      return;
    }
  }
};

}  // namespace

// TestCreateUdpPort, TestSharedSocketPrepareAddress.
SM_TEST(P2pUdpPort, SharedSocketPrepareAddress) {
  SharedFixture f;
  f.Create({kStunAddr1});
  SM_EXPECT_TRUE(f.port->Type() == IceCandidateType::kHost);
  SM_EXPECT_EQ(f.port->Candidates().size(), size_t{0});
  f.port->PrepareAddress(f.now);
  SM_ASSERT_EQ(f.port->Candidates().size(), size_t{1});
  SM_EXPECT_TRUE(f.port->Candidates()[0].is_local());
  SM_EXPECT_EQ(f.port->Candidates()[0].address().ToString(), std::string("192.168.1.2:5000"));
  SM_EXPECT_FALSE(f.done);
  SM_ASSERT_EQ(f.socket.sent.size(), size_t{1});
  SM_EXPECT_TRUE(f.socket.sent[0].to == kStunAddr1);
  f.Answer(f.now + TimeDelta::Millis(20), kStunAddr1, Addr("5.5.5.5:7000"));
  SM_EXPECT_TRUE(f.done);
  SM_EXPECT_FALSE(f.error);
  SM_ASSERT_EQ(f.port->Candidates().size(), size_t{2});
  SM_EXPECT_TRUE(f.port->Candidates()[1].is_stun());
  SM_EXPECT_EQ(f.port->Candidates()[1].address().ToString(), std::string("5.5.5.5:7000"));
}

// A shared socket's reflexive address equal to its local address adds no
// candidate, and the port is complete even though nothing new was found.
SM_TEST(P2pUdpPort, SharedSocketSkipsReflexiveEqualToLocal) {
  SharedFixture f;
  f.Create({kStunAddr1});
  f.port->PrepareAddress(f.now);
  f.Answer(f.now, kStunAddr1, Addr("192.168.1.2:5000"));
  SM_EXPECT_TRUE(f.done);
  SM_EXPECT_EQ(f.port->Candidates().size(), size_t{1});
}

// A shared socket is complete (not failed) even when every server fails.
SM_TEST(P2pUdpPort, SharedSocketCompleteWhenServersFail) {
  SharedFixture f;
  f.Create({kBadAddr});
  f.port->PrepareAddress(f.now);
  for (Timestamp t = f.port->request_manager().NextProcessTime(); !f.done && t.IsFinite();
       t = f.port->request_manager().NextProcessTime()) {
    f.port->Process(t);
  }
  SM_EXPECT_TRUE(f.done);
  SM_EXPECT_FALSE(f.error);
}

// TestSocketCloseFailsPendingBindingRequest.
SM_TEST(P2pUdpPort, SocketCloseFailsPendingBindingRequest) {
  SharedFixture f;
  f.Create({kBadAddr});
  f.port->PrepareAddress(f.now);
  SM_ASSERT_TRUE(f.port->request_manager().HasRequestForTest(STUN_BINDING_REQUEST));
  SM_ASSERT_FALSE(f.done);
  constexpr int kSocketError = 1;
  f.socket.NotifyClosedForTest(kSocketError);
  SM_EXPECT_TRUE(f.done);  // Without advancing the clock.
  SM_EXPECT_FALSE(f.port->request_manager().HasRequestForTest(STUN_BINDING_REQUEST));
  SM_EXPECT_EQ(f.port->GetError(), kSocketError);
  SM_ASSERT_TRUE(f.error_event.has_value());
  SM_EXPECT_EQ(f.error_event->error_code, STUN_ERROR_SERVER_NOT_REACHABLE);
  SM_EXPECT_EQ(f.error_event->url, "stun:" + kBadAddr.ToString());
}

// No servers: complete with the host candidate at once.
SM_TEST(P2pUdpPort, NoServersCompleteAtOnce) {
  SharedFixture f;
  f.Create({});
  f.port->PrepareAddress(f.now);
  SM_EXPECT_TRUE(f.done);
  SM_EXPECT_FALSE(f.error);
  SM_EXPECT_EQ(f.port->Candidates().size(), size_t{1});
  SM_EXPECT_TRUE(f.socket.sent.empty());
}

// TestStunPacketsHaveDscpPacketOption.
SM_TEST(P2pUdpPort, StunPacketsHaveDscp) {
  SharedFixture f;
  f.Create({kStunAddr1});
  f.port->SetOption(SocketOption::kDscp, static_cast<int>(Dscp::kAf41));
  f.port->PrepareAddress(f.now);
  SM_ASSERT_EQ(f.socket.sent.size(), size_t{1});
  SM_EXPECT_TRUE(f.socket.sent[0].options.dscp == Dscp::kAf41);
  // Error responses from the port carry it too.
  auto in = WithUsername(STUN_BINDING_REQUEST, "wrong:x");
  in->AddMessageIntegrity("passwordpasswordpassword");
  in->AddFingerprint();
  f.port->HandleIncomingPacket(&f.socket, ReceivedPacket{Write(*in), Addr("10.0.0.9:9"), f.now});
  SM_ASSERT_EQ(f.socket.sent.size(), size_t{2});
  SM_EXPECT_TRUE(f.socket.sent[1].options.dscp == Dscp::kAf41);
}

// emit_local_for_anyaddress: bound to the any address, the host candidate is
// the network's default local address.
SM_TEST(P2pUdpPort, EmitLocalForAnyAddress) {
  for (bool emit : {false, true}) {
    SharedFixture f(Addr("0.0.0.0:5000"));
    f.network.set_default_local_address(Ip("192.168.1.7"));
    f.Create({}, emit);
    f.port->PrepareAddress(f.now);
    SM_ASSERT_EQ(f.port->Candidates().size(), size_t{1});
    SM_EXPECT_EQ(f.port->Candidates()[0].address().ToString(),
                 std::string(emit ? "192.168.1.7:5000" : "0.0.0.0:5000"));
  }
}

// The STUN server's answers are eaten even when stale; other STUN from
// unknown addresses goes to Port::OnReadPacket (here a 401).
SM_TEST(P2pUdpPort, ServerPacketsGoToRequests) {
  SharedFixture f;
  f.Create({kStunAddr1});
  f.port->SetIceRole(ICEROLE_CONTROLLED);
  f.port->PrepareAddress(f.now);
  f.Answer(f.now, kStunAddr1, Addr("5.5.5.5:7000"));
  f.Answer(f.now, kStunAddr1, Addr("5.5.5.5:7001"));  // A duplicate: no new candidate.
  SM_EXPECT_EQ(f.port->Candidates().size(), size_t{2});
  const size_t sent = f.socket.sent.size();
  auto in = WithUsername(STUN_BINDING_REQUEST, "bad:x");
  in->AddMessageIntegrity("passwordpasswordpassword");
  in->AddFingerprint();
  f.port->HandleIncomingPacket(&f.socket, ReceivedPacket{Write(*in), Addr("10.0.0.9:9"), f.now});
  SM_ASSERT_EQ(f.socket.sent.size(), sent + 1);
  ByteBufferReader buf(f.socket.sent.back().data);
  StunMessage resp;
  SM_ASSERT_TRUE(resp.Read(&buf));
  SM_EXPECT_EQ(resp.GetErrorCodeValue(), STUN_ERROR_UNAUTHORIZED);
}

// A non-shared port binds its own socket in the port range; none free fails
// Create.
SM_TEST(P2pUdpPort, OwnSocketInPortRange) {
  StunFixture f;
  Port::Parameters args = f.Args(f.client.get());
  auto a = UDPPort::Create(f.now, args, 6000, 6001, false, std::nullopt);
  auto b = UDPPort::Create(f.now, args, 6000, 6001, false, std::nullopt);
  auto c = UDPPort::Create(f.now, args, 6000, 6001, false, std::nullopt);
  SM_ASSERT_TRUE(a != nullptr);
  SM_ASSERT_TRUE(b != nullptr);
  SM_EXPECT_TRUE(c == nullptr);
  SM_EXPECT_EQ(a->GetLocalAddress().port(), 6000);
  SM_EXPECT_EQ(b->GetLocalAddress().port(), 6001);
  SM_EXPECT_FALSE(a->SharedSocket());
  // The port dies kPortTimeoutDelay later unless kept alive.
  a->KeepAliveUntilPruned();
  for (Timestamp t = b->NextProcessTime(); t.IsFinite(); t = b->NextProcessTime()) b->Process(t);
  SM_EXPECT_TRUE(b->dead());
  SM_EXPECT_FALSE(a->dead());
}
