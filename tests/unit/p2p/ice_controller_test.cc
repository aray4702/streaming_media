// The ICE controller layer (#1002, part of ICE #990): libwebrtc M157's ice_credentials_iterator_unittest.cc and
// wrapping_active_ice_controller_unittest.cc (compare/webrtc/reference/p2p/base/), with recording fakes in place of
// gmock and Process(now) in place of the simulated time controller, plus BasicIceController's pinging, sorting,
// switching, pruning and nomination rules (basic_ice_controller.cc) checked on real candidate pairs, and IceConfig's
// defaults (ice_transport_internal.cc).
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/basic_ice_controller.h"
#include "sm/p2p/byte_buffer.h"
#include "sm/p2p/candidate.h"
#include "sm/p2p/connection.h"
#include "sm/p2p/ice_controller_interface.h"
#include "sm/p2p/ice_credentials_iterator.h"
#include "sm/p2p/ice_switch_reason.h"
#include "sm/p2p/ice_transport_internal.h"
#include "sm/p2p/network.h"
#include "sm/p2p/port.h"
#include "sm/p2p/wrapping_active_ice_controller.h"
#include "sm_test.h"
#include "test_crypto.h"

using namespace sm;
using namespace sm::p2p;

namespace {

// ------------------------------------------------------------ fakes for wrapping_active_ice_controller_unittest.cc

const Connection* kConnection = reinterpret_cast<const Connection*>(0xabcd);
const Connection* kConnectionTwo = reinterpret_cast<const Connection*>(0xbcde);
const Connection* kConnectionThree = reinterpret_cast<const Connection*>(0xcdef);
constexpr TimeDelta kTick = TimeDelta::Millis(1);
const Timestamp kT0 = Timestamp::Seconds(1);

std::string Id(const Connection* c) {
  if (c == nullptr) return "null";
  if (c == kConnection) return "c1";
  if (c == kConnectionTwo) return "c2";
  if (c == kConnectionThree) return "c3";
  return "?";
}
std::string Ids(std::span<const Connection* const> cs) {
  std::string s = "[";
  for (const Connection* c : cs) s += (s.size() > 1 ? "," : "") + Id(c);
  return s + "]";
}

// MockIceAgent: records each call.
class FakeIceAgent : public IceAgentInterface {
 public:
  std::vector<std::string> log;
  Timestamp last_ping_sent = Timestamp::Millis(0);
  IceRole role = ICEROLE_CONTROLLING;

  Timestamp GetLastPingSent() const override {
    const_cast<FakeIceAgent*>(this)->log.push_back("GetLastPingSent");
    return last_ping_sent;
  }
  IceRole GetIceRole() const override { return role; }
  void OnStartedPinging() override { log.push_back("OnStartedPinging"); }
  void UpdateConnectionStates(Timestamp) override { log.push_back("UpdateConnectionStates"); }
  void UpdateState(Timestamp) override { log.push_back("UpdateState"); }
  void ForgetLearnedStateForConnections(std::span<const Connection* const> cs) override {
    log.push_back("Forget" + Ids(cs));
  }
  void SendPingRequest(Timestamp, const Connection* c) override { log.push_back("SendPingRequest " + Id(c)); }
  void SwitchSelectedConnection(Timestamp, const Connection* c, IceSwitchReason reason) override {
    log.push_back("Switch " + Id(c) + " " + IceSwitchReasonToString(reason));
  }
  bool PruneConnections(Timestamp, std::span<const Connection* const> cs) override {
    log.push_back("Prune" + Ids(cs));
    return true;
  }
};

// MockIceController: records each call into the agent's log (one sequence) and returns what the test set.
class FakeIceController : public IceControllerInterface {
 public:
  explicit FakeIceController(std::vector<std::string>* log) : log_(log) {}
  const IceConfig* config_seen = nullptr;
  bool has_pingable = false;
  bool use_candidate = false;
  std::optional<PingResult> ping_result;
  SwitchResult switch_result;
  std::vector<const Connection*> to_prune;
  const Connection* next_pingable = nullptr;

  void SetIceConfig(const IceConfig& config) override {
    config_seen = &config;
    log_->push_back("SetIceConfig");
  }
  void SetSelectedConnection(const Connection* c) override { log_->push_back("SetSelectedConnection " + Id(c)); }
  void AddConnection(const Connection* c) override { log_->push_back("AddConnection " + Id(c)); }
  void OnConnectionDestroyed(const Connection* c) override { log_->push_back("OnConnectionDestroyed " + Id(c)); }
  std::span<const Connection* const> GetConnections() const override { return {}; }
  bool HasPingableConnection(Timestamp) const override {
    log_->push_back("HasPingableConnection");
    return has_pingable;
  }
  PingResult GetConnectionToPing(Timestamp, Timestamp last_ping_sent) override {
    log_->push_back("GetConnectionToPing " + std::to_string(last_ping_sent.ms()));
    return ping_result.value_or(PingResult(nullptr, 0));
  }
  bool GetUseCandidateAttr(const Connection* c, NominationMode mode, IceMode remote_ice_mode) const override {
    log_->push_back("GetUseCandidateAttr " + Id(c) + (mode == NominationMode::AGGRESSIVE ? " aggressive" : " other") +
                    (remote_ice_mode == ICEMODE_LITE ? " lite" : " full"));
    return use_candidate;
  }
  const Connection* FindNextPingableConnection(Timestamp) override {
    log_->push_back("FindNextPingableConnection");
    return next_pingable;
  }
  void MarkConnectionPinged(const Connection* c) override { log_->push_back("MarkConnectionPinged " + Id(c)); }
  SwitchResult ShouldSwitchConnection(Timestamp, IceSwitchReason reason, const Connection* c) override {
    log_->push_back("ShouldSwitchConnection " + IceSwitchReasonToString(reason) + " " + Id(c));
    return std::exchange(switch_result, SwitchResult{});
  }
  SwitchResult SortAndSwitchConnection(Timestamp, IceSwitchReason reason) override {
    log_->push_back("SortAndSwitchConnection " + IceSwitchReasonToString(reason));
    return std::exchange(switch_result, SwitchResult{});
  }
  std::vector<const Connection*> PruneConnections() override {
    log_->push_back("PruneConnections");
    return std::exchange(to_prune, {});
  }

