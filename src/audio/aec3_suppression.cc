// AEC3's residual echo suppression: compare/webrtc/reference/modules/
// audio_processing/aec3/{moving_average_spectrum,dominant_nearend_detector,
// subband_nearend_detector,residual_echo_estimator,suppression_gain,
// suppression_filter,comfort_noise_generator}.cc (M157). See
// sm/audio/aec3_suppression.h.
#include "sm/audio/aec3_suppression.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <numeric>

namespace sm::audio::aec3 {
namespace {

// VectorMath::Sqrt, the portable path.
void SqrtInPlace(std::span<float> x) {
  for (auto& v : x) v = std::sqrt(v);
}

float LowFrequencyEnergy(std::span<const float> spectrum) {
  return std::accumulate(spectrum.begin() + 1, spectrum.begin() + 16, 0.f);
}

}  // namespace

// --- moving_average_spectrum.cc ------------------------------------------------

MovingAverageSpectrum::MovingAverageSpectrum(size_t num_elem, size_t mem_len)
    : num_elem_(num_elem), mem_len_(mem_len - 1), memory_(num_elem * mem_len_, 0.f) {}

void MovingAverageSpectrum::Average(std::span<const float> input, std::span<float> output) {
  // Sum all contributions.
  std::copy(input.begin(), input.end(), output.begin());
  for (auto i = memory_.begin(); i < memory_.end(); i += static_cast<std::ptrdiff_t>(num_elem_)) {
    std::transform(i, i + static_cast<std::ptrdiff_t>(num_elem_), output.begin(), output.begin(), std::plus<float>());
  }

  // Divide by the number of points used to compute the average.
  const float scaling = 1.0f / static_cast<float>(number_updates_ + 1);
  for (float& o : output) o *= scaling;

  // Update memory.
  if (mem_len_ > 0) {
    std::copy(input.begin(), input.end(), memory_.begin() + static_cast<std::ptrdiff_t>(mem_index_ * num_elem_));
    mem_index_ = (mem_index_ + 1) % mem_len_;
  }
  number_updates_ = std::min(static_cast<int>(mem_len_), number_updates_ + 1);
}

void MovingAverageSpectrum::UpdateMemoryLength(size_t mem_len) {
  if (mem_len_ + 1 == mem_len) return;
  mem_len_ = mem_len - 1;
  memory_.resize(num_elem_ * mem_len_);
  std::fill(memory_.begin(), memory_.end(), 0.0f);
  mem_index_ = 0;
  number_updates_ = 0;
}

// --- dominant_nearend_detector.cc ----------------------------------------------

DominantNearendDetector::DominantNearendDetector(const SuppressorConfig::DominantNearendDetection& config,
                                                 size_t num_capture_channels)
    : config_(config),
      num_capture_channels_(num_capture_channels),
      trigger_counters_(num_capture_channels_),
      hold_counters_(num_capture_channels_) {}

void DominantNearendDetector::Update(std::span<const std::array<float, kFftLengthBy2Plus1>> nearend_spectrum,
                                     std::span<const std::array<float, kFftLengthBy2Plus1>> residual_echo_spectrum,
                                     std::span<const std::array<float, kFftLengthBy2Plus1>> comfort_noise_spectrum,
                                     bool initial_state) {
  nearend_state_ = false;
  for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
    const float ne_sum = LowFrequencyEnergy(nearend_spectrum[ch]);
    const float echo_sum = LowFrequencyEnergy(residual_echo_spectrum[ch]);
    const float noise_sum = LowFrequencyEnergy(comfort_noise_spectrum[ch]);

    // Detect strong active nearend if the nearend is sufficiently stronger
    // than the echo and the nearend noise.
    if ((!initial_state || config_.use_during_initial_phase) && echo_sum < config_.enr_threshold * ne_sum &&
        ne_sum > config_.snr_threshold * noise_sum) {
      if (++trigger_counters_[ch] >= config_.trigger_threshold) {
        // After a period of strong active nearend activity, flag nearend mode.
        hold_counters_[ch] = config_.hold_duration;
        trigger_counters_[ch] = config_.trigger_threshold;
      }
    } else {
      // Forget previously detected strong active nearend activity.
      trigger_counters_[ch] = std::max(0, trigger_counters_[ch] - 1);
    }

    // Exit nearend-state early at strong echo.
    if (echo_sum > config_.enr_exit_threshold * ne_sum && echo_sum > config_.snr_threshold * noise_sum) {
      hold_counters_[ch] = 0;
    }

    // Remain in any nearend mode for a certain duration.
    hold_counters_[ch] = std::max(0, hold_counters_[ch] - 1);
    nearend_state_ = nearend_state_ || hold_counters_[ch] > 0;
  }
}

// --- subband_nearend_detector.cc -----------------------------------------------

SubbandNearendDetector::SubbandNearendDetector(const SuppressorConfig& config, size_t num_capture_channels)
    : num_capture_channels_(num_capture_channels),
      nearend_smoothers_(num_capture_channels_,
                         MovingAverageSpectrum(kFftLengthBy2Plus1,
                                               config.subband_nearend_detection.nearend_average_blocks)) {
  SetConfig(config);
}

void SubbandNearendDetector::Update(std::span<const std::array<float, kFftLengthBy2Plus1>> nearend_spectrum,
                                    std::span<const std::array<float, kFftLengthBy2Plus1>> /*residual_echo_spectrum*/,
                                    std::span<const std::array<float, kFftLengthBy2Plus1>> comfort_noise_spectrum,
                                    bool /*initial_state*/) {
  nearend_state_ = false;
  const auto sum = [](const std::array<float, kFftLengthBy2Plus1>& v, size_t low, size_t high) {
    return std::accumulate(v.begin() + static_cast<std::ptrdiff_t>(low),
                           v.begin() + static_cast<std::ptrdiff_t>(high) + 1, 0.f);
  };
  for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
    const std::array<float, kFftLengthBy2Plus1>& noise = comfort_noise_spectrum[ch];
    std::array<float, kFftLengthBy2Plus1> nearend;
    nearend_smoothers_[ch].Average(nearend_spectrum[ch], nearend);

    // Noise power of the first region.
    float noise_power = sum(noise, config_.subband1.low, config_.subband1.high) * one_over_subband_length1_;
    // Nearend power of the first region.
    float nearend_power_subband1 =
        sum(nearend, config_.subband1.low, config_.subband1.high) * one_over_subband_length1_;
    // Nearend power of the second region.
    float nearend_power_subband2 =
        sum(nearend, config_.subband2.low, config_.subband2.high) * one_over_subband_length2_;

    // One channel is sufficient to trigger nearend state.
    nearend_state_ = nearend_state_ || (nearend_power_subband1 < config_.nearend_threshold * nearend_power_subband2 &&
                                        (nearend_power_subband1 > config_.snr_threshold * noise_power));
  }
}

