// Ports of libwebrtc's goog_cc/goog_cc_network_control_unittest.cc (M157,
// compare/webrtc/reference/) to GccController with `cc v2` (#166). Only the
// GoogCcNetworkControllerTest cases, which drive the controller directly, are
// ported; the GoogCcScenario cases need libwebrtc's network emulation. The cases
// left out, and why, are listed in tools/catalog.py (5.4).
#include <optional>

#include "sm/cc/send_side_bwe.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::cc;

namespace {

constexpr int64_t kInitialBitrateKbps = 60;
const DataRate kInitialBitrate = DataRate::KilobitsPerSec(kInitialBitrateKbps);
constexpr double kDefaultPacingRate = 2.5;
constexpr double kDefaultPacingRateSendSideBwe = 1.1;

// NetworkControllerTestFixture::InitialConfig: start 60 kbps, min 0, max 5 x start.
GccConfig InitialConfig(DataRate max_rate = kInitialBitrate * 5) {
  GccConfig cfg;
  cfg.start_rate = kInitialBitrate;
  cfg.min_rate = DataRate::Zero();
  cfg.max_rate = max_rate;
  return cfg;
}

// Transport feedback reporting one packet received at `arrival`.
TransportFeedback OnePacketFeedback(uint16_t seq, Timestamp arrival) {
  TransportFeedback fb;
  fb.base_seq = seq;
  fb.reference_time = static_cast<int32_t>(arrival.us() / 64000);
  TransportFeedback::Packet p;
  p.seq = seq;
  p.received = true;
  p.delta_ticks = (arrival.us() - int64_t{fb.reference_time} * 64000) / 250;
  fb.packets.push_back(p);
  return fb;
}

// PacketTransmissionAndFeedbackBlock: every 50 ms one 1000-byte packet, reported
// at its arrival, which is `delay` ms later than the previous one's; then a
// process interval. Returns the target after `runtime_ms`.
DataRate PacketTransmissionAndFeedbackBlock(GccController& g, int64_t runtime_ms, int64_t delay, Timestamp& current_time,
                                            uint16_t& seq) {
  int64_t delay_buildup = 0;
  const int64_t start_time_ms = current_time.ms();
  while (current_time.ms() - start_time_ms < runtime_ms) {
    const Timestamp arrival = current_time + TimeDelta::Millis(delay_buildup);
    delay_buildup += delay;
    g.OnPacketSent(seq, current_time, DataSize::Bytes(1000));
    g.OnTransportFeedback(OnePacketFeedback(seq, arrival), arrival);
    ++seq;
    current_time += TimeDelta::Millis(50);
    g.OnProcessInterval(current_time, DataRate::Zero());
  }
  return g.target();
}

}  // namespace

// Start() is OnNetworkAvailability(true) followed by the first process interval,
// which applies the initial constraints.
SM_TEST(GoogCcNetworkController, InitializeTargetRateOnFirstProcessIntervalAfterNetworkAvailable) {
  GccController g(InitialConfig());
  NetworkControlUpdate update = g.Start(Timestamp::Millis(123456));
  SM_EXPECT_TRUE(update.target_rate == kInitialBitrate);
  SM_EXPECT_TRUE(g.PacingRate() == kInitialBitrate * kDefaultPacingRate);
  SM_ASSERT_EQ(update.probes.size(), 2u);
  SM_EXPECT_TRUE(update.probes[0].rate == kInitialBitrate * 3);
  SM_EXPECT_TRUE(update.probes[1].rate == kInitialBitrate * 5);
}

SM_TEST(GoogCcNetworkController, ChangeDefaultPacingFactorAfterFirstFeedback) {
  GccController g(InitialConfig());
  g.Start(Timestamp::Millis(123456));
  SM_ASSERT_TRUE(g.PacingRate() == kInitialBitrate * kDefaultPacingRate);

  const Timestamp feedback_time = Timestamp::Millis(123457);
  g.OnPacketSent(0, feedback_time, DataSize::Bytes(100));
  g.OnTransportFeedback(OnePacketFeedback(0, feedback_time), feedback_time);
  SM_EXPECT_TRUE(g.PacingRate() >= kInitialBitrate * kDefaultPacingRateSendSideBwe);
  SM_EXPECT_TRUE(g.PacingRate() < (kInitialBitrate + DataRate::KilobitsPerSec(5)) * kDefaultPacingRateSendSideBwe);
}

