// The test bench's scripted speech made as the call reads it (issue #633): sim::SpeechLikeGenerator gives
// SpeechLike's samples one at a time, and testbench::SpeechPcm gives any frame of the call's 48 kHz PCM from a few
// cached blocks, sample for sample what building the whole signal gave, without holding the whole call.
#include <cmath>
#include <cstring>
#include <numbers>
#include <vector>

#include "sm/core/random.h"
#include "sm/sim/signals.h"
#include "sm_test.h"
#include "speech_pcm.h"

using namespace sm;
using namespace sm::testbench;

namespace {

// sim::SpeechLike as it was before #633 built it, the whole signal in one loop: the generator must give these very
// samples, so the bench's audio (and sm_compare's, which still builds it whole) does not move.
std::vector<float> SpeechLikeBefore633(int sample_rate, double seconds, uint64_t seed, bool with_pauses, float amp) {
  Random rng(seed);
  size_t n = static_cast<size_t>(seconds * sample_rate);
  std::vector<float> x(n);
  double phase = 0;
  double f0 = 140, f0_target = 160;
  double syl_phase = rng.Uniform(0, 6.28);
  double syl_rate = 4.0;
  bool paused = false;
  size_t seg_left = 0;
  for (size_t i = 0; i < n; ++i) {
    if (seg_left == 0) {
      f0_target = rng.Uniform(90, 240);
      syl_rate = rng.Uniform(3, 5.5);
      paused = with_pauses && rng.Bernoulli(0.25);
      seg_left = static_cast<size_t>(rng.Uniform(0.25, 0.6) * sample_rate);
    }
    --seg_left;
    f0 += (f0_target - f0) * (20.0 / sample_rate);
    phase += 2 * std::numbers::pi * f0 / sample_rate;
    if (phase > 2 * std::numbers::pi * 1000) phase -= 2 * std::numbers::pi * 1000;
    double s = 0;
    for (int h = 1; h * f0 < sample_rate * 0.45 && h <= 20; ++h) {
      double fh = h * f0;
      double formant = 1.0 + 2.0 * std::exp(-std::pow((fh - 500) / 200, 2)) +
                       1.2 * std::exp(-std::pow((fh - 1500) / 300, 2));
      s += formant / h * std::sin(h * phase);
    }
    syl_phase += 2 * std::numbers::pi * syl_rate / sample_rate;
    double env = 0.55 + 0.45 * std::sin(syl_phase);
    x[i] = paused ? 0.0f : static_cast<float>(amp * 0.35 * env * s);
  }
  return x;
}

bool SameBits(const std::vector<float>& a, const std::vector<float>& b) {
  return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

std::vector<int16_t> WholePcm(double seconds, uint64_t seed, bool pauses) {
  std::vector<int16_t> pcm;
  for (float v : sim::SpeechLike(SpeechPcm::kSampleRateHz, seconds, seed, pauses)) pcm.push_back(OpusSendEncoder::ToPcm(v));
  return pcm;
}

// Frame k of the whole signal, as the bench cut it before #633: fewer samples at the end, none past it.
std::vector<int16_t> WholeFrame(const std::vector<int16_t>& pcm, int64_t k) {
  const size_t off = std::min(static_cast<size_t>(k) * SpeechPcm::kFrameSamples, pcm.size());
  const size_t n = std::min(SpeechPcm::kFrameSamples, pcm.size() - off);
  return std::vector<int16_t>(pcm.begin() + static_cast<std::ptrdiff_t>(off), pcm.begin() + static_cast<std::ptrdiff_t>(off + n));
}

bool FrameIs(SpeechPcm& s, const std::vector<int16_t>& pcm, int64_t k) {
  const std::span<const int16_t> got = s.Frame(k);
  const std::vector<int16_t> want = WholeFrame(pcm, k);
  return std::vector<int16_t>(got.begin(), got.end()) == want;
}

}  // namespace

SM_TEST(TestbenchSpeechPcm, GeneratorGivesTheWholeSignalsSamples) {
  for (bool pauses : {true, false}) {
    SM_EXPECT_TRUE(SameBits(sim::SpeechLike(48000, 3.0, 8, pauses), SpeechLikeBefore633(48000, 3.0, 8, pauses, 0.3f)));
  }
  SM_EXPECT_TRUE(SameBits(sim::SpeechLike(16000, 2.5, 3, true, 0.25f), SpeechLikeBefore633(16000, 2.5, 3, true, 0.25f)));
  // A copy carries on where the original is.
  sim::SpeechLikeGenerator a(48000, 5, true);
  for (int i = 0; i < 30000; ++i) a.Next();
  sim::SpeechLikeGenerator b = a;
  const std::vector<float> want = SpeechLikeBefore633(48000, 1.0, 5, true, 0.3f);
  bool same = true;
  for (size_t i = 30000; i < want.size(); ++i) {
    const float va = a.Next(), vb = b.Next();
    same = same && std::memcmp(&va, &want[i], sizeof(float)) == 0 && std::memcmp(&vb, &want[i], sizeof(float)) == 0;
  }
  SM_EXPECT_TRUE(same);
}

// Every way the bench reads the signal: at capture in order, ahead (send-ahead, #762), behind (the listeners, #960 and
// #961), far behind (remade from a kept start state), and the end of the signal.
SM_TEST(TestbenchSpeechPcm, FramesAreTheWholeSignals) {
  const double seconds = 12.51;  // 600480 samples: frame 625 is the last, 480 samples.
  const std::vector<int16_t> pcm = WholePcm(seconds, 8, true);
  SpeechPcm s(seconds, 8, true);
  SM_EXPECT_EQ(s.size(), pcm.size());
  bool ok = true;
  for (int64_t k = 0; k < 626; ++k) {
    ok = ok && FrameIs(s, pcm, k);
    if (k >= 150) ok = ok && FrameIs(s, pcm, k - 140);  // Almost 3 s behind.
    if (k % 7 == 0) ok = ok && FrameIs(s, pcm, std::min<int64_t>(k + 30, 625));  // Ahead.
  }
  SM_EXPECT_TRUE(ok);
  // Far behind: block 0 left the cache long ago.
  SM_EXPECT_TRUE(FrameIs(s, pcm, 0));
  SM_EXPECT_TRUE(FrameIs(s, pcm, 333));
  SM_EXPECT_EQ(s.Frame(625).size(), size_t{480});
  SM_EXPECT_TRUE(s.Frame(626).empty());
  SM_EXPECT_TRUE(s.Frame(-1).empty());
  // Jumping ahead first makes the blocks between without keeping them.
  SpeechPcm jump(seconds, 8, true);
  SM_EXPECT_TRUE(FrameIs(jump, pcm, 600));
  SM_EXPECT_TRUE(FrameIs(jump, pcm, 49));
  SM_EXPECT_TRUE(FrameIs(jump, pcm, 50));
}

// Issue #633's regression: what the source holds does not grow with the call. Built whole, a call of this length held
// its 48 kHz PCM, 2 bytes a sample, for the whole call plus 1 s (and the float signal, 4 bytes a sample, while it was
// built).
SM_TEST(TestbenchSpeechPcm, HoldsAFewBlocksWhateverTheLength) {
  const double seconds = 3600 + 1;
  SpeechPcm s(seconds, 1, true);
  for (int64_t k = 0; k < 20 * 50; ++k) s.Frame(k);  // The first 20 s, as a call reads them.
  const size_t bound = SpeechPcm::kCachedBlocks * SpeechPcm::kBlockSamples * sizeof(int16_t);
  SM_EXPECT_LE(s.peak_bytes(), bound);
  SM_EXPECT_LE(bound, size_t{400 * 1024});
  SM_EXPECT_LT(bound * 100, static_cast<size_t>(seconds * 48000 * sizeof(int16_t)));
}

// A block older than the kept start states is remade from the signal's start.
SM_TEST(TestbenchSpeechPcm, OlderThanTheKeptStatesStillReadsTheSignal) {
  const double seconds = static_cast<double>(SpeechPcm::kCheckpoints) + 3;
  SpeechPcm s(seconds, 4, false);
  s.Frame(static_cast<int64_t>(SpeechPcm::kCheckpoints + 2) * 50);  // Past the kept states of the first blocks.
  const std::vector<int16_t> head = WholePcm(2.0, 4, false);
  SM_EXPECT_TRUE(FrameIs(s, head, 0));
  SM_EXPECT_TRUE(FrameIs(s, head, 77));
}
