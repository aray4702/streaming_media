// Ports of libwebrtc's AEC3 linear-filter unit tests (`apm v2`, #240 step 2),
// plus a DFT check of the 128-point Ooura FFT under Aec3Fft (#240 step 6):
// aec3_fft_unittest.cc, render_delay_buffer_unittest.cc,
// adaptive_fir_filter_unittest.cc (FilterSize, FilterStatisticsAccess,
// FilterAndAdapt), coarse_filter_update_gain_unittest.cc,
// refined_filter_update_gain_unittest.cc and subtractor_unittest.cc, with
// their signals, lengths, delays and pass bounds. The SIMD-against-C tests
// have no counterpart (only the C paths are ported), and the death tests check
// RTC_DCHECKs this library doesn't have. The multi-channel cases use the
// parameters libwebrtc's debug builds run.
//
// Where libwebrtc's tests read AecState::MinDirectPathFilterDelay() for the
// render signal analyzer, these pass delay_samples / 64, the delay of the
// filter peak (AecState is #240's next step; coarse_filter_update_gain's test
// already does this). AecState::SaturatedCapture() is false in all of them.
#include "sm/audio/aec3_filter.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "sm/audio/ooura_fft.h"
#include "sm/core/random.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::audio::aec3;

namespace {

// echo_canceller_test_tools: RandomizeSampleVector and DelayBuffer.
void RandomizeSampleVector(Random& rng, std::span<float> v, float amplitude = 32767.f) {
  for (auto& x : v) x = static_cast<float>(rng.Uniform(-amplitude, amplitude));
}

class DelayBuffer {
 public:
  explicit DelayBuffer(size_t delay) : buffer_(delay, 0.f) {}
  void Delay(std::span<const float> x, std::span<float> y) {
    for (size_t k = 0; k < x.size(); ++k) {
      if (buffer_.empty()) {
        y[k] = x[k];
        continue;
      }
      y[k] = buffer_[next_];
      buffer_[next_] = x[k];
      next_ = next_ + 1 < buffer_.size() ? next_ + 1 : 0;
    }
  }

 private:
  std::vector<float> buffer_;
  size_t next_ = 0;
};

constexpr std::array<CascadedBiQuadFilter::BiQuadCoefficients, 1> kHighPassFilterCoefficients = {{
    {{0.97261f, -1.94523f, 0.97261f}, {-1.94448f, 0.94598f}},
}};

float Energy(std::span<const float> x) { return std::inner_product(x.begin(), x.end(), x.begin(), 0.f); }

double GainPower(const FftData& G) {
  std::array<float, kFftLengthBy2Plus1> G2;
  G.Spectrum(G2);
  return std::accumulate(G2.begin(), G2.end(), 0.);
}

bool IsZero(const FftData& G) {
  return std::all_of(G.re.begin(), G.re.end(), [](float a) { return a == 0.f; }) &&
         std::all_of(G.im.begin(), G.im.end(), [](float a) { return a == 0.f; });
}

}  // namespace

// --- aec3_fft_unittest.cc ----------------------------------------------------

SM_TEST(Aec3Filter, FftOfZeroImpulseAndConstant) {
  Aec3Fft fft;
  FftData X;
  std::array<float, kFftLength> x;
  x.fill(0.f);
  fft.Fft(&x, &X);
  for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
    SM_EXPECT_EQ(0.f, X.re[k]);
    SM_EXPECT_EQ(0.f, X.im[k]);
  }
  x.fill(0.f);
  x[0] = 1.f;
  fft.Fft(&x, &X);
  for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
    SM_EXPECT_EQ(1.f, X.re[k]);
    SM_EXPECT_EQ(0.f, X.im[k]);
  }
  x.fill(1.f);
  fft.Fft(&x, &X);
  SM_EXPECT_EQ(128.f, X.re[0]);
  for (size_t k = 1; k < kFftLengthBy2Plus1; ++k) SM_EXPECT_EQ(0.f, X.re[k]);
  for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) SM_EXPECT_EQ(0.f, X.im[k]);
}

SM_TEST(Aec3Filter, IfftOfZeroImpulseAndConstant) {
  Aec3Fft fft;
  FftData X;
  std::array<float, kFftLength> x;
  X.re.fill(0.f);
  X.im.fill(0.f);
  fft.Ifft(X, &x);
  for (float a : x) SM_EXPECT_EQ(0.f, a);
  X.re.fill(1.f);
  X.im.fill(0.f);
  fft.Ifft(X, &x);
  SM_EXPECT_EQ(64.f, x[0]);
  for (size_t j = 1; j < x.size(); ++j) SM_EXPECT_EQ(0.f, x[j]);
  X.re.fill(0.f);
  X.re[0] = 128;
  X.im.fill(0.f);
  fft.Ifft(X, &x);
  for (float a : x) SM_EXPECT_EQ(64.f, a);
}

SM_TEST(Aec3Filter, FftAndIfft) {
  Aec3Fft fft;
  FftData X;
  std::array<float, kFftLength> x;
  std::array<float, kFftLength> x_ref;
  int v = 0;
  for (int k = 0; k < 20; ++k) {
    for (size_t j = 0; j < x.size(); ++j) {
      x[j] = static_cast<float>(v++);
      x_ref[j] = x[j] * 64.f;
    }
    fft.Fft(&x, &X);
    fft.Ifft(X, &x);
    for (size_t j = 0; j < x.size(); ++j) SM_EXPECT_NEAR(x_ref[j], x[j], 0.001f);
  }
}

