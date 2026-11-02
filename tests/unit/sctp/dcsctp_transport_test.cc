// SCTP over DTLS (C7 part 3, #1069): DcSctpTransport (media/sctp/dcsctp_transport) and SctpTransport
// (pc/sctp_transport) at M157. Two endpoints over ICE (P2PTransportChannel) and DTLS (DtlsTransport) on the emulated
// network: the association comes up once DTLS is connected; ordered, unordered and empty messages of each type;
// reliable delivery under loss; partial reliability by max retransmits and by max lifetime; stream reset from one end
// and from both; the send buffer (resource exhaustion, ready to send, buffered amount low); the max message size;
// SNAP with early packets; a DTLS failure closing the transport. libwebrtc's dcsctp_transport_unittest.cc and
// sctp_transport_unittest.cc are not in compare/webrtc/reference/ (asked for on #1069); the unit cases below follow
// dcsctp_transport.cc's behaviour without a transport.
#include <algorithm>
#include <deque>
#include <memory>
#include <optional>
#include <set>
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
#include "sm/sctp/dcsctp_transport.h"
#include "sm/sctp/sctp_transport.h"
#include "sm/sim/emulated_sockets.h"
#include "sm/tls/message_digest.h"
#include "sm/tls/ssl_fingerprint.h"
#include "sm/tls/ssl_identity.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::p2p;
using namespace sm::sctp;

namespace {

IpAddress Ip(std::string_view s) { return *IpAddress::FromString(s); }

const IpAddress kIp[2] = {Ip("11.11.11.11"), Ip("22.22.22.22")};
const IceParameters kIce[2] = {{"UF00", "TESTICEPWD00000000000000", false},
                               {"UF01", "TESTICEPWD00000000000001", false}};

constexpr int kSid = 1;

sim::LinkConfig Link(int delay_ms, double loss = 0) {
  sim::LinkConfig c;
  c.base_delay = TimeDelta::Millis(delay_ms);
  c.loss_rate = loss;
  return c;
}

std::vector<uint8_t> Bytes(std::string_view s) { return {s.begin(), s.end()}; }

struct Message {
  int sid;
  DataMessageType type;
  std::vector<uint8_t> data;
};

struct Sink final : DataChannelSink {
  std::vector<Message> messages;
  std::vector<int> closing, closed, buffered_low;
  int ready_to_send = 0, connected = 0, transport_closed = 0;
  std::optional<int> max_message_size;
  std::optional<RtcError> close_error;

  void OnDataReceived(int channel_id, DataMessageType type, std::span<const uint8_t> buffer) override {
    messages.push_back({channel_id, type, {buffer.begin(), buffer.end()}});
  }
  void OnChannelClosing(int channel_id) override { closing.push_back(channel_id); }
  void OnChannelClosed(int channel_id) override { closed.push_back(channel_id); }
  void OnReadyToSend() override { ++ready_to_send; }
  void OnTransportClosed(RtcError error) override {
    ++transport_closed;
    close_error = error;
  }
  void OnBufferedAmountLow(int channel_id) override { buffered_low.push_back(channel_id); }
  void OnTransportConnected() override { ++connected; }
  void OnMaxMessageSize(int size) override { max_message_size = size; }

  bool Got(std::string_view s) const {
    return std::any_of(messages.begin(), messages.end(), [&](const Message& m) { return m.data == Bytes(s); });
  }
};

struct Observer final : SctpTransportObserverInterface {
  std::vector<SctpTransportState> states;
  void OnStateChange(SctpTransportInformation info) override { states.push_back(info.state()); }
};

struct Peer {
  std::unique_ptr<sim::EmulatedSocketServer> host;
  StaticNetworkManager networks;
  std::unique_ptr<BasicPortAllocator> allocator;
  std::unique_ptr<P2PTransportChannel> ice;
  std::shared_ptr<const tls::SslIdentity> cert;
  std::unique_ptr<DtlsTransport> dtls;
  DcSctpTransport* internal = nullptr;
  std::unique_ptr<SctpTransport> sctp;
  Sink sink;
  Observer observer;
};

struct Fixture {
  sim::ByteNetwork net;
  Peer peer[2];
  std::deque<Candidate> signaling[2];  // Candidates for peer [i].
  Timestamp now = Timestamp::Seconds(10);

