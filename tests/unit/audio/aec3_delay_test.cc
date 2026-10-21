// Ports of libwebrtc's AEC3 delay-estimation unit tests (`apm v2`, #240):
// decimator_unittest.cc, matched_filter_unittest.cc,
// matched_filter_lag_aggregator_unittest.cc, clockdrift_detector_unittest.cc,
// echo_path_delay_estimator_unittest.cc (mono cases), plus delay-controller
// tests on speech. Signals are in AEC3's FloatS16 scale (+-32767).
// RandomizeSampleVector and DelayBuffer come from libwebrtc's
// echo_canceller_test_tools; sm::Random stands in for rtc::Random.
#include "sm/audio/aec3_delay.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <numbers>
#include <numeric>

#include "sm/core/random.h"
#include "sm/sim/signals.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::audio::aec3;

namespace {
constexpr size_t kDownSamplingFactors[] = {4, 8};
constexpr int kNumMatchedFilters = 10;
constexpr size_t kWindowSizeSubBlocks = 32;
constexpr size_t kAlignmentShiftSubBlocks = kWindowSizeSubBlocks * 3 / 4;

void RandomizeSampleVector(Random& rng, std::span<float> v, float amplitude = 32767.f) {
  for (auto& x : v) x = static_cast<float>(rng.Uniform(-amplitude, amplitude));
}

// Delays a signal by a fixed number of samples across calls.
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

MatchedFilter MakeFilter(size_t down_sampling_factor, bool detect_pre_echo) {
  DelayConfig config;
  return MatchedFilter(kBlockSize / down_sampling_factor, kWindowSizeSubBlocks, kNumMatchedFilters,
                       kAlignmentShiftSubBlocks, 150, config.delay_estimate_smoothing,
                       config.delay_estimate_smoothing_delay_found, config.delay_candidate_detection_threshold,
                       detect_pre_echo);
}

float Power(std::span<const float> x) { return std::inner_product(x.begin(), x.end(), x.begin(), 0.f) / x.size(); }
}  // namespace

// decimator_unittest.cc: little aliasing from upper frequencies.
SM_TEST(Aec3Delay, DecimatorNoLeakageFromUpperFrequencies) {
  constexpr size_t kNumStartupBlocks = 50;
  constexpr size_t kNumBlocks = 1000;
  for (int rate : {16000, 32000, 48000}) {
    for (size_t factor : kDownSamplingFactors) {
      const size_t sub_block_size = kBlockSize / factor;
      std::vector<float> input(kBlockSize * kNumBlocks);
      for (size_t k = 0; k < input.size(); ++k) {
        input[k] = 32767.f * std::sin(2.f * std::numbers::pi_v<float> * (3.f / 8.f * rate) * k / rate);
      }
      Decimator decimator(factor);
      std::vector<float> output(sub_block_size * kNumBlocks);
      for (size_t k = 0; k < kNumBlocks; ++k) {
        decimator.Decimate(std::span<const float>(&input[k * kBlockSize], kBlockSize),
                           std::span<float>(&output[k * sub_block_size], sub_block_size));
      }
      const float input_power = Power(std::span<const float>(input).subspan(kNumStartupBlocks * kBlockSize));
      const float output_power = Power(std::span<const float>(output).subspan(kNumStartupBlocks * sub_block_size));
      SM_EXPECT_GT(0.0001f * input_power, output_power);
    }
  }
}

// matched_filter_unittest.cc
SM_TEST(Aec3Delay, MaxSquarePeakIndex) {
  Random rng(42);
  for (int length = 1; length < 128; ++length) {
    std::vector<float> y(length);
    for (int i = 0; i < 256; ++i) {
      RandomizeSampleVector(rng, y);
      size_t expected = std::distance(
          y.begin(), std::max_element(y.begin(), y.end(), [](float a, float b) { return a * a < b * b; }));
      SM_EXPECT_EQ(MaxSquarePeakIndex(y), expected);
    }
  }
}

