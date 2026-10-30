// Ports of libwebrtc's modules/pacing/task_queue_paced_sender_unittest.cc (M157,
// compare/webrtc/reference/) to Pacer v2 with task_queue_scheduling, the port of
// TaskQueuePacedSender's scheduling around the PacingController (#123, #166).
// Each case is named after the one it follows. libwebrtc's
// GlobalSimulatedTimeController is Sim below: it runs the pacer's posted work
// (Process at NextSendTime) until the time it advances to. The test's
// TaskQueuePacedSender arguments map to PacerConfig: max_hold_back_window to
// hold_back_window, max_hold_back_window_in_packets to hold_back_packets
// (kNoPacketHoldback is -1) and PacerConfig's time_window to
// v2.send_burst_interval. Packets are kDefaultPacketSize (1234) bytes, as
// libwebrtc counts them (the pacer is run with SetIncludeOverhead, as
// RtpTransportControllerSend does, so its sizes are wire sizes like ours).
//
// Not ported:
//   - PostedPacketsNotSendFromRemovePacketsForSsrc, second half: the library
//     never removes a stream's packets from the pacer except by a keyframe
//     flush (pacing_controller_test.cc). The first half (5 of 6 packets in
//     400 ms, the oldest has waited 400 ms) is OldestPacketWaitTime below.
//   - Stats: FirstSentPacketTime and ExpectedQueueTime are not exposed by Pacer.
//     The queue size and the oldest packet's wait are checked below.
//   - ChangePacingRateInSentPacketCallstackDoesNotSendNextPacketInSameStack:
//     Pacer returns the packets it sends instead of calling back into the
//     sender, so a rate change cannot happen inside a send. Its intent (the
//     next packet waits for the new rate) is RateChangeAfterASendPacesTheNext.
#include <algorithm>
#include <vector>

#include "sm/cc/pacer.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::cc;

namespace {

constexpr int64_t kDefaultPacketSize = 1234;
const Timestamp kStart = Timestamp::Millis(1234);

PacerConfig Config(TimeDelta hold_back_window = TimeDelta::Millis(1), int hold_back_packets = -1,
                   TimeDelta time_window = TimeDelta::Millis(40)) {
  PacerConfig cfg;
  cfg.task_queue_scheduling = true;
  cfg.hold_back_window = hold_back_window;  // PacingController::kMinSleepTime by default.
  cfg.hold_back_packets = hold_back_packets;
  cfg.controller.send_burst_interval = time_window;
  return cfg;
}

struct Sent {
  PacedPacket p;
  Timestamp t;
};

struct Sim {
  explicit Sim(Pacer& p) : pacer(p) {}
  Pacer& pacer;
  Timestamp now = kStart;
  uint64_t next_id = 1;
  std::vector<Sent> sent;
  std::vector<Timestamp> passes;  // Times at which a Process call sent something.

