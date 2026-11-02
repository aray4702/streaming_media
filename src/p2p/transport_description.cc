// libwebrtc M157's p2p/base/transport_description.cc (compare/webrtc/reference/p2p/base/), #1062.
#include "sm/p2p/transport_description.h"

#include <cctype>
#include <iterator>

#include "sm/p2p/p2p_constants.h"

namespace sm::p2p {
namespace {

bool IsValidIceChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '/'; }

bool IsIceChar(char c) {
  if (c == '-' || c == '=' || c == '#' || c == '_') {
    // Not ice-char (RFC 5245 15.1), but libwebrtc still accepts them to allow upgrading (crbug.com/1053756).
    return true;
  }
  return IsValidIceChar(c);
}

bool AllIceChars(std::string_view s) {
  for (char c : s) {
    if (!IsIceChar(c)) return false;
  }
  return true;
}

std::string ValidateIceUfrag(std::string_view raw_ufrag) {
  if (!(ICE_UFRAG_MIN_LENGTH <= raw_ufrag.size() && raw_ufrag.size() <= ICE_UFRAG_MAX_LENGTH)) {
    return "ICE ufrag must be between " + std::to_string(ICE_UFRAG_MIN_LENGTH) + " and " +
           std::to_string(ICE_UFRAG_MAX_LENGTH) + " characters long.";
  }
  if (!AllIceChars(raw_ufrag)) return "ICE ufrag must contain only alphanumeric characters, '+', and '/'.";
  return {};
}

std::string ValidateIcePwd(std::string_view raw_pwd) {
  if (!(ICE_PWD_MIN_LENGTH <= raw_pwd.size() && raw_pwd.size() <= ICE_PWD_MAX_LENGTH)) {
    return "ICE pwd must be between " + std::to_string(ICE_PWD_MIN_LENGTH) + " and " +
           std::to_string(ICE_PWD_MAX_LENGTH) + " characters long.";
  }
  if (!AllIceChars(raw_pwd)) return "ICE pwd must contain only alphanumeric characters, '+', and '/'.";
  return {};
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

}  // namespace

std::optional<IceParameters> IceParameters::Parse(std::string_view raw_ufrag, std::string_view raw_pwd,
                                                  std::string* error) {
  IceParameters parameters(raw_ufrag, raw_pwd, /*ice_renomination=*/false);
  std::string result = parameters.Validate();
  if (!result.empty()) {
    if (error) *error = std::move(result);
    return std::nullopt;
  }
  return parameters;
}

std::string IceParameters::Validate() const {
  // For legacy protocols, the ufrag and password may be empty.
  if (ufrag.empty() && pwd.empty()) return {};
  std::string ufrag_result = ValidateIceUfrag(ufrag);
  if (!ufrag_result.empty()) return ufrag_result;
  return ValidateIcePwd(pwd);
}

std::optional<ConnectionRole> StringToConnectionRole(std::string_view role_str) {
  const char* const roles[] = {CONNECTIONROLE_ACTIVE_STR, CONNECTIONROLE_PASSIVE_STR, CONNECTIONROLE_ACTPASS_STR,
                               CONNECTIONROLE_HOLDCONN_STR};
  const ConnectionRole values[] = {ConnectionRole::kActive, ConnectionRole::kPassive, ConnectionRole::kActpass,
                                   ConnectionRole::kHoldconn};
  for (size_t i = 0; i < std::size(roles); ++i) {
    if (EqualsIgnoreCase(roles[i], role_str)) return values[i];
  }
  return std::nullopt;
}

bool ConnectionRoleToString(ConnectionRole role, std::string* role_str) {
  switch (role) {
    case ConnectionRole::kActive:
      *role_str = CONNECTIONROLE_ACTIVE_STR;
      break;
    case ConnectionRole::kActpass:
      *role_str = CONNECTIONROLE_ACTPASS_STR;
      break;
    case ConnectionRole::kPassive:
      *role_str = CONNECTIONROLE_PASSIVE_STR;
      break;
    case ConnectionRole::kHoldconn:
      *role_str = CONNECTIONROLE_HOLDCONN_STR;
      break;
    default:
      return false;
  }
  return true;
}

}  // namespace sm::p2p
