// Ports of libwebrtc's AEC3 unit tests for the whole echo canceller (`apm v2`,
// #240 step 5): frame_blocker_unittest.cc, block_framer_unittest.cc,
// block_delay_buffer_unittest.cc, echo_remover_metrics_unittest.cc,
// echo_remover_unittest.cc and block_processor_unittest.cc, with their
// signals, lengths and pass bounds. The death tests have no counterpart
// (there are no DCHECK deaths here), nor do the tests that need an external
// delay estimator, a mocked BlockProcessor, field trials, stereo content
// detection or the neural residual echo estimator, none of which are ported.
// The EchoCanceller3 tests at the end are derived from
// echo_canceller3_unittest.cc's buffering and messaging tests, run on the
// real block processor: before any render arrives it passes the capture
// through, which shows the 64-sample delay of the blocking; the render queue
// drops frames beyond 100; a capture sample at 32700 marks the capture
// saturated.
#include "sm/audio/aec3.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <numeric>

#include "sm/core/random.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::audio::aec3;

namespace {

void RandomizeSampleVector(Random& rng, std::span<float> v, float amplitude = 32767.f) {
  for (auto& x : v) x = static_cast<float>(rng.Uniform(-amplitude, amplitude));
}

// test/delay_buffer: delays a signal by a fixed number of samples.
class DelayBuffer {
 public:
  explicit DelayBuffer(size_t delay) : buffer_(delay, 0.f) {}
  void Delay(std::span<const float> x, std::span<float> x_delayed) {
    for (size_t k = 0; k < x.size(); ++k) {
      if (buffer_.empty()) {
        x_delayed[k] = x[k];
        continue;
      }
      x_delayed[k] = buffer_[next_insert_index_];
      buffer_[next_insert_index_] = x[k];
      next_insert_index_ = (next_insert_index_ + 1) % buffer_.size();
    }
  }

