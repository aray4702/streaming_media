// AEC3's ERL and ERLE estimators, ported from libwebrtc M157's
// modules/audio_processing/aec3/ (see sm/audio/aec3_erle.h for the files).
#include "sm/audio/aec3_erle.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <numeric>

namespace sm::audio::aec3 {

// --- erl_estimator.cc --------------------------------------------------------

namespace {
constexpr float kMinErl = 0.01f;
constexpr float kMaxErl = 1000.f;
}  // namespace

ErlEstimator::ErlEstimator(size_t startup_phase_length_blocks_)
    : startup_phase_length_blocks__(startup_phase_length_blocks_) {
  erl_.fill(kMaxErl);
  hold_counters_.fill(0);
  erl_time_domain_ = kMaxErl;
  hold_counter_time_domain_ = 0;
}

void ErlEstimator::Reset() { blocks_since_reset_ = 0; }

void ErlEstimator::Update(const std::vector<bool>& converged_filters,
                          std::span<const std::array<float, kFftLengthBy2Plus1>> render_spectra,
                          std::span<const std::array<float, kFftLengthBy2Plus1>> capture_spectra) {
  const size_t num_capture_channels = converged_filters.size();

  // Corresponds to WGN of power -46 dBFS.
  constexpr float kX2Min = 44015068.0f;

  const auto first_converged_iter = std::find(converged_filters.begin(), converged_filters.end(), true);
  const bool any_filter_converged = first_converged_iter != converged_filters.end();

  if (++blocks_since_reset_ < startup_phase_length_blocks__ || !any_filter_converged) {
    return;
  }

  // The maximum spectrum across capture and the maximum across render.
  std::array<float, kFftLengthBy2Plus1> max_capture_spectrum_data;
  std::array<float, kFftLengthBy2Plus1> max_capture_spectrum = capture_spectra[/*channel=*/0];
  if (num_capture_channels > 1) {
    // Initialize using the first channel with a converged filter.
    const size_t first_converged =
        static_cast<size_t>(std::distance(converged_filters.begin(), first_converged_iter));
    max_capture_spectrum_data = capture_spectra[first_converged];
    for (size_t ch = first_converged + 1; ch < num_capture_channels; ++ch) {
      if (!converged_filters[ch]) {
        continue;
      }
      for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
        max_capture_spectrum_data[k] = std::max(max_capture_spectrum_data[k], capture_spectra[ch][k]);
      }
    }
    max_capture_spectrum = max_capture_spectrum_data;
  }

  const size_t num_render_channels = render_spectra.size();
  std::array<float, kFftLengthBy2Plus1> max_render_spectrum_data;
  std::span<const float, kFftLengthBy2Plus1> max_render_spectrum = render_spectra[/*channel=*/0];
  if (num_render_channels > 1) {
    std::copy(render_spectra[0].begin(), render_spectra[0].end(), max_render_spectrum_data.begin());
    for (size_t ch = 1; ch < num_render_channels; ++ch) {
      for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
        max_render_spectrum_data[k] = std::max(max_render_spectrum_data[k], render_spectra[ch][k]);
      }
    }
    max_render_spectrum = max_render_spectrum_data;
  }

  const auto& X2 = max_render_spectrum;
  const auto& Y2 = max_capture_spectrum;

  // Update the estimates in a maximum statistics manner.
  for (size_t k = 1; k < kFftLengthBy2; ++k) {
    if (X2[k] > kX2Min) {
      const float new_erl = Y2[k] / X2[k];
      if (new_erl < erl_[k]) {
        hold_counters_[k - 1] = 1000;
        erl_[k] += 0.1f * (new_erl - erl_[k]);
        erl_[k] = std::max(erl_[k], kMinErl);
      }
    }
  }

  std::for_each(hold_counters_.begin(), hold_counters_.end(), [](int& a) { --a; });
  std::transform(hold_counters_.begin(), hold_counters_.end(), erl_.begin() + 1, erl_.begin() + 1,
                 [](int a, float b) { return a > 0 ? b : std::min(kMaxErl, 2.f * b); });

  erl_[0] = erl_[1];
  erl_[kFftLengthBy2] = erl_[kFftLengthBy2 - 1];

  // ERL over all frequency bins.
  const float X2_sum = std::accumulate(X2.begin(), X2.end(), 0.0f);

  if (X2_sum > kX2Min * static_cast<float>(X2.size())) {
    const float Y2_sum = std::accumulate(Y2.begin(), Y2.end(), 0.0f);
    const float new_erl = Y2_sum / X2_sum;
    if (new_erl < erl_time_domain_) {
      hold_counter_time_domain_ = 1000;
      erl_time_domain_ += 0.1f * (new_erl - erl_time_domain_);
      erl_time_domain_ = std::max(erl_time_domain_, kMinErl);
    }
  }

  --hold_counter_time_domain_;
  erl_time_domain_ = (hold_counter_time_domain_ > 0) ? erl_time_domain_ : std::min(kMaxErl, 2.f * erl_time_domain_);
}