SM_TEST(Aec3Delay, MatchedFilterLagEstimation) {
  for (bool detect_pre_echo : {true, false}) {
    Random rng(42);
    for (size_t factor : kDownSamplingFactors) {
      const size_t sub_block_size = kBlockSize / factor;
      for (size_t delay_samples : {5, 64, 150, 200, 800, 1000}) {
        DelayConfig config;
        config.down_sampling_factor = factor;
        config.num_filters = kNumMatchedFilters;
        Decimator capture_decimator(factor);
        DelayBuffer signal_delay_buffer(factor * delay_samples);
        MatchedFilter filter = MakeFilter(factor, detect_pre_echo);
        RenderDelayBufferLowRate render_delay_buffer(config);
        std::vector<float> render(kBlockSize), capture(kBlockSize);
        for (size_t k = 0; k < 600 + delay_samples / sub_block_size; ++k) {
          RandomizeSampleVector(rng, render);
          signal_delay_buffer.Delay(render, capture);
          render_delay_buffer.Insert(render);
          if (k == 0) render_delay_buffer.Reset();
          render_delay_buffer.PrepareCaptureProcessing();
          std::array<float, kBlockSize> ds;
          std::span<float> downsampled_capture(ds.data(), sub_block_size);
          capture_decimator.Decimate(capture, downsampled_capture);
          filter.Update(render_delay_buffer.GetDownsampledRenderBuffer(), downsampled_capture, false);
        }
        auto lag_estimate = filter.GetBestLagEstimate();
        SM_ASSERT_TRUE(lag_estimate.has_value());
        SM_EXPECT_EQ(lag_estimate->lag, delay_samples);
        SM_EXPECT_EQ(lag_estimate->pre_echo_lag, delay_samples);
      }
    }
  }
}

SM_TEST(Aec3Delay, MatchedFilterPreEchoEstimation) {
  for (bool detect_pre_echo : {true, false}) {
    Random rng(42);
    for (size_t factor : kDownSamplingFactors) {
      const size_t sub_block_size = kBlockSize / factor;
      const size_t pre_echo_delay_samples = static_cast<size_t>(20e-3 * 16000 / factor);
      const size_t echo_delay_samples = static_cast<size_t>(50e-3 * 16000 / factor);
      DelayConfig config;
      config.down_sampling_factor = factor;
      config.num_filters = kNumMatchedFilters;
      Decimator capture_decimator(factor);
      DelayBuffer echo_delay(factor * echo_delay_samples);
      DelayBuffer pre_echo_delay(factor * pre_echo_delay_samples);
      MatchedFilter filter = MakeFilter(factor, detect_pre_echo);
      RenderDelayBufferLowRate render_delay_buffer(config);
      std::vector<float> render(kBlockSize), capture(kBlockSize), capture_with_pre_echo(kBlockSize);
      for (size_t k = 0; k < 600 + echo_delay_samples / sub_block_size; ++k) {
        RandomizeSampleVector(rng, render);
        echo_delay.Delay(render, capture);
        pre_echo_delay.Delay(render, capture_with_pre_echo);
        for (size_t l = 0; l < capture.size(); ++l) capture[l] += 0.8f * capture_with_pre_echo[l];
        render_delay_buffer.Insert(render);
        if (k == 0) render_delay_buffer.Reset();
        render_delay_buffer.PrepareCaptureProcessing();
        std::array<float, kBlockSize> ds;
        std::span<float> downsampled_capture(ds.data(), sub_block_size);
        capture_decimator.Decimate(capture, downsampled_capture);
        filter.Update(render_delay_buffer.GetDownsampledRenderBuffer(), downsampled_capture, false);
      }
      auto lag_estimate = filter.GetBestLagEstimate();
      SM_ASSERT_TRUE(lag_estimate.has_value());
      SM_EXPECT_EQ(lag_estimate->lag, echo_delay_samples);
      if (detect_pre_echo) {
        // Estimated in a subsampled domain, so a larger error is allowed.
        SM_EXPECT_NEAR(static_cast<double>(lag_estimate->pre_echo_lag), pre_echo_delay_samples, 4);
      } else {
        SM_EXPECT_EQ(lag_estimate->pre_echo_lag, echo_delay_samples);
      }
    }
  }
}

SM_TEST(Aec3Delay, MatchedFilterLagNotReliableForUncorrelatedRenderAndCapture) {
  for (bool detect_pre_echo : {true, false}) {
    Random rng(42);
    for (size_t factor : kDownSamplingFactors) {
      DelayConfig config;
      config.down_sampling_factor = factor;
      config.num_filters = kNumMatchedFilters;
      const size_t sub_block_size = kBlockSize / factor;
      std::vector<float> render(kBlockSize), capture(sub_block_size, 0.f);
      RenderDelayBufferLowRate render_delay_buffer(config);
      MatchedFilter filter = MakeFilter(factor, detect_pre_echo);
      for (size_t k = 0; k < 100; ++k) {
        RandomizeSampleVector(rng, render);
        RandomizeSampleVector(rng, capture);
        render_delay_buffer.Insert(render);
        filter.Update(render_delay_buffer.GetDownsampledRenderBuffer(), capture, false);
      }
      SM_EXPECT_FALSE(filter.GetBestLagEstimate().has_value());
    }
  }
}

