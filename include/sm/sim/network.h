// Emulated IPv4 network with addresses, routing and NATs (issue #985, the
// groundwork for ICE, #944 C3).
//
// `NetworkLink` (network_link.h) carries datagrams between two fixed ends and
// knows nothing about addresses. ICE needs them: candidates are addresses,
// and whether two candidates can reach each other depends on the NATs in
// between. `Network` adds that layer on top of `NetworkLink`, which it uses
// unchanged:
//
// - **Hosts** have an IPv4 address in a **realm**. Realm 0 is the public
//   internet; every NAT creates a private realm behind it. Each host has an
//   access link in each direction (`host -> realm` and `realm -> host`), each
//   a `NetworkLink` with today's impairments (capacity, queue, loss, delay,
//   jitter, duplication, outages), configurable at any time.
// - **Datagrams** carry a source and a destination `SocketAddress`. A NAT
//   rewrites them as they cross it.
// - **NATs** sit between their private realm and their parent realm (the
//   internet, or another NAT's private realm, so NATs nest), with one
//   external IP in the parent realm. Routing and translation take no time;
//   the delay is in the hosts' access links.
//
// The NAT is libwebrtc's test NAT (compare/webrtc/reference/p2p/test/,
// nat_types.{h,cc} and nat_server.{h,cc}), ported rule for rule:
//
// - The four types and their rules are `NATType` / `NAT::Create`
//   (nat_types.cc): open cone (no filtering), address-restricted (filters
//   by IP), port-restricted (filters by IP and port) and symmetric (filters
//   by IP and port, and maps per destination).
// - **Mapping** follows `RouteCmp` (nat_server.cc): a binding is keyed by the
//   internal source address, plus the destination for a symmetric NAT. A new
//   binding takes the NAT's external IP and a fresh port (`Translate`, which
//   binds a new external socket to `external_ip:0`).
// - **Filtering** follows `TransEntry`'s allowlist with `AddrCmp`: each
//   outbound datagram adds its destination to the binding's allowlist
//   (`OnInternalUDPPacket`), and an inbound datagram passes only if its
//   source matches an allowlisted address on the fields the type filters
//   (IP when `FiltersIP`, port when `FiltersPort`; `OnExternalUDPPacket`).
//   An inbound datagram to an external port with no binding is dropped.
//
// Two behaviours the issue asks for go beyond libwebrtc's test NAT, which
// keeps bindings for its lifetime and never sees its own external address
// as a destination. Both follow RFC 4787 and default to what libwebrtc's
// test NAT does:
//
// - **Binding timeout** (`NatConfig::binding_timeout`, default infinite, as
//   libwebrtc's NATServer, which never removes a TransEntry). A binding with
//   no outbound traffic for the timeout expires (RFC 4787 REQ-5 asks real
//   NATs for at least 2 min, 5 min recommended), and inbound traffic does not
//   refresh it unless `inbound_refreshes` (REQ-6: outbound refresh is a
//   must, inbound is optional). A later datagram gets a new binding on a new
//   port.
// - **Hairpinning** (`NatConfig::hairpinning`, default on, RFC 4787 REQ-9):
//   a datagram from the private realm to one of the NAT's own external
//   addresses is translated outbound (its source becomes the sender's mapped
//   address) and then inbound, filters included, as if it had left and come
//   back. In libwebrtc's virtual network the external socket's SendTo to the
//   NAT's own external IP lands on the matching external socket the same
//   way. With hairpinning off, the NAT drops it.
//
// Like every module, it takes `now`, reads no clock and starts no thread.
// It is a test bench building block: `testbench/call.cc` does not use it, so
// every scenario is unchanged.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sm/core/units.h"
#include "sm/sim/network_link.h"

namespace sm::sim {

// An IPv4 address and port, compared and ordered field by field.
struct SocketAddress {
  uint32_t ip = 0;
  uint16_t port = 0;

