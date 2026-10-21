// Port of libwebrtc's aec_state_unittest.cc (`apm v2`, #240 step 3):
// NormalUsage for 1, 2 and 8 render and capture channels, and
// ConvergedFilterDelay, with their signals, lengths and pass bounds.
#include "sm/audio/aec3_state.h"

#include <algorithm>
#include <tuple>

#include "sm_test.h"

using namespace sm;
using namespace sm::audio::aec3;

namespace {

void Fill(Block& x, int band, int ch, float value) {
  auto v = x.View(band, ch);
  std::fill(v.begin(), v.end(), value);
}

void RunNormalUsageTest(size_t num_render_channels, size_t num_capture_channels) {
  constexpr int kSampleRateHz = 48000;
  constexpr size_t kNumBands = NumBandsForRate(kSampleRateHz);
  Aec3Config config;
  AecState state(config, num_capture_channels);
  std::optional<DelayEstimate> delay_estimate = DelayEstimate(DelayEstimate::Quality::kRefined, 10);
  auto render_delay_buffer = std::make_unique<RenderDelayBuffer>(config, kSampleRateHz, num_render_channels);
  std::vector<std::array<float, kFftLengthBy2Plus1>> E2_refined(num_capture_channels);
  std::vector<std::array<float, kFftLengthBy2Plus1>> Y2(num_capture_channels);
  Block x(static_cast<int>(kNumBands), static_cast<int>(num_render_channels));
  std::vector<std::array<float, kBlockSize>> y(num_capture_channels);
  std::vector<SubtractorOutput> subtractor_output(num_capture_channels);
  for (size_t ch = 0; ch < num_capture_channels; ++ch) {
    subtractor_output[ch].Reset();
    subtractor_output[ch].s_refined.fill(100.f);
    subtractor_output[ch].e_refined.fill(100.f);
    y[ch].fill(1000.f);
    E2_refined[ch].fill(0.f);
    Y2[ch].fill(0.f);
  }
  std::vector<std::vector<std::array<float, kFftLengthBy2Plus1>>> converged_filter_frequency_response(
      num_capture_channels, std::vector<std::array<float, kFftLengthBy2Plus1>>(10));
  for (auto& v_ch : converged_filter_frequency_response) {
    for (auto& v : v_ch) v.fill(0.01f);
  }
  converged_filter_frequency_response[0][2].fill(100.f);
  converged_filter_frequency_response[0][2][0] = 1.f;
  std::vector<std::vector<float>> impulse_response(
      num_capture_channels,
      std::vector<float>(static_cast<size_t>(GetTimeDomainLength(static_cast<int>(config.filter.refined.length_blocks))),
                         0.f));

  auto update = [&]() {
    for (size_t ch = 0; ch < num_capture_channels; ++ch) subtractor_output[ch].ComputeMetrics(y[ch]);
    state.Update(delay_estimate, converged_filter_frequency_response, impulse_response,
                 *render_delay_buffer->GetRenderBuffer(), E2_refined, Y2, subtractor_output);
  };

  // Verify that linear AEC usability is true when the filter is converged.
  for (size_t band = 0; band < kNumBands; ++band) {
    for (size_t ch = 0; ch < num_render_channels; ++ch) Fill(x, static_cast<int>(band), static_cast<int>(ch), 101.f);
  }
  for (int k = 0; k < 3000; ++k) {
    render_delay_buffer->Insert(x);
    update();
  }
  SM_EXPECT_TRUE(state.UsableLinearEstimate());

  // Verify that linear AEC usability becomes false after an echo path change
  // is reported.
  for (size_t ch = 0; ch < num_capture_channels; ++ch) subtractor_output[ch].ComputeMetrics(y[ch]);
  state.HandleEchoPathChange(
      EchoPathVariability(false, EchoPathVariability::DelayAdjustment::kNewDetectedDelay, false));
  state.Update(delay_estimate, converged_filter_frequency_response, impulse_response,
               *render_delay_buffer->GetRenderBuffer(), E2_refined, Y2, subtractor_output);
  SM_EXPECT_FALSE(state.UsableLinearEstimate());

  // Verify that the active render detection works as intended.
  for (size_t ch = 0; ch < num_render_channels; ++ch) Fill(x, 0, static_cast<int>(ch), 101.f);
  render_delay_buffer->Insert(x);
  for (size_t ch = 0; ch < num_capture_channels; ++ch) subtractor_output[ch].ComputeMetrics(y[ch]);
  state.HandleEchoPathChange(
      EchoPathVariability(true, EchoPathVariability::DelayAdjustment::kNewDetectedDelay, false));
  state.Update(delay_estimate, converged_filter_frequency_response, impulse_response,
               *render_delay_buffer->GetRenderBuffer(), E2_refined, Y2, subtractor_output);
  SM_EXPECT_FALSE(state.ActiveRender());

  for (int k = 0; k < 1000; ++k) {
    render_delay_buffer->Insert(x);
    update();
  }
  SM_EXPECT_TRUE(state.ActiveRender());

  // Verify that the ERL is properly estimated.
  for (int band = 0; band < x.NumBands(); ++band) {
    for (int channel = 0; channel < x.NumChannels(); ++channel) Fill(x, band, channel, 0.f);
  }
  for (size_t ch = 0; ch < num_render_channels; ++ch) x.View(0, static_cast<int>(ch))[0] = 5000.f;
  for (size_t k = 0; k < render_delay_buffer->GetRenderBuffer()->GetFftBuffer().size(); ++k) {
    render_delay_buffer->Insert(x);
    if (k == 0) render_delay_buffer->Reset();
    render_delay_buffer->PrepareCaptureProcessing();
  }

  for (auto& Y2_ch : Y2) Y2_ch.fill(10.f * 10000.f * 10000.f);
  for (size_t k = 0; k < 1000; ++k) update();

  SM_ASSERT_TRUE(state.UsableLinearEstimate());
  const std::array<float, kFftLengthBy2Plus1>& erl = state.Erl();
  SM_EXPECT_EQ(erl[0], erl[1]);
  for (size_t k = 1; k < erl.size() - 1; ++k) SM_EXPECT_NEAR(k % 2 == 0 ? 10.f : 1000.f, erl[k], 0.1);
  SM_EXPECT_EQ(erl[erl.size() - 2], erl[erl.size() - 1]);

  // Verify that the ERLE is properly estimated.
  for (auto& E2_refined_ch : E2_refined) E2_refined_ch.fill(1.f * 10000.f * 10000.f);
  for (auto& Y2_ch : Y2) Y2_ch.fill(10.f * E2_refined[0][0]);
  for (size_t k = 0; k < 1000; ++k) update();
  SM_ASSERT_TRUE(state.UsableLinearEstimate());
  {
    // The render spectrum is built so it has energy only in the even bands.
    const auto& erle = state.Erle(/*onset_compensated=*/true)[0];
    SM_EXPECT_EQ(erle[0], erle[1]);
    constexpr size_t kLowFrequencyLimit = 32;
    for (size_t k = 2; k < kLowFrequencyLimit; k = k + 2) SM_EXPECT_NEAR(4.f, erle[k], 0.1);
    for (size_t k = kLowFrequencyLimit; k < erle.size() - 1; k = k + 2) SM_EXPECT_NEAR(1.5f, erle[k], 0.1);
    SM_EXPECT_EQ(erle[erle.size() - 2], erle[erle.size() - 1]);
  }
  for (auto& E2_refined_ch : E2_refined) E2_refined_ch.fill(1.f * 10000.f * 10000.f);
  for (auto& Y2_ch : Y2) Y2_ch.fill(5.f * E2_refined[0][0]);
  for (size_t k = 0; k < 1000; ++k) update();

  SM_ASSERT_TRUE(state.UsableLinearEstimate());
  {
    const auto& erle = state.Erle(/*onset_compensated=*/true)[0];
    SM_EXPECT_EQ(erle[0], erle[1]);
    constexpr size_t kLowFrequencyLimit = 32;
    for (size_t k = 1; k < kLowFrequencyLimit; ++k) SM_EXPECT_NEAR(k % 2 == 0 ? 4.f : 1.f, erle[k], 0.1);
    for (size_t k = kLowFrequencyLimit; k < erle.size() - 1; ++k) SM_EXPECT_NEAR(k % 2 == 0 ? 1.5f : 1.f, erle[k], 0.1);
    SM_EXPECT_EQ(erle[erle.size() - 2], erle[erle.size() - 1]);
  }
}

}  // namespace

