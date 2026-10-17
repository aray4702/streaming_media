// Tests for #48: BitrateProber, AlrDetector, ProbeControllerV2 and the libwebrtc
// mode of ProbeBitrateEstimator, ported from libwebrtc's unit tests (M157,
// compare/webrtc/reference/modules/pacing/bitrate_prober_unittest.cc,
// goog_cc/alr_detector_unittest.cc, goog_cc/probe_controller_unittest.cc,
// goog_cc/probe_bitrate_estimator_unittest.cc), with their numbers.
#include <algorithm>

#include "sm/cc/alr_detector.h"
#include "sm/cc/bitrate_prober.h"
#include "sm/cc/pacing_controller.h"
#include "sm/cc/probing.h"
#include "sm/cc/send_side_bwe.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::cc;

namespace {

ProbeCluster Cluster(int id, DataRate rate, Timestamp at, TimeDelta min_delta = TimeDelta::Millis(2), TimeDelta duration = TimeDelta::Millis(15),
                     int probes = 5) {
  ProbeCluster c;
  c.id = id;
  c.rate = rate;
  c.at_time = at;
  c.min_probe_delta = min_delta;
  c.min_probes = probes;
  c.min_bytes = rate * duration;
  return c;
}

}  // namespace

// --- BitrateProber --------------------------------------------------------------

SM_TEST(BitrateProber, VerifyStatesAndTimeBetweenProbes) {
  BitrateProber prober;
  SM_EXPECT_TRUE(!prober.is_probing());
  Timestamp now = Timestamp::Micros(0);
  const Timestamp start_time = now;
  SM_EXPECT_TRUE(prober.NextProbeTime(now) == Timestamp::PlusInfinity());

  const DataRate kTestBitrate1 = DataRate::KilobitsPerSec(900);
  const DataRate kTestBitrate2 = DataRate::KilobitsPerSec(1800);
  const int kClusterSize = 5;
  const DataSize kProbeSize = DataSize::Bytes(1000);
  const TimeDelta kMinProbeDuration = TimeDelta::Millis(15);

  prober.CreateProbeCluster(Cluster(0, kTestBitrate1, now));
  prober.CreateProbeCluster(Cluster(1, kTestBitrate2, now));
  SM_EXPECT_TRUE(!prober.is_probing());

  prober.OnIncomingPacket(kProbeSize);
  SM_EXPECT_TRUE(prober.is_probing());
  SM_EXPECT_EQ(prober.CurrentCluster(now)->cluster_id, 0);

  // The first packet probes as soon as possible.
  SM_EXPECT_TRUE(prober.NextProbeTime(now) == Timestamp::MinusInfinity());

  for (int i = 0; i < kClusterSize; ++i) {
    now = std::max(now, prober.NextProbeTime(now));
    SM_EXPECT_EQ(prober.CurrentCluster(now)->cluster_id, 0);
    prober.ProbeSent(now, kProbeSize);
  }
  SM_EXPECT_TRUE(now - start_time >= kMinProbeDuration);
  // The actual bitrate is within 10% of the target.
  DataRate bitrate = DataRate::FromSizeAndTime(DataSize::Bytes(kProbeSize.bytes() * (kClusterSize - 1)), now - start_time);
  SM_EXPECT_TRUE(bitrate > kTestBitrate1 * 0.9);
  SM_EXPECT_TRUE(bitrate < kTestBitrate1 * 1.1);

  now = std::max(now, prober.NextProbeTime(now));
  const Timestamp probe2_started = now;
  for (int i = 0; i < kClusterSize; ++i) {
    now = std::max(now, prober.NextProbeTime(now));
    SM_EXPECT_EQ(prober.CurrentCluster(now)->cluster_id, 1);
    prober.ProbeSent(now, kProbeSize);
  }
  const TimeDelta duration = now - probe2_started;
  SM_EXPECT_TRUE(duration >= kMinProbeDuration);
  bitrate = DataRate::FromSizeAndTime(DataSize::Bytes(kProbeSize.bytes() * (kClusterSize - 1)), duration);
  SM_EXPECT_TRUE(bitrate > kTestBitrate2 * 0.9);
  SM_EXPECT_TRUE(bitrate < kTestBitrate2 * 1.1);

  SM_EXPECT_TRUE(prober.NextProbeTime(now) == Timestamp::PlusInfinity());
  SM_EXPECT_TRUE(!prober.is_probing());
}

SM_TEST(BitrateProber, DiscardsDelayedProbes) {
  BitrateProberConfig cfg;
  cfg.max_probe_delay = TimeDelta::Millis(3);
  BitrateProber prober(cfg);
  const DataSize kProbeSize = DataSize::Bytes(1000);
  Timestamp now = Timestamp::Micros(0);
  prober.CreateProbeCluster(Cluster(0, DataRate::KilobitsPerSec(900), now));
  prober.OnIncomingPacket(kProbeSize);
  SM_EXPECT_TRUE(prober.is_probing());
  SM_EXPECT_EQ(prober.CurrentCluster(now)->cluster_id, 0);
  now = std::max(now, prober.NextProbeTime(now));
  prober.ProbeSent(now, kProbeSize);

  // Advance 1 ms past the timeout for the next probe.
  const Timestamp next_probe_time = prober.NextProbeTime(now);
  SM_EXPECT_TRUE(next_probe_time > now);
  now += (next_probe_time - now) + cfg.max_probe_delay + TimeDelta::Millis(1);
  // Still indicates the time we wanted to probe at.
  SM_EXPECT_TRUE(prober.NextProbeTime(now) == next_probe_time);
  // The first and only cluster is removed due to the timeout.
  SM_EXPECT_TRUE(!prober.CurrentCluster(now).has_value());
  SM_EXPECT_EQ(prober.discarded(), 1u);
}

