// Port of libwebrtc's moving_average_spectrum_unittest.cc,
// comfort_noise_generator_unittest.cc, suppression_gain_unittest.cc,
// suppression_filter_unittest.cc and residual_echo_estimator_unittest.cc
// (`apm v2`, #240 step 4), with their signals, lengths and pass bounds. The
// death tests (null outputs, an invalid sample rate) and the neural residual
// echo estimator's metrics tests have no counterpart: there are no DCHECK
// deaths here and the neural estimator is not ported. The two nearend
// detectors have no unit tests in libwebrtc; their tests here are derived
// from the code (trigger after 12 blocks, hold for 50, early exit at strong
// echo; the subband ratio test).
#include "sm/audio/aec3_suppression.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <tuple>

#include "sm/core/random.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::audio::aec3;

namespace {

void RandomizeSampleVector(Random& rng, std::span<float> v, float amplitude = 32767.f) {
  for (auto& x : v) x = static_cast<float>(rng.Uniform(-amplitude, amplitude));
}

float Energy(std::span<const float> x) { return std::inner_product(x.begin(), x.end(), x.begin(), 0.f); }

}  // namespace

// --- moving_average_spectrum_unittest.cc -----------------------------------------

SM_TEST(Aec3Suppression, MovingAverageSpectrumAverage) {
  constexpr size_t num_elem = 4;
  constexpr size_t mem_len = 3;
  constexpr float e = 1e-6f;
  MovingAverageSpectrum ma(num_elem, mem_len);
  std::array<float, num_elem> data1 = {1, 2, 3, 4};
  std::array<float, num_elem> data2 = {5, 1, 9, 7};
  std::array<float, num_elem> data3 = {3, 3, 5, 6};
  std::array<float, num_elem> data4 = {8, 4, 2, 1};
  std::array<float, num_elem> output;

  ma.Average(data1, output);
  for (size_t i = 0; i < num_elem; ++i) SM_EXPECT_NEAR(output[i], data1[i] / 1.0f, e);
  ma.Average(data2, output);
  for (size_t i = 0; i < num_elem; ++i) SM_EXPECT_NEAR(output[i], (data1[i] + data2[i]) / 2.0f, e);
  ma.Average(data3, output);
  for (size_t i = 0; i < num_elem; ++i) SM_EXPECT_NEAR(output[i], (data1[i] + data2[i] + data3[i]) / 3.0f, e);
  ma.Average(data4, output);
  for (size_t i = 0; i < num_elem; ++i) SM_EXPECT_NEAR(output[i], (data2[i] + data3[i] + data4[i]) / 3.0f, e);
}

SM_TEST(Aec3Suppression, MovingAverageSpectrumUpdateMemoryLength) {
  constexpr size_t num_elem = 4;
  constexpr float e = 1e-6f;
  MovingAverageSpectrum ma(num_elem, 3);
  std::array<float, num_elem> data1 = {1, 2, 3, 4};
  std::array<float, num_elem> data2 = {5, 1, 9, 7};
  std::array<float, num_elem> output;

  ma.Average(data1, output);
  SM_EXPECT_NEAR(output[0], data1[0], e);

  ma.UpdateMemoryLength(1);
  ma.Average(data2, output);
  // After update, it should behave as if it was just created with mem_len = 1.
  for (size_t i = 0; i < num_elem; ++i) SM_EXPECT_NEAR(output[i], data2[i], e);
}

SM_TEST(Aec3Suppression, MovingAverageSpectrumPassThrough) {
  constexpr size_t num_elem = 4;
  constexpr float e = 1e-6f;
  MovingAverageSpectrum ma(num_elem, 1);
  const std::array<std::array<float, num_elem>, 4> data = {
      {{1, 2, 3, 4}, {5, 1, 9, 7}, {3, 3, 5, 6}, {8, 4, 2, 1}}};
  std::array<float, num_elem> output;
  for (const auto& d : data) {
    ma.Average(d, output);
    for (size_t i = 0; i < num_elem; ++i) SM_EXPECT_NEAR(output[i], d[i], e);
  }
}

// --- comfort_noise_generator_unittest.cc -----------------------------------------

