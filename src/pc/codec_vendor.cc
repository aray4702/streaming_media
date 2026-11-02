// libwebrtc M157 pc/codec_vendor.cc, media/base/codec_comparators.cc and api/video_codecs/h264_profile_level_id.cc
// (not in compare/webrtc/reference/; see sm/pc/codec_vendor.h).
#include "sm/pc/codec_vendor.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace sm::pc {

namespace {

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

// h264_profile_level_id.cc
constexpr uint8_t kConstraintSet3Flag = 0x10;

struct BitPattern {
  uint8_t mask;
  uint8_t masked_value;
  bool IsMatch(uint8_t value) const { return masked_value == (value & mask); }
};
constexpr BitPattern MakeBitPattern(const char (&str)[9]) {
  uint8_t mask = 0, value = 0;
  for (int i = 0; i < 8; ++i) {
    mask = static_cast<uint8_t>(mask << 1);
    value = static_cast<uint8_t>(value << 1);
    if (str[i] != 'x') mask |= 1;
    if (str[i] == '1') value |= 1;
  }
  return BitPattern{mask, value};
}
struct ProfilePattern {
  uint8_t profile_idc;
  BitPattern profile_iop;
  H264Profile profile;
};
constexpr ProfilePattern kProfilePatterns[] = {
    {0x42, MakeBitPattern("x1xx0000"), H264Profile::kProfileConstrainedBaseline},
    {0x4D, MakeBitPattern("1xxx0000"), H264Profile::kProfileConstrainedBaseline},
    {0x58, MakeBitPattern("11xx0000"), H264Profile::kProfileConstrainedBaseline},
    {0x42, MakeBitPattern("x0xx0000"), H264Profile::kProfileBaseline},
    {0x58, MakeBitPattern("10xx0000"), H264Profile::kProfileBaseline},
    {0x4D, MakeBitPattern("0x0x0000"), H264Profile::kProfileMain},
    {0x64, MakeBitPattern("00000000"), H264Profile::kProfileHigh},
    {0x64, MakeBitPattern("00001100"), H264Profile::kProfileConstrainedHigh},
    {0xF4, MakeBitPattern("00000000"), H264Profile::kProfilePredictiveHigh444}};

bool IsLess(H264Level a, H264Level b) {
  if (a == H264Level::kLevel1_b) return b != H264Level::kLevel1 && b != H264Level::kLevel1_b;
  if (b == H264Level::kLevel1_b) return a == H264Level::kLevel1;
  return static_cast<int>(a) < static_cast<int>(b);
}
H264Level Min(H264Level a, H264Level b) { return IsLess(a, b) ? a : b; }

bool IsLevelAsymmetryAllowed(const CodecParameterMap& params) {
  auto it = params.find(kH264FmtpLevelAsymmetryAllowed);
  return it != params.end() && std::strtol(it->second.c_str(), nullptr, 10) == 1;
}

std::string H264PacketizationModeOrDefault(const CodecParameterMap& params) {
  auto it = params.find(kH264FmtpPacketizationMode);
  // If packetization-mode is not present, default to "0" (RFC 6184, section 6.2).
  return it == params.end() ? "0" : it->second;
}

// codec_comparators.cc IsSameCodecSpecific.
bool IsSameCodecSpecific(std::string_view name1, const CodecParameterMap& params1, std::string_view name2,
                         const CodecParameterMap& params2) {
  // The names might not necessarily match, so check both.
  if (EqualsIgnoreCase(kH264CodecName, name1) || EqualsIgnoreCase(kH264CodecName, name2)) {
    return H264IsSameProfile(params1, params2) &&
           H264PacketizationModeOrDefault(params1) == H264PacketizationModeOrDefault(params2);
  }
  return true;
}

bool IsDynamicPayloadType(int id) { return (id >= 35 && id <= 63) || (id >= 96 && id <= 127); }

bool ReferencedCodecsMatch(const std::vector<Codec>& codecs1, int id1, const std::vector<Codec>& codecs2, int id2) {
  const Codec* c1 = FindCodecById(codecs1, id1);
  const Codec* c2 = FindCodecById(codecs2, id2);
  return c1 && c2 && MatchesWithCodecRules(*c1, *c2);
}

const Codec* GetAssociatedCodecForRtx(const std::vector<Codec>& codec_list, const Codec& rtx_codec) {
  int associated_payload_type;
  if (!rtx_codec.GetParam(kCodecParamAssociatedPayloadType, &associated_payload_type)) return nullptr;
  const Codec* associated = FindCodecById(codec_list, associated_payload_type);
  // A codec that is itself RTX can't be associated.
  if (associated && associated->GetResiliencyType() == Codec::ResiliencyType::kRtx) return nullptr;
  return associated;
}

void StripCnCodecs(std::vector<Codec>* codecs) {
  codecs->erase(std::remove_if(codecs->begin(), codecs->end(),
                               [](const Codec& c) { return EqualsIgnoreCase(c.name, kComfortNoiseCodecName); }),
                codecs->end());
}

// Adds the codecs of `reference_codecs` that `offered_codecs` doesn't have, media codecs first, then RTX (with apt
// pointing at the offered copy of its codec), each with a payload type from the suggester.
bool MergeCodecs(const std::vector<Codec>& reference_codecs, const std::string& mid,
                 std::vector<Codec>& offered_codecs, SdpPayloadTypeSuggester& suggester) {
  for (const Codec& reference_codec : reference_codecs) {
    if (reference_codec.GetResiliencyType() != Codec::ResiliencyType::kRtx &&
        !FindMatchingCodec(reference_codecs, offered_codecs, reference_codec)) {
      Codec codec = reference_codec;
      std::optional<int> pt = suggester.SuggestPayloadType(mid, codec);
      if (!pt) return false;
      codec.id = *pt;
      offered_codecs.push_back(codec);
    }
  }
  for (const Codec& reference_codec : reference_codecs) {
    if (reference_codec.GetResiliencyType() == Codec::ResiliencyType::kRtx &&
        !FindMatchingCodec(reference_codecs, offered_codecs, reference_codec)) {
      Codec rtx_codec = reference_codec;
      const Codec* associated_codec = GetAssociatedCodecForRtx(reference_codecs, rtx_codec);
      if (!associated_codec) continue;
      // The offered copy of the codec; its payload type may differ from the reference one.
      std::optional<Codec> matching_codec = FindMatchingCodec(reference_codecs, offered_codecs, *associated_codec);
      if (!matching_codec) continue;
      rtx_codec.params[kCodecParamAssociatedPayloadType] = std::to_string(matching_codec->id);
      std::optional<int> pt = suggester.SuggestPayloadType(mid, rtx_codec);
      if (!pt) return false;
      rtx_codec.id = *pt;
      offered_codecs.push_back(rtx_codec);
    }
  }
  return true;
}

// Our codecs that the offer has, with the offer's payload types and names, the negotiated feedback and H.264
// level, in the offer's order when `keep_offer_order`.
void NegotiateCodecs(const std::vector<Codec>& local_codecs, const std::vector<Codec>& offered_codecs,
                     std::vector<Codec>* negotiated_codecs, bool keep_offer_order) {
  for (const Codec& ours : local_codecs) {
    std::optional<Codec> theirs = FindMatchingCodec(local_codecs, offered_codecs, ours);
    // Only one matching codec for each of our codecs, in case the offer has duplicates.
    if (!theirs) continue;
    Codec negotiated = ours;
    if (ours.packetization != theirs->packetization) negotiated.packetization.reset();
    negotiated.IntersectFeedbackParams(*theirs);
    if (negotiated.GetResiliencyType() == Codec::ResiliencyType::kRtx) {
      // FindMatchingCodec doesn't return an RTX codec without apt.
      negotiated.SetParam(kCodecParamAssociatedPayloadType, theirs->params.at(kCodecParamAssociatedPayloadType));
      // The declarative rtx-time parameter.
      auto rtx_time_it = theirs->params.find(kCodecParamRtxTime);
      if (rtx_time_it != theirs->params.end()) negotiated.SetParam(kCodecParamRtxTime, rtx_time_it->second);
    }
    if (EqualsIgnoreCase(ours.name, kH264CodecName)) {
      H264GenerateProfileLevelIdForAnswer(ours.params, theirs->params, &negotiated.params);
    }
    negotiated.id = theirs->id;
    negotiated.name = theirs->name;
    negotiated_codecs->push_back(std::move(negotiated));
  }
  if (keep_offer_order) {
    // RFC 3264: the answerer SHOULD list the formats in the order they were present in the offer.
    std::unordered_map<int, int> payload_type_preferences;
    int preference = static_cast<int>(offered_codecs.size() + 1);
    for (const Codec& codec : offered_codecs) payload_type_preferences[codec.id] = preference--;
    std::sort(negotiated_codecs->begin(), negotiated_codecs->end(), [&](const Codec& a, const Codec& b) {
      return payload_type_preferences[a.id] > payload_type_preferences[b.id];
    });
  }
}

// Adds `codecs2` to `codecs1` without duplicate payload types (ComputeCodecsUnion).
std::vector<Codec> ComputeCodecsUnion(const std::vector<Codec>& codecs1, const std::vector<Codec>& codecs2) {
  std::vector<Codec> all_codecs;
  std::vector<int> used_ids;
  for (const Codec& codec : codecs1) {
    all_codecs.push_back(codec);
    used_ids.push_back(codec.id);
  }
  // Use the PTs from codecs1; drop codecs2 entries whose PT is taken or that match one of codecs1.
  for (const Codec& codec : codecs2) {
    if (!FindMatchingCodec(codecs2, all_codecs, codec) &&
        std::find(used_ids.begin(), used_ids.end(), codec.id) == used_ids.end()) {
      all_codecs.push_back(codec);
      used_ids.push_back(codec.id);
    }
  }
  return all_codecs;
}

void ComputeCodecsIntersectionAndUnion(const std::vector<Codec>& send_codecs, const std::vector<Codec>& recv_codecs,
                                       std::vector<Codec>& intersection, std::vector<Codec>& union_codecs) {
  // Order and payload types from the send list.
  for (const Codec& send : send_codecs) {
    if (FindMatchingCodec(send_codecs, recv_codecs, send)) intersection.push_back(send);
  }
  union_codecs = ComputeCodecsUnion(send_codecs, recv_codecs);
}

}  // namespace

