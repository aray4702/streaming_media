#include "sm/cc/congestion_window_pushback.h"
#include "sm/cc/transport_feedback.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::cc;

namespace {
const Timestamp kNow = Timestamp::Millis(1000);
}

// The first six cases are libwebrtc's congestion_window_pushback_controller_unittest.cc.
SM_TEST(CongestionWindowPushback, FullWindowShrinksTheTarget) {
  CongestionWindowPushback c;
  c.UpdateOutstandingData(DataSize::Bytes(100000));
  c.SetDataWindow(DataSize::Bytes(50000));
  DataRate r = c.UpdateTargetBitrate(DataRate::BitsPerSec(80000), kNow);
  SM_EXPECT_EQ(r.bps(), 72000);  // Fill 2.0 > 1.5: ratio 0.9.
  r = c.UpdateTargetBitrate(DataRate::BitsPerSec(80000), kNow);
  SM_EXPECT_EQ(r.bps(), static_cast<int64_t>(80000 * 0.9 * 0.9));
}

SM_TEST(CongestionWindowPushback, UnderTheWindowLeavesTheTarget) {
  CongestionWindowPushback c;
  c.UpdateOutstandingData(DataSize::Bytes(199999));
  c.SetDataWindow(DataSize::Bytes(200000));
  SM_EXPECT_EQ(c.UpdateTargetBitrate(DataRate::BitsPerSec(80000), kNow).bps(), 80000);
  SM_EXPECT_EQ(c.reduce_ratio(), 0.0);
}

SM_TEST(CongestionWindowPushback, NeverBelowTheMinimumPushbackRate) {
  CongestionWindowPushback c;
  c.UpdateOutstandingData(DataSize::Bytes(100000));
  c.SetDataWindow(DataSize::Bytes(50000));
  DataRate r = c.UpdateTargetBitrate(DataRate::BitsPerSec(35000), kNow);
  SM_EXPECT_EQ(r.bps(), static_cast<int64_t>(35000 * 0.9));
  c.SetDataWindow(DataSize::Bytes(20000));
  r = c.UpdateTargetBitrate(r, kNow);
  SM_EXPECT_EQ(r.bps(), 30000);
  // A target already under the minimum is left alone.
  r = c.UpdateTargetBitrate(DataRate::BitsPerSec(20000), kNow);
  SM_EXPECT_EQ(r.bps(), 20000);
}

SM_TEST(CongestionWindowPushback, NoWindowNoPushback) {
  CongestionWindowPushback c;
  c.UpdateOutstandingData(DataSize::Bytes(100000000));
  SM_EXPECT_EQ(c.UpdateTargetBitrate(DataRate::BitsPerSec(80000), kNow).bps(), 80000);
}

SM_TEST(CongestionWindowPushback, DisabledDoesNothing) {
  CongestionWindowConfig cfg;
  cfg.enabled = false;
  CongestionWindowPushback c(cfg);
  c.OnFeedbackRtt(TimeDelta::Millis(50));
  c.UpdateWindow(DataRate::KilobitsPerSec(500));
  c.UpdateOutstandingData(DataSize::Bytes(100000000));
  SM_EXPECT_FALSE(c.data_window().has_value());
  SM_EXPECT_EQ(c.UpdateTargetBitrate(DataRate::BitsPerSec(80000), kNow).bps(), 80000);
}

SM_TEST(CongestionWindowPushback, RatioRecoversWhenTheWindowDrains) {
  CongestionWindowPushback c;
  c.SetDataWindow(DataSize::Bytes(50000));
  c.UpdateOutstandingData(DataSize::Bytes(100000));
  for (int i = 0; i < 10; ++i) c.UpdateTargetBitrate(DataRate::KilobitsPerSec(500), kNow);
  SM_EXPECT_LT(c.encoding_rate_ratio(), 0.4);
  SM_EXPECT_GT(c.reduce_ratio(), 0.6);
  // Between 0.1 and 1 of the window: grows by 5% a step.
  c.UpdateOutstandingData(DataSize::Bytes(30000));
  const double before = c.encoding_rate_ratio();
  c.UpdateTargetBitrate(DataRate::KilobitsPerSec(500), kNow);
  SM_EXPECT_NEAR(c.encoding_rate_ratio(), before * 1.05, 1e-9);
  // Under 10%: back to 1 at once.
  c.UpdateOutstandingData(DataSize::Bytes(4000));
  c.UpdateTargetBitrate(DataRate::KilobitsPerSec(500), kNow);
  SM_EXPECT_EQ(c.encoding_rate_ratio(), 1.0);
  SM_EXPECT_EQ(c.reduce_ratio(), 0.0);
}

