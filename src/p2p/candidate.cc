// sm/p2p/candidate.h: libwebrtc M157's api/candidate.cc and
// p2p/base/p2p_constants.cc's IsMdnsHostname.
#include "sm/p2p/candidate.h"

#include <array>
#include <cctype>
#include <span>
#include <vector>

#include "sm/p2p/p2p_constants.h"
#include "sm/p2p/stun.h"

namespace sm::p2p {
namespace {

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
                   std::to_string(priority_) + " " + address_.ipaddr().ToString() + " " + PortString(address_) +
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

}  // namespace sm::p2p