void SubbandNearendDetector::SetConfig(const SuppressorConfig& config) {
  config_ = config.subband_nearend_detection;
  one_over_subband_length1_ = 1.f / static_cast<float>(config_.subband1.high - config_.subband1.low + 1);
  one_over_subband_length2_ = 1.f / static_cast<float>(config_.subband2.high - config_.subband2.low + 1);
  for (auto& smoother : nearend_smoothers_) smoother.UpdateMemoryLength(config_.nearend_average_blocks);
}

// --- residual_echo_estimator.cc ------------------------------------------------

namespace {

constexpr float kDefaultTransparentModeGain = 0.01f;

// Computes the indexes that will be used for computing spectral power over
// the blocks surrounding the delay.
void GetRenderIndexesToAnalyze(const SpectrumBuffer& spectrum_buffer, const EchoModelConfig& echo_model,
                               int filter_delay_blocks, int* idx_start, int* idx_stop) {
  size_t window_start = static_cast<size_t>(
      std::max(0, filter_delay_blocks - static_cast<int>(echo_model.render_pre_window_size)));
  size_t window_end = static_cast<size_t>(filter_delay_blocks + static_cast<int>(echo_model.render_post_window_size));
  *idx_start = spectrum_buffer.OffsetIndex(spectrum_buffer.read, static_cast<int>(window_start));
  *idx_stop = spectrum_buffer.OffsetIndex(spectrum_buffer.read, static_cast<int>(window_end + 1));
}

// Estimates the residual echo power based on the ERLE and the linear power
// estimate.
void LinearEstimate(std::span<const std::array<float, kFftLengthBy2Plus1>> S2_linear,
                    std::span<const std::array<float, kFftLengthBy2Plus1>> erle,
                    std::span<std::array<float, kFftLengthBy2Plus1>> R2) {
  for (size_t ch = 0; ch < R2.size(); ++ch) {
    for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) R2[ch][k] = S2_linear[ch][k] / erle[ch][k];
  }
}

// Estimates the residual echo power based on the estimate of the echo path
// gain.
void NonLinearEstimate(float echo_path_gain, const std::array<float, kFftLengthBy2Plus1>& X2,
                       std::span<std::array<float, kFftLengthBy2Plus1>> R2) {
  for (size_t ch = 0; ch < R2.size(); ++ch) {
    for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) R2[ch][k] = X2[k] * echo_path_gain;
  }
}

// Applies a soft noise gate to the echo generating power.
void ApplyNoiseGate(const EchoModelConfig& config, std::span<float, kFftLengthBy2Plus1> X2) {
  for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
    if (config.noise_gate_power > X2[k]) {
      X2[k] = std::max(0.f, X2[k] - config.noise_gate_slope * (config.noise_gate_power - X2[k]));
    }
  }
}

// Estimates the echo generating signal power as gated maximal power over a
// time window.
void EchoGeneratingPower(size_t num_render_channels, const SpectrumBuffer& spectrum_buffer,
                         const EchoModelConfig& echo_model, int filter_delay_blocks,
                         std::span<float, kFftLengthBy2Plus1> X2) {
  int idx_stop;
  int idx_start;
  GetRenderIndexesToAnalyze(spectrum_buffer, echo_model, filter_delay_blocks, &idx_start, &idx_stop);

  std::fill(X2.begin(), X2.end(), 0.f);
  if (num_render_channels == 1) {
    for (int k = idx_start; k != idx_stop; k = spectrum_buffer.IncIndex(k)) {
      for (size_t j = 0; j < kFftLengthBy2Plus1; ++j) {
        X2[j] = std::max(X2[j], spectrum_buffer.buffer[static_cast<size_t>(k)][/*channel=*/0][j]);
      }
    }
  } else {
    for (int k = idx_start; k != idx_stop; k = spectrum_buffer.IncIndex(k)) {
      std::array<float, kFftLengthBy2Plus1> render_power;
      render_power.fill(0.f);
      for (size_t ch = 0; ch < num_render_channels; ++ch) {
        const auto& channel_power = spectrum_buffer.buffer[static_cast<size_t>(k)][ch];
        for (size_t j = 0; j < kFftLengthBy2Plus1; ++j) render_power[j] += channel_power[j];
      }
      for (size_t j = 0; j < kFftLengthBy2Plus1; ++j) X2[j] = std::max(X2[j], render_power[j]);
    }
  }
}

// The render power of spectrum X2 summed over the channels.
std::span<const float, kFftLengthBy2Plus1> RenderPower(std::span<const std::array<float, kFftLengthBy2Plus1>> X2,
                                                       size_t num_render_channels,
                                                       std::array<float, kFftLengthBy2Plus1>& render_power_data) {
  if (num_render_channels == 1) return X2[/*channel=*/0];
  render_power_data.fill(0.f);
  for (size_t ch = 0; ch < num_render_channels; ++ch) {
    for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) render_power_data[k] += X2[ch][k];
  }
  return render_power_data;
}

}  // namespace

ResidualEchoEstimator::ResidualEchoEstimator(const Aec3Config& config, size_t num_render_channels)
    : config_(config),
      num_render_channels_(num_render_channels),
      early_reflections_transparent_mode_gain_(kDefaultTransparentModeGain),
      late_reflections_transparent_mode_gain_(kDefaultTransparentModeGain),
      // WebRTC-Aec3UseLow{Early,Late}ReflectionsDefaultGain not set.
      early_reflections_general_gain_(config_.ep_strength.default_gain),
      late_reflections_general_gain_(config_.ep_strength.default_gain),
      // WebRTC-Aec3UseErleOnsetCompensationInDominantNearend not set.
      erle_onset_compensation_in_dominant_nearend_(config_.ep_strength.erle_onset_compensation_in_dominant_nearend) {
  Reset();
}

