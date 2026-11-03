// Data channels (C7 part 4, #1070): SctpDataChannel against a fake controller (the behaviour of libwebrtc M157's
// pc/sctp_data_channel.cc; its data_channel_unittest.cc is not in compare/webrtc/reference/), and two endpoints
// opening channels both ways over ICE + DTLS + SCTP on the emulated network, through DataChannelSession.
#include <algorithm>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/basic_port_allocator.h"
#include "sm/p2p/candidate.h"
#include "sm/p2p/dtls_transport.h"
#include "sm/p2p/network_manager.h"
#include "sm/p2p/p2p_transport_channel.h"
#include "sm/p2p/port_allocator.h"
#include "sm/p2p/stun_crypto_tls.h"
#include "sm/pc/data_channel_controller.h"
#include "sm/sim/emulated_sockets.h"
#include "sm/tls/message_digest.h"
#include "sm/tls/ssl_fingerprint.h"
#include "sm/tls/ssl_identity.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::p2p;
using namespace sm::pc;
using sm::sctp::DataMessageType;
using sm::sctp::SendDataParams;

namespace {

std::vector<uint8_t> Bytes(std::string_view s) { return {s.begin(), s.end()}; }

struct Observer final : DataChannelObserver {
  SctpDataChannel* channel = nullptr;
  std::vector<DataState> states;
  std::vector<std::string> messages;
  std::vector<bool> binary;
  uint64_t buffered_change = 0;
  std::optional<int> max_message_size;
  void OnStateChange() override { states.push_back(channel->state()); }
  void OnMessage(const DataBuffer& b) override {
    messages.emplace_back(b.data.begin(), b.data.end());
    binary.push_back(b.binary);
  }
  void OnBufferedAmountChange(uint64_t sent) override { buffered_change += sent; }
  void OnMaxMessageSize(int size) override { max_message_size = size; }
  void Attach(SctpDataChannel& c) {
    channel = &c;
    c.RegisterObserver(this);
  }
};

// ---- SctpDataChannel against a fake controller ----

struct Sent {
  int sid;
  SendDataParams params;
  std::vector<uint8_t> data;
};

struct FakeController final : SctpDataChannelControllerInterface {
  FakeController() : self(std::make_shared<SctpDataChannelControllerInterface*>(this)) {}
  ControllerHandle self;
  std::vector<Sent> sent;
  std::vector<int> removed;
  size_t buffered = 0;
  bool fail_send = false;
  RtcError SendData(Timestamp, StreamId sid, const SendDataParams& params,
                    std::span<const uint8_t> payload) override {
    if (fail_send) return RtcError(sctp::RtcErrorType::kResourceExhausted);
    sent.push_back({sid.stream_id_int(), params, {payload.begin(), payload.end()}});
    return RtcError::OK();
  }
  RtcError AddSctpDataStream(StreamId, PriorityValue) override { return RtcError::OK(); }
  void RemoveSctpDataStream(Timestamp, StreamId sid) override { removed.push_back(sid.stream_id_int()); }
  void OnChannelStateChanged(SctpDataChannel*, DataState) override {}
  size_t buffered_amount(StreamId) const override { return buffered; }
  size_t buffered_amount_low_threshold(StreamId) const override { return 0; }
  void SetBufferedAmountLowThreshold(StreamId, size_t) override {}
};

const Timestamp kT0 = Timestamp::Seconds(1);

std::shared_ptr<SctpDataChannel> MakeChannel(FakeController& c, DataChannelInit init, int sid,
                                             InternalDataChannelInit::OpenHandshakeRole role) {
  InternalDataChannelInit config(init);
  config.open_handshake_role = role;
  config.id = sid;
  return SctpDataChannel::Create(c.self, "label", true, config, std::nullopt);
}

}  // namespace

