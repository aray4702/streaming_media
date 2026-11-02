// sm/pc/media_protocol_names.h: libwebrtc M157's pc/media_protocol_names.cc (compare/webrtc/reference/pc/), #1062.
#include "sm/pc/media_protocol_names.h"

#include <cctype>
#include <string>

namespace sm::pc {
namespace {

constexpr char kMediaProtocolSavp[] = "RTP/SAVP";
constexpr char kMediaProtocolAvp[] = "RTP/AVP";
constexpr char kMediaProtocolTcpTlsSavpf[] = "TCP/TLS/RTP/SAVPF";
constexpr char kMediaProtocolUdpTlsSavpf[] = "UDP/TLS/RTP/SAVPF";
constexpr char kMediaProtocolTcpTlsSavp[] = "TCP/TLS/RTP/SAVP";
constexpr char kMediaProtocolUdpTlsSavp[] = "UDP/TLS/RTP/SAVP";

}  // namespace

bool IsDtlsSctp(std::string_view protocol) {
  return protocol == kMediaProtocolDtlsSctp || protocol == kMediaProtocolUdpDtlsSctp ||
         protocol == kMediaProtocolTcpDtlsSctp;
}

bool IsPlainSctp(std::string_view protocol) { return protocol == kMediaProtocolSctp; }

bool IsSctpProtocol(std::string_view protocol) { return IsPlainSctp(protocol) || IsDtlsSctp(protocol); }

bool IsRtpProtocol(std::string_view protocol) {
  if (protocol.empty()) return true;
  size_t pos = protocol.find(kMediaProtocolRtpPrefix);
  if (pos == std::string_view::npos) return false;
  // RTP must be at the beginning of a string or not preceded by alpha
  if (pos == 0 || !std::isalpha(static_cast<unsigned char>(protocol[pos - 1]))) return true;
  return false;
}

bool IsDtlsRtp(std::string_view protocol) {
  return protocol == kMediaProtocolDtlsSavpf || protocol == kMediaProtocolTcpTlsSavpf ||
         protocol == kMediaProtocolUdpTlsSavpf || protocol == kMediaProtocolUdpTlsSavp ||
         protocol == kMediaProtocolTcpTlsSavp;
}

bool IsPlainRtp(std::string_view protocol) {
  return protocol == kMediaProtocolSavpf || protocol == kMediaProtocolAvpf || protocol == kMediaProtocolSavp ||
         protocol == kMediaProtocolAvp;
}

}  // namespace sm::pc
