// NetworkManagerBase and StaticNetworkManager (issue #1001): libwebrtc M157's
// rtc_base/network.cc (MergeNetworkList, SortNetworks, CompareNetworks,
// GetAnyAddressNetworks, GetDefaultLocalAddress) and ip_address.cc
// (TruncateIP).
#include "sm/p2p/network_manager.h"

#include <algorithm>

namespace sm::p2p {
namespace {

// rtc_base/network.cc SortNetworks: by type, then RFC 3484 precedence of the
// best IP (higher first), then key.
bool SortNetworks(const Network* a, const Network* b) {
  if (a->type() != b->type()) return a->type() < b->type();
  const IpAddress ip_a = a->GetBestIP();
  const IpAddress ip_b = b->GetBestIP();
  if (IpAddressPrecedence(ip_a) != IpAddressPrecedence(ip_b)) {
    return IpAddressPrecedence(ip_a) > IpAddressPrecedence(ip_b);
  }
  return a->key() < b->key();
}

// webrtc_network_internal::CompareNetworks.
bool CompareNetworks(const std::unique_ptr<Network>& a, const std::unique_ptr<Network>& b) {
  if (a->prefix_length() != b->prefix_length()) return a->prefix_length() < b->prefix_length();
  if (a->name() != b->name()) return a->name() < b->name();
  return a->prefix() < b->prefix();
}

// ShouldAdapterChangeTriggerNetworkChange: not from one cellular type to another.
bool ShouldAdapterChangeTriggerNetworkChange(AdapterType old_type, AdapterType new_type) {
  return !(Network::IsCellular(old_type) && Network::IsCellular(new_type));
}

// Network::SetIPs: true if the addresses differ (or `changed` already was).
bool SetIps(Network* network, const std::vector<IpAddress>& ips, bool changed) {
  const std::vector<IpAddress>& old = network->GetIps();
  changed = changed || ips.size() != old.size();
  if (!changed) {
    for (const IpAddress& ip : ips) {
      if (std::find(old.begin(), old.end(), ip) == old.end()) {
        changed = true;
        break;
      }
    }
  }
  network->SetIps(ips);
  return changed;
}

}  // namespace

IpAddress TruncateIp(const IpAddress& ip, int length) {
  if (length < 0) return IpAddress();
  if (ip.family() == IpFamily::kV4) {
    if (length > 31) return ip;
    if (length == 0) return IpAddress(uint32_t{0});
    const uint32_t mask = 0xFFFFFFFFu << (32 - length);
    return IpAddress(ip.v4() & mask);
  }
  if (ip.family() == IpFamily::kV6) {
    if (length > 127) return ip;
    std::array<uint8_t, 16> bytes = ip.v6();
    for (size_t i = 0; i < bytes.size(); ++i) {
      const int keep = std::clamp(length - static_cast<int>(i) * 8, 0, 8);
      bytes[i] = static_cast<uint8_t>(bytes[i] & static_cast<uint8_t>(0xFF00u >> keep));
    }
    return IpAddress::V6(bytes);
  }
  return IpAddress();
}

std::vector<const Network*> NetworkManagerBase::GetNetworks() const {
  return std::vector<const Network*>(networks_.begin(), networks_.end());
}

std::vector<const Network*> NetworkManagerBase::GetAnyAddressNetworks() {
  // The any-address networks' default local address comes from this manager
  // (libwebrtc's DefaultLocalAddressProvider), read when they are handed out.
  if (!ipv4_any_address_network_) {
    const IpAddress any = GetAnyIp(IpFamily::kV4);
    ipv4_any_address_network_ = std::make_unique<Network>("any", any, 0, ADAPTER_TYPE_ANY);
    ipv4_any_address_network_->AddIp(any);
  }
  if (!ipv6_any_address_network_) {
    const IpAddress any = GetAnyIp(IpFamily::kV6);
    ipv6_any_address_network_ = std::make_unique<Network>("any", any, 0, ADAPTER_TYPE_ANY);
    ipv6_any_address_network_->AddIp(any);
  }
  for (Network* n : {ipv4_any_address_network_.get(), ipv6_any_address_network_.get()}) {
    IpAddress ip;
    // A provider that knows no address reports an unspecified one.
    n->set_default_local_address(GetDefaultLocalAddress(n->family(), &ip) ? ip : IpAddress());
  }
  return {ipv4_any_address_network_.get(), ipv6_any_address_network_.get()};
}

void NetworkManagerBase::MergeNetworkList(std::vector<std::unique_ptr<Network>> new_networks, bool* changed) {
  *changed = false;
  struct AddressList {
    std::unique_ptr<Network> net;
    std::vector<IpAddress> ips;
  };
  std::map<std::string, AddressList> consolidated;
  std::sort(new_networks.begin(), new_networks.end(), CompareNetworks);
  for (auto& network : new_networks) {
    const std::string key = network->key();
    const std::vector<IpAddress> addresses = network->GetIps();
    auto it = consolidated.find(key);
    if (it == consolidated.end()) it = consolidated.emplace(key, AddressList{std::move(network), {}}).first;
    for (const IpAddress& a : addresses) it->second.ips.push_back(a);
  }

  std::vector<Network*> merged;
  for (auto& [key, list] : consolidated) {
    std::unique_ptr<Network> net = std::move(list.net);
    auto existing = networks_map_.find(key);
    if (existing == networks_map_.end()) {
      net->set_id(next_available_network_id_++);
      net->SetIps(list.ips);
      merged.push_back(net.get());
      networks_map_[key] = std::move(net);
      *changed = true;
    } else {
      Network* existing_net = existing->second.get();
      *changed = SetIps(existing_net, list.ips, *changed);
      merged.push_back(existing_net);
      if (net->type() != ADAPTER_TYPE_UNKNOWN && net->type() != existing_net->type()) {
        if (ShouldAdapterChangeTriggerNetworkChange(existing_net->type(), net->type())) *changed = true;
        existing_net->set_type(net->type());
      }
      existing_net->set_underlying_type_for_vpn(net->underlying_type_for_vpn());
      if (!existing_net->active()) *changed = true;
    }
  }
  if (merged.size() != networks_.size()) *changed = true;

  if (*changed) {
    networks_ = merged;
    for (const auto& [key, network] : networks_map_) {
      network->set_active(std::find(networks_.begin(), networks_.end(), network.get()) != networks_.end());
    }
    std::sort(networks_.begin(), networks_.end(), SortNetworks);
    int pref = kHighestNetworkPreference;
    for (Network* network : networks_) {
      network->set_preference(pref);
      if (pref > 0) {
        --pref;
      } else {
        break;  // "Too many network interfaces to handle!"
      }
    }
  }
}

void NetworkManagerBase::set_default_local_addresses(const IpAddress& ipv4, const IpAddress& ipv6) {
  if (ipv4.family() == IpFamily::kV4) default_local_ipv4_address_ = ipv4;
  if (ipv6.family() == IpFamily::kV6) default_local_ipv6_address_ = ipv6;
}

bool NetworkManagerBase::GetDefaultLocalAddress(IpFamily family, IpAddress* ipaddr) const {
  if (family == IpFamily::kV4 && !IpIsUnspec(default_local_ipv4_address_)) {
    *ipaddr = default_local_ipv4_address_;
    return true;
  }
  if (family == IpFamily::kV6 && !IpIsUnspec(default_local_ipv6_address_)) {
    // The default IPv6 network's best IP, if it differs (WebRTC bug 5376).
    const Network* ipv6_network = GetNetworkFromAddress(default_local_ipv6_address_);
    *ipaddr = ipv6_network ? ipv6_network->GetBestIP() : default_local_ipv6_address_;
    return true;
  }
  return false;
}

Network* NetworkManagerBase::GetNetworkFromAddress(const IpAddress& ip) const {
  for (Network* network : networks_) {
    const auto& ips = network->GetIps();
    if (std::find(ips.begin(), ips.end(), ip) != ips.end()) return network;
  }
  return nullptr;
}

void StaticNetworkManager::AddInterface(const IpAddress& ip, std::string name, AdapterType type,
                                        AdapterType underlying_vpn_type) {
  ifaces_.push_back(Interface{ip, std::move(name), type, underlying_vpn_type});
  DoUpdateNetworks();
}

void StaticNetworkManager::RemoveInterface(const IpAddress& ip) {
  std::erase_if(ifaces_, [&](const Interface& i) { return i.ip == ip; });
  DoUpdateNetworks();
}

void StaticNetworkManager::ChangeInterfaceType(const IpAddress& ip, AdapterType type) {
  for (Interface& i : ifaces_) {
    if (i.ip == ip) i.type = type;
  }
  DoUpdateNetworks();
}

void StaticNetworkManager::StartUpdating() {
  ++start_count_;
  if (start_count_ == 1) {
    sent_first_update_ = false;
    DoUpdateNetworks();
  } else if (sent_first_update_) {
    NotifyNetworksChanged();
  }
}

void StaticNetworkManager::StopUpdating() {
  if (start_count_ > 0) --start_count_;
}

void StaticNetworkManager::DoUpdateNetworks() {
  if (start_count_ == 0) return;
  std::vector<std::unique_ptr<Network>> networks;
  for (const Interface& i : ifaces_) {
    const int prefix_length = i.ip.family() == IpFamily::kV4 ? 24 : 64;
    auto net = std::make_unique<Network>(i.name, TruncateIp(i.ip, prefix_length), prefix_length, i.type);
    if (i.type == ADAPTER_TYPE_VPN) net->set_underlying_type_for_vpn(i.underlying_vpn_type);
    net->AddIp(i.ip);
    networks.push_back(std::move(net));
  }
  bool changed = false;
  MergeNetworkList(std::move(networks), &changed);
  if (changed || !sent_first_update_) {
    NotifyNetworksChanged();
    sent_first_update_ = true;
  }
}

}  // namespace sm::p2p
