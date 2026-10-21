#include "sm/audio/clipping_predictor.h"

#include <algorithm>
#include <cmath>

#include "sm/audio/agc2.h"
#include "sm/audio/agc_manager.h"

namespace sm::audio::agc1 {

// clipping_predictor_level_buffer.cc.
bool ClippingPredictorLevelBuffer::Level::operator==(const Level& level) const {
  constexpr float kEpsilon = 1e-6f;
  return std::fabs(average - level.average) < kEpsilon && std::fabs(max - level.max) < kEpsilon;
}

ClippingPredictorLevelBuffer::ClippingPredictorLevelBuffer(int capacity)
    : data_(static_cast<size_t>(std::max(1, capacity))) {}

void ClippingPredictorLevelBuffer::Reset() {
  tail_ = -1;
  size_ = 0;
}

void ClippingPredictorLevelBuffer::Push(Level level) {
  ++tail_;
  if (tail_ == Capacity()) tail_ = 0;
  if (size_ < Capacity()) ++size_;
  data_[static_cast<size_t>(tail_)] = level;
}

std::optional<ClippingPredictorLevelBuffer::Level> ClippingPredictorLevelBuffer::ComputePartialMetrics(
    int delay, int num_items) const {
  if (delay < 0 || num_items <= 0 || delay + num_items > Capacity()) return std::nullopt;
  if (delay + num_items > Size()) return std::nullopt;
  float sum = 0.0f;
  float max = 0.0f;
  for (int i = 0; i < num_items && i < Size(); ++i) {
    int idx = tail_ - delay - i;
    if (idx < 0) idx += Capacity();
    sum += data_[static_cast<size_t>(idx)].average;
    max = std::fmax(data_[static_cast<size_t>(idx)].max, max);
  }
  return Level{sum / static_cast<float>(num_items), max};
}

namespace {

// clipping_predictor.cc.
constexpr int kClippingPredictorMaxGainChange = 15;

// An input volume in [min_input_volume, max_input_volume] that reduces
// `gain_error_db` (measured at `input_volume`) by the gain map.
int ComputeVolumeUpdate(int gain_error_db, int input_volume, int min_input_volume, int max_input_volume) {
  if (gain_error_db == 0) return input_volume;
  int new_volume = input_volume;
  if (gain_error_db > 0) {
    while (kGainMap[new_volume] - kGainMap[input_volume] < gain_error_db && new_volume < max_input_volume) {
      ++new_volume;
    }
  } else {
    while (kGainMap[new_volume] - kGainMap[input_volume] > gain_error_db && new_volume > min_input_volume) {
      --new_volume;
    }
  }
  return new_volume;
}

float ComputeCrestFactor(const ClippingPredictorLevelBuffer::Level& level) {
  return agc2::FloatS16ToDbfs(level.max) - agc2::FloatS16ToDbfs(std::sqrt(level.average));
}

bool ValidStepArgs(int level, int default_step, int min_mic_level, int max_mic_level) {
  return level >= 0 && level <= 255 && default_step > 0 && default_step <= 255 && min_mic_level >= 0 &&
         min_mic_level <= 255 && max_mic_level >= 0 && max_mic_level <= 255;
}

// The per-frame mean square and peak of each channel, shared by both
// predictors.
class LevelPredictor : public ClippingPredictor {
 public:
  LevelPredictor(int num_channels, int window_length, int reference_window_length, int reference_window_delay)
      : window_length_(window_length),
        reference_window_length_(reference_window_length),
        reference_window_delay_(reference_window_delay) {
    for (int i = 0; i < num_channels; ++i) {
      ch_buffers_.emplace_back(reference_window_delay_ + reference_window_length_);
    }
  }

  void Reset() override {
    for (auto& b : ch_buffers_) b.Reset();
  }

  void Analyze(std::span<const std::span<const float>> channels) override {
    const size_t n = std::min(channels.size(), ch_buffers_.size());
    for (size_t ch = 0; ch < n; ++ch) {
      if (channels[ch].empty()) continue;
      float sum_squares = 0.0f;
      float peak = 0.0f;
      for (float sample : channels[ch]) {
        sum_squares += sample * sample;
        peak = std::max(std::fabs(sample), peak);
      }
      ch_buffers_[ch].Push({sum_squares / static_cast<float>(channels[ch].size()), peak});
    }
  }

 protected:
  bool HasChannel(int channel) const { return channel >= 0 && static_cast<size_t>(channel) < ch_buffers_.size(); }

  std::vector<ClippingPredictorLevelBuffer> ch_buffers_;
  const int window_length_;
  const int reference_window_length_;
  const int reference_window_delay_;
};

// Crest factor-based clipping event prediction, fixed step.
class ClippingEventPredictor : public LevelPredictor {
 public:
  ClippingEventPredictor(int num_channels, int window_length, int reference_window_length, int reference_window_delay,
                         float clipping_threshold, float crest_factor_margin)
      : LevelPredictor(num_channels, window_length, reference_window_length, reference_window_delay),
        clipping_threshold_(clipping_threshold),
        crest_factor_margin_(crest_factor_margin) {}

