// interop.Coturn (issue #1037, C4 part 5, milestone 3): the TURN client
// (TurnPort, sm/p2p/turn_port.h) against a real coturn over OS sockets.
//
// Each case starts a local coturn (`turnserver`, 4.x) on 127.0.0.1 with a
// self-signed certificate, makes two TurnPorts against it and checks, for
// UDP, TCP and TLS to the server and for long-term and `use-auth-secret`
// (TURN REST) credentials:
// - allocate: a relay candidate on the server's relay address;
// - permission: CreatePermission succeeds for the other allocation;
// - channel bind: ChannelBind is answered with success, and data then goes
//   as ChannelData both ways;
// - data relayed both ways between the two allocations, byte for byte;
// - refresh: the server is run with a short allocation lifetime, so the port
//   refreshes within the test, and the server accepts it;
// - deallocate: Release sends Refresh with LIFETIME 0 and the port closes on
//   the server's answer.
//
// The case is skipped (ctest SKIP_REGULAR_EXPRESSION "coturn not
// installed") when `turnserver` or `openssl` isn't on PATH. CI runs it once
// coturn is in the workflow (`.github/` is the Owner's; #944).
//
// To check a real server (for example the Lightsail coturn of
// docs/deploy/turn-server.md) instead of a local one, set:
//   SM_COTURN_SERVER=<IPv4>      the server (no local coturn is started)
//   SM_COTURN_PORT=3478          UDP and TCP port (default 3478)
//   SM_COTURN_TLS_PORT=5349      TLS port (default 5349)
//   SM_COTURN_USER, SM_COTURN_PASSWORD   long-term credentials, or
//   SM_COTURN_SECRET             the static-auth-secret (REST credentials)
//   SM_COTURN_LOCAL_IP=<IPv4>    the local address to bind (default: the
//                                one the host routes to the server from)
// Cases whose credentials aren't given are skipped. The refresh check is
// skipped against a real server, whose allocation lifetime is its own.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fcntl.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "os_sockets.h"
#include "sm/p2p/candidate.h"
#include "sm/p2p/connection.h"
#include "sm/p2p/network.h"
#include "sm/p2p/port.h"
#include "sm/p2p/ssl_adapter_tls.h"
#include "sm/p2p/stun.h"
#include "sm/p2p/stun_crypto_tls.h"
#include "sm/p2p/turn_port.h"
#include "sm_test.h"

extern char** environ;

using namespace sm;
using namespace sm::p2p;

