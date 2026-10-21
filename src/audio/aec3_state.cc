// AEC3's AecState: compare/webrtc/reference/modules/audio_processing/aec3/
// aec_state.cc (M157). See sm/audio/aec3_state.h.
#include "sm/audio/aec3_state.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace sm::audio::aec3 {
namespace {

// WebRTC-Aec3AecStateSubtractorAnalyzerResetKillSwitch, not set in M157's
// default field trials.
constexpr bool kSubtractorAnalyzerResetAtEchoPathChange = true;

void ComputeAvgRenderReverb(const SpectrumBuffer& spectrum_buffer, int delay_blocks, float reverb_decay,
                            ReverbModel* reverb_model,
                            std::span<float, kFftLengthBy2Plus1> reverb_power_spectrum) {
  const size_t num_render_channels = spectrum_buffer.buffer[0].size();
  int idx_at_delay = spectrum_buffer.OffsetIndex(spectrum_buffer.read, delay_blocks);
  int idx_past = spectrum_buffer.IncIndex(idx_at_delay);

  std::array<float, kFftLengthBy2Plus1> X2_data;
  std::span<const float> X2;
  if (num_render_channels > 1) {
    auto average_channels = [](size_t num_channels,
                               std::span<const std::array<float, kFftLengthBy2Plus1>> spectrum_band_0,
                               std::span<float, kFftLengthBy2Plus1> render_power) {
      std::fill(render_power.begin(), render_power.end(), 0.f);
      for (size_t ch = 0; ch < num_channels; ++ch) {
        for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) render_power[k] += spectrum_band_0[ch][k];
      }
      const float normalizer = 1.f / static_cast<float>(num_channels);
      for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) render_power[k] *= normalizer;
    };
    average_channels(num_render_channels, spectrum_buffer.buffer[static_cast<size_t>(idx_past)], X2_data);
    reverb_model->UpdateReverbNoFreqShaping(X2_data, /*power_spectrum_scaling=*/1.0f, reverb_decay);

    average_channels(num_render_channels, spectrum_buffer.buffer[static_cast<size_t>(idx_at_delay)], X2_data);
    X2 = X2_data;
  } else {
    reverb_model->UpdateReverbNoFreqShaping(spectrum_buffer.buffer[static_cast<size_t>(idx_past)][0],
                                            /*power_spectrum_scaling=*/1.0f, reverb_decay);
    X2 = spectrum_buffer.buffer[static_cast<size_t>(idx_at_delay)][0];
  }

  std::span<const float, kFftLengthBy2Plus1> reverb_power = reverb_model->reverb();
  for (size_t k = 0; k < X2.size(); ++k) reverb_power_spectrum[k] = X2[k] + reverb_power[k];
}

}  // namespace

void AecState::GetResidualEchoScaling(std::span<float> residual_scaling) const {
  bool filter_has_had_time_to_converge;
  if (config_.filter.conservative_initial_phase) {
    filter_has_had_time_to_converge =
        static_cast<float>(strong_not_saturated_render_blocks_) >= 1.5f * kNumBlocksPerSecond;
  } else {
    filter_has_had_time_to_converge =
        static_cast<float>(strong_not_saturated_render_blocks_) >= 0.8f * kNumBlocksPerSecond;
  }
  echo_audibility_.GetResidualEchoScaling(filter_has_had_time_to_converge, residual_scaling);
}

AecState::AecState(const Aec3Config& config, size_t num_capture_channels)
    : config_(config),
      num_capture_channels_(num_capture_channels),
      initial_state_(config_),
      delay_state_(config_, num_capture_channels_),
      transparent_state_(TransparentMode::Create(config_)),
      filter_quality_state_(config_, num_capture_channels_),
      erl_estimator_(2 * kNumBlocksPerSecond),
      erle_estimator_(2 * kNumBlocksPerSecond, config_, num_capture_channels_),
      filter_analyzer_(config_, num_capture_channels_),
      echo_audibility_(config_.echo_audibility.use_stationarity_properties_at_init),
      reverb_model_estimator_(config_, num_capture_channels_),
      subtractor_output_analyzer_(num_capture_channels_) {}

