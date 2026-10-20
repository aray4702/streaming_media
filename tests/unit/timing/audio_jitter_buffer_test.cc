#include "sm/sim/signals.h"
#include "sm/timing/audio_jitter_buffer.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::timing;

namespace {
std::vector<float> Chunk(const std::vector<float>& x, int i, int n = 320) {
  return std::vector<float>(x.begin() + i * n, x.begin() + (i + 1) * n);
}
}  // namespace

SM_TEST(AudioJitterBuffer, PerfectNetworkPlaysNormally) {
  AudioJitterBuffer jb;
  auto x = sim::SpeechLike(16000, 3.0, 1, false);
  std::vector<float> out(160);
  int expands = 0;
  for (int t = 0; t < 2900; t += 10) {
    if (t % 20 == 0) jb.InsertPacket(static_cast<uint32_t>(t / 20 * 320), Chunk(x, t / 20), Timestamp::Millis(t));
    auto op = jb.GetAudio(out, Timestamp::Millis(t));
    if (op == AudioOperation::kExpand) ++expands;
  }
  SM_EXPECT_EQ(expands, 0);
  SM_EXPECT_EQ(jb.stats().concealed_samples, 0u);
  SM_EXPECT_EQ(jb.TargetDelayMs(), 20);
}

SM_TEST(AudioJitterBuffer, LostPacketIsConcealedThenSkipped) {
  AudioJitterBuffer jb;
  auto x = sim::SpeechLike(16000, 2.0, 2, false);
  std::vector<float> out(160);
  for (int t = 0; t < 1900; t += 10) {
    int pkt = t / 20;
    if (t % 20 == 0 && pkt != 40) jb.InsertPacket(static_cast<uint32_t>(pkt * 320), Chunk(x, pkt), Timestamp::Millis(t));
    jb.GetAudio(out, Timestamp::Millis(t));
  }
  SM_EXPECT_EQ(jb.stats().concealment_events, 1u);
  SM_EXPECT_EQ(jb.stats().packets_lost, 1u);
  SM_EXPECT_GE(jb.stats().ops[static_cast<int>(AudioOperation::kMerge)], 1u);
  SM_EXPECT_NEAR(static_cast<double>(jb.stats().concealed_samples), 320, 160);
}

SM_TEST(AudioJitterBuffer, LatePacketMergesInsteadOfLoss) {
  AudioJitterBuffer jb;
  auto x = sim::Sine(200, 16000, 2.0, 0.3f);
  std::vector<float> out(160);
  for (int t = 0; t < 1900; t += 10) {
    int pkt = t / 20;
    if (t % 20 == 0 && pkt != 40) jb.InsertPacket(static_cast<uint32_t>(pkt * 320), Chunk(x, pkt), Timestamp::Millis(t));
    // 10 ms late: arrives while the buffer is concealing its slot, so it is
    // merged rather than lost. (Later than the concealed duration -> lost.)
    if (t == 810) jb.InsertPacket(40 * 320, Chunk(x, 40), Timestamp::Millis(t));
    jb.GetAudio(out, Timestamp::Millis(t));
  }
  SM_EXPECT_EQ(jb.stats().packets_lost, 0u);
  SM_EXPECT_EQ(jb.stats().packets_discarded_late, 0u);
  SM_EXPECT_GE(jb.stats().ops[static_cast<int>(AudioOperation::kMerge)], 1u);
}

