// Tests for #12: LossBasedBweV2 and SendSideBandwidthEstimation (`cc v2`), ported from
// libwebrtc's unit tests (M157, compare/webrtc/reference/modules/congestion_controller/goog_cc/
// loss_based_bwe_v2_test.cc, send_side_bandwidth_estimation_unittest.cc) with their numbers. The libwebrtc
// tests run on a field-trial config (Config() there); TestConfig() below is the same set of values.
#include <vector>

#include "sm/cc/loss_based_bwe_v2.h"
#include "sm/cc/send_side_bandwidth_estimation.h"
#include "sm/cc/send_side_bwe.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::cc;

namespace {

constexpr TimeDelta kObservationDurationLowerBound = TimeDelta::Millis(250);
constexpr int kPacketSize = 15'000;

// LossBasedBweV2Test::Config(enabled, valid) of libwebrtc's test.
LossBasedBweV2Config TestConfig() {
  LossBasedBweV2Config c;
  c.bandwidth_rampup_upper_bound_factor = 1.2;
  c.candidate_factors = {1.1, 1.0, 0.95};
  c.higher_bandwidth_bias_factor = 0.01;
  c.inherent_loss_lower_bound = 0.001;
  c.inherent_loss_upper_bound_bandwidth_balance = DataRate::KilobitsPerSec(14);
  c.inherent_loss_upper_bound_offset = 0.9;
  c.initial_inherent_loss_estimate = 0.01;
  c.newton_iterations = 2;
  c.newton_step_size = 0.4;
  c.observation_window_size = 15;
  c.sending_rate_smoothing_factor = 0.01;
  c.instant_upper_bound_temporal_weight_factor = 0.97;
  c.instant_upper_bound_bandwidth_balance = DataRate::KilobitsPerSec(90);
  c.instant_upper_bound_loss_offset = 0.1;
  c.temporal_weight_factor = 0.98;
  c.min_num_observations = 1;
  c.observation_duration_lower_bound = kObservationDurationLowerBound;
  c.max_increase_factor = 1.5;
  c.delayed_increase_window = TimeDelta::Millis(300);
  return c;
}

struct Feedback {
  int64_t seq = 0;
  PacketResult Make(Timestamp send, Timestamp recv, int size = kPacketSize) {
    PacketResult r;
    r.seq = seq++;
    r.send_time = send;
    r.receive_time = recv;
    r.size = DataSize::Bytes(size);
    return r;
  }
  std::vector<PacketResult> Received(Timestamp t0) {
    std::vector<PacketResult> v;
    v.push_back(Make(t0, t0 + kObservationDurationLowerBound));
    v.push_back(Make(t0 + kObservationDurationLowerBound, t0 + kObservationDurationLowerBound * 2));
    return v;
  }
  std::vector<PacketResult> Loss50p(Timestamp t0) {
    std::vector<PacketResult> v;
    v.push_back(Make(t0, t0 + kObservationDurationLowerBound));
    v.push_back(Make(t0 + kObservationDurationLowerBound, Timestamp::PlusInfinity()));
    return v;
  }
  std::vector<PacketResult> Loss100p(Timestamp t0, unsigned n = 2) {
    std::vector<PacketResult> v;
    for (unsigned i = 0; i + 1 < n; ++i) v.push_back(Make(t0 + TimeDelta::Millis(i * 10), Timestamp::PlusInfinity()));
    v.push_back(Make(t0 + kObservationDurationLowerBound, Timestamp::PlusInfinity()));
    return v;
  }
  std::vector<PacketResult> Loss10p(Timestamp t0, int lost_size = kPacketSize) {
    std::vector<PacketResult> v;
    for (int i = 0; i < 10; ++i) v.push_back(Make(t0 + kObservationDurationLowerBound * i, t0 + kObservationDurationLowerBound * (i + 1)));
    v[9].receive_time = Timestamp::PlusInfinity();
    v[9].size = DataSize::Bytes(lost_size);
    return v;
  }
};

DataRate Kbps(int64_t k) { return DataRate::KilobitsPerSec(k); }

}  // namespace

// --- LossBasedBweV2 -----------------------------------------------------------------------