void ResidualEchoEstimator::Estimate(const AecState& aec_state, const RenderBuffer& render_buffer,
                                     std::span<const std::array<float, kFftLengthBy2Plus1>> S2_linear,
                                     std::span<const std::array<float, kFftLengthBy2Plus1>> Y2,
                                     std::span<const std::array<float, kFftLengthBy2Plus1>> /*E2*/,
                                     bool dominant_nearend, std::span<std::array<float, kFftLengthBy2Plus1>> R2,
                                     std::span<std::array<float, kFftLengthBy2Plus1>> R2_unbounded) {
  const size_t num_capture_channels = R2.size();

  // Estimate the power of the stationary noise in the render signal.
  UpdateRenderNoisePower(render_buffer);

  // Estimate the residual echo power (the neural estimator is absent).
  if (aec_state.SaturatedEcho()) ++saturated_blocks_;
  if (aec_state.UsableLinearEstimate()) {
    ++linear_blocks_;
    // When there is saturated echo, assume the same spectral content as is
    // present in the microphone signal.
    if (aec_state.SaturatedEcho()) {
      for (size_t ch = 0; ch < num_capture_channels; ++ch) {
        std::copy(Y2[ch].begin(), Y2[ch].end(), R2[ch].begin());
        std::copy(Y2[ch].begin(), Y2[ch].end(), R2_unbounded[ch].begin());
      }
    } else {
      const bool onset_compensated = erle_onset_compensation_in_dominant_nearend_ || !dominant_nearend;
      LinearEstimate(S2_linear, aec_state.Erle(onset_compensated), R2);
      LinearEstimate(S2_linear, aec_state.ErleUnbounded(), R2_unbounded);
    }

    UpdateReverb(ReverbType::kLinear, aec_state, render_buffer, dominant_nearend);
    AddReverb(R2);
    AddReverb(R2_unbounded);
  } else {
    ++nonlinear_blocks_;
    const float echo_path_gain = GetEchoPathGain(aec_state, /*gain_for_early_reflections=*/true);

    // When there is saturated echo, assume the same spectral content as is
    // present in the microphone signal.
    if (aec_state.SaturatedEcho()) {
      for (size_t ch = 0; ch < num_capture_channels; ++ch) {
        std::copy(Y2[ch].begin(), Y2[ch].end(), R2[ch].begin());
        std::copy(Y2[ch].begin(), Y2[ch].end(), R2_unbounded[ch].begin());
      }
    } else {
      // Estimate the echo generating signal power.
      std::array<float, kFftLengthBy2Plus1> X2;
      EchoGeneratingPower(num_render_channels_, render_buffer.GetSpectrumBuffer(), config_.echo_model,
                          aec_state.MinDirectPathFilterDelay(), X2);
      if (!aec_state.UseStationarityProperties()) ApplyNoiseGate(config_.echo_model, X2);

      // Subtract the stationary noise power to avoid stationary noise causing
      // excessive echo suppression.
      for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
        X2[k] -= config_.echo_model.stationary_gate_slope * X2_noise_floor_[k];
        X2[k] = std::max(0.f, X2[k]);
      }

      NonLinearEstimate(echo_path_gain, X2, R2);
      NonLinearEstimate(echo_path_gain, X2, R2_unbounded);
    }

    if (config_.echo_model.model_reverb_in_nonlinear_mode && !aec_state.TransparentModeActive()) {
      UpdateReverb(ReverbType::kNonLinear, aec_state, render_buffer, dominant_nearend);
      AddReverb(R2);
      AddReverb(R2_unbounded);
    }
  }

  if (aec_state.UseStationarityProperties()) {
    // Scale the echo according to echo audibility.
    std::array<float, kFftLengthBy2Plus1> residual_scaling;
    aec_state.GetResidualEchoScaling(residual_scaling);
    for (size_t ch = 0; ch < num_capture_channels; ++ch) {
      for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
        R2[ch][k] *= residual_scaling[k];
        R2_unbounded[ch][k] *= residual_scaling[k];
      }
    }
  }
}

void ResidualEchoEstimator::Reset() {
  echo_reverb_.Reset();
  X2_noise_floor_counter_.fill(static_cast<int>(config_.echo_model.noise_floor_hold));
  X2_noise_floor_.fill(config_.echo_model.min_noise_floor_power);
}

void ResidualEchoEstimator::UpdateRenderNoisePower(const RenderBuffer& render_buffer) {
  std::array<float, kFftLengthBy2Plus1> render_power_data;
  std::span<const float, kFftLengthBy2Plus1> render_power =
      RenderPower(render_buffer.Spectrum(0), num_render_channels_, render_power_data);

  // Estimate the stationary noise power in a minimum statistics manner.
  for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
    // Decrease rapidly.
    if (render_power[k] < X2_noise_floor_[k]) {
      X2_noise_floor_[k] = render_power[k];
      X2_noise_floor_counter_[k] = 0;
    } else {
      // Increase in a delayed, leaky manner.
      if (X2_noise_floor_counter_[k] >= static_cast<int>(config_.echo_model.noise_floor_hold)) {
        X2_noise_floor_[k] = std::max(X2_noise_floor_[k] * 1.1f, config_.echo_model.min_noise_floor_power);
      } else {
        ++X2_noise_floor_counter_[k];
      }
    }
  }
}

void ResidualEchoEstimator::UpdateReverb(ReverbType reverb_type, const AecState& aec_state,
                                         const RenderBuffer& render_buffer, bool dominant_nearend) {
  // Choose reverb partition based on what type of echo power model is used.
  const size_t first_reverb_partition = reverb_type == ReverbType::kLinear
                                            ? static_cast<size_t>(aec_state.FilterLengthBlocks() + 1)
                                            : static_cast<size_t>(aec_state.MinDirectPathFilterDelay() + 1);

  // Compute render power for the reverb.
  std::array<float, kFftLengthBy2Plus1> render_power_data;
  std::span<const float, kFftLengthBy2Plus1> render_power = RenderPower(
      render_buffer.Spectrum(static_cast<int>(first_reverb_partition)), num_render_channels_, render_power_data);

  // Update the reverb estimate.
  float reverb_decay = aec_state.ReverbDecay(/*mild=*/dominant_nearend);
  if (reverb_type == ReverbType::kLinear) {
    echo_reverb_.UpdateReverb(render_power, aec_state.GetReverbFrequencyResponse(), reverb_decay);
  } else {
    const float echo_path_gain = GetEchoPathGain(aec_state, /*gain_for_early_reflections=*/false);
    echo_reverb_.UpdateReverbNoFreqShaping(render_power, echo_path_gain, reverb_decay);
  }
}

void ResidualEchoEstimator::AddReverb(std::span<std::array<float, kFftLengthBy2Plus1>> R2) const {
  std::span<const float, kFftLengthBy2Plus1> reverb_power = echo_reverb_.reverb();
  for (auto& R2_ch : R2) {
    for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) R2_ch[k] += reverb_power[k];
  }
}

float ResidualEchoEstimator::GetEchoPathGain(const AecState& aec_state, bool gain_for_early_reflections) const {
  float gain_amplitude;
  if (aec_state.TransparentModeActive()) {
    gain_amplitude =
        gain_for_early_reflections ? early_reflections_transparent_mode_gain_ : late_reflections_transparent_mode_gain_;
  } else {
    gain_amplitude = gain_for_early_reflections ? early_reflections_general_gain_ : late_reflections_general_gain_;
  }
  return gain_amplitude * gain_amplitude;
}

void ResidualEchoEstimator::ExportMetrics(MetricsWriter& w) const {
  w.Counter("linear_blocks", linear_blocks_);
  w.Counter("nonlinear_blocks", nonlinear_blocks_);
  w.Counter("saturated_echo_blocks", saturated_blocks_);
}

// --- suppression_gain.cc -------------------------------------------------------