// Verify the general functionality of AecState.
SM_TEST(Aec3State, NormalUsage) {
  for (size_t num_render_channels : {1, 2, 8}) {
    for (size_t num_capture_channels : {1, 2, 8}) RunNormalUsageTest(num_render_channels, num_capture_channels);
  }
}

// Verifies the delay for a converged filter is correctly identified.
SM_TEST(Aec3State, ConvergedFilterDelay) {
  constexpr int kFilterLengthBlocks = 10;
  constexpr size_t kNumCaptureChannels = 1;
  Aec3Config config;
  AecState state(config, kNumCaptureChannels);
  RenderDelayBuffer render_delay_buffer(config, 48000, 1);
  std::optional<DelayEstimate> delay_estimate;
  std::vector<std::array<float, kFftLengthBy2Plus1>> E2_refined(kNumCaptureChannels);
  std::vector<std::array<float, kFftLengthBy2Plus1>> Y2(kNumCaptureChannels);
  EchoPathVariability echo_path_variability(false, EchoPathVariability::DelayAdjustment::kNone, false);
  std::vector<SubtractorOutput> subtractor_output(kNumCaptureChannels);
  for (auto& output : subtractor_output) {
    output.Reset();
    output.s_refined.fill(100.f);
  }
  std::array<float, kBlockSize> y;
  y.fill(0.f);

  std::vector<std::vector<std::array<float, kFftLengthBy2Plus1>>> frequency_response(
      kNumCaptureChannels, std::vector<std::array<float, kFftLengthBy2Plus1>>(kFilterLengthBlocks));
  for (auto& v_ch : frequency_response) {
    for (auto& v : v_ch) v.fill(0.01f);
  }

  std::vector<std::vector<float>> impulse_response(
      kNumCaptureChannels,
      std::vector<float>(static_cast<size_t>(GetTimeDomainLength(static_cast<int>(config.filter.refined.length_blocks))),
                         0.f));

  // Verify that the filter delay for a converged filter is properly
  // identified (libwebrtc's test runs this without an expectation).
  for (int k = 0; k < kFilterLengthBlocks; ++k) {
    for (auto& ir : impulse_response) {
      std::fill(ir.begin(), ir.end(), 0.f);
      ir[static_cast<size_t>(k) * kBlockSize + 1] = 1.f;
    }

    state.HandleEchoPathChange(echo_path_variability);
    subtractor_output[0].ComputeMetrics(y);
    state.Update(delay_estimate, frequency_response, impulse_response, *render_delay_buffer.GetRenderBuffer(),
                 E2_refined, Y2, subtractor_output);
  }
}
