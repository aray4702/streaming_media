#include <algorithm>
#include <optional>
#include <vector>

#include "sm/rtp/send_side_controller.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::rtp;

namespace {
SenderPacket Media(uint16_t seq, PacketKind kind, cc::PacketPriority prio, size_t payload = 100) {
  SenderPacket p;
  p.packet.ssrc = 1;
  p.packet.payload_type = 96;
  p.packet.seq = seq;
  p.packet.payload.assign(payload, 0);
  p.kind = kind;
  p.priority = prio;
  return p;
}
}  // namespace

SM_TEST(SendSideController, ReleasesWithTransportSequenceNumbersInPriorityOrder) {
  sync::ExtensionMap map;
  map.Register(3, sync::uri::kTransportCc);
  SendSideController c({}, &map);
  c.Start(Timestamp::Millis(0));
  Timestamp now = Timestamp::Millis(0);
  c.Enqueue(Media(10, PacketKind::kVideo, cc::PacketPriority::kVideo), now);
  c.Enqueue(Media(11, PacketKind::kVideo, cc::PacketPriority::kVideo), now);
  c.Enqueue(Media(500, PacketKind::kAudio, cc::PacketPriority::kAudio, 40), now);
  std::vector<ReleasedPacket> all;
  for (int t = 0; t < 50; ++t) {
    auto r = c.Process(Timestamp::Millis(t));
    all.insert(all.end(), r.begin(), r.end());
  }
  SM_ASSERT_EQ(all.size(), 3u);
  sync::ExtensionAccessor access(map);
  for (size_t i = 0; i < all.size(); ++i) {
    SM_EXPECT_EQ(all[i].transport_seq, i);
    auto parsed = RtpPacket::Parse(all[i].bytes);
    SM_ASSERT_TRUE(parsed.has_value());
    SM_EXPECT_TRUE(access.GetTransportSeq(*parsed) == std::optional<uint16_t>(static_cast<uint16_t>(i)));
    SM_EXPECT_EQ(all[i].wire_size.bytes(), static_cast<int64_t>(all[i].bytes.size()) + 28);
  }
  SM_EXPECT_TRUE(all[0].kind == PacketKind::kAudio);  // Audio goes first.
  SM_EXPECT_EQ(c.stats().released, 3u);
  SM_EXPECT_GT(c.send_rate(Timestamp::Millis(49)).bps(), 0);
  SM_EXPECT_EQ(c.send_rate(Timestamp::Millis(10000)).bps(), 0);
}

SM_TEST(SendSideController, PacingSpreadsAFrameOverTime) {
  SendSideController c;
  c.Start(Timestamp::Millis(0));
  for (uint16_t i = 0; i < 40; ++i)
    c.Enqueue(Media(i, PacketKind::kVideo, cc::PacketPriority::kVideo, 1100), Timestamp::Millis(0));
  size_t at_zero = c.Process(Timestamp::Millis(0)).size();
  SM_EXPECT_LT(at_zero, 40u);
  size_t total = at_zero;
  for (int t = 1; t < 500; ++t) total += c.Process(Timestamp::Millis(t)).size();
  SM_EXPECT_EQ(total, 40u);
}

SM_TEST(SendSideController, PaddingIsARealPaddingOnlyPacket) {
  SendSideController c;
  RtpSenderConfig sc;
  sc.ssrc = 0x10;
  sc.rtx_ssrc = 0x20;
  RtpSender sender(sc);
  c.SetPaddingSender(&sender);
  c.Start(Timestamp::Millis(0));
  c.pacer().SetPacingRates(DataRate::KilobitsPerSec(1000), DataRate::KilobitsPerSec(200));
  size_t padding = 0;
  for (int t = 0; t < 200; ++t) {
    for (auto& r : c.Process(Timestamp::Millis(t))) {
      SM_EXPECT_TRUE(r.kind == PacketKind::kPadding);
      auto p = RtpPacket::Parse(r.bytes);
      SM_ASSERT_TRUE(p.has_value());
      SM_EXPECT_EQ(p->ssrc, 0x20u);
      SM_EXPECT_TRUE(p->payload.empty());
      SM_EXPECT_GT(p->padding_size, 0);
      ++padding;
    }
  }
  SM_EXPECT_GT(padding, 0u);
  SM_EXPECT_EQ(c.stats().padding, padding);
}

SM_TEST(SendSideController, ComputeRatesDividesOutProtectionAndClamps) {
  SendSideConfig cfg;
  cfg.gcc.start_rate = DataRate::KilobitsPerSec(1000);
  cfg.video_min = DataRate::KilobitsPerSec(50);
  cfg.video_max = DataRate::KilobitsPerSec(600);
  SendSideController c(cfg);
  c.Start(Timestamp::Millis(0));
  SendRates r = c.ComputeRates(DataRate::KilobitsPerSec(100), 0.0);
  SM_EXPECT_EQ(r.video_target.bps(), 600000);  // 900 kbps clamped to the maximum.
  r = c.ComputeRates(DataRate::KilobitsPerSec(100), 0.5);
  SM_EXPECT_EQ(r.video_target.bps(), 600000);  // 900 / 1.5 = 600.
  r = c.ComputeRates(DataRate::KilobitsPerSec(100), 1.0);
  SM_EXPECT_EQ(r.video_target.bps(), 450000);
  r = c.ComputeRates(DataRate::KilobitsPerSec(990), 0.0);
  SM_EXPECT_EQ(r.video_target.bps(), 50000);  // Clamped to the minimum.
  SM_EXPECT_EQ(r.audio_rate.bps(), 990000);
}