// OouraFft (fft_size_128), which Aec3Fft runs on, against the DFT in double
// precision: a[2k] = sum x[j] cos, a[2k+1] = sum x[j] sin (2 pi j k / 128),
// a[1] the bin 64; the inverse is 64 times the input.
SM_TEST(Aec3Filter, OouraFft128MatchesDft) {
  const sm::audio::ooura::OouraFft128 fft;
  Random random(42);
  for (int trial = 0; trial < 10; ++trial) {
    std::array<float, kFftLength> x;
    for (float& v : x) v = static_cast<float>(random.Uniform(-32768.0, 32767.0));
    std::array<float, kFftLength> a = x;
    fft.Fft(a.data());
    for (size_t k = 0; k <= kFftLengthBy2; ++k) {
      double re = 0, im = 0;
      for (size_t j = 0; j < kFftLength; ++j) {
        const double phi = 2 * 3.14159265358979323846 * static_cast<double>(j * k) / kFftLength;
        re += x[j] * std::cos(phi);
        im += x[j] * std::sin(phi);
      }
      const float got_re = k == kFftLengthBy2 ? a[1] : a[2 * k];
      SM_EXPECT_NEAR(re, got_re, 1.f);
      if (k != 0 && k != kFftLengthBy2) SM_EXPECT_NEAR(im, a[2 * k + 1], 1.f);
    }
    fft.InverseFft(a.data());
    for (size_t j = 0; j < kFftLength; ++j) SM_EXPECT_NEAR(64.f * x[j], a[j], 64.f * 32768.f * 1e-6f);
  }
}

SM_TEST(Aec3Filter, ZeroPaddedFft) {
  Aec3Fft fft;
  FftData X;
  std::array<float, kFftLengthBy2> x_in;
  std::array<float, kFftLength> x_ref;
  std::array<float, kFftLength> x_out;
  int v = 0;
  x_ref.fill(0.f);
  for (int k = 0; k < 20; ++k) {
    for (size_t j = 0; j < x_in.size(); ++j) {
      x_in[j] = static_cast<float>(v++);
      x_ref[j + kFftLengthBy2] = x_in[j] * 64.f;
    }
    fft.ZeroPaddedFft(x_in, Aec3Fft::Window::kRectangular, &X);
    fft.Ifft(X, &x_out);
    for (size_t j = 0; j < x_out.size(); ++j) SM_EXPECT_NEAR(x_ref[j], x_out[j], 0.1f);
  }
}

SM_TEST(Aec3Filter, PaddedFft) {
  Aec3Fft fft;
  FftData X;
  std::array<float, kFftLengthBy2> x_in;
  std::array<float, kFftLength> x_out;
  std::array<float, kFftLengthBy2> x_old;
  std::array<float, kFftLengthBy2> x_old_ref;
  std::array<float, kFftLength> x_ref;
  int v = 0;
  x_old.fill(0.f);
  for (int k = 0; k < 20; ++k) {
    for (size_t j = 0; j < x_in.size(); ++j) x_in[j] = static_cast<float>(v++);
    std::copy(x_old.begin(), x_old.end(), x_ref.begin());
    std::copy(x_in.begin(), x_in.end(), x_ref.begin() + kFftLengthBy2);
    std::copy(x_in.begin(), x_in.end(), x_old_ref.begin());
    for (float& a : x_ref) a *= 64.f;
    fft.PaddedFft(x_in, x_old, &X);
    std::copy(x_in.begin(), x_in.end(), x_old.begin());
    fft.Ifft(X, &x_out);
    for (size_t j = 0; j < x_out.size(); ++j) SM_EXPECT_NEAR(x_ref[j], x_out[j], 0.1f);
    SM_EXPECT_TRUE(x_old_ref == x_old);
  }
}

// --- render_delay_buffer_unittest.cc ------------------------------------------

SM_TEST(Aec3Filter, RenderDelayBufferOverflow) {
  const Aec3Config config;
  for (size_t num_channels : {1, 2, 8}) {
    for (int rate : {16000, 32000, 48000}) {
      RenderDelayBuffer delay_buffer(config, rate, num_channels);
      Block block_to_insert(static_cast<int>(NumBandsForRate(rate)), static_cast<int>(num_channels));
      for (size_t k = 0; k < 10; ++k) {
        SM_EXPECT_TRUE(RenderDelayBuffer::BufferingEvent::kNone == delay_buffer.Insert(block_to_insert));
      }
      bool overrun_occurred = false;
      for (size_t k = 0; k < 1000; ++k) {
        overrun_occurred = overrun_occurred ||
                           RenderDelayBuffer::BufferingEvent::kRenderOverrun == delay_buffer.Insert(block_to_insert);
      }
      SM_EXPECT_TRUE(overrun_occurred);
    }
  }
}

SM_TEST(Aec3Filter, RenderDelayBufferAvailableBlock) {
  constexpr int kSampleRateHz = 48000;
  RenderDelayBuffer delay_buffer(Aec3Config(), kSampleRateHz, 1);
  Block input_block(static_cast<int>(NumBandsForRate(kSampleRateHz)), 1, 1.0f);
  SM_EXPECT_TRUE(RenderDelayBuffer::BufferingEvent::kNone == delay_buffer.Insert(input_block));
  delay_buffer.PrepareCaptureProcessing();
}

