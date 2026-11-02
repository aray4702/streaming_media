// libwebrtc M157's p2p/base/transport_description.h and transport_info.h (compare/webrtc/reference/p2p/base/): the
// ICE role, mode and credentials that the port, the connection (#1000) and the ICE transport share, and the
// transport half of a session description (#1062, C6 part 1): ice-ufrag/ice-pwd, ice-options, ice-lite, the DTLS
// fingerprint and a=setup, per m= section (TransportInfo). One definition, so the port, the ICE transport interface
// (#1006), the DTLS transport and the SDP layer (sm/pc/session_description.h) can be included together.
//
// Differences from libwebrtc:
// - ConnectionRole is an enum class (kNone, kActive, ...) rather than CONNECTIONROLE_*; SdpType (api/jsep.h) is here
//   too, because the DTLS role negotiation (sm/p2p/dtls_transport.h) needs it without the SDP layer.
// - IceParameters::Parse/Validate return the error text (empty when valid) instead of RTCError(SYNTAX_ERROR).
#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sm/tls/ssl_fingerprint.h"

namespace sm::p2p {

// Whether our side of the call is in the ICE controlling or controlled role.
enum IceRole { ICEROLE_CONTROLLING = 0, ICEROLE_CONTROLLED, ICEROLE_UNKNOWN };

// ICE RFC 5245 implementation type.
enum IceMode {
  ICEMODE_FULL,  // As defined in http://tools.ietf.org/html/rfc5245#section-4.1
  ICEMODE_LITE   // As defined in http://tools.ietf.org/html/rfc5245#section-4.2
};

// RFC 4145 section 4: SDP's a=setup.
enum class ConnectionRole { kNone, kActive, kPassive, kActpass, kHoldconn };

// api/jsep.h's SdpType.
enum class SdpType {
  kOffer,     // An SDP offer.
  kPrAnswer,  // An answer, but not a final one: it may be changed by a later answer.
  kAnswer,    // A final answer.
  kRollback   // Resets any pending offer and sets the signaling state back to stable.
};

inline constexpr char CONNECTIONROLE_ACTIVE_STR[] = "active";
inline constexpr char CONNECTIONROLE_PASSIVE_STR[] = "passive";
inline constexpr char CONNECTIONROLE_ACTPASS_STR[] = "actpass";
inline constexpr char CONNECTIONROLE_HOLDCONN_STR[] = "holdconn";

inline constexpr std::string_view ICE_OPTION_TRICKLE = "trickle";
inline constexpr std::string_view ICE_OPTION_RENOMINATION = "renomination";
inline constexpr std::string_view ICE_OPTION_GOOG_SPED_V1 = "googspedv1";
inline constexpr std::string_view ICE_OPTION_SPED = "sped";

struct IceParameters {
  IceParameters() = default;
  IceParameters(std::string_view ice_ufrag, std::string_view ice_pwd, bool ice_renomination)
      : ufrag(ice_ufrag), pwd(ice_pwd), renomination(ice_renomination) {}

  // Constructs from a raw ufrag and password; nullopt (with the reason in `error`) if they are invalid.
  static std::optional<IceParameters> Parse(std::string_view raw_ufrag, std::string_view raw_pwd,
                                            std::string* error = nullptr);

  std::string ufrag;
  std::string pwd;
  bool renomination = false;
  friend bool operator==(const IceParameters&, const IceParameters&) = default;

  // RFC 5245 15.4: 4-256 ice-chars of ufrag, 22-256 of password (both empty is valid). Empty when valid.
  std::string Validate() const;
};

// Case-insensitive, as libwebrtc.
std::optional<ConnectionRole> StringToConnectionRole(std::string_view role_str);
bool ConnectionRoleToString(ConnectionRole role, std::string* role_str);

// The ICE and DTLS parameters of one transport, as an m= section (or the session level) describes them.
struct TransportDescription {
  using SslFingerprint = tls::SslFingerprint;

  TransportDescription() = default;
  TransportDescription(const std::vector<std::string>& transport_options, std::string_view ice_ufrag,
                       std::string_view ice_pwd, IceMode ice_mode, ConnectionRole role,
                       const SslFingerprint* identity_fingerprint)
      : transport_options(transport_options),
        ice_ufrag(ice_ufrag),
        ice_pwd(ice_pwd),
        ice_mode(ice_mode),
        connection_role(role),
        identity_fingerprint(CopyFingerprint(identity_fingerprint)) {}
  TransportDescription(std::string_view ice_ufrag, std::string_view ice_pwd) : ice_ufrag(ice_ufrag), ice_pwd(ice_pwd) {}
  TransportDescription(const TransportDescription& from)
      : transport_options(from.transport_options),
        ice_ufrag(from.ice_ufrag),
        ice_pwd(from.ice_pwd),
        ice_mode(from.ice_mode),
        connection_role(from.connection_role),
        identity_fingerprint(CopyFingerprint(from.identity_fingerprint.get())),
        cryptex(from.cryptex) {}
  TransportDescription& operator=(const TransportDescription& from) {
    if (this == &from) return *this;
    transport_options = from.transport_options;
    ice_ufrag = from.ice_ufrag;
    ice_pwd = from.ice_pwd;
    ice_mode = from.ice_mode;
    connection_role = from.connection_role;
    identity_fingerprint = CopyFingerprint(from.identity_fingerprint.get());
    cryptex = from.cryptex;
    return *this;
  }

  bool HasOption(std::string_view option) const {
    return std::find(transport_options.begin(), transport_options.end(), option) != transport_options.end();
  }
  void AddOption(std::string_view option) { transport_options.emplace_back(option); }
  bool secure() const { return identity_fingerprint != nullptr; }
  IceParameters GetIceParameters() const {
    return IceParameters(ice_ufrag, ice_pwd, HasOption(ICE_OPTION_RENOMINATION));
  }
  static std::unique_ptr<SslFingerprint> CopyFingerprint(const SslFingerprint* from) {
    return from ? std::make_unique<SslFingerprint>(*from) : nullptr;
  }

  std::vector<std::string> transport_options;
  std::string ice_ufrag;
  std::string ice_pwd;
  IceMode ice_mode = ICEMODE_FULL;
  ConnectionRole connection_role = ConnectionRole::kNone;
  std::unique_ptr<SslFingerprint> identity_fingerprint;
  bool cryptex = false;
};

// p2p/base/transport_info.h: the transport of one content (m= section), by its mid.
struct TransportInfo {
  TransportInfo() = default;
  TransportInfo(std::string_view content_name, const TransportDescription& description)
      : content_name(content_name), description(description) {}

  std::string content_name;
  TransportDescription description;
};
using TransportInfos = std::vector<TransportInfo>;

}  // namespace sm::p2p
