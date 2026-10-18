// The Opus send path of `audio v2` (issue #126): libwebrtc's encoder defaults
// and the frame-size model fitted to its output.
#include <algorithm>
#include <cmath>
#include <vector>

#include "opus_model.h"
#include "sm/sim/signals.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::testbench;

SM_TEST(OpusModel, DefaultsAreLibwebrtcs) {
  // api/audio_codecs/opus/audio_encoder_opus_config.h
  SM_EXPECT_EQ(OpusDefaults::kFrameSizeMs, 20);
  SM_EXPECT_EQ(OpusDefaults::kBitrateBps, 32000);
  SM_EXPECT_EQ(OpusDefaults::kMinBitrateBps, 6000);
  SM_EXPECT_EQ(OpusDefaults::kMaxBitrateBps, 510000);
  SM_EXPECT_FALSE(OpusDefaults::kCbr);
  SM_EXPECT_FALSE(OpusDefaults::kDtx);
}

SM_TEST(OpusModel, BitrateIsAllocationLessOverhead) {
  // OnReceivedUplinkAllocation: overhead_bps = bytes * 8 * 100 / Num10MsFramesInNextPacket.
  SM_EXPECT_EQ(OpusBitrateBps(32000 + 53 * 8 * 50, 53), 32000);
  SM_EXPECT_EQ(OpusBitrateBps(10000, 53), 6000);       // Clamped to the encoder's minimum.
  SM_EXPECT_EQ(OpusBitrateBps(900000, 0), 510000);     // And its maximum.
  SM_EXPECT_EQ(OpusBitrateBps(60000, 40, 40), 60000 - 40 * 8 * 25);
}

SM_TEST(OpusModel, SilenceIsSmallAndConstantPerRun) {
  OpusSizeModel m(1, 32000);
  const std::vector<float> zeros(320, 0.f);
  const size_t first = m.NextFrameBytes(zeros);
  SM_EXPECT_TRUE(first == 3 || first == 17 || first == 21 || first == 32 || first == 35);
  for (int i = 0; i < 20; ++i) SM_EXPECT_EQ(m.NextFrameBytes(zeros), first);
}

SM_TEST(OpusModel, SpeechMatchesLibwebrtcs32kbpsFrames) {
  const auto speech = sim::SpeechLike(16000, 60, 8, false);  // No pauses: all frames carry speech.
  OpusSizeModel m(5, 32000);
  double sum = 0, sum2 = 0;
  size_t lo = 1000, hi = 0, n = 0;
  for (size_t off = 0; off + 320 <= speech.size(); off += 320, ++n) {
    const size_t b = m.NextFrameBytes(std::span<const float>(speech.data() + off, 320));
    sum += static_cast<double>(b);
    sum2 += static_cast<double>(b) * static_cast<double>(b);
    lo = std::min(lo, b);
    hi = std::max(hi, b);
  }
  const double mean = sum / static_cast<double>(n);
  const double sd = std::sqrt(sum2 / static_cast<double>(n) - mean * mean);
  SM_EXPECT_TRUE(mean > 68 && mean < 80);  // libwebrtc: 72 B on speech, 20 ms at 32 kbps VBR.
  SM_EXPECT_TRUE(sd > 6 && sd < 20);       // libwebrtc: 12.9 B.
  SM_EXPECT_TRUE(lo >= 36 && hi <= 144);
}

SM_TEST(OpusModel, SizeScalesWithTheBitrateAndIsDeterministic) {
  const auto speech = sim::SpeechLike(16000, 20, 3, false);
  OpusSizeModel a(9, 32000), b(9, 32000), c(9, 16000);
  double sa = 0, sc = 0;
  for (size_t off = 0; off + 320 <= speech.size(); off += 320) {
    std::span<const float> f(speech.data() + off, 320);
    const size_t x = a.NextFrameBytes(f);
    SM_EXPECT_EQ(x, b.NextFrameBytes(f));
    sa += static_cast<double>(x);
    sc += static_cast<double>(c.NextFrameBytes(f));
  }
  SM_EXPECT_TRUE(sc < 0.6 * sa);
}