SM_TEST(PcSctpDataChannel, InternalInitClampsAndValidates) {
  DataChannelInit base;
  base.maxRetransmits = -1;
  base.maxRetransmitTime = 70000;
  base.id = 7;
  InternalDataChannelInit in(base);
  SM_EXPECT_FALSE(in.maxRetransmits.has_value());
  SM_EXPECT_EQ(*in.maxRetransmitTime, 65535);
  SM_EXPECT_EQ(in.id, -1);  // Not negotiated: the id is allocated later.
  SM_EXPECT_EQ(in.open_handshake_role, InternalDataChannelInit::kOpener);
  SM_EXPECT_TRUE(in.IsValid());
  base.negotiated = true;
  base.maxRetransmits = 3;
  base.maxRetransmitTime = 3;
  InternalDataChannelInit both(base);
  SM_EXPECT_EQ(both.id, 7);
  SM_EXPECT_EQ(both.open_handshake_role, InternalDataChannelInit::kNone);
  SM_EXPECT_FALSE(both.IsValid());  // maxRetransmits and maxPacketLifeTime together.
}

SM_TEST(PcSctpDataChannel, SidAllocatorEvenOddByDtlsRole) {
  SctpSidAllocator a;
  SM_EXPECT_EQ(a.AllocateSid(SslRole::kClient)->stream_id_int(), 0);
  SM_EXPECT_EQ(a.AllocateSid(SslRole::kClient)->stream_id_int(), 2);
  SM_EXPECT_EQ(a.AllocateSid(SslRole::kServer)->stream_id_int(), 1);
  SM_EXPECT_TRUE(a.ReserveSid(StreamId(3)));
  SM_EXPECT_FALSE(a.ReserveSid(StreamId(3)));
  SM_EXPECT_EQ(a.AllocateSid(SslRole::kServer)->stream_id_int(), 5);
  a.ReleaseSid(StreamId(0));
  SM_EXPECT_EQ(a.AllocateSid(SslRole::kClient)->stream_id_int(), 0);
  a.SetMaxSid(4);
  SM_EXPECT_EQ(a.AllocateSid(SslRole::kClient)->stream_id_int(), 4);
  SM_EXPECT_FALSE(a.AllocateSid(SslRole::kClient).has_value());
  a.SetMaxSid(100);  // Can only go down.
  SM_EXPECT_FALSE(a.AllocateSid(SslRole::kClient).has_value());
}

// The opener sends OPEN (ordered, control), opens at once, sends unordered data ordered until the ACK.
SM_TEST(PcSctpDataChannel, OpenerHandshake) {
  FakeController c;
  DataChannelInit init;
  init.ordered = false;
  init.protocol = "p";
  auto ch = MakeChannel(c, init, 2, InternalDataChannelInit::kOpener);
  Observer obs;
  obs.Attach(*ch);
  SM_EXPECT_TRUE(ch->state() == DataState::kConnecting);
  ch->OnTransportReady(kT0);
  SM_EXPECT_TRUE(ch->state() == DataState::kOpen);
  SM_ASSERT_EQ(c.sent.size(), size_t{1});
  SM_EXPECT_TRUE(c.sent[0].params.type == DataMessageType::kControl);
  SM_EXPECT_TRUE(c.sent[0].params.ordered);
  SM_EXPECT_TRUE(IsOpenMessage(c.sent[0].data));
  SM_EXPECT_TRUE(ch->Send(kT0, DataBuffer("a")));
  SM_EXPECT_TRUE(c.sent[1].params.ordered);  // Before the ACK.
  ch->OnDataReceived(DataMessageType::kControl, std::vector<uint8_t>{0x02});
  SM_EXPECT_TRUE(ch->Send(kT0, DataBuffer("b")));
  SM_EXPECT_FALSE(c.sent[2].params.ordered);
  SM_EXPECT_TRUE(c.sent[2].params.type == DataMessageType::kText);
  SM_EXPECT_EQ(ch->messages_sent(), 2u);
  SM_EXPECT_EQ(ch->bytes_sent(), uint64_t{2});
}