  void Enqueue(PacketPriority prio, int n) {
    for (int i = 0; i < n; ++i) {
      PacedPacket p;
      p.id = next_id++;
      p.priority = prio;
      p.size = DataSize::Bytes(kDefaultPacketSize);
      p.enqueue_time = now;
      pacer.Enqueue(p);
    }
  }
  // Runs every pass that is due up to now + d; the clock then stands at now + d.
  // `after_pass` runs on the task queue after each pass that sent something.
  template <typename F>
  size_t Advance(TimeDelta d, F after_pass) {
    const size_t before = sent.size();
    const Timestamp end = now + d;
    for (int i = 0; i < 1000000; ++i) {
      const Timestamp next = pacer.NextSendTime(now);
      if (next > end) break;
      now = std::max(now, next);
      std::vector<PacedPacket> out = pacer.Process(now);
      if (!out.empty()) passes.push_back(now);
      for (PacedPacket& p : out) sent.push_back({p, now});
      if (!out.empty()) after_pass();
    }
    now = end;
    return sent.size() - before;
  }
  size_t Advance(TimeDelta d) { return Advance(d, [] {}); }
  size_t ProbeSends(int cluster, size_t from = 0) const {
    size_t n = 0;
    for (size_t i = from; i < sent.size(); ++i) n += sent[i].p.probe_cluster == cluster;
    return n;
  }
};

// TaskQueuePacedSenderTest's GeneratePadding: plain padding packets of at most 224 bytes that
// add up to the target exactly.
void UseTestPadding(Pacer& pacer) {
  auto id = std::make_shared<uint64_t>(1ull << 40);
  pacer.SetPaddingGenerator([id](DataSize target, Timestamp now) {
    std::vector<PacedPacket> out;
    int64_t left = target.bytes();
    while (left > 0) {
      PacedPacket p;
      p.id = (*id)++;
      p.priority = PacketPriority::kPadding;
      p.is_padding = true;
      p.size = DataSize::Bytes(std::min<int64_t>(left, 224));
      p.enqueue_time = now;
      left -= p.size.bytes();
      out.push_back(p);
    }
    return out;
  });
}

ProbeCluster Probe(int id, DataRate rate, int probes, TimeDelta min_probe_delta, Timestamp at) {
  ProbeCluster c;
  c.id = id;
  c.rate = rate;
  c.min_probes = probes;
  c.min_bytes = rate * TimeDelta::Millis(15);  // target_duration
  c.at_time = at;
  c.min_probe_delta = min_probe_delta;
  return c;
}

DataRate BytesPer(int64_t bytes, TimeDelta t) { return DataRate::BitsPerSec(bytes * 8 * 1000000 / t.us()); }
// Time to send `bytes` at `rate`.
TimeDelta TimeFor(int64_t bytes, DataRate rate) { return TimeDelta::Micros(bytes * 8 * 1000000 / rate.bps()); }

}  // namespace

// PacesPackets
SM_TEST(TaskQueuePacedSender, PacesPackets) {
  constexpr int kPacketsToSend = 42;
  Pacer pacer(Config());
  pacer.SetPacingRates(DataRate::BitsPerSec(kDefaultPacketSize * 8 * kPacketsToSend), DataRate::Zero());
  Sim sim(pacer);
  sim.Enqueue(PacketPriority::kVideo, kPacketsToSend);
  SM_EXPECT_EQ(sim.Advance(TimeDelta::Seconds(1)), size_t{kPacketsToSend});
  SM_ASSERT_EQ(sim.sent.size(), size_t{kPacketsToSend});
  SM_EXPECT_NEAR((sim.sent.back().t - kStart).ms(), 1000, 50);
}

// PacesPacketsWithBurst: half a second of burst sends a second's worth in about 0.5 s.
SM_TEST(TaskQueuePacedSender, PacesPacketsWithBurst) {
  constexpr int kPacketsToSend = 42;
  Pacer pacer(Config(TimeDelta::Millis(1), -1, TimeDelta::Millis(500)));
  pacer.SetPacingRates(DataRate::BitsPerSec(kDefaultPacketSize * 8 * kPacketsToSend), DataRate::Zero());
  Sim sim(pacer);
  sim.Enqueue(PacketPriority::kVideo, kPacketsToSend);
  SM_EXPECT_EQ(sim.Advance(TimeDelta::Seconds(1)), size_t{kPacketsToSend});
  SM_ASSERT_EQ(sim.sent.size(), size_t{kPacketsToSend});
  SM_EXPECT_NEAR((sim.sent.back().t - kStart).ms(), 500, 50);
}