SM_TEST(LossBweV2, BandwidthEstimateGivenInitializationAndThenFeedback) {
  Feedback fb;
  LossBasedBweV2 e(TestConfig());
  e.SetBandwidthEstimate(Kbps(600));
  e.UpdateBandwidthEstimate(fb.Received(Timestamp::Micros(0)), DataRate::PlusInfinity(), false);
  SM_EXPECT_TRUE(e.IsReady());
  SM_EXPECT_TRUE(e.GetLossBasedResult().bandwidth_estimate.IsFinite());
}

SM_TEST(LossBweV2, NoBandwidthEstimateGivenNoInitialization) {
  Feedback fb;
  LossBasedBweV2 e(TestConfig());
  e.UpdateBandwidthEstimate(fb.Received(Timestamp::Micros(0)), DataRate::PlusInfinity(), false);
  SM_EXPECT_FALSE(e.IsReady());
  SM_EXPECT_FALSE(e.GetLossBasedResult().bandwidth_estimate.IsFinite());
}

SM_TEST(LossBweV2, ReturnsTheDelayBasedEstimateUntilReady) {
  LossBasedBweV2 e(TestConfig());
  e.UpdateBandwidthEstimate({}, Kbps(100), false);
  SM_EXPECT_EQ(e.GetLossBasedResult().bandwidth_estimate.bps(), 100000);
  SM_EXPECT_TRUE(e.GetLossBasedResult().state == LossBasedState::kDelayBasedEstimate);
}

SM_TEST(LossBweV2, NoBandwidthEstimateGivenNotEnoughFeedback) {
  Feedback fb;
  std::vector<PacketResult> v;
  v.push_back(fb.Make(Timestamp::Micros(0), Timestamp::Micros(0) + kObservationDurationLowerBound / 2));
  v.push_back(fb.Make(Timestamp::Micros(0) + kObservationDurationLowerBound / 2, Timestamp::Micros(0) + kObservationDurationLowerBound));
  LossBasedBweV2 e(TestConfig());
  e.SetBandwidthEstimate(Kbps(600));
  SM_EXPECT_FALSE(e.IsReady());
  e.UpdateBandwidthEstimate(v, DataRate::PlusInfinity(), false);
  SM_EXPECT_FALSE(e.IsReady());
  SM_EXPECT_FALSE(e.GetLossBasedResult().bandwidth_estimate.IsFinite());
}

SM_TEST(LossBweV2, SetValueIsTheEstimateUntilAdditionalFeedbackHasBeenReceived) {
  Feedback fb;
  LossBasedBweV2 e(TestConfig());
  e.SetBandwidthEstimate(Kbps(600));
  e.UpdateBandwidthEstimate(fb.Received(Timestamp::Micros(0)), DataRate::PlusInfinity(), false);
  SM_EXPECT_TRUE(e.GetLossBasedResult().bandwidth_estimate != Kbps(600));
  e.SetBandwidthEstimate(Kbps(600));
  SM_EXPECT_TRUE(e.GetLossBasedResult().bandwidth_estimate == Kbps(600));
  e.UpdateBandwidthEstimate(fb.Received(Timestamp::Micros(0) + kObservationDurationLowerBound * 2), DataRate::PlusInfinity(), false);
  SM_EXPECT_TRUE(e.GetLossBasedResult().bandwidth_estimate != Kbps(600));
}

SM_TEST(LossBweV2, AcknowledgedBitrateOnlyAffectsTheEstimateWhenAdditionalFeedbackIsGiven) {
  Feedback fb1, fb2;
  LossBasedBweV2 e1(TestConfig()), e2(TestConfig());
  e1.SetBandwidthEstimate(Kbps(600));
  e2.SetBandwidthEstimate(Kbps(600));
  e1.UpdateBandwidthEstimate(fb1.Received(Timestamp::Micros(0)), DataRate::PlusInfinity(), false);
  e2.UpdateBandwidthEstimate(fb2.Received(Timestamp::Micros(0)), DataRate::PlusInfinity(), false);
  SM_EXPECT_EQ(e1.GetLossBasedResult().bandwidth_estimate.bps(), 660000);
  e1.SetAcknowledgedBitrate(Kbps(900));
  SM_EXPECT_EQ(e1.GetLossBasedResult().bandwidth_estimate.bps(), 660000);
  e1.UpdateBandwidthEstimate(fb1.Received(Timestamp::Micros(0) + kObservationDurationLowerBound * 2), DataRate::PlusInfinity(), false);
  e2.UpdateBandwidthEstimate(fb2.Received(Timestamp::Micros(0) + kObservationDurationLowerBound * 2), DataRate::PlusInfinity(), false);
  SM_EXPECT_TRUE(e1.GetLossBasedResult().bandwidth_estimate != e2.GetLossBasedResult().bandwidth_estimate);
}

