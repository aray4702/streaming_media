// sm/pc/simulcast_description.h: libwebrtc M157's pc/simulcast_description.cc and pc/simulcast_sdp_serializer.cc
// (compare/webrtc/reference/pc/), #1062.
#include "sm/pc/simulcast_description.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "sm/p2p/string_encode.h"
#include "sm/pc/session_description.h"

namespace sm::pc {
namespace {

constexpr char kSimulcastPausedStream[] = "~";
constexpr char kSimulcastPausedStreamChar = '~';
constexpr char kSendDirection[] = "send";
constexpr char kReceiveDirection[] = "recv";
constexpr char kPayloadType[] = "pt";

std::nullopt_t ParseError(std::string* error, std::string_view message) {
  if (error) *error = std::string(message);
  return std::nullopt;
}

void AppendLayer(std::string& builder, const SimulcastLayer& layer) {
  if (layer.is_paused) builder += kSimulcastPausedStream;
  builder += layer.rid;
}

void AppendLayerList(std::string& builder, const SimulcastLayerList& simulcast_layers) {
  bool first = true;
  for (const auto& alternatives : simulcast_layers) {
    if (!first) builder += ";";
    bool first_alternative = true;
    for (const SimulcastLayer& rid : alternatives) {
      if (!first_alternative) builder += ",";
      AppendLayer(builder, rid);
      first_alternative = false;
    }
    first = false;
  }
}

// These methods deserialize simulcast according to the specification:
// https://tools.ietf.org/html/draft-ietf-mmusic-sdp-simulcast-13#section-5.1
// sc-str-list  = sc-alt-list *( ";" sc-alt-list )
// sc-alt-list  = sc-id *( "," sc-id )
// sc-id-paused = "~"
// sc-id        = [sc-id-paused] rid-id
// rid-id       = 1*(alpha-numeric / "-" / "_") ; see: I-D.ietf-mmusic-rid
std::optional<SimulcastLayerList> ParseSimulcastLayerList(const std::string& str, std::string* error) {
  std::vector<std::string_view> tokens = p2p::split(str, ';');
  if (tokens.empty()) return ParseError(error, "Layer list cannot be empty.");
  SimulcastLayerList result;
  size_t total_rids = 0;
  for (const std::string_view& token : tokens) {
    if (token.empty()) return ParseError(error, "Simulcast alternative layer list is empty.");
    std::vector<std::string_view> rid_tokens = p2p::split(token, ',');
    if (rid_tokens.empty()) return ParseError(error, "Simulcast alternative layer list is malformed.");
    total_rids += rid_tokens.size();
    if (total_rids > kMaxSimulcastRids) return ParseError(error, "Simulcast description contains too many RIDs.");
    std::vector<SimulcastLayer> layers;
    for (const std::string_view& rid_token : rid_tokens) {
      if (rid_token.empty() || rid_token == kSimulcastPausedStream) return ParseError(error, "Rid must not be empty.");
      bool paused = rid_token[0] == kSimulcastPausedStreamChar;
      std::string_view rid = paused ? rid_token.substr(1) : rid_token;
      layers.push_back(SimulcastLayer(rid, paused));
    }
    result.AddLayerWithAlternatives(layers);
  }
  return result;
}

bool ParseRidPayloadList(const std::string& payload_list, RidDescription* rid_description,
                         std::vector<int>* rid_payload_types, std::string* error) {
  // Check that the description doesn't have any payload types or restrictions. If the pt= field is specified, it
  // must be first and must not repeat.
  if (!rid_payload_types->empty()) return ParseError(error, "Multiple pt= found in RID Description."), false;
  if (!rid_description->restrictions.empty()) {
    return ParseError(error, "Payload list must appear first in the restrictions."), false;
  }
  // If the pt= field is specified, it must have a value.
  if (payload_list.empty()) return ParseError(error, "Payload list must have at least one value."), false;
  // Tokenize the ',' delimited list
  std::vector<std::string> string_payloads;
  p2p::tokenize(payload_list, ',', &string_payloads);
  if (string_payloads.empty()) return ParseError(error, "Payload list must have at least one value."), false;
  for (const std::string& payload_type : string_payloads) {
    int value = 0;
    if (!p2p::FromString(payload_type, &value)) return ParseError(error, "Invalid payload type: " + payload_type), false;
    // Check if the value already appears in the payload list.
    if (std::find(rid_payload_types->begin(), rid_payload_types->end(), value) != rid_payload_types->end()) {
      return ParseError(error, "Duplicate payload type in list: " + payload_type), false;
    }
    rid_payload_types->push_back(value);
  }
  return true;
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

// Stands in for IsSameRtpCodecIgnoringLevel (see the header).
bool IsSameCodecIgnoringLevel(const Codec& a, const Codec& b) {
  if (!EqualsIgnoreCase(a.name, b.name) || a.clockrate != b.clockrate || a.type != b.type) return false;
  if (a.type == Codec::Type::kAudio && a.channels != b.channels) return false;
  // The parameters with their SDP defaults filled in (as webrtc_sdp's BackfillCodecParameters), without the level
  // ones.
  static const std::set<std::string> kLevelParams = {kH264FmtpProfileLevelId, kH265FmtpLevelId, kAv1FmtpLevelIdx,
                                                     kAv1FmtpTier};
  auto normalized = [](const Codec& codec) {
    CodecParameterMap out = codec.params;
    if (EqualsIgnoreCase(codec.name, kVp9CodecName)) out.try_emplace(kVP9ProfileId, "0");
    if (EqualsIgnoreCase(codec.name, kH264CodecName)) out.try_emplace(kH264FmtpPacketizationMode, "0");
    if (EqualsIgnoreCase(codec.name, kAv1CodecName)) out.try_emplace(kAv1FmtpProfile, "0");
    if (EqualsIgnoreCase(codec.name, kH265CodecName)) out.try_emplace(kH265FmtpTxMode, "SRST");
    std::erase_if(out, [](const auto& entry) { return kLevelParams.contains(entry.first); });
    return out;
  };
  return normalized(a) == normalized(b);
}

}  // namespace

std::vector<SimulcastLayer> SimulcastLayerList::GetAllLayers() const {
  std::vector<SimulcastLayer> result;
  for (const auto& group : list_) result.insert(result.end(), group.begin(), group.end());
  return result;
}

bool IsLegalRsidName(std::string_view name) {
  return !name.empty() && name.size() <= 16 &&
         std::all_of(name.begin(), name.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)); });
}

