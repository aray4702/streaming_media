// Tests of AEC3's echo-path analysis (`apm v2`, #240 step 3): the port of
// libwebrtc M157's filter_analyzer_unittest.cc (FilterResize), and, since
// echo_audibility, stationarity_estimator, transparent_mode and
// subtractor_output_analyzer have no unit tests in libwebrtc, behavioural
// tests derived from their code: the filter analyzer's delay, gain and
// consistency on a synthetic impulse response, the render stationarity on
// steady white noise and after a level step, the transparent mode factory
// and the switching points of both classifiers, and the subtractor output
// thresholds.
#include "sm/audio/aec3_analysis.h"

#include <algorithm>

#include "sm/core/random.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::audio::aec3;

namespace {

void RandomizeSampleVector(Random& rng, std::span<float> v, float amplitude = 32767.f) {
  for (auto& x : v) x = static_cast<float>(rng.Uniform(-amplitude, amplitude));
}

// Inserts one block of white noise of the given amplitude and prepares the
// capture processing, as one render/capture call pair.
void FeedNoise(Random& rng, RenderDelayBuffer& buffer, Block& x, float amplitude, bool first) {
  for (int ch = 0; ch < x.NumChannels(); ++ch) RandomizeSampleVector(rng, x.View(/*band=*/0, ch), amplitude);
  buffer.Insert(x);
  if (first) buffer.Reset();
  buffer.PrepareCaptureProcessing();
}

}  // namespace

// --- filter_analyzer_unittest.cc ----------------------------------------------

// Verifies that the filter analyzer handles filter resizes properly.
SM_TEST(Aec3Analysis, FilterAnalyzerFilterResize) {
  Aec3Config c;
  std::vector<float> filter(65, 0.f);
  for (size_t num_capture_channels : {1, 2, 4}) {
    FilterAnalyzer fa(c, num_capture_channels);
    fa.SetRegionToAnalyze(filter.size());
    fa.SetRegionToAnalyze(filter.size());
    filter.resize(32);
    fa.SetRegionToAnalyze(filter.size());
  }
}

// --- FilterAnalyzer on a synthetic filter --------------------------------------

// Impulse responses with a single tap at 200 (channel 0) and 400 (channel 1):
// delays of 3 and 6 blocks, consistent once the peak has been at the same
// delay for more than 1.5 s of active render; the gain follows the high-passed
// peak only after 5 s.
SM_TEST(Aec3Analysis, FilterAnalyzerDelayGainAndConsistency) {
  Aec3Config config;
  constexpr size_t kNumCaptureChannels = 2;
  const size_t filter_size = static_cast<size_t>(GetTimeDomainLength(static_cast<int>(config.filter.refined.length_blocks)));
  std::vector<std::vector<float>> h(kNumCaptureChannels, std::vector<float>(filter_size, 0.f));
  h[0][200] = 0.5f;
  h[1][400] = -0.25f;

  FilterAnalyzer analyzer(config, kNumCaptureChannels);
  RenderDelayBuffer render_delay_buffer(config, 16000, 1);
  Block x(1, 1);
  Random rng(42U);

  bool consistent = false;
  float gain = 0.f;
  int first_consistent_block = -1;
  for (int j = 0; j < 1300; ++j) {
    FeedNoise(rng, render_delay_buffer, x, 1000.f, j == 0);
    analyzer.Update(h, *render_delay_buffer.GetRenderBuffer(), &consistent, &gain);
    if (consistent && first_consistent_block < 0) first_consistent_block = j;
    if (j < 5 * kNumBlocksPerSecond) {
      // The default gain (1) is kept while it is above the filter peak.
      SM_EXPECT_EQ(gain, config.ep_strength.default_gain);
    }
  }
  SM_EXPECT_EQ(analyzer.FilterDelaysBlocks()[0], 3);
  SM_EXPECT_EQ(analyzer.FilterDelaysBlocks()[1], 6);
  SM_EXPECT_EQ(analyzer.MinFilterDelayBlocks(), 3);
  SM_EXPECT_EQ(analyzer.FilterLengthBlocks(), 13);
  SM_EXPECT_TRUE(consistent);
  // The peak becomes significant after one sweep of the 13 blocks, then 376
  // counts at the same delay.
  SM_EXPECT_GT(first_consistent_block, 375);
  SM_EXPECT_LT(first_consistent_block, 375 + 2 * 13 + 2);
  // The largest high-passed peak over the channels: 0.5 * 0.7929742.
  SM_EXPECT_EQ(gain, 0.5f * 0.7929742f);
  SM_EXPECT_EQ(analyzer.GetAdjustedFilters()[0][201], 0.5f * -0.36072128f);
  SM_EXPECT_EQ(analyzer.GetAdjustedFilters()[0][202], 0.5f * -0.47047766f);

  // A reset restarts the consistency count and the default gain.
  analyzer.Reset();
  analyzer.Update(h, *render_delay_buffer.GetRenderBuffer(), &consistent, &gain);
  SM_EXPECT_FALSE(consistent);
  SM_EXPECT_EQ(gain, config.ep_strength.default_gain);
}