std::optional<H264ProfileLevelId> ParseH264ProfileLevelId(const std::string& str) {
  // The string should consist of 3 bytes in hexadecimal format.
  if (str.size() != 6u) return std::nullopt;
  for (char c : str) {
    if (!std::isxdigit(static_cast<unsigned char>(c))) return std::nullopt;
  }
  const uint32_t numeric = static_cast<uint32_t>(std::strtoul(str.c_str(), nullptr, 16));
  if (numeric == 0) return std::nullopt;
  const uint8_t level_idc = static_cast<uint8_t>(numeric & 0xFF);
  const uint8_t profile_iop = static_cast<uint8_t>((numeric >> 8) & 0xFF);
  const uint8_t profile_idc = static_cast<uint8_t>((numeric >> 16) & 0xFF);
  H264Level level;
  switch (level_idc) {
    case 11:
      level = (profile_iop & kConstraintSet3Flag) != 0 ? H264Level::kLevel1_b : H264Level::kLevel1_1;
      break;
    case 10:
    case 12:
    case 13:
    case 20:
    case 21:
    case 22:
    case 30:
    case 31:
    case 32:
    case 40:
    case 41:
    case 42:
    case 50:
    case 51:
    case 52:
      level = static_cast<H264Level>(level_idc);
      break;
    default:
      return std::nullopt;
  }
  for (const ProfilePattern& pattern : kProfilePatterns) {
    if (profile_idc == pattern.profile_idc && pattern.profile_iop.IsMatch(profile_iop)) {
      return H264ProfileLevelId{pattern.profile, level};
    }
  }
  return std::nullopt;
}

