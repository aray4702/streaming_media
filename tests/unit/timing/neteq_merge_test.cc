// NetEq's Merge (issue #62): libwebrtc M157's merge_unittest.cc, with the
// sync buffer pre-loaded with a synthetic voiced signal instead of
// testfile32kHz.pcm, plus tests of the ported decisions (the alignment,
// SignalScaling's mute factor, the unmuting and the crossfade) and of the
// jitter buffer's use of them.
//
// `NetEqMerge, BitExactWithLibwebrtc` (#783): the golden hashes come from
// libwebrtc itself: compare/webrtc/reference/modules/audio_coding/neteq/
// merge.cc, sync_buffer.cc, audio_multi_vector.cc, audio_vector.cc,
// dsp_helper.cc and cross_correlation.cc with the signal processing
// library's C files, compiled unchanged against the prebuilt's headers (stubs
// for rtc_base/checks.h, DCHECKs off as in a release build, and for Expand,
// which hands Merge::Process the case's period, mute factor, max lag and
// overlap), driven with ForEachMergeCase() below. Each case hashes (FNV-1a
// step h = (h ^ v) * prime) Process's return value, the sync buffer's
// unplayed samples after the merge and every output sample as uint16. The
// merge is integer only, so the hashes hold on every platform.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "sm/core/event_log.h"
#include "sm/timing/audio_jitter_buffer.h"
#include "sm/timing/neteq_expand.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::timing;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Harmonics of `f0` Hz, from sample `start` of the same signal.
std::vector<float> Voiced(int rate, double f0, size_t n, double amplitude = 0.3, size_t start = 0) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) {
    double v = 0;
    const double t = static_cast<double>(start + i) / rate;
    for (int h = 1; h <= 4; ++h) v += std::sin(2 * kPi * f0 * h * t) / h;
    x[i] = static_cast<float>(amplitude * v / 2);
  }
  return x;
}

// One expand period appended to `sync`, as NetEqImpl::DoExpand.
void ExpandInto(NetEqExpand& expand, std::vector<float>& sync) {
  std::vector<float> out;
  expand.Process(sync, out);
  sync.insert(sync.end(), out.begin(), out.end());
}

std::vector<int16_t> ToInt16(const std::vector<float>& x) {
  std::vector<int16_t> y(x.size());
  for (size_t i = 0; i < x.size(); ++i) y[i] = static_cast<int16_t>(std::lround(x[i] * 32768.0f));
  return y;
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

uint64_t Mix(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; }

struct MergeCase {
  std::vector<int16_t> sync;  // Oldest first; the last `old_length` are unplayed.
  size_t old_length;
  std::vector<int16_t> period;  // Expand's output.
  int16_t mute_factor;
  size_t max_lag;
  size_t overlap_length;
  std::vector<int16_t> input;
};

// Every case at `fs_hz`: 4 signal kinds x 5 pitch periods x 3 unplayed
// lengths x 3 decoded lengths (10 ms, which takes Downsample's short path,
// 20 and 60 ms) x 3 Expand mute factors x 3 expansion / decoded amplitudes.
template <class F>
void ForEachMergeCase(int fs_hz, F f) {
  const size_t m = static_cast<size_t>(fs_hz / 8000);
  const size_t pitches[] = {20, 37, 64, 97, 120};
  const size_t olds[] = {5, 18, 80};
  const size_t inputs[] = {80, 160, 480};
  const int16_t mutes[] = {16384, 9000, 0};
  const int64_t amps[][2] = {{8000, 8000}, {2000, 12000}, {12000, 2000}};
  uint32_t seed = 1;
  for (int kind = 0; kind < 4; ++kind) {
    for (size_t p : pitches) {
      for (size_t old : olds) {
        for (size_t in : inputs) {
          for (int16_t mute : mutes) {
            for (const auto& a : amps) {
              ++seed;
              MergeCase c;
              const size_t pitch = p * m;
              c.sync = GoldenSignal(kind, 1500 * m, static_cast<int64_t>(pitch), a[0], seed);
              c.old_length = old * m;
              c.period = GoldenSignal(kind, pitch, static_cast<int64_t>(pitch), a[0], seed + 7);
              c.mute_factor = mute;
              c.max_lag = pitch + (seed % 3) * m;
              c.overlap_length = 5 * m;
              c.input = GoldenSignal(kind, in * m, static_cast<int64_t>(pitch), a[1], seed + 13);
              f(c);
            }
          }
        }
      }
    }
  }
}

struct Recorder : EventSink {
  std::vector<Event> events;
  void OnEvent(const Event& e) override { events.push_back(e); }
};

}  // namespace

