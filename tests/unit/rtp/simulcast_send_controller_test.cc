// C17 (issue #1162): simulcast send. RID and repaired RID on the wire as RTPSender::AttachMidAndRid
// (modules/rtp_rtcp/source/rtp_sender.cc:797-825), VideoSendStreamImpl's allocation limits for a simulcast
// stream (video/video_send_stream_impl.cc CalculateMaxPadBitrateBps, OnEncoderConfigurationChanged), and
// rtp::SimulcastSendController's SimulcastEncoderAdapter behaviour: one encoder per layer, layers paused and
// resumed by the allocation (with a keyframe on every layer when one comes back), keyframe requests per SSRC
// through EncoderRtcpFeedback.
#include <vector>

#include "sm/loss/rtx.h"
#include "sm/rtp/rtp_sender.h"
#include "sm/rtp/simulcast_send_controller.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::rtp;
using Bytes = std::vector<uint8_t>;

namespace {
Timestamp At(int ms) { return Timestamp::Millis(ms); }

std::vector<SenderPacket> Release(RtpSender& s, Timestamp now = Timestamp::Millis(0)) {
  auto out = s.TakePackets();
  for (auto& p : out) {
    s.AssignSequenceNumber(p, now);
    s.OnPacketReleased(p, now);
  }
  return out;
}

sync::ExtensionMap RidMap() {
  sync::ExtensionMap m;
  m.Register(1, sync::uri::kTransportCc);
  m.Register(2, sync::uri::kRid);
  m.Register(3, sync::uri::kRepairedRid);
  m.Register(4, sync::uri::kMid);
  return m;
}
}  // namespace

SM_TEST(SimulcastSend, RidOnMediaUntilAckedAndRepairedRidOnRtx) {
  const sync::ExtensionMap map = RidMap();
  const sync::ExtensionAccessor access(map);
  RtpSenderConfig c;
  c.ssrc = 0x1111;
  c.payload_type = 96;
  c.rtx_ssrc = 0x2222;
  c.rtx_payload_type = 97;
  c.video = true;
  c.rid = "h";
  c.extensions = &map;
  RtpSender s(c);
  const std::vector<Bytes> chunks{Bytes(100, 1), Bytes(100, 2)};
  s.SendVideoFrame(chunks, 1, At(0));
  auto out = Release(s);
  SM_ASSERT_EQ(out.size(), 2u);
  for (auto& p : out) {
    SM_EXPECT_EQ(access.GetString(p.packet, sync::uri::kRid).value_or(""), std::string("h"));
    SM_EXPECT_FALSE(access.GetString(p.packet, sync::uri::kRepairedRid).has_value());
  }
  // A resend on the RTX SSRC carries the repaired RID, never the RID.
  const uint16_t seq = out[0].packet.seq;
  s.OnNack(std::span<const uint16_t>(&seq, 1), At(50), TimeDelta::Millis(20));
  auto rtx = s.TakePackets();
  SM_ASSERT_EQ(rtx.size(), 1u);
  SM_EXPECT_EQ(rtx[0].packet.ssrc, 0x2222u);
  SM_EXPECT_FALSE(access.GetString(rtx[0].packet, sync::uri::kRid).has_value());
  SM_EXPECT_EQ(access.GetString(rtx[0].packet, sync::uri::kRepairedRid).value_or(""), std::string("h"));
  // A report block on the media SSRC stops the RID; the RTX room then reserves the repaired RID.
  const size_t rtx_room_before = s.RtxPacketOverhead();
  s.OnReceivedAckOnSsrc();
  SM_EXPECT_EQ(s.RtxPacketOverhead(), rtx_room_before + 2 + 1 + 3);  // + RRID (2 + 1) and 3 for rounding.
  s.SendVideoFrame(chunks, 3000, At(100));
  out = Release(s, At(100));
  for (auto& p : out) SM_EXPECT_FALSE(access.GetString(p.packet, sync::uri::kRid).has_value());
  // Once the RTX SSRC is acked too, RTX packets carry neither.
  s.OnReceivedAckOnRtxSsrc();
  const uint16_t seq2 = out[0].packet.seq;
  s.OnNack(std::span<const uint16_t>(&seq2, 1), At(150), TimeDelta::Millis(20));
  rtx = s.TakePackets();
  SM_ASSERT_EQ(rtx.size(), 1u);
  SM_EXPECT_FALSE(access.GetString(rtx[0].packet, sync::uri::kRid).has_value());
  SM_EXPECT_FALSE(access.GetString(rtx[0].packet, sync::uri::kRepairedRid).has_value());
  SM_EXPECT_EQ(s.stats().rid_packets, 2u);
  SM_EXPECT_EQ(s.stats().repaired_rid_packets, 1u);
}