void AecState::HandleEchoPathChange(const EchoPathVariability& echo_path_variability) {
  const auto full_reset = [&]() {
    filter_analyzer_.Reset();
    capture_signal_saturation_ = false;
    strong_not_saturated_render_blocks_ = 0;
    blocks_with_active_render_ = 0;
    initial_state_.Reset();
    if (transparent_state_) transparent_state_->Reset();
    erle_estimator_.Reset(true);
    erl_estimator_.Reset();
    filter_quality_state_.Reset();
    ++echo_path_resets_;
  };

  if (echo_path_variability.delay_change != EchoPathVariability::DelayAdjustment::kNone) {
    full_reset();
  } else if (echo_path_variability.gain_change) {
    erle_estimator_.Reset(false);
  }
  if (kSubtractorAnalyzerResetAtEchoPathChange) subtractor_output_analyzer_.HandleEchoPathChange();
}

void AecState::Update(
    const std::optional<DelayEstimate>& external_delay,
    std::span<const std::vector<std::array<float, kFftLengthBy2Plus1>>> adaptive_filter_frequency_responses,
    std::span<const std::vector<float>> adaptive_filter_impulse_responses, const RenderBuffer& render_buffer,
    std::span<const std::array<float, kFftLengthBy2Plus1>> E2_refined,
    std::span<const std::array<float, kFftLengthBy2Plus1>> Y2, std::span<const SubtractorOutput> subtractor_output) {
  // Analyze the filter outputs and filters.
  bool any_filter_converged;
  bool any_coarse_filter_converged;
  bool all_filters_diverged;
  subtractor_output_analyzer_.Update(subtractor_output, &any_filter_converged, &any_coarse_filter_converged,
                                     &all_filters_diverged);

  bool any_filter_consistent;
  float max_echo_path_gain;
  filter_analyzer_.Update(adaptive_filter_impulse_responses, render_buffer, &any_filter_consistent,
                          &max_echo_path_gain);

  // Estimate the direct path delay of the filter.
  if (config_.filter.use_linear_filter) {
    delay_state_.Update(filter_analyzer_.FilterDelaysBlocks(), external_delay, strong_not_saturated_render_blocks_);
  }

  const Block& aligned_render_block = render_buffer.GetBlock(-delay_state_.MinDirectPathFilterDelay());

  // Update render counters.
  bool active_render = false;
  for (int ch = 0; ch < aligned_render_block.NumChannels(); ++ch) {
    const auto x = aligned_render_block.View(/*band=*/0, ch);
    const float render_energy = std::inner_product(x.begin(), x.end(), x.begin(), 0.f);
    if (render_energy > (config_.active_render_limit * config_.active_render_limit) * kFftLengthBy2) {
      active_render = true;
      break;
    }
  }
  blocks_with_active_render_ += active_render ? 1 : 0;
  strong_not_saturated_render_blocks_ += active_render && !SaturatedCapture() ? 1 : 0;

  std::array<float, kFftLengthBy2Plus1> avg_render_spectrum_with_reverb;
  ComputeAvgRenderReverb(render_buffer.GetSpectrumBuffer(), delay_state_.MinDirectPathFilterDelay(),
                         ReverbDecay(/*mild=*/false), &avg_render_reverb_, avg_render_spectrum_with_reverb);

  if (config_.echo_audibility.use_stationarity_properties) {
    // Update the echo audibility evaluator.
    echo_audibility_.Update(render_buffer, avg_render_reverb_.reverb(), delay_state_.MinDirectPathFilterDelay(),
                            delay_state_.ExternalDelayReported());
  }

  // Update the ERL and ERLE measures.
  if (initial_state_.TransitionTriggered()) erle_estimator_.Reset(false);

  erle_estimator_.Update(render_buffer, adaptive_filter_frequency_responses, avg_render_spectrum_with_reverb, Y2,
                         E2_refined, subtractor_output_analyzer_.ConvergedFilters());

  erl_estimator_.Update(subtractor_output_analyzer_.ConvergedFilters(),
                        render_buffer.Spectrum(delay_state_.MinDirectPathFilterDelay()), Y2);

  // Detect and flag echo saturation.
  if (config_.ep_strength.echo_can_saturate) {
    saturation_detector_.Update(aligned_render_block, SaturatedCapture(), UsableLinearEstimate(), subtractor_output,
                                max_echo_path_gain);
  }

  // Update the decision on whether to use the initial state parameter set.
  initial_state_.Update(active_render, SaturatedCapture());

  // Detect whether the transparent mode should be activated.
  if (transparent_state_) {
    transparent_state_->Update(delay_state_.MinDirectPathFilterDelay(), any_filter_consistent, any_filter_converged,
                               any_coarse_filter_converged, all_filters_diverged, active_render, SaturatedCapture());
  }

  // Analyze the quality of the filter.
  filter_quality_state_.Update(active_render, TransparentModeActive(), SaturatedCapture(), external_delay,
                               any_filter_converged);

  // Update the reverb estimate.
  const bool stationary_block =
      config_.echo_audibility.use_stationarity_properties && echo_audibility_.IsBlockStationary();

  reverb_model_estimator_.Update(filter_analyzer_.GetAdjustedFilters(), adaptive_filter_frequency_responses,
                                 erle_estimator_.GetInstLinearQualityEstimates(),
                                 delay_state_.DirectPathFilterDelays(),
                                 filter_quality_state_.UsableLinearFilterOutputs(), stationary_block);
}

