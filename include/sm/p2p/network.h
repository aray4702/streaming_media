// The network interface an ICE port gathers on (issue #999, part of ICE #990,
// milestone 3): the parts of libwebrtc M157's rtc_base/network.{h,cc} and
// rtc_base/network_constants.h (compare/webrtc/reference/rtc_base/) that a
// Port reads: the adapter type and its network cost, the preference the
// network manager gives it, its id, its addresses and the best one to bind.
//
// Enumerating the host's interfaces (NetworkManager) is the host stack's job
// (C11, C12) and comes with BasicPortAllocator (#1001); a test or the emulated
// network builds a Network directly. Field trials are at libwebrtc's defaults
// (all off): WebRTC-UseDifferentiatedCellularCosts, WebRTC-AddNetworkCostToVpn
// and WebRTC-UnifiedCommunications (network slices) are not ported.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sm/p2p/socket_address.h"

namespace sm::p2p {

// rtc_base/network_constants.h.
inline constexpr uint16_t kNetworkCostMax = 999;
inline constexpr uint16_t kNetworkCostCellular2G = 980;
inline constexpr uint16_t kNetworkCostCellular3G = 910;
inline constexpr uint16_t kNetworkCostCellular = 900;
inline constexpr uint16_t kNetworkCostCellular4G = 500;
inline constexpr uint16_t kNetworkCostCellular5G = 250;
inline constexpr uint16_t kNetworkCostUnknown = 50;
inline constexpr uint16_t kNetworkCostLow = 10;
inline constexpr uint16_t kNetworkCostMin = 0;
inline constexpr uint16_t kNetworkCostVpn = 1;
// Networks at or above this cost are "high cost" (UDPPort's keepalive lifetime).
inline constexpr uint16_t kNetworkCostHigh = kNetworkCostCellular;
// rtc_base/network.cc: the first network the manager sorts gets 127.
inline constexpr int kHighestNetworkPreference = 127;

enum AdapterType {
  ADAPTER_TYPE_UNKNOWN = 0,
  ADAPTER_TYPE_ETHERNET = 1 << 0,
  ADAPTER_TYPE_WIFI = 1 << 1,
  ADAPTER_TYPE_CELLULAR = 1 << 2,  // Cellular of unknown type.
  ADAPTER_TYPE_VPN = 1 << 3,
  ADAPTER_TYPE_LOOPBACK = 1 << 4,
  ADAPTER_TYPE_ANY = 1 << 5,  // Only the any address; uses every interface.
  ADAPTER_TYPE_CELLULAR_2G = 1 << 6,
  ADAPTER_TYPE_CELLULAR_3G = 1 << 7,
  ADAPTER_TYPE_CELLULAR_4G = 1 << 8,
  ADAPTER_TYPE_CELLULAR_5G = 1 << 9
};

const char* AdapterTypeToString(AdapterType type);

// rtc_base/network.cc ComputeNetworkCostByType, with the field trials off.
uint16_t ComputeNetworkCostByType(AdapterType type, bool is_vpn);

// rtc_base/ip_address.cc IPAddressPrecedence (RFC 3484-bis): the "address
// preference" byte of a candidate's local preference.
int IpAddressPrecedence(const IpAddress& ip);

class Network {
 public:
  Network(std::string name, IpAddress prefix, int prefix_length, AdapterType type = ADAPTER_TYPE_UNKNOWN)
      : name_(std::move(name)), prefix_(prefix), prefix_length_(prefix_length), type_(type) {}

  const std::string& name() const { return name_; }
  const IpAddress& prefix() const { return prefix_; }
  int prefix_length() const { return prefix_length_; }
  IpFamily family() const { return prefix_.family(); }

  void AddIp(const IpAddress& ip) { ips_.push_back(ip); }
  void SetIps(std::vector<IpAddress> ips) { ips_ = std::move(ips); }
  const std::vector<IpAddress>& GetIps() const { return ips_; }
  // Network::GetBestIP: an IPv4 network's first address. For IPv6, the
  // last global address, else link-local, else ULA. (The deprecated and
  // temporary IPv6 flags of InterfaceAddress are not modelled.)
  IpAddress GetBestIP() const;

  AdapterType type() const { return type_; }
  void set_type(AdapterType type) { type_ = type; }
  AdapterType underlying_type_for_vpn() const { return underlying_type_for_vpn_; }
  void set_underlying_type_for_vpn(AdapterType type) { underlying_type_for_vpn_ = type; }
  bool IsVpn() const { return type_ == ADAPTER_TYPE_VPN; }
  // Network::GetCost: a VPN costs as its underlying type.
  uint16_t GetCost() const {
    return ComputeNetworkCostByType(IsVpn() ? underlying_type_for_vpn_ : type_, IsVpn());
  }

  // Set by the network manager: 127 for the first network down to 0.
  int preference() const { return preference_; }
  void set_preference(int preference) { preference_ = preference; }
  // Unique among the networks of one manager; signalled as network-id.
  uint16_t id() const { return id_; }
  void set_id(uint16_t id) { id_ = id; }

  // DefaultLocalAddressProvider: the address a port bound to the any address
  // reports when `emit_local_for_anyaddress` is set. nullopt when there is no
  // provider (the default); a provider that knows no address returns an
  // unspecified IpAddress.
  const std::optional<IpAddress>& default_local_address() const { return default_local_address_; }
  void set_default_local_address(std::optional<IpAddress> ip) { default_local_address_ = ip; }

  // "Net[name:prefix/len:type]", as Network::ToString.
  std::string ToString() const;

 private:
  std::string name_;
  IpAddress prefix_;
  int prefix_length_ = 0;
  AdapterType type_ = ADAPTER_TYPE_UNKNOWN;
  AdapterType underlying_type_for_vpn_ = ADAPTER_TYPE_UNKNOWN;
  std::vector<IpAddress> ips_;
  int preference_ = 0;
  uint16_t id_ = 0;
  std::optional<IpAddress> default_local_address_;
};

}  // namespace sm::p2p
