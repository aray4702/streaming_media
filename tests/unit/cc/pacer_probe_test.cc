#include "sm/cc/pacer.h"
#include "sm/cc/probing.h"
#include <algorithm>
#include <vector>

#include "sm_test.h"

using namespace sm;
using namespace sm::cc;

namespace {
PacedPacket Pkt(uint64_t id, PacketPriority prio, int64_t bytes, Timestamp t) {
  PacedPacket p;
  p.id = id;
  p.priority = prio;
  p.size = DataSize::Bytes(bytes);
  p.enqueue_time = t;
  return p;
}

// Calls Process() at NextSendTime() until `stop` is true or `until` passes;
// returns every sent packet with its send time.
struct Sent {
  PacedPacket p;
  Timestamp t;
};
std::vector<Sent> Run(Pacer& pacer, Timestamp& now, Timestamp until, size_t stop_at_queue = 0) {
  std::vector<Sent> out;
  while (now < until && pacer.QueuePackets() > stop_at_queue) {
    now = std::max(now, pacer.NextSendTime(now));
    for (auto& s : pacer.Process(now)) out.push_back({s, now});
  }
  return out;
}
}  // namespace

// The cases below port the intent of libwebrtc's pacing_controller_unittest.cc
// (compare/webrtc/reference/modules/pacing/), named after the case they follow.

// QueueAndPacePacketsWithZeroBurstPeriod
SM_TEST(Pacer, ZeroBurstIntervalPacesPacketByPacket) {
  PacerConfig cfg;
  cfg.send_burst_interval = TimeDelta::Zero();
  Pacer pacer(cfg);
  pacer.SetPacingRates(DataRate::KilobitsPerSec(400), DataRate::Zero());  // 50 bytes/ms
  Timestamp now = Timestamp::Millis(1000);
  for (uint64_t i = 0; i < 5; ++i) pacer.Enqueue(Pkt(i, PacketPriority::kVideo, 250, now));
  const Timestamp start = now;
  auto sent = Run(pacer, now, start + TimeDelta::Seconds(1));
  SM_ASSERT_EQ(sent.size(), 5u);
  // One 250-byte packet per 5 ms: the last leaves 4 packets (20 ms) after the first.
  SM_EXPECT_EQ((sent.back().t - sent.front().t).us(), 20000);
}

// SendsPacketsInBurstImmediately
SM_TEST(Pacer, BurstIntervalAllowsABurst) {
  PacerConfig cfg;
  cfg.send_burst_interval = TimeDelta::Millis(20);
  Pacer pacer(cfg);
  pacer.SetPacingRates(DataRate::BitsPerSec(80000), DataRate::Zero());  // 10 kB/s
  const Timestamp t0 = Timestamp::Millis(1000);
  for (uint64_t i = 0; i < 3; ++i) pacer.Enqueue(Pkt(i, PacketPriority::kVideo, 100, t0));
  // The debt may grow to one packet beyond 20 ms of the rate (200 bytes): two go out.
  SM_EXPECT_EQ(pacer.Process(t0).size(), 2u);
  SM_EXPECT_EQ(pacer.QueuePackets(), 1u);
  SM_EXPECT_EQ((pacer.NextSendTime(t0) - t0).us(), 20000);
  SM_EXPECT_EQ(pacer.Process(t0 + TimeDelta::Millis(20)).size(), 1u);
}

// RespectsTargetRateWhenSendingPacketsInBursts
SM_TEST(Pacer, BurstsKeepTheTargetRate) {
  PacerConfig cfg;
  cfg.send_burst_interval = TimeDelta::Millis(20);
  Pacer pacer(cfg);
  pacer.SetPacingRates(DataRate::KilobitsPerSec(1000), DataRate::Zero());
  Timestamp now = Timestamp::Millis(1000);
  const Timestamp start = now;
  uint64_t id = 0;
  int sent = 0, bursts = 0;
  while (now < start + TimeDelta::Millis(100)) {
    for (int k = 0; k < 5; ++k) pacer.Enqueue(Pkt(id++, PacketPriority::kVideo, 1000, now));
    if (pacer.NextSendTime(now) <= now) {
      sent += static_cast<int>(pacer.Process(now).size());
      ++bursts;
    }
    now += TimeDelta::Millis(5);
  }
  // 1 Mbps for 100 ms is 12.5 kB; bursts of 3 packets (24 ms each) leave 4 bursts.
  SM_EXPECT_EQ(sent, 12);
  SM_EXPECT_EQ(bursts, 4);
}

