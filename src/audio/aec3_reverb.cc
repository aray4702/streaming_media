// AEC3's reverb model and its estimator, ported from libwebrtc M157's
// modules/audio_processing/aec3/ (see sm/audio/aec3_reverb.h for the files).
#include "sm/audio/aec3_reverb.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace sm::audio::aec3 {

// --- reverb_model.cc ---------------------------------------------------------

ReverbModel::ReverbModel() { Reset(); }

void ReverbModel::Reset() { reverb_.fill(0.); }

void ReverbModel::UpdateReverbNoFreqShaping(std::span<const float> power_spectrum, float power_spectrum_scaling,
                                            float reverb_decay) {
  if (reverb_decay > 0) {
    // Update the estimate of the reverberant power.
    for (size_t k = 0; k < power_spectrum.size(); ++k) {
      reverb_[k] = (reverb_[k] + power_spectrum[k] * power_spectrum_scaling) * reverb_decay;
    }
  }
}

void ReverbModel::UpdateReverb(std::span<const float> power_spectrum, std::span<const float> power_spectrum_scaling,
                               float reverb_decay) {
  if (reverb_decay > 0) {
    // Update the estimate of the reverberant power.
    for (size_t k = 0; k < power_spectrum.size(); ++k) {
      reverb_[k] = (reverb_[k] + power_spectrum[k] * power_spectrum_scaling[k]) * reverb_decay;
    }
  }
}

// --- reverb_decay_estimator.cc -----------------------------------------------

namespace {

constexpr int kEarlyReverbMinSizeBlocks = 3;
constexpr int kBlocksPerSection = 6;
// Linear regression approach assumes symmetric index around 0.
constexpr float kEarlyReverbFirstPointAtLinearRegressors =
    -0.5f * kBlocksPerSection * static_cast<float>(kFftLengthBy2) + 0.5f;

// Averages the values in a block of size kFftLengthBy2;
float BlockAverage(std::span<const float> v, size_t block_index) {
  constexpr float kOneByFftLengthBy2 = 1.f / kFftLengthBy2;
  const size_t i = block_index * kFftLengthBy2;
  const float sum = std::accumulate(v.begin() + static_cast<std::ptrdiff_t>(i),
                                    v.begin() + static_cast<std::ptrdiff_t>(i + kFftLengthBy2), 0.f);
  return sum * kOneByFftLengthBy2;
}

// Analyzes the gain in a block.
void AnalyzeBlockGain(const std::array<float, kFftLengthBy2>& h2, float floor_gain, float* previous_gain,
                      bool* block_adapting, bool* decaying_gain) {
  float gain = std::max(BlockAverage(h2, 0), 1e-32f);
  *block_adapting = *previous_gain > 1.1f * gain || *previous_gain < 0.9f * gain;
  *decaying_gain = gain > floor_gain;
  *previous_gain = gain;
}

// Arithmetic sum of $2 \sum_{i=0.5}^{(N-1)/2}i^2$ calculated directly.
constexpr float SymmetricArithmetricSum(int N) {
  return static_cast<float>(N) * (static_cast<float>(N * N) - 1.0f) * (1.f / 12.f);
}

// Returns the peak energy of an impulse response.
float BlockEnergyPeak(std::span<const float> h, int peak_block) {
  const auto begin = h.begin() + peak_block * static_cast<std::ptrdiff_t>(kFftLengthBy2);
  float peak_value = *std::max_element(begin, begin + static_cast<std::ptrdiff_t>(kFftLengthBy2),
                                       [](float a, float b) { return a * a < b * b; });
  return peak_value * peak_value;
}

// Returns the average energy of an impulse response block.
float BlockEnergyAverage(std::span<const float> h, int block_index) {
  constexpr float kOneByFftLengthBy2 = 1.f / kFftLengthBy2;
  const auto sum_of_squares = [](float a, float b) { return a + b * b; };
  const auto begin = h.begin() + block_index * static_cast<std::ptrdiff_t>(kFftLengthBy2);
  return std::accumulate(begin, begin + static_cast<std::ptrdiff_t>(kFftLengthBy2), 0.f, sum_of_squares) *
         kOneByFftLengthBy2;
}

}  // namespace

