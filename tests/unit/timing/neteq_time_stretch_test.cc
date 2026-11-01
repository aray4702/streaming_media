// NetEq's Accelerate and PreemptiveExpand (issue #62): libwebrtc M157's
// time_stretch_unittest.cc (CreateAndDestroy; Accelerate and
// AccelerateFastMode read testfile32kHz.pcm, which isn't in the tree, so
// synthetic voiced signals and noise stand in), plus tests of the ported
// decisions: the pitch period, the 0.9 (0.5 fast) correlation threshold, the
// VAD against the background noise energy, the 15 ms split, preemptive
// expand's old data rule, the crossfade, and the jitter buffer's use of them.
//
// `NetEqTimeStretch, BitExactWithLibwebrtc` (#644): the golden hashes come
// from libwebrtc itself: compare/webrtc/reference/modules/audio_coding/neteq/
// time_stretch.cc, accelerate.cc, preemptive_expand.cc, dsp_helper.cc,
// audio_vector.cc, audio_multi_vector.cc and background_noise.cc with the
// signal processing library's C files, compiled unchanged (stubs for
// rtc_base/checks.h and api/audio/audio_view.h only), driven with
// GoldenSignal() below: four signal kinds x 7 pitch periods x 3 amplitudes x
// 2 input lengths (30 and 40 ms) per rate and mode, with the background noise
// unset (75000 threshold) or estimated from GoldenSignal(1, 512, 1, 1200, 99).
// Each case hashes (FNV-1a step h = (h ^ v) * prime) the return code (0
// success, 1 low energy, 2 no stretch, 3 error), the length change (0 unless
// it stretched), the output length and every output sample as uint16. The
// time stretch is integer only, so the hashes hold on every platform.
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
using Result = NetEqTimeStretch::Result;

// Harmonics of `f0` Hz, peak amplitude about `amplitude` (full scale 1).
std::vector<float> Voiced(int rate, double f0, size_t n, double amplitude) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) {
    double v = 0;
    for (int h = 1; h <= 4; ++h) v += std::sin(2 * kPi * f0 * h * static_cast<double>(i) / rate) / h;
    x[i] = static_cast<float>(amplitude * v / 2);
  }
  return x;
}

// White noise with an rms of `rms_int16` on the int16 scale (deterministic LCG).
std::vector<float> WhiteNoise(size_t n, double rms_int16, uint32_t seed = 1) {
  std::vector<float> x(n);
  uint32_t s = seed;
  const double amplitude = rms_int16 * std::sqrt(3.0) / 32768.0;  // Uniform in [-a, a].
  for (float& v : x) {
    s = s * 1664525u + 1013904223u;
    v = static_cast<float>(amplitude * (2.0 * (s >> 8) / 16777216.0 - 1.0));
  }
  return x;
}

std::vector<float> Add(std::vector<float> a, const std::vector<float>& b) {
  for (size_t i = 0; i < a.size(); ++i) a[i] += b[i];
  return a;
}

bool SamePrefix(const std::vector<float>& a, const std::vector<float>& b, size_t n) {
  return a.size() >= n && b.size() >= n && std::equal(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(n), b.begin());
}

// Integer-only test signals for the golden test, the same code as the driver
// that produced the hashes. Kind 0: voiced (a triangle of period `p` plus a
// third-harmonic triangle), amplitude `a`. Kind 1: LCG noise of amplitude
// `a`. Kind 2: voiced plus noise at a quarter of `a`. Kind 3: voiced at twice
// `a` whose period grows by one sample every 64, with full-scale clips.
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

std::vector<float> ToFloat(const std::vector<int16_t>& x) {
  std::vector<float> f(x.size());
  for (size_t i = 0; i < x.size(); ++i) f[i] = static_cast<float>(x[i]) / 32768.0f;
  return f;
}

uint64_t Mix(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; }

