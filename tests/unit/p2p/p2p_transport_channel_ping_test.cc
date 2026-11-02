// The ICE agent's single-channel cases (issue #1002 part 3): libwebrtc M157's P2PTransportChannelPingTest
// (compare/webrtc/reference/p2p/base/p2p_transport_channel_unittest.cc) and IceConfig(RTCConfiguration)
// (p2p/base/ice_transport_internal.cc). One channel gathers a host candidate on the emulated network (#985), and the
// test adds remote candidates at addresses nobody answers, then drives each connection's state by hand
// (ReceivedPingResponse, ReceivedPing, OnReadPacket, nomination), as libwebrtc's FakePortAllocator fixture does. The
// library's time model replaces the simulated clock: Wait* runs the channel and the network to the next event,
// AdvanceTime runs them for a fixed time. Each test names the libwebrtc case it follows.
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/basic_ice_controller.h"
#include "sm/p2p/basic_port_allocator.h"
#include "sm/p2p/byte_buffer.h"
#include "sm/p2p/candidate.h"
#include "sm/p2p/network_manager.h"
#include "sm/p2p/p2p_transport_channel.h"
#include "sm/p2p/port_allocator.h"
#include "sm/p2p/stun.h"
#include "sm/sim/emulated_sockets.h"
#include "sm_test.h"
#include "test_crypto.h"

using namespace sm;
using namespace sm::p2p;

namespace {

IpAddress Ip(std::string_view s) { return *IpAddress::FromString(s); }

const char* kIceUfrag[4] = {"UF00", "UF01", "UF02", "UF03"};
const char* kIcePwd[4] = {"TESTICEPWD00000000000000", "TESTICEPWD00000000000001", "TESTICEPWD00000000000002",
                          "TESTICEPWD00000000000003"};
const IceParameters kIceParams[4] = {{kIceUfrag[0], kIcePwd[0], false},
                                     {kIceUfrag[1], kIcePwd[1], false},
                                     {kIceUfrag[2], kIcePwd[2], false},
                                     {kIceUfrag[3], kIcePwd[3], false}};

constexpr TimeDelta kShortTimeout = TimeDelta::Seconds(1);
constexpr TimeDelta kMediumTimeout = TimeDelta::Seconds(3);
constexpr TimeDelta kDefaultTimeout = TimeDelta::Seconds(10);
constexpr TimeDelta kLowRtt = TimeDelta::Millis(20);

constexpr uint32_t kOnlyLocalPorts = PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY | PORTALLOCATOR_DISABLE_TCP;

Candidate CreateUdpCandidate(IceCandidateType type, std::string_view ip, int port, int priority,
                             std::string_view ufrag = "") {
  Candidate c;
  c.set_address(SocketAddress(Ip(ip), static_cast<uint16_t>(port)));
  c.set_component(ICE_CANDIDATE_COMPONENT_DEFAULT);
  c.set_protocol(UDP_PROTOCOL_NAME);
  c.set_priority(static_cast<uint32_t>(priority));
  c.set_username(ufrag);
  c.set_type(type);
  return c;
}

IceConfig CreateIceConfig(TimeDelta receiving_timeout, ContinualGatheringPolicy policy,
                          std::optional<TimeDelta> backup_ping_interval = std::nullopt) {
  IceConfig config;
  config.receiving_timeout = receiving_timeout;
  config.continual_gathering_policy = policy;
  config.backup_connection_ping_interval = backup_ping_interval;
  return config;
}

// A minimal binding request with the remote ufrag and a prflx priority, as the libwebrtc cases build it.
std::unique_ptr<IceMessage> PrflxRequest(std::string_view remote_ufrag) {
  auto request = std::make_unique<IceMessage>(STUN_BINDING_REQUEST);
  request->AddAttribute(std::make_unique<StunByteStringAttribute>(STUN_ATTR_USERNAME, remote_ufrag));
  request->AddAttribute(
      std::make_unique<StunUInt32Attribute>(STUN_ATTR_PRIORITY, uint32_t{ICE_TYPE_PREFERENCE_PRFLX} << 24));
  return request;
}

// P2PTransportChannelPingTest's fixture: one endpoint with a host candidate, and the channel under test.
struct PingFixture {
  sim::ByteNetwork net;
  std::unique_ptr<sim::EmulatedSocketServer> host;
  StaticNetworkManager network_manager;
  std::unique_ptr<BasicPortAllocator> allocator;
  std::unique_ptr<P2PTransportChannel> channel;
  Timestamp now = Timestamp::Seconds(10);

  int selected_candidate_pair_switches = 0;
  int64_t last_sent_packet_id = -1;
  bool channel_ready_to_send = false;
  std::optional<CandidatePairChangeEvent> last_candidate_change_event;
  IceTransportStateInternal channel_state = IceTransportStateInternal::kInit;
  std::optional<NetworkRoute> last_network_route;

  explicit PingFixture(const IceFieldTrials& trials = {}, IceControllerFactoryInterface* controller_factory = nullptr) {
    testing::InstallTestCrypto();
    sim::LinkConfig link;
    link.base_delay = TimeDelta::Millis(10);
    const IpAddress ip = Ip("192.168.1.1");
    host = std::make_unique<sim::EmulatedSocketServer>(
        net, net.AddHost(ip.v4(), sim::ByteNetwork::kPublicRealm, link, link));
    network_manager.AddInterface(ip);
    allocator = std::make_unique<BasicPortAllocator>(&network_manager, host.get());
    allocator->set_flags(kOnlyLocalPorts);
    allocator->set_step_delay(kMinimumStepDelay);
    P2PTransportChannel::Options options;
    options.field_trials = trials;
    options.ice_controller_factory = controller_factory;
    channel = std::make_unique<P2PTransportChannel>(now, "ping test", 1, allocator.get(), options);
  }
  ~PingFixture() { channel.reset(); }
  P2PTransportChannel& ch() { return *channel; }

  // PrepareChannel.
  void PrepareChannel() {
    ch().SetIceRole(ICEROLE_CONTROLLING);
    ch().SetIceParameters(kIceParams[0]);
    ch().SetRemoteIceParameters(kIceParams[1]);
    ch().SubscribeNetworkRouteChanged(this, [this](std::optional<NetworkRoute> route) {
      last_network_route = route;
      if (route) last_sent_packet_id = route->last_sent_packet_id;
      ++selected_candidate_pair_switches;
    });
    ch().SubscribeReadyToSend(this, [this](PacketTransportInternal*, Timestamp) { channel_ready_to_send = true; });
    ch().SubscribeIceTransportStateChanged(this,
                                           [this](IceTransportInternal* t) { channel_state = t->GetState(); });
    ch().SetCandidatePairChangeCallback(
        [this](const CandidatePairChangeEvent& event) { last_candidate_change_event = event; });
  }

  Timestamp NextEvent() const { return std::min(net.NextEvent(), channel->NextProcessTime()); }
  void Step(Timestamp t) {
    now = std::max(now, t);
    channel->Process(now);
    host->Deliver(now);
  }
  // The libwebrtc Waiter (test/wait_until.h, not in the reference tree) polls the condition every millisecond of
  // simulated time (WaitUntilSettings::polling_interval), so this runs at most 1 ms between checks.
  template <class F>
  bool WaitUntil(F stop, TimeDelta limit) {
    const Timestamp until = now + limit;
    for (int guard = 0; guard < 2000000; ++guard) {
      if (stop()) return true;
      const Timestamp t = std::min(NextEvent(), now + TimeDelta::Millis(1));
      if (t > until) {
        now = until;
        return stop();
      }
      Step(t);
    }
    return stop();
  }
  // time_controller_.AdvanceTime: runs every event up to now + d.
  void AdvanceTime(TimeDelta d) {
    const Timestamp until = now + d;
    while (NextEvent() <= until) Step(NextEvent());
    Step(until);
  }

  // ch.MaybeStartGathering() with libwebrtc's FakePortAllocator, whose session makes its one port ready inside
  // StartGettingPorts (p2p/test/fake_port_allocator.h), with no time passing: runs only the new allocator session at
  // `now`, so the host port is ready, remote candidates added next pair at once, and the controller's queued tasks
  // (a sort SetRemoteIceParameters requested, the first ping) are still to come, as there.
  void MaybeStartGathering() {
    ch().MaybeStartGathering(now);
    PortAllocatorSession* session = ch().allocator_session();
    for (int i = 0; i < 100 && session && session->NextProcessTime() <= now; ++i) session->Process(now);
  }