namespace {

void LimitLowFrequencyGains(std::array<float, kFftLengthBy2Plus1>* gain) {
  // Limit the low frequency gains to avoid the impact of the high-pass filter
  // on the lower-frequency gain influencing the overall achieved gain.
  (*gain)[0] = (*gain)[1] = std::min((*gain)[1], (*gain)[2]);
}

void LimitHighFrequencyGains(const SuppressorConfig::HighFrequencySuppression& high_frequency_suppression,
                             bool conservative_hf_suppression, std::array<float, kFftLengthBy2Plus1>* gain) {
  // Limit the high frequency gains to avoid echo leakage due to an imperfect
  // filter.
  const int limiting_gain_band = high_frequency_suppression.limiting_gain_band;
  const int bands_in_limiting_gain = high_frequency_suppression.bands_in_limiting_gain;
  if (bands_in_limiting_gain > 0) {
    float min_upper_gain = 1.f;
    for (int band = limiting_gain_band; band < limiting_gain_band + bands_in_limiting_gain; ++band) {
      min_upper_gain = std::min(min_upper_gain, (*gain)[static_cast<size_t>(band)]);
    }
    std::for_each(gain->begin() + limiting_gain_band + 1, gain->end(),
                  [min_upper_gain](float& a) { a = std::min(a, min_upper_gain); });
  }
  (*gain)[kFftLengthBy2] = (*gain)[kFftLengthBy2Minus1];

  if (conservative_hf_suppression) {
    // Limits the gain in the frequencies for which the adaptive filter has not
    // converged.
    constexpr size_t kUpperAccurateBandPlus1 = 29;
    constexpr float oneByBandsInSum = 1 / static_cast<float>(kUpperAccurateBandPlus1 - 20);
    const float hf_gain_bound =
        std::accumulate(gain->begin() + 20, gain->begin() + kUpperAccurateBandPlus1, 0.f) * oneByBandsInSum;
    std::for_each(gain->begin() + kUpperAccurateBandPlus1, gain->end(),
                  [hf_gain_bound](float& a) { a = std::min(a, hf_gain_bound); });
  }
}

// Scales the echo according to assessed audibility at the other end.
void WeightEchoForAudibility(const EchoAudibilityConfig& echo_audibility, std::span<const float> echo,
                             std::span<float> weighted_echo) {
  auto weigh = [](float threshold, float normalizer, size_t begin, size_t end, std::span<const float> x,
                  std::span<float> weighted_x) {
    for (size_t k = begin; k < end; ++k) {
      if (x[k] < threshold) {
        float tmp = (threshold - x[k]) * normalizer;
        weighted_x[k] = x[k] * std::max(0.f, 1.f - tmp * tmp);
      } else {
        weighted_x[k] = x[k];
      }
    }
  };

  float threshold = echo_audibility.floor_power * echo_audibility.audibility_threshold_lf;
  float normalizer = 1.f / (threshold - echo_audibility.floor_power);
  weigh(threshold, normalizer, 0, 3, echo, weighted_echo);

  threshold = echo_audibility.floor_power * echo_audibility.audibility_threshold_mf;
  normalizer = 1.f / (threshold - echo_audibility.floor_power);
  weigh(threshold, normalizer, 3, 7, echo, weighted_echo);

  threshold = echo_audibility.floor_power * echo_audibility.audibility_threshold_hf;
  normalizer = 1.f / (threshold - echo_audibility.floor_power);
  weigh(threshold, normalizer, 7, kFftLengthBy2Plus1, echo, weighted_echo);
}

float SumOfSquares(std::span<const float> x) {
  return std::accumulate(x.begin(), x.end(), 0.f, [](float a, float b) { return a + b * b; });
}

}  // namespace

float SuppressionGain::UpperBandsGain(const SuppressorConfig& suppressor_config,
                                      std::span<const std::array<float, kFftLengthBy2Plus1>> echo_spectrum,
                                      std::span<const std::array<float, kFftLengthBy2Plus1>> comfort_noise_spectrum,
                                      const std::optional<int>& narrow_peak_band, bool saturated_echo,
                                      const Block& render,
                                      const std::array<float, kFftLengthBy2Plus1>& low_band_gain) const {
  if (render.NumBands() == 1) return 1.f;
  const int num_render_channels = render.NumChannels();

  if (narrow_peak_band && (*narrow_peak_band > static_cast<int>(kFftLengthBy2Plus1 - 10))) return 0.001f;

  constexpr size_t kLowBandGainLimit = kFftLengthBy2 / 2;
  const float gain_below_8_khz = *std::min_element(low_band_gain.begin() + kLowBandGainLimit, low_band_gain.end());

  // Always attenuate the upper bands when there is saturated echo.
  if (saturated_echo) return std::min(0.001f, gain_below_8_khz);

  // Compute the upper and lower band energies.
  float low_band_energy = 0.f;
  for (int ch = 0; ch < num_render_channels; ++ch) {
    low_band_energy = std::max(low_band_energy, SumOfSquares(render.View(/*band=*/0, ch)));
  }
  float high_band_energy = 0.f;
  for (int k = 1; k < render.NumBands(); ++k) {
    for (int ch = 0; ch < num_render_channels; ++ch) {
      high_band_energy = std::max(high_band_energy, SumOfSquares(render.View(k, ch)));
    }
  }

  // If there is more power in the lower frequencies than the upper
  // frequencies, or if the power in upper frequencies is low, do not bound the
  // gain in the upper bands.
  float anti_howling_gain;
  const float activation_threshold =
      kBlockSize * suppressor_config.high_bands_suppression.anti_howling_activation_threshold;
  if (high_band_energy < std::max(low_band_energy, activation_threshold)) {
    anti_howling_gain = 1.f;
  } else {
    // In all other cases, bound the gain for upper frequencies.
    anti_howling_gain =
        suppressor_config.high_bands_suppression.anti_howling_gain * std::sqrt(low_band_energy / high_band_energy);
  }

  float gain_bound = 1.f;
  if (!dominant_nearend_detector_->IsNearendState()) {
    // Bound the upper gain during significant echo activity.
    const auto& cfg = suppressor_config.high_bands_suppression;
    for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
      const float echo_sum = LowFrequencyEnergy(echo_spectrum[ch]);
      const float noise_sum = LowFrequencyEnergy(comfort_noise_spectrum[ch]);
      if (echo_sum > cfg.enr_threshold * noise_sum) {
        gain_bound = cfg.max_gain_during_echo;
        break;
      }
    }
  }

  // Choose the gain as the minimum of the lower and upper gains.
  return std::min(std::min(gain_below_8_khz, anti_howling_gain), gain_bound);
}