  explicit Fixture(bool start_sctp = true, SctpOptions options0 = {}, SctpOptions options1 = {}) {
    InstallStunCryptoFromTls();
    for (int i = 0; i < 2; ++i) {
      Peer& p = peer[i];
      p.host = std::make_unique<sim::EmulatedSocketServer>(
          net, net.AddHost(kIp[i].v4(), sim::ByteNetwork::kPublicRealm, Link(10), Link(10)));
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
      auto internal = std::make_unique<DcSctpTransport>(now + TimeDelta::Micros(i), p.dtls.get());
      p.internal = internal.get();
      p.sctp = std::make_unique<SctpTransport>(std::move(internal), p.dtls.get());
      p.sctp->SetDataSink(&p.sink);
      p.sctp->RegisterObserver(&p.observer);
    }
    for (int i = 0; i < 2; ++i) {
      auto fp = tls::SslFingerprint::Create(tls::kDigestSha256, *peer[1 - i].cert);
      SM_EXPECT_TRUE(peer[i].dtls->SetRemoteParameters(fp->algorithm, fp->digest,
                                                       i == 0 ? SslRole::kClient : SslRole::kServer, now));
    }
    if (start_sctp) {
      SM_EXPECT_TRUE(peer[0].sctp->Start(now, options0));
      SM_EXPECT_TRUE(peer[1].sctp->Start(now, options1));
    }
    for (Peer& p : peer) p.ice->MaybeStartGathering(now);
  }
  ~Fixture() {
    for (Peer& p : peer) p.sctp.reset();
  }

  void SetLinks(double loss, bool down = false) {
    for (Peer& p : peer) {
      sim::LinkConfig c = Link(10, loss);
      c.down = down;
      net.uplink(p.host->host()).SetConfig(c);
      net.downlink(p.host->host()).SetConfig(c);
    }
  }

  Timestamp NextEvent() const {
    Timestamp t = net.NextEvent();
    for (const Peer& p : peer) {
      t = std::min(t, p.ice->NextProcessTime());
      if (auto d = p.dtls->NextTimeout()) t = std::min(t, *d);
      if (p.sctp->internal()) t = std::min(t, p.sctp->internal()->NextProcessTime());
    }
    if (!signaling[0].empty() || !signaling[1].empty()) t = std::min(t, now);
    return t;
  }
  void Step(Timestamp t) {
    now = std::max(now, t);
    for (Peer& p : peer) {
      p.ice->Process(now);
      if (auto d = p.dtls->NextTimeout(); d && *d <= now) p.dtls->OnTimeout(now);
      if (p.sctp->internal()) p.sctp->internal()->Process(now);
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

  bool Connected() const { return peer[0].sink.connected > 0 && peer[1].sink.connected > 0; }
  void OpenBoth(int sid) {
    for (Peer& p : peer) SM_EXPECT_TRUE(p.sctp->OpenChannel(sid, PriorityValue()).ok());
  }
  RtcError Send(int from, int sid, std::string_view data, SendDataParams params = {}) {
    std::vector<uint8_t> b = Bytes(data);
    return peer[from].sctp->SendData(now, sid, params, b);
  }
};

SendDataParams Params(DataMessageType type, bool ordered = true) {
  SendDataParams p;
  p.type = type;
  p.ordered = ordered;
  return p;
}

}  // namespace

// The association comes up after DTLS, with the negotiated streams and max message size.
SM_TEST(SctpOverDtls, AssociationUp) {
  Fixture f;
  const Timestamp start = f.now;
  for (const Peer& p : f.peer) {
    SM_EXPECT_TRUE(p.sctp->Information().state() == SctpTransportState::kConnecting);
    SM_EXPECT_FALSE(p.sctp->IsReadyToSend());
    SM_EXPECT_TRUE(p.internal->socket_state() == dcsctp::SocketState::kClosed);  // Waits for DTLS.
    SM_EXPECT_EQ(p.sink.max_message_size.value_or(0), kSctpSendBufferSize);
  }
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected(); }, TimeDelta::Seconds(10)));
  SM_EXPECT_LT((f.now - start).ms(), 2000);
  for (const Peer& p : f.peer) {
    SM_EXPECT_TRUE(p.dtls->dtls_state() == DtlsTransportState::kConnected);
    SM_EXPECT_TRUE(p.internal->socket_state() == dcsctp::SocketState::kConnected);
    SM_EXPECT_TRUE(p.sctp->IsReadyToSend());
    SM_EXPECT_EQ(p.sink.ready_to_send, 1);
    SM_EXPECT_TRUE(p.sctp->Information().state() == SctpTransportState::kConnected);
    SM_ASSERT_TRUE(p.observer.states.size() == 1);
    SM_EXPECT_TRUE(p.observer.states[0] == SctpTransportState::kConnected);
    SM_EXPECT_EQ(p.sctp->MaxChannels().value_or(0), kMaxSctpStreams);
    SM_EXPECT_EQ(p.internal->max_outbound_streams().value_or(0), kMaxSctpStreams);
    SM_EXPECT_EQ(p.internal->max_inbound_streams().value_or(0), kMaxSctpStreams);
    SM_EXPECT_EQ(p.internal->max_message_size(), kSctpSendBufferSize);
    SM_EXPECT_EQ(p.sctp->Information().MaxMessageSize().value_or(0), static_cast<double>(kSctpSendBufferSize));
    SM_EXPECT_TRUE(p.sctp->DtlsRole().has_value());
  }
  SM_EXPECT_TRUE(*f.peer[0].sctp->DtlsRole() == SslRole::kClient);
  SM_EXPECT_TRUE(*f.peer[1].sctp->DtlsRole() == SslRole::kServer);
}

