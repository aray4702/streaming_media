// Connection stats (issue #1059): see sm/pc/connection_stats.h.
#include "sm/pc/connection_stats.h"

#if SM_ENABLE_STATS
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "sm/p2p/candidate.h"
#include "sm/p2p/connection_info.h"
#include "sm/p2p/ice_transport_internal.h"
#include "sm/p2p/network.h"
#include "sm/p2p/socket_address.h"
#include "sm/tls/ssl_fingerprint.h"
#include "sm/tls/ssl_identity.h"
#endif

namespace sm::pc {

#if SM_ENABLE_STATS
namespace {

const char* IceRoleName(p2p::IceRole role) {
  switch (role) {
    case p2p::ICEROLE_CONTROLLING: return "controlling";
    case p2p::ICEROLE_CONTROLLED: return "controlled";
    default: return "unknown";
  }
}

const char* IceStateName(p2p::IceTransportState s) {
  switch (s) {
    case p2p::IceTransportState::kNew: return "new";
    case p2p::IceTransportState::kChecking: return "checking";
    case p2p::IceTransportState::kConnected: return "connected";
    case p2p::IceTransportState::kCompleted: return "completed";
    case p2p::IceTransportState::kFailed: return "failed";
    case p2p::IceTransportState::kDisconnected: return "disconnected";
    case p2p::IceTransportState::kClosed: return "closed";
  }
  return "new";
}

const char* GatheringStateName(p2p::IceGatheringState s) {
  switch (s) {
    case p2p::kIceGatheringNew: return "new";
    case p2p::kIceGatheringGathering: return "gathering";
    case p2p::kIceGatheringComplete: return "complete";
  }
  return "new";
}

// RTCStatsIceCandidatePairState.
const char* PairStateName(p2p::IceCandidatePairState s) {
  switch (s) {
    case p2p::IceCandidatePairState::WAITING: return "waiting";
    case p2p::IceCandidatePairState::IN_PROGRESS: return "in-progress";
    case p2p::IceCandidatePairState::SUCCEEDED: return "succeeded";
    case p2p::IceCandidatePairState::FAILED: return "failed";
  }
  return "waiting";
}

const char* DtlsStateName(p2p::DtlsTransportState s) {
  switch (s) {
    case p2p::DtlsTransportState::kNew: return "new";
    case p2p::DtlsTransportState::kConnecting: return "connecting";
    case p2p::DtlsTransportState::kConnected: return "connected";
    case p2p::DtlsTransportState::kClosed: return "closed";
    case p2p::DtlsTransportState::kFailed: return "failed";
  }
  return "new";
}

// rtc_base/ssl_stream_adapter.cc's SrtpCryptoSuiteToName.
std::string SrtpCryptoSuiteToName(int suite) {
  switch (suite) {
    case tls::kSrtpAes128CmSha1_80: return "SRTP_AES128_CM_HMAC_SHA1_80";
    case tls::kSrtpAes128CmSha1_32: return "SRTP_AES128_CM_HMAC_SHA1_32";
    case tls::kSrtpAeadAes128Gcm: return "SRTP_AEAD_AES_128_GCM";
    case tls::kSrtpAeadAes256Gcm: return "SRTP_AEAD_AES_256_GCM";
    default: return std::string();
  }
}

std::string Base64(std::span<const uint8_t> in) {
  static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((in.size() + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 2 < in.size(); i += 3) {
    const uint32_t v = (uint32_t{in[i]} << 16) | (uint32_t{in[i + 1]} << 8) | in[i + 2];
    out += kAlphabet[(v >> 18) & 63];
    out += kAlphabet[(v >> 12) & 63];
    out += kAlphabet[(v >> 6) & 63];
    out += kAlphabet[v & 63];
  }
  if (i < in.size()) {
    const bool two = i + 1 < in.size();
    const uint32_t v = (uint32_t{in[i]} << 16) | (two ? uint32_t{in[i + 1]} << 8 : 0u);
    out += kAlphabet[(v >> 18) & 63];
    out += kAlphabet[(v >> 12) & 63];
    out += two ? kAlphabet[(v >> 6) & 63] : '=';
    out += '=';
  }
  return out;
}

std::string IpString(const p2p::SocketAddress& a) {
  if (p2p::IpIsUnspec(a.ipaddr())) return a.hostname();
  return a.ipaddr().ToString();
}

// ProduceCertificateStats for one certificate: its id, or empty if its fingerprint can't be computed.
std::string AddCertificate(const tls::SslCertificate& cert, rtp::StatsReport& r) {
  std::string algorithm;
  if (!cert.GetSignatureDigestAlgorithm(algorithm)) return {};
  std::unique_ptr<tls::SslFingerprint> fp = tls::SslFingerprint::Create(algorithm, cert);
  if (!fp) return {};
  rtp::CertificateStats c;
  c.fingerprint = fp->GetRfc4572Fingerprint();
  c.id = "CF" + c.fingerprint;
  c.fingerprint_algorithm = fp->algorithm;
  c.base64_certificate = Base64(cert.der());
  const std::string id = c.id;
  if (std::none_of(r.certificates.begin(), r.certificates.end(), [&](const auto& e) { return e.id == id; })) {
    r.certificates.push_back(std::move(c));
  }
  return id;
}

// ProduceIceCandidateStats: the candidate's id, adding its entry if it isn't in the report yet.
std::string AddCandidate(const p2p::Candidate& cand, bool is_remote, const std::string& transport_id,
                         rtp::StatsReport& r) {
  const std::string id = "I" + cand.id();
  if (std::any_of(r.candidates.begin(), r.candidates.end(), [&](const auto& e) { return e.id == id; })) return id;
  rtp::IceCandidateStats c;
  c.id = id;
  c.transport_id = transport_id;
  c.is_remote = is_remote;
  c.address = IpString(cand.address());
  c.port = cand.address().port();
  c.protocol = cand.protocol();
  c.candidate_type = std::string(cand.type_name());
  c.priority = cand.priority();
  c.foundation = cand.foundation();
  if (!cand.related_address().IsNil()) {
    c.related_address = IpString(cand.related_address());
    c.related_port = cand.related_address().port();
  }
  c.username_fragment = cand.username();
  c.tcp_type = cand.tcptype();
  if (!is_remote) {
    c.network_type = p2p::AdapterTypeToString(cand.network_type());
    if (cand.is_relay()) c.relay_protocol = cand.relay_protocol();
    c.url = cand.url();
  }
  r.candidates.push_back(std::move(c));
  return id;
}

}  // namespace
#endif  // SM_ENABLE_STATS

ObsStatus GetConnectionStats(Timestamp now, p2p::DtlsTransport& transport, rtp::StatsReport* out) {
#if SM_ENABLE_STATS
  if (out == nullptr) return ObsStatus::kOk;
  rtp::StatsReport& r = *out;
  r.timestamp = now;
  p2p::IceTransportInternal* ice = transport.ice_transport();

  rtp::IceDtlsTransportStats t;
  t.id = "T" + transport.transport_name() + std::to_string(transport.component());

  p2p::IceTransportStats ice_stats;
  if (ice != nullptr && ice->GetStats(now, &ice_stats)) {
    t.bytes_sent = ice_stats.bytes_sent;
    t.packets_sent = ice_stats.packets_sent;
    t.bytes_received = ice_stats.bytes_received;
    t.packets_received = ice_stats.packets_received;
    t.selected_candidate_pair_changes = ice_stats.selected_candidate_pair_changes;
    t.ice_role = IceRoleName(ice_stats.ice_role);
    t.ice_local_username_fragment = ice_stats.ice_local_username_fragment;
    t.ice_state = IceStateName(ice_stats.ice_state);
    for (const p2p::ConnectionInfo& info : ice_stats.connection_infos) {
      rtp::IceCandidatePairStats p;
      p.transport_id = t.id;
      p.local_candidate_id = AddCandidate(info.local_candidate, false, t.id, r);
      p.remote_candidate_id = AddCandidate(info.remote_candidate, true, t.id, r);
      p.id = "CP" + info.local_candidate.id() + "_" + info.remote_candidate.id();
      p.state = PairStateName(info.state);
      p.priority = info.priority;
      p.nominated = info.nominated;
      // writable means "has received a STUN response", the W3C's responsesReceived > 0.
      p.writable = info.writable;
      p.selected = info.best_connection;
      p.packets_sent = info.sent_total_packets;
      p.packets_received = info.packets_received;
      p.bytes_sent = info.sent_total_bytes;
      p.bytes_received = info.recv_total_bytes;
      p.total_round_trip_time = static_cast<double>(info.total_round_trip_time_ms) / 1000.0;
      if (info.current_round_trip_time_ms) p.current_round_trip_time = *info.current_round_trip_time_ms / 1000.0;
      p.requests_received = info.recv_ping_requests;
      p.requests_sent = info.sent_ping_requests_before_first_response;
      p.responses_received = info.recv_ping_responses;
      p.responses_sent = info.sent_ping_responses;
      p.consent_requests_sent = info.sent_ping_requests_total - info.sent_ping_requests_before_first_response;
      p.packets_discarded_on_send = info.sent_discarded_packets;
      p.bytes_discarded_on_send = info.sent_discarded_bytes;
      p.last_packet_received_timestamp = info.last_data_received;
      p.last_packet_sent_timestamp = info.last_data_sent;
      if (p.selected) t.selected_candidate_pair_id = p.id;
      r.candidate_pairs.push_back(std::move(p));
    }
  } else {
    t.ice_role = IceRoleName(p2p::ICEROLE_UNKNOWN);
    t.ice_state = IceStateName(p2p::IceTransportState::kNew);
  }
  t.ice_gathering_state = GatheringStateName(ice ? ice->gathering_state() : p2p::kIceGatheringNew);

  t.dtls_state = DtlsStateName(transport.dtls_state());
  p2p::SslRole role;
  t.dtls_role = !transport.GetDtlsRole(&role) ? "unknown" : role == p2p::SslRole::kClient ? "client" : "server";
  int version = 0;
  if (transport.GetSslVersionBytes(&version)) {
    char hex[8];
    std::snprintf(hex, sizeof(hex), "%04X", static_cast<unsigned>(version));
    t.tls_version = hex;
  }
  t.dtls_cipher = transport.GetTlsCipherSuiteName();
  int srtp = 0;
  if (transport.GetSrtpCryptoSuite(&srtp)) {
    std::string name = SrtpCryptoSuiteToName(srtp);
    if (!name.empty()) t.srtp_cipher = std::move(name);
  }
  if (const tls::SslIdentity* local = transport.local_certificate()) {
    t.local_certificate_id = AddCertificate(local->certificate(), r);
  }
  if (const tls::SslCertificate* remote = transport.GetRemoteCertificate()) {
    t.remote_certificate_id = AddCertificate(*remote, r);
  }
  r.ice_transports.push_back(std::move(t));
  return ObsStatus::kOk;
#else
  (void)now;
  (void)transport;
  (void)out;
  return ObsStatus::kNotSupported;
#endif
}

}  // namespace sm::pc