SM_TEST(PcSctpDataChannel, AckerAndNegotiated) {
  FakeController c;
  auto acker = MakeChannel(c, {}, 1, InternalDataChannelInit::kAcker);
  acker->OnTransportReady(kT0);
  SM_ASSERT_EQ(c.sent.size(), size_t{1});
  SM_EXPECT_TRUE(c.sent[0].data == std::vector<uint8_t>{0x02});
  SM_EXPECT_TRUE(acker->state() == DataState::kOpen);
  DataChannelInit neg;
  neg.negotiated = true;
  neg.id = 4;
  auto n = MakeChannel(c, neg, 4, InternalDataChannelInit::kNone);
  n->OnTransportReady(kT0);
  SM_EXPECT_EQ(c.sent.size(), size_t{1});  // No control message.
  SM_EXPECT_TRUE(n->state() == DataState::kOpen);
  SM_EXPECT_TRUE(n->negotiated());
  SM_EXPECT_EQ(n->id(), 4);
}

// Messages that arrive before the observer is registered are queued and delivered in order.
SM_TEST(PcSctpDataChannel, QueuedUntilObserver) {
  FakeController c;
  auto ch = MakeChannel(c, {}, 1, InternalDataChannelInit::kAcker);
  ch->OnTransportReady(kT0);
  ch->OnDataReceived(DataMessageType::kText, Bytes("one"));
  ch->OnDataReceived(DataMessageType::kBinary, Bytes("two"));
  Observer obs;
  obs.Attach(*ch);
  SM_ASSERT_EQ(obs.messages.size(), size_t{2});
  SM_EXPECT_EQ(obs.messages[0], std::string("one"));
  SM_EXPECT_TRUE(obs.binary[1]);
  SM_EXPECT_EQ(ch->messages_received(), 2u);
  SM_EXPECT_EQ(ch->bytes_received(), uint64_t{6});
}

// Close waits for the send buffer to drain before the stream reset; closed when the reset completes.
SM_TEST(PcSctpDataChannel, CloseWaitsForBufferedData) {
  FakeController c;
  auto ch = MakeChannel(c, {}, 0, InternalDataChannelInit::kOpener);
  Observer obs;
  obs.Attach(*ch);
  ch->OnTransportReady(kT0);
  c.buffered = 100;
  ch->Close(kT0);
  SM_EXPECT_TRUE(ch->state() == DataState::kClosing);
  SM_EXPECT_TRUE(c.removed.empty());
  c.buffered = 0;
  ch->OnBufferedAmountLow(kT0);
  SM_ASSERT_EQ(c.removed.size(), size_t{1});
  SM_EXPECT_EQ(c.removed[0], 0);
  ch->OnClosingProcedureComplete();
  SM_EXPECT_TRUE(ch->state() == DataState::kClosed);
  SM_EXPECT_TRUE((obs.states == std::vector<DataState>{DataState::kOpen, DataState::kClosing, DataState::kClosed}));
  SM_EXPECT_FALSE(ch->Send(kT0, DataBuffer("late")));
  SM_EXPECT_TRUE(ch->error().type() == sctp::RtcErrorType::kInvalidState);
}

// A transport error on send closes the channel with a network error, as libwebrtc.
SM_TEST(PcSctpDataChannel, SendFailureClosesChannel) {
  FakeController c;
  auto ch = MakeChannel(c, {}, 1, InternalDataChannelInit::kAcker);
  ch->OnTransportReady(kT0);
  c.fail_send = true;
  SM_EXPECT_FALSE(ch->Send(kT0, DataBuffer("x")));
  SM_EXPECT_TRUE(ch->state() == DataState::kClosed);
  SM_EXPECT_TRUE(ch->error().type() == sctp::RtcErrorType::kNetworkError);
}

// The channel outlives its controller without touching it (libwebrtc's WeakPtr).
SM_TEST(PcSctpDataChannel, ControllerGone) {
  auto c = std::make_unique<FakeController>();
  auto ch = MakeChannel(*c, {}, 1, InternalDataChannelInit::kAcker);
  ch->OnTransportReady(kT0);
  c.reset();
  SM_EXPECT_EQ(ch->buffered_amount(), uint64_t{0});
  SM_EXPECT_FALSE(ch->Send(kT0, DataBuffer("x")));
  SM_EXPECT_TRUE(ch->error().type() == sctp::RtcErrorType::kInvalidState);
}

// ---- Two endpoints over ICE + DTLS + SCTP ----

