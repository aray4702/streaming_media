// 6.1 AEC3's echo return loss (ERL) and echo return loss enhancement (ERLE)
// estimators (`apm v2`, #240 step 3), ported from
// compare/webrtc/reference/modules/audio_processing/aec3/
// {erl_estimator,erle_estimator,fullband_erle_estimator,
// subband_erle_estimator,signal_dependent_erle_estimator}.{h,cc} (M157), with
// Log2TodB from aec3_common.cc.
//
//   ErlEstimator        ERL per bin and over the whole band, Y2 / X2 over the
//                       maximum render and converged-capture spectra where
//                       the render power exceeds 44015068 (-46 dBFS WGN):
//                       decreases smoothed by 0.1, held 1000 blocks, then
//                       doubled each block up to 1000; floor 0.01.
//   FullBandErleEstimator  log2 of the fullband ERLE from 6 accumulated
//                       blocks, smoothed by 0.05 and held 100 blocks, plus a
//                       linear-filter quality in [0, 1] from where the
//                       instantaneous ERLE lies between its slowly forgotten
//                       maximum and minimum.
//   SubbandErleEstimator   ERLE per bin from 6 accumulated blocks (rising by
//                       0.05, falling by 0.1, not at all on low render),
//                       clamped to [min, max_l] below bin 32 and
//                       [min, max_h] above; an onset-compensated copy that
//                       decays by 0.97 per block towards the ERLE during
//                       render onsets (the minimum ERLE: the
//                       WebRTC-Aec3MinErleDuringOnsetsKillSwitch trial is not
//                       set), and a virtually unbounded copy (max 100000).
//   SignalDependentErleEstimator  only when erle.num_sections > 1 (the
//                       default is 1): corrects the subband ERLE in 6
//                       subbands by how many filter sections carry 90 % of
//                       the echo estimate.
//   ErleEstimator       the three above after a startup phase.
//
// Differences that remain: no Environment (the field trial is hard-coded at
// its default, see SubbandErleEstimator) and no data dumps. SafeClamp is
// rtc_base/numerics/safe_minmax.h's float case (not in reference/): x <= min
// gives min, x >= max gives max, otherwise x.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "sm/audio/aec3_filter.h"

namespace sm::audio::aec3 {

// aec3_common.cc: dB from a power in log2.
inline float Log2TodB(const float in_log2) { return static_cast<float>(3.0102999566398121 * in_log2); }

// rtc_base/numerics/safe_minmax.h's SafeClamp for floats (min <= max).
inline float SafeClamp(float x, float min, float max) { return x <= min ? min : (x >= max ? max : x); }

// erl_estimator.h: estimates the echo return loss from the signal spectra.
class ErlEstimator {
 public:
  explicit ErlEstimator(size_t startup_phase_length_blocks_);
  ErlEstimator(const ErlEstimator&) = delete;
  ErlEstimator& operator=(const ErlEstimator&) = delete;
  void Reset();
  void Update(const std::vector<bool>& converged_filters,
              std::span<const std::array<float, kFftLengthBy2Plus1>> render_spectra,
              std::span<const std::array<float, kFftLengthBy2Plus1>> capture_spectra);
  const std::array<float, kFftLengthBy2Plus1>& Erl() const { return erl_; }
  float ErlTimeDomain() const { return erl_time_domain_; }

 private:
  const size_t startup_phase_length_blocks__;
  std::array<float, kFftLengthBy2Plus1> erl_;
  std::array<int, kFftLengthBy2Minus1> hold_counters_;
  float erl_time_domain_;
  int hold_counter_time_domain_;
  size_t blocks_since_reset_ = 0;
};

// fullband_erle_estimator.h: the ERLE over all bins.
class FullBandErleEstimator {
 public:
  FullBandErleEstimator(const ErleConfig& config, size_t num_capture_channels);
  void Reset();
  void Update(std::span<const float> X2, std::span<const std::array<float, kFftLengthBy2Plus1>> Y2,
              std::span<const std::array<float, kFftLengthBy2Plus1>> E2, const std::vector<bool>& converged_filters);
  // The fullband ERLE in log2 units (the minimum over the capture channels).
  float FullbandErleLog2() const {
    float min_erle = erle_time_domain_log2_[0];
    for (size_t ch = 1; ch < erle_time_domain_log2_.size(); ++ch) {
      min_erle = std::min(min_erle, erle_time_domain_log2_[ch]);
    }
    return min_erle;
  }
  // The current linear filter quality per capture channel, in [0, 1] (1 the
  // best), when there is an instantaneous estimate.
  std::span<const std::optional<float>> GetInstLinearQualityEstimates() const { return linear_filters_qualities_; }

 private:
  void UpdateQualityEstimates();