 private:
  std::vector<std::string>* log_;
};

struct WrappingFixture {
  FakeIceAgent agent;
  FakeIceController* wrapped = nullptr;
  std::unique_ptr<WrappingActiveIceController> controller;
  Timestamp now = kT0;

  WrappingFixture() {
    auto will_move = std::make_unique<FakeIceController>(&agent.log);
    wrapped = will_move.get();
    controller = std::make_unique<WrappingActiveIceController>(&agent, std::move(will_move));
  }
  // time_controller.AdvanceTime: runs every task due up to now + d.
  void Advance(TimeDelta d) {
    const Timestamp end = now + d;
    while (controller->NextProcessTime() <= end) {
      now = std::max(now, controller->NextProcessTime());
      controller->Process(now);
    }
    now = end;
  }
  std::vector<std::string> Take() { return std::exchange(agent.log, {}); }
};

bool Eq(const std::vector<std::string>& got, const std::vector<std::string>& want) {
  if (got == want) return true;
  std::string s = "got:";
  for (const auto& g : got) s += " {" + g + "}";
  std::printf("%s\n", s.c_str());
  return false;
}

class CountingFactory : public IceControllerFactoryInterface {
 public:
  int created = 0;
  std::unique_ptr<IceControllerInterface> Create(const IceControllerFactoryArgs& args) override {
    ++created;
    return std::make_unique<BasicIceController>(args);
  }
};

}  // namespace

// ------------------------------------------------------------ ice_credentials_iterator_unittest.cc

SM_TEST(P2pIceCredentialsIterator, GetEmpty) {
  testing::InstallTestCrypto();
  std::vector<IceParameters> empty;
  IceCredentialsIterator iterator(empty);
  // Verify that we can get credentials even if input is empty.
  IceParameters credentials1 = iterator.GetIceCredentials();
  SM_EXPECT_EQ(credentials1.ufrag.size(), size_t{ICE_UFRAG_LENGTH});
  SM_EXPECT_EQ(credentials1.pwd.size(), size_t{ICE_PWD_LENGTH});
}

SM_TEST(P2pIceCredentialsIterator, GetOne) {
  testing::InstallTestCrypto();
  std::vector<IceParameters> one = {IceCredentialsIterator::CreateRandomIceCredentials()};
  IceCredentialsIterator iterator(one);
  SM_EXPECT_TRUE(iterator.GetIceCredentials() == one[0]);
  auto random = iterator.GetIceCredentials();
  SM_EXPECT_TRUE(random != one[0]);
  SM_EXPECT_TRUE(random != iterator.GetIceCredentials());
}

SM_TEST(P2pIceCredentialsIterator, GetTwo) {
  testing::InstallTestCrypto();
  std::vector<IceParameters> two = {IceCredentialsIterator::CreateRandomIceCredentials(),
                                    IceCredentialsIterator::CreateRandomIceCredentials()};
  IceCredentialsIterator iterator(two);
  SM_EXPECT_TRUE(iterator.GetIceCredentials() == two[1]);
  SM_EXPECT_TRUE(iterator.GetIceCredentials() == two[0]);
  auto random = iterator.GetIceCredentials();
  SM_EXPECT_TRUE(random != two[0]);
  SM_EXPECT_TRUE(random != two[1]);
  SM_EXPECT_TRUE(random != iterator.GetIceCredentials());
}

// ------------------------------------------------------------ wrapping_active_ice_controller_unittest.cc

SM_TEST(P2pWrappingActiveIceController, CreateLegacyIceControllerFromFactory) {
  FakeIceAgent agent;
  IceFieldTrials trials;
  IceControllerFactoryArgs args;
  args.ice_field_trials = &trials;
  CountingFactory legacy_controller_factory;
  WrappingActiveIceController controller(&agent, &legacy_controller_factory, args);
  SM_EXPECT_EQ(legacy_controller_factory.created, 1);
  // Without a factory it wraps a BasicIceController.
  WrappingActiveIceController basic(&agent, nullptr, args);
  SM_EXPECT_TRUE(basic.FindNextPingableConnection(kT0) == nullptr);
}

SM_TEST(P2pWrappingActiveIceController, PassthroughIceControllerInterface) {
  WrappingFixture f;
  IceConfig config{};
  f.controller->SetIceConfig(config);
  SM_EXPECT_TRUE(f.wrapped->config_seen == &config);

  f.wrapped->use_candidate = true;
  SM_EXPECT_TRUE(f.controller->GetUseCandidateAttribute(kConnection, NominationMode::AGGRESSIVE, ICEMODE_LITE));
  f.controller->OnConnectionAdded(kConnection);
  f.controller->OnConnectionDestroyed(kConnection);
  f.controller->OnConnectionSwitched(kConnection);
  f.controller->OnConnectionPinged(kConnection);
  f.wrapped->next_pingable = kConnection;
  SM_EXPECT_TRUE(f.controller->FindNextPingableConnection(f.now) == kConnection);
  SM_EXPECT_TRUE(Eq(f.Take(), {"SetIceConfig", "GetUseCandidateAttr c1 aggressive lite", "AddConnection c1",
                               "OnConnectionDestroyed c1", "SetSelectedConnection c1", "MarkConnectionPinged c1",
                               "FindNextPingableConnection"}));
}