// --- fullband_erle_estimator.cc ----------------------------------------------

namespace {
constexpr float kFullbandEpsilon = 1e-3f;
constexpr float kFullbandX2BandEnergyThreshold = 44015068.0f;
constexpr int kFullbandBlocksToHoldErle = 100;
constexpr int kFullbandPointsToAccumulate = 6;
}  // namespace

FullBandErleEstimator::FullBandErleEstimator(const ErleConfig& config, size_t num_capture_channels)
    : min_erle_log2_(FastApproxLog2f(config.min + kFullbandEpsilon)),
      max_erle_lf_log2_(FastApproxLog2f(config.max_l + kFullbandEpsilon)),
      hold_counters_instantaneous_erle_(num_capture_channels, 0),
      erle_time_domain_log2_(num_capture_channels, min_erle_log2_),
      instantaneous_erle_(num_capture_channels, ErleInstantaneous(config)),
      linear_filters_qualities_(num_capture_channels) {
  Reset();
}

void FullBandErleEstimator::Reset() {
  for (auto& instantaneous_erle_ch : instantaneous_erle_) {
    instantaneous_erle_ch.Reset();
  }

  UpdateQualityEstimates();
  std::fill(erle_time_domain_log2_.begin(), erle_time_domain_log2_.end(), min_erle_log2_);
  std::fill(hold_counters_instantaneous_erle_.begin(), hold_counters_instantaneous_erle_.end(), 0);
}

void FullBandErleEstimator::Update(std::span<const float> X2,
                                   std::span<const std::array<float, kFftLengthBy2Plus1>> Y2,
                                   std::span<const std::array<float, kFftLengthBy2Plus1>> E2,
                                   const std::vector<bool>& converged_filters) {
  for (size_t ch = 0; ch < Y2.size(); ++ch) {
    if (converged_filters[ch]) {
      // The fullband ERLE.
      const float X2_sum = std::accumulate(X2.begin(), X2.end(), 0.0f);
      if (X2_sum > kFullbandX2BandEnergyThreshold * static_cast<float>(X2.size())) {
        const float Y2_sum = std::accumulate(Y2[ch].begin(), Y2[ch].end(), 0.0f);
        const float E2_sum = std::accumulate(E2[ch].begin(), E2[ch].end(), 0.0f);
        if (instantaneous_erle_[ch].Update(Y2_sum, E2_sum)) {
          hold_counters_instantaneous_erle_[ch] = kFullbandBlocksToHoldErle;
          erle_time_domain_log2_[ch] +=
              0.05f * ((instantaneous_erle_[ch].GetInstErleLog2().value()) - erle_time_domain_log2_[ch]);
          erle_time_domain_log2_[ch] = std::max(erle_time_domain_log2_[ch], min_erle_log2_);
        }
      }
    }
    --hold_counters_instantaneous_erle_[ch];
    if (hold_counters_instantaneous_erle_[ch] == 0) {
      instantaneous_erle_[ch].ResetAccumulators();
    }
  }

  UpdateQualityEstimates();
}

void FullBandErleEstimator::UpdateQualityEstimates() {
  for (size_t ch = 0; ch < instantaneous_erle_.size(); ++ch) {
    linear_filters_qualities_[ch] = instantaneous_erle_[ch].GetQualityEstimate();
  }
}

FullBandErleEstimator::ErleInstantaneous::ErleInstantaneous(const ErleConfig& config)
    : clamp_inst_quality_to_zero_(config.clamp_quality_estimate_to_zero),
      clamp_inst_quality_to_one_(config.clamp_quality_estimate_to_one) {
  Reset();
}

bool FullBandErleEstimator::ErleInstantaneous::Update(const float Y2_sum, const float E2_sum) {
  bool update_estimates = false;
  E2_acum_ += E2_sum;
  Y2_acum_ += Y2_sum;
  num_points_++;
  if (num_points_ == kFullbandPointsToAccumulate) {
    if (E2_acum_ > 0.f) {
      update_estimates = true;
      erle_log2_ = FastApproxLog2f(Y2_acum_ / E2_acum_ + kFullbandEpsilon);
    }
    num_points_ = 0;
    E2_acum_ = 0.f;
    Y2_acum_ = 0.f;
  }

  if (update_estimates) {
    UpdateMaxMin();
    UpdateQualityEstimate();
  }
  return update_estimates;
}

void FullBandErleEstimator::ErleInstantaneous::Reset() {
  ResetAccumulators();
  max_erle_log2_ = -10.f;  // -30 dB.
  min_erle_log2_ = 33.f;   // 100 dB.
  inst_quality_estimate_ = 0.f;
}