 private:
  std::vector<float> buffer_;
  size_t next_insert_index_ = 0;
};

using SubFrame = std::vector<std::vector<std::vector<float>>>;

SubFrame MakeSubFrame(size_t num_bands, size_t num_channels) {
  return SubFrame(num_bands, std::vector<std::vector<float>>(num_channels, std::vector<float>(kSubFrameLength, 0.f)));
}

void SetupSubFrameView(SubFrame* sub_frame, SubFrameView* view) {
  for (size_t band = 0; band < view->size(); ++band) {
    for (size_t channel = 0; channel < (*view)[band].size(); ++channel) {
      (*view)[band][channel] = std::span<float>((*sub_frame)[band][channel].data(), (*sub_frame)[band][channel].size());
    }
  }
}

// --- frame_blocker_unittest.cc ---

float BlockerSampleValue(size_t chunk_counter, size_t chunk_size, size_t band, size_t channel, size_t sample_index,
                         int offset) {
  float value = static_cast<float>(static_cast<int>(chunk_counter * chunk_size + sample_index + channel) + offset);
  return value > 0 ? 5000.f * static_cast<float>(band) + value : 0;
}

void FillBlockerSubFrame(size_t sub_frame_counter, int offset, SubFrame* sub_frame) {
  for (size_t band = 0; band < sub_frame->size(); ++band) {
    for (size_t channel = 0; channel < (*sub_frame)[band].size(); ++channel) {
      for (size_t sample = 0; sample < (*sub_frame)[band][channel].size(); ++sample) {
        (*sub_frame)[band][channel][sample] =
            BlockerSampleValue(sub_frame_counter, kSubFrameLength, band, channel, sample, offset);
      }
    }
  }
}

bool VerifyBlockerSubFrame(size_t sub_frame_counter, int offset, const SubFrameView& view) {
  SubFrame reference = MakeSubFrame(view.size(), view[0].size());
  FillBlockerSubFrame(sub_frame_counter, offset, &reference);
  for (size_t band = 0; band < view.size(); ++band) {
    for (size_t channel = 0; channel < view[band].size(); ++channel) {
      for (size_t sample = 0; sample < view[band][channel].size(); ++sample) {
        if (reference[band][channel][sample] != view[band][channel][sample]) return false;
      }
    }
  }
  return true;
}

bool VerifyBlock(size_t block_counter, int offset, const Block& block) {
  for (int band = 0; band < block.NumBands(); ++band) {
    for (int channel = 0; channel < block.NumChannels(); ++channel) {
      auto b = block.View(band, channel);
      for (size_t sample = 0; sample < kBlockSize; ++sample) {
        if (BlockerSampleValue(block_counter, kBlockSize, static_cast<size_t>(band), static_cast<size_t>(channel),
                               sample, offset) != b[sample]) {
          return false;
        }
      }
    }
  }
  return true;
}

void RunBlockerTest(int sample_rate_hz, size_t num_channels) {
  constexpr size_t kNumSubFramesToProcess = 20;
  const size_t num_bands = NumBandsForRate(sample_rate_hz);
  Block block(static_cast<int>(num_bands), static_cast<int>(num_channels));
  SubFrame input = MakeSubFrame(num_bands, num_channels);
  SubFrameView input_view(num_bands, std::vector<std::span<float>>(num_channels));
  SetupSubFrameView(&input, &input_view);
  FrameBlocker blocker(num_bands, num_channels);

  size_t block_counter = 0;
  for (size_t sub_frame_index = 0; sub_frame_index < kNumSubFramesToProcess; ++sub_frame_index) {
    FillBlockerSubFrame(sub_frame_index, 0, &input);
    blocker.InsertSubFrameAndExtractBlock(input_view, &block);
    // libwebrtc calls VerifyBlock without checking its result; it holds.
    SM_EXPECT_TRUE(VerifyBlock(block_counter++, 0, block));
    if ((sub_frame_index + 1) % 4 == 0) {
      SM_EXPECT_TRUE(blocker.IsBlockAvailable());
    } else {
      SM_EXPECT_FALSE(blocker.IsBlockAvailable());
    }
    if (blocker.IsBlockAvailable()) {
      blocker.ExtractBlock(&block);
      SM_EXPECT_TRUE(VerifyBlock(block_counter++, 0, block));
    }
  }
}

void RunBlockerAndFramerTest(int sample_rate_hz, size_t num_channels) {
  constexpr size_t kNumSubFramesToProcess = 20;
  const size_t num_bands = NumBandsForRate(sample_rate_hz);
  Block block(static_cast<int>(num_bands), static_cast<int>(num_channels));
  SubFrame input = MakeSubFrame(num_bands, num_channels);
  SubFrame output = MakeSubFrame(num_bands, num_channels);
  SubFrameView input_view(num_bands, std::vector<std::span<float>>(num_channels));
  SubFrameView output_view(num_bands, std::vector<std::span<float>>(num_channels));
  SetupSubFrameView(&input, &input_view);
  SetupSubFrameView(&output, &output_view);
  FrameBlocker blocker(num_bands, num_channels);
  BlockFramer framer(num_bands, num_channels);

  for (size_t sub_frame_index = 0; sub_frame_index < kNumSubFramesToProcess; ++sub_frame_index) {
    FillBlockerSubFrame(sub_frame_index, 0, &input);
    FillBlockerSubFrame(sub_frame_index, 0, &output);
    blocker.InsertSubFrameAndExtractBlock(input_view, &block);
    framer.InsertBlockAndExtractSubFrame(block, &output_view);
    if ((sub_frame_index + 1) % 4 == 0) {
      SM_EXPECT_TRUE(blocker.IsBlockAvailable());
    } else {
      SM_EXPECT_FALSE(blocker.IsBlockAvailable());
    }
    if (blocker.IsBlockAvailable()) {
      blocker.ExtractBlock(&block);
      framer.InsertBlock(block);
    }
    if (sub_frame_index > 1) SM_EXPECT_TRUE(VerifyBlockerSubFrame(sub_frame_index, -64, output_view));
  }
}

// --- block_framer_unittest.cc ---

float FramerSampleValue(size_t chunk_counter, size_t chunk_size, size_t band, size_t channel, size_t sample_index,
                        int offset) {
  float value = static_cast<float>(static_cast<int>(100 + chunk_counter * chunk_size + sample_index + channel) + offset);
  return 5000.f * static_cast<float>(band) + value;
}

bool VerifyFramerSubFrame(size_t sub_frame_counter, int offset, const SubFrameView& view) {
  for (size_t band = 0; band < view.size(); ++band) {
    for (size_t channel = 0; channel < view[band].size(); ++channel) {
      for (size_t sample = 0; sample < view[band][channel].size(); ++sample) {
        if (FramerSampleValue(sub_frame_counter, kSubFrameLength, band, channel, sample, offset) !=
            view[band][channel][sample]) {
          return false;
        }
      }
    }
  }
  return true;
}

void FillFramerBlock(size_t block_counter, Block* block) {
  for (int band = 0; band < block->NumBands(); ++band) {
    for (int channel = 0; channel < block->NumChannels(); ++channel) {
      auto b = block->View(band, channel);
      for (size_t sample = 0; sample < kBlockSize; ++sample) {
        b[sample] =
            FramerSampleValue(block_counter, kBlockSize, static_cast<size_t>(band), static_cast<size_t>(channel), sample, 0);
      }
    }
  }
}

void RunFramerTest(int sample_rate_hz, size_t num_channels) {
  constexpr size_t kNumSubFramesToProcess = 10;
  const size_t num_bands = NumBandsForRate(sample_rate_hz);
  Block block(static_cast<int>(num_bands), static_cast<int>(num_channels));
  SubFrame output = MakeSubFrame(num_bands, num_channels);
  SubFrameView output_view(num_bands, std::vector<std::span<float>>(num_channels));
  SetupSubFrameView(&output, &output_view);
  BlockFramer framer(num_bands, num_channels);

  size_t block_index = 0;
  for (size_t sub_frame_index = 0; sub_frame_index < kNumSubFramesToProcess; ++sub_frame_index) {
    FillFramerBlock(block_index++, &block);
    framer.InsertBlockAndExtractSubFrame(block, &output_view);
    if (sub_frame_index > 1) SM_EXPECT_TRUE(VerifyFramerSubFrame(sub_frame_index, -64, output_view));
    if ((sub_frame_index + 1) % 4 == 0) {
      FillFramerBlock(block_index++, &block);
      framer.InsertBlock(block);
    }
  }
}

}  // namespace