SM_TEST(SendSideController, SilentFeedbackPausesTheEncoderAndResumes) {
  SendSideController c;
  c.Start(Timestamp::Millis(0));
  c.gcc().OnRttUpdate(TimeDelta::Millis(50));
  bool paused = false;
  // No feedback for a long time while sending: the outage monitor must react.
  for (int t = 0; t < 8000; t += 10) {
    c.Process(Timestamp::Millis(t));
    if (t < 7000) c.outage().OnFeedback(Timestamp::Millis(t < 1000 ? t : 999));
    ControllerActions a = c.TakeActions();
    if (a.pause_encoder) paused = true;
  }
  SM_EXPECT_TRUE(paused);
  SM_EXPECT_FALSE(c.TakeActions().pause_encoder);  // Taking clears the flags.
}

SM_TEST(SendSideController, PaddingCarriesTheLastMediaTimestampAndFitsItsAccountedSize) {
  sync::ExtensionMap map;
  map.Register(1, sync::uri::kTransportCc);
  map.Register(2, sync::uri::kAbsSendTime);
  SendSideController c({}, &map);
  RtpSenderConfig sc;
  sc.ssrc = 0x10;
  sc.rtx_ssrc = 0x20;
  sc.extensions = &map;
  RtpSender sender(sc);
  c.SetPaddingSender(&sender);
  c.Start(Timestamp::Millis(0));
  sender.SendVideoFrame(std::vector<std::vector<uint8_t>>{std::vector<uint8_t>(100, 1)}, 123456, Timestamp::Millis(0));
  for (auto& p : sender.TakePackets()) c.Enqueue(std::move(p), Timestamp::Millis(0));
  c.pacer().SetPacingRates(DataRate::KilobitsPerSec(1000), DataRate::KilobitsPerSec(300));
  size_t padding = 0;
  for (int t = 0; t < 300; ++t) {
    for (auto& r : c.Process(Timestamp::Millis(t))) {
      if (r.kind != PacketKind::kPadding) continue;
      auto p = RtpPacket::Parse(r.bytes);
      SM_ASSERT_TRUE(p.has_value());
      SM_EXPECT_EQ(p->timestamp, 123456u);
      SM_EXPECT_LE(static_cast<int64_t>(r.bytes.size()) + 28, r.wire_size.bytes());
      ++padding;
    }
  }
  SM_EXPECT_GT(padding, 0u);
}

SM_TEST(SendSideController, VideoBudgetShrinksByTheMeasuredRepairShare) {
  SendSideConfig cfg;
  cfg.gcc.start_rate = DataRate::KilobitsPerSec(2000);
  cfg.video_min = DataRate::KilobitsPerSec(50);
  cfg.video_max = DataRate::KilobitsPerSec(5000);
  SendSideController c(cfg);
  c.Start(Timestamp::Millis(0));
  SM_EXPECT_EQ(c.ComputeMeasuredRates(DataRate::Zero(), 0.0).video_target.bps(), 2000000);  // Nothing sent yet.
  // Three media packets for every resend: a quarter of the bytes is repair.
  for (uint16_t i = 0; i < 12; ++i) {
    c.Enqueue(Media(i, i % 4 == 3 ? PacketKind::kRtx : PacketKind::kVideo,
                    i % 4 == 3 ? cc::PacketPriority::kRetransmission : cc::PacketPriority::kVideo, 500),
              Timestamp::Millis(0));
  }
  for (int t = 0; t < 200; ++t) c.Process(Timestamp::Millis(t));
  SM_EXPECT_NEAR(c.repair_share(), 0.25, 0.01);
  SM_EXPECT_NEAR(static_cast<double>(c.ComputeMeasuredRates(DataRate::Zero(), 0.0).video_target.bps()), 1500000.0, 20000.0);
  // The configured FEC rate is a floor: 100% FEC takes half the budget.
  SM_EXPECT_NEAR(static_cast<double>(c.ComputeMeasuredRates(DataRate::Zero(), 1.0).video_target.bps()), 1000000.0, 1.0);
  // The share is measured over one second, so it is forgotten afterwards.
  c.Process(Timestamp::Millis(1500));
  SM_EXPECT_EQ(c.repair_share(), 0.0);
}

SM_TEST(SendSideController, RepairNeverTakesMoreThanHalfOfTheBudget) {
  SendSideConfig cfg;
  cfg.gcc.start_rate = DataRate::KilobitsPerSec(2000);
  SendSideController c(cfg);
  c.Start(Timestamp::Millis(0));
  for (uint16_t i = 0; i < 10; ++i)
    c.Enqueue(Media(i, PacketKind::kRtx, cc::PacketPriority::kRetransmission, 500), Timestamp::Millis(0));
  for (int t = 0; t < 200; ++t) c.Process(Timestamp::Millis(t));
  SM_EXPECT_EQ(c.repair_share(), 1.0);
  SM_EXPECT_NEAR(static_cast<double>(c.ComputeMeasuredRates(DataRate::Zero(), 0.0).video_target.bps()), 1000000.0, 1.0);
}
