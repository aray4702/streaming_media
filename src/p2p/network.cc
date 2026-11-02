// sm/p2p/network.h: libwebrtc M157's rtc_base/network.cc (GetCost,
// ComputeNetworkCostByType, GetBestIP, ToString) and rtc_base/ip_address.cc
// (IPAddressPrecedence).
#include "sm/p2p/network.h"

#include <array>

namespace sm::p2p {
namespace {

bool Prefix(const IpAddress& ip, std::initializer_list<uint8_t> bytes) {
  if (ip.family() != IpFamily::kV6) return false;
  size_t i = 0;
  for (uint8_t b : bytes) {
    if (ip.v6()[i++] != b) return false;
  }
  return true;
}

bool IsUla(const IpAddress& ip) { return ip.family() == IpFamily::kV6 && (ip.v6()[0] & 0xFE) == 0xFC; }
bool IsSiteLocal(const IpAddress& ip) {
  return ip.family() == IpFamily::kV6 && ip.v6()[0] == 0xFE && (ip.v6()[1] & 0xC0) == 0xC0;
}
bool IsV4Mapped(const IpAddress& ip) { return Prefix(ip, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff}); }
bool IsV4Compatibility(const IpAddress& ip) {
  if (!Prefix(ip, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0})) return false;
  const auto& a = ip.v6();
  const uint32_t tail = (uint32_t{a[12]} << 24) | (uint32_t{a[13]} << 16) | (uint32_t{a[14]} << 8) | a[15];
  return tail > 1;  // Not :: or ::1.
}

}  // namespace

const char* AdapterTypeToString(AdapterType type) {
  switch (type) {
    case ADAPTER_TYPE_ANY: return "Wildcard";
    case ADAPTER_TYPE_UNKNOWN: return "Unknown";
    case ADAPTER_TYPE_ETHERNET: return "Ethernet";
    case ADAPTER_TYPE_WIFI: return "Wifi";
    case ADAPTER_TYPE_CELLULAR: return "Cellular";
    case ADAPTER_TYPE_CELLULAR_2G: return "Cellular2G";
    case ADAPTER_TYPE_CELLULAR_3G: return "Cellular3G";
    case ADAPTER_TYPE_CELLULAR_4G: return "Cellular4G";
    case ADAPTER_TYPE_CELLULAR_5G: return "Cellular5G";
    case ADAPTER_TYPE_VPN: return "VPN";
    case ADAPTER_TYPE_LOOPBACK: return "Loopback";
  }
  return "Unknown";
}

uint16_t ComputeNetworkCostByType(AdapterType type, bool /*is_vpn*/) {
  // WebRTC-AddNetworkCostToVpn and WebRTC-UseDifferentiatedCellularCosts are
  // off by default, so a VPN adds nothing and every cellular type costs
  // kNetworkCostCellular.
  switch (type) {
    case ADAPTER_TYPE_ETHERNET:
    case ADAPTER_TYPE_LOOPBACK:
      return kNetworkCostMin;
    case ADAPTER_TYPE_WIFI:
      return kNetworkCostLow;
    case ADAPTER_TYPE_CELLULAR:
    case ADAPTER_TYPE_CELLULAR_2G:
    case ADAPTER_TYPE_CELLULAR_3G:
    case ADAPTER_TYPE_CELLULAR_4G:
    case ADAPTER_TYPE_CELLULAR_5G:
      return kNetworkCostCellular;
    case ADAPTER_TYPE_ANY:
      return kNetworkCostMax;
    case ADAPTER_TYPE_VPN:  // Costed by the underlying type; libwebrtc DCHECKs.
    default:
      return kNetworkCostUnknown;
  }
}

int IpAddressPrecedence(const IpAddress& ip) {
  if (ip.family() == IpFamily::kV4) return 30;
  if (ip.family() == IpFamily::kV6) {
    if (IpIsLoopback(ip)) return 60;
    if (IsUla(ip)) return 50;
    if (IsV4Mapped(ip)) return 30;
    if (Prefix(ip, {0x20, 0x02})) return 20;              // 6to4.
    if (Prefix(ip, {0x20, 0x01, 0x00, 0x00})) return 10;  // Teredo.
    if (IsV4Compatibility(ip) || IsSiteLocal(ip) || Prefix(ip, {0x3f, 0xfe})) return 1;  // ... 6bone.
    return 40;
  }
  return 0;
}

IpAddress Network::GetBestIP() const {
  if (ips_.empty()) return IpAddress();
  if (prefix_.family() == IpFamily::kV4) return ips_.front();
  IpAddress selected, link_local, ula;
  for (const IpAddress& ip : ips_) {
    if (IpIsLinkLocal(ip)) {
      link_local = ip;
      continue;
    }
    if (IsUla(ip)) {
      ula = ip;
      continue;
    }
    selected = ip;
  }
  if (IpIsUnspec(selected)) {
    if (!IpIsUnspec(link_local)) {
      selected = link_local;
    } else if (!IpIsUnspec(ula)) {
      selected = ula;
    }
  }
  return selected;
}

std::string Network::ToString() const {
  std::string s = "Net[" + name_.substr(0, name_.find(' ')) + ":" + prefix_.ToSensitiveString() + "/" +
                  std::to_string(prefix_length_) + ":" + AdapterTypeToString(type_);
  if (IsVpn()) s += std::string("/") + AdapterTypeToString(underlying_type_for_vpn_);
  return s + ":id=" + std::to_string(id_) + "]";
}

std::string Network::key() const { return name_ + "%" + prefix_.ToString() + "/" + std::to_string(prefix_length_); }

}  // namespace sm::p2p