SM_TEST(LossBweV2, BandwidthEstimateIsCappedToBeTcpFairGivenTooHighLossRate) {
  Feedback fb;
  LossBasedBweV2 e(TestConfig());
  e.SetBandwidthEstimate(Kbps(600));
  e.UpdateBandwidthEstimate(fb.Loss100p(Timestamp::Micros(0)), DataRate::PlusInfinity(), false);
  SM_EXPECT_EQ(e.GetLossBasedResult().bandwidth_estimate.bps(), 100000);
}

SM_TEST(LossBweV2, BandwidthEstimateCappedByTheDelayBasedEstimateWhenNetworkNormal) {
  Feedback fb;
  LossBasedBweV2 e(TestConfig());
  e.SetBandwidthEstimate(Kbps(600));
  e.UpdateBandwidthEstimate(fb.Received(Timestamp::Micros(0)), DataRate::PlusInfinity(), false);
  SM_EXPECT_TRUE(e.GetLossBasedResult().bandwidth_estimate > Kbps(600));
  e.UpdateBandwidthEstimate(fb.Received(Timestamp::Micros(0) + kObservationDurationLowerBound * 2), Kbps(500), false);
  SM_EXPECT_EQ(e.GetLossBasedResult().bandwidth_estimate.bps(), 500000);
}

SM_TEST(LossBweV2, UseAckedBitrateForEmergencyBackOff) {
  Feedback fb;
  LossBasedBweV2 e(TestConfig());
  e.SetBandwidthEstimate(Kbps(600));
  const DataRate acked = Kbps(300);
  e.SetAcknowledgedBitrate(acked);
  e.UpdateBandwidthEstimate(fb.Loss50p(Timestamp::Micros(0)), DataRate::PlusInfinity(), false);
  e.UpdateBandwidthEstimate(fb.Loss100p(Timestamp::Micros(0) + kObservationDurationLowerBound * 2), DataRate::PlusInfinity(), false);
  SM_EXPECT_TRUE(e.GetLossBasedResult().bandwidth_estimate <= acked);
}

SM_TEST(LossBweV2, NoChangeIfObservationDurationUnchanged) {
  Feedback fb;
  LossBasedBweV2 e(TestConfig());
  e.SetBandwidthEstimate(Kbps(600));
  e.SetAcknowledgedBitrate(Kbps(300));
  auto f = fb.Received(Timestamp::Micros(0));
  e.UpdateBandwidthEstimate(f, DataRate::PlusInfinity(), false);
  const DataRate e1 = e.GetLossBasedResult().bandwidth_estimate;
  // The same feedback again: no observation of at least 250 ms, so no change.
  e.UpdateBandwidthEstimate(f, DataRate::PlusInfinity(), false);
  SM_EXPECT_TRUE(e.GetLossBasedResult().bandwidth_estimate == e1);
}

SM_TEST(LossBweV2, NoChangeIfObservationDurationIsSmallAndNetworkNormal) {
  Feedback fb;
  LossBasedBweV2 e(TestConfig());
  e.SetBandwidthEstimate(Kbps(600));
  e.UpdateBandwidthEstimate(fb.Received(Timestamp::Micros(0)), DataRate::PlusInfinity(), false);
  const DataRate e1 = e.GetLossBasedResult().bandwidth_estimate;
  e.UpdateBandwidthEstimate(fb.Received(Timestamp::Micros(0) + kObservationDurationLowerBound - TimeDelta::Millis(1)), DataRate::PlusInfinity(), false);
  SM_EXPECT_TRUE(e.GetLossBasedResult().bandwidth_estimate == e1);
}