  static constexpr uint32_t Ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return (uint32_t{a} << 24) | (uint32_t{b} << 16) | (uint32_t{c} << 8) | uint32_t{d};
  }
  friend bool operator==(const SocketAddress& a, const SocketAddress& b) = default;
  friend bool operator<(const SocketAddress& a, const SocketAddress& b) {
    return a.ip != b.ip ? a.ip < b.ip : a.port < b.port;
  }
  std::string ToString() const {
    std::string s;
    for (int shift = 24; shift >= 0; shift -= 8) {
      s += std::to_string((ip >> shift) & 0xff);
      s += shift ? "." : ":";
    }
    return s + std::to_string(port);
  }
};

// libwebrtc's NATType (p2p/test/nat_types.h).
enum class NatType : uint8_t { kOpenCone, kAddrRestricted, kPortRestricted, kSymmetric };

// libwebrtc's NAT rules (nat_types.cc: OpenConeNAT, AddressRestrictedNAT,
// PortRestrictedNAT, SymmetricNAT).
struct NatRules {
  bool symmetric;     // NAT::IsSymmetric: the binding also depends on the destination.
  bool filters_ip;    // NAT::FiltersIP.
  bool filters_port;  // NAT::FiltersPort.
  static constexpr NatRules Of(NatType t) {
    switch (t) {
      case NatType::kOpenCone: return {false, false, false};
      case NatType::kAddrRestricted: return {false, true, false};
      case NatType::kPortRestricted: return {false, true, true};
      case NatType::kSymmetric: return {true, true, true};
    }
    return {true, true, true};
  }
};

struct NatConfig {
  NatType type = NatType::kPortRestricted;
  uint32_t external_ip = 0;  // In the parent realm.
  // First external port handed out; each new binding takes the next one.
  // 49152 is the start of the IANA dynamic port range (RFC 6335).
  uint16_t first_port = 49152;
  // Idle time after which a binding expires; infinite (never) as in
  // libwebrtc's NATServer.
  TimeDelta binding_timeout = TimeDelta::PlusInfinity();
  bool inbound_refreshes = false;  // Inbound traffic also keeps a binding alive.
  bool hairpinning = true;
};

// A datagram on the emulated network.
template <class T>
struct Datagram {
  SocketAddress src;
  SocketAddress dst;
  T payload;
};

// Counters of what the network did with the datagrams offered to it.
struct NetworkStats {
  uint64_t sent = 0;            // Offered by hosts.
  uint64_t delivered = 0;       // Handed to a destination host's downlink.
  uint64_t unroutable = 0;      // No host or NAT has the destination address.
  uint64_t nat_filtered = 0;    // Dropped by a NAT's inbound filter.
  uint64_t nat_no_binding = 0;  // Inbound to an external port with no binding.
  uint64_t hairpinned = 0;      // Turned around by a NAT with hairpinning on.
  uint64_t hairpin_dropped = 0; // Dropped by a NAT with hairpinning off.
  uint64_t bindings_created = 0;
  uint64_t bindings_expired = 0;
};

template <class T>
class Network {
 public:
  // What an access link carries: the datagram with its wire size and kind,
  // so the second link charges the same size as the first.
  struct Packet {
    Datagram<T> d;
    DataSize size;
    LinkKind kind;
  };
  using Link = NetworkLink<Packet>;
  using HostId = int;
  using NatId = int;
  static constexpr int kPublicRealm = 0;

  explicit Network(uint64_t seed = 1) : seed_(seed) {}

  // Adds a NAT whose external side is in `parent_realm`; returns its id. Its
  // private realm is `NatRealm(id)`.
  NatId AddNat(const NatConfig& cfg, int parent_realm = kPublicRealm) {
    Nat n;
    n.cfg = cfg;
    n.rules = NatRules::Of(cfg.type);
    n.parent_realm = parent_realm;
    n.next_port = cfg.first_port;
    nats_.push_back(std::move(n));
    return static_cast<NatId>(nats_.size()) - 1;
  }
  int NatRealm(NatId nat) const { return nat + 1; }

  // Adds a host with address `ip` in `realm`, with the given access links.
  HostId AddHost(uint32_t ip, int realm = kPublicRealm, const LinkConfig& up = {}, const LinkConfig& down = {}) {
    const uint64_t n = hosts_.size();
    hosts_.push_back(Host{ip, realm, Link(up, seed_ * 1000003 + 2 * n + 1), Link(down, seed_ * 1000003 + 2 * n + 2)});
    return static_cast<HostId>(hosts_.size()) - 1;
  }