// Computes the gain to reduce the echo to a non audible level.
void SuppressionGain::GainToNoAudibleEcho(const std::array<float, kFftLengthBy2Plus1>& nearend,
                                          const std::array<float, kFftLengthBy2Plus1>& echo,
                                          const std::array<float, kFftLengthBy2Plus1>& masker,
                                          std::array<float, kFftLengthBy2Plus1>* gain) const {
  const auto& p = dominant_nearend_detector_->IsNearendState() ? nearend_params_ : normal_params_;
  for (size_t k = 0; k < gain->size(); ++k) {
    float enr = echo[k] / (nearend[k] + 1.f);  // Echo-to-nearend ratio.
    float emr = echo[k] / (masker[k] + 1.f);   // Echo-to-masker (noise) ratio.
    float g = 1.0f;
    if (enr > p.enr_transparent_[k] && emr > p.emr_transparent_[k]) {
      g = (p.enr_suppress_[k] - enr) / (p.enr_suppress_[k] - p.enr_transparent_[k]);
      g = std::max(g, p.emr_transparent_[k] / emr);
    }
    (*gain)[k] = g;
  }
}

// Compute the minimum gain as the attenuating gain to put the signal just
// above the zero sample values.
void SuppressionGain::GetMinGain(const SuppressorConfig& suppressor_config,
                                 std::span<const float> weighted_residual_echo, std::span<const float> last_nearend,
                                 std::span<const float> last_echo, bool low_noise_render, bool saturated_echo,
                                 std::span<float> min_gain) const {
  if (!saturated_echo) {
    const float min_echo_power =
        low_noise_render ? echo_audibility_config_.low_render_limit : echo_audibility_config_.normal_render_limit;

    for (size_t k = 0; k < min_gain.size(); ++k) {
      min_gain[k] = weighted_residual_echo[k] > 0.f ? min_echo_power / weighted_residual_echo[k] : 1.f;
      min_gain[k] = std::min(min_gain[k], 1.f);
    }

    if (!initial_state_ || suppressor_config.lf_smoothing_during_initial_phase) {
      const float& dec = dominant_nearend_detector_->IsNearendState() ? nearend_params_.max_dec_factor_lf
                                                                      : normal_params_.max_dec_factor_lf;

      for (int k = 0; k <= suppressor_config.last_lf_smoothing_band; ++k) {
        const size_t kk = static_cast<size_t>(k);
        // Make sure the gains of the low frequencies do not decrease too
        // quickly after strong nearend.
        if (last_nearend[kk] > last_echo[kk] || k <= suppressor_config.last_permanent_lf_smoothing_band) {
          min_gain[kk] = std::max(min_gain[kk], last_gain_[kk] * dec);
          min_gain[kk] = std::min(min_gain[kk], 1.f);
        }
      }
    }
  } else {
    std::fill(min_gain.begin(), min_gain.end(), 0.f);
  }
}

// Compute the maximum gain by limiting the gain increase from the previous
// gain.
void SuppressionGain::GetMaxGain(float floor_first_increase, std::span<float> max_gain) const {
  const auto& inc =
      dominant_nearend_detector_->IsNearendState() ? nearend_params_.max_inc_factor : normal_params_.max_inc_factor;
  for (size_t k = 0; k < max_gain.size(); ++k) {
    max_gain[k] = std::min(std::max(last_gain_[k] * inc, floor_first_increase), 1.f);
  }
}

void SuppressionGain::LowerBandGain(const SuppressorConfig& suppressor_config, bool low_noise_render,
                                    const AecState& aec_state,
                                    std::span<const std::array<float, kFftLengthBy2Plus1>> suppressor_input,
                                    std::span<const std::array<float, kFftLengthBy2Plus1>> residual_echo,
                                    std::span<const std::array<float, kFftLengthBy2Plus1>> comfort_noise,
                                    bool clock_drift, std::array<float, kFftLengthBy2Plus1>* gain) {
  gain->fill(1.f);
  const bool saturated_echo = aec_state.SaturatedEcho();
  std::array<float, kFftLengthBy2Plus1> max_gain;
  GetMaxGain(suppressor_config.floor_first_increase, max_gain);

  for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
    std::array<float, kFftLengthBy2Plus1> G;
    std::array<float, kFftLengthBy2Plus1> nearend;
    nearend_smoothers_[ch].Average(suppressor_input[ch], nearend);

    // Weight echo power in terms of audibility.
    std::array<float, kFftLengthBy2Plus1> weighted_residual_echo;
    WeightEchoForAudibility(echo_audibility_config_, residual_echo[ch], weighted_residual_echo);

    std::array<float, kFftLengthBy2Plus1> min_gain;
    GetMinGain(suppressor_config, weighted_residual_echo, last_nearend_[ch], last_echo_[ch], low_noise_render,
               saturated_echo, min_gain);

    GainToNoAudibleEcho(nearend, weighted_residual_echo, comfort_noise[0], &G);

    // Clamp gains.
    for (size_t k = 0; k < gain->size(); ++k) {
      G[k] = std::max(std::min(G[k], max_gain[k]), min_gain[k]);
      (*gain)[k] = std::min((*gain)[k], G[k]);
    }

    // Store data required for the gain computation of the next block.
    std::copy(nearend.begin(), nearend.end(), last_nearend_[ch].begin());
    std::copy(weighted_residual_echo.begin(), weighted_residual_echo.end(), last_echo_[ch].begin());
  }

  LimitLowFrequencyGains(gain);
  // Use conservative high-frequency gains during clock-drift or when not in
  // dominant nearend.
  if (!dominant_nearend_detector_->IsNearendState() || clock_drift ||
      suppressor_config.conservative_hf_suppression) {
    LimitHighFrequencyGains(suppressor_config.high_frequency_suppression,
                            suppressor_config.conservative_hf_suppression, gain);
  }

  // Store computed gains.
  std::copy(gain->begin(), gain->end(), last_gain_.begin());

  // Transform gains to amplitude domain.
  SqrtInPlace(*gain);
}

SuppressionGain::SuppressionGain(const Aec3Config& config, int /*sample_rate_hz*/, size_t num_capture_channels)
    : num_capture_channels_(num_capture_channels),
      echo_audibility_config_(config.echo_audibility),
      use_subband_nearend_detection_(config.suppressor.use_subband_nearend_detection),
      last_nearend_(num_capture_channels_, std::array<float, kFftLengthBy2Plus1>{}),
      last_echo_(num_capture_channels_, std::array<float, kFftLengthBy2Plus1>{}),
      nearend_smoothers_(num_capture_channels_,
                         MovingAverageSpectrum(kFftLengthBy2Plus1, config.suppressor.nearend_average_blocks)),
      nearend_params_(config.suppressor.last_lf_band, config.suppressor.first_hf_band,
                      config.suppressor.nearend_tuning),
      normal_params_(config.suppressor.last_lf_band, config.suppressor.first_hf_band,
                     config.suppressor.normal_tuning) {
  last_gain_.fill(1.f);
  if (config.suppressor.use_subband_nearend_detection) {
    dominant_nearend_detector_ = std::make_unique<SubbandNearendDetector>(config.suppressor, num_capture_channels_);
  } else {
    dominant_nearend_detector_ = std::make_unique<DominantNearendDetector>(
        config.suppressor.dominant_nearend_detection, num_capture_channels_);
  }
}

