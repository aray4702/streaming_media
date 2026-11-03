// `transport ice` (issue #1087): a test bench call set up through the full connectivity stack, between two
// C6 part 3 endpoints (sm/pc/peer_endpoint.h, #1064).
//
// An IceLink is one connection: two PeerEndpoints, side 0 and side 1, in the endpoint's external-media mode, so
// the call's own rtp::Sessions (the ones a `transport direct` call uses) send and receive the media unchanged and
// the endpoints carry it. They exchange offer and answer as SDP text and trickle candidates as a=candidate lines
// over an in-process signaling channel, run ICE on an emulated network (sim::ByteNetwork, #985) with optional NATs
// per side, a STUN server and a TURN server, finish DTLS and send every RTP and RTCP packet as SRTP over the one
// BUNDLE transport.
//
// Where the scenario's links sit: every datagram that reaches a side's host (from the other side, or from the
// STUN or TURN server) crosses the scenario's link toward that side last, as on an access link. The emulated
// network's own links take no time; the call's forward and reverse links (or a mesh's uplink and downlink) carry
// the datagrams, with the same impairments, queue and traces as a direct call's packets. So a relayed call's
// packets cross the scenario's link once, as a direct call's do.
//
// The call starts after setup: Connect() runs the signaling, ICE and DTLS over a private copy of the scenario's
// links at their first settings, and the media phase starts at call time 0 with both sides connected. The
// setup.* metrics are measured from the offer.
//
// A call without TLS in the build (wasm) can't run `transport ice`; Available() says so and the scenario parser
// refuses it.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/sim/network_link.h"

namespace sm::testbench {

struct IceOptions {
  // Per side: -1 no NAT (a public address), else a sim::NatType (0 open cone .. 3 symmetric).
  int nat[2] = {-1, -1};
  // 0: no TURN server; 1 UDP, 2 TCP, 3 TLS: both sides relay through it (iceTransportPolicy relay).
  int turn = 0;
  bool stun = true;
  // Two interfaces per side (for a network change, FailInterface); one otherwise.
  bool two_interfaces = false;
  bool audio = true, video = true;
  // The SSRCs each side's session sends on (announced in its SDP).
  struct Ssrcs {
    uint32_t audio = 0, video = 0;
    std::optional<uint32_t> rtx;
  } ssrcs[2];
  uint64_t seed = 1;
};

struct IceSetupStats {
  bool connected = false;
  double ice_connected_ms = -1;   // From the offer to both sides' ICE writable.
  double dtls_connected_ms = -1;  // ... to both sides' DTLS connected and SRTP keyed.
  // Side 0's selected pair at the end: local and remote candidate types (host, srflx, prflx, relay) and the
  // TURN protocol of a relayed local candidate ("udp", "tcp", "tls"; empty when not relayed).
  std::string local_type, remote_type, relay_protocol;
  std::string error;
};

class IceLink {
 public:
  // A datagram on the scenario's link toward a side; opaque to the call.
  struct Datagram;
  using DatagramPtr = std::shared_ptr<Datagram>;
  // Puts a datagram on the link toward `to_side`, now.
  using Emit = std::function<void(int to_side, DatagramPtr d, DataSize wire)>;
  // An RTP or RTCP packet that reached `side`, after SRTP.
  using OnMedia = std::function<void(int side, std::vector<uint8_t> packet, bool rtcp, Timestamp at)>;

  static bool Available();

  explicit IceLink(const IceOptions& options);
  ~IceLink();
  IceLink(const IceLink&) = delete;
  IceLink& operator=(const IceLink&) = delete;

  // Signaling, ICE and DTLS over private links with `toward[i]`, the scenario's link toward side i at call start;
  // gives up after `limit`. False on failure (stats().error says why).
  bool Connect(const sim::LinkConfig toward[2], TimeDelta limit = TimeDelta::Seconds(30));
  const IceSetupStats& stats() const { return stats_; }
  // From the offer to the start of the media phase (call time 0).
  TimeDelta setup_duration() const;

  // The media phase, in call time.
  void SetEmit(Emit emit);
  void SetOnMedia(OnMedia on_media);
  // Sends one of `side`'s session packets; its datagrams go out through Emit at once. False if the endpoint could
  // not send it.
  bool Send(int side, std::span<const uint8_t> packet, bool rtcp, Timestamp now);
  // The endpoints' ICE and DTLS timers and the servers, due by `now`; their datagrams go out through Emit.
  void Process(Timestamp now);
  // A datagram arriving over the link toward `to_side`; the media in it goes to OnMedia.
  void Arrive(int to_side, const DatagramPtr& d, Timestamp at);
  // Whether a datagram carries the call's RTP or RTCP (false for STUN, DTLS and TURN control traffic).
  static bool IsMedia(const Datagram& d);
  // A network change: `side`'s first interface stops passing traffic (a firewall rule on its address).
  void FailInterface(int side, Timestamp now);
  // Side 0's selected pair now (updates stats()).
  void UpdateSelectedPair();
  bool connected() const;

  // Bytes both sides' hosts offered to the network (each datagram with its UDP/IP or TCP/IP headers), and the
  // bytes of the session packets they were given, with UDP/IP headers: the direct call's wire bytes.
  int64_t wire_bytes() const;
  int64_t media_bytes() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  IceSetupStats stats_;
};

}  // namespace sm::testbench