// merge_unittest.cc Process: a primed Expand, then Merge with very short
// decoded inputs (around Downsample's signal offset of 2 and its 10 ms
// length limit of 80 at 8 kHz) and a 20 ms one.
SM_TEST(NetEqMerge, ProcessShortInputs) {
  for (size_t input_len : {1u, 2u, 3u, 80u, 81u, 160u}) {
    NetEqExpand expand(8000);
    NetEqMerge merge(8000);
    // 720 ms of speech, 5 ms of it left to play.
    std::vector<float> sync = Voiced(8000, 180, 720 * 8);
    std::vector<float> out;
    SM_EXPECT_GT(expand.Process(sync, out), 0u);
    out.clear();
    const std::vector<float> input(input_len, 17.0f / 32768.0f);
    const size_t n = merge.Process(input, sync, 40, expand, out);
    SM_EXPECT_GT(n, 0u);
    SM_EXPECT_EQ(out.size(), n);
    SM_EXPECT_EQ(merge.old_length(), 40u);
    // The sync buffer then holds 10 ms plus Expand's overlap, and at least
    // all the old samples (CorrelateAndPeakSearch's two criteria).
    SM_EXPECT_GE(n + merge.old_length(), 80u + expand.overlap_length());
    SM_EXPECT_EQ(n + merge.old_length(), merge.best_correlation_index() + input_len);
  }
}

// SignalScaling: sqrt(expanded / input energy) in Q14 when the decoded audio
// is the louder, else 1.
SM_TEST(NetEqMerge, SignalScaling) {
  const std::vector<float> quiet = Voiced(16000, 200, 320, 0.05);
  const std::vector<float> loud = Voiced(16000, 200, 320, 0.2);
  const auto q = ToInt16(quiet), l = ToInt16(loud);
  // 4x the amplitude: 1/4 = 4096 in Q14, to within the integer rounding.
  const int16_t f = NetEqMerge::SignalScaling(l.data(), l.size(), q.data(), 2);
  SM_EXPECT_GE(f, 4000);
  SM_EXPECT_LE(f, 4100);
  SM_EXPECT_EQ(NetEqMerge::SignalScaling(q.data(), q.size(), l.data(), 2), 16384);
  SM_EXPECT_EQ(NetEqMerge::SignalScaling(q.data(), q.size(), q.data(), 2), 16384);
  // Only the first 64 * fs/8000 samples count.
  std::vector<int16_t> tail_loud = q;
  for (size_t i = 128; i < tail_loud.size(); ++i) tail_loud[i] = l[i];
  SM_EXPECT_EQ(NetEqMerge::SignalScaling(tail_loud.data(), tail_loud.size(), q.data(), 2), 16384);
  // Silence on both sides.
  const std::vector<int16_t> zeros(320, 0);
  SM_EXPECT_EQ(NetEqMerge::SignalScaling(zeros.data(), zeros.size(), zeros.data(), 2), 16384);
}

// The decoded audio goes in where its waveform continues the expansion: on
// a periodic signal the alignment puts it in phase with the expansion
// (within the 4 kHz search's resolution), wherever in the period the
// packet starts.
SM_TEST(NetEqMerge, AlignsTheDecodedAudioWithTheExpansion) {
  constexpr size_t kPeriod = 80;  // 200 Hz at 16 kHz.
  for (size_t phase : {0u, 20u, 37u, 61u}) {
    NetEqExpand expand(16000);
    NetEqMerge merge(16000);
    std::vector<float> sync = Voiced(16000, 200, 1600);
    ExpandInto(expand, sync);
    const size_t future = expand.overlap_length();
    // The expansion continues the signal from sample sync.size() - future;
    // the decoded packet starts `phase` samples into some later period.
    const size_t expansion_start = sync.size() - future;
    const std::vector<float> input = Voiced(16000, 200, 320, 0.3, expansion_start + 10 * kPeriod + phase);
    std::vector<float> out;
    merge.Process(input, sync, future, expand, out);
    const size_t best = merge.best_correlation_index();
    // In phase: expansion sample `best` is signal sample expansion_start + best.
    const size_t off = (best + kPeriod - phase % kPeriod) % kPeriod;
    SM_EXPECT_TRUE(off <= 2 || off >= kPeriod - 2);
    SM_EXPECT_LE(best, expand.max_lag() + 2);
  }
}

