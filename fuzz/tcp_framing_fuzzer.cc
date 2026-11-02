// TCP framing (#1058, C10 part 3): the packet sockets that cut a TCP byte
// stream into packets, on arbitrary streams. libwebrtc M157 has no fuzzer
// for them (compare/webrtc/reference/test/fuzzers/), so this one feeds the
// functions themselves:
//
// - rtc_base/async_tcp_socket.cc: AsyncTcpSocket (a 2-byte big-endian length
//   before each packet) and AsyncTcpSocketBase::OnReadEvent, whose input
//   buffer grows from 128 bytes to the largest packet;
// - p2p/base/async_stun_tcp_socket.cc: AsyncStunTcpSocket, framed by the STUN
//   header's length (20 + length) or TURN ChannelData's (4 + length, padded to
//   a multiple of 4), and GetExpectedLength;
// - the TURN server behind it (sim::TurnServer::AddInternalServerSocket), as
//   TURN over TCP reaches it.
//
// The first byte's low 2 bits pick the receiver: 0 AsyncTcpSocket, 1
// AsyncStunTcpSocket, 2 a TURN server listening on TCP (AsyncStunTcpSocket in
// front of TurnServer's request handling), 3 AsyncStunTcpSocket again. The
// rest is the stream, as records of a big-endian 16-bit length and that many
// bytes (fuzz_data_reader.h); each record is written to an emulated TCP
// connection in one Send and delivered, so the receiver sees the stream in
// the record's pieces, as TCP may cut it.
//
// Checks (abort if wrong): ProcessInput on the whole stream returns no more
// than it was given, and the packets it hands up tile the bytes it used, each
// with the length its header announces (and the ChannelData padding skipped);
// the packets the socket hands up from the connection are a prefix of those,
// and all of them once the whole stream arrived.
//
// With SM_FUZZ_WRITE_SEEDS=<dir> in the environment, the first run writes the
// seed corpus (corpus/tcp_framing/) to <dir>: libwebrtc's
// async_stun_tcp_socket_unittest.cc and async_tcp_socket_unittest.cc
// messages, alone, back to back and cut in pieces, and a TURN Allocate.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "fuzz_data_reader.h"
#include "sm/core/units.h"
#include "sm/p2p/async_packet_socket.h"
#include "sm/p2p/async_tcp_socket.h"
#include "sm/p2p/byte_buffer.h"
#include "sm/p2p/socket.h"
#include "sm/p2p/stun.h"
#include "sm/sim/emulated_sockets.h"
#include "sm/sim/turn_server.h"
#include "test_crypto.h"

namespace {

using namespace sm;
using Bytes = std::vector<uint8_t>;
using p2p::AsyncPacketSocket;
using p2p::ReceivedPacket;

constexpr uint32_t kClientIp = sim::SocketAddress::Ip(11, 11, 11, 11);
constexpr uint32_t kServerIp = sim::SocketAddress::Ip(22, 22, 22, 22);
constexpr uint32_t kRelayIp = sim::SocketAddress::Ip(33, 33, 33, 33);

void Check(bool ok) {
  if (!ok) std::abort();
}

sim::LinkConfig Link(int delay_ms) {
  sim::LinkConfig c;
  c.base_delay = TimeDelta::Millis(delay_ms);
  return c;
}

uint16_t Be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

class TestAuth final : public sim::TurnAuthInterface {
 public:
  bool GetKey(std::string_view username, std::string_view realm, std::string* key) override {
    return p2p::ComputeStunCredentialHash(std::string(username), std::string(realm), std::string(username), key);
  }
};

class StunListenSocket final : public p2p::AsyncTcpListenSocket {
 public:
  using AsyncTcpListenSocket::AsyncTcpListenSocket;