// ReschedulesProcessOnRateChange
SM_TEST(TaskQueuePacedSender, ReschedulesProcessOnRateChange) {
  constexpr int kPacketsPerSecond = 5;
  const DataRate kPacingRate = DataRate::BitsPerSec(kDefaultPacketSize * 8 * kPacketsPerSecond);
  Pacer pacer(Config());
  pacer.SetPacingRates(kPacingRate, DataRate::Zero());
  Sim sim(pacer);
  sim.Enqueue(PacketPriority::kVideo, kPacketsPerSecond);
  SM_EXPECT_EQ(sim.Advance(TimeDelta::Seconds(1)), size_t{kPacketsPerSecond});

  // After the second packet, double the rate: the third leaves after half the wait.
  const size_t first = sim.sent.size();
  sim.Enqueue(PacketPriority::kVideo, 3);
  SM_EXPECT_EQ(sim.Advance(TimeDelta::Millis(500), [&] {
    if (sim.sent.size() == first + 2) pacer.SetPacingRates(kPacingRate * 2, DataRate::Zero());
  }), 3u);
  SM_ASSERT_EQ(sim.sent.size(), first + 3);
  SM_EXPECT_NEAR((sim.sent[first + 1].t - sim.sent[first].t).ms(), 200, 1);
  SM_EXPECT_NEAR((sim.sent[first + 2].t - sim.sent[first + 1].t).ms(), 100, 1);
}

// SendsAudioImmediately
SM_TEST(TaskQueuePacedSender, SendsAudioImmediately) {
  Pacer pacer(Config());
  pacer.SetPacingRates(DataRate::KilobitsPerSec(125), DataRate::Zero());
  Sim sim(pacer);
  sim.Enqueue(PacketPriority::kVideo, 10);
  SM_EXPECT_LE(sim.Advance(TimeDelta::Zero()), 9u);
  sim.Enqueue(PacketPriority::kAudio, 1);
  const size_t from = sim.sent.size();
  SM_EXPECT_EQ(sim.Advance(TimeDelta::Zero()), 1u);
  SM_ASSERT_EQ(sim.sent.size(), from + 1);
  SM_EXPECT_TRUE(sim.sent[from].p.priority == PacketPriority::kAudio);
}

// SleepsDuringCoalscingWindow
SM_TEST(TaskQueuePacedSender, SleepsDuringCoalescingWindow) {
  const TimeDelta kCoalescingWindow = TimeDelta::Millis(5);
  Pacer pacer(Config(kCoalescingWindow, -1, TimeDelta::Zero()));
  pacer.SetPacingRates(BytesPer(kDefaultPacketSize, TimeDelta::Millis(1)), DataRate::Zero());  // 1 ms per packet.
  Sim sim(pacer);
  sim.Enqueue(PacketPriority::kVideo, 10);
  SM_EXPECT_LE(sim.Advance(TimeDelta::Zero()), 9u);
  // Nothing until the coalescing window ends, then what was due by then.
  SM_EXPECT_EQ(sim.Advance(kCoalescingWindow - TimeDelta::Millis(1)), 0u);
  SM_EXPECT_EQ(sim.Advance(TimeDelta::Millis(1)), 5u);
}

// ProbingOverridesCoalescingWindow
SM_TEST(TaskQueuePacedSender, ProbingOverridesCoalescingWindow) {
  const TimeDelta kCoalescingWindow = TimeDelta::Millis(5);
  const DataRate kPacingDataRate = BytesPer(kDefaultPacketSize, TimeDelta::Millis(1));
  Pacer pacer(Config(kCoalescingWindow));
  pacer.SetPacingRates(kPacingDataRate, DataRate::Zero());
  UseTestPadding(pacer);
  Sim sim(pacer);
  pacer.AddProbeCluster(Probe(17, kPacingDataRate * 2, 5, TimeDelta::Millis(2), sim.now));
  sim.Enqueue(PacketPriority::kVideo, 10);
  SM_EXPECT_GE(sim.Advance(TimeDelta::Zero()), 1u);
  // Packets are flying before the coalescing window ends.
  SM_EXPECT_GE(sim.Advance(kCoalescingWindow - TimeDelta::Millis(1)), 1u);
}

