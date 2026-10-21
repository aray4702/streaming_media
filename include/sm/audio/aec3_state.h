// 6.1 AEC3's echo state (`apm v2`, #240 step 3), ported from
// compare/webrtc/reference/modules/audio_processing/aec3/aec_state.{h,cc}
// (M157), with EchoCanceller3Config's defaults (Aec3Config).
//
//   AecState            what the residual echo suppression reads about the
//                       linear stage, updated once per capture block:
//                       whether the linear filter is usable (0.4 s of active,
//                       unsaturated render since the start, 0.2 s since the
//                       last reset, and convergence or an external delay
//                       seen), the direct-path filter delay (FilterAnalyzer,
//                       or the 32-sample headroom while the filter may not
//                       have converged and a delay was reported), active
//                       render (energy over 100^2 * 64 for more than 200
//                       blocks), the initial state (2.5 s of strong render),
//                       echo saturation, transparent mode, the ERL and ERLE
//                       (aec3_erle.h) and the reverb model (aec3_reverb.h)
//                       fed with the average render spectrum plus its reverb.
//
// Differences that remain (tools/catalog.py 6.1): no data dumps. The field
// trial WebRTC-Aec3AecStateSubtractorAnalyzerResetKillSwitch is read as not
// set (M157's default: the subtractor output analyzer resets at echo path
// changes). The residual echo estimator, the suppression gain and
// EchoCanceller3 are #240's next steps, so nothing calls this in a call yet.
#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "sm/audio/aec3_analysis.h"
#include "sm/audio/aec3_erle.h"
#include "sm/audio/aec3_filter.h"
#include "sm/audio/aec3_reverb.h"
#include "sm/core/metrics.h"

namespace sm::audio::aec3 {

class AecState : public MetricsSource {
 public:
  AecState(const Aec3Config& config, size_t num_capture_channels);
  AecState(const AecState&) = delete;
  AecState& operator=(const AecState&) = delete;

  // Whether the linear filter's echo estimate is usable.
  bool UsableLinearEstimate() const {
    return filter_quality_state_.LinearFilterUsable() && config_.filter.use_linear_filter;
  }
  // Whether the linear filter's output is used as the echo canceller output.
  bool UseLinearFilterOutput() const {
    return filter_quality_state_.LinearFilterUsable() && config_.filter.use_linear_filter;
  }
  bool ActiveRender() const { return blocks_with_active_render_ > 200; }
  // The scaling of the residual echo per bin (from the echo audibility).
  void GetResidualEchoScaling(std::span<float> residual_scaling) const;
  bool UseStationarityProperties() const { return config_.echo_audibility.use_stationarity_properties; }
  std::span<const std::array<float, kFftLengthBy2Plus1>> Erle(bool onset_compensated) const {
    return erle_estimator_.Erle(onset_compensated);
  }
  std::span<const std::array<float, kFftLengthBy2Plus1>> ErleUnbounded() const {
    return erle_estimator_.ErleUnbounded();
  }
  float FullBandErleLog2() const { return erle_estimator_.FullbandErleLog2(); }
  const std::array<float, kFftLengthBy2Plus1>& Erl() const { return erl_estimator_.Erl(); }
  float ErlTimeDomain() const { return erl_estimator_.ErlTimeDomain(); }
  int MinDirectPathFilterDelay() const { return delay_state_.MinDirectPathFilterDelay(); }
  bool SaturatedCapture() const { return capture_signal_saturation_; }
  bool SaturatedEcho() const { return saturation_detector_.SaturatedEcho(); }
  void UpdateCaptureSaturation(bool capture_signal_saturation) {
    capture_signal_saturation_ = capture_signal_saturation;
  }
  bool TransparentModeActive() const { return transparent_state_ && transparent_state_->Active(); }
  void HandleEchoPathChange(const EchoPathVariability& echo_path_variability);
  float ReverbDecay(bool mild) const { return reverb_model_estimator_.ReverbDecay(mild); }
  std::span<const float> GetReverbFrequencyResponse() const {
    return reverb_model_estimator_.GetReverbFrequencyResponse();
  }
  // Whether the initial state ended in the last Update.
  bool TransitionTriggered() const { return initial_state_.TransitionTriggered(); }
  // One capture block: the filters' responses, the render buffer, the
  // refined filter's error spectra E2 and the capture spectra Y2 per channel.
  void Update(const std::optional<DelayEstimate>& external_delay,
              std::span<const std::vector<std::array<float, kFftLengthBy2Plus1>>> adaptive_filter_frequency_responses,
              std::span<const std::vector<float>> adaptive_filter_impulse_responses,
              const RenderBuffer& render_buffer, std::span<const std::array<float, kFftLengthBy2Plus1>> E2_refined,
              std::span<const std::array<float, kFftLengthBy2Plus1>> Y2,
              std::span<const SubtractorOutput> subtractor_output);
  int FilterLengthBlocks() const { return filter_analyzer_.FilterLengthBlocks(); }
  std::optional<DelayEstimate> ExternalDelayBlocks() const { return delay_state_.ExternalDelayBlocks(); }
  bool InitialStateActive() const { return initial_state_.InitialStateActive(); }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  class InitialState {
   public:
    explicit InitialState(const Aec3Config& config);
    void Reset();
    void Update(bool active_render, bool saturated_capture);
    bool InitialStateActive() const { return initial_state_; }
    bool TransitionTriggered() const { return transition_triggered_; }

   private:
    const bool conservative_initial_phase_;
    const float initial_state_seconds_;
    bool transition_triggered_ = false;
    bool initial_state_ = true;
    size_t strong_not_saturated_render_blocks_ = 0;
  };

