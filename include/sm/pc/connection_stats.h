// Connection stats for the p2p layer (issue #1059, requirement C8): the getStats entries libwebrtc M157's
// RTCStatsCollector produces for an ICE + DTLS transport, from what it reads there:
// - P2PTransportChannel::GetStats (IceTransportStats, p2p/base/ice_transport_internal.h): one candidate-pair entry
//   per connection (ConnectionInfo, p2p/base/connection_info.h) with its local and remote candidates, the selected
//   pair, the transport's bytes and packets, ICE role, local ufrag and state;
// - the DTLS transport (pc/dtls_transport.cc's UpdateInformation, pc/jsep_transport.cc's GetStats): DTLS state and
//   role, and once connected the TLS version, the cipher suite's standard name (GetTlsCipherSuiteName) and the SRTP
//   profile's name (SrtpCryptoSuiteToName, rtc_base/ssl_stream_adapter.cc); the local and the peer's certificate as
//   certificate entries (fingerprint with the certificate's signature digest algorithm, and the DER in base64).
//
// pc/rtc_stats_collector.cc and api/stats/rtcstats_objects.h are not in compare/webrtc/reference/, so the ids and
// field names follow the W3C webrtc-stats dictionaries RTCStatsCollector fills, as sm/rtp/stats_report.h does.
// Differences from libwebrtc:
// - Candidates come from the candidate pairs only: IceTransportStats has no candidate_stats_list (local candidates
//   that are in no pair yet, GetCandidateStatsFromReadyPorts, are not ported), so such a candidate has no entry.
// - The candidate pair's available_outgoing_bitrate (Call's stats) and the candidate's network adapter type and vpn
//   flag (from the Network) are not filled; the transport's rtcp_transport_stats_id is not (RTCP mux only).
// - The key-exchange group (tlsGroup) is not reported: sm::tls doesn't expose it (sm/p2p/dtls_transport.h).
// - ice_gathering_state is RTCIceTransport's gatheringState, which libwebrtc's transport stats don't carry.
//
// Like Session::GetStats, it is computed only when the app calls it (the runtime switch: nothing is kept for it in
// between), and a build with SM_ENABLE_STATS=OFF compiles none of it: GetConnectionStats returns
// ObsStatus::kNotSupported and leaves `out` untouched (sm/core/observability.h).
#pragma once

#include "sm/core/observability.h"
#include "sm/core/units.h"
#include "sm/p2p/dtls_transport.h"
#include "sm/rtp/stats_report.h"

namespace sm::pc {

// Appends `transport`'s stats (its ICE transport's candidate pairs and candidates, its DTLS state and certificates)
// to `out`: one transport entry, its candidate pairs, and each candidate and certificate not already in `out`. Sets
// `out->timestamp` to `now`. GetStats on the ICE transport takes `now` (the pairs' RTTs and rates).
ObsStatus GetConnectionStats(Timestamp now, p2p::DtlsTransport& transport, rtp::StatsReport* out);

}  // namespace sm::pc