SM_TEST(Aec3Suppression, ComfortNoiseGeneratorCorrectLevel) {
  auto power = [](const FftData& N) {
    std::array<float, kFftLengthBy2Plus1> N2;
    N.Spectrum(N2);
    return std::accumulate(N2.begin(), N2.end(), 0.f) / static_cast<float>(N2.size());
  };
  constexpr size_t kNumChannels = 5;
  Aec3Config config;
  ComfortNoiseGenerator cng(config, kNumChannels);
  AecState aec_state(config, kNumChannels);

  std::vector<std::array<float, kFftLengthBy2Plus1>> N2(kNumChannels);
  std::vector<FftData> n_lower(kNumChannels);
  std::vector<FftData> n_upper(kNumChannels);
  for (size_t ch = 0; ch < kNumChannels; ++ch) {
    N2[ch].fill(1000.f * 1000.f / static_cast<float>(ch + 1));
    n_lower[ch].Clear();
    n_upper[ch].Clear();
  }

  // Ensure instantaneous update to nonzero noise.
  cng.Compute(false, N2, n_lower, n_upper);
  for (size_t ch = 0; ch < kNumChannels; ++ch) {
    SM_EXPECT_LT(0.f, power(n_lower[ch]));
    SM_EXPECT_LT(0.f, power(n_upper[ch]));
  }

  for (int k = 0; k < 10000; ++k) cng.Compute(false, N2, n_lower, n_upper);

  for (size_t ch = 0; ch < kNumChannels; ++ch) {
    SM_EXPECT_NEAR(2.f * N2[ch][0], power(n_lower[ch]), N2[ch][0] / 10.f);
    SM_EXPECT_NEAR(2.f * N2[ch][0], power(n_upper[ch]), N2[ch][0] / 10.f);
  }
}

// --- suppression_gain_unittest.cc ------------------------------------------------

// Does a sanity check that the gains are correctly computed.
SM_TEST(Aec3Suppression, SuppressionGainBasicGainComputation) {
  constexpr size_t kNumRenderChannels = 1;
  constexpr size_t kNumCaptureChannels = 2;
  constexpr int kSampleRateHz = 16000;
  constexpr size_t kNumBands = NumBandsForRate(kSampleRateHz);
  Aec3Config config;
  SuppressionGain suppression_gain(config, kSampleRateHz, kNumCaptureChannels);
  RenderSignalAnalyzer analyzer(config);
  float high_bands_gain;
  std::vector<std::array<float, kFftLengthBy2Plus1>> E2(kNumCaptureChannels);
  std::vector<std::array<float, kFftLengthBy2Plus1>> S2(kNumCaptureChannels, std::array<float, kFftLengthBy2Plus1>{});
  std::vector<std::array<float, kFftLengthBy2Plus1>> Y2(kNumCaptureChannels);
  std::vector<std::array<float, kFftLengthBy2Plus1>> R2(kNumCaptureChannels);
  std::vector<std::array<float, kFftLengthBy2Plus1>> R2_unbounded(kNumCaptureChannels);
  std::vector<std::array<float, kFftLengthBy2Plus1>> N2(kNumCaptureChannels);
  std::array<float, kFftLengthBy2Plus1> g;
  std::vector<SubtractorOutput> output(kNumCaptureChannels);
  Block x(static_cast<int>(kNumBands), static_cast<int>(kNumRenderChannels));
  AecState aec_state(config, kNumCaptureChannels);
  Subtractor subtractor(config, kNumRenderChannels, kNumCaptureChannels);
  auto render_delay_buffer = std::make_unique<RenderDelayBuffer>(config, kSampleRateHz, kNumRenderChannels);
  std::optional<DelayEstimate> delay_estimate;

  // Ensure that a strong noise is detected to mask any echoes.
  for (size_t ch = 0; ch < kNumCaptureChannels; ++ch) {
    E2[ch].fill(10.f);
    Y2[ch].fill(10.f);
    R2[ch].fill(0.1f);
    R2_unbounded[ch].fill(0.1f);
    N2[ch].fill(100.0f);
  }
  for (auto& subtractor_output : output) subtractor_output.Reset();

  auto update_state = [&] {
    aec_state.Update(delay_estimate, subtractor.FilterFrequencyResponses(), subtractor.FilterImpulseResponses(),
                     *render_delay_buffer->GetRenderBuffer(), E2, Y2, output);
  };
  auto get_gain = [&] {
    suppression_gain.GetGain(config.suppressor, /*config_changed=*/false, E2, S2, R2, R2_unbounded, N2, analyzer,
                             aec_state, x, /*clock_drift=*/false, &high_bands_gain, &g);
  };

  // Ensure that the gain is no longer forced to zero.
  for (int k = 0; k <= kNumBlocksPerSecond / 5 + 1; ++k) update_state();

  for (int k = 0; k < 100; ++k) {
    update_state();
    get_gain();
  }
  for (float a : g) SM_EXPECT_NEAR(1.0f, a, 0.001f);

  // Ensure that a strong nearend is detected to mask any echoes.
  for (size_t ch = 0; ch < kNumCaptureChannels; ++ch) {
    E2[ch].fill(100.f);
    Y2[ch].fill(100.f);
    R2[ch].fill(0.1f);
    R2_unbounded[ch].fill(0.1f);
    S2[ch].fill(0.1f);
    N2[ch].fill(0.f);
  }
  for (int k = 0; k < 100; ++k) {
    update_state();
    get_gain();
  }
  for (float a : g) SM_EXPECT_NEAR(1.0f, a, 0.001f);

  // Add a strong echo to one of the channels and ensure that it is suppressed.
  E2[1].fill(1000000000.0f);
  R2[1].fill(10000000000000.0f);
  R2_unbounded[1].fill(10000000000000.0f);
  for (int k = 0; k < 10; ++k) get_gain();
  for (float a : g) SM_EXPECT_NEAR(0.0f, a, 0.001f);
}

