// A simulated SFU for the test bench's group calls (`call sfu <N>`, C21, #1180), modelled on LiveKit, the SFU the
// apps use (C19, #1160). Test bench code, not library code: the library serves the endpoints (program.md 0.1).
//
// Every participant publishes on its own connection (the publish leg) and subscribes on another (the subscribe leg),
// as LiveKit's clients have a publisher and a subscriber PeerConnection. The SFU:
//   - terminates RTCP on the publish leg: an ingress rtp::Session per publisher receives the audio and every simulcast
//     layer and sends the publisher its receiver reports, transport-cc feedback (for the publisher's GoogCC), NACKs
//     and keyframe requests; RTX from the publisher is unwrapped and forwarded as the media packet it repairs;
//   - forwards, per subscriber, one downtrack per remote track: audio as received, video as the selected layer only,
//     on one SSRC per track (the track's first-layer SSRC) with sequence numbers and RTP timestamps rewritten so a
//     layer switch is seamless (LiveKit's forwarder and RTPMunger; the timestamps are aligned through the layers'
//     sender reports). A switch waits for a keyframe of the target layer, which the SFU asks the publisher for (PLI
//     through the ingress session);
//   - terminates RTCP on the subscribe leg: its own sender reports per downtrack (NTP now, RTP extrapolated from the
//     publisher's reports), DLRR answers to the subscriber's RRTR, NACKs answered from the ingress packet cache with
//     RTX, PLI and FIR passed on to the publisher as a keyframe request for the layer forwarded, and the subscriber's
//     transport-cc feedback and report blocks into a per-subscriber GoogCC (cc::GccController) with padding probes
//     (cc::BitrateProber) - LiveKit's send-side bandwidth estimation;
//   - splits each subscriber's estimate among its video tracks every 100 ms (SfuAllocate, LiveKit's StreamAllocator:
//     the lowest layer of each track first, then upgrades in turn, video paused when even the lowest doesn't fit;
//     audio is always forwarded);
//   - with dynacast (LiveKit's dynacast, the clients' default in our apps), asks each publisher to pause the layers no
//     subscriber wants (simulcast_pause, #1173) and to resume them when one does.
// Times are call times; the sessions run on the library's clock, call time + `clock0`.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sm/cc/bitrate_prober.h"
#include "sm/cc/send_side_bwe.h"
#include "sm/core/rtp_packet.h"
#include "sm/core/units.h"
#include "sm/rtp/session.h"

namespace sm::testbench {

// --- Layer selection ------------------------------------------------------------------------------------------------

// One of a subscriber's video tracks, as the allocator sees it.
struct SfuAllocTrack {
  // Each layer's rate as the SFU receives it, lowest first; zero: the layer is not available (paused or not received).
  std::vector<DataRate> layer_rates;
  int max_layer = 2;  // The highest layer the subscriber wants (its tile size).
  int current = -1;   // The layer forwarded now; -1: none (paused or not started).
};

// LiveKit's StreamAllocator (allocateAllTracks) in short: the lowest available layer of every track, in order, while
// it fits; then each track in turn moves up one layer while the step fits; a track that gets no layer is paused.
// Moving above a track's current layer costs `upgrade_margin` times the layer's rate, so a measured rate that wobbles
// doesn't switch a track up and down. Returns each track's layer, -1 for paused.
std::vector<int> SfuAllocate(DataRate available, const std::vector<SfuAllocTrack>& tracks, double upgrade_margin = 1.1);

// --- One video downtrack -------------------------------------------------------------------------------------------

struct SfuForwarderConfig {
  uint32_t out_ssrc = 0;      // The downtrack's SSRC.
  uint32_t out_rtx_ssrc = 0;  // Its RTX SSRC (0: no RTX).
  uint8_t rtx_payload_type = 97;
  int clock_rate = 90000;
};

// Forwards one publisher's simulcast video to one subscriber (LiveKit's Forwarder and RTPMunger).
class SfuVideoForwarder {
 public:
  // The packet the ingress kept for layer `layer` and sequence number `seq`, or null.
  using CacheLookup = std::function<const RtpPacket*(int layer, uint16_t seq)>;

  explicit SfuVideoForwarder(SfuForwarderConfig cfg) : cfg_(cfg) {}

