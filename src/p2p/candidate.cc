// sm/p2p/candidate.h: libwebrtc M157's api/candidate.cc and
// p2p/base/p2p_constants.cc's IsMdnsHostname.
#include "sm/p2p/candidate.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iterator>
#include <optional>
#include <span>
#include <vector>

#include "sm/p2p/p2p_constants.h"
#include "sm/p2p/string_encode.h"
#include "sm/p2p/stun.h"

namespace sm::p2p {
namespace {

// port_interface.h's ProtocolType, without including the port.
enum class ProtocolKind { kUdp, kTcp, kSsltcp, kTls };

constexpr char kCandidateHost[] = "host";
constexpr char kCandidateSrflx[] = "srflx";
constexpr char kCandidatePrflx[] = "prflx";
constexpr char kCandidateRelay[] = "relay";

// GetFoundationKey: 32 random bytes (the size of a SHA-256 output), made once
// per process and never sent. Taken from the installed crypto the first time
// it is available; until then (tests without crypto) a fixed key is used and
// not kept.
std::span<const uint8_t> FoundationKey() {
  static std::array<uint8_t, 32> key{};
  static bool have_key = false;
  if (!have_key) {
    const StunCrypto& c = GetStunCrypto();
    if (c.random_bytes != nullptr && c.random_bytes(key)) {
      have_key = true;
    } else {
      key.fill(0);
    }
  }
  return key;
}

std::string PortString(const SocketAddress& a) { return std::to_string(a.port()); }

bool IsValidPort(int port) { return port >= 0 && port <= 65535; }

// port_interface.h's StringToProto: case-insensitive.
std::optional<ProtocolKind> StringToProtoKind(std::string_view name) {
  static constexpr std::string_view kNames[] = {UDP_PROTOCOL_NAME, TCP_PROTOCOL_NAME, SSLTCP_PROTOCOL_NAME,
                                                TLS_PROTOCOL_NAME};
  for (size_t i = 0; i < std::size(kNames); ++i) {
    if (name.size() != kNames[i].size()) continue;
    bool equal = true;
    for (size_t j = 0; j < name.size() && equal; ++j) {
      equal = std::tolower(static_cast<unsigned char>(name[j])) == kNames[i][j];
    }
    if (equal) return static_cast<ProtocolKind>(i);
  }
  return std::nullopt;
}

// SSLFingerprint::CreateOptionalFromRfc4572 as the candidate parser uses it: a FIPS 180 algorithm (lower case) and
// one or more colon-separated hex bytes. Checked only: the candidate keeps no fingerprint (see the header).
bool IsValidRfc4572Fingerprint(std::string_view algorithm, std::string_view digest) {
  static constexpr std::string_view kFips180[] = {"sha-1", "sha-224", "sha-256", "sha-384", "sha-512"};
  std::string lower(algorithm);
  for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  bool known = false;
  for (std::string_view a : kFips180) known = known || lower == a;
  if (!known || digest.empty()) return false;
  size_t bytes = 0;
  for (size_t i = 0; i < digest.size();) {
    if (i + 1 >= digest.size() || !std::isxdigit(static_cast<unsigned char>(digest[i])) ||
        !std::isxdigit(static_cast<unsigned char>(digest[i + 1]))) {
      return false;
    }
    ++bytes;
    i += 2;
    if (i < digest.size()) {
      if (digest[i] != ':' || i + 1 >= digest.size()) return false;
      ++i;
    }
  }
  return bytes > 0 && bytes <= 64;  // MessageDigest::kMaxSize.
}

bool Fail(std::string* error, std::string_view reason) {
  if (error) *error = std::string(reason);
  return false;
}

}  // namespace

bool IsMdnsHostname(std::string_view hostname) {
  if (!hostname.empty() && hostname.back() == '.') hostname.remove_suffix(1);
  constexpr std::string_view kLocalTld = ".local";
  if (hostname.size() < kLocalTld.size()) return false;
  std::string_view tail = hostname.substr(hostname.size() - kLocalTld.size());
  for (size_t i = 0; i < tail.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(tail[i])) != kLocalTld[i]) return false;
  }
  return true;
}

std::string_view IceCandidateTypeToString(IceCandidateType type) {
  switch (type) {
    case IceCandidateType::kHost: return kCandidateHost;
    case IceCandidateType::kSrflx: return kCandidateSrflx;
    case IceCandidateType::kPrflx: return kCandidatePrflx;
    case IceCandidateType::kRelay: return kCandidateRelay;
  }
  return kCandidateHost;
}