struct Golden {
  int bgn, rate, mode;  // Mode 0 accelerate, 1 fast accelerate, 2-5 preemptive expand (old data 0, 60, 130 x fs/8000, all but the overlap).
  int counts[4];        // Success, low energy, no stretch, error.
  uint64_t hash;
};
// From libwebrtc's time_stretch.cc (see the file comment).
constexpr Golden kGolden[] = {
    {0, 8000, 0, {64, 42, 62, 0}, 0x30cccfa4dd9f18c8ull},
    {0, 8000, 1, {96, 42, 30, 0}, 0x0ee1b1142552b3b4ull},
    {0, 8000, 2, {64, 42, 62, 0}, 0x7aaff530fb955232ull},
    {0, 8000, 3, {64, 42, 62, 0}, 0x7aaff530fb955232ull},
    {0, 8000, 4, {0, 42, 126, 0}, 0x91c38f42ef4f4ee4ull},
    {0, 8000, 5, {0, 0, 0, 168}, 0xa500bade4e03fd26ull},
    {0, 16000, 0, {58, 42, 68, 0}, 0x8f10100cd341143full},
    {0, 16000, 1, {68, 42, 58, 0}, 0xe01abc4d7e5c9d6bull},
    {0, 16000, 2, {58, 42, 68, 0}, 0x1580516feec950dfull},
    {0, 16000, 3, {58, 42, 68, 0}, 0x1580516feec950dfull},
    {0, 16000, 4, {0, 42, 126, 0}, 0x4ba72bc96a12e831ull},
    {0, 16000, 5, {0, 0, 0, 168}, 0x0270b211f35b6993ull},
    {0, 32000, 0, {56, 42, 70, 0}, 0xdabbaeda70138ac2ull},
    {0, 32000, 1, {72, 42, 54, 0}, 0xf40202b1b061c69eull},
    {0, 32000, 2, {56, 42, 70, 0}, 0x671389d9f79a64ecull},
    {0, 32000, 3, {56, 42, 70, 0}, 0x671389d9f79a64ecull},
    {0, 32000, 4, {0, 42, 126, 0}, 0x490eb46407124877ull},
    {0, 32000, 5, {0, 0, 0, 168}, 0x7e1b671095a20c1cull},
    {0, 48000, 0, {56, 42, 70, 0}, 0x0e3de6d7411a18c4ull},
    {0, 48000, 1, {74, 42, 52, 0}, 0x0c488867cf22e36eull},
    {0, 48000, 2, {56, 42, 70, 0}, 0x4d269182069eba7cull},
    {0, 48000, 3, {56, 42, 70, 0}, 0x4d269182069eba7cull},
    {0, 48000, 4, {0, 42, 126, 0}, 0x990613d1aeab1456ull},
    {0, 48000, 5, {0, 0, 0, 168}, 0xbbf2daf58106040aull},
    {1, 8000, 0, {48, 72, 48, 0}, 0x71ff953fd734baeeull},
    {1, 8000, 1, {78, 72, 18, 0}, 0xfb7961ee2bfcd530ull},
    {1, 8000, 2, {48, 72, 48, 0}, 0x424b73e1537cf344ull},
    {1, 8000, 3, {48, 72, 48, 0}, 0x424b73e1537cf344ull},
    {1, 8000, 4, {0, 72, 96, 0}, 0x424bc53190198b0dull},
    {1, 8000, 5, {0, 0, 0, 168}, 0xa500bade4e03fd26ull},
    {1, 16000, 0, {46, 54, 68, 0}, 0x3f0ec8da9e4a4967ull},
    {1, 16000, 1, {56, 54, 58, 0}, 0x91996a0e10e912ffull},
    {1, 16000, 2, {46, 54, 68, 0}, 0xc51ed43d2a3ee127ull},
    {1, 16000, 3, {46, 54, 68, 0}, 0xc51ed43d2a3ee127ull},
    {1, 16000, 4, {0, 54, 114, 0}, 0x9d56e8339161f2f7ull},
    {1, 16000, 5, {0, 0, 0, 168}, 0x0270b211f35b6993ull},
    {1, 32000, 0, {52, 48, 68, 0}, 0x8fbf6ed528017c88ull},
    {1, 32000, 1, {68, 48, 52, 0}, 0x0f15606bb01d9d8aull},
    {1, 32000, 2, {52, 48, 68, 0}, 0xa01cff8a9c5bb842ull},
    {1, 32000, 3, {52, 48, 68, 0}, 0xa01cff8a9c5bb842ull},
    {1, 32000, 4, {0, 48, 120, 0}, 0x0f501cc6c89156ffull},
    {1, 32000, 5, {0, 0, 0, 168}, 0x7e1b671095a20c1cull},
    {1, 48000, 0, {56, 42, 70, 0}, 0x0e3de6d7411a18c4ull},
    {1, 48000, 1, {74, 42, 52, 0}, 0x0c488867cf22e36eull},
    {1, 48000, 2, {56, 42, 70, 0}, 0x4d269182069eba7cull},
    {1, 48000, 3, {56, 42, 70, 0}, 0x4d269182069eba7cull},
    {1, 48000, 4, {0, 42, 126, 0}, 0x990613d1aeab1456ull},
    {1, 48000, 5, {0, 0, 0, 168}, 0xbbf2daf58106040aull},
};

}  // namespace