// MaxBurstSizeLimitedAtHighPacingRateWhenSendingPacketsInBursts
SM_TEST(Pacer, BurstIsCappedAtHighRates) {
  PacerConfig cfg;
  cfg.send_burst_interval = TimeDelta::Millis(100);
  Pacer pacer(cfg);
  pacer.SetPacingRates(DataRate::KilobitsPerSec(10000), DataRate::Zero());
  Timestamp now = Timestamp::Millis(1000);
  for (uint64_t i = 0; i < 200; ++i) pacer.Enqueue(Pkt(i, PacketPriority::kVideo, 1000, now));
  while (pacer.QueuePackets() > 70) {
    int64_t burst = 0;
    for (auto& s : pacer.Process(now)) burst += s.size.bytes();
    SM_EXPECT_NEAR(static_cast<double>(burst), 63000.0, 1000.0);  // kMaxBurstSize
    const TimeDelta next = pacer.NextSendTime(now) - now;
    SM_EXPECT_NEAR(next.ms_f(), 50.0, 2.0);
    now += next;
  }
}

// PaceQueuedPackets: a queue drains at the pacing rate.
SM_TEST(Pacer, RespectsPacingRate) {
  Pacer p;
  p.SetPacingRates(DataRate::KilobitsPerSec(800), DataRate::Zero());
  // 150 kB drains in 1.5 s at 800 kbps: inside the 2 s limit, so no boost.
  Timestamp now = Timestamp::Millis(0);
  for (uint64_t i = 0; i < 150; ++i) p.Enqueue(Pkt(i, PacketPriority::kVideo, 1000, now));
  int64_t bytes = 0;
  for (; now <= Timestamp::Millis(1000); now += TimeDelta::Millis(1)) {
    for (auto& s : p.Process(now)) bytes += s.size.bytes();
  }
  // 800 kbps for 1 s = 100 kB, plus the burst interval's 40 ms (4 kB) and one packet.
  SM_EXPECT_NEAR(static_cast<double>(bytes), 100000 + 4000, 2000);
  SM_EXPECT_EQ(p.stats().drain_boosts, 0u);
}

// Priority: audio first, then retransmissions, then video; FEC shares video's level.
SM_TEST(Pacer, PriorityOrder) {
  Pacer p;
  p.SetPacingRates(DataRate::KilobitsPerSec(100), DataRate::Zero());
  Timestamp now = Timestamp::Millis(0);
  p.Enqueue(Pkt(1, PacketPriority::kVideo, 1000, now));  // Spends the burst allowance.
  p.Enqueue(Pkt(2, PacketPriority::kVideo, 1000, now));
  p.Enqueue(Pkt(3, PacketPriority::kFec, 1000, now));
  p.Enqueue(Pkt(4, PacketPriority::kVideo, 1000, now));
  p.Enqueue(Pkt(5, PacketPriority::kRetransmission, 1000, now));
  p.Enqueue(Pkt(6, PacketPriority::kPadding, 1000, now));
  p.Enqueue(Pkt(7, PacketPriority::kAudio, 100, now));
  std::vector<uint64_t> order;
  for (auto& s : Run(p, now, Timestamp::Seconds(30))) order.push_back(s.p.id);
  // Audio, the retransmission, then video and FEC in the order they arrived, padding last.
  const std::vector<uint64_t> want = {7, 5, 1, 2, 3, 4, 6};
  SM_EXPECT_TRUE(order == want);
}

