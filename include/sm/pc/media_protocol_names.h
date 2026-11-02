// The m= line's <proto> (#1062, C6 part 1): libwebrtc M157's pc/media_protocol_names.{h,cc}
// (compare/webrtc/reference/pc/).
#pragma once

#include <string_view>

namespace sm::pc {

inline constexpr char kMediaProtocolRtpPrefix[] = "RTP/";
inline constexpr char kMediaProtocolSctp[] = "SCTP";
inline constexpr char kMediaProtocolUdpDtlsSctp[] = "UDP/DTLS/SCTP";
inline constexpr char kMediaProtocolDtlsSctp[] = "DTLS/SCTP";
inline constexpr char kMediaProtocolTcpDtlsSctp[] = "TCP/DTLS/SCTP";
inline constexpr char kMediaProtocolDtlsSavpf[] = "UDP/TLS/RTP/SAVPF";
inline constexpr char kMediaProtocolSavpf[] = "RTP/SAVPF";
inline constexpr char kMediaProtocolAvpf[] = "RTP/AVPF";

// DTLS/SCTP, UDP/DTLS/SCTP or TCP/DTLS/SCTP.
bool IsDtlsSctp(std::string_view protocol);
bool IsPlainSctp(std::string_view protocol);
bool IsSctpProtocol(std::string_view protocol);
// Contains "RTP/" at the start or after a non-letter; an empty protocol counts as RTP.
bool IsRtpProtocol(std::string_view protocol);
bool IsDtlsRtp(std::string_view protocol);
bool IsPlainRtp(std::string_view protocol);

}  // namespace sm::pc