void SuppressionGain::GetGain(const SuppressorConfig& suppressor_config, bool config_changed,
                              std::span<const std::array<float, kFftLengthBy2Plus1>> nearend_spectrum,
                              std::span<const std::array<float, kFftLengthBy2Plus1>> echo_spectrum,
                              std::span<const std::array<float, kFftLengthBy2Plus1>> residual_echo_spectrum,
                              std::span<const std::array<float, kFftLengthBy2Plus1>> residual_echo_spectrum_unbounded,
                              std::span<const std::array<float, kFftLengthBy2Plus1>> comfort_noise_spectrum,
                              const RenderSignalAnalyzer& render_signal_analyzer, const AecState& aec_state,
                              const Block& render, bool clock_drift, float* high_bands_gain,
                              std::array<float, kFftLengthBy2Plus1>* low_band_gain) {
  if (config_changed) UpdateStateDependingOnConfig(suppressor_config);

  // Choose residual echo spectrum for dominant nearend detection.
  const auto echo = suppressor_config.dominant_nearend_detection.use_unbounded_echo_spectrum
                        ? residual_echo_spectrum_unbounded
                        : residual_echo_spectrum;

  // Update the nearend state selection.
  dominant_nearend_detector_->Update(nearend_spectrum, echo, comfort_noise_spectrum, initial_state_);
  const bool nearend_state = dominant_nearend_detector_->IsNearendState();
  if (nearend_state) ++nearend_blocks_;
  if (nearend_state && !last_nearend_state_) ++nearend_entries_;
  last_nearend_state_ = nearend_state;

  // Compute gain for the lower band.
  bool low_noise_render = low_render_detector_.Detect(render);
  LowerBandGain(suppressor_config, low_noise_render, aec_state, nearend_spectrum, residual_echo_spectrum,
                comfort_noise_spectrum, clock_drift, low_band_gain);

  // Compute the gain for the upper bands.
  const std::optional<int> narrow_peak_band = render_signal_analyzer.NarrowPeakBand();
  *high_bands_gain = UpperBandsGain(suppressor_config, echo_spectrum, comfort_noise_spectrum, narrow_peak_band,
                                    aec_state.SaturatedEcho(), render, *low_band_gain);
}

void SuppressionGain::UpdateStateDependingOnConfig(const SuppressorConfig& suppressor_config) {
  // Update nearend average blocks.
  for (auto& smoother : nearend_smoothers_) smoother.UpdateMemoryLength(suppressor_config.nearend_average_blocks);
  dominant_nearend_detector_->SetConfig(suppressor_config);
  nearend_params_.SetConfig(suppressor_config.last_lf_band, suppressor_config.first_hf_band,
                            suppressor_config.nearend_tuning);
  normal_params_.SetConfig(suppressor_config.last_lf_band, suppressor_config.first_hf_band,
                           suppressor_config.normal_tuning);
}

void SuppressionGain::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("dominant_nearend", last_nearend_state_ ? 1.0 : 0.0);
  w.Counter("dominant_nearend_blocks", nearend_blocks_);
  w.Counter("dominant_nearend_entries", nearend_entries_);
}

// Detects when the render signal can be considered to have low power and
// consist of stationary noise.
bool SuppressionGain::LowNoiseRenderDetector::Detect(const Block& render) {
  float x2_sum = 0.f;
  float x2_max = 0.f;
  for (int ch = 0; ch < render.NumChannels(); ++ch) {
    for (float x_k : render.View(/*band=*/0, ch)) {
      const float x2 = x_k * x_k;
      x2_sum += x2;
      x2_max = std::max(x2_max, x2);
    }
  }
  x2_sum = x2_sum / static_cast<float>(render.NumChannels());

  constexpr float kThreshold = 50.f * 50.f * 64.f;
  const bool low_noise_render = average_power_ < kThreshold && x2_max < 3 * average_power_;
  average_power_ = average_power_ * 0.9f + x2_sum * 0.1f;
  return low_noise_render;
}

SuppressionGain::GainParameters::GainParameters(int last_lf_band, int first_hf_band,
                                                const SuppressorConfig::Tuning& tuning) {
  SetConfig(last_lf_band, first_hf_band, tuning);
}

void SuppressionGain::GainParameters::SetConfig(int last_lf_band, int first_hf_band,
                                                const SuppressorConfig::Tuning& tuning) {
  max_inc_factor = tuning.max_inc_factor;
  max_dec_factor_lf = tuning.max_dec_factor_lf;
  // Compute per-band masking thresholds.
  const auto& lf = tuning.mask_lf;
  const auto& hf = tuning.mask_hf;
  for (int k = 0; k < static_cast<int>(kFftLengthBy2Plus1); k++) {
    float a;
    if (k <= last_lf_band) {
      a = 0.f;
    } else if (k < first_hf_band) {
      a = static_cast<float>(k - last_lf_band) / static_cast<float>(first_hf_band - last_lf_band);
    } else {
      a = 1.f;
    }
    const size_t kk = static_cast<size_t>(k);
    enr_transparent_[kk] = (1 - a) * lf.enr_transparent + a * hf.enr_transparent;
    enr_suppress_[kk] = (1 - a) * lf.enr_suppress + a * hf.enr_suppress;
    emr_transparent_[kk] = (1 - a) * lf.emr_transparent + a * hf.emr_transparent;
  }
}

// --- suppression_filter.cc -----------------------------------------------------