// HighPrioDoesntAffectBudget / AudioNotPacedEvenWhenAccountedFor
SM_TEST(Pacer, AudioIsNotPacedButIsAccounted) {
  Pacer p;  // account_for_audio is on, as in a call with audio in the allocation.
  p.SetPacingRates(DataRate::BitsPerSec(123 * 8), DataRate::Zero());  // One 123-byte packet per second.
  Timestamp now = Timestamp::Millis(1000);
  p.Enqueue(Pkt(1, PacketPriority::kAudio, 123, now));
  SM_EXPECT_EQ(p.Process(now).size(), 1u);
  now += TimeDelta::Millis(5);
  p.Enqueue(Pkt(2, PacketPriority::kAudio, 123, now));
  SM_EXPECT_EQ(p.Process(now).size(), 1u);  // Sent at once, whatever the debt.
  // The two audio packets (246 bytes) are debt that video has to wait for; the
  // debt stops at 500 ms of the rate (kMaxDebtInTime), so the wait is 0.5 s.
  p.Enqueue(Pkt(3, PacketPriority::kVideo, 100, now));
  SM_EXPECT_TRUE(p.Process(now).empty());
  SM_EXPECT_NEAR((p.NextSendTime(now) - now).ms_f(), 500.0, 10.0);

  PacerConfig cfg;
  cfg.account_for_audio = false;
  Pacer q(cfg);
  q.SetPacingRates(DataRate::BitsPerSec(123 * 8), DataRate::Zero());
  q.Enqueue(Pkt(1, PacketPriority::kAudio, 123, now));
  q.Process(now);
  q.Enqueue(Pkt(2, PacketPriority::kVideo, 100, now));
  SM_EXPECT_EQ(q.Process(now).size(), 1u);  // Audio left no debt.
}

// GapInPacingDoesntAccumulateBudget
SM_TEST(Pacer, IdleTimeIsNotBudget) {
  Pacer p;
  p.SetPacingRates(DataRate::BitsPerSec(1000 * 8 * 40), DataRate::Zero());  // 1000 bytes per 25 ms
  Timestamp now = Timestamp::Millis(1000);
  p.Enqueue(Pkt(1, PacketPriority::kVideo, 1000, now));
  SM_EXPECT_EQ(p.Process(now).size(), 1u);
  now += TimeDelta::Millis(50);
  for (uint64_t i = 2; i <= 4; ++i) p.Enqueue(Pkt(i, PacketPriority::kVideo, 1000, now));
  // Only the 40 ms burst interval of budget: two packets, not three.
  SM_EXPECT_EQ(p.Process(now).size(), 2u);
  SM_EXPECT_EQ(p.QueuePackets(), 1u);
}

// RespectsQueueTimeLimit: the average queue time decides the rate boost.
SM_TEST(Pacer, QueueTimeLimitRaisesTheRate) {
  PacerConfig cfg;
  cfg.queue_time_limit = TimeDelta::Millis(1000);
  Pacer p(cfg);
  p.SetPacingRates(DataRate::KilobitsPerSec(200), DataRate::Zero());
  Timestamp now = Timestamp::Millis(1000);
  const int per_limit = 250;  // 100-byte packets: 1 s of data at 200 kbps.
  for (int i = 0; i < per_limit; ++i) p.Enqueue(Pkt(i, PacketPriority::kVideo, 100, now));
  SM_EXPECT_EQ(p.pacing_rate().bps(), 200000);
  SM_EXPECT_EQ(p.stats().drain_boosts, 0u);
  for (int i = 0; i < per_limit; ++i) p.Enqueue(Pkt(per_limit + i, PacketPriority::kVideo, 100, now));
  SM_EXPECT_EQ(p.pacing_rate().bps(), 400000);  // Twice the data in the same time.
  SM_EXPECT_EQ(p.stats().drain_boosts, 1u);
  const Timestamp start = now;
  Run(p, now, start + TimeDelta::Seconds(10));
  // The queue drained in about the limit, not in the 2 s the nominal rate needs.
  SM_EXPECT_NEAR((now - start).ms_f(), 1000.0, 50.0);
  SM_EXPECT_EQ(p.pacing_rate().bps(), 200000);
}

// The limit is on the average packet, not the oldest (libwebrtc), so one old
// packet in a short queue does not boost the rate.
SM_TEST(Pacer, OneOldPacketDoesNotBoost) {
  Pacer p;
  p.SetPacingRates(DataRate::KilobitsPerSec(100), DataRate::Zero());
  Timestamp now = Timestamp::Millis(0);
  p.SetCongested(true);  // Nothing leaves, and the queue does not age while congested.
  p.Enqueue(Pkt(1, PacketPriority::kVideo, 1000, now));
  now += TimeDelta::Millis(1900);
  for (uint64_t i = 2; i < 12; ++i) p.Enqueue(Pkt(i, PacketPriority::kVideo, 1000, now));
  SM_EXPECT_EQ(p.stats().drain_boosts, 0u);
  SM_EXPECT_EQ(p.pacing_rate().bps(), 100000);
}