std::string SimulcastSdpSerializer::SerializeSimulcastDescription(const SimulcastDescription& simulcast) const {
  std::string sb;
  std::string delimiter;
  if (!simulcast.send_layers().empty()) {
    sb += std::string(kSendDirection) + " ";
    AppendLayerList(sb, simulcast.send_layers());
    delimiter = " ";
  }
  if (!simulcast.receive_layers().empty()) {
    sb += delimiter + kReceiveDirection + " ";
    AppendLayerList(sb, simulcast.receive_layers());
  }
  return sb;
}

// https://tools.ietf.org/html/draft-ietf-mmusic-sdp-simulcast-13#section-5.1
// a:simulcast:<send> <streams> <recv> <streams>
std::optional<SimulcastDescription> SimulcastSdpSerializer::DeserializeSimulcastDescription(
    std::string_view string, std::string* error) const {
  std::vector<std::string> tokens;
  p2p::tokenize(string, ' ', &tokens);
  if (tokens.size() != 2 && tokens.size() != 4) {
    return ParseError(error, "Must have one or two <direction, streams> pairs.");
  }
  bool bidirectional = tokens.size() == 4;  // indicates both send and recv
  // Tokens 0, 2 (if exists) should be send / recv
  if ((tokens[0] != kSendDirection && tokens[0] != kReceiveDirection) ||
      (bidirectional && tokens[2] != kSendDirection && tokens[2] != kReceiveDirection) ||
      (bidirectional && tokens[0] == tokens[2])) {
    return ParseError(error, "Valid values: send / recv.");
  }
  // Tokens 1, 3 (if exists) should be alternative layer lists
  std::optional<SimulcastLayerList> list1 = ParseSimulcastLayerList(tokens[1], error);
  if (!list1) return std::nullopt;
  std::optional<SimulcastLayerList> list2;
  if (bidirectional) {
    list2 = ParseSimulcastLayerList(tokens[3], error);
    if (!list2) return std::nullopt;
  }
  // Set the layers so that list1 is for send and list2 is for recv
  if (tokens[0] != kSendDirection) std::swap(list1, list2);
  SimulcastDescription simulcast;
  if (list1) simulcast.send_layers() = std::move(*list1);
  if (list2) simulcast.receive_layers() = std::move(*list2);
  return simulcast;
}

