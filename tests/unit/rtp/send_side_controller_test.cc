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