  Port* GetPort() { return ch().ports().empty() ? nullptr : ch().ports()[0]; }
  Port* GetPrunedPort() { return ch().pruned_ports().empty() ? nullptr : ch().pruned_ports()[0]; }
  Connection* GetConnectionTo(std::string_view ip, int port) {
    Port* p = GetPort();
    return p ? p->GetConnection(SocketAddress(Ip(ip), static_cast<uint16_t>(port))) : nullptr;
  }
  Connection* WaitForConnectionTo(std::string_view ip, int port) {
    WaitUntil([&] { return GetConnectionTo(ip, port) != nullptr; }, kMediumTimeout);
    return GetConnectionTo(ip, port);
  }
  void AddRemote(std::string_view ip, int port, int priority, std::string_view ufrag = "") {
    ch().AddRemoteCandidate(now, CreateUdpCandidate(IceCandidateType::kHost, ip, port, priority, ufrag));
  }
  Connection* CreateConnectionWithCandidate(std::string_view ip, int port, int priority, bool writable) {
    AddRemote(ip, port, priority);
    Connection* conn = WaitForConnectionTo(ip, port);
    if (conn && writable) conn->ReceivedPingResponse(now, kLowRtt, "id");  // make it writable
    return conn;
  }
  Connection* FindNextPingableConnectionAndPingIt() {
    Connection* conn = ch().FindNextPingableConnection(now);
    if (conn) ch().MarkConnectionPinged(conn);
    return conn;
  }
  int SendData(std::string_view data, int packet_id) {
    PacketOptions options;
    options.packet_id = packet_id;
    return ch().SendPacket(now, std::span(reinterpret_cast<const uint8_t*>(data.data()), data.size()), options, 0);
  }
  // conn->OnReadPacket(ReceivedIpPacket::CreateFromLegacy(data, ..., now)): data that does not parse as STUN.
  void ReceiveData(Connection* conn, std::string_view data) {
    ReceivedPacket packet;
    packet.payload = std::span(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    packet.arrival_time = now;
    conn->OnReadPacket(packet);
  }
  void NominateConnection(Connection* conn, uint32_t remote_nomination = 1) {
    conn->set_remote_nomination(remote_nomination);
    conn->NotifyNominatedForTesting(conn);
  }
  void ReceivePingOnConnection(Connection* conn, std::string_view remote_ufrag, int priority,
                               uint32_t nomination = 0) {
    IceMessage msg(STUN_BINDING_REQUEST);
    msg.AddAttribute(std::make_unique<StunByteStringAttribute>(
        STUN_ATTR_USERNAME, conn->local_candidate().username() + ":" + std::string(remote_ufrag)));
    msg.AddAttribute(std::make_unique<StunUInt32Attribute>(STUN_ATTR_PRIORITY, static_cast<uint32_t>(priority)));
    if (nomination != 0) msg.AddAttribute(std::make_unique<StunUInt32Attribute>(STUN_ATTR_NOMINATION, nomination));
    msg.AddMessageIntegrity(conn->local_candidate().password());
    msg.AddFingerprint();
    ByteBufferWriter buf;
    msg.Write(&buf);
    ReceivedPacket packet;
    packet.payload = buf.DataView();
    packet.arrival_time = now;
    conn->OnReadPacket(packet);
  }
  int reset_selected_candidate_pair_switches() {
    const int switches = selected_candidate_pair_switches;
    selected_candidate_pair_switches = 0;
    return switches;
  }
  bool CandidatePairMatchesNetworkRoute(const Connection* conn) const {
    if (!conn) return !last_network_route.has_value();
    return last_network_route && conn->local_candidate().network_id() == last_network_route->local.network_id &&
           conn->remote_candidate().network_id() == last_network_route->remote.network_id;
  }
  bool ConnectionMatchesChangeEvent(const Connection* conn, std::string_view reason) const {
    if (!conn) return !last_candidate_change_event.has_value();
    if (!last_candidate_change_event) return false;
    const CandidatePair& pair = last_candidate_change_event->selected_candidate_pair;
    return pair.local_candidate().IsEquivalent(conn->local_candidate()) &&
           pair.remote_candidate().IsEquivalent(conn->remote_candidate()) &&
           last_candidate_change_event->last_data_received_ms == conn->LastDataReceived().ms() &&
           last_candidate_change_event->reason == reason;
  }
  int64_t LastEstimatedDisconnectedTimeMs() const {
    return last_candidate_change_event ? last_candidate_change_event->estimated_disconnected_time_ms : 0;
  }
};

}  // namespace

// IceConfig(const RTCConfiguration&) with RTCConfiguration's defaults: everything unset, so the channel's
// *_or_default values are libwebrtc's constants, and presume_writable_when_fully_relayed off.
SM_TEST(P2pIceConfig, FromRtcConfigurationDefaults) {
  const IceConfig c = IceConfigFromRtcConfiguration(RtcIceConfiguration{});
  SM_EXPECT_FALSE(c.receiving_timeout.has_value());
  SM_EXPECT_FALSE(c.backup_connection_ping_interval.has_value());
  SM_EXPECT_FALSE(c.gather_continually());
  SM_EXPECT_FALSE(c.prioritize_most_likely_candidate_pairs);
  SM_EXPECT_FALSE(c.stable_writable_connection_ping_interval.has_value());
  SM_EXPECT_FALSE(c.presume_writable_when_fully_relayed);
  SM_EXPECT_FALSE(c.surface_ice_candidates_on_ice_transport_type_changed);
  SM_EXPECT_FALSE(c.regather_on_failed_networks_interval.has_value());
  SM_EXPECT_FALSE(c.receiving_switching_delay.has_value());
  SM_EXPECT_FALSE(c.ice_check_interval_strong_connectivity.has_value());
  SM_EXPECT_FALSE(c.ice_check_interval_weak_connectivity.has_value());
  SM_EXPECT_FALSE(c.ice_check_min_interval.has_value());
  SM_EXPECT_FALSE(c.ice_unwritable_timeout.has_value());
  SM_EXPECT_FALSE(c.ice_unwritable_min_checks.has_value());
  SM_EXPECT_FALSE(c.ice_inactive_timeout.has_value());
  SM_EXPECT_FALSE(c.stun_keepalive_interval.has_value());
  SM_EXPECT_FALSE(c.network_preference.has_value());
  SM_EXPECT_FALSE(c.dtls_handshake_in_stun);
  SM_EXPECT_TRUE(c.default_nomination_mode == NominationMode::SEMI_AGGRESSIVE);
  SM_EXPECT_FALSE(c.IsValid().has_value());
  SM_EXPECT_EQ(c.receiving_timeout_or_default().ms(), kReceivingTimeout.ms());
  SM_EXPECT_EQ(c.backup_connection_ping_interval_or_default().ms(), kBackupConnectionPingInterval.ms());
  SM_EXPECT_EQ(c.stable_writable_connection_ping_interval_or_default().ms(),
               kStrongAndStableWritableConnectionPingInterval.ms());
  SM_EXPECT_EQ(c.stun_keepalive_interval_or_default().ms(), kStunKeepaliveInterval.ms());
}

// Each RTCConfiguration field reaches its IceConfig field, in milliseconds.
SM_TEST(P2pIceConfig, FromRtcConfigurationFields) {
  RtcIceConfiguration rtc;
  rtc.ice_connection_receiving_timeout = 1500;
  rtc.ice_backup_candidate_pair_ping_interval = 20000;
  rtc.continual_gathering_policy = ContinualGatheringPolicy::kGatherContinually;
  rtc.prioritize_most_likely_ice_candidate_pairs = true;
  rtc.presume_writable_when_fully_relayed = true;
  rtc.surface_ice_candidates_on_ice_transport_type_changed = true;
  rtc.ice_check_interval_strong_connectivity = 1000;
  rtc.ice_check_interval_weak_connectivity = 100;
  rtc.ice_check_min_interval = 50;
  rtc.ice_unwritable_timeout = 4000;
  rtc.ice_unwritable_min_checks = 7;
  rtc.ice_inactive_timeout = 6000;
  rtc.stun_candidate_keepalive_interval = 15000;
  rtc.network_preference = ADAPTER_TYPE_WIFI;
  rtc.stable_writable_connection_ping_interval_ms = 3000;
  const IceConfig c = IceConfigFromRtcConfiguration(rtc);
  SM_EXPECT_EQ(c.receiving_timeout->ms(), int64_t{1500});
  SM_EXPECT_EQ(c.backup_connection_ping_interval->ms(), int64_t{20000});
  SM_EXPECT_TRUE(c.gather_continually());
  SM_EXPECT_TRUE(c.prioritize_most_likely_candidate_pairs);
  SM_EXPECT_TRUE(c.presume_writable_when_fully_relayed);
  SM_EXPECT_TRUE(c.surface_ice_candidates_on_ice_transport_type_changed);
  SM_EXPECT_EQ(c.ice_check_interval_strong_connectivity->ms(), int64_t{1000});
  SM_EXPECT_EQ(c.ice_check_interval_weak_connectivity->ms(), int64_t{100});
  SM_EXPECT_EQ(c.ice_check_min_interval->ms(), int64_t{50});
  SM_EXPECT_EQ(c.ice_unwritable_timeout->ms(), int64_t{4000});
  SM_EXPECT_EQ(*c.ice_unwritable_min_checks, 7);
  SM_EXPECT_EQ(c.ice_inactive_timeout->ms(), int64_t{6000});
  SM_EXPECT_EQ(c.stun_keepalive_interval->ms(), int64_t{15000});
  SM_EXPECT_TRUE(c.network_preference == ADAPTER_TYPE_WIFI);
  SM_EXPECT_EQ(c.stable_writable_connection_ping_interval->ms(), int64_t{3000});
  SM_EXPECT_FALSE(c.IsValid().has_value());
  // An inconsistent configuration is caught by IsValid, as PeerConnection::ValidateConfiguration does.
  rtc.stable_writable_connection_ping_interval_ms = 10;  // below the weak ping interval
  SM_EXPECT_TRUE(IceConfigFromRtcConfiguration(rtc).IsValid().has_value());
}

// PeerConnection hands the channel this config (InitializeTransportController_n): the channel's own defaults for
// the unset fields, presume-writable off.
SM_TEST(P2pIceConfig, ChannelTakesRtcConfiguration) {
  PingFixture f;
  SM_EXPECT_TRUE(f.ch().config().presume_writable_when_fully_relayed);  // the constructor's IceConfig
  f.ch().SetIceConfig(IceConfigFromRtcConfiguration(RtcIceConfiguration{}));
  SM_EXPECT_FALSE(f.ch().config().presume_writable_when_fully_relayed);
  SM_EXPECT_EQ(f.ch().config().receiving_timeout_or_default().ms(), kReceivingTimeout.ms());
  SM_EXPECT_EQ(f.ch().config().receiving_switching_delay_or_default().ms(), kReceivingSwitchingDelay.ms());
  SM_EXPECT_EQ(f.ch().check_receiving_interval().ms(), int64_t{250});
}

// TestTriggeredChecks.
SM_TEST(P2pTransportChannelPing, TestTriggeredChecks) {
  PingFixture f;
  f.PrepareChannel();
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 1);
  f.AddRemote("2.2.2.2", 2, 2);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  Connection* conn2 = f.WaitForConnectionTo("2.2.2.2", 2);
  SM_ASSERT_TRUE(conn1 != nullptr);
  SM_ASSERT_TRUE(conn2 != nullptr);
  // Before a triggered check, the first connection to ping is the highest priority one.
  SM_EXPECT_TRUE(conn2 == f.FindNextPingableConnectionAndPingIt());
  // Receiving a ping causes a triggered check which should make conn1 be pinged first instead of conn2.
  conn1->ReceivedPing(f.now);
  SM_EXPECT_TRUE(conn1 == f.FindNextPingableConnectionAndPingIt());
}