SM_TEST(Aec3Suppression, SuppressionGainUpdateStateDependingOnConfig) {
  Aec3Config config;
  config.suppressor.nearend_tuning.max_inc_factor = 2.0f;
  config.suppressor.normal_tuning.max_dec_factor_lf = 0.2f;
  SuppressionGain suppression_gain(config, 16000, 1);

  // Initial call to set up the state.
  suppression_gain.UpdateStateDependingOnConfig(config.suppressor);
  SM_EXPECT_EQ(suppression_gain.nearend_params().max_inc_factor, 2.0f);
  SM_EXPECT_EQ(suppression_gain.normal_params().max_dec_factor_lf, 0.2f);

  // Change config and verify state is updated.
  Aec3Config new_config = config;
  new_config.suppressor.nearend_tuning.max_inc_factor = 3.0f;
  new_config.suppressor.normal_tuning.max_dec_factor_lf = 0.3f;
  suppression_gain.UpdateStateDependingOnConfig(new_config.suppressor);
  SM_EXPECT_EQ(suppression_gain.nearend_params().max_inc_factor, 3.0f);
  SM_EXPECT_EQ(suppression_gain.normal_params().max_dec_factor_lf, 0.3f);
}

// The default per-bin masking thresholds: low bands up to 5, a linear blend
// up to 8, high bands after.
SM_TEST(Aec3Suppression, SuppressionGainMaskingThresholds) {
  Aec3Config config;
  SuppressionGain suppression_gain(config, 16000, 1);
  const auto& p = suppression_gain.normal_params();
  SM_EXPECT_NEAR(p.enr_transparent_[5], 0.3f, 1e-6f);
  SM_EXPECT_NEAR(p.enr_suppress_[5], 0.4f, 1e-6f);
  SM_EXPECT_NEAR(p.enr_transparent_[8], 0.07f, 1e-6f);
  SM_EXPECT_NEAR(p.enr_suppress_[8], 0.1f, 1e-6f);
  SM_EXPECT_NEAR(p.enr_transparent_[6], 0.3f + (0.07f - 0.3f) / 3.f, 1e-6f);
  const auto& n = suppression_gain.nearend_params();
  SM_EXPECT_NEAR(n.enr_transparent_[0], 1.09f, 1e-6f);
  SM_EXPECT_NEAR(n.enr_suppress_[64], 0.3f, 1e-6f);
}

// --- suppression_filter_unittest.cc ----------------------------------------------