std::optional<IceCandidateType> StringToIceCandidateType(std::string_view type) {
  if (type == kCandidateHost) return IceCandidateType::kHost;
  if (type == kCandidateSrflx) return IceCandidateType::kSrflx;
  if (type == kCandidatePrflx) return IceCandidateType::kPrflx;
  if (type == kCandidateRelay) return IceCandidateType::kRelay;
  return std::nullopt;
}

Candidate::Candidate() : id_(CreateRandomString(8)), component_(ICE_CANDIDATE_COMPONENT_DEFAULT) {}

Candidate::Candidate(int component, std::string_view protocol, const SocketAddress& address, uint32_t priority,
                     std::string_view username, std::string_view password, IceCandidateType type,
                     uint32_t generation, std::string_view foundation, uint16_t network_id, uint16_t network_cost)
    : id_(CreateRandomString(8)),
      component_(component),
      protocol_(protocol),
      address_(address),
      priority_(priority),
      username_(username),
      password_(password),
      type_(type),
      generation_(generation),
      foundation_(foundation),
      network_id_(network_id),
      network_cost_(network_cost) {}

void Candidate::generate_id() { id_ = CreateRandomString(8); }

bool Candidate::IsEquivalent(const Candidate& c) const {
  return component_ == c.component_ && protocol_ == c.protocol_ && address_ == c.address_ &&
         username_ == c.username_ && password_ == c.password_ && type_ == c.type_ &&
         generation_ == c.generation_ && foundation_ == c.foundation_ && related_address_ == c.related_address_ &&
         network_id_ == c.network_id_;
}

bool Candidate::MatchesForRemoval(const Candidate& c) const {
  return component_ == c.component_ && protocol_ == c.protocol_ && address_ == c.address_;
}

std::string Candidate::ToStringInternal(bool sensitive) const {
  const std::string address = sensitive ? address_.ToSensitiveString() : address_.ToString();
  const std::string related = sensitive ? related_address_.ToSensitiveString() : related_address_.ToString();
  return "Cand[:" + foundation_ + ":" + std::to_string(component_) + ":" + protocol_ + ":" +
         std::to_string(priority_) + ":" + address + ":" + std::string(type_name()) + ":" + related + ":" +
         username_ + ":" + password_ + ":" + std::to_string(network_id_) + ":" + std::to_string(network_cost_) +
         ":" + std::to_string(generation_) + "]";
}

std::string Candidate::ToCandidateAttribute(bool include_ufrag) const {
  // BuildCandidate.
  std::string os = "candidate:" + foundation_ + " " + std::to_string(component_) + " " + protocol_ + " " +
                   std::to_string(priority_) + " " +
                   (IpIsUnspec(address_.ipaddr()) ? address_.hostname() : address_.ipaddr().ToString()) + " " +
                   PortString(address_) +
                   " typ " + std::string(type_name()) + " ";
  if (!related_address_.IsNil()) {
    os += "raddr " + related_address_.ipaddr().ToString() + " rport " + PortString(related_address_) + " ";
  }
  if ((protocol_ == TCP_PROTOCOL_NAME || protocol_ == TLS_PROTOCOL_NAME) && !tcptype_.empty()) {
    os += "tcptype " + tcptype_ + " ";
  }
  os += "generation " + std::to_string(generation_);
  if (include_ufrag && !username_.empty()) os += " ufrag " + username_;
  if (network_id_ > 0) os += " network-id " + std::to_string(network_id_);
  if (network_cost_ > 0) os += " network-cost " + std::to_string(network_cost_);
  return os;
}

Candidate Candidate::ToSanitizedCopy(bool use_hostname_address, bool filter_related_address,
                                     bool filter_ufrag) const {
  Candidate copy(*this);
  if (use_hostname_address) {
    if (address().hostname().empty()) {
      // IP needs to be redacted, but no hostname available.
      copy.set_address(SocketAddress("redacted-ip.invalid", address().port()));
    } else if (IpAddress::FromString(address().hostname())) {
      // The hostname is an IP literal, and needs to be redacted too.
      copy.set_address(SocketAddress("redacted-literal.invalid", address().port()));
    } else {
      copy.set_address(SocketAddress(address().hostname(), address().port()));
    }
  }
  if (filter_related_address) {
    // EmptySocketAddressWithFamily.
    const IpFamily family = copy.address().family();
    copy.set_related_address(family == IpFamily::kUnspec ? SocketAddress() : SocketAddress(GetAnyIp(family), 0));
  }
  if (filter_ufrag) copy.set_username("");
  return copy;
}