ReverbDecayEstimator::ReverbDecayEstimator(const Aec3Config& config)
    : filter_length_blocks_(static_cast<int>(config.filter.refined.length_blocks)),
      filter_length_coefficients_(GetTimeDomainLength(filter_length_blocks_)),
      use_adaptive_echo_decay_(config.ep_strength.default_len < 0.f),
      early_reverb_estimator_(static_cast<int>(config.filter.refined.length_blocks) - kEarlyReverbMinSizeBlocks),
      late_reverb_start_(kEarlyReverbMinSizeBlocks),
      late_reverb_end_(kEarlyReverbMinSizeBlocks),
      previous_gains_(config.filter.refined.length_blocks, 0.f),
      decay_(std::fabs(config.ep_strength.default_len)),
      mild_decay_(std::fabs(config.ep_strength.nearend_len)) {}

void ReverbDecayEstimator::Update(std::span<const float> filter, const std::optional<float>& filter_quality,
                                  int filter_delay_blocks, bool usable_linear_filter, bool stationary_signal) {
  const int filter_size = static_cast<int>(filter.size());

  if (stationary_signal) {
    return;
  }

  bool estimation_feasible = filter_delay_blocks <= filter_length_blocks_ - kEarlyReverbMinSizeBlocks - 1;
  estimation_feasible = estimation_feasible && filter_size == filter_length_coefficients_;
  estimation_feasible = estimation_feasible && filter_delay_blocks > 0;
  estimation_feasible = estimation_feasible && usable_linear_filter;

  if (!estimation_feasible) {
    ResetDecayEstimation();
    return;
  }

  if (!use_adaptive_echo_decay_) {
    return;
  }

  const float new_smoothing = filter_quality ? *filter_quality * 0.2f : 0.f;
  smoothing_constant_ = std::max(new_smoothing, smoothing_constant_);
  if (smoothing_constant_ == 0.f) {
    return;
  }

  if (block_to_analyze_ < filter_length_blocks_) {
    // Analyze the filter and accumulate data for reverb estimation.
    AnalyzeFilter(filter);
    ++block_to_analyze_;
  } else {
    // When the filter is fully analyzed, estimate the reverb decay and reset
    // the block_to_analyze_ counter.
    EstimateDecay(filter, filter_delay_blocks);
  }
}

void ReverbDecayEstimator::ResetDecayEstimation() {
  early_reverb_estimator_.Reset();
  late_reverb_decay_estimator_.Reset(0);
  block_to_analyze_ = 0;
  estimation_region_candidate_size_ = 0;
  estimation_region_identified_ = false;
  smoothing_constant_ = 0.f;
  late_reverb_start_ = 0;
  late_reverb_end_ = 0;
}

void ReverbDecayEstimator::EstimateDecay(std::span<const float> filter, int peak_block) {
  auto& h = filter;

  // Reset the block analysis counter.
  block_to_analyze_ = std::min(peak_block + kEarlyReverbMinSizeBlocks, filter_length_blocks_);

  // To estimate the reverb decay, the energy of the first filter section must
  // be substantially larger than the last. Also, the first filter section
  // energy must not deviate too much from the max peak.
  const float first_reverb_gain = BlockEnergyAverage(h, block_to_analyze_);
  const size_t h_size_blocks = h.size() >> kFftLengthBy2Log2;
  tail_gain_ = BlockEnergyAverage(h, static_cast<int>(h_size_blocks) - 1);
  float peak_energy = BlockEnergyPeak(h, peak_block);
  const bool sufficient_reverb_decay = first_reverb_gain > 4.f * tail_gain_;
  const bool valid_filter = first_reverb_gain > 2.f * tail_gain_ && peak_energy < 100.f;

  // Estimate the size of the regions with early and late reflections.
  const int size_early_reverb = early_reverb_estimator_.Estimate();
  const int size_late_reverb = std::max(estimation_region_candidate_size_ - size_early_reverb, 0);

  // Only update the reverb decay estimate if the size of the identified late
  // reverb is sufficiently large.
  if (size_late_reverb >= 5) {
    if (valid_filter && late_reverb_decay_estimator_.EstimateAvailable()) {
      float decay =
          std::pow(2.0f, late_reverb_decay_estimator_.Estimate() * static_cast<float>(kFftLengthBy2));
      constexpr float kMaxDecay = 0.95f;  // ~1 sec min RT60.
      constexpr float kMinDecay = 0.02f;  // ~15 ms max RT60.
      decay = std::max(.97f * decay_, decay);
      decay = std::min(decay, kMaxDecay);
      decay = std::max(decay, kMinDecay);
      decay_ += smoothing_constant_ * (decay - decay_);
    }

    // Update length of decay. Must have enough data (number of sections) in
    // order to estimate decay rate.
    late_reverb_decay_estimator_.Reset(size_late_reverb * static_cast<int>(kFftLengthBy2));
    late_reverb_start_ = peak_block + kEarlyReverbMinSizeBlocks + size_early_reverb;
    late_reverb_end_ = block_to_analyze_ + estimation_region_candidate_size_ - 1;
  } else {
    late_reverb_decay_estimator_.Reset(0);
    late_reverb_start_ = 0;
    late_reverb_end_ = 0;
  }

  // Reset variables for the identification of the region for reverb decay
  // estimation.
  estimation_region_identified_ = !(valid_filter && sufficient_reverb_decay);
  estimation_region_candidate_size_ = 0;

  // Stop estimation of the decay until another good filter is received.
  smoothing_constant_ = 0.f;

  // Reset early reflections detector.
  early_reverb_estimator_.Reset();
}