SM_TEST(Aec3Filter, RenderDelayBufferAlignFromDelay) {
  RenderDelayBuffer delay_buffer(Aec3Config(), 16000, 1);
  SM_ASSERT_TRUE(delay_buffer.Delay() != 0);
  delay_buffer.Reset();
  for (size_t delay = 0; delay < 20; ++delay) {
    SM_ASSERT_TRUE(delay_buffer.AlignFromDelay(delay));
    SM_EXPECT_EQ(delay, delay_buffer.Delay());
  }
}

// The buffer sizes of render_delay_buffer.cc with M157's defaults: 64 / 4 * (24
// * 5 + 32 + 1) = 2448 decimated samples, 153 + 13 + 1 = 167 blocks.
SM_TEST(Aec3Filter, RenderDelayBufferSizes) {
  SM_EXPECT_EQ(size_t{2448}, GetDownSampledBufferSize(4, 5));
  SM_EXPECT_EQ(size_t{167}, GetRenderDelayBufferSize(4, 5, 13));
  RenderDelayBuffer delay_buffer(Aec3Config(), 16000, 1);
  SM_EXPECT_EQ(size_t{167 - 1 - 13}, delay_buffer.MaxDelay());
  // set_stream_delay_ms: 40 ms is 10 blocks of 4 ms; Reset applies 10 less a
  // headroom of 2 as the total delay, and Delay() is that less the one block
  // the low-rate buffer starts ahead.
  delay_buffer.SetAudioBufferDelay(40);
  SM_EXPECT_TRUE(delay_buffer.HasReceivedBufferDelay());
  delay_buffer.Reset();
  SM_EXPECT_EQ(1, delay_buffer.BufferLatency());
  SM_EXPECT_EQ(size_t{7}, delay_buffer.Delay());
}

// --- adaptive_fir_filter_unittest.cc ------------------------------------------

SM_TEST(Aec3Filter, AdaptiveFirFilterStatisticsAccess) {
  AdaptiveFirFilter filter(9, 9, 250, 1);
  std::vector<std::array<float, kFftLengthBy2Plus1>> H2(filter.max_filter_size_partitions(),
                                                        std::array<float, kFftLengthBy2Plus1>());
  for (auto& H2_k : H2) H2_k.fill(0.f);
  std::array<float, kFftLengthBy2Plus1> erl;
  ComputeErl(H2, erl);
  filter.ComputeFrequencyResponse(&H2);
  SM_EXPECT_EQ(size_t{9}, H2.size());
}

SM_TEST(Aec3Filter, AdaptiveFirFilterSize) {
  for (size_t filter_size = 1; filter_size < 5; ++filter_size) {
    AdaptiveFirFilter filter(filter_size, filter_size, 250, 1);
    SM_EXPECT_EQ(filter_size, filter.SizePartitions());
  }
}

// The size moves linearly from the old to the new target over the change
// duration (UpdateSize), and the partitions it grows into start at zero.
SM_TEST(Aec3Filter, AdaptiveFirFilterSizeChange) {
  AdaptiveFirFilter filter(13, 12, 250, 1);
  SM_EXPECT_EQ(size_t{12}, filter.SizePartitions());
  filter.SetSizePartitions(13, false);
  BlockBuffer blocks(20, 1, 1);
  SpectrumBuffer spectra(20, 1);
  FftBuffer ffts(20, 1);
  RenderBuffer render_buffer(&blocks, &spectra, &ffts);
  FftData G;
  G.Clear();
  int blocks_to_13 = 0;
  for (int k = 1; k <= 250; ++k) {
    filter.Adapt(render_buffer, G);
    if (filter.SizePartitions() == 13 && blocks_to_13 == 0) blocks_to_13 = k;
  }
  // average(12, 13, c / 250) truncates to 13 only at c = 0: the last block.
  SM_EXPECT_EQ(250, blocks_to_13);
}