std::string SimulcastSdpSerializer::SerializeRidDescription(const MediaContentDescription& media_desc,
                                                            const RidDescription& rid_description) const {
  std::string builder =
      rid_description.rid + " " + (rid_description.direction == RidDirection::kSend ? kSendDirection : kReceiveDirection);
  // Find the m-section payload types of the RID's codecs.
  std::vector<int> payload_types;
  for (const Codec& codec : rid_description.codecs) {
    const auto it = std::find_if(media_desc.codecs().begin(), media_desc.codecs().end(),
                                 [&codec](const Codec& m_section_codec) {
                                   return IsSameCodecIgnoringLevel(m_section_codec, codec);
                                 });
    // The desired codec from the RID was not found in the media section.
    if (it == media_desc.codecs().end()) break;
    if (it->id == Codec::kIdNotSet) break;
    payload_types.push_back(it->id);
  }
  const char* property_delimiter = " ";
  if (!payload_types.empty()) {
    builder += std::string(property_delimiter) + kPayloadType + "=";
    property_delimiter = ";";
    const char* format_delimiter = "";
    for (int payload_type : payload_types) {
      builder += format_delimiter + std::to_string(payload_type);
      format_delimiter = ",";
    }
  }
  for (const auto& pair : rid_description.restrictions) {
    builder += property_delimiter + pair.first;
    if (!pair.second.empty()) builder += "=" + pair.second;
    property_delimiter = ";";
  }
  return builder;
}

// https://tools.ietf.org/html/draft-ietf-mmusic-rid-15#section-10
// Formal Grammar
// rid-syntax         = %s"a=rid:" rid-id SP rid-dir [ rid-pt-param-list / rid-param-list ]
// rid-id             = 1*(alpha-numeric / "-" / "_")
// rid-dir            = %s"send" / %s"recv"
// rid-pt-param-list  = SP rid-fmt-list *( ";" rid-param )
// rid-param-list     = SP rid-param *( ";" rid-param )
// rid-fmt-list       = %s"pt=" fmt *( "," fmt )
// rid-param          = 1*(alpha-numeric / "-") [ "=" param-val ]
// param-val          = *( %x20-58 / %x60-7E )
//                      ; Any printable character except semicolon
std::optional<RidDescription> SimulcastSdpSerializer::DeserializeRidDescription(
    const MediaContentDescription& media_desc, std::string_view string, std::string* error) const {
  std::vector<std::string> tokens;
  p2p::tokenize(string, ' ', &tokens);
  if (tokens.size() < 2) return ParseError(error, "RID Description must contain <RID> <direction>.");
  if (tokens.size() > 3) return ParseError(error, "Invalid RID Description format. Too many arguments.");
  if (!IsLegalRsidName(tokens[0])) return ParseError(error, "Invalid RID value: " + tokens[0] + ".");
  if (tokens[1] != kSendDirection && tokens[1] != kReceiveDirection) {
    return ParseError(error, "Invalid RID direction. Supported values: send / recv.");
  }
  RidDirection direction = tokens[1] == kSendDirection ? RidDirection::kSend : RidDirection::kReceive;
  RidDescription rid_description(tokens[0], direction);
  // If there is a third argument it is a payload list and/or restriction list.
  std::vector<int> rid_payload_types;
  if (tokens.size() == 3) {
    std::vector<std::string> restrictions;
    p2p::tokenize(tokens[2], ';', &restrictions);
    // Check for malformed restriction list, such as ';' or ';;;' etc.
    if (restrictions.empty()) return ParseError(error, "Invalid RID restriction list: " + tokens[2]);
    // Parse the restrictions. The payload indicator (pt) can only appear first.
    for (const std::string& restriction : restrictions) {
      std::vector<std::string> parts;
      p2p::tokenize(restriction, '=', &parts);
      if (parts.empty() || parts.size() > 2) return ParseError(error, "Invalid format for restriction: " + restriction);
      // `parts` contains at least one value and it does not contain a space. Note: `parts` and other values might
      // still contain tab, newline, unprintable characters, etc. which will not generate errors here but will
      // (most-likely) be ignored by components down stream.
      if (parts[0] == kPayloadType) {
        if (!ParseRidPayloadList(parts.size() > 1 ? parts[1] : std::string(), &rid_description, &rid_payload_types,
                                 error)) {
          return std::nullopt;
        }
        continue;
      }
      // Parse `parts` as a key=value pair which allows unspecified values.
      if (rid_description.restrictions.find(parts[0]) != rid_description.restrictions.end()) {
        return ParseError(error, "Duplicate restriction specified: " + parts[0]);
      }
      rid_description.restrictions[parts[0]] = parts.size() > 1 ? parts[1] : std::string();
    }
  }
  // Look up the codecs from the media section by payload type; an unknown payload type is skipped.
  for (const int& payload_type : rid_payload_types) {
    const auto it = std::find_if(media_desc.codecs().begin(), media_desc.codecs().end(),
                                 [&payload_type](const Codec& m_section_codec) {
                                   return m_section_codec.id == payload_type;
                                 });
    if (it == media_desc.codecs().end()) continue;
    rid_description.codecs.push_back(*it);
  }
  return rid_description;
}

}  // namespace sm::pc
