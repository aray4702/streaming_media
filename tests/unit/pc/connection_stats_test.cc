// Connection stats (issue #1059, C8): pc::GetConnectionStats on ICE + DTLS connections over the emulated network
// (#985), two P2PTransportChannels with a DTLS transport each, checked against the connection: the selected pair and
// its candidates' types and addresses, the bytes the app sent, the RTT against the link's, the ICE and DTLS state,
// the negotiated TLS and SRTP suites and both certificates' fingerprints. With SM_ENABLE_STATS=OFF the call returns
// kNotSupported.
#include <algorithm>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../p2p/test_crypto.h"
#include "sm/core/units.h"
#include "sm/p2p/basic_port_allocator.h"
#include "sm/p2p/dtls_transport.h"
#include "sm/p2p/network_manager.h"
#include "sm/p2p/p2p_transport_channel.h"
#include "sm/pc/connection_stats.h"
#include "sm/sim/emulated_sockets.h"
#include "sm/sim/stun_server.h"
#include "sm/tls/message_digest.h"
#include "sm/tls/ssl_fingerprint.h"
#include "sm/tls/ssl_identity.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::p2p;

namespace {

IpAddress Ip(std::string_view s) { return *IpAddress::FromString(s); }

const IpAddress kPublicIp[2] = {Ip("11.11.11.11"), Ip("22.22.22.22")};
const IpAddress kPrivateIp = Ip("192.168.1.11");
const IpAddress kNatIp = Ip("77.77.77.77");
const SocketAddress kStunAddr(Ip("99.99.99.1"), 3478);
const IceParameters kIce[2] = {{"UF00", "TESTICEPWD00000000000000", false}, {"UF01", "TESTICEPWD00000000000001", false}};

// Each host's access link: 10 ms each way, so 20 ms one way between the two hosts and a 40 ms RTT.
constexpr int kLinkDelayMs = 10;
sim::LinkConfig Link(int delay_ms) {
  sim::LinkConfig c;
  c.base_delay = TimeDelta::Millis(delay_ms);
  return c;
}

// An RTP packet (already SRTP as far as the DTLS transport is concerned: it goes out with kPfSrtpBypass).
std::vector<uint8_t> RtpPacket(uint16_t seq, size_t size) {
  std::vector<uint8_t> p = {0x80, 0x00, static_cast<uint8_t>(seq >> 8), static_cast<uint8_t>(seq), 0, 0, 0, 0, 0, 0, 0, 1};
  p.resize(size, 0xAB);
  return p;
}

struct Endpoint {
  std::unique_ptr<sim::EmulatedSocketServer> host;
  StaticNetworkManager network_manager;
  std::unique_ptr<BasicPortAllocator> allocator;
  std::unique_ptr<P2PTransportChannel> ch;
  std::unique_ptr<DtlsTransport> dtls;
  std::shared_ptr<const tls::SslIdentity> cert;
  size_t received = 0;
};

struct Fixture {
  sim::ByteNetwork net;
  std::unique_ptr<sim::EmulatedSocketServer> stun_host;
  std::unique_ptr<sim::StunServer> stun;
  Endpoint ep[2];
  std::deque<Candidate> signaling[2];
  Timestamp now = Timestamp::Seconds(10);