// After a long expansion the decoded audio starts at the larger of Expand's
// mute factor and SignalScaling's (both well below 1 here) and comes back to
// full gain linearly by the end of the packet (back_to_fullscale_inc, faster
// than 4194 / fs_mult in Q20 here).
SM_TEST(NetEqMerge, UnmutesOverTheFrame) {
  NetEqExpand expand(16000);
  NetEqMerge merge(16000);
  std::vector<float> sync = Voiced(16000, 200, 1600);
  for (int i = 0; i < 20; ++i) ExpandInto(expand, sync);
  const int16_t expand_mute = expand.MuteFactor();
  SM_EXPECT_LT(expand_mute, 8192);
  const size_t future = expand.overlap_length();
  const std::vector<float> input = Voiced(16000, 200, 320, 0.3, sync.size() - future);
  std::vector<float> out;
  const size_t n = merge.Process(input, sync, future, expand, out);
  SM_EXPECT_GE(merge.mute_factor(), expand_mute);
  SM_EXPECT_LT(merge.mute_factor(), 16384);
  // Merged sample k is decoded sample k - best; out starts at k = old_length.
  const size_t best = merge.best_correlation_index();
  SM_EXPECT_EQ(n + merge.old_length(), best + 320u);
  // The gain (out / decoded, where the decoded audio is not small) rises
  // from about the mute factor to 1 at the end.
  double first = -1, last = 0, prev = 0;
  bool rising = true;
  for (size_t i = 0; i < n; ++i) {
    const float x = input[i + future - best];
    if (std::fabs(x) < 0.05f) continue;
    const double gain = out[i] / x;
    if (first < 0) first = gain;
    rising = rising && gain >= prev - 0.02;
    prev = last = gain;
  }
  SM_EXPECT_TRUE(rising);
  // It starts at the merge's mute factor (Expand's after 20 periods, below
  // 0.5 as checked above), as merge.cc's RampSignal.
  SM_EXPECT_NEAR(first, merge.mute_factor() / 16384.0, 0.01);
  SM_EXPECT_GT(last, 0.95);
  SM_EXPECT_LE(last, 1.0 + 1e-6);
}

// After a short expansion Expand's mute factor is close to 1: the decoded
// audio is at full gain after (16384 - mute) * 64 / (4194 / fs_mult) samples.
SM_TEST(NetEqMerge, ShortExpansionUnmutesAtTheMinimumRate) {
  NetEqExpand expand(16000);
  NetEqMerge merge(16000);
  std::vector<float> sync = Voiced(16000, 200, 1600);
  ExpandInto(expand, sync);
  const size_t future = expand.overlap_length();
  // Decoded audio is int16, as from a decoder.
  std::vector<float> input = Voiced(16000, 200, 320, 0.3, sync.size() - future);
  for (float& v : input) v = static_cast<float>(std::lround(v * 32768.0f)) / 32768.0f;
  std::vector<float> out;
  const size_t n = merge.Process(input, sync, future, expand, out);
  const int mute = merge.mute_factor();
  SM_EXPECT_GE(mute, 16000);
  const size_t best = merge.best_correlation_index();
  const size_t full = static_cast<size_t>((16384 - mute) * 64 / (4194 / 2)) + 1;
  // Past the crossfade (at most 60 * fs_mult) and the ramp, out is the decoded audio.
  for (size_t i = 0; i < n; ++i) {
    const size_t k = i + future;  // Index in the merged signal.
    if (k < best + std::max<size_t>(120, full)) continue;
    SM_EXPECT_NEAR(out[i], input[k - best], 1e-6);
  }
}

// An unplayed part longer than 210 * 48000 / 8000 samples is cut to its
// first kMaxOldLength samples, with zeros put before it.
SM_TEST(NetEqMerge, LongFutureIsCut) {
  NetEqExpand expand(48000);
  NetEqMerge merge(48000);
  std::vector<float> sync = Voiced(48000, 200, 4000);
  ExpandInto(expand, sync);
  const size_t size = sync.size();
  const size_t future = NetEqMerge::kMaxOldLength + 100;
  const float first_future = sync[size - future];
  const std::vector<float> input = Voiced(48000, 200, 960);
  std::vector<float> out;
  merge.Process(input, sync, future, expand, out);
  SM_EXPECT_EQ(merge.old_length(), NetEqMerge::kMaxOldLength);
  SM_EXPECT_EQ(sync.size(), size);
  SM_EXPECT_EQ(sync[size - NetEqMerge::kMaxOldLength - 1], 0.0f);
  SM_EXPECT_EQ(sync[size - NetEqMerge::kMaxOldLength - 100], 0.0f);
  // The kept part starts with the old future's first sample (the merged
  // signal starts with the expansion, which starts with it).
  SM_EXPECT_NEAR(sync[size - NetEqMerge::kMaxOldLength], first_future, 1e-6);
}