SM_TEST(Aec3, FrameBlockerBlockBitexactness) {
  for (int rate : {16000, 32000, 48000}) {
    for (size_t num_channels : {1, 2, 4, 8}) RunBlockerTest(rate, num_channels);
  }
}

SM_TEST(Aec3, FrameBlockerBlockerAndFramer) {
  for (int rate : {16000, 32000, 48000}) {
    for (size_t num_channels : {1, 2, 4, 8}) RunBlockerAndFramerTest(rate, num_channels);
  }
}

SM_TEST(Aec3, BlockFramerFrameBitexactness) {
  for (int rate : {16000, 32000, 48000}) {
    for (size_t num_channels : {1, 2, 4, 8}) RunFramerTest(rate, num_channels);
  }
}

// block_delay_buffer_unittest.cc: CorrectDelayApplied, every combination.
SM_TEST(Aec3, BlockDelayBufferCorrectDelayApplied) {
  auto sample_value = [](size_t sample_index) { return static_cast<float>(sample_index % 32768); };
  for (size_t delay : {0, 1, 27, 160, 4321, 7021}) {
    for (int rate : {16000, 32000, 48000}) {
      for (size_t num_channels : {1, 2, 4}) {
        const size_t num_bands = NumBandsForRate(rate);
        constexpr size_t kSubbandFrameLength = 160;
        BlockDelayBuffer delay_buffer(num_channels, num_bands, kSubbandFrameLength, delay);
        for (size_t frame_index = 0; frame_index < 20; ++frame_index) {
          Frame frame(num_bands, std::vector<std::vector<float>>(num_channels, std::vector<float>(kSubbandFrameLength)));
          const size_t first_sample_index = frame_index * kSubbandFrameLength;
          for (auto& band : frame) {
            for (auto& channel : band) {
              for (size_t i = 0; i < kSubbandFrameLength; ++i) channel[i] = sample_value(first_sample_index + i);
            }
          }
          delay_buffer.DelaySignal(&frame);
          bool ok = true;
          for (size_t ch = 0; ch < num_channels; ++ch) {
            for (size_t band = 0; band < num_bands; ++band) {
              size_t sample_index = first_sample_index;
              for (size_t i = 0; i < kSubbandFrameLength; ++i, ++sample_index) {
                const float want = sample_index < delay ? 0.f : sample_value(sample_index - delay);
                ok = ok && frame[band][ch][i] == want;
              }
            }
          }
          SM_EXPECT_TRUE(ok);
        }
      }
    }
  }
}