// TestAllConnectionsPingedSufficiently.
SM_TEST(P2pTransportChannelPing, TestAllConnectionsPingedSufficiently) {
  PingFixture f;
  f.PrepareChannel();
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 1);
  f.AddRemote("2.2.2.2", 2, 2);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  Connection* conn2 = f.WaitForConnectionTo("2.2.2.2", 2);
  SM_ASSERT_TRUE(conn1 != nullptr);
  SM_ASSERT_TRUE(conn2 != nullptr);
  // Low-priority connection becomes writable so that the other connection is not pruned.
  conn1->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_EXPECT_TRUE(f.WaitUntil(
      [&] {
        return conn1->num_pings_sent() >= kMinPingsAtWeakPingInterval &&
               conn2->num_pings_sent() >= kMinPingsAtWeakPingInterval;
      },
      kMediumTimeout));
}

// TestStunPingIntervals: weak (48 ms), stabilizing (900 ms), stable (2.5 s), and back to stabilizing when a ping
// goes unanswered for twice the RTT.
SM_TEST(P2pTransportChannelPing, TestStunPingIntervals) {
  const int kRttRatio = 4;
  constexpr TimeDelta kSchedulingRange = TimeDelta::Millis(200);
  constexpr TimeDelta kRttRange = TimeDelta::Millis(10);
  PingFixture f;
  f.PrepareChannel();
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 1);
  Connection* conn = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn != nullptr);
  SM_EXPECT_TRUE(f.WaitUntil([&] { return conn->num_pings_sent() == 1; }, kDefaultTimeout));

  // Initializing.
  Timestamp start = f.now;
  SM_EXPECT_TRUE(
      f.WaitUntil([&] { return conn->num_pings_sent() >= kMinPingsAtWeakPingInterval; }, kDefaultTimeout));
  TimeDelta ping_interval = (f.now - start) / (kMinPingsAtWeakPingInterval - 1);
  SM_EXPECT_EQ(ping_interval.ms(), kWeakPingInterval.ms());

  // Stabilizing.
  conn->ReceivedPingResponse(f.now, kLowRtt, "id");
  int ping_sent_before = conn->num_pings_sent();
  start = f.now;
  // The connection becomes strong but not stable because we haven't been able to converge the RTT.
  SM_EXPECT_TRUE(f.WaitUntil([&] { return conn->num_pings_sent() == ping_sent_before + 1; }, kDefaultTimeout));
  ping_interval = f.now - start;
  SM_EXPECT_GE(ping_interval.ms(), kWeakOrStabilizingWritableConnectionPingInterval.ms());
  SM_EXPECT_LE(ping_interval.ms(), (kWeakOrStabilizingWritableConnectionPingInterval + kSchedulingRange).ms());

  // Stabilized: the connection becomes stable after receiving more than kRttRatio RTT samples.
  for (int i = 0; i < kRttRatio; ++i) conn->ReceivedPingResponse(f.now, kLowRtt, "id");
  ping_sent_before = conn->num_pings_sent();
  start = f.now;
  SM_EXPECT_TRUE(f.WaitUntil([&] { return conn->num_pings_sent() == ping_sent_before + 1; }, kMediumTimeout));
  ping_interval = f.now - start;
  SM_EXPECT_GE(ping_interval.ms(), kStrongAndStableWritableConnectionPingInterval.ms());
  SM_EXPECT_LE(ping_interval.ms(), (kStrongAndStableWritableConnectionPingInterval + kSchedulingRange).ms());

  // Destabilized.
  conn->ReceivedPingResponse(f.now, kLowRtt, "id");
  // Create an in-flight ping.
  conn->Ping(f.now);
  start = f.now;
  // In-flight ping timeout and the connection will be unstable.
  SM_EXPECT_TRUE(f.WaitUntil([&] { return !conn->stable(f.now); }, kMediumTimeout));
  const TimeDelta duration = f.now - start;
  SM_EXPECT_GE(duration.ms(), (2 * conn->Rtt() - kRttRange).ms());
  SM_EXPECT_LE(duration.ms(), (2 * conn->Rtt() + kRttRange).ms());
  // The connection becomes unstable due to not receiving ping responses.
  ping_sent_before = conn->num_pings_sent();
  SM_EXPECT_TRUE(f.WaitUntil([&] { return conn->num_pings_sent() == ping_sent_before + 1; }, kMediumTimeout));
  // The interval is expected to be kWeakOrStabilizingWritableConnectionPingInterval.
  start = f.now;
  ping_sent_before = conn->num_pings_sent();
  SM_EXPECT_TRUE(f.WaitUntil([&] { return conn->num_pings_sent() == ping_sent_before + 1; }, kMediumTimeout));
  ping_interval = f.now - start;
  SM_EXPECT_GE(ping_interval.ms(), kWeakOrStabilizingWritableConnectionPingInterval.ms());
  SM_EXPECT_LE(ping_interval.ms(), (kWeakOrStabilizingWritableConnectionPingInterval + kSchedulingRange).ms());
}

// PingingStartedAsSoonAsPossible: a peer-reflexive pair made before the remote ICE parameters is pinged as soon as
// they arrive.
SM_TEST(P2pTransportChannelPing, PingingStartedAsSoonAsPossible) {
  PingFixture f;
  f.ch().SetIceRole(ICEROLE_CONTROLLING);
  f.ch().SetIceParameters(kIceParams[0]);
  f.MaybeStartGathering();
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().gathering_state() == kIceGatheringComplete; }, kMediumTimeout));
  // Simulate a binding request being received, creating a peer reflexive candidate pair while we still don't have
  // remote ICE parameters.
  auto request = PrflxRequest(kIceUfrag[1]);
  Port* port = f.GetPort();
  SM_ASSERT_TRUE(port != nullptr);
  port->NotifyUnknownAddress(f.now, SocketAddress(Ip("1.1.1.1"), 1), PROTO_UDP, request.get(), kIceUfrag[1], false);
  Connection* conn = f.GetConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn != nullptr);
  // A second (and change) later no pings were sent, since we don't yet have remote ICE parameters.
  f.AdvanceTime(TimeDelta::Millis(1025));
  SM_EXPECT_EQ(conn->num_pings_sent(), 0);
  // Set remote ICE parameters. Now we should be able to ping, at once.
  f.ch().SetRemoteIceParameters(kIceParams[1]);
  SM_EXPECT_TRUE(f.WaitUntil([&] { return conn->num_pings_sent() > 0; }, kDefaultTimeout));
}

// TestNoTriggeredChecksWhenWritable.
SM_TEST(P2pTransportChannelPing, TestNoTriggeredChecksWhenWritable) {
  PingFixture f;
  f.PrepareChannel();
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 1);
  f.AddRemote("2.2.2.2", 2, 2);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  Connection* conn2 = f.WaitForConnectionTo("2.2.2.2", 2);
  SM_ASSERT_TRUE(conn1 != nullptr);
  SM_ASSERT_TRUE(conn2 != nullptr);
  SM_EXPECT_TRUE(conn2 == f.FindNextPingableConnectionAndPingIt());
  SM_EXPECT_TRUE(conn1 == f.FindNextPingableConnectionAndPingIt());
  conn1->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_ASSERT_TRUE(conn1->writable());
  conn1->ReceivedPing(f.now);
  // Ping received, but the connection is already writable, so no triggered check: conn2 goes first by priority.
  SM_EXPECT_TRUE(conn2 == f.FindNextPingableConnectionAndPingIt());
}

// TestFailedConnectionNotPingable.
SM_TEST(P2pTransportChannelPing, TestFailedConnectionNotPingable) {
  PingFixture f;
  f.PrepareChannel();
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 1);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn1 != nullptr);
  SM_EXPECT_TRUE(conn1 == f.ch().FindNextPingableConnection(f.now));
  conn1->Prune();  // A pruned connection may still be pingable.
  SM_EXPECT_TRUE(conn1 == f.ch().FindNextPingableConnection(f.now));
  conn1->FailAndPrune();
  SM_EXPECT_TRUE(f.ch().FindNextPingableConnection(f.now) == nullptr);
}

// TestSignalStateChanged: pruning the only connection makes the channel FAILED.
SM_TEST(P2pTransportChannelPing, TestSignalStateChanged) {
  PingFixture f;
  f.PrepareChannel();
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 1);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn1 != nullptr);
  conn1->Prune();
  SM_EXPECT_TRUE(
      f.WaitUntil([&] { return f.channel_state == IceTransportStateInternal::kFailed; }, kDefaultTimeout));
}

// TestAddRemoteCandidateWithVariousUfrags: a future ufrag pairs without a password until the parameters arrive, an
// old one is dropped, the current one gets the password and generation.
SM_TEST(P2pTransportChannelPing, TestAddRemoteCandidateWithVariousUfrags) {
  PingFixture f;
  f.PrepareChannel();
  f.MaybeStartGathering();
  // Add a candidate with a future ufrag.
  f.AddRemote("1.1.1.1", 1, 1, kIceUfrag[2]);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn1 != nullptr);
  const Candidate& candidate = conn1->remote_candidate();
  SM_EXPECT_EQ(candidate.username(), std::string(kIceUfrag[2]));
  SM_EXPECT_TRUE(candidate.password().empty());
  SM_EXPECT_TRUE(f.FindNextPingableConnectionAndPingIt() == nullptr);
  // The remote ICE parameters with the "future" ufrag set the password in conn1's remote candidate.
  f.ch().SetRemoteIceParameters(kIceParams[2]);
  SM_EXPECT_EQ(candidate.username(), std::string(kIceUfrag[2]));
  SM_EXPECT_EQ(candidate.password(), std::string(kIcePwd[2]));
  SM_EXPECT_TRUE(conn1 == f.FindNextPingableConnectionAndPingIt());
  // A candidate with an old ufrag makes no connection.
  f.AddRemote("2.2.2.2", 2, 2, kIceUfrag[1]);
  f.AdvanceTime(TimeDelta::Millis(500));
  SM_EXPECT_TRUE(f.GetConnectionTo("2.2.2.2", 2) == nullptr);
  // A candidate with the current ufrag gets its password and generation, even if the generation is not set.
  f.AddRemote("3.3.3.3", 3, 0, kIceUfrag[2]);
  Connection* conn3 = nullptr;
  SM_EXPECT_TRUE(f.WaitUntil([&] { return (conn3 = f.GetConnectionTo("3.3.3.3", 3)) != nullptr; }, kDefaultTimeout));
  SM_ASSERT_TRUE(conn3 != nullptr);
  SM_EXPECT_EQ(conn3->remote_candidate().password(), std::string(kIcePwd[2]));
  SM_EXPECT_EQ(conn3->remote_candidate().generation(), uint32_t{1});
  // The password of every remote candidate is assigned.
  for (const RemoteCandidate& remote : f.ch().remote_candidates()) {
    SM_EXPECT_TRUE(remote.username() == kIceUfrag[1] || remote.username() == kIceUfrag[2]);
    if (remote.username() == kIceUfrag[1]) SM_EXPECT_EQ(remote.password(), std::string(kIcePwd[1]));
    if (remote.username() == kIceUfrag[2]) SM_EXPECT_EQ(remote.password(), std::string(kIcePwd[2]));
  }
}

