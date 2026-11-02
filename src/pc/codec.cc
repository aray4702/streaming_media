// sm/pc/codec.h: libwebrtc M157's media/base/codec.cc (compare/webrtc/reference/media/base/), #1062.
#include "sm/pc/codec.h"

#include <algorithm>
#include <cctype>

#include "sm/p2p/string_encode.h"

namespace sm::pc {
namespace {

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

std::string_view StripAsciiWhitespace(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

}  // namespace

bool FeedbackParam::operator==(const FeedbackParam& other) const {
  return EqualsIgnoreCase(other.id(), id()) && EqualsIgnoreCase(other.param(), param());
}

bool FeedbackParams::Has(const FeedbackParam& param) const {
  return std::find(params_.begin(), params_.end(), param) != params_.end();
}

void FeedbackParams::Add(const FeedbackParam& param) {
  if (param.id().empty()) return;
  if (Has(param)) return;  // Param already in `this`.
  params_.push_back(param);
}

bool FeedbackParams::Remove(const FeedbackParam& param) {
  if (!Has(param)) return false;
  std::erase(params_, param);
  return true;
}

void FeedbackParams::Intersect(const FeedbackParams& from) {
  auto iter_to = params_.begin();
  while (iter_to != params_.end()) {
    if (!from.Has(*iter_to)) {
      iter_to = params_.erase(iter_to);
    } else {
      ++iter_to;
    }
  }
}

bool Codec::operator==(const Codec& c) const {
  return type == c.type && id == c.id && name == c.name && clockrate == c.clockrate && params == c.params &&
         feedback_params == c.feedback_params &&
         (type == Type::kAudio ? (bitrate == c.bitrate && channels == c.channels) : (packetization == c.packetization));
}

bool Codec::GetParam(std::string_view key, std::string* out) const {
  auto iter = params.find(std::string(key));
  if (iter == params.end()) return false;
  *out = iter->second;
  return true;
}

bool Codec::GetParam(std::string_view key, int* out) const {
  auto iter = params.find(std::string(key));
  if (iter == params.end()) return false;
  return p2p::FromString(iter->second, out);
}

void Codec::SetParam(std::string_view key, std::string_view value) { params[std::string(key)] = std::string(value); }

void Codec::SetParam(std::string_view key, int value) { params[std::string(key)] = std::to_string(value); }

bool Codec::RemoveParam(std::string_view key) { return params.erase(std::string(key)) == 1; }

bool Codec::IsMediaCodec() const { return !IsResiliencyCodec() && !EqualsIgnoreCase(name, kComfortNoiseCodecName); }

Codec::ResiliencyType Codec::GetResiliencyType() const {
  if (EqualsIgnoreCase(name, kRedCodecName)) return ResiliencyType::kRed;
  if (EqualsIgnoreCase(name, kUlpfecCodecName)) return ResiliencyType::kUlpfec;
  if (EqualsIgnoreCase(name, kFlexfecCodecName)) return ResiliencyType::kFlexfec;
  if (EqualsIgnoreCase(name, kRtxCodecName)) return ResiliencyType::kRtx;
  return ResiliencyType::kNone;
}

bool Codec::ValidateCodecFormat() const {
  if (id < 0 || id > 127) return false;
  if (IsResiliencyCodec()) return true;
  int min_bitrate = -1;
  int max_bitrate = -1;
  if (GetParam(kCodecParamMinBitrate, &min_bitrate) && GetParam(kCodecParamMaxBitrate, &max_bitrate)) {
    if (max_bitrate < min_bitrate) return false;
  }
  return true;
}

std::string Codec::ToString() const {
  if (type == Type::kAudio) {
    return "AudioCodec[" + std::to_string(id) + ":" + name + ":" + std::to_string(clockrate) + ":" +
           std::to_string(bitrate) + ":" + std::to_string(channels) + "]";
  }
  std::string s = "VideoCodec[" + std::to_string(id) + ":" + name;
  if (packetization.has_value()) s += ":" + *packetization;
  return s + "]";
}

Codec CreateAudioCodec(int id, std::string_view name, int clockrate, size_t channels) {
  return Codec(Codec::Type::kAudio, id, name, clockrate, channels);
}

Codec CreateAudioRtxCodec(int rtx_payload_type, int associated_payload_type) {
  Codec rtx_codec = CreateAudioCodec(rtx_payload_type, kRtxCodecName, kDefaultAudioClockRateHz, 1);
  rtx_codec.SetParam(kCodecParamAssociatedPayloadType, associated_payload_type);
  return rtx_codec;
}

Codec CreateVideoCodec(std::string_view name) { return CreateVideoCodec(Codec::kIdNotSet, name); }

Codec CreateVideoCodec(int id, std::string_view name) {
  Codec c(Codec::Type::kVideo, id, name, kVideoCodecClockrate);
  if (EqualsIgnoreCase(kH264CodecName, name)) {
    // This default is set for all H.264 codecs created because that was the default before packetization mode
    // support was added.
    c.SetParam(kH264FmtpPacketizationMode, "1");
  }
  return c;
}

Codec CreateVideoRtxCodec(int rtx_payload_type, int associated_payload_type) {
  Codec rtx_codec = CreateVideoCodec(rtx_payload_type, kRtxCodecName);
  rtx_codec.SetParam(kCodecParamAssociatedPayloadType, associated_payload_type);
  return rtx_codec;
}

const Codec* FindCodecById(const std::vector<Codec>& codecs, int payload_type) {
  for (const auto& codec : codecs) {
    if (codec.id == payload_type) return &codec;
  }
  return nullptr;
}

bool HasLntf(const Codec& codec) { return codec.HasFeedbackParam(FeedbackParam(kRtcpFbParamLntf, kParamValueEmpty)); }
bool HasNack(const Codec& codec) { return codec.HasFeedbackParam(FeedbackParam(kRtcpFbParamNack, kParamValueEmpty)); }
bool HasRemb(const Codec& codec) { return codec.HasFeedbackParam(FeedbackParam(kRtcpFbParamRemb, kParamValueEmpty)); }
bool HasRrtr(const Codec& codec) { return codec.HasFeedbackParam(FeedbackParam(kRtcpFbParamRrtr, kParamValueEmpty)); }

bool WriteFmtpParameters(const CodecParameterMap& parameters, std::string* os) {
  bool empty = true;
  const char* delimiter = "";  // No delimiter before first parameter.
  for (const auto& [key, value] : parameters) {
    // ptime and maxptime are a=ptime / a=maxptime lines, not fmtp parameters.
    if (key == kCodecParamPTime || key == kCodecParamMaxPTime) continue;
    *os += delimiter;
    delimiter = ";";  // A semicolon before each subsequent parameter.
    if (!key.empty()) *os += key + "=";
    *os += value;
    empty = false;
  }
  return !empty;
}

bool ParseFmtpParameterSet(std::string_view line_params, CodecParameterMap& codec_params) {
  // RFC 4566: a=fmtp:<format> <format specific parameters>, here "<param1>=<value1>; <param2>=<value2>; ...".
  for (std::string_view param : p2p::split(line_params, ';')) {
    std::string name;
    std::string value;
    const std::string_view stripped = StripAsciiWhitespace(param);
    if (!p2p::tokenize_first(stripped, '=', &name, &value)) {
      // Support for non-key-value lines like RFC 2198 or RFC 4733.
      name.clear();
      value = std::string(stripped);
    }
    codec_params[name] = value;
  }
  return true;
}

}  // namespace sm::pc