// A packet that one feedback reports as not received and a later one reports as received is not lost
// (PushBackObservation): only reports count, and only the reports that say "not received".
SM_TEST(LossBweV2, AReorderedPacketReportedLaterIsNotLost) {
  LossBasedBweV2Config cfg = TestConfig();
  cfg.observation_window_size = 2;
  cfg.min_num_observations = 1;
  Feedback fb;
  LossBasedBweV2 e(cfg);
  e.SetBandwidthEstimate(Kbps(600));
  // 10 packets over 100 ms: no observation yet (under 250 ms), packet 5 reported as lost.
  std::vector<PacketResult> a;
  for (int i = 0; i < 10; ++i) a.push_back(fb.Make(Timestamp::Millis(i * 10), Timestamp::Millis(i * 10 + 30), 1200));
  a[5].receive_time = Timestamp::PlusInfinity();
  e.UpdateBandwidthEstimate(a, DataRate::PlusInfinity(), false);
  SM_EXPECT_EQ(e.num_observations(), 0);
  // The next report has it as received, and closes the observation.
  std::vector<PacketResult> b;
  b.push_back(a[5]);
  b[0].receive_time = Timestamp::Millis(120);
  for (int i = 10; i < 30; ++i) b.push_back(fb.Make(Timestamp::Millis(i * 10), Timestamp::Millis(i * 10 + 30), 1200));
  e.UpdateBandwidthEstimate(b, DataRate::PlusInfinity(), false);
  SM_EXPECT_EQ(e.num_observations(), 1);
  SM_EXPECT_NEAR(e.average_reported_loss_ratio(), 0.0, 1e-9);
}

// The same packet counted as lost when no later report arrives: average loss is its share.
SM_TEST(LossBweV2, ALostPacketIsCountedWhenNoReportArrives) {
  LossBasedBweV2Config cfg = TestConfig();
  cfg.observation_window_size = 2;
  cfg.min_num_observations = 1;
  Feedback fb;
  LossBasedBweV2 e(cfg);
  e.SetBandwidthEstimate(Kbps(600));
  std::vector<PacketResult> a;
  for (int i = 0; i < 30; ++i) a.push_back(fb.Make(Timestamp::Millis(i * 10), Timestamp::Millis(i * 10 + 30), 1200));
  a[5].receive_time = Timestamp::PlusInfinity();
  e.UpdateBandwidthEstimate(a, DataRate::PlusInfinity(), false);
  SM_EXPECT_EQ(e.num_observations(), 1);
  SM_EXPECT_NEAR(e.average_reported_loss_ratio(), 1.0 / 30.0, 1e-9);
}

// The defaults are CreateConfig's.
SM_TEST(LossBweV2, DefaultsAreLibwebrtcs) {
  LossBasedBweV2 e;
  const auto& c = e.config();
  SM_EXPECT_EQ(c.observation_window_size, 15);
  SM_EXPECT_EQ(c.min_num_observations, 3);
  SM_EXPECT_EQ(c.candidate_factors.size(), 3u);
  SM_EXPECT_NEAR(c.candidate_factors[0], 1.02, 1e-12);
  SM_EXPECT_NEAR(c.candidate_factors[2], 0.95, 1e-12);
  SM_EXPECT_NEAR(c.instant_upper_bound_loss_offset, 0.05, 1e-12);
  SM_EXPECT_EQ(c.instant_upper_bound_bandwidth_balance.bps(), 100000);
  SM_EXPECT_EQ(c.padding_duration.ms(), 2000);
  SM_EXPECT_NEAR(c.hold_duration_factor, 2.0, 1e-12);
  SM_EXPECT_TRUE(c.use_byte_loss_rate);
}

// A clean link never makes the estimator limit the rate: the result stays a delay-based estimate or increases.
SM_TEST(LossBweV2, CleanFeedbackDoesNotLimitBelowTheDelayBasedEstimate) {
  LossBasedBweV2 e;
  e.SetMinMaxBitrate(Kbps(30), Kbps(10000));
  e.SetBandwidthEstimate(Kbps(1000));
  Feedback fb;
  Timestamp t = Timestamp::Micros(0);
  for (int round = 0; round < 40; ++round) {
    std::vector<PacketResult> v;
    for (int i = 0; i < 25; ++i) v.push_back(fb.Make(t + TimeDelta::Millis(i * 10), t + TimeDelta::Millis(i * 10 + 30), 1200));
    t += TimeDelta::Millis(250);
    e.UpdateBandwidthEstimate(v, Kbps(1200), false);
  }
  SM_EXPECT_TRUE(e.IsReady());
  SM_EXPECT_TRUE(e.GetLossBasedResult().bandwidth_estimate >= Kbps(1000));
  SM_EXPECT_NEAR(e.average_reported_loss_ratio(), 0.0, 1e-9);
}

