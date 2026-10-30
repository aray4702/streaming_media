// NetEq's Expand (issue #62): libwebrtc M157's expand_unittest.cc, with the
// sync buffer pre-loaded with a synthetic voiced signal instead of
// testfile32kHz.pcm, plus tests of the ported decisions (lags, mute factor,
// mute slope floors, onset) and of the jitter buffer's use of them.
//
// `NetEqExpand, BitExactWithLibwebrtc` (#783): the golden hashes come from
// libwebrtc itself: compare/webrtc/reference/modules/audio_coding/neteq/
// expand.cc, background_noise.cc, random_vector.cc, dsp_helper.cc,
// cross_correlation.cc, sync_buffer.cc, audio_multi_vector.cc and
// audio_vector.cc with the signal processing library's C files (and
// spl_sqrt_floor.c), compiled unchanged against the prebuilt's headers
// (stubs for rtc_base/checks.h, DCHECKs off as in a release build, and for
// StatisticsCalculator), driven with ForEachExpandCase() below on a SyncBuffer
// of the case's samples. Each case runs 6 expands, SetParametersForMerge-
// AfterExpand and one more (Merge's), then Reset and 9 more on the sync
// buffer they extended. Each expand hashes (FNV-1a step h = (h ^ v) * prime)
// its length, every output sample and the sync buffer's last overlap samples
// as uint16, the mute factor and Muted(). Expand is integer only, so the
// hashes hold on every platform.
#include <algorithm>
#include <cmath>
#include <cstdint>
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

// Integer-only test signals (the time stretch test's GoldenSignal).
std::vector<int16_t> GoldenSignal(int kind, size_t n, int64_t p, int64_t a, uint32_t seed) {
  std::vector<int16_t> x(n);
  uint32_t s = seed;
  for (size_t i = 0; i < n; ++i) {
    s = s * 1664525u + 1013904223u;
    const int64_t noise = (static_cast<int64_t>(s >> 16) - 32768) * a / 32768;
    const int64_t pp = kind == 3 ? p + static_cast<int64_t>(i / 64) : p;
    const int64_t ph = static_cast<int64_t>(i + seed) % pp;
    const int64_t tri = ph < pp / 2 ? (4 * a * ph) / pp - a : 3 * a - (4 * a * ph) / pp;
    const int64_t pp3 = pp / 3 > 1 ? pp / 3 : 2;
    const int64_t ph3 = static_cast<int64_t>(i + seed) % pp3;
    const int64_t tri3 = (ph3 < pp3 / 2 ? (4 * a * ph3) / pp3 - a : 3 * a - (4 * a * ph3) / pp3) / 3;
    int64_t v = 0;
    switch (kind) {
      case 0: v = tri + tri3; break;
      case 1: v = noise; break;
      case 2: v = tri + tri3 + noise / 4; break;
      default:
        v = (tri + tri3) * 2;
        if (i % 211 < 2) v = (i & 1) ? 32767 : -32768;
        break;
    }
    x[i] = static_cast<int16_t>(std::clamp<int64_t>(v, -32768, 32767));
  }
  return x;
}

// The sync buffer of every expand case at `fs_hz`: 4 signal kinds x 6
// pitch periods x 3 amplitudes x 4 envelopes (flat; an onset, the last 10 ms
// 3x louder; a decay, the last 10 ms at 1/3; silence before the last 4 ms),
// plus an all-zero buffer.
template <class F>
void ForEachExpandCase(int fs_hz, F f) {
  const size_t m = static_cast<size_t>(fs_hz / 8000);
  const size_t pitches[] = {20, 33, 47, 64, 97, 118};
  const int64_t amps[] = {300, 4000, 14000};
  uint32_t seed = 1;
  for (int kind = 0; kind < 4; ++kind) {
    for (size_t p : pitches) {
      for (int64_t a : amps) {
        for (int envelope = 0; envelope < 4; ++envelope) {
          ++seed;
          std::vector<int16_t> sync = GoldenSignal(kind, 600 * m, static_cast<int64_t>(p * m), a, seed);
          const size_t n = sync.size();
          for (size_t i = 0; i < n; ++i) {
            int32_t v = sync[i];
            if (envelope == 1 && i >= n - 80 * m) v = std::clamp(v * 3, -32768, 32767);
            if (envelope == 2 && i >= n - 80 * m) v = v / 3;
            if (envelope == 3 && i < n - 32 * m) v = 0;
            sync[i] = static_cast<int16_t>(v);
          }
          f(sync);
        }
      }
    }
  }
  f(std::vector<int16_t>(600 * m, 0));
}

uint64_t Mix(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; }

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
  // Every tick after the first is wholly expanded audio, all of it counted
  // as concealed; the first may still play decoded audio left in the sync
  // buffer, which libwebrtc doesn't count either (StatisticsCalculator::
  // ExpandedVoiceSamples counts what Expand generates).
  SM_EXPECT_GE(jb.stats().concealed_samples, static_cast<uint64_t>(expands - 1) * 160);
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

// Expand::Process bit for bit with libwebrtc's expand.cc (see the top of this
// file): AnalyzeSignal's correlation, peak and distortion search, Levinson-
// Durbin, residual gain and mute slope, and Process's voiced/unvoiced mix,
// overlap-add and muting.
SM_TEST(NetEqExpand, BitExactWithLibwebrtc) {
  const struct {
    int fs_hz;
    uint64_t hash;
  } golden[] = {{8000, 0xcd6aa40582f22af5ull},
                {16000, 0x7c7e4d711c484cafull},
                {32000, 0xfa17381805bed086ull},
                {48000, 0x03fa4dd22028126eull}};
  for (const auto& g : golden) {
    uint64_t h = 1469598103934665603ull;
    size_t n = 0;
    ForEachExpandCase(g.fs_hz, [&](const std::vector<int16_t>& signal) {
      std::vector<float> sync(signal.size());
      for (size_t i = 0; i < signal.size(); ++i) sync[i] = static_cast<float>(signal[i]) / 32768.0f;
      NetEqExpand expand(g.fs_hz);
      const size_t overlap = expand.overlap_length();
      auto step = [&] {
        std::vector<float> out;
        expand.Process(sync, out);
        h = Mix(h, out.size());
        for (float v : out) h = Mix(h, static_cast<uint16_t>(static_cast<int16_t>(std::lround(v * 32768.0f))));
        for (size_t i = sync.size() - overlap; i < sync.size(); ++i) {
          h = Mix(h, static_cast<uint16_t>(static_cast<int16_t>(std::lround(sync[i] * 32768.0f))));
        }
        h = Mix(h, static_cast<uint16_t>(expand.MuteFactor()));
        h = Mix(h, expand.Muted() ? 1 : 0);
        sync.insert(sync.end(), out.begin(), out.end());
      };
      for (int i = 0; i < 6; ++i) step();
      expand.SetParametersForMergeAfterExpand();
      step();
      expand.Reset();
      for (int i = 0; i < 9; ++i) step();
      ++n;
    });
    SM_EXPECT_EQ(n, 289u);
    SM_EXPECT_EQ(h, g.hash);
  }
}