SM_TEST(P2pWrappingActiveIceController, HandlesImmediateSwitchRequest) {
  WrappingFixture f;
  const IceSwitchReason reason = IceSwitchReason::NOMINATION_ON_CONTROLLED_SIDE;
  const int recheck_delay_ms = 10;
  f.wrapped->switch_result = {.connection = kConnection,
                              .recheck_event = IceRecheckEvent(IceSwitchReason::ICE_CONTROLLER_RECHECK,
                                                               recheck_delay_ms),
                              .connections_to_forget_state_on = {kConnectionTwo}};

  // ICE controller should switch to given connection immediately.
  SM_EXPECT_TRUE(f.controller->OnImmediateSwitchRequest(f.now, reason, kConnection));
  SM_EXPECT_TRUE(Eq(f.Take(), {"ShouldSwitchConnection nomination on the controlled side c1",
                               "Switch c1 nomination on the controlled side", "Forget[c2]"}));

  // No rechecks before recheck delay.
  f.Advance(TimeDelta::Millis(recheck_delay_ms - 1));
  SM_EXPECT_TRUE(f.Take().empty());

  // ICE controller should recheck for best connection after the recheck delay.
  f.Advance(kTick);
  SM_EXPECT_TRUE(Eq(f.Take(), {"UpdateConnectionStates", "SortAndSwitchConnection ice-controller-request-recheck",
                               "Forget[]", "PruneConnections", "Prune[]", "UpdateState", "HasPingableConnection"}));
}

SM_TEST(P2pWrappingActiveIceController, HandlesImmediateSortAndSwitchRequest) {
  WrappingFixture f;
  const IceSwitchReason reason = IceSwitchReason::NEW_CONNECTION_FROM_LOCAL_CANDIDATE;
  const int recheck_delay_ms = 10;
  f.wrapped->switch_result = {.connection = kConnection,
                              .recheck_event = IceRecheckEvent(IceSwitchReason::ICE_CONTROLLER_RECHECK,
                                                               recheck_delay_ms),
                              .connections_to_forget_state_on = {kConnectionTwo}};
  f.wrapped->to_prune = {kConnectionThree};

  f.controller->OnImmediateSortAndSwitchRequest(f.now, reason);
  SM_EXPECT_TRUE(Eq(f.Take(), {"UpdateConnectionStates",
                               "SortAndSwitchConnection new candidate pairs created from a new local candidate",
                               "Switch c1 new candidate pairs created from a new local candidate", "Forget[c2]",
                               "PruneConnections", "Prune[c3]", "UpdateState", "HasPingableConnection"}));

  // No rechecks before recheck delay.
  f.Advance(TimeDelta::Millis(recheck_delay_ms - 1));
  SM_EXPECT_TRUE(f.Take().empty());

  // ICE controller should recheck for best connection after the recheck delay.
  f.Advance(kTick);
  SM_EXPECT_TRUE(Eq(f.Take(), {"UpdateConnectionStates", "SortAndSwitchConnection ice-controller-request-recheck",
                               "Forget[]", "PruneConnections", "Prune[]", "UpdateState", "HasPingableConnection"}));
}

SM_TEST(P2pWrappingActiveIceController, HandlesSortAndSwitchRequest) {
  WrappingFixture f;
  const IceSwitchReason reason = IceSwitchReason::NETWORK_PREFERENCE_CHANGE;

  // No action should occur immediately
  f.controller->OnSortAndSwitchRequest(f.now, reason);
  SM_EXPECT_TRUE(f.Take().empty());
  // A second request while one is pending posts nothing more.
  f.controller->OnSortAndSwitchRequest(f.now, IceSwitchReason::DATA_RECEIVED);

  f.wrapped->switch_result = {.connection = kConnection,
                              .recheck_event = IceRecheckEvent(IceSwitchReason::ICE_CONTROLLER_RECHECK, 10),
                              .connections_to_forget_state_on = {kConnectionTwo}};

  // Sort and switch should take place as the subsequent task.
  f.Advance(TimeDelta::Zero());
  SM_EXPECT_TRUE(Eq(f.Take(), {"UpdateConnectionStates", "SortAndSwitchConnection network preference changed",
                               "Switch c1 network preference changed", "Forget[c2]", "PruneConnections", "Prune[]",
                               "UpdateState", "HasPingableConnection"}));
}

SM_TEST(P2pWrappingActiveIceController, StartPingingAfterSortAndSwitch) {
  WrappingFixture f;
  // Pinging does not start automatically, unless triggered through a sort.
  f.controller->OnSortAndSwitchRequest(f.now, IceSwitchReason::DATA_RECEIVED);
  SM_EXPECT_TRUE(f.Take().empty());

  // Pinging does not start if no pingable connection.
  f.wrapped->has_pingable = false;
  f.Advance(TimeDelta::Zero());
  SM_EXPECT_TRUE(Eq(f.Take(), {"UpdateConnectionStates", "SortAndSwitchConnection data received", "Forget[]",
                               "PruneConnections", "Prune[]", "UpdateState", "HasPingableConnection"}));

  const TimeDelta recheck_delay = TimeDelta::Millis(10);
  // Pinging starts when there is a pingable connection.
  f.wrapped->has_pingable = true;
  f.wrapped->ping_result.emplace(kConnection, recheck_delay);
  f.agent.last_ping_sent = Timestamp::Millis(123);
  f.controller->OnSortAndSwitchRequest(f.now, IceSwitchReason::DATA_RECEIVED);
  f.Advance(kTick);
  SM_EXPECT_TRUE(Eq(f.Take(), {"UpdateConnectionStates", "SortAndSwitchConnection data received", "Forget[]",
                               "PruneConnections", "Prune[]", "UpdateState", "HasPingableConnection",
                               "OnStartedPinging", "UpdateConnectionStates", "GetLastPingSent",
                               "GetConnectionToPing 123", "SendPingRequest c1"}));

  // ICE controller should recheck and ping after the recheck delay. No ping should be sent if no connection selected
  // to ping.
  f.agent.last_ping_sent = Timestamp::Millis(456);
  f.wrapped->ping_result.emplace(nullptr, recheck_delay);
  f.Advance(recheck_delay);
  SM_EXPECT_TRUE(Eq(f.Take(), {"UpdateConnectionStates", "GetLastPingSent", "GetConnectionToPing 456"}));

  // A sort does not start pinging twice.
  f.controller->OnImmediateSortAndSwitchRequest(f.now, IceSwitchReason::DATA_RECEIVED);
  SM_EXPECT_TRUE(Eq(f.Take(), {"UpdateConnectionStates", "SortAndSwitchConnection data received", "Forget[]",
                               "PruneConnections", "Prune[]", "UpdateState"}));
}

