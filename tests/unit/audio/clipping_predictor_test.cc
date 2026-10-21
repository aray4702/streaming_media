// Unit tests: AGC1's clipping predictor (`apm v2`, #242), ported from
// libwebrtc M157's agc2/clipping_predictor_unittest.cc and
// agc2/clipping_predictor_level_buffer_unittest.cc. The parametrized suites
// loop over the same parameter values.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include "sm/audio/clipping_predictor.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::audio::agc1;

namespace {

using Level = ClippingPredictorLevelBuffer::Level;
using Mode = ClippingPredictorConfig::Mode;

constexpr int kSampleRateHz = 32000;
constexpr int kNumChannels = 1;
constexpr int kSamplesPerChannel = kSampleRateHz / 100;
constexpr int kMaxMicLevel = 255;
constexpr int kMinMicLevel = 12;
constexpr int kDefaultClippedLevelStep = 15;
constexpr float kMaxSampleS16 = static_cast<float>(std::numeric_limits<int16_t>::max());
constexpr float kClippingThresholdDb = -0.08729610804900176f;

bool Is(const std::optional<Level>& l, Level expected) { return l.has_value() && *l == expected; }

void Analyze(int num_calls, int num_channels, const std::vector<float>& samples, ClippingPredictor& predictor) {
  std::vector<std::span<const float>> frame(static_cast<size_t>(num_channels), std::span<const float>(samples));
  for (int i = 0; i < num_calls; ++i) predictor.Analyze(frame);
}

// A ramp 0.1..1.0 of `peak_ratio` full scale, repeated.
void AnalyzeNonZeroCrestFactorAudio(int num_calls, int num_channels, float peak_ratio, ClippingPredictor& predictor) {
  std::vector<float> audio(kSamplesPerChannel, 0.0f);
  for (int sample = 0; sample < kSamplesPerChannel; sample += 10) {
    for (int k = 0; k < 10; ++k) {
      audio[static_cast<size_t>(sample + k)] = 0.1f * static_cast<float>(k + 1) * peak_ratio * kMaxSampleS16;
    }
  }
  Analyze(num_calls, num_channels, audio, predictor);
}

// Every sample at `peak_ratio` full scale.
void AnalyzeZeroCrestFactorAudio(int num_calls, int num_channels, float peak_ratio, ClippingPredictor& predictor) {
  const std::vector<float> audio(kSamplesPerChannel, peak_ratio * kMaxSampleS16);
  Analyze(num_calls, num_channels, audio, predictor);
}

bool EstimatesWithValue(int num_channels, int level, const ClippingPredictor& predictor, int expected) {
  for (int i = 0; i < num_channels; ++i) {
    const auto step =
        predictor.EstimateClippedLevelStep(i, level, kDefaultClippedLevelStep, kMinMicLevel, kMaxMicLevel);
    if (!step.has_value() || *step != expected) return false;
  }
  return true;
}

bool EstimatesWithoutValue(int num_channels, int level, const ClippingPredictor& predictor) {
  for (int i = 0; i < num_channels; ++i) {
    if (predictor.EstimateClippedLevelStep(i, level, kDefaultClippedLevelStep, kMinMicLevel, kMaxMicLevel)) {
      return false;
    }
  }
  return true;
}

ClippingPredictorConfig Config(Mode mode, int window, int ref_window, int ref_delay, float threshold, float margin) {
  ClippingPredictorConfig c;
  c.enabled = true;
  c.mode = mode;
  c.window_length = window;
  c.reference_window_length = ref_window;
  c.reference_window_delay = ref_delay;
  c.clipping_threshold = threshold;
  c.crest_factor_margin = margin;
  return c;
}

// ClippingPredictorParameterization: Combine(Values(1, 5), Values(1, 5, 10),
// Values(1, 5), Values(0, 1, 5)), with threshold -1 and margin 0.5.
template <typename F>
void ForEachParam(Mode mode, F f) {
  for (int channels : {1, 5}) {
    for (int window : {1, 5, 10}) {
      for (int ref_window : {1, 5}) {
        for (int ref_delay : {0, 1, 5}) {
          const auto c = Config(mode, window, ref_window, ref_delay, -1.0f, 0.5f);
          if (c.reference_window_length + c.reference_window_delay <= c.window_length) continue;
          f(channels, c);
        }
      }
    }
  }
}

}  // namespace

