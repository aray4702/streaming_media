// ICE candidates (issue #999, part of ICE #990, milestone 3): libwebrtc
// M157's api/candidate.{h,cc} (compare/webrtc/reference/api/).
//
// A Candidate is one transport address an ICE agent offers: its type (host,
// server-reflexive, peer-reflexive, relay), priority, foundation, related
// address, generation, network id and cost. The priority follows RFC 5245
// 4.1.2.1 with libwebrtc's local preference ((network preference << 8 |
// RFC 3484 address precedence) + relay preference); the foundation is
// libwebrtc's keyed hash: HMAC-SHA256, under a random key that never leaves
// the process, of the type, the base IP, the protocols and the ICE
// tie-breaker, so a hidden base address can't be recovered from it.
//
// A hostname (mDNS) address is kept as libwebrtc keeps it (#1152): the parser
// sets it with SetIP (a name that is not an IP literal leaves the IP
// unspecified), the candidate-attribute writes the name when there is no IP,
// and ToSanitizedCopy hides an address behind its name.
//
// Differences from libwebrtc:
// - No DTLS fingerprint on the candidate and no network slice. The parser
//   (ParseCandidateString, #1062) checks a TLS candidate's fingerprint
//   extension as libwebrtc does but doesn't keep it.
// - The foundation's HMAC-SHA256 and the random bytes for ids and the key
//   come from the STUN crypto hooks (sm/p2p/stun.h, SetStunCrypto). Without
//   them the foundation is a CRC-32 of the same input under a fixed key and
//   ids come from a fixed-seed generator: tests only, as for transaction ids.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "sm/p2p/network.h"
#include "sm/p2p/socket_address.h"

namespace sm::p2p {

enum class IceCandidateType : int { kHost, kSrflx, kPrflx, kRelay };
std::string_view IceCandidateTypeToString(IceCandidateType type);
std::optional<IceCandidateType> StringToIceCandidateType(std::string_view type);

inline constexpr int DISCARD_PORT = 9;
inline constexpr char TCPTYPE_ACTIVE_STR[] = "active";
inline constexpr char TCPTYPE_PASSIVE_STR[] = "passive";
inline constexpr char TCPTYPE_SIMOPEN_STR[] = "so";
inline constexpr uint32_t kMaxTurnServers = 32;

class Candidate {
 public:
  Candidate();
  // api/candidate.cc's ParseCandidate: RFC 5245 15.1's candidate-attribute ("candidate:..."; "a=candidate:..." is
  // accepted too), one line, as SDP and trickled candidates carry it. nullopt, with libwebrtc's reason in `error`,
  // if it is malformed.
  static std::optional<Candidate> ParseCandidateString(std::string_view message, std::string* error = nullptr);
  Candidate(int component, std::string_view protocol, const SocketAddress& address, uint32_t priority,
            std::string_view username, std::string_view password, IceCandidateType type, uint32_t generation,
            std::string_view foundation, uint16_t network_id = 0, uint16_t network_cost = 0);

  const std::string& id() const { return id_; }
  void generate_id();

  int component() const { return component_; }
  void set_component(int component) { component_ = component; }
  const std::string& protocol() const { return protocol_; }
  void set_protocol(std::string_view protocol) { protocol_.assign(protocol); }
  // The protocol between the client and a TURN server (empty otherwise).
  const std::string& relay_protocol() const { return relay_protocol_; }
  void set_relay_protocol(std::string_view protocol) { relay_protocol_.assign(protocol); }
  const SocketAddress& address() const { return address_; }
  void set_address(const SocketAddress& address) { address_ = address; }
  uint32_t priority() const { return priority_; }
  void set_priority(uint32_t priority) { priority_ = priority; }
  const std::string& username() const { return username_; }
  void set_username(std::string_view username) { username_.assign(username); }
  const std::string& password() const { return password_; }
  void set_password(std::string_view password) { password_.assign(password); }

  IceCandidateType type() const { return type_; }
  std::string_view type_name() const { return IceCandidateTypeToString(type_); }
  void set_type(IceCandidateType type) { type_ = type; }
  bool is_local() const { return type_ == IceCandidateType::kHost; }
  bool is_stun() const { return type_ == IceCandidateType::kSrflx; }
  bool is_prflx() const { return type_ == IceCandidateType::kPrflx; }
  bool is_relay() const { return type_ == IceCandidateType::kRelay; }
  // 1 host, 2 reflexive, 3 relay, 0 peer-reflexive (lowest), as libwebrtc.
  int type_preference() const {
    if (is_local()) return 1;
    if (is_stun()) return 2;
    if (is_relay()) return 3;
    return 0;
  }