// WrappingActiveIceController::PruneConnections: the controlled side prunes only once its selected pair is nominated.
SM_TEST(P2pWrappingActiveIceController, ControlledSidePrunesOnlyWhenNominated) {
  WrappingFixture f;
  f.agent.role = ICEROLE_CONTROLLED;
  f.controller->OnImmediateSortAndSwitchRequest(f.now, IceSwitchReason::DATA_RECEIVED);
  SM_EXPECT_TRUE(Eq(f.Take(), {"UpdateConnectionStates", "SortAndSwitchConnection data received", "Forget[]",
                               "UpdateState", "HasPingableConnection"}));
}

// ------------------------------------------------------------ BasicIceController on real candidate pairs

namespace {

SocketAddress Addr(std::string_view s) { return *SocketAddress::FromString(s); }

Network MakeNetwork(std::string name, const SocketAddress& addr, AdapterType type) {
  Network n(std::move(name), addr.ipaddr(), 32, type);
  n.AddIp(addr.ipaddr());
  return n;
}

class NullFactory final : public PacketSocketFactory {
 public:
  std::unique_ptr<AsyncPacketSocket> CreateUdpSocket(const SocketAddress&, uint16_t, uint16_t) override {
    return nullptr;
  }
};

// A host port whose sends succeed (p2p/test/test_port.h without the STUN capture).
class TestPort final : public Port {
 public:
  TestPort(Timestamp now, const Parameters& args) : Port(args, IceCandidateType::kHost, 0, 0, false, now) {}
  bool SupportsProtocol(std::string_view protocol) const override { return protocol == UDP_PROTOCOL_NAME; }
  ProtocolType GetProtocol() const override { return PROTO_UDP; }
  void PrepareAddress(Timestamp) override {
    const SocketAddress addr(network()->GetBestIP(), 5000);
    AddAddress(addr, addr, SocketAddress(), "udp", "", "", IceCandidateType::kHost, ICE_TYPE_PREFERENCE_HOST, 0, "",
               true);
  }
  Connection* CreateConnection(Timestamp now, const Candidate& remote_candidate, CandidateOrigin) override {
    auto conn = std::make_unique<ProxyConnection>(now, this, 0, remote_candidate);
    Connection* raw = conn.get();
    AddOrReplaceConnection(std::move(conn));
    return raw;
  }
  int SendTo(Timestamp, std::span<const uint8_t> data, const SocketAddress&, const PacketOptions&, bool) override {
    return static_cast<int>(data.size());
  }
  int SetOption(SocketOption, int) override { return 0; }
  int GetOption(SocketOption, int*) override { return -1; }
  int GetError() override { return 0; }
};

const Timestamp kStart = Timestamp::Seconds(100);

struct ControllerFixture {
  Network wifi = MakeNetwork("wlan0", Addr("192.168.1.2:0"), ADAPTER_TYPE_WIFI);
  Network cell = MakeNetwork("cell0", Addr("10.0.0.2:0"), ADAPTER_TYPE_CELLULAR);
  Network vpn = MakeNetwork("tun0", Addr("10.8.0.2:0"), ADAPTER_TYPE_VPN);
  NullFactory factory;
  Timestamp now = kStart;
  IceTransportStateInternal state = IceTransportStateInternal::kConnecting;
  IceRole role = ICEROLE_CONTROLLING;
  IceFieldTrials trials;
  std::vector<std::unique_ptr<TestPort>> ports;
  std::unique_ptr<BasicIceController> controller;

  ControllerFixture() {
    testing::InstallTestCrypto();
    IceControllerFactoryArgs args;
    args.ice_transport_state_func = [this] { return state; };
    args.ice_role_func = [this] { return role; };
    args.is_connection_pruned_func = [](const Connection*) { return false; };
    args.ice_field_trials = &trials;
    controller = std::make_unique<BasicIceController>(args);
  }

  TestPort* PortOn(Network* network) {
    Port::Parameters p;
    p.network = network;
    p.socket_factory = &factory;
    p.ice_username_fragment = "lfrag";
    p.ice_password = "lpasswordlpasswordlpass";
    p.ice_tiebreaker = 1111;
    ports.push_back(std::make_unique<TestPort>(now, p));
    TestPort* port = ports.back().get();
    port->SetIceRole(role);
    port->PrepareAddress(now);
    return port;
  }

  // A pair from `port` to a remote host candidate at `remote` with `priority`; added to the controller.
  Connection* Pair(TestPort* port, std::string_view remote, uint32_t priority) {
    Candidate c;
    c.set_component(ICE_CANDIDATE_COMPONENT_RTP);
    c.set_protocol(UDP_PROTOCOL_NAME);
    c.set_address(Addr(remote));
    c.set_priority(priority);
    c.set_username("rfrag");
    c.set_password("rpasswordrpasswordrpass");
    c.set_type(IceCandidateType::kHost);
    Connection* conn = port->CreateConnection(now, c, Port::ORIGIN_MESSAGE);
    controller->AddConnection(conn);
    return conn;
  }