SM_TEST(Aec3Filter, AdaptiveFirFilterFilterAndAdapt) {
  for (size_t num_render_channels : {1, 4}) {
    for (size_t num_capture_channels : {1, 8}) {
      constexpr int kSampleRateHz = 48000;
      constexpr size_t kNumBands = NumBandsForRate(kSampleRateHz);
      constexpr size_t kNumBlocksToProcessPerRenderChannel = 1000;
      Aec3Config config;
      AdaptiveFirFilter filter(config.filter.refined.length_blocks, config.filter.refined.length_blocks,
                               config.filter.config_change_duration_blocks, num_render_channels);
      std::vector<std::vector<std::array<float, kFftLengthBy2Plus1>>> H2(
          num_capture_channels, std::vector<std::array<float, kFftLengthBy2Plus1>>(
                                    filter.max_filter_size_partitions(), std::array<float, kFftLengthBy2Plus1>()));
      std::vector<std::vector<float>> h(
          num_capture_channels,
          std::vector<float>(static_cast<size_t>(GetTimeDomainLength(static_cast<int>(filter.max_filter_size_partitions()))),
                             0.f));
      Aec3Fft fft;
      config.delay.default_delay = 1;
      RenderDelayBuffer render_delay_buffer(config, kSampleRateHz, num_render_channels);
      CoarseFilterUpdateGain gain(config.filter.coarse, config.filter.config_change_duration_blocks);
      Random random_generator(42U);
      Block x(kNumBands, static_cast<int>(num_render_channels));
      std::vector<float> n(kBlockSize, 0.f);
      std::vector<float> y(kBlockSize, 0.f);
      RenderSignalAnalyzer render_signal_analyzer(config);
      std::vector<float> e(kBlockSize, 0.f);
      std::array<float, kFftLength> s_scratch;
      FftData S;
      FftData G;
      FftData E;
      constexpr float kScale = 1.0f / kFftLengthBy2;

      for (size_t delay_samples : {0, 64, 150, 200, 301}) {
        std::vector<DelayBuffer> delay_buffer(num_render_channels, DelayBuffer(delay_samples));
        std::vector<CascadedBiQuadFilter> x_hp_filter(num_render_channels,
                                                      CascadedBiQuadFilter(kHighPassFilterCoefficients));
        CascadedBiQuadFilter y_hp_filter(kHighPassFilterCoefficients);
        const size_t num_blocks_to_process = kNumBlocksToProcessPerRenderChannel * num_render_channels;
        for (size_t j = 0; j < num_blocks_to_process; ++j) {
          std::fill(y.begin(), y.end(), 0.f);
          for (size_t ch = 0; ch < num_render_channels; ++ch) {
            RandomizeSampleVector(random_generator, x.View(/*band=*/0, static_cast<int>(ch)));
            std::array<float, kBlockSize> y_channel;
            delay_buffer[ch].Delay(x.View(/*band=*/0, static_cast<int>(ch)), y_channel);
            for (size_t k = 0; k < y.size(); ++k) y[k] += y_channel[k] / static_cast<float>(num_render_channels);
          }
          RandomizeSampleVector(random_generator, n);
          const float noise_scaling = 1.f / 100.f / static_cast<float>(num_render_channels);
          for (size_t k = 0; k < y.size(); ++k) y[k] += n[k] * noise_scaling;
          for (size_t ch = 0; ch < num_render_channels; ++ch) {
            x_hp_filter[ch].Process(x.View(/*band=*/0, static_cast<int>(ch)));
          }
          y_hp_filter.Process(y);

          render_delay_buffer.Insert(x);
          if (j == 0) render_delay_buffer.Reset();
          render_delay_buffer.PrepareCaptureProcessing();
          auto* const render_buffer = render_delay_buffer.GetRenderBuffer();
          render_signal_analyzer.Update(*render_buffer, delay_samples / kBlockSize);

          filter.Filter(*render_buffer, &S);
          fft.Ifft(S, &s_scratch);
          std::transform(y.begin(), y.end(), s_scratch.begin() + kFftLengthBy2, e.begin(),
                         [&](float a, float b) { return a - b * kScale; });
          for (float& a : e) a = std::clamp(a, -32768.f, 32767.f);
          fft.ZeroPaddedFft(e, Aec3Fft::Window::kRectangular, &E);

          std::array<float, kFftLengthBy2Plus1> render_power;
          render_buffer->SpectralSum(filter.SizePartitions(), &render_power);
          gain.Compute(render_power, render_signal_analyzer, E, filter.SizePartitions(), false, &G);
          filter.Adapt(*render_buffer, G, &h[0]);
          filter.ComputeFrequencyResponse(&H2[0]);
        }
        // Verify that the filter is able to perform well.
        SM_EXPECT_LT(1000 * Energy(e), Energy(y));
      }
    }
  }
}

// --- coarse_filter_update_gain_unittest.cc ------------------------------------

namespace {

// Runs the coarse filter and its gain on white noise, echo delayed by
// delay_samples.
void RunCoarseFilterUpdateTest(int num_blocks_to_process, size_t delay_samples, size_t num_render_channels,
                               int filter_length_blocks, const std::vector<int>& blocks_with_saturation,
                               std::array<float, kBlockSize>* e_last_block, std::array<float, kBlockSize>* y_last_block,
                               FftData* G_last_block) {
  Aec3Config config;
  config.filter.refined.length_blocks = static_cast<size_t>(filter_length_blocks);
  AdaptiveFirFilter coarse_filter(config.filter.coarse.length_blocks, config.filter.coarse.length_blocks,
                                  config.filter.config_change_duration_blocks, num_render_channels);
  Aec3Fft fft;
  constexpr int kSampleRateHz = 48000;
  config.delay.default_delay = 1;
  RenderDelayBuffer render_delay_buffer(config, kSampleRateHz, num_render_channels);
  CoarseFilterUpdateGain coarse_gain(config.filter.coarse, config.filter.config_change_duration_blocks);
  Random random_generator(42U);
  Block x(static_cast<int>(NumBandsForRate(kSampleRateHz)), static_cast<int>(num_render_channels));
  std::array<float, kBlockSize> y;
  RenderSignalAnalyzer render_signal_analyzer(config);
  std::array<float, kFftLength> s;
  FftData S;
  FftData G;
  FftData E_coarse;
  std::array<float, kBlockSize> e_coarse;
  constexpr float kScale = 1.0f / kFftLengthBy2;
  DelayBuffer delay_buffer(delay_samples);
  for (int k = 0; k < num_blocks_to_process; ++k) {
    // Handle saturation.
    const bool saturation =
        std::find(blocks_with_saturation.begin(), blocks_with_saturation.end(), k) != blocks_with_saturation.end();
    // Create the render signal.
    for (int band = 0; band < x.NumBands(); ++band) {
      for (int channel = 0; channel < x.NumChannels(); ++channel) {
        RandomizeSampleVector(random_generator, x.View(band, channel));
      }
    }
    delay_buffer.Delay(x.View(/*band=*/0, /*channel*/ 0), y);
    render_delay_buffer.Insert(x);
    if (k == 0) render_delay_buffer.Reset();
    render_delay_buffer.PrepareCaptureProcessing();
    render_signal_analyzer.Update(*render_delay_buffer.GetRenderBuffer(), delay_samples / kBlockSize);

    coarse_filter.Filter(*render_delay_buffer.GetRenderBuffer(), &S);
    fft.Ifft(S, &s);
    std::transform(y.begin(), y.end(), s.begin() + kFftLengthBy2, e_coarse.begin(),
                   [&](float a, float b) { return a - b * kScale; });
    for (float& a : e_coarse) a = std::clamp(a, -32768.f, 32767.f);
    fft.ZeroPaddedFft(e_coarse, Aec3Fft::Window::kRectangular, &E_coarse);

    std::array<float, kFftLengthBy2Plus1> render_power;
    render_delay_buffer.GetRenderBuffer()->SpectralSum(coarse_filter.SizePartitions(), &render_power);
    coarse_gain.Compute(render_power, render_signal_analyzer, E_coarse, coarse_filter.SizePartitions(), saturation, &G);
    coarse_filter.Adapt(*render_delay_buffer.GetRenderBuffer(), G);
  }
  *e_last_block = e_coarse;
  *y_last_block = y;
  G_last_block->re = G.re;
  G_last_block->im = G.im;
}

}  // namespace