// time_stretch_unittest.cc CreateAndDestroy, at every NetEq rate.
SM_TEST(NetEqTimeStretch, CreateAndDestroy) {
  for (int rate : {8000, 16000, 32000, 48000}) {
    NetEqTimeStretch ts(rate);
    SM_EXPECT_EQ(ts.peak_index(), 0u);
  }
  SM_EXPECT_EQ(NetEqTimeStretch::kCorrelationThreshold, 14746);
  SM_EXPECT_EQ(NetEqTimeStretch::kFastCorrelationThreshold, 8192);
}

// A loud voiced block of 30 ms: the period (125 Hz, 8 ms, twice that is past
// the 15 ms longest lag) is found at every rate, the correlation is above
// 0.9, and accelerate removes one period after 15 ms unchanged.
SM_TEST(NetEqTimeStretch, AccelerateVoicedRemovesOnePeriod) {
  for (int rate : {8000, 16000, 32000, 48000}) {
    const size_t fs_mult = static_cast<size_t>(rate / 8000);
    const size_t period = static_cast<size_t>(rate / 125);
    const std::vector<float> in = Voiced(rate, 125, 240 * fs_mult, 0.4);
    NetEqTimeStretch ts(rate);
    NetEqBackgroundNoise bgn;
    std::vector<float> out;
    size_t change = 99;
    SM_EXPECT_TRUE(ts.Accelerate(in, false, bgn, out, &change) == Result::kSuccess);
    SM_EXPECT_TRUE(ts.active_speech());
    SM_EXPECT_GT(ts.best_correlation(), NetEqTimeStretch::kCorrelationThreshold);
    SM_EXPECT_LE(ts.best_correlation(), 16384);
    SM_EXPECT_NEAR(static_cast<double>(change), static_cast<double>(period), static_cast<double>(fs_mult));
    SM_EXPECT_EQ(change, ts.peak_index());
    SM_EXPECT_EQ(out.size() + change, in.size());
    SM_EXPECT_TRUE(SamePrefix(out, in, 120 * fs_mult - change));
    // After the crossfade the output is the input one period later.
    SM_EXPECT_EQ(out.back(), in.back());
    SM_EXPECT_EQ(out[120 * fs_mult], in[120 * fs_mult + change]);
  }
}