SM_TEST(BitrateProber, LimitsNumberOfPendingProbeClusters) {
  BitrateProber prober;
  const DataSize kProbeSize = DataSize::Bytes(1000);
  Timestamp now = Timestamp::Micros(0);
  prober.CreateProbeCluster(Cluster(0, DataRate::KilobitsPerSec(900), now));
  prober.OnIncomingPacket(kProbeSize);
  SM_ASSERT_TRUE(prober.is_probing());
  SM_ASSERT_EQ(prober.CurrentCluster(now)->cluster_id, 0);
  for (int i = 1; i < 11; ++i) {
    prober.CreateProbeCluster(Cluster(i, DataRate::KilobitsPerSec(900), now));
    prober.OnIncomingPacket(kProbeSize);
  }
  // Some clusters have been dropped.
  SM_EXPECT_TRUE(prober.is_probing());
  SM_EXPECT_GE(prober.CurrentCluster(now)->cluster_id, 5);
  const Timestamp max_expected_probe_time = now + TimeDelta::Seconds(1);
  while (prober.is_probing() && now < max_expected_probe_time) {
    now = std::max(now, prober.NextProbeTime(now));
    prober.ProbeSent(now, kProbeSize);
  }
  SM_EXPECT_TRUE(!prober.is_probing());
}

SM_TEST(BitrateProber, DoesntInitializeProbingForSmallPackets) {
  BitrateProber prober;
  prober.CreateProbeCluster(Cluster(0, DataRate::KilobitsPerSec(1000), Timestamp::Micros(0)));
  prober.OnIncomingPacket(DataSize::Bytes(100));
  SM_EXPECT_TRUE(!prober.is_probing());
}

SM_TEST(BitrateProber, VerifyProbeSizeOnHighBitrate) {
  BitrateProber prober;
  const DataRate kHighBitrate = DataRate::KilobitsPerSec(10000);
  prober.CreateProbeCluster(Cluster(0, kHighBitrate, Timestamp::Micros(0)));
  // The probe size ensures a minimum of 1 ms interval.
  SM_EXPECT_TRUE(prober.RecommendedMinProbeSize() > kHighBitrate * TimeDelta::Millis(1));
}

SM_TEST(BitrateProber, ProbeSizeCanBeSetInProbeClusterConfig) {
  BitrateProber prober;
  const DataRate kHighBitrate = DataRate::KilobitsPerSec(10000);
  prober.CreateProbeCluster(Cluster(0, kHighBitrate, Timestamp::Micros(0), TimeDelta::Millis(20)));
  SM_EXPECT_TRUE(prober.RecommendedMinProbeSize() == kHighBitrate * TimeDelta::Millis(20));
  prober.OnIncomingPacket(DataSize::Bytes(1000));
  // The next probe is due `min_probe_delta` later if the recommended number of bytes has been sent.
  prober.ProbeSent(Timestamp::Micros(0), prober.RecommendedMinProbeSize());
  SM_EXPECT_TRUE(prober.NextProbeTime(Timestamp::Micros(0)) == Timestamp::Micros(0) + TimeDelta::Millis(20));
}

SM_TEST(BitrateProber, MinimumNumberOfProbingPackets) {
  BitrateProber prober;
  // Even when probing at a low bitrate a minimum number of packets is sent.
  const DataSize kPacketSize = DataSize::Bytes(1000);
  const Timestamp now = Timestamp::Micros(0);
  prober.CreateProbeCluster(Cluster(0, DataRate::KilobitsPerSec(100), now));
  prober.OnIncomingPacket(kPacketSize);
  for (int i = 0; i < 5; ++i) {
    SM_EXPECT_TRUE(prober.is_probing());
    prober.ProbeSent(now, kPacketSize);
  }
  SM_EXPECT_TRUE(!prober.is_probing());
}

SM_TEST(BitrateProber, ScaleBytesUsedForProbing) {
  BitrateProber prober;
  const DataRate kBitrate = DataRate::KilobitsPerSec(10000);
  const DataSize kPacketSize = DataSize::Bytes(1000);
  const DataSize kExpectedDataSent = kBitrate * TimeDelta::Millis(15);
  const Timestamp now = Timestamp::Micros(0);
  prober.CreateProbeCluster(Cluster(0, kBitrate, now));
  prober.OnIncomingPacket(kPacketSize);
  DataSize data_sent;
  while (data_sent < kExpectedDataSent) {
    SM_ASSERT_TRUE(prober.is_probing());
    prober.ProbeSent(now, kPacketSize);
    data_sent += kPacketSize;
  }
  SM_EXPECT_TRUE(!prober.is_probing());
}

SM_TEST(BitrateProber, ProbeClusterTimeout) {
  BitrateProber prober;
  const DataRate kBitrate = DataRate::KilobitsPerSec(300);
  const DataSize kSmallPacketSize = DataSize::Bytes(20);
  // Two probe clusters of 5 packets each are expected.
  const DataSize kExpectedDataSent = DataSize::Bytes(kSmallPacketSize.bytes() * 2 * 5);
  Timestamp now = Timestamp::Micros(0);
  prober.CreateProbeCluster(Cluster(0, kBitrate, now));
  prober.OnIncomingPacket(kSmallPacketSize);
  SM_EXPECT_TRUE(!prober.is_probing());
  now += TimeDelta::Millis(5000);
  prober.CreateProbeCluster(Cluster(1, kBitrate * 0.1, now));
  prober.OnIncomingPacket(kSmallPacketSize);
  SM_EXPECT_TRUE(!prober.is_probing());
  now += TimeDelta::Millis(1);
  prober.CreateProbeCluster(Cluster(2, kBitrate * 0.1, now));
  prober.OnIncomingPacket(kSmallPacketSize);
  SM_EXPECT_TRUE(prober.is_probing());
  DataSize data_sent;
  while (data_sent < kExpectedDataSent) {
    SM_ASSERT_TRUE(prober.is_probing());
    prober.ProbeSent(now, kSmallPacketSize);
    data_sent += kSmallPacketSize;
  }
  SM_EXPECT_TRUE(!prober.is_probing());
}

