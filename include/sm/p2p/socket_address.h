// IP and socket addresses (issue #996, part of ICE #990, milestone 3).
//
// libwebrtc's rtc_base/ip_address.{h,cc} and rtc_base/socket_address.{h,cc}
// at M157 (compare/webrtc/reference/rtc_base/), the parts the p2p layer uses:
// IPv4 and IPv6 addresses ordered and compared as libwebrtc does, the address
// classes ICE looks at (any, loopback, link-local, private, shared), parsing
// and printing. Hostnames and DNS resolution are left out: the emulated
// network has none, and real sockets (C11, C12) add them with the resolver.
//
// No socket API is used, so it builds on every platform, wasm included.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

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
  // Parses "a.b.c.d:port" and "[v6]:port" (FromString). nullopt otherwise.
  static std::optional<SocketAddress> FromString(std::string_view s);

  const IpAddress& ipaddr() const { return ip_; }
  uint32_t ip() const { return ip_.v4(); }
  IpFamily family() const { return ip_.family(); }
  uint16_t port() const { return port_; }
  void SetIp(const IpAddress& ip) { ip_ = ip; }
  void SetPort(uint16_t port) { port_ = port; }

  // No IP and no port.
  bool IsNil() const { return IpIsUnspec(ip_) && port_ == 0; }
  // A usable IP and a port.
  bool IsComplete() const { return !IpIsAny(ip_) && !IpIsUnspec(ip_) && port_ != 0; }
  bool IsAnyIp() const { return IpIsAny(ip_); }
  bool IsLoopbackIp() const { return IpIsLoopback(ip_); }
  bool IsPrivateIp() const { return IpIsPrivate(ip_); }

  // "a.b.c.d:port" or "[v6]:port".
  std::string ToString() const;
  std::string ToSensitiveString() const;

  bool EqualIps(const SocketAddress& o) const { return ip_ == o.ip_; }
  bool EqualPorts(const SocketAddress& o) const { return port_ == o.port_; }
  friend bool operator==(const SocketAddress& a, const SocketAddress& b) {
    return a.ip_ == b.ip_ && a.port_ == b.port_;
  }
  friend bool operator!=(const SocketAddress& a, const SocketAddress& b) { return !(a == b); }
  // By IP, then port.
  friend bool operator<(const SocketAddress& a, const SocketAddress& b) {
    if (a.ip_ != b.ip_) return a.ip_ < b.ip_;
    return a.port_ < b.port_;
  }

 private:
  IpAddress ip_;
  uint16_t port_ = 0;
};

}  // namespace sm::p2p
