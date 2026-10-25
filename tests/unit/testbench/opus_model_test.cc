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
  // libwebrtc's frames on parity-data's traces (issue #375): 71.1 B, sd 12.9 B; 17.6 % held at 81 B; 95-158 B in bursts.
  const auto speech = sim::SpeechLike(16000, 60, 8, false);  // No pauses: all frames carry speech.
  OpusSizeModel m(5, 32000);
  double sum = 0, sum2 = 0;
  size_t lo = 1000, hi = 0, n = 0, held = 0, burst = 0;
  for (size_t off = 0; off + 320 <= speech.size(); off += 320, ++n) {
    const size_t b = m.NextFrameBytes(std::span<const float>(speech.data() + off, 320));
    sum += static_cast<double>(b);
    sum2 += static_cast<double>(b) * static_cast<double>(b);
    lo = std::min(lo, b);
    hi = std::max(hi, b);
    held += b == 81;
    burst += b >= 95;
  }
  const double mean = sum / static_cast<double>(n);
  const double sd = std::sqrt(sum2 / static_cast<double>(n) - mean * mean);
  SM_EXPECT_TRUE(mean > 67 && mean < 75);
  SM_EXPECT_TRUE(sd > 10 && sd < 16);
  SM_EXPECT_TRUE(static_cast<double>(held) > 0.10 * static_cast<double>(n) && static_cast<double>(held) < 0.26 * static_cast<double>(n));
  SM_EXPECT_TRUE(static_cast<double>(burst) > 0.01 * static_cast<double>(n) && static_cast<double>(burst) < 0.05 * static_cast<double>(n));
  SM_EXPECT_TRUE(lo >= 36 && hi <= 158);
}

