// The test bench's simulated SFU (testbench/sfu.h; C21, #1180): the stream allocator's layer selection, the video
// downtrack's forwarding (a switch only on the target layer's keyframe, sequence numbers and timestamps rewritten,
// resends from the ingress cache), and RTCP termination on both legs of the Sfu node.
#include <map>
#include <optional>
#include <vector>

#include "sfu.h"
#include "sm/cc/transport_feedback.h"
#include "sm/loss/rtcp_feedback.h"
#include "sm/rtp/h264.h"
#include "sm/sync/header_extensions.h"
#include "sm/timing/rtcp_stats.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::testbench;

namespace {

DataRate Kbps(int64_t k) { return DataRate::KilobitsPerSec(k); }

SfuAllocTrack Track(std::vector<int64_t> kbps, int max_layer, int current) {
  SfuAllocTrack t;
  for (int64_t k : kbps) t.layer_rates.push_back(Kbps(k));
  t.max_layer = max_layer;
  t.current = current;
  return t;
}

// An H.264 packet of one NAL unit: an IDR slice (a keyframe's first packet) or a non-IDR slice, both with
// first_mb_in_slice 0 (ue '1'), then slice_type and pic_parameter_set_id 0.
std::vector<uint8_t> H264Payload(bool keyframe, size_t size = 600) {
  std::vector<uint8_t> p(size, 0x5a);
  p[0] = keyframe ? 0x65 : 0x41;
  p[1] = keyframe ? 0x88 : 0x9a;  // '1' '0001000' (I, 7) / '1' '00110' (P, 5) ...
  p[2] = 0x80;                    // ... pps id '1' (0).
  return p;
}

RtpPacket Media(uint32_t ssrc, uint16_t seq, uint32_t ts, bool keyframe, uint16_t tcc = 0) {
  RtpPacket p;
  p.payload_type = 96;
  p.ssrc = ssrc;
  p.seq = seq;
  p.timestamp = ts;
  p.payload = H264Payload(keyframe);
  if (tcc) sync::WriteHeaderExtensions({{1, {static_cast<uint8_t>(tcc >> 8), static_cast<uint8_t>(tcc)}}}, p);
  return p;
}

}  // namespace

// --- Layer selection ------------------------------------------------------------------------------------------------

SM_TEST(TestbenchSfu, AllocatorGivesEveryTrackItsWantedLayerWhenItFits) {
  const std::vector<SfuAllocTrack> tracks = {Track({150, 500, 2500}, 2, 2), Track({150, 500, 2500}, 2, 2)};
  const std::vector<int> l = SfuAllocate(Kbps(6000), tracks);
  SM_EXPECT_EQ(l[0], 2);
  SM_EXPECT_EQ(l[1], 2);
  // The subscriber's wanted layer caps it (a grid tile asks for the middle one).
  const std::vector<int> grid = SfuAllocate(Kbps(6000), {Track({150, 500, 2500}, 1, 1)});
  SM_EXPECT_EQ(grid[0], 1);
}

SM_TEST(TestbenchSfu, AllocatorFillsLowestLayersFirstThenUpgradesInTurn) {
  // 1400 kbps for three tracks of 150/500/2500: all three bottom layers (450), then the first two move up one layer
  // each (+350 each = 1150); the third's step (+350) no longer fits. Nobody gets a top layer before everyone has one.
  std::vector<SfuAllocTrack> tracks(3, Track({150, 500, 2500}, 2, 1));
  const std::vector<int> l = SfuAllocate(Kbps(1400), tracks, 1.0);
  SM_EXPECT_EQ(l[0], 1);
  SM_EXPECT_EQ(l[1], 1);
  SM_EXPECT_EQ(l[2], 0);
}