uint32_t Candidate::GetPriority(uint32_t type_preference, int network_adapter_preference, int relay_preference,
                                bool adjust_local_preference) const {
  const int addr_pref = IpAddressPrecedence(address_.ipaddr());
  int local_preference = ((network_adapter_preference << 8) | addr_pref) + relay_preference;
  // Adding kMaxTurnServers can't overflow 16 bits: the highest network
  // preference is 127.
  if (adjust_local_preference && relay_protocol_.empty()) local_preference += static_cast<int>(kMaxTurnServers);
  return (type_preference << 24) | (static_cast<uint32_t>(local_preference) << 8) |
         static_cast<uint32_t>(256 - component_);
}

bool Candidate::operator==(const Candidate& o) const {
  return id_ == o.id_ && component_ == o.component_ && protocol_ == o.protocol_ &&
         relay_protocol_ == o.relay_protocol_ && address_ == o.address_ && priority_ == o.priority_ &&
         username_ == o.username_ && password_ == o.password_ && type_ == o.type_ &&
         network_name_ == o.network_name_ && network_type_ == o.network_type_ && generation_ == o.generation_ &&
         foundation_ == o.foundation_ && related_address_ == o.related_address_ && tcptype_ == o.tcptype_ &&
         network_id_ == o.network_id_;
}

void Candidate::ComputeFoundation(const SocketAddress& base_address, uint64_t tie_breaker) {
  const std::string input = std::string(type_name()) + base_address.ipaddr().ToString() + protocol_ +
                            relay_protocol_ + std::to_string(tie_breaker);
  const std::span<const uint8_t> key = FoundationKey();
  const std::span<const uint8_t> data(reinterpret_cast<const uint8_t*>(input.data()), input.size());
  uint32_t value = 0;
  uint8_t digest[32];
  const StunCrypto& c = GetStunCrypto();
  if (c.hmac_sha256 != nullptr && c.hmac_sha256(key, data, digest)) {
    // GetBE32 of the digest.
    value = (uint32_t{digest[0]} << 24) | (uint32_t{digest[1]} << 16) | (uint32_t{digest[2]} << 8) | digest[3];
  } else {
    // No crypto installed (tests only, see the header).
    std::vector<uint8_t> keyed(key.begin(), key.end());
    keyed.insert(keyed.end(), data.begin(), data.end());
    value = ComputeCrc32(keyed);
  }
  foundation_ = std::to_string(value);
}

void Candidate::ComputePrflxFoundation() {
  foundation_ = std::to_string(
      ComputeCrc32(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(id_.data()), id_.size())));
}