// --- PacingController probing -----------------------------------------------------

namespace {
PacedPacket Media(uint64_t id, int64_t bytes, Timestamp t) {
  PacedPacket p;
  p.id = id;
  p.priority = PacketPriority::kVideo;
  p.size = DataSize::Bytes(bytes);
  p.enqueue_time = t;
  return p;
}
}  // namespace

// The pacer asks the prober: a probe starts on a media packet of a useful size,
// begins with a small padding packet, and each burst carries the cluster rate x
// min_probe_delta bytes (bitrate_prober_unittest, pacing_controller_unittest).
SM_TEST(PacingControllerProbing, ClusterWaitsForMediaThenTagsAndPads) {
  PacingController p;
  p.SetPacingRates(DataRate::KilobitsPerSec(300), DataRate::Zero());
  p.AddProbeCluster(Cluster(7, DataRate::KilobitsPerSec(2000), Timestamp::Micros(0)));
  // No media packet has arrived: nothing probes (allow_start_probing_immediately is off).
  int tagged = 0;
  for (int t = 0; t < 20; ++t) {
    for (auto& s : p.Process(Timestamp::Millis(t))) tagged += s.probe_cluster == 7;
  }
  SM_EXPECT_EQ(tagged, 0);

  // A tiny packet does not start it either; a full-size one does.
  p.Enqueue(Media(1, 100, Timestamp::Millis(20)));
  tagged = 0;
  int padding = 0;
  bool first = true;
  int64_t first_size = 0;
  for (int t = 20; t < 80; ++t) {
    for (auto& s : p.Process(Timestamp::Millis(t))) {
      if (s.probe_cluster == 7) {
        ++tagged;
        if (first) {
          first = false;
          first_size = s.size.bytes();
        }
      }
      padding += s.is_padding;
    }
  }
  SM_EXPECT_EQ(tagged, 0);
  p.Enqueue(Media(2, 1200, Timestamp::Millis(80)));
  int64_t cluster_bytes = 0;
  for (int t = 80; t < 160; ++t) {
    for (auto& s : p.Process(Timestamp::Millis(t))) {
      if (s.probe_cluster == 7) {
        ++tagged;
        cluster_bytes += s.size.bytes();
        if (first) {
          first = false;
          first_size = s.size.bytes();
        }
      }
      padding += s.is_padding;
    }
  }
  SM_EXPECT_GE(tagged, 5);
  SM_EXPECT_GE(cluster_bytes, 3750);  // 2 Mbps x 15 ms.
  // The cluster starts with the small padding packet, and padding fills the bursts.
  SM_EXPECT_EQ(first_size, PacingControllerConfig{}.probe_start_padding.bytes());
  SM_EXPECT_GE(padding, 2);
}

SM_TEST(PacingControllerProbing, BurstsAreTimedByTheClusterRate) {
  PacingController p;
  p.SetPacingRates(DataRate::KilobitsPerSec(300), DataRate::Zero());
  const DataRate rate = DataRate::KilobitsPerSec(1800);
  p.AddProbeCluster(Cluster(1, rate, Timestamp::Micros(0)));
  p.Enqueue(Media(1, 1200, Timestamp::Millis(0)));
  Timestamp first = Timestamp::PlusInfinity(), last = Timestamp::MinusInfinity();
  int64_t bytes = 0, last_size = 0;
  for (int us = 0; us < 100000; us += 250) {
    const Timestamp t = Timestamp::Micros(us);
    for (auto& s : p.Process(t)) {
      if (s.probe_cluster != 1) continue;
      if (t < first) first = t;
      last = t;
      bytes += s.size.bytes();
      last_size = s.size.bytes();
    }
  }
  SM_ASSERT_TRUE(first.IsFinite());
  // The bytes sent before the last burst were paced at the cluster rate (what
  // ProbeBitrateEstimator measures: it leaves out the last packet).
  const DataRate measured = DataRate::FromSizeAndTime(DataSize::Bytes(bytes - last_size), last - first);
  SM_EXPECT_TRUE(measured > rate * 0.8);
  SM_EXPECT_TRUE(measured < rate * 1.25);
  SM_EXPECT_TRUE(last - first >= TimeDelta::Millis(10));
}

SM_TEST(PacingControllerProbing, DelayedProbeIsDiscarded) {
  PacingController p;
  p.SetPacingRates(DataRate::KilobitsPerSec(300), DataRate::Zero());
  p.AddProbeCluster(Cluster(1, DataRate::KilobitsPerSec(1800), Timestamp::Micros(0)));
  p.Enqueue(Media(1, 1200, Timestamp::Millis(0)));
  int tagged = 0;
  // Process late: the first burst goes out, the next is due long before the pacer runs again.
  for (auto& s : p.Process(Timestamp::Millis(0))) tagged += s.probe_cluster == 1;
  SM_EXPECT_GE(tagged, 1);
  for (auto& s : p.Process(Timestamp::Millis(500))) tagged += s.probe_cluster == 1;
  const int after_late = tagged;
  for (int t = 501; t < 700; ++t) {
    for (auto& s : p.Process(Timestamp::Millis(t))) tagged += s.probe_cluster == 1;
  }
  SM_EXPECT_EQ(tagged, after_late);  // The late cluster was dropped, not resumed.
}

// --- AlrDetector ----------------------------------------------------------------------