SM_TEST(TestbenchSfu, AllocatorPausesTracksThatDoNotFitEvenAtTheBottom) {
  std::vector<SfuAllocTrack> tracks(3, Track({150, 500, 2500}, 2, 0));
  const std::vector<int> l = SfuAllocate(Kbps(320), tracks);
  SM_EXPECT_EQ(l[0], 0);
  SM_EXPECT_EQ(l[1], 0);
  SM_EXPECT_EQ(l[2], -1);
  SM_EXPECT_EQ(SfuAllocate(Kbps(0), tracks)[0], -1);
}

SM_TEST(TestbenchSfu, AllocatorSkipsLayersThatAreNotReceived) {
  // The top layer is paused at the publisher (dynacast) or not received yet: rate 0, never chosen.
  const std::vector<int> l = SfuAllocate(Kbps(10000), {Track({150, 500, 0}, 2, 1)});
  SM_EXPECT_EQ(l[0], 1);
  // Nothing received at all: paused.
  SM_EXPECT_EQ(SfuAllocate(Kbps(10000), {Track({0, 0, 0}, 2, -1)})[0], -1);
}

SM_TEST(TestbenchSfu, AllocatorNeedsAMarginToMoveUpButNotToStay) {
  // Exactly the top layer's rate: a track already on it keeps it, one below it doesn't move up (10% margin).
  SM_EXPECT_EQ(SfuAllocate(Kbps(2500), {Track({150, 500, 2500}, 2, 2)})[0], 2);
  SM_EXPECT_EQ(SfuAllocate(Kbps(2500), {Track({150, 500, 2500}, 2, 1)})[0], 1);
  SM_EXPECT_EQ(SfuAllocate(Kbps(2800), {Track({150, 500, 2500}, 2, 1)})[0], 2);
}

// --- The video downtrack -------------------------------------------------------------------------------------------

SM_TEST(TestbenchSfu, PayloadsAreWhatTheForwarderLooksFor) {
  const auto key = rtp::H264Depacketizer::Parse(H264Payload(true));
  SM_EXPECT_TRUE(key.keyframe && key.first_packet_in_frame);
  SM_EXPECT_FALSE(rtp::H264Depacketizer::Parse(H264Payload(false)).keyframe);
}

SM_TEST(TestbenchSfu, ForwarderStartsAndSwitchesOnlyOnTheTargetLayersKeyframe) {
  SfuVideoForwarder f({1002, 1003, 97, 90000});
  const Timestamp t0 = Timestamp::Seconds(1);
  f.SetTarget(0);
  SM_EXPECT_TRUE(f.WantsKeyframe());
  SM_EXPECT_FALSE(f.Forward(Media(1002, 100, 9000, false), 0, t0).has_value());  // Waits for a keyframe.
  auto first = f.Forward(Media(1002, 101, 12000, true), 0, t0);
  SM_ASSERT_TRUE(first.has_value());
  SM_EXPECT_EQ(first->ssrc, 1002u);
  SM_EXPECT_EQ(f.current(), 0);
  SM_EXPECT_FALSE(f.WantsKeyframe());
  SM_EXPECT_EQ(f.resumes(), 1);
  SM_EXPECT_TRUE(f.Forward(Media(1002, 102, 15000, false), 0, t0 + TimeDelta::Millis(33)).has_value());
  // Another layer's packets are dropped.
  SM_EXPECT_FALSE(f.Forward(Media(1202, 7000, 500000, false), 2, t0 + TimeDelta::Millis(33)).has_value());
  // Switch up: the current layer keeps going until layer 2's keyframe.
  f.SetTarget(2);
  SM_EXPECT_TRUE(f.WantsKeyframe());
  SM_EXPECT_FALSE(f.Forward(Media(1202, 7001, 503000, false), 2, t0 + TimeDelta::Millis(66)).has_value());
  auto still = f.Forward(Media(1002, 103, 18000, false), 0, t0 + TimeDelta::Millis(66));
  SM_ASSERT_TRUE(still.has_value());
  auto sw = f.Forward(Media(1202, 7002, 506000, true), 2, t0 + TimeDelta::Millis(100));
  SM_ASSERT_TRUE(sw.has_value());
  SM_EXPECT_EQ(f.current(), 2);
  SM_EXPECT_EQ(f.switches(), 1);
  // One SSRC, sequence numbers without a gap, timestamps moving forward.
  SM_EXPECT_EQ(sw->ssrc, 1002u);
  SM_EXPECT_EQ(sw->seq, static_cast<uint16_t>(still->seq + 1));
  SM_EXPECT_GT(static_cast<int32_t>(sw->timestamp - still->timestamp), 0);
  // The old layer is no longer forwarded; the new one continues.
  SM_EXPECT_FALSE(f.Forward(Media(1002, 104, 21000, false), 0, t0 + TimeDelta::Millis(100)).has_value());
  auto next = f.Forward(Media(1202, 7003, 509000, false), 2, t0 + TimeDelta::Millis(133));
  SM_ASSERT_TRUE(next.has_value());
  SM_EXPECT_EQ(next->seq, static_cast<uint16_t>(sw->seq + 1));
  SM_EXPECT_EQ(next->timestamp - sw->timestamp, 3000u);
  // A duplicate (the original and its resend from the publisher) goes once.
  SM_EXPECT_FALSE(f.Forward(Media(1202, 7003, 509000, false), 2, t0 + TimeDelta::Millis(134)).has_value());
  // Pausing stops at once.
  f.SetTarget(-1);
  SM_EXPECT_EQ(f.current(), -1);
  SM_EXPECT_FALSE(f.Forward(Media(1202, 7004, 512000, false), 2, t0 + TimeDelta::Millis(166)).has_value());
}

