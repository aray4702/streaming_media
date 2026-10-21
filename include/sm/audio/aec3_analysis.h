// AEC3's echo-path analysis for AecState (`apm v2`, #240 step 3), ported from
// compare/webrtc/reference/modules/audio_processing/aec3/
// {filter_analyzer,echo_audibility,stationarity_estimator,transparent_mode,
// subtractor_output_analyzer}.{h,cc} (M157), reading the EchoCanceller3Config
// defaults that Aec3Config (sm/audio/aec3_filter.h) holds.
//
//   FilterAnalyzer      high-passes the refined filter's impulse response
//                       (minimum phase, about 600 Hz) one 64-tap block per
//                       call, tracks its peak (the delay in blocks, the echo
//                       path gain) and calls the filter consistent once the
//                       peak is above 10x the floor and 2x any secondary peak
//                       and has stayed at the same delay for more than 375
//                       blocks of active render (1.5 s).
//   StationarityEstimator  render noise spectrum (20-block average, then
//                       alpha from 0.04 down to 0.004 over 2 s, slowed when
//                       the power is 10x the noise); a band is stationary
//                       when 13 spectra around the delay plus the reverb stay
//                       below 10x the noise, with a 12-block hangover and
//                       3-band smoothing.
//   EchoAudibility      feeds the estimator the new render spectra once the
//                       render was not near zero (max |x| >= 10) and turns the
//                       stationary bands into residual echo scalings of 0.
//   TransparentMode     headset detection: the legacy classifier (render
//                       without converged filters for long enough) or the
//                       two-state HMM. Create() returns nullptr when
//                       ep_strength.bounded_erl is set and otherwise the
//                       legacy classifier, as M157 does with the field trial
//                       WebRTC-Aec3TransparentModeHmm not set (its default);
//                       CreateHmm() builds the HMM one.
//   SubtractorOutputAnalyzer  per capture channel: refined filter converged
//                       (e2 < 0.5 y2, y2 > 50^2 * 64), coarse converged
//                       (strict 0.05 / relaxed 0.3 with y2 > 20^2 * 64), both
//                       diverged (min e2 > 1.5 y2, y2 > 30^2 * 64).
//
// DelayEstimate (delay_estimate.h) is in sm/audio/aec3_delay.h; moving_average
// isn't used by these classes. Differences that remain: no data dumps, no
// RTC_LOG of the transparent mode choice, the HMM chosen by calling
// CreateHmm() instead of by field trial, no RTC_(D)CHECKs (the stationarity
// estimate's RTC_CHECK_LT(0, noise) holds by construction: the noise spectrum
// never goes below 10). AecState (#240's next step) is what calls these.
#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "sm/audio/aec3_filter.h"

namespace sm::audio::aec3 {

// filter_analyzer.h: the properties of the refined adaptive filters.
class FilterAnalyzer {
 public:
  FilterAnalyzer(const Aec3Config& config, size_t num_capture_channels);
  FilterAnalyzer(const FilterAnalyzer&) = delete;
  FilterAnalyzer& operator=(const FilterAnalyzer&) = delete;

  // Resets the analysis.
  void Reset();

  // Updates the estimates with new input data.
  void Update(std::span<const std::vector<float>> filters_time_domain, const RenderBuffer& render_buffer,
              bool* any_filter_consistent, float* max_echo_path_gain);

  // The delay in blocks of each filter.
  std::span<const int> FilterDelaysBlocks() const { return filter_delays_blocks_; }

  // The minimum delay over the filters, in blocks.
  int MinFilterDelayBlocks() const { return min_filter_delay_blocks_; }

  // The number of blocks of the filter currently used.
  int FilterLengthBlocks() const { return filter_analysis_states_[0].filter_length_blocks; }

  // The preprocessed (high-passed) filters.
  std::span<const std::vector<float>> GetAdjustedFilters() const { return h_highpass_; }

  // Public for testing purposes only.
  void SetRegionToAnalyze(size_t filter_size);

 private:
  struct FilterAnalysisState;

  void AnalyzeRegion(std::span<const std::vector<float>> filters_time_domain, const RenderBuffer& render_buffer);
  void UpdateFilterGain(std::span<const float> filters_time_domain, FilterAnalysisState* st);
  void PreProcessFilters(std::span<const std::vector<float>> filters_time_domain);
  void ResetRegion();

  struct FilterRegion {
    size_t start_sample_;
    size_t end_sample_;
  };

  // Whether the shape of the impulse response has been consistent over time.
  class ConsistentFilterDetector {
   public:
    explicit ConsistentFilterDetector(const Aec3Config& config);
    void Reset();
    bool Detect(std::span<const float> filter_to_analyze, const FilterRegion& region, const Block& x_block,
                size_t peak_index, int delay_blocks);

   private:
    bool significant_peak_;
    float filter_floor_accum_;
    float filter_secondary_peak_;
    size_t filter_floor_low_limit_;
    size_t filter_floor_high_limit_;
    const float active_render_threshold_;
    size_t consistent_estimate_counter_ = 0;
    int consistent_delay_reference_ = -10;
  };

  struct FilterAnalysisState {
    explicit FilterAnalysisState(const Aec3Config& config)
        : filter_length_blocks(static_cast<int>(config.filter.refined_initial.length_blocks)),
          consistent_filter_detector(config) {
      Reset(config.ep_strength.default_gain);
    }

    void Reset(float default_gain) {
      peak_index = 0;
      gain = default_gain;
      consistent_filter_detector.Reset();
    }

    float gain;
    size_t peak_index;
    int filter_length_blocks;
    bool consistent_estimate = false;
    ConsistentFilterDetector consistent_filter_detector;
  };

