// AEC3 echo-path delay estimation, ported from libwebrtc M157's
// modules/audio_processing/aec3/ (see sm/audio/aec3_delay.h for the files).
#include "sm/audio/aec3_delay.h"

#include <algorithm>
#include <iterator>

namespace sm::audio::aec3 {

// --- decimator.cc ------------------------------------------------------------

namespace {
// signal.ellip(6, 1, 40, 1800/8000, 'lowpass', output='sos')
constexpr std::array<CascadedBiQuadFilter::BiQuadCoefficients, 3> kLowPassFilterDs4 = {{
    {{0.0180919877f, 0.00320961363f, 0.0180919877f}, {-1.5183195f, 0.633165865f}},
    {{1.0f, -1.24550459f, 1.0f}, {-1.49784254f, 0.853586692f}},
    {{1.0f, -1.4221681f, 1.0f}, {-1.49791282f, 0.969572384f}},
}};
// signal.cheby1(1, 6, [1000/8000, 2000/8000], 'bandpass', output='sos'), 5 times.
constexpr std::array<CascadedBiQuadFilter::BiQuadCoefficients, 5> kBandPassFilterDs8 = {{
    {{0.103304783f, 0.0f, -0.103304783f}, {-1.520363f, 0.793390435f}},
    {{0.103304783f, 0.0f, -0.103304783f}, {-1.520363f, 0.793390435f}},
    {{0.103304783f, 0.0f, -0.103304783f}, {-1.520363f, 0.793390435f}},
    {{0.103304783f, 0.0f, -0.103304783f}, {-1.520363f, 0.793390435f}},
    {{0.103304783f, 0.0f, -0.103304783f}, {-1.520363f, 0.793390435f}},
}};
// signal.butter(2, 1000/8000.0, 'highpass', output='sos')
constexpr std::array<CascadedBiQuadFilter::BiQuadCoefficients, 1> kHighPassFilter = {{
    {{0.757076375f, -1.51415275f, 0.757076375f}, {-1.45424359f, 0.574061915f}},
}};
using Coefficients = std::span<const CascadedBiQuadFilter::BiQuadCoefficients>;
}  // namespace

Decimator::Decimator(size_t down_sampling_factor)
    : down_sampling_factor_(down_sampling_factor),
      anti_aliasing_filter_(down_sampling_factor == 4 ? Coefficients(kLowPassFilterDs4)
                                                      : Coefficients(kBandPassFilterDs8)),
      noise_reduction_filter_(down_sampling_factor == 8 ? Coefficients() : Coefficients(kHighPassFilter)) {}

void Decimator::Decimate(std::span<const float> in, std::span<float> out) {
  std::array<float, kBlockSize> x;
  // Limit the frequency content of the signal to avoid aliasing.
  anti_aliasing_filter_.Process(in, x);
  // Reduce the impact of near-end noise.
  noise_reduction_filter_.Process(x);
  for (size_t j = 0, k = 0; j < out.size(); ++j, k += down_sampling_factor_) out[j] = x[k];
}

// --- matched_filter.cc -------------------------------------------------------

namespace {
// Subsample rate used for computing the accumulated error.
constexpr int kAccumulatedErrorSubSampleRate = 4;

void UpdateAccumulatedError(std::span<const float> instantaneous_accumulated_error,
                            std::span<float> accumulated_error, float one_over_error_sum_anchor) {
  static constexpr float kSmoothConstantIncreases = 0.015f;
  for (size_t k = 0; k < instantaneous_accumulated_error.size(); ++k) {
    float error_norm = instantaneous_accumulated_error[k] * one_over_error_sum_anchor;
    if (error_norm < accumulated_error[k]) {
      accumulated_error[k] = error_norm;
    } else {
      accumulated_error[k] += kSmoothConstantIncreases * (error_norm - accumulated_error[k]);
    }
  }
}

size_t ComputePreEchoLag(std::span<const float> accumulated_error, size_t lag, size_t alignment_shift_winner) {
  static constexpr float kPreEchoThreshold = 0.5f;
  size_t pre_echo_lag_estimate = lag - alignment_shift_winner;
  size_t maximum_pre_echo_lag =
      std::min(pre_echo_lag_estimate / kAccumulatedErrorSubSampleRate, accumulated_error.size());
  for (int k = static_cast<int>(maximum_pre_echo_lag) - 1; k >= 0; --k) {
    if (accumulated_error[k] > kPreEchoThreshold) break;
    pre_echo_lag_estimate = (k + 1) * kAccumulatedErrorSubSampleRate - 1;
  }
  return pre_echo_lag_estimate + alignment_shift_winner;
}
}  // namespace

void MatchedFilterCore(size_t x_start_index, float x2_sum_threshold, float smoothing, std::span<const float> x,
                       std::span<const float> y, std::span<float> h, bool* filters_updated, float* error_sum,
                       bool compute_accumulated_error, std::span<float> accumulated_error) {
  if (compute_accumulated_error) std::fill(accumulated_error.begin(), accumulated_error.end(), 0.0f);

  // Process for all samples in the sub-block.
  for (size_t i = 0; i < y.size(); ++i) {
    // Apply the matched filter as filter * x, and compute x * x.
    float x2_sum = 0.f;
    float s = 0;
    size_t x_index = x_start_index;
    if (compute_accumulated_error) {
      for (size_t k = 0; k < h.size(); ++k) {
        x2_sum += x[x_index] * x[x_index];
        s += h[k] * x[x_index];
        x_index = x_index < (x.size() - 1) ? x_index + 1 : 0;
        if (((k + 1) & 0b11) == 0) {
          size_t idx = k >> 2;
          accumulated_error[idx] += (y[i] - s) * (y[i] - s);
        }
      }
    } else {
      for (size_t k = 0; k < h.size(); ++k) {
        x2_sum += x[x_index] * x[x_index];
        s += h[k] * x[x_index];
        x_index = x_index < (x.size() - 1) ? x_index + 1 : 0;
      }
    }

    // Compute the matched filter error.
    float e = y[i] - s;
    const bool saturation = y[i] >= 32000.f || y[i] <= -32000.f;
    (*error_sum) += e * e;

    // Update the matched filter estimate in an NLMS manner.
    if (x2_sum > x2_sum_threshold && !saturation) {
      const float alpha = smoothing * e / x2_sum;
      // filter = filter + smoothing * (y - filter * x) * x / x * x.
      size_t x_index2 = x_start_index;
      for (size_t k = 0; k < h.size(); ++k) {
        h[k] += alpha * x[x_index2];
        x_index2 = x_index2 < (x.size() - 1) ? x_index2 + 1 : 0;
      }
      *filters_updated = true;
    }

    x_start_index = x_start_index > 0 ? x_start_index - 1 : x.size() - 1;
  }
}

size_t MaxSquarePeakIndex(std::span<const float> h) {
  if (h.size() < 2) return 0;
  float max_element1 = h[0] * h[0];
  float max_element2 = h[1] * h[1];
  size_t lag_estimate1 = 0;
  size_t lag_estimate2 = 1;
  const size_t last_index = h.size() - 1;
  // Even and odd maxima are tracked separately, as in libwebrtc.
  for (size_t k = 2; k < last_index; k += 2) {
    float element1 = h[k] * h[k];
    float element2 = h[k + 1] * h[k + 1];
    if (element1 > max_element1) {
      max_element1 = element1;
      lag_estimate1 = k;
    }
    if (element2 > max_element2) {
      max_element2 = element2;
      lag_estimate2 = k + 1;
    }
  }
  if (max_element2 > max_element1) {
    max_element1 = max_element2;
    lag_estimate1 = lag_estimate2;
  }
  // In case of odd h size, the last element has not been checked yet.
  float last_element = h[last_index] * h[last_index];
  if (last_element > max_element1) return last_index;
  return lag_estimate1;
}

MatchedFilter::MatchedFilter(size_t sub_block_size, size_t window_size_sub_blocks, int num_matched_filters,
                             size_t alignment_shift_sub_blocks, float excitation_limit, float smoothing_fast,
                             float smoothing_slow, float matching_filter_threshold, bool detect_pre_echo)
    : sub_block_size_(sub_block_size),
      filter_intra_lag_shift_(alignment_shift_sub_blocks * sub_block_size),
      filters_(num_matched_filters, std::vector<float>(window_size_sub_blocks * sub_block_size, 0.f)),
      excitation_limit_(excitation_limit),
      smoothing_fast_(smoothing_fast),
      smoothing_slow_(smoothing_slow),
      matching_filter_threshold_(matching_filter_threshold),
      detect_pre_echo_(detect_pre_echo) {
  if (detect_pre_echo_) {
    const size_t n = window_size_sub_blocks * sub_block_size / kAccumulatedErrorSubSampleRate;
    accumulated_error_.assign(num_matched_filters, std::vector<float>(n, 1.0f));
    instantaneous_accumulated_error_.assign(n, 0.0f);
  }
}

void MatchedFilter::Reset(bool full_reset) {
  for (auto& f : filters_) std::fill(f.begin(), f.end(), 0.f);
  winner_lag_ = std::nullopt;
  reported_lag_estimate_ = std::nullopt;
  if (full_reset) {
    for (auto& e : accumulated_error_) std::fill(e.begin(), e.end(), 1.0f);
    number_pre_echo_updates_ = 0;
  }
}

void MatchedFilter::Update(const DownsampledRenderBuffer& render_buffer, std::span<const float> capture,
                           bool use_slow_smoothing) {
  const auto& y = capture;
  const float smoothing = use_slow_smoothing ? smoothing_slow_ : smoothing_fast_;
  const float x2_sum_threshold = filters_[0].size() * excitation_limit_ * excitation_limit_;

  // Compute anchor for the matched filter error.
  float error_sum_anchor = 0.0f;
  for (size_t k = 0; k < y.size(); ++k) error_sum_anchor += y[k] * y[k];

  // Apply all matched filters.
  float winner_error_sum = error_sum_anchor;
  winner_lag_ = std::nullopt;
  reported_lag_estimate_ = std::nullopt;
  size_t alignment_shift = 0;
  std::optional<size_t> previous_lag_estimate;
  const int num_filters = static_cast<int>(filters_.size());
  int winner_index = -1;
  for (int n = 0; n < num_filters; ++n) {
    float error_sum = 0.f;
    bool filters_updated = false;
    const bool compute_pre_echo = detect_pre_echo_ && n == last_detected_best_lag_filter_;

    size_t x_start_index = (render_buffer.read + alignment_shift + sub_block_size_ - 1) % render_buffer.buffer.size();
    MatchedFilterCore(x_start_index, x2_sum_threshold, smoothing, render_buffer.buffer, y, filters_[n],
                      &filters_updated, &error_sum, compute_pre_echo, instantaneous_accumulated_error_);

    // The lag is the distance to the part of the filter that contributes the
    // most to its output: the peak of the filter.
    const size_t lag_estimate = MaxSquarePeakIndex(filters_[n]);
    const bool reliable = lag_estimate > 2 && lag_estimate < (filters_[n].size() - 10) &&
                          error_sum < matching_filter_threshold_ * error_sum_anchor;

    // Find the best estimate.
    const size_t lag = lag_estimate + alignment_shift;
    if (filters_updated && reliable && error_sum < winner_error_sum) {
      winner_error_sum = error_sum;
      winner_index = n;
      // If 2 matched filters return the same winner candidate (overlap
      // region), the one with the smaller index is chosen in order to search
      // for pre-echoes.
      if (previous_lag_estimate && previous_lag_estimate == lag) {
        winner_lag_ = previous_lag_estimate;
        winner_index = n - 1;
      } else {
        winner_lag_ = lag;
      }
    }
    previous_lag_estimate = lag;
    alignment_shift += filter_intra_lag_shift_;
  }

  if (winner_index != -1) {
    reported_lag_estimate_ = LagEstimate(*winner_lag_, /*pre_echo_lag=*/*winner_lag_);
    if (detect_pre_echo_ && last_detected_best_lag_filter_ == winner_index) {
      static constexpr float kEnergyThreshold = 1.0f;
      if (error_sum_anchor > kEnergyThreshold) {
        UpdateAccumulatedError(instantaneous_accumulated_error_, accumulated_error_[winner_index],
                               1.0f / error_sum_anchor);
        number_pre_echo_updates_++;
      }
      if (number_pre_echo_updates_ >= 50) {
        reported_lag_estimate_->pre_echo_lag = ComputePreEchoLag(
            accumulated_error_[winner_index], *winner_lag_, winner_index * filter_intra_lag_shift_);
      } else {
        reported_lag_estimate_->pre_echo_lag = *winner_lag_;
      }
    }
    last_detected_best_lag_filter_ = winner_index;
  }
}

// --- matched_filter_lag_aggregator.cc ----------------------------------------

namespace {
constexpr int kPreEchoHistogramDataNotUpdated = -1;

int GetDownSamplingBlockSizeLog2(int down_sampling_factor) {
  int down_sampling_factor_log2 = 0;
  down_sampling_factor >>= 1;
  while (down_sampling_factor > 0) {
    down_sampling_factor_log2++;
    down_sampling_factor >>= 1;
  }
  return static_cast<int>(kBlockSizeLog2) > down_sampling_factor_log2
             ? static_cast<int>(kBlockSizeLog2) - down_sampling_factor_log2
             : 0;
}
}  // namespace

MatchedFilterLagAggregator::MatchedFilterLagAggregator(size_t max_filter_lag, const DelayConfig& config)
    : threshold_initial_(config.delay_selection_threshold_initial),
      threshold_converged_(config.delay_selection_threshold_converged),
      headroom_(static_cast<int>(config.delay_headroom_samples / config.down_sampling_factor)),
      highest_peak_aggregator_(max_filter_lag) {
  if (config.detect_pre_echo) {
    pre_echo_lag_aggregator_ = std::make_unique<PreEchoLagAggregator>(max_filter_lag, config.down_sampling_factor);
  }
}

void MatchedFilterLagAggregator::Reset(bool hard_reset) {
  highest_peak_aggregator_.Reset();
  if (pre_echo_lag_aggregator_) pre_echo_lag_aggregator_->Reset();
  if (hard_reset) significant_candidate_found_ = false;
}

std::optional<DelayEstimate> MatchedFilterLagAggregator::Aggregate(
    const std::optional<MatchedFilter::LagEstimate>& lag_estimate) {
  if (lag_estimate && pre_echo_lag_aggregator_) {
    pre_echo_lag_aggregator_->Aggregate(std::max(0, static_cast<int>(lag_estimate->pre_echo_lag) - headroom_));
  }

  if (lag_estimate) {
    highest_peak_aggregator_.Aggregate(std::max(0, static_cast<int>(lag_estimate->lag) - headroom_));
    std::span<const int> histogram = highest_peak_aggregator_.histogram();
    int candidate = highest_peak_aggregator_.candidate();
    significant_candidate_found_ = significant_candidate_found_ || histogram[candidate] > threshold_converged_;
    if (histogram[candidate] > threshold_converged_ ||
        (histogram[candidate] > threshold_initial_ && !significant_candidate_found_)) {
      DelayEstimate::Quality quality =
          significant_candidate_found_ ? DelayEstimate::Quality::kRefined : DelayEstimate::Quality::kCoarse;
      int reported_delay = pre_echo_lag_aggregator_ ? pre_echo_lag_aggregator_->pre_echo_candidate() : candidate;
      return DelayEstimate(quality, static_cast<size_t>(reported_delay));
    }
  }
  return std::nullopt;
}

MatchedFilterLagAggregator::HighestPeakAggregator::HighestPeakAggregator(size_t max_filter_lag)
    : histogram_(max_filter_lag + 1, 0) {
  histogram_data_.fill(0);
}

void MatchedFilterLagAggregator::HighestPeakAggregator::Reset() {
  std::fill(histogram_.begin(), histogram_.end(), 0);
  histogram_data_.fill(0);
  histogram_data_index_ = 0;
}

void MatchedFilterLagAggregator::HighestPeakAggregator::Aggregate(int lag) {
  --histogram_[histogram_data_[histogram_data_index_]];
  histogram_data_[histogram_data_index_] = lag;
  ++histogram_[histogram_data_[histogram_data_index_]];
  histogram_data_index_ = (histogram_data_index_ + 1) % static_cast<int>(histogram_data_.size());
  candidate_ = static_cast<int>(
      std::distance(histogram_.begin(), std::max_element(histogram_.begin(), histogram_.end())));
}

MatchedFilterLagAggregator::PreEchoLagAggregator::PreEchoLagAggregator(size_t max_filter_lag,
                                                                       size_t down_sampling_factor)
    : block_size_log2_(GetDownSamplingBlockSizeLog2(static_cast<int>(down_sampling_factor))),
      histogram_(((max_filter_lag + 1) * down_sampling_factor) >> kBlockSizeLog2, 0) {
  Reset();
}

void MatchedFilterLagAggregator::PreEchoLagAggregator::Reset() {
  std::fill(histogram_.begin(), histogram_.end(), 0);
  histogram_data_.fill(kPreEchoHistogramDataNotUpdated);
  histogram_data_index_ = 0;
  pre_echo_candidate_ = 0;
  // number_updates_ is kept, as in libwebrtc.
}

void MatchedFilterLagAggregator::PreEchoLagAggregator::Aggregate(int pre_echo_lag) {
  int pre_echo_block_size = pre_echo_lag >> block_size_log2_;
  pre_echo_block_size = std::clamp(pre_echo_block_size, 0, static_cast<int>(histogram_.size()) - 1);
  // Remove the oldest point from the histogram, ignoring the initial points
  // where nothing was stored yet.
  if (histogram_data_[histogram_data_index_] != kPreEchoHistogramDataNotUpdated) {
    --histogram_[histogram_data_[histogram_data_index_]];
  }
  histogram_data_[histogram_data_index_] = pre_echo_block_size;
  ++histogram_[histogram_data_[histogram_data_index_]];
  histogram_data_index_ = (histogram_data_index_ + 1) % static_cast<int>(histogram_data_.size());
  int pre_echo_candidate_block_size = 0;
  if (number_updates_ < kNumBlocksPerSecond * 2) {
    number_updates_++;
    float penalization_per_delay = 1.0f;
    float max_histogram_value = -1.0f;
    for (auto it = histogram_.begin();
         std::distance(it, histogram_.end()) >= static_cast<int>(kMatchedFilterWindowSizeSubBlocks);
         it = it + kMatchedFilterWindowSizeSubBlocks) {
      auto it_max_element = std::max_element(it, it + kMatchedFilterWindowSizeSubBlocks);
      float weighted_max_value = static_cast<float>(*it_max_element) * penalization_per_delay;
      if (weighted_max_value > max_histogram_value) {
        max_histogram_value = weighted_max_value;
        pre_echo_candidate_block_size = static_cast<int>(std::distance(histogram_.begin(), it_max_element));
      }
      penalization_per_delay *= 0.7f;
    }
  } else {
    pre_echo_candidate_block_size =
        static_cast<int>(std::distance(histogram_.begin(), std::max_element(histogram_.begin(), histogram_.end())));
  }
  pre_echo_candidate_ = (pre_echo_candidate_block_size << block_size_log2_);
}

// --- clockdrift_detector.cc --------------------------------------------------

void ClockdriftDetector::Update(int delay_estimate) {
  if (delay_estimate == delay_history_[0]) {
    // Reset the level if the delay estimate is stable for 7500 blocks (30 s).
    if (++stability_counter_ > 7500) level_ = Level::kNone;
    return;
  }

  stability_counter_ = 0;
  const int d1 = delay_history_[0] - delay_estimate;
  const int d2 = delay_history_[1] - delay_estimate;
  const int d3 = delay_history_[2] - delay_estimate;

  // Positive drift: [x-3], x-2, x-1, x or [x-3], x-1, x-2, x.
  const bool probable_drift_up = (d1 == -1 && d2 == -2) || (d1 == -2 && d2 == -1);
  const bool drift_up = probable_drift_up && d3 == -3;
  // Negative drift: [x+3], x+2, x+1, x or [x+3], x+1, x+2, x.
  const bool probable_drift_down = (d1 == 1 && d2 == 2) || (d1 == 2 && d2 == 1);
  const bool drift_down = probable_drift_down && d3 == 3;

  if (drift_up || drift_down) {
    level_ = Level::kVerified;
  } else if ((probable_drift_up || probable_drift_down) && level_ == Level::kNone) {
    level_ = Level::kProbable;
  }

  delay_history_[2] = delay_history_[1];
  delay_history_[1] = delay_history_[0];
  delay_history_[0] = delay_estimate;
}

// --- echo_path_delay_estimator.cc --------------------------------------------

EchoPathDelayEstimator::EchoPathDelayEstimator(const DelayConfig& config)
    : down_sampling_factor_(config.down_sampling_factor),
      sub_block_size_(down_sampling_factor_ != 0 ? kBlockSize / down_sampling_factor_ : kBlockSize),
      capture_decimator_(down_sampling_factor_),
      matched_filter_(sub_block_size_, kMatchedFilterWindowSizeSubBlocks, static_cast<int>(config.num_filters),
                      kMatchedFilterAlignmentShiftSizeSubBlocks,
                      config.down_sampling_factor == 8 ? config.poor_excitation_render_limit_ds8
                                                       : config.poor_excitation_render_limit,
                      config.delay_estimate_smoothing, config.delay_estimate_smoothing_delay_found,
                      config.delay_candidate_detection_threshold, config.detect_pre_echo),
      matched_filter_lag_aggregator_(matched_filter_.GetMaxFilterLag(), config) {}

std::optional<DelayEstimate> EchoPathDelayEstimator::EstimateDelay(const DownsampledRenderBuffer& render_buffer,
                                                                   std::span<const float> capture) {
  std::array<float, kBlockSize> downsampled_capture_data;
  std::span<float> downsampled_capture(downsampled_capture_data.data(), sub_block_size_);
  // AlignmentMixer with one capture channel: channel 0 as is.
  capture_decimator_.Decimate(capture, downsampled_capture);
  matched_filter_.Update(render_buffer, downsampled_capture, matched_filter_lag_aggregator_.ReliableDelayFound());

  std::optional<DelayEstimate> aggregated_matched_filter_lag =
      matched_filter_lag_aggregator_.Aggregate(matched_filter_.GetBestLagEstimate());

  // Run clockdrift detection.
  if (aggregated_matched_filter_lag && aggregated_matched_filter_lag->quality == DelayEstimate::Quality::kRefined) {
    clockdrift_detector_.Update(matched_filter_lag_aggregator_.GetDelayAtHighestPeak());
  }

  // The delay in samples is the aggregated lag compensated by the down
  // sampling factor of the correlated signal.
  if (aggregated_matched_filter_lag) aggregated_matched_filter_lag->delay *= down_sampling_factor_;

  if (old_aggregated_lag_ && aggregated_matched_filter_lag &&
      old_aggregated_lag_->delay == aggregated_matched_filter_lag->delay) {
    ++consistent_estimate_counter_;
  } else {
    consistent_estimate_counter_ = 0;
  }
  old_aggregated_lag_ = aggregated_matched_filter_lag;
  constexpr size_t kNumBlocksPerSecondBy2 = kNumBlocksPerSecond / 2;
  if (consistent_estimate_counter_ > kNumBlocksPerSecondBy2) Reset(false, false);

  return aggregated_matched_filter_lag;
}

void EchoPathDelayEstimator::Reset(bool reset_lag_aggregator, bool reset_delay_confidence) {
  if (reset_lag_aggregator) matched_filter_lag_aggregator_.Reset(reset_delay_confidence);
  matched_filter_.Reset(/*full_reset=*/reset_lag_aggregator);
  old_aggregated_lag_ = std::nullopt;
  consistent_estimate_counter_ = 0;
}

// --- render_delay_controller.cc ----------------------------------------------

namespace {
DelayEstimate ComputeBufferDelay(const std::optional<DelayEstimate>& current_delay, int hysteresis_limit_blocks,
                                 DelayEstimate estimated_delay) {
  // The buffer delay increase required to achieve the desired latency.
  size_t new_delay_blocks = estimated_delay.delay >> kBlockSizeLog2;
  // Add hysteresis.
  if (current_delay) {
    size_t current_delay_blocks = current_delay->delay;
    if (new_delay_blocks > current_delay_blocks &&
        new_delay_blocks <= current_delay_blocks + hysteresis_limit_blocks) {
      new_delay_blocks = current_delay_blocks;
    }
  }
  DelayEstimate new_delay = estimated_delay;
  new_delay.delay = new_delay_blocks;
  return new_delay;
}
}  // namespace

RenderDelayController::RenderDelayController(const DelayConfig& config)
    : hysteresis_limit_blocks_(static_cast<int>(config.hysteresis_limit_blocks)), delay_estimator_(config) {}

void RenderDelayController::Reset(bool reset_delay_confidence) {
  delay_ = std::nullopt;
  delay_samples_ = std::nullopt;
  delay_estimator_.Reset(reset_delay_confidence);
  if (reset_delay_confidence) last_delay_estimate_quality_ = DelayEstimate::Quality::kCoarse;
}

std::optional<DelayEstimate> RenderDelayController::GetDelay(const DownsampledRenderBuffer& render_buffer,
                                                             std::span<const float> capture) {
  ++capture_call_counter_;
  auto delay_samples = delay_estimator_.EstimateDelay(render_buffer, capture);
  if (delay_samples) delay_samples_ = delay_samples;

  if (delay_samples_) {
    // Compute the render delay buffer delay.
    const bool use_hysteresis = last_delay_estimate_quality_ == DelayEstimate::Quality::kRefined &&
                                delay_samples_->quality == DelayEstimate::Quality::kRefined;
    const std::optional<size_t> before = delay_ ? std::optional<size_t>(delay_->delay) : std::nullopt;
    delay_ = ComputeBufferDelay(delay_, use_hysteresis ? hysteresis_limit_blocks_ : 0, *delay_samples_);
    if (before != delay_->delay) ++delay_changes_;
    last_delay_estimate_quality_ = delay_->quality;
  }
  return delay_;
}

void RenderDelayController::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("delay_blocks", delay_ ? static_cast<double>(delay_->delay) : -1.0);
  w.Gauge("delay_ms", delay_samples_ ? delay_samples_->delay / 16.0 : -1.0);
  w.Gauge("refined", delay_ && delay_->quality == DelayEstimate::Quality::kRefined ? 1.0 : 0.0);
  w.Gauge("clockdrift_level", static_cast<double>(delay_estimator_.Clockdrift()));
  w.Counter("capture_blocks", capture_call_counter_);
  w.Counter("delay_changes", delay_changes_);
}