// SchedulesProbeAtSentTime (WebRTC-Bwe-ProbingBehavior/min_probe_delta:1ms/)
SM_TEST(TaskQueuePacedSender, SchedulesProbeAtSentTime) {
  const DataRate kPacingDataRate = BytesPer(kDefaultPacketSize, TimeDelta::Millis(4));  // 4 ms per packet.
  Pacer pacer(Config(TimeDelta::Millis(1), -1, TimeDelta::Zero()));
  pacer.SetPacingRates(kPacingDataRate, DataRate::Zero());
  UseTestPadding(pacer);
  Sim sim(pacer);
  // Two packets: the first leaves now, the next is due in 4 ms.
  sim.Enqueue(PacketPriority::kVideo, 2);
  SM_EXPECT_EQ(sim.Advance(TimeDelta::Micros(1001)), 1u);
  SM_EXPECT_EQ(sim.ProbeSends(-1), 1u);

  // A probe at twice the rate, with the packets it needs. Each probe is the rate times 2 ms,
  // plus the small padding packet a probe starts with.
  const DataRate kProbeRate = kPacingDataRate * 2;
  pacer.AddProbeCluster(Probe(1, kProbeRate, 4, TimeDelta::Millis(1), sim.now));
  const int64_t kProbeSize = (kProbeRate * TimeDelta::Millis(2)).bytes();
  const int kNumPacketsInProbe = static_cast<int>((kProbeSize + kDefaultPacketSize - 1) / kDefaultPacketSize);
  sim.Enqueue(PacketPriority::kVideo, kNumPacketsInProbe);
  size_t from = sim.sent.size();
  sim.Advance(TimeDelta::Zero());
  SM_EXPECT_EQ(sim.ProbeSends(1, from), size_t(kNumPacketsInProbe + 1));

  // The next probe is due in 2 ms; the timer set before it for the paced packet must not delay it.
  from = sim.sent.size();
  sim.Advance(TimeDelta::Millis(2));
  SM_EXPECT_GE(sim.ProbeSends(1, from), 1u);
}

// NoMinSleepTimeWhenProbing: probes 200 us apart are not held to the 1 ms minimum sleep.
SM_TEST(TaskQueuePacedSender, NoMinSleepTimeWhenProbing) {
  const TimeDelta kMinProbeDelta = TimeDelta::Micros(200);
  const DataRate kPacingDataRate = BytesPer(kDefaultPacketSize, TimeDelta::Millis(4));
  Pacer pacer(Config());
  pacer.SetPacingRates(kPacingDataRate, DataRate::Zero());
  UseTestPadding(pacer);
  Sim sim(pacer);
  const DataRate kProbingRate = kPacingDataRate * 10;
  pacer.AddProbeCluster(Probe(1, kProbingRate, 5, kMinProbeDelta, sim.now));
  // One packet starts the probe; the rest is padding.
  sim.Enqueue(PacketPriority::kVideo, 1);
  sim.Advance(kMinProbeDelta);
  SM_EXPECT_GE(sim.ProbeSends(1), 4u);
  int64_t data_sent = 0;
  for (const Sent& s : sim.sent)
    if (s.p.probe_cluster == 1) data_sent += s.p.size.bytes();
  const int64_t kMinProbeSize = (kProbingRate * kMinProbeDelta).bytes();
  SM_EXPECT_EQ(data_sent, 1 + kDefaultPacketSize + 4 * kMinProbeSize);
}

// PacketBasedCoalescing
SM_TEST(TaskQueuePacedSender, PacketBasedCoalescing) {
  const TimeDelta kFixedCoalescingWindow = TimeDelta::Millis(10);
  constexpr int kPacketBasedHoldback = 5;
  const TimeDelta kPacketPacingTime = TimeDelta::Millis(1);
  const TimeDelta kExpectedHoldbackWindow = kPacketPacingTime * kPacketBasedHoldback;
  Pacer pacer(Config(kFixedCoalescingWindow, kPacketBasedHoldback, TimeDelta::Zero()));
  pacer.SetPacingRates(BytesPer(kDefaultPacketSize, kPacketPacingTime), DataRate::Zero());
  Sim sim(pacer);
  // Warm up the packet size estimate.
  constexpr int kNumWarmupPackets = 40;
  sim.Enqueue(PacketPriority::kVideo, kNumWarmupPackets);
  SM_EXPECT_EQ(sim.Advance(kPacketPacingTime * (kNumWarmupPackets * 2)), size_t{kNumWarmupPackets});
  // Only the first leaves now; the rest wait for the packet-based window, not the fixed one.
  sim.Enqueue(PacketPriority::kVideo, kPacketBasedHoldback);
  SM_EXPECT_EQ(sim.Advance(TimeDelta::Zero()), 1u);
  SM_EXPECT_EQ(sim.Advance(kExpectedHoldbackWindow - TimeDelta::Millis(1)), 0u);
  SM_EXPECT_EQ(sim.Advance(TimeDelta::Millis(1)), size_t{kPacketBasedHoldback - 1});
}

