// IP and socket addresses (issue #996, part of ICE #990, milestone 3).
//
// libwebrtc's rtc_base/ip_address.{h,cc} and rtc_base/socket_address.{h,cc}
// at M157 (compare/webrtc/reference/rtc_base/), the parts the p2p layer uses:
// IPv4 and IPv6 addresses ordered and compared as libwebrtc does, the address
// classes ICE looks at (any, loopback, link-local, private, shared), parsing
// and printing. A hostname is kept as libwebrtc keeps it (SetIP with a name,
// SetResolvedIP, IsUnresolvedIP, and the name in comparisons of addresses
// without an IP), for mDNS candidates (#1152); the library resolves nothing
// itself: the ICE agent asks an AsyncDnsResolverFactoryInterface the host
// provides (sm/p2p/async_dns_resolver.h).
//
// No socket API is used, so it builds on every platform, wasm included.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace sm::p2p {

// AF_UNSPEC / AF_INET / AF_INET6 without the socket headers.
enum class IpFamily : uint8_t { kUnspec, kV4, kV6 };

class IpAddress {
 public:
  IpAddress() = default;  // AF_UNSPEC, as IPAddress().
  // IPv4 in host byte order, as IPAddress(uint32_t).
  explicit IpAddress(uint32_t v4_host_order) : family_(IpFamily::kV4), v4_(v4_host_order) {}
  static IpAddress V4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return IpAddress((uint32_t{a} << 24) | (uint32_t{b} << 16) | (uint32_t{c} << 8) | uint32_t{d});
  }
  static IpAddress V6(const std::array<uint8_t, 16>& bytes) {
    IpAddress ip;
    ip.family_ = IpFamily::kV6;
    ip.v6_ = bytes;
    return ip;
  }
  // IPFromString: dotted IPv4 or RFC 4291 IPv6 text (with "::" and an
  // embedded IPv4 tail). nullopt when it is neither.
  static std::optional<IpAddress> FromString(std::string_view s);

  IpFamily family() const { return family_; }
  // v4AddressAsHostOrderInteger: 0 for anything that isn't IPv4.
  uint32_t v4() const { return family_ == IpFamily::kV4 ? v4_ : 0; }
  // ipv6_address(): zero for anything that isn't IPv6.
  const std::array<uint8_t, 16>& v6() const { return v6_; }
  // Size of the address in bytes: 4, 16, or 0 when unspecified.
  size_t Size() const { return family_ == IpFamily::kV4 ? 4 : family_ == IpFamily::kV6 ? 16 : 0; }
  // overhead(): the IP header's size, 20 bytes for IPv4 and 40 for IPv6.
  int overhead() const { return family_ == IpFamily::kV4 ? 20 : family_ == IpFamily::kV6 ? 40 : 0; }

  // inet_ntop's text: dotted IPv4; IPv6 with the longest run of two or more
  // zero groups as "::" (RFC 5952), and IPv4-mapped as ::ffff:a.b.c.d. Empty
  // for AF_UNSPEC.
  std::string ToString() const;
  // The last IPv4 byte or the last five IPv6 groups hidden, as libwebrtc logs.
  std::string ToSensitiveString() const;

  // Normalized(): an IPv4-mapped IPv6 address becomes the IPv4 address.
  IpAddress Normalized() const;
  // NormalizeWithCheckForEmbeddedIPv4Address(): also IPv4-compatible
  // (::a.b.c.d) and 6to4 (2002:a.b.c.d::/48) addresses.
  IpAddress NormalizeWithCheckForEmbeddedIPv4Address() const;
  // AsIPv6Address(): an IPv4 address as IPv4-mapped IPv6.
  IpAddress AsIPv6Address() const;

  friend bool operator==(const IpAddress& a, const IpAddress& b);
  friend bool operator!=(const IpAddress& a, const IpAddress& b) { return !(a == b); }
  // IPv4 sorts before IPv6; then by value (IPAddress::operator<).
  friend bool operator<(const IpAddress& a, const IpAddress& b);

 private:
  IpFamily family_ = IpFamily::kUnspec;
  uint32_t v4_ = 0;
  std::array<uint8_t, 16> v6_{};
};

