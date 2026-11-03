#pragma once
// The codecs of an offer or answer (C6 part 2, #1063): libwebrtc M157's pc/codec_vendor (CodecVendor,
// GetNegotiatedCodecsForOffer / ForAnswer, MergeCodecs, NegotiateCodecs), media/base/codec_comparators
// (MatchesWithCodecRules, FindMatchingCodec) and api/video_codecs/h264_profile_level_id (profiles, levels and
// H264GenerateProfileLevelIdForAnswer). None of these files is in compare/webrtc/reference/; they are ported from
// their M157 behaviour as pc/media_session_unittest.cc pins it.
//
// Differences from libwebrtc, for C6's subset:
// - The codecs come from the caller (`CodecVendor(audio_send, audio_recv, video_send, video_recv)`), not from a
//   MediaEngine; `DefaultAudioCodecs()` and `DefaultVideoCodecs()` are what libwebrtc's engines give for Opus and
//   H.264 Constrained Baseline (profile-level-id 42e01f, packetization-mode 1) with RTX, with their payload types,
//   fmtp and rtcp-fb.
// - No codec preferences, no raw packetization, and RED's redundancy fmtp is not matched codec by codec (RED is
//   matched by name, clock rate and channels). Codec-specific matching covers H.264 only (VP9, AV1 and H.265
//   profiles are not compared).
#include <optional>
#include <string>
#include <vector>

#include "sm/pc/codec.h"
#include "sm/pc/media_options.h"
#include "sm/pc/sdp_payload_type_suggester.h"
#include "sm/pc/session_description.h"

namespace sm::pc {

// api/video_codecs/h264_profile_level_id.
enum class H264Profile {
  kProfileConstrainedBaseline,
  kProfileBaseline,
  kProfileMain,
  kProfileConstrainedHigh,
  kProfileHigh,
  kProfilePredictiveHigh444,
};
enum class H264Level {
  kLevel1_b = 0,
  kLevel1 = 10,
  kLevel1_1 = 11,
  kLevel1_2 = 12,
  kLevel1_3 = 13,
  kLevel2 = 20,
  kLevel2_1 = 21,
  kLevel2_2 = 22,
  kLevel3 = 30,
  kLevel3_1 = 31,
  kLevel3_2 = 32,
  kLevel4 = 40,
  kLevel4_1 = 41,
  kLevel4_2 = 42,
  kLevel5 = 50,
  kLevel5_1 = 51,
  kLevel5_2 = 52,
};
struct H264ProfileLevelId {
  H264Profile profile;
  H264Level level;
};
// Parses a profile-level-id string ("42e01f").
std::optional<H264ProfileLevelId> ParseH264ProfileLevelId(const std::string& str);
// The profile-level-id of fmtp `params`; Constrained Baseline level 3.1 when it is absent, as libwebrtc.
std::optional<H264ProfileLevelId> ParseSdpForH264ProfileLevelId(const CodecParameterMap& params);
std::optional<std::string> H264ProfileLevelIdToString(const H264ProfileLevelId& profile_level_id);
bool H264IsSameProfile(const CodecParameterMap& params1, const CodecParameterMap& params2);
// The answer's profile-level-id: the local level if both sides allow level asymmetry, else the lower level.
void H264GenerateProfileLevelIdForAnswer(const CodecParameterMap& local_supported_params,
                                         const CodecParameterMap& remote_offered_params, CodecParameterMap* answer);

// The codec in `codecs2` that matches `codec_to_match` (a member of `codecs1`); RTX matches if the codecs its apt
// points to match.
std::optional<Codec> FindMatchingCodec(const std::vector<Codec>& codecs1, const std::vector<Codec>& codecs2,
                                       const Codec& codec_to_match);

class CodecVendor {
 public:
  CodecVendor(std::vector<Codec> audio_send, std::vector<Codec> audio_recv, std::vector<Codec> video_send,
              std::vector<Codec> video_recv);

  // The codecs to offer in the m= section of `options`, with their payload types.
  std::optional<std::vector<Codec>> GetNegotiatedCodecsForOffer(const MediaDescriptionOptions& options,
                                                                const MediaSessionOptions& session_options,
                                                                const ContentInfo* current_content,
                                                                SdpPayloadTypeSuggester& suggester) const;
  // The codecs to answer `offered_codecs` with: ours that the offer has, with the offer's payload types, in the
  // offer's order.
  std::optional<std::vector<Codec>> GetNegotiatedCodecsForAnswer(const MediaDescriptionOptions& options,
                                                                 const MediaSessionOptions& session_options,
                                                                 RtpTransceiverDirection offer_rtd,
                                                                 RtpTransceiverDirection answer_rtd,
                                                                 const ContentInfo* current_content,
                                                                 const std::vector<Codec>& offered_codecs,
                                                                 SdpPayloadTypeSuggester& suggester) const;

  const std::vector<Codec>& audio_sendrecv_codecs() const { return audio_sendrecv_codecs_; }
  const std::vector<Codec>& audio_send_codecs() const { return audio_send_codecs_; }
  const std::vector<Codec>& audio_recv_codecs() const { return audio_recv_codecs_; }
  const std::vector<Codec>& video_sendrecv_codecs() const { return video_sendrecv_codecs_; }
  const std::vector<Codec>& video_send_codecs() const { return video_send_codecs_; }
  const std::vector<Codec>& video_recv_codecs() const { return video_recv_codecs_; }

 private:
  const std::vector<Codec>& GetCodecsForOffer(MediaType type, RtpTransceiverDirection direction) const;
  const std::vector<Codec>& GetCodecsForAnswer(MediaType type, RtpTransceiverDirection offer,
                                               RtpTransceiverDirection answer) const;
  const std::vector<Codec>& AllCodecs(MediaType type) const {
    return type == MediaType::AUDIO ? all_audio_codecs_ : all_video_codecs_;
  }

  std::vector<Codec> audio_send_codecs_, audio_recv_codecs_, audio_sendrecv_codecs_, all_audio_codecs_;
  std::vector<Codec> video_send_codecs_, video_recv_codecs_, video_sendrecv_codecs_, all_video_codecs_;
};

// What libwebrtc's voice engine offers for Opus: 111 opus/48000/2, minptime=10;useinbandfec=1, rtcp-fb
// transport-cc.
std::vector<Codec> DefaultAudioCodecs();
// What libwebrtc's video engine offers for H.264 Constrained Baseline: 96 H264/90000,
// level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f, rtcp-fb goog-remb, transport-cc, ccm fir,
// nack, nack pli; 97 rtx/90000 apt=96.
std::vector<Codec> DefaultVideoCodecs();
// The engines' RTP header extensions (WebRtcVoiceEngine / WebRtcVideoEngine::GetRtpHeaderExtensions): audio
// ssrc-audio-level 1, abs-send-time 2, transport-cc 3, sdes:mid 4 (abs-capture-time stopped); video toffset 1,
// abs-send-time 2, video-orientation 3, transport-cc 4, playout-delay 5, video-content-type 6, video-timing 7,
// color-space 8, sdes:mid 9, rtp-stream-id 10, repaired-rtp-stream-id 11 (the others stopped).
std::vector<RtpHeaderExtensionCapability> DefaultAudioRtpHeaderExtensions();
std::vector<RtpHeaderExtensionCapability> DefaultVideoRtpHeaderExtensions();

}  // namespace sm::pc