// The gain makes the coarse filter converge.
SM_TEST(Aec3Filter, CoarseGainCausesFilterToConverge) {
  for (size_t num_render_channels : {1, 2, 8}) {
    for (int filter_length_blocks : {12, 20, 30}) {
      for (size_t delay_samples : {0, 64, 150, 200, 301}) {
        std::array<float, kBlockSize> e;
        std::array<float, kBlockSize> y;
        FftData G;
        RunCoarseFilterUpdateTest(5000, delay_samples, num_render_channels, filter_length_blocks, {}, &e, &y, &G);
        // Verify that the coarse filter is able to perform well. Use different
        // criteria to take overmodelling into account.
        if (filter_length_blocks == 12) {
          SM_EXPECT_LT(1000 * Energy(e), Energy(y));
        } else {
          SM_EXPECT_LT(Energy(e), Energy(y));
        }
      }
    }
  }
}

// No update while the capture saturates.
SM_TEST(Aec3Filter, CoarseGainSaturationBehavior) {
  std::vector<int> blocks_with_saturation;
  for (int k = 99; k < 200; ++k) blocks_with_saturation.push_back(k);
  for (size_t num_render_channels : {1, 2, 8}) {
    for (int filter_length_blocks : {12, 20, 30}) {
      std::array<float, kBlockSize> e;
      std::array<float, kBlockSize> y;
      FftData G_a;
      RunCoarseFilterUpdateTest(100, 65, num_render_channels, filter_length_blocks, blocks_with_saturation, &e, &y,
                                &G_a);
      SM_EXPECT_TRUE(IsZero(G_a));
    }
  }
}

// The gain decreases as the filter converges.
SM_TEST(Aec3Filter, CoarseGainDecreasing) {
  for (size_t num_render_channels : {1, 2, 4}) {
    for (int filter_length_blocks : {12, 20, 30}) {
      std::array<float, kBlockSize> e;
      std::array<float, kBlockSize> y;
      FftData G_a, G_b, G_c;
      RunCoarseFilterUpdateTest(100, 65, num_render_channels, filter_length_blocks, {}, &e, &y, &G_a);
      RunCoarseFilterUpdateTest(200, 65, num_render_channels, filter_length_blocks, {}, &e, &y, &G_b);
      RunCoarseFilterUpdateTest(300, 65, num_render_channels, filter_length_blocks, {}, &e, &y, &G_c);
      SM_EXPECT_GT(GainPower(G_a), GainPower(G_b));
      SM_EXPECT_GT(GainPower(G_b), GainPower(G_c));
    }
  }
}

// --- refined_filter_update_gain_unittest.cc -----------------------------------