// #951: DspHelper::DownsampleTo4kHz (here and in Expand/Merge's
// DownsampleFast) filters backwards from `num_coefficients - 1` samples into
// the signal, so the first outputs read samples before that pointer. The read
// is in bounds, but `in[i - j]` with unsigned i < j wrapped the pointer
// arithmetic, which Apple clang's UBSan reports (pointer-overflow) and
// GCC's doesn't instrument. Runs the path at every rate on the shortest
// inputs; under `native-asan` on macOS, or a clang build with
// `-fsanitize=pointer-overflow -fsanitize-trap=pointer-overflow`, the old
// indexing traps here. BitExactWithLibwebrtc pins the values.
SM_TEST(NetEqTimeStretch, DownsamplingReadsTheFilterHistoryWithoutOverflow) {
  for (int rate : {8000, 16000, 32000, 48000}) {
    const size_t fs_mult = static_cast<size_t>(rate / 8000);
    const std::vector<float> in = Voiced(rate, 125, 240 * fs_mult, 0.4);
    NetEqBackgroundNoise bgn;
    std::vector<float> out;
    size_t change = 0;
    NetEqTimeStretch accelerate(rate);
    SM_EXPECT_TRUE(accelerate.Accelerate(in, false, bgn, out, &change) == Result::kSuccess);
    out.clear();
    NetEqTimeStretch preemptive(rate);
    SM_EXPECT_TRUE(preemptive.PreemptiveExpand(in, 0, 5 * fs_mult, bgn, out, &change) == Result::kSuccess);
    SM_EXPECT_EQ(accelerate.peak_index(), preemptive.peak_index());
  }
}

// AudioVector::CrossFade: the period before 15 ms fades into the one after
// it with weights falling by 16384 / (L + 1) in Q14, on the int16 samples
// and rounded as libwebrtc's ((a * x + (16384 - a) * y + 8192) >> 14, #644).
SM_TEST(NetEqTimeStretch, AccelerateCrossfadeIsLibwebrtcsRamp) {
  const std::vector<float> in = Voiced(16000, 125, 480, 0.4);
  NetEqTimeStretch ts(16000);
  NetEqBackgroundNoise bgn;
  std::vector<float> out;
  size_t l = 0;
  SM_ASSERT_TRUE(ts.Accelerate(in, false, bgn, out, &l) == Result::kSuccess);
  const int step = 16384 / (static_cast<int>(l) + 1);
  for (size_t i : {size_t{0}, l / 2, l - 1}) {
    const int alpha = 16384 - step * static_cast<int>(i + 1);
    const int x = static_cast<int>(std::lround(in[240 - l + i] * 32768.0f));
    const int y = static_cast<int>(std::lround(in[240 + i] * 32768.0f));
    const float expected = static_cast<float>((alpha * x + (16384 - alpha) * y + 8192) >> 14) / 32768.0f;
    SM_EXPECT_EQ(out[240 - l + i], expected);
  }
}

// Loud white noise is active speech without a strong correlation: no stretch,
// and the input is copied.
SM_TEST(NetEqTimeStretch, AccelerateRejectsUncorrelatedSpeech) {
  const std::vector<float> in = WhiteNoise(480, 5000);
  NetEqTimeStretch ts(16000);
  NetEqBackgroundNoise bgn;
  std::vector<float> out;
  size_t change = 99;
  SM_EXPECT_TRUE(ts.Accelerate(in, false, bgn, out, &change) == Result::kNoStretch);
  SM_EXPECT_TRUE(ts.active_speech());
  SM_EXPECT_LT(ts.best_correlation(), NetEqTimeStretch::kCorrelationThreshold);
  SM_EXPECT_EQ(change, 0u);
  SM_EXPECT_TRUE(out == in);
}