// --- echo_remover_metrics_unittest.cc ---

SM_TEST(Aec3, UpdateDbMetricUpdating) {
  std::array<float, kFftLengthBy2Plus1> value;
  std::array<EchoRemoverMetrics::DbMetric, 2> statistic;
  statistic.fill(EchoRemoverMetrics::DbMetric(0.f, 100.f, -100.f));
  constexpr float kValue0 = 10.f;
  constexpr float kValue1 = 20.f;
  value.fill(0.f);
  std::fill(value.begin(), value.begin() + 32, kValue0);
  std::fill(value.begin() + 32, value.begin() + 64, kValue1);

  UpdateDbMetric(value, &statistic);
  SM_EXPECT_NEAR(statistic[0].sum_value, kValue0, 1e-5);
  SM_EXPECT_NEAR(statistic[0].ceil_value, kValue0, 1e-5);
  SM_EXPECT_NEAR(statistic[0].floor_value, kValue0, 1e-5);
  SM_EXPECT_NEAR(statistic[1].sum_value, kValue1, 1e-5);
  SM_EXPECT_NEAR(statistic[1].ceil_value, kValue1, 1e-5);
  SM_EXPECT_NEAR(statistic[1].floor_value, kValue1, 1e-5);

  UpdateDbMetric(value, &statistic);
  SM_EXPECT_NEAR(statistic[0].sum_value, 2.f * kValue0, 1e-5);
  SM_EXPECT_NEAR(statistic[0].ceil_value, kValue0, 1e-5);
  SM_EXPECT_NEAR(statistic[0].floor_value, kValue0, 1e-5);
  SM_EXPECT_NEAR(statistic[1].sum_value, 2.f * kValue1, 1e-5);
  SM_EXPECT_NEAR(statistic[1].ceil_value, kValue1, 1e-5);
  SM_EXPECT_NEAR(statistic[1].floor_value, kValue1, 1e-5);
}

SM_TEST(Aec3, TransformDbMetricForReportingDbFsScaling) {
  std::array<float, kBlockSize> x;
  FftData X;
  std::array<float, kFftLengthBy2Plus1> X2;
  Aec3Fft fft;
  x.fill(1000.f);
  fft.ZeroPaddedFft(x, Aec3Fft::Window::kRectangular, &X);
  X.Spectrum(X2);

  float offset = -10.f * std::log10(32768.f * 32768.f);
  SM_EXPECT_NEAR(offset, -90.3f, 0.1f);
  SM_EXPECT_EQ(static_cast<int>(30.3f),
               TransformDbMetricForReporting(true, 0.f, 90.f, offset, 1.f / (kBlockSize * kBlockSize), X2[0]));
}

SM_TEST(Aec3, TransformDbMetricForReportingLimits) {
  SM_EXPECT_EQ(0, TransformDbMetricForReporting(false, 0.f, 10.f, 0.f, 1.f, 0.001f));
  SM_EXPECT_EQ(10, TransformDbMetricForReporting(false, 0.f, 10.f, 0.f, 1.f, 100.f));
}

