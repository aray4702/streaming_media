// 7.5 Video codec negotiation (SDP offer/answer, RFC 3264, codec-specific
// rules from RFC 6184 / RFC 7741 / VP9 and AV1 RTP payload formats).
//
// The answerer keeps offered codecs it supports, in its own preference
// order (hardware-accelerated codecs first by default), reusing the
// offerer's payload types. Codec identity rules:
//   H.264  same profile (from profile-level-id) and packetization-mode;
//          the answered level is the lower of the two
//   VP9    same profile-id (default 0)
//   AV1    same profile (default 0)
//   VP8    name match
// RTCP feedback (nack, nack pli, ccm fir, transport-cc, goog-remb) is the
// intersection. RTX entries (apt=) are answered for each kept codec.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sm::video {

struct CodecCapability {
  std::string name;  // "VP8", "VP9", "H264", "AV1", "H265", "rtx"
  int payload_type = 0;
  std::map<std::string, std::string> params;  // fmtp
  std::vector<std::string> feedback;          // rtcp-fb
  bool hardware = false;
};

enum class H264Profile { kConstrainedBaseline, kBaseline, kMain, kHigh, kConstrainedHigh, kUnknown };

struct H264ProfileLevel {
  H264Profile profile = H264Profile::kUnknown;
  int level_idc = 0;  // e.g. 31 = level 3.1
};

std::optional<H264ProfileLevel> ParseH264ProfileLevelId(const std::string& hex6);
std::string H264ProfileLevelIdString(const H264ProfileLevel& pl);
bool IsSameCodec(const CodecCapability& a, const CodecCapability& b);

struct NegotiationOptions {
  bool prefer_hardware = true;
};

// Returns the answer (codec list with payload types from the offer).
std::vector<CodecCapability> NegotiateCodecs(const std::vector<CodecCapability>& offer,
                                             const std::vector<CodecCapability>& local,
                                             NegotiationOptions opts = {});

}  // namespace sm::video