SM_TEST(Agc1ClippingLevelBuffer, Sizes) {
  for (int capacity : {-1, 0, 1, 123}) {
    const int expected = std::max(capacity, 1);
    {
      ClippingPredictorLevelBuffer b(capacity);
      SM_EXPECT_EQ(b.Capacity(), expected);
      SM_EXPECT_EQ(b.Size(), 0);
    }
    {
      ClippingPredictorLevelBuffer b(capacity);
      for (int i = 0; i < b.Capacity() / 2; ++i) b.Push({2, 4});
      SM_EXPECT_EQ(b.Size(), expected / 2);
    }
    {
      ClippingPredictorLevelBuffer b(capacity);
      for (int i = 0; i < b.Capacity(); ++i) b.Push({2, 4});
      SM_EXPECT_EQ(b.Size(), expected);
    }
    {
      ClippingPredictorLevelBuffer b(capacity);
      for (int i = 0; i < 2 * b.Capacity(); ++i) b.Push({2, 4});
      SM_EXPECT_EQ(b.Capacity(), expected);
      SM_EXPECT_EQ(b.Size(), expected);
    }
    {
      ClippingPredictorLevelBuffer b(capacity);
      b.Push({1, 1});
      b.Push({1, 1});
      b.Reset();
      SM_EXPECT_EQ(b.Size(), 0);
      b.Push({1, 1});
      SM_EXPECT_EQ(b.Capacity(), expected);
      SM_EXPECT_EQ(b.Size(), 1);
    }
  }
}

SM_TEST(Agc1ClippingLevelBuffer, MetricsAfterFullBuffer) {
  ClippingPredictorLevelBuffer b(2);
  b.Push({1, 2});
  b.Push({3, 6});
  SM_EXPECT_TRUE(Is(b.ComputePartialMetrics(0, 1), {3, 6}));
  SM_EXPECT_TRUE(Is(b.ComputePartialMetrics(1, 1), {1, 2}));
  SM_EXPECT_TRUE(Is(b.ComputePartialMetrics(0, 2), {2, 6}));
}

SM_TEST(Agc1ClippingLevelBuffer, MetricsAfterPushBeyondCapacity) {
  ClippingPredictorLevelBuffer b(2);
  b.Push({1, 1});
  b.Push({3, 6});
  b.Push({5, 10});
  b.Push({7, 14});
  b.Push({6, 12});
  SM_EXPECT_TRUE(Is(b.ComputePartialMetrics(0, 1), {6, 12}));
  SM_EXPECT_TRUE(Is(b.ComputePartialMetrics(1, 1), {7, 14}));
  SM_EXPECT_TRUE(Is(b.ComputePartialMetrics(0, 2), {6.5f, 14}));
}

SM_TEST(Agc1ClippingLevelBuffer, MetricsAfterTooFewItems) {
  ClippingPredictorLevelBuffer b(4);
  b.Push({1, 2});
  b.Push({3, 6});
  SM_EXPECT_FALSE(b.ComputePartialMetrics(0, 3).has_value());
  SM_EXPECT_FALSE(b.ComputePartialMetrics(2, 1).has_value());
}

SM_TEST(Agc1ClippingLevelBuffer, MetricsAfterReset) {
  ClippingPredictorLevelBuffer b(2);
  b.Push({1, 2});
  b.Reset();
  b.Push({5, 10});
  b.Push({7, 14});
  SM_EXPECT_TRUE(Is(b.ComputePartialMetrics(0, 1), {7, 14}));
  SM_EXPECT_TRUE(Is(b.ComputePartialMetrics(0, 2), {6, 14}));
  SM_EXPECT_TRUE(Is(b.ComputePartialMetrics(1, 1), {5, 10}));
}

SM_TEST(Agc1ClippingPredictor, Creation) {
  ClippingPredictorConfig off;
  SM_EXPECT_FALSE(off.enabled);  // ClippingPredictorDisabledByDefault.
  SM_EXPECT_TRUE(CreateClippingPredictor(kNumChannels, off) == nullptr);
  for (Mode m :
       {Mode::kClippingEventPrediction, Mode::kAdaptiveStepClippingPeakPrediction,
        Mode::kFixedStepClippingPeakPrediction}) {
    ClippingPredictorConfig c;
    c.enabled = true;
    c.mode = m;
    SM_EXPECT_TRUE(CreateClippingPredictor(kNumChannels, c) != nullptr);
  }
}