namespace {

// Runs the refined and coarse filters with their gains on white noise, echo
// delayed by delay_samples.
void RunRefinedFilterUpdateTest(int num_blocks_to_process, size_t delay_samples, int filter_length_blocks,
                                const std::vector<int>& blocks_with_echo_path_changes,
                                const std::vector<int>& blocks_with_saturation, bool use_silent_render_in_second_half,
                                std::array<float, kBlockSize>* e_last_block,
                                std::array<float, kBlockSize>* y_last_block, FftData* G_last_block) {
  constexpr size_t kNumRenderChannels = 1;
  constexpr int kSampleRateHz = 48000;
  constexpr size_t kNumBands = NumBandsForRate(kSampleRateHz);
  Aec3Config config;
  config.filter.refined.length_blocks = static_cast<size_t>(filter_length_blocks);
  config.filter.coarse.length_blocks = static_cast<size_t>(filter_length_blocks);
  AdaptiveFirFilter refined_filter(config.filter.refined.length_blocks, config.filter.refined.length_blocks,
                                   config.filter.config_change_duration_blocks, kNumRenderChannels);
  AdaptiveFirFilter coarse_filter(config.filter.coarse.length_blocks, config.filter.coarse.length_blocks,
                                  config.filter.config_change_duration_blocks, kNumRenderChannels);
  std::vector<std::array<float, kFftLengthBy2Plus1>> H2(refined_filter.max_filter_size_partitions(),
                                                        std::array<float, kFftLengthBy2Plus1>());
  for (auto& H2_k : H2) H2_k.fill(0.f);
  std::vector<float> h(static_cast<size_t>(GetTimeDomainLength(static_cast<int>(refined_filter.max_filter_size_partitions()))),
                       0.f);
  Aec3Fft fft;
  CoarseFilterUpdateGain coarse_gain(config.filter.coarse, config.filter.config_change_duration_blocks);
  RefinedFilterUpdateGain refined_gain(config.filter.refined, config.filter.config_change_duration_blocks);
  Random random_generator(42U);
  Block x(kNumBands, kNumRenderChannels);
  std::vector<float> y(kBlockSize, 0.f);
  config.delay.default_delay = 1;
  RenderDelayBuffer render_delay_buffer(config, kSampleRateHz, kNumRenderChannels);
  RenderSignalAnalyzer render_signal_analyzer(config);
  std::array<float, kFftLength> s_scratch;
  FftData S;
  FftData G;
  SubtractorOutput output;
  output.Reset();
  FftData& E_refined = output.E_refined;
  FftData E_coarse;
  std::array<float, kBlockSize>& e_refined = output.e_refined;
  std::array<float, kBlockSize>& e_coarse = output.e_coarse;
  constexpr float kScale = 1.0f / kFftLengthBy2;

  DelayBuffer delay_buffer(delay_samples);
  for (int k = 0; k < num_blocks_to_process; ++k) {
    // Handle echo path changes.
    if (std::find(blocks_with_echo_path_changes.begin(), blocks_with_echo_path_changes.end(), k) !=
        blocks_with_echo_path_changes.end()) {
      refined_filter.HandleEchoPathChange();
    }
    // Handle saturation.
    const bool saturation =
        std::find(blocks_with_saturation.begin(), blocks_with_saturation.end(), k) != blocks_with_saturation.end();
    // Create the render signal. libwebrtc's test loops the bands up to the
    // channel count, so only band 0 gets noise.
    if (use_silent_render_in_second_half && k > num_blocks_to_process / 2) {
      for (int band = 0; band < x.NumBands(); ++band) {
        for (int channel = 0; channel < x.NumChannels(); ++channel) {
          auto v = x.View(band, channel);
          std::fill(v.begin(), v.end(), 0.f);
        }
      }
    } else {
      for (int band = 0; band < x.NumChannels(); ++band) {
        for (int channel = 0; channel < x.NumChannels(); ++channel) {
          RandomizeSampleVector(random_generator, x.View(band, channel));
        }
      }
    }
    delay_buffer.Delay(x.View(/*band=*/0, /*channel=*/0), y);
    render_delay_buffer.Insert(x);
    if (k == 0) render_delay_buffer.Reset();
    render_delay_buffer.PrepareCaptureProcessing();
    render_signal_analyzer.Update(*render_delay_buffer.GetRenderBuffer(), delay_samples / kBlockSize);

    // Apply the refined filter.
    refined_filter.Filter(*render_delay_buffer.GetRenderBuffer(), &S);
    fft.Ifft(S, &s_scratch);
    std::transform(y.begin(), y.end(), s_scratch.begin() + kFftLengthBy2, e_refined.begin(),
                   [&](float a, float b) { return a - b * kScale; });
    for (float& a : e_refined) a = std::clamp(a, -32768.f, 32767.f);
    fft.ZeroPaddedFft(e_refined, Aec3Fft::Window::kRectangular, &E_refined);

    // Apply the coarse filter.
    coarse_filter.Filter(*render_delay_buffer.GetRenderBuffer(), &S);
    fft.Ifft(S, &s_scratch);
    std::transform(y.begin(), y.end(), s_scratch.begin() + kFftLengthBy2, e_coarse.begin(),
                   [&](float a, float b) { return a - b * kScale; });
    for (float& a : e_coarse) a = std::clamp(a, -32768.f, 32767.f);
    fft.ZeroPaddedFft(e_coarse, Aec3Fft::Window::kRectangular, &E_coarse);

    // Compute spectra for future use.
    E_refined.Spectrum(output.E2_refined);
    E_coarse.Spectrum(output.E2_coarse);

    // Adapt the coarse filter.
    std::array<float, kFftLengthBy2Plus1> render_power;
    render_delay_buffer.GetRenderBuffer()->SpectralSum(coarse_filter.SizePartitions(), &render_power);
    coarse_gain.Compute(render_power, render_signal_analyzer, E_coarse, coarse_filter.SizePartitions(), saturation, &G);
    coarse_filter.Adapt(*render_delay_buffer.GetRenderBuffer(), G);

    // Adapt the refined filter.
    render_delay_buffer.GetRenderBuffer()->SpectralSum(refined_filter.SizePartitions(), &render_power);
    std::array<float, kFftLengthBy2Plus1> erl;
    ComputeErl(H2, erl);
    refined_gain.Compute(render_power, render_signal_analyzer, output, erl, refined_filter.SizePartitions(),
                         saturation, false, &G);
    refined_filter.Adapt(*render_delay_buffer.GetRenderBuffer(), G, &h);
    refined_filter.ComputeFrequencyResponse(&H2);
  }
  *e_last_block = e_refined;
  std::copy(y.begin(), y.end(), y_last_block->begin());
  G_last_block->re = G.re;
  G_last_block->im = G.im;
}

}  // namespace

