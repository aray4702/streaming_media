// libwebrtc's rtc_base/ip_address.cc and socket_address.cc at M157 (see the
// header).
#include "sm/p2p/socket_address.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace sm::p2p {
namespace {

constexpr std::array<uint8_t, 16> kV4MappedPrefix = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0, 0, 0, 0};
constexpr std::array<uint8_t, 16> kV4CompatibilityPrefix = {};
constexpr std::array<uint8_t, 16> kTeredoPrefix = {0x20, 0x01, 0x00, 0x00};
constexpr std::array<uint8_t, 16> kNat64Prefix = {0x00, 0x64, 0xff, 0x9b};
constexpr std::array<uint8_t, 16> kPrivateNetworkPrefix = {0xfd};

// IPIsHelper: the first `bits` bits of `ip` match `prefix`.
bool Matches(const IpAddress& ip, const std::array<uint8_t, 16>& prefix, int bits) {
  if (ip.family() != IpFamily::kV6) return false;
  const auto& a = ip.v6();
  int i = 0;
  for (; bits >= 8; bits -= 8, ++i) {
    if (a[i] != prefix[i]) return false;
  }
  if (bits == 0) return true;
  const uint8_t mask = static_cast<uint8_t>(0xff << (8 - bits));
  return (a[i] & mask) == (prefix[i] & mask);
}

uint32_t V4At(const std::array<uint8_t, 16>& a, int offset) {
  return (uint32_t{a[offset]} << 24) | (uint32_t{a[offset + 1]} << 16) | (uint32_t{a[offset + 2]} << 8) |
         uint32_t{a[offset + 3]};
}

// IPIsIsatap: an ISATAP interface identifier (RFC 5214), 00-00-5E-FE or 02-00-5E-FE.
bool IsIsatap(const IpAddress& ip) {
  if (ip.family() != IpFamily::kV6) return false;
  const auto& a = ip.v6();
  return (a[8] == 0x00 || a[8] == 0x02) && a[9] == 0x00 && a[10] == 0x5e && a[11] == 0xfe;
}

std::string V4Text(uint32_t v) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%u.%u.%u.%u", (v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
  return buf;
}

std::optional<uint32_t> ParseV4(std::string_view s) {
  uint32_t out = 0;
  int parts = 0;
  size_t i = 0;
  while (parts < 4) {
    if (i >= s.size() || s[i] < '0' || s[i] > '9') return std::nullopt;
    uint32_t v = 0;
    size_t digits = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
      v = v * 10 + static_cast<uint32_t>(s[i] - '0');
      if (++digits > 3 || v > 255) return std::nullopt;
      ++i;
    }
    // inet_pton rejects leading zeros on glibc and accepts them elsewhere;
    // libwebrtc's tests only use canonical text, so accept them.
    out = (out << 8) | v;
    ++parts;
    if (parts < 4) {
      if (i >= s.size() || s[i] != '.') return std::nullopt;
      ++i;
    }
  }
  if (i != s.size()) return std::nullopt;
  return out;
}

std::optional<std::array<uint8_t, 16>> ParseV6(std::string_view s) {
  // Split on "::" at most once.
  std::vector<uint16_t> head, tail;
  bool has_gap = false;
  std::optional<uint32_t> v4_tail;
  auto parse_groups = [&](std::string_view part, std::vector<uint16_t>& out, bool allow_v4) -> bool {
    if (part.empty()) return true;
    size_t i = 0;
    while (true) {
      size_t j = part.find(':', i);
      std::string_view g = part.substr(i, j == std::string_view::npos ? std::string_view::npos : j - i);
      if (j == std::string_view::npos && allow_v4 && g.find('.') != std::string_view::npos) {
        v4_tail = ParseV4(g);
        return v4_tail.has_value();
      }
      if (g.empty() || g.size() > 4) return false;
      uint32_t v = 0;
      for (char c : g) {
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return false;
        v = v * 16 + static_cast<uint32_t>(d);
      }
      out.push_back(static_cast<uint16_t>(v));
      if (j == std::string_view::npos) return true;
      i = j + 1;
    }
  };
  const size_t gap = s.find("::");
  if (gap != std::string_view::npos) {
    has_gap = true;
    if (s.find("::", gap + 1) != std::string_view::npos) return std::nullopt;
    if (!parse_groups(s.substr(0, gap), head, false)) return std::nullopt;
    if (!parse_groups(s.substr(gap + 2), tail, true)) return std::nullopt;
  } else {
    if (!parse_groups(s, head, true)) return std::nullopt;
  }
  const size_t groups = head.size() + tail.size() + (v4_tail ? 2 : 0);
  if (has_gap ? groups > 7 : groups != 8) return std::nullopt;
  std::vector<uint16_t> all = head;
  all.resize(8 - tail.size() - (v4_tail ? 2 : 0), 0);
  all.insert(all.end(), tail.begin(), tail.end());
  if (v4_tail) {
    all.push_back(static_cast<uint16_t>(*v4_tail >> 16));
    all.push_back(static_cast<uint16_t>(*v4_tail & 0xffff));
  }
  std::array<uint8_t, 16> out{};
  for (int k = 0; k < 8; ++k) {
    out[2 * k] = static_cast<uint8_t>(all[k] >> 8);
    out[2 * k + 1] = static_cast<uint8_t>(all[k] & 0xff);
  }
  return out;
}

}  // namespace