SM_TEST(TestbenchSfu, ForwarderAlignsLayerTimestampsThroughSenderReports) {
  // Both layers stamp the capture instant with their own random offset; their sender reports map the same NTP time
  // to each. After the switch, a frame captured 33 ms after the last forwarded one is 2970 ticks later.
  SfuVideoForwarder f({1002, 1003, 97, 90000});
  const Timestamp t0 = Timestamp::Seconds(10);
  const uint32_t off0 = 1000, off2 = 4000000000u;
  f.OnLayerSenderReport(0, t0, off0 + 900000);
  f.OnLayerSenderReport(2, t0, off2 + 900000);
  f.SetTarget(0);
  auto a = f.Forward(Media(1002, 1, off0 + 900000, true), 0, t0);
  SM_ASSERT_TRUE(a.has_value());
  f.SetTarget(2);
  auto b = f.Forward(Media(1202, 50, off2 + 900000 + 2970, true), 2, t0 + TimeDelta::Millis(70));
  SM_ASSERT_TRUE(b.has_value());
  SM_EXPECT_EQ(b->timestamp - a->timestamp, 2970u);
  // The downtrack's own sender report: the current layer's timestamp at that NTP time, in the downtrack's terms.
  const std::optional<uint32_t> at = f.TimestampAt(t0 + TimeDelta::Millis(100));
  SM_ASSERT_TRUE(at.has_value());
  SM_EXPECT_EQ(*at - a->timestamp, 9000u);
}

