// A codec as SDP describes it (#1062, C6 part 1): libwebrtc M157's media/base/codec.{h,cc} (FeedbackParam,
// FeedbackParams, Codec) and the constants of media/base/media_constants.{h,cc} the SDP layer uses
// (compare/webrtc/reference/media/base/; media_constants.h is not there, so the values are its M157 ones).
//
// Differences from libwebrtc:
// - The payload type is an int (-1 = not set, as Codec::kIdNotSet), not the PayloadType class.
// - No SdpAudioFormat/SdpVideoFormat constructors, no scalability modes, no codec matching (Matches,
//   MatchesRtpCodec, FindMatchingVideoCodec, the H.264 profile helpers) and no AddDefaultFeedbackParams: offer/answer
//   generation (C6 part 2) brings them with media/base/codec_comparators.
#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sm::pc {

// media/base/media_constants.
inline constexpr int kVideoCodecClockrate = 90000;
inline constexpr char kCodecParamPTime[] = "ptime";
inline constexpr char kCodecParamMaxPTime[] = "maxptime";
inline constexpr char kCodecParamMinPTime[] = "minptime";
inline constexpr char kCodecParamSPropStereo[] = "sprop-stereo";
inline constexpr char kCodecParamStereo[] = "stereo";
inline constexpr char kCodecParamUseInbandFec[] = "useinbandfec";
inline constexpr char kCodecParamUseDtx[] = "usedtx";
inline constexpr char kCodecParamMaxAverageBitrate[] = "maxaveragebitrate";
inline constexpr char kCodecParamMaxPlaybackRate[] = "maxplaybackrate";
inline constexpr char kCodecParamAssociatedPayloadType[] = "apt";
inline constexpr char kCodecParamAssociatedCodecName[] = "acn";
inline constexpr char kCodecParamRtxTime[] = "rtx-time";
inline constexpr char kCodecParamMinBitrate[] = "x-google-min-bitrate";
inline constexpr char kCodecParamStartBitrate[] = "x-google-start-bitrate";
inline constexpr char kCodecParamMaxBitrate[] = "x-google-max-bitrate";
inline constexpr char kCodecParamMaxQuantization[] = "x-google-max-quantization";
inline constexpr char kParamValueTrue[] = "1";
inline constexpr char kParamValueEmpty[] = "";
inline constexpr char kRtcpFbParamLntf[] = "goog-lntf";
inline constexpr char kRtcpFbParamNack[] = "nack";
inline constexpr char kRtcpFbNackParamPli[] = "pli";
inline constexpr char kRtcpFbParamRemb[] = "goog-remb";
inline constexpr char kRtcpFbParamTransportCc[] = "transport-cc";
inline constexpr char kRtcpFbParamCcm[] = "ccm";
inline constexpr char kRtcpFbCcmParamFir[] = "fir";
inline constexpr char kRtcpFbParamRrtr[] = "rrtr";
inline constexpr char kPacketizationParamRaw[] = "raw";
inline constexpr char kVp8CodecName[] = "VP8";
inline constexpr char kVp9CodecName[] = "VP9";
inline constexpr char kAv1CodecName[] = "AV1";
inline constexpr char kH264CodecName[] = "H264";
inline constexpr char kH265CodecName[] = "H265";
inline constexpr char kH264FmtpProfileLevelId[] = "profile-level-id";
inline constexpr char kH264FmtpLevelAsymmetryAllowed[] = "level-asymmetry-allowed";
inline constexpr char kH264FmtpPacketizationMode[] = "packetization-mode";
inline constexpr char kH264FmtpSpropParameterSets[] = "sprop-parameter-sets";
inline constexpr char kH265FmtpLevelId[] = "level-id";
inline constexpr char kH265FmtpTxMode[] = "tx-mode";
inline constexpr char kVP9ProfileId[] = "profile-id";
inline constexpr char kAv1FmtpProfile[] = "profile";
inline constexpr char kAv1FmtpLevelIdx[] = "level-idx";
inline constexpr char kAv1FmtpTier[] = "tier";
inline constexpr char kRedCodecName[] = "red";
inline constexpr char kUlpfecCodecName[] = "ulpfec";
inline constexpr char kFlexfecCodecName[] = "flexfec-03";
inline constexpr char kRtxCodecName[] = "rtx";
inline constexpr char kOpusCodecName[] = "opus";
inline constexpr char kL16CodecName[] = "L16";
inline constexpr char kG722CodecName[] = "G722";
inline constexpr char kPcmuCodecName[] = "PCMU";
inline constexpr char kPcmaCodecName[] = "PCMA";
inline constexpr char kCnCodecName[] = "CN";
inline constexpr char kDtmfCodecName[] = "telephone-event";
inline constexpr char kComfortNoiseCodecName[] = "CN";
// b=AS (kbps in SDP, bps in the model) and b=TIAS (bps).
inline constexpr char kApplicationSpecificBandwidth[] = "AS";
inline constexpr char kTransportSpecificBandwidth[] = "TIAS";

// api/rtp_parameters.h's CodecParameterMap: fmtp parameters by name (an unnamed one, as RED's "111/111", has key "").
using CodecParameterMap = std::map<std::string, std::string>;