  // A response to a check after `rtt`: writable and receiving.
  static void MakeWritable(Connection* conn, Timestamp at, TimeDelta rtt = TimeDelta::Millis(50)) {
    conn->ReceivedPingResponse(at, rtt, "id");
  }
  // One more check, unanswered: the pair stays writable (5 failures and 5 s make it unreliable) but is not receiving
  // once the receiving timeout (2.5 s) has passed since the last response.
  static void StopReceiving(Connection* conn, Timestamp last_response, Timestamp now) {
    conn->Ping(last_response + TimeDelta::Millis(1));
    conn->UpdateState(now);
  }
};

}  // namespace

// IceConfig's defaults are libwebrtc's constants, and IsValid checks them as ice_transport_internal.cc does.
SM_TEST(P2pIceConfig, DefaultsAndValidation) {
  IceConfig config;
  SM_EXPECT_EQ(config.receiving_timeout_or_default().ms(), 2500);
  SM_EXPECT_EQ(config.backup_connection_ping_interval_or_default().ms(), 25000);
  SM_EXPECT_EQ(config.stable_writable_connection_ping_interval_or_default().ms(), 2500);
  SM_EXPECT_EQ(config.regather_on_failed_networks_interval_or_default().ms(), 300000);
  SM_EXPECT_EQ(config.receiving_switching_delay_or_default().ms(), 1000);
  SM_EXPECT_EQ(config.ice_check_interval_strong_connectivity_or_default().ms(), 480);
  SM_EXPECT_EQ(config.ice_check_interval_weak_connectivity_or_default().ms(), 48);
  SM_EXPECT_EQ(config.ice_check_min_interval_or_default().ms(), -1);
  SM_EXPECT_EQ(config.ice_unwritable_timeout_or_default().ms(), 5000);
  SM_EXPECT_EQ(config.ice_unwritable_min_checks_or_default(), 5);
  SM_EXPECT_EQ(config.ice_inactive_timeout_or_default().ms(), 15000);
  SM_EXPECT_EQ(config.stun_keepalive_interval_or_default().ms(), 10000);
  SM_EXPECT_TRUE(config.default_nomination_mode == NominationMode::SEMI_AGGRESSIVE);
  SM_EXPECT_FALSE(config.gather_continually());
  SM_EXPECT_FALSE(config.IsValid().has_value());

  IceConfig strong_below_weak;
  strong_below_weak.ice_check_interval_strong_connectivity = TimeDelta::Millis(40);
  SM_EXPECT_TRUE(strong_below_weak.IsValid().has_value());
  IceConfig receiving_below_ping;
  receiving_below_ping.receiving_timeout = TimeDelta::Millis(400);
  SM_EXPECT_TRUE(receiving_below_ping.IsValid().has_value());
  IceConfig backup_below_strong;
  backup_below_strong.backup_connection_ping_interval = TimeDelta::Millis(100);
  SM_EXPECT_TRUE(backup_below_strong.IsValid().has_value());
  IceConfig unwritable_after_inactive;
  unwritable_after_inactive.ice_unwritable_timeout = TimeDelta::Seconds(20);
  SM_EXPECT_TRUE(unwritable_after_inactive.IsValid().has_value());
  SM_EXPECT_EQ(IceRecheckEvent(IceSwitchReason::DATA_RECEIVED, 0).ToString(), std::string("data received"));
  SM_EXPECT_EQ(IceRecheckEvent(IceSwitchReason::DATA_RECEIVED, 5).ToString(),
               std::string("data received (after delay: 5)"));
}

// Sorting and switching: nothing is selected before a pair is ready to send; a writable pair is selected over a
// higher-priority one that is not; once both are writable and receiving, the higher priority wins.
SM_TEST(P2pBasicIceController, SortPrefersWritableThenPriority) {
  ControllerFixture f;
  TestPort* port = f.PortOn(&f.wifi);
  Connection* hi = f.Pair(port, "1.1.1.1:1000", 2000);
  Connection* lo = f.Pair(port, "1.1.1.2:1000", 1000);
  SM_ASSERT_TRUE(hi->priority() > lo->priority());

  auto r = f.controller->SortAndSwitchConnection(f.now, IceSwitchReason::NEW_CONNECTION_FROM_REMOTE_CANDIDATE);
  SM_EXPECT_FALSE(r.connection.has_value());
  SM_EXPECT_TRUE(f.controller->GetConnections()[0] == hi);

  ControllerFixture::MakeWritable(lo, f.now);
  r = f.controller->SortAndSwitchConnection(f.now, IceSwitchReason::CONNECT_STATE_CHANGE);
  SM_ASSERT_TRUE(r.connection.has_value());
  SM_EXPECT_TRUE(*r.connection == lo);
  SM_EXPECT_TRUE(f.controller->GetConnections()[0] == lo);
  f.controller->SetSelectedConnection(lo);

  f.now += TimeDelta::Millis(100);
  ControllerFixture::MakeWritable(hi, f.now);
  r = f.controller->SortAndSwitchConnection(f.now, IceSwitchReason::CONNECT_STATE_CHANGE);
  SM_ASSERT_TRUE(r.connection.has_value());
  SM_EXPECT_TRUE(*r.connection == hi);
  SM_EXPECT_FALSE(r.recheck_event.has_value());
  // Asking again for the selected pair switches nothing.
  f.controller->SetSelectedConnection(hi);
  SM_EXPECT_FALSE(f.controller->ShouldSwitchConnection(f.now, IceSwitchReason::DATA_RECEIVED, hi).connection);
}