// The 200 B RTP packets (RED: 25 B of headers, the previous and the current
// frame) start the start probe, so their share and the time of the first one
// follow libwebrtc's (issue #375): 3.6 % of audio packets over seeds 1, 2, 3,
// 4, 5, 7 and 10 (1.4-4.9 % per seed), the first at 1.39-10.2 s (median 3.79 s).
SM_TEST(OpusModel, LargeRedPacketsAsLibwebrtcs) {
  std::vector<double> firsts;
  size_t large = 0, packets = 0;
  double early = 0, later = 0;
  size_t n_early = 0, n_later = 0;
  for (uint64_t seed = 1; seed <= 10; ++seed) {
    const auto speech = sim::SpeechLike(16000, 30, seed + 7, true);  // As testbench/call.cc.
    OpusSizeModel m(seed + 17, 32000);
    size_t prev = 0;
    double first = -1;
    for (size_t k = 0; (k + 1) * 320 <= speech.size(); ++k) {
      const size_t b = m.NextFrameBytes(std::span<const float>(speech.data() + k * 320, 320));
      if (k > 0 && 25 + prev + b >= 200) {
        ++large;
        if (first < 0) first = static_cast<double>(k) * 0.02;
      }
      if (b >= 36 && b < 95 && b != 81) {  // Free frames: quieter before the encoder's ramp.
        if (k < 70) {
          early += static_cast<double>(b);
          ++n_early;
        } else if (k >= 140) {
          later += static_cast<double>(b);
          ++n_later;
        }
      }
      ++packets;
      prev = b;
    }
    firsts.push_back(first < 0 ? 30 : first);
  }
  const double share = static_cast<double>(large) / static_cast<double>(packets);
  SM_EXPECT_TRUE(share > 0.025 && share < 0.05);
  std::sort(firsts.begin(), firsts.end());
  SM_EXPECT_TRUE(firsts[5] > 2.5 && firsts[5] < 5.5);  // Median.
  SM_EXPECT_TRUE(firsts[1] > 1.2);                      // At most 1 of 10 seeds before libwebrtc's earliest.
  SM_EXPECT_TRUE(early / static_cast<double>(n_early) < 64);   // libwebrtc: 61 B in the first 1.4 s,
  SM_EXPECT_TRUE(later / static_cast<double>(n_later) > 65);   // 67.5 B after 2.8 s.
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

// AudioEncoderOpusImpl's loss input (audio_encoder_opus.cc):
// PacketLossFractionSmoother, then SetProjectedPacketLossRate.
SM_TEST(OpusModel, PacketLossRateIsSmoothedClampedAndRounded) {
  OpusPacketLossRate r(Timestamp::Millis(0));
  SM_EXPECT_EQ(r.percent(), 0);
  r.OnReceivedUplinkPacketLossFraction(0.1f, Timestamp::Millis(1000));  // The first sample is taken as is.
  SM_EXPECT_EQ(r.percent(), 10);
  // 10 s later a 0 report: 0.9999^10000 = 0.368 of the old value is kept.
  r.OnReceivedUplinkPacketLossFraction(0.0f, Timestamp::Millis(11000));
  SM_EXPECT_EQ(r.percent(), 4);
  OpusPacketLossRate high(Timestamp::Millis(0));
  high.OnReceivedUplinkPacketLossFraction(0.5f, Timestamp::Millis(1000));
  SM_EXPECT_EQ(high.percent(), 20);  // kMaxPacketLossFraction.
  OpusPacketLossRate low(Timestamp::Millis(0));
  low.OnReceivedUplinkPacketLossFraction(0.004f, Timestamp::Millis(1000));
  SM_EXPECT_EQ(low.percent(), 0);  // Rounded to a whole percent: no LBRR.
  low.OnReceivedUplinkPacketLossFraction(0.006f, Timestamp::Millis(1001));
  SM_EXPECT_EQ(low.percent(), 0);  // 0.0040002 after one ms.
}

SM_TEST(OpusModel, LastFrameActiveFollowsTheSignal) {
  OpusSizeModel m(3, 32000);
  SM_EXPECT_FALSE(m.LastFrameActive());  // No frame yet.
  const std::vector<float> zeros(320, 0.f);
  const auto speech = sim::SpeechLike(16000, 1.0, 3, false);
  m.NextFrameBytes(std::span<const float>(speech.data() + 3200, 320));
  SM_EXPECT_TRUE(m.LastFrameActive());
  m.NextFrameBytes(zeros);
  SM_EXPECT_FALSE(m.LastFrameActive());
}

// libopus's compute_equiv_rate (src/opus_encoder.c): 20 ms VBR, complexity 9.
SM_TEST(OpusModel, EquivRateIsLibopuss) {
  SM_EXPECT_EQ(OpusFecDecision::EquivRate(32000, OpusFecDecision::kModeSilkOnly, 9, 0), 31680);
  SM_EXPECT_EQ(OpusFecDecision::EquivRate(32000, OpusFecDecision::kModeSilkOnly, 9, 10), 31680 - 31680 * 10 / 70);
  SM_EXPECT_EQ(OpusFecDecision::EquivRate(32000, OpusFecDecision::kModeUnknown, 9, 10), 31680 - 31680 * 10 / 140);
  SM_EXPECT_EQ(OpusFecDecision::EquivRate(32000, OpusFecDecision::kModeCeltOnly, 9, 10), 31680);
}

// decide_fec: fec_thresholds with 1000 bps of hysteresis, scaled by (125 - loss) %.
SM_TEST(OpusModel, DecideFecThresholdsAreLibopuss) {
  int bw = OpusFecDecision::kFullband;
  // (22000 + 1000) * 124 / 100 = 28504 after silk_SMULWB.
  SM_EXPECT_TRUE(OpusFecDecision::DecideFec(1, false, OpusFecDecision::kModeSilkOnly, &bw, 28505));
  SM_EXPECT_FALSE(OpusFecDecision::DecideFec(1, false, OpusFecDecision::kModeSilkOnly, &bw, 28504));
  // (22000 - 1000) * 124 / 100 = 26026 once LBRR is on.
  SM_EXPECT_TRUE(OpusFecDecision::DecideFec(1, true, OpusFecDecision::kModeSilkOnly, &bw, 26027));
  SM_EXPECT_FALSE(OpusFecDecision::DecideFec(0, true, OpusFecDecision::kModeSilkOnly, &bw, 100000));
  SM_EXPECT_FALSE(OpusFecDecision::DecideFec(10, false, OpusFecDecision::kModeCeltOnly, &bw, 100000));
  SM_EXPECT_EQ(bw, static_cast<int>(OpusFecDecision::kFullband));
  // Above 5% loss the bandwidth goes down until LBRR fits: NB needs (12000 + 1000) * 115 / 100.
  SM_EXPECT_TRUE(OpusFecDecision::DecideFec(10, false, OpusFecDecision::kModeSilkOnly, &bw, 16000));
  SM_EXPECT_EQ(bw, static_cast<int>(OpusFecDecision::kNarrowband));
  // Nothing fits: no LBRR, the bandwidth is restored.
  bw = OpusFecDecision::kWideband;
  SM_EXPECT_FALSE(OpusFecDecision::DecideFec(10, false, OpusFecDecision::kModeSilkOnly, &bw, 10000));
  SM_EXPECT_EQ(bw, static_cast<int>(OpusFecDecision::kWideband));
  // At or below 5% the bandwidth is kept.
  SM_EXPECT_FALSE(OpusFecDecision::DecideFec(5, false, OpusFecDecision::kModeSilkOnly, &bw, 10000));
  SM_EXPECT_EQ(bw, static_cast<int>(OpusFecDecision::kWideband));
}

SM_TEST(OpusModel, FecDecisionPerPacket) {
  // libwebrtc's 32 kbps: LBRR at any loss up to the 20% clamp, full band.
  for (int loss : {1, 2, 5, 10, 20}) {
    OpusFecDecision d;
    SM_EXPECT_TRUE(d.Update(32000, loss));
    SM_EXPECT_EQ(d.bandwidth(), static_cast<int>(OpusFecDecision::kFullband));
  }
  OpusFecDecision none;
  SM_EXPECT_FALSE(none.Update(32000, 0));
  // 12 kbps codes wide band and has no rate for LBRR, even at 10% loss.
  OpusFecDecision low;
  SM_EXPECT_FALSE(low.Update(12000, 3));
  SM_EXPECT_EQ(low.bandwidth(), static_cast<int>(OpusFecDecision::kWideband));
  SM_EXPECT_FALSE(low.Update(12000, 10));
  // 20 kbps at 10% loss: LBRR in narrow band.
  OpusFecDecision mid;
  SM_EXPECT_TRUE(mid.Update(20000, 10));
  SM_EXPECT_EQ(mid.bandwidth(), static_cast<int>(OpusFecDecision::kNarrowband));
  // Hysteresis: 29 kbps at 1% (equiv 26916) is below the switch-on threshold
  // (28504) but above the switch-off one (26026).
  OpusFecDecision h;
  SM_EXPECT_FALSE(h.Update(29000, 1));
  SM_EXPECT_TRUE(h.Update(32000, 1));
  SM_EXPECT_TRUE(h.Update(29000, 1));
  SM_EXPECT_FALSE(h.Update(29000, 0));
  SM_EXPECT_FALSE(h.Update(29000, 1));
}