namespace {
constexpr DataRate kEstimatedBitrate = DataRate::BitsPerSec(300000);

struct AlrTraffic {
  AlrDetector& d;
  Timestamp now;
  void Send(TimeDelta interval, int usage_percentage) {
    const TimeDelta kTimeStep = TimeDelta::Millis(10);
    for (TimeDelta t = TimeDelta::Zero(); t < interval; t += kTimeStep) {
      now += kTimeStep;
      d.OnBytesSent(DataSize::Bytes(kEstimatedBitrate.bps() * usage_percentage / 100 * kTimeStep.us() / 8000000), now);
    }
  }
};
}  // namespace

SM_TEST(AlrDetector, AlrDetection) {
  AlrDetector d;
  d.SetEstimatedBitrate(kEstimatedBitrate);
  AlrTraffic t{d, Timestamp::Seconds(1)};
  SM_EXPECT_TRUE(!d.GetApplicationLimitedRegionStartTime());
  // Stays in non-ALR when usage is close to 100%.
  t.Send(TimeDelta::Seconds(1), 90);
  SM_EXPECT_TRUE(!d.GetApplicationLimitedRegionStartTime());
  // ALR starts when the usage drops to 20%.
  t.Send(TimeDelta::Millis(1500), 20);
  SM_EXPECT_TRUE(d.GetApplicationLimitedRegionStartTime().has_value());
  // ALR ends when the usage is above 65%.
  t.Send(TimeDelta::Seconds(4), 100);
  SM_EXPECT_TRUE(!d.GetApplicationLimitedRegionStartTime());
}

SM_TEST(AlrDetector, ShortSpike) {
  AlrDetector d;
  d.SetEstimatedBitrate(kEstimatedBitrate);
  AlrTraffic t{d, Timestamp::Seconds(1)};
  t.Send(TimeDelta::Seconds(1), 20);
  SM_EXPECT_TRUE(d.GetApplicationLimitedRegionStartTime().has_value());
  // A short bitrate spike does not end it.
  t.Send(TimeDelta::Millis(100), 150);
  SM_EXPECT_TRUE(d.GetApplicationLimitedRegionStartTime().has_value());
  t.Send(TimeDelta::Seconds(3), 100);
  SM_EXPECT_TRUE(!d.GetApplicationLimitedRegionStartTime());
}

SM_TEST(AlrDetector, BandwidthEstimateChanges) {
  AlrDetector d;
  d.SetEstimatedBitrate(kEstimatedBitrate);
  AlrTraffic t{d, Timestamp::Seconds(1)};
  t.Send(TimeDelta::Seconds(1), 20);
  SM_EXPECT_TRUE(d.GetApplicationLimitedRegionStartTime().has_value());
  // When the estimate drops the detector stays in ALR and leaves it shortly
  // afterwards, so the probe controller can still react to the drop.
  d.SetEstimatedBitrate(DataRate::BitsPerSec(kEstimatedBitrate.bps() / 5));
  SM_EXPECT_TRUE(d.GetApplicationLimitedRegionStartTime().has_value());
  t.Send(TimeDelta::Seconds(1), 50);
  SM_EXPECT_TRUE(!d.GetApplicationLimitedRegionStartTime());
}

SM_TEST(AlrDetector, IntervalBudget) {
  // interval_budget_unittest.cc: the budget refills with time, and unused budget
  // is not carried over unless the budget can build up underuse.
  IntervalBudget b(100);  // 100 kbps -> 6250 bytes in the 500 ms window.
  b.IncreaseBudget(100);
  SM_EXPECT_EQ(b.bytes_remaining(), 1250u);
  b.IncreaseBudget(100);
  SM_EXPECT_EQ(b.bytes_remaining(), 1250u);  // Underuse is lost.
  b.UseBudget(2000);
  SM_EXPECT_EQ(b.bytes_remaining(), 0u);
  b.IncreaseBudget(100);  // Overuse is compensated: -750 + 1250.
  SM_EXPECT_EQ(b.bytes_remaining(), 500u);
  IntervalBudget c(100, true);
  c.IncreaseBudget(100);
  c.IncreaseBudget(100);
  SM_EXPECT_EQ(c.bytes_remaining(), 2500u);
}

// --- ProbeControllerV2 ----------------------------------------------------------------

namespace {
constexpr DataRate kMinBitrate = DataRate::BitsPerSec(100);
constexpr DataRate kStartBitrate = DataRate::BitsPerSec(300);
constexpr DataRate kMaxBitrate = DataRate::BitsPerSec(10000);
constexpr BandwidthLimitedCause kDelay = BandwidthLimitedCause::kDelayBasedLimited;
constexpr TimeDelta kExponentialProbingTimeout = TimeDelta::Seconds(5);
constexpr TimeDelta kAlrProbeInterval = TimeDelta::Seconds(5);
constexpr TimeDelta kAlrEndedTimeout = TimeDelta::Seconds(3);
constexpr TimeDelta kBitrateDropTimeout = TimeDelta::Seconds(5);
const Timestamp kT0 = Timestamp::Seconds(100);

DataRate Bps(int64_t b) { return DataRate::BitsPerSec(b); }

ProbeControllerV2 Started(std::vector<ProbeCluster>* probes = nullptr) {
  ProbeControllerV2 c;
  c.OnNetworkAvailability(true, kT0);
  auto p = c.SetBitrates(kMinBitrate, kStartBitrate, kMaxBitrate, kT0);
  if (probes) *probes = p;
  return c;
}
}  // namespace

SM_TEST(ProbeControllerV2, InitiatesProbingAfterSetBitrates) {
  ProbeControllerV2 c;
  SM_ASSERT_TRUE(c.OnNetworkAvailability(true, kT0).empty());
  auto probes = c.SetBitrates(kMinBitrate, kStartBitrate, kMaxBitrate, kT0);
  SM_ASSERT_EQ(probes.size(), 2u);
  // 3x and 6x the start rate, 15 ms and 5 packets each.
  SM_EXPECT_EQ(probes[0].rate.bps(), 900);
  SM_EXPECT_EQ(probes[1].rate.bps(), 1800);
  SM_EXPECT_EQ(probes[0].min_probes, 5);
  SM_EXPECT_TRUE(probes[0].min_probe_delta == TimeDelta::Millis(2));
  SM_EXPECT_EQ(probes[0].min_bytes.bytes(), 2);  // 900 bps x 15 ms = 1.69 bytes, rounded as libwebrtc's unit types do.
}

