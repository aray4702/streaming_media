// Data channel (9.6-9.9) benchmarks, built with SM_WITH_DATACHANNEL: the dcSCTP packet layer (9.6) and socket (9.7)
// on their own, then, with SM_WITH_TLS as well (SM_BENCH_DC_OVER_DTLS), SCTP over DTLS (9.8) and DCEP with the data
// channel API (9.9) over two DTLS transports on the DTLS tests' fake ICE transport
// (tests/unit/p2p_dtls/fake_ice_transport.h). Time is simulated: the library reads no clock, so the harness jumps
// from one due event (a packet's arrival, a DTLS or SCTP timer) to the next.
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "sm/core/units.h"
#include "sm/dcsctp/packet/chunk/data_chunk.h"
#include "sm/dcsctp/packet/chunk/sack_chunk.h"
#include "sm/dcsctp/packet/sctp_packet.h"
#include "sm/dcsctp/public/dcsctp_message.h"
#include "sm/dcsctp/public/dcsctp_options.h"
#include "sm/dcsctp/public/dcsctp_socket.h"
#include "sm/dcsctp/socket/dcsctp_socket.h"
#include "sm/dcsctp/timer/process_timeout.h"
#include "sm_bench.h"

#if SM_BENCH_DC_OVER_DTLS
#include "fake_ice_transport.h"
#include "sm/p2p/dtls_transport.h"
#include "sm/pc/data_channel_controller.h"
#include "sm/pc/sctp_data_channel.h"
#include "sm/sctp/dcsctp_transport.h"
#include "sm/sctp/sctp_transport.h"
#include "sm/tls/message_digest.h"
#include "sm/tls/ssl_fingerprint.h"
#include "sm/tls/ssl_identity.h"
#endif

using namespace sm;

namespace {

namespace dc = sm::dcsctp;

constexpr size_t kMessageSize = 1000;

// ---- 9.7: a dcSCTP socket with its timers on a ProcessTimeoutFactory and its packets queued for the peer.
struct SocketPeer final : dc::DcSctpSocketCallbacks {
  dc::ProcessTimeoutFactory timeouts;
  std::unique_ptr<dc::DcSctpSocket> socket;
  std::vector<std::vector<uint8_t>> sent;
  uint64_t rng;
  size_t received = 0, received_bytes = 0;
  bool connected = false, failed = false;

  SocketPeer(std::string_view name, uint64_t seed, Timestamp now)
      : timeouts([this](dc::TimeoutID id) { socket->HandleTimeout(id); }, now), rng(seed) {
    socket = std::make_unique<dc::DcSctpSocket>(name, *this, nullptr, dc::DcSctpOptions{});
  }
  dc::SendPacketStatus SendPacketWithStatus(std::span<const uint8_t> data) override {
    sent.emplace_back(data.begin(), data.end());
    return dc::SendPacketStatus::kSuccess;
  }
  std::unique_ptr<dc::Timeout> CreateTimeout(dc::DelayPrecision precision) override {
    return timeouts.CreateTimeout(precision);
  }
  dc::TimeMs TimeMillis() override { return dc::TimeMs(timeouts.now().ms()); }
  Timestamp Now() override { return timeouts.now(); }
  uint32_t GetRandomInt(uint32_t low, uint32_t high) override {
    rng ^= rng >> 12;
    rng ^= rng << 25;
    rng ^= rng >> 27;
    const uint64_t r = rng * 0x2545F4914F6CDD1DULL;
    return high > low ? low + static_cast<uint32_t>((r >> 32) % (high - low)) : low;
  }
  void OnMessageReceived(dc::DcSctpMessage message) override {
    ++received;
    received_bytes += message.payload().size();
  }
  void OnError(dc::ErrorKind, std::string_view) override {}
  void OnAborted(dc::ErrorKind, std::string_view) override { failed = true; }
  void OnConnected() override { connected = true; }
  void OnClosed() override {}
  void OnConnectionRestarted() override {}
  void OnStreamsResetFailed(std::span<const dc::StreamID>, std::string_view) override {}
  void OnStreamsResetPerformed(std::span<const dc::StreamID>) override {}
  void OnIncomingStreamsReset(std::span<const dc::StreamID>) override {}
};

// Two sockets on a lossless, zero-delay path: packets go straight across until both are quiet.
struct SocketPair {
  Timestamp now = Timestamp::Seconds(10);
  SocketPeer a{"A", 1, now}, b{"Z", 2, now};