void FullBandErleEstimator::ErleInstantaneous::ResetAccumulators() {
  erle_log2_ = std::nullopt;
  inst_quality_estimate_ = 0.f;
  num_points_ = 0;
  E2_acum_ = 0.f;
  Y2_acum_ = 0.f;
}

void FullBandErleEstimator::ErleInstantaneous::UpdateMaxMin() {
  // Forgetting factors for the maximum and minimum, capped to the incoming
  // value.
  max_erle_log2_ -= 0.0004f;  // Forget factor, approx 1dB every 3 sec.
  max_erle_log2_ = std::max(max_erle_log2_, erle_log2_.value());
  min_erle_log2_ += 0.0004f;  // Forget factor, approx 1dB every 3 sec.
  min_erle_log2_ = std::min(min_erle_log2_, erle_log2_.value());
}

void FullBandErleEstimator::ErleInstantaneous::UpdateQualityEstimate() {
  const float alpha = 0.07f;
  float quality_estimate = 0.f;
  // libwebrtc's TODO: the estimate can become less than 0.
  if (max_erle_log2_ > min_erle_log2_) {
    quality_estimate = (erle_log2_.value() - min_erle_log2_) / (max_erle_log2_ - min_erle_log2_);
  }
  if (quality_estimate > inst_quality_estimate_) {
    inst_quality_estimate_ = quality_estimate;
  } else {
    inst_quality_estimate_ += alpha * (quality_estimate - inst_quality_estimate_);
  }
}

// --- subband_erle_estimator.cc -----------------------------------------------

namespace {

constexpr float kSubbandX2BandEnergyThreshold = 44015068.0f;
constexpr int kSubbandBlocksToHoldErle = 100;
constexpr int kBlocksForOnsetDetection = kSubbandBlocksToHoldErle + 150;
constexpr int kSubbandPointsToAccumulate = 6;

std::array<float, kFftLengthBy2Plus1> SetMaxErleBands(float max_erle_l, float max_erle_h) {
  std::array<float, kFftLengthBy2Plus1> max_erle;
  std::fill(max_erle.begin(), max_erle.begin() + kFftLengthBy2 / 2, max_erle_l);
  std::fill(max_erle.begin() + kFftLengthBy2 / 2, max_erle.end(), max_erle_h);
  return max_erle;
}

// !IsEnabled("WebRTC-Aec3MinErleDuringOnsetsKillSwitch"): the trial is not
// set.
constexpr bool kEnableMinErleDuringOnsets = true;

}  // namespace

SubbandErleEstimator::SubbandErleEstimator(const Aec3Config& config, size_t num_capture_channels)
    : use_onset_detection_(config.erle.onset_detection),
      min_erle_(config.erle.min),
      max_erle_(SetMaxErleBands(config.erle.max_l, config.erle.max_h)),
      use_min_erle_during_onsets_(kEnableMinErleDuringOnsets),
      accum_spectra_(num_capture_channels),
      erle_(num_capture_channels),
      erle_onset_compensated_(num_capture_channels),
      erle_unbounded_(num_capture_channels),
      erle_during_onsets_(num_capture_channels),
      coming_onset_(num_capture_channels),
      hold_counters_(num_capture_channels) {
  Reset();
}

void SubbandErleEstimator::Reset() {
  const size_t num_capture_channels = erle_.size();
  for (size_t ch = 0; ch < num_capture_channels; ++ch) {
    erle_[ch].fill(min_erle_);
    erle_onset_compensated_[ch].fill(min_erle_);
    erle_unbounded_[ch].fill(min_erle_);
    erle_during_onsets_[ch].fill(min_erle_);
    coming_onset_[ch].fill(true);
    hold_counters_[ch].fill(0);
  }
  ResetAccumulatedSpectra();
}

void SubbandErleEstimator::Update(std::span<const float, kFftLengthBy2Plus1> X2,
                                  std::span<const std::array<float, kFftLengthBy2Plus1>> Y2,
                                  std::span<const std::array<float, kFftLengthBy2Plus1>> E2,
                                  const std::vector<bool>& converged_filters) {
  UpdateAccumulatedSpectra(X2, Y2, E2, converged_filters);
  UpdateBands(converged_filters);

  if (use_onset_detection_) {
    DecreaseErlePerBandForLowRenderSignals();
  }

  const size_t num_capture_channels = erle_.size();
  for (size_t ch = 0; ch < num_capture_channels; ++ch) {
    auto& erle = erle_[ch];
    erle[0] = erle[1];
    erle[kFftLengthBy2] = erle[kFftLengthBy2 - 1];

    auto& erle_oc = erle_onset_compensated_[ch];
    erle_oc[0] = erle_oc[1];
    erle_oc[kFftLengthBy2] = erle_oc[kFftLengthBy2 - 1];

    auto& erle_u = erle_unbounded_[ch];
    erle_u[0] = erle_u[1];
    erle_u[kFftLengthBy2] = erle_u[kFftLengthBy2 - 1];
  }
}