// FixedHoldBackHasPriorityOverPackets
SM_TEST(TaskQueuePacedSender, FixedHoldBackHasPriorityOverPackets) {
  const TimeDelta kFixedCoalescingWindow = TimeDelta::Millis(2);
  constexpr int kPacketBasedHoldback = 5;
  const TimeDelta kPacketPacingTime = TimeDelta::Millis(1);
  Pacer pacer(Config(kFixedCoalescingWindow, kPacketBasedHoldback, TimeDelta::Zero()));
  pacer.SetPacingRates(BytesPer(kDefaultPacketSize, kPacketPacingTime), DataRate::Zero());
  Sim sim(pacer);
  constexpr int kNumWarmupPackets = 40;
  sim.Enqueue(PacketPriority::kVideo, kNumWarmupPackets);
  SM_EXPECT_EQ(sim.Advance(kPacketPacingTime * (kNumWarmupPackets * 2)), size_t{kNumWarmupPackets});
  sim.Enqueue(PacketPriority::kVideo, kPacketBasedHoldback);
  SM_EXPECT_EQ(sim.Advance(TimeDelta::Zero()), 1u);
  // The fixed window is the shorter one, so some packets leave when it ends.
  SM_EXPECT_GE(sim.Advance(kFixedCoalescingWindow), 1u);
}

// ProbingStopDuringSendLoop (WebRTC-Bwe-ProbingBehavior/min_probe_delta:100us/)
SM_TEST(TaskQueuePacedSender, ProbingStopDuringSendLoop) {
  const DataRate kPacingDataRate = BytesPer(2 * kDefaultPacketSize, TimeDelta::Millis(1));
  Pacer pacer(Config());
  pacer.SetPacingRates(kPacingDataRate, DataRate::Zero());
  UseTestPadding(pacer);
  Sim sim(pacer);
  pacer.AddProbeCluster(Probe(1, kPacingDataRate, 4, TimeDelta::Micros(100), sim.now));
  constexpr int kPacketsToSend = 100;
  const TimeDelta kPacketsPacedTime = TimeFor(kPacketsToSend * kDefaultPacketSize, kPacingDataRate);
  // Every packet and one padding packet (the probe's start).
  sim.Enqueue(PacketPriority::kVideo, kPacketsToSend);
  SM_EXPECT_EQ(sim.Advance(kPacketsPacedTime + TimeDelta::Millis(1)), size_t{kPacketsToSend + 1});
}

// PostedPacketsNotSendFromRemovePacketsForSsrc, first half: 10 packets a second, 5 of 6 leave
// in 400 ms and the last has waited 400 ms.
SM_TEST(TaskQueuePacedSender, OldestPacketWaitTime) {
  Pacer pacer(Config());
  pacer.SetPacingRates(BytesPer(kDefaultPacketSize * 10, TimeDelta::Seconds(1)), DataRate::Zero());
  Sim sim(pacer);
  sim.Enqueue(PacketPriority::kVideo, 6);
  SM_EXPECT_EQ(sim.Advance(TimeDelta::Millis(400)), 5u);
  SM_EXPECT_EQ(pacer.OldestQueueTime(sim.now).ms(), 400);
}