  void Pump() {
    for (int round = 0; round < 64; ++round) {
      bool moved = false;
      for (SocketPeer* from : {&a, &b}) {
        SocketPeer* to = from == &a ? &b : &a;
        std::vector<std::vector<uint8_t>> packets;
        packets.swap(from->sent);
        for (auto& p : packets) to->socket->ReceivePacket(p);
        moved = moved || !packets.empty();
      }
      if (!moved) return;
    }
  }
  // Advances the clock and fires the timers due (the delayed SACK among them), then delivers what they sent.
  void Advance(TimeDelta d) {
    now = now + d;
    a.timeouts.Process(now);
    b.timeouts.Process(now);
    Pump();
  }
  bool Connect() {
    a.socket->Connect();
    Pump();
    return a.connected && b.connected;
  }
};

#if SM_BENCH_DC_OVER_DTLS
using p2p::test::FakeIceTransport;
using p2p::test::FakeNetwork;

// Two p2p::DtlsTransports over the fake ICE transport, 5 ms apart, each with the other's fingerprint; end 0 is the
// DTLS client. ICE is connected by Connect() once the layers above are in place.
struct DtlsEnd {
  std::unique_ptr<FakeIceTransport> ice;
  std::shared_ptr<const tls::SslIdentity> cert;
  std::unique_ptr<p2p::DtlsTransport> dtls;
};

struct DtlsLink {
  FakeNetwork net;
  DtlsEnd end[2];

  DtlsLink() {
    net.delay = TimeDelta::Millis(5);
    for (int i = 0; i < 2; ++i) {
      DtlsEnd& e = end[i];
      e.ice = std::make_unique<FakeIceTransport>(i == 0 ? "P0" : "P1", 1, &net);
      e.cert = tls::SslIdentity::Create(i == 0 ? "peer0" : "peer1");
      e.dtls = std::make_unique<p2p::DtlsTransport>(e.ice.get());
      e.dtls->SetLocalCertificate(e.cert);
    }
    for (int i = 0; i < 2; ++i) {
      auto fp = tls::SslFingerprint::Create(tls::kDigestSha256, *end[1 - i].cert);
      end[i].dtls->SetRemoteParameters(fp->algorithm, fp->digest, i == 0 ? p2p::SslRole::kClient : p2p::SslRole::kServer,
                                       net.now);
    }
  }
  void ConnectIce() { end[0].ice->SetDestination(end[1].ice.get()); }
  Timestamp NextNetworkEvent() const {
    Timestamp t = Timestamp::PlusInfinity();
    for (const auto& f : net.flight) t = std::min(t, f.at);
    for (const DtlsEnd& e : end) {
      if (auto d = e.dtls->NextTimeout()) t = std::min(t, *d);
    }
    return t;
  }
  void StepNetwork() {
    net.DeliverDue();
    for (DtlsEnd& e : end) {
      if (auto d = e.dtls->NextTimeout(); d && *d <= net.now) e.dtls->OnTimeout(net.now);
    }
    net.DeliverDue();
  }
};

// Runs `link` and the layers above it (`next_above`, `process_above`) event by event until `stop` holds or `limit`
// of simulated time has passed.
template <class Stop, class Next, class Process>
bool RunUntil(DtlsLink& link, Stop stop, Next next_above, Process process_above, TimeDelta limit) {
  const Timestamp until = link.net.now + limit;
  for (int guard = 0; guard < 1000000; ++guard) {
    if (stop()) return true;
    const Timestamp t = std::max(link.net.now, std::min(link.NextNetworkEvent(), next_above()));
    if (t > until) {
      link.net.now = until;
      return stop();
    }
    link.net.now = t;
    link.StepNetwork();
    process_above(t);
  }
  return stop();
}

// ---- 9.8: SctpTransport (DcSctpTransport) on each DTLS end.
struct SctpSink final : sctp::DataChannelSink {
  size_t messages = 0, bytes = 0;
  bool connected = false;
  void OnDataReceived(int, sctp::DataMessageType, std::span<const uint8_t> buffer) override {
    ++messages;
    bytes += buffer.size();
  }
  void OnChannelClosing(int) override {}
  void OnChannelClosed(int) override {}
  void OnReadyToSend() override {}
  void OnTransportClosed(sctp::RtcError) override {}
  void OnTransportConnected() override { connected = true; }
};

struct SctpOverDtls {
  static constexpr int kSid = 1;
  DtlsLink link;
  std::unique_ptr<sctp::SctpTransport> sctp[2];
  SctpSink sink[2];