// Fast accelerate (0.5 in Q14) takes a period whose correlation is between
// 0.5 and 0.9, and removes as many whole periods as fit in 15 ms. Its length
// change is one period, as libwebrtc's TimeStretch::Process reports it (#644).
SM_TEST(NetEqTimeStretch, FastModeThresholdAndWholePeriods) {
  const std::vector<float> in = Add(Voiced(16000, 125, 480, 0.4), WhiteNoise(480, 2200, 7));
  NetEqTimeStretch ts(16000);
  NetEqBackgroundNoise bgn;
  std::vector<float> out;
  size_t change = 0;
  SM_EXPECT_TRUE(ts.Accelerate(in, false, bgn, out, &change) == Result::kNoStretch);
  SM_EXPECT_GT(ts.best_correlation(), NetEqTimeStretch::kFastCorrelationThreshold);
  SM_EXPECT_LE(ts.best_correlation(), NetEqTimeStretch::kCorrelationThreshold);
  out.clear();
  SM_EXPECT_TRUE(ts.Accelerate(in, true, bgn, out, &change) == Result::kSuccess);
  SM_EXPECT_EQ(change, ts.peak_index());
  SM_EXPECT_EQ(out.size() + (240 / ts.peak_index()) * ts.peak_index(), in.size());

  // A short period: fast mode removes three of them.
  const std::vector<float> high = Voiced(16000, 250, 480, 0.4);
  out.clear();
  SM_EXPECT_TRUE(ts.Accelerate(high, true, bgn, out, &change) == Result::kSuccess);
  SM_EXPECT_EQ(change, ts.peak_index());
  SM_EXPECT_GE(in.size() - out.size(), 2 * ts.peak_index());
  SM_EXPECT_EQ(in.size() - out.size(), (240 / ts.peak_index()) * ts.peak_index());
}

// Until the background noise is estimated the VAD's threshold is 75000: a
// block at up to 8 * 75000 energy per sample (rms 775) is passive speech and
// is stretched whatever its correlation (kSuccessLowEnergy). Once the noise
// is estimated, the threshold is 8 times its energy.
SM_TEST(NetEqTimeStretch, VadAgainstTheBackgroundNoise) {
  const std::vector<float> in = WhiteNoise(480, 500, 3);
  NetEqTimeStretch ts(16000);
  NetEqBackgroundNoise bgn;
  std::vector<float> out;
  size_t change = 0;
  SM_EXPECT_TRUE(ts.Accelerate(in, false, bgn, out, &change) == Result::kSuccessLowEnergy);
  SM_EXPECT_FALSE(ts.active_speech());
  SM_EXPECT_EQ(ts.best_correlation(), 0);
  SM_EXPECT_GT(change, 0u);
  SM_EXPECT_EQ(out.size() + change, in.size());

  // Louder than 8 * 75000 per sample: active, and rejected.
  out.clear();
  SM_EXPECT_TRUE(ts.Accelerate(WhiteNoise(480, 1100, 3), false, bgn, out, &change) == Result::kNoStretch);

  // Noise estimated at rms 100 (energy about 10000): rms 500 is active now.
  SM_ASSERT_TRUE(bgn.Update(WhiteNoise(256, 100, 5)));
  SM_ASSERT_TRUE(bgn.initialized());
  SM_EXPECT_LT(bgn.Energy(), 20000);
  out.clear();
  SM_EXPECT_TRUE(ts.Accelerate(in, false, bgn, out, &change) == Result::kNoStretch);
  SM_EXPECT_TRUE(ts.active_speech());
  // And rms 200 (40000, under 8 * 10000) is passive.
  out.clear();
  SM_EXPECT_TRUE(ts.Accelerate(WhiteNoise(480, 200, 9), false, bgn, out, &change) == Result::kSuccessLowEnergy);
}

// Accelerate::Process and PreemptiveExpand::Process need (almost) 30 ms:
// (2 * 120 - 1) * fs_mult samples; shorter is copied with kError.
SM_TEST(NetEqTimeStretch, ShortInputIsAnError) {
  NetEqTimeStretch ts(16000);
  NetEqBackgroundNoise bgn;
  std::vector<float> out;
  size_t change = 99;
  const std::vector<float> in = Voiced(16000, 125, 477, 0.4);
  SM_EXPECT_TRUE(ts.Accelerate(in, false, bgn, out, &change) == Result::kError);
  SM_EXPECT_EQ(change, 0u);
  SM_EXPECT_TRUE(out == in);
  out.clear();
  SM_EXPECT_TRUE(ts.PreemptiveExpand(in, 0, 10, bgn, out, &change) == Result::kError);
  SM_EXPECT_TRUE(out == in);
  out.clear();
  SM_EXPECT_TRUE(ts.Accelerate(Voiced(16000, 125, 478, 0.4), false, bgn, out, &change) == Result::kSuccess);
}