// Text, binary and control messages, empty ones (sent as one zero byte with PPID 56 / 57) and unordered ones, both
// ways; a message to a SID that isn't open fails.
SM_TEST(SctpOverDtls, MessagesByTypeOrderedAndUnordered) {
  Fixture f;
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected(); }, TimeDelta::Seconds(10)));
  f.OpenBoth(kSid);
  SM_EXPECT_TRUE(f.Send(0, kSid, "hello", Params(DataMessageType::kText)).ok());
  SM_EXPECT_TRUE(f.Send(0, kSid, "\x01\x02\x03", Params(DataMessageType::kBinary)).ok());
  SM_EXPECT_TRUE(f.Send(0, kSid, "", Params(DataMessageType::kText)).ok());
  SM_EXPECT_TRUE(f.Send(0, kSid, "", Params(DataMessageType::kBinary)).ok());
  SM_EXPECT_TRUE(f.Send(0, kSid, "dcep", Params(DataMessageType::kControl)).ok());
  SM_EXPECT_TRUE(f.Send(0, kSid, "unordered", Params(DataMessageType::kBinary, false)).ok());
  SM_EXPECT_TRUE(f.Send(1, kSid, "reply", Params(DataMessageType::kText)).ok());
  SM_EXPECT_TRUE(f.Send(0, 7, "nope").type() == RtcErrorType::kInvalidState);  // Not open.
  Sink& r = f.peer[1].sink;
  SM_ASSERT_TRUE(f.RunUntil([&] { return r.messages.size() == 6 && f.peer[0].sink.Got("reply"); },
                            TimeDelta::Seconds(5)));
  // Ordered messages in order; the unordered one somewhere.
  std::vector<Message> ordered;
  for (const Message& m : r.messages) {
    SM_EXPECT_EQ(m.sid, kSid);
    if (m.data != Bytes("unordered")) ordered.push_back(m);
  }
  SM_ASSERT_TRUE(ordered.size() == 5);
  SM_EXPECT_TRUE(ordered[0].type == DataMessageType::kText && ordered[0].data == Bytes("hello"));
  SM_EXPECT_TRUE(ordered[1].type == DataMessageType::kBinary && ordered[1].data == Bytes("\x01\x02\x03"));
  SM_EXPECT_TRUE(ordered[2].type == DataMessageType::kText && ordered[2].data.empty());
  SM_EXPECT_TRUE(ordered[3].type == DataMessageType::kBinary && ordered[3].data.empty());
  SM_EXPECT_TRUE(ordered[4].type == DataMessageType::kControl && ordered[4].data == Bytes("dcep"));
  SM_EXPECT_TRUE(r.Got("unordered"));
  SM_EXPECT_TRUE(f.peer[0].sink.messages[0].type == DataMessageType::kText);
}