namespace {

IpAddress Ip(std::string_view s) { return *IpAddress::FromString(s); }

const IpAddress kIp[2] = {Ip("11.11.11.11"), Ip("22.22.22.22")};
const IceParameters kIce[2] = {{"UF00", "TESTICEPWD00000000000000", false},
                               {"UF01", "TESTICEPWD00000000000001", false}};

sim::LinkConfig Link(double loss = 0, bool down = false) {
  sim::LinkConfig c;
  c.base_delay = TimeDelta::Millis(10);
  c.loss_rate = loss;
  c.down = down;
  return c;
}

struct Peer {
  std::unique_ptr<sim::EmulatedSocketServer> host;
  StaticNetworkManager networks;
  std::unique_ptr<BasicPortAllocator> allocator;
  std::unique_ptr<P2PTransportChannel> ice;
  std::shared_ptr<const tls::SslIdentity> cert;
  std::unique_ptr<DtlsTransport> dtls;
  std::unique_ptr<DataChannelSession> session;
  std::vector<std::shared_ptr<SctpDataChannel>> remote;  // Opened by the other side.
  std::deque<Observer> observers;
  EventLog events;
  Observer& Obs(const SctpDataChannel* c) {
    for (Observer& o : observers) {
      if (o.channel == c) return o;
    }
    return observers.front();
  }
  SctpDataChannel* Remote(const std::string& label) {
    for (auto& c : remote) {
      if (c->label() == label) return c.get();
    }
    return nullptr;
  }
};

struct Fixture {
  sim::ByteNetwork net;
  Peer peer[2];
  std::deque<Candidate> signaling[2];
  Timestamp now = Timestamp::Seconds(10);

  explicit Fixture(bool enable0 = true, bool enable1 = true) {
    InstallStunCryptoFromTls();
    const bool enable[2] = {enable0, enable1};
    for (int i = 0; i < 2; ++i) {
      Peer& p = peer[i];
      p.host = std::make_unique<sim::EmulatedSocketServer>(
          net, net.AddHost(kIp[i].v4(), sim::ByteNetwork::kPublicRealm, Link(), Link()));
      p.networks.AddInterface(kIp[i]);
      p.allocator = std::make_unique<BasicPortAllocator>(&p.networks, p.host.get());
      p.allocator->set_flags(kDefaultPortAllocatorFlags | PORTALLOCATOR_DISABLE_TCP);
      p.allocator->set_step_delay(kMinimumStepDelay);
      p.ice = std::make_unique<P2PTransportChannel>(now, i == 0 ? "t0" : "t1", ICE_CANDIDATE_COMPONENT_RTP,
                                                    p.allocator.get());
      p.ice->SubscribeCandidateGathered(this, [this, i](IceTransportInternal*, const Candidate& c) {
        signaling[1 - i].push_back(c);
      });
      p.ice->SetIceParameters(kIce[i]);
      p.ice->SetRemoteIceParameters(kIce[1 - i]);
      p.ice->SetIceRole(i == 0 ? ICEROLE_CONTROLLING : ICEROLE_CONTROLLED);
      p.cert = tls::SslIdentity::Create(i == 0 ? "peer0" : "peer1");
      p.dtls = std::make_unique<DtlsTransport>(p.ice.get());
      SM_EXPECT_TRUE(p.dtls->SetLocalCertificate(p.cert));
      DataChannelSession::Config config;
      config.enable_data_channels = enable[i];
      p.session = std::make_unique<DataChannelSession>(now + TimeDelta::Micros(i), p.dtls.get(), config);
      p.session->SetEventSink(&p.events);
      p.session->SetOnDataChannel([&p](std::shared_ptr<SctpDataChannel> c) {
        p.observers.emplace_back();
        p.observers.back().Attach(*c);
        p.remote.push_back(std::move(c));
      });
    }
    for (int i = 0; i < 2; ++i) {
      auto fp = tls::SslFingerprint::Create(tls::kDigestSha256, *peer[1 - i].cert);
      // Peer 0 is the DTLS client: even stream ids.
      SM_EXPECT_TRUE(peer[i].dtls->SetRemoteParameters(fp->algorithm, fp->digest,
                                                       i == 0 ? SslRole::kClient : SslRole::kServer, now));
      SM_EXPECT_EQ(peer[i].session->Start(now), enable[i]);
    }
    for (Peer& p : peer) p.ice->MaybeStartGathering(now);
  }
  ~Fixture() {
    for (Peer& p : peer) {
      p.remote.clear();
      p.session.reset();
    }
  }