SM_TEST(ProbeControllerV2, InitiatesProbingWhenNetworkAvailable) {
  ProbeControllerV2 c;
  auto probes = c.SetBitrates(kMinBitrate, kStartBitrate, kMaxBitrate, kT0);
  SM_EXPECT_TRUE(probes.empty());
  probes = c.OnNetworkAvailability(true, kT0);
  SM_EXPECT_GE(probes.size(), 2u);
}

SM_TEST(ProbeControllerV2, ProbeOnlyWhenNetworkIsUp) {
  ProbeControllerV2 c;
  c.OnNetworkAvailability(false, kT0);
  auto probes = c.SetBitrates(kMinBitrate, kStartBitrate, kMaxBitrate, kT0);
  SM_EXPECT_TRUE(probes.empty());
  probes = c.OnNetworkAvailability(true, kT0);
  SM_EXPECT_GE(probes.size(), 2u);
}

SM_TEST(ProbeControllerV2, InitiatesProbingOnMaxBitrateIncrease) {
  ProbeControllerV2 c = Started();
  Timestamp now = kT0 + kExponentialProbingTimeout;  // Long enough to time out exponential probing.
  c.SetEstimatedBitrate(kStartBitrate, kDelay, now);
  c.Process(now);
  auto probes = c.SetBitrates(kMinBitrate, kStartBitrate, kMaxBitrate + Bps(100), now);
  SM_ASSERT_EQ(probes.size(), 1u);
  SM_EXPECT_EQ(probes[0].rate.bps(), kMaxBitrate.bps() + 100);
}

SM_TEST(ProbeControllerV2, TestExponentialProbing) {
  ProbeControllerV2 c = Started();
  // A repeated probe is only sent when the estimate climbs above 0.7 x 6 x start = 1260.
  auto probes = c.SetEstimatedBitrate(Bps(1000), kDelay, kT0);
  SM_EXPECT_TRUE(probes.empty());
  probes = c.SetEstimatedBitrate(Bps(1800), kDelay, kT0);
  SM_ASSERT_EQ(probes.size(), 1u);
  SM_EXPECT_EQ(probes[0].rate.bps(), 2 * 1800);
}

SM_TEST(ProbeControllerV2, InitialProbingTimeout) {
  ProbeControllerV2 c = Started();
  Timestamp now = kT0 + kExponentialProbingTimeout;  // Time out waiting for the result.
  SM_EXPECT_TRUE(c.Process(now).empty());
  SM_EXPECT_TRUE(c.SetEstimatedBitrate(Bps(1800), kDelay, now).empty());
}

SM_TEST(ProbeControllerV2, RequestProbeInAlr) {
  ProbeControllerV2 c = Started();
  c.SetEstimatedBitrate(Bps(500), kDelay, kT0);
  c.SetAlrStartTime(kT0);
  Timestamp now = kT0 + kAlrProbeInterval + TimeDelta::Millis(1);
  c.Process(now);
  c.SetEstimatedBitrate(Bps(250), kDelay, now);
  auto probes = c.RequestProbe(now);
  SM_ASSERT_EQ(probes.size(), 1u);
  SM_EXPECT_EQ(probes[0].rate.bps(), static_cast<int64_t>(0.85 * 500));
}

SM_TEST(ProbeControllerV2, RequestProbeWhenAlrEndedRecently) {
  ProbeControllerV2 c = Started();
  c.SetEstimatedBitrate(Bps(500), kDelay, kT0);
  c.SetAlrStartTime(std::nullopt);
  Timestamp now = kT0 + kAlrProbeInterval + TimeDelta::Millis(1);
  c.Process(now);
  c.SetEstimatedBitrate(Bps(250), kDelay, now);
  c.SetAlrEndedTime(now);
  now += kAlrEndedTimeout - TimeDelta::Millis(1);
  auto probes = c.RequestProbe(now);
  SM_ASSERT_EQ(probes.size(), 1u);
  SM_EXPECT_EQ(probes[0].rate.bps(), static_cast<int64_t>(0.85 * 500));
}

SM_TEST(ProbeControllerV2, RequestProbeWhenAlrNotEndedRecently) {
  ProbeControllerV2 c = Started();
  c.SetEstimatedBitrate(Bps(500), kDelay, kT0);
  c.SetAlrStartTime(std::nullopt);
  Timestamp now = kT0 + kAlrProbeInterval + TimeDelta::Millis(1);
  c.Process(now);
  c.SetEstimatedBitrate(Bps(250), kDelay, now);
  c.SetAlrEndedTime(now);
  now += kAlrEndedTimeout + TimeDelta::Millis(1);
  SM_EXPECT_TRUE(c.RequestProbe(now).empty());
}

SM_TEST(ProbeControllerV2, RequestProbeWhenBweDropNotRecent) {
  ProbeControllerV2 c = Started();
  c.SetEstimatedBitrate(Bps(500), kDelay, kT0);
  c.SetAlrStartTime(kT0);
  Timestamp now = kT0 + kAlrProbeInterval + TimeDelta::Millis(1);
  c.Process(now);
  c.SetEstimatedBitrate(Bps(250), kDelay, now);
  now += kBitrateDropTimeout + TimeDelta::Millis(1);
  SM_EXPECT_TRUE(c.RequestProbe(now).empty());
}