// Reliable ordered messages all arrive, in order, with 10% loss each way on every link.
SM_TEST(SctpOverDtls, ReliableUnderLoss) {
  Fixture f;
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected(); }, TimeDelta::Seconds(10)));
  f.OpenBoth(kSid);
  f.SetLinks(0.10);
  constexpr int kCount = 100;
  for (int i = 0; i < kCount; ++i) {
    SM_ASSERT_TRUE(f.Send(0, kSid, "m" + std::to_string(i) + std::string(1000, 'x'), Params(DataMessageType::kText))
                       .ok());
    f.RunFor(TimeDelta::Millis(10));
  }
  Sink& r = f.peer[1].sink;
  SM_ASSERT_TRUE(f.RunUntil([&] { return r.messages.size() == kCount; }, TimeDelta::Seconds(60)));
  for (int i = 0; i < kCount; ++i) {
    SM_EXPECT_TRUE(r.messages[i].data == Bytes("m" + std::to_string(i) + std::string(1000, 'x')));
  }
  SM_EXPECT_GT(f.net.uplink(f.peer[0].host->host()).stats().lost, 0u);
}

// Max retransmits 0 under 30% loss on each link: lost messages are abandoned (FORWARD-TSN), the rest arrive once
// each, and the stream keeps working for a reliable message after them.
SM_TEST(SctpOverDtls, MaxRetransmitsUnderLoss) {
  Fixture f;
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected(); }, TimeDelta::Seconds(10)));
  f.OpenBoth(kSid);
  f.SetLinks(0.30);
  constexpr int kCount = 200;
  SendDataParams pr = Params(DataMessageType::kBinary, /*ordered=*/false);
  pr.max_rtx_count = 0;
  for (int i = 0; i < kCount; ++i) {
    SM_ASSERT_TRUE(f.Send(0, kSid, "pr" + std::to_string(i), pr).ok());
    f.RunFor(TimeDelta::Millis(10));
  }
  SM_ASSERT_TRUE(f.Send(0, kSid, "end", Params(DataMessageType::kText)).ok());
  Sink& r = f.peer[1].sink;
  SM_ASSERT_TRUE(f.RunUntil([&] { return r.Got("end"); }, TimeDelta::Seconds(60)));
  std::set<std::vector<uint8_t>> unique;
  for (const Message& m : r.messages) unique.insert(m.data);
  SM_EXPECT_EQ(unique.size(), r.messages.size());  // No duplicates.
  const size_t pr_received = r.messages.size() - 1;
  SM_EXPECT_LT(pr_received, static_cast<size_t>(kCount));
  // Each packet crosses two lossy links (the sender's uplink, the receiver's downlink): about 0.7 x 0.7 get through.
  SM_EXPECT_GT(pr_received, static_cast<size_t>(kCount / 3));
}

// Max lifetime: ordered messages sent during an outage expire and are skipped; a reliable message after the outage
// arrives.
SM_TEST(SctpOverDtls, MaxLifetimeDuringOutage) {
  Fixture f;
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected(); }, TimeDelta::Seconds(10)));
  f.OpenBoth(kSid);
  SM_ASSERT_TRUE(f.Send(0, kSid, "before", Params(DataMessageType::kText)).ok());
  Sink& r = f.peer[1].sink;
  SM_ASSERT_TRUE(f.RunUntil([&] { return r.Got("before"); }, TimeDelta::Seconds(2)));
  f.SetLinks(0, /*down=*/true);
  SendDataParams pr = Params(DataMessageType::kText, /*ordered=*/true);
  pr.max_rtx_ms = 100;
  for (int i = 0; i < 10; ++i) SM_ASSERT_TRUE(f.Send(0, kSid, "short" + std::to_string(i), pr).ok());
  f.RunFor(TimeDelta::Millis(1500));
  f.SetLinks(0);
  SM_ASSERT_TRUE(f.Send(0, kSid, "after", Params(DataMessageType::kText)).ok());
  SM_ASSERT_TRUE(f.RunUntil([&] { return r.Got("after"); }, TimeDelta::Seconds(30)));
  SM_EXPECT_EQ(r.messages.size(), 2u);  // "before" and "after" only.
}

