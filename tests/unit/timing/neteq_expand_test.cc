// NetEq's Expand (issue #62): libwebrtc M157's expand_unittest.cc, with the
// sync buffer pre-loaded with a synthetic voiced signal instead of
// testfile32kHz.pcm, plus tests of the ported decisions (lags, mute factor,
// mute slope floors, onset) and of the jitter buffer's use of them.
#include <algorithm>
#include <cmath>
#include <vector>

#include "sm/timing/audio_jitter_buffer.h"
#include "sm/timing/neteq_expand.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::timing;

namespace {

constexpr double kPi = 3.14159265358979323846;

// A voiced signal: harmonics of `f0` Hz.
std::vector<float> Voiced(int rate, double f0, size_t n, double amplitude = 0.3) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) {
    double v = 0;
    for (int h = 1; h <= 4; ++h) v += std::sin(2 * kPi * f0 * h * static_cast<double>(i) / rate) / h;
    x[i] = static_cast<float>(amplitude * v / 2);
  }
  return x;
}

// Expand::Process on `sync`, with what it appends kept at the end of it.
size_t ProcessInto(NetEqExpand& expand, std::vector<float>& sync) {
  std::vector<float> out;
  const size_t n = expand.Process(sync, out);
  sync.insert(sync.end(), out.begin(), out.end());
  return n;
}

}  // namespace

SM_TEST(NetEqRandomVector, Generate) {
  NetEqRandomVector rv;
  int16_t out[3];
  rv.Generate(3, out);
  // seed 777 + 1, 2, 3 -> positions 10, 11, 12 of the table.
  SM_EXPECT_EQ(out[0], NetEqRandomVector::kRandomTable[10]);
  SM_EXPECT_EQ(out[2], NetEqRandomVector::kRandomTable[12]);
  rv.IncreaseSeedIncrement(255);
  SM_EXPECT_EQ(rv.seed_increment(), 0);  // (1 + 255) & 255.
}

// expand_unittest.cc DelayedPacketOutage.
SM_TEST(NetEqExpand, DelayedPacketOutage) {
  NetEqExpand expand(32000);
  std::vector<float> sync = Voiced(32000, 180, 3200);
  size_t sum = 0;
  for (int i = 0; i < 10; ++i) {
    const size_t n = ProcessInto(expand, sync);
    SM_EXPECT_GT(n, 0u);
    sum += n;
    SM_EXPECT_EQ(expand.last_outage_duration_samples(), 0u);
  }
  expand.SetParametersForNormalAfterExpand();
  SM_EXPECT_EQ(expand.last_outage_duration_samples(), sum);
}

// expand_unittest.cc LostPacketOutage.
SM_TEST(NetEqExpand, LostPacketOutage) {
  NetEqExpand expand(32000);
  std::vector<float> sync = Voiced(32000, 180, 3200);
  for (int i = 0; i < 10; ++i) SM_EXPECT_GT(ProcessInto(expand, sync), 0u);
  expand.SetParametersForMergeAfterExpand();
  SM_EXPECT_EQ(expand.last_outage_duration_samples(), 0u);
}

// expand_unittest.cc CheckOutageStatsAfterReset.
SM_TEST(NetEqExpand, CheckOutageStatsAfterReset) {
  NetEqExpand expand(32000);
  std::vector<float> sync = Voiced(32000, 180, 3200);
  size_t sum = 0;
  for (int i = 0; i < 10; ++i) {
    sum += ProcessInto(expand, sync);
    if (i == 5) {
      expand.Reset();
      sum = 0;
    }
  }
  expand.SetParametersForNormalAfterExpand();
  SM_EXPECT_EQ(expand.last_outage_duration_samples(), sum);
}

// expand_unittest.cc Muted.
SM_TEST(NetEqExpand, Muted) {
  NetEqExpand expand(32000);
  std::vector<float> sync = Voiced(32000, 180, 3200);
  auto until_muted = [&] {
    SM_EXPECT_FALSE(expand.Muted());
    int calls = 0;
    while (!expand.Muted() && calls < 1000) {
      ProcessInto(expand, sync);
      ++calls;
    }
    SM_EXPECT_LT(calls, 1000);
  };
  until_muted();
  expand.Reset();
  SM_EXPECT_FALSE(expand.Muted());
  until_muted();
  expand.SetParametersForMergeAfterExpand();
  SM_EXPECT_FALSE(expand.Muted());
  expand.Reset();
  until_muted();
  expand.SetParametersForNormalAfterExpand();
  SM_EXPECT_FALSE(expand.Muted());
}

// AnalyzeSignal finds a whole number of pitch periods (on a strictly
// periodic signal one and two periods correlate equally), and each call
// appends one lag.
SM_TEST(NetEqExpand, LagIsThePitchPeriod) {
  NetEqExpand expand(16000);
  std::vector<float> sync = Voiced(16000, 200, 1600);  // 80 samples per period.
  const size_t n = ProcessInto(expand, sync);
  for (int i = 0; i < NetEqExpand::kNumLags; ++i) {
    const size_t lag = expand.expand_lag(i);
    SM_EXPECT_GE(lag, 40u);   // 2.5 ms.
    SM_EXPECT_LT(lag, 240u);  // 15 ms.
    const size_t off = lag % 80;
    SM_EXPECT_TRUE(off <= 2 || off >= 78);
  }
  SM_EXPECT_EQ(n, expand.expand_lag(0));
  SM_EXPECT_EQ(expand.overlap_length(), 10u);  // 5 * fs / 8000.
  SM_EXPECT_EQ(expand.history_length(), 512u);
}

