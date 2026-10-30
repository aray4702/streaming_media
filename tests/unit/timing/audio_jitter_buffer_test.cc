#include "sm/sim/signals.h"
#include "sm/timing/audio_jitter_buffer.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::timing;

namespace {
std::vector<float> Chunk(const std::vector<float>& x, int i, int n = 320) {
  return std::vector<float>(x.begin() + i * n, x.begin() + (i + 1) * n);
}

// The library's own buffer (version 1); the default is NetEq (version 2, #579).
AudioJitterBufferConfig V1() {
  AudioJitterBufferConfig c;
  c.version = 1;
  return c;
}
}  // namespace

SM_TEST(AudioJitterBuffer, PerfectNetworkPlaysNormally) {
  AudioJitterBuffer jb(V1());
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
  AudioJitterBuffer jb(V1());
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
  AudioJitterBuffer jb(V1());
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
  AudioJitterBuffer jb(V1());
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
  AudioJitterBuffer jb(V1());
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

// Issue #331: PacketBuffer::InsertPacket flushes a full buffer completely
// before inserting, and NetEq::Config::max_packets_in_buffer is 200 (4 s of
// 20 ms packets, the capacity DelayConstraints caps the minimum delay by).
// A/V sync on a slow link raises the audio delay past 2 s without a flush.
SM_TEST(AudioJitterBuffer, V2HoldsTwoHundredPacketsThenFlushesThemAll) {
  AudioJitterBufferConfig cfg;
  cfg.version = 2;
  AudioJitterBuffer jb(cfg);
  for (int i = 0; i < 200; ++i) {
    jb.InsertPacket(static_cast<uint32_t>(i * 320), std::vector<float>(320, 0.1f), Timestamp::Millis(i));
  }
  SM_EXPECT_EQ(jb.stats().buffer_flushes, 0u);
  // The 201st packet finds the buffer full: everything before it goes.
  jb.InsertPacket(200 * 320, std::vector<float>(320, 0.5f), Timestamp::Millis(200));
  SM_EXPECT_EQ(jb.stats().buffer_flushes, 1u);
  jb.InsertPacket(201 * 320, std::vector<float>(320, 0.5f), Timestamp::Millis(201));
  SM_EXPECT_EQ(jb.stats().buffer_flushes, 1u);
  // Playout starts at the packet that came in on the flush.
  std::vector<float> out(160);
  jb.GetAudio(out, Timestamp::Millis(210));
  SM_ASSERT_TRUE(jb.PlayoutTimestamp().has_value());
  SM_EXPECT_GE(*jb.PlayoutTimestamp(), 200u * 320u);
}

// Issue #773: PacketBuffer::kFlushed sets NetEqImpl::new_codec_, so the next
// decision restarts the timestamp at the next packet and soft-resets the
// controller (DelayManager::Reset: target back to kStartDelayMs, 80 ms), and
// the packets in between skip the statistics (neteq_impl.cc:369-389, 1034-1061).
SM_TEST(AudioJitterBuffer, V2FullBufferFlushSoftResetsTheDelayManagerAtTheNextDecision) {
  AudioJitterBufferConfig cfg;
  cfg.version = 2;
  AudioJitterBuffer jb(cfg);
  std::vector<float> out(160);
  const auto pcm = [](int i) { return std::vector<float>(320, i % 2 ? 0.25f : -0.25f); };
  // 4 s of audio arriving in bursts of 10 packets every 200 ms: the target rises.
  int pkt = 0;
  int t = 0;
  for (; t < 4000; t += 10) {
    if (t % 200 == 0) {
      for (int k = 0; k < 10; ++k, ++pkt) jb.InsertPacket(static_cast<uint32_t>(pkt * 320), pcm(pkt), Timestamp::Millis(t));
    }
    jb.GetAudio(out, Timestamp::Millis(t));
  }
  const int raised = jb.TargetDelayMs();
  SM_EXPECT_GT(raised, 80);
  // 4 s more at once, without a pull: the buffer fills and is flushed.
  const int first_after = pkt + 210;
  for (int k = 0; k < 220; ++k, ++pkt) jb.InsertPacket(static_cast<uint32_t>(pkt * 320), pcm(pkt), Timestamp::Millis(t));
  SM_EXPECT_EQ(jb.stats().buffer_flushes, 1u);
  // Nothing is reset before the next decision.
  SM_EXPECT_EQ(jb.TargetDelayMs(), raised);
  // The pulls that still play decoded audio from the sync buffer return before
  // the reset (neteq_impl.cc:1019-1025); the first decision after them resets.
  int pulls = 0;
  while (jb.TargetDelayMs() != 80 && pulls < 50) {
    jb.GetAudio(out, Timestamp::Millis(t));
    t += 10;
    ++pulls;
  }
  SM_EXPECT_EQ(jb.TargetDelayMs(), 80);
  SM_EXPECT_LE(pulls, 30);  // The sync buffer holds at most the old target's audio.
  // Playout restarts at the packets kept after the flush.
  SM_ASSERT_TRUE(jb.PlayoutTimestamp().has_value());
  SM_EXPECT_GE(*jb.PlayoutTimestamp(), static_cast<uint32_t>((first_after - 10) * 320));
  // The packets that came after the flush, before the pull, left no history:
  // the next on-time packets keep the target at the start value.
  for (int k = 0; k < 5; ++k, t += 10) jb.GetAudio(out, Timestamp::Millis(t));
  SM_EXPECT_EQ(jb.TargetDelayMs(), 80);
}

SM_TEST(AudioJitterBuffer, V1KeepsTheFirstPacketAndLateFecIsNotALateDiscard) {
  AudioJitterBuffer jb(V1());
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

// #256: NetEq's expand indexed past its history at rates other than 8, 16, 32
// and 48 kHz. As libwebrtc, where no decoder exists at such a rate, version 2
// discards those packets and plays silence; version 1 runs at any rate.
SM_TEST(AudioJitterBuffer, NetEqDiscardsPacketsAtUnsupportedRates) {
  for (int rate : {4000, 11025, 12000, 22050, 24000, 44100, 8000, 16000, 32000, 48000}) {
    AudioJitterBufferConfig c;
    c.version = 2;
    c.sample_rate = rate;
    AudioJitterBuffer jb(c);
    std::vector<float> out(static_cast<size_t>(rate / 100));
    // 1 s of 20 ms packets, then 2 s of concealment once they run out.
    for (int t = 0; t < 3000; t += 10) {
      const Timestamp now = Timestamp::Millis(t);
      if (t % 20 == 0 && t < 1000) {
        jb.InsertPacket(static_cast<uint32_t>(int64_t{t} * rate / 1000), std::vector<float>(static_cast<size_t>(rate / 50), 0.1f), now);
      }
      jb.GetAudio(out, now);
    }
    const bool supported = NetEqSupportsSampleRate(rate);
    SM_EXPECT_EQ(jb.stats().packets_received, supported ? 50u : 0u);
    SM_EXPECT_EQ(jb.stats().packets_discarded_unsupported_rate, supported ? 0u : 50u);
    if (!supported) {
      for (float s : out) SM_EXPECT_EQ(s, 0.0f);
    }
  }
}

// #568: when the sync buffer can't fill the output (packets under 10 ms),
// NetEqImpl::GetAudioInternal reads nothing from it, mutes the frame and
// returns kSampleUnderrun (neteq_impl.cc, "treatment of under-run"); NetEq's
// statistics don't count the frame. Version 2 plays silence there and keeps
// the decoded audio for the next pull, instead of concealing the remainder.
SM_TEST(AudioJitterBufferNetEqV2, SampleUnderrunMutesAndKeepsTheSyncBuffer) {
  AudioJitterBufferConfig cfg;
  cfg.version = 2;
  cfg.packet_ms = 5;
  cfg.time_stretch = false;
  AudioJitterBuffer jb(cfg);
  std::vector<float> out(160, 1.0f);
  jb.InsertPacket(0, std::vector<float>(80, 0.25f), Timestamp::Millis(0));
  SM_EXPECT_TRUE(jb.GetAudio(out, Timestamp::Millis(0)) == AudioOperation::kSilence);
  for (float s : out) SM_EXPECT_EQ(s, 0.0f);
  SM_EXPECT_EQ(jb.stats().total_samples_received, 0u);
  SM_EXPECT_EQ(jb.stats().concealed_samples, 0u);
  SM_EXPECT_TRUE(!jb.PlayoutTimestamp().has_value());
  // The next packet completes the frame: the first packet's audio is played,
  // after Expand's lookahead of zeros.
  jb.InsertPacket(80, std::vector<float>(80, 0.5f), Timestamp::Millis(10));
  SM_EXPECT_TRUE(jb.GetAudio(out, Timestamp::Millis(10)) == AudioOperation::kNormal);
  size_t first = 0, second = 0;
  for (float s : out) {
    if (s == 0.25f) ++first;
    if (s == 0.5f) ++second;
  }
  SM_EXPECT_EQ(first, 80u);
  SM_EXPECT_GT(second, 0u);
  SM_EXPECT_EQ(jb.stats().total_samples_received, 160u);
  SM_EXPECT_EQ(jb.stats().concealed_samples, 0u);
}

// NetEqImpl::GetAudioInternal's muted state (neteq_impl.cc:682-698, #574),
// ported from libwebrtc's NetEqDecodingTestWithMutedState and
// CompareMutedStateOnOff (neteq_unittest.cc): 10 ms packets at 16 kHz.
// MutedStateOldPacket inserts packets 10 s older than playout, which NetEq's
// DiscardOldPackets horizon keeps and plays as future packets (#783).
// The CNG cases went with RFC 3389 comfort noise (#591).
namespace {
constexpr uint32_t kMutedTestSamples = 160;

struct MutedStateCall {
  explicit MutedStateCall(bool muted_state, bool silent_payload = false)
      : jb([&] {
          AudioJitterBufferConfig cfg;
          cfg.version = 2;
          cfg.packet_ms = 10;
          cfg.enable_muted_state = muted_state;
          return cfg;
        }()),
        silent(silent_payload),
        x(sim::SpeechLike(16000, 10.0, 7, false)) {}
  void Insert(uint32_t rtp_ts) {
    std::vector<float> pcm(kMutedTestSamples, 0.0f);
    if (!silent) pcm = Chunk(x, static_cast<int>(rtp_ts / kMutedTestSamples % 900), kMutedTestSamples);
    jb.InsertPacket(rtp_ts, std::move(pcm), Timestamp::Millis(now_ms));
  }
  // GetAudio; true when the frame was muted.
  bool Pull() {
    const uint64_t muted_before = jb.stats().muted_frames;
    op = jb.GetAudio(out, Timestamp::Millis(now_ms));
    now_ms += 10;
    return jb.stats().muted_frames > muted_before;
  }
  bool PullUntilMuted() {
    while (!Pull()) {
      if (++counter >= 1000) return false;
    }
    return true;
  }
  // NetEq's kNormalSpeech: any frame that is not concealment.
  bool PullUntilNormal() {
    do {
      if (Pull() || ++counter >= 1000) return false;
    } while (op == AudioOperation::kExpand || op == AudioOperation::kSilence);
    return true;
  }
  AudioJitterBuffer jb;
  bool silent;
  std::vector<float> x;
  std::vector<float> out = std::vector<float>(kMutedTestSamples);
  AudioOperation op = AudioOperation::kSilence;
  int64_t now_ms = 0;
  int counter = 0;
};
}  // namespace

// MutedState: in and out of muted state.
SM_TEST(AudioJitterBufferNetEqV2, MutedState) {
  MutedStateCall c(true);
  c.Insert(0);
  SM_EXPECT_FALSE(c.Pull());
  SM_ASSERT_TRUE(c.PullUntilMuted());
  SM_EXPECT_TRUE(c.op == AudioOperation::kExpand);
  // A muted frame writes silence; the playout timestamp still moves on by the frame.
  const uint32_t ts_before = *c.jb.PlayoutTimestamp();
  std::fill(c.out.begin(), c.out.end(), 17.0f);
  SM_EXPECT_TRUE(c.Pull());
  for (float s : c.out) SM_EXPECT_EQ(s, 0.0f);
  SM_EXPECT_EQ(*c.jb.PlayoutTimestamp(), ts_before + kMutedTestSamples);
  SM_EXPECT_TRUE(c.op == AudioOperation::kExpand);
  // Insert new data, its timestamp corrected for the time elapsed since the
  // last packet: normal operation resumes.
  c.Insert(kMutedTestSamples * static_cast<uint32_t>(c.counter));
  SM_ASSERT_TRUE(c.PullUntilNormal());
  SM_EXPECT_FALSE(c.Pull());
  // Almost all the output was concealment (libwebrtc: expand_rate > 13000 in Q14).
  const auto& st = c.jb.stats();
  SM_EXPECT_GT(static_cast<double>(st.concealed_samples) * 16384.0 / static_cast<double>(st.total_samples_received),
               13000.0);
  auto m = CollectMetrics(c.jb);
  SM_EXPECT_GT(m["muted_frames"], 0.0);
}

// MutedStateDelayedPacket: timestamp corrected for only half the elapsed time.
SM_TEST(AudioJitterBufferNetEqV2, MutedStateDelayedPacket) {
  MutedStateCall c(true);
  c.Insert(0);
  SM_EXPECT_FALSE(c.Pull());
  SM_ASSERT_TRUE(c.PullUntilMuted());
  c.Insert(kMutedTestSamples * static_cast<uint32_t>(c.counter) / 2);
  SM_EXPECT_TRUE(c.PullUntilNormal());
}

// MutedStateFuturePacket: timestamp over-corrected, the packet is early.
SM_TEST(AudioJitterBufferNetEqV2, MutedStateFuturePacket) {
  MutedStateCall c(true);
  c.Insert(0);
  SM_EXPECT_FALSE(c.Pull());
  SM_ASSERT_TRUE(c.PullUntilMuted());
  c.Insert(kMutedTestSamples * static_cast<uint32_t>(c.counter) * 2);
  SM_EXPECT_TRUE(c.PullUntilNormal());
}

// Without muted state NetEq expands for as long as no packet comes.
SM_TEST(AudioJitterBufferNetEqV2, MutedStateIsOffByDefault) {
  MutedStateCall c(false);
  c.Insert(0);
  SM_EXPECT_FALSE(c.PullUntilMuted());
  SM_EXPECT_EQ(c.jb.stats().muted_frames, 0u);
  SM_EXPECT_TRUE(c.op == AudioOperation::kExpand);
}

// CompareMutedStateOnOff: the same frames with and without muted state, but
// for the muted frames' data, through 10 s without packets and back.
SM_TEST(AudioJitterBufferNetEqV2, CompareMutedStateOnOff) {
  MutedStateCall off(false, true), on(true, true);
  off.Insert(0);
  on.Insert(0);
  bool muted = false;
  int mismatches = 0;
  auto same_frame = [&] {
    SM_EXPECT_FALSE(off.Pull());
    muted = on.Pull();
    if (off.op != on.op || off.jb.PlayoutTimestamp() != on.jb.PlayoutTimestamp()) ++mismatches;
    if (!muted && off.out != on.out) ++mismatches;
  };
  for (int i = 0; i < 1000; ++i) same_frame();
  SM_EXPECT_TRUE(muted);
  // New data, its timestamp corrected for the time elapsed since the last packet.
  for (uint32_t i = 0; i < 5; ++i) {
    off.Insert(kMutedTestSamples * 1000 + kMutedTestSamples * i);
    on.Insert(kMutedTestSamples * 1000 + kMutedTestSamples * i);
  }
  int counter = 0;
  do {
    same_frame();
  } while ((off.op == AudioOperation::kExpand || off.op == AudioOperation::kSilence) && ++counter < 1000);
  SM_EXPECT_LT(counter, 1000);
  SM_EXPECT_FALSE(muted);
  SM_EXPECT_EQ(mismatches, 0);
}

// MutedStateOldPacket: packets 10 s older than playout end muted state.
// NetEq keeps them (PacketBuffer::DiscardOldPackets only drops packets less
// than 5 s old, packet_buffer.h IsObsoleteTimestamp) and DecisionLogic plays
// them as future packets.
SM_TEST(AudioJitterBufferNetEqV2, MutedStateOldPacket) {
  MutedStateCall c(true);
  c.Insert(0);
  SM_EXPECT_FALSE(c.Pull());
  SM_ASSERT_TRUE(c.PullUntilMuted());
  SM_EXPECT_TRUE(c.op == AudioOperation::kExpand);
  for (int i = 0; i < 5; ++i) c.Insert(kMutedTestSamples * static_cast<uint32_t>(i - 1000));
  SM_EXPECT_TRUE(c.PullUntilNormal());
}

// A packet more than 5 s older than playout, arriving mid-call (issue #783,
// issue_533 seed 1 at 6790 ms): DecisionLogic::GetDecision takes it as a
// future packet (not IsObsoleteTimestamp), and its uint32 timestamp leap
// makes it too early, so NetEq expands although the expected packet is
// there, then merges back to the old packet (FuturePacketAvailable). A packet
// less than 5 s old is discarded (DiscardOldPackets) and changes nothing.
namespace {
struct OldPacketRun {
  std::vector<AudioOperation> ops;
  uint64_t discarded_late = 0;
};
OldPacketRun PlayWithOldPacket(int age_packets) {
  MutedStateCall c(false);
  OldPacketRun run;
  uint32_t next = 0;
  for (int i = 0; i < 4; ++i) c.Insert(kMutedTestSamples * next++);
  for (int t = 0; t < 800; ++t) {
    c.Insert(kMutedTestSamples * next++);
    c.Pull();
  }
  const uint32_t played = *c.jb.PlayoutTimestamp();
  c.Insert(played - kMutedTestSamples * static_cast<uint32_t>(age_packets));
  for (int t = 0; t < 4; ++t) {
    c.Insert(kMutedTestSamples * next++);
    c.Pull();
    run.ops.push_back(c.op);
  }
  run.discarded_late = c.jb.stats().packets_discarded_late;
  return run;
}
}  // namespace

SM_TEST(AudioJitterBufferNetEqV2, PacketOlderThanFiveSecondsIsPlayedAsAFuturePacket) {
  const OldPacketRun run = PlayWithOldPacket(600);  // 6 s.
  SM_EXPECT_TRUE(run.ops[0] == AudioOperation::kExpand);
  SM_EXPECT_TRUE(run.ops[1] == AudioOperation::kMerge);
  // The old packet played, the next one is 6 s ahead: expand again.
  SM_EXPECT_TRUE(run.ops[2] == AudioOperation::kExpand);
  SM_EXPECT_EQ(run.discarded_late, 0u);
}

SM_TEST(AudioJitterBufferNetEqV2, PacketLessThanFiveSecondsOldIsDiscarded) {
  const OldPacketRun run = PlayWithOldPacket(400);  // 4 s.
  for (AudioOperation op : run.ops) SM_EXPECT_TRUE(op == AudioOperation::kNormal);
  SM_EXPECT_EQ(run.discarded_late, 1u);
}