// StreamsConfig::pacing_factor replaces the defaults, also after the first
// feedback (#187: SetPacingFactor is OnStreamsConfig's pacing_factor).
SM_TEST(GoogCcNetworkController, RespectsConfiguredPacingFactor) {
  GccController g(InitialConfig());
  g.Start(Timestamp::Millis(123456));
  SM_ASSERT_TRUE(g.PacingRate() == kInitialBitrate * kDefaultPacingRate);

  g.SetPacingFactor(10);

  const Timestamp feedback_time = Timestamp::Millis(123457);
  g.OnPacketSent(0, feedback_time, DataSize::Bytes(100));
  g.OnTransportFeedback(OnePacketFeedback(0, feedback_time), feedback_time);
  SM_EXPECT_TRUE(g.PacingRate() >= kInitialBitrate * 10);
  SM_EXPECT_TRUE(g.PacingRate() < (kInitialBitrate + DataRate::KilobitsPerSec(5)) * 10);
}

SM_TEST(GoogCcNetworkController, ReactsToChangedNetworkConditionsFromRemote) {
  GccController g(InitialConfig());
  Timestamp current_time = Timestamp::Millis(123);
  g.Start(current_time);
  g.OnProcessInterval(current_time, DataRate::Zero());
  g.OnRemb(kInitialBitrate * 2);

  current_time += TimeDelta::Millis(25);
  NetworkControlUpdate update = g.OnProcessInterval(current_time, DataRate::Zero());
  SM_EXPECT_TRUE(update.target_rate == kInitialBitrate * 2);
  SM_EXPECT_TRUE(g.PacingRate() == kInitialBitrate * 2 * kDefaultPacingRate);

  g.OnRemb(kInitialBitrate);
  current_time += TimeDelta::Millis(25);
  update = g.OnProcessInterval(current_time, DataRate::Zero());
  SM_EXPECT_TRUE(update.target_rate == kInitialBitrate);
  SM_EXPECT_TRUE(g.PacingRate() == kInitialBitrate * kDefaultPacingRate);
}

// The first half of libwebrtc's case. The second half (a route change without a
// starting rate falls to the 5 kbps floor) has no counterpart: ResetEstimate
// always takes a rate (catalog 5.4).
SM_TEST(GoogCcNetworkController, OnNetworkRouteChanged) {
  GccController g(InitialConfig());
  const Timestamp current_time = Timestamp::Millis(123);
  g.Start(current_time);
  const DataRate new_bitrate = DataRate::BitsPerSec(200000);

  NetworkControlUpdate update = g.ResetEstimate(new_bitrate, current_time);
  SM_EXPECT_TRUE(update.target_rate == new_bitrate);
  SM_EXPECT_TRUE(g.PacingRate() == new_bitrate * kDefaultPacingRate);
  SM_EXPECT_EQ(update.probes.size(), 2u);
}

// libwebrtc's route change also brings new constraints (max 20 x the initial
// rate); ResetEstimate keeps the configured ones, so the controller is created
// with that maximum.
SM_TEST(GoogCcNetworkController, ProbeOnRouteChange) {
  GccController g(InitialConfig(kInitialBitrate * 20));
  Timestamp current_time = Timestamp::Millis(123);
  g.Start(current_time);
  current_time += TimeDelta::Seconds(3);

  NetworkControlUpdate update = g.ResetEstimate(kInitialBitrate * 2, current_time);
  SM_EXPECT_TRUE(update.target_rate == kInitialBitrate * 2);
  SM_ASSERT_EQ(update.probes.size(), 2u);
  SM_EXPECT_TRUE(update.probes[0].rate == kInitialBitrate * 6);
  SM_EXPECT_TRUE(update.probes[1].rate == kInitialBitrate * 12);

  g.OnProcessInterval(current_time, DataRate::Zero());
}