std::optional<H264ProfileLevelId> ParseSdpForH264ProfileLevelId(const CodecParameterMap& params) {
  // libwebrtc's default when profile-level-id is absent (RFC 6184 says Baseline level 1).
  static constexpr H264ProfileLevelId kDefaultProfileLevelId{H264Profile::kProfileConstrainedBaseline,
                                                             H264Level::kLevel3_1};
  auto it = params.find(kH264FmtpProfileLevelId);
  return it == params.end() ? std::optional<H264ProfileLevelId>(kDefaultProfileLevelId)
                            : ParseH264ProfileLevelId(it->second);
}

std::optional<std::string> H264ProfileLevelIdToString(const H264ProfileLevelId& profile_level_id) {
  // Level 1b is a special case.
  if (profile_level_id.level == H264Level::kLevel1_b) {
    switch (profile_level_id.profile) {
      case H264Profile::kProfileConstrainedBaseline:
        return std::string("42f00b");
      case H264Profile::kProfileBaseline:
        return std::string("42100b");
      case H264Profile::kProfileMain:
        return std::string("4d100b");
      default:
        return std::nullopt;
    }
  }
  const char* profile_idc_iop_string = nullptr;
  switch (profile_level_id.profile) {
    case H264Profile::kProfileConstrainedBaseline:
      profile_idc_iop_string = "42e0";
      break;
    case H264Profile::kProfileBaseline:
      profile_idc_iop_string = "4200";
      break;
    case H264Profile::kProfileMain:
      profile_idc_iop_string = "4d00";
      break;
    case H264Profile::kProfileConstrainedHigh:
      profile_idc_iop_string = "640c";
      break;
    case H264Profile::kProfileHigh:
      profile_idc_iop_string = "6400";
      break;
    case H264Profile::kProfilePredictiveHigh444:
      profile_idc_iop_string = "f400";
      break;
  }
  char level[3];
  std::snprintf(level, sizeof(level), "%02x", static_cast<unsigned>(profile_level_id.level));
  return std::string(profile_idc_iop_string) + level;
}

