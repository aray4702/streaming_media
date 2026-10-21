#include "sm/audio/ns.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

#include "sm/audio/fft4g.h"
#include "sm/core/dsp.h"

namespace sm::audio::ns {

// suppression_params.cc
SuppressionParams::SuppressionParams(SuppressionLevel level) {
  switch (level) {
    case SuppressionLevel::k6dB:
      over_subtraction_factor = 1.f;
      minimum_attenuating_gain = 0.5f;  // 6 dB attenuation.
      use_attenuation_adjustment = false;
      break;
    case SuppressionLevel::k12dB:
      over_subtraction_factor = 1.f;
      minimum_attenuating_gain = 0.25f;  // 12 dB attenuation.
      use_attenuation_adjustment = true;
      break;
    case SuppressionLevel::k18dB:
      over_subtraction_factor = 1.1f;
      minimum_attenuating_gain = 0.125f;  // 18 dB attenuation.
      use_attenuation_adjustment = true;
      break;
    case SuppressionLevel::k21dB:
    default:
      over_subtraction_factor = 1.25f;
      minimum_attenuating_gain = 0.09f;  // 20.9 dB attenuation.
      use_attenuation_adjustment = true;
      break;
  }
}

// fast_math.cc
namespace {
float FastLog2f(float in) {
  // The float's bits read as an integer, scaled by 1/2^23, minus the bias.
  float out = static_cast<float>(std::bit_cast<uint32_t>(in));
  out *= 1.1920929e-7f;  // 1/2^23
  out -= 126.942695f;    // Remove bias.
  return out;
}
float SqrtFastApproximation(float f) { return std::sqrt(f); }
float Pow2Approximation(float p) { return std::pow(2.f, p); }
}  // namespace

float PowApproximation(float x, float p) { return Pow2Approximation(p * FastLog2f(x)); }
float LogApproximation(float x) { return FastLog2f(x) * std::numbers::ln2_v<float>; }
float ExpApproximation(float x) { return PowApproximation(10.f, x * std::numbers::log10e_v<float>); }

// quantile_noise_estimator.cc
QuantileNoiseEstimator::QuantileNoiseEstimator() {
  quantile_.fill(0.f);
  density_.fill(0.3f);
  log_quantile_.fill(8.f);
  constexpr float kOneBySimult = 1.f / kSimult;
  for (int i = 0; i < kSimult; ++i) {
    counter_[i] = static_cast<int>(std::floor(kLongStartupPhaseBlocks * (i + 1.f) * kOneBySimult));
  }
}

void QuantileNoiseEstimator::Estimate(std::span<const float, kFftSizeBy2Plus1> signal_spectrum,
                                      std::span<float, kFftSizeBy2Plus1> noise_spectrum) {
  Spectrum log_spectrum;
  for (size_t i = 0; i < kFftSizeBy2Plus1; ++i) log_spectrum[i] = LogApproximation(signal_spectrum[i]);

  int quantile_index_to_return = -1;
  // Loop over simultaneous estimates.
  for (int s = 0, k = 0; s < kSimult; ++s, k += static_cast<int>(kFftSizeBy2Plus1)) {
    const float one_by_counter_plus_1 = 1.f / (counter_[s] + 1.f);
    for (int i = 0, j = k; i < static_cast<int>(kFftSizeBy2Plus1); ++i, ++j) {
      // Update log quantile estimate.
      const float delta = density_[j] > 1.f ? 40.f / density_[j] : 40.f;
      const float multiplier = delta * one_by_counter_plus_1;
      if (log_spectrum[i] > log_quantile_[j]) {
        log_quantile_[j] += 0.25f * multiplier;
      } else {
        log_quantile_[j] -= 0.75f * multiplier;
      }
      // Update density estimate.
      constexpr float kWidth = 0.01f;
      constexpr float kOneByWidthPlus2 = 1.f / (2.f * kWidth);
      if (std::fabs(log_spectrum[i] - log_quantile_[j]) < kWidth) {
        density_[j] = (counter_[s] * density_[j] + kOneByWidthPlus2) * one_by_counter_plus_1;
      }
    }
    if (counter_[s] >= kLongStartupPhaseBlocks) {
      counter_[s] = 0;
      if (num_updates_ >= kLongStartupPhaseBlocks) quantile_index_to_return = k;
    }
    ++counter_[s];
  }

  // Sequentially update the noise during startup, from the last estimate.
  if (num_updates_ < kLongStartupPhaseBlocks) {
    quantile_index_to_return = static_cast<int>(kFftSizeBy2Plus1) * (kSimult - 1);
    ++num_updates_;
  }
  if (quantile_index_to_return >= 0) {
    for (size_t i = 0; i < kFftSizeBy2Plus1; ++i) {
      quantile_[i] = ExpApproximation(log_quantile_[static_cast<size_t>(quantile_index_to_return) + i]);
    }
  }
  std::copy(quantile_.begin(), quantile_.end(), noise_spectrum.begin());
}

// noise_estimator.cc
namespace {
// Log(i); the first five entries are unused (kStartBand).
constexpr std::array<float, 129> kLogTable = {
    0.f,       0.f,       0.f,       0.f,       0.f,       1.609438f, 1.791759f, 1.945910f, 2.079442f, 2.197225f,
    std::numbers::ln10_v<float>,     2.397895f, 2.484907f, 2.564949f, 2.639057f, 2.708050f, 2.772589f, 2.833213f,
    2.890372f, 2.944439f, 2.995732f, 3.044522f, 3.091043f, 3.135494f, 3.178054f, 3.218876f, 3.258097f, 3.295837f,
    3.332205f, 3.367296f, 3.401197f, 3.433987f, 3.465736f, 3.496507f, 3.526361f, 3.555348f, 3.583519f, 3.610918f,
    3.637586f, 3.663562f, 3.688879f, 3.713572f, 3.737669f, 3.761200f, 3.784190f, 3.806663f, 3.828641f, 3.850147f,
    3.871201f, 3.891820f, 3.912023f, 3.931826f, 3.951244f, 3.970292f, 3.988984f, 4.007333f, 4.025352f, 4.043051f,
    4.060443f, 4.077538f, 4.094345f, 4.110874f, 4.127134f, 4.143135f, 4.158883f, 4.174387f, 4.189655f, 4.204693f,
    4.219508f, 4.234107f, 4.248495f, 4.262680f, 4.276666f, 4.290460f, 4.304065f, 4.317488f, 4.330733f, 4.343805f,
    4.356709f, 4.369448f, 4.382027f, 4.394449f, 4.406719f, 4.418841f, 4.430817f, 4.442651f, 4.454347f, 4.465908f,
    4.477337f, 4.488636f, 4.499810f, 4.510859f, 4.521789f, 4.532599f, 4.543295f, 4.553877f, 4.564348f, 4.574711f,
    4.584968f, 4.595119f, 4.605170f, 4.615121f, 4.624973f, 4.634729f, 4.644391f, 4.653960f, 4.663439f, 4.672829f,
    4.682131f, 4.691348f, 4.700480f, 4.709530f, 4.718499f, 4.727388f, 4.736198f, 4.744932f, 4.753591f, 4.762174f,
    4.770685f, 4.779124f, 4.787492f, 4.795791f, 4.804021f, 4.812184f, 4.820282f, 4.828314f, 4.836282f, 4.844187f,
    4.852030f};
}  // namespace

NoiseEstimator::NoiseEstimator(const SuppressionParams& params) : params_(params) {}

void NoiseEstimator::PreUpdate(int32_t num_analyzed_frames, std::span<const float, kFftSizeBy2Plus1> signal_spectrum,
                               float signal_spectral_sum) {
  quantile_noise_estimator_.Estimate(signal_spectrum, noise_spectrum_);
  if (num_analyzed_frames >= kShortStartupPhaseBlocks) return;

  // Simplified noise model during startup.
  constexpr size_t kStartBand = 5;
  float sum_log_i_log_magn = 0.f, sum_log_i = 0.f, sum_log_i_square = 0.f, sum_log_magn = 0.f;
  for (size_t i = kStartBand; i < kFftSizeBy2Plus1; ++i) {
    float log_i = kLogTable[i];
    sum_log_i += log_i;
    sum_log_i_square += log_i * log_i;
    float log_signal = LogApproximation(signal_spectrum[i]);
    sum_log_magn += log_signal;
    sum_log_i_log_magn += log_i * log_signal;
  }

  // White noise level.
  constexpr float kOneByFftSizeBy2Plus1 = 1.f / kFftSizeBy2Plus1;
  white_noise_level_ += signal_spectral_sum * kOneByFftSizeBy2Plus1 * params_.over_subtraction_factor;

  // Pink noise parameters.
  float denom = sum_log_i_square * (kFftSizeBy2Plus1 - kStartBand) - sum_log_i * sum_log_i;
  float num = sum_log_i_square * sum_log_magn - sum_log_i * sum_log_i_log_magn;
  float pink_noise_adjustment = num / denom;
  // Constrain the estimated spectrum to be positive.
  pink_noise_adjustment = std::max(pink_noise_adjustment, 0.f);
  pink_noise_numerator_ += pink_noise_adjustment;
  num = sum_log_i * sum_log_magn - (kFftSizeBy2Plus1 - kStartBand) * sum_log_i_log_magn;
  pink_noise_adjustment = num / denom;
  // Constrain the pink noise power to [0, 1].
  pink_noise_adjustment = std::max(std::min(pink_noise_adjustment, 1.f), 0.f);
  pink_noise_exp_ += pink_noise_adjustment;

  const float one_by_num_analyzed_frames_plus_1 = 1.f / (num_analyzed_frames + 1.f);

  // Frequency-independent parts of the parametric noise estimate.
  float parametric_exp = 0.f, parametric_num = 0.f;
  if (pink_noise_exp_ > 0.f) {
    parametric_num = ExpApproximation(pink_noise_numerator_ * one_by_num_analyzed_frames_plus_1);
    parametric_num *= num_analyzed_frames + 1.f;
    parametric_exp = pink_noise_exp_ * one_by_num_analyzed_frames_plus_1;
  }

  constexpr float kOneByShortStartupPhaseBlocks = 1.f / kShortStartupPhaseBlocks;
  for (size_t i = 0; i < kFftSizeBy2Plus1; ++i) {
    if (pink_noise_exp_ == 0.f) {
      parametric_noise_spectrum_[i] = white_noise_level_;
    } else {
      float use_band = static_cast<float>(i < kStartBand ? kStartBand : i);
      parametric_noise_spectrum_[i] = parametric_num / PowApproximation(use_band, parametric_exp);
    }
  }

  // Weight quantile noise with modeled noise.
  for (size_t i = 0; i < kFftSizeBy2Plus1; ++i) {
    noise_spectrum_[i] *= num_analyzed_frames;
    float tmp = parametric_noise_spectrum_[i] * (kShortStartupPhaseBlocks - num_analyzed_frames);
    noise_spectrum_[i] += tmp * one_by_num_analyzed_frames_plus_1;
    noise_spectrum_[i] *= kOneByShortStartupPhaseBlocks;
  }
}

void NoiseEstimator::PostUpdate(std::span<const float, kFftSizeBy2Plus1> speech_probability,
                                std::span<const float, kFftSizeBy2Plus1> signal_spectrum) {
  constexpr float kNoiseUpdate = 0.9f;  // Time-average for the noise update.
  constexpr float kProbRange = .2f;
  float gamma = kNoiseUpdate;
  for (size_t i = 0; i < kFftSizeBy2Plus1; ++i) {
    const float prob_speech = speech_probability[i];
    const float prob_non_speech = 1.f - prob_speech;
    // Update used for speech frames if it is below the previous one.
    float noise_update_tmp = gamma * prev_noise_spectrum_[i] +
                             (1.f - gamma) * (prob_non_speech * signal_spectrum[i] + prob_speech * prev_noise_spectrum_[i]);
    // Time constant from the speech/noise state; gamma carries over bins,
    // as in libwebrtc.
    float gamma_old = gamma;
    gamma = prob_speech > kProbRange ? .99f : kNoiseUpdate;
    // Conservative noise update.
    if (prob_speech < kProbRange) {
      conservative_noise_spectrum_[i] += 0.05f * (signal_spectrum[i] - conservative_noise_spectrum_[i]);
    }
    if (gamma == gamma_old) {
      noise_spectrum_[i] = noise_update_tmp;
    } else {
      noise_spectrum_[i] = gamma * prev_noise_spectrum_[i] +
                           (1.f - gamma) * (prob_non_speech * signal_spectrum[i] + prob_speech * prev_noise_spectrum_[i]);
      // Decreases are safe, so allow them.
      noise_spectrum_[i] = std::min(noise_spectrum_[i], noise_update_tmp);
    }
  }
}

// signal_model.cc
SignalModel::SignalModel() {
  constexpr float kSfFeatureThr = 0.5f;
  lrt = kLtrFeatureThr;
  spectral_flatness = kSfFeatureThr;
  spectral_diff = kSfFeatureThr;
  avg_log_lrt.fill(kLtrFeatureThr);
}

// histograms.cc
void Histograms::Clear() {
  lrt_.fill(0);
  spectral_flatness_.fill(0);
  spectral_diff_.fill(0);
}

void Histograms::Update(const SignalModel& features) {
  constexpr float kOneByBinSizeLrt = 1.f / kBinSizeLrt;
  if (features.lrt < kHistogramSize * kBinSizeLrt && features.lrt >= 0.f) {
    ++lrt_[static_cast<size_t>(kOneByBinSizeLrt * features.lrt)];
  }
  constexpr float kOneByBinSizeSpecFlat = 1.f / kBinSizeSpecFlat;
  if (features.spectral_flatness < kHistogramSize * kBinSizeSpecFlat && features.spectral_flatness >= 0.f) {
    ++spectral_flatness_[static_cast<size_t>(features.spectral_flatness * kOneByBinSizeSpecFlat)];
  }
  constexpr float kOneByBinSizeSpecDiff = 1.f / kBinSizeSpecDiff;
  if (features.spectral_diff < kHistogramSize * kBinSizeSpecDiff && features.spectral_diff >= 0.f) {
    ++spectral_diff_[static_cast<size_t>(features.spectral_diff * kOneByBinSizeSpecDiff)];
  }
}

// prior_signal_model_estimator.cc
namespace {
// The first of the two largest peaks in the histogram.
void FindFirstOfTwoLargestPeaks(float bin_size, std::span<const int, kHistogramSize> histogram, float* peak_position,
                                int* peak_weight) {
  int peak_value = 0, secondary_peak_value = 0;
  *peak_position = 0.f;
  float secondary_peak_position = 0.f;
  *peak_weight = 0;
  int secondary_peak_weight = 0;
  for (int i = 0; i < kHistogramSize; ++i) {
    const float bin_mid = (i + 0.5f) * bin_size;
    if (histogram[i] > peak_value) {
      secondary_peak_value = peak_value;
      secondary_peak_weight = *peak_weight;
      secondary_peak_position = *peak_position;
      peak_value = histogram[i];
      *peak_weight = histogram[i];
      *peak_position = bin_mid;
    } else if (histogram[i] > secondary_peak_value) {
      secondary_peak_value = histogram[i];
      secondary_peak_weight = histogram[i];
      secondary_peak_position = bin_mid;
    }
  }
  // Merge the peaks if they are close.
  if ((std::fabs(secondary_peak_position - *peak_position) < 2 * bin_size) &&
      (secondary_peak_weight > 0.5f * (*peak_weight))) {
    *peak_weight += secondary_peak_weight;
    *peak_position = 0.5f * (*peak_position + secondary_peak_position);
  }
}

void UpdateLrt(std::span<const int, kHistogramSize> lrt_histogram, float* prior_model_lrt,
               bool* low_lrt_fluctuations) {
  float average = 0.f, average_compl = 0.f, average_squared = 0.f;
  int count = 0;
  for (int i = 0; i < 10; ++i) {
    float bin_mid = (i + 0.5f) * kBinSizeLrt;
    average += lrt_histogram[i] * bin_mid;
    count += lrt_histogram[i];
  }
  if (count > 0) average = average / count;
  for (int i = 0; i < kHistogramSize; ++i) {
    float bin_mid = (i + 0.5f) * kBinSizeLrt;
    average_squared += lrt_histogram[i] * bin_mid * bin_mid;
    average_compl += lrt_histogram[i] * bin_mid;
  }
  constexpr float kOneFeatureUpdateWindowSize = 1.f / kFeatureUpdateWindowSize;
  average_squared = average_squared * kOneFeatureUpdateWindowSize;
  average_compl = average_compl * kOneFeatureUpdateWindowSize;
  // Fluctuation limit of the LRT feature.
  *low_lrt_fluctuations = average_squared - average * average_compl < 0.05f;
  constexpr float kMaxLrt = 1.f;
  constexpr float kMinLrt = .2f;
  if (*low_lrt_fluctuations) {
    *prior_model_lrt = kMaxLrt;  // Very low fluctuation: likely noise.
  } else {
    *prior_model_lrt = std::min(kMaxLrt, std::max(kMinLrt, 1.2f * average));
  }
}
}  // namespace

void UpdatePriorSignalModel(const Histograms& histograms, PriorSignalModel* prior_model) {
  bool low_lrt_fluctuations;
  UpdateLrt(histograms.lrt(), &prior_model->lrt, &low_lrt_fluctuations);

  float spectral_flatness_peak_position;
  int spectral_flatness_peak_weight;
  FindFirstOfTwoLargestPeaks(kBinSizeSpecFlat, histograms.spectral_flatness(), &spectral_flatness_peak_position,
                             &spectral_flatness_peak_weight);
  float spectral_diff_peak_position = 0.f;
  int spectral_diff_peak_weight = 0;
  FindFirstOfTwoLargestPeaks(kBinSizeSpecDiff, histograms.spectral_diff(), &spectral_diff_peak_position,
                             &spectral_diff_peak_weight);

  // Reject a feature whose peak is too light or too low (flatness), or when
  // the LRT barely fluctuates, which indicates noise (difference).
  const int use_spec_flat =
      spectral_flatness_peak_weight < 0.3f * 500 || spectral_flatness_peak_position < 0.6f ? 0 : 1;
  const int use_spec_diff = spectral_diff_peak_weight < 0.3f * 500 || low_lrt_fluctuations ? 0 : 1;

  prior_model->template_diff_threshold = 1.2f * spectral_diff_peak_position;
  prior_model->template_diff_threshold = std::min(1.f, std::max(0.16f, prior_model->template_diff_threshold));

  float one_by_feature_sum = 1.f / (1.f + use_spec_flat + use_spec_diff);
  prior_model->lrt_weighting = one_by_feature_sum;
  if (use_spec_flat == 1) {
    prior_model->flatness_threshold = 0.9f * spectral_flatness_peak_position;
    prior_model->flatness_threshold = std::min(.95f, std::max(0.1f, prior_model->flatness_threshold));
    prior_model->flatness_weighting = one_by_feature_sum;
  } else {
    prior_model->flatness_weighting = 0.f;
  }
  prior_model->difference_weighting = use_spec_diff == 1 ? one_by_feature_sum : 0.f;
}

// signal_model_estimator.cc
namespace {
constexpr float kOneByFftSizeBy2Plus1 = 1.f / kFftSizeBy2Plus1;

// Difference between the input spectrum and the learnt noise spectrum:
// var(signal) - cov(signal, noise)^2 / var(noise), normalized.
float ComputeSpectralDiff(std::span<const float, kFftSizeBy2Plus1> conservative_noise_spectrum,
                          std::span<const float, kFftSizeBy2Plus1> signal_spectrum, float signal_spectral_sum,
                          float diff_normalization) {
  float noise_average = 0.f;
  for (size_t i = 0; i < kFftSizeBy2Plus1; ++i) noise_average += conservative_noise_spectrum[i];
  noise_average = noise_average * kOneByFftSizeBy2Plus1;
  float signal_average = signal_spectral_sum * kOneByFftSizeBy2Plus1;
  float covariance = 0.f, noise_variance = 0.f, signal_variance = 0.f;
  for (size_t i = 0; i < kFftSizeBy2Plus1; ++i) {
    float signal_diff = signal_spectrum[i] - signal_average;
    float noise_diff = conservative_noise_spectrum[i] - noise_average;
    covariance += signal_diff * noise_diff;
    noise_variance += noise_diff * noise_diff;
    signal_variance += signal_diff * signal_diff;
  }
  covariance *= kOneByFftSizeBy2Plus1;
  noise_variance *= kOneByFftSizeBy2Plus1;
  signal_variance *= kOneByFftSizeBy2Plus1;
  float spectral_diff = signal_variance - (covariance * covariance) / (noise_variance + 0.0001f);
  return spectral_diff / (diff_normalization + 0.0001f);
}

// Log of the ratio of the geometric to the arithmetic mean, time-averaged.
void UpdateSpectralFlatness(std::span<const float, kFftSizeBy2Plus1> signal_spectrum, float signal_spectral_sum,
                            float* spectral_flatness) {
  constexpr float kAveraging = 0.3f;
  for (size_t i = 1; i < kFftSizeBy2Plus1; ++i) {
    if (signal_spectrum[i] == 0.f) {
      *spectral_flatness -= kAveraging * (*spectral_flatness);
      return;
    }
  }
  float avg_spect_flatness_num = 0.f;
  for (size_t i = 1; i < kFftSizeBy2Plus1; ++i) avg_spect_flatness_num += LogApproximation(signal_spectrum[i]);
  float avg_spect_flatness_denom = signal_spectral_sum - signal_spectrum[0];
  avg_spect_flatness_denom = avg_spect_flatness_denom * kOneByFftSizeBy2Plus1;
  avg_spect_flatness_num = avg_spect_flatness_num * kOneByFftSizeBy2Plus1;
  float spectral_tmp = ExpApproximation(avg_spect_flatness_num) / avg_spect_flatness_denom;
  *spectral_flatness += kAveraging * (spectral_tmp - *spectral_flatness);
}

void UpdateSpectralLrt(std::span<const float, kFftSizeBy2Plus1> prior_snr,
                       std::span<const float, kFftSizeBy2Plus1> post_snr, std::span<float, kFftSizeBy2Plus1> avg_log_lrt,
                       float* lrt) {
  for (size_t i = 0; i < kFftSizeBy2Plus1; ++i) {
    float tmp1 = 1.f + 2.f * prior_snr[i];
    float tmp2 = 2.f * prior_snr[i] / (tmp1 + 0.0001f);
    float bessel_tmp = (post_snr[i] + 1.f) * tmp2;
    avg_log_lrt[i] += .5f * (bessel_tmp - LogApproximation(tmp1) - avg_log_lrt[i]);
  }
  float log_lrt_time_avg_k_sum = 0.f;
  for (size_t i = 0; i < kFftSizeBy2Plus1; ++i) log_lrt_time_avg_k_sum += avg_log_lrt[i];
  *lrt = log_lrt_time_avg_k_sum * kOneByFftSizeBy2Plus1;
}
}  // namespace

void SignalModelEstimator::AdjustNormalization(int32_t num_analyzed_frames, float signal_energy) {
  diff_normalization_ *= num_analyzed_frames;
  diff_normalization_ += signal_energy;
  diff_normalization_ /= (num_analyzed_frames + 1);
}

void SignalModelEstimator::Update(std::span<const float, kFftSizeBy2Plus1> prior_snr,
                                  std::span<const float, kFftSizeBy2Plus1> post_snr,
                                  std::span<const float, kFftSizeBy2Plus1> conservative_noise_spectrum,
                                  std::span<const float, kFftSizeBy2Plus1> signal_spectrum, float signal_spectral_sum,
                                  float signal_energy) {
  UpdateSpectralFlatness(signal_spectrum, signal_spectral_sum, &features_.spectral_flatness);
  float spectral_diff =
      ComputeSpectralDiff(conservative_noise_spectrum, signal_spectrum, signal_spectral_sum, diff_normalization_);
  features_.spectral_diff += 0.3f * (spectral_diff - features_.spectral_diff);
  signal_energy_sum_ += signal_energy;

  // Thresholds and weights for the features are re-learnt every window.
  if (--histogram_analysis_counter_ > 0) {
    histograms_.Update(features_);
  } else {
    UpdatePriorSignalModel(histograms_, &prior_model_);
    histograms_.Clear();
    histogram_analysis_counter_ = kFeatureUpdateWindowSize;
    // Normalization of the spectral difference for the next window.
    signal_energy_sum_ = signal_energy_sum_ / kFeatureUpdateWindowSize;
    diff_normalization_ = 0.5f * (signal_energy_sum_ + diff_normalization_);
    signal_energy_sum_ = 0.f;
  }
  UpdateSpectralLrt(prior_snr, post_snr, features_.avg_log_lrt, &features_.lrt);
}

// speech_probability_estimator.cc
void SpeechProbabilityEstimator::Update(int32_t num_analyzed_frames, std::span<const float, kFftSizeBy2Plus1> prior_snr,
                                        std::span<const float, kFftSizeBy2Plus1> post_snr,
                                        std::span<const float, kFftSizeBy2Plus1> conservative_noise_spectrum,
                                        std::span<const float, kFftSizeBy2Plus1> signal_spectrum,
                                        float signal_spectral_sum, float signal_energy) {
  if (num_analyzed_frames < kLongStartupPhaseBlocks) {
    signal_model_estimator_.AdjustNormalization(num_analyzed_frames, signal_energy);
  }
  signal_model_estimator_.Update(prior_snr, post_snr, conservative_noise_spectrum, signal_spectrum,
                                 signal_spectral_sum, signal_energy);
  const SignalModel& model = signal_model_estimator_.model();
  const PriorSignalModel& prior_model = signal_model_estimator_.prior_model();

  // Sigmoid maps of the features; wider in pause regions.
  constexpr float kWidthPrior0 = 4.f;
  constexpr float kWidthPrior1 = 2.f * kWidthPrior0;
  float width_prior = model.lrt < prior_model.lrt ? kWidthPrior1 : kWidthPrior0;
  float indicator0 = 0.5f * (std::tanh(width_prior * (model.lrt - prior_model.lrt)) + 1.f);
  width_prior = model.spectral_flatness > prior_model.flatness_threshold ? kWidthPrior1 : kWidthPrior0;
  float indicator1 =
      0.5f * (std::tanh(1.f * width_prior * (prior_model.flatness_threshold - model.spectral_flatness)) + 1.f);
  width_prior = model.spectral_diff < prior_model.template_diff_threshold ? kWidthPrior1 : kWidthPrior0;
  float indicator2 =
      0.5f * (std::tanh(width_prior * (model.spectral_diff - prior_model.template_diff_threshold)) + 1.f);

  float ind_prior = prior_model.lrt_weighting * indicator0 + prior_model.flatness_weighting * indicator1 +
                    prior_model.difference_weighting * indicator2;
  prior_speech_prob_ += 0.1f * (ind_prior - prior_speech_prob_);
  prior_speech_prob_ = std::max(std::min(prior_speech_prob_, 1.f), 0.01f);

  // Combine the prior with the LR factor per bin.
  float gain_prior = (1.f - prior_speech_prob_) / (prior_speech_prob_ + 0.0001f);
  for (size_t i = 0; i < kFftSizeBy2Plus1; ++i) {
    float inv_lrt = ExpApproximation(-model.avg_log_lrt[i]);
    speech_probability_[i] = 1.f / (1.f + gain_prior * inv_lrt);
  }
}

// wiener_filter.cc
WienerFilter::WienerFilter(const SuppressionParams& params) : params_(params) { filter_.fill(1.f); }

void WienerFilter::Update(int32_t num_analyzed_frames, std::span<const float, kFftSizeBy2Plus1> noise_spectrum,
                          std::span<const float, kFftSizeBy2Plus1> prev_noise_spectrum,
                          std::span<const float, kFftSizeBy2Plus1> parametric_noise_spectrum,
                          std::span<const float, kFftSizeBy2Plus1> signal_spectrum) {
  for (size_t i = 0; i < kFftSizeBy2Plus1; ++i) {
    // Previous estimate, from the previous frame with the gain filter.
    float prev_tsa = spectrum_prev_process_[i] / (prev_noise_spectrum[i] + 0.0001f) * filter_[i];
    float current_tsa = signal_spectrum[i] > noise_spectrum[i]
                            ? signal_spectrum[i] / (noise_spectrum[i] + 0.0001f) - 1.f
                            : 0.f;
    // Decision-directed estimate.
    float snr_prior = 0.98f * prev_tsa + (1.f - 0.98f) * current_tsa;
    filter_[i] = snr_prior / (params_.over_subtraction_factor + snr_prior);
    filter_[i] = std::max(std::min(filter_[i], 1.f), params_.minimum_attenuating_gain);
  }
  if (num_analyzed_frames < kShortStartupPhaseBlocks) {
    constexpr float kOneByShortStartupPhaseBlocks = 1.f / kShortStartupPhaseBlocks;
    for (size_t i = 0; i < kFftSizeBy2Plus1; ++i) {
      initial_spectral_estimate_[i] += signal_spectrum[i];
      float filter_initial =
          initial_spectral_estimate_[i] - params_.over_subtraction_factor * parametric_noise_spectrum[i];
      filter_initial /= initial_spectral_estimate_[i] + 0.0001f;
      filter_initial = std::max(std::min(filter_initial, 1.f), params_.minimum_attenuating_gain);
      // Weight the two suppression filters.
      filter_initial *= kShortStartupPhaseBlocks - num_analyzed_frames;
      filter_[i] *= num_analyzed_frames;
      filter_[i] += filter_initial;
      filter_[i] *= kOneByShortStartupPhaseBlocks;
    }
  }
  std::copy(signal_spectrum.begin(), signal_spectrum.end(), spectrum_prev_process_.begin());
}

float WienerFilter::ComputeOverallScalingFactor(int32_t num_analyzed_frames, float prior_speech_probability,
                                                float energy_before_filtering, float energy_after_filtering) const {
  if (!params_.use_attenuation_adjustment || num_analyzed_frames <= kLongStartupPhaseBlocks) return 1.f;
  float gain = SqrtFastApproximation(energy_after_filtering / (energy_before_filtering + 1.f));
  constexpr float kBLim = 0.5f;  // Threshold in the final energy gain.
  float scale_factor1 = 1.f;
  if (gain > kBLim) {
    scale_factor1 = 1.f + 1.3f * (gain - kBLim);
    if (gain * scale_factor1 > 1.f) scale_factor1 = 1.f / gain;
  }
  float scale_factor2 = 1.f;
  if (gain < kBLim) {
    // Pauses are left to the flooring.
    gain = std::max(gain, params_.minimum_attenuating_gain);
    scale_factor2 = 1.f - 0.3f * (kBLim - gain);
  }
  return prior_speech_probability * scale_factor1 + (1.f - prior_speech_probability) * scale_factor2;
}

// noise_suppressor.cc
namespace {
constexpr float kToFloatS16 = 32768.f;

// Hybrid Hann and flat window for the filter bank.
constexpr std::array<float, 96> kBlocks160w256FirstHalf = {
    0.00000000f, 0.01636173f, 0.03271908f, 0.04906767f, 0.06540313f, 0.08172107f, 0.09801714f, 0.11428696f,
    0.13052619f, 0.14673047f, 0.16289547f, 0.17901686f, 0.19509032f, 0.21111155f, 0.22707626f, 0.24298018f,
    0.25881905f, 0.27458862f, 0.29028468f, 0.30590302f, 0.32143947f, 0.33688985f, 0.35225005f, 0.36751594f,
    0.38268343f, 0.39774847f, 0.41270703f, 0.42755509f, 0.44228869f, 0.45690388f, 0.47139674f, 0.48576339f,
    0.50000000f, 0.51410274f, 0.52806785f, 0.54189158f, 0.55557023f, 0.56910015f, 0.58247770f, 0.59569930f,
    0.60876143f, 0.62166057f, 0.63439328f, 0.64695615f, 0.65934582f, 0.67155895f, 0.68359230f, 0.69544264f,
    0.70710678f, 0.71858162f, 0.72986407f, 0.74095113f, 0.75183981f, 0.76252720f, 0.77301045f, 0.78328675f,
    0.79335334f, 0.80320753f, 0.81284668f, 0.82226822f, 0.83146961f, 0.84044840f, 0.84920218f, 0.85772861f,
    0.86602540f, 0.87409034f, 0.88192126f, 0.88951608f, 0.89687274f, 0.90398929f, 0.91086382f, 0.91749450f,
    0.92387953f, 0.93001722f, 0.93590593f, 0.94154407f, 0.94693013f, 0.95206268f, 0.95694034f, 0.96156180f,
    0.96592583f, 0.97003125f, 0.97387698f, 0.97746197f, 0.98078528f, 0.98384601f, 0.98664333f, 0.98917651f,
    0.99144486f, 0.99344778f, 0.99518473f, 0.99665524f, 0.99785892f, 0.99879546f, 0.99946459f, 0.99986614f};

void ApplyFilterBankWindow(std::span<float, kFftSize> x) {
  for (size_t i = 0; i < 96; ++i) x[i] = kBlocks160w256FirstHalf[i] * x[i];
  for (size_t i = 161, k = 95; i < kFftSize; ++i, --k) x[i] = kBlocks160w256FirstHalf[k] * x[i];
}

// Extends a frame with the previous data.
void FormExtendedFrame(std::span<const float, kNsFrameSize> frame, std::span<float, kOverlapSize> old_data,
                       std::span<float, kFftSize> extended_frame) {
  std::copy(old_data.begin(), old_data.end(), extended_frame.begin());
  std::copy(frame.begin(), frame.end(), extended_frame.begin() + old_data.size());
  std::copy(extended_frame.end() - old_data.size(), extended_frame.end(), old_data.begin());
}

void OverlapAndAdd(std::span<const float, kFftSize> extended_frame, std::span<float, kOverlapSize> overlap_memory,
                   std::span<float, kNsFrameSize> output_frame) {
  for (size_t i = 0; i < kOverlapSize; ++i) output_frame[i] = overlap_memory[i] + extended_frame[i];
  std::copy(extended_frame.begin() + kOverlapSize, extended_frame.begin() + kNsFrameSize,
            output_frame.begin() + kOverlapSize);
  std::copy(extended_frame.begin() + kNsFrameSize, extended_frame.end(), overlap_memory.begin());
}

void DelaySignal(std::span<const float, kNsFrameSize> frame, std::span<float, kOverlapSize> delay_buffer,
                 std::span<float, kNsFrameSize> delayed_frame) {
  constexpr size_t kSamplesFromFrame = kNsFrameSize - kOverlapSize;
  std::copy(delay_buffer.begin(), delay_buffer.end(), delayed_frame.begin());
  std::copy(frame.begin(), frame.begin() + kSamplesFromFrame, delayed_frame.begin() + delay_buffer.size());
  std::copy(frame.begin() + kSamplesFromFrame, frame.end(), delay_buffer.begin());
}

float Energy(std::span<const float> x) {
  float energy = 0.f;
  for (float v : x) energy += v * v;
  return energy;
}

void ComputeMagnitudeSpectrum(std::span<const float, kFftSize> real, std::span<const float, kFftSize> imag,
                              std::span<float, kFftSizeBy2Plus1> signal_spectrum) {
  signal_spectrum[0] = std::fabs(real[0]) + 1.f;
  signal_spectrum[kFftSizeBy2Plus1 - 1] = std::fabs(real[kFftSizeBy2Plus1 - 1]) + 1.f;
  for (size_t i = 1; i < kFftSizeBy2Plus1 - 1; ++i) {
    signal_spectrum[i] = SqrtFastApproximation(real[i] * real[i] + imag[i] * imag[i]) + 1.f;
  }
}

// Prior and post SNR.
void ComputeSnr(std::span<const float, kFftSizeBy2Plus1> filter, std::span<const float> prev_signal_spectrum,
                std::span<const float> signal_spectrum, std::span<const float> prev_noise_spectrum,
                std::span<const float> noise_spectrum, std::span<float> prior_snr, std::span<float> post_snr) {
  for (size_t i = 0; i < kFftSizeBy2Plus1; ++i) {
    float prev_estimate = prev_signal_spectrum[i] / (prev_noise_spectrum[i] + 0.0001f) * filter[i];
    post_snr[i] = signal_spectrum[i] > noise_spectrum[i] ? signal_spectrum[i] / (noise_spectrum[i] + 0.0001f) - 1.f
                                                         : 0.f;
    prior_snr[i] = 0.98f * prev_estimate + (1.f - 0.98f) * post_snr[i];
  }
}

// The gain for the upper bands, from the top of the lowest band.
float ComputeUpperBandsGain(float minimum_attenuating_gain, std::span<const float, kFftSizeBy2Plus1> filter,
                            std::span<const float> speech_probability,
                            std::span<const float, kFftSizeBy2Plus1> prev_analysis_signal_spectrum,
                            std::span<const float, kFftSizeBy2Plus1> signal_spectrum) {
  constexpr int kNumAvgBins = 32;
  constexpr float kOneByNumAvgBins = 1.f / kNumAvgBins;
  float avg_prob_speech = 0.f, avg_filter_gain = 0.f;
  for (size_t i = kFftSizeBy2Plus1 - kNumAvgBins - 1; i < kFftSizeBy2Plus1 - 1; i++) {
    avg_prob_speech += speech_probability[i];
    avg_filter_gain += filter[i];
  }
  avg_prob_speech = avg_prob_speech * kOneByNumAvgBins;
  avg_filter_gain = avg_filter_gain * kOneByNumAvgBins;
  // Speech that a module between Analyze and Process removed (the AEC) is
  // not speech for the upper bands.
  float sum_analysis_spectrum = 0.f, sum_processing_spectrum = 0.f;
  for (size_t i = 0; i < kFftSizeBy2Plus1; ++i) {
    sum_analysis_spectrum += prev_analysis_signal_spectrum[i];
    sum_processing_spectrum += signal_spectrum[i];
  }
  avg_prob_speech *= sum_processing_spectrum / sum_analysis_spectrum;
  float gain = 0.5f * (1.f + std::tanh(2.f * avg_prob_speech - 1.f));
  if (avg_prob_speech >= 0.5f) {
    gain = 0.25f * gain + 0.75f * avg_filter_gain;
  } else {
    gain = 0.5f * gain + 0.5f * avg_filter_gain;
  }
  return std::min(std::max(gain, minimum_attenuating_gain), 1.f);
}

size_t NumBandsForRate(int sample_rate) { return sample_rate >= 48000 ? 3 : sample_rate >= 32000 ? 2 : 1; }
}  // namespace

NoiseSuppressor::ChannelState::ChannelState(const SuppressionParams& params, size_t num_bands)
    : wiener_filter(params), noise_estimator(params), process_delay_memory(num_bands > 1 ? num_bands - 1 : 0) {
  prev_analysis_signal_spectrum.fill(1.f);
  for (auto& d : process_delay_memory) d.fill(0.f);
}

NoiseSuppressor::NoiseSuppressor(NoiseSuppressorConfig cfg)
    : cfg_(cfg),
      num_bands_(NumBandsForRate(cfg.sample_rate)),
      params_(cfg.level),
      filter_bank_states_(std::max<size_t>(cfg.num_channels, 1)),
      upper_band_gains_(std::max<size_t>(cfg.num_channels, 1)),
      energies_before_filtering_(std::max<size_t>(cfg.num_channels, 1)),
      gain_adjustments_(std::max<size_t>(cfg.num_channels, 1)) {
  for (size_t ch = 0; ch < std::max<size_t>(cfg.num_channels, 1); ++ch) {
    channels_.push_back(std::make_unique<ChannelState>(params_, num_bands_));
  }
  // NrFft::NrFft: ip[0] = 0 makes the first Rdft build the tables.
  bit_reversal_state_[0] = 0;
  std::array<float, kFftSize> tmp_buffer;
  tmp_buffer.fill(0.f);
  ooura::Rdft(kFftSize, 1, tmp_buffer.data(), bit_reversal_state_.data(), tables_.data());
}

void NoiseSuppressor::Fft(std::span<const float, kFftSize> time, std::span<float, kFftSize> real,
                          std::span<float, kFftSize> imag) {
  std::array<float, kFftSize> time_data;
  std::copy(time.begin(), time.end(), time_data.begin());
  ooura::Rdft(kFftSize, 1, time_data.data(), bit_reversal_state_.data(), tables_.data());

  imag[0] = 0;
  real[0] = time_data[0];

  imag[kFftSizeBy2Plus1 - 1] = 0;
  real[kFftSizeBy2Plus1 - 1] = time_data[1];

  for (size_t i = 1; i < kFftSizeBy2Plus1 - 1; ++i) {
    real[i] = time_data[2 * i];
    imag[i] = time_data[2 * i + 1];
  }
}

void NoiseSuppressor::Ifft(std::span<const float, kFftSize> real, std::span<const float, kFftSize> imag,
                           std::span<float, kFftSize> time) {
  time[0] = real[0];
  time[1] = real[kFftSizeBy2Plus1 - 1];
  for (size_t i = 1; i < kFftSizeBy2Plus1 - 1; ++i) {
    time[2 * i] = real[i];
    time[2 * i + 1] = imag[i];
  }
  ooura::Rdft(kFftSize, -1, time.data(), bit_reversal_state_.data(), tables_.data());

  constexpr float kScaling = 2.f / kFftSize;
  for (float& d : time) d *= kScaling;
}

void NoiseSuppressor::Analyze(std::span<const float> bands) {
  const size_t num_channels = channels_.size();
  for (auto& ch : channels_) ch->noise_estimator.PrepareAnalysis();

  std::array<float, kNsFrameSize> y_band0;
  auto band0 = [&](size_t ch) {
    for (size_t i = 0; i < kNsFrameSize; ++i) y_band0[i] = bands[ch * frame_samples() + i] * kToFloatS16;
  };

  // Zero frames would move the thresholds towards silence, so that speech
  // looks like everything once the signal starts; they are not learnt from.
  bool zero_frame = true;
  for (size_t ch = 0; ch < num_channels && zero_frame; ++ch) {
    band0(ch);
    if (Energy(channels_[ch]->analyze_analysis_memory) + Energy(y_band0) > 0.f) zero_frame = false;
  }
  if (zero_frame) return;

  if (++num_analyzed_frames_ < 0) num_analyzed_frames_ = 0;
  ++stats_.analyzed_frames;

  for (size_t ch = 0; ch < num_channels; ++ch) {
    ChannelState& c = *channels_[ch];
    band0(ch);
    std::array<float, kFftSize> extended_frame;
    FormExtendedFrame(y_band0, c.analyze_analysis_memory, extended_frame);
    ApplyFilterBankWindow(extended_frame);

    std::array<float, kFftSize> real, imag;
    Fft(extended_frame, real, imag);
    Spectrum signal_spectrum;
    ComputeMagnitudeSpectrum(real, imag, signal_spectrum);

    float signal_energy = 0.f;
    for (size_t i = 0; i < kFftSizeBy2Plus1; ++i) signal_energy += real[i] * real[i] + imag[i] * imag[i];
    signal_energy /= kFftSizeBy2Plus1;
    float signal_spectral_sum = 0.f;
    for (size_t i = 0; i < kFftSizeBy2Plus1; ++i) signal_spectral_sum += signal_spectrum[i];

    // Noise spectra and the speech presence probability.
    c.noise_estimator.PreUpdate(num_analyzed_frames_, signal_spectrum, signal_spectral_sum);
    Spectrum post_snr, prior_snr;
    ComputeSnr(c.wiener_filter.filter(), c.prev_analysis_signal_spectrum, signal_spectrum,
               c.noise_estimator.prev_noise_spectrum(), c.noise_estimator.noise_spectrum(), prior_snr, post_snr);
    c.speech_probability_estimator.Update(num_analyzed_frames_, prior_snr, post_snr,
                                          c.noise_estimator.conservative_noise_spectrum(), signal_spectrum,
                                          signal_spectral_sum, signal_energy);
    c.noise_estimator.PostUpdate(c.speech_probability_estimator.probability(), signal_spectrum);
    c.prev_analysis_signal_spectrum = signal_spectrum;
  }
  stats_.prior_speech_probability = channels_[0]->speech_probability_estimator.prior_probability();
}

void NoiseSuppressor::Process(std::span<float> bands) {
  const size_t num_channels = channels_.size();

  // The suppression filter of every channel.
  for (size_t ch = 0; ch < num_channels; ++ch) {
    ChannelState& c = *channels_[ch];
    FilterBankState& st = filter_bank_states_[ch];
    for (size_t i = 0; i < kNsFrameSize; ++i) st.y_band0[i] = bands[ch * frame_samples() + i] * kToFloatS16;
    FormExtendedFrame(st.y_band0, c.process_analysis_memory, st.extended_frame);
    ApplyFilterBankWindow(st.extended_frame);
    energies_before_filtering_[ch] = Energy(st.extended_frame);

    Fft(st.extended_frame, st.real, st.imag);
    Spectrum signal_spectrum;
    ComputeMagnitudeSpectrum(st.real, st.imag, signal_spectrum);

    // The frequency-domain gain filter.
    c.wiener_filter.Update(num_analyzed_frames_, c.noise_estimator.noise_spectrum(),
                           c.noise_estimator.prev_noise_spectrum(), c.noise_estimator.parametric_noise_spectrum(),
                           signal_spectrum);
    if (num_bands_ > 1) {
      upper_band_gains_[ch] =
          ComputeUpperBandsGain(params_.minimum_attenuating_gain, c.wiener_filter.filter(),
                                c.speech_probability_estimator.probability(), c.prev_analysis_signal_spectrum,
                                signal_spectrum);
    }
  }

  // One filter for all channels: the minimum per bin (AggregateWienerFilters).
  Spectrum filter = channels_[0]->wiener_filter.filter();
  for (size_t ch = 1; ch < num_channels; ++ch) {
    const Spectrum& filter_ch = channels_[ch]->wiener_filter.filter();
    for (size_t k = 0; k < kFftSizeBy2Plus1; ++k) filter[k] = std::min(filter[k], filter_ch[k]);
  }
  float mean_gain = 0.f;
  for (size_t i = 0; i < kFftSizeBy2Plus1; ++i) mean_gain += filter[i];
  mean_gain /= kFftSizeBy2Plus1;

  // Synthesis.
  for (size_t ch = 0; ch < num_channels; ++ch) {
    FilterBankState& st = filter_bank_states_[ch];
    for (size_t i = 0; i < kFftSizeBy2Plus1; ++i) {
      st.real[i] *= filter[i];
      st.imag[i] *= filter[i];
    }
    Ifft(st.real, st.imag, st.extended_frame);
    const float energy_after_filtering = Energy(st.extended_frame);
    ApplyFilterBankWindow(st.extended_frame);
    gain_adjustments_[ch] = channels_[ch]->wiener_filter.ComputeOverallScalingFactor(
        num_analyzed_frames_, channels_[ch]->speech_probability_estimator.prior_probability(),
        energies_before_filtering_[ch], energy_after_filtering);
  }
  float gain_adjustment = gain_adjustments_[0];
  for (size_t ch = 1; ch < num_channels; ++ch) gain_adjustment = std::min(gain_adjustment, gain_adjustments_[ch]);
  float upper_band_gain = 1.f;
  if (num_bands_ > 1) {
    upper_band_gain = upper_band_gains_[0];
    for (size_t ch = 1; ch < num_channels; ++ch) upper_band_gain = std::min(upper_band_gain, upper_band_gains_[ch]);
  }

  for (size_t ch = 0; ch < num_channels; ++ch) {
    ChannelState& c = *channels_[ch];
    FilterBankState& st = filter_bank_states_[ch];
    std::span<float> out_ch = bands.subspan(ch * frame_samples(), frame_samples());
    for (float& v : st.extended_frame) v *= gain_adjustment;
    OverlapAndAdd(st.extended_frame, c.process_synthesis_memory, st.y_band0);

    // Limit to the S16 range and back to this library's scale.
    for (size_t i = 0; i < kNsFrameSize; ++i) {
      out_ch[i] = std::min(std::max(st.y_band0[i], -32768.f), 32767.f) / kToFloatS16;
    }

    // Upper bands: delayed as the filter bank delays the lowest band, then
    // scaled by one gain.
    for (size_t b = 1; b < num_bands_; ++b) {
      std::array<float, kNsFrameSize> y_band, delayed_frame;
      std::span<float> out = out_ch.subspan(b * kNsFrameSize, kNsFrameSize);
      for (size_t i = 0; i < kNsFrameSize; ++i) y_band[i] = out[i] * kToFloatS16;
      DelaySignal(y_band, c.process_delay_memory[b - 1], delayed_frame);
      for (size_t i = 0; i < kNsFrameSize; ++i) {
        out[i] = std::min(std::max(upper_band_gain * delayed_frame[i], -32768.f), 32767.f) / kToFloatS16;
      }
    }
  }

  ++stats_.processed_frames;
  stats_.last_mean_gain_db = dsp::LinearToDb(mean_gain);
  stats_.last_upper_band_gain = upper_band_gain;
  stats_.last_gain_adjustment = gain_adjustment;
  stats_.gain_db.Add(-stats_.last_mean_gain_db);
}

void NoiseSuppressor::ExportMetrics(MetricsWriter& w) const {
  w.Counter("analyzed_frames", stats_.analyzed_frames);
  w.Counter("frames", stats_.processed_frames);
  w.Gauge("mean_gain_db", stats_.last_mean_gain_db);
  w.Gauge("upper_band_gain", stats_.last_upper_band_gain);
  w.Gauge("gain_adjustment", stats_.last_gain_adjustment);
  w.Gauge("prior_speech_probability", stats_.prior_speech_probability);
  w.Hist("attenuation_db", stats_.gain_db);
}

}  // namespace sm::audio::ns