  Link& uplink(HostId h) { return hosts_[h].up; }
  Link& downlink(HostId h) { return hosts_[h].down; }
  uint32_t host_ip(HostId h) const { return hosts_[h].ip; }
  const NetworkStats& stats() const { return stats_; }
  size_t BindingCount(NatId nat) const { return nats_[nat].bindings.size(); }

  // Sends a datagram from `host` (source IP is the host's; the caller picks
  // the port) to `dst`.
  void Send(Timestamp now, HostId host, uint16_t src_port, SocketAddress dst, T payload, DataSize size,
            LinkKind kind = LinkKind::kOther) {
    Process(now);
    ++stats_.sent;
    Host& h = hosts_[host];
    h.up.Send(now, Packet{Datagram<T>{{h.ip, src_port}, dst, std::move(payload)}, size, kind}, size, kind);
  }

  // Routes every datagram that has left a host's uplink by `now`, in time
  // order, onto its destination's downlink.
  void Process(Timestamp now) {
    for (;;) {
      HostId first = -1;
      Timestamp t = Timestamp::PlusInfinity();
      for (size_t i = 0; i < hosts_.size(); ++i) {
        const Timestamp a = hosts_[i].up.NextArrival();
        if (a < t) {
          t = a;
          first = static_cast<HostId>(i);
        }
      }
      if (first < 0 || t > now) return;
      std::vector<Packet> ready = hosts_[first].up.Receive(t);
      for (Packet& p : ready) Route(t, hosts_[first].realm, std::move(p));
    }
  }

  // Datagrams that have reached `host` by `now`, in arrival order.
  std::vector<Datagram<T>> Receive(Timestamp now, HostId host) {
    Process(now);
    std::vector<Datagram<T>> out;
    for (Packet& p : hosts_[host].down.Receive(now)) out.push_back(std::move(p.d));
    return out;
  }

  // The earliest time anything moves: a datagram leaving an uplink or
  // reaching a host.
  Timestamp NextEvent() const {
    Timestamp t = Timestamp::PlusInfinity();
    for (const Host& h : hosts_) t = std::min({t, h.up.NextArrival(), h.down.NextArrival()});
    return t;
  }

  // The external address a NAT has bound for `internal` (to `dst`, for a
  // symmetric NAT), if any.
  std::optional<SocketAddress> MappedAddress(NatId nat, SocketAddress internal, SocketAddress dst = {}) const {
    const Nat& n = nats_[nat];
    auto it = n.bindings.find(RouteKey(n, internal, dst));
    if (it == n.bindings.end()) return std::nullopt;
    return SocketAddress{n.cfg.external_ip, it->second.external_port};
  }

 private:
  struct Host {
    uint32_t ip;
    int realm;
    Link up;
    Link down;
  };
  struct Binding {
    uint16_t external_port;
    SocketAddress internal;
    std::vector<SocketAddress> allowlist;
    Timestamp last_active;
  };
  struct Nat {
    NatConfig cfg;
    NatRules rules;
    int parent_realm;
    uint32_t next_port;
    // RouteCmp: (internal source, destination or {} unless symmetric).
    std::map<std::pair<SocketAddress, SocketAddress>, Binding> bindings;
    std::map<uint16_t, std::pair<SocketAddress, SocketAddress>> by_port;  // ext_map_.
  };

  static std::pair<SocketAddress, SocketAddress> RouteKey(const Nat& n, SocketAddress src, SocketAddress dst) {
    return {src, n.rules.symmetric ? dst : SocketAddress{}};
  }

  // AddrCmp: equal on the fields the NAT filters on.
  static bool Allowed(const Nat& n, const Binding& b, SocketAddress from) {
    for (const SocketAddress& a : b.allowlist) {
      if ((!n.rules.filters_ip || a.ip == from.ip) && (!n.rules.filters_port || a.port == from.port)) return true;
    }
    return false;
  }

  void Expire(Nat& n, Timestamp now) {
    if (!n.cfg.binding_timeout.IsFinite()) return;
    for (auto it = n.bindings.begin(); it != n.bindings.end();) {
      if (now - it->second.last_active >= n.cfg.binding_timeout) {
        n.by_port.erase(it->second.external_port);
        it = n.bindings.erase(it);
        ++stats_.bindings_expired;
      } else {
        ++it;
      }
    }
  }