void SubbandErleEstimator::UpdateBands(const std::vector<bool>& converged_filters) {
  const int num_capture_channels = static_cast<int>(accum_spectra_.Y2.size());
  for (int ch = 0; ch < num_capture_channels; ++ch) {
    // The converged_filter flag already imposes a minimum on the ERLE that
    // can be estimated: it is false when the filter performs poorly.
    if (!converged_filters[static_cast<size_t>(ch)]) {
      continue;
    }

    if (accum_spectra_.num_points[static_cast<size_t>(ch)] != kSubbandPointsToAccumulate) {
      continue;
    }

    const size_t c = static_cast<size_t>(ch);
    std::array<float, kFftLengthBy2> new_erle;
    std::array<bool, kFftLengthBy2> is_erle_updated;
    is_erle_updated.fill(false);

    for (size_t k = 1; k < kFftLengthBy2; ++k) {
      if (accum_spectra_.E2[c][k] > 0.f) {
        new_erle[k] = accum_spectra_.Y2[c][k] / accum_spectra_.E2[c][k];
        is_erle_updated[k] = true;
      }
    }

    if (use_onset_detection_) {
      for (size_t k = 1; k < kFftLengthBy2; ++k) {
        if (is_erle_updated[k] && !accum_spectra_.low_render_energy[c][k]) {
          if (coming_onset_[c][k]) {
            coming_onset_[c][k] = false;
            if (!use_min_erle_during_onsets_) {
              float alpha = new_erle[k] < erle_during_onsets_[c][k] ? 0.3f : 0.15f;
              erle_during_onsets_[c][k] =
                  SafeClamp(erle_during_onsets_[c][k] + alpha * (new_erle[k] - erle_during_onsets_[c][k]),
                            min_erle_, max_erle_[k]);
            }
          }
          hold_counters_[c][k] = kBlocksForOnsetDetection;
        }
      }
    }

    auto update_erle_band = [](float& erle, float new_erle_band, bool low_render_energy, float min_erle,
                               float max_erle) {
      float alpha = 0.05f;
      if (new_erle_band < erle) {
        alpha = low_render_energy ? 0.f : 0.1f;
      }
      erle = SafeClamp(erle + alpha * (new_erle_band - erle), min_erle, max_erle);
    };

    for (size_t k = 1; k < kFftLengthBy2; ++k) {
      if (is_erle_updated[k]) {
        const bool low_render_energy = accum_spectra_.low_render_energy[c][k];
        update_erle_band(erle_[c][k], new_erle[k], low_render_energy, min_erle_, max_erle_[k]);
        if (use_onset_detection_) {
          update_erle_band(erle_onset_compensated_[c][k], new_erle[k], low_render_energy, min_erle_, max_erle_[k]);
        }

        // Virtually unbounded ERLE.
        constexpr float kUnboundedErleMax = 100000.0f;
        update_erle_band(erle_unbounded_[c][k], new_erle[k], low_render_energy, min_erle_, kUnboundedErleMax);
      }
    }
  }
}

void SubbandErleEstimator::DecreaseErlePerBandForLowRenderSignals() {
  const int num_capture_channels = static_cast<int>(accum_spectra_.Y2.size());
  for (int ch = 0; ch < num_capture_channels; ++ch) {
    const size_t c = static_cast<size_t>(ch);
    for (size_t k = 1; k < kFftLengthBy2; ++k) {
      --hold_counters_[c][k];
      if (hold_counters_[c][k] <= (kBlocksForOnsetDetection - kSubbandBlocksToHoldErle)) {
        if (erle_onset_compensated_[c][k] > erle_during_onsets_[c][k]) {
          erle_onset_compensated_[c][k] = std::max(erle_during_onsets_[c][k], 0.97f * erle_onset_compensated_[c][k]);
        }
        if (hold_counters_[c][k] <= 0) {
          coming_onset_[c][k] = true;
          hold_counters_[c][k] = 0;
        }
      }
    }
  }
}

void SubbandErleEstimator::ResetAccumulatedSpectra() {
  for (size_t ch = 0; ch < erle_during_onsets_.size(); ++ch) {
    accum_spectra_.Y2[ch].fill(0.f);
    accum_spectra_.E2[ch].fill(0.f);
    accum_spectra_.num_points[ch] = 0;
    accum_spectra_.low_render_energy[ch].fill(false);
  }
}