 protected:
  std::unique_ptr<AsyncPacketSocket> WrapIncomingConnection(std::unique_ptr<p2p::Socket> socket) override {
    return std::make_unique<p2p::AsyncStunTcpSocket>(std::move(socket));
  }
};

class TcpListenSocket final : public p2p::AsyncTcpListenSocket {
 public:
  using AsyncTcpListenSocket::AsyncTcpListenSocket;
};

// ProcessInput on the whole stream; returns the packets and checks the tiling.
std::vector<Bytes> ProcessAll(p2p::AsyncTcpSocketBase& socket, bool stun, std::span<const uint8_t> stream) {
  std::vector<Bytes> packets;
  std::vector<size_t> offsets;
  socket.RegisterReceivedPacketCallback([&](AsyncPacketSocket*, const ReceivedPacket& p) {
    Check(p.payload.data() >= stream.data() && p.payload.data() + p.payload.size() <= stream.data() + stream.size());
    offsets.push_back(static_cast<size_t>(p.payload.data() - stream.data()));
    packets.emplace_back(p.payload.begin(), p.payload.end());
  });
  const size_t used = socket.ProcessInput(Timestamp::Seconds(1), stream);
  socket.DeregisterReceivedPacketCallback();
  Check(used <= stream.size());
  size_t pos = 0;
  for (size_t i = 0; i < packets.size(); ++i) {
    if (stun) {
      Check(offsets[i] == pos);
      int pad = 0;
      const size_t expected = p2p::AsyncStunTcpSocket::GetExpectedLength(stream.subspan(pos), &pad);
      Check(packets[i].size() == expected && pad >= 0 && pad < 4);
      pos += expected + static_cast<size_t>(pad);
    } else {
      Check(offsets[i] == pos + 2 && packets[i].size() == Be16(stream.data() + pos));
      pos += 2 + packets[i].size();
    }
  }
  Check(pos == used);
  return packets;
}

struct Session {
  sim::ByteNetwork net;
  sim::EmulatedSocketServer client_host{net, net.AddHost(kClientIp, 0, Link(5), Link(5))};
  sim::EmulatedSocketServer server_host{net, net.AddHost(kServerIp, 0, Link(5), Link(5))};
  sim::EmulatedSocketServer relay_host{net, net.AddHost(kRelayIp, 0, Link(5), Link(5))};
  TestAuth auth;
  std::unique_ptr<sim::TurnServer> turn;
  std::unique_ptr<p2p::AsyncListenSocket> listen_socket;
  std::unique_ptr<AsyncPacketSocket> recv_socket;
  std::unique_ptr<p2p::Socket> client;
  std::vector<Bytes> received;
  Timestamp now = Timestamp::Seconds(1);

  explicit Session(int mode) {
    auto server = server_host.CreateStreamSocket(p2p::IpFamily::kV4);
    const p2p::SocketAddress addr(server_host.ip(), 3478);
    Check(server->Bind(addr) == 0);
    p2p::SocketAddress connect_to = addr;
    if (mode == 2) {
      turn = std::make_unique<sim::TurnServer>();
      turn->set_realm("example.org");
      turn->set_auth_hook(&auth);
      turn->SetExternalSocketFactory(&relay_host, p2p::SocketAddress(relay_host.ip(), 0));
      Check(server->Listen(5) == 0);
      turn->AddInternalServerSocket(std::move(server), p2p::PROTO_TCP);
    } else {
      if (mode == 0) {
        listen_socket = std::make_unique<TcpListenSocket>(std::move(server));
      } else {
        listen_socket = std::make_unique<StunListenSocket>(std::move(server));
      }
      listen_socket->SubscribeNewConnection(this, [this](p2p::AsyncListenSocket*, std::unique_ptr<AsyncPacketSocket>& s) {
        recv_socket = std::move(s);
        recv_socket->RegisterReceivedPacketCallback([this](AsyncPacketSocket*, const ReceivedPacket& p) {
          received.emplace_back(p.payload.begin(), p.payload.end());
        });
      });
    }
    client = client_host.CreateStreamSocket(p2p::IpFamily::kV4);
    Check(client->Bind(p2p::SocketAddress(client_host.ip(), 0)) == 0);
    Check(client->Connect(now, connect_to) == 0);
    Run();
  }
  ~Session() {
    if (listen_socket) listen_socket->UnsubscribeNewConnection(this);
  }