SM_TEST(ProbeControllerV2, PeriodicAlrProbingIsOffUnlessRequested) {
  {
    ProbeControllerV2 c = Started();
    Timestamp now = kT0 + kExponentialProbingTimeout;
    c.SetEstimatedBitrate(Bps(500), kDelay, now);
    c.Process(now);
    c.SetAlrStartTime(now);
    now += kAlrProbeInterval + TimeDelta::Millis(1);
    SM_EXPECT_TRUE(c.Process(now).empty());  // enable_periodic_alr_probing_ is false by default.
  }
  ProbeControllerV2 c = Started();
  c.EnablePeriodicAlrProbing(true);
  Timestamp now = kT0 + kExponentialProbingTimeout;
  c.SetEstimatedBitrate(Bps(500), kDelay, now);
  c.Process(now);
  c.SetAlrStartTime(now);
  SM_EXPECT_TRUE(c.Process(now + kAlrProbeInterval - TimeDelta::Millis(1)).empty());
  auto probes = c.Process(now + kAlrProbeInterval);
  SM_ASSERT_EQ(probes.size(), 1u);
  SM_EXPECT_EQ(probes[0].rate.bps(), 1000);  // alr_probe_scale 2 x estimate.
}

SM_TEST(ProbeControllerV2, NoProbesWhenLimitedByDelayIncrease) {
  ProbeControllerV2 c = Started();
  Timestamp now = kT0 + kExponentialProbingTimeout;
  c.SetEstimatedBitrate(Bps(500), kDelay, now);
  c.Process(now);
  // DontProbeIfDelayIncreased: no probe in a bandwidth limited state.
  c.SetEstimatedBitrate(Bps(500), BandwidthLimitedCause::kDelayBasedLimitedDelayIncreased, now);
  c.EnablePeriodicAlrProbing(true);
  c.SetAlrStartTime(now);
  SM_EXPECT_TRUE(c.Process(now + kAlrProbeInterval + TimeDelta::Millis(1)).empty());
}

SM_TEST(ProbeControllerV2, ProbesOnMaxAllocatedBitrateIncrease) {
  ProbeControllerV2 c = Started();
  Timestamp now = kT0 + kExponentialProbingTimeout;
  c.SetEstimatedBitrate(Bps(500), kDelay, now);
  c.Process(now);
  // alloc_p1 = 1, alloc_p2 = 2, limited to 2 x the estimate: probes at 1000 only.
  auto probes = c.OnMaxTotalAllocatedBitrate(Bps(700), now);
  SM_ASSERT_EQ(probes.size(), 2u);
  SM_EXPECT_EQ(probes[0].rate.bps(), 700);
  SM_EXPECT_EQ(probes[1].rate.bps(), 1000);
  // A decrease does not probe.
  now += kExponentialProbingTimeout;
  c.SetEstimatedBitrate(Bps(500), kDelay, now);
  c.Process(now);
  SM_EXPECT_TRUE(c.OnMaxTotalAllocatedBitrate(Bps(400), now).empty());
}

SM_TEST(ProbeControllerV2, ProbesAreCappedAtTwiceMaxAllocated) {
  ProbeControllerV2 c;
  c.OnNetworkAvailability(true, kT0);
  c.OnMaxTotalAllocatedBitrate(Bps(500), kT0);
  auto probes = c.SetBitrates(kMinBitrate, kStartBitrate, kMaxBitrate, kT0);
  // TestAllocatedBitrateCap: 3x300 = 900 stays, 6x300 = 1800 is capped at 2 x 500.
  SM_ASSERT_EQ(probes.size(), 2u);
  SM_EXPECT_EQ(probes[0].rate.bps(), 900);
  SM_EXPECT_EQ(probes[1].rate.bps(), 1000);
}

// --- ProbeBitrateEstimator (libwebrtc mode) ---------------------------------------------

namespace {
struct EstimatorFixture {
  ProbeBitrateEstimator est{true};
  std::optional<DataRate> measured;
  void Add(int cluster, int64_t size, int64_t send_ms, int64_t arrival_ms, int min_probes = 5, int64_t min_bytes = 5000) {
    const Timestamp ref = Timestamp::Seconds(1000);
    ProbeCluster c;
    c.id = cluster;
    c.min_probes = min_probes;
    c.min_bytes = DataSize::Bytes(min_bytes);
    est.SetClusterInfo(c);
    PacketResult r;
    r.send_time = ref + TimeDelta::Millis(send_ms);
    r.receive_time = ref + TimeDelta::Millis(arrival_ms);
    r.size = DataSize::Bytes(size);
    r.probe_cluster = cluster;
    measured = est.OnPacketResult(r);
  }
};
constexpr double kTarget = 0.95;
}  // namespace

SM_TEST(ProbeBitrateEstimatorV2, OneCluster) {
  EstimatorFixture f;
  f.Add(0, 1000, 0, 10);
  f.Add(0, 1000, 10, 20);
  f.Add(0, 1000, 20, 30);
  f.Add(0, 1000, 30, 40);
  SM_ASSERT_TRUE(f.measured.has_value());
  SM_EXPECT_NEAR(static_cast<double>(f.measured->bps()), 800000, 10);
}

SM_TEST(ProbeBitrateEstimatorV2, OneClusterTooFewProbes) {
  EstimatorFixture f;
  f.Add(0, 2000, 0, 10);
  f.Add(0, 2000, 10, 20);
  f.Add(0, 2000, 20, 30);
  SM_EXPECT_TRUE(!f.measured);
}

SM_TEST(ProbeBitrateEstimatorV2, OneClusterTooFewBytes) {
  EstimatorFixture f;
  for (int i = 0; i < 5; ++i) f.Add(0, 800, i * 10, i * 10 + 10, 5, 6000);
  SM_EXPECT_TRUE(!f.measured);
}