// A floor of noise taps above a tenth of the peak makes it insignificant.
SM_TEST(Aec3Analysis, FilterAnalyzerNoisyFilterIsNotConsistent) {
  Aec3Config config;
  const size_t filter_size = static_cast<size_t>(GetTimeDomainLength(static_cast<int>(config.filter.refined.length_blocks)));
  std::vector<std::vector<float>> h(1, std::vector<float>(filter_size, 0.f));
  Random rng(7U);
  RandomizeSampleVector(rng, h[0], 0.2f);
  h[0][200] = 0.5f;

  FilterAnalyzer analyzer(config, 1);
  RenderDelayBuffer render_delay_buffer(config, 16000, 1);
  Block x(1, 1);
  bool consistent = false;
  float gain = 0.f;
  for (int j = 0; j < 1000; ++j) {
    FeedNoise(rng, render_delay_buffer, x, 1000.f, j == 0);
    analyzer.Update(h, *render_delay_buffer.GetRenderBuffer(), &consistent, &gain);
    SM_ASSERT_FALSE(consistent);
  }
}

// --- StationarityEstimator / EchoAudibility -------------------------------------

// Steady white noise becomes stationary (all bands scaled to 0 once the
// filter has had time to converge); a 30 dB step makes it non-stationary.
SM_TEST(Aec3Analysis, EchoAudibilityStationaryNoise) {
  Aec3Config config;
  RenderDelayBuffer render_delay_buffer(config, 16000, 1);
  EchoAudibility audibility(/*use_render_stationarity_at_init=*/true);
  std::array<float, kFftLengthBy2Plus1> average_reverb{};
  Block x(1, 1);
  Random rng(1U);
  std::array<float, kFftLengthBy2Plus1> scaling;

  for (int j = 0; j < 4 * kNumBlocksPerSecond; ++j) {
    FeedNoise(rng, render_delay_buffer, x, 1000.f, j == 0);
    audibility.Update(*render_delay_buffer.GetRenderBuffer(), average_reverb, 0, false);
  }
  SM_EXPECT_TRUE(audibility.IsBlockStationary());
  audibility.GetResidualEchoScaling(true, scaling);
  for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) SM_EXPECT_EQ(scaling[k], 0.f);

  for (int j = 0; j < 5; ++j) {
    FeedNoise(rng, render_delay_buffer, x, 31623.f, false);
    audibility.Update(*render_delay_buffer.GetRenderBuffer(), average_reverb, 0, false);
  }
  SM_EXPECT_FALSE(audibility.IsBlockStationary());
  audibility.GetResidualEchoScaling(true, scaling);
  for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) SM_EXPECT_EQ(scaling[k], 1.f);
}