  void SetLinks(double loss, bool down = false) {
    for (Peer& p : peer) {
      net.uplink(p.host->host()).SetConfig(Link(loss, down));
      net.downlink(p.host->host()).SetConfig(Link(loss, down));
    }
  }
  Timestamp NextEvent() const {
    Timestamp t = net.NextEvent();
    for (const Peer& p : peer) {
      t = std::min(t, p.ice->NextProcessTime());
      if (auto d = p.dtls->NextTimeout()) t = std::min(t, *d);
      t = std::min(t, p.session->NextProcessTime());
    }
    if (!signaling[0].empty() || !signaling[1].empty()) t = std::min(t, now);
    return t;
  }
  void Step(Timestamp t) {
    now = std::max(now, t);
    for (Peer& p : peer) {
      p.ice->Process(now);
      if (auto d = p.dtls->NextTimeout(); d && *d <= now) p.dtls->OnTimeout(now);
      p.session->Process(now);
      p.host->Deliver(now);
    }
    for (int i = 0; i < 2; ++i) {
      while (!signaling[i].empty()) {
        Candidate c = signaling[i].front();
        signaling[i].pop_front();
        peer[i].ice->AddRemoteCandidate(now, c);
      }
    }
  }
  template <class F>
  bool RunUntil(F stop, TimeDelta limit) {
    const Timestamp until = now + limit;
    for (int guard = 0; guard < 5000000; ++guard) {
      if (stop()) return true;
      const Timestamp t = NextEvent();
      if (t > until) {
        now = until;
        return stop();
      }
      Step(t);
    }
    return stop();
  }
  void RunFor(TimeDelta d) {
    RunUntil([] { return false; }, d);
  }
  bool DtlsUp() const {
    return peer[0].dtls->dtls_state() == DtlsTransportState::kConnected &&
           peer[1].dtls->dtls_state() == DtlsTransportState::kConnected;
  }
  // Creates a channel on `from` with an observer.
  std::shared_ptr<SctpDataChannel> Create(int from, const std::string& label, DataChannelInit init = {},
                                          Observer** obs = nullptr) {
    RtcError err;
    auto ch = peer[from].session->CreateDataChannel(now, label, init, &err);
    SM_EXPECT_TRUE(err.ok());
    if (!ch) return nullptr;
    peer[from].observers.emplace_back();
    peer[from].observers.back().Attach(*ch);
    if (obs) *obs = &peer[from].observers.back();
    return ch;
  }
};

}  // namespace