SM_TEST(Aec3Delay, MatchedFilterLagNotUpdatedForLowLevelRender) {
  for (bool detect_pre_echo : {true, false}) {
    Random rng(42);
    for (size_t factor : kDownSamplingFactors) {
      const size_t sub_block_size = kBlockSize / factor;
      std::vector<float> render(kBlockSize), capture(kBlockSize);
      MatchedFilter filter = MakeFilter(factor, detect_pre_echo);
      RenderDelayBufferLowRate render_delay_buffer{DelayConfig{}};
      Decimator capture_decimator(factor);
      for (size_t k = 0; k < 100; ++k) {
        RandomizeSampleVector(rng, render);
        for (auto& r : render) r *= 149.f / 32767.f;
        std::copy(render.begin(), render.end(), capture.begin());
        std::array<float, kBlockSize> ds;
        std::span<float> downsampled_capture(ds.data(), sub_block_size);
        capture_decimator.Decimate(capture, downsampled_capture);
        filter.Update(render_delay_buffer.GetDownsampledRenderBuffer(), downsampled_capture, false);
      }
      SM_EXPECT_FALSE(filter.GetBestLagEstimate().has_value());
    }
  }
}

// matched_filter_lag_aggregator_unittest.cc
SM_TEST(Aec3Delay, LagAggregatorInvarianceRequiredForAggregatedLag) {
  constexpr size_t kNumLagsBeforeDetection = 26;
  MatchedFilterLagAggregator aggregator(/*max_filter_lag=*/100, DelayConfig{});
  std::optional<DelayEstimate> aggregated_lag;
  for (size_t k = 0; k < kNumLagsBeforeDetection; ++k) {
    aggregated_lag = aggregator.Aggregate(MatchedFilter::LagEstimate(10, 10));
  }
  SM_EXPECT_TRUE(aggregated_lag.has_value());
  for (size_t k = 0; k < kNumLagsBeforeDetection * 100; ++k) {
    aggregated_lag = aggregator.Aggregate(MatchedFilter::LagEstimate(k % 100, k % 100));
  }
  SM_EXPECT_FALSE(aggregated_lag.has_value());
  for (size_t k = 0; k < kNumLagsBeforeDetection * 100; ++k) {
    aggregated_lag = aggregator.Aggregate(MatchedFilter::LagEstimate(k % 100, k % 100));
    SM_EXPECT_FALSE(aggregated_lag.has_value());
  }
}

// The coarse estimate needs more than 5 votes and the refined one more than
// 20 (delay_selection_thresholds {5, 20}); the delay is reported minus the
// 32-sample headroom (8 at 4 kHz), in 16-sample pre-echo blocks.
SM_TEST(Aec3Delay, LagAggregatorCoarseThenRefined) {
  MatchedFilterLagAggregator aggregator(/*max_filter_lag=*/700, DelayConfig{});
  std::optional<DelayEstimate> lag;
  for (int k = 0; k < 5; ++k) {
    lag = aggregator.Aggregate(MatchedFilter::LagEstimate(250, 250));
    SM_EXPECT_FALSE(lag.has_value());
  }
  lag = aggregator.Aggregate(MatchedFilter::LagEstimate(250, 250));
  SM_ASSERT_TRUE(lag.has_value());
  SM_EXPECT_TRUE(lag->quality == DelayEstimate::Quality::kCoarse);
  SM_EXPECT_EQ(lag->delay, (250u - 8u) >> 4 << 4);
  SM_EXPECT_FALSE(aggregator.ReliableDelayFound());
  for (int k = 0; k < 15; ++k) lag = aggregator.Aggregate(MatchedFilter::LagEstimate(250, 250));
  SM_EXPECT_TRUE(aggregator.ReliableDelayFound());
  SM_ASSERT_TRUE(lag.has_value());
  SM_EXPECT_TRUE(lag->quality == DelayEstimate::Quality::kRefined);
  SM_EXPECT_EQ(aggregator.GetDelayAtHighestPeak(), 242);
}