namespace {

const char kRealm[] = "example.org";
const char kUser[] = "test";
const char kPassword[] = "test";
const char kSecret[] = "sm-interop-secret";
// coturn's allocation lifetime cap for the local server: the port refreshes
// at half a lifetime under 2 minutes (TurnPort::ScheduleRefresh).
constexpr int kLocalLifetimeSeconds = 6;
constexpr TimeDelta kStepTimeout = TimeDelta::Seconds(5);

enum class Creds { kLongTerm, kRest };

Timestamp Now() {
  const auto d = std::chrono::steady_clock::now().time_since_epoch();
  // Offset so that the first time is well after Timestamp zero, as the
  // library's tests start at 10 s.
  return Timestamp::Micros(std::chrono::duration_cast<std::chrono::microseconds>(d).count() + 10'000'000);
}

std::optional<std::string> FindOnPath(const std::string& name) {
  const char* path = std::getenv("PATH");
  std::string p = path ? path : "/usr/bin:/bin";
  p += ":/usr/local/bin:/opt/homebrew/bin:/usr/sbin";
  size_t start = 0;
  while (start <= p.size()) {
    size_t end = p.find(':', start);
    if (end == std::string::npos) end = p.size();
    std::string candidate = p.substr(start, end - start) + "/" + name;
    if (access(candidate.c_str(), X_OK) == 0) return candidate;
    start = end + 1;
  }
  return std::nullopt;
}

int RunProcess(const std::vector<std::string>& args, const std::string& out_file, pid_t* pid_out) {
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, 1, out_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  posix_spawn_file_actions_adddup2(&actions, 1, 2);
  std::vector<char*> argv;
  for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
  argv.push_back(nullptr);
  pid_t pid = 0;
  const int rc = posix_spawn(&pid, argv[0], &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  if (rc != 0) return -1;
  if (pid_out) {
    *pid_out = pid;
    return 0;
  }
  int status = 0;
  waitpid(pid, &status, 0);
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// A free port on 127.0.0.1 for both TCP and UDP.
uint16_t FreePort() {
  for (int attempt = 0; attempt < 50; ++attempt) {
    int t = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof(sin);
    bind(t, reinterpret_cast<sockaddr*>(&sin), sizeof(sin));
    getsockname(t, reinterpret_cast<sockaddr*>(&sin), &len);
    int u = socket(AF_INET, SOCK_DGRAM, 0);
    const bool udp_free = bind(u, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) == 0;
    close(t);
    close(u);
    if (udp_free) return ntohs(sin.sin_port);
  }
  return 0;
}

bool TcpReachable(uint32_t ip, uint16_t port) {
  int t = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in sin;
  interop::ToSockaddr(SocketAddress(ip, port), &sin);
  const bool ok = connect(t, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) == 0;
  close(t);
  return ok;
}

// The local address the host uses to reach `ip`.
uint32_t RouteTo(uint32_t ip) {
  int u = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in sin;
  interop::ToSockaddr(SocketAddress(ip, 3478), &sin);
  uint32_t local = 0;
  if (connect(u, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) == 0) {
    socklen_t len = sizeof(sin);
    getsockname(u, reinterpret_cast<sockaddr*>(&sin), &len);
    local = ntohl(sin.sin_addr.s_addr);
  }
  close(u);
  return local;
}

std::optional<uint32_t> EnvIp(const char* name) {
  const char* v = std::getenv(name);
  if (!v || !*v) return std::nullopt;
  in_addr a{};
  if (inet_pton(AF_INET, v, &a) != 1) return std::nullopt;
  return ntohl(a.s_addr);
}

std::string Env(const char* name) {
  const char* v = std::getenv(name);
  return v ? v : "";
}

// The TURN server under test: a local coturn this class starts and stops,
// or the real one SM_COTURN_SERVER names.
class Server {
 public:
  explicit Server(Creds creds) : creds_(creds) {
    if (auto remote = EnvIp("SM_COTURN_SERVER")) {
      StartRemote(*remote);
    } else {
      StartLocal();
    }
  }
  ~Server() {
    if (pid_ > 0) {
      kill(pid_, SIGTERM);
      int status = 0;
      waitpid(pid_, &status, 0);
    }
    if (!dir_.empty()) {
      std::string cmd = "rm -rf '" + dir_ + "'";
      if (std::system(cmd.c_str()) != 0) std::fprintf(stderr, "could not remove %s\n", dir_.c_str());
    }
  }

  // Empty when the server is up; otherwise why the case is skipped.
  const std::string& skip_reason() const { return skip_; }
  bool local() const { return pid_ > 0; }
  uint32_t server_ip() const { return server_ip_; }
  uint32_t local_ip() const { return local_ip_; }
  uint16_t port(ProtocolType proto) const { return proto == PROTO_TLS ? tls_port_ : port_; }
  RelayCredentials credentials() const {
    if (creds_ == Creds::kRest) {
      // Valid for an hour from now (coturn compares with its wall clock).
      return CreateTurnRestCredentials(secret_, "sm-interop", static_cast<int64_t>(std::time(nullptr)) + 3600);
    }
    return RelayCredentials(user_, password_);
  }
  std::string log() const {
    std::string out;
    if (FILE* f = std::fopen((dir_ + "/turnserver.log").c_str(), "r")) {
      char buf[4096];
      size_t n;
      while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
      std::fclose(f);
    }
    return out;
  }

 private:
  void StartRemote(uint32_t ip) {
    server_ip_ = ip;
    local_ip_ = EnvIp("SM_COTURN_LOCAL_IP").value_or(RouteTo(ip));
    port_ = static_cast<uint16_t>(std::atoi(Env("SM_COTURN_PORT").empty() ? "3478" : Env("SM_COTURN_PORT").c_str()));
    tls_port_ = static_cast<uint16_t>(
        std::atoi(Env("SM_COTURN_TLS_PORT").empty() ? "5349" : Env("SM_COTURN_TLS_PORT").c_str()));
    if (creds_ == Creds::kRest) {
      secret_ = Env("SM_COTURN_SECRET");
      if (secret_.empty()) skip_ = "SM_COTURN_SECRET not set";
    } else {
      user_ = Env("SM_COTURN_USER");
      password_ = Env("SM_COTURN_PASSWORD");
      if (user_.empty()) skip_ = "SM_COTURN_USER not set";
    }
  }

  void StartLocal() {
    const auto turnserver = FindOnPath("turnserver");
    const auto openssl = FindOnPath("openssl");
    if (!turnserver || !openssl) {
      skip_ = "coturn not installed (turnserver and openssl on PATH)";
      return;
    }
    char tmpl[] = "/tmp/sm_coturn_XXXXXX";
    if (!mkdtemp(tmpl)) {
      skip_ = "coturn not installed: no temporary directory";
      return;
    }
    dir_ = tmpl;
    const std::string cert = dir_ + "/cert.pem";
    const std::string key = dir_ + "/key.pem";
    if (RunProcess({*openssl, "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1",
                    "-nodes", "-keyout", key, "-out", cert, "-days", "1", "-subj", "/CN=turn.example.org"},
                   dir_ + "/openssl.log", nullptr) != 0) {
      failed_ = "openssl could not make a certificate";
      return;
    }
    server_ip_ = local_ip_ = INADDR_LOOPBACK;
    port_ = FreePort();
    tls_port_ = FreePort();
    while (tls_port_ == port_) tls_port_ = FreePort();
    // Relay ports away from the ephemeral range the client sockets use.
    const uint16_t relay_min = static_cast<uint16_t>(20000 + (getpid() % 400) * 100);
    std::vector<std::string> args = {
        *turnserver,
        "-n",  // No configuration file.
        "--listening-ip=127.0.0.1",
        "--relay-ip=127.0.0.1",
        "--listening-port=" + std::to_string(port_),
        "--tls-listening-port=" + std::to_string(tls_port_),
        "--min-port=" + std::to_string(relay_min),
        "--max-port=" + std::to_string(relay_min + 99),
        "--realm=" + std::string(kRealm),
        "--cert=" + cert,
        "--pkey=" + key,
        "--allow-loopback-peers",
        "--max-allocate-lifetime=" + std::to_string(kLocalLifetimeSeconds),
        "--no-cli",
        "--no-dtls",
        "--no-multicast-peers",
        "--fingerprint",
        "--log-file=stdout",
        "--simple-log",
        "--verbose",
    };
    if (creds_ == Creds::kRest) {
      secret_ = kSecret;
      args.push_back("--use-auth-secret");
      args.push_back("--static-auth-secret=" + secret_);
    } else {
      user_ = kUser;
      password_ = kPassword;
      args.push_back("--lt-cred-mech");
      args.push_back("--user=" + user_ + ":" + password_);
    }
    if (RunProcess(args, dir_ + "/turnserver.log", &pid_) != 0) {
      failed_ = "could not start turnserver";
      return;
    }
    for (int i = 0; i < 100; ++i) {
      if (TcpReachable(INADDR_LOOPBACK, port_) && TcpReachable(INADDR_LOOPBACK, tls_port_)) return;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    failed_ = "turnserver did not start listening";
  }

 public:
  // Set when the server should have started and didn't: a failure, not a skip.
  std::string failed_;

 private:
  Creds creds_;
  std::string skip_;
  std::string dir_;
  pid_t pid_ = 0;
  uint32_t server_ip_ = 0;
  uint32_t local_ip_ = 0;
  uint16_t port_ = 0;
  uint16_t tls_port_ = 0;
  std::string user_, password_, secret_;
};

// What went over the client sockets, by STUN message type, plus ChannelData.
struct Traffic {
  std::map<uint16_t, int> sent, received;
  int channel_data_sent = 0;
  int channel_data_received = 0;
  void Record(bool is_sent, std::span<const uint8_t> m) {
    if (m.size() < 4) return;
    if ((m[0] & 0xC0) == 0x40) {
      (is_sent ? channel_data_sent : channel_data_received)++;
    } else if ((m[0] & 0xC0) == 0 && m.size() >= kStunHeaderSize) {
      (is_sent ? sent : received)[static_cast<uint16_t>(m[0] << 8 | m[1])]++;
    }
  }
  int Sent(uint16_t t) const { return sent.count(t) ? sent.at(t) : 0; }
  int Received(uint16_t t) const { return received.count(t) ? received.at(t) : 0; }
};

struct Client : TurnPort::CallbacksForTest {
  std::unique_ptr<TurnPort> port;
  ProtocolAddress server_address{SocketAddress(), PROTO_UDP};
  RelayServerConfig config;
  bool ready = false;
  bool error = false;
  bool closed = false;
  int permission_ok = 0;
  int permission_failed = 0;
  int refresh_ok = 0;
  int refresh_failed = 0;
  std::vector<std::vector<uint8_t>> packets;

  void OnTurnCreatePermissionResult(int code) override { (code == 0 ? permission_ok : permission_failed)++; }
  void OnTurnRefreshResult(int code) override { (code == 0 ? refresh_ok : refresh_failed)++; }
  void OnTurnPortClosed() override { closed = true; }
};

struct Interop {
  explicit Interop(Creds creds) : server(creds) {
    InstallStunCryptoFromTls();
    InstallSslAdapterFromTls();
    sockets.set_tap([this](bool sent, std::span<const uint8_t> m) { traffic.Record(sent, m); });
  }

  Server server;
  interop::OsSocketServer sockets;
  Traffic traffic;
  std::unique_ptr<Network> network;
  Client a, b;
  Timestamp now = Now();

  void MakeClient(Client& c, ProtocolType proto, const char* ufrag, uint64_t tiebreaker) {
    if (!network) {
      network = std::make_unique<Network>("lo", IpAddress(server.local_ip()), 32);
      network->AddIp(IpAddress(server.local_ip()));
    }
    c.server_address = ProtocolAddress(SocketAddress(server.server_ip(), server.port(proto)), proto);
    const RelayCredentials creds = server.credentials();
    c.config = RelayServerConfig(c.server_address.address, creds.username, creds.password, proto);
    if (proto == PROTO_TLS) c.config.tls_cert_policy = TlsCertPolicy::TLS_CERT_POLICY_INSECURE_NO_CHECK;
    CreateRelayPortArgs args;
    args.port.network = network.get();
    args.port.socket_factory = &sockets;
    args.port.ice_username_fragment = ufrag;
    args.port.ice_password = std::string(ufrag) + "passwordpasswordpasswo";
    args.port.ice_tiebreaker = tiebreaker;
    args.server_address = &c.server_address;
    args.config = &c.config;
    args.username = args.port.ice_username_fragment;
    args.password = args.port.ice_password;
    c.port = TurnPort::Create(now, args, 0, 0);
    SM_ASSERT_TRUE(c.port != nullptr);
    // The local server's certificate is self-signed (as libwebrtc's TURN tests).
    if (proto == PROTO_TLS) c.port->SetTlsCertPolicy(TlsCertPolicy::TLS_CERT_POLICY_INSECURE_NO_CHECK);
    c.port->SetIceRole(tiebreaker == 1 ? ICEROLE_CONTROLLING : ICEROLE_CONTROLLED);
    c.port->SetCallbacksForTest(&c);
    c.port->SubscribePortComplete(nullptr, [&c](Port*) { c.ready = true; });
    c.port->SubscribePortError(nullptr, [&c](Port*) { c.error = true; });
  }

  // Runs the ports and the sockets on the real clock until `stop()` holds or
  // `timeout` passes.
  template <class F>
  bool WaitUntil(F stop, TimeDelta timeout) {
    const Timestamp until = Now() + timeout;
    for (;;) {
      now = Now();
      for (Client* c : {&a, &b}) {
        if (c->port && c->port->NextProcessTime() <= now) c->port->Process(now);
      }
      if (stop()) return true;
      if (now >= until) return false;
      Timestamp next = until;
      for (Client* c : {&a, &b}) {
        if (c->port) next = std::min(next, c->port->NextProcessTime());
      }
      sockets.Poll(now, std::clamp(next - now, TimeDelta::Zero(), TimeDelta::Millis(20)));
    }
  }
};

void Run(Creds creds, ProtocolType proto) {
  Interop f(creds);
  if (!f.server.failed_.empty()) {
    std::printf("%s\n%s\n", f.server.failed_.c_str(), f.server.log().c_str());
    SM_ASSERT_TRUE(f.server.failed_.empty());
  }
  if (!f.server.skip_reason().empty()) {
    std::printf("SKIPPED: %s\n", f.server.skip_reason().c_str());
    return;
  }

  // Allocate: both ports get a relay candidate on the server's relay address.
  f.MakeClient(f.a, proto, "afrag", 1);
  f.MakeClient(f.b, proto, "bfrag", 2);
  f.a.port->PrepareAddress(f.now);
  f.b.port->PrepareAddress(f.now);
  const bool allocated = f.WaitUntil([&] { return (f.a.ready || f.a.error) && (f.b.ready || f.b.error); },
                                     kStepTimeout);
  if (!allocated || f.a.error || f.b.error) std::printf("%s\n", f.server.log().c_str());
  SM_ASSERT_TRUE(allocated);
  SM_ASSERT_FALSE(f.a.error);
  SM_ASSERT_FALSE(f.b.error);
  SM_ASSERT_EQ(f.a.port->Candidates().size(), size_t{1});
  SM_ASSERT_EQ(f.b.port->Candidates().size(), size_t{1});
  const Candidate& ca = f.a.port->Candidates()[0];
  const Candidate& cb = f.b.port->Candidates()[0];
  SM_EXPECT_TRUE(ca.type() == IceCandidateType::kRelay);
  SM_EXPECT_EQ(ca.protocol(), std::string(UDP_PROTOCOL_NAME));
  SM_EXPECT_EQ(ca.relay_protocol(), std::string(ProtoToString(proto)));
  SM_EXPECT_TRUE(ca.address() != cb.address());
  if (f.server.local()) {
    SM_EXPECT_EQ(ca.address().ip(), static_cast<uint32_t>(INADDR_LOOPBACK));
    SM_EXPECT_EQ(cb.address().ip(), static_cast<uint32_t>(INADDR_LOOPBACK));
  }
  // The first Allocate is challenged (401), the second carries the
  // credential: two requests, two answers.
  SM_EXPECT_EQ(f.traffic.Sent(STUN_ALLOCATE_REQUEST), 4);
  SM_EXPECT_EQ(f.traffic.Received(STUN_ALLOCATE_ERROR_RESPONSE), 2);
  SM_EXPECT_EQ(f.traffic.Received(STUN_ALLOCATE_RESPONSE), 2);

  // Permission and channel bind: each port connects to the other's relay
  // candidate and pings it until the pair is writable both ways.
  Connection* ab = f.a.port->CreateConnection(f.now, cb, Port::ORIGIN_MESSAGE);
  Connection* ba = f.b.port->CreateConnection(f.now, ca, Port::ORIGIN_MESSAGE);
  SM_ASSERT_TRUE(ab != nullptr);
  SM_ASSERT_TRUE(ba != nullptr);
  ab->RegisterReceivedPacketCallback(
      [&](Connection*, const ReceivedPacket& p) { f.a.packets.emplace_back(p.payload.begin(), p.payload.end()); });
  ba->RegisterReceivedPacketCallback(
      [&](Connection*, const ReceivedPacket& p) { f.b.packets.emplace_back(p.payload.begin(), p.payload.end()); });
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.a.permission_ok > 0 && f.b.permission_ok > 0; }, kStepTimeout));
  SM_EXPECT_EQ(f.a.permission_failed + f.b.permission_failed, 0);
  ab->Ping(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return ab->writable(); }, kStepTimeout));
  ba->Ping(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return ba->writable(); }, kStepTimeout));
  SM_EXPECT_GE(f.traffic.Received(TURN_CREATE_PERMISSION_RESPONSE), 2);
  SM_EXPECT_EQ(f.traffic.Received(TURN_CREATE_PERMISSION_ERROR_RESPONSE), 0);
  // The first payload to each peer goes in a Send indication and starts the
  // channel bind (TurnEntry::Send; pings don't).
  SM_EXPECT_EQ(f.traffic.Sent(TURN_CHANNEL_BIND_REQUEST), 0);
  const std::vector<uint8_t> first = {0xAB};
  SM_EXPECT_EQ(ab->Send(f.now, first, {}), 1);
  SM_EXPECT_EQ(ba->Send(f.now, first, {}), 1);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.traffic.Received(TURN_CHANNEL_BIND_RESPONSE) >= 2; }, kStepTimeout));
  SM_EXPECT_EQ(f.traffic.Received(TURN_CHANNEL_BIND_ERROR_RESPONSE), 0);
  SM_EXPECT_EQ(f.traffic.Sent(TURN_CHANNEL_BIND_REQUEST), 2);
  SM_EXPECT_GE(f.traffic.Sent(TURN_SEND_INDICATION), 2);
  SM_EXPECT_GE(f.traffic.Received(TURN_DATA_INDICATION), 2);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.a.packets.size() == 1 && f.b.packets.size() == 1; }, kStepTimeout));
  SM_EXPECT_TRUE(f.a.packets[0] == first);
  SM_EXPECT_TRUE(f.b.packets[0] == first);

  // Data both ways: 64 packets of 1 to 64 bytes each way, as ChannelData now
  // that the channels are bound.
  f.a.packets.clear();
  f.b.packets.clear();
  const int channel_sent = f.traffic.channel_data_sent;
  const int channel_received = f.traffic.channel_data_received;
  constexpr size_t kPackets = 64;
  for (size_t i = 0; i < kPackets; ++i) {
    std::vector<uint8_t> buf(i + 1);
    uint8_t val = static_cast<uint8_t>(0xFF - i);
    for (auto& byte : buf) byte = val--;
    SM_EXPECT_EQ(ab->Send(f.now, buf, {}), static_cast<int>(buf.size()));
    SM_EXPECT_EQ(ba->Send(f.now, buf, {}), static_cast<int>(buf.size()));
    f.WaitUntil([] { return false; }, TimeDelta::Millis(2));
  }
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.a.packets.size() >= kPackets && f.b.packets.size() >= kPackets; },
                             kStepTimeout));
  // UDP on loopback doesn't lose or reorder.
  SM_ASSERT_EQ(f.a.packets.size(), kPackets);
  SM_ASSERT_EQ(f.b.packets.size(), kPackets);
  for (size_t i = 0; i < kPackets; ++i) {
    SM_EXPECT_EQ(f.a.packets[i].size(), i + 1);
    SM_EXPECT_TRUE(f.a.packets[i] == f.b.packets[i]);
  }
  SM_EXPECT_GE(f.traffic.channel_data_sent - channel_sent, static_cast<int>(2 * kPackets));
  SM_EXPECT_GE(f.traffic.channel_data_received - channel_received, static_cast<int>(2 * kPackets));

  // Refresh: the local server caps the lifetime, so a refresh comes due.
  if (f.server.local()) {
    SM_ASSERT_TRUE(
        f.WaitUntil([&] { return f.a.refresh_ok > 0 && f.b.refresh_ok > 0; }, TimeDelta::Seconds(kLocalLifetimeSeconds)));
    SM_EXPECT_EQ(f.a.refresh_failed + f.b.refresh_failed, 0);
    SM_EXPECT_GE(f.traffic.Received(TURN_REFRESH_RESPONSE), 2);
    // Still relaying after the refresh.
    f.a.packets.clear();
    const std::vector<uint8_t> after = {1, 2, 3, 4, 5};
    ba->Send(f.now, after, {});
    SM_ASSERT_TRUE(f.WaitUntil([&] { return !f.a.packets.empty(); }, kStepTimeout));
    SM_EXPECT_TRUE(f.a.packets[0] == after);
  }

  // Deallocate: Refresh with LIFETIME 0; the port closes on the answer.
  const int refreshes = f.traffic.Received(TURN_REFRESH_RESPONSE);
  f.a.port->Release(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.a.closed; }, kStepTimeout));
  SM_EXPECT_EQ(f.traffic.Received(TURN_REFRESH_RESPONSE), refreshes + 1);
  SM_EXPECT_EQ(f.traffic.Received(TURN_REFRESH_ERROR_RESPONSE), 0);
  f.b.port->Release(f.now);
  SM_ASSERT_TRUE(f.WaitUntil([&] { return f.b.closed; }, kStepTimeout));
  SM_EXPECT_EQ(f.traffic.Received(TURN_REFRESH_ERROR_RESPONSE), 0);
}

}  // namespace

SM_TEST(Coturn, UdpLongTermCredentials) { Run(Creds::kLongTerm, PROTO_UDP); }
SM_TEST(Coturn, TcpLongTermCredentials) { Run(Creds::kLongTerm, PROTO_TCP); }
SM_TEST(Coturn, TlsLongTermCredentials) { Run(Creds::kLongTerm, PROTO_TLS); }
SM_TEST(Coturn, UdpRestCredentials) { Run(Creds::kRest, PROTO_UDP); }
SM_TEST(Coturn, TcpRestCredentials) { Run(Creds::kRest, PROTO_TCP); }
SM_TEST(Coturn, TlsRestCredentials) { Run(Creds::kRest, PROTO_TLS); }