namespace {

// Hanning window from Matlab command win = sqrt(hanning(128)).
constexpr float kSqrtHanning[kFftLength] = {
    0.00000000000000f, 0.02454122852291f, 0.04906767432742f, 0.07356456359967f, 0.09801714032956f,
    0.12241067519922f, 0.14673047445536f, 0.17096188876030f, 0.19509032201613f, 0.21910124015687f,
    0.24298017990326f, 0.26671275747490f, 0.29028467725446f, 0.31368174039889f, 0.33688985339222f,
    0.35989503653499f, 0.38268343236509f, 0.40524131400499f, 0.42755509343028f, 0.44961132965461f,
    0.47139673682600f, 0.49289819222978f, 0.51410274419322f, 0.53499761988710f, 0.55557023301960f,
    0.57580819141785f, 0.59569930449243f, 0.61523159058063f, 0.63439328416365f, 0.65317284295378f,
    0.67155895484702f, 0.68954054473707f, 0.70710678118655f, 0.72424708295147f, 0.74095112535496f,
    0.75720884650648f, 0.77301045336274f, 0.78834642762661f, 0.80320753148064f, 0.81758481315158f,
    0.83146961230255f, 0.84485356524971f, 0.85772861000027f, 0.87008699110871f, 0.88192126434835f,
    0.89322430119552f, 0.90398929312344f, 0.91420975570353f, 0.92387953251129f, 0.93299279883474f,
    0.94154406518302f, 0.94952818059304f, 0.95694033573221f, 0.96377606579544f, 0.97003125319454f,
    0.97570213003853f, 0.98078528040323f, 0.98527764238894f, 0.98917650996478f, 0.99247953459871f,
    0.99518472667220f, 0.99729045667869f, 0.99879545620517f, 0.99969881869620f, 1.00000000000000f,
    0.99969881869620f, 0.99879545620517f, 0.99729045667869f, 0.99518472667220f, 0.99247953459871f,
    0.98917650996478f, 0.98527764238894f, 0.98078528040323f, 0.97570213003853f, 0.97003125319454f,
    0.96377606579544f, 0.95694033573221f, 0.94952818059304f, 0.94154406518302f, 0.93299279883474f,
    0.92387953251129f, 0.91420975570353f, 0.90398929312344f, 0.89322430119552f, 0.88192126434835f,
    0.87008699110871f, 0.85772861000027f, 0.84485356524971f, 0.83146961230255f, 0.81758481315158f,
    0.80320753148064f, 0.78834642762661f, 0.77301045336274f, 0.75720884650648f, 0.74095112535496f,
    0.72424708295147f, 0.70710678118655f, 0.68954054473707f, 0.67155895484702f, 0.65317284295378f,
    0.63439328416365f, 0.61523159058063f, 0.59569930449243f, 0.57580819141785f, 0.55557023301960f,
    0.53499761988710f, 0.51410274419322f, 0.49289819222978f, 0.47139673682600f, 0.44961132965461f,
    0.42755509343028f, 0.40524131400499f, 0.38268343236509f, 0.35989503653499f, 0.33688985339222f,
    0.31368174039889f, 0.29028467725446f, 0.26671275747490f, 0.24298017990326f, 0.21910124015687f,
    0.19509032201613f, 0.17096188876030f, 0.14673047445536f, 0.12241067519922f, 0.09801714032956f,
    0.07356456359967f, 0.04906767432742f, 0.02454122852291f};

}  // namespace

SuppressionFilter::SuppressionFilter(int sample_rate_hz, size_t num_capture_channels)
    : sample_rate_hz_(sample_rate_hz),
      num_capture_channels_(num_capture_channels),
      e_output_old_(NumBandsForRate(sample_rate_hz_),
                    std::vector<std::array<float, kFftLengthBy2>>(num_capture_channels_)) {
  for (auto& band : e_output_old_) {
    for (auto& ch : band) ch.fill(0.f);
  }
}

void SuppressionFilter::ApplyGain(std::span<const FftData> comfort_noise,
                                  std::span<const FftData> comfort_noise_high_band,
                                  const std::array<float, kFftLengthBy2Plus1>& suppression_gain,
                                  float high_bands_gain, std::span<const FftData> E_lowest_band, Block* e) {
  // Comfort noise gain is sqrt(1-g^2), where g is the suppression gain.
  std::array<float, kFftLengthBy2Plus1> noise_gain;
  for (size_t i = 0; i < kFftLengthBy2Plus1; ++i) noise_gain[i] = 1.f - suppression_gain[i] * suppression_gain[i];
  SqrtInPlace(noise_gain);

  const float high_bands_noise_scaling = 0.4f * std::sqrt(1.f - high_bands_gain * high_bands_gain);

  for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
    const int c = static_cast<int>(ch);
    FftData E;

    // Analysis filterbank.
    E.Assign(E_lowest_band[ch]);

    for (size_t i = 0; i < kFftLengthBy2Plus1; ++i) {
      // Apply suppression gains.
      float E_real = E.re[i] * suppression_gain[i];
      float E_imag = E.im[i] * suppression_gain[i];

      // Scale and add the comfort noise.
      E.re[i] = E_real + noise_gain[i] * comfort_noise[ch].re[i];
      E.im[i] = E_imag + noise_gain[i] * comfort_noise[ch].im[i];
    }

    // Synthesis filterbank.
    std::array<float, kFftLength> e_extended;
    constexpr float kIfftNormalization = 2.f / kFftLength;
    fft_.Ifft(E, &e_extended);

    auto e0 = e->View(/*band=*/0, c);
    float* e0_old = e_output_old_[0][ch].data();

    // Window and add the first half of e_extended with the second half of
    // e_extended from the previous block.
    for (size_t i = 0; i < kFftLengthBy2; ++i) {
      float e0_i = e0_old[i] * kSqrtHanning[kFftLengthBy2 + i];
      e0_i += e_extended[i] * kSqrtHanning[i];
      e0[i] = e0_i * kIfftNormalization;
    }

    // The second half of e_extended is stored for the succeeding frame.
    std::copy(e_extended.begin() + kFftLengthBy2, e_extended.begin() + kFftLength, e_output_old_[0][ch].begin());

    // Apply suppression gain to upper bands.
    for (int b = 1; b < e->NumBands(); ++b) {
      for (auto& v : e->View(b, c)) v *= high_bands_gain;
    }

    // Add comfort noise to band 1.
    if (e->NumBands() > 1) {
      E.Assign(comfort_noise_high_band[ch]);
      std::array<float, kFftLength> time_domain_high_band_noise;
      fft_.Ifft(E, &time_domain_high_band_noise);

      auto e1 = e->View(/*band=*/1, c);
      const float gain = high_bands_noise_scaling * kIfftNormalization;
      for (size_t i = 0; i < kFftLengthBy2; ++i) e1[i] += time_domain_high_band_noise[i] * gain;
    }

    // Delay upper bands to match the delay of the filter bank.
    for (int b = 1; b < e->NumBands(); ++b) {
      auto e_band = e->View(b, c);
      float* e_band_old = e_output_old_[static_cast<size_t>(b)][ch].data();
      for (size_t i = 0; i < kFftLengthBy2; ++i) std::swap(e_band[i], e_band_old[i]);
    }

    // Clamp output of all bands.
    for (int b = 0; b < e->NumBands(); ++b) {
      for (auto& v : e->View(b, c)) v = std::clamp(v, -32768.f, 32767.f);
    }
  }
}

// --- comfort_noise_generator.cc ------------------------------------------------