void AecState::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("usable_linear_estimate", UsableLinearEstimate() ? 1.0 : 0.0);
  w.Gauge("transparent_mode", TransparentModeActive() ? 1.0 : 0.0);
  w.Gauge("initial_state", initial_state_.InitialStateActive() ? 1.0 : 0.0);
  w.Gauge("filter_delay_blocks", static_cast<double>(MinDirectPathFilterDelay()));
  w.Gauge("erl_time_domain", static_cast<double>(ErlTimeDomain()));
  w.Gauge("fullband_erle_log2", static_cast<double>(FullBandErleLog2()));
  w.Gauge("reverb_decay", static_cast<double>(ReverbDecay(/*mild=*/false)));
  w.Counter("echo_path_resets", echo_path_resets_);
}

AecState::InitialState::InitialState(const Aec3Config& config)
    : conservative_initial_phase_(config.filter.conservative_initial_phase),
      initial_state_seconds_(config.filter.initial_state_seconds) {
  Reset();
}

void AecState::InitialState::Reset() {
  initial_state_ = true;
  strong_not_saturated_render_blocks_ = 0;
}

void AecState::InitialState::Update(bool active_render, bool saturated_capture) {
  strong_not_saturated_render_blocks_ += active_render && !saturated_capture ? 1 : 0;

  // Flag whether the initial state is still active.
  bool prev_initial_state = initial_state_;
  if (conservative_initial_phase_) {
    initial_state_ = strong_not_saturated_render_blocks_ < 5 * kNumBlocksPerSecond;
  } else {
    initial_state_ =
        static_cast<float>(strong_not_saturated_render_blocks_) < initial_state_seconds_ * kNumBlocksPerSecond;
  }

  // Flag whether the transition from the initial state has started.
  transition_triggered_ = !initial_state_ && prev_initial_state;
}

AecState::FilterDelay::FilterDelay(const Aec3Config& config, size_t num_capture_channels)
    : delay_headroom_blocks_(static_cast<int>(config.delay.delay_headroom_samples / kBlockSize)),
      filter_delays_blocks_(num_capture_channels, delay_headroom_blocks_),
      min_filter_delay_(delay_headroom_blocks_) {}