// The receiving switching delay (1 s): a receiving pair does not replace a selected pair that stopped receiving until
// both states are 1 s old; until then the result asks for a recheck after the delay.
SM_TEST(P2pBasicIceController, ReceivingSwitchingDelay) {
  ControllerFixture f;
  TestPort* port = f.PortOn(&f.wifi);
  Connection* hi = f.Pair(port, "1.1.1.1:1000", 2000);
  Connection* lo = f.Pair(port, "1.1.1.2:1000", 1000);
  ControllerFixture::MakeWritable(hi, f.now);
  f.controller->SetSelectedConnection(hi);

  // `hi` stops receiving after the receiving timeout (2.5 s).
  const Timestamp hi_response = f.now;
  f.now += TimeDelta::Millis(2600);
  ControllerFixture::StopReceiving(hi, hi_response, f.now);
  SM_ASSERT_TRUE(hi->writable());
  SM_ASSERT_FALSE(hi->receiving());
  ControllerFixture::MakeWritable(lo, f.now);
  SM_ASSERT_TRUE(lo->receiving());

  auto r = f.controller->ShouldSwitchConnection(f.now, IceSwitchReason::DATA_RECEIVED, lo);
  SM_EXPECT_FALSE(r.connection.has_value());
  SM_ASSERT_TRUE(r.recheck_event.has_value());
  SM_EXPECT_EQ(r.recheck_event->recheck_delay_ms, 1000);
  SM_EXPECT_TRUE(r.recheck_event->reason == IceSwitchReason::DATA_RECEIVED);

  f.now += TimeDelta::Millis(999);
  SM_EXPECT_FALSE(f.controller->ShouldSwitchConnection(f.now, IceSwitchReason::DATA_RECEIVED, lo).connection);
  f.now += TimeDelta::Millis(1);
  r = f.controller->ShouldSwitchConnection(f.now, IceSwitchReason::ICE_CONTROLLER_RECHECK, lo);
  SM_ASSERT_TRUE(r.connection.has_value());
  SM_EXPECT_TRUE(*r.connection == lo);
}

// Equal pairs switch only for an RTT at least kMinImprovement (10 ms) lower.
SM_TEST(P2pBasicIceController, SwitchesForRttImprovementOnly) {
  ControllerFixture f;
  TestPort* port = f.PortOn(&f.wifi);
  Connection* a = f.Pair(port, "1.1.1.1:1000", 1500);
  Connection* b = f.Pair(port, "1.1.1.2:1000", 1500);
  SM_ASSERT_EQ(a->priority(), b->priority());
  ControllerFixture::MakeWritable(a, f.now, TimeDelta::Millis(100));
  ControllerFixture::MakeWritable(b, f.now, TimeDelta::Millis(95));
  f.controller->SetSelectedConnection(a);
  SM_EXPECT_FALSE(f.controller->ShouldSwitchConnection(f.now, IceSwitchReason::DATA_RECEIVED, b).connection);

  Connection* c = f.Pair(port, "1.1.1.3:1000", 1500);
  ControllerFixture::MakeWritable(c, f.now, TimeDelta::Millis(90));
  auto r = f.controller->ShouldSwitchConnection(f.now, IceSwitchReason::DATA_RECEIVED, c);
  SM_ASSERT_TRUE(r.connection.has_value());
  SM_EXPECT_TRUE(*r.connection == c);
  // Sorting puts the lowest RTT first among equal pairs.
  f.controller->SortAndSwitchConnection(f.now, IceSwitchReason::DATA_RECEIVED);
  SM_EXPECT_TRUE(f.controller->GetConnections()[0] == c);
  SM_EXPECT_TRUE(f.controller->GetConnections()[2] == a);
}

// A pair on a costlier network does not replace the selected one unless it is receiving.
SM_TEST(P2pBasicIceController, NoSwitchToCostlierNetworkUnlessReceiving) {
  ControllerFixture f;
  Connection* wifi = f.Pair(f.PortOn(&f.wifi), "1.1.1.1:1000", 1000);
  Connection* cell = f.Pair(f.PortOn(&f.cell), "1.1.1.1:1000", 1000);
  SM_ASSERT_TRUE(cell->ComputeNetworkCost() > wifi->ComputeNetworkCost());
  ControllerFixture::MakeWritable(wifi, f.now);
  f.controller->SetSelectedConnection(wifi);
  ControllerFixture::MakeWritable(cell, f.now);
  const Timestamp cell_response = f.now;
  f.now += TimeDelta::Millis(2600);
  ControllerFixture::StopReceiving(cell, cell_response, f.now);
  SM_ASSERT_FALSE(cell->receiving());
  SM_EXPECT_FALSE(f.controller->ShouldSwitchConnection(f.now, IceSwitchReason::DATA_RECEIVED, cell).connection);
  SM_EXPECT_FALSE(f.controller->ShouldSwitchConnection(f.now, IceSwitchReason::DATA_RECEIVED, cell).recheck_event);
}

// The VPN preference sorts VPN pairs first (kPreferVpn) or last (kAvoidVpn), ahead of the network cost.
SM_TEST(P2pBasicIceController, VpnPreference) {
  for (VpnPreference pref : {VpnPreference::kPreferVpn, VpnPreference::kAvoidVpn}) {
    ControllerFixture f;
    IceConfig config;
    config.vpn_preference = pref;
    f.controller->SetIceConfig(config);
    Connection* vpn = f.Pair(f.PortOn(&f.vpn), "1.1.1.1:1000", 1000);
    Connection* wifi = f.Pair(f.PortOn(&f.wifi), "1.1.1.1:1000", 1000);
    ControllerFixture::MakeWritable(vpn, f.now);
    ControllerFixture::MakeWritable(wifi, f.now);
    auto r = f.controller->SortAndSwitchConnection(f.now, IceSwitchReason::NETWORK_PREFERENCE_CHANGE);
    SM_ASSERT_TRUE(r.connection.has_value());
    SM_EXPECT_TRUE(*r.connection == (pref == VpnPreference::kPreferVpn ? vpn : wifi));
  }
}