// Without render stationarity at init and before the delay is known, the
// flags are never updated: nothing is stationary.
SM_TEST(Aec3Analysis, EchoAudibilityWaitsForTheDelay) {
  Aec3Config config;
  RenderDelayBuffer render_delay_buffer(config, 16000, 1);
  EchoAudibility audibility(/*use_render_stationarity_at_init=*/false);
  std::array<float, kFftLengthBy2Plus1> average_reverb{};
  Block x(1, 1);
  Random rng(2U);
  for (int j = 0; j < 4 * kNumBlocksPerSecond; ++j) {
    FeedNoise(rng, render_delay_buffer, x, 1000.f, j == 0);
    audibility.Update(*render_delay_buffer.GetRenderBuffer(), average_reverb, 0, false);
  }
  SM_EXPECT_FALSE(audibility.IsBlockStationary());
  // Once it is, the noise estimate (trained above) makes the noise stationary.
  for (int j = 0; j < 20; ++j) {
    FeedNoise(rng, render_delay_buffer, x, 1000.f, false);
    audibility.Update(*render_delay_buffer.GetRenderBuffer(), average_reverb, 0, true);
  }
  SM_EXPECT_TRUE(audibility.IsBlockStationary());
  std::array<float, kFftLengthBy2Plus1> scaling;
  audibility.GetResidualEchoScaling(false, scaling);
  for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) SM_EXPECT_EQ(scaling[k], 1.f);
}

// --- TransparentMode -----------------------------------------------------------

SM_TEST(Aec3Analysis, TransparentModeFactory) {
  Aec3Config config;
  // M157's default (WebRTC-Aec3TransparentModeHmm not set): the legacy
  // classifier.
  auto legacy = TransparentMode::Create(config);
  SM_ASSERT_TRUE(legacy != nullptr);
  SM_EXPECT_FALSE(legacy->Active());
  config.ep_strength.bounded_erl = true;
  SM_EXPECT_TRUE(TransparentMode::Create(config) == nullptr);
  auto hmm = TransparentMode::CreateHmm();
  SM_ASSERT_TRUE(hmm != nullptr);
  SM_EXPECT_FALSE(hmm->Active());
}

// Legacy: active after more than 6 s of unsaturated render without a
// converged filter, inactive again once more than 50 blocks converged.
SM_TEST(Aec3Analysis, TransparentModeLegacySwitching) {
  auto tm = TransparentMode::Create(Aec3Config());
  SM_ASSERT_TRUE(tm != nullptr);
  for (int j = 1; j <= 6 * kNumBlocksPerSecond; ++j) {
    tm->Update(10, false, false, false, false, /*active_render=*/true, false);
    SM_ASSERT_FALSE(tm->Active());
  }
  // Saturated capture doesn't count.
  tm->Update(10, false, false, false, false, true, /*saturated_capture=*/true);
  SM_EXPECT_FALSE(tm->Active());
  tm->Update(10, false, false, false, false, true, false);
  SM_EXPECT_TRUE(tm->Active());
  for (int j = 1; j <= 50; ++j) {
    tm->Update(10, false, /*any_filter_converged=*/true, false, false, true, false);
    SM_ASSERT_TRUE(tm->Active());
  }
  tm->Update(10, false, true, false, false, true, false);
  SM_EXPECT_FALSE(tm->Active());

  // After a reset it stays off: a finite ERL was recently detected, and a
  // consistent filter at a short delay has been seen with recent convergence.
  tm->Reset();
  for (int j = 0; j < 2000; ++j) tm->Update(2, true, false, false, false, true, false);
  SM_EXPECT_FALSE(tm->Active());
}

