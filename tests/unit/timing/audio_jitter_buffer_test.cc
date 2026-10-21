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

// NetEq::GetPlayoutTimestamp (issue #63): unset until playout starts, then the
// RTP time of the next sample to play, moving on by one output block per pull
// (dead reckoning through an expand) and never backwards.
namespace {
void CheckPlayoutTimestamp(int version) {
  AudioJitterBufferConfig cfg;
  cfg.version = version;
  AudioJitterBuffer jb(cfg);
  auto x = sim::SpeechLike(16000, 3.0, 3, false);
  std::vector<float> out(160);
  SM_EXPECT_FALSE(jb.PlayoutTimestamp().has_value());
  std::optional<uint32_t> prev;
  int steps = 0, backwards = 0, expand_steps = 0;
  for (int t = 0; t < 2900; t += 10) {
    int pkt = t / 20;
    if (t % 20 == 0 && pkt != 60 && pkt != 61) {
      jb.InsertPacket(static_cast<uint32_t>(pkt * 320), Chunk(x, pkt), Timestamp::Millis(t));
    }
    auto op = jb.GetAudio(out, Timestamp::Millis(t));
    auto ts = jb.PlayoutTimestamp();
    if (prev && ts) {
      int32_t step = static_cast<int32_t>(*ts - *prev);
      if (step < 0) ++backwards;
      if (op == AudioOperation::kExpand) {
        ++expand_steps;
        SM_EXPECT_EQ(step, 160);
      }
      if (op == AudioOperation::kNormal && step == 160) ++steps;
    }
    if (ts) prev = ts;
  }
  SM_EXPECT_TRUE(prev.has_value());
  SM_EXPECT_EQ(backwards, 0);
  SM_EXPECT_GE(expand_steps, 2);
  SM_EXPECT_GE(steps, 200);
  // The audio playing at the end lags the last packet's capture by about the buffer delay.
  SM_EXPECT_LE(static_cast<int64_t>(*prev), 2900 * 16);
  SM_EXPECT_GE(static_cast<int64_t>(*prev), (2900 - 200) * 16);
}
}  // namespace

SM_TEST(AudioJitterBuffer, PlayoutTimestampFollowsPlayout) { CheckPlayoutTimestamp(1); }
SM_TEST(AudioJitterBufferNetEqV2, PlayoutTimestampFollowsPlayout) { CheckPlayoutTimestamp(2); }

// StatisticsCalculator::JitterBufferDelay: a packet counts the whole 10 ms
// ticks it waited in the packet buffer, weighted by its samples; the decoded
// audio waiting in the sync buffer is not counted.
SM_TEST(AudioJitterBuffer, JitterBufferDelayCountsPacketWaitingTicks) {
  AudioJitterBufferConfig cfg;
  cfg.version = 2;
  cfg.time_stretch = false;
  AudioJitterBuffer jb(cfg);
  auto x = sim::SpeechLike(16000, 1.0, 3, false);
  std::vector<float> out(160);
  // Two packets arrive together before the first pull.
  jb.InsertPacket(0, Chunk(x, 0), Timestamp::Millis(0));
  jb.InsertPacket(320, Chunk(x, 1), Timestamp::Millis(0));
  jb.GetAudio(out, Timestamp::Millis(0));
  SM_EXPECT_EQ(jb.stats().jitter_buffer_emitted_count, 320u);
  SM_EXPECT_EQ(jb.stats().jitter_buffer_delay_ms_samples, 10u * 320u);
  // The second packet is extracted by the third pull, once the sync buffer
  // holds less than 10 ms beyond Expand's overlap: it waited 3 ticks.
  for (int t = 10; t < 40; t += 10) jb.GetAudio(out, Timestamp::Millis(t));
  SM_EXPECT_EQ(jb.stats().jitter_buffer_emitted_count, 640u);
  SM_EXPECT_EQ(jb.stats().jitter_buffer_delay_ms_samples, 10u * 320u + 30u * 320u);
}