  // The allocator's choice (-1: pause). Pausing stops forwarding at once; another layer is switched to at its next
  // keyframe, and until then the current layer keeps going.
  void SetTarget(int layer);
  int target() const { return target_; }
  int current() const { return current_; }
  // Whether the forwarder waits for a keyframe of the target layer.
  bool WantsKeyframe() const { return target_ >= 0 && target_ != current_; }

  // A media packet of `layer` that reached the SFU (RTX unwrapped). Returns what goes to the subscriber: the same
  // packet on the downtrack's SSRC with its sequence number and timestamp rewritten, or nothing (another layer, before
  // the switch's keyframe, paused, or already forwarded).
  std::optional<RtpPacket> Forward(const RtpPacket& in, int layer, Timestamp now);

  // A sender report of `layer`: its NTP time (call time) and RTP timestamp, for aligning the layers' timestamps.
  void OnLayerSenderReport(int layer, Timestamp ntp_time, uint32_t rtp_timestamp);
  // The downtrack's RTP timestamp at `ntp_time`, from the current layer's last sender report; nothing before one.
  std::optional<uint32_t> TimestampAt(Timestamp ntp_time) const;

  // RFC 4588 resends of the downtrack's sequence numbers that the cache still has, on the RTX SSRC, while
  // `*budget_bytes` lasts (each resend takes its size off). A packet resent less than `min_interval` ago (the RTT) is
  // not resent again, as libwebrtc's RTPSender::ReSendPacket holds back a repeated request, and a packet is resent at
  // most kMaxResends times, as LiveKit's NACK responder.
  static constexpr int kMaxResends = 3;
  std::vector<RtpPacket> Resend(const std::vector<uint16_t>& seqs, const CacheLookup& cache, Timestamp now,
                                TimeDelta min_interval, int64_t* budget_bytes);
  // The layer a downtrack sequence number was forwarded from (-1: unknown).
  int LayerOf(uint16_t out_seq) const;
  // A padding-only packet on the RTX SSRC, for a bandwidth probe.
  RtpPacket MakePadding();
  bool started() const { return started_ && cfg_.out_rtx_ssrc != 0; }

  int switches() const { return switches_; }       // Layer changes once forwarding (not the first start).
  int resumes() const { return resumes_; }         // Starts and restarts after a pause.
  uint64_t packets() const { return packets_; }    // Media packets forwarded.
  uint64_t octets() const { return octets_; }      // Their payload bytes.
  uint64_t resent() const { return resent_; }

 private:
  // A run of the downtrack forwarded from one layer: out = in - seq_offset, out_ts = in_ts + ts_offset.
  struct Segment {
    uint16_t out_start = 0;
    int layer = 0;
    uint16_t seq_offset = 0;
    uint32_t ts_offset = 0;
    uint16_t in_start = 0;
  };
  bool SwitchTo(int layer, const RtpPacket& in, Timestamp now);
  const Segment* SegmentFor(uint16_t out_seq) const;