// ip_address.h's free functions.
bool IpIsAny(const IpAddress& ip);           // 0.0.0.0, ::, or ::ffff:0.0.0.0.
bool IpIsLoopback(const IpAddress& ip);      // 127.0.0.0/8, ::1.
bool IpIsLinkLocal(const IpAddress& ip);     // 169.254.0.0/16, fe80::/10.
bool IpIsPrivateNetwork(const IpAddress& ip);  // 10/8, 172.16/12, 192.168/16, fd00::/8.
bool IpIsSharedNetwork(const IpAddress& ip);   // 100.64.0.0/10 (carrier-grade NAT).
// Link-local, loopback, private or shared.
bool IpIsPrivate(const IpAddress& ip);
bool IpIsUnspec(const IpAddress& ip);
IpAddress GetAnyIp(IpFamily family);
IpAddress GetLoopbackIp(IpFamily family);

class SocketAddress {
 public:
  SocketAddress() = default;  // Nil.
  SocketAddress(const IpAddress& ip, uint16_t port) : ip_(ip), port_(port) {}
  SocketAddress(uint32_t v4_host_order, uint16_t port) : ip_(v4_host_order), port_(port) {}
  // SocketAddress(hostname, port): SetIP(hostname), so an IP literal gives that IP and any other name an unresolved
  // address (no IP, the name kept).
  SocketAddress(std::string_view hostname, uint16_t port) : port_(port) { SetIP(hostname); }
  // Parses "a.b.c.d:port" and "[v6]:port" (FromString). nullopt otherwise.
  static std::optional<SocketAddress> FromString(std::string_view s);

  const IpAddress& ipaddr() const { return ip_; }
  uint32_t ip() const { return ip_.v4(); }
  IpFamily family() const { return ip_.family(); }
  uint16_t port() const { return port_; }
  void SetIp(const IpAddress& ip) { ip_ = ip; }
  // SetIP(hostname): an IP literal sets the IP (and is kept as the name); any other name leaves the IP unspecified.
  void SetIP(std::string_view hostname);
  // SetResolvedIP: the IP a name resolved to; the name is kept.
  void SetResolvedIP(const IpAddress& ip) { ip_ = ip; }
  void SetPort(uint16_t port) { port_ = port; }
  // The name the address was given by, if any (SocketAddress::hostname): an mDNS candidate's name (#1152), and
  // TLS's SNI and the name the server's certificate must match (#1035).
  const std::string& hostname() const { return hostname_; }
  // Sets the name and keeps the IP (the library's own: a TLS server's name beside its resolved IP).
  void SetHostname(std::string hostname) {
    hostname_ = std::move(hostname);
    literal_ = IpAddress::FromString(hostname_).has_value();
  }
  // A name that is not an IP literal and has no IP yet (an mDNS candidate before it is resolved).
  bool IsUnresolvedIP() const { return IpIsUnspec(ip_) && !literal_ && !hostname_.empty(); }

  // No name, no IP and no port.
  bool IsNil() const { return hostname_.empty() && IpIsUnspec(ip_) && port_ == 0; }
  // A usable IP and a port.
  bool IsComplete() const { return !IpIsAny(ip_) && !IpIsUnspec(ip_) && port_ != 0; }
  bool IsAnyIp() const { return IpIsAny(ip_); }
  bool IsLoopbackIp() const { return IpIsLoopback(ip_); }
  bool IsPrivateIp() const { return IpIsPrivate(ip_); }

  // The name if the address has one that is not an IP literal, else "a.b.c.d" or "[v6]" (HostAsURIString).
  std::string HostAsURIString() const;
  std::string HostAsSensitiveURIString() const;
  // HostAsURIString() + ":port".
  std::string ToString() const;
  std::string ToSensitiveString() const;

  // The IPs are equal, and so are the names when there is no usable IP (any or unspecified), as libwebrtc's EqualIPs.
  bool EqualIps(const SocketAddress& o) const {
    return ip_ == o.ip_ && ((!IpIsAny(ip_) && !IpIsUnspec(ip_)) || hostname_ == o.hostname_);
  }
  bool EqualPorts(const SocketAddress& o) const { return port_ == o.port_; }
  friend bool operator==(const SocketAddress& a, const SocketAddress& b) { return a.EqualIps(b) && a.EqualPorts(b); }
  friend bool operator!=(const SocketAddress& a, const SocketAddress& b) { return !(a == b); }
  // By IP, then by name when neither has a usable IP, then by port.
  friend bool operator<(const SocketAddress& a, const SocketAddress& b) {
    if (a.ip_ != b.ip_) return a.ip_ < b.ip_;
    if ((IpIsAny(a.ip_) || IpIsUnspec(a.ip_)) && a.hostname_ != b.hostname_) return a.hostname_ < b.hostname_;
    return a.port_ < b.port_;
  }

 private:
  IpAddress ip_;
  uint16_t port_ = 0;
  std::string hostname_;
  bool literal_ = false;  // The name is an IP literal.
};

}  // namespace sm::p2p