SM_TEST(AudioJitterBuffer, BurstArrivalTriggersAccelerate) {
  AudioJitterBuffer jb;
  auto x = sim::Sine(180, 16000, 12.0, 0.3f);
  std::vector<float> out(160);
  int pkt = 0;
  for (int t = 0; t < 11000; t += 10) {
    // Network stalls for 400 ms at t=3s, then delivers the backlog at once.
    bool stalled = t >= 3000 && t < 3400;
    while (!stalled && pkt * 20 <= t) {
      jb.InsertPacket(static_cast<uint32_t>(pkt * 320), Chunk(x, pkt), Timestamp::Millis(t));
      ++pkt;
    }
    jb.GetAudio(out, Timestamp::Millis(t));
  }
  SM_EXPECT_GT(jb.stats().removed_samples_for_acceleration, 0u);
  SM_EXPECT_LT(jb.CurrentLevelMs(), 200.0);
  auto m = CollectMetrics(jb);
  SM_EXPECT_GT(m["op_accelerate"], 0.0);
  SM_EXPECT_GT(m["jitter_buffer_delay_ms.count"], 0.0);
}

SM_TEST(AudioJitterBuffer, MinimumDelayHonoured) {
  AudioJitterBuffer jb;
  jb.SetMinimumDelayMs(120);
  SM_EXPECT_EQ(jb.TargetDelayMs(), 120);
}

// PacketBuffer::InsertPacket (neteq/packet_buffer.cc) with Packet::Priority
// (neteq/packet.h): one packet per timestamp, the higher priority kept.
SM_TEST(AudioJitterBuffer, V2KeepsTheHigherPriorityPacketPerTimestamp) {
  AudioJitterBufferConfig cfg;
  cfg.version = 2;
  AudioJitterBuffer jb(cfg);
  const std::vector<float> fec(320, 0.25f), primary(320, 0.5f), red(320, 0.75f);
  jb.InsertPacket(0, std::vector<float>(320, 0.1f), Timestamp::Millis(0));
  jb.InsertPacket(320, fec, Timestamp::Millis(5), {1, 0});       // In-band FEC first.
  jb.InsertPacket(320, primary, Timestamp::Millis(20), {0, 0});  // The frame replaces it.
  jb.InsertPacket(320, red, Timestamp::Millis(25), {0, 1});      // A RED copy loses to it.
  jb.InsertPacket(320, fec, Timestamp::Millis(30), {1, 0});      // And so does the FEC again.
  SM_EXPECT_EQ(jb.stats().packets_received, 5u);
  SM_EXPECT_EQ(jb.stats().packets_duplicate, 3u);
  std::vector<float> out(160);
  float last = 0;
  for (int t = 0; t < 4; ++t) {
    jb.GetAudio(out, Timestamp::Millis(40 + 10 * t));
    last = out.back();
  }
  // The second frame played is the primary's (Normal may unmute its start).
  SM_EXPECT_NEAR(last, 0.5f, 1e-6);
  SM_EXPECT_TRUE((AudioPacketPriority{0, 1} < AudioPacketPriority{1, 0}));
  SM_EXPECT_TRUE((AudioPacketPriority{0, 0} < AudioPacketPriority{0, 1}));
}

SM_TEST(AudioJitterBuffer, V1KeepsTheFirstPacketAndLateFecIsNotALateDiscard) {
  AudioJitterBuffer jb;
  jb.InsertPacket(0, std::vector<float>(320, 0.1f), Timestamp::Millis(0));
  jb.InsertPacket(320, std::vector<float>(320, 0.25f), Timestamp::Millis(5), {1, 0});
  jb.InsertPacket(320, std::vector<float>(320, 0.5f), Timestamp::Millis(20), {0, 0});
  SM_EXPECT_EQ(jb.stats().packets_duplicate, 1u);
  std::vector<float> out(160);
  for (int t = 0; t < 10; ++t) jb.GetAudio(out, Timestamp::Millis(40 + 10 * t));
  jb.InsertPacket(0, std::vector<float>(320, 0.1f), Timestamp::Millis(150), {1, 0});
  SM_EXPECT_EQ(jb.stats().packets_discarded_late, 0u);
  jb.InsertPacket(0, std::vector<float>(320, 0.1f), Timestamp::Millis(150));
  SM_EXPECT_EQ(jb.stats().packets_discarded_late, 1u);
}