// In-band channels both ways: even ids from the DTLS client, odd from the server; label, protocol and the
// reliability settings reach the other side through DATA_CHANNEL_OPEN; text and binary both ways.
SM_TEST(PcDataChannels, InBandBothWays) {
  Fixture f;
  Observer* o0 = nullptr;
  Observer* o1 = nullptr;
  DataChannelInit init;
  init.protocol = "chat-v1";
  auto a = f.Create(0, "from-0", init, &o0);  // Before the association is up; the DTLS role is already set.
  SM_ASSERT_TRUE(a != nullptr);
  SM_EXPECT_TRUE(a->state() == DataState::kConnecting);
  SM_ASSERT_TRUE(f.RunUntil([&] { return a->state() == DataState::kOpen; }, TimeDelta::Seconds(10)));
  SM_EXPECT_EQ(a->id() % 2, 0);
  DataChannelInit init1;
  init1.ordered = false;
  init1.maxRetransmits = 2;
  auto b = f.Create(1, "from-1", init1, &o1);
  SM_ASSERT_TRUE(b != nullptr);
  SM_EXPECT_EQ(b->id() % 2, 1);  // DTLS server.
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.peer[0].remote.size() == 1 && f.peer[1].remote.size() == 1; },
                            TimeDelta::Seconds(5)));
  auto& a_remote = f.peer[1].remote[0];
  auto& b_remote = f.peer[0].remote[0];
  SM_EXPECT_EQ(a_remote->label(), std::string("from-0"));
  SM_EXPECT_EQ(a_remote->protocol(), std::string("chat-v1"));
  SM_EXPECT_EQ(a_remote->id(), a->id());
  SM_EXPECT_TRUE(a_remote->ordered());
  SM_EXPECT_TRUE(a_remote->reliable());
  SM_EXPECT_EQ(b_remote->label(), std::string("from-1"));
  SM_EXPECT_FALSE(b_remote->ordered());
  SM_EXPECT_EQ(*b_remote->maxRetransmitsOpt(), 2);
  SM_ASSERT_TRUE(f.RunUntil([&] { return a_remote->state() == DataState::kOpen && b->state() == DataState::kOpen; },
                            TimeDelta::Seconds(5)));
  SM_EXPECT_TRUE(o0->states.front() == DataState::kOpen);
  SM_EXPECT_TRUE(a->Send(f.now, DataBuffer("hello")));
  SM_EXPECT_TRUE(a->Send(f.now, DataBuffer(Bytes("\x01\x02"), true)));
  SM_EXPECT_TRUE(b->Send(f.now, DataBuffer("back")));
  Observer& ar = f.peer[1].Obs(a_remote.get());
  Observer& br = f.peer[0].Obs(b_remote.get());
  SM_ASSERT_TRUE(f.RunUntil([&] { return ar.messages.size() == 2 && br.messages.size() == 1; },
                            TimeDelta::Seconds(5)));
  SM_EXPECT_EQ(ar.messages[0], std::string("hello"));
  SM_EXPECT_FALSE(ar.binary[0]);
  SM_EXPECT_TRUE(ar.binary[1]);
  SM_EXPECT_EQ(br.messages[0], std::string("back"));
  SM_EXPECT_TRUE(ar.max_message_size.has_value() || a_remote->messages_received() == 2u);
  SM_EXPECT_GE(f.peer[1].events.Count("dc", "remote_open"), size_t{1});
  SM_EXPECT_TRUE(f.peer[0].session->controller()->HasDataChannels());
}

// Negotiated channels: same id on both sides, no DCEP, open as soon as the association is up.
SM_TEST(PcDataChannels, Negotiated) {
  Fixture f;
  DataChannelInit init;
  init.negotiated = true;
  init.id = 7;
  Observer* o0 = nullptr;
  Observer* o1 = nullptr;
  auto a = f.Create(0, "neg", init, &o0);
  auto b = f.Create(1, "neg", init, &o1);
  SM_ASSERT_TRUE(a && b);
  SM_EXPECT_EQ(a->id(), 7);
  SM_ASSERT_TRUE(f.RunUntil([&] { return a->state() == DataState::kOpen && b->state() == DataState::kOpen; },
                            TimeDelta::Seconds(10)));
  SM_EXPECT_TRUE(a->Send(f.now, DataBuffer("x")));
  SM_EXPECT_TRUE(b->Send(f.now, DataBuffer("y")));
  SM_ASSERT_TRUE(f.RunUntil([&] { return o0->messages.size() == 1 && o1->messages.size() == 1; },
                            TimeDelta::Seconds(5)));
  SM_EXPECT_EQ(o1->messages[0], std::string("x"));
  SM_EXPECT_TRUE(f.peer[0].remote.empty() && f.peer[1].remote.empty());
  // The id is taken.
  RtcError err;
  SM_EXPECT_TRUE(f.peer[0].session->CreateDataChannel(f.now, "dup", init, &err) == nullptr);
  SM_EXPECT_TRUE(err.type() == sctp::RtcErrorType::kInvalidRange);
  DataChannelInit bad;
  bad.maxRetransmits = 1;
  bad.maxRetransmitTime = 1;
  SM_EXPECT_TRUE(f.peer[0].session->CreateDataChannel(f.now, "bad", bad, &err) == nullptr);
  SM_EXPECT_TRUE(err.type() == sctp::RtcErrorType::kInvalidParameter);
}