// On the controlled side the remote nomination comes before the priority.
SM_TEST(P2pBasicIceController, ControlledSidePrefersNominated) {
  ControllerFixture f;
  f.role = ICEROLE_CONTROLLED;
  TestPort* port = f.PortOn(&f.wifi);
  Connection* hi = f.Pair(port, "1.1.1.1:1000", 2000);
  Connection* lo = f.Pair(port, "1.1.1.2:1000", 1000);
  ControllerFixture::MakeWritable(hi, f.now);
  ControllerFixture::MakeWritable(lo, f.now);
  lo->set_remote_nomination(1);
  auto r = f.controller->SortAndSwitchConnection(f.now, IceSwitchReason::NOMINATION_ON_CONTROLLED_SIDE);
  SM_ASSERT_TRUE(r.connection.has_value());
  SM_EXPECT_TRUE(*r.connection == lo);
  // The controlling side would pick the higher priority.
  f.role = ICEROLE_CONTROLLING;
  r = f.controller->SortAndSwitchConnection(f.now, IceSwitchReason::DATA_RECEIVED);
  SM_ASSERT_TRUE(r.connection.has_value());
  SM_EXPECT_TRUE(*r.connection == hi);
}

// The ping order on a weak channel: unpinged pairs first, in the sort order; then the least recently pinged; a
// triggered check (a check arrived after our last ping) comes before both.
SM_TEST(P2pBasicIceController, PingOrder) {
  ControllerFixture f;
  TestPort* port = f.PortOn(&f.wifi);
  Connection* c1 = f.Pair(port, "1.1.1.1:1000", 3000);
  Connection* c2 = f.Pair(port, "1.1.1.2:1000", 2000);
  Connection* c3 = f.Pair(port, "1.1.1.3:1000", 1000);
  SM_EXPECT_TRUE(f.controller->HasPingableConnection(f.now));
  auto ping = [&](const Connection* expected) {
    const Connection* next = f.controller->FindNextPingableConnection(f.now);
    if (next != expected) return false;
    const_cast<Connection*>(next)->Ping(f.now);
    f.controller->MarkConnectionPinged(next);
    f.now += TimeDelta::Millis(48);
    return true;
  };
  SM_EXPECT_TRUE(ping(c1));
  SM_EXPECT_TRUE(ping(c2));
  SM_EXPECT_TRUE(ping(c3));
  // All pinged: the least recently pinged next.
  SM_EXPECT_TRUE(ping(c1));
  // A check from the remote side on c3 makes it a triggered check.
  c3->ReceivedPing(f.now);
  SM_EXPECT_TRUE(ping(c3));
  SM_EXPECT_TRUE(ping(c2));
}

// GetConnectionToPing: a weak channel pings at the weak interval (48 ms), the recheck delay is min(interval,
// receiving timeout / 10); a strong one at the strong interval (480 ms) once every pair has had 3 pings.
SM_TEST(P2pBasicIceController, PingIntervals) {
  ControllerFixture f;
  TestPort* port = f.PortOn(&f.wifi);
  Connection* c = f.Pair(port, "1.1.1.1:1000", 1000);
  const auto r1 = f.controller->GetConnectionToPing(f.now, f.now - TimeDelta::Millis(47));
  SM_EXPECT_FALSE(r1.connection.has_value());
  SM_EXPECT_EQ(r1.recheck_delay_ms, 48);
  const auto r2 = f.controller->GetConnectionToPing(f.now, f.now - TimeDelta::Millis(48));
  SM_ASSERT_TRUE(r2.connection.has_value());
  SM_EXPECT_TRUE(*r2.connection == c);

  for (int i = 0; i < kMinPingsAtWeakPingInterval; ++i) {
    c->Ping(f.now);
    f.now += TimeDelta::Millis(48);
  }
  ControllerFixture::MakeWritable(c, f.now);
  f.controller->SetSelectedConnection(c);
  const auto r3 = f.controller->GetConnectionToPing(f.now, f.now - TimeDelta::Millis(479));
  SM_EXPECT_FALSE(r3.connection.has_value());
  // min(480 ms, 2500 ms / 10)
  SM_EXPECT_EQ(r3.recheck_delay_ms, 250);
  // The selected pair is pinged at 900 ms until it is stable.
  SM_EXPECT_TRUE(f.controller->FindNextPingableConnection(f.now) == nullptr);
  f.now += TimeDelta::Millis(900);
  SM_EXPECT_TRUE(f.controller->FindNextPingableConnection(f.now) == c);
}

// Backup pairs (state completed, not selected) are pinged every 25 s once they have an RTT sample.
SM_TEST(P2pBasicIceController, BackupPingInterval) {
  ControllerFixture f;
  f.state = IceTransportStateInternal::kCompleted;
  TestPort* port = f.PortOn(&f.wifi);
  Connection* selected = f.Pair(port, "1.1.1.1:1000", 2000);
  Connection* backup = f.Pair(port, "1.1.1.2:1000", 1000);
  ControllerFixture::MakeWritable(selected, f.now);
  ControllerFixture::MakeWritable(backup, f.now);
  f.controller->SetSelectedConnection(selected);
  // Keep the selected pair recently pinged so only the backup can be picked.
  selected->Ping(f.now);
  f.now += TimeDelta::Millis(100);
  const Timestamp response_at = backup->LastPingResponseReceived();
  auto at = [&](TimeDelta since_response) {
    selected->Ping(response_at + since_response);
    selected->ReceivedPingResponse(response_at + since_response, TimeDelta::Millis(50), "id");
    return f.controller->FindNextPingableConnection(response_at + since_response);
  };
  SM_EXPECT_TRUE(at(TimeDelta::Seconds(24)) == nullptr);
  SM_EXPECT_TRUE(at(TimeDelta::Seconds(25)) == backup);
}