namespace {

void ProduceSinusoid(int sample_rate_hz, float sinusoidal_frequency_hz, size_t* sample_counter, Block* x) {
  constexpr float kPi = std::numbers::pi_v<float>;
  for (size_t k = *sample_counter, j = 0; k < (*sample_counter + kBlockSize); ++k, ++j) {
    for (int channel = 0; channel < x->NumChannels(); ++channel) {
      x->View(/*band=*/0, channel)[j] = 32767.f * std::sin(2.f * kPi * sinusoidal_frequency_hz *
                                                          static_cast<float>(k) / static_cast<float>(sample_rate_hz));
    }
  }
  *sample_counter = *sample_counter + kBlockSize;
  for (int band = 1; band < x->NumBands(); ++band) {
    for (int channel = 0; channel < x->NumChannels(); ++channel) {
      auto v = x->View(band, channel);
      std::fill(v.begin(), v.end(), 0.f);
    }
  }
}

// Runs 100 blocks of a sinusoid through the filter with the given gain and
// returns the input and output energy of band 0.
std::pair<float, float> RunSinusoid(float frequency_hz, const std::array<float, kFftLengthBy2Plus1>& gain) {
  constexpr int kSampleRateHz = 48000;
  constexpr size_t kNumBands = NumBandsForRate(kSampleRateHz);
  SuppressionFilter filter(kSampleRateHz, 1);
  std::vector<FftData> cn(1);
  std::vector<FftData> cn_high_bands(1);
  std::array<float, kFftLengthBy2> e_old;
  Aec3Fft fft;
  Block e(static_cast<int>(kNumBands), 1);
  e_old.fill(0.f);
  cn[0].Clear();
  cn_high_bands[0].Clear();

  size_t sample_counter = 0;
  float e0_input = 0.f;
  float e0_output = 0.f;
  for (size_t k = 0; k < 100; ++k) {
    ProduceSinusoid(16000, frequency_hz, &sample_counter, &e);
    e0_input += Energy(e.View(0, 0));
    std::vector<FftData> E(1);
    fft.PaddedFft(e.View(0, 0), e_old, Aec3Fft::Window::kSqrtHanning, &E[0]);
    std::copy(e.View(0, 0).begin(), e.View(0, 0).end(), e_old.begin());
    filter.ApplyGain(cn, cn_high_bands, gain, 1.f, E, &e);
    e0_output += Energy(e.View(0, 0));
  }
  return {e0_input, e0_output};
}

}  // namespace

// Verifies that no comfort noise is added when the gain is 1.
SM_TEST(Aec3Suppression, SuppressionFilterComfortNoiseInUnityGain) {
  SuppressionFilter filter(48000, 1);
  std::vector<FftData> cn(1);
  std::vector<FftData> cn_high_bands(1);
  std::array<float, kFftLengthBy2Plus1> gain;
  std::array<float, kFftLengthBy2> e_old;
  Aec3Fft fft;

  e_old.fill(0.f);
  gain.fill(1.f);
  cn[0].re.fill(1.f);
  cn[0].im.fill(1.f);
  cn_high_bands[0].re.fill(1.f);
  cn_high_bands[0].im.fill(1.f);

  Block e(3, kBlockSize);
  Block e_ref = e;

  std::vector<FftData> E(1);
  fft.PaddedFft(e.View(0, 0), e_old, Aec3Fft::Window::kSqrtHanning, &E[0]);
  std::copy(e.View(0, 0).begin(), e.View(0, 0).end(), e_old.begin());

  filter.ApplyGain(cn, cn_high_bands, gain, 1.f, E, &e);

  for (int band = 0; band < e.NumBands(); ++band) {
    for (int channel = 0; channel < e.NumChannels(); ++channel) {
      const auto e_view = e.View(band, channel);
      const auto e_ref_view = e_ref.View(band, channel);
      for (size_t sample = 0; sample < e_view.size(); ++sample) SM_EXPECT_EQ(e_ref_view[sample], e_view[sample]);
    }
  }
}

// Verifies that the suppressor is able to suppress a signal.
SM_TEST(Aec3Suppression, SuppressionFilterSignalSuppression) {
  std::array<float, kFftLengthBy2Plus1> gain;
  gain.fill(1.f);
  std::for_each(gain.begin() + 10, gain.end(), [](float& a) { a = 0.f; });
  const auto [e0_input, e0_output] = RunSinusoid(16000 * 40 / kFftLengthBy2 / 2, gain);
  SM_EXPECT_LT(e0_output, e0_input / 1000.f);
}

// Verifies that the suppressor is able to pass through a desired signal while
// applying suppressing for some frequencies.
SM_TEST(Aec3Suppression, SuppressionFilterSignalTransparency) {
  std::array<float, kFftLengthBy2Plus1> gain;
  gain.fill(1.f);
  std::for_each(gain.begin() + 30, gain.end(), [](float& a) { a = 0.f; });
  const auto [e0_input, e0_output] = RunSinusoid(16000 * 10 / kFftLengthBy2 / 2, gain);
  SM_EXPECT_LT(0.9f * e0_input, e0_output);
}