  SctpOverDtls() {
    for (int i = 0; i < 2; ++i) {
      auto internal = std::make_unique<sctp::DcSctpTransport>(link.net.now + TimeDelta::Micros(i), link.end[i].dtls.get());
      sctp[i] = std::make_unique<sctp::SctpTransport>(std::move(internal), link.end[i].dtls.get());
      sctp[i]->SetDataSink(&sink[i]);
    }
    for (auto& s : sctp) s->Start(link.net.now, sctp::SctpOptions{});
    link.ConnectIce();
  }
  ~SctpOverDtls() {
    for (auto& s : sctp) s.reset();
  }
  template <class Stop>
  bool Run(Stop stop, TimeDelta limit) {
    return RunUntil(
        link, stop,
        [&] {
          Timestamp t = Timestamp::PlusInfinity();
          for (auto& s : sctp) {
            if (s->internal()) t = std::min(t, s->internal()->NextProcessTime());
          }
          return t;
        },
        [&](Timestamp now) {
          for (auto& s : sctp) {
            if (s->internal()) s->internal()->Process(now);
          }
        },
        limit);
  }
  bool Connect() {
    if (!Run([&] { return sink[0].connected && sink[1].connected; }, TimeDelta::Seconds(10))) return false;
    for (auto& s : sctp) {
      if (!s->OpenChannel(kSid, sctp::PriorityValue()).ok()) return false;
    }
    return true;
  }
};

// ---- 9.9: a DataChannelSession (DataChannelController, DCEP) on each DTLS end.
struct ChannelObserver final : pc::DataChannelObserver {
  size_t messages = 0, bytes = 0;
  void OnStateChange() override {}
  void OnMessage(const pc::DataBuffer& b) override {
    ++messages;
    bytes += b.data.size();
  }
};

struct Sessions {
  DtlsLink link;
  std::unique_ptr<pc::DataChannelSession> session[2];
  std::vector<std::shared_ptr<pc::SctpDataChannel>> remote[2];  // Opened by the other end.
  ChannelObserver observer[2];