// ConnectionResurrection: a pair destroyed and then pinged again from its address comes back with the signaled
// candidate's priority; a real peer-reflexive one gets the prflx priority.
SM_TEST(P2pTransportChannelPing, ConnectionResurrection) {
  PingFixture f;
  f.PrepareChannel();
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 1);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn1 != nullptr);
  const uint32_t remote_priority = conn1->remote_candidate().priority();
  // A higher priority candidate, receiving and writable, prunes conn1.
  f.AddRemote("2.2.2.2", 2, 2);
  Connection* conn2 = f.WaitForConnectionTo("2.2.2.2", 2);
  SM_ASSERT_TRUE(conn2 != nullptr);
  conn2->ReceivedPing(f.now);
  conn2->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn2; }, kMediumTimeout));
  // Destroy the connection to test the unknown-address signal.
  f.ch().RemoveConnectionForTest(f.now, conn1);
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.GetConnectionTo("1.1.1.1", 1) == nullptr; }, kMediumTimeout));
  auto request = PrflxRequest(kIceUfrag[1]);
  const uint32_t prflx_priority = uint32_t{ICE_TYPE_PREFERENCE_PRFLX} << 24;
  SM_EXPECT_NE(prflx_priority, remote_priority);
  Port* port = f.GetPort();
  SM_ASSERT_TRUE(port != nullptr);
  // conn1 is resurrected with its original priority.
  port->NotifyUnknownAddress(f.now, SocketAddress(Ip("1.1.1.1"), 1), PROTO_UDP, request.get(), kIceUfrag[1], false);
  conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn1 != nullptr);
  SM_EXPECT_EQ(conn1->remote_candidate().priority(), remote_priority);
  // conn3, a real prflx connection, has the prflx priority.
  port->NotifyUnknownAddress(f.now, SocketAddress(Ip("3.3.3.3"), 1), PROTO_UDP, request.get(), kIceUfrag[1], false);
  Connection* conn3 = f.WaitForConnectionTo("3.3.3.3", 1);
  SM_ASSERT_TRUE(conn3 != nullptr);
  SM_EXPECT_EQ(conn3->remote_candidate().priority(), prflx_priority);
}

// TestReceivingStateChange: the receiving timeout and check interval follow the config; data makes the channel
// receiving and it stops being receiving after the timeout.
SM_TEST(P2pTransportChannelPing, TestReceivingStateChange) {
  PingFixture f;
  f.PrepareChannel();
  SM_EXPECT_GE(f.ch().config().receiving_timeout_or_default().ms(), int64_t{1000});
  SM_EXPECT_GE(f.ch().check_receiving_interval().ms(), int64_t{200});
  f.ch().SetIceConfig(CreateIceConfig(TimeDelta::Millis(500), ContinualGatheringPolicy::kGatherOnce));
  SM_EXPECT_EQ(f.ch().config().receiving_timeout_or_default().ms(), int64_t{500});
  SM_EXPECT_EQ(f.ch().check_receiving_interval().ms(), int64_t{50});
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 1);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn1 != nullptr);
  f.AdvanceTime(TimeDelta::Seconds(1));
  conn1->ReceivedPing(f.now);
  f.ReceiveData(conn1, "ABC");
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().receiving(); }, kMediumTimeout));
  SM_EXPECT_TRUE(f.WaitUntil([&] { return !f.ch().receiving(); }, kShortTimeout));
}

// TestSelectConnectionBeforeNomination: the controlled side selects by priority until the controlling side
// nominates, then keeps the nominated pair unless a nominated, writable one comes along.
SM_TEST(P2pTransportChannelPing, TestSelectConnectionBeforeNomination) {
  PingFixture f;
  f.PrepareChannel();
  f.ch().SetIceRole(ICEROLE_CONTROLLED);
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 1);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn1 != nullptr);
  // Not ready to send because not writable.
  SM_EXPECT_FALSE(f.channel_ready_to_send);
  int last_packet_id = 0;
  const std::string data = "ABCDEFGH";
  const int len = static_cast<int>(data.size());
  SM_EXPECT_EQ(f.SendData(data, ++last_packet_id), -1);
  SM_EXPECT_EQ(f.last_sent_packet_id, int64_t{-1});

  // A connection needs to be writable before it is selected for transmission.
  conn1->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn1; }, kShortTimeout));
  SM_EXPECT_TRUE(f.CandidatePairMatchesNetworkRoute(conn1));
  SM_EXPECT_TRUE(f.ConnectionMatchesChangeEvent(conn1, "remote candidate generation maybe changed"));
  SM_EXPECT_EQ(f.SendData(data, ++last_packet_id), len);

  // A higher priority candidate becomes the selected connection.
  f.AddRemote("2.2.2.2", 2, 10);
  Connection* conn2 = f.WaitForConnectionTo("2.2.2.2", 2);
  SM_ASSERT_TRUE(conn2 != nullptr);
  conn2->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn2; }, kDefaultTimeout));
  SM_EXPECT_TRUE(f.CandidatePairMatchesNetworkRoute(conn2));
  SM_EXPECT_TRUE(f.ConnectionMatchesChangeEvent(conn2, "candidate pair state changed"));
  SM_EXPECT_TRUE(f.channel_ready_to_send);
  SM_EXPECT_EQ(f.last_sent_packet_id, int64_t{last_packet_id});

  // A nominated pair is selected even though its priority is lower.
  SM_EXPECT_EQ(f.SendData(data, ++last_packet_id), len);
  f.AddRemote("3.3.3.3", 3, 1);
  Connection* conn3 = f.WaitForConnectionTo("3.3.3.3", 3);
  SM_ASSERT_TRUE(conn3 != nullptr);
  SM_EXPECT_TRUE(f.ch().selected_connection() == conn2);
  conn3->ReceivedPingResponse(f.now, kLowRtt, "id");  // Become writable.
  f.NominateConnection(conn3);
  SM_ASSERT_TRUE(f.ch().selected_connection() == conn3);
  SM_EXPECT_TRUE(f.CandidatePairMatchesNetworkRoute(conn3));
  SM_EXPECT_TRUE(f.ConnectionMatchesChangeEvent(conn3, "nomination on the controlled side"));
  SM_EXPECT_EQ(f.last_sent_packet_id, int64_t{last_packet_id});
  SM_EXPECT_TRUE(f.channel_ready_to_send);

  // A higher priority candidate does not replace the nominated pair...
  SM_EXPECT_EQ(f.SendData(data, ++last_packet_id), len);
  f.AddRemote("4.4.4.4", 4, 100);
  Connection* conn4 = f.WaitForConnectionTo("4.4.4.4", 4);
  SM_ASSERT_TRUE(conn4 != nullptr);
  SM_EXPECT_TRUE(f.ch().selected_connection() == conn3);
  // ...until it is nominated and writable.
  f.NominateConnection(conn4);
  SM_EXPECT_TRUE(f.ch().selected_connection() == conn3);
  f.channel_ready_to_send = false;
  conn4->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn4; }, kDefaultTimeout));
  SM_EXPECT_TRUE(f.CandidatePairMatchesNetworkRoute(conn4));
  SM_EXPECT_TRUE(f.ConnectionMatchesChangeEvent(conn4, "candidate pair state changed"));
  SM_EXPECT_EQ(f.last_sent_packet_id, int64_t{last_packet_id});
  // Ready-to-send fires again because conn4 is writable.
  SM_EXPECT_TRUE(f.channel_ready_to_send);
}

// TestPingOnNomination: with send_ping_on_nomination_ice_controlled, the controlled side pings a pair as soon as it
// is nominated.
SM_TEST(P2pTransportChannelPing, TestPingOnNomination) {
  IceFieldTrials trials;
  trials.send_ping_on_nomination_ice_controlled = true;
  PingFixture f(trials);
  f.PrepareChannel();
  f.ch().SetIceConfig(f.ch().config());
  f.ch().SetIceRole(ICEROLE_CONTROLLED);
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 1);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn1 != nullptr);
  conn1->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn1; }, kDefaultTimeout));
  f.AddRemote("2.2.2.2", 2, 10);
  Connection* conn2 = f.WaitForConnectionTo("2.2.2.2", 2);
  SM_ASSERT_TRUE(conn2 != nullptr);
  conn2->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn2; }, kDefaultTimeout));
  // Nominate conn1 (low priority): it is chosen, and pinged at once.
  const int before = conn1->num_pings_sent();
  f.NominateConnection(conn1);
  SM_ASSERT_TRUE(f.ch().selected_connection() == conn1);
  SM_EXPECT_TRUE(f.CandidatePairMatchesNetworkRoute(conn1));
  SM_EXPECT_EQ(conn1->num_pings_sent(), before + 1);
}

// TestPingOnSwitch: with send_ping_on_switch_ice_controlling, the controlling side pings the pair it switches to.
SM_TEST(P2pTransportChannelPing, TestPingOnSwitch) {
  IceFieldTrials trials;
  trials.send_ping_on_switch_ice_controlling = true;
  PingFixture f(trials);
  f.PrepareChannel();
  f.ch().SetIceConfig(f.ch().config());
  f.ch().SetIceRole(ICEROLE_CONTROLLING);
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 1);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn1 != nullptr);
  conn1->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn1; }, kDefaultTimeout));
  f.AddRemote("2.2.2.2", 2, 10);
  Connection* conn2 = f.WaitForConnectionTo("2.2.2.2", 2);
  SM_ASSERT_TRUE(conn2 != nullptr);
  const int before = conn2->num_pings_sent();
  conn2->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn2; }, kDefaultTimeout));
  SM_EXPECT_TRUE(f.CandidatePairMatchesNetworkRoute(conn2));
  SM_EXPECT_EQ(conn2->num_pings_sent(), before + 1);
}