// Verifies the suppressor delay.
SM_TEST(Aec3Suppression, SuppressionFilterDelay) {
  constexpr size_t kNumChannels = 1;
  constexpr int kSampleRateHz = 48000;
  constexpr size_t kNumBands = NumBandsForRate(kSampleRateHz);

  SuppressionFilter filter(kSampleRateHz, 1);
  std::vector<FftData> cn(1);
  std::vector<FftData> cn_high_bands(1);
  std::array<float, kFftLengthBy2> e_old;
  e_old.fill(0.f);
  Aec3Fft fft;
  std::array<float, kFftLengthBy2Plus1> gain;
  Block e(static_cast<int>(kNumBands), static_cast<int>(kNumChannels));
  gain.fill(1.f);
  cn[0].Clear();
  cn_high_bands[0].Clear();

  for (size_t k = 0; k < 100; ++k) {
    for (size_t band = 0; band < kNumBands; ++band) {
      for (size_t channel = 0; channel < kNumChannels; ++channel) {
        auto e_view = e.View(static_cast<int>(band), static_cast<int>(channel));
        for (size_t sample = 0; sample < kBlockSize; ++sample) {
          e_view[sample] = static_cast<float>(k * kBlockSize + sample + channel);
        }
      }
    }

    std::vector<FftData> E(1);
    fft.PaddedFft(e.View(0, 0), e_old, Aec3Fft::Window::kSqrtHanning, &E[0]);
    std::copy(e.View(0, 0).begin(), e.View(0, 0).end(), e_old.begin());

    filter.ApplyGain(cn, cn_high_bands, gain, 1.f, E, &e);
    if (k > 2) {
      for (size_t band = 0; band < kNumBands; ++band) {
        for (size_t channel = 0; channel < kNumChannels; ++channel) {
          const auto e_view = e.View(static_cast<int>(band), static_cast<int>(channel));
          for (size_t sample = 0; sample < kBlockSize; ++sample) {
            SM_EXPECT_NEAR(static_cast<float>(k * kBlockSize + sample - kBlockSize + channel), e_view[sample], 0.01f);
          }
        }
      }
    }
  }
}

// --- residual_echo_estimator_unittest.cc -----------------------------------------

namespace {

class ResidualEchoEstimatorTest {
 public:
  static constexpr int kSampleRateHz = 48000;
  static constexpr size_t kNumBands = NumBandsForRate(kSampleRateHz);

  ResidualEchoEstimatorTest(size_t num_render_channels, size_t num_capture_channels, const Aec3Config& config)
      : num_capture_channels_(num_capture_channels),
        config_(config),
        estimator_(config_, num_render_channels),
        aec_state_(config_, num_capture_channels_),
        render_delay_buffer_(std::make_unique<RenderDelayBuffer>(config_, kSampleRateHz, num_render_channels)),
        E2_refined_(num_capture_channels_),
        S2_linear_(num_capture_channels_),
        Y2_(num_capture_channels_),
        R2_(num_capture_channels_),
        R2_unbounded_(num_capture_channels_),
        x_(static_cast<int>(kNumBands), static_cast<int>(num_render_channels)),
        H2_(num_capture_channels_, std::vector<std::array<float, kFftLengthBy2Plus1>>(10)),
        h_(num_capture_channels_,
           std::vector<float>(static_cast<size_t>(GetTimeDomainLength(
                                  static_cast<int>(config_.filter.refined.length_blocks))),
                              0.0f)),
        random_generator_(42U),
        output_(num_capture_channels_) {
    for (auto& H2_ch : H2_) {
      for (auto& H2_k : H2_ch) H2_k.fill(0.01f);
      H2_ch[2].fill(10.f);
      H2_ch[2][0] = 0.1f;
    }
    for (auto& subtractor_output : output_) {
      subtractor_output.Reset();
      subtractor_output.s_refined.fill(100.f);
    }
    constexpr float kLevel = 10.f;
    for (auto& E2_refined_ch : E2_refined_) E2_refined_ch.fill(kLevel);
    S2_linear_[0].fill(kLevel);
    for (auto& Y2_ch : Y2_) Y2_ch.fill(kLevel);
  }