SM_TEST(SimulcastSend, ExpectedOverheadCountsTheRidUntilAcked) {
  const sync::ExtensionMap map = RidMap();
  RtpSenderConfig c;
  c.ssrc = 0x1111;
  c.rtx_ssrc = 0x2222;
  c.video = true;
  c.extensions = &map;
  RtpSender plain(c);
  c.rid = "q";
  RtpSender with_rid(c);
  // RtpStreamId at its largest (16 bytes + 1 header byte) while the media SSRC is not acked.
  SM_EXPECT_GE(with_rid.ExpectedPerPacketOverhead(), plain.ExpectedPerPacketOverhead() + 16);
  // Acked: the repaired RID while the RTX SSRC is not.
  with_rid.OnReceivedAckOnSsrc();
  plain.OnReceivedAckOnSsrc();
  SM_EXPECT_GT(with_rid.ExpectedPerPacketOverhead(), plain.ExpectedPerPacketOverhead());
  with_rid.OnReceivedAckOnRtxSsrc();
  plain.OnReceivedAckOnRtxSsrc();
  SM_EXPECT_EQ(with_rid.ExpectedPerPacketOverhead(), plain.ExpectedPerPacketOverhead());
}

// The default 720p H.264 simulcast's allocation limits: min 30 kbps, max 200 + 700 + 2500 kbps, and padding up
// to the start of the top layer: 150 + 500 + min(1.2 x 600, 2500) kbps.
SM_TEST(SimulcastSend, AllocationLimitsFor720p) {
  video::VideoEncoderConfig ec;
  ec.codec_type = video::VideoCodecType::kH264;
  ec.number_of_streams = 3;
  ec.simulcast_layers.resize(3);
  const auto streams = video::CreateEncoderStreams({}, 1280, 720, ec);
  const SimulcastAllocationLimits l = SimulcastAllocationLimitsFor(streams);
  SM_EXPECT_EQ(l.min.kbps(), 30.0);
  SM_EXPECT_EQ(l.max.kbps(), 3400.0);
  SM_EXPECT_EQ(l.pad_up.kbps(), 1370.0);
  // With ALR probing, padding only to the lowest stream's minimum.
  SM_EXPECT_EQ(SimulcastAllocationLimitsFor(streams, DataRate::Zero(), true).pad_up.kbps(), 30.0);
  // One active stream: no padding.
  auto one = streams;
  one[1].active = one[2].active = false;
  const auto l1 = SimulcastAllocationLimitsFor(one);
  SM_EXPECT_EQ(l1.pad_up.bps(), 0);
  SM_EXPECT_EQ(l1.max.kbps(), 200.0);
}

namespace {
struct Fixture {
  SessionConfig cfg;
  std::unique_ptr<Session> session;
  std::vector<VideoSendId> ids;
  explicit Fixture(DataRate start_rate) {
    cfg.local_ssrc = 1;
    cfg.extensions.Register(1, sync::uri::kTransportCc);
    cfg.extensions.Register(2, sync::uri::kRid);
    cfg.extensions.Register(3, sync::uri::kRepairedRid);
    cfg.send_side.gcc.start_rate = start_rate;
    cfg.send_side.gcc.max_rate = DataRate::KilobitsPerSec(5000);
    session = std::make_unique<Session>(cfg);
    const char* rids[] = {"q", "h", "f"};
    for (int i = 0; i < 3; ++i) {
      VideoStreamConfig v;
      v.ssrc = 100 + static_cast<uint32_t>(i);
      v.rtx_ssrc = 200 + static_cast<uint32_t>(i);
      v.rid = rids[i];
      ids.push_back(session->AddVideoSend(v));
    }
  }
  SimulcastSendController Controller(SimulcastSendControllerConfig c = {}) { return SimulcastSendController(c, *session, ids); }
};

SessionEvent Pli(uint32_t ssrc, Timestamp t) {
  SessionEvent e;
  e.type = SessionEventType::kKeyframeRequested;
  e.ssrc = ssrc;
  e.time = t;
  return e;
}

void EncodeAll(SimulcastSendController& c, const SimulcastEncodeDecision& d, Timestamp t) {
  for (size_t i = 0; i < d.layers.size(); ++i) {
    if (!d.layers[i].encode) continue;
    c.OnEncoded(i, {DataSize::Bytes(std::max<int64_t>(200, d.layers[i].target.bps() / 8 / 30)), d.layers[i].keyframe}, t);
  }
}
}  // namespace

