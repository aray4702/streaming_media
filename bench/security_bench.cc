// Security (section 2) benchmarks: sm::tls on the configured backend (BoringSSL or mbedTLS, SM_TLS_BACKEND), with
// in-memory transports and no clock: digests, random bytes and certificates (2.1), the DTLS handshake and record
// layer (2.2), the DTLS-SRTP key export (2.3), SRTP and SRTCP through libsrtp (2.4), and TLS over a byte stream, the
// engine of the TLS client socket (2.5). Built only with SM_WITH_TLS (never for wasm).
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/tls/crypto_random.h"
#include "sm/tls/dtls_transport.h"
#include "sm/tls/message_digest.h"
#include "sm/tls/srtp_session.h"
#include "sm/tls/ssl_fingerprint.h"
#include "sm/tls/ssl_identity.h"
#include "sm/tls/tls_stream.h"
#include "sm_bench.h"

using namespace sm;
using namespace sm::tls;

namespace {

// Key generation and handshakes have a budget per TLS library (mbedTLS's ECDSA is about twice BoringSSL's time).
#if SM_TLS_BACKEND_MBEDTLS
constexpr bool kMbedTls = true;
#else
constexpr bool kMbedTls = false;
#endif

// Moves datagrams between two DTLS transports with no delay or loss until neither has anything to send. The
// handshake needs no timer on a lossless pipe, so `now` stays put.
void Pump(DtlsTransport& a, DtlsTransport& b, Timestamp now) {
  for (int round = 0; round < 64; ++round) {
    bool moved = false;
    for (auto& d : a.TakeOutgoing()) {
      b.OnPacket(d, now);
      moved = true;
    }
    for (auto& d : b.TakeOutgoing()) {
      a.OnPacket(d, now);
      moved = true;
    }
    if (!moved) return;
  }
}

// A client and a server DTLS transport, each with the other's fingerprint, as SDP gives them.
struct DtlsPair {
  std::unique_ptr<DtlsTransport> client, server;
  Timestamp now = Timestamp::Seconds(1000);