// Ordered reliable gets everything in order under loss; unordered maxRetransmits=0 drops some;
// maxPacketLifeTime drops what is sent during an outage longer than its lifetime.
SM_TEST(PcDataChannels, PartialReliability) {
  Fixture f;
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.DtlsUp(); }, TimeDelta::Seconds(10)));
  DataChannelInit rel;
  DataChannelInit rtx0;
  rtx0.ordered = false;
  rtx0.maxRetransmits = 0;
  DataChannelInit life;
  life.maxRetransmitTime = 100;
  auto c_rel = f.Create(0, "rel", rel);
  auto c_rtx = f.Create(0, "rtx0", rtx0);
  auto c_life = f.Create(0, "life", life);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.peer[1].remote.size() == 3; }, TimeDelta::Seconds(5)));
  f.RunFor(TimeDelta::Millis(500));  // ACKs back, so unordered goes unordered.
  f.SetLinks(0.3);
  for (int i = 0; i < 50; ++i) {
    const std::string m = std::to_string(i);
    SM_EXPECT_TRUE(c_rel->Send(f.now, DataBuffer(m)));
    SM_EXPECT_TRUE(c_rtx->Send(f.now, DataBuffer(m)));
    f.RunFor(TimeDelta::Millis(20));
  }
  f.SetLinks(0);
  Observer& rel_r = f.peer[1].Obs(f.peer[1].Remote("rel"));
  Observer& rtx_r = f.peer[1].Obs(f.peer[1].Remote("rtx0"));
  Observer& life_r = f.peer[1].Obs(f.peer[1].Remote("life"));
  SM_ASSERT_TRUE(f.RunUntil([&] { return rel_r.messages.size() == 50; }, TimeDelta::Seconds(30)));
  for (int i = 0; i < 50; ++i) SM_EXPECT_EQ(rel_r.messages[static_cast<size_t>(i)], std::to_string(i));
  f.RunFor(TimeDelta::Seconds(2));
  SM_EXPECT_LT(rtx_r.messages.size(), size_t{50});
  SM_EXPECT_GT(rtx_r.messages.size(), size_t{10});
  // Outage of 2 s: everything sent during it outlives 100 ms.
  f.SetLinks(0, true);
  for (int i = 0; i < 5; ++i) SM_EXPECT_TRUE(c_life->Send(f.now, DataBuffer("lost")));
  f.RunFor(TimeDelta::Seconds(2));
  f.SetLinks(0);
  SM_EXPECT_TRUE(c_life->Send(f.now, DataBuffer("after")));
  SM_ASSERT_TRUE(f.RunUntil([&] { return !life_r.messages.empty(); }, TimeDelta::Seconds(20)));
  f.RunFor(TimeDelta::Seconds(1));
  SM_EXPECT_EQ(life_r.messages.size(), size_t{1});
  SM_EXPECT_EQ(life_r.messages[0], std::string("after"));
}

// Close from one side: both go closing -> closed; the id is free for a new channel.
SM_TEST(PcDataChannels, CloseBothSides) {
  Fixture f;
  Observer* o0 = nullptr;
  auto a = f.Create(0, "c", {}, &o0);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.peer[1].remote.size() == 1 && a->state() == DataState::kOpen; },
                            TimeDelta::Seconds(10)));
  f.RunFor(TimeDelta::Millis(200));
  auto r = f.peer[1].remote[0];
  Observer& ro = f.peer[1].Obs(r.get());
  const int id = a->id();
  a->Close(f.now);
  SM_EXPECT_TRUE(a->state() == DataState::kClosing);
  SM_ASSERT_TRUE(f.RunUntil([&] { return a->state() == DataState::kClosed && r->state() == DataState::kClosed; },
                            TimeDelta::Seconds(5)));
  SM_EXPECT_TRUE((o0->states == std::vector<DataState>{DataState::kOpen, DataState::kClosing, DataState::kClosed}));
  SM_EXPECT_TRUE((ro.states == std::vector<DataState>{DataState::kOpen, DataState::kClosing, DataState::kClosed}));
  SM_EXPECT_TRUE(a->error().ok());
  auto again = f.Create(0, "again");
  SM_ASSERT_TRUE(again != nullptr);
  SM_EXPECT_EQ(again->id(), id);
  SM_EXPECT_GE(f.peer[0].events.Count("dc", "state"), size_t{3});
}