// --- render_delay_buffer.cc (the low-rate buffer) ----------------------------

RenderDelayBufferLowRate::RenderDelayBufferLowRate(const DelayConfig& config)
    : config_(config),
      sub_block_size_(static_cast<int>(config.down_sampling_factor > 0 ? kBlockSize / config.down_sampling_factor
                                                                        : kBlockSize)),
      low_rate_(GetDownSampledBufferSize(config.down_sampling_factor, config.num_filters)),
      render_decimator_(config.down_sampling_factor),
      render_ds_(sub_block_size_, 0.f) {
  Reset();
}

void RenderDelayBufferLowRate::Reset() {
  min_latency_blocks_ = 0;
  excess_render_detection_counter_ = 0;
  // Initialize the read index to one sub-block before the write index.
  low_rate_.read = low_rate_.OffsetIndex(low_rate_.write, sub_block_size_);
}

RenderDelayBufferLowRate::BufferingEvent RenderDelayBufferLowRate::Insert(std::span<const float> block) {
  // Increase the write index to where the new block should be written.
  low_rate_.UpdateWriteIndex(-sub_block_size_);
  // Allow overrun and reset when more render than capture data arrives.
  const BufferingEvent event = low_rate_.read == low_rate_.write ? BufferingEvent::kRenderOverrun : BufferingEvent::kNone;
  // The decimated block is stored newest sample first.
  render_decimator_.Decimate(block, render_ds_);
  std::copy(render_ds_.rbegin(), render_ds_.rend(), low_rate_.buffer.begin() + low_rate_.write);
  if (event != BufferingEvent::kNone) Reset();
  return event;
}