std::optional<IpAddress> IpAddress::FromString(std::string_view s) {
  if (auto v4 = ParseV4(s)) return IpAddress(*v4);
  if (auto v6 = ParseV6(s)) return IpAddress::V6(*v6);
  return std::nullopt;
}

std::string IpAddress::ToString() const {
  if (family_ == IpFamily::kV4) return V4Text(v4_);
  if (family_ != IpFamily::kV6) return {};
  if (Matches(*this, kV4MappedPrefix, 96)) return "::ffff:" + V4Text(V4At(v6_, 12));
  uint16_t g[8];
  for (int k = 0; k < 8; ++k) g[k] = static_cast<uint16_t>((v6_[2 * k] << 8) | v6_[2 * k + 1]);
  // RFC 5952 4.2: the longest run of two or more zero groups, the first if tied.
  int best = -1, best_len = 0;
  for (int k = 0; k < 8;) {
    if (g[k] != 0) {
      ++k;
      continue;
    }
    int e = k;
    while (e < 8 && g[e] == 0) ++e;
    if (e - k > best_len && e - k >= 2) {
      best = k;
      best_len = e - k;
    }
    k = e;
  }
  std::string out;
  char buf[8];
  for (int k = 0; k < 8; ++k) {
    if (k == best) {
      out += "::";
      k += best_len - 1;
      continue;
    }
    if (!out.empty() && out.back() != ':') out += ':';
    std::snprintf(buf, sizeof buf, "%x", g[k]);
    out += buf;
  }
  return out;
}

std::string IpAddress::ToSensitiveString() const {
  if (family_ == IpFamily::kV4) {
    std::string s = ToString();
    return s.substr(0, s.rfind('.')) + ".x";
  }
  if (family_ == IpFamily::kV6) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%x:%x:%x:x:x:x:x:x", (v6_[0] << 8) + v6_[1], (v6_[2] << 8) + v6_[3],
                  (v6_[4] << 8) + v6_[5]);
    return buf;
  }
  return {};
}

IpAddress IpAddress::Normalized() const {
  if (family_ == IpFamily::kV6 && Matches(*this, kV4MappedPrefix, 96)) return IpAddress(V4At(v6_, 12));
  return *this;
}

IpAddress IpAddress::NormalizeWithCheckForEmbeddedIPv4Address() const {
  if (family_ != IpFamily::kV6) return *this;
  // IPv4-mapped, IPv4-compatible (::a.b.c.d other than :: and ::1) and NAT64
  // (64:ff9b::/96) carry the IPv4 address in the last 4 bytes. libwebrtc
  // leaves 6to4 alone (crbug.com/497635018).
  const uint32_t tail = V4At(v6_, 12);
  if (Matches(*this, kV4MappedPrefix, 96) || (Matches(*this, kV4CompatibilityPrefix, 96) && tail > 1) ||
      Matches(*this, kNat64Prefix, 96)) {
    return IpAddress(tail);
  }
  if (Matches(*this, kTeredoPrefix, 32)) return IpAddress(~tail);  // Obfuscated client address.
  if (IsIsatap(*this)) return IpAddress(tail);
  return *this;
}

IpAddress IpAddress::AsIPv6Address() const {
  if (family_ != IpFamily::kV4) return *this;
  std::array<uint8_t, 16> a = kV4MappedPrefix;
  a[12] = static_cast<uint8_t>(v4_ >> 24);
  a[13] = static_cast<uint8_t>(v4_ >> 16);
  a[14] = static_cast<uint8_t>(v4_ >> 8);
  a[15] = static_cast<uint8_t>(v4_);
  return IpAddress::V6(a);
}

bool operator==(const IpAddress& a, const IpAddress& b) {
  if (a.family_ != b.family_) return false;
  if (a.family_ == IpFamily::kV4) return a.v4_ == b.v4_;
  if (a.family_ == IpFamily::kV6) return a.v6_ == b.v6_;
  return true;
}