  class ErleInstantaneous {
   public:
    explicit ErleInstantaneous(const ErleConfig& config);
    // Adds a point; true when the instantaneous ERLE was updated.
    bool Update(float Y2_sum, float E2_sum);
    void Reset();
    void ResetAccumulators();
    std::optional<float> GetInstErleLog2() const { return erle_log2_; }
    std::optional<float> GetQualityEstimate() const {
      if (erle_log2_) {
        float value = inst_quality_estimate_;
        if (clamp_inst_quality_to_zero_) {
          value = std::max(0.f, value);
        }
        if (clamp_inst_quality_to_one_) {
          value = std::min(1.f, value);
        }
        return std::optional<float>(value);
      }
      return std::nullopt;
    }

   private:
    void UpdateMaxMin();
    void UpdateQualityEstimate();
    const bool clamp_inst_quality_to_zero_;
    const bool clamp_inst_quality_to_one_;
    std::optional<float> erle_log2_;
    float inst_quality_estimate_;
    float max_erle_log2_;
    float min_erle_log2_;
    float Y2_acum_;
    float E2_acum_;
    int num_points_;
  };

  const float min_erle_log2_;
  [[maybe_unused]] const float max_erle_lf_log2_;  // Unused in libwebrtc too.
  std::vector<int> hold_counters_instantaneous_erle_;
  std::vector<float> erle_time_domain_log2_;
  std::vector<ErleInstantaneous> instantaneous_erle_;
  std::vector<std::optional<float>> linear_filters_qualities_;
};

// subband_erle_estimator.h: the ERLE per bin.
class SubbandErleEstimator {
 public:
  SubbandErleEstimator(const Aec3Config& config, size_t num_capture_channels);
  void Reset();
  void Update(std::span<const float, kFftLengthBy2Plus1> X2, std::span<const std::array<float, kFftLengthBy2Plus1>> Y2,
              std::span<const std::array<float, kFftLengthBy2Plus1>> E2, const std::vector<bool>& converged_filters);
  std::span<const std::array<float, kFftLengthBy2Plus1>> Erle(bool onset_compensated) const {
    return onset_compensated && use_onset_detection_ ? erle_onset_compensated_ : erle_;
  }
  // The non-capped ERLE.
  std::span<const std::array<float, kFftLengthBy2Plus1>> ErleUnbounded() const { return erle_unbounded_; }
  // The ERLE at render onsets (only used for testing).
  std::span<const std::array<float, kFftLengthBy2Plus1>> ErleDuringOnsets() const { return erle_during_onsets_; }

 private:
  struct AccumulatedSpectra {
    explicit AccumulatedSpectra(size_t num_capture_channels)
        : Y2(num_capture_channels),
          E2(num_capture_channels),
          low_render_energy(num_capture_channels),
          num_points(num_capture_channels) {}
    std::vector<std::array<float, kFftLengthBy2Plus1>> Y2;
    std::vector<std::array<float, kFftLengthBy2Plus1>> E2;
    std::vector<std::array<bool, kFftLengthBy2Plus1>> low_render_energy;
    std::vector<int> num_points;
  };

  void UpdateAccumulatedSpectra(std::span<const float, kFftLengthBy2Plus1> X2,
                                std::span<const std::array<float, kFftLengthBy2Plus1>> Y2,
                                std::span<const std::array<float, kFftLengthBy2Plus1>> E2,
                                const std::vector<bool>& converged_filters);
  void ResetAccumulatedSpectra();
  void UpdateBands(const std::vector<bool>& converged_filters);
  void DecreaseErlePerBandForLowRenderSignals();

  const bool use_onset_detection_;
  const float min_erle_;
  const std::array<float, kFftLengthBy2Plus1> max_erle_;
  const bool use_min_erle_during_onsets_;
  AccumulatedSpectra accum_spectra_;
  // ERLE without special handling of render onsets.
  std::vector<std::array<float, kFftLengthBy2Plus1>> erle_;
  // ERLE lowered during render onsets.
  std::vector<std::array<float, kFftLengthBy2Plus1>> erle_onset_compensated_;
  std::vector<std::array<float, kFftLengthBy2Plus1>> erle_unbounded_;
  // ERLE during render onsets.
  std::vector<std::array<float, kFftLengthBy2Plus1>> erle_during_onsets_;
  std::vector<std::array<bool, kFftLengthBy2Plus1>> coming_onset_;
  std::vector<std::array<int, kFftLengthBy2Plus1>> hold_counters_;
};

// signal_dependent_erle_estimator.h: refines the average ERLE by whether the
// echo estimate comes from the direct path or a more reverberant one.
class SignalDependentErleEstimator {
 public:
  SignalDependentErleEstimator(const Aec3Config& config, size_t num_capture_channels);
  void Reset();
  std::span<const std::array<float, kFftLengthBy2Plus1>> Erle(bool onset_compensated) const {
    return onset_compensated && use_onset_detection_ ? erle_onset_compensated_ : erle_;
  }
  // `average_erle` is the average ERLE of the linear filter.
  void Update(const RenderBuffer& render_buffer,
              std::span<const std::vector<std::array<float, kFftLengthBy2Plus1>>> filter_frequency_response,
              std::span<const float, kFftLengthBy2Plus1> X2, std::span<const std::array<float, kFftLengthBy2Plus1>> Y2,
              std::span<const std::array<float, kFftLengthBy2Plus1>> E2,
              std::span<const std::array<float, kFftLengthBy2Plus1>> average_erle,
              std::span<const std::array<float, kFftLengthBy2Plus1>> average_erle_onset_compensated,
              const std::vector<bool>& converged_filters);