// Pruning: a pair is pruned when a strong pair on its network is at least as good by candidates; a pair on another
// network is kept, and nothing is pruned against a weak pair.
SM_TEST(P2pBasicIceController, PruneConnections) {
  ControllerFixture f;
  TestPort* wifi_port = f.PortOn(&f.wifi);
  Connection* best = f.Pair(wifi_port, "1.1.1.1:1000", 2000);
  Connection* worse = f.Pair(wifi_port, "1.1.1.2:1000", 1000);
  Connection* other_network = f.Pair(f.PortOn(&f.cell), "1.1.1.1:1000", 1000);
  SM_EXPECT_TRUE(f.controller->PruneConnections().empty());  // No selected pair, and nothing writable.
  ControllerFixture::MakeWritable(best, f.now);
  f.controller->SetSelectedConnection(best);
  f.controller->SortAndSwitchConnection(f.now, IceSwitchReason::CONNECT_STATE_CHANGE);
  const std::vector<const Connection*> pruned = f.controller->PruneConnections();
  SM_ASSERT_EQ(pruned.size(), size_t{1});
  SM_EXPECT_TRUE(pruned[0] == worse);
  (void)other_network;
  // A weak best pair prunes nothing.
  const Timestamp best_response = f.now;
  f.now += TimeDelta::Millis(2600);
  ControllerFixture::StopReceiving(best, best_response, f.now);
  SM_ASSERT_TRUE(best->weak());
  SM_EXPECT_TRUE(f.controller->PruneConnections().empty());
}

// USE-CANDIDATE: semi-aggressive nomination (the default) nominates the selected pair, any pair while nothing writable
// is selected, and a pair better than the selected one; with an ICE-lite remote only the writable selected pair.
// Aggressive nominates every pair unless the remote is lite; regular nominates none.
SM_TEST(P2pBasicIceController, UseCandidateAttribute) {
  ControllerFixture f;
  TestPort* port = f.PortOn(&f.wifi);
  Connection* hi = f.Pair(port, "1.1.1.1:1000", 2000);
  Connection* lo = f.Pair(port, "1.1.1.2:1000", 1000);
  const auto semi = NominationMode::SEMI_AGGRESSIVE;
  SM_EXPECT_TRUE(f.controller->GetUseCandidateAttr(lo, semi, ICEMODE_FULL));
  SM_EXPECT_FALSE(f.controller->GetUseCandidateAttr(lo, semi, ICEMODE_LITE));
  f.controller->SetSelectedConnection(lo);
  SM_EXPECT_TRUE(f.controller->GetUseCandidateAttr(hi, semi, ICEMODE_FULL));  // The selected pair is unwritable.
  ControllerFixture::MakeWritable(lo, f.now);
  SM_EXPECT_TRUE(f.controller->GetUseCandidateAttr(hi, semi, ICEMODE_FULL));  // Better than the selected pair.
  SM_EXPECT_TRUE(f.controller->GetUseCandidateAttr(lo, semi, ICEMODE_FULL));
  SM_EXPECT_TRUE(f.controller->GetUseCandidateAttr(lo, semi, ICEMODE_LITE));
  SM_EXPECT_FALSE(f.controller->GetUseCandidateAttr(hi, semi, ICEMODE_LITE));
  f.controller->SetSelectedConnection(hi);
  ControllerFixture::MakeWritable(hi, f.now);
  SM_EXPECT_FALSE(f.controller->GetUseCandidateAttr(lo, semi, ICEMODE_FULL));
  SM_EXPECT_TRUE(f.controller->GetUseCandidateAttr(lo, NominationMode::AGGRESSIVE, ICEMODE_FULL));
  SM_EXPECT_FALSE(f.controller->GetUseCandidateAttr(lo, NominationMode::AGGRESSIVE, ICEMODE_LITE));
  SM_EXPECT_FALSE(f.controller->GetUseCandidateAttr(hi, NominationMode::REGULAR, ICEMODE_FULL));
}

// The initial-select dampening field trials (off by default): the first selection waits up to the dampening delay,
// with a recheck at the shorter of the two delays.
SM_TEST(P2pBasicIceController, InitialSelectDampening) {
  ControllerFixture f;
  TestPort* port = f.PortOn(&f.wifi);
  Connection* c = f.Pair(port, "1.1.1.1:1000", 1000);
  ControllerFixture::MakeWritable(c, f.now);
  SM_EXPECT_TRUE(f.controller->ShouldSwitchConnection(f.now, IceSwitchReason::DATA_RECEIVED, c).connection);

  f.trials.initial_select_dampening = 300;
  auto r = f.controller->ShouldSwitchConnection(f.now, IceSwitchReason::DATA_RECEIVED, c);
  SM_EXPECT_FALSE(r.connection.has_value());
  SM_ASSERT_TRUE(r.recheck_event.has_value());
  SM_EXPECT_EQ(r.recheck_event->recheck_delay_ms, 300);
  SM_EXPECT_TRUE(r.recheck_event->reason == IceSwitchReason::ICE_CONTROLLER_RECHECK);
  f.now += TimeDelta::Millis(299);
  SM_EXPECT_FALSE(f.controller->ShouldSwitchConnection(f.now, IceSwitchReason::DATA_RECEIVED, c).connection);
  f.now += TimeDelta::Millis(1);
  SM_EXPECT_TRUE(f.controller->ShouldSwitchConnection(f.now, IceSwitchReason::DATA_RECEIVED, c).connection);
}