  void Run() {
    const Timestamp until = now + TimeDelta::Seconds(5);
    for (;;) {
      Timestamp t = std::min({client_host.NextEvent(), server_host.NextEvent(), relay_host.NextEvent()});
      if (turn) t = std::min(t, turn->NextProcessTime());
      if (!t.IsFinite() || t > until) return;
      now = std::max(now, t);
      if (turn) turn->Process(now);
      for (auto* h : {&client_host, &server_host, &relay_host}) h->Deliver(now);
    }
  }
  // Writes `chunk` to the connection, waiting while the send buffer is full.
  bool Write(std::span<const uint8_t> chunk) {
    while (!chunk.empty()) {
      const int n = client->Send(now, chunk);
      if (n > 0) chunk = chunk.subspan(static_cast<size_t>(n));
      const Timestamp before = now;
      Run();
      if (n <= 0 && now == before) return false;  // No progress.
    }
    return true;
  }
};

void WriteFile(const std::string& path, const Bytes& data) {
  if (FILE* f = std::fopen(path.c_str(), "wb")) {
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
  }
}

void AppendRecord(Bytes& out, std::span<const uint8_t> bytes) {
  out.push_back(static_cast<uint8_t>(bytes.size() >> 8));
  out.push_back(static_cast<uint8_t>(bytes.size()));
  out.insert(out.end(), bytes.begin(), bytes.end());
}

void MaybeWriteSeeds() {
  static bool done = false;
  if (done) return;
  done = true;
  const char* dir = std::getenv("SM_FUZZ_WRITE_SEEDS");
  if (!dir) return;
  const Bytes id = {0x21, 0x12, 0xA4, 0x42, '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b'};
  auto stun = [&](uint16_t len) {
    Bytes b = {0x00, 0x01, static_cast<uint8_t>(len >> 8), static_cast<uint8_t>(len)};
    b.insert(b.end(), id.begin(), id.end());
    b.resize(20 + len, 0x5a);
    return b;
  };
  auto channel = [](uint16_t len, bool padded) {
    Bytes b = {0x40, 0x00, static_cast<uint8_t>(len >> 8), static_cast<uint8_t>(len)};
    b.resize(4 + len, 0x33);
    if (padded) b.resize((b.size() + 3) / 4 * 4, 0);
    return b;
  };
  // A TURN Allocate request (no credentials: the server answers 401).
  p2p::TurnMessage allocate(p2p::STUN_ALLOCATE_REQUEST, std::string("0123456789ab"));
  auto transport = p2p::StunAttribute::CreateUInt32(p2p::STUN_ATTR_REQUESTED_TRANSPORT);
  transport->SetValue(17u << 24);  // UDP.
  allocate.AddAttribute(std::move(transport));
  p2p::ByteBufferWriter w;
  Check(allocate.Write(&w));
  const Bytes alloc(w.DataView().begin(), w.DataView().end());

  struct Seed {
    const char* name;
    uint8_t mode;
    std::vector<Bytes> pieces;
  };
  auto concat = [](std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const Bytes& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
  };
  const Bytes two_stun = concat({stun(0), stun(16)});
  const Bytes mixed = concat({channel(0, true), channel(5, true), stun(4), channel(16, true)});
  // AsyncTcpSocket: length-prefixed packets.
  const Bytes tcp_two = {0x00, 0x03, 'a', 'b', 'c', 0x00, 0x00, 0x00, 0x05, 'h', 'e', 'l', 'l', 'o'};
  const std::vector<Seed> seeds = {
      {"tcp-two", 0, {tcp_two}},
      {"tcp-split", 0, {Bytes(tcp_two.begin(), tcp_two.begin() + 1), Bytes(tcp_two.begin() + 1, tcp_two.end())}},
      {"tcp-partial", 0, {Bytes{0x01, 0x00, 'x', 'y'}}},
      {"stun-zero-length", 1, {stun(0)}},
      {"stun-two", 1, {two_stun}},
      {"stun-split", 1, {Bytes(two_stun.begin(), two_stun.begin() + 7), Bytes(two_stun.begin() + 7, two_stun.end())}},
      {"stun-invalid-length", 1, {Bytes(stun(16).begin(), stun(16).begin() + 20)}},
      {"channel-zero-length", 1, {channel(0, false)}},
      {"channel-odd-padded", 1, {channel(5, true)}},
      {"channel-odd-unpadded", 1, {channel(5, false)}},
      {"mixed", 3, {mixed}},
      {"turn-allocate", 2, {alloc}},
      {"turn-allocate-twice", 2, {concat({alloc, channel(3, true)}), alloc}},
  };
  for (const Seed& s : seeds) {
    Bytes file = {s.mode};
    for (const Bytes& p : s.pieces) AppendRecord(file, p);
    WriteFile(std::string(dir) + "/" + s.name, file);
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static const bool crypto = [] {
    sm::p2p::testing::InstallTestCryptoWithUniqueIds();
    return true;
  }();
  (void)crypto;
  MaybeWriteSeeds();

  sm::fuzz::FuzzDataReader in(std::span<const uint8_t>(data, size));
  const int mode = in.ReadU8() & 3;
  std::vector<std::span<const uint8_t>> pieces;
  Bytes stream;
  while (in.BytesLeft() > 0 && pieces.size() < 64) {
    pieces.push_back(in.ReadLengthPrefixed());
    stream.insert(stream.end(), pieces.back().begin(), pieces.back().end());
  }

  // Every framing on the whole stream, whatever the mode.
  sim::ByteNetwork net;
  sim::EmulatedSocketServer host(net, net.AddHost(kClientIp, 0, Link(5), Link(5)));
  p2p::AsyncTcpSocket tcp(host.CreateStreamSocket(p2p::IpFamily::kV4));
  p2p::AsyncStunTcpSocket stun(host.CreateStreamSocket(p2p::IpFamily::kV4));
  const std::vector<Bytes> tcp_packets = ProcessAll(tcp, false, stream);
  const std::vector<Bytes> stun_packets = ProcessAll(stun, true, stream);
  if (stream.size() >= 4) {
    int pad = 0;
    (void)p2p::AsyncStunTcpSocket::GetExpectedLength(stream, &pad);
  }

  // The stream over a connection, in its pieces.
  Session s(mode);
  bool all = true;
  for (auto piece : pieces) {
    if (!s.Write(piece)) {
      all = false;
      break;
    }
  }
  s.Run();
  if (mode == 2) return 0;
  const std::vector<Bytes>& want = mode == 0 ? tcp_packets : stun_packets;
  Check(s.received.size() <= want.size());
  for (size_t i = 0; i < s.received.size(); ++i) Check(s.received[i] == want[i]);
  if (all) Check(s.received.size() == want.size());
  return 0;
}