  DtlsPair(const SslIdentity& cid, const SslIdentity& sid, const SslFingerprint& cfp, const SslFingerprint& sfp) {
    client = std::make_unique<DtlsTransport>(cid.Clone(), DtlsRole::kClient);
    server = std::make_unique<DtlsTransport>(sid.Clone(), DtlsRole::kServer);
    client->SetPeerFingerprint(sfp, now);
    server->SetPeerFingerprint(cfp, now);
  }
  bool Connect() {
    if (!server->Start(now) || !client->Start(now)) return false;
    Pump(*client, *server, now);
    return client->state() == DtlsState::kConnected && server->state() == DtlsState::kConnected;
  }
};

struct Identities {
  std::unique_ptr<SslIdentity> client = SslIdentity::Create("client");
  std::unique_ptr<SslIdentity> server = SslIdentity::Create("server");
  std::unique_ptr<SslFingerprint> client_fp = SslFingerprint::CreateFromCertificate(*client);
  std::unique_ptr<SslFingerprint> server_fp = SslFingerprint::CreateFromCertificate(*server);
};
const Identities& Ids() {
  static const Identities ids;
  return ids;
}

std::unique_ptr<DtlsPair> ConnectedPair() {
  const Identities& ids = Ids();
  auto p = std::make_unique<DtlsPair>(*ids.client, *ids.server, *ids.client_fp, *ids.server_fp);
  return p->Connect() ? std::move(p) : nullptr;
}

// An RTP packet of `size` bytes: V=2, PT 96, the given sequence number and timestamp, SSRC 1.
void FillRtp(std::vector<uint8_t>& p, size_t size, uint16_t seq, uint32_t ts) {
  p.assign(size, 0xAB);
  p[0] = 0x80;
  p[1] = 96;
  p[2] = static_cast<uint8_t>(seq >> 8);
  p[3] = static_cast<uint8_t>(seq);
  p[4] = static_cast<uint8_t>(ts >> 24);
  p[5] = static_cast<uint8_t>(ts >> 16);
  p[6] = static_cast<uint8_t>(ts >> 8);
  p[7] = static_cast<uint8_t>(ts);
  p[8] = p[9] = p[10] = 0;
  p[11] = 1;
}

// A receiver report from SSRC 1 with one report block (32 bytes).
const std::vector<uint8_t> kRtcpReport = {
    0x81, 0xC9, 0x00, 0x07, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// A sender and a receiver SRTP session on the same key (master key then salt, the suite's lengths).
struct SrtpPair {
  SrtpSession send, recv;
  bool ok = false;
  explicit SrtpPair(int suite) {
    int key_len = 0, salt_len = 0;
    if (!SrtpKeyAndSaltLength(suite, key_len, salt_len)) return;
    std::vector<uint8_t> key(static_cast<size_t>(key_len + salt_len));
    for (size_t i = 0; i < key.size(); ++i) key[i] = static_cast<uint8_t>(i * 7 + 1);
    ok = send.SetSend(suite, key) && recv.SetReceive(suite, key);
  }
};

void SrtpRtpRoundTrip(bench::State& st, int suite) {
  SrtpPair s(suite);
  if (!s.ok) {
    st.Require("srtp_session_set_up", 0, ">=", 1);
    return;
  }
  std::vector<uint8_t> p;
  p.reserve(1300);
  uint16_t seq = 0;
  uint32_t ts = 0;
  int failures = 0;
  for (auto _ : st) {
    FillRtp(p, 1200, seq++, ts += 3000);
    if (!s.send.ProtectRtp(p) || !s.recv.UnprotectRtp(p)) ++failures;
    bench::DoNotOptimize(p.data());
  }
  st.Require("unprotect_failures", failures, "<=", 0);
}

TlsConfig ServerConfig() {
  TlsConfig c;
  c.role = TlsRole::kServer;
  c.ignore_bad_cert = true;
  c.load_system_roots = false;
  return c;
}

// A TLS client that trusts the server's self-signed certificate, and the server, over an in-memory byte stream (the
// TCP connection under the TLS client socket).
struct TlsPair {
  std::unique_ptr<TlsStream> client, server;
  explicit TlsPair(const SslIdentity& server_id) {
    TlsConfig c;
    c.server_name = "turn.example.org";
    c.load_system_roots = false;
    const auto der = server_id.certificate().der();
    c.trusted_roots.emplace_back(der.begin(), der.end());
    server = std::make_unique<TlsStream>(ServerConfig(), server_id.Clone());
    client = std::make_unique<TlsStream>(std::move(c));
  }
  void Pump() {
    for (int i = 0; i < 100; ++i) {
      bool moved = false;
      if (client->HasOutgoing()) {
        server->Feed(client->TakeOutgoing());
        moved = true;
      }
      if (server->HasOutgoing()) {
        client->Feed(server->TakeOutgoing());
        moved = true;
      }
      if (!moved) return;
    }
  }
  bool Connect() {
    if (!server->Start() || !client->Start()) return false;
    Pump();
    return client->state() == TlsState::kConnected && server->state() == TlsState::kConnected;
  }
};

const SslIdentity& TlsServerIdentity() {
  static const std::unique_ptr<SslIdentity> id = SslIdentity::Create("turn.example.org");
  return *id;
}

}  // namespace

// ---- 2.1 The TLS/DTLS interface ------------------------------------------------------------------------------------

// Unit: one SHA-256 digest of 1200 bytes (an RTP packet's size).
SM_BENCH(security, Digest_Sha256_1200B) {
  std::vector<uint8_t> in(1200, 0x5A);
  uint8_t out[MessageDigest::kMaxSize];
  for (auto _ : st) {
    in[0]++;
    bench::DoNotOptimize(ComputeDigest(kDigestSha256, in, out));
    bench::DoNotOptimize(out[0]);
  }
  st.BudgetNsPerItem(kMbedTls ? 20000 : 4000);
}

// Unit: one HMAC-SHA1 of a 100-byte STUN message (MESSAGE-INTEGRITY).
SM_BENCH(security, Hmac_Sha1_100B) {
  std::vector<uint8_t> key(16, 0x11), in(100, 0x22);
  uint8_t out[MessageDigest::kMaxSize];
  for (auto _ : st) {
    in[0]++;
    bench::DoNotOptimize(ComputeHmac(kDigestSha1, key, in, out));
    bench::DoNotOptimize(out[0]);
  }
  st.BudgetNsPerItem(kMbedTls ? 3000 : 1500);
}

// Unit: 16 random bytes from the backend's CSPRNG (an ICE ufrag's worth).
SM_BENCH(security, CryptoRandom_16B) {
  uint8_t out[16];
  for (auto _ : st) {
    bench::DoNotOptimize(CreateRandomBytes(out));
    bench::DoNotOptimize(out[0]);
  }
  st.BudgetNsPerItem(kMbedTls ? 1000 : 2000);
}

// Unit: one ECDSA P-256 identity (key pair and self-signed certificate) and its SHA-256 fingerprint, as a
// connection's certificate is made.
SM_BENCH(security, Identity_EcdsaAndFingerprint) {
  int failures = 0;
  for (auto _ : st) {
    auto id = SslIdentity::Create("bench");
    auto fp = id ? SslFingerprint::CreateFromCertificate(*id) : nullptr;
    if (!fp) ++failures;
    bench::DoNotOptimize(fp);
  }
  st.Require("failures", failures, "<=", 0);
  st.BudgetNsPerItem(kMbedTls ? 2000000 : 200000);
}

// Unit: the SHA-256 fingerprint of a certificate and its RFC 4572 text, as SDP's a=fingerprint is written.
SM_BENCH(security, Fingerprint_Sha256) {
  const SslIdentity& id = *Ids().server;
  for (auto _ : st) {
    auto fp = SslFingerprint::CreateFromCertificate(id);
    bench::DoNotOptimize(fp->GetRfc4572Fingerprint());
  }
  st.BudgetNsPerItem(kMbedTls ? 10000 : 4000);
}

// ---- 2.2 DTLS transport --------------------------------------------------------------------------------------------

// Unit: one full DTLS handshake (both ends, fingerprints checked) between two in-memory transports.
SM_BENCH(security, Dtls_Handshake) {
  const Identities& ids = Ids();
  int failures = 0;
  for (auto _ : st) {
    DtlsPair p(*ids.client, *ids.server, *ids.client_fp, *ids.server_fp);
    if (!p.Connect()) ++failures;
  }
  st.Require("failures", failures, "<=", 0);
  st.BudgetNsPerItem(kMbedTls ? 15000000 : 1500000);
}

// Unit: one 1000-byte application record (an SCTP packet's size; 1200 bytes and the record overhead would pass the
// 1200-byte DTLS MTU, which mbedTLS refuses) sent by one end and received by the other (protect + unprotect).
SM_BENCH(security, Dtls_Record_1000B) {
  auto p = ConnectedPair();
  if (!p) {
    st.Require("connected", 0, ">=", 1);
    return;
  }
  std::vector<uint8_t> data(1000, 0x42);
  size_t received = 0;
  for (auto _ : st) {
    data[0]++;
    p->client->Send(data, p->now);
    for (auto& d : p->client->TakeOutgoing()) p->server->OnPacket(d, p->now);
    for (auto& r : p->server->TakeReceived()) received += r.size();
  }
  st.Require("bytes_received_per_record", st.iterations() ? double(received) / double(st.iterations()) : 1000, ">=",
             1000);
  st.BudgetNsPerItem(8000);
}

// ---- 2.3 DTLS-SRTP key export --------------------------------------------------------------------------------------

// Unit: one RFC 5764 key export after a handshake, split into the send and receive keys for the DTLS role.
SM_BENCH(security, DtlsSrtp_ExportKeys) {
  auto p = ConnectedPair();
  if (!p) {
    st.Require("connected", 0, ">=", 1);
    return;
  }
  int failures = 0;
  for (auto _ : st) {
    SrtpKeys keys;
    if (!ExtractSrtpKeys(*p->client, keys)) ++failures;
    bench::DoNotOptimize(keys.send_key.data());
  }
  st.Require("failures", failures, "<=", 0);
  st.BudgetNsPerItem(kMbedTls ? 30000 : 5000);
}

// ---- 2.4 SRTP and SRTCP --------------------------------------------------------------------------------------------

// Unit: one 1200-byte RTP packet protected and unprotected, AES-CM-128-HMAC-SHA1-80 (the default profile).
SM_BENCH(security, Srtp_RtpRoundTrip_1200B_AesCmSha1_80) {
  SrtpRtpRoundTrip(st, kSrtpAes128CmSha1_80);
  st.BudgetNsPerItem(kMbedTls ? 25000 : 8000);
}

// Unit: one 1200-byte RTP packet protected and unprotected, AEAD AES-128-GCM.
SM_BENCH(security, Srtp_RtpRoundTrip_1200B_AeadAes128Gcm) {
  SrtpRtpRoundTrip(st, kSrtpAeadAes128Gcm);
  st.BudgetNsPerItem(kMbedTls ? 10000 : 3000);
}

// Unit: one 32-byte RTCP receiver report protected and unprotected (SRTCP index, auth tag), AES-CM-128-HMAC-SHA1-80.
SM_BENCH(security, Srtcp_RoundTrip_ReceiverReport) {
  SrtpPair s(kSrtpAes128CmSha1_80);
  if (!s.ok) {
    st.Require("srtp_session_set_up", 0, ">=", 1);
    return;
  }
  std::vector<uint8_t> p;
  p.reserve(128);
  int failures = 0;
  for (auto _ : st) {
    p = kRtcpReport;
    if (!s.send.ProtectRtcp(p) || !s.recv.UnprotectRtcp(p)) ++failures;
    bench::DoNotOptimize(p.data());
  }
  st.Require("unprotect_failures", failures, "<=", 0);
  st.BudgetNsPerItem(kMbedTls ? 4000 : 1500);
}

// ---- 2.5 TLS client socket over TCP --------------------------------------------------------------------------------

// Unit: one TLS handshake (the highest version both ends have, the server's certificate checked against a trusted
// root) between a client and a server over an in-memory byte stream.
SM_BENCH(security, Tls_Handshake) {
  const SslIdentity& id = TlsServerIdentity();
  int failures = 0;
  for (auto _ : st) {
    TlsPair p(id);
    if (!p.Connect()) ++failures;
  }
  st.Require("failures", failures, "<=", 0);
  st.BudgetNsPerItem(kMbedTls ? 10000000 : 1500000);
}

// Unit: 1200 bytes written by the client, carried over the stream and read by the server (one TLS record).
SM_BENCH(security, Tls_Record_1200B) {
  TlsPair p(TlsServerIdentity());
  if (!p.Connect()) {
    st.Require("connected", 0, ">=", 1);
    return;
  }
  std::vector<uint8_t> data(1200, 0x42), buf(2048);
  size_t received = 0;
  for (auto _ : st) {
    data[0]++;
    p.client->Write(data);
    p.server->Feed(p.client->TakeOutgoing());
    int n;
    while ((n = p.server->Read(buf)) > 0) received += static_cast<size_t>(n);
  }
  st.Require("bytes_received_per_record", st.iterations() ? double(received) / double(st.iterations()) : 1200, ">=",
             1200);
  st.BudgetNsPerItem(8000);
}
