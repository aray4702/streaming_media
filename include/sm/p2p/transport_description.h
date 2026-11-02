// libwebrtc M157's p2p/base/transport_description.h (compare/webrtc/reference/p2p/base/): the ICE role, mode and
// credentials that the port, the connection (#1000) and the ICE transport share. One definition, so the port and the
// ICE transport interface (#1006) can be included together.
#pragma once

#include <string>

namespace sm::p2p {

// Whether our side of the call is in the ICE controlling or controlled role.
enum IceRole { ICEROLE_CONTROLLING = 0, ICEROLE_CONTROLLED, ICEROLE_UNKNOWN };

// ICE RFC 5245 implementation type.
enum IceMode {
  ICEMODE_FULL,  // As defined in http://tools.ietf.org/html/rfc5245#section-4.1
  ICEMODE_LITE   // As defined in http://tools.ietf.org/html/rfc5245#section-4.2
};

struct IceParameters {
  std::string ufrag;
  std::string pwd;
  bool renomination = false;
  friend bool operator==(const IceParameters&, const IceParameters&) = default;
};

}  // namespace sm::p2p