// Feedback that shows an increasing delay lowers the estimate.
SM_TEST(GoogCcNetworkController, UpdatesDelayBasedEstimate) {
  GccController g(InitialConfig());
  const int64_t kRunTimeMs = 6000;
  Timestamp current_time = Timestamp::Millis(123);
  uint16_t seq = 0;
  g.Start(current_time);

  // First with no bandwidth limit, so no delay builds up.
  const DataRate target_bitrate_before_delay = PacketTransmissionAndFeedbackBlock(g, kRunTimeMs, 0, current_time, seq);
  // Then with a growing delay: the estimate is adjusted downwards.
  const DataRate target_bitrate_after_delay = PacketTransmissionAndFeedbackBlock(g, kRunTimeMs, 50, current_time, seq);
  SM_EXPECT_TRUE(target_bitrate_after_delay < target_bitrate_before_delay);
}

// libwebrtc checks TargetTransferRate::is_bandwidth_limited, which only its
// RFC 8888 feedback generator reads. GccController computes the same flag from
// the AlrDetector (MaybeTriggerOnNetworkChanged), so the port checks the
// detector: one packet after 10 s of silence starts ALR, and a burst of 50
// 1000-byte packets in 50 ms ends it.
SM_TEST(GoogCcNetworkController, TriggersTargetRateUpdateWhenBandwidthLimitedChanges) {
  GccController g(InitialConfig());
  Timestamp current_time = Timestamp::Millis(123456);
  uint16_t seq = 0;
  g.Start(current_time);
  g.OnProcessInterval(current_time, DataRate::Zero());
  SM_EXPECT_FALSE(g.alr_detector().GetApplicationLimitedRegionStartTime().has_value());

  g.OnPacketSent(seq++, current_time, DataSize::Bytes(1000));
  current_time += TimeDelta::Seconds(10);
  g.OnPacketSent(seq++, current_time, DataSize::Bytes(1));
  g.OnProcessInterval(current_time, DataRate::Zero());
  SM_EXPECT_TRUE(g.alr_detector().GetApplicationLimitedRegionStartTime().has_value());

  for (int i = 0; i < 50; ++i) {
    current_time += TimeDelta::Millis(1);
    g.OnPacketSent(seq++, current_time, DataSize::Bytes(1000));
  }
  g.OnProcessInterval(current_time, DataRate::Zero());
  SM_EXPECT_FALSE(g.alr_detector().GetApplicationLimitedRegionStartTime().has_value());
}

// GoogCcScenario.NoCrashOnVeryLateFeedback at the controller level: feedback for
// packets sent 300 s earlier, after a 300 s pause, is handled without a crash
// and the controller keeps a finite target.
SM_TEST(GoogCcNetworkController, NoCrashOnVeryLateFeedback) {
  GccController g(InitialConfig());
  Timestamp current_time = Timestamp::Millis(123);
  uint16_t seq = 0;
  g.Start(current_time);
  PacketTransmissionAndFeedbackBlock(g, 5000, 0, current_time, seq);
  const uint16_t late_seq = seq;
  const Timestamp late_send = current_time;
  for (int i = 0; i < 10; ++i) g.OnPacketSent(seq++, late_send + TimeDelta::Millis(i), DataSize::Bytes(1000));
  for (int i = 0; i < 299 * 40; ++i) {
    current_time += TimeDelta::Millis(25);
    g.OnProcessInterval(current_time, DataRate::Zero());
  }
  TransportFeedback fb = OnePacketFeedback(late_seq, late_send + TimeDelta::Millis(50));
  for (uint16_t i = 1; i < 10; ++i) {
    TransportFeedback::Packet p;
    p.seq = static_cast<uint16_t>(late_seq + i);
    p.received = true;
    p.delta_ticks = 4;
    fb.packets.push_back(p);
  }
  g.OnTransportFeedback(fb, current_time);
  PacketTransmissionAndFeedbackBlock(g, 2000, 0, current_time, seq);
  SM_EXPECT_TRUE(g.target().IsFinite());
  SM_EXPECT_TRUE(g.target() > DataRate::Zero());
}