// clockdrift_detector_unittest.cc
SM_TEST(Aec3Delay, ClockdriftDetector) {
  using Level = ClockdriftDetector::Level;
  ClockdriftDetector c;
  SM_EXPECT_TRUE(c.ClockdriftLevel() == Level::kNone);
  for (int i = 0; i < 100; i++) c.Update(1000);
  SM_EXPECT_TRUE(c.ClockdriftLevel() == Level::kNone);
  for (int i = 0; i < 100; i++) c.Update(1001);
  SM_EXPECT_TRUE(c.ClockdriftLevel() == Level::kNone);
  for (int i = 0; i < 100; i++) c.Update(1002);
  SM_EXPECT_TRUE(c.ClockdriftLevel() == Level::kProbable);
  for (int i = 0; i < 100; i++) c.Update(1003);
  SM_EXPECT_TRUE(c.ClockdriftLevel() == Level::kVerified);
  for (int i = 0; i < 10000; i++) c.Update(1003);
  SM_EXPECT_TRUE(c.ClockdriftLevel() == Level::kNone);
  for (int i = 0; i < 100; i++) c.Update(1001);
  for (int i = 0; i < 100; i++) c.Update(999);
  SM_EXPECT_TRUE(c.ClockdriftLevel() == Level::kProbable);
  for (int i = 0; i < 100; i++) c.Update(1000);
  for (int i = 0; i < 100; i++) c.Update(998);
  SM_EXPECT_TRUE(c.ClockdriftLevel() == Level::kVerified);
}

// echo_path_delay_estimator_unittest.cc
SM_TEST(Aec3Delay, EchoPathDelayEstimatorBasicApiCalls) {
  DelayConfig config;
  RenderDelayBufferLowRate render_delay_buffer(config);
  EchoPathDelayEstimator estimator(config);
  std::vector<float> render(kBlockSize, 0.f), capture(kBlockSize, 0.f);
  for (size_t k = 0; k < 100; ++k) {
    render_delay_buffer.Insert(render);
    SM_EXPECT_FALSE(estimator.EstimateDelay(render_delay_buffer.GetDownsampledRenderBuffer(), capture).has_value());
  }
}

SM_TEST(Aec3Delay, EchoPathDelayEstimatorDelayEstimation) {
  Random rng(42);
  for (size_t factor : kDownSamplingFactors) {
    DelayConfig config;
    config.delay_headroom_samples = 0;
    config.down_sampling_factor = factor;
    config.num_filters = 10;
    for (size_t delay_samples : {30, 64, 150, 200, 800, 4000}) {
      RenderDelayBufferLowRate render_delay_buffer(config);
      DelayBuffer signal_delay_buffer(delay_samples);
      EchoPathDelayEstimator estimator(config);
      std::vector<float> render(kBlockSize), capture(kBlockSize);
      std::optional<DelayEstimate> estimated;
      for (size_t k = 0; k < 500 + delay_samples / kBlockSize; ++k) {
        RandomizeSampleVector(rng, render);
        signal_delay_buffer.Delay(render, capture);
        render_delay_buffer.Insert(render);
        if (k == 0) render_delay_buffer.Reset();
        render_delay_buffer.PrepareCaptureProcessing();
        auto estimate = estimator.EstimateDelay(render_delay_buffer.GetDownsampledRenderBuffer(), capture);
        if (estimate) estimated = estimate;
      }
      SM_ASSERT_TRUE(estimated.has_value());
      // Off by up to a block: the delay is quantized internally.
      SM_EXPECT_NEAR(static_cast<double>(estimated->delay / factor), static_cast<double>(delay_samples / factor),
                     static_cast<double>(kBlockSize / factor));
    }
  }
}

SM_TEST(Aec3Delay, EchoPathDelayEstimatorNoEstimatesForLowLevelRender) {
  Random rng(42);
  DelayConfig config;
  EchoPathDelayEstimator estimator(config);
  RenderDelayBufferLowRate render_delay_buffer(config);
  std::vector<float> render(kBlockSize), capture(kBlockSize);
  for (size_t k = 0; k < 100; ++k) {
    RandomizeSampleVector(rng, render);
    for (auto& r : render) r *= 100.f / 32767.f;
    std::copy(render.begin(), render.end(), capture.begin());
    render_delay_buffer.Insert(render);
    render_delay_buffer.PrepareCaptureProcessing();
    SM_EXPECT_FALSE(estimator.EstimateDelay(render_delay_buffer.GetDownsampledRenderBuffer(), capture).has_value());
  }
}