bool operator<(const IpAddress& a, const IpAddress& b) {
  if (a.family_ != b.family_) {
    if (a.family_ == IpFamily::kUnspec) return true;
    if (a.family_ == IpFamily::kV4 && b.family_ == IpFamily::kV6) return true;
    return false;
  }
  if (a.family_ == IpFamily::kV4) return a.v4_ < b.v4_;
  if (a.family_ == IpFamily::kV6) return std::memcmp(a.v6_.data(), b.v6_.data(), 16) < 0;
  return false;
}

bool IpIsAny(const IpAddress& ip) {
  switch (ip.family()) {
    case IpFamily::kV4: return ip.v4() == 0;
    case IpFamily::kV6: return ip.v6() == kV4CompatibilityPrefix || ip.v6() == kV4MappedPrefix;
    case IpFamily::kUnspec: return false;
  }
  return false;
}

bool IpIsLoopback(const IpAddress& ip) {
  const IpAddress n = ip.NormalizeWithCheckForEmbeddedIPv4Address();
  if (n.family() == IpFamily::kV4) return (n.v4() >> 24) == 127;
  if (n.family() == IpFamily::kV6) return n == GetLoopbackIp(IpFamily::kV6);
  return false;
}

bool IpIsLinkLocal(const IpAddress& ip) {
  const IpAddress n = ip.NormalizeWithCheckForEmbeddedIPv4Address();
  if (n.family() == IpFamily::kV4) return (n.v4() >> 16) == ((169u << 8) | 254u);
  if (n.family() == IpFamily::kV6) return n.v6()[0] == 0xfe && (n.v6()[1] & 0xc0) == 0x80;
  return false;
}

bool IpIsPrivateNetwork(const IpAddress& ip) {
  const IpAddress n = ip.NormalizeWithCheckForEmbeddedIPv4Address();
  if (n.family() == IpFamily::kV4) {
    const uint32_t v = n.v4();
    return (v >> 24) == 10 || (v >> 20) == ((172u << 4) | 1u) || (v >> 16) == ((192u << 8) | 168u);
  }
  if (n.family() == IpFamily::kV6) return Matches(n, kPrivateNetworkPrefix, 8);
  return false;
}

bool IpIsSharedNetwork(const IpAddress& ip) {
  const IpAddress n = ip.NormalizeWithCheckForEmbeddedIPv4Address();
  return n.family() == IpFamily::kV4 && (n.v4() >> 22) == ((100u << 2) | 1u);
}

bool IpIsPrivate(const IpAddress& ip) {
  return IpIsLinkLocal(ip) || IpIsLoopback(ip) || IpIsPrivateNetwork(ip) || IpIsSharedNetwork(ip);
}

bool IpIsUnspec(const IpAddress& ip) { return ip.family() == IpFamily::kUnspec; }

IpAddress GetAnyIp(IpFamily family) {
  if (family == IpFamily::kV4) return IpAddress(0u);
  if (family == IpFamily::kV6) return IpAddress::V6({});
  return {};
}

IpAddress GetLoopbackIp(IpFamily family) {
  if (family == IpFamily::kV4) return IpAddress::V4(127, 0, 0, 1);
  if (family == IpFamily::kV6) {
    std::array<uint8_t, 16> a{};
    a[15] = 1;
    return IpAddress::V6(a);
  }
  return {};
}

std::optional<SocketAddress> SocketAddress::FromString(std::string_view s) {
  std::string_view host, port;
  if (!s.empty() && s.front() == '[') {
    const size_t close = s.find(']');
    if (close == std::string_view::npos || close + 1 >= s.size() || s[close + 1] != ':') return std::nullopt;
    host = s.substr(1, close - 1);
    port = s.substr(close + 2);
  } else {
    const size_t colon = s.rfind(':');
    if (colon == std::string_view::npos) return std::nullopt;
    host = s.substr(0, colon);
    port = s.substr(colon + 1);
  }
  auto ip = IpAddress::FromString(host);
  if (!ip || port.empty() || port.size() > 5) return std::nullopt;
  uint32_t p = 0;
  for (char c : port) {
    if (c < '0' || c > '9') return std::nullopt;
    p = p * 10 + static_cast<uint32_t>(c - '0');
  }
  if (p > 0xffff) return std::nullopt;
  return SocketAddress(*ip, static_cast<uint16_t>(p));
}

std::string SocketAddress::ToString() const {
  const std::string host = ip_.family() == IpFamily::kV6 ? "[" + ip_.ToString() + "]" : ip_.ToString();
  return host + ":" + std::to_string(port_);
}

std::string SocketAddress::ToSensitiveString() const {
  const std::string host =
      ip_.family() == IpFamily::kV6 ? "[" + ip_.ToSensitiveString() + "]" : ip_.ToSensitiveString();
  return host + ":" + std::to_string(port_);
}

}  // namespace sm::p2p