// One a=rtcp-fb value: "nack", "nack pli", "ccm fir", ...
class FeedbackParam {
 public:
  FeedbackParam() = default;
  FeedbackParam(std::string_view id, std::string_view param) : id_(id), param_(param) {}
  explicit FeedbackParam(std::string_view id) : id_(id), param_(kParamValueEmpty) {}

  // Case-insensitive, as libwebrtc.
  bool operator==(const FeedbackParam& other) const;
  bool operator!=(const FeedbackParam& c) const { return !(*this == c); }

  const std::string& id() const { return id_; }
  const std::string& param() const { return param_; }

 private:
  std::string id_;     // e.g. "nack", "ccm"
  std::string param_;  // e.g. "", "rpsi", "fir"
};

class FeedbackParams {
 public:
  bool operator==(const FeedbackParams& other) const { return params_ == other.params_; }
  bool operator!=(const FeedbackParams& c) const { return !(*this == c); }

  bool Has(const FeedbackParam& param) const;
  // Ignores an empty id and a duplicate.
  void Add(const FeedbackParam& param);
  bool Remove(const FeedbackParam& param);
  // Keeps only the params `from` has too.
  void Intersect(const FeedbackParams& from);

  const std::vector<FeedbackParam>& params() const { return params_; }

 private:
  std::vector<FeedbackParam> params_;
};

inline constexpr int kDefaultVideoClockRateHz = 90'000;
inline constexpr int kDefaultAudioClockRateHz = 8'000;

struct Codec {
  enum class Type { kAudio, kVideo };
  enum class ResiliencyType { kNone, kRed, kUlpfec, kFlexfec, kRtx };
  static constexpr int kIdNotSet = -1;

  Type type = Type::kAudio;
  int id = kIdNotSet;
  std::string name;
  int clockrate = kDefaultAudioClockRateHz;
  // Audio only.
  int bitrate = 0;
  size_t channels = 0;
  // Video only: a=packetization (only "raw" is kept).
  std::optional<std::string> packetization;
  CodecParameterMap params;
  FeedbackParams feedback_params;

  Codec() = default;
  Codec(Type type, int id, std::string_view name, int clockrate, size_t channels = 0)
      : type(type), id(id), name(name), clockrate(clockrate), channels(channels) {}

  bool GetParam(std::string_view key, std::string* out) const;
  bool GetParam(std::string_view key, int* out) const;
  void SetParam(std::string_view key, std::string_view value);
  void SetParam(std::string_view key, int value);
  bool RemoveParam(std::string_view key);
  bool HasFeedbackParam(const FeedbackParam& param) const { return feedback_params.Has(param); }
  void AddFeedbackParam(const FeedbackParam& param) { feedback_params.Add(param); }
  void IntersectFeedbackParams(const Codec& other) { feedback_params.Intersect(other.feedback_params); }

  // Not RED, FEC, RTX or comfort noise.
  bool IsMediaCodec() const;
  bool IsResiliencyCodec() const { return GetResiliencyType() != ResiliencyType::kNone; }
  ResiliencyType GetResiliencyType() const;
  // A payload type in 0-127 and, for a media codec, no x-google-max-bitrate below x-google-min-bitrate.
  bool ValidateCodecFormat() const;
  // "AudioCodec[111:opus:48000:0:2]" / "VideoCodec[96:VP8]".
  std::string ToString() const;

  bool operator==(const Codec& c) const;
  bool operator!=(const Codec& c) const { return !(*this == c); }
};
using Codecs = std::vector<Codec>;

Codec CreateAudioCodec(int id, std::string_view name, int clockrate, size_t channels);
Codec CreateAudioRtxCodec(int rtx_payload_type, int associated_payload_type);
Codec CreateVideoCodec(std::string_view name);
Codec CreateVideoCodec(int id, std::string_view name);
Codec CreateVideoRtxCodec(int rtx_payload_type, int associated_payload_type);
const Codec* FindCodecById(const std::vector<Codec>& codecs, int payload_type);
bool HasLntf(const Codec& codec);
bool HasNack(const Codec& codec);
bool HasRemb(const Codec& codec);
bool HasRrtr(const Codec& codec);

// The fmtp helpers webrtc_sdp uses (libwebrtc's WriteFmtpParameters / ParseFmtpParameterSet, not in
// compare/webrtc/reference/): "key=value;key2=value2", with an unnamed parameter written as its value alone, and
// ptime/maxptime left out (they are a=ptime/a=maxptime lines). Returns false if nothing was written.
bool WriteFmtpParameters(const CodecParameterMap& parameters, std::string* os);
// Splits at ';', trims ASCII white space around each parameter, and takes "key=value" (or the whole parameter as
// an unnamed value). A later duplicate overwrites an earlier one.
bool ParseFmtpParameterSet(std::string_view line_params, CodecParameterMap& codec_params);

// media/base/rtp_utils.h: 0-127.
inline bool IsValidRtpPayloadType(int payload_type) { return payload_type >= 0 && payload_type <= 127; }

}  // namespace sm::pc