SM_TEST(Aec3, TransformDbMetricForReportingNegate) {
  SM_EXPECT_EQ(10, TransformDbMetricForReporting(true, -20.f, 20.f, 0.f, 1.f, 0.1f));
  SM_EXPECT_EQ(-10, TransformDbMetricForReporting(true, -20.f, 20.f, 0.f, 1.f, 10.f));
}

SM_TEST(Aec3, DbMetricUpdate) {
  EchoRemoverMetrics::DbMetric metric(0.f, 20.f, -20.f);
  constexpr int kNumValues = 100;
  constexpr float kValue = 10.f;
  for (int k = 0; k < kNumValues; ++k) metric.Update(kValue);
  SM_EXPECT_NEAR(metric.sum_value, kValue * kNumValues, 1e-4);
  SM_EXPECT_NEAR(metric.ceil_value, kValue, 1e-5);
  SM_EXPECT_NEAR(metric.floor_value, kValue, 1e-5);
}

SM_TEST(Aec3, DbMetricUpdateInstant) {
  EchoRemoverMetrics::DbMetric metric(0.f, 20.f, -20.f);
  constexpr float kMinValue = -77.f;
  constexpr float kMaxValue = 33.f;
  constexpr float kLastValue = (kMinValue + kMaxValue) / 2.0f;
  for (float value = kMinValue; value <= kMaxValue; value++) metric.UpdateInstant(value);
  metric.UpdateInstant(kLastValue);
  SM_EXPECT_NEAR(metric.sum_value, kLastValue, 1e-5);
  SM_EXPECT_NEAR(metric.ceil_value, kMaxValue, 1e-5);
  SM_EXPECT_NEAR(metric.floor_value, kMinValue, 1e-5);
}

SM_TEST(Aec3, DbMetricConstructor) {
  EchoRemoverMetrics::DbMetric metric;
  SM_EXPECT_NEAR(metric.sum_value, 0.f, 1e-9);
  SM_EXPECT_NEAR(metric.ceil_value, 0.f, 1e-9);
  SM_EXPECT_NEAR(metric.floor_value, 0.f, 1e-9);
  metric = EchoRemoverMetrics::DbMetric(1.f, 2.f, 3.f);
  SM_EXPECT_NEAR(metric.sum_value, 1.f, 1e-9);
  SM_EXPECT_NEAR(metric.floor_value, 2.f, 1e-9);
  SM_EXPECT_NEAR(metric.ceil_value, 3.f, 1e-9);
}

SM_TEST(Aec3, EchoRemoverMetricsNormalUsage) {
  EchoRemoverMetrics metrics;
  AecState aec_state(Aec3Config{}, 1);
  for (int j = 0; j < 3; ++j) {
    for (int k = 0; k < EchoRemoverMetrics::kMetricsReportingIntervalBlocks - 1; ++k) {
      metrics.Update(aec_state);
      SM_EXPECT_FALSE(metrics.MetricsReported());
    }
    metrics.Update(aec_state);
    SM_EXPECT_TRUE(metrics.MetricsReported());
  }
}

// --- echo_remover_unittest.cc ---