  std::optional<int> EstimateClippedLevelStep(int channel, int level, int default_step, int min_mic_level,
                                              int max_mic_level) const override {
    if (!HasChannel(channel) || !ValidStepArgs(level, default_step, min_mic_level, max_mic_level)) return std::nullopt;
    if (level <= min_mic_level) return std::nullopt;
    if (PredictClippingEvent(channel)) {
      const int new_level = std::clamp(level - default_step, min_mic_level, max_mic_level);
      const int step = level - new_level;
      if (step > 0) return step;
    }
    return std::nullopt;
  }

 private:
  bool PredictClippingEvent(int channel) const {
    const auto& buffer = ch_buffers_[static_cast<size_t>(channel)];
    const auto metrics = buffer.ComputePartialMetrics(0, window_length_);
    if (!metrics.has_value() || !(agc2::FloatS16ToDbfs(metrics->max) > clipping_threshold_)) return false;
    const auto reference_metrics = buffer.ComputePartialMetrics(reference_window_delay_, reference_window_length_);
    if (!reference_metrics.has_value()) return false;
    const float crest_factor = ComputeCrestFactor(*metrics);
    const float reference_crest_factor = ComputeCrestFactor(*reference_metrics);
    return crest_factor < reference_crest_factor - crest_factor_margin_;
  }

  const float clipping_threshold_;
  const float crest_factor_margin_;
};

// Crest factor-based clipping peak prediction, fixed or adaptive step.
class ClippingPeakPredictor : public LevelPredictor {
 public:
  // `clipping_threshold` is an int, as in libwebrtc (the config's float is
  // truncated).
  ClippingPeakPredictor(int num_channels, int window_length, int reference_window_length, int reference_window_delay,
                        int clipping_threshold, bool adaptive_step_estimation)
      : LevelPredictor(num_channels, window_length, reference_window_length, reference_window_delay),
        clipping_threshold_(clipping_threshold),
        adaptive_step_estimation_(adaptive_step_estimation) {}

  std::optional<int> EstimateClippedLevelStep(int channel, int level, int default_step, int min_mic_level,
                                              int max_mic_level) const override {
    if (!HasChannel(channel) || !ValidStepArgs(level, default_step, min_mic_level, max_mic_level)) return std::nullopt;
    if (level <= min_mic_level) return std::nullopt;
    const std::optional<float> estimate_db = EstimatePeakValue(channel);
    if (estimate_db.has_value() && *estimate_db > static_cast<float>(clipping_threshold_)) {
      int step = 0;
      if (!adaptive_step_estimation_) {
        step = default_step;
      } else {
        const int estimated_gain_change =
            std::clamp(-static_cast<int>(std::ceil(*estimate_db)), -kClippingPredictorMaxGainChange, 0);
        step = std::max(level - ComputeVolumeUpdate(estimated_gain_change, level, min_mic_level, max_mic_level),
                        default_step);
      }
      const int new_level = std::clamp(level - step, min_mic_level, max_mic_level);
      if (level > new_level) return level - new_level;
    }
    return std::nullopt;
  }

 private:
  // The projected peak (dBFS) if the window's peak is above the threshold.
  std::optional<float> EstimatePeakValue(int channel) const {
    const auto& buffer = ch_buffers_[static_cast<size_t>(channel)];
    const auto reference_metrics = buffer.ComputePartialMetrics(reference_window_delay_, reference_window_length_);
    if (!reference_metrics.has_value()) return std::nullopt;
    const auto metrics = buffer.ComputePartialMetrics(0, window_length_);
    if (!metrics.has_value() || !(agc2::FloatS16ToDbfs(metrics->max) > static_cast<float>(clipping_threshold_))) {
      return std::nullopt;
    }
    const float reference_crest_factor = ComputeCrestFactor(*reference_metrics);
    return reference_crest_factor + agc2::FloatS16ToDbfs(std::sqrt(metrics->average));
  }

  const int clipping_threshold_;
  const bool adaptive_step_estimation_;
};

}  // namespace

std::unique_ptr<ClippingPredictor> CreateClippingPredictor(int num_channels, const ClippingPredictorConfig& config) {
  if (!config.enabled) return nullptr;
  // libwebrtc DCHECKs these; an invalid config gets no predictor here.
  if (num_channels <= 0 || config.window_length <= 0 || config.reference_window_length <= 0 ||
      config.reference_window_delay < 0 ||
      config.reference_window_length + config.reference_window_delay <= config.window_length) {
    return nullptr;
  }
  using Mode = ClippingPredictorConfig::Mode;
  switch (config.mode) {
    case Mode::kClippingEventPrediction:
      return std::make_unique<ClippingEventPredictor>(num_channels, config.window_length,
                                                      config.reference_window_length, config.reference_window_delay,
                                                      config.clipping_threshold, config.crest_factor_margin);
    case Mode::kAdaptiveStepClippingPeakPrediction:
      return std::make_unique<ClippingPeakPredictor>(
          num_channels, config.window_length, config.reference_window_length, config.reference_window_delay,
          static_cast<int>(config.clipping_threshold), /*adaptive_step_estimation=*/true);
    case Mode::kFixedStepClippingPeakPrediction:
      return std::make_unique<ClippingPeakPredictor>(
          num_channels, config.window_length, config.reference_window_length, config.reference_window_delay,
          static_cast<int>(config.clipping_threshold), /*adaptive_step_estimation=*/false);
  }
  return nullptr;
}

}  // namespace sm::audio::agc1