void AecState::FilterDelay::Update(std::span<const int> analyzer_filter_delay_estimates_blocks,
                                   const std::optional<DelayEstimate>& external_delay,
                                   size_t blocks_with_proper_filter_adaptation) {
  // Update the delay based on the external delay.
  if (external_delay) external_delay_ = external_delay;

  // Override the estimated delay if it is not certain that the filter has had
  // time to converge.
  const bool delay_estimator_may_not_have_converged = blocks_with_proper_filter_adaptation < 2 * kNumBlocksPerSecond;
  if (delay_estimator_may_not_have_converged && external_delay_) {
    const int delay_guess = delay_headroom_blocks_;
    std::fill(filter_delays_blocks_.begin(), filter_delays_blocks_.end(), delay_guess);
  } else {
    std::copy(analyzer_filter_delay_estimates_blocks.begin(), analyzer_filter_delay_estimates_blocks.end(),
              filter_delays_blocks_.begin());
  }

  min_filter_delay_ = *std::min_element(filter_delays_blocks_.begin(), filter_delays_blocks_.end());
}

AecState::FilteringQualityAnalyzer::FilteringQualityAnalyzer(const Aec3Config& config, size_t num_capture_channels)
    : use_linear_filter_(config.filter.use_linear_filter), usable_linear_filter_estimates_(num_capture_channels, false) {}

void AecState::FilteringQualityAnalyzer::Reset() {
  std::fill(usable_linear_filter_estimates_.begin(), usable_linear_filter_estimates_.end(), false);
  overall_usable_linear_estimates_ = false;
  filter_update_blocks_since_reset_ = 0;
}

void AecState::FilteringQualityAnalyzer::Update(bool active_render, bool transparent_mode, bool saturated_capture,
                                                const std::optional<DelayEstimate>& external_delay,
                                                bool any_filter_converged) {
  // Update blocks counter.
  const bool filter_update = active_render && !saturated_capture;
  filter_update_blocks_since_reset_ += filter_update ? 1 : 0;
  filter_update_blocks_since_start_ += filter_update ? 1 : 0;

  // Store convergence flag when observed.
  convergence_seen_ = convergence_seen_ || any_filter_converged;

  // The requirements for filter adaptation at call startup are more
  // restrictive than after an in-call reset.
  const bool sufficient_data_to_converge_at_startup =
      static_cast<float>(filter_update_blocks_since_start_) > kNumBlocksPerSecond * 0.4f;
  const bool sufficient_data_to_converge_at_reset =
      sufficient_data_to_converge_at_startup &&
      static_cast<float>(filter_update_blocks_since_reset_) > kNumBlocksPerSecond * 0.2f;

  // The linear filter can only be used if it has had time to converge.
  overall_usable_linear_estimates_ = sufficient_data_to_converge_at_startup && sufficient_data_to_converge_at_reset;

  // The linear filter can only be used if an external delay or convergence
  // have been identified.
  overall_usable_linear_estimates_ = overall_usable_linear_estimates_ && (external_delay || convergence_seen_);

  // If transparent mode is on, deactivate using the linear filter.
  overall_usable_linear_estimates_ = overall_usable_linear_estimates_ && !transparent_mode;

  if (use_linear_filter_) {
    std::fill(usable_linear_filter_estimates_.begin(), usable_linear_filter_estimates_.end(),
              overall_usable_linear_estimates_);
  }
}

void AecState::SaturationDetector::Update(const Block& x, bool saturated_capture, bool usable_linear_estimate,
                                          std::span<const SubtractorOutput> subtractor_output, float echo_path_gain) {
  saturated_echo_ = false;
  if (!saturated_capture) return;

  if (usable_linear_estimate) {
    constexpr float kSaturationThreshold = 20000.f;
    for (size_t ch = 0; ch < subtractor_output.size(); ++ch) {
      saturated_echo_ = saturated_echo_ || (subtractor_output[ch].s_refined_max_abs > kSaturationThreshold ||
                                            subtractor_output[ch].s_coarse_max_abs > kSaturationThreshold);
    }
  } else {
    float max_sample = 0.f;
    for (int ch = 0; ch < x.NumChannels(); ++ch) {
      for (float sample : x.View(/*band=*/0, ch)) max_sample = std::max(max_sample, std::fabs(sample));
    }

    const float kMargin = 10.f;
    float peak_echo_amplitude = max_sample * echo_path_gain * kMargin;
    saturated_echo_ = saturated_echo_ || peak_echo_amplitude > 32000;
  }
}

}  // namespace sm::audio::aec3