// One end closes a channel: the other reports it closing and resets its own direction; both report it closed.
// Sending on it then fails, and other channels keep working.
SM_TEST(SctpOverDtls, StreamReset) {
  Fixture f;
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected(); }, TimeDelta::Seconds(10)));
  f.OpenBoth(kSid);
  f.OpenBoth(2);
  SM_ASSERT_TRUE(f.Send(0, kSid, "x", Params(DataMessageType::kText)).ok());
  SM_EXPECT_TRUE(f.peer[0].sctp->CloseChannel(f.now, kSid).ok());
  SM_EXPECT_TRUE(f.Send(0, kSid, "y").type() == RtcErrorType::kInvalidState);  // Closing.
  SM_EXPECT_FALSE(f.peer[0].internal->ResetStream(f.now, kSid));               // Already closing.
  Sink& s0 = f.peer[0].sink;
  Sink& s1 = f.peer[1].sink;
  SM_ASSERT_TRUE(f.RunUntil([&] { return s0.closed.size() == 1 && s1.closed.size() == 1; }, TimeDelta::Seconds(5)));
  SM_EXPECT_EQ(s0.closed[0], kSid);
  SM_EXPECT_EQ(s1.closed[0], kSid);
  SM_EXPECT_TRUE(s0.closing.empty());  // Closed locally.
  SM_ASSERT_TRUE(s1.closing.size() == 1);
  SM_EXPECT_EQ(s1.closing[0], kSid);
  SM_EXPECT_TRUE(s1.Got("x"));
  SM_EXPECT_TRUE(f.Send(0, kSid, "z").type() == RtcErrorType::kInvalidState);
  SM_EXPECT_TRUE(f.Send(1, kSid, "z").type() == RtcErrorType::kInvalidState);
  SM_EXPECT_FALSE(f.peer[0].internal->ResetStream(f.now, kSid));  // Not open.
  SM_ASSERT_TRUE(f.Send(1, 2, "still", Params(DataMessageType::kText)).ok());
  SM_ASSERT_TRUE(f.RunUntil([&] { return s0.Got("still"); }, TimeDelta::Seconds(2)));
  // The SID can be opened again.
  f.OpenBoth(kSid);
  SM_ASSERT_TRUE(f.Send(0, kSid, "again", Params(DataMessageType::kText)).ok());
  SM_ASSERT_TRUE(f.RunUntil([&] { return s1.Got("again"); }, TimeDelta::Seconds(2)));
}

// Both ends close the channel at once: both report it closed and neither reports it closing.
SM_TEST(SctpOverDtls, StreamResetSimultaneous) {
  Fixture f;
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected(); }, TimeDelta::Seconds(10)));
  f.OpenBoth(kSid);
  SM_EXPECT_TRUE(f.peer[0].sctp->CloseChannel(f.now, kSid).ok());
  SM_EXPECT_TRUE(f.peer[1].sctp->CloseChannel(f.now, kSid).ok());
  Sink& s0 = f.peer[0].sink;
  Sink& s1 = f.peer[1].sink;
  SM_ASSERT_TRUE(f.RunUntil([&] { return s0.closed.size() == 1 && s1.closed.size() == 1; }, TimeDelta::Seconds(5)));
  SM_EXPECT_TRUE(s0.closing.empty());
  SM_EXPECT_TRUE(s1.closing.empty());
}

// The send buffer: sending past the 16 MiB per-stream queue fails with kResourceExhausted and clears ready to send;
// once it drains, OnBufferedAmountLow and OnReadyToSend come and everything sent arrives.
SM_TEST(SctpOverDtls, SendBufferFullThenReadyToSend) {
  Fixture f;
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected(); }, TimeDelta::Seconds(10)));
  f.OpenBoth(kSid);
  Peer& s = f.peer[0];
  s.sctp->SetBufferedAmountLowThreshold(kSid, 1000);
  SM_EXPECT_EQ(s.sctp->buffered_amount_low_threshold(kSid), 1000u);
  const std::string chunk(64 * 1024, 'b');
  int sent = 0;
  RtcError err;
  for (; sent < 1000; ++sent) {
    err = f.Send(0, kSid, chunk, Params(DataMessageType::kBinary));
    if (!err.ok()) break;
  }
  SM_EXPECT_TRUE(err.type() == RtcErrorType::kResourceExhausted);
  SM_EXPECT_GE(static_cast<size_t>(sent) * chunk.size(), kMaxSendQueueSize);
  SM_EXPECT_FALSE(s.sctp->IsReadyToSend());
  SM_EXPECT_GE(s.sctp->buffered_amount(kSid), kMaxSendQueueSize);
  const int ready_before = s.sink.ready_to_send;
  Sink& r = f.peer[1].sink;
  SM_ASSERT_TRUE(f.RunUntil([&] { return r.messages.size() == static_cast<size_t>(sent); }, TimeDelta::Seconds(120)));
  SM_EXPECT_TRUE(s.sctp->IsReadyToSend());
  SM_EXPECT_EQ(s.sink.ready_to_send, ready_before + 1);
  SM_EXPECT_EQ(s.sctp->buffered_amount(kSid), 0u);
  SM_ASSERT_TRUE(!s.sink.buffered_low.empty());
  SM_EXPECT_EQ(s.sink.buffered_low.back(), kSid);
}