// Preemptive expand repeats the period before 15 ms: one period longer, the
// first 15 ms unchanged, and the input continues one period later.
SM_TEST(NetEqTimeStretch, PreemptiveExpandVoicedAddsOnePeriod) {
  for (int rate : {8000, 16000, 32000, 48000}) {
    const size_t fs_mult = static_cast<size_t>(rate / 8000);
    const std::vector<float> in = Voiced(rate, 125, 240 * fs_mult, 0.4);
    NetEqTimeStretch ts(rate);
    NetEqBackgroundNoise bgn;
    std::vector<float> out;
    size_t change = 0;
    SM_EXPECT_TRUE(ts.PreemptiveExpand(in, 0, 5 * fs_mult, bgn, out, &change) == Result::kSuccess);
    SM_EXPECT_NEAR(static_cast<double>(change), static_cast<double>(rate / 125), static_cast<double>(fs_mult));
    SM_EXPECT_EQ(out.size(), in.size() + change);
    SM_EXPECT_TRUE(SamePrefix(out, in, 120 * fs_mult));
    SM_EXPECT_EQ(out[120 * fs_mult + change], in[120 * fs_mult]);
    SM_EXPECT_EQ(out.back(), in.back());
  }
}

// PreemptiveExpand's old data: active speech is stretched only with at most
// 15 ms already played; passive speech is stretched after the old data,
// with a period of at most the new samples; old data within the overlap of
// the end is an error.
SM_TEST(NetEqTimeStretch, PreemptiveExpandOldData) {
  NetEqTimeStretch ts(16000);
  NetEqBackgroundNoise bgn;
  std::vector<float> out;
  size_t change = 0;
  const std::vector<float> loud = Voiced(16000, 125, 480, 0.4);
  SM_EXPECT_TRUE(ts.PreemptiveExpand(loud, 240, 10, bgn, out, &change) == Result::kSuccess);
  out.clear();
  SM_EXPECT_TRUE(ts.PreemptiveExpand(loud, 241, 10, bgn, out, &change) == Result::kNoStretch);
  SM_EXPECT_EQ(change, 0u);
  SM_EXPECT_TRUE(out == loud);

  const std::vector<float> quiet = Voiced(16000, 125, 480, 0.01);
  out.clear();
  SM_EXPECT_TRUE(ts.PreemptiveExpand(quiet, 400, 10, bgn, out, &change) == Result::kSuccessLowEnergy);
  SM_EXPECT_LE(change, 80u);
  SM_EXPECT_EQ(out.size(), quiet.size() + change);
  SM_EXPECT_TRUE(SamePrefix(out, quiet, 400));

  out.clear();
  SM_EXPECT_TRUE(ts.PreemptiveExpand(quiet, 470, 10, bgn, out, &change) == Result::kError);
  SM_EXPECT_TRUE(out == quiet);
}