  void RunOneFrame(bool dominant_nearend) {
    RandomizeSampleVector(random_generator_, x_.View(/*band=*/0, /*channel=*/0));
    render_delay_buffer_->Insert(x_);
    if (first_frame_) {
      render_delay_buffer_->Reset();
      first_frame_ = false;
    }
    render_delay_buffer_->PrepareCaptureProcessing();
    aec_state_.Update(delay_estimate_, H2_, h_, *render_delay_buffer_->GetRenderBuffer(), E2_refined_, Y2_, output_);
    estimator_.Estimate(aec_state_, *render_delay_buffer_->GetRenderBuffer(), S2_linear_, Y2_, E2_refined_,
                        dominant_nearend, R2_, R2_unbounded_);
  }

  std::span<const std::array<float, kFftLengthBy2Plus1>> R2() const { return R2_; }
  void SetDelayEstimate(const DelayEstimate& delay_estimate) { delay_estimate_ = delay_estimate; }
  const AecState& aec_state() const { return aec_state_; }

 private:
  const size_t num_capture_channels_;
  const Aec3Config config_;
  ResidualEchoEstimator estimator_;
  AecState aec_state_;
  std::unique_ptr<RenderDelayBuffer> render_delay_buffer_;
  std::vector<std::array<float, kFftLengthBy2Plus1>> E2_refined_;
  std::vector<std::array<float, kFftLengthBy2Plus1>> S2_linear_;
  std::vector<std::array<float, kFftLengthBy2Plus1>> Y2_;
  std::vector<std::array<float, kFftLengthBy2Plus1>> R2_;
  std::vector<std::array<float, kFftLengthBy2Plus1>> R2_unbounded_;
  Block x_;
  std::vector<std::vector<std::array<float, kFftLengthBy2Plus1>>> H2_;
  std::vector<std::vector<float>> h_;
  Random random_generator_;
  std::vector<SubtractorOutput> output_;
  std::optional<DelayEstimate> delay_estimate_;
  bool first_frame_ = true;
};

}  // namespace

// MultiChannel/BasicTest for render and capture channels in {1, 2, 4}: 1993
// blocks without a failure (and, here, a finite non-negative estimate).
SM_TEST(Aec3Suppression, ResidualEchoEstimatorBasicTest) {
  for (size_t num_render_channels : {1u, 2u, 4u}) {
    for (size_t num_capture_channels : {1u, 2u, 4u}) {
      Aec3Config config;
      ResidualEchoEstimatorTest test(num_render_channels, num_capture_channels, config);
      for (int k = 0; k < 1993; ++k) test.RunOneFrame(/*dominant_nearend=*/false);
      for (const auto& R2_ch : test.R2()) {
        for (float v : R2_ch) {
          SM_EXPECT_TRUE(std::isfinite(v));
          SM_EXPECT_GE(v, 0.f);
        }
      }
    }
  }
}

// The milder reverb decay (nearend_len) gives less residual echo during
// dominant nearend and the same otherwise.
SM_TEST(Aec3Suppression, ResidualEchoEstimatorReverbTest) {
  constexpr float kEpsilon = 1e-4f;
  const size_t num_render_channels = 1;
  const size_t num_capture_channels = 1;
  const size_t nFrames = 100;

  Aec3Config reference_config;
  reference_config.ep_strength.default_len = 0.95f;
  reference_config.ep_strength.nearend_len = 0.95f;
  Aec3Config config_use_nearend_len = reference_config;
  config_use_nearend_len.ep_strength.default_len = 0.95f;
  config_use_nearend_len.ep_strength.nearend_len = 0.83f;

  ResidualEchoEstimatorTest reference_test(num_render_channels, num_capture_channels, reference_config);
  ResidualEchoEstimatorTest use_nearend_len_test(num_render_channels, num_capture_channels, config_use_nearend_len);

  std::vector<float> acum_energy_reference_R2(num_capture_channels, 0.0f);
  std::vector<float> acum_energy_R2(num_capture_channels, 0.0f);
  for (size_t frame = 0; frame < nFrames; ++frame) {
    bool dominant_nearend = frame > nFrames / 2;
    reference_test.RunOneFrame(dominant_nearend);
    use_nearend_len_test.RunOneFrame(dominant_nearend);
    const auto& reference_R2 = reference_test.R2();
    const auto& R2 = use_nearend_len_test.R2();
    SM_ASSERT_EQ(reference_R2.size(), R2.size());
    for (size_t ch = 0; ch < reference_R2.size(); ++ch) {
      float energy_reference_R2 = std::accumulate(reference_R2[ch].cbegin(), reference_R2[ch].cend(), 0.0f);
      float energy_R2 = std::accumulate(R2[ch].cbegin(), R2[ch].cend(), 0.0f);
      if (dominant_nearend) {
        SM_EXPECT_GE(energy_reference_R2, energy_R2);
      } else {
        SM_EXPECT_NEAR(energy_reference_R2, energy_R2, kEpsilon);
      }
      acum_energy_reference_R2[ch] += energy_reference_R2;
      acum_energy_R2[ch] += energy_R2;
    }
    if (frame == nFrames / 2 || frame == nFrames - 1) {
      for (size_t ch = 0; ch < acum_energy_reference_R2.size(); ch++) {
        if (dominant_nearend) {
          SM_EXPECT_GT(acum_energy_reference_R2[ch], acum_energy_R2[ch]);
        } else {
          SM_EXPECT_NEAR(acum_energy_reference_R2[ch], acum_energy_R2[ch], kEpsilon);
        }
      }
    }
  }
}