// The max message size: larger messages fail with kInvalidRange; a second Start may lower it but not change the
// ports, and a failed Start closes the transport.
SM_TEST(SctpOverDtls, MaxMessageSizeAndRestart) {
  Fixture f;
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.Connected(); }, TimeDelta::Seconds(10)));
  f.OpenBoth(kSid);
  Peer& s = f.peer[0];
  SM_EXPECT_TRUE(f.Send(0, kSid, std::string(kSctpSendBufferSize + 1, 'a')).type() == RtcErrorType::kInvalidRange);
  SM_EXPECT_TRUE(f.Send(0, kSid, std::string(kSctpSendBufferSize, 'a')).ok());
  SctpOptions smaller;
  smaller.max_message_size = 1000;
  SM_EXPECT_TRUE(s.sctp->Start(f.now, smaller));
  SM_EXPECT_EQ(s.internal->max_message_size(), 1000);
  SM_EXPECT_EQ(s.sink.max_message_size.value_or(0), 1000);
  SM_EXPECT_TRUE(f.Send(0, kSid, std::string(1001, 'a')).type() == RtcErrorType::kInvalidRange);
  SM_EXPECT_TRUE(f.Send(0, kSid, std::string(1000, 'a')).ok());
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.peer[1].sink.messages.size() == 2; }, TimeDelta::Seconds(5)));
  SctpOptions other_port = smaller;
  other_port.local_port = 5001;
  SM_EXPECT_FALSE(s.sctp->Start(f.now, other_port));
  SM_EXPECT_TRUE(s.sctp->Information().state() == SctpTransportState::kClosed);
  SM_EXPECT_TRUE(s.observer.states.back() == SctpTransportState::kClosed);
}

// SNAP (a=sctp-init on both sides): the association is up without INIT; packets that reach the answerer before its
// Start() are buffered and replayed when it starts.
SM_TEST(SctpOverDtls, SnapWithEarlyPackets) {
  Fixture f(/*start_sctp=*/false);
  const std::vector<uint8_t> token[2] = {DcSctpTransport::GenerateConnectionToken(f.now),
                                         DcSctpTransport::GenerateConnectionToken(f.now + TimeDelta::Millis(1))};
  SM_EXPECT_FALSE(token[0].empty());
  SM_EXPECT_TRUE(token[0] != token[1]);
  SctpOptions o[2];
  for (int i = 0; i < 2; ++i) {
    o[i].local_init = token[i];
    o[i].remote_init = token[1 - i];
  }
  SM_EXPECT_TRUE(f.peer[0].sctp->Start(f.now, o[0]));
  f.peer[0].sctp->OpenChannel(kSid, PriorityValue());
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.peer[0].sink.connected > 0 && f.peer[1].dtls->writable(); },
                            TimeDelta::Seconds(10)));
  SM_ASSERT_TRUE(f.Send(0, kSid, "early", Params(DataMessageType::kText)).ok());
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.peer[1].internal->EarlyReceivedPacketCountForTesting() > 0; },
                            TimeDelta::Seconds(2)));
  f.peer[1].sctp->OpenChannel(kSid, PriorityValue());
  SM_EXPECT_TRUE(f.peer[1].sctp->Start(f.now, o[1]));
  SM_EXPECT_EQ(f.peer[1].internal->EarlyReceivedPacketCountForTesting(), 0u);
  SM_EXPECT_EQ(f.peer[1].sink.connected, 1);
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.peer[1].sink.Got("early"); }, TimeDelta::Seconds(2)));
  SM_ASSERT_TRUE(f.Send(1, kSid, "back", Params(DataMessageType::kText)).ok());
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.peer[0].sink.Got("back"); }, TimeDelta::Seconds(2)));
  // A second Start may not change sctp-init.
  SctpOptions changed = o[1];
  changed.local_init = token[0];
  SM_EXPECT_FALSE(f.peer[1].internal->Start(f.now, changed));
}