// TestPingOnSelected: with send_ping_on_selected_ice_controlling, also the first selection is pinged at once.
SM_TEST(P2pTransportChannelPing, TestPingOnSelected) {
  IceFieldTrials trials;
  trials.send_ping_on_selected_ice_controlling = true;
  PingFixture f(trials);
  f.PrepareChannel();
  f.ch().SetIceConfig(f.ch().config());
  f.ch().SetIceRole(ICEROLE_CONTROLLING);
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 1);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn1 != nullptr);
  const int before = conn1->num_pings_sent();
  conn1->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn1; }, kDefaultTimeout));
  SM_EXPECT_TRUE(f.CandidatePairMatchesNetworkRoute(conn1));
  SM_EXPECT_EQ(conn1->num_pings_sent(), before + 1);
}

// TestSelectConnectionFromUnknownAddress: requests from unknown addresses make peer-reflexive pairs that are
// answered, selected by priority until a nomination, and USE-CANDIDATE nominates one; an old-generation ufrag gets
// that generation's password.
SM_TEST(P2pTransportChannelPing, TestSelectConnectionFromUnknownAddress) {
  PingFixture f;
  f.PrepareChannel();
  f.ch().SetIceRole(ICEROLE_CONTROLLED);
  f.MaybeStartGathering();
  auto request = PrflxRequest(kIceUfrag[1]);
  Port* port = f.GetPort();
  SM_ASSERT_TRUE(port != nullptr);
  port->NotifyUnknownAddress(f.now, SocketAddress(Ip("1.1.1.1"), 1), PROTO_UDP, request.get(), kIceUfrag[1], false);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn1 != nullptr);
  SM_EXPECT_EQ(conn1->stats(f.now).sent_ping_responses, size_t{1});
  SM_EXPECT_TRUE(conn1 != f.ch().selected_connection());
  conn1->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn1; }, kDefaultTimeout));

  // Another connection is nominated via use_candidate.
  f.AddRemote("2.2.2.2", 2, 1);
  Connection* conn2 = f.WaitForConnectionTo("2.2.2.2", 2);
  SM_ASSERT_TRUE(conn2 != nullptr);
  // Because it has a lower priority, the selected connection is still conn1.
  SM_EXPECT_TRUE(f.ch().selected_connection() == conn1);
  // Nominated and writable, it is selected.
  conn2->ReceivedPingResponse(f.now, kLowRtt, "id");
  f.NominateConnection(conn2);
  SM_EXPECT_TRUE(f.ch().selected_connection() == conn2);

  // Another unknown address is not selected: the selected connection was nominated.
  port->NotifyUnknownAddress(f.now, SocketAddress(Ip("3.3.3.3"), 3), PROTO_UDP, request.get(), kIceUfrag[1], false);
  Connection* conn3 = f.WaitForConnectionTo("3.3.3.3", 3);
  SM_ASSERT_TRUE(conn3 != nullptr);
  SM_EXPECT_EQ(conn3->stats(f.now).sent_ping_responses, size_t{1});
  conn3->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_EXPECT_TRUE(f.ch().selected_connection() == conn2);

  // With USE-CANDIDATE it is selected once writable.
  request->AddAttribute(std::make_unique<StunByteStringAttribute>(STUN_ATTR_USE_CANDIDATE));
  port->NotifyUnknownAddress(f.now, SocketAddress(Ip("4.4.4.4"), 4), PROTO_UDP, request.get(), kIceUfrag[1], false);
  Connection* conn4 = f.WaitForConnectionTo("4.4.4.4", 4);
  SM_ASSERT_TRUE(conn4 != nullptr);
  SM_EXPECT_EQ(conn4->stats(f.now).sent_ping_responses, size_t{1});
  SM_EXPECT_TRUE(f.ch().selected_connection() == conn2);  // not writable yet
  conn4->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn4; }, kDefaultTimeout));

  // A request whose ufrag is of an old generation.
  f.ch().SetRemoteIceParameters(kIceParams[2]);
  f.ch().SetRemoteIceParameters(kIceParams[3]);
  port->NotifyUnknownAddress(f.now, SocketAddress(Ip("5.5.5.5"), 5), PROTO_UDP, request.get(), kIceUfrag[2], false);
  Connection* conn5 = f.WaitForConnectionTo("5.5.5.5", 5);
  SM_ASSERT_TRUE(conn5 != nullptr);
  SM_EXPECT_EQ(conn5->stats(f.now).sent_ping_responses, size_t{1});
  SM_EXPECT_EQ(conn5->remote_candidate().password(), std::string(kIcePwd[2]));
}

// TestSelectConnectionBasedOnMediaReceived: the controlled side follows the media path until a nomination.
SM_TEST(P2pTransportChannelPing, TestSelectConnectionBasedOnMediaReceived) {
  PingFixture f;
  f.PrepareChannel();
  f.ch().SetIceRole(ICEROLE_CONTROLLED);
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 10);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn1 != nullptr);
  conn1->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn1; }, kDefaultTimeout));
  // Data on conn2 switches the selection: the controlled side mirrors the controlling side's media path.
  f.AddRemote("2.2.2.2", 2, 1);
  Connection* conn2 = f.WaitForConnectionTo("2.2.2.2", 2);
  SM_ASSERT_TRUE(conn2 != nullptr);
  conn2->ReceivedPingResponse(f.now, kLowRtt, "id");
  f.ReceiveData(conn2, "ABC");
  SM_EXPECT_TRUE(f.ch().selected_connection() == conn2);
  conn2->ReceivedPingResponse(f.now, kLowRtt, "id");
  // A request with USE-CANDIDATE from an unknown address nominates conn3.
  auto request = PrflxRequest(kIceUfrag[1]);
  request->AddAttribute(std::make_unique<StunByteStringAttribute>(STUN_ATTR_USE_CANDIDATE));
  Port* port = f.GetPort();
  SM_ASSERT_TRUE(port != nullptr);
  port->NotifyUnknownAddress(f.now, SocketAddress(Ip("3.3.3.3"), 3), PROTO_UDP, request.get(), kIceUfrag[1], false);
  Connection* conn3 = f.WaitForConnectionTo("3.3.3.3", 3);
  SM_ASSERT_TRUE(conn3 != nullptr);
  SM_EXPECT_TRUE(f.ch().selected_connection() != conn3);  // Not writable yet.
  conn3->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn3; }, kDefaultTimeout));
  // Data on conn2 no longer switches: the selected connection was nominated.
  conn2->ReceivedPing(f.now);
  conn2->ReceivedPingResponse(f.now, kLowRtt, "id");
  f.ReceiveData(conn2, "XYZ");
  f.AdvanceTime(TimeDelta::Millis(100));
  SM_EXPECT_TRUE(f.ch().selected_connection() == conn3);
}

// TestControlledAgentDataReceivingTakesHigherPrecedenceThanPriority.
SM_TEST(P2pTransportChannelPing, TestControlledAgentDataReceivingTakesHigherPrecedenceThanPriority) {
  PingFixture f;
  f.AdvanceTime(TimeDelta::Seconds(1));
  f.PrepareChannel();
  f.ch().SetIceRole(ICEROLE_CONTROLLED);
  f.MaybeStartGathering();
  // The connections have decreasing priority.
  Connection* conn1 = f.CreateConnectionWithCandidate("1.1.1.1", 1, 10, true);
  SM_ASSERT_TRUE(conn1 != nullptr);
  Connection* conn2 = f.CreateConnectionWithCandidate("2.2.2.2", 2, 9, true);
  SM_ASSERT_TRUE(conn2 != nullptr);
  // Initially, connections are selected based on priority.
  SM_EXPECT_EQ(f.reset_selected_candidate_pair_switches(), 1);
  SM_EXPECT_TRUE(f.CandidatePairMatchesNetworkRoute(conn1));
  // conn2 receives data (1 ms later, so its last data is newer); it becomes selected.
  f.AdvanceTime(TimeDelta::Millis(1));
  f.ReceiveData(conn2, "XYZ");
  SM_EXPECT_EQ(f.reset_selected_candidate_pair_switches(), 1);
  SM_EXPECT_TRUE(f.CandidatePairMatchesNetworkRoute(conn2));
  // conn1 also receives data; it becomes selected due to priority again.
  f.ReceiveData(conn1, "ABC");
  SM_EXPECT_EQ(f.reset_selected_candidate_pair_switches(), 1);
  SM_EXPECT_TRUE(f.CandidatePairMatchesNetworkRoute(conn2));
  // conn2 received data more recently; it is selected now.
  f.AdvanceTime(TimeDelta::Millis(1));
  // It needs to become writable again because it was pruned.
  conn2->ReceivedPingResponse(f.now, kLowRtt, "id");
  f.ReceiveData(conn2, "ABC");
  SM_EXPECT_EQ(f.reset_selected_candidate_pair_switches(), 1);
  SM_EXPECT_TRUE(f.CandidatePairMatchesNetworkRoute(conn2));
  // Sorting does not reselect.
  f.AdvanceTime(TimeDelta::Millis(10));
  SM_EXPECT_EQ(f.reset_selected_candidate_pair_switches(), 0);
}