  SfuForwarderConfig cfg_;
  int target_ = -1, current_ = -1;
  std::deque<Segment> segments_;  // Newest last; the old ones serve NACKs.
  bool started_ = false;
  uint16_t last_out_seq_ = 0;      // Highest forwarded (wrap-aware).
  uint32_t last_out_ts_ = 0;
  Timestamp last_out_time_ = Timestamp::MinusInfinity();
  std::set<uint16_t> recent_out_;  // Downtrack sequence numbers forwarded lately (duplicates are dropped).
  struct Resent {
    Timestamp at;
    int count = 0;
  };
  std::map<uint16_t, Resent> resent_at_;  // Downtrack sequence number -> its last resend and how many.
  std::map<int, std::pair<Timestamp, uint32_t>> layer_sr_;  // Layer -> (NTP time, RTP timestamp) of its last SR.
  uint16_t rtx_seq_ = 0;
  int switches_ = 0, resumes_ = 0;
  uint64_t packets_ = 0, octets_ = 0, resent_ = 0;
};

// --- The SFU --------------------------------------------------------------------------------------------------------

struct SfuConfig {
  TimeDelta clock0 = TimeDelta::Zero();  // The library's clock at call time 0.
  // Template of an ingress session (extensions, RTCP); its local SSRC is set per publisher.
  rtp::SessionConfig ingress;
  uint8_t transport_cc_id = 1;  // The transport-wide sequence number's extension id, on every leg.
  uint8_t rtx_payload_type = 97;
  cc::GccConfig egress_gcc;     // Each subscriber's bandwidth estimate.
  bool dynacast = true;
  TimeDelta dynacast_delay = TimeDelta::Millis(50);  // Signaling time for a pause or resume to reach the publisher.
  TimeDelta allocation_interval = TimeDelta::Millis(100);
  TimeDelta keyframe_request_interval = TimeDelta::Millis(300);
  double upgrade_margin = 1.1;
};

struct SfuPublisher {
  std::optional<rtp::AudioStreamConfig> audio;
  std::vector<rtp::VideoStreamConfig> layers;  // Lowest first; each with its SSRC and RTX SSRC.
};

enum class SfuPacketKind : uint8_t { kAudio, kVideo, kRtx, kPadding, kRtcp };

struct SfuOutgoing {
  int to = 0;                 // The participant.
  bool subscribe_leg = true;  // False: RTCP from the ingress session to the publisher.
  SfuPacketKind kind = SfuPacketKind::kRtcp;
  std::vector<uint8_t> bytes;
};

// A dynacast request to a publisher: pause or resume one layer, due at `at`.
struct SfuLayerActivity {
  int publisher = 0;
  int layer = 0;
  bool active = true;
  Timestamp at = Timestamp::Millis(0);
};

class Sfu {
 public:
  explicit Sfu(SfuConfig cfg);
  ~Sfu();

  // A participant joins (publishing `pub`, wanting at most `max_layer` of every other's video) or leaves.
  void Join(int id, const SfuPublisher& pub, int max_layer, Timestamp now);
  void Leave(int id, Timestamp now);
  bool present(int id) const { return members_.count(id) != 0; }

  // A datagram from a participant: from its publisher (media and RTCP) or from its subscriber (RTCP).
  void OnPublishPacket(int from, std::span<const uint8_t> bytes, Timestamp at);
  void OnSubscribePacket(int from, std::span<const uint8_t> bytes, Timestamp at);
  // Timers: the ingress sessions, each subscriber's estimate, probes and allocation.
  void Process(Timestamp now);
  std::vector<SfuOutgoing> TakeOutgoing();
  std::vector<SfuLayerActivity> TakeLayerActivity();

  // Measurement taps (the test bench's meters, not the SFU's logic).
  // A video packet forwarded from `pub` to `sub`: the layer, its timestamp at the publisher and on the downtrack.
  std::function<void(int pub, int sub, int layer, uint32_t in_ts, uint32_t out_ts)> on_forward_video;

  // The downtrack of `pub`'s video to `sub` (null if none), and its SSRCs.
  const SfuVideoForwarder* forwarder(int pub, int sub) const;
  // `sub`'s bandwidth estimate (zero if not present).
  DataRate estimate(int sub) const;
  // Keyframe requests sent to `pub`, NACKed packets resent to `sub`.
  uint64_t keyframe_requests(int pub) const;
  uint64_t probes_sent(int sub) const;

 private:
  struct Ingress;
  struct Egress;
  struct Member;
  void SendDown(Egress& e, RtpPacket p, SfuPacketKind kind, Timestamp now, int probe_cluster = -1);
  void SendRtcpDown(int to, std::vector<uint8_t> bytes);
  void Apply(Egress& e, const cc::NetworkControlUpdate& u);
  void RequestKeyframe(int pub, int layer, Timestamp now);
  void OnPublisherRtcp(Member& m, std::span<const uint8_t> bytes, Timestamp at);
  void ForwardVideo(Member& m, int layer, const RtpPacket& media, Timestamp at);
  void Allocate(Egress& e, Timestamp now);
  void UpdateDynacast(Timestamp now);
  DataRate LayerRate(const Ingress& in, int layer, Timestamp now) const;

  SfuConfig cfg_;
  std::map<int, std::unique_ptr<Member>> members_;  // By participant id, in id order.
  std::vector<SfuOutgoing> out_;
  std::vector<SfuLayerActivity> activity_;
};

}  // namespace sm::testbench
