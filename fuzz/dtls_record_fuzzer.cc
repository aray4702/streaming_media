// DTLS records (#1058, C10 part 3): sm::tls::DtlsTransport's read path
// (OnPacket) on arbitrary datagrams, before and after a handshake. libwebrtc
// M157 has no DTLS fuzzer (compare/webrtc/reference/test/fuzzers/), so this
// feeds the function libwebrtc's OpenSSLStreamAdapter reads records through
// (rtc_base/openssl_stream_adapter.cc: the transport's datagram into the
// BIO, then SSL_do_handshake or SSL_read), here sm::tls's adapter on
// BoringSSL or Mbed TLS. CMake builds it against both backends when
// SM_TLS_CROSS_TEST is on (dtls_record and dtls_record_<other backend>), with
// one corpus.
//
// The first byte holds flags: bit 0 the role of the transport under test
// (0 server, 1 client), bit 1 a completed handshake first (with a real peer,
// over a 10 ms pipe), bit 2 DTLS 1.2 as the highest version (else the
// default, 1.3 on BoringSSL). The rest is records: a control byte, the
// milliseconds to run after the datagram (its timers fire), then a
// big-endian 16-bit length and that many bytes (fuzz_data_reader.h). Each
// datagram goes to OnPacket; what the transport sends and receives is
// drained. After a handshake the transport also sends one record of its own.
//
// With SM_FUZZ_WRITE_SEEDS=<dir> in the environment, the first run writes the
// seed corpus (corpus/dtls_record/) to <dir>: each side's handshake flights
// of a real handshake at DTLS 1.3 and 1.2, application data records, and a
// ClientHello on its own. The handshake's keys are random, so the seeds are
// shapes for the mutator, not a replayable session.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "fuzz_data_reader.h"
#include "sm/core/units.h"
#include "sm/tls/dtls_transport.h"
#include "sm/tls/ssl_fingerprint.h"
#include "sm/tls/ssl_identity.h"

namespace {

using namespace sm;
using namespace sm::tls;
using Bytes = std::vector<uint8_t>;

void Check(bool ok) {
  if (!ok) std::abort();
}

struct Identities {
  std::unique_ptr<SslIdentity> client = SslIdentity::Create("client");
  std::unique_ptr<SslIdentity> server = SslIdentity::Create("server");
  std::unique_ptr<SslFingerprint> client_fp = SslFingerprint::CreateFromCertificate(*client);
  std::unique_ptr<SslFingerprint> server_fp = SslFingerprint::CreateFromCertificate(*server);
};

const Identities& Ids() {
  static const Identities* ids = [] {
    auto* i = new Identities;
    Check(i->client && i->server && i->client_fp && i->server_fp);
    return i;
  }();
  return *ids;
}

struct Pair {
  std::unique_ptr<DtlsTransport> client, server;
  Timestamp now = Timestamp::Seconds(1000);
  // Datagrams each way, for the seeds.
  std::vector<Bytes> to_server, to_client;