  Sessions() {
    for (int i = 0; i < 2; ++i) {
      session[i] = std::make_unique<pc::DataChannelSession>(link.net.now + TimeDelta::Micros(i), link.end[i].dtls.get(),
                                                            pc::DataChannelSession::Config{});
      session[i]->SetOnDataChannel([this, i](std::shared_ptr<pc::SctpDataChannel> c) {
        c->RegisterObserver(&observer[i]);
        remote[i].push_back(std::move(c));
      });
    }
    for (auto& s : session) s->Start(link.net.now);
    link.ConnectIce();
  }
  ~Sessions() {
    for (int i = 0; i < 2; ++i) {
      remote[i].clear();
      session[i].reset();
    }
  }
  template <class Stop>
  bool Run(Stop stop, TimeDelta limit) {
    return RunUntil(
        link, stop, [&] { return std::min(session[0]->NextProcessTime(), session[1]->NextProcessTime()); },
        [&](Timestamp now) {
          for (auto& s : session) s->Process(now);
        },
        limit);
  }
  Timestamp now() const { return link.net.now; }
  // An in-band channel from end 0, open at both ends (DATA_CHANNEL_OPEN, then ACK).
  std::shared_ptr<pc::SctpDataChannel> Open(ChannelObserver* obs) {
    auto ch = session[0]->CreateDataChannel(now(), "bench", pc::DataChannelInit{});
    if (!ch) return nullptr;
    if (obs) ch->RegisterObserver(obs);
    const size_t before = remote[1].size();
    if (!Run(
            [&] {
              return ch->state() == pc::DataState::kOpen && remote[1].size() > before &&
                     remote[1].back()->state() == pc::DataState::kOpen;
            },
            TimeDelta::Seconds(10))) {
      return nullptr;
    }
    return ch;
  }
};
#endif  // SM_BENCH_DC_OVER_DTLS

}  // namespace

// ---- 9.6 dcSCTP packet layer ---------------------------------------------------------------------------------------

// Unit: one SCTP packet with a SACK and a 1000-byte DATA chunk built (CRC-32C written), then parsed (CRC-32C checked)
// and its two chunks parsed back.
SM_BENCH(dc, SctpPacket_BuildParse_Sack_Data1000B) {
  const dc::DcSctpOptions options;
  const std::vector<uint8_t> payload(kMessageSize, 0x5A);
  uint32_t tsn = 1000;
  size_t parsed = 0;
  for (auto _ : st) {
    dc::SctpPacket::Builder b(dc::VerificationTag(0x12345678), options);
    b.Add(dc::SackChunk(dc::TSN(tsn - 1), 65536, {}, {}));
    b.Add(dc::DataChunk(dc::TSN(tsn), dc::StreamID(1), dc::SSN(static_cast<uint16_t>(tsn)), dc::PPID(53), payload, {}));
    ++tsn;
    const std::vector<uint8_t> wire = b.Build();
    auto packet = dc::SctpPacket::Parse(wire, options);
    if (packet && packet->descriptors().size() == 2) {
      auto sack = dc::SackChunk::Parse(packet->descriptors()[0].data);
      auto data = dc::DataChunk::Parse(packet->descriptors()[1].data);
      if (sack && data && data->payload().size() == kMessageSize) ++parsed;
    }
  }
  st.Require("parsed_per_packet", st.iterations() ? double(parsed) / double(st.iterations()) : 1, ">=", 1);
  st.BudgetNsPerItem(30000);
}

// ---- 9.7 dcSCTP socket ---------------------------------------------------------------------------------------------

// Unit: one 1000-byte ordered message sent by one socket and received by the other (DATA, and the SACKs and timers
// that come with it), the clock moving 1 ms per message.
SM_BENCH(dc, Socket_Message1000B) {
  SocketPair p;
  if (!p.Connect()) {
    st.Require("connected", 0, ">=", 1);
    return;
  }
  const std::vector<uint8_t> payload(kMessageSize, 0x42);
  for (auto _ : st) {
    p.a.socket->Send(dc::DcSctpMessage(dc::StreamID(1), dc::PPID(53), payload), {});
    p.Pump();
    p.Advance(TimeDelta::Millis(1));
  }
  st.Require("messages_received_per_sent", st.iterations() ? double(p.b.received) / double(st.iterations()) : 1, ">=",
             1);
  st.Require("aborted", p.a.failed || p.b.failed, "<=", 0);
  st.BudgetNsPerItem(30000);
}

// Unit: one association set up (INIT, INIT-ACK, COOKIE-ECHO, COOKIE-ACK) between two new sockets.
SM_BENCH(dc, Socket_Connect) {
  int failures = 0;
  for (auto _ : st) {
    SocketPair p;
    if (!p.Connect()) ++failures;
  }
  st.Require("failures", failures, "<=", 0);
  st.BudgetNsPerItem(30000);
}