  const std::string& network_name() const { return network_name_; }
  void set_network_name(std::string_view name) { network_name_.assign(name); }
  AdapterType network_type() const { return network_type_; }
  void set_network_type(AdapterType type) { network_type_ = type; }
  AdapterType underlying_type_for_vpn() const { return underlying_type_for_vpn_; }
  void set_underlying_type_for_vpn(AdapterType type) { underlying_type_for_vpn_ = type; }
  uint32_t generation() const { return generation_; }
  void set_generation(uint32_t generation) { generation_ = generation; }
  uint16_t network_cost() const { return network_cost_; }
  void set_network_cost(uint16_t cost) { network_cost_ = cost > kNetworkCostMax ? kNetworkCostMax : cost; }
  uint16_t network_id() const { return network_id_; }
  void set_network_id(uint16_t id) { network_id_ = id; }
  const std::string& foundation() const { return foundation_; }
  void set_foundation(std::string_view foundation) { foundation_.assign(foundation); }
  const SocketAddress& related_address() const { return related_address_; }
  void set_related_address(const SocketAddress& a) { related_address_ = a; }
  const std::string& tcptype() const { return tcptype_; }
  void set_tcptype(std::string_view tcptype) { tcptype_.assign(tcptype); }
  // The STUN or TURN server URL a reflexive or relay candidate came from.
  const std::string& url() const { return url_; }
  void set_url(std::string_view url) { url_.assign(url); }

  // Same candidate, ignoring the debug fields, the priority and the cost.
  bool IsEquivalent(const Candidate& c) const;
  // Same component, protocol and address.
  bool MatchesForRemoval(const Candidate& c) const;

  std::string ToString() const { return ToStringInternal(false); }
  std::string ToSensitiveString() const { return ToStringInternal(true); }
  // RFC 5245 15.1 candidate-attribute ("candidate:..." without "a="); the address's name when it has no IP.
  std::string ToCandidateAttribute(bool include_ufrag) const;
  // A copy for the application or stats: with `use_hostname_address`, the address is replaced by its name (an IP
  // name or none: "redacted-literal.invalid" / "redacted-ip.invalid") and keeps its port; with
  // `filter_related_address`, the related address becomes the any address of the family; with `filter_ufrag`, no
  // username.
  Candidate ToSanitizedCopy(bool use_hostname_address, bool filter_related_address, bool filter_ufrag) const;

  // RFC 5245 4.1.2.1: (type_preference << 24) | (local_preference << 8) |
  // (256 - component), local_preference = (network_adapter_preference << 8 |
  // address precedence) + relay_preference, plus kMaxTurnServers for a
  // non-relay candidate when `adjust_local_preference`
  // (WebRTC-IncreaseIceCandidatePriorityHostSrflx, off by default).
  uint32_t GetPriority(uint32_t type_preference, int network_adapter_preference, int relay_preference,
                       bool adjust_local_preference) const;

  bool operator==(const Candidate& o) const;
  bool operator!=(const Candidate& o) const { return !(*this == o); }

  // RFC 5245 4.1.1.3 via libwebrtc's keyed hash (see the top of the file).
  void ComputeFoundation(const SocketAddress& base_address, uint64_t tie_breaker);
  // A peer-reflexive candidate's foundation: the CRC-32 of its id.
  void ComputePrflxFoundation();

 private:
  std::string ToStringInternal(bool sensitive) const;

  std::string id_;
  int component_;
  std::string protocol_;
  std::string relay_protocol_;
  SocketAddress address_;
  uint32_t priority_ = 0;
  std::string username_;
  std::string password_;
  IceCandidateType type_ = IceCandidateType::kHost;
  std::string network_name_;
  AdapterType network_type_ = ADAPTER_TYPE_UNKNOWN;
  AdapterType underlying_type_for_vpn_ = ADAPTER_TYPE_UNKNOWN;
  uint32_t generation_ = 0;
  std::string foundation_;
  SocketAddress related_address_;
  std::string tcptype_;
  uint16_t network_id_ = 0;
  uint16_t network_cost_ = 0;
  std::string url_;
};

}  // namespace sm::p2p