SM_TEST(ProbeBitrateEstimatorV2, SmallCluster) {
  EstimatorFixture f;
  for (int i = 0; i < 6; ++i) f.Add(0, 150, i * 10, i * 10 + 10, 5, 1000);
  SM_ASSERT_TRUE(f.measured.has_value());
  SM_EXPECT_NEAR(static_cast<double>(f.measured->bps()), 120000, 10);
}

SM_TEST(ProbeBitrateEstimatorV2, LargeCluster) {
  EstimatorFixture f;
  int64_t send_time = 0, receive_time = 5;
  for (int i = 0; i < 25; ++i) {
    f.Add(0, 12500, send_time, receive_time, 30, 312500);
    ++send_time;
    ++receive_time;
  }
  SM_ASSERT_TRUE(f.measured.has_value());
  SM_EXPECT_NEAR(static_cast<double>(f.measured->bps()), 100000000, 10);
}

SM_TEST(ProbeBitrateEstimatorV2, FastReceive) {
  EstimatorFixture f;
  f.Add(0, 1000, 0, 15);
  f.Add(0, 1000, 10, 30);
  f.Add(0, 1000, 20, 35);
  f.Add(0, 1000, 30, 40);
  SM_ASSERT_TRUE(f.measured.has_value());
  SM_EXPECT_NEAR(static_cast<double>(f.measured->bps()), 800000, 10);
}

SM_TEST(ProbeBitrateEstimatorV2, TooFastReceive) {
  EstimatorFixture f;  // The receive rate is more than twice the send rate: rejected.
  f.Add(0, 1000, 0, 19);
  f.Add(0, 1000, 10, 22);
  f.Add(0, 1000, 20, 25);
  f.Add(0, 1000, 40, 27);
  SM_EXPECT_TRUE(!f.measured);
}

SM_TEST(ProbeBitrateEstimatorV2, SlowReceive) {
  EstimatorFixture f;
  f.Add(0, 1000, 0, 10);
  f.Add(0, 1000, 10, 40);
  f.Add(0, 1000, 20, 70);
  f.Add(0, 1000, 30, 85);
  // Send rate 800 kbps, receive rate 320 kbps.
  SM_ASSERT_TRUE(f.measured.has_value());
  SM_EXPECT_NEAR(static_cast<double>(f.measured->bps()), kTarget * 320000, 10);
}

SM_TEST(ProbeBitrateEstimatorV2, BurstReceive) {
  EstimatorFixture f;
  f.Add(0, 1000, 0, 50);
  f.Add(0, 1000, 10, 50);
  f.Add(0, 1000, 20, 50);
  f.Add(0, 1000, 40, 50);
  SM_EXPECT_TRUE(!f.measured);
}

SM_TEST(ProbeBitrateEstimatorV2, MultipleClusters) {
  EstimatorFixture f;
  f.Add(0, 1000, 0, 10);
  f.Add(0, 1000, 10, 20);
  f.Add(0, 1000, 20, 30);
  f.Add(0, 1000, 40, 60);
  // Send rate 600 kbps, receive rate 480 kbps.
  SM_ASSERT_TRUE(f.measured.has_value());
  SM_EXPECT_NEAR(static_cast<double>(f.measured->bps()), kTarget * 480000, 10);
  f.Add(0, 1000, 50, 60);
  SM_EXPECT_NEAR(static_cast<double>(f.measured->bps()), 640000, 10);
  f.Add(1, 1000, 60, 70);
  f.Add(1, 1000, 65, 77);
  f.Add(1, 1000, 70, 84);
  f.Add(1, 1000, 75, 90);
  SM_ASSERT_TRUE(f.measured.has_value());
  SM_EXPECT_NEAR(static_cast<double>(f.measured->bps()), kTarget * 1200000, 10);
}

SM_TEST(ProbeBitrateEstimatorV2, IgnoreOldClusters) {
  EstimatorFixture f;
  f.Add(0, 1000, 0, 10);
  f.Add(0, 1000, 10, 20);
  f.Add(0, 1000, 20, 30);
  f.Add(1, 1000, 60, 70);
  f.Add(1, 1000, 65, 77);
  f.Add(1, 1000, 70, 84);
  f.Add(1, 1000, 75, 90);
  SM_ASSERT_TRUE(f.measured.has_value());
  SM_EXPECT_NEAR(static_cast<double>(f.measured->bps()), kTarget * 1200000, 10);
  // Coming in 6 s later.
  f.Add(0, 1000, 40 + 6000, 60 + 6000);
  SM_EXPECT_TRUE(!f.measured);
}

SM_TEST(ProbeBitrateEstimatorV2, IgnoreSizeLastSendPacket) {
  EstimatorFixture f;
  for (int i = 0; i < 4; ++i) f.Add(0, 1000, i * 10, i * 10 + 10);
  f.Add(0, 1500, 40, 50);
  SM_ASSERT_TRUE(f.measured.has_value());
  SM_EXPECT_NEAR(static_cast<double>(f.measured->bps()), 800000, 10);
}

SM_TEST(ProbeBitrateEstimatorV2, IgnoreSizeFirstReceivePacket) {
  EstimatorFixture f;
  f.Add(0, 1500, 0, 10);
  f.Add(0, 1000, 10, 20);
  f.Add(0, 1000, 20, 30);
  f.Add(0, 1000, 30, 40);
  SM_ASSERT_TRUE(f.measured.has_value());
  SM_EXPECT_NEAR(static_cast<double>(f.measured->bps()), kTarget * 800000, 10);
}

SM_TEST(ProbeBitrateEstimatorV2, FetchLastEstimatedBitrate) {
  EstimatorFixture f;
  SM_EXPECT_TRUE(!f.est.FetchAndResetLastEstimate());
  for (int i = 0; i < 4; ++i) f.Add(0, 1000, i * 10, i * 10 + 10);
  auto e = f.est.FetchAndResetLastEstimate();
  SM_ASSERT_TRUE(e.has_value());
  SM_EXPECT_NEAR(static_cast<double>(e->bps()), 800000, 10);
  SM_EXPECT_TRUE(!f.est.FetchAndResetLastEstimate());
}