void SubbandErleEstimator::UpdateAccumulatedSpectra(std::span<const float, kFftLengthBy2Plus1> X2,
                                                    std::span<const std::array<float, kFftLengthBy2Plus1>> Y2,
                                                    std::span<const std::array<float, kFftLengthBy2Plus1>> E2,
                                                    const std::vector<bool>& converged_filters) {
  auto& st = accum_spectra_;
  const int num_capture_channels = static_cast<int>(Y2.size());
  for (int ch = 0; ch < num_capture_channels; ++ch) {
    const size_t c = static_cast<size_t>(ch);
    // The converged_filter flag already imposes a minimum on the ERLE that
    // can be estimated: it is false when the filter performs poorly.
    if (!converged_filters[c]) {
      continue;
    }

    if (st.num_points[c] == kSubbandPointsToAccumulate) {
      st.num_points[c] = 0;
      st.Y2[c].fill(0.f);
      st.E2[c].fill(0.f);
      st.low_render_energy[c].fill(false);
    }

    std::transform(Y2[c].begin(), Y2[c].end(), st.Y2[c].begin(), st.Y2[c].begin(), std::plus<float>());
    std::transform(E2[c].begin(), E2[c].end(), st.E2[c].begin(), st.E2[c].begin(), std::plus<float>());

    for (size_t k = 0; k < X2.size(); ++k) {
      st.low_render_energy[c][k] = st.low_render_energy[c][k] || X2[k] < kSubbandX2BandEnergyThreshold;
    }

    ++st.num_points[c];
  }
}

// --- signal_dependent_erle_estimator.cc --------------------------------------

namespace {

constexpr std::array<size_t, SignalDependentErleEstimator::kSubbands + 1> kBandBoundaries = {1,  8,  16, 24,
                                                                                             32, 48, kFftLengthBy2Plus1};

std::array<size_t, kFftLengthBy2Plus1> FormSubbandMap() {
  std::array<size_t, kFftLengthBy2Plus1> map_band_to_subband;
  size_t subband = 1;
  for (size_t k = 0; k < map_band_to_subband.size(); ++k) {
    if (k >= kBandBoundaries[subband]) {
      subband++;
    }
    map_band_to_subband[k] = subband - 1;
  }
  return map_band_to_subband;
}

// The sizes in blocks of the sections the linear filter is divided into:
// non-linear, so that the lower sections (typically the direct path) have a
// finer resolution than the higher, more reverberant ones.
std::vector<size_t> DefineFilterSectionSizes(size_t delay_headroom_blocks, size_t num_blocks, size_t num_sections) {
  size_t filter_length_blocks = num_blocks - delay_headroom_blocks;
  std::vector<size_t> section_sizes(num_sections);
  size_t remaining_blocks = filter_length_blocks;
  size_t remaining_sections = num_sections;
  size_t estimator_size = 2;
  size_t idx = 0;
  while (remaining_sections > 1 && remaining_blocks > estimator_size * remaining_sections) {
    section_sizes[idx] = estimator_size;
    remaining_blocks -= estimator_size;
    remaining_sections--;
    estimator_size *= 2;
    idx++;
  }

  size_t last_groups_size = remaining_blocks / remaining_sections;
  for (; idx < num_sections; idx++) {
    section_sizes[idx] = last_groups_size;
  }
  section_sizes[num_sections - 1] += remaining_blocks - last_groups_size * remaining_sections;
  return section_sizes;
}

// The limits in blocks of each filter section, for finding which sections
// contribute most to the echo estimate energy.
std::vector<size_t> SetSectionsBoundaries(size_t delay_headroom_blocks, size_t num_blocks, size_t num_sections) {
  std::vector<size_t> estimator_boundaries_blocks(num_sections + 1);
  if (estimator_boundaries_blocks.size() == 2) {
    estimator_boundaries_blocks[0] = 0;
    estimator_boundaries_blocks[1] = num_blocks;
    return estimator_boundaries_blocks;
  }
  const std::vector<size_t> section_sizes =
      DefineFilterSectionSizes(delay_headroom_blocks, num_blocks, estimator_boundaries_blocks.size() - 1);

  size_t idx = 0;
  size_t current_size_block = 0;
  estimator_boundaries_blocks[0] = delay_headroom_blocks;
  for (size_t k = delay_headroom_blocks; k < num_blocks; ++k) {
    current_size_block++;
    if (current_size_block >= section_sizes[idx]) {
      idx = idx + 1;
      if (idx == section_sizes.size()) {
        break;
      }
      estimator_boundaries_blocks[idx] = k + 1;
      current_size_block = 0;
    }
  }
  estimator_boundaries_blocks[section_sizes.size()] = num_blocks;
  return estimator_boundaries_blocks;
}

std::array<float, SignalDependentErleEstimator::kSubbands> SetMaxErleSubbands(float max_erle_l, float max_erle_h,
                                                                              size_t limit_subband_l) {
  std::array<float, SignalDependentErleEstimator::kSubbands> max_erle;
  std::fill(max_erle.begin(), max_erle.begin() + static_cast<std::ptrdiff_t>(limit_subband_l), max_erle_l);
  std::fill(max_erle.begin() + static_cast<std::ptrdiff_t>(limit_subband_l), max_erle.end(), max_erle_h);
  return max_erle;
}

}  // namespace

