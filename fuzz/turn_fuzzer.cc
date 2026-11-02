// TURN (#1058, C10 part 3): the client's and the server's receive paths on
// arbitrary datagrams, after libwebrtc's test/fuzzers/turn_unwrap_fuzzer.cc
// (compare/webrtc/reference/test/fuzzers/).
//
// libwebrtc's target feeds media/base/turn_utils.cc's UnwrapTurnPacket, which
// strips a ChannelData header or a Send indication from a packet the
// application sees on a TURN connection. This library has no such function
// (turn_utils.cc is not in the reference tree, and nothing here sends media
// that way); the code that unwraps TURN framing here is TurnPort's receive
// path and sim::TurnServer's, so this target feeds those, in a live
// allocation:
//
// - Setup (every input): the TURN port of libwebrtc's TurnPortTest
//   (p2p/base/turn_port_unittest.cc, tests/unit/p2p/turn_fixture.h) on the
//   emulated network: a TurnPort on a shared UDP socket allocates on
//   sim::TurnServer, a UDPPort peer pings it both ways, and two payloads go
//   from the TURN side, so a permission and channel 0x4000 are bound and the
//   peer's data arrives as ChannelData.
// - Then the input is a list of records, each a control byte and a
//   big-endian 16-bit length with that many bytes (fuzz_data_reader.h).
//   The control byte's low 2 bits say where the bytes go:
//   0: to TurnPort::HandleIncomingPacket from the server's address: ChannelData
//      (HandleChannelData, framing and padding), Data indications
//      (HandleDataIndication) and responses (StunRequestManager::CheckResponse);
//   1: to the server from the allocation's 5-tuple (the port's socket):
//      Allocate, Refresh, CreatePermission and ChannelBind requests, Send
//      indications and ChannelData (TurnServer::OnInternalPacket, Allocation);
//   2: to the server from an address without an allocation;
//   3: to the server's relayed address from the peer's host, which the server
//      relays to the port as ChannelData or a Data indication.
//   The other 6 bits advance the emulated time by that many 10 ms steps after
//   the record, so responses, retransmissions and timers run in between.
//
// With SM_FUZZ_WRITE_SEEDS=<dir> in the environment, the first run writes the
// seed corpus (corpus/turn/) to <dir>: every packet the server sent the port
// during the setup and every TURN message the port sent the server, each as a
// record to the right side, plus ChannelData and peer data records and the
// whole exchange as one session.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "fuzz_data_reader.h"
#include "sm/core/units.h"
#include "sm/p2p/byte_buffer.h"
#include "sm/p2p/connection.h"
#include "sm/p2p/network.h"
#include "sm/p2p/port.h"
#include "sm/p2p/stun.h"
#include "sm/p2p/stun_port.h"
#include "sm/p2p/turn_port.h"
#include "sm/sim/emulated_sockets.h"
#include "sm/sim/turn_server.h"
#include "test_crypto.h"

namespace {

using namespace sm;
using namespace sm::p2p;

constexpr uint32_t kLocalIp1 = sim::SocketAddress::Ip(11, 11, 11, 11);
constexpr uint32_t kLocalIp2 = sim::SocketAddress::Ip(22, 22, 22, 22);
constexpr uint32_t kTurnIntIp = sim::SocketAddress::Ip(99, 99, 99, 3);
constexpr uint32_t kTurnExtIp = sim::SocketAddress::Ip(99, 99, 99, 5);
const char kUser[] = "test";
const char kRealm[] = "example.org";

void Check(bool ok) {
  if (!ok) std::abort();
}

sim::LinkConfig Link(int delay_ms) {
  sim::LinkConfig c;
  c.base_delay = TimeDelta::Millis(delay_ms);
  return c;
}

// test_turn_server.h's auth: the password is the username.
class TestAuth final : public sim::TurnAuthInterface {
 public:
  bool GetKey(std::string_view username, std::string_view realm, std::string* key) override {
    return ComputeStunCredentialHash(std::string(username), std::string(realm), std::string(username), key);
  }
};

using Bytes = std::vector<uint8_t>;

struct Seeds {
  std::vector<std::pair<int, Bytes>> records;  // (kind, bytes)
};

struct Session {
  sim::ByteNetwork net;
  sim::EmulatedSocketServer local1;
  sim::EmulatedSocketServer local2;
  sim::EmulatedSocketServer turn_int;
  sim::EmulatedSocketServer turn_ext;
  TestAuth auth;
  sim::TurnServer server;
  Network network1;
  Network network2;
  Timestamp now = Timestamp::Seconds(10);
  SocketAddress server_addr{kTurnIntIp, 3478};
  ProtocolAddress server_paddr{server_addr, PROTO_UDP};
  RelayServerConfig config{server_addr, kUser, kUser, PROTO_UDP};
  std::unique_ptr<AsyncPacketSocket> shared_socket;    // The TURN port's.
  std::unique_ptr<AsyncPacketSocket> stranger_socket;  // On the peer's host, no allocation.
  std::unique_ptr<UDPPort> udp_port;
  std::unique_ptr<TurnPort> turn_port;  // Destroyed first.
  Seeds* seeds = nullptr;