  const bool bounded_erl_;
  const float default_gain_;
  std::vector<std::vector<float>> h_highpass_;
  size_t blocks_since_reset_ = 0;
  FilterRegion region_;
  std::vector<FilterAnalysisState> filter_analysis_states_;
  std::vector<int> filter_delays_blocks_;
  int min_filter_delay_blocks_ = 0;
};

// stationarity_estimator.h: which render bands are stationary.
class StationarityEstimator {
 public:
  StationarityEstimator();

  // Resets the stationarity estimator.
  void Reset();

  // Updates only the noise estimator (useful until the delay is known).
  void UpdateNoiseEstimator(std::span<const std::array<float, kFftLengthBy2Plus1>> spectrum);

  // Updates the stationarity flags of the current frame, looking at future
  // and/or past frames.
  void UpdateStationarityFlags(const SpectrumBuffer& spectrum_buffer,
                               std::span<const float> render_reverb_contribution_spectrum, int idx_current,
                               int num_lookahead);

  // Whether the band is stationary.
  bool IsBandStationary(size_t band) const { return stationarity_flags_[band] && (hangovers_[band] == 0); }

  // Whether the current block is estimated as stationary.
  bool IsBlockStationary() const;

 private:
  static constexpr int kWindowLength = 13;
  // The power of the stationary noise spectrum at a band.
  float GetStationarityPowerBand(size_t k) const { return noise_.Power(k); }

  bool EstimateBandStationarity(const SpectrumBuffer& spectrum_buffer, std::span<const float> average_reverb,
                                const std::array<int, kWindowLength>& indexes, size_t band) const;
  bool AreAllBandsStationary();
  void UpdateHangover();
  void SmoothStationaryPerFreq();

  class NoiseSpectrum {
   public:
    NoiseSpectrum();
    void Reset();
    void Update(std::span<const std::array<float, kFftLengthBy2Plus1>> spectrum);
    std::span<const float> Spectrum() const { return noise_spectrum_; }
    float Power(size_t band) const { return noise_spectrum_[band]; }

   private:
    float GetAlpha() const;
    float UpdateBandBySmoothing(float power_band, float power_band_noise, float alpha) const;
    std::array<float, kFftLengthBy2Plus1> noise_spectrum_;
    size_t block_counter_;
  };

  NoiseSpectrum noise_;
  std::array<int, kFftLengthBy2Plus1> hangovers_;
  std::array<bool, kFftLengthBy2Plus1> stationarity_flags_;
};

// echo_audibility.h
class EchoAudibility {
 public:
  explicit EchoAudibility(bool use_render_stationarity_at_init);
  EchoAudibility(const EchoAudibility&) = delete;
  EchoAudibility& operator=(const EchoAudibility&) = delete;

  // Feeds new render data to the echo audibility estimator.
  void Update(const RenderBuffer& render_buffer, std::span<const float> average_reverb, int min_channel_delay_blocks,
              bool external_delay_seen);

  // The residual echo scaling.
  void GetResidualEchoScaling(bool filter_has_had_time_to_converge, std::span<float> residual_scaling) const {
    for (size_t band = 0; band < residual_scaling.size(); ++band) {
      if (render_stationarity_.IsBandStationary(band) &&
          (filter_has_had_time_to_converge || use_render_stationarity_at_init_)) {
        residual_scaling[band] = 0.f;
      } else {
        residual_scaling[band] = 1.0f;
      }
    }
  }

  // Whether the current render block is estimated as stationary.
  bool IsBlockStationary() const { return render_stationarity_.IsBlockStationary(); }

 private:
  void Reset();
  void UpdateRenderStationarityFlags(const RenderBuffer& render_buffer, std::span<const float> average_reverb,
                                     int min_channel_delay_blocks);
  void UpdateRenderNoiseEstimator(const SpectrumBuffer& spectrum_buffer, const BlockBuffer& block_buffer,
                                  bool external_delay_seen);
  bool IsRenderTooLow(const BlockBuffer& block_buffer);

  std::optional<int> render_spectrum_write_prev_;
  int render_block_write_prev_ = 0;
  bool non_zero_render_seen_;
  const bool use_render_stationarity_at_init_;
  StationarityEstimator render_stationarity_;
};

// transparent_mode.h: detects and toggles the transparent mode, in which the
// suppressor applies less suppression.
class TransparentMode {
 public:
  // nullptr when config.ep_strength.bounded_erl, otherwise the legacy
  // classifier (M157 with WebRTC-Aec3TransparentModeHmm not set).
  static std::unique_ptr<TransparentMode> Create(const Aec3Config& config);
  // The HMM classifier (M157 with WebRTC-Aec3TransparentModeHmm enabled).
  static std::unique_ptr<TransparentMode> CreateHmm();

  virtual ~TransparentMode() = default;

  // Whether the transparent mode should be active.
  virtual bool Active() const = 0;

  // Resets the state of the detector.
  virtual void Reset() = 0;

  // Updates the detection decision based on new data.
  virtual void Update(int filter_delay_blocks, bool any_filter_consistent, bool any_filter_converged,
                      bool any_coarse_filter_converged, bool all_filters_diverged, bool active_render,
                      bool saturated_capture) = 0;
};

// subtractor_output_analyzer.h
class SubtractorOutputAnalyzer {
 public:
  explicit SubtractorOutputAnalyzer(size_t num_capture_channels);

  // Analyses the subtractor output.
  void Update(std::span<const SubtractorOutput> subtractor_output, bool* any_filter_converged,
              bool* any_coarse_filter_converged, bool* all_filters_diverged);

  const std::vector<bool>& ConvergedFilters() const { return filters_converged_; }

  // Handles an echo path change.
  void HandleEchoPathChange();

 private:
  std::vector<bool> filters_converged_;
};

}  // namespace sm::audio::aec3