SignalDependentErleEstimator::SignalDependentErleEstimator(const Aec3Config& config, size_t num_capture_channels)
    : min_erle_(config.erle.min),
      num_sections_(config.erle.num_sections),
      num_blocks_(config.filter.refined.length_blocks),
      delay_headroom_blocks_(config.delay.delay_headroom_samples / kBlockSize),
      band_to_subband_(FormSubbandMap()),
      max_erle_(SetMaxErleSubbands(config.erle.max_l, config.erle.max_h, band_to_subband_[kFftLengthBy2 / 2])),
      section_boundaries_blocks_(SetSectionsBoundaries(delay_headroom_blocks_, num_blocks_, num_sections_)),
      use_onset_detection_(config.erle.onset_detection),
      erle_(num_capture_channels),
      erle_onset_compensated_(num_capture_channels),
      S2_section_accum_(num_capture_channels, std::vector<std::array<float, kFftLengthBy2Plus1>>(num_sections_)),
      erle_estimators_(num_capture_channels, std::vector<std::array<float, kSubbands>>(num_sections_)),
      erle_ref_(num_capture_channels),
      correction_factors_(num_capture_channels, std::vector<std::array<float, kSubbands>>(num_sections_)),
      num_updates_(num_capture_channels),
      n_active_sections_(num_capture_channels) {
  Reset();
}

void SignalDependentErleEstimator::Reset() {
  for (size_t ch = 0; ch < erle_.size(); ++ch) {
    erle_[ch].fill(min_erle_);
    erle_onset_compensated_[ch].fill(min_erle_);
    for (auto& erle_estimator : erle_estimators_[ch]) {
      erle_estimator.fill(min_erle_);
    }
    erle_ref_[ch].fill(min_erle_);
    for (auto& factor : correction_factors_[ch]) {
      factor.fill(1.0f);
    }
    num_updates_[ch].fill(0);
    n_active_sections_[ch].fill(0);
  }
}

// Estimates how many sections of the linear filter carry most of the echo
// estimate's energy, and corrects the input ERLE by a factor learnt for that
// number of sections.
void SignalDependentErleEstimator::Update(
    const RenderBuffer& render_buffer,
    std::span<const std::vector<std::array<float, kFftLengthBy2Plus1>>> filter_frequency_responses,
    std::span<const float, kFftLengthBy2Plus1> X2, std::span<const std::array<float, kFftLengthBy2Plus1>> Y2,
    std::span<const std::array<float, kFftLengthBy2Plus1>> E2,
    std::span<const std::array<float, kFftLengthBy2Plus1>> average_erle,
    std::span<const std::array<float, kFftLengthBy2Plus1>> average_erle_onset_compensated,
    const std::vector<bool>& converged_filters) {
  // The number of filter sections needed for 90 % of the power spectrum
  // energy of the echo estimate.
  ComputeNumberOfActiveFilterSections(render_buffer, filter_frequency_responses);

  // The correction factors that adapt the ERLE to the input signal.
  UpdateCorrectionFactors(X2, Y2, E2, converged_filters);

  // Applies the correction factor to the input ERLE.
  for (size_t ch = 0; ch < erle_.size(); ++ch) {
    for (size_t k = 0; k < kFftLengthBy2; ++k) {
      float correction_factor = correction_factors_[ch][n_active_sections_[ch][k]][band_to_subband_[k]];
      erle_[ch][k] = SafeClamp(average_erle[ch][k] * correction_factor, min_erle_, max_erle_[band_to_subband_[k]]);
      if (use_onset_detection_) {
        erle_onset_compensated_[ch][k] = SafeClamp(average_erle_onset_compensated[ch][k] * correction_factor,
                                                   min_erle_, max_erle_[band_to_subband_[k]]);
      }
    }
  }
}

// For each band, the smallest number of filter sections that together carry
// 90 % of the estimated echo energy.
void SignalDependentErleEstimator::ComputeNumberOfActiveFilterSections(
    const RenderBuffer& render_buffer,
    std::span<const std::vector<std::array<float, kFftLengthBy2Plus1>>> filter_frequency_responses) {
  // The power spectrum of the echo estimate with the filter limited to a
  // number of sections.
  ComputeEchoEstimatePerFilterSection(render_buffer, filter_frequency_responses);

  // The number of sections needed per band for 90 % of that energy.
  ComputeActiveFilterSections();
}