namespace {

// Computes the noise floor value that matches a WGN input of
// noise_floor_dbfs.
float GetNoiseFloorFactor(float noise_floor_dbfs) {
  // kdBfsNormalization = 20.f*log10(32768.f).
  constexpr float kdBfsNormalization = 90.30899869919436f;
  return 64.f * std::pow(10.f, (kdBfsNormalization + noise_floor_dbfs) * 0.1f);
}

// Table of sqrt(2) * sin(2*pi*i/32).
constexpr float kSqrt2 = std::numbers::sqrt2_v<float>;
constexpr std::array<float, 32> kSqrt2Sin = {
    +0.0000000f, +0.2758994f, +0.5411961f, +0.7856950f, +1.0000000f, +1.1758756f, +1.3065630f, +1.3870398f,
    +kSqrt2,     +1.3870398f, +1.3065630f, +1.1758756f, +1.0000000f, +0.7856950f, +0.5411961f, +0.2758994f,
    +0.0000000f, -0.2758994f, -0.5411961f, -0.7856950f, -1.0000000f, -1.1758756f, -1.3065630f, -1.3870398f,
    -kSqrt2,     -1.3870398f, -1.3065630f, -1.1758756f, -1.0000000f, -0.7856950f, -0.5411961f, -0.2758994f};

void GenerateRandomSinTableIndices(uint32_t& seed, std::span<int16_t, kFftLengthBy2 - 1> re_sin_table_indices,
                                   std::span<int16_t, kFftLengthBy2 - 1> im_sin_table_indices) {
  constexpr int32_t kSeedMask = 0x80000000 - 1;
  constexpr int16_t kImIndexMask = 32 - 1;
  for (size_t k = 0; k < re_sin_table_indices.size(); ++k) {
    // Generate a random 31-bit integer.
    seed = (seed * 69069 + 1) & kSeedMask;

    // Convert to 5-bit indices.
    int16_t index = static_cast<int16_t>(seed >> 26);
    re_sin_table_indices[k] = index;

    index = static_cast<int16_t>((re_sin_table_indices[k] + 8) & kImIndexMask);
    im_sin_table_indices[k] = index;
  }
}

void GenerateComfortNoise(const std::array<float, kFftLengthBy2Plus1>& N2,
                          std::span<const int16_t, kFftLengthBy2 - 1> re_sin_table_indices,
                          std::span<const int16_t, kFftLengthBy2 - 1> im_sin_table_indices, FftData* lower_band_noise,
                          FftData* upper_band_noise) {
  FftData* N_low = lower_band_noise;
  FftData* N_high = upper_band_noise;

  // Compute square root spectrum.
  std::array<float, kFftLengthBy2Plus1> N;
  std::copy(N2.begin(), N2.end(), N.begin());
  SqrtInPlace(N);

  // Compute the noise level for the upper bands.
  constexpr float kOneByNumBands = 1.f / (kFftLengthBy2Plus1 / 2 + 1);
  constexpr int kFftLengthBy2Plus1By2 = kFftLengthBy2Plus1 / 2;
  const float high_band_noise_level = std::accumulate(N.begin() + kFftLengthBy2Plus1By2, N.end(), 0.f) * kOneByNumBands;

  // The analysis and synthesis windowing cause loss of power when
  // cross-fading the noise where frames are completely uncorrelated
  // (generated with random phase), hence the factor sqrt(2). This is not the
  // case for the speech signal where the input is overlapping (strong
  // correlation).
  N_low->re[0] = N_low->re[kFftLengthBy2] = N_high->re[0] = N_high->re[kFftLengthBy2] = 0.f;
  for (size_t k = 1; k < kFftLengthBy2; k++) {
    // y = sqrt(2) * sin(a)
    const float& x = kSqrt2Sin[static_cast<size_t>(re_sin_table_indices[k - 1])];
    // x = sqrt(2) * cos(a) = sqrt(2) * sin(a + pi/2)
    const float& y = kSqrt2Sin[static_cast<size_t>(im_sin_table_indices[k - 1])];

    // Form low-frequency noise via spectral shaping.
    N_low->re[k] = N[k] * x;
    N_low->im[k] = N[k] * y;

    // Form the high-frequency noise via simple levelling.
    N_high->re[k] = high_band_noise_level * x;
    N_high->im[k] = high_band_noise_level * y;
  }
}

}  // namespace

ComfortNoiseGenerator::ComfortNoiseGenerator(const Aec3Config& config, size_t num_capture_channels)
    : num_capture_channels_(num_capture_channels),
      noise_floor_(GetNoiseFloorFactor(config.comfort_noise.noise_floor_dbfs)),
      N2_initial_(std::make_unique<std::vector<std::array<float, kFftLengthBy2Plus1>>>(num_capture_channels_)),
      Y2_smoothed_(num_capture_channels_),
      N2_(num_capture_channels_) {
  for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
    (*N2_initial_)[ch].fill(0.f);
    Y2_smoothed_[ch].fill(0.f);
    N2_[ch].fill(1.0e6f);
  }
}

void ComfortNoiseGenerator::Compute(bool saturated_capture,
                                    std::span<const std::array<float, kFftLengthBy2Plus1>> capture_spectrum,
                                    std::span<FftData> lower_band_noise, std::span<FftData> upper_band_noise) {
  const auto& Y2 = capture_spectrum;

  if (!saturated_capture) {
    // Smooth Y2.
    for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
      std::transform(Y2_smoothed_[ch].begin(), Y2_smoothed_[ch].end(), Y2[ch].begin(), Y2_smoothed_[ch].begin(),
                     [](float a, float b) { return a + 0.1f * (b - a); });
    }

    if (N2_counter_ > 50) {
      // Update N2 from Y2_smoothed.
      for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
        std::transform(N2_[ch].begin(), N2_[ch].end(), Y2_smoothed_[ch].begin(), N2_[ch].begin(),
                       [](float a, float b) { return b < a ? (0.9f * b + 0.1f * a) * 1.0002f : a * 1.0002f; });
      }
    }

    if (N2_initial_) {
      if (++N2_counter_ == 1000) {
        N2_initial_.reset();
      } else {
        // Compute the N2_initial from N2.
        for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
          std::transform(N2_[ch].begin(), N2_[ch].end(), (*N2_initial_)[ch].begin(), (*N2_initial_)[ch].begin(),
                         [](float a, float b) { return a > b ? b + 0.001f * (a - b) : a; });
        }
      }
    }

    for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
      for (auto& n : N2_[ch]) n = std::max(n, noise_floor_);
      if (N2_initial_) {
        for (auto& n : (*N2_initial_)[ch]) n = std::max(n, noise_floor_);
      }
    }
  }

  // Choose N2 estimate to use.
  const auto& N2 = N2_initial_ ? (*N2_initial_) : N2_;

  std::array<int16_t, kFftLengthBy2 - 1> re_sin_table_indices;
  std::array<int16_t, kFftLengthBy2 - 1> im_sin_table_indices;
  GenerateRandomSinTableIndices(seed_, re_sin_table_indices, im_sin_table_indices);

  for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
    GenerateComfortNoise(N2[ch], re_sin_table_indices, im_sin_table_indices, &lower_band_noise[ch],
                         &upper_band_noise[ch]);
  }
}

}  // namespace sm::audio::aec3