// BasicApiCalls, for 1, 2 and 8 render and capture channels.
SM_TEST(Aec3, EchoRemoverBasicApiCalls) {
  for (size_t num_render_channels : {1, 2, 8}) {
    for (size_t num_capture_channels : {1, 2, 8}) {
      std::optional<DelayEstimate> delay_estimate;
      for (int rate : {16000, 32000, 48000}) {
        auto remover = std::make_unique<EchoRemover>(Aec3Config{}, rate, num_render_channels, num_capture_channels);
        auto render_buffer = std::make_unique<RenderDelayBuffer>(Aec3Config{}, rate, num_render_channels);
        const int num_bands = static_cast<int>(NumBandsForRate(rate));
        Block render(num_bands, static_cast<int>(num_render_channels));
        Block capture(num_bands, static_cast<int>(num_capture_channels));
        for (size_t k = 0; k < 100; ++k) {
          EchoPathVariability echo_path_variability(
              k % 3 == 0, k % 5 == 0 ? EchoPathVariability::DelayAdjustment::kNewDetectedDelay
                                     : EchoPathVariability::DelayAdjustment::kNone,
              false);
          render_buffer->Insert(render);
          render_buffer->PrepareCaptureProcessing();
          remover->ProcessCapture(echo_path_variability, k % 2 == 0, delay_estimate,
                                  render_buffer->GetRenderBuffer(), nullptr, &capture);
        }
        // Silence in, silence out.
        float energy = 0.f;
        for (int ch = 0; ch < static_cast<int>(num_capture_channels); ++ch) {
          for (float v : capture.View(0, ch)) energy += v * v;
        }
        SM_EXPECT_NEAR(energy, 0.f, 1e-3);
      }
    }
  }
}

// BasicEchoRemoval: noise bursts of 10 blocks every 100, echoed at 0-301
// samples; the second half of the output has under a tenth of the input
// energy.
SM_TEST(Aec3, EchoRemoverBasicEchoRemoval) {
  constexpr int kNumBlocksToProcess = 500;
  Random random_generator(42U);
  std::optional<DelayEstimate> delay_estimate;
  for (size_t num_channels : {1, 2, 4}) {
    for (int rate : {16000, 32000, 48000}) {
      const int num_bands = static_cast<int>(NumBandsForRate(rate));
      Block x(num_bands, static_cast<int>(num_channels));
      Block y(num_bands, static_cast<int>(num_channels));
      EchoPathVariability echo_path_variability(false, EchoPathVariability::DelayAdjustment::kNone, false);
      for (size_t delay_samples : {0, 64, 150, 200, 301}) {
        Aec3Config config;
        auto remover = std::make_unique<EchoRemover>(config, rate, num_channels, num_channels);
        auto render_buffer = std::make_unique<RenderDelayBuffer>(config, rate, num_channels);
        render_buffer->AlignFromDelay(delay_samples / kBlockSize);

        std::vector<std::vector<DelayBuffer>> delay_buffers(
            static_cast<size_t>(num_bands), std::vector<DelayBuffer>(num_channels, DelayBuffer(delay_samples)));

        float input_energy = 0.f;
        float output_energy = 0.f;
        for (int k = 0; k < kNumBlocksToProcess; ++k) {
          const bool silence = k < 100 || (k % 100 >= 10);
          for (int band = 0; band < num_bands; ++band) {
            for (int channel = 0; channel < static_cast<int>(num_channels); ++channel) {
              auto xv = x.View(band, channel);
              if (silence) {
                std::fill(xv.begin(), xv.end(), 0.f);
              } else {
                RandomizeSampleVector(random_generator, xv);
              }
              delay_buffers[static_cast<size_t>(band)][static_cast<size_t>(channel)].Delay(xv, y.View(band, channel));
            }
          }
          if (k > kNumBlocksToProcess / 2) {
            auto yv = y.View(0, 0);
            input_energy = std::inner_product(yv.begin(), yv.end(), yv.begin(), input_energy);
          }
          render_buffer->Insert(x);
          render_buffer->PrepareCaptureProcessing();
          remover->ProcessCapture(echo_path_variability, false, delay_estimate, render_buffer->GetRenderBuffer(),
                                  nullptr, &y);
          if (k > kNumBlocksToProcess / 2) {
            auto yv = y.View(0, 0);
            output_energy = std::inner_product(yv.begin(), yv.end(), yv.begin(), output_energy);
          }
        }
        SM_EXPECT_GT(input_energy, 10.f * output_energy);
      }
    }
  }
}

// --- block_processor_unittest.cc ---