  // OnInternalUDPPacket: finds or makes the binding, allowlists the
  // destination and returns the translated source; nullopt when the NAT
  // has run out of ports.
  std::optional<SocketAddress> Outbound(Nat& n, Timestamp now, SocketAddress src, SocketAddress dst) {
    Expire(n, now);
    const auto key = RouteKey(n, src, dst);
    auto it = n.bindings.find(key);
    if (it == n.bindings.end()) {
      if (n.next_port > 0xffff) return std::nullopt;
      const uint16_t port = static_cast<uint16_t>(n.next_port++);
      it = n.bindings.emplace(key, Binding{port, src, {}, now}).first;
      n.by_port[port] = key;
      ++stats_.bindings_created;
    }
    Binding& b = it->second;
    if (std::find(b.allowlist.begin(), b.allowlist.end(), dst) == b.allowlist.end()) b.allowlist.push_back(dst);
    b.last_active = now;
    return SocketAddress{n.cfg.external_ip, b.external_port};
  }

  // OnExternalUDPPacket: the internal address to forward to, or nullopt when
  // the datagram is dropped.
  std::optional<SocketAddress> Inbound(Nat& n, Timestamp now, SocketAddress src, SocketAddress dst) {
    Expire(n, now);
    auto p = n.by_port.find(dst.port);
    if (p == n.by_port.end()) {
      ++stats_.nat_no_binding;
      return std::nullopt;
    }
    Binding& b = n.bindings.at(p->second);
    if (!Allowed(n, b, src)) {
      ++stats_.nat_filtered;
      return std::nullopt;
    }
    if (n.cfg.inbound_refreshes) b.last_active = now;
    return b.internal;
  }

  // Delivers `d` in `realm`: to a host with its address, or into a NAT whose
  // external IP it is, or out through the NAT that owns `realm`.
  // Once a datagram has entered a NAT from outside it only goes further in,
  // so it crosses each NAT at most twice (out, then in or hairpinned) and
  // the walk ends.
  void Route(Timestamp now, int realm, Packet p) {
    Datagram<T>& d = p.d;
    bool descending = false;
    for (;;) {
      for (size_t i = 0; i < hosts_.size(); ++i) {
        if (hosts_[i].realm == realm && hosts_[i].ip == d.dst.ip) {
          ++stats_.delivered;
          const DataSize size = p.size;
          const LinkKind kind = p.kind;
          hosts_[i].down.Send(now, std::move(p), size, kind);
          return;
        }
      }
      // Into a NAT from its parent realm (or hairpinned from its own realm).
      bool moved = false;
      for (size_t i = 0; i < nats_.size(); ++i) {
        Nat& n = nats_[i];
        if (n.parent_realm != realm || n.cfg.external_ip != d.dst.ip) continue;
        std::optional<SocketAddress> to = Inbound(n, now, d.src, d.dst);
        if (!to) return;
        d.dst = *to;
        realm = NatRealm(static_cast<NatId>(i));
        moved = true;
        descending = true;
        break;
      }
      if (moved) continue;
      if (descending || realm == kPublicRealm) break;
      // Out through the NAT that owns this realm.
      Nat& n = nats_[static_cast<size_t>(realm - 1)];
      std::optional<SocketAddress> mapped = Outbound(n, now, d.src, d.dst);
      if (!mapped) break;
      d.src = *mapped;
      if (d.dst.ip == n.cfg.external_ip) {
        if (!n.cfg.hairpinning) {
          ++stats_.hairpin_dropped;
          return;
        }
        ++stats_.hairpinned;
        std::optional<SocketAddress> to = Inbound(n, now, d.src, d.dst);
        if (!to) return;
        d.dst = *to;
        descending = true;
        continue;  // Back in the same private realm.
      }
      realm = n.parent_realm;
    }
    ++stats_.unroutable;
  }

  uint64_t seed_;
  std::vector<Host> hosts_;
  std::vector<Nat> nats_;
  NetworkStats stats_;
};

}  // namespace sm::sim