// With a usable linear filter (a refined delay estimate, as in libwebrtc's
// neural-estimator metrics tests) the estimate is the linear one.
SM_TEST(Aec3Suppression, ResidualEchoEstimatorLinearMode) {
  Aec3Config config;
  ResidualEchoEstimatorTest test(1, 1, config);
  test.SetDelayEstimate(DelayEstimate(DelayEstimate::Quality::kRefined, 10));
  for (int k = 0; k < 300; ++k) test.RunOneFrame(/*dominant_nearend=*/false);
  SM_EXPECT_TRUE(test.aec_state().UsableLinearEstimate());
}

// --- nearend detectors (no libwebrtc unit tests) ---------------------------------

SM_TEST(Aec3Suppression, DominantNearendDetectorTriggerAndHold) {
  SuppressorConfig config;
  DominantNearendDetector detector(config.dominant_nearend_detection, 1);
  std::vector<std::array<float, kFftLengthBy2Plus1>> nearend(1), echo(1), noise(1);
  nearend[0].fill(1000.f);
  echo[0].fill(100.f);  // Under 0.25x the nearend.
  noise[0].fill(10.f);  // The nearend is over 30x the noise.

  // Nearend after 12 blocks.
  for (int k = 0; k < 11; ++k) {
    detector.Update(nearend, echo, noise, /*initial_state=*/false);
    SM_EXPECT_FALSE(detector.IsNearendState());
  }
  detector.Update(nearend, echo, noise, false);
  SM_EXPECT_TRUE(detector.IsNearendState());

  // Held for 50 blocks after the nearend goes away (silence: no echo).
  std::vector<std::array<float, kFftLengthBy2Plus1>> silence(1);
  silence[0].fill(0.f);
  for (int k = 0; k < 48; ++k) {
    detector.Update(silence, silence, noise, false);
    SM_EXPECT_TRUE(detector.IsNearendState());
  }
  detector.Update(silence, silence, noise, false);
  SM_EXPECT_FALSE(detector.IsNearendState());

  // Strong echo (over 10x the nearend and 30x the noise) leaves at once.
  for (int k = 0; k < 12; ++k) detector.Update(nearend, echo, noise, false);
  SM_EXPECT_TRUE(detector.IsNearendState());
  std::vector<std::array<float, kFftLengthBy2Plus1>> strong_echo(1);
  strong_echo[0].fill(20000.f);
  detector.Update(nearend, strong_echo, noise, false);
  SM_EXPECT_FALSE(detector.IsNearendState());
}

SM_TEST(Aec3Suppression, SubbandNearendDetector) {
  SuppressorConfig config;
  config.subband_nearend_detection.subband1 = {1, 4};
  config.subband_nearend_detection.subband2 = {5, 8};
  config.subband_nearend_detection.nearend_threshold = 2.f;
  config.subband_nearend_detection.snr_threshold = 3.f;
  SubbandNearendDetector detector(config, 1);
  std::vector<std::array<float, kFftLengthBy2Plus1>> nearend(1), echo(1), noise(1);
  noise[0].fill(10.f);
  echo[0].fill(0.f);
  nearend[0].fill(100.f);  // subband1 100 < 2 * 100 and 100 > 3 * 10.
  detector.Update(nearend, echo, noise, false);
  SM_EXPECT_TRUE(detector.IsNearendState());
  nearend[0].fill(20.f);  // Under the SNR threshold.
  detector.Update(nearend, echo, noise, false);
  SM_EXPECT_FALSE(detector.IsNearendState());
}