SM_TEST(Agc1ClippingPredictor, EventPredictorEstimateAfterCrestFactorDrop) {
  ForEachParam(Mode::kClippingEventPrediction, [](int channels, const ClippingPredictorConfig& c) {
    auto p = CreateClippingPredictor(channels, c);
    SM_ASSERT_TRUE(p != nullptr);
    AnalyzeNonZeroCrestFactorAudio(c.reference_window_length + c.reference_window_delay - c.window_length, channels,
                                   0.99f, *p);
    SM_EXPECT_TRUE(EstimatesWithoutValue(channels, 255, *p));
    AnalyzeZeroCrestFactorAudio(c.window_length, channels, 0.99f, *p);
    SM_EXPECT_TRUE(EstimatesWithValue(channels, 255, *p, kDefaultClippedLevelStep));
  });
}

SM_TEST(Agc1ClippingPredictor, EventPredictorNoEstimateAfterConstantCrestFactor) {
  ForEachParam(Mode::kClippingEventPrediction, [](int channels, const ClippingPredictorConfig& c) {
    auto p = CreateClippingPredictor(channels, c);
    SM_ASSERT_TRUE(p != nullptr);
    AnalyzeNonZeroCrestFactorAudio(c.reference_window_length + c.reference_window_delay - c.window_length, channels,
                                   0.99f, *p);
    SM_EXPECT_TRUE(EstimatesWithoutValue(channels, 255, *p));
    AnalyzeNonZeroCrestFactorAudio(c.window_length, channels, 0.99f, *p);
    SM_EXPECT_TRUE(EstimatesWithoutValue(channels, 255, *p));
  });
}

SM_TEST(Agc1ClippingPredictor, PeakPredictorEstimateAfterHighCrestFactor) {
  ForEachParam(Mode::kAdaptiveStepClippingPeakPrediction, [](int channels, const ClippingPredictorConfig& c) {
    auto p = CreateClippingPredictor(channels, c);
    SM_ASSERT_TRUE(p != nullptr);
    AnalyzeNonZeroCrestFactorAudio(c.reference_window_length + c.reference_window_delay - c.window_length, channels,
                                   0.99f, *p);
    SM_EXPECT_TRUE(EstimatesWithoutValue(channels, 255, *p));
    AnalyzeNonZeroCrestFactorAudio(c.window_length, channels, 0.99f, *p);
    SM_EXPECT_TRUE(EstimatesWithValue(channels, 255, *p, kDefaultClippedLevelStep));
  });
}

SM_TEST(Agc1ClippingPredictor, PeakPredictorNoEstimateAfterLowCrestFactor) {
  ForEachParam(Mode::kAdaptiveStepClippingPeakPrediction, [](int channels, const ClippingPredictorConfig& c) {
    auto p = CreateClippingPredictor(channels, c);
    SM_ASSERT_TRUE(p != nullptr);
    AnalyzeZeroCrestFactorAudio(c.reference_window_length + c.reference_window_delay - c.window_length, channels,
                                0.99f, *p);
    SM_EXPECT_TRUE(EstimatesWithoutValue(channels, 255, *p));
    AnalyzeNonZeroCrestFactorAudio(c.window_length, channels, 0.99f, *p);
    SM_EXPECT_TRUE(EstimatesWithoutValue(channels, 255, *p));
  });
}

// ClippingEventPredictorParameterization: Combine(Values(-1, 0),
// Values(3, 4.16)).
SM_TEST(Agc1ClippingPredictor, EventPredictorThresholdAndMargin) {
  for (float threshold : {-1.0f, 0.0f}) {
    for (float margin : {3.0f, 4.16f}) {
      const auto c = Config(Mode::kClippingEventPrediction, 5, 5, 5, threshold, margin);
      auto p = CreateClippingPredictor(kNumChannels, c);
      SM_ASSERT_TRUE(p != nullptr);
      AnalyzeNonZeroCrestFactorAudio(c.reference_window_length, kNumChannels, 0.99f, *p);
      SM_EXPECT_TRUE(EstimatesWithoutValue(kNumChannels, 255, *p));
      AnalyzeZeroCrestFactorAudio(c.window_length, kNumChannels, 0.99f, *p);
      if (threshold < kClippingThresholdDb && margin < 4.15f) {
        SM_EXPECT_TRUE(EstimatesWithValue(kNumChannels, 255, *p, kDefaultClippedLevelStep));
      } else {
        SM_EXPECT_TRUE(EstimatesWithoutValue(kNumChannels, 255, *p));
      }
    }
  }
}