// Padding: sent at the padding rate when there is no media, only after a media packet.
SM_TEST(Pacer, PaddingFillsThePaddingRate) {
  Pacer p;
  p.SetPacingRates(DataRate::KilobitsPerSec(1000), DataRate::KilobitsPerSec(400));
  Timestamp now = Timestamp::Millis(1000);
  // NoPaddingBeforeNormalPacket.
  for (int i = 0; i < 20; ++i, now += TimeDelta::Millis(5)) SM_EXPECT_TRUE(p.Process(now).empty());
  p.Enqueue(Pkt(1, PacketPriority::kVideo, 1000, now));
  p.Process(now);
  int64_t pad = 0;
  const Timestamp start = now;
  while (now < start + TimeDelta::Seconds(2)) {
    now = std::max(now + TimeDelta::Micros(100), p.NextSendTime(now));
    for (auto& s : p.Process(now)) {
      SM_EXPECT_TRUE(s.is_padding);
      pad += s.size.bytes();
    }
  }
  // 400 kbps for 2 s = 100 kB (the first debt was media).
  SM_EXPECT_NEAR(static_cast<double>(pad), 100000.0, 4000.0);
}

// SendsOnlyPaddingWhenCongested / CongestionPausesQueueTimeAging
SM_TEST(Pacer, CongestedSendsOnlyKeepalives) {
  Pacer p;
  p.SetPacingRates(DataRate::KilobitsPerSec(500), DataRate::Zero());
  Timestamp now = Timestamp::Millis(1000);
  p.Enqueue(Pkt(1, PacketPriority::kVideo, 1000, now));
  SM_EXPECT_EQ(p.Process(now).size(), 1u);
  for (uint64_t i = 2; i < 7; ++i) p.Enqueue(Pkt(i, PacketPriority::kVideo, 1000, now));
  p.SetCongested(true);
  int keepalives = 0, media = 0;
  for (int k = 0; k < 6; ++k) {
    now += TimeDelta::Millis(500);
    for (auto& s : p.Process(now)) (s.is_padding ? keepalives : media)++;
  }
  SM_EXPECT_EQ(media, 0);
  SM_EXPECT_EQ(keepalives, 6);
  // Audio is not held by congestion.
  p.Enqueue(Pkt(100, PacketPriority::kAudio, 100, now));
  auto out = p.Process(now);
  SM_ASSERT_EQ(out.size(), 1u);
  SM_EXPECT_EQ(out[0].id, 100u);
  p.SetCongested(false);
  // The queue did not age during 3 s of congestion: five packets are ~80 ms of data.
  SM_EXPECT_GT(p.ExpectedQueueTime().ms_f(), 50.0);
  SM_EXPECT_LT(p.ExpectedQueueTime().ms_f(), 100.0);
  SM_EXPECT_EQ(p.stats().drain_boosts, 0u);
}

// The cap on media debt (500 ms) and on credit for a long gap (2 s).
SM_TEST(Pacer, DebtIsCapped) {
  PacerConfig cfg;
  cfg.send_burst_interval = TimeDelta::Zero();
  Pacer p(cfg);
  p.SetPacingRates(DataRate::KilobitsPerSec(80), DataRate::Zero());  // 10 kB/s
  Timestamp now = Timestamp::Millis(0);
  // 12 kB of audio is 1.2 s of debt at the rate; the debt stops at 500 ms (5 kB).
  for (uint64_t i = 0; i < 12; ++i) p.Enqueue(Pkt(i, PacketPriority::kAudio, 1000, now));
  SM_EXPECT_EQ(p.Process(now).size(), 12u);
  p.Enqueue(Pkt(100, PacketPriority::kVideo, 1000, now));
  SM_EXPECT_NEAR((p.NextSendTime(now) - now).ms_f(), 500.0, 1.0);
}