bool H264IsSameProfile(const CodecParameterMap& params1, const CodecParameterMap& params2) {
  const auto a = ParseSdpForH264ProfileLevelId(params1);
  const auto b = ParseSdpForH264ProfileLevelId(params2);
  return a && b && a->profile == b->profile;
}

void H264GenerateProfileLevelIdForAnswer(const CodecParameterMap& local_supported_params,
                                         const CodecParameterMap& remote_offered_params, CodecParameterMap* answer) {
  // If both local and remote haven't set profile-level-id, they are both using the default and the answer leaves
  // it out too.
  if (!local_supported_params.count(kH264FmtpProfileLevelId) && !remote_offered_params.count(kH264FmtpProfileLevelId)) {
    return;
  }
  const auto local = ParseSdpForH264ProfileLevelId(local_supported_params);
  const auto remote = ParseSdpForH264ProfileLevelId(remote_offered_params);
  // The codecs matched, so both parse and have the same profile.
  if (!local || !remote || local->profile != remote->profile) return;
  const bool level_asymmetry_allowed =
      IsLevelAsymmetryAllowed(local_supported_params) && IsLevelAsymmetryAllowed(remote_offered_params);
  const H264Level answer_level = level_asymmetry_allowed ? local->level : Min(local->level, remote->level);
  if (auto str = H264ProfileLevelIdToString(H264ProfileLevelId{local->profile, answer_level})) {
    (*answer)[kH264FmtpProfileLevelId] = *str;
  }
}

