// SDP parse and write (#1062, C6 part 1): libwebrtc M157's api/webrtc_sdp.{h,cc} (compare/webrtc/reference/api/).
// SdpSerialize writes a session description (with its candidates) as libwebrtc writes it, line for line;
// SdpDeserialize reads one with libwebrtc's rules: the same accepted syntax, the same lines kept and ignored, and
// the same SdpParseError (the failing line and libwebrtc's reason) when it fails.
//
// RFC 4566 (SDP), RFC 8839 (ICE), RFC 5888 / RFC 8843 (groups, BUNDLE), RFC 8122 / RFC 4145 (fingerprint, setup),
// RFC 8285 (extmap), RFC 5576 (ssrc, ssrc-group), RFC 8830 (msid), RFC 8851/8853 (rid, simulcast),
// draft-ietf-mmusic-sctp-sdp (sctp-port, max-message-size).
//
// Differences from libwebrtc (also in docs/design/libwebrtc-parity.md, section 10.x):
// - A candidate's TLS fingerprint extension is checked but not kept.
// A candidate with a hostname (mDNS) address, and a c= line with a hostname, are kept with the name as their address
// (SetIP), as libwebrtc keeps them (#1152); the ICE agent resolves the name (sm/p2p/p2p_transport_channel.h).
// - No UMA histograms (WebRTC.PeerConnection.SdpBandwidth) and no logging of ignored lines.
// - A track id or a track without one gets 8 random characters from sm::tls::CreateRandomString, as libwebrtc's
//   CreateRandomString.
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "sm/pc/jsep.h"

namespace sm::pc {

// The SDP of `jdesc`, including its candidates.
std::string SdpSerialize(const SessionDescriptionInterface& jdesc);

// Parses `sdp` as a description of type `sdp_type`; null, with the failing line and reason in `error`, if it fails.
std::unique_ptr<SessionDescriptionInterface> SdpDeserialize(SdpType sdp_type, std::string_view sdp,
                                                            SdpParseError* error = nullptr);

}  // namespace sm::pc