// ClippingPredictorModeParameterization: both peak prediction modes.
SM_TEST(Agc1ClippingPredictor, PeakModesClippingMargin) {
  for (Mode m : {Mode::kAdaptiveStepClippingPeakPrediction, Mode::kFixedStepClippingPeakPrediction}) {
    {
      // A 0 dBFS threshold: no estimate.
      const auto c = Config(m, 5, 5, 5, 0.0f, 3.0f);
      auto p = CreateClippingPredictor(kNumChannels, c);
      AnalyzeNonZeroCrestFactorAudio(c.reference_window_length, kNumChannels, 0.99f, *p);
      SM_EXPECT_TRUE(EstimatesWithoutValue(kNumChannels, 255, *p));
      AnalyzeZeroCrestFactorAudio(c.window_length, kNumChannels, 0.99f, *p);
      SM_EXPECT_TRUE(EstimatesWithoutValue(kNumChannels, 255, *p));
    }
    {
      const auto c = Config(m, 5, 5, 5, -1.0f, 3.0f);
      auto p = CreateClippingPredictor(kNumChannels, c);
      AnalyzeNonZeroCrestFactorAudio(c.reference_window_length, kNumChannels, 0.99f, *p);
      SM_EXPECT_TRUE(EstimatesWithoutValue(kNumChannels, 255, *p));
      AnalyzeZeroCrestFactorAudio(c.window_length, kNumChannels, 0.99f, *p);
      const int expected = m == Mode::kAdaptiveStepClippingPeakPrediction ? 17 : kDefaultClippedLevelStep;
      SM_EXPECT_TRUE(EstimatesWithValue(kNumChannels, 255, *p, expected));
    }
  }
}

SM_TEST(Agc1ClippingPredictor, NoEstimateAfterReset) {
  for (Mode m : {Mode::kClippingEventPrediction, Mode::kAdaptiveStepClippingPeakPrediction}) {
    const auto c = Config(m, 5, 5, 5, -1.0f, 3.0f);
    auto p = CreateClippingPredictor(kNumChannels, c);
    AnalyzeNonZeroCrestFactorAudio(c.reference_window_length, kNumChannels, 0.99f, *p);
    SM_EXPECT_TRUE(EstimatesWithoutValue(kNumChannels, 255, *p));
    p->Reset();
    AnalyzeZeroCrestFactorAudio(c.window_length, kNumChannels, 0.99f, *p);
    SM_EXPECT_TRUE(EstimatesWithoutValue(kNumChannels, 255, *p));
  }
}

SM_TEST(Agc1ClippingPredictor, AdaptiveAndFixedStepEstimates) {
  for (Mode m : {Mode::kAdaptiveStepClippingPeakPrediction, Mode::kFixedStepClippingPeakPrediction}) {
    // CheckAdaptiveStepEstimate / CheckFixedStepEstimate (margin default).
    ClippingPredictorConfig c = Config(m, 5, 5, 5, -1.0f, ClippingPredictorConfig{}.crest_factor_margin);
    auto p = CreateClippingPredictor(kNumChannels, c);
    AnalyzeNonZeroCrestFactorAudio(c.reference_window_length, kNumChannels, 0.99f, *p);
    SM_EXPECT_TRUE(EstimatesWithoutValue(kNumChannels, 255, *p));
    AnalyzeZeroCrestFactorAudio(c.window_length, kNumChannels, 0.99f, *p);
    SM_EXPECT_TRUE(
        EstimatesWithValue(kNumChannels, 255, *p, m == Mode::kAdaptiveStepClippingPeakPrediction ? 17 : 15));
  }
}

// libwebrtc DCHECKs an invalid window; here it gets no predictor.
SM_TEST(Agc1ClippingPredictor, InvalidWindowGetsNoPredictor) {
  SM_EXPECT_TRUE(CreateClippingPredictor(1, Config(Mode::kClippingEventPrediction, 10, 5, 5, -1.0f, 3.0f)) == nullptr);
  SM_EXPECT_TRUE(CreateClippingPredictor(0, Config(Mode::kClippingEventPrediction, 5, 5, 5, -1.0f, 3.0f)) == nullptr);
  auto p = CreateClippingPredictor(1, Config(Mode::kClippingEventPrediction, 5, 5, 5, -1.0f, 3.0f));
  SM_EXPECT_FALSE(p->EstimateClippedLevelStep(1, 255, 15, 12, 255).has_value());  // No such channel.
}
