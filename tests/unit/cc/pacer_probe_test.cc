#include "sm/cc/pacer.h"
#include "sm/cc/probing.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::cc;

SM_TEST(Pacer, RespectsPacingRate) {
  Pacer p;
  p.SetPacingRates(DataRate::KilobitsPerSec(800), DataRate::Zero());
  // 150 kB drains in 1.5 s at 800 kbps: inside the 2 s queue time limit, so
  // the pacer must not boost its rate.
  for (uint64_t i = 0; i < 150; ++i) p.Enqueue({i, PacketPriority::kVideo, DataSize::Bytes(1000), Timestamp::Millis(0)});
  int64_t bytes = 0;
  for (int t = 0; t <= 1000; ++t) {
    for (auto& s : p.Process(Timestamp::Millis(t))) bytes += s.size.bytes();
  }
  // 800 kbps for 1 s = 100 kB, plus PacingController's 40 ms send burst
  // interval at the start (4 kB).
  SM_EXPECT_NEAR(static_cast<double>(bytes), 104000, 1000);
}

SM_TEST(Pacer, PriorityOrderAndAudioBypass) {
  Pacer p;
  p.SetPacingRates(DataRate::KilobitsPerSec(100), DataRate::Zero());
  Timestamp t0 = Timestamp::Millis(0);
  for (uint64_t i = 0; i < 20; ++i) p.Enqueue({i, PacketPriority::kVideo, DataSize::Bytes(1000), t0});
  p.Process(t0);  // Spends the budget on the first video packets.
  // Queued behind 18+ video packets: a retransmission and an audio packet.
  p.Enqueue({100, PacketPriority::kRetransmission, DataSize::Bytes(1000), Timestamp::Millis(1)});
  p.Enqueue({200, PacketPriority::kAudio, DataSize::Bytes(100), Timestamp::Millis(1)});
  auto sent = p.Process(Timestamp::Millis(1));
  SM_ASSERT_GE(sent.size(), 1u);
  SM_EXPECT_EQ(sent[0].id, 200u);  // Audio goes out despite the debt.
  // The next paced packet is the retransmission, ahead of older video.
  uint64_t next = 0;
  for (int t = 2; t < 2000 && next == 0; ++t) {
    auto out = p.Process(Timestamp::Millis(t));
    if (!out.empty()) next = out[0].id;
  }
  SM_EXPECT_EQ(next, 100u);
}

SM_TEST(Pacer, PauseHoldsVideo) {
  Pacer p;
  p.SetPacingRates(DataRate::KilobitsPerSec(1000), DataRate::Zero());
  p.Pause();
  p.Enqueue({1, PacketPriority::kVideo, DataSize::Bytes(500), Timestamp::Millis(0)});
  SM_EXPECT_TRUE(p.Process(Timestamp::Millis(10)).empty());
  p.Resume();
  SM_EXPECT_EQ(p.Process(Timestamp::Millis(20)).size(), 1u);
  SM_EXPECT_EQ(CollectMetrics(p)["sent_packets_video"], 1.0);
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