// The jitter buffer merges the packet after a lost one through NetEqMerge:
// the playout around the merge has no jump larger than the signal's own
// steps, and the merge is logged with its alignment.
SM_TEST(AudioJitterBufferNetEqV2, MergeAfterALostPacketIsContinuous) {
  Recorder rec;
  AudioJitterBufferConfig cfg;
  cfg.version = 2;
  cfg.events = &rec;
  AudioJitterBuffer jb(cfg);
  const std::vector<float> tone = Voiced(16000, 200, 320 * 200);
  auto packet = [&](int i) { return std::vector<float>(tone.begin() + i * 320, tone.begin() + (i + 1) * 320); };
  float max_step = 0;
  for (size_t i = 1; i < tone.size(); ++i) max_step = std::max(max_step, std::fabs(tone[i] - tone[i - 1]));
  std::vector<float> out(160), played;
  for (int i = 0; i < 100; ++i) {
    if (i != 60) jb.InsertPacket(static_cast<uint32_t>(i * 320), packet(i), Timestamp::Millis(i * 20));
    for (int k = 0; k < 2; ++k) {
      jb.GetAudio(out, Timestamp::Millis(i * 20 + 10 * k));
      if (i >= 50) played.insert(played.end(), out.begin(), out.end());
    }
  }
  SM_EXPECT_GE(jb.stats().ops[static_cast<int>(AudioOperation::kMerge)], 1u);
  int merges = 0;
  for (const Event& e : rec.events) {
    if (std::string(e.name) == "merge") {
      ++merges;
      SM_EXPECT_GE(e.value, 0.0);
    }
  }
  SM_EXPECT_GE(merges, 1);
  float worst = 0;
  for (size_t i = 1; i < played.size(); ++i) worst = std::max(worst, std::fabs(played[i] - played[i - 1]));
  SM_EXPECT_LT(worst, 1.5f * max_step);
}

// Merge::Process after GetExpandedSignal, bit for bit with libwebrtc's
// merge.cc (see the top of this file): SignalScaling, Downsample's Q12
// filter, CrossCorrelationWithAutoShift normalized to 14 bits, the fixed
// point peak search, RampSignal, UnmuteSignal and CrossFade.
SM_TEST(NetEqMerge, BitExactWithLibwebrtc) {
  const struct {
    int fs_hz;
    uint64_t hash;
  } golden[] = {{8000, 0xfb20724263b5f776ull},
                {16000, 0x2ae4a81243795bc6ull},
                {32000, 0x439fa8525d9f6c60ull},
                {48000, 0x3fbcc1d89cebc94full}};
  for (const auto& g : golden) {
    uint64_t h = 1469598103934665603ull;
    size_t n = 0;
    ForEachMergeCase(g.fs_hz, [&](const MergeCase& c) {
      // GetExpandedSignal: the unplayed samples, then Expand's period
      // repeated to 202 * fs/8000 samples.
      std::vector<int16_t> expanded(c.sync.end() - static_cast<std::ptrdiff_t>(c.old_length), c.sync.end());
      const size_t required = static_cast<size_t>(202 * g.fs_hz / 8000);
      while (expanded.size() < required) expanded.insert(expanded.end(), c.period.begin(), c.period.end());
      expanded.resize(std::max(required, c.old_length));
      NetEqMerge merge(g.fs_hz);
      const size_t length = merge.MergeExpanded(c.input, expanded, c.old_length, c.period.size(), c.mute_factor,
                                                c.max_lag, c.overlap_length);
      h = Mix(h, length - c.old_length);
      for (size_t i = 0; i < length; ++i) h = Mix(h, static_cast<uint16_t>(merge.merged()[i]));
      ++n;
    });
    SM_EXPECT_EQ(n, 1620u);
    SM_EXPECT_EQ(h, g.hash);
  }
}