SM_TEST(TestbenchSfu, ForwarderResendsFromTheLayerItForwardedWithinItsBudget) {
  SfuVideoForwarder f({1002, 1003, 97, 90000});
  const Timestamp t0 = Timestamp::Seconds(1);
  std::map<std::pair<int, uint16_t>, RtpPacket> cache;
  auto keep = [&](int layer, const RtpPacket& p) { cache[{layer, p.seq}] = p; };
  auto lookup = [&](int layer, uint16_t seq) -> const RtpPacket* {
    auto it = cache.find({layer, seq});
    return it == cache.end() ? nullptr : &it->second;
  };
  f.SetTarget(0);
  const RtpPacket k0 = Media(1002, 500, 1000, true), d0 = Media(1002, 501, 4000, false);
  keep(0, k0);
  keep(0, d0);
  auto o1 = f.Forward(k0, 0, t0);
  auto o2 = f.Forward(d0, 0, t0);
  f.SetTarget(1);
  const RtpPacket k1 = Media(1102, 9000, 7000, true);
  keep(1, k1);
  auto o3 = f.Forward(k1, 1, t0 + TimeDelta::Millis(66));
  SM_ASSERT_TRUE(o1 && o2 && o3);
  SM_EXPECT_EQ(f.LayerOf(o2->seq), 0);
  SM_EXPECT_EQ(f.LayerOf(o3->seq), 1);
  int64_t budget = 100000;
  const std::vector<RtpPacket> rtx = f.Resend({o2->seq, o3->seq}, lookup, t0 + TimeDelta::Millis(100), TimeDelta::Millis(50), &budget);
  SM_ASSERT_EQ(rtx.size(), 2u);
  // RFC 4588: the RTX SSRC and payload type, the downtrack's sequence number first, then the layer packet's payload.
  SM_EXPECT_EQ(rtx[0].ssrc, 1003u);
  SM_EXPECT_EQ(rtx[0].payload_type, 97);
  SM_EXPECT_EQ(static_cast<uint16_t>(rtx[0].payload[0] << 8 | rtx[0].payload[1]), o2->seq);
  SM_EXPECT_TRUE(std::equal(d0.payload.begin(), d0.payload.end(), rtx[0].payload.begin() + 2));
  SM_EXPECT_EQ(rtx[0].timestamp, o2->timestamp);
  SM_EXPECT_EQ(static_cast<uint16_t>(rtx[1].payload[0] << 8 | rtx[1].payload[1]), o3->seq);
  SM_EXPECT_TRUE(std::equal(k1.payload.begin(), k1.payload.end(), rtx[1].payload.begin() + 2));
  SM_EXPECT_EQ(rtx[1].seq, static_cast<uint16_t>(rtx[0].seq + 1));
  // Asked again within the RTT: not resent; after it: resent, at most kMaxResends times in all.
  SM_EXPECT_EQ(f.Resend({o2->seq}, lookup, t0 + TimeDelta::Millis(120), TimeDelta::Millis(50), &budget).size(), 0u);
  SM_EXPECT_EQ(f.Resend({o2->seq}, lookup, t0 + TimeDelta::Millis(200), TimeDelta::Millis(50), &budget).size(), 1u);
  SM_EXPECT_EQ(f.Resend({o2->seq}, lookup, t0 + TimeDelta::Millis(300), TimeDelta::Millis(50), &budget).size(), 1u);
  SM_EXPECT_EQ(f.Resend({o2->seq}, lookup, t0 + TimeDelta::Millis(400), TimeDelta::Millis(50), &budget).size(), 0u);
  // An empty budget sends nothing; a sequence number never forwarded, or gone from the cache, neither.
  int64_t none = 0;
  SM_EXPECT_EQ(f.Resend({o3->seq}, lookup, t0 + TimeDelta::Seconds(1), TimeDelta::Millis(50), &none).size(), 0u);
  SM_EXPECT_EQ(f.Resend({static_cast<uint16_t>(o3->seq + 5)}, lookup, t0 + TimeDelta::Seconds(1), TimeDelta::Millis(50), &budget).size(), 0u);
  cache.clear();
  SM_EXPECT_EQ(f.Resend({o3->seq}, lookup, t0 + TimeDelta::Seconds(1), TimeDelta::Millis(50), &budget).size(), 0u);
}

// --- RTCP termination through the Sfu node --------------------------------------------------------------------------