// The gain makes the refined filter converge.
SM_TEST(Aec3Filter, RefinedGainCausesFilterToConverge) {
  for (int filter_length_blocks : {12, 20, 30}) {
    for (size_t delay_samples : {0, 64, 150, 200, 301}) {
      std::array<float, kBlockSize> e;
      std::array<float, kBlockSize> y;
      FftData G;
      RunRefinedFilterUpdateTest(600, delay_samples, filter_length_blocks, {}, {}, false, &e, &y, &G);
      // Verify that the refined filter is able to perform well. Use different
      // criteria to take overmodelling into account.
      if (filter_length_blocks == 12) {
        SM_EXPECT_LT(1000 * Energy(e), Energy(y));
      } else {
        SM_EXPECT_LT(Energy(e), Energy(y));
      }
    }
  }
}

// The gain decreases as the filter converges.
SM_TEST(Aec3Filter, RefinedGainDecreasing) {
  std::array<float, kBlockSize> e;
  std::array<float, kBlockSize> y;
  FftData G_a, G_b, G_c;
  RunRefinedFilterUpdateTest(250, 65, 12, {}, {}, false, &e, &y, &G_a);
  RunRefinedFilterUpdateTest(500, 65, 12, {}, {}, false, &e, &y, &G_b);
  RunRefinedFilterUpdateTest(750, 65, 12, {}, {}, false, &e, &y, &G_c);
  SM_EXPECT_GT(GainPower(G_a), GainPower(G_b));
  SM_EXPECT_GT(GainPower(G_b), GainPower(G_c));
}

// No update during saturation, and the update after it is larger than just
// before it.
SM_TEST(Aec3Filter, RefinedGainSaturationBehavior) {
  std::vector<int> blocks_with_saturation;
  for (int k = 99; k < 200; ++k) blocks_with_saturation.push_back(k);
  for (int filter_length_blocks : {12, 20, 30}) {
    std::array<float, kBlockSize> e;
    std::array<float, kBlockSize> y;
    FftData G_a, G_b;
    RunRefinedFilterUpdateTest(100, 65, filter_length_blocks, {}, blocks_with_saturation, false, &e, &y, &G_a);
    SM_EXPECT_TRUE(IsZero(G_a));
    RunRefinedFilterUpdateTest(99, 65, filter_length_blocks, {}, blocks_with_saturation, false, &e, &y, &G_a);
    RunRefinedFilterUpdateTest(201, 65, filter_length_blocks, {}, blocks_with_saturation, false, &e, &y, &G_b);
    SM_EXPECT_LT(GainPower(G_a), GainPower(G_b));
  }
}

// --- subtractor_unittest.cc ---------------------------------------------------

namespace {

// Runs the subtractor on white noise and returns the echo-to-capture power
// ratio of the refined filter's output on the last block, per capture channel.
std::vector<float> RunSubtractorTest(size_t num_render_channels, size_t num_capture_channels, int num_blocks_to_process,
                                     int delay_samples, int refined_filter_length_blocks,
                                     int coarse_filter_length_blocks, bool uncorrelated_inputs,
                                     const std::vector<int>& blocks_with_echo_path_changes) {
  constexpr int kSampleRateHz = 48000;
  constexpr size_t kNumBands = NumBandsForRate(kSampleRateHz);
  Aec3Config config;
  config.filter.refined.length_blocks = static_cast<size_t>(refined_filter_length_blocks);
  config.filter.coarse.length_blocks = static_cast<size_t>(coarse_filter_length_blocks);

  Subtractor subtractor(config, num_render_channels, num_capture_channels);
  Block x(kNumBands, static_cast<int>(num_render_channels));
  Block y(/*num_bands=*/1, static_cast<int>(num_capture_channels));
  std::vector<SubtractorOutput> output(num_capture_channels);
  config.delay.default_delay = 1;
  RenderDelayBuffer render_delay_buffer(config, kSampleRateHz, num_render_channels);
  RenderSignalAnalyzer render_signal_analyzer(config);
  Random random_generator(42U);

  std::vector<std::vector<DelayBuffer>> delay_buffer(
      num_capture_channels, std::vector<DelayBuffer>(num_render_channels, DelayBuffer(static_cast<size_t>(delay_samples))));
  std::vector<CascadedBiQuadFilter> x_hp_filter(num_render_channels, CascadedBiQuadFilter(kHighPassFilterCoefficients));
  std::vector<CascadedBiQuadFilter> y_hp_filter(num_capture_channels, CascadedBiQuadFilter(kHighPassFilterCoefficients));

  for (int block_num = 0; block_num < num_blocks_to_process; ++block_num) {
    for (size_t render_ch = 0; render_ch < num_render_channels; ++render_ch) {
      RandomizeSampleVector(random_generator, x.View(/*band=*/0, static_cast<int>(render_ch)));
    }
    if (uncorrelated_inputs) {
      for (size_t capture_ch = 0; capture_ch < num_capture_channels; ++capture_ch) {
        RandomizeSampleVector(random_generator, y.View(/*band=*/0, static_cast<int>(capture_ch)));
      }
    } else {
      for (size_t capture_ch = 0; capture_ch < num_capture_channels; ++capture_ch) {
        auto y_view = y.View(/*band=*/0, static_cast<int>(capture_ch));
        for (size_t render_ch = 0; render_ch < num_render_channels; ++render_ch) {
          std::array<float, kBlockSize> y_channel;
          delay_buffer[capture_ch][render_ch].Delay(x.View(/*band=*/0, static_cast<int>(render_ch)), y_channel);
          for (size_t k = 0; k < kBlockSize; ++k) y_view[k] += y_channel[k] / static_cast<float>(num_render_channels);
        }
      }
    }
    for (size_t ch = 0; ch < num_render_channels; ++ch) x_hp_filter[ch].Process(x.View(/*band=*/0, static_cast<int>(ch)));
    for (size_t ch = 0; ch < num_capture_channels; ++ch) y_hp_filter[ch].Process(y.View(/*band=*/0, static_cast<int>(ch)));

    render_delay_buffer.Insert(x);
    if (block_num == 0) render_delay_buffer.Reset();
    render_delay_buffer.PrepareCaptureProcessing();
    render_signal_analyzer.Update(*render_delay_buffer.GetRenderBuffer(),
                                  static_cast<size_t>(delay_samples) / kBlockSize);

    // Handle echo path changes.
    if (std::find(blocks_with_echo_path_changes.begin(), blocks_with_echo_path_changes.end(), block_num) !=
        blocks_with_echo_path_changes.end()) {
      subtractor.HandleEchoPathChange(
          EchoPathVariability(true, EchoPathVariability::DelayAdjustment::kNewDetectedDelay, false));
    }
    subtractor.Process(*render_delay_buffer.GetRenderBuffer(), y, render_signal_analyzer, false, output);
  }

  std::vector<float> results(num_capture_channels);
  for (size_t ch = 0; ch < num_capture_channels; ++ch) {
    const float output_power = Energy(output[ch].e_refined);
    const float y_power = Energy(y.View(/*band=*/0, static_cast<int>(ch)));
    results[ch] = y_power == 0.f ? -1.f : output_power / y_power;
  }
  return results;
}

}  // namespace