// --- GccController (cc v2) -------------------------------------------------------------

SM_TEST(GccV2Probing, StartsWithTheThreeAndSixTimesClusters) {
  GccConfig cfg;
  cfg.version = 2;
  GccController g(cfg);
  auto u = g.Start(Timestamp::Seconds(1));
  SM_ASSERT_EQ(u.probes.size(), 2u);
  SM_EXPECT_EQ(u.probes[0].rate.bps(), 900000);
  SM_EXPECT_EQ(u.probes[1].rate.bps(), 1800000);
  SM_EXPECT_TRUE(u.probes[0].min_probe_delta == TimeDelta::Millis(2));
}

SM_TEST(GccV2Probing, AlrIsTrackedButThereAreNoPeriodicProbes) {
  GccConfig cfg;
  cfg.version = 2;
  GccController g(cfg);
  g.Start(Timestamp::Seconds(1));
  // A trickle of small packets, far under the estimate, puts the sender in ALR
  // (GoogCcNetworkController::OnSentPacket feeds the detector) ...
  Timestamp now = Timestamp::Seconds(1);
  int clusters = 0;
  for (int i = 0; i < 400; ++i) {
    now += TimeDelta::Millis(25);
    g.OnPacketSent(static_cast<uint16_t>(i), now, DataSize::Bytes(100));
    clusters += static_cast<int>(g.OnProcessInterval(now, DataRate::Zero()).probes.size());
  }
  SM_EXPECT_TRUE(g.alr_detector().GetApplicationLimitedRegionStartTime().has_value());
  // ... but a stream that does not request ALR probing never probes because of it.
  SM_EXPECT_EQ(clusters, 0);
}

// --- #48: the start-up probe does not depend on the size of the first packet ----------

namespace {
// Runs the start-up clusters (3x and 6x 300 kbps) through the pacer and a
// bottleneck of `capacity`, with a first packet of `first_packet` bytes followed
// by a keyframe of 1200-byte packets, and returns the estimate of each cluster.
struct StartUp {
  std::optional<DataRate> estimate[3];  // By cluster id 1 and 2.
  int tagged[3] = {0, 0, 0};
};

StartUp RunStartUp(int64_t first_packet, DataRate capacity) {
  StartUp out;
  PacingController pacer;
  pacer.SetPacingRates(DataRate::KilobitsPerSec(750), DataRate::Zero());
  ProbeBitrateEstimator est(true);
  ProbeControllerV2 controller;
  controller.OnNetworkAvailability(true, Timestamp::Micros(0));
  for (const auto& c : controller.SetBitrates(DataRate::KilobitsPerSec(30), DataRate::KilobitsPerSec(300), DataRate::KilobitsPerSec(5000),
                                              Timestamp::Micros(0))) {
    pacer.AddProbeCluster(c);
    est.SetClusterInfo(c);
  }
  uint64_t id = 1;
  pacer.Enqueue(Media(id++, first_packet, Timestamp::Micros(0)));
  for (int i = 0; i < 24; ++i) pacer.Enqueue(Media(id++, 1200, Timestamp::Micros(0)));
  Timestamp link_free = Timestamp::Micros(0);
  const TimeDelta delay = TimeDelta::Millis(25);
  for (int us = 0; us < 400000; us += 250) {
    const Timestamp now = Timestamp::Micros(us);
    for (auto& s : pacer.Process(now)) {
      const Timestamp start = std::max(now + delay, link_free);
      link_free = start + TimeDelta::Micros(s.size.bytes() * 8000000 / capacity.bps());
      if (s.probe_cluster < 0) continue;
      PacketResult r;
      r.send_time = now;
      r.receive_time = link_free;
      r.size = s.size;
      r.probe_cluster = s.probe_cluster;
      if (s.probe_cluster <= 2) ++out.tagged[s.probe_cluster];
      if (auto e = est.OnPacketResult(r)) {
        if (s.probe_cluster <= 2) out.estimate[s.probe_cluster] = e;
      }
    }
  }
  return out;
}
}  // namespace

SM_TEST(StartUpProbing, DoesNotDependOnTheFirstPacketSize) {
  for (int64_t first : {24, 70, 1200}) {
    const StartUp s = RunStartUp(first, DataRate::KilobitsPerSec(3000));
    SM_EXPECT_GE(s.tagged[1], 5);
    SM_EXPECT_GE(s.tagged[2], 5);
    // Both clusters leave the pacer at their rate and the link keeps up: the
    // estimates are the probe rates (900 and 1800 kbps), as issue #48 expects.
    SM_ASSERT_TRUE(s.estimate[1].has_value());
    SM_ASSERT_TRUE(s.estimate[2].has_value());
    SM_EXPECT_NEAR(s.estimate[1]->kbps(), 900, 150);
    SM_EXPECT_NEAR(s.estimate[2]->kbps(), 1800, 300);
  }
}

SM_TEST(StartUpProbing, SaturatedLinkIsMeasuredAtTheBottleneck) {
  for (int64_t first : {24, 1200}) {
    const StartUp s = RunStartUp(first, DataRate::KilobitsPerSec(1200));
    SM_ASSERT_TRUE(s.estimate[1].has_value());
    SM_ASSERT_TRUE(s.estimate[2].has_value());
    SM_EXPECT_NEAR(s.estimate[1]->kbps(), 900, 150);
    // The 1800 kbps cluster is received at the link rate: 0.95 x about 1200 kbps.
    SM_EXPECT_NEAR(s.estimate[2]->kbps(), 0.95 * 1200, 200);
  }
}