namespace {

struct TwoParticipants {
  SfuConfig cfg;
  std::unique_ptr<Sfu> sfu;
  std::vector<SfuOutgoing> out;
  uint16_t tcc = 1;
  TwoParticipants() {
    cfg.ingress.extensions.Register(1, sync::uri::kTransportCc);
    cfg.ingress.rtcp.has_video = true;
    cfg.dynacast = false;
    sfu = std::make_unique<Sfu>(cfg);
    sfu->Join(0, Publisher(1000), 2, Timestamp::Millis(0));
    sfu->Join(1, Publisher(2000), 2, Timestamp::Millis(0));
  }
  static SfuPublisher Publisher(uint32_t base) {
    SfuPublisher p;
    rtp::AudioStreamConfig a;
    a.ssrc = base + 1;
    p.audio = a;
    for (uint32_t l = 0; l < 3; ++l) {
      rtp::VideoStreamConfig v;
      v.ssrc = base + 2 + 100 * l;
      v.rtx_ssrc = base + 3 + 100 * l;
      v.rtx_payload_type = 97;
      v.payload_type = 96;
      p.layers.push_back(v);
    }
    return p;
  }
  // Participant 0 publishes layer 0 at about 160 kbps: a packet every 30 ms (600 B), a keyframe when `key`.
  void Publish(Timestamp at, uint16_t seq, bool key) {
    const RtpPacket p = Media(1002, seq, 90 * static_cast<uint32_t>(at.ms()), key, tcc++);
    sfu->OnPublishPacket(0, p.Serialize(), at);
  }
  void Run(Timestamp at) {
    sfu->Process(at);
    for (SfuOutgoing& o : sfu->TakeOutgoing()) out.push_back(std::move(o));
  }
  std::vector<SfuOutgoing> Take() { return std::exchange(out, {}); }
};

bool HasRtcp(const std::vector<SfuOutgoing>& out, int to, bool subscribe_leg, loss::RtcpType type,
             std::optional<uint32_t> media_ssrc = std::nullopt) {
  for (const SfuOutgoing& o : out) {
    if (o.to != to || o.subscribe_leg != subscribe_leg || o.kind != SfuPacketKind::kRtcp) continue;
    auto views = loss::SplitCompoundRtcp(o.bytes);
    if (!views) continue;
    for (const auto& v : *views) {
      if (v.type != type) continue;
      if (!media_ssrc) return true;
      if (type == loss::RtcpType::kPli) {
        if (auto p = loss::ParsePli(v.data); p && p->media_ssrc == *media_ssrc) return true;
      } else if (type == loss::RtcpType::kSenderReport) {
        if (auto s = timing::ParseSenderReport(v.data); s && s->ssrc == *media_ssrc) return true;
      }
    }
  }
  return false;
}

}  // namespace

SM_TEST(TestbenchSfu, PublishLegIsTerminatedAndASwitchAsksForAKeyframe) {
  TwoParticipants t;
  uint16_t seq = 100;
  std::vector<SfuOutgoing> all;
  // A second of delta frames on layer 0: the SFU measures the layer, gives it to participant 1, and, having no
  // keyframe yet, asks participant 0 for one with a PLI on layer 0's SSRC from its ingress session.
  for (int ms = 0; ms <= 1200; ms += 10) {
    if (ms % 30 == 0) t.Publish(Timestamp::Millis(ms), seq++, false);
    t.Run(Timestamp::Millis(ms));
    for (SfuOutgoing& o : t.Take()) all.push_back(std::move(o));
  }
  SM_EXPECT_TRUE(HasRtcp(all, 0, false, loss::RtcpType::kTransportCc));  // Feedback for the publisher's GoogCC.
  SM_EXPECT_TRUE(HasRtcp(all, 0, false, loss::RtcpType::kPli, 1002u));
  bool video_before_key = false;
  for (const SfuOutgoing& o : all) video_before_key |= o.to == 1 && o.kind == SfuPacketKind::kVideo;
  SM_EXPECT_FALSE(video_before_key);
  SM_EXPECT_GE(t.sfu->keyframe_requests(0), 1u);
  // The keyframe arrives: from there participant 1 gets the layer on the track's SSRC, with its own transport-wide
  // sequence numbers (from 1) and none of the publish leg's other extensions.
  t.Publish(Timestamp::Millis(1210), seq++, true);
  t.Publish(Timestamp::Millis(1240), seq++, false);
  t.Run(Timestamp::Millis(1240));
  std::vector<RtpPacket> got;
  for (const SfuOutgoing& o : t.Take()) {
    SM_EXPECT_FALSE(o.to == 0 && o.kind != SfuPacketKind::kRtcp);  // Only the ingress session's RTCP goes to the publisher.
    if (o.kind == SfuPacketKind::kVideo) got.push_back(*RtpPacket::Parse(o.bytes));
  }
  SM_ASSERT_EQ(got.size(), 2u);
  SM_EXPECT_EQ(got[0].ssrc, 1002u);
  SM_EXPECT_EQ(got[1].seq, static_cast<uint16_t>(got[0].seq + 1));
  auto ext = sync::ParseHeaderExtensions(got[1].extension_profile, got[1].extension);
  SM_ASSERT_TRUE(ext.has_value());
  SM_ASSERT_EQ(ext->size(), 1u);
  SM_EXPECT_EQ((*ext)[0].id, 1);
  SM_EXPECT_EQ(t.sfu->forwarder(0, 1)->current(), 0);
}