void SignalDependentErleEstimator::UpdateCorrectionFactors(
    std::span<const float, kFftLengthBy2Plus1> X2, std::span<const std::array<float, kFftLengthBy2Plus1>> Y2,
    std::span<const std::array<float, kFftLengthBy2Plus1>> E2, const std::vector<bool>& converged_filters) {
  for (size_t ch = 0; ch < converged_filters.size(); ++ch) {
    if (converged_filters[ch]) {
      constexpr float kX2BandEnergyThreshold = 44015068.0f;
      constexpr float kSmthConstantDecreases = 0.1f;
      constexpr float kSmthConstantIncreases = kSmthConstantDecreases / 2.f;
      auto subband_powers = [](std::span<const float> power_spectrum, std::span<float> power_spectrum_subbands) {
        for (size_t subband = 0; subband < kSubbands; ++subband) {
          power_spectrum_subbands[subband] =
              std::accumulate(power_spectrum.begin() + static_cast<std::ptrdiff_t>(kBandBoundaries[subband]),
                              power_spectrum.begin() + static_cast<std::ptrdiff_t>(kBandBoundaries[subband + 1]), 0.f);
        }
      };

      std::array<float, kSubbands> X2_subbands, E2_subbands, Y2_subbands;
      subband_powers(X2, X2_subbands);
      subband_powers(E2[ch], E2_subbands);
      subband_powers(Y2[ch], Y2_subbands);
      std::array<size_t, kSubbands> idx_subbands;
      for (size_t subband = 0; subband < kSubbands; ++subband) {
        // The number of active sections of a subband is the minimum over its
        // bands: if the direct path is the main contributor to one band's
        // echo estimate, it is taken as the main contributor for the whole
        // subband. That number picks the ERLE estimator to update.
        idx_subbands[subband] =
            *std::min_element(n_active_sections_[ch].begin() + static_cast<std::ptrdiff_t>(kBandBoundaries[subband]),
                              n_active_sections_[ch].begin() + static_cast<std::ptrdiff_t>(kBandBoundaries[subband + 1]));
      }

      std::array<float, kSubbands> new_erle;
      std::array<bool, kSubbands> is_erle_updated;
      is_erle_updated.fill(false);
      new_erle.fill(0.f);
      for (size_t subband = 0; subband < kSubbands; ++subband) {
        if (X2_subbands[subband] > kX2BandEnergyThreshold && E2_subbands[subband] > 0) {
          new_erle[subband] = Y2_subbands[subband] / E2_subbands[subband];
          is_erle_updated[subband] = true;
          ++num_updates_[ch][subband];
        }
      }

      for (size_t subband = 0; subband < kSubbands; ++subband) {
        const size_t idx = idx_subbands[subband];
        float alpha =
            new_erle[subband] > erle_estimators_[ch][idx][subband] ? kSmthConstantIncreases : kSmthConstantDecreases;
        alpha = static_cast<float>(is_erle_updated[subband]) * alpha;
        erle_estimators_[ch][idx][subband] += alpha * (new_erle[subband] - erle_estimators_[ch][idx][subband]);
        erle_estimators_[ch][idx][subband] =
            SafeClamp(erle_estimators_[ch][idx][subband], min_erle_, max_erle_[subband]);
      }

      for (size_t subband = 0; subband < kSubbands; ++subband) {
        float alpha = new_erle[subband] > erle_ref_[ch][subband] ? kSmthConstantIncreases : kSmthConstantDecreases;
        alpha = static_cast<float>(is_erle_updated[subband]) * alpha;
        erle_ref_[ch][subband] += alpha * (new_erle[subband] - erle_ref_[ch][subband]);
        erle_ref_[ch][subband] = SafeClamp(erle_ref_[ch][subband], min_erle_, max_erle_[subband]);
      }

      for (size_t subband = 0; subband < kSubbands; ++subband) {
        constexpr int kNumUpdateThr = 50;
        if (is_erle_updated[subband] && num_updates_[ch][subband] > kNumUpdateThr) {
          const size_t idx = idx_subbands[subband];
          // The ratio of the ERLE updated on signals sharing this number of
          // active sections to the ERLE updated on all of them.
          float new_correction_factor = erle_estimators_[ch][idx][subband] / erle_ref_[ch][subband];

          correction_factors_[ch][idx][subband] += 0.1f * (new_correction_factor - correction_factors_[ch][idx][subband]);
        }
      }
    }
  }
}