void ReverbDecayEstimator::AnalyzeFilter(std::span<const float> filter) {
  auto h = filter.subspan(static_cast<size_t>(block_to_analyze_) * kFftLengthBy2, kFftLengthBy2);

  // Compute squared filter coeffiecients for the block to analyze_;
  std::array<float, kFftLengthBy2> h2;
  std::transform(h.begin(), h.end(), h2.begin(), [](float a) { return a * a; });

  // Map out the region for estimating the reverb decay.
  bool adapting;
  bool above_noise_floor;
  AnalyzeBlockGain(h2, tail_gain_, &previous_gains_[static_cast<size_t>(block_to_analyze_)], &adapting,
                   &above_noise_floor);

  // Count consecutive number of "good" filter sections, where "good" means:
  // 1) energy is above noise floor.
  // 2) energy of current section has not changed too much from last check.
  estimation_region_identified_ = estimation_region_identified_ || adapting || !above_noise_floor;
  if (!estimation_region_identified_) {
    ++estimation_region_candidate_size_;
  }

  // Accumulate data for reverb decay estimation and for the estimation of early
  // reflections. (libwebrtc adds the double 1e-10 and passes the float.)
  if (block_to_analyze_ <= late_reverb_end_) {
    if (block_to_analyze_ >= late_reverb_start_) {
      for (float h2_k : h2) {
        float h2_log2 = FastApproxLog2f(static_cast<float>(h2_k + 1e-10));
        late_reverb_decay_estimator_.Accumulate(h2_log2);
        early_reverb_estimator_.Accumulate(h2_log2, smoothing_constant_);
      }
    } else {
      for (float h2_k : h2) {
        float h2_log2 = FastApproxLog2f(static_cast<float>(h2_k + 1e-10));
        early_reverb_estimator_.Accumulate(h2_log2, smoothing_constant_);
      }
    }
  }
}

void ReverbDecayEstimator::LateReverbLinearRegressor::Reset(int num_data_points) {
  const int N = num_data_points;
  nz_ = 0.f;
  // Arithmetic sum of $2 \sum_{i=0.5}^{(N-1)/2}i^2$ calculated directly.
  nn_ = SymmetricArithmetricSum(N);
  // The linear regression approach assumes symmetric index around 0.
  count_ = N > 0 ? static_cast<float>(-N) * 0.5f + 0.5f : 0.f;
  N_ = N;
  n_ = 0;
}

void ReverbDecayEstimator::LateReverbLinearRegressor::Accumulate(float z) {
  nz_ += count_ * z;
  ++count_;
  ++n_;
}

float ReverbDecayEstimator::LateReverbLinearRegressor::Estimate() {
  if (nn_ == 0.f) {
    return 0.f;
  }
  return nz_ / nn_;
}

ReverbDecayEstimator::EarlyReverbLengthEstimator::EarlyReverbLengthEstimator(int max_blocks)
    : numerators_smooth_(static_cast<size_t>(max_blocks - kBlocksPerSection), 0.f),
      numerators_(numerators_smooth_.size(), 0.f),
      coefficients_counter_(0) {}