SM_TEST(TestbenchSfu, SubscribeLegIsTerminatedOnlyKeyframeRequestsGoUp) {
  TwoParticipants t;
  uint16_t seq = 100;
  for (int ms = 0; ms <= 1200; ms += 10) {
    if (ms % 30 == 0) t.Publish(Timestamp::Millis(ms), seq++, ms == 1200);
    t.Run(Timestamp::Millis(ms));
  }
  for (int ms = 1210; ms <= 1500; ms += 10) {
    if (ms % 30 == 0) t.Publish(Timestamp::Millis(ms), seq++, false);
    t.Run(Timestamp::Millis(ms));
  }
  std::vector<RtpPacket> got;
  for (const SfuOutgoing& o : t.Take()) {
    if (o.to == 1 && o.kind == SfuPacketKind::kVideo) got.push_back(*RtpPacket::Parse(o.bytes));
  }
  SM_ASSERT_GE(got.size(), 5u);
  // The subscriber's NACK is answered by the SFU with RTX on the downtrack's RTX SSRC; its receiver report goes
  // no further. Neither reaches the publisher.
  loss::Nack nack;
  nack.sender_ssrc = 9001;
  nack.media_ssrc = 1002;
  nack.seqs = {got[2].seq};
  timing::ReceiverReport rr;
  rr.ssrc = 9001;
  timing::ReportBlock b;
  b.ssrc = 1002;
  b.extended_highest_seq = got.back().seq;
  rr.blocks.push_back(b);
  const std::vector<std::vector<uint8_t>> parts = {timing::SerializeReceiverReport(rr), loss::SerializeNack(nack)};
  t.sfu->OnSubscribePacket(1, loss::BuildCompoundRtcp(parts), Timestamp::Millis(1510));
  t.Run(Timestamp::Millis(1510));
  std::vector<SfuOutgoing> out = t.Take();
  int rtx = 0;
  for (const SfuOutgoing& o : out) {
    if (o.to == 1 && o.kind == SfuPacketKind::kRtx) {
      auto p = RtpPacket::Parse(o.bytes);
      SM_ASSERT_TRUE(p.has_value());
      SM_EXPECT_EQ(p->ssrc, 1003u);
      SM_EXPECT_EQ(static_cast<uint16_t>(p->payload[0] << 8 | p->payload[1]), got[2].seq);
      ++rtx;
    }
    SM_EXPECT_FALSE(o.to == 0 && o.kind == SfuPacketKind::kRtcp && HasRtcp({o}, 0, false, loss::RtcpType::kNack));
  }
  SM_EXPECT_EQ(rtx, 1);
  // The subscriber's PLI on the downtrack becomes the ingress session's PLI on the layer being forwarded.
  const uint64_t before = t.sfu->keyframe_requests(0);
  loss::Pli pli;
  pli.sender_ssrc = 9001;
  pli.media_ssrc = 1002;
  const std::vector<std::vector<uint8_t>> pli_parts = {timing::SerializeReceiverReport(rr), loss::SerializePli(pli)};
  t.sfu->OnSubscribePacket(1, loss::BuildCompoundRtcp(pli_parts), Timestamp::Millis(1900));
  t.Run(Timestamp::Millis(1900));
  out = t.Take();
  SM_EXPECT_EQ(t.sfu->keyframe_requests(0), before + 1);
  SM_EXPECT_TRUE(HasRtcp(out, 0, false, loss::RtcpType::kPli, 1002u));
  // An RRTR from the subscriber is answered by the SFU with a DLRR, on the subscribe leg.
  timing::ExtendedReports xr;
  xr.sender_ssrc = 9001;
  xr.rrtr = timing::NtpTime::FromTimestamp(Timestamp::Seconds(5));
  const std::vector<std::vector<uint8_t>> xr_parts = {timing::SerializeReceiverReport(rr), timing::SerializeExtendedReports(xr)};
  t.sfu->OnSubscribePacket(1, loss::BuildCompoundRtcp(xr_parts), Timestamp::Millis(1910));
  bool dlrr = false;
  for (const SfuOutgoing& o : t.sfu->TakeOutgoing()) {
    auto views = loss::SplitCompoundRtcp(o.bytes);
    if (o.to != 1 || !views) continue;
    for (const auto& v : *views) {
      if (v.type != loss::RtcpType::kXr) continue;
      auto r = timing::ParseExtendedReports(v.data);
      dlrr |= r && r->dlrr.size() == 1 && r->dlrr[0].ssrc == 9001 && r->dlrr[0].last_rr == xr.rrtr->Compact();
    }
  }
  SM_EXPECT_TRUE(dlrr);
}