bool MatchesWithCodecRules(const Codec& a, const Codec& b) {
  // Dynamic payload types match by name, static ones by payload type.
  const bool matches_id = (IsDynamicPayloadType(a.id) && IsDynamicPayloadType(b.id)) ? EqualsIgnoreCase(a.name, b.name)
                                                                                     : a.id == b.id;
  if (!matches_id || a.type != b.type) return false;
  switch (a.type) {
    case Codec::Type::kAudio:
      // A nonzero clock rate must match; a nonzero bitrate must match unless either is VBR (0); channels 0 and 1
      // are the same (RFC 4566 section 6).
      return (b.clockrate == 0 || a.clockrate == b.clockrate) && (b.bitrate == 0 || a.bitrate <= 0 || a.bitrate == b.bitrate) &&
             ((b.channels < 2 && a.channels < 2) || a.channels == b.channels);
    case Codec::Type::kVideo:
      return IsSameCodecSpecific(a.name, a.params, b.name, b.params);
  }
  return false;
}

std::optional<Codec> FindMatchingCodec(const std::vector<Codec>& codecs1, const std::vector<Codec>& codecs2,
                                       const Codec& codec_to_match) {
  for (const Codec& potential_match : codecs2) {
    if (!MatchesWithCodecRules(codec_to_match, potential_match)) continue;
    if (codec_to_match.GetResiliencyType() == Codec::ResiliencyType::kRtx) {
      int apt_value_1 = 0;
      int apt_value_2 = 0;
      if (!codec_to_match.GetParam(kCodecParamAssociatedPayloadType, &apt_value_1) ||
          !potential_match.GetParam(kCodecParamAssociatedPayloadType, &apt_value_2)) {
        continue;  // RTX without an associated payload type.
      }
      if (!ReferencedCodecsMatch(codecs1, apt_value_1, codecs2, apt_value_2)) continue;
    }
    return potential_match;
  }
  return std::nullopt;
}

CodecVendor::CodecVendor(std::vector<Codec> audio_send, std::vector<Codec> audio_recv, std::vector<Codec> video_send,
                         std::vector<Codec> video_recv)
    : audio_send_codecs_(std::move(audio_send)),
      audio_recv_codecs_(std::move(audio_recv)),
      video_send_codecs_(std::move(video_send)),
      video_recv_codecs_(std::move(video_recv)) {
  ComputeCodecsIntersectionAndUnion(audio_send_codecs_, audio_recv_codecs_, audio_sendrecv_codecs_, all_audio_codecs_);
  ComputeCodecsIntersectionAndUnion(video_send_codecs_, video_recv_codecs_, video_sendrecv_codecs_, all_video_codecs_);
}

const std::vector<Codec>& CodecVendor::GetCodecsForOffer(MediaType type, RtpTransceiverDirection direction) const {
  const bool audio = type == MediaType::AUDIO;
  switch (direction) {
    // If the stream is inactive, generate the list as if sendrecv.
    case RtpTransceiverDirection::kSendRecv:
    case RtpTransceiverDirection::kStopped:
    case RtpTransceiverDirection::kInactive:
      return audio ? audio_sendrecv_codecs_ : video_sendrecv_codecs_;
    case RtpTransceiverDirection::kSendOnly:
      return audio ? audio_send_codecs_ : video_send_codecs_;
    case RtpTransceiverDirection::kRecvOnly:
      return audio ? audio_recv_codecs_ : video_recv_codecs_;
  }
  return audio ? audio_sendrecv_codecs_ : video_sendrecv_codecs_;
}

const std::vector<Codec>& CodecVendor::GetCodecsForAnswer(MediaType type, RtpTransceiverDirection offer,
                                                          RtpTransceiverDirection answer) const {
  const bool audio = type == MediaType::AUDIO;
  switch (answer) {
    // For inactive and sendrecv answers, generate lists as if we were to accept the offer's direction (RFC 3264
    // section 6.1).
    case RtpTransceiverDirection::kSendRecv:
    case RtpTransceiverDirection::kStopped:
    case RtpTransceiverDirection::kInactive:
      return GetCodecsForOffer(type, RtpTransceiverDirectionReversed(offer));
    case RtpTransceiverDirection::kSendOnly:
      return audio ? audio_send_codecs_ : video_send_codecs_;
    case RtpTransceiverDirection::kRecvOnly:
      return audio ? audio_recv_codecs_ : video_recv_codecs_;
  }
  return audio ? audio_sendrecv_codecs_ : video_sendrecv_codecs_;
}