SM_TEST(Aec3, BlockProcessorBasicSetupAndApiCalls) {
  for (int rate : {16000, 32000, 48000}) {
    BlockProcessor block_processor(Aec3Config{}, rate, 1, 1);
    Block block(static_cast<int>(NumBandsForRate(rate)), 1, 1000.f);
    block_processor.BufferRender(block);
    block_processor.ProcessCapture(false, false, nullptr, &block);
    block_processor.UpdateEchoLeakageStatus(false);
  }
}

SM_TEST(Aec3, BlockProcessorTestLongerCall) {
  BlockProcessor block_processor(Aec3Config{}, 16000, 1, 1);
  Block block(1, 1, 1000.f);
  for (int k = 0; k < 20 * kNumBlocksPerSecond; ++k) {
    block_processor.BufferRender(block);
    block_processor.ProcessCapture(false, false, nullptr, &block);
    block_processor.UpdateEchoLeakageStatus(false);
  }
  // The capture is the render: the echo is found and removed.
  float energy = 0.f;
  for (float v : block.View(0, 0)) energy += v * v;
  SM_EXPECT_LT(energy, 1000.f * 1000.f * kBlockSize / 100.f);
}

// --- EchoCanceller3 (from echo_canceller3_unittest.cc) ---

// CaptureBitexactness on the real processor: with no render the capture is
// passed through, delayed by the 64 samples of the blocking, in every band.
SM_TEST(Aec3, EchoCanceller3CaptureDelayedByOneBlockWithoutRender) {
  for (int rate : {16000, 32000, 48000}) {
    const size_t num_bands = NumBandsForRate(rate);
    EchoCanceller3 aec3(Aec3Config{}, rate, 1, 1);
    std::vector<std::vector<float>> in(num_bands), out(num_bands);
    for (size_t frame_index = 0; frame_index < 20; ++frame_index) {
      Frame frame(num_bands, std::vector<std::vector<float>>(1, std::vector<float>(kFrameSize)));
      for (size_t band = 0; band < num_bands; ++band) {
        for (size_t k = 0; k < kFrameSize; ++k) {
          frame[band][0][k] = static_cast<float>(1000 * band + frame_index * kFrameSize + k + 1);
        }
        in[band].insert(in[band].end(), frame[band][0].begin(), frame[band][0].end());
      }
      aec3.ProcessCapture(&frame, false);
      for (size_t band = 0; band < num_bands; ++band) {
        out[band].insert(out[band].end(), frame[band][0].begin(), frame[band][0].end());
      }
    }
    for (size_t band = 0; band < num_bands; ++band) {
      bool ok = true;
      for (size_t k = 0; k < out[band].size(); ++k) {
        const float want = k < kBlockSize ? 0.f : in[band][k - kBlockSize];
        ok = ok && out[band][k] == want;
      }
      SM_EXPECT_TRUE(ok);
    }
  }
}

// RenderSwapQueueOverrunReturnValue: the queue holds 100 frames; further
// frames are lost until a capture call empties it.
SM_TEST(Aec3, EchoCanceller3RenderQueueOverrun) {
  EchoCanceller3 aec3(Aec3Config{}, 16000, 1, 1);
  std::vector<float> render(kFrameSize, 100.f);
  for (size_t k = 0; k < kRenderTransferQueueSizeFrames; ++k) aec3.AnalyzeRender(render);
  SM_EXPECT_EQ(aec3.render_frames_dropped(), 0u);
  aec3.AnalyzeRender(render);
  SM_EXPECT_EQ(aec3.render_frames_dropped(), 1u);
  std::vector<float> capture(kFrameSize, 0.f);
  aec3.ProcessCapture(capture);
  aec3.AnalyzeRender(render);
  SM_EXPECT_EQ(aec3.render_frames_dropped(), 1u);
}