// HMM: the transparent state probability rises from 0.2 past 0.95 during
// render without converged coarse filters, and falls below 0.5 within a few
// converged observations. Inactive render doesn't update the model.
SM_TEST(Aec3Analysis, TransparentModeHmmSwitching) {
  auto tm = TransparentMode::CreateHmm();
  int activation_block = -1;
  for (int j = 0; j < 2000 && activation_block < 0; ++j) {
    tm->Update(0, false, false, /*any_coarse_filter_converged=*/false, false, /*active_render=*/true, false);
    if (tm->Active()) activation_block = j;
  }
  // log(19 / 0.25) / log(0.999 / 0.99) is about 479 blocks.
  SM_EXPECT_GT(activation_block, 400);
  SM_EXPECT_LT(activation_block, 560);
  for (int j = 0; j < 100; ++j) tm->Update(0, false, false, true, false, /*active_render=*/false, false);
  SM_EXPECT_TRUE(tm->Active());
  int deactivation_block = -1;
  for (int j = 0; j < 10 && deactivation_block < 0; ++j) {
    tm->Update(0, false, false, true, false, true, false);
    if (!tm->Active()) deactivation_block = j;
  }
  SM_EXPECT_GE(deactivation_block, 0);
  SM_EXPECT_LT(deactivation_block, 3);
  tm->Reset();
  SM_EXPECT_FALSE(tm->Active());
}

// --- SubtractorOutputAnalyzer -------------------------------------------------

SM_TEST(Aec3Analysis, SubtractorOutputAnalyzerThresholds) {
  SubtractorOutputAnalyzer analyzer(2);
  std::vector<SubtractorOutput> out(2);
  bool any_converged = false, any_coarse_converged = false, all_diverged = false;

  // Channel 0: refined converged (e2 < 0.5 y2, y2 > 50^2 * 64). Channel 1:
  // coarse relaxed only (0.05 y2 <= e2 < 0.3 y2, y2 > 20^2 * 64).
  out[0].y2 = 50.f * 50.f * 64.f * 1.01f;
  out[0].e2_refined = 0.49f * out[0].y2;
  out[0].e2_coarse = out[0].y2;
  out[1].y2 = 20.f * 20.f * 64.f * 1.01f;
  out[1].e2_refined = out[1].y2;
  out[1].e2_coarse = 0.29f * out[1].y2;
  analyzer.Update(out, &any_converged, &any_coarse_converged, &all_diverged);
  SM_EXPECT_TRUE(any_converged);
  SM_EXPECT_TRUE(any_coarse_converged);
  SM_EXPECT_FALSE(all_diverged);
  SM_EXPECT_TRUE(analyzer.ConvergedFilters()[0]);
  SM_EXPECT_FALSE(analyzer.ConvergedFilters()[1]);

  // Coarse strict on channel 1 at a high level converges it.
  out[1].y2 = out[0].y2;
  out[1].e2_coarse = 0.04f * out[1].y2;
  analyzer.Update(out, &any_converged, &any_coarse_converged, &all_diverged);
  SM_EXPECT_TRUE(analyzer.ConvergedFilters()[1]);
  analyzer.HandleEchoPathChange();
  SM_EXPECT_FALSE(analyzer.ConvergedFilters()[0]);
  SM_EXPECT_FALSE(analyzer.ConvergedFilters()[1]);

  // Both diverged (min e2 > 1.5 y2, y2 > 30^2 * 64).
  for (auto& o : out) {
    o.y2 = 30.f * 30.f * 64.f * 1.01f;
    o.e2_refined = 1.6f * o.y2;
    o.e2_coarse = 1.51f * o.y2;
  }
  analyzer.Update(out, &any_converged, &any_coarse_converged, &all_diverged);
  SM_EXPECT_FALSE(any_converged);
  SM_EXPECT_FALSE(any_coarse_converged);
  SM_EXPECT_TRUE(all_diverged);
  // Below the level threshold nothing diverges.
  out[1].y2 = 30.f * 30.f * 64.f;
  out[1].e2_refined = out[1].e2_coarse = 2.f * out[1].y2;
  analyzer.Update(out, &any_converged, &any_coarse_converged, &all_diverged);
  SM_EXPECT_FALSE(all_diverged);
}