SM_TEST(SimulcastSend, FirstFrameConfiguresThreeLayersWithKeyframes) {
  Fixture f(DataRate::KilobitsPerSec(3000));
  auto c = f.Controller();
  const auto d = c.OnCapturedFrame({1280, 720}, At(0));
  SM_ASSERT_EQ(c.streams().size(), 3u);
  SM_EXPECT_EQ(c.streams()[0].width, 320);
  SM_EXPECT_EQ(c.streams()[1].width, 640);
  SM_EXPECT_EQ(c.streams()[2].width, 1280);
  SM_ASSERT_EQ(d.layers.size(), 3u);
  SM_EXPECT_TRUE(d.encode);
  for (const auto& l : d.layers) {
    SM_EXPECT_TRUE(l.encode);
    SM_EXPECT_TRUE(l.keyframe);
  }
  SM_EXPECT_EQ(d.layers[2].height, 720);
  // The lower layers get their targets; the top layer the rest (each at least its minimum).
  SM_EXPECT_GE(d.layers[0].target.kbps(), 30.0);
  SM_EXPECT_LE(d.layers[0].target.kbps(), 150.0);
  SM_EXPECT_GE(d.layers[2].target.kbps(), 600.0);
  // The session registered the stream with the simulcast limits.
  SM_EXPECT_EQ(f.session->send_side().allocator().config("video")->max.kbps(), 3400.0);
  EncodeAll(c, d, At(0));
  const auto d2 = c.OnCapturedFrame({1280, 720}, At(33));
  for (const auto& l : d2.layers) SM_EXPECT_FALSE(l.keyframe);
}

SM_TEST(SimulcastSend, LowRateLeavesTheTopLayerOff) {
  Fixture f(DataRate::KilobitsPerSec(400));
  auto c = f.Controller();
  const auto d = c.OnCapturedFrame({1280, 720}, At(0));
  SM_EXPECT_TRUE(d.layers[0].encode);
  SM_EXPECT_FALSE(d.layers[2].encode);  // 600 kbps minimum not covered.
  SM_EXPECT_TRUE(c.layer_paused(2));
  SM_EXPECT_TRUE(c.allocation().is_bw_limited());
  SM_EXPECT_EQ(c.active_layers(), 2u);
}

SM_TEST(SimulcastSend, KeyframeRequestOnAnyLayerKeysEveryLayer) {
  Fixture f(DataRate::KilobitsPerSec(3000));
  auto c = f.Controller();
  EncodeAll(c, c.OnCapturedFrame({1280, 720}, At(0)), At(0));
  EncodeAll(c, c.OnCapturedFrame({1280, 720}, At(33)), At(33));
  c.OnSessionEvent(Pli(102, At(500)));  // The top layer's SSRC.
  auto d = c.OnCapturedFrame({1280, 720}, At(533));
  for (const auto& l : d.layers) SM_EXPECT_TRUE(l.keyframe);
  EncodeAll(c, d, At(533));
  // Within 300 ms of the last one passed on: ignored.
  c.OnSessionEvent(Pli(100, At(600)));
  d = c.OnCapturedFrame({1280, 720}, At(633));
  for (const auto& l : d.layers) SM_EXPECT_FALSE(l.keyframe);
  // An SSRC that is not this stream's is not ours.
  c.OnSessionEvent(Pli(999, At(1000)));
  SM_EXPECT_FALSE(c.OnCapturedFrame({1280, 720}, At(1033)).layers[0].keyframe);
}

SM_TEST(SimulcastSend, PerLayerKeyframesKeyOnlyTheLayerAsked) {
  Fixture f(DataRate::KilobitsPerSec(3000));
  SimulcastSendControllerConfig cfg;
  cfg.per_layer_keyframes = true;
  auto c = f.Controller(cfg);
  EncodeAll(c, c.OnCapturedFrame({1280, 720}, At(0)), At(0));
  c.OnSessionEvent(Pli(101, At(500)));
  const auto d = c.OnCapturedFrame({1280, 720}, At(533));
  SM_EXPECT_FALSE(d.layers[0].keyframe);
  SM_EXPECT_TRUE(d.layers[1].keyframe);
  SM_EXPECT_FALSE(d.layers[2].keyframe);
}

SM_TEST(SimulcastSend, OutagePauseHoldsTheFrameForAllLayers) {
  Fixture f(DataRate::KilobitsPerSec(3000));
  auto c = f.Controller();
  EncodeAll(c, c.OnCapturedFrame({1280, 720}, At(0)), At(0));
  SessionEvent pause;
  pause.type = SessionEventType::kEncoderPause;
  c.OnSessionEvent(pause);
  const auto d = c.OnCapturedFrame({1280, 720}, At(33));
  SM_EXPECT_FALSE(d.encode);
  SM_EXPECT_TRUE(d.reason == DropReason::kPaused);
  SM_EXPECT_TRUE(d.hold);
  for (const auto& l : d.layers) SM_EXPECT_FALSE(l.encode);
}

SM_TEST(SimulcastSend, SmallInputLimitsTheLayerCount) {
  Fixture f(DataRate::KilobitsPerSec(3000));
  auto c = f.Controller();
  const auto d = c.OnCapturedFrame({640, 360}, At(0));
  // 640x360 allows two layers (LimitSimulcastLayerCount): 320x180 and 640x360; the third is not sent.
  SM_ASSERT_EQ(c.streams().size(), 2u);
  SM_EXPECT_EQ(c.streams()[1].width, 640);
  SM_EXPECT_TRUE(d.layers[1].encode);
  SM_EXPECT_FALSE(d.layers[2].encode);
}
