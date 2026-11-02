// DTLS over ICE (#1058, C10 part 3): p2p::DtlsTransport's receive path on
// arbitrary datagrams from the ICE transport, before and after a handshake,
// and the packet classification it uses. libwebrtc M157 has no DTLS fuzzer
// (compare/webrtc/reference/test/fuzzers/), so this feeds the functions
// themselves:
//
// - p2p/dtls/dtls_utils.cc: IsDtlsPacket, IsDtlsClientHelloPacket,
//   IsDtlsHandshakePacket, ComputeDtlsPacketHash and PacketStash, on every
//   datagram;
// - p2p/dtls/dtls_transport.cc: DtlsTransportInternalImpl::OnReadPacket and
//   HandleDtlsPacket (records split out of the datagram, ClientHellos cached
//   before the role is known, SRTP passed through), over sm::tls.
//
// The input format is dtls_record_fuzzer.cc's, so the two share a corpus
// (corpus/dtls_record/): a flags byte, then records of a control byte (the
// milliseconds to run after the datagram), a big-endian 16-bit length and
// that many bytes. Flags: bit 0 the role of the transport under test (0
// server, 1 client); bit 1 a completed handshake with a peer over the fake
// ICE network first (tests/unit/p2p_dtls/fake_ice_transport.h, libwebrtc's
// dtls_transport_unittest.cc harness), else ICE is writable with the remote
// fingerprint set and no peer, so the transport is mid-handshake; bit 2
// DTLS 1.3 as the highest version (else 1.2, DtlsTransport's default); bit 3
// no local certificate (DTLS off: packets pass straight through).
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

#include "fake_ice_transport.h"
#include "fuzz_data_reader.h"
#include "sm/core/units.h"
#include "sm/p2p/dtls_transport.h"
#include "sm/p2p/dtls_utils.h"
#include "sm/tls/message_digest.h"
#include "sm/tls/ssl_fingerprint.h"
#include "sm/tls/ssl_identity.h"

namespace {

using namespace sm;
using namespace sm::p2p;
using sm::p2p::test::FakeIceTransport;
using sm::p2p::test::FakeNetwork;

void Check(bool ok) {
  if (!ok) std::abort();
}

std::shared_ptr<const tls::SslIdentity> Identity(int i) {
  static const std::shared_ptr<const tls::SslIdentity> ids[2] = {tls::SslIdentity::Create("P1"),
                                                                 tls::SslIdentity::Create("P2")};
  Check(ids[i] != nullptr);
  return ids[i];
}

struct Side {
  std::unique_ptr<FakeIceTransport> ice;
  std::unique_ptr<DtlsTransport> dtls;
  Side(const char* name, FakeNetwork* net, int version) {
    ice = std::make_unique<FakeIceTransport>(name, 1, net);
    dtls = std::make_unique<DtlsTransport>(ice.get(), tls::DtlsConfig{}.srtp_crypto_suites, version);
  }
};

void SetRemote(DtlsTransport& dtls, const tls::SslIdentity& peer, Timestamp now) {
  auto fp = tls::SslFingerprint::Create(tls::kDigestSha256, peer);
  Check(fp != nullptr);
  Check(dtls.SetRemoteParameters(fp->algorithm, fp->digest, std::nullopt, now));
}

void Classify(std::span<const uint8_t> d, PacketStash& stash) {
  const bool dtls = IsDtlsPacket(d);
  const bool hello = IsDtlsClientHelloPacket(d);
  const bool hs = IsDtlsHandshakePacket(d);
  Check(!hello || (dtls && hs));
  Check(!hs || dtls);
  const uint32_t hash = ComputeDtlsPacketHash(d);
  if (!stash.AddIfUnique(d)) Check(stash.size() > 0);
  stash.Prune(4u);
  if (!stash.empty()) (void)stash.GetNext();
  if (hash & 1) stash.Prune(std::unordered_set<uint32_t>{hash});
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  sm::fuzz::FuzzDataReader in(std::span<const uint8_t>(data, size));
  const uint8_t flags = in.ReadU8();
  const bool c1_client = flags & 1;
  const bool after = flags & 2;
  const int version = (flags & 4) ? tls::kDtls13 : tls::kDtls12;
  const bool use_dtls = !(flags & 8);

  FakeNetwork net;
  Side c1("P1", &net, version);
  Side c2("P2", &net, version);
  auto step = [&] {
    net.now = net.now + TimeDelta::Millis(1);
    net.DeliverDue();
    for (Side* s : {&c1, &c2}) {
      if (auto due = s->dtls->NextTimeout(); due && *due <= net.now) s->dtls->OnTimeout(net.now);
    }
    net.DeliverDue();
  };
  if (use_dtls) {
    Check(c1.dtls->SetLocalCertificate(Identity(0)));
    Check(c2.dtls->SetLocalCertificate(Identity(1)));
  }
  c1.dtls->SetDtlsRole(c1_client ? SslRole::kClient : SslRole::kServer);
  c2.dtls->SetDtlsRole(c1_client ? SslRole::kServer : SslRole::kClient);
  if (use_dtls) {
    SetRemote(*c1.dtls, *Identity(1), net.now);
    SetRemote(*c2.dtls, *Identity(0), net.now);
  } else {
    Check(c1.dtls->SetRemoteParameters("", {}, std::nullopt, net.now));
  }
  if (after) {
    c1.ice->SetDestination(c2.ice.get());
    for (int i = 0; i < 2000 && !(c1.dtls->writable() && c2.dtls->writable()); ++i) step();
    Check(c1.dtls->writable() && c2.dtls->writable());
  } else {
    // Writable toward nowhere: DTLS starts, its flights go nowhere, and the
    // input plays the peer.
    c1.ice->SetWritable(true);
  }
  c1.ice->SetReceiving(true);

  PacketStash stash;
  for (int i = 0; i < 64 && in.BytesLeft() > 0; ++i) {
    const uint8_t ms = in.ReadU8();
    const auto datagram = in.ReadLengthPrefixed();
    Classify(datagram, stash);
    c1.ice->Receive(datagram);
    for (int k = 0; k < ms; ++k) step();
  }
  // A packet of each kind the other way, whatever state the input left.
  const std::vector<uint8_t> rtp = {0x80, 0x00, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0xaa};
  (void)c1.dtls->SendPacket(net.now, rtp, {}, use_dtls ? kPfSrtpBypass : kPfNormal);
  (void)c1.dtls->SendPacket(net.now, rtp, {}, kPfNormal);
  for (int k = 0; k < 20; ++k) step();
  (void)c1.dtls->dtls_state();
  return 0;
}