// Stats: queue size and oldest wait while a second of packets drains.
SM_TEST(TaskQueuePacedSender, Stats) {
  constexpr int kPacketsToSend = 200;
  const DataRate kPacingRate = BytesPer(kDefaultPacketSize * kPacketsToSend, TimeDelta::Seconds(1));
  Pacer pacer(Config());
  pacer.SetPacingRates(kPacingRate, DataRate::Zero());
  Sim sim(pacer);
  // One packet plus the burst interval (PacerConfig::kDefaultTimeInterval).
  const int64_t kAllowedQueueSizeDeviation =
      kDefaultPacketSize + (kPacingRate * TimeDelta::Millis(40)).bytes();
  const TimeDelta kAllowedQueueTimeDeviation = TimeFor(kAllowedQueueSizeDeviation, kPacingRate);

  SM_EXPECT_EQ(pacer.OldestQueueTime(sim.now).us(), 0);
  SM_EXPECT_EQ(pacer.QueueSize().bytes(), 0);
  sim.Enqueue(PacketPriority::kVideo, kPacketsToSend);

  sim.Advance(TimeDelta::Millis(200));
  SM_EXPECT_EQ(pacer.OldestQueueTime(sim.now).ms(), 200);
  SM_EXPECT_NEAR(pacer.QueueSize().bytes(), (kPacingRate * TimeDelta::Millis(800)).bytes(), kAllowedQueueSizeDeviation);

  sim.Advance(TimeDelta::Millis(300));
  SM_EXPECT_EQ(pacer.OldestQueueTime(sim.now).ms(), 500);
  SM_EXPECT_NEAR(pacer.QueueSize().bytes(), (kPacingRate * TimeDelta::Millis(500)).bytes(), kAllowedQueueSizeDeviation);

  sim.Advance(TimeDelta::Millis(500) + kAllowedQueueTimeDeviation);
  SM_EXPECT_EQ(sim.sent.size(), size_t{kPacketsToSend});
  SM_EXPECT_EQ(pacer.OldestQueueTime(sim.now).us(), 0);
  SM_EXPECT_EQ(pacer.QueueSize().bytes(), 0);
}

// ChangePacingRateInSentPacketCallstackDoesNotSendNextPacketInSameStack, as far as it applies:
// a rate change after a send paces the next packet at the new rate, in a later pass.
SM_TEST(TaskQueuePacedSender, RateChangeAfterASendPacesTheNext) {
  Pacer pacer(Config(TimeDelta::Millis(1), -1, TimeDelta::Zero()));
  pacer.SetPacingRates(DataRate::KilobitsPerSec(500), DataRate::Zero());
  Sim sim(pacer);
  sim.Enqueue(PacketPriority::kVideo, 2);
  SM_EXPECT_EQ(sim.Advance(TimeDelta::Millis(10), [&] { pacer.SetPacingRates(DataRate::KilobitsPerSec(1000), DataRate::Zero()); }),
               2u);
  SM_ASSERT_EQ(sim.passes.size(), 2u);
}

// MaybeProcessPackets sizes the packet-based hold back by pacing_controller_.pacing_rate(), which is
// adjusted_media_rate_: the rate after the long-queue boost (pacing_controller.h:146). A queue far
// beyond queue_time_limit raises that rate to drain it, so the hold back shrinks with it; sized by
// the configured rate it would stay at the full 5 ms window and the pacer would wake half as often.
SM_TEST(TaskQueuePacedSender, HoldBackFollowsTheBoostedRate) {
  // RtpTransportControllerSend's hold back: 5 ms, 3 packets.
  Pacer pacer(Config(TimeDelta::Millis(5), 3, TimeDelta::Zero()));
  pacer.SetPacingRates(BytesPer(kDefaultPacketSize, TimeDelta::Millis(10)), DataRate::Zero());  // 3 packets: 30 ms.
  Sim sim(pacer);
  // 4000 packets is 40 s at the configured rate: drained in 2 s, a packet every 0.5 ms.
  sim.Enqueue(PacketPriority::kVideo, 4000);
  sim.Advance(TimeDelta::Millis(200));
  SM_ASSERT_GT(sim.passes.size(), 10u);
  TimeDelta max_gap = TimeDelta::Zero();
  for (size_t i = 5; i < sim.passes.size(); ++i) max_gap = std::max(max_gap, sim.passes[i] - sim.passes[i - 1]);
  // 3 packets at the boosted rate (1.5 ms), rounded up to the 1 ms timer.
  SM_EXPECT_LE(max_gap.us(), 2000);
}

