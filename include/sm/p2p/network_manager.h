// The list of networks a port allocator gathers on (issue #1001, part of ICE
// #990, milestone 3): libwebrtc M157's NetworkManager and NetworkManagerBase
// from rtc_base/network.{h,cc} (compare/webrtc/reference/rtc_base/).
//
// NetworkManager is the interface BasicPortAllocator reads: the current
// networks, the "any address" networks (used when adapter enumeration is
// disabled or yields nothing), the enumeration permission, the default local
// addresses, and a networks-changed signal. NetworkManagerBase keeps the list
// as libwebrtc does: MergeNetworkList consolidates networks by key
// (name%prefix/len), keeps the Network objects of networks it already knew
// (so their ids and the ports bound on them stay valid), marks the ones that
// went away inactive, sorts by adapter type, then RFC 3484 precedence of the
// best IP, then key, and gives the networks preferences 127, 126, ... in
// that order. Ids start at 1.
//
// An OS network monitor (C11, C12) implements NetworkManager over the host's
// interfaces later. StaticNetworkManager is the emulated network's: the
// caller adds and removes interfaces (an address, a name, an adapter type),
// as libwebrtc's test FakeNetworkManager does, and the networks change when
// it does.
//
// Differences from libwebrtc: no mDNS responder (GetMdnsResponder is always
// null, so the allocator never obfuscates host candidates), no VPN list or
// MAC-based VPN detection, no network preference or slice, and the network
// thread is the caller's: the networks-changed signal runs inside the call
// that changed them (StartUpdating, MergeNetworkList).
// StaticNetworkManager follows rtc_base/fake_network.h's FakeNetworkManager
// (prefix lengths /24 and /64, "test<N>" names, the first-update rule).
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "sm/p2p/network.h"
#include "sm/p2p/socket_address.h"

namespace sm::p2p {

// rtc_base/network.h: by default, loopback networks are ignored.
inline constexpr int kDefaultNetworkIgnoreMask = ADAPTER_TYPE_LOOPBACK;

class NetworkManager {
 public:
  enum EnumerationPermission {
    ENUMERATION_ALLOWED,  // Adapter enumeration is allowed (0 networks means none).
    ENUMERATION_BLOCKED,  // Disabled: GetAnyAddressNetworks() should be used.
  };

  virtual ~NetworkManager() = default;

  // Starts and stops watching the networks. The networks-changed signal
  // follows the first StartUpdating once the list is known.
  virtual void StartUpdating() = 0;
  virtual void StopUpdating() = 0;
  // The current networks, sorted and with preferences set. Valid until the
  // next change.
  virtual std::vector<const Network*> GetNetworks() const = 0;
  virtual EnumerationPermission enumeration_permission() const { return ENUMERATION_ALLOWED; }
  // An IPv4 and an IPv6 network bound to the any address, type
  // ADAPTER_TYPE_ANY, whose default local address is this manager's.
  virtual std::vector<const Network*> GetAnyAddressNetworks() = 0;
  // The address the OS would route public traffic from. False if unknown.
  virtual bool GetDefaultLocalAddress(IpFamily family, IpAddress* ipaddr) const = 0;

  void SubscribeNetworksChanged(const void* tag, std::function<void()> cb) {
    networks_changed_.emplace_back(tag, std::move(cb));
  }
  void UnsubscribeNetworksChanged(const void* tag) {
    std::erase_if(networks_changed_, [tag](const auto& e) { return e.first == tag; });
  }
  void NotifyNetworksChanged() {
    auto cbs = networks_changed_;  // A receiver may unsubscribe while it runs.
    for (auto& [tag, cb] : cbs) cb();
  }

 private:
  std::vector<std::pair<const void*, std::function<void()>>> networks_changed_;
};

class NetworkManagerBase : public NetworkManager {
 public:
  std::vector<const Network*> GetNetworks() const override;
  std::vector<const Network*> GetAnyAddressNetworks() override;
  EnumerationPermission enumeration_permission() const override { return enumeration_permission_; }
  bool GetDefaultLocalAddress(IpFamily family, IpAddress* ipaddr) const override;

  void set_enumeration_permission(EnumerationPermission state) { enumeration_permission_ = state; }
  void set_default_local_addresses(const IpAddress& ipv4, const IpAddress& ipv6);

 protected:
  // Updates the list from `list`, reusing the Network objects of networks
  // already known by key. `changed` says whether the list changed.
  void MergeNetworkList(std::vector<std::unique_ptr<Network>> list, bool* changed);
  Network* GetNetworkFromAddress(const IpAddress& ip) const;

 private:
  EnumerationPermission enumeration_permission_ = ENUMERATION_ALLOWED;
  std::vector<Network*> networks_;
  std::map<std::string, std::unique_ptr<Network>> networks_map_;
  std::unique_ptr<Network> ipv4_any_address_network_;
  std::unique_ptr<Network> ipv6_any_address_network_;
  IpAddress default_local_ipv4_address_;
  IpAddress default_local_ipv6_address_;
  // 16 bits, as signalled; a wrap to 0 is harmless (ids are only compared).
  uint16_t next_available_network_id_ = 1;
};

// The emulated network's NetworkManager: interfaces the caller adds and
// removes.
class StaticNetworkManager final : public NetworkManagerBase {
 public:
  // An interface with one address, named "test<N>" if no name is given. A
  // second address of the same name and prefix joins that network.
  void AddInterface(const IpAddress& ip) { AddInterface(ip, "test" + std::to_string(next_index_++)); }
  void AddInterface(const IpAddress& ip, std::string name, AdapterType type = ADAPTER_TYPE_UNKNOWN,
                    AdapterType underlying_vpn_type = ADAPTER_TYPE_UNKNOWN);
  // Removes the first interface with this address.
  void RemoveInterface(const IpAddress& ip);
  void ChangeInterfaceType(const IpAddress& ip, AdapterType type);

  void StartUpdating() override;
  void StopUpdating() override;
  int start_count() const { return start_count_; }

 private:
  struct Interface {
    IpAddress ip;
    std::string name;
    AdapterType type;
    AdapterType underlying_vpn_type;
  };
  void DoUpdateNetworks();

  std::vector<Interface> ifaces_;
  int next_index_ = 0;
  int start_count_ = 0;
  bool sent_first_update_ = false;
};

// rtc_base/ip_address.cc TruncateIP: the first `length` bits of `ip`.
IpAddress TruncateIp(const IpAddress& ip, int length);

}  // namespace sm::p2p
