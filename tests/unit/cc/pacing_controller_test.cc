#include <algorithm>
#include <vector>

#include "sm/cc/pacing_controller.h"
#include "sm/cc/probing.h"
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
std::vector<Sent> Run(PacingController& pacer, Timestamp& now, Timestamp until, size_t stop_at_queue = 0) {
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
SM_TEST(PacingController, ZeroBurstIntervalPacesPacketByPacket) {
  PacingControllerConfig cfg;
  cfg.send_burst_interval = TimeDelta::Zero();
  PacingController pacer(cfg);
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
SM_TEST(PacingController, BurstIntervalAllowsABurst) {
  PacingControllerConfig cfg;
  cfg.send_burst_interval = TimeDelta::Millis(20);
  PacingController pacer(cfg);
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
SM_TEST(PacingController, BurstsKeepTheTargetRate) {
  PacingControllerConfig cfg;
  cfg.send_burst_interval = TimeDelta::Millis(20);
  PacingController pacer(cfg);
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
SM_TEST(PacingController, BurstIsCappedAtHighRates) {
  PacingControllerConfig cfg;
  cfg.send_burst_interval = TimeDelta::Millis(100);
  PacingController pacer(cfg);
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
SM_TEST(PacingController, RespectsPacingRate) {
  PacingController p;
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
SM_TEST(PacingController, PriorityOrder) {
  PacingController p;
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
SM_TEST(PacingController, AudioIsNotPacedButIsAccounted) {
  PacingController p;  // account_for_audio is on, as in a call with audio in the allocation.
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

  PacingControllerConfig cfg;
  cfg.account_for_audio = false;
  PacingController q(cfg);
  q.SetPacingRates(DataRate::BitsPerSec(123 * 8), DataRate::Zero());
  q.Enqueue(Pkt(1, PacketPriority::kAudio, 123, now));
  q.Process(now);
  q.Enqueue(Pkt(2, PacketPriority::kVideo, 100, now));
  SM_EXPECT_EQ(q.Process(now).size(), 1u);  // Audio left no debt.
}

// GapInPacingDoesntAccumulateBudget
SM_TEST(PacingController, IdleTimeIsNotBudget) {
  PacingController p;
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
SM_TEST(PacingController, QueueTimeLimitRaisesTheRate) {
  PacingControllerConfig cfg;
  cfg.queue_time_limit = TimeDelta::Millis(1000);
  PacingController p(cfg);
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
SM_TEST(PacingController, OneOldPacketDoesNotBoost) {
  PacingController p;
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
SM_TEST(PacingController, PaddingFillsThePaddingRate) {
  PacingController p;
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
SM_TEST(PacingController, CongestedSendsOnlyKeepalives) {
  PacingController p;
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
SM_TEST(PacingController, DebtIsCapped) {
  PacingControllerConfig cfg;
  cfg.send_burst_interval = TimeDelta::Zero();
  PacingController p(cfg);
  p.SetPacingRates(DataRate::KilobitsPerSec(80), DataRate::Zero());  // 10 kB/s
  Timestamp now = Timestamp::Millis(0);
  // 12 kB of audio is 1.2 s of debt at the rate; the debt stops at 500 ms (5 kB).
  for (uint64_t i = 0; i < 12; ++i) p.Enqueue(Pkt(i, PacketPriority::kAudio, 1000, now));
  SM_EXPECT_EQ(p.Process(now).size(), 12u);
  p.Enqueue(Pkt(100, PacketPriority::kVideo, 1000, now));
  SM_EXPECT_NEAR((p.NextSendTime(now) - now).ms_f(), 500.0, 1.0);
}

SM_TEST(PacingController, ProbeClusterTagsAndPads) {
  PacingController p;
  p.SetPacingRates(DataRate::KilobitsPerSec(300), DataRate::Zero());
  ProbeCluster c{7, DataRate::KilobitsPerSec(2000), 5, DataSize::Bytes(3750)};
  p.AddProbeCluster(c);
  // BitrateProber (#48): a probe starts when a media packet of a useful size is
  // enqueued; padding then fills the bursts.
  p.Enqueue(Pkt(1, PacketPriority::kVideo, 1200, Timestamp::Millis(0)));
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

SM_TEST(PacingController, PauseHoldsVideo) {
  PacingController p;
  p.SetPacingRates(DataRate::KilobitsPerSec(1000), DataRate::Zero());
  p.Pause();
  p.Enqueue(Pkt(1, PacketPriority::kVideo, 500, Timestamp::Millis(0)));
  SM_EXPECT_TRUE(p.Process(Timestamp::Millis(10)).empty());
  p.Resume();
  SM_EXPECT_EQ(p.Process(Timestamp::Millis(20)).size(), 1u);
  SM_EXPECT_EQ(CollectMetrics(p)["sent_packets_video"], 1.0);
}


namespace {
PacedPacket KeyPkt(uint64_t id, bool first, Timestamp t) {
  PacedPacket p = Pkt(id, PacketPriority::kVideo, 1000, t);
  p.is_key_frame = true;
  p.first_packet_of_frame = first;
  return p;
}
}  // namespace

// KeyframeFlushesQueuedPackets (PacingController::EnqueuePacket): the first
// packet of a keyframe discards the video stream's queued packets, video, FEC
// and retransmissions, and keeps audio.
SM_TEST(PacingController, KeyframeFlushesTheQueuedVideoPackets) {
  PacingController pacer;
  pacer.SetPacingRates(DataRate::KilobitsPerSec(100), DataRate::Zero());
  const Timestamp t0 = Timestamp::Millis(1000);
  pacer.Enqueue(Pkt(1, PacketPriority::kVideo, 1000, t0));
  pacer.Enqueue(Pkt(2, PacketPriority::kVideo, 1000, t0));
  pacer.Enqueue(Pkt(3, PacketPriority::kFec, 500, t0));
  pacer.Enqueue(Pkt(4, PacketPriority::kRetransmission, 1000, t0));
  pacer.Enqueue(Pkt(5, PacketPriority::kAudio, 100, t0));  // Not paced: leaves with the first Process().
  SM_ASSERT_EQ(pacer.QueuePackets(), 5u);
  pacer.Enqueue(KeyPkt(6, true, t0 + TimeDelta::Millis(10)));
  auto flushed = pacer.TakeFlushed();
  SM_ASSERT_EQ(flushed.size(), 4u);  // 1-4; the audio packet stays.
  SM_EXPECT_EQ(pacer.QueuePackets(), 2u);
  SM_EXPECT_EQ(pacer.QueueSize().bytes(), 1100);
  SM_EXPECT_EQ(pacer.stats().keyframe_flushes, 1u);
  SM_EXPECT_EQ(pacer.stats().flushed_packets, 4u);
  SM_EXPECT_TRUE(pacer.TakeFlushed().empty());
}

// Only the first packet of a keyframe flushes, and not while the queue already
// holds keyframe packets (HasKeyframePackets), so one keyframe is not cut by
// its own later packets.
SM_TEST(PacingController, KeyframeFlushOnlyOnTheFirstPacketAndOnlyOnce) {
  PacingController pacer;
  pacer.SetPacingRates(DataRate::KilobitsPerSec(100), DataRate::Zero());
  const Timestamp t0 = Timestamp::Millis(1000);
  pacer.Enqueue(Pkt(1, PacketPriority::kVideo, 1000, t0));
  pacer.Enqueue(KeyPkt(2, true, t0));
  SM_EXPECT_EQ(pacer.TakeFlushed().size(), 1u);
  pacer.Enqueue(KeyPkt(3, false, t0));  // Not the first packet of the frame.
  pacer.Enqueue(KeyPkt(4, true, t0));   // A keyframe is already queued.
  SM_EXPECT_TRUE(pacer.TakeFlushed().empty());
  SM_EXPECT_EQ(pacer.QueuePackets(), 3u);
  PacedPacket delta = Pkt(5, PacketPriority::kVideo, 1000, t0);  // A delta frame never flushes.
  delta.first_packet_of_frame = true;
  pacer.Enqueue(delta);
  SM_EXPECT_TRUE(pacer.TakeFlushed().empty());
}

// The queue accounting stays consistent after a flush: what is left drains.
SM_TEST(PacingController, QueueDrainsAfterAKeyframeFlush) {
  PacingController pacer;
  pacer.SetPacingRates(DataRate::KilobitsPerSec(400), DataRate::Zero());
  Timestamp now = Timestamp::Millis(1000);
  for (uint64_t i = 0; i < 10; ++i) pacer.Enqueue(Pkt(i, PacketPriority::kVideo, 1000, now));
  pacer.Enqueue(KeyPkt(100, true, now));
  pacer.Enqueue(KeyPkt(101, false, now));
  SM_EXPECT_EQ(pacer.TakeFlushed().size(), 10u);
  auto sent = Run(pacer, now, now + TimeDelta::Seconds(2));
  SM_ASSERT_EQ(sent.size(), 2u);
  SM_EXPECT_EQ(sent[0].p.id, 100u);
  SM_EXPECT_EQ(pacer.QueuePackets(), 0u);
  SM_EXPECT_EQ(pacer.QueueSize().bytes(), 0);
  SM_EXPECT_EQ(pacer.OldestQueueTime(now).us(), 0);
}

// PacketRouter::GeneratePadding in PacingController::ProcessPackets (pacing_controller.cc:480-510, 629-641,
// 420-433), #120: the padding comes from the sender with its real sizes, and the pacer accounts those.
SM_TEST(PacingController, PaddingGeneratorSizesAreAccounted) {
  PacingController pacer;
  pacer.SetPacingRates(DataRate::KilobitsPerSec(1000), DataRate::KilobitsPerSec(800));
  std::vector<int64_t> asked;
  uint64_t next_id = 1;
  pacer.SetPaddingGenerator([&](DataSize target, Timestamp now) {
    asked.push_back(target.bytes());
    std::vector<PacedPacket> out;
    // One packet of 303 bytes whatever the target, like a sender that pads in full packets.
    out.push_back(Pkt(next_id++, PacketPriority::kPadding, 303, now));
    return out;
  });
  Timestamp now = Timestamp::Millis(1000);
  pacer.Enqueue(Pkt(100, PacketPriority::kVideo, 1000, now));
  Timestamp until = now + TimeDelta::Millis(500);
  int64_t padding_bytes = 0;
  size_t padding_packets = 0;
  while (now < until) {
    now = std::max(now + TimeDelta::Millis(1), pacer.NextSendTime(now));
    for (auto& p : pacer.Process(now)) {
      if (p.priority == PacketPriority::kPadding) {
        SM_EXPECT_EQ(p.size.bytes(), 303);  // Not the 224 bytes of a placeholder.
        SM_EXPECT_FALSE(p.is_padding);
        padding_bytes += p.size.bytes();
        ++padding_packets;
      }
    }
  }
  SM_ASSERT_GT(padding_packets, 0u);
  SM_EXPECT_FALSE(asked.empty());
  // 800 kbps of padding for ~0.5 s is ~50 kB: the real sizes keep the rate, not the packet count of 224 B pieces.
  SM_EXPECT_LE(padding_bytes, 60000);
  SM_EXPECT_GE(padding_bytes, 30000);
}

// A generator that makes nothing still charges the padding budget (UpdatePaddingBudgetWithSentData), so the
// pacer does not ask again before the budget has drained: 5 ms of 800 kbps is 500 bytes, drained in 5 ms.
SM_TEST(PacingController, EmptyPaddingGeneratorChargesTheBudget) {
  PacingController pacer;
  pacer.SetPacingRates(DataRate::KilobitsPerSec(1000), DataRate::KilobitsPerSec(800));
  std::vector<Timestamp> asks;
  pacer.SetPaddingGenerator([&](DataSize target, Timestamp now) {
    SM_EXPECT_EQ(target.bytes(), 500);
    asks.push_back(now);
    return std::vector<PacedPacket>{};
  });
  Timestamp now = Timestamp::Millis(1000);
  pacer.Enqueue(Pkt(1, PacketPriority::kVideo, 200, now));
  for (int i = 0; i < 100; ++i) {
    now = std::max(now + TimeDelta::Millis(1), pacer.NextSendTime(now));
    for (auto& p : pacer.Process(now)) SM_EXPECT_TRUE(p.priority != PacketPriority::kPadding);
  }
  SM_ASSERT_GT(asks.size(), 10u);
  for (size_t i = 1; i < asks.size(); ++i) SM_EXPECT_GE((asks[i] - asks[i - 1]).ms(), 5);
}

// The first packet of a probe comes from GeneratePadding(1 byte) too.
SM_TEST(PacingController, ProbeStartsWithGeneratedPadding) {
  PacingController pacer;
  pacer.SetPacingRates(DataRate::KilobitsPerSec(1000), DataRate::Zero());
  uint64_t next_id = 1;
  std::vector<int64_t> asked;
  pacer.SetPaddingGenerator([&](DataSize target, Timestamp now) {
    asked.push_back(target.bytes());
    return std::vector<PacedPacket>{Pkt(next_id++, PacketPriority::kPadding, 150, now)};
  });
  ProbeCluster c{7, DataRate::KilobitsPerSec(2000), 5, DataSize::Bytes(3750)};
  Timestamp now = Timestamp::Millis(1000);
  pacer.AddProbeCluster(c);
  pacer.Enqueue(Pkt(100, PacketPriority::kVideo, 1000, now));
  bool saw_start = false;
  for (int i = 0; i < 300 && !saw_start; ++i) {
    now = std::max(now + TimeDelta::Millis(1), pacer.NextSendTime(now));
    for (auto& p : pacer.Process(now)) {
      if (p.probe_cluster == 7 && p.priority == PacketPriority::kPadding) {
        SM_EXPECT_EQ(p.size.bytes(), 150);
        saw_start = true;
      }
    }
  }
  SM_EXPECT_TRUE(saw_start);
  SM_EXPECT_FALSE(asked.empty());
  SM_EXPECT_EQ(asked.front(), 1);
}