// TestControlledAgentNominationTakesHigherPrecedenceThanDataReceiving.
SM_TEST(P2pTransportChannelPing, TestControlledAgentNominationTakesHigherPrecedenceThanDataReceiving) {
  PingFixture f;
  f.AdvanceTime(TimeDelta::Seconds(1));
  f.PrepareChannel();
  f.ch().SetIceRole(ICEROLE_CONTROLLED);
  f.MaybeStartGathering();
  Connection* conn1 = f.CreateConnectionWithCandidate("1.1.1.1", 1, 10, true);
  SM_ASSERT_TRUE(conn1 != nullptr);
  Connection* conn2 = f.CreateConnectionWithCandidate("2.2.2.2", 2, 9, true);
  SM_ASSERT_TRUE(conn2 != nullptr);
  // conn1 received data; it is the selected connection.
  f.AdvanceTime(TimeDelta::Millis(1));
  f.ReceiveData(conn1, "XYZ");
  SM_EXPECT_EQ(f.reset_selected_candidate_pair_switches(), 1);
  SM_EXPECT_TRUE(f.CandidatePairMatchesNetworkRoute(conn1));
  // conn2 is nominated; it becomes the selected connection.
  f.NominateConnection(conn2);
  SM_EXPECT_EQ(f.reset_selected_candidate_pair_switches(), 1);
  SM_EXPECT_TRUE(f.CandidatePairMatchesNetworkRoute(conn2));
  // conn1 is selected because it has higher priority and also nominated.
  f.NominateConnection(conn1);
  SM_EXPECT_EQ(f.reset_selected_candidate_pair_switches(), 1);
  SM_EXPECT_TRUE(f.CandidatePairMatchesNetworkRoute(conn2));
  f.AdvanceTime(TimeDelta::Millis(10));
  SM_EXPECT_EQ(f.reset_selected_candidate_pair_switches(), 0);
}

// TestControlledAgentSelectsConnectionWithHigherNomination.
SM_TEST(P2pTransportChannelPing, TestControlledAgentSelectsConnectionWithHigherNomination) {
  PingFixture f;
  f.AdvanceTime(TimeDelta::Seconds(1));
  f.PrepareChannel();
  f.ch().SetIceRole(ICEROLE_CONTROLLED);
  f.MaybeStartGathering();
  Connection* conn1 = f.CreateConnectionWithCandidate("1.1.1.1", 1, 10, true);
  SM_ASSERT_TRUE(conn1 != nullptr);
  Connection* conn2 = f.CreateConnectionWithCandidate("2.2.2.2", 2, 9, true);
  SM_ASSERT_TRUE(conn2 != nullptr);
  // conn1 is the selected connection because it has a higher priority.
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn1; }, kDefaultTimeout));
  SM_EXPECT_TRUE(f.CandidatePairMatchesNetworkRoute(conn1));
  f.reset_selected_candidate_pair_switches();
  // conn2 is nominated; it becomes selected.
  f.NominateConnection(conn2);
  SM_EXPECT_EQ(f.reset_selected_candidate_pair_switches(), 1);
  SM_EXPECT_TRUE(f.ch().selected_connection() == conn2);
  SM_EXPECT_TRUE(f.CandidatePairMatchesNetworkRoute(conn2));
  // conn1 is selected because of its priority.
  f.NominateConnection(conn1);
  SM_EXPECT_EQ(f.reset_selected_candidate_pair_switches(), 1);
  SM_EXPECT_TRUE(f.ch().selected_connection() == conn1);
  SM_EXPECT_TRUE(f.CandidatePairMatchesNetworkRoute(conn1));
  // conn2 gets a higher remote nomination; it is selected again.
  f.NominateConnection(conn2, 2);
  SM_EXPECT_EQ(f.reset_selected_candidate_pair_switches(), 1);
  SM_EXPECT_TRUE(f.ch().selected_connection() == conn2);
  SM_EXPECT_TRUE(f.CandidatePairMatchesNetworkRoute(conn2));
  f.AdvanceTime(TimeDelta::Millis(100));
  SM_EXPECT_EQ(f.reset_selected_candidate_pair_switches(), 0);
}

// TestEstimatedDisconnectedTime: the switch event estimates the outage from the old pair's last data or ping.
SM_TEST(P2pTransportChannelPing, TestEstimatedDisconnectedTime) {
  PingFixture f;
  f.AdvanceTime(TimeDelta::Seconds(1));
  f.PrepareChannel();
  f.ch().SetIceRole(ICEROLE_CONTROLLED);
  f.MaybeStartGathering();
  Connection* conn1 = f.CreateConnectionWithCandidate("1.1.1.1", 1, 10, true);
  SM_ASSERT_TRUE(conn1 != nullptr);
  Connection* conn2 = f.CreateConnectionWithCandidate("2.2.2.2", 2, 9, true);
  SM_ASSERT_TRUE(conn2 != nullptr);
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn1; }, kDefaultTimeout));
  SM_EXPECT_TRUE(f.CandidatePairMatchesNetworkRoute(conn1));
  // No estimated disconnect time at first connect.
  SM_EXPECT_EQ(f.LastEstimatedDisconnectedTimeMs(), int64_t{0});
  uint32_t nomination = 1;
  {
    f.AdvanceTime(TimeDelta::Seconds(1));
    f.ReceiveData(conn1, "XYZ");  // not STUN: data
    f.AdvanceTime(TimeDelta::Seconds(2));
    // To be eligible for selection upon nomination, conn2 must be receiving.
    conn2->ReceivedPingResponse(f.now, kLowRtt, "id");
    f.NominateConnection(conn2, nomination++);
    SM_EXPECT_TRUE(f.ch().selected_connection() == conn2);
    // Data 2 s ago: guess 2 s of lost connectivity.
    SM_EXPECT_EQ(f.LastEstimatedDisconnectedTimeMs(), int64_t{2000});
  }
  {
    f.AdvanceTime(TimeDelta::Seconds(1));
    f.ReceiveData(conn2, "XYZ");
    f.AdvanceTime(TimeDelta::Seconds(2));
    f.ReceivePingOnConnection(conn2, kIceUfrag[1], 1, nomination++);
    // To be eligible for selection upon nomination, conn1 must be receiving.
    conn1->ReceivedPingResponse(f.now, kLowRtt, "id");
    f.AdvanceTime(TimeDelta::Millis(500));
    f.ReceivePingOnConnection(conn1, kIceUfrag[1], 1, nomination++);
    SM_EXPECT_TRUE(f.ch().selected_connection() == conn1);
    // A ping 500 ms ago: guess 500 ms of lost connectivity.
    SM_EXPECT_EQ(f.LastEstimatedDisconnectedTimeMs(), int64_t{500});
  }
}

// TestControlledAgentIgnoresSmallerNomination.
SM_TEST(P2pTransportChannelPing, TestControlledAgentIgnoresSmallerNomination) {
  PingFixture f;
  f.AdvanceTime(TimeDelta::Seconds(1));
  f.PrepareChannel();
  f.ch().SetIceRole(ICEROLE_CONTROLLED);
  f.MaybeStartGathering();
  Connection* conn = f.CreateConnectionWithCandidate("1.1.1.1", 1, 10, false);
  SM_ASSERT_TRUE(conn != nullptr);
  f.ReceivePingOnConnection(conn, kIceUfrag[1], 1, 2);
  SM_EXPECT_EQ(conn->remote_nomination(), uint32_t{2});
  // A smaller nomination is ignored.
  f.ReceivePingOnConnection(conn, kIceUfrag[1], 1, 1);
  SM_EXPECT_EQ(conn->remote_nomination(), uint32_t{2});
}

// TestControlledAgentWriteStateTakesHigherPrecedenceThanNomination.
SM_TEST(P2pTransportChannelPing, TestControlledAgentWriteStateTakesHigherPrecedenceThanNomination) {
  PingFixture f;
  f.PrepareChannel();
  f.ch().SetIceRole(ICEROLE_CONTROLLED);
  f.MaybeStartGathering();
  Connection* conn1 = f.CreateConnectionWithCandidate("1.1.1.1", 1, 10, false);
  SM_ASSERT_TRUE(conn1 != nullptr);
  Connection* conn2 = f.CreateConnectionWithCandidate("2.2.2.2", 2, 9, false);
  SM_ASSERT_TRUE(conn2 != nullptr);
  f.NominateConnection(conn1);
  // No selected connection: none is writable.
  SM_EXPECT_EQ(f.reset_selected_candidate_pair_switches(), 0);
  // conn2 becomes writable; it is selected even though it is not nominated.
  conn2->ReceivedPingResponse(f.now, kLowRtt, "id");
  int switches = 0;
  SM_EXPECT_TRUE(f.WaitUntil([&] { return (switches += f.reset_selected_candidate_pair_switches()) == 1; },
                             kDefaultTimeout));
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn2; }, kDefaultTimeout));
  SM_EXPECT_TRUE(f.CandidatePairMatchesNetworkRoute(conn2));
  // If conn1 is also writable, it becomes selected.
  conn1->ReceivedPingResponse(f.now, kLowRtt, "id");
  switches = 0;
  SM_EXPECT_TRUE(f.WaitUntil([&] { return (switches += f.reset_selected_candidate_pair_switches()) == 1; },
                             kDefaultTimeout));
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn1; }, kDefaultTimeout));
  SM_EXPECT_TRUE(f.CandidatePairMatchesNetworkRoute(conn1));
  f.AdvanceTime(TimeDelta::Millis(10));
  SM_EXPECT_EQ(f.reset_selected_candidate_pair_switches(), 0);
}

// TestAddRemoteCandidateWithAddressReuse: a candidate at the same address with a new ufrag makes a new connection.
SM_TEST(P2pTransportChannelPing, TestAddRemoteCandidateWithAddressReuse) {
  PingFixture f;
  f.PrepareChannel();
  f.MaybeStartGathering();
  // kIceUfrag[1] is the current generation ufrag.
  Candidate candidate = CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 1, kIceUfrag[1]);
  f.ch().AddRemoteCandidate(f.now, candidate);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn1 != nullptr);
  SM_EXPECT_EQ(conn1->remote_candidate().generation(), uint32_t{0});
  // Adding the same candidate again makes no new connection.
  f.ch().AddRemoteCandidate(f.now, candidate);
  SM_EXPECT_TRUE(f.GetConnectionTo("1.1.1.1", 1) == conn1);
  // With another ufrag it does.
  candidate.set_username(kIceUfrag[2]);
  f.ch().AddRemoteCandidate(f.now, candidate);
  Connection* conn2 = f.GetConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn2 != nullptr);
  SM_EXPECT_TRUE(conn1 != conn2);
  SM_EXPECT_EQ(conn2->remote_candidate().username(), std::string(kIceUfrag[2]));
  SM_EXPECT_EQ(conn2->remote_candidate().generation(), uint32_t{1});
  // A ping with the new ufrag is received on the new connection.
  SM_EXPECT_TRUE(conn2->LastPingReceived() == Timestamp::Millis(0));
  f.ReceivePingOnConnection(conn2, kIceUfrag[2], 1);
  SM_EXPECT_TRUE(conn2->LastPingReceived() > Timestamp::Millis(0));
}