SM_TEST(CongestionWindowPushback, WindowIsTargetTimesMinRttPlusQueueAndSmoothed) {
  CongestionWindowPushback c;  // 350 ms of queue.
  c.OnFeedbackRtt(TimeDelta::Millis(200));
  c.OnFeedbackRtt(TimeDelta::Millis(50));  // The smallest of the recent maxima counts.
  c.UpdateWindow(DataRate::KilobitsPerSec(800));
  // 800 kbps x (50 + 350) ms = 40000 bytes.
  SM_EXPECT_EQ(c.data_window()->bytes(), 40000);
  c.UpdateWindow(DataRate::KilobitsPerSec(400));  // New 20000, averaged with 40000.
  SM_EXPECT_EQ(c.data_window()->bytes(), 30000);
  // Tiny targets keep a two-packet window.
  CongestionWindowPushback d;
  d.OnFeedbackRtt(TimeDelta::Millis(10));
  d.UpdateWindow(DataRate::KilobitsPerSec(10));
  SM_EXPECT_EQ(d.data_window()->bytes(), 3000);
}

SM_TEST(CongestionWindowPushback, OnlyTheRecentRttsCount) {
  CongestionWindowConfig cfg;
  cfg.rtt_window = 3;
  CongestionWindowPushback c(cfg);
  c.OnFeedbackRtt(TimeDelta::Millis(20));  // Falls out of the window.
  for (int i = 0; i < 3; ++i) c.OnFeedbackRtt(TimeDelta::Millis(100));
  c.UpdateWindow(DataRate::KilobitsPerSec(800));
  SM_EXPECT_EQ(c.data_window()->bytes(), 800000 / 8 * 450 / 1000);
}

SM_TEST(CongestionWindowPushback, PacingQueueCountsOnlyWhenConfigured) {
  CongestionWindowPushback off;
  off.SetDataWindow(DataSize::Bytes(50000));
  off.UpdatePacingQueue(DataSize::Bytes(1000000));
  off.UpdateOutstandingData(DataSize::Bytes(1000));
  SM_EXPECT_EQ(off.UpdateTargetBitrate(DataRate::BitsPerSec(80000), kNow).bps(), 80000);
  CongestionWindowConfig cfg;
  cfg.add_pacing = true;
  CongestionWindowPushback on(cfg);
  on.SetDataWindow(DataSize::Bytes(50000));
  on.UpdatePacingQueue(DataSize::Bytes(1000000));
  on.UpdateOutstandingData(DataSize::Bytes(1000));
  SM_EXPECT_LT(on.UpdateTargetBitrate(DataRate::BitsPerSec(80000), kNow).bps(), 80000);
}

SM_TEST(CongestionWindowPushback, FrameDropIntervalFollowsTheRatio) {
  const DataRate min = DataRate::KilobitsPerSec(30);
  const DataRate t = DataRate::KilobitsPerSec(500);
  // No frame dropping under 1%.
  SM_EXPECT_EQ(CongestionWindowPushback::FrameDropInterval(t, 0.01, min).interval, 0);
  // 10% pushback: every 10th frame, and the encoder target loses a tenth.
  auto d = CongestionWindowPushback::FrameDropInterval(t, 0.1, min);
  SM_EXPECT_EQ(d.interval, 10);
  SM_EXPECT_EQ(d.encoder_target.bps(), 450000);
  // At most every second frame, however hard the push.
  d = CongestionWindowPushback::FrameDropInterval(t, 0.95, min);
  SM_EXPECT_EQ(d.interval, 2);
  SM_EXPECT_EQ(d.encoder_target.bps(), 250000);
  // A target at the codec minimum is never pushed down.
  SM_EXPECT_EQ(CongestionWindowPushback::FrameDropInterval(min, 0.5, min).interval, 0);
}

// Lost feedback must not leave packets "in flight" for ever (it would hold
// the window shut): a packet below the newest reported one is settled.
SM_TEST(CongestionWindowPushback, SendHistoryForgetsPacketsOfLostFeedback) {
  SendHistory h;
  for (uint16_t s = 0; s < 10; ++s) h.OnPacketSent(s, Timestamp::Millis(s), DataSize::Bytes(1000));
  SM_EXPECT_EQ(h.InFlight().bytes(), 10000);
  // Feedback for 0..2 arrives; the one for 3..6 is lost; 7..9 arrives.
  auto fb = [](uint16_t base, int n) {
    TransportFeedback f;
    f.base_seq = base;
    f.reference_time = 0;
    for (int i = 0; i < n; ++i) {
      TransportFeedback::Packet p;
      p.seq = static_cast<uint16_t>(base + i);
      p.received = true;
      p.delta_ticks = 40;
      f.packets.push_back(p);
    }
    return f;
  };
  h.OnFeedback(fb(0, 3), Timestamp::Millis(100));
  SM_EXPECT_EQ(h.InFlight().bytes(), 7000);
  h.OnFeedback(fb(7, 3), Timestamp::Millis(200));
  SM_EXPECT_EQ(h.InFlight().bytes(), 0);
}