void ReverbDecayEstimator::EarlyReverbLengthEstimator::Reset() {
  coefficients_counter_ = 0;
  std::fill(numerators_.begin(), numerators_.end(), 0.f);
  block_counter_ = 0;
}

void ReverbDecayEstimator::EarlyReverbLengthEstimator::Accumulate(float value, float smoothing) {
  // Each section is composed by kBlocksPerSection blocks and each section
  // overlaps with the next one in (kBlocksPerSection - 1) blocks. For example,
  // the first section covers the blocks [0:5], the second covers the blocks
  // [1:6] and so on. As a result, for each value, kBlocksPerSection sections
  // need to be updated.
  int first_section_index = std::max(block_counter_ - kBlocksPerSection + 1, 0);
  int last_section_index = std::min(block_counter_, static_cast<int>(numerators_.size() - 1));
  float x_value = static_cast<float>(coefficients_counter_) + kEarlyReverbFirstPointAtLinearRegressors;
  const float value_to_inc = static_cast<float>(kFftLengthBy2) * value;
  float value_to_add = x_value * value + static_cast<float>(block_counter_ - last_section_index) * value_to_inc;
  for (int section = last_section_index; section >= first_section_index; --section, value_to_add += value_to_inc) {
    numerators_[static_cast<size_t>(section)] += value_to_add;
  }

  // Check if this update was the last coefficient of the current block. In that
  // case, check if we are at the end of one of the sections and update the
  // numerator of the linear regressor that is computed in such section.
  if (++coefficients_counter_ == static_cast<int>(kFftLengthBy2)) {
    if (block_counter_ >= (kBlocksPerSection - 1)) {
      size_t section = static_cast<size_t>(block_counter_ - (kBlocksPerSection - 1));
      numerators_smooth_[section] += smoothing * (numerators_[section] - numerators_smooth_[section]);
      n_sections_ = static_cast<int>(section) + 1;
    }
    ++block_counter_;
    coefficients_counter_ = 0;
  }
}

// Estimates the size in blocks of the early reverb. The estimation is done by
// comparing the tilt that is estimated in each section. As an optimization
// detail and due to the fact that all the linear regressors that are computed
// shared the same denominator, the comparison of the tilts is done by a
// comparison of the numerator of the linear regressors.
int ReverbDecayEstimator::EarlyReverbLengthEstimator::Estimate() {
  // libwebrtc's N is a float (384) passed to the int parameter.
  constexpr float N = kBlocksPerSection * kFftLengthBy2;
  constexpr float nn = SymmetricArithmetricSum(static_cast<int>(N));
  // numerator_11 refers to the quantity that the linear regressor needs in the
  // numerator for getting a decay equal to 1.1 (which is not a decay).
  // log2(1.1) * nn / kFftLengthBy2.
  constexpr float numerator_11 = 0.13750352374993502f * nn / static_cast<float>(kFftLengthBy2);
  // log2(0.8) *  nn / kFftLengthBy2.
  constexpr float numerator_08 = -0.32192809488736229f * nn / static_cast<float>(kFftLengthBy2);
  constexpr int kNumSectionsToAnalyze = 9;

  if (n_sections_ < kNumSectionsToAnalyze) {
    return 0;
  }

  // Estimation of the blocks that correspond to early reverberations. The
  // estimation is done by analyzing the impulse response. The portions of the
  // impulse response whose energy is not decreasing over its coefficients are
  // considered to be part of the early reverberations. Furthermore, the blocks
  // where the energy is decreasing faster than what it does at the end of the
  // impulse response are also considered to be part of the early
  // reverberations. The estimation is limited to the first
  // kNumSectionsToAnalyze sections.
  const float min_numerator_tail = *std::min_element(numerators_smooth_.begin() + kNumSectionsToAnalyze,
                                                     numerators_smooth_.begin() + n_sections_);
  int early_reverb_size_minus_1 = 0;
  for (int k = 0; k < kNumSectionsToAnalyze; ++k) {
    const float numerator = numerators_smooth_[static_cast<size_t>(k)];
    if ((numerator > numerator_11) || (numerator < numerator_08 && numerator < 0.9f * min_numerator_tail)) {
      early_reverb_size_minus_1 = k;
    }
  }

  return early_reverb_size_minus_1 == 0 ? 0 : early_reverb_size_minus_1 + 1;
}