  // Endpoint 0 is public, or behind a port-restricted NAT with `nat0`; endpoint 1 is public. `dtls` adds
  // certificates, roles (0 server, 1 client) and fingerprints; without it the DTLS transports pass packets through.
  explicit Fixture(bool nat0 = false, bool dtls = true) {
    testing::InstallTestCrypto();
    stun_host = std::make_unique<sim::EmulatedSocketServer>(
        net, net.AddHost(kStunAddr.ip(), sim::ByteNetwork::kPublicRealm, Link(5), Link(5)));
    stun = std::make_unique<sim::StunServer>(stun_host->CreateUdpSocket(kStunAddr, 0, 0));
    for (int i = 0; i < 2; ++i) {
      int realm = sim::ByteNetwork::kPublicRealm;
      IpAddress ip = kPublicIp[i];
      if (i == 0 && nat0) {
        sim::NatConfig cfg;
        cfg.type = sim::NatType::kPortRestricted;
        cfg.external_ip = kNatIp.v4();
        realm = net.NatRealm(net.AddNat(cfg));
        ip = kPrivateIp;
      }
      Endpoint& e = ep[i];
      e.host = std::make_unique<sim::EmulatedSocketServer>(
          net, net.AddHost(ip.v4(), realm, Link(kLinkDelayMs), Link(kLinkDelayMs)));
      e.network_manager.AddInterface(ip);
      e.allocator = std::make_unique<BasicPortAllocator>(&e.network_manager, e.host.get());
      e.allocator->SetConfiguration(now, ServerAddresses{kStunAddr}, {}, 0, NO_PRUNE);
      e.allocator->set_flags(PORTALLOCATOR_DISABLE_RELAY | PORTALLOCATOR_DISABLE_TCP);
      e.allocator->set_step_delay(kMinimumStepDelay);
      e.ch = std::make_unique<P2PTransportChannel>(now, "0", ICE_CANDIDATE_COMPONENT_RTP, e.allocator.get());
      e.ch->SubscribeCandidateGathered(this, [this, i](IceTransportInternal*, const Candidate& c) {
        signaling[1 - i].push_back(c);
      });
      e.ch->SetIceParameters(kIce[i]);
      e.ch->SetRemoteIceParameters(kIce[1 - i]);
      e.ch->SetIceRole(i == 0 ? ICEROLE_CONTROLLING : ICEROLE_CONTROLLED);
      e.dtls = std::make_unique<DtlsTransport>(e.ch.get());
      e.dtls->RegisterReceivedPacketCallback(this, [&e](PacketTransportInternal*, const ReceivedPacket&) { ++e.received; });
      if (dtls) {
        e.cert = tls::SslIdentity::Create(i == 0 ? "stats-a" : "stats-b");
        SM_EXPECT_TRUE(e.dtls->SetLocalCertificate(e.cert));
        e.dtls->SetDtlsRole(i == 0 ? SslRole::kServer : SslRole::kClient);
      }
    }
    if (dtls) {
      for (int i = 0; i < 2; ++i) {
        auto fp = tls::SslFingerprint::Create(tls::kDigestSha256, *ep[1 - i].cert);
        SM_EXPECT_TRUE(ep[i].dtls->SetRemoteParameters(fp->algorithm, fp->digest, std::nullopt, now));
      }
    }
    for (Endpoint& e : ep) e.ch->MaybeStartGathering(now);
  }
  ~Fixture() {
    for (Endpoint& e : ep) {
      e.dtls.reset();
      e.ch.reset();
    }
  }

