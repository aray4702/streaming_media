#pragma once
// Payload type and RTP header extension id assignment (C6 part 2, #1063): libwebrtc M157's
// pc/sdp_payload_type_suggester.{h,cc} (compare/webrtc/reference/pc/), with the PayloadTypePicker,
// PayloadTypeRecorder and RtpHeaderExtensionPicker it uses (call/payload_type_picker.cc, not in reference/).
//
// The recorders are kept per BUNDLE group (under the group's first mid), or per mid for a section that is not
// bundled, with a local and a remote payload type map and one header extension map each. `Update` records what a
// description set as local or remote uses, and the bundle groups from it (committed on an answer, as BundleManager
// does).
//
// Differences from libwebrtc (call/payload_type_picker.cc is not in reference/, so these follow its behaviour as
// media_session_unittest.cc pins it):
// - A payload type for a new codec is, in order: the one the local recorder already has for a matching codec; the
//   remote one for it if it is free locally; the codec's own payload type (the media engine's: opus 111, H.264 96
//   with RTX 97) if it is free; then the first free one in 96..127, then 35..63. libwebrtc's picker keeps a table of
//   default assignments instead of the engine's payload type; for this library's codecs both give the same values.
// - A header extension id is the recorder's id for the URI, else the preferred id if no other URI has it, else the
//   highest free id from 14 down to 1, then (two-byte ids allowed) 15 up to 255. This reproduces libwebrtc's
//   offers (for example toffset at 14 and video-orientation at 13 next to the audio section's ids).
// - No BundlePolicy: bundle groups are taken from the descriptions.
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sm/pc/codec.h"
#include "sm/pc/rtp_parameters.h"
#include "sm/pc/session_description.h"

namespace sm::pc {

enum class RtpTransceiverIdDomain { kOneByteOnly, kTwoByteAllowed };

class SdpPayloadTypeSuggester {
 public:
  SdpPayloadTypeSuggester() = default;
  SdpPayloadTypeSuggester(const SdpPayloadTypeSuggester&) = delete;
  SdpPayloadTypeSuggester& operator=(const SdpPayloadTypeSuggester&) = delete;

  // A payload type for `codec` in the m= section `mid`; nullopt when none is free.
  std::optional<int> SuggestPayloadType(std::string_view mid, const Codec& codec);
  // False if `payload_type` is already mapped to a different codec.
  bool AddLocalMapping(std::string_view mid, int payload_type, const Codec& codec);
  // An id for `extension` on `mid`; nullopt when none is free in `id_domain`.
  std::optional<int> SuggestRtpHeaderExtensionId(std::string_view mid, const RtpExtension& extension,
                                                 RtpTransceiverIdDomain id_domain);
  // Records `extension.id` as mapped to its URI.
  bool AddRtpHeaderExtensionMapping(std::string_view mid, const RtpExtension& extension, bool local);
  // Records the bundle groups and the payload types and header extensions of `description`, set as local or
  // remote with `type`. Payload types that it does not use are released. False on a payload type conflict.
  bool Update(const SessionDescription* description, bool local, p2p::SdpType type);

 private:
  using ExtensionKey = std::pair<std::string, bool>;  // URI, encrypted.
  struct Recorder {
    std::map<int, Codec> local_payload_types;
    std::map<int, Codec> remote_payload_types;
    std::map<int, ExtensionKey> header_extensions;
  };
  Recorder& LookupRecorder(std::string_view mid);
  std::string RecorderName(std::string_view mid) const;
  bool IsPayloadTypeSeen(int payload_type) const { return seen_payload_types_.count(payload_type) > 0; }

  std::vector<ContentGroup> bundle_groups_;          // Committed (from the last answer).
  std::vector<ContentGroup> pending_bundle_groups_;  // From the last offer.
  bool has_pending_ = false;
  std::map<std::string, Recorder> recorders_;
  std::set<int> seen_payload_types_;
  std::map<int, ExtensionKey> extension_ids_;  // Every id handed out or recorded, across all sections.
};

// codec_comparators: whether two codecs are the same codec for SDP (name or static payload type, clock rate and
// channels for audio, the codec-specific parameters for video: H.264 profile and packetization-mode).
bool MatchesWithCodecRules(const Codec& a, const Codec& b);

}  // namespace sm::pc