#if SM_BENCH_DC_OVER_DTLS
// ---- 9.8 SCTP over DTLS --------------------------------------------------------------------------------------------

// Unit: one 1000-byte binary message from one SctpTransport to the other through SCTP, DTLS and the fake ICE link,
// run event by event until it is delivered.
SM_BENCH(dc, SctpOverDtls_Message1000B) {
  SctpOverDtls f;
  if (!f.Connect()) {
    st.Require("connected", 0, ">=", 1);
    return;
  }
  const std::vector<uint8_t> payload(kMessageSize, 0x42);
  sctp::SendDataParams params;
  params.type = sctp::DataMessageType::kBinary;
  params.ordered = true;
  int failures = 0;
  for (auto _ : st) {
    const size_t want = f.sink[1].messages + 1;
    if (!f.sctp[0]->SendData(f.link.net.now, SctpOverDtls::kSid, params, payload).ok() ||
        !f.Run([&] { return f.sink[1].messages >= want; }, TimeDelta::Seconds(5))) {
      ++failures;
    }
  }
  st.Require("failures", failures, "<=", 0);
  st.BudgetNsPerItem(50000);
}

// ---- 9.9 DCEP and the data channel API -----------------------------------------------------------------------------

// Unit: one in-band data channel opened (DATA_CHANNEL_OPEN and ACK), a 1000-byte message sent on it and received, and
// the channel closed at both ends (stream reset).
SM_BENCH(dc, DataChannel_OpenSendClose) {
  Sessions s;
  if (!s.Run([&] { return s.session[0]->sctp_transport()->IsReadyToSend(); }, TimeDelta::Seconds(10))) {
    st.Require("connected", 0, ">=", 1);
    return;
  }
  const std::vector<uint8_t> payload(kMessageSize, 0x42);
  int failures = 0;
  for (auto _ : st) {
    auto ch = s.Open(nullptr);
    if (!ch) {
      ++failures;
      break;
    }
    const size_t want = s.observer[1].messages + 1;
    auto remote = s.remote[1].back();
    if (!ch->Send(s.now(), pc::DataBuffer(payload, true)) ||
        !s.Run([&] { return s.observer[1].messages >= want; }, TimeDelta::Seconds(5))) {
      ++failures;
    }
    ch->Close(s.now());
    if (!s.Run([&] { return ch->state() == pc::DataState::kClosed && remote->state() == pc::DataState::kClosed; },
               TimeDelta::Seconds(5))) {
      ++failures;
    }
    s.remote[1].clear();
  }
  st.Require("failures", failures, "<=", 0);
  st.BudgetNsPerItem(120000);
}

// Unit: one 1000-byte binary message on an open in-band channel, from SctpDataChannel::Send to the far end's
// observer.
SM_BENCH(dc, DataChannel_Message1000B) {
  Sessions s;
  if (!s.Run([&] { return s.session[0]->sctp_transport()->IsReadyToSend(); }, TimeDelta::Seconds(10))) {
    st.Require("connected", 0, ">=", 1);
    return;
  }
  auto ch = s.Open(nullptr);
  if (!ch) {
    st.Require("opened", 0, ">=", 1);
    return;
  }
  const std::vector<uint8_t> payload(kMessageSize, 0x42);
  int failures = 0;
  for (auto _ : st) {
    const size_t want = s.observer[1].messages + 1;
    if (!ch->Send(s.now(), pc::DataBuffer(payload, true)) ||
        !s.Run([&] { return s.observer[1].messages >= want; }, TimeDelta::Seconds(5))) {
      ++failures;
    }
  }
  st.Require("failures", failures, "<=", 0);
  st.BudgetNsPerItem(50000);
}
#endif  // SM_BENCH_DC_OVER_DTLS