RenderDelayBufferLowRate::BufferingEvent RenderDelayBufferLowRate::PrepareCaptureProcessing() {
  if (DetectExcessRenderBlocks()) {
    // Too many render blocks compared to capture blocks: the delay risks
    // ending up before the filters of the delay estimator.
    Reset();
    return BufferingEvent::kRenderOverrun;
  }
  if (low_rate_.read == low_rate_.write) {
    // Don't move the low-rate read index on a render underrun.
    return BufferingEvent::kRenderUnderrun;
  }
  // Point the read index at the most recent sub-block for this capture block.
  low_rate_.UpdateReadIndex(-sub_block_size_);
  return BufferingEvent::kNone;
}

bool RenderDelayBufferLowRate::DetectExcessRenderBlocks() {
  bool excess_render_detected = false;
  const size_t latency_blocks = static_cast<size_t>(BufferLatency());
  // The recently seen minimum latency in blocks. Should be close to 0.
  min_latency_blocks_ = std::min(min_latency_blocks_, latency_blocks);
  // After a configurable number of blocks the minimum latency is checked.
  if (++excess_render_detection_counter_ >= config_.excess_render_detection_interval_blocks) {
    // If the minimum latency is not lower than the threshold there have been
    // more render than capture frames.
    excess_render_detected = min_latency_blocks_ > config_.max_allowed_excess_render_blocks;
    // Reset the counter and let the minimum latency be the current latency.
    min_latency_blocks_ = latency_blocks;
    excess_render_detection_counter_ = 0;
  }
  return excess_render_detected;
}

int RenderDelayBufferLowRate::BufferLatency() const {
  const DownsampledRenderBuffer& l = low_rate_;
  int latency_samples = (l.size + l.read - l.write) % l.size;
  return latency_samples / sub_block_size_;
}

}  // namespace sm::audio::aec3