// render_delay_buffer.cc: an overrun resets the read index; underruns leave it.
SM_TEST(Aec3Delay, RenderBufferLowRateOverrunAndUnderrun) {
  DelayConfig config;
  RenderDelayBufferLowRate buffer(config);
  std::vector<float> block(kBlockSize, 0.f);
  // The ring holds 24 * 5 + 32 + 1 = 153 sub-blocks; one stays between read
  // and write after a reset.
  int overrun_at = -1;
  for (int k = 0; k < 200 && overrun_at < 0; ++k) {
    if (buffer.Insert(block) == RenderDelayBufferLowRate::BufferingEvent::kRenderOverrun) overrun_at = k;
  }
  SM_EXPECT_EQ(overrun_at, 151);
  SM_EXPECT_EQ(buffer.BufferLatency(), 1);
  SM_EXPECT_TRUE(buffer.PrepareCaptureProcessing() == RenderDelayBufferLowRate::BufferingEvent::kNone);
  SM_EXPECT_TRUE(buffer.PrepareCaptureProcessing() == RenderDelayBufferLowRate::BufferingEvent::kRenderUnderrun);
}

// Speech through an echo path of 60 ms, then 180 ms (the echo-path change of
// #63's section 6 signals). The controller reports the delay minus the 32
// sample headroom, in 64-sample pre-echo blocks, and follows the change.
SM_TEST(Aec3Delay, RenderDelayControllerFollowsEchoPathChangeOnSpeech) {
  const int fs = 16000;
  auto far = sim::SpeechLike(fs, 16.0, 7, false);
  auto rir = sim::RoomImpulseResponse(fs, 50, 120, 4);
  auto conv = sim::Convolve(far, rir);
  const size_t d1 = 960, d2 = 2880;
  std::vector<float> near(far.size(), 0.f);
  for (size_t i = 0; i < far.size(); ++i) {
    const size_t d = i < 8 * static_cast<size_t>(fs) ? d1 : d2;
    near[i] = i >= d ? 0.5f * conv[i - d] : 0.f;
  }
  DelayConfig config;
  RenderDelayBufferLowRate buffer(config);
  RenderDelayController controller(config);
  std::vector<float> r(kBlockSize), c(kBlockSize);
  std::optional<DelayEstimate> at_8s, at_16s;
  int first_estimate_block = -1;
  for (size_t b = 0; (b + 1) * kBlockSize <= far.size(); ++b) {
    for (size_t i = 0; i < kBlockSize; ++i) {
      r[i] = 32767.f * far[b * kBlockSize + i];
      c[i] = 32767.f * near[b * kBlockSize + i];
    }
    buffer.Insert(r);
    buffer.PrepareCaptureProcessing();
    auto delay = controller.GetDelay(buffer.GetDownsampledRenderBuffer(), c);
    if (delay && first_estimate_block < 0) first_estimate_block = static_cast<int>(b);
    if (b == 8 * kNumBlocksPerSecond - 1) at_8s = controller.delay_samples();
  }
  at_16s = controller.delay_samples();
  SM_ASSERT_TRUE(at_8s.has_value());
  SM_ASSERT_TRUE(at_16s.has_value());
  // The room response's direct path adds 8 samples. Pre-echo detection
  // reports the start of the echo, which the decimator's filters smear by up
  // to one more 64-sample block (832 for 968 - 32 here).
  SM_EXPECT_GE(at_8s->delay + 32 + 128, d1 + 8);
  SM_EXPECT_LE(at_8s->delay + 32, d1 + 8);
  SM_EXPECT_GE(at_16s->delay + 32 + 128, d2 + 8);
  SM_EXPECT_LE(at_16s->delay + 32, d2 + 8);
  SM_EXPECT_TRUE(at_16s->quality == DelayEstimate::Quality::kRefined);
  SM_EXPECT_FALSE(controller.HasClockdrift());
  SM_RECORD("first_estimate_ms", first_estimate_block * 4);
  SM_RECORD("delay_8s_samples", static_cast<double>(at_8s->delay));
  SM_RECORD("delay_16s_samples", static_cast<double>(at_16s->delay));
}