// --- reverb_frequency_response.cc --------------------------------------------

namespace {

// Computes the ratio of the energies between the direct path and the tail. The
// energy is computed in the power spectrum domain discarding the DC
// contributions.
float AverageDecayWithinFilter(std::span<const float> freq_resp_direct_path, std::span<const float> freq_resp_tail) {
  // Skipping the DC for the ratio computation
  constexpr size_t kSkipBins = 1;

  float direct_path_energy = std::accumulate(freq_resp_direct_path.begin() + kSkipBins, freq_resp_direct_path.end(), 0.f);

  if (direct_path_energy == 0.f) {
    return 0.f;
  }

  float tail_energy = std::accumulate(freq_resp_tail.begin() + kSkipBins, freq_resp_tail.end(), 0.f);
  return tail_energy / direct_path_energy;
}

}  // namespace

ReverbFrequencyResponse::ReverbFrequencyResponse(bool use_conservative_tail_frequency_response)
    : use_conservative_tail_frequency_response_(use_conservative_tail_frequency_response) {
  tail_response_.fill(0.0f);
}

void ReverbFrequencyResponse::Update(const std::vector<std::array<float, kFftLengthBy2Plus1>>& frequency_response,
                                     int filter_delay_blocks, const std::optional<float>& linear_filter_quality,
                                     bool stationary_block) {
  if (stationary_block || !linear_filter_quality) {
    return;
  }

  Update(frequency_response, filter_delay_blocks, *linear_filter_quality);
}

void ReverbFrequencyResponse::Update(const std::vector<std::array<float, kFftLengthBy2Plus1>>& frequency_response,
                                     int filter_delay_blocks, float linear_filter_quality) {
  std::span<const float> freq_resp_tail(frequency_response[frequency_response.size() - 1]);

  std::span<const float> freq_resp_direct_path(frequency_response[static_cast<size_t>(filter_delay_blocks)]);

  float average_decay = AverageDecayWithinFilter(freq_resp_direct_path, freq_resp_tail);

  const float smoothing = 0.2f * linear_filter_quality;
  average_decay_ += smoothing * (average_decay - average_decay_);

  for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
    tail_response_[k] = freq_resp_direct_path[k] * average_decay_;
  }

  if (use_conservative_tail_frequency_response_) {
    for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
      tail_response_[k] = std::max(freq_resp_tail[k], tail_response_[k]);
    }
  }

  for (size_t k = 1; k < kFftLengthBy2; ++k) {
    const float avg_neighbour = 0.5f * (tail_response_[k - 1] + tail_response_[k + 1]);
    tail_response_[k] = std::max(tail_response_[k], avg_neighbour);
  }
}

// --- reverb_model_estimator.cc -----------------------------------------------

ReverbModelEstimator::ReverbModelEstimator(const Aec3Config& config, size_t num_capture_channels)
    : reverb_decay_estimators_(num_capture_channels),
      reverb_frequency_responses_(num_capture_channels,
                                  ReverbFrequencyResponse(config.ep_strength.use_conservative_tail_frequency_response)) {
  for (size_t ch = 0; ch < reverb_decay_estimators_.size(); ++ch) {
    reverb_decay_estimators_[ch] = std::make_unique<ReverbDecayEstimator>(config);
  }
}

void ReverbModelEstimator::Update(
    std::span<const std::vector<float>> impulse_responses,
    std::span<const std::vector<std::array<float, kFftLengthBy2Plus1>>> frequency_responses,
    std::span<const std::optional<float>> linear_filter_qualities, std::span<const int> filter_delays_blocks,
    const std::vector<bool>& usable_linear_estimates, bool stationary_block) {
  const size_t num_capture_channels = reverb_decay_estimators_.size();

  for (size_t ch = 0; ch < num_capture_channels; ++ch) {
    // Estimate the frequency response for the reverb.
    reverb_frequency_responses_[ch].Update(frequency_responses[ch], filter_delays_blocks[ch],
                                           linear_filter_qualities[ch], stationary_block);

    // Estimate the reverb decay,
    reverb_decay_estimators_[ch]->Update(impulse_responses[ch], linear_filter_qualities[ch], filter_delays_blocks[ch],
                                         usable_linear_estimates[ch], stationary_block);
  }
}

}  // namespace sm::audio::aec3