// TestSameAddressCandidateDoesNotReplaceSelectedConnection.
SM_TEST(P2pTransportChannelPing, TestSameAddressCandidateDoesNotReplaceSelectedConnection) {
  PingFixture f;
  f.PrepareChannel();
  f.ch().SetIceRole(ICEROLE_CONTROLLED);
  f.MaybeStartGathering();
  Candidate candidate = CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 1, kIceUfrag[1]);
  f.ch().AddRemoteCandidate(f.now, candidate);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn1 != nullptr);
  SM_EXPECT_EQ(conn1->remote_candidate().generation(), uint32_t{0});
  conn1->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn1; }, kShortTimeout));
  // A newer generation's candidate at the same address.
  candidate.set_username(kIceUfrag[2]);
  f.ch().AddRemoteCandidate(f.now, candidate);
  // The selected connection survives: no replacement and no switch.
  SM_ASSERT_TRUE(f.GetConnectionTo("1.1.1.1", 1) == conn1);
  SM_EXPECT_TRUE(conn1->selected());
  SM_EXPECT_TRUE(conn1->writable());
  SM_EXPECT_TRUE(f.ch().selected_connection() == conn1);
}

// TestDontPruneWhenWeak.
SM_TEST(P2pTransportChannelPing, TestDontPruneWhenWeak) {
  PingFixture f;
  f.AdvanceTime(TimeDelta::Seconds(1));
  f.PrepareChannel();
  f.ch().SetIceRole(ICEROLE_CONTROLLED);
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 1);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn1 != nullptr);
  SM_EXPECT_TRUE(f.ch().selected_connection() == nullptr);
  conn1->ReceivedPingResponse(f.now, kLowRtt, "id");
  // A higher-priority, nominated candidate prunes the lower-priority ones.
  f.AddRemote("2.2.2.2", 2, 10);
  Connection* conn2 = f.WaitForConnectionTo("2.2.2.2", 2);
  SM_ASSERT_TRUE(conn2 != nullptr);
  conn2->ReceivedPingResponse(f.now, kLowRtt, "id");
  f.NominateConnection(conn2);
  SM_EXPECT_TRUE(f.WaitUntil([&] { return conn1->pruned(); }, kDefaultTimeout));
  f.ch().SetIceConfig(CreateIceConfig(TimeDelta::Millis(500), ContinualGatheringPolicy::kGatherOnce));
  // Wait until conn2 becomes not receiving.
  SM_EXPECT_TRUE(f.WaitUntil([&] { return !conn2->receiving(); }, kMediumTimeout));
  f.AddRemote("3.3.3.3", 3, 1);
  Connection* conn3 = f.WaitForConnectionTo("3.3.3.3", 3);
  SM_ASSERT_TRUE(conn3 != nullptr);
  // conn3 is not pruned: the selected connection is not receiving.
  SM_EXPECT_FALSE(f.WaitUntil([&] { return conn3->pruned(); }, kMediumTimeout));
  SM_EXPECT_FALSE(conn3->pruned());
}

// TestDontPruneHighPriorityConnections.
SM_TEST(P2pTransportChannelPing, TestDontPruneHighPriorityConnections) {
  PingFixture f;
  f.PrepareChannel();
  f.ch().SetIceRole(ICEROLE_CONTROLLED);
  f.MaybeStartGathering();
  Connection* conn1 = f.CreateConnectionWithCandidate("1.1.1.1", 1, 100, true);
  SM_ASSERT_TRUE(conn1 != nullptr);
  Connection* conn2 = f.CreateConnectionWithCandidate("2.2.2.2", 2, 200, false);
  SM_ASSERT_TRUE(conn2 != nullptr);
  // Even if conn1 is writable, nominated and receiving data, conn2 is not pruned.
  f.NominateConnection(conn1);
  f.AdvanceTime(TimeDelta::Millis(1));
  f.ReceiveData(conn1, "XYZ");
  f.WaitUntil([&] { return conn2->pruned(); }, kShortTimeout);
  SM_EXPECT_FALSE(conn2->pruned());
}

// TestGetState: new, then checking with connections, connected/COMPLETED once one is writable and the other pruned,
// failed when all are pruned.
SM_TEST(P2pTransportChannelPing, TestGetState) {
  PingFixture f;
  f.AdvanceTime(TimeDelta::Seconds(1));
  SM_EXPECT_TRUE(f.ch().GetIceTransportState() == IceTransportState::kNew);
  f.PrepareChannel();
  f.MaybeStartGathering();
  // After gathering we are still new: no connection is checked yet.
  SM_EXPECT_TRUE(f.ch().GetIceTransportState() == IceTransportState::kNew);
  SM_EXPECT_TRUE(f.ch().GetState() == IceTransportStateInternal::kInit);
  f.AddRemote("1.1.1.1", 1, 100);
  f.AddRemote("2.2.2.2", 2, 1);
  SM_ASSERT_GT(f.ch().connections().size(), size_t{0});
  SM_EXPECT_TRUE(f.ch().GetIceTransportState() == IceTransportState::kChecking);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  Connection* conn2 = f.WaitForConnectionTo("2.2.2.2", 2);
  SM_ASSERT_TRUE(conn1 != nullptr);
  SM_ASSERT_TRUE(conn2 != nullptr);
  SM_EXPECT_TRUE(f.ch().GetState() == IceTransportStateInternal::kConnecting);
  SM_EXPECT_TRUE(f.ch().GetIceTransportState() == IceTransportState::kChecking);
  // conn1 becomes writable and receiving; it prunes conn2.
  conn1->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_EXPECT_TRUE(f.WaitUntil([&] { return conn2->pruned(); }, kDefaultTimeout));
  SM_EXPECT_TRUE(f.ch().GetState() == IceTransportStateInternal::kCompleted);
  SM_EXPECT_TRUE(f.ch().GetIceTransportState() == IceTransportState::kConnected);
  conn1->Prune();  // All connections are pruned.
  SM_EXPECT_TRUE(
      f.WaitUntil([&] { return f.ch().GetState() == IceTransportStateInternal::kFailed; }, kShortTimeout));
  SM_EXPECT_TRUE(f.ch().GetIceTransportState() == IceTransportState::kFailed);
}

// TestConnectionPrunedAgain: a pruned low-priority pair is kept, takes over while the selected one is not
// receiving, and is pruned again when it comes back.
SM_TEST(P2pTransportChannelPing, TestConnectionPrunedAgain) {
  PingFixture f;
  f.AdvanceTime(TimeDelta::Seconds(1));
  f.PrepareChannel();
  IceConfig config = CreateIceConfig(TimeDelta::Seconds(1), ContinualGatheringPolicy::kGatherOnce);
  config.receiving_switching_delay = TimeDelta::Millis(800);
  f.ch().SetIceConfig(config);
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 100);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn1 != nullptr);
  SM_EXPECT_TRUE(f.ch().selected_connection() == nullptr);
  conn1->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn1; }, kShortTimeout));
  // A low-priority conn2 is pruned but not deleted.
  f.AddRemote("2.2.2.2", 2, 1);
  Connection* conn2 = f.WaitForConnectionTo("2.2.2.2", 2);
  SM_ASSERT_TRUE(conn2 != nullptr);
  SM_EXPECT_TRUE(f.WaitUntil([&] { return !conn2->active(); }, kDefaultTimeout));
  // conn2 does not send a ping yet.
  SM_EXPECT_TRUE(conn2->state() == IceCandidatePairState::WAITING);
  SM_EXPECT_TRUE(f.ch().GetState() == IceTransportStateInternal::kCompleted);
  // Once conn1 is not receiving, conn2 pings and, answered, becomes selected.
  SM_EXPECT_TRUE(f.WaitUntil([&] { return !conn1->receiving(); }, kDefaultTimeout));
  conn2 = f.WaitForConnectionTo("2.2.2.2", 2);
  SM_ASSERT_TRUE(conn2 != nullptr);
  SM_EXPECT_TRUE(f.WaitUntil([&] { return conn2->state() == IceCandidatePairState::IN_PROGRESS; }, kMediumTimeout));
  conn2->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn2; }, kDefaultTimeout));
  SM_EXPECT_TRUE(f.ch().GetState() == IceTransportStateInternal::kConnecting);
  // When conn1 comes back, conn2 is pruned again.
  conn1->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn1; }, kDefaultTimeout));
  SM_EXPECT_TRUE(f.WaitUntil([&] { return !conn2->active(); }, kDefaultTimeout));
  SM_EXPECT_TRUE(f.ch().GetState() == IceTransportStateInternal::kCompleted);
}

// TestStopPortAllocatorSessions with the ping fixture: a writable pair of the current session stops gathering, one
// of an older session does not.
SM_TEST(P2pTransportChannelPing, TestStopPortAllocatorSessions) {
  PingFixture f;
  f.PrepareChannel();
  f.ch().SetIceConfig(CreateIceConfig(TimeDelta::Seconds(2), ContinualGatheringPolicy::kGatherOnce));
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 100);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn1 != nullptr);
  conn1->ReceivedPingResponse(f.now, kLowRtt, "id");
  f.Step(f.now);
  SM_EXPECT_FALSE(f.ch().allocator_session()->IsGettingPorts());
  // A new session: conn1, of the older session, going unwritable and writable again does not stop it.
  f.ch().SetIceParameters(kIceParams[1]);
  f.MaybeStartGathering();
  conn1->Prune();
  conn1->ReceivedPingResponse(f.now, kLowRtt, "id");
  f.Step(f.now);
  SM_EXPECT_TRUE(f.ch().allocator_session()->IsGettingPorts());
  // A writable pair of the new session stops it.
  f.AddRemote("2.2.2.2", 2, 100);
  Connection* conn2 = f.WaitForConnectionTo("2.2.2.2", 2);
  SM_ASSERT_TRUE(conn2 != nullptr);
  conn2->ReceivedPingResponse(f.now, kLowRtt, "id");
  f.Step(f.now);
  SM_EXPECT_FALSE(f.ch().allocator_session()->IsGettingPorts());
}