  Session()
      : local1(net, net.AddHost(kLocalIp1, sim::ByteNetwork::kPublicRealm, Link(5), Link(5))),
        local2(net, net.AddHost(kLocalIp2, sim::ByteNetwork::kPublicRealm, Link(5), Link(5))),
        turn_int(net, net.AddHost(kTurnIntIp, sim::ByteNetwork::kPublicRealm, Link(15), Link(15))),
        turn_ext(net, net.AddHost(kTurnExtIp, sim::ByteNetwork::kPublicRealm, Link(15), Link(15))),
        network1("eth0", IpAddress(kLocalIp1), 24),
        network2("eth1", IpAddress(kLocalIp2), 24) {
    network1.AddIp(IpAddress(kLocalIp1));
    network2.AddIp(IpAddress(kLocalIp2));
    server.set_realm(kRealm);
    server.set_auth_hook(&auth);
    server.AddInternalSocket(turn_int.CreateUdpSocket(server_addr, 0, 0), PROTO_UDP);
    server.SetExternalSocketFactory(&turn_ext, SocketAddress(kTurnExtIp, 0));
  }
  ~Session() {
    turn_port.reset();
    udp_port.reset();
  }

  Timestamp Next() const {
    Timestamp t = std::min(server.NextProcessTime(), net.NextEvent());
    if (turn_port) t = std::min(t, turn_port->NextProcessTime());
    if (udp_port) t = std::min(t, udp_port->NextProcessTime());
    return t;
  }
  void Step(Timestamp t) {
    now = std::max(now, t);
    if (turn_port) turn_port->Process(now);
    if (udp_port) udp_port->Process(now);
    server.Process(now);
    for (sim::EmulatedSocketServer* h : {&local1, &local2, &turn_int, &turn_ext}) h->Deliver(now);
  }
  void Advance(TimeDelta d) {
    const Timestamp until = now + d;
    for (;;) {
      const Timestamp t = Next();
      if (t > until) break;
      Step(t);
    }
    Step(until);
  }

  void Setup() {
    shared_socket = local1.CreateUdpSocket(SocketAddress(kLocalIp1, 0), 0, 0);
    Check(shared_socket != nullptr);
    shared_socket->RegisterReceivedPacketCallback([this](AsyncPacketSocket* s, const ReceivedPacket& p) {
      if (seeds && p.source_address == server_addr) seeds->records.emplace_back(0, Bytes(p.payload.begin(), p.payload.end()));
      if (turn_port) turn_port->HandleIncomingPacket(s, p);
    });
    stranger_socket = local2.CreateUdpSocket(SocketAddress(kLocalIp2, 0), 0, 0);
    Check(stranger_socket != nullptr);
    if (seeds) {
      server.SetStunMessageObserver([this](const TurnMessage& msg) {
        ByteBufferWriter out;
        if (msg.Write(&out)) seeds->records.emplace_back(1, Bytes(out.DataView().begin(), out.DataView().end()));
      });
    }

    CreateRelayPortArgs args;
    args.port.network = &network1;
    args.port.socket_factory = &local1;
    args.port.ice_tiebreaker = 1;
    args.server_address = &server_paddr;
    args.config = &config;
    args.username = "lfrag";
    args.password = "lpasswordlpasswordlpassw";
    turn_port = TurnPort::Create(now, args, shared_socket.get());
    Check(turn_port != nullptr);
    turn_port->SetIceRole(ICEROLE_CONTROLLING);
    turn_port->PrepareAddress(now);
    Advance(TimeDelta::Millis(200));
    Check(turn_port->Candidates().size() == 1);

    Port::Parameters udp;
    udp.network = &network2;
    udp.socket_factory = &local2;
    udp.ice_username_fragment = "rfrag";
    udp.ice_password = "rpasswordrpasswordrpassw";
    udp.ice_tiebreaker = 2;
    udp_port = UDPPort::Create(now, udp, 0, 0, false, std::nullopt);
    Check(udp_port != nullptr);
    udp_port->SetIceRole(ICEROLE_CONTROLLED);
    udp_port->PrepareAddress(now);
    Advance(TimeDelta::Millis(100));
    Check(udp_port->Candidates().size() == 1);

    Connection* conn1 = turn_port->CreateConnection(now, udp_port->Candidates()[0], Port::ORIGIN_MESSAGE);
    Connection* conn2 = udp_port->CreateConnection(now, turn_port->Candidates()[0], Port::ORIGIN_MESSAGE);
    Check(conn1 && conn2);
    Advance(TimeDelta::Millis(100));
    conn1->Ping(now);
    Advance(TimeDelta::Millis(100));
    conn2->Ping(now);
    Advance(TimeDelta::Millis(100));
    // The first payload goes in a Send indication and binds a channel; the
    // second goes as ChannelData. The peer answers, so ChannelData reaches
    // the port too.
    const Bytes payload = {0x80, 0x00, 0x00, 0x01, 1, 2, 3, 4, 5};
    for (int i = 0; i < 2; ++i) {
      conn1->Send(now, payload, {});
      Advance(TimeDelta::Millis(100));
      conn2->Send(now, payload, {});
      Advance(TimeDelta::Millis(100));
    }
  }