// SetConfig, Resume and SetCongested post MaybeProcessPackets through PostMaybeProcessPackets, which posts
// nothing while the queue is empty (task_queue_paced_sender.cc:217-225). A padding rate set on an empty
// queue therefore waits for the next enqueued packet (or the pending timer): it does not start padding at
// once. Issue #813: audio-only calls padded from the GoogCC update itself, 15 ms before libwebrtc.
SM_TEST(TaskQueuePacedSender, PaddingRateOnAnEmptyQueueWaitsForTheNextPacket) {
  Pacer pacer(Config(TimeDelta::Millis(5), 3));  // RtpTransportControllerSend's hold back.
  UseTestPadding(pacer);
  pacer.SetPacingRates(DataRate::KilobitsPerSec(335), DataRate::Zero());
  Sim sim(pacer);
  sim.Enqueue(PacketPriority::kAudio, 1);
  SM_EXPECT_EQ(sim.Advance(TimeDelta::Millis(10)), 1u);

  pacer.SetPacingRates(DataRate::KilobitsPerSec(335), DataRate::KilobitsPerSec(300));
  SM_EXPECT_GT(pacer.NextSendTime(sim.now), sim.now);
  SM_EXPECT_EQ(sim.Advance(TimeDelta::Millis(10)), 0u);

  // The next packet runs a pass: it goes first, and padding follows once its debt (1234 B at 335 kbps,
  // 29.5 ms) has drained.
  const Timestamp enqueued = sim.now;
  const size_t from = sim.sent.size();
  sim.Enqueue(PacketPriority::kAudio, 1);
  SM_EXPECT_GT(sim.Advance(TimeDelta::Millis(50)), 1u);
  SM_ASSERT_GT(sim.sent.size(), from + 1);
  SM_EXPECT_TRUE(sim.sent[from].p.priority == PacketPriority::kAudio);
  SM_EXPECT_EQ(sim.sent[from].t.us(), enqueued.us());
  SM_EXPECT_TRUE(sim.sent[from + 1].p.is_padding);
  SM_EXPECT_GE((sim.sent[from + 1].t - enqueued).us(), 29000);
}

// With packets queued, a rate change still runs a pass at once (PostMaybeProcessPackets posts it).
SM_TEST(TaskQueuePacedSender, RateChangeWithPacketsQueuedRunsAPass) {
  Pacer pacer(Config(TimeDelta::Millis(5), 3));
  pacer.SetPacingRates(DataRate::KilobitsPerSec(100), DataRate::Zero());
  Sim sim(pacer);
  sim.Enqueue(PacketPriority::kVideo, 3);
  sim.Advance(TimeDelta::Zero());
  SM_ASSERT_GT(pacer.QueuePackets(), 0u);
  SM_EXPECT_GT(pacer.NextSendTime(sim.now), sim.now);
  pacer.SetPacingRates(DataRate::KilobitsPerSec(200), DataRate::Zero());
  SM_EXPECT_EQ(pacer.NextSendTime(sim.now).us(), sim.now.us());
}

// TaskQueuePacedSender::Pause posts nothing.
SM_TEST(TaskQueuePacedSender, PauseRunsNoPass) {
  Pacer pacer(Config(TimeDelta::Millis(5), 3));
  pacer.SetPacingRates(DataRate::KilobitsPerSec(100), DataRate::Zero());
  Sim sim(pacer);
  sim.Enqueue(PacketPriority::kVideo, 3);
  sim.Advance(TimeDelta::Zero());
  const Timestamp next = pacer.NextSendTime(sim.now);
  SM_ASSERT_GT(next, sim.now);
  pacer.Pause();
  SM_EXPECT_EQ(pacer.NextSendTime(sim.now).us(), next.us());
}