// The subtractor converges on correlated signals.
SM_TEST(Aec3Filter, SubtractorConvergence) {
  for (int filter_length_blocks : {12, 20, 30}) {
    for (int delay_samples : {0, 64, 150, 200, 301}) {
      for (float echo_to_nearend_power :
           RunSubtractorTest(1, 1, 2500, delay_samples, filter_length_blocks, filter_length_blocks, false, {})) {
        SM_EXPECT_GT(0.1f, echo_to_nearend_power);
      }
    }
  }
}

SM_TEST(Aec3Filter, SubtractorRefinedFilterLongerThanCoarseFilter) {
  for (float echo_to_nearend_power : RunSubtractorTest(1, 1, 400, 64, 20, 15, false, {})) {
    SM_EXPECT_GT(0.5f, echo_to_nearend_power);
  }
}

SM_TEST(Aec3Filter, SubtractorCoarseFilterLongerThanRefinedFilter) {
  for (float echo_to_nearend_power : RunSubtractorTest(1, 1, 400, 64, 15, 20, false, {})) {
    SM_EXPECT_GT(0.5f, echo_to_nearend_power);
  }
}

// The subtractor does not converge on uncorrelated signals.
SM_TEST(Aec3Filter, SubtractorNonConvergenceOnUncorrelatedSignals) {
  for (int filter_length_blocks : {12, 20, 30}) {
    for (int delay_samples : {0, 64, 150, 200, 301}) {
      for (float echo_to_nearend_power :
           RunSubtractorTest(1, 1, 3000, delay_samples, filter_length_blocks, filter_length_blocks, true, {})) {
        SM_EXPECT_NEAR(1.f, echo_to_nearend_power, 0.1);
      }
    }
  }
}

SM_TEST(Aec3Filter, SubtractorMultiChannelConvergence) {
  for (size_t num_render_channels : {1, 2}) {
    for (size_t num_capture_channels : {1, 2}) {
      for (float echo_to_nearend_power : RunSubtractorTest(num_render_channels, num_capture_channels,
                                                           static_cast<int>(2500 * num_render_channels), 64, 20, 20,
                                                           false, {})) {
        SM_EXPECT_GT(0.1f, echo_to_nearend_power);
      }
    }
  }
}

SM_TEST(Aec3Filter, SubtractorMultiChannelNonConvergenceOnUncorrelatedSignals) {
  for (size_t num_render_channels : {1, 2}) {
    for (size_t num_capture_channels : {1, 2}) {
      for (float echo_to_nearend_power : RunSubtractorTest(num_render_channels, num_capture_channels,
                                                           static_cast<int>(5000 * num_render_channels), 64, 20, 20,
                                                           true, {})) {
        SM_EXPECT_LT(.8f, echo_to_nearend_power);
        SM_EXPECT_NEAR(1.f, echo_to_nearend_power, 0.25f);
      }
    }
  }
}

// An echo-path change resets the filters to their initial size and the
// subtractor reconverges.
SM_TEST(Aec3Filter, SubtractorReconvergesAfterEchoPathChange) {
  for (float echo_to_nearend_power : RunSubtractorTest(1, 1, 2500, 150, 13, 13, false, {1000})) {
    SM_EXPECT_GT(0.1f, echo_to_nearend_power);
  }
}