// TestIceRoleUpdatedOnRemovedPort: a pruned port still gets the new role.
SM_TEST(P2pTransportChannelPing, TestIceRoleUpdatedOnRemovedPort) {
  PingFixture f;
  f.PrepareChannel();  // controlling
  f.ch().SetIceConfig(CreateIceConfig(TimeDelta::Seconds(1), ContinualGatheringPolicy::kGatherContinually));
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 1);
  Connection* conn = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn != nullptr);
  f.ch().allocator_session()->NotifyPortsPruned(std::vector<Port*>{conn->PortForTest()});
  f.ch().SetIceRole(ICEROLE_CONTROLLED);
  SM_EXPECT_TRUE(conn->PortForTest()->GetIceRole() == ICEROLE_CONTROLLED);
}

// TestIceRoleUpdatedOnPortAfterIceRestart: an older generation's port gets the new role.
SM_TEST(P2pTransportChannelPing, TestIceRoleUpdatedOnPortAfterIceRestart) {
  PingFixture f;
  f.PrepareChannel();
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 1);
  Connection* conn = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn != nullptr);
  f.ch().SetIceParameters(kIceParams[1]);
  f.MaybeStartGathering();
  f.ch().SetIceRole(ICEROLE_CONTROLLED);
  SM_EXPECT_TRUE(conn->PortForTest()->GetIceRole() == ICEROLE_CONTROLLED);
}

// TestPortDestroyedAfterTimeoutAndPruned: the connection dies after two minutes without data; the port goes only
// once the session prunes it.
SM_TEST(P2pTransportChannelPing, TestPortDestroyedAfterTimeoutAndPruned) {
  PingFixture f;
  f.PrepareChannel();
  f.ch().SetIceRole(ICEROLE_CONTROLLED);
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 1);
  Connection* conn = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn != nullptr);
  for (int second = 0; second < 120; ++second) f.AdvanceTime(TimeDelta::Seconds(1));
  SM_EXPECT_TRUE(f.GetConnectionTo("1.1.1.1", 1) == nullptr);
  // The port stays: it is not pruned yet.
  SM_ASSERT_TRUE(f.GetPort() != nullptr);
  // If the session prunes all ports, the port is destroyed.
  f.ch().allocator_session()->PruneAllPorts(f.now);
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.GetPort() == nullptr; }, kDefaultTimeout));
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.GetPrunedPort() == nullptr; }, kDefaultTimeout));
}

// TestMaxOutstandingPingsFieldTrial: with max_outstanding_pings 3, a pair stops after 3 unanswered pings.
SM_TEST(P2pTransportChannelPing, TestMaxOutstandingPingsFieldTrial) {
  IceFieldTrials trials;
  trials.max_outstanding_pings = 3;
  PingFixture f(trials);
  f.ch().SetIceConfig(f.ch().config());
  f.PrepareChannel();
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 1);
  f.AddRemote("2.2.2.2", 2, 2);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  Connection* conn2 = f.WaitForConnectionTo("2.2.2.2", 2);
  SM_ASSERT_TRUE(conn1 != nullptr);
  SM_ASSERT_TRUE(conn2 != nullptr);
  SM_EXPECT_TRUE(f.WaitUntil([&] { return conn1->num_pings_sent() == 3 && conn2->num_pings_sent() == 3; },
                             kDefaultTimeout));
  // They send no more pings.
  SM_EXPECT_TRUE(f.ch().FindNextPingableConnection(f.now) == nullptr);
}

// The agent's last ping time starts at Timestamp::Zero() (p2p_transport_channel.h, last_ping_sent_), so the first
// GetConnectionToPing finds a connection whenever the clock is past the weak ping interval.
SM_TEST(P2pTransportChannelPing, InitialLastPingSent) {
  PingFixture f;
  SM_EXPECT_TRUE(f.ch().GetLastPingSent() == Timestamp::Millis(0));
}

// TestInitialSelectDampening0: initial_select_dampening:0 selects at once.
SM_TEST(P2pTransportChannelPing, TestInitialSelectDampening0) {
  IceFieldTrials trials;
  trials.initial_select_dampening = 0;
  PingFixture f(trials);
  f.AdvanceTime(TimeDelta::Seconds(1));
  f.PrepareChannel();
  f.ch().SetIceConfig(f.ch().config());
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 100);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn1 != nullptr);
  SM_EXPECT_TRUE(f.ch().selected_connection() == nullptr);
  conn1->ReceivedPingResponse(f.now, kLowRtt, "id");  // Becomes writable and receiving
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn1; }, kDefaultTimeout));
}

// TestInitialSelectDampening: initial_select_dampening:100 selects once 100 ms have passed.
SM_TEST(P2pTransportChannelPing, TestInitialSelectDampening) {
  IceFieldTrials trials;
  trials.initial_select_dampening = 100;
  PingFixture f(trials);
  f.AdvanceTime(TimeDelta::Seconds(1));
  f.PrepareChannel();
  f.ch().SetIceConfig(f.ch().config());
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 100);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn1 != nullptr);
  SM_EXPECT_TRUE(f.ch().selected_connection() == nullptr);
  conn1->ReceivedPingResponse(f.now, kLowRtt, "id");
  f.WaitUntil([&] { return f.ch().selected_connection() == conn1; }, kDefaultTimeout);
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn1; }, kDefaultTimeout));
}

// TestInitialSelectDampeningPingReceived: initial_select_dampening_ping_received:100 with a ping received.
SM_TEST(P2pTransportChannelPing, TestInitialSelectDampeningPingReceived) {
  IceFieldTrials trials;
  trials.initial_select_dampening_ping_received = 100;
  PingFixture f(trials);
  f.AdvanceTime(TimeDelta::Seconds(1));
  f.PrepareChannel();
  f.ch().SetIceConfig(f.ch().config());
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 100);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn1 != nullptr);
  SM_EXPECT_TRUE(f.ch().selected_connection() == nullptr);
  conn1->ReceivedPingResponse(f.now, kLowRtt, "id");
  conn1->ReceivedPing(f.now, std::string("id1"));
  f.WaitUntil([&] { return f.ch().selected_connection() == conn1; }, kDefaultTimeout);
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn1; }, kDefaultTimeout));
}

// TestInitialSelectDampeningBoth: 100 ms, then 50 ms once a ping is received.
SM_TEST(P2pTransportChannelPing, TestInitialSelectDampeningBoth) {
  IceFieldTrials trials;
  trials.initial_select_dampening = 100;
  trials.initial_select_dampening_ping_received = 50;
  PingFixture f(trials);
  f.AdvanceTime(TimeDelta::Seconds(1));
  f.PrepareChannel();
  f.ch().SetIceConfig(f.ch().config());
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 100);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  SM_ASSERT_TRUE(conn1 != nullptr);
  SM_EXPECT_TRUE(f.ch().selected_connection() == nullptr);
  conn1->ReceivedPingResponse(f.now, kLowRtt, "id");
  f.WaitUntil([&] { return f.ch().selected_connection() == conn1; }, kDefaultTimeout);
  // Now receiving a ping, and the new timeout kicks in.
  conn1->ReceivedPing(f.now, std::string("id1"));
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn1; }, kDefaultTimeout));
}

namespace {

// ForgetLearnedStateController: a BasicIceController that asks the agent to forget one connection's learned state
// on its next sort, and rechecks every 100 ms.
class ForgetLearnedStateController : public BasicIceController {
 public:
  explicit ForgetLearnedStateController(const IceControllerFactoryArgs& args) : BasicIceController(args) {}
  SwitchResult SortAndSwitchConnection(Timestamp now, IceSwitchReason reason) override {
    SwitchResult result = BasicIceController::SortAndSwitchConnection(now, reason);
    if (forget_connection_) {
      result.connections_to_forget_state_on.push_back(forget_connection_);
      forget_connection_ = nullptr;
    }
    result.recheck_event.emplace(IceSwitchReason::ICE_CONTROLLER_RECHECK, 100);
    return result;
  }
  void ForgetThisConnectionNextTimeSortAndSwitchConnectionIsCalled(Connection* conn) { forget_connection_ = conn; }

 private:
  Connection* forget_connection_ = nullptr;
};

class ForgetLearnedStateControllerFactory : public IceControllerFactoryInterface {
 public:
  std::unique_ptr<IceControllerInterface> Create(const IceControllerFactoryArgs& args) override {
    auto controller = std::make_unique<ForgetLearnedStateController>(args);
    controller_ = controller.get();
    return controller;
  }
  ForgetLearnedStateController* controller_ = nullptr;
};

}  // namespace

// TestForgetLearnedState: a connection the controller asks to forget loses its learned state (it is no longer
// writable).
SM_TEST(P2pTransportChannelPing, TestForgetLearnedState) {
  ForgetLearnedStateControllerFactory factory;
  PingFixture f(IceFieldTrials{}, &factory);
  f.PrepareChannel();
  f.MaybeStartGathering();
  f.AddRemote("1.1.1.1", 1, 1);
  f.AddRemote("2.2.2.2", 2, 2);
  Connection* conn1 = f.WaitForConnectionTo("1.1.1.1", 1);
  Connection* conn2 = f.WaitForConnectionTo("2.2.2.2", 2);
  SM_ASSERT_TRUE(conn1 != nullptr);
  SM_ASSERT_TRUE(conn2 != nullptr);
  // Wait for conn1 to be selected.
  conn1->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_EXPECT_TRUE(f.WaitUntil([&] { return f.ch().selected_connection() == conn1; }, kDefaultTimeout));
  conn2->ReceivedPingResponse(f.now, kLowRtt, "id");
  SM_EXPECT_TRUE(conn2->writable());
  // The controller tells the channel to forget conn2: it is no longer writable.
  SM_ASSERT_TRUE(factory.controller_ != nullptr);
  factory.controller_->ForgetThisConnectionNextTimeSortAndSwitchConnectionIsCalled(conn2);
  SM_EXPECT_TRUE(f.WaitUntil([&] { return !conn2->writable(); }, kMediumTimeout));
}