void SignalDependentErleEstimator::ComputeEchoEstimatePerFilterSection(
    const RenderBuffer& render_buffer,
    std::span<const std::vector<std::array<float, kFftLengthBy2Plus1>>> filter_frequency_responses) {
  const SpectrumBuffer& spectrum_render_buffer = render_buffer.GetSpectrumBuffer();
  const size_t num_render_channels = spectrum_render_buffer.buffer[0].size();
  const size_t num_capture_channels = S2_section_accum_.size();
  const float one_by_num_render_channels = 1.f / static_cast<float>(num_render_channels);

  for (size_t capture_ch = 0; capture_ch < num_capture_channels; ++capture_ch) {
    size_t idx_render = render_buffer.Position();
    idx_render = static_cast<size_t>(spectrum_render_buffer.OffsetIndex(
        static_cast<int>(idx_render), static_cast<int>(section_boundaries_blocks_[0])));

    for (size_t section = 0; section < num_sections_; ++section) {
      std::array<float, kFftLengthBy2Plus1> X2_section;
      std::array<float, kFftLengthBy2Plus1> H2_section;
      X2_section.fill(0.f);
      H2_section.fill(0.f);
      const size_t block_limit =
          std::min(section_boundaries_blocks_[section + 1], filter_frequency_responses[capture_ch].size());
      for (size_t block = section_boundaries_blocks_[section]; block < block_limit; ++block) {
        for (size_t render_ch = 0; render_ch < spectrum_render_buffer.buffer[idx_render].size(); ++render_ch) {
          for (size_t k = 0; k < X2_section.size(); ++k) {
            X2_section[k] += spectrum_render_buffer.buffer[idx_render][render_ch][k] * one_by_num_render_channels;
          }
        }
        std::transform(H2_section.begin(), H2_section.end(), filter_frequency_responses[capture_ch][block].begin(),
                       H2_section.begin(), std::plus<float>());
        idx_render = static_cast<size_t>(spectrum_render_buffer.IncIndex(static_cast<int>(idx_render)));
      }

      std::transform(X2_section.begin(), X2_section.end(), H2_section.begin(),
                     S2_section_accum_[capture_ch][section].begin(), std::multiplies<float>());
    }

    for (size_t section = 1; section < num_sections_; ++section) {
      std::transform(S2_section_accum_[capture_ch][section - 1].begin(),
                     S2_section_accum_[capture_ch][section - 1].end(), S2_section_accum_[capture_ch][section].begin(),
                     S2_section_accum_[capture_ch][section].begin(), std::plus<float>());
    }
  }
}

void SignalDependentErleEstimator::ComputeActiveFilterSections() {
  for (size_t ch = 0; ch < n_active_sections_.size(); ++ch) {
    std::fill(n_active_sections_[ch].begin(), n_active_sections_[ch].end(), 0);
    for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
      size_t section = num_sections_;
      float target = 0.9f * S2_section_accum_[ch][num_sections_ - 1][k];
      while (section > 0 && S2_section_accum_[ch][section - 1][k] >= target) {
        n_active_sections_[ch][k] = --section;
      }
    }
  }
}

// --- erle_estimator.cc -------------------------------------------------------

ErleEstimator::ErleEstimator(size_t startup_phase_length_blocks, const Aec3Config& config,
                             size_t num_capture_channels)
    : startup_phase_length_blocks_(startup_phase_length_blocks),
      fullband_erle_estimator_(config.erle, num_capture_channels),
      subband_erle_estimator_(config, num_capture_channels) {
  if (config.erle.num_sections > 1) {
    signal_dependent_erle_estimator_ = std::make_unique<SignalDependentErleEstimator>(config, num_capture_channels);
  }
  Reset(true);
}

void ErleEstimator::Reset(bool delay_change) {
  fullband_erle_estimator_.Reset();
  subband_erle_estimator_.Reset();
  if (signal_dependent_erle_estimator_) {
    signal_dependent_erle_estimator_->Reset();
  }
  if (delay_change) {
    blocks_since_reset_ = 0;
  }
}

void ErleEstimator::Update(
    const RenderBuffer& render_buffer,
    std::span<const std::vector<std::array<float, kFftLengthBy2Plus1>>> filter_frequency_responses,
    std::span<const float, kFftLengthBy2Plus1> avg_render_spectrum_with_reverb,
    std::span<const std::array<float, kFftLengthBy2Plus1>> capture_spectra,
    std::span<const std::array<float, kFftLengthBy2Plus1>> subtractor_spectra,
    const std::vector<bool>& converged_filters) {
  const auto& X2_reverb = avg_render_spectrum_with_reverb;
  const auto& Y2 = capture_spectra;
  const auto& E2 = subtractor_spectra;

  if (++blocks_since_reset_ < startup_phase_length_blocks_) {
    return;
  }

  subband_erle_estimator_.Update(X2_reverb, Y2, E2, converged_filters);

  if (signal_dependent_erle_estimator_) {
    signal_dependent_erle_estimator_->Update(render_buffer, filter_frequency_responses, X2_reverb, Y2, E2,
                                             subband_erle_estimator_.Erle(/*onset_compensated=*/false),
                                             subband_erle_estimator_.Erle(/*onset_compensated=*/true),
                                             converged_filters);
  }

  fullband_erle_estimator_.Update(X2_reverb, Y2, E2, converged_filters);
}

}  // namespace sm::audio::aec3