// High sustained loss caps the estimate by the instant upper bound, 100 kbps / (loss - 5 %).
SM_TEST(LossBweV2, SustainedHighLossIsCappedByTheInstantUpperBound) {
  LossBasedBweV2 e;
  e.SetMinMaxBitrate(Kbps(30), Kbps(10000));
  e.SetBandwidthEstimate(Kbps(2000));
  Feedback fb;
  Timestamp t = Timestamp::Micros(0);
  for (int round = 0; round < 12; ++round) {
    std::vector<PacketResult> v;
    for (int i = 0; i < 25; ++i) v.push_back(fb.Make(t + TimeDelta::Millis(i * 10), t + TimeDelta::Millis(i * 10 + 30), 1200));
    for (int i = 0; i < 25; i += 4) v[i].receive_time = Timestamp::PlusInfinity();  // 28 % lost.
    t += TimeDelta::Millis(250);
    e.UpdateBandwidthEstimate(v, Kbps(3000), false);
  }
  // loss about 0.28: 100 kbps / (0.28 - 0.05) about 435 kbps.
  SM_EXPECT_TRUE(e.GetLossBasedResult().bandwidth_estimate < Kbps(600));
  SM_EXPECT_TRUE(e.GetLossBasedResult().state == LossBasedState::kDecreasing);
}

// --- SendSideBandwidthEstimation ----------------------------------------------------------

SM_TEST(SendSideBweV2, SetBitratesSetsTheTargetWithinTheLimits) {
  SendSideBandwidthEstimation bwe;
  bwe.SetBitrates(Kbps(300), Kbps(30), Kbps(5000), Timestamp::Seconds(1));
  SM_EXPECT_EQ(bwe.target_rate().bps(), 300000);
  bwe.SetMinMaxBitrate(Kbps(400), Kbps(5000));
  bwe.UpdateDelayBasedEstimate(Timestamp::Seconds(2), Kbps(10000));
  SM_EXPECT_EQ(bwe.target_rate().bps(), 400000);
}

SM_TEST(SendSideBweV2, ReceiverLimitCapsTheTargetRate) {
  SendSideBandwidthEstimation bwe;
  bwe.SetBitrates(Kbps(300), Kbps(30), Kbps(5000), Timestamp::Seconds(1));
  bwe.UpdateReceiverEstimate(Timestamp::Seconds(2), Kbps(200));
  SM_EXPECT_EQ(bwe.target_rate().bps(), 200000);
  bwe.UpdateReceiverEstimate(Timestamp::Seconds(3), DataRate::Zero());  // No limit.
  SM_EXPECT_EQ(bwe.target_rate().bps(), 300000);
}

// UpdateEstimate: in the first 2 s with no loss reported the delay-based estimate raises the target.
SM_TEST(SendSideBweV2, TrustsTheDelayBasedEstimateInTheStartPhase) {
  SendSideBandwidthEstimation bwe;
  bwe.SetBitrates(Kbps(300), Kbps(30), Kbps(5000), Timestamp::Seconds(1));
  bwe.UpdateDelayBasedEstimate(Timestamp::Seconds(1), Kbps(900));
  bwe.UpdateEstimate(Timestamp::Seconds(1));
  SM_EXPECT_EQ(bwe.target_rate().bps(), 900000);
}