// The jitter buffer under version 2: a 200 ms minimum delay is reached by
// preemptive expand and, once it is lifted, left by accelerate. Each one
// adds or removes one 8 ms period of the 125 Hz signal (libwebrtc's DSP),
// not an arbitrary length, and nothing is concealed.
SM_TEST(AudioJitterBufferNetEqV2, TimeStretchMovesWholePitchPeriods) {
  AudioJitterBufferConfig cfg;
  AudioJitterBuffer jb(cfg);
  std::vector<float> out(160);
  const std::vector<float> voice = Voiced(16000, 125, 320 * 300, 0.4);
  jb.SetMinimumDelayMs(200);
  uint64_t accelerations = 0, expansions = 0;
  for (int i = 0; i < 300; ++i) {
    if (i == 150) jb.SetMinimumDelayMs(0);
    jb.InsertPacket(static_cast<uint32_t>(i * 320), std::vector<float>(voice.begin() + i * 320, voice.begin() + (i + 1) * 320),
                    Timestamp::Millis(i * 20));
    for (int k = 0; k < 2; ++k) {
      const AudioOperation op = jb.GetAudio(out, Timestamp::Millis(i * 20 + k * 10));
      if (op == AudioOperation::kAccelerate) ++accelerations;
      if (op == AudioOperation::kPreemptiveExpand) ++expansions;
    }
    if (i == 149) {
      SM_EXPECT_EQ(accelerations, 0u);
      SM_EXPECT_GT(jb.CurrentLevelMs(), 180.0);
    }
  }
  SM_ASSERT_GT(expansions, 0u);
  SM_ASSERT_GT(accelerations, 0u);
  const auto& st = jb.stats();
  SM_EXPECT_NEAR(static_cast<double>(st.inserted_samples_for_deceleration) / static_cast<double>(expansions), 128.0, 2.0);
  SM_EXPECT_NEAR(static_cast<double>(st.removed_samples_for_acceleration) / static_cast<double>(accelerations), 128.0, 2.0);
  SM_EXPECT_LT(jb.CurrentLevelMs(), 60.0);
  SM_EXPECT_EQ(st.concealed_samples, 0u);
}

// The time stretch is bit-exact with libwebrtc's on the same int16 input:
// the pitch peak (DspHelper::PeakDetection and ParabolicFit in fixed point),
// the decisions and AudioVector::CrossFade's Q14 rounding (#644).
SM_TEST(NetEqTimeStretch, BitExactWithLibwebrtc) {
  for (const Golden& g : kGolden) {
    const size_t fm = static_cast<size_t>(g.rate / 8000);
    NetEqBackgroundNoise bgn;
    if (g.bgn) SM_ASSERT_TRUE(bgn.Update(ToFloat(GoldenSignal(1, 512, 1, 1200, 99))));
    uint64_t h = 1469598103934665603ull;
    int counts[4] = {0, 0, 0, 0};
    for (int kind = 0; kind < 4; ++kind) {
      for (int64_t p8 : {20, 27, 40, 55, 64, 90, 119}) {
        for (int64_t a : {300, 3000, 12000}) {
          for (size_t len8 : {240, 320}) {
            const size_t n = len8 * fm;
            const std::vector<float> x = ToFloat(GoldenSignal(kind, n, p8 * static_cast<int64_t>(fm), a,
                                                              static_cast<uint32_t>(p8 * 7 + a + kind)));
            NetEqTimeStretch ts(g.rate);
            std::vector<float> out;
            size_t change = 0;
            Result r;
            if (g.mode < 2) {
              r = ts.Accelerate(x, g.mode == 1, bgn, out, &change);
            } else {
              const size_t overlap = 5 * fm;
              const size_t old = g.mode == 2 ? 0 : g.mode == 3 ? 60 * fm : g.mode == 4 ? 130 * fm : n - overlap;
              r = ts.PreemptiveExpand(x, old, overlap, bgn, out, &change);
            }
            const int idx = r == Result::kSuccess ? 0 : r == Result::kSuccessLowEnergy ? 1 : r == Result::kNoStretch ? 2 : 3;
            ++counts[idx];
            h = Mix(h, static_cast<uint64_t>(idx));
            h = Mix(h, change);
            h = Mix(h, out.size());
            for (float v : out) h = Mix(h, static_cast<uint16_t>(static_cast<int16_t>(std::lround(v * 32768.0f))));
          }
        }
      }
    }
    for (int i = 0; i < 4; ++i) SM_EXPECT_EQ(counts[i], g.counts[i]);
    SM_EXPECT_EQ(h, g.hash);
  }
}