std::optional<std::vector<Codec>> CodecVendor::GetNegotiatedCodecsForOffer(const MediaDescriptionOptions& options,
                                                                           const MediaSessionOptions& session_options,
                                                                           const ContentInfo* current_content,
                                                                           SdpPayloadTypeSuggester& suggester) const {
  const bool current_matches = current_content && current_content->mid() == options.mid &&
                               current_content->media_description() &&
                               current_content->media_description()->type() == options.type;
  std::vector<Codec> codecs;
  if (current_matches) codecs = current_content->media_description()->codecs();
  // Add our codecs that are not in the current description.
  if (!MergeCodecs(AllCodecs(options.type), options.mid, codecs, suggester)) return std::nullopt;

  const std::vector<Codec>& supported_codecs = GetCodecsForOffer(options.type, options.direction);
  std::vector<Codec> filtered_codecs;
  // The current content's codecs first (if it is not rejected or recycled), in their order and with their PTs.
  if (current_matches && !current_content->rejected) {
    const auto& current_codecs = current_content->media_description()->codecs();
    for (const Codec& codec : current_codecs) {
      if (FindMatchingCodec(current_codecs, supported_codecs, codec)) filtered_codecs.push_back(codec);
    }
  }
  // Then the other supported codecs, with the payload types of `codecs`.
  for (const Codec& codec : supported_codecs) {
    std::optional<Codec> found_codec = FindMatchingCodec(supported_codecs, codecs, codec);
    if (found_codec && !FindMatchingCodec(supported_codecs, filtered_codecs, codec)) {
      filtered_codecs.push_back(*found_codec);
    }
  }
  if (options.type == MediaType::AUDIO && !session_options.vad_enabled) StripCnCodecs(&filtered_codecs);
  return filtered_codecs;
}

std::optional<std::vector<Codec>> CodecVendor::GetNegotiatedCodecsForAnswer(
    const MediaDescriptionOptions& options, const MediaSessionOptions& session_options,
    RtpTransceiverDirection offer_rtd, RtpTransceiverDirection answer_rtd, const ContentInfo* current_content,
    const std::vector<Codec>& offered_codecs, SdpPayloadTypeSuggester& suggester) const {
  const bool current_matches = current_content && current_content->mid() == options.mid &&
                               current_content->media_description() &&
                               current_content->media_description()->type() == options.type;
  std::vector<Codec> codecs;
  if (current_matches) codecs = current_content->media_description()->codecs();
  if (!MergeCodecs(AllCodecs(options.type), options.mid, codecs, suggester)) return std::nullopt;

  const std::vector<Codec>& supported_codecs = GetCodecsForAnswer(options.type, offer_rtd, answer_rtd);
  std::vector<Codec> filtered_codecs;
  if (current_matches && !current_content->rejected) {
    const auto& current_codecs = current_content->media_description()->codecs();
    for (const Codec& codec : current_codecs) {
      if (FindMatchingCodec(current_codecs, supported_codecs, codec)) filtered_codecs.push_back(codec);
    }
  }
  // Add the other supported codecs.
  std::vector<Codec> other_codecs;
  for (const Codec& codec : supported_codecs) {
    if (FindMatchingCodec(supported_codecs, codecs, codec) &&
        !FindMatchingCodec(supported_codecs, filtered_codecs, codec)) {
      // Use the supported codec: NegotiateCodecs maps its payload type to the offer's.
      other_codecs.push_back(codec);
    }
  }
  // ComputeCodecsUnion avoids duplicate payload types.
  filtered_codecs = ComputeCodecsUnion(filtered_codecs, other_codecs);

  std::vector<Codec> negotiated_codecs;
  NegotiateCodecs(filtered_codecs, offered_codecs, &negotiated_codecs, /*keep_offer_order=*/true);
  if (options.type == MediaType::AUDIO && !session_options.vad_enabled) StripCnCodecs(&negotiated_codecs);
  return negotiated_codecs;
}