// A DTLS failure (the peer's certificate doesn't match the fingerprint) closes the SCTP transport.
SM_TEST(SctpOverDtls, DtlsFailureClosesTransport) {
  Fixture f(/*start_sctp=*/false);
  // A new DTLS transport on peer 1 that expects another certificate.
  auto fp = tls::SslFingerprint::Create(tls::kDigestSha256, *tls::SslIdentity::Create("someone-else"));
  // The fingerprint is set once DTLS is set up; a different one restarts DTLS with it, as a new remote description.
  SM_EXPECT_TRUE(f.peer[1].dtls->SetRemoteParameters(fp->algorithm, fp->digest, SslRole::kServer, f.now));
  SM_EXPECT_TRUE(f.peer[0].sctp->Start(f.now, {}));
  SM_EXPECT_TRUE(f.peer[1].sctp->Start(f.now, {}));
  f.RunUntil([&] { return f.peer[1].dtls->dtls_state() == DtlsTransportState::kFailed; }, TimeDelta::Seconds(10));
  SM_EXPECT_TRUE(f.peer[1].dtls->dtls_state() == DtlsTransportState::kFailed);
  SM_EXPECT_TRUE(f.peer[1].sctp->Information().state() == SctpTransportState::kClosed);
  SM_EXPECT_FALSE(f.peer[1].sctp->IsReadyToSend());
  SM_EXPECT_EQ(f.peer[1].sink.connected, 0);
}

// Without a transport and before Start(), as dcsctp_transport.cc: sends fail with kInvalidState, nothing is open.
SM_TEST(SctpDcSctpTransport, NotStarted) {
  DcSctpTransport t(Timestamp::Seconds(1), nullptr);
  const uint8_t b[1] = {1};
  SM_EXPECT_TRUE(t.SendData(Timestamp::Seconds(1), kSid, {}, b).type() == RtcErrorType::kInvalidState);
  SM_EXPECT_FALSE(t.ResetStream(Timestamp::Seconds(1), kSid));
  SM_EXPECT_FALSE(t.ReadyToSendData());
  SM_EXPECT_EQ(t.max_message_size(), 0);
  SM_EXPECT_FALSE(t.max_outbound_streams().has_value());
  SM_EXPECT_EQ(t.buffered_amount(kSid), 0u);
  SM_EXPECT_FALSE(t.NextProcessTime().IsFinite());
  SM_EXPECT_TRUE(t.dtls_transport() == nullptr);
}

// Started without a writable transport: the socket exists but doesn't connect; a SID must be open to send, and a
// stream must be open to reset.
SM_TEST(SctpDcSctpTransport, StartedWithoutTransport) {
  DcSctpTransport t(Timestamp::Seconds(1), nullptr);
  Sink sink;
  t.SetDataChannelSink(&sink);
  SM_EXPECT_TRUE(t.Start(Timestamp::Seconds(1), {}));
  SM_EXPECT_TRUE(t.socket_state() == dcsctp::SocketState::kClosed);
  SM_EXPECT_EQ(sink.max_message_size.value_or(0), kSctpSendBufferSize);
  SM_EXPECT_EQ(t.max_message_size(), kSctpSendBufferSize);
  const uint8_t b[1] = {1};
  SM_EXPECT_TRUE(t.SendData(Timestamp::Seconds(1), kSid, {}, b).type() == RtcErrorType::kInvalidState);
  SM_EXPECT_FALSE(t.ResetStream(Timestamp::Seconds(1), kSid));
  SM_EXPECT_TRUE(t.OpenStream(kSid, PriorityValue(PriorityValue::kHigh)));
  SM_EXPECT_TRUE(t.ResetStream(Timestamp::Seconds(1), kSid));
  SM_EXPECT_FALSE(t.ResetStream(Timestamp::Seconds(1), kSid));
  SM_EXPECT_EQ(sink.ready_to_send, 0);
}