  static constexpr size_t kSubbands = 6;

 private:
  void ComputeNumberOfActiveFilterSections(
      const RenderBuffer& render_buffer,
      std::span<const std::vector<std::array<float, kFftLengthBy2Plus1>>> filter_frequency_responses);
  void UpdateCorrectionFactors(std::span<const float, kFftLengthBy2Plus1> X2,
                               std::span<const std::array<float, kFftLengthBy2Plus1>> Y2,
                               std::span<const std::array<float, kFftLengthBy2Plus1>> E2,
                               const std::vector<bool>& converged_filters);
  void ComputeEchoEstimatePerFilterSection(
      const RenderBuffer& render_buffer,
      std::span<const std::vector<std::array<float, kFftLengthBy2Plus1>>> filter_frequency_responses);
  void ComputeActiveFilterSections();

  const float min_erle_;
  const size_t num_sections_;
  const size_t num_blocks_;
  const size_t delay_headroom_blocks_;
  const std::array<size_t, kFftLengthBy2Plus1> band_to_subband_;
  const std::array<float, kSubbands> max_erle_;
  const std::vector<size_t> section_boundaries_blocks_;
  const bool use_onset_detection_;
  std::vector<std::array<float, kFftLengthBy2Plus1>> erle_;
  std::vector<std::array<float, kFftLengthBy2Plus1>> erle_onset_compensated_;
  std::vector<std::vector<std::array<float, kFftLengthBy2Plus1>>> S2_section_accum_;
  std::vector<std::vector<std::array<float, kSubbands>>> erle_estimators_;
  std::vector<std::array<float, kSubbands>> erle_ref_;
  std::vector<std::vector<std::array<float, kSubbands>>> correction_factors_;
  std::vector<std::array<int, kSubbands>> num_updates_;
  std::vector<std::array<size_t, kFftLengthBy2Plus1>> n_active_sections_;
};

// erle_estimator.h: the subband (or signal-dependent) and fullband ERLE.
class ErleEstimator {
 public:
  ErleEstimator(size_t startup_phase_length_blocks, const Aec3Config& config, size_t num_capture_channels);
  ErleEstimator(const ErleEstimator&) = delete;
  ErleEstimator& operator=(const ErleEstimator&) = delete;
  // Resets the fullband and subband estimators (and the startup phase on a
  // delay change).
  void Reset(bool delay_change);
  void Update(const RenderBuffer& render_buffer,
              std::span<const std::vector<std::array<float, kFftLengthBy2Plus1>>> filter_frequency_responses,
              std::span<const float, kFftLengthBy2Plus1> avg_render_spectrum_with_reverb,
              std::span<const std::array<float, kFftLengthBy2Plus1>> capture_spectra,
              std::span<const std::array<float, kFftLengthBy2Plus1>> subtractor_spectra,
              const std::vector<bool>& converged_filters);
  // The subband ERLE.
  std::span<const std::array<float, kFftLengthBy2Plus1>> Erle(bool onset_compensated) const {
    return signal_dependent_erle_estimator_ ? signal_dependent_erle_estimator_->Erle(onset_compensated)
                                            : subband_erle_estimator_.Erle(onset_compensated);
  }
  // The non-capped subband ERLE (the capped one with the signal-dependent
  // estimator).
  std::span<const std::array<float, kFftLengthBy2Plus1>> ErleUnbounded() const {
    return !signal_dependent_erle_estimator_ ? subband_erle_estimator_.ErleUnbounded()
                                             : signal_dependent_erle_estimator_->Erle(/*onset_compensated=*/false);
  }
  // The subband ERLE during onsets (only used for testing).
  std::span<const std::array<float, kFftLengthBy2Plus1>> ErleDuringOnsets() const {
    return subband_erle_estimator_.ErleDuringOnsets();
  }
  float FullbandErleLog2() const { return fullband_erle_estimator_.FullbandErleLog2(); }
  // The current linear filter quality per capture channel, in [0, 1].
  std::span<const std::optional<float>> GetInstLinearQualityEstimates() const {
    return fullband_erle_estimator_.GetInstLinearQualityEstimates();
  }

 private:
  const size_t startup_phase_length_blocks_;
  FullBandErleEstimator fullband_erle_estimator_;
  SubbandErleEstimator subband_erle_estimator_;
  std::unique_ptr<SignalDependentErleEstimator> signal_dependent_erle_estimator_;
  size_t blocks_since_reset_ = 0;
};

}  // namespace sm::audio::aec3