// bufferedAmount grows with queued data and drains; the observer hears the change; a message over the
// 256 KiB max message size fails and closes the channel (libwebrtc M157).
SM_TEST(PcDataChannels, BufferedAmountAndMaxMessageSize) {
  Fixture f;
  Observer* o0 = nullptr;
  auto a = f.Create(0, "bulk", {}, &o0);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.peer[1].remote.size() == 1 && a->state() == DataState::kOpen; },
                            TimeDelta::Seconds(10)));
  SM_ASSERT_TRUE(o0->max_message_size.has_value());
  SM_EXPECT_EQ(*o0->max_message_size, 256 * 1024);
  const std::vector<uint8_t> chunk(200 * 1024, 0x5a);
  for (int i = 0; i < 4; ++i) SM_EXPECT_TRUE(a->Send(f.now, DataBuffer(chunk, true)));
  SM_EXPECT_GT(a->buffered_amount(), uint64_t{100 * 1024});
  Observer& ro = f.peer[1].Obs(f.peer[1].remote[0].get());
  SM_ASSERT_TRUE(f.RunUntil([&] { return ro.messages.size() == 4 && a->buffered_amount() == 0; },
                            TimeDelta::Seconds(30)));
  SM_EXPECT_EQ(o0->buffered_change, uint64_t{4 * 200 * 1024});
  const std::vector<uint8_t> big(256 * 1024 + 1, 1);
  SM_EXPECT_FALSE(a->Send(f.now, DataBuffer(big, true)));
  SM_EXPECT_TRUE(a->state() == DataState::kClosed);
  SM_EXPECT_TRUE(a->error().type() == sctp::RtcErrorType::kNetworkError);
}

// The runtime switch: off on one side means no SCTP transport and no channel there; DTLS still connects.
SM_TEST(PcDataChannels, RuntimeSwitchOff) {
  Fixture f(/*enable0=*/false, /*enable1=*/true);
  SM_EXPECT_FALSE(f.peer[0].session->enabled());
  SM_EXPECT_TRUE(f.peer[0].session->sctp_transport() == nullptr);
  SM_EXPECT_TRUE(f.peer[0].session->controller() == nullptr);
  RtcError err;
  SM_EXPECT_TRUE(f.peer[0].session->CreateDataChannel(f.now, "x", {}, &err) == nullptr);
  SM_EXPECT_TRUE(err.type() == sctp::RtcErrorType::kInvalidState);
  auto b = f.Create(1, "unanswered");
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.DtlsUp(); }, TimeDelta::Seconds(10)));
  f.RunFor(TimeDelta::Seconds(3));
  SM_EXPECT_TRUE(f.peer[1].session->sctp_transport()->Information().state() != sctp::SctpTransportState::kConnected);
  SM_EXPECT_TRUE(b->state() == DataState::kConnecting);
}

// DTLS closing closes every channel with the transport's error.
SM_TEST(PcDataChannels, TransportClosedClosesChannels) {
  Fixture f;
  Observer* o0 = nullptr;
  auto a = f.Create(0, "t", {}, &o0);
  SM_ASSERT_TRUE(f.RunUntil([&] { return a->state() == DataState::kOpen; }, TimeDelta::Seconds(10)));
  f.peer[0].session.reset();
  SM_EXPECT_TRUE(a->state() == DataState::kClosed);
  SM_EXPECT_FALSE(a->error().ok());
  SM_EXPECT_FALSE(a->Send(f.now, DataBuffer("x")));
  f.peer[0].session =
      std::make_unique<DataChannelSession>(f.now, f.peer[0].dtls.get(), DataChannelSession::Config{false, {}, {}});
}