SM_TEST(Pacer, ProbeClusterTagsAndPads) {
  Pacer p;
  p.SetPacingRates(DataRate::KilobitsPerSec(300), DataRate::Zero());
  ProbeCluster c{7, DataRate::KilobitsPerSec(2000), 5, DataSize::Bytes(3750)};
  p.AddProbeCluster(c);
  int tagged = 0, padding = 0;
  for (int t = 0; t < 50; ++t) {
    for (auto& s : p.Process(Timestamp::Millis(t))) {
      if (s.probe_cluster == 7) ++tagged;
      if (s.is_padding) ++padding;
    }
  }
  SM_EXPECT_GE(tagged, 5);
  SM_EXPECT_GE(padding, 5);  // No media queued: probe uses padding.
}

SM_TEST(Pacer, PauseHoldsVideo) {
  Pacer p;
  p.SetPacingRates(DataRate::KilobitsPerSec(1000), DataRate::Zero());
  p.Pause();
  p.Enqueue(Pkt(1, PacketPriority::kVideo, 500, Timestamp::Millis(0)));
  SM_EXPECT_TRUE(p.Process(Timestamp::Millis(10)).empty());
  p.Resume();
  SM_EXPECT_EQ(p.Process(Timestamp::Millis(20)).size(), 1u);
  SM_EXPECT_EQ(CollectMetrics(p)["sent_packets_video"], 1.0);
}

SM_TEST(Probing, InitialAndFurtherProbes) {
  ProbeController pc;
  auto init = pc.SetBitrates(DataRate::KilobitsPerSec(30), DataRate::KilobitsPerSec(300),
                             DataRate::KilobitsPerSec(5000), Timestamp::Millis(0));
  SM_ASSERT_EQ(init.size(), 2u);
  SM_EXPECT_EQ(init[1].rate.kbps(), 1800.0);
  // Estimate reaches 70% of the last probe -> probe 2x higher.
  auto more = pc.SetEstimate(DataRate::KilobitsPerSec(1500), Timestamp::Millis(500));
  SM_ASSERT_EQ(more.size(), 1u);
  SM_EXPECT_EQ(more[0].rate.kbps(), 3000.0);
  // Below threshold: stop.
  SM_EXPECT_TRUE(pc.SetEstimate(DataRate::KilobitsPerSec(1600), Timestamp::Millis(900)).empty());
}

SM_TEST(Probing, PeriodicAlrProbe) {
  ProbeController pc;
  pc.SetBitrates(DataRate::KilobitsPerSec(30), DataRate::KilobitsPerSec(1000), DataRate::KilobitsPerSec(5000),
                 Timestamp::Millis(0));
  pc.SetEstimate(DataRate::KilobitsPerSec(1000), Timestamp::Millis(100));
  pc.SetApplicationLimited(true, Timestamp::Millis(1000));
  SM_EXPECT_TRUE(pc.Process(Timestamp::Millis(3000)).empty());
  SM_EXPECT_EQ(pc.Process(Timestamp::Millis(6100)).size(), 1u);
}

SM_TEST(Probing, EstimatorSaturatedVsFree) {
  auto run = [](double recv_spacing_ms) {
    ProbeBitrateEstimator e;
    e.SetClusterInfo({1, DataRate::KilobitsPerSec(2000), 5, DataSize::Bytes(3000)});
    std::optional<DataRate> est;
    for (int i = 0; i < 10; ++i) {
      PacketResult r;
      r.seq = i;
      r.probe_cluster = 1;
      r.size = DataSize::Bytes(1000);
      r.send_time = Timestamp::Millis(i * 4);  // 2 Mbps send rate.
      r.receive_time = Timestamp::Micros(static_cast<int64_t>((50 + i * recv_spacing_ms) * 1000));
      if (auto x = e.OnPacketResult(r)) est = x;
    }
    return est;
  };
  auto free = run(4.0);   // Link keeps up.
  auto sat = run(8.0);    // Link delivers half.
  SM_ASSERT_TRUE(free.has_value());
  SM_ASSERT_TRUE(sat.has_value());
  SM_EXPECT_NEAR(free->kbps(), 2000, 100);
  SM_EXPECT_NEAR(sat->kbps(), 950, 60);
}