std::optional<Candidate> Candidate::ParseCandidateString(std::string_view message, std::string* error) {
  // Makes sure `message` contains only one line.
  std::string_view first_line;
  const size_t line_end = message.find('\n');
  if (line_end == std::string_view::npos) {
    first_line = message;
  } else if (line_end + 1 == message.size()) {
    first_line = message.substr(0, line_end);
  } else {
    Fail(error, "Expect one line only");
    return std::nullopt;
  }
  // For backwards compatibility, accept "a=candidate...".
  if (first_line.length() > 2 && first_line[0] == 'a' && first_line[1] == '=') first_line = first_line.substr(2);
  if (!first_line.empty() && first_line.back() == '\r') first_line.remove_suffix(1);

  std::string attribute_candidate;
  std::string candidate_value;
  if (!tokenize_first(first_line, ':', &attribute_candidate, &candidate_value) ||
      attribute_candidate != "candidate") {
    Fail(error, "Expected candidate got " + attribute_candidate);
    return std::nullopt;
  }
  const std::vector<std::string_view> fields = split(candidate_value, ' ');
  // a=candidate:<foundation> <component-id> <transport> <priority> <connection-address> <port> typ
  // <candidate-types> [raddr <connection-address>] [rport <port>] *(SP extension-att-name SP extension-att-value)
  const size_t expected_min_fields = 8;
  if (fields.size() < expected_min_fields || fields[6] != "typ") {
    Fail(error, "Expect at least " + std::to_string(expected_min_fields) + " fields.");
    return std::nullopt;
  }
  const std::string_view foundation = fields[0];
  int component_id = 0;
  if (!FromString(fields[1], &component_id)) {
    Fail(error, "Invalid component id");
    return std::nullopt;
  }
  const std::string_view transport = fields[2];
  uint32_t priority = 0;
  if (!FromString(fields[3], &priority)) {
    Fail(error, "Invalid priority");
    return std::nullopt;
  }
  int port = 0;
  if (!FromString(fields[5], &port) || !IsValidPort(port)) {
    Fail(error, "Invalid port");
    return std::nullopt;
  }
  const std::optional<ProtocolKind> protocol = StringToProtoKind(transport);
  if (!protocol) {
    Fail(error, "Unsupported transport type");
    return std::nullopt;
  }
  const bool tcp_protocol = *protocol != ProtocolKind::kUdp;
  IceCandidateType candidate_type;
  const std::string_view type = fields[7];
  if (type == "host") {
    candidate_type = IceCandidateType::kHost;
  } else if (type == "srflx") {
    candidate_type = IceCandidateType::kSrflx;
  } else if (type == "relay") {
    candidate_type = IceCandidateType::kRelay;
  } else if (type == "prflx") {
    candidate_type = IceCandidateType::kPrflx;
  } else {
    Fail(error, "Unsupported candidate type");
    return std::nullopt;
  }

  size_t current_position = expected_min_fields;
  SocketAddress related_address;
  if (fields.size() >= current_position + 2 && fields[current_position] == "raddr") {
    related_address.SetIP(fields[++current_position]);
    ++current_position;
  }
  if (fields.size() >= current_position + 2 && fields[current_position] == "rport") {
    int related_port = 0;
    if (!FromString(fields[++current_position], &related_port) || !IsValidPort(related_port)) {
      Fail(error, "Invalid port");
      return std::nullopt;
    }
    related_address.SetPort(static_cast<uint16_t>(related_port));
    ++current_position;
  }
  // RFC 6544's tcptype for a TCP candidate; a missing one means passive, for backwards compatibility.
  std::string_view tcptype;
  if (fields.size() >= current_position + 2 && fields[current_position] == "tcptype") {
    tcptype = fields[++current_position];
    ++current_position;
    if (tcptype != TCPTYPE_ACTIVE_STR && tcptype != TCPTYPE_PASSIVE_STR && tcptype != TCPTYPE_SIMOPEN_STR) {
      Fail(error, "Invalid TCP candidate type");
      return std::nullopt;
    }
    if (!tcp_protocol) {
      Fail(error, "Invalid non-TCP candidate");
      return std::nullopt;
    }
  } else if (tcp_protocol) {
    tcptype = TCPTYPE_PASSIVE_STR;
  }

  // Extensions. The ufrag and password are non-standard but accepted, for trickling several generations at once.
  std::string_view username;
  std::string_view password;
  uint32_t generation = 0;
  uint16_t network_id = 0;
  uint16_t network_cost = 0;
  bool has_fingerprint = false;
  for (size_t i = current_position; i + 1 < fields.size(); ++i) {
    if (fields[i] == "generation") {
      if (!FromString(fields[++i], &generation)) {
        Fail(error, "Invalid generation");
        return std::nullopt;
      }
    } else if (fields[i] == "ufrag") {
      username = fields[++i];
    } else if (fields[i] == "pwd") {
      password = fields[++i];
    } else if (fields[i] == "network-id") {
      if (!FromString(fields[++i], &network_id)) {
        Fail(error, "Invalid network-id");
        return std::nullopt;
      }
    } else if (fields[i] == "network-cost") {
      if (!FromString(fields[++i], &network_cost)) {
        Fail(error, "Invalid network-cost");
        return std::nullopt;
      }
      network_cost = std::min(network_cost, kNetworkCostMax);
    } else if (fields[i] == "fingerprint") {
      const std::vector<std::string_view> fp_fields = split(fields[++i], ';');
      if (fp_fields.size() != 2) {
        Fail(error, "Invalid fingerprint");
        return std::nullopt;
      }
      if (!IsValidRfc4572Fingerprint(fp_fields[0], fp_fields[1])) {
        Fail(error, "Failed to create fingerprint from the digest.");
        return std::nullopt;
      }
      has_fingerprint = true;
    } else {
      ++i;  // Skip the unknown extension.
    }
  }
  if (*protocol == ProtocolKind::kTls && !has_fingerprint) {
    Fail(error, "Missing fingerprint extension for TLS candidate");
    return std::nullopt;
  }
  // SocketAddress(connection_address, port): a hostname (an mDNS name) is kept, with no IP until it is resolved.
  const SocketAddress address(fields[4], static_cast<uint16_t>(port));
  static constexpr std::string_view kProtoNames[] = {UDP_PROTOCOL_NAME, TCP_PROTOCOL_NAME, SSLTCP_PROTOCOL_NAME,
                                                     TLS_PROTOCOL_NAME};
  Candidate candidate(component_id, kProtoNames[static_cast<int>(*protocol)], address, priority, username, password,
                      candidate_type, generation, foundation, network_id, network_cost);
  candidate.set_related_address(related_address);
  candidate.set_tcptype(tcptype);
  return candidate;
}

}  // namespace sm::p2p