  explicit Pair(DtlsConfig cfg) {
    client = std::make_unique<DtlsTransport>(Ids().client->Clone(), DtlsRole::kClient, cfg);
    server = std::make_unique<DtlsTransport>(Ids().server->Clone(), DtlsRole::kServer, cfg);
    Check(client->SetPeerFingerprint(*Ids().server_fp, now));
    Check(server->SetPeerFingerprint(*Ids().client_fp, now));
  }
  // Runs the 10 ms pipe for `ms`.
  void Run(int ms) {
    struct InFlight {
      Timestamp at;
      bool to_server;
      Bytes d;
    };
    std::vector<InFlight> flight;
    for (int t = 0; t < ms; ++t, now = now + TimeDelta::Millis(1)) {
      for (size_t i = 0; i < flight.size();) {
        if (flight[i].at <= now) {
          (flight[i].to_server ? *server : *client).OnPacket(flight[i].d, now);
          flight.erase(flight.begin() + static_cast<long>(i));
        } else {
          ++i;
        }
      }
      for (DtlsTransport* tr : {client.get(), server.get()}) {
        if (auto due = tr->NextTimeout(); due && *due <= now) tr->OnTimeout(now);
      }
      for (auto& d : client->TakeOutgoing()) {
        to_server.push_back(d);
        flight.push_back({now + TimeDelta::Millis(10), true, std::move(d)});
      }
      for (auto& d : server->TakeOutgoing()) {
        to_client.push_back(d);
        flight.push_back({now + TimeDelta::Millis(10), false, std::move(d)});
      }
      (void)client->TakeReceived();
      (void)server->TakeReceived();
    }
  }
  bool Handshake() {
    if (!server->Start(now) || !client->Start(now)) return false;
    Run(500);
    return client->handshake_complete() && server->handshake_complete();
  }
};

void Drain(DtlsTransport& t, Timestamp now) {
  if (auto due = t.NextTimeout(); due && *due <= now) t.OnTimeout(now);
  (void)t.TakeOutgoing();
  (void)t.TakeReceived();
}

void WriteFile(const std::string& path, const Bytes& data) {
  if (FILE* f = std::fopen(path.c_str(), "wb")) {
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
  }
}

void AppendRecord(Bytes& out, uint8_t ms, std::span<const uint8_t> bytes) {
  out.push_back(ms);
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
  const std::string base(dir);
  for (bool dtls12 : {false, true}) {
    DtlsConfig cfg;
    if (dtls12) cfg.max_version = kDtls12;
    Pair p(cfg);
    Check(p.Handshake());
    const size_t hs_to_server = p.to_server.size();
    const size_t hs_to_client = p.to_client.size();
    const Bytes app = {0x17, 'h', 'e', 'l', 'l', 'o'};
    Check(p.client->Send(app, p.now));
    Check(p.server->Send(app, p.now));
    p.Run(50);
    const uint8_t v = dtls12 ? 4 : 0;
    const std::string sfx = dtls12 ? "-12" : "-13";
    // Before a handshake: the server gets the client's flights, the client
    // the server's.
    Bytes server_hs = {static_cast<uint8_t>(v | 0)};
    for (size_t i = 0; i < hs_to_server; ++i) AppendRecord(server_hs, 20, p.to_server[i]);
    WriteFile(base + "/server-handshake" + sfx, server_hs);
    Bytes hello = {static_cast<uint8_t>(v | 0)};
    AppendRecord(hello, 20, p.to_server[0]);
    WriteFile(base + "/server-clienthello" + sfx, hello);
    Bytes client_hs = {static_cast<uint8_t>(v | 1)};
    for (size_t i = 0; i < hs_to_client; ++i) AppendRecord(client_hs, 20, p.to_client[i]);
    WriteFile(base + "/client-handshake" + sfx, client_hs);
    // After a handshake: application data records (under other keys).
    Bytes server_app = {static_cast<uint8_t>(v | 2)};
    for (size_t i = hs_to_server; i < p.to_server.size(); ++i) AppendRecord(server_app, 5, p.to_server[i]);
    WriteFile(base + "/server-appdata" + sfx, server_app);
    Bytes client_app = {static_cast<uint8_t>(v | 3)};
    for (size_t i = hs_to_client; i < p.to_client.size(); ++i) AppendRecord(client_app, 5, p.to_client[i]);
    WriteFile(base + "/client-appdata" + sfx, client_app);
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  MaybeWriteSeeds();
  sm::fuzz::FuzzDataReader in(std::span<const uint8_t>(data, size));
  const uint8_t flags = in.ReadU8();
  const bool client = flags & 1;
  const bool after = flags & 2;
  DtlsConfig cfg;
  if (flags & 4) cfg.max_version = kDtls12;

  Pair p(cfg);
  DtlsTransport& t = client ? *p.client : *p.server;
  if (after) {
    Check(p.Handshake());
    const Bytes app = {1, 2, 3, 4};
    Check(t.Send(app, p.now));
  } else {
    Check(t.Start(p.now));
  }
  Drain(t, p.now);
  for (int i = 0; i < 64 && in.BytesLeft() > 0; ++i) {
    const uint8_t ms = in.ReadU8();
    const auto datagram = in.ReadLengthPrefixed();
    t.OnPacket(datagram, p.now);
    Drain(t, p.now);
    for (int k = 0; k < ms; ++k) {
      p.now = p.now + TimeDelta::Millis(1);
      Drain(t, p.now);
    }
  }
  (void)t.state();
  (void)t.version();
  (void)t.srtp_crypto_suite();
  return 0;
}