SM_TEST(TestbenchSfu, PublisherSenderReportsBecomeTheDowntracksOwn) {
  TwoParticipants t;
  uint16_t seq = 100;
  for (int ms = 0; ms <= 1200; ms += 10) {
    if (ms % 30 == 0) t.Publish(Timestamp::Millis(ms), seq++, ms == 1200);
    t.Run(Timestamp::Millis(ms));
  }
  t.Take();
  // The publisher's SR for layer 0 at 1200 ms (its RTP clock: 90 ticks per ms of call time); the SFU sends its own
  // SR on the downtrack at 1250 ms: NTP of then, the RTP timestamp of then.
  timing::SenderReport sr;
  sr.ssrc = 1002;
  sr.ntp = timing::NtpTime::FromTimestamp(Timestamp::Millis(1200));
  sr.rtp_timestamp = 90 * 1200;
  t.sfu->OnPublishPacket(0, timing::SerializeSenderReport(sr), Timestamp::Millis(1250));
  std::optional<timing::SenderReport> down;
  for (const SfuOutgoing& o : t.sfu->TakeOutgoing()) {
    if (o.to != 1 || o.kind != SfuPacketKind::kRtcp) continue;
    if (auto views = loss::SplitCompoundRtcp(o.bytes)) {
      for (const auto& v : *views) {
        if (v.type == loss::RtcpType::kSenderReport) down = timing::ParseSenderReport(v.data);
      }
    }
  }
  SM_ASSERT_TRUE(down.has_value());
  SM_EXPECT_EQ(down->ssrc, 1002u);
  SM_EXPECT_EQ(down->ntp.ToTimestamp(), Timestamp::Millis(1250));
  // The downtrack forwards layer 0 with its timestamps unchanged (its first layer), so the RTP of 1250 ms is 90 x 1250.
  SM_EXPECT_EQ(down->rtp_timestamp, 90u * 1250u);
  SM_EXPECT_GT(down->packet_count, 0u);
}