  void Deliver(int kind, std::span<const uint8_t> bytes) {
    switch (kind) {
      case 0:
        turn_port->HandleIncomingPacket(shared_socket.get(),
                                        ReceivedPacket{bytes, server_addr, now, ReceivedPacket::kNotDecrypted});
        break;
      case 1:
        shared_socket->SendTo(now, bytes, server_addr, {});
        break;
      case 2:
        stranger_socket->SendTo(now, bytes, server_addr, {});
        break;
      default:
        stranger_socket->SendTo(now, bytes, turn_port->Candidates()[0].address(), {});
        break;
    }
  }
};

void WriteFile(const std::string& path, const Bytes& data) {
  if (FILE* f = std::fopen(path.c_str(), "wb")) {
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
  }
}

void AppendRecord(Bytes& out, int control, std::span<const uint8_t> bytes) {
  out.push_back(static_cast<uint8_t>(control));
  out.push_back(static_cast<uint8_t>(bytes.size() >> 8));
  out.push_back(static_cast<uint8_t>(bytes.size()));
  out.insert(out.end(), bytes.begin(), bytes.end());
}

// SM_FUZZ_WRITE_SEEDS: the seed corpus, from one setup.
void MaybeWriteSeeds() {
  static bool done = false;
  if (done) return;
  done = true;
  const char* dir = std::getenv("SM_FUZZ_WRITE_SEEDS");
  if (!dir) return;
  Seeds seeds;
  {
    Session s;
    s.seeds = &seeds;
    s.Setup();
  }
  // ChannelData on channel 0x4000 with 1..4 bytes of data, unpadded and
  // padded to 4 as over TCP, and on an unbound channel; a peer datagram.
  for (uint8_t n = 1; n <= 4; ++n) {
    Bytes cd = {0x40, 0x00, 0x00, n};
    for (uint8_t i = 0; i < n; ++i) cd.push_back(static_cast<uint8_t>(0xa0 + i));
    seeds.records.emplace_back(0, cd);
    seeds.records.emplace_back(1, cd);
    while (cd.size() % 4) cd.push_back(0);
    seeds.records.emplace_back(0, cd);
  }
  seeds.records.emplace_back(0, Bytes{0x7f, 0xff, 0x00, 0x02, 0xde, 0xad});
  seeds.records.emplace_back(3, Bytes{0x80, 0x00, 0x00, 0x02, 9, 8, 7});

  const std::string base(dir);
  Bytes session;
  int n = 0;
  for (const auto& [kind, bytes] : seeds.records) {
    Bytes one;
    AppendRecord(one, kind | (10 << 2), bytes);  // 100 ms after each.
    char name[32];
    std::snprintf(name, sizeof(name), "/seed-%03d-%d", n++, kind);
    WriteFile(base + name, one);
    // The same message from a new address too.
    if (kind == 1) {
      Bytes other;
      AppendRecord(other, 2 | (10 << 2), bytes);
      std::snprintf(name, sizeof(name), "/seed-%03d-2", n++);
      WriteFile(base + name, other);
    }
    AppendRecord(session, kind | (2 << 2), bytes);
  }
  WriteFile(base + "/session", session);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static const bool crypto = [] {
    sm::p2p::testing::InstallTestCryptoWithUniqueIds();
    return true;
  }();
  (void)crypto;
  MaybeWriteSeeds();

  Session s;
  s.Setup();
  sm::fuzz::FuzzDataReader in(std::span<const uint8_t>(data, size));
  // At most 64 records, so one input stays fast.
  for (int i = 0; i < 64 && in.BytesLeft() > 0; ++i) {
    const uint8_t control = in.ReadU8();
    const auto bytes = in.ReadLengthPrefixed();
    s.Deliver(control & 3, bytes);
    s.Advance(TimeDelta::Millis(10 * (control >> 2)));
  }
  s.Advance(TimeDelta::Millis(200));
  return 0;
}