// CaptureSaturation: a capture sample at 32700 marks the capture saturated in
// AecState; one under it does not.
SM_TEST(Aec3, EchoCanceller3CaptureSaturation) {
  EchoCanceller3 aec3(Aec3Config{}, 16000, 1, 1);
  std::vector<float> render(kFrameSize, 0.f);
  std::vector<float> capture(kFrameSize, 0.f);
  capture[10] = 32699.f;
  aec3.AnalyzeRender(render);
  aec3.ProcessCapture(capture);
  SM_EXPECT_FALSE(aec3.block_processor().echo_remover().aec_state().SaturatedCapture());
  std::fill(capture.begin(), capture.end(), 0.f);
  capture[10] = 32700.f;
  aec3.AnalyzeRender(render);
  aec3.ProcessCapture(capture);
  SM_EXPECT_TRUE(aec3.block_processor().echo_remover().aec_state().SaturatedCapture());
}

// #594: libwebrtc's EchoCanceller3 runs only at a full-band rate of 16, 32 or
// 48 kHz (`RTC_DCHECK(ValidFullBandRate(...))`, echo_canceller3.cc, and APM
// never hands it another rate). Any other rate is rejected: valid() is false
// and every call leaves the audio untouched, where it used to corrupt memory.
SM_TEST(Aec3, EchoCanceller3RejectsUnsupportedRates) {
  for (int rate : {0, 8000, 11025, 12000, 24000, 44100, -16000}) {
    EchoCanceller3 aec3(Aec3Config{}, rate, 1, 1);
    SM_EXPECT_FALSE(aec3.valid());
    std::vector<float> render(kFrameSize, 100.f);
    std::vector<float> capture(kFrameSize, 50.f);
    Frame frame(kMaxNumBands, std::vector<std::vector<float>>(1, std::vector<float>(kFrameSize, 50.f)));
    for (int i = 0; i < 50; ++i) {
      aec3.AnalyzeRender(render);
      aec3.ProcessCapture(capture);
      aec3.AnalyzeRender(frame);
      aec3.ProcessCapture(&frame, false);
    }
    SM_EXPECT_TRUE(std::all_of(capture.begin(), capture.end(), [](float x) { return x == 50.f; }));
    SM_EXPECT_TRUE(std::all_of(frame[0][0].begin(), frame[0][0].end(), [](float x) { return x == 50.f; }));
  }
}

SM_TEST(Aec3, EchoCanceller3RunsAtFullBandRates) {
  for (int rate : {16000, 32000, 48000}) {
    EchoCanceller3 aec3(Aec3Config{}, rate, 1, 1);
    SM_EXPECT_TRUE(aec3.valid());
    const size_t bands = static_cast<size_t>(rate / 16000);
    Random rng(rate);
    Frame render(bands, std::vector<std::vector<float>>(1, std::vector<float>(kFrameSize)));
    Frame capture = render;
    for (int i = 0; i < 50; ++i) {
      for (size_t b = 0; b < bands; ++b) {
        RandomizeSampleVector(rng, render[b][0], 1000.f);
        RandomizeSampleVector(rng, capture[b][0], 1000.f);
      }
      aec3.AnalyzeRender(render);
      aec3.ProcessCapture(&capture, false);
    }
    SM_EXPECT_EQ(aec3.render_frames_dropped(), 0u);
  }
}

// The mono convenience calls hold one 160-sample 16 kHz frame: at another
// rate, or with another length, they leave the audio untouched.
SM_TEST(Aec3, EchoCanceller3MonoCallsNeed16kHzFrames) {
  EchoCanceller3 at32k(Aec3Config{}, 32000, 1, 1);
  std::vector<float> capture(2 * kFrameSize, 50.f);
  for (int i = 0; i < 10; ++i) {
    at32k.AnalyzeRender(std::span<const float>(capture));
    at32k.ProcessCapture(std::span<float>(capture));
  }
  SM_EXPECT_TRUE(std::all_of(capture.begin(), capture.end(), [](float x) { return x == 50.f; }));
  EchoCanceller3 at16k(Aec3Config{}, 16000, 1, 1);
  for (int i = 0; i < 10; ++i) {
    at16k.AnalyzeRender(std::span<const float>(capture));
    at16k.ProcessCapture(std::span<float>(capture));
  }
  SM_EXPECT_TRUE(std::all_of(capture.begin(), capture.end(), [](float x) { return x == 50.f; }));
}