  class FilterDelay {
   public:
    FilterDelay(const Aec3Config& config, size_t num_capture_channels);
    bool ExternalDelayReported() const { return external_delay_.has_value(); }
    std::optional<DelayEstimate> ExternalDelayBlocks() const { return external_delay_; }
    std::span<const int> DirectPathFilterDelays() const { return filter_delays_blocks_; }
    int MinDirectPathFilterDelay() const { return min_filter_delay_; }
    void Update(std::span<const int> analyzer_filter_delay_estimates_blocks,
                const std::optional<DelayEstimate>& external_delay, size_t blocks_with_proper_filter_adaptation);

   private:
    const int delay_headroom_blocks_;
    std::vector<int> filter_delays_blocks_;
    int min_filter_delay_;
    std::optional<DelayEstimate> external_delay_;
  };

  class FilteringQualityAnalyzer {
   public:
    FilteringQualityAnalyzer(const Aec3Config& config, size_t num_capture_channels);
    bool LinearFilterUsable() const { return overall_usable_linear_estimates_; }
    const std::vector<bool>& UsableLinearFilterOutputs() const { return usable_linear_filter_estimates_; }
    void Reset();
    void Update(bool active_render, bool transparent_mode, bool saturated_capture,
                const std::optional<DelayEstimate>& external_delay, bool any_filter_converged);

   private:
    const bool use_linear_filter_;
    bool overall_usable_linear_estimates_ = false;
    size_t filter_update_blocks_since_reset_ = 0;
    size_t filter_update_blocks_since_start_ = 0;
    bool convergence_seen_ = false;
    std::vector<bool> usable_linear_filter_estimates_;
  };

  class SaturationDetector {
   public:
    bool SaturatedEcho() const { return saturated_echo_; }
    void Update(const Block& x, bool saturated_capture, bool usable_linear_estimate,
                std::span<const SubtractorOutput> subtractor_output, float echo_path_gain);

   private:
    bool saturated_echo_ = false;
  };

  const Aec3Config config_;
  const size_t num_capture_channels_;
  InitialState initial_state_;
  FilterDelay delay_state_;
  std::unique_ptr<TransparentMode> transparent_state_;
  FilteringQualityAnalyzer filter_quality_state_;
  SaturationDetector saturation_detector_;
  ErlEstimator erl_estimator_;
  ErleEstimator erle_estimator_;
  size_t strong_not_saturated_render_blocks_ = 0;
  size_t blocks_with_active_render_ = 0;
  bool capture_signal_saturation_ = false;
  FilterAnalyzer filter_analyzer_;
  EchoAudibility echo_audibility_;
  ReverbModelEstimator reverb_model_estimator_;
  ReverbModel avg_render_reverb_;
  SubtractorOutputAnalyzer subtractor_output_analyzer_;
  uint64_t echo_path_resets_ = 0;
};

}  // namespace sm::audio::aec3