  Timestamp NextEvent() const {
    Timestamp t = net.NextEvent();
    for (const Endpoint& e : ep) {
      t = std::min(t, e.ch->NextProcessTime());
      if (auto d = e.dtls->NextTimeout()) t = std::min(t, *d);
    }
    if (!signaling[0].empty() || !signaling[1].empty()) t = std::min(t, now);
    return t;
  }
  void Step(Timestamp t) {
    now = std::max(now, t);
    for (Endpoint& e : ep) {
      e.ch->Process(now);
      if (auto d = e.dtls->NextTimeout(); d && *d <= now) e.dtls->OnTimeout(now);
    }
    for (Endpoint& e : ep) e.host->Deliver(now);
    stun_host->Deliver(now);
    for (int i = 0; i < 2; ++i) {
      while (!signaling[i].empty()) {
        Candidate c = signaling[i].front();
        signaling[i].pop_front();
        c.set_username("");
        c.set_password("");
        ep[i].ch->AddRemoteCandidate(now, c);
      }
    }
  }
  template <class F>
  bool RunUntil(F stop, TimeDelta limit) {
    const Timestamp until = now + limit;
    for (int guard = 0; guard < 2000000; ++guard) {
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
  bool Writable() const { return ep[0].dtls->writable() && ep[1].dtls->writable(); }
  bool Completed() const {
    return ep[0].ch->GetState() == IceTransportStateInternal::kCompleted &&
           ep[1].ch->GetState() == IceTransportStateInternal::kCompleted;
  }
};

#if SM_ENABLE_STATS
const rtp::IceCandidateStats* FindCandidate(const rtp::StatsReport& r, const std::string& id) {
  for (const auto& c : r.candidates) {
    if (c.id == id) return &c;
  }
  return nullptr;
}
const rtp::IceCandidatePairStats* Selected(const rtp::StatsReport& r) {
  if (r.ice_transports.empty()) return nullptr;
  for (const auto& p : r.candidate_pairs) {
    if (p.id == r.ice_transports[0].selected_candidate_pair_id) return &p;
  }
  return nullptr;
}
const rtp::CertificateStats* FindCertificate(const rtp::StatsReport& r, const std::string& id) {
  for (const auto& c : r.certificates) {
    if (c.id == id) return &c;
  }
  return nullptr;
}
#endif

}  // namespace

#if SM_ENABLE_STATS

// Before anything connects: the transport entry with ICE and DTLS "new", no pairs, the local certificate.
SM_TEST(PcConnectionStats, BeforeConnecting) {
  Fixture f;
  rtp::StatsReport r;
  SM_ASSERT_TRUE(pc::GetConnectionStats(f.now, *f.ep[0].dtls, &r) == ObsStatus::kOk);
  SM_EXPECT_EQ(r.timestamp.ms(), f.now.ms());
  SM_ASSERT_EQ(r.ice_transports.size(), size_t{1});
  const rtp::IceDtlsTransportStats& t = r.ice_transports[0];
  SM_EXPECT_EQ(t.id, std::string("T01"));
  SM_EXPECT_EQ(t.dtls_state, std::string("new"));
  SM_EXPECT_EQ(t.dtls_role, std::string("server"));
  SM_EXPECT_EQ(t.ice_role, std::string("controlling"));
  SM_EXPECT_EQ(t.ice_state, std::string("new"));
  SM_EXPECT_EQ(t.ice_gathering_state, std::string("gathering"));
  SM_EXPECT_EQ(t.ice_local_username_fragment, std::string("UF00"));
  SM_EXPECT_FALSE(t.tls_version.has_value() || t.dtls_cipher.has_value() || t.srtp_cipher.has_value());
  SM_EXPECT_TRUE(t.selected_candidate_pair_id.empty());
  SM_EXPECT_TRUE(r.candidate_pairs.empty());
  SM_EXPECT_TRUE(t.remote_certificate_id.empty());
  SM_ASSERT_EQ(r.certificates.size(), size_t{1});
  SM_EXPECT_EQ(t.local_certificate_id, r.certificates[0].id);
}

// ICE + DTLS-SRTP between two public hosts: every field checked against the connection.
SM_TEST(PcConnectionStats, IceDtlsConnection) {
  Fixture f;
  SM_ASSERT_TRUE(f.RunUntil([&] { return f.Writable() && f.Completed(); }, TimeDelta::Seconds(10)));
  // The app's media: 50 RTP packets of 200 bytes from 0 to 1, 20 ms apart.
  rtp::StatsReport before[2];
  for (int i = 0; i < 2; ++i) SM_ASSERT_TRUE(pc::GetConnectionStats(f.now, *f.ep[i].dtls, &before[i]) == ObsStatus::kOk);
  const size_t received_before = f.ep[1].received;
  constexpr int kPackets = 50;
  constexpr size_t kSize = 200;
  for (int n = 0; n < kPackets; ++n) {
    const std::vector<uint8_t> p = RtpPacket(static_cast<uint16_t>(n), kSize);
    SM_ASSERT_EQ(f.ep[0].dtls->SendPacket(f.now, p, PacketOptions{}, kPfSrtpBypass), static_cast<int>(kSize));
    f.RunUntil([] { return false; }, TimeDelta::Millis(20));
  }
  f.RunUntil([] { return false; }, TimeDelta::Millis(200));
  SM_EXPECT_EQ(f.ep[1].received - received_before, size_t{kPackets});

  rtp::StatsReport r[2];
  for (int i = 0; i < 2; ++i) SM_ASSERT_TRUE(pc::GetConnectionStats(f.now, *f.ep[i].dtls, &r[i]) == ObsStatus::kOk);
  for (int i = 0; i < 2; ++i) {
    SM_ASSERT_EQ(r[i].ice_transports.size(), size_t{1});
    const rtp::IceDtlsTransportStats& t = r[i].ice_transports[0];
    SM_EXPECT_EQ(t.dtls_state, std::string("connected"));
    SM_EXPECT_EQ(t.dtls_role, std::string(i == 0 ? "server" : "client"));
    SM_EXPECT_EQ(t.ice_role, std::string(i == 0 ? "controlling" : "controlled"));
    // RTCIceTransportState "completed" also needs gathering complete (ComputeIceTransportState).
    SM_EXPECT_EQ(t.ice_state, std::string("connected"));
    // DTLS 1.2 (DtlsTransport's default maximum), an ECDHE-ECDSA suite for the ECDSA certificates, and the default
    // first SRTP profile.
    SM_EXPECT_TRUE(t.tls_version == std::optional<std::string>("FEFD"));
    SM_ASSERT_TRUE(t.dtls_cipher.has_value());
    SM_EXPECT_EQ(t.dtls_cipher->rfind("TLS_ECDHE_ECDSA_WITH_", 0), size_t{0});
    SM_EXPECT_TRUE(t.srtp_cipher == std::optional<std::string>("SRTP_AES128_CM_HMAC_SHA1_80"));
    SM_EXPECT_EQ(t.selected_candidate_pair_changes, uint32_t{1});
    // Certificates: ours and the peer's, each with the fingerprint SDP carries.
    const rtp::CertificateStats* local = FindCertificate(r[i], t.local_certificate_id);
    const rtp::CertificateStats* remote = FindCertificate(r[i], t.remote_certificate_id);
    SM_ASSERT_TRUE(local != nullptr && remote != nullptr);
    auto fp_local = tls::SslFingerprint::Create(tls::kDigestSha256, *f.ep[i].cert);
    auto fp_remote = tls::SslFingerprint::Create(tls::kDigestSha256, *f.ep[1 - i].cert);
    SM_EXPECT_EQ(local->fingerprint, fp_local->GetRfc4572Fingerprint());
    SM_EXPECT_EQ(remote->fingerprint, fp_remote->GetRfc4572Fingerprint());
    SM_EXPECT_EQ(remote->fingerprint_algorithm, std::string("sha-256"));
    SM_EXPECT_EQ(remote->id, "CF" + remote->fingerprint);
    SM_EXPECT_GT(remote->base64_certificate.size(), size_t{100});
    // The selected pair: host to host, between the two public addresses, nominated and writable.
    const rtp::IceCandidatePairStats* sel = Selected(r[i]);
    SM_ASSERT_TRUE(sel != nullptr);
    SM_EXPECT_TRUE(sel->selected && sel->writable && sel->nominated);
    SM_EXPECT_EQ(sel->state, std::string("succeeded"));
    SM_EXPECT_EQ(sel->transport_id, t.id);
    const rtp::IceCandidateStats* lc = FindCandidate(r[i], sel->local_candidate_id);
    const rtp::IceCandidateStats* rc = FindCandidate(r[i], sel->remote_candidate_id);
    SM_ASSERT_TRUE(lc != nullptr && rc != nullptr);
    SM_EXPECT_FALSE(lc->is_remote);
    SM_EXPECT_TRUE(rc->is_remote);
    SM_EXPECT_EQ(lc->candidate_type, std::string("host"));
    SM_EXPECT_EQ(rc->candidate_type, std::string("host"));
    SM_EXPECT_EQ(lc->protocol, std::string("udp"));
    SM_EXPECT_EQ(lc->address, kPublicIp[i].ToString());
    SM_EXPECT_EQ(rc->address, kPublicIp[1 - i].ToString());
    SM_EXPECT_EQ(lc->port, f.ep[i].ch->selected_connection()->local_candidate().address().port());
    SM_EXPECT_EQ(lc->username_fragment, std::string(kIce[i].ufrag));
    // The RTT: within the link's 40 ms, plus the check's processing (none here) and the 1 ms rounding.
    SM_ASSERT_TRUE(sel->current_round_trip_time.has_value());
    SM_EXPECT_GE(*sel->current_round_trip_time, 0.040);
    SM_EXPECT_LE(*sel->current_round_trip_time, 0.042);
    SM_EXPECT_GT(sel->responses_received, uint64_t{0});
    SM_EXPECT_GE(sel->total_round_trip_time, 0.040 * static_cast<double>(sel->responses_received));
    SM_EXPECT_LE(sel->total_round_trip_time, 0.042 * static_cast<double>(sel->responses_received));
    SM_EXPECT_GT(sel->requests_sent + sel->consent_requests_sent, uint64_t{0});
    SM_EXPECT_GT(sel->requests_received, uint64_t{0});
    SM_EXPECT_GT(sel->responses_sent, uint64_t{0});
    SM_EXPECT_TRUE(sel->last_packet_received_timestamp.has_value());
    // Every pair's candidates are in the report, each candidate once.
    for (const auto& p : r[i].candidate_pairs) {
      SM_EXPECT_TRUE(FindCandidate(r[i], p.local_candidate_id) != nullptr);
      SM_EXPECT_TRUE(FindCandidate(r[i], p.remote_candidate_id) != nullptr);
    }
    for (size_t a = 0; a < r[i].candidates.size(); ++a) {
      for (size_t b = a + 1; b < r[i].candidates.size(); ++b) SM_EXPECT_NE(r[i].candidates[a].id, r[i].candidates[b].id);
    }
  }
  // Bytes: what 0 sent is what 1 received, on the transport and on the selected pair, and the media is all of it.
  const auto& t0 = r[0].ice_transports[0];
  const auto& t1 = r[1].ice_transports[0];
  SM_EXPECT_EQ(t0.bytes_sent - before[0].ice_transports[0].bytes_sent, uint64_t{kPackets * kSize});
  SM_EXPECT_EQ(t0.packets_sent - before[0].ice_transports[0].packets_sent, uint64_t{kPackets});
  SM_EXPECT_EQ(t1.bytes_received - before[1].ice_transports[0].bytes_received, uint64_t{kPackets * kSize});
  SM_EXPECT_EQ(t1.packets_received - before[1].ice_transports[0].packets_received, uint64_t{kPackets});
  SM_EXPECT_EQ(t0.bytes_sent, t1.bytes_received);
  SM_EXPECT_EQ(t1.bytes_sent, t0.bytes_received);
  SM_EXPECT_EQ(Selected(r[0])->bytes_sent, Selected(r[1])->bytes_received);
  SM_EXPECT_EQ(Selected(r[0])->packets_sent, Selected(r[1])->packets_received);
  SM_EXPECT_GE(Selected(r[0])->bytes_sent, uint64_t{kPackets * kSize});
  SM_EXPECT_TRUE(Selected(r[0])->last_packet_sent_timestamp.has_value());
}

// Through a port-restricted NAT, without DTLS: the NATed side's selected local candidate is server-reflexive or the
// host one behind the NAT, the public side sees the NAT's address as a peer-reflexive or srflx remote candidate, and
// the DTLS transport reports passthrough ("new", no certificates).
SM_TEST(PcConnectionStats, ThroughNatWithoutDtls) {
  Fixture f(/*nat0=*/true, /*dtls=*/false);
  // The public side keeps both the srflx and the prflx pair active, so it stays "connecting" (kConnecting), not
  // completed: wait for both ends' selected pairs.
  SM_ASSERT_TRUE(f.RunUntil(
      [&] { return f.Writable() && f.ep[0].ch->selected_connection() && f.ep[1].ch->selected_connection(); },
      TimeDelta::Seconds(10)));
  f.RunUntil([] { return false; }, TimeDelta::Seconds(1));
  rtp::StatsReport r1;
  SM_ASSERT_TRUE(pc::GetConnectionStats(f.now, *f.ep[1].dtls, &r1) == ObsStatus::kOk);
  const rtp::IceCandidatePairStats* sel = Selected(r1);
  SM_ASSERT_TRUE(sel != nullptr);
  const rtp::IceCandidateStats* rc = FindCandidate(r1, sel->remote_candidate_id);
  SM_ASSERT_TRUE(rc != nullptr);
  SM_EXPECT_TRUE(rc->candidate_type == "srflx" || rc->candidate_type == "prflx");
  // A prflx remote candidate's address is sanitized away (P2PTransportChannel::SanitizeRemoteCandidate).
  if (rc->candidate_type == "srflx") SM_EXPECT_EQ(rc->address, kNatIp.ToString());
  if (rc->candidate_type == "prflx") SM_EXPECT_TRUE(rc->address.empty());
  SM_EXPECT_EQ(rc->port, f.ep[1].ch->selected_connection()->remote_candidate().address().port());
  const auto& t = r1.ice_transports[0];
  SM_EXPECT_EQ(t.dtls_state, std::string("new"));
  SM_EXPECT_EQ(t.dtls_role, std::string("unknown"));
  SM_EXPECT_TRUE(t.local_certificate_id.empty() && t.remote_certificate_id.empty());
  SM_EXPECT_TRUE(r1.certificates.empty());
  // The NATed side gathered a srflx candidate with the NAT's address and the STUN server's URL.
  rtp::StatsReport r0;
  SM_ASSERT_TRUE(pc::GetConnectionStats(f.now, *f.ep[0].dtls, &r0) == ObsStatus::kOk);
  bool srflx = false;
  for (const auto& c : r0.candidates) {
    if (c.is_remote || c.candidate_type != "srflx") continue;
    srflx = true;
    SM_EXPECT_EQ(c.address, kNatIp.ToString());
    SM_EXPECT_EQ(c.related_address, kPrivateIp.ToString());
  }
  SM_EXPECT_TRUE(srflx);
}

#else  // SM_ENABLE_STATS

SM_TEST(PcConnectionStats, NotSupportedWhenCompiledOut) {
  Fixture f;
  rtp::StatsReport r;
  r.timestamp = Timestamp::Millis(1);
  SM_EXPECT_TRUE(pc::GetConnectionStats(f.now, *f.ep[0].dtls, &r) == ObsStatus::kNotSupported);
  SM_EXPECT_EQ(r.timestamp.ms(), 1);
  SM_EXPECT_TRUE(r.ice_transports.empty());
}

#endif  // SM_ENABLE_STATS