// A steady voiced signal: no slope from the analysis (amplitude ratio ~1 and
// voice mix factor above 0.8), the legacy 128/16384 per call, then the slope
// floors of the 4th and 8th calls.
SM_TEST(NetEqExpand, MuteSlopeFloors) {
  NetEqExpand expand(16000);
  std::vector<float> sync = Voiced(16000, 200, 1600);
  ProcessInto(expand, sync);
  SM_EXPECT_FALSE(expand.onset());
  SM_EXPECT_GT(expand.voice_mix_factor(), 13107);
  SM_EXPECT_EQ(expand.mute_slope(), 0);
  SM_EXPECT_EQ(expand.MuteFactor(), (16256 * 16384 + 8192) >> 14);
  for (int i = 1; i < 4; ++i) ProcessInto(expand, sync);
  SM_EXPECT_EQ(expand.mute_slope(), 1049 / 2);  // From the 4th call: 0.001 / fs_mult.
  for (int i = 4; i < 8; ++i) ProcessInto(expand, sync);
  SM_EXPECT_EQ(expand.mute_slope(), 2097 / 2);  // From the 8th: 0.002 / fs_mult.
  SM_EXPECT_EQ(expand.consecutive_expands(), 8);
  // Above half (DecisionLogic::PostponeDecode's limit) for at least 8 calls.
  SM_EXPECT_GT(expand.MuteFactor(), 8192);
}

// The last period much louder than the one before: an onset, which is not
// muted on the first call.
SM_TEST(NetEqExpand, OnsetIsNotMutedOnTheFirstCall) {
  NetEqExpand expand(16000);
  std::vector<float> sync = Voiced(16000, 200, 1600, 0.3);
  for (size_t i = 0; i < sync.size(); ++i) {
    const double ramp = std::exp(static_cast<double>(i) / 30.0 - 1600.0 / 30.0);  // x2.4 per 80 samples.
    sync[i] = static_cast<float>(sync[i] * ramp);
  }
  ProcessInto(expand, sync);
  SM_EXPECT_TRUE(expand.onset());
  SM_EXPECT_GT(expand.mute_slope(), 0);
  SM_EXPECT_EQ(expand.MuteFactor(), 16384);
}

// Noise is unvoiced: the voice mix factor is 0 and the mute factor falls by
// at least 0.1 per 6.25 ms.
SM_TEST(NetEqExpand, UnvoicedMutesFast) {
  NetEqExpand expand(16000);
  std::vector<float> sync(1600);
  uint32_t s = 12345;
  for (float& v : sync) {
    s = s * 1664525u + 1013904223u;
    v = (static_cast<float>(s >> 8) / 16777216.0f - 0.5f) * 0.4f;
  }
  ProcessInto(expand, sync);
  SM_EXPECT_LT(expand.voice_mix_factor(), 13107);
  SM_EXPECT_GE(expand.mute_slope(), 5243 / 2);
}

// The jitter buffer conceals a gap with whole periods and keeps Expand's
// overlap as lookahead: every 10 ms of a gap is an expand.
SM_TEST(AudioJitterBufferNetEqV2, ExpandCoversEveryTickOfAGap) {
  AudioJitterBufferConfig cfg;
  cfg.version = 2;
  AudioJitterBuffer jb(cfg);
  std::vector<float> out(160);
  const std::vector<float> tone = Voiced(16000, 200, 320 * 400);
  auto packet = [&](int i) {
    return std::vector<float>(tone.begin() + i * 320, tone.begin() + (i + 1) * 320);
  };
  for (int i = 0; i < 50; ++i) {
    jb.InsertPacket(static_cast<uint32_t>(i * 320), packet(i), Timestamp::Millis(i * 20));
    jb.GetAudio(out, Timestamp::Millis(i * 20));
    jb.GetAudio(out, Timestamp::Millis(i * 20 + 10));
  }
  // A 1 s gap: after the buffered audio plays out, every tick is an expand.
  int expands = 0, run = 0, longest = 0;
  for (int t = 1000; t < 2000; t += 10) {
    if (jb.GetAudio(out, Timestamp::Millis(t)) == AudioOperation::kExpand) {
      ++expands;
      longest = std::max(longest, ++run);
    } else {
      run = 0;
    }
  }
  SM_EXPECT_EQ(expands, longest);
  SM_EXPECT_GT(expands, 80);
  SM_EXPECT_GE(jb.stats().concealed_samples, static_cast<uint64_t>(expands) * 160);
  SM_EXPECT_EQ(jb.stats().concealment_events, 1u);
  // Playout resumes after it.
  for (int i = 100; i < 120; ++i) {
    jb.InsertPacket(static_cast<uint32_t>(i * 320), packet(i), Timestamp::Millis(i * 20));
    jb.GetAudio(out, Timestamp::Millis(i * 20));
    jb.GetAudio(out, Timestamp::Millis(i * 20 + 10));
  }
  SM_EXPECT_GT(jb.stats().ops[static_cast<int>(AudioOperation::kNormal)] + jb.stats().ops[static_cast<int>(AudioOperation::kMerge)],
               100u);
}