std::vector<Codec> DefaultAudioCodecs() {
  Codec opus = CreateAudioCodec(111, kOpusCodecName, 48000, 2);
  opus.SetParam(kCodecParamMinPTime, "10");
  opus.SetParam(kCodecParamUseInbandFec, kParamValueTrue);
  opus.AddFeedbackParam(FeedbackParam(kRtcpFbParamTransportCc));
  return {opus};
}

std::vector<Codec> DefaultVideoCodecs() {
  Codec h264 = CreateVideoCodec(96, kH264CodecName);
  h264.SetParam(kH264FmtpLevelAsymmetryAllowed, "1");
  h264.SetParam(kH264FmtpPacketizationMode, "1");
  h264.SetParam(kH264FmtpProfileLevelId, "42e01f");
  // AddDefaultFeedbackParams (webrtc_video_engine.cc), without the field-trial ones (goog-lntf, ack ccfb).
  h264.AddFeedbackParam(FeedbackParam(kRtcpFbParamRemb, kParamValueEmpty));
  h264.AddFeedbackParam(FeedbackParam(kRtcpFbParamTransportCc, kParamValueEmpty));
  h264.AddFeedbackParam(FeedbackParam(kRtcpFbParamCcm, kRtcpFbCcmParamFir));
  h264.AddFeedbackParam(FeedbackParam(kRtcpFbParamNack, kParamValueEmpty));
  h264.AddFeedbackParam(FeedbackParam(kRtcpFbParamNack, kRtcpFbNackParamPli));
  return {h264, CreateVideoRtxCodec(97, 96)};
}

std::vector<RtpHeaderExtensionCapability> DefaultAudioRtpHeaderExtensions() {
  std::vector<RtpHeaderExtensionCapability> result;
  int id = 1;
  for (const char* uri : {RtpExtension::kAudioLevelUri, RtpExtension::kAbsSendTimeUri,
                          RtpExtension::kTransportSequenceNumberUri, RtpExtension::kMidUri}) {
    result.emplace_back(uri, id++, RtpTransceiverDirection::kSendRecv);
  }
  result.emplace_back(RtpExtension::kAbsoluteCaptureTimeUri, id, RtpTransceiverDirection::kStopped);
  return result;
}

std::vector<RtpHeaderExtensionCapability> DefaultVideoRtpHeaderExtensions() {
  std::vector<RtpHeaderExtensionCapability> result;
  int id = 1;
  for (const char* uri :
       {RtpExtension::kTimestampOffsetUri, RtpExtension::kAbsSendTimeUri, RtpExtension::kVideoRotationUri,
        RtpExtension::kTransportSequenceNumberUri, RtpExtension::kPlayoutDelayUri, RtpExtension::kVideoContentTypeUri,
        RtpExtension::kVideoTimingUri, RtpExtension::kColorSpaceUri, RtpExtension::kMidUri, RtpExtension::kRidUri,
        RtpExtension::kRepairedRidUri}) {
    result.emplace_back(uri, id++, RtpTransceiverDirection::kSendRecv);
  }
  for (const char* uri : {RtpExtension::kGenericFrameDescriptorUri00, RtpExtension::kDependencyDescriptorUri,
                          RtpExtension::kVideoLayersAllocationUri, RtpExtension::kVideoFrameTrackingIdUri,
                          RtpExtension::kAbsoluteCaptureTimeUri}) {
    result.emplace_back(uri, id++, RtpTransceiverDirection::kStopped);
  }
  return result;
}

}  // namespace sm::pc