// Before the loss-based estimator is ready: loss under 2 % raises the target by 8 % of the minimum of the
// last second plus 1 kbps; above 10 % it cuts it by 1 - loss / 2.
SM_TEST(SendSideBweV2, ReceiverReportLossRule) {
  SendSideBandwidthEstimation bwe;
  Timestamp t = Timestamp::Seconds(1);
  bwe.SetBitrates(Kbps(1000), Kbps(30), Kbps(5000), t);
  bwe.UpdatePacketsLost(0, 100, t);  // The first report, loss 0: 1000 x 1.08 + 1 kbps.
  SM_EXPECT_EQ(bwe.target_rate().bps(), 1081000);
  t += TimeDelta::Seconds(3);
  bwe.UpdatePacketsLost(0, 100, t);  // The history of the last second holds only 1081 kbps: again +8 % + 1 kbps.
  SM_EXPECT_EQ(bwe.target_rate().bps(), 1168480);
  t += TimeDelta::Seconds(1);
  bwe.UpdatePacketsLost(30, 100, t);  // 30 % -> fraction 76/256, x (512 - 76) / 512.
  SM_EXPECT_NEAR(static_cast<double>(bwe.target_rate().bps()), 1168480.0 * (512 - 76) / 512.0, 2);
}

// RttBasedBackoff: the target is cut x0.8 once a second while the propagation RTT is over 3 s.
SM_TEST(SendSideBweV2, RttBackoffCutsTheTargetWhenFeedbackStops) {
  SendSideBandwidthEstimation bwe;
  Timestamp t = Timestamp::Seconds(1);
  bwe.SetBitrates(Kbps(1000), Kbps(30), Kbps(5000), t);
  bwe.UpdatePropagationRtt(t, TimeDelta::Zero());
  bwe.OnSentPacket(t);
  bwe.UpdateEstimate(t);
  SM_EXPECT_FALSE(bwe.IsRttAboveLimit());
  // Packets keep being sent for 4 s with no feedback.
  for (int i = 0; i < 40; ++i) {
    t += TimeDelta::Millis(100);
    bwe.OnSentPacket(t);
  }
  SM_EXPECT_TRUE(bwe.IsRttAboveLimit());
  bwe.UpdateEstimate(t);
  SM_EXPECT_EQ(bwe.target_rate().bps(), 800000);
  bwe.UpdateEstimate(t + TimeDelta::Millis(500));
  SM_EXPECT_EQ(bwe.target_rate().bps(), 800000);
  bwe.UpdateEstimate(t + TimeDelta::Seconds(1));
  SM_EXPECT_EQ(bwe.target_rate().bps(), 640000);
}

// --- GccController (cc v2) ----------------------------------------------------------------

namespace {
// A sender and a link that delivers every packet with the given loss: feedback for 25 packets every 250 ms.
struct GccLoop {
  GccConfig cfg;
  GccController g;
  SendHistory unused;
  uint16_t seq = 0;
  Timestamp now = Timestamp::Seconds(1);
  explicit GccLoop(GccConfig c) : cfg(c), g(c) { g.Start(now); }
  // Sends 25 packets of 1200 bytes over 250 ms (a rate of 960 kbps) and returns their feedback;
  // every `lose_every`-th one is lost (0: none).
  void Round(int lose_every) {
    TransportFeedback fb;
    fb.base_seq = seq;
    const Timestamp start = now;
    for (int i = 0; i < 25; ++i) {
      const Timestamp send = start + TimeDelta::Millis(i * 10);
      g.OnPacketSent(seq, send, DataSize::Bytes(1200));
      TransportFeedback::Packet p;
      p.seq = seq;
      p.received = !(lose_every && i % lose_every == 0);
      p.delta_ticks = i == 0 ? 0 : 40;  // 10 ms
      fb.packets.push_back(p);
      ++seq;
    }
    fb.reference_time = static_cast<int32_t>(start.ms() / 64);
    now = start + TimeDelta::Millis(300);
    g.OnTransportFeedback(fb, now);
    g.OnProcessInterval(now, DataRate::KilobitsPerSec(960));
  }
};
}  // namespace

SM_TEST(GccV2Loss, LossThatTheFeedbackReportsLimitsTheTarget) {
  GccConfig cfg;
  cfg.version = 2;
  cfg.enable_probing = false;
  GccLoop clean(cfg), lossy(cfg);
  for (int r = 0; r < 40; ++r) {
    clean.Round(0);
    lossy.Round(3);  // 34 % reported lost
  }
  SM_EXPECT_TRUE(lossy.g.target() < clean.g.target());
  SM_EXPECT_TRUE(lossy.g.send_side_bwe().loss_based().average_reported_loss_ratio() > 0.2);
  SM_EXPECT_TRUE(clean.g.send_side_bwe().loss_based().average_reported_loss_ratio() < 1e-9);
}
