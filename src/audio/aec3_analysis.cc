// AEC3's echo-path analysis, ported from libwebrtc M157's
// modules/audio_processing/aec3/ (see sm/audio/aec3_analysis.h for the files).
#include "sm/audio/aec3_analysis.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace sm::audio::aec3 {

// --- filter_analyzer.cc ------------------------------------------------------

namespace {

size_t FindPeakIndex(std::span<const float> filter_time_domain, size_t peak_index_in, size_t start_sample,
                     size_t end_sample) {
  size_t peak_index_out = peak_index_in;
  float max_h2 = filter_time_domain[peak_index_out] * filter_time_domain[peak_index_out];
  for (size_t k = start_sample; k <= end_sample; ++k) {
    float tmp = filter_time_domain[k] * filter_time_domain[k];
    if (tmp > max_h2) {
      peak_index_out = k;
      max_h2 = tmp;
    }
  }
  return peak_index_out;
}

}  // namespace

FilterAnalyzer::FilterAnalyzer(const Aec3Config& config, size_t num_capture_channels)
    : bounded_erl_(config.ep_strength.bounded_erl),
      default_gain_(config.ep_strength.default_gain),
      h_highpass_(num_capture_channels,
                  std::vector<float>(
                      static_cast<size_t>(GetTimeDomainLength(static_cast<int>(config.filter.refined.length_blocks))),
                      0.f)),
      filter_analysis_states_(num_capture_channels, FilterAnalysisState(config)),
      filter_delays_blocks_(num_capture_channels, 0) {
  Reset();
}

void FilterAnalyzer::Reset() {
  blocks_since_reset_ = 0;
  ResetRegion();
  for (auto& state : filter_analysis_states_) state.Reset(default_gain_);
  std::fill(filter_delays_blocks_.begin(), filter_delays_blocks_.end(), 0);
}

void FilterAnalyzer::Update(std::span<const std::vector<float>> filters_time_domain,
                            const RenderBuffer& render_buffer, bool* any_filter_consistent,
                            float* max_echo_path_gain) {
  ++blocks_since_reset_;
  SetRegionToAnalyze(filters_time_domain[0].size());
  AnalyzeRegion(filters_time_domain, render_buffer);

  // Aggregate the results for all capture channels.
  auto& st_ch0 = filter_analysis_states_[0];
  *any_filter_consistent = st_ch0.consistent_estimate;
  *max_echo_path_gain = st_ch0.gain;
  min_filter_delay_blocks_ = filter_delays_blocks_[0];
  for (size_t ch = 1; ch < filters_time_domain.size(); ++ch) {
    auto& st_ch = filter_analysis_states_[ch];
    *any_filter_consistent = *any_filter_consistent || st_ch.consistent_estimate;
    *max_echo_path_gain = std::max(*max_echo_path_gain, st_ch.gain);
    min_filter_delay_blocks_ = std::min(min_filter_delay_blocks_, filter_delays_blocks_[ch]);
  }
}

void FilterAnalyzer::AnalyzeRegion(std::span<const std::vector<float>> filters_time_domain,
                                   const RenderBuffer& render_buffer) {
  // Preprocess the filter to avoid issues with low-frequency components in the
  // filter.
  PreProcessFilters(filters_time_domain);

  constexpr float kOneByBlockSize = 1.f / kBlockSize;
  for (size_t ch = 0; ch < filters_time_domain.size(); ++ch) {
    auto& st_ch = filter_analysis_states_[ch];
    st_ch.peak_index = std::min(st_ch.peak_index, h_highpass_[ch].size() - 1);

    st_ch.peak_index = FindPeakIndex(h_highpass_[ch], st_ch.peak_index, region_.start_sample_, region_.end_sample_);
    filter_delays_blocks_[ch] = static_cast<int>(st_ch.peak_index >> kBlockSizeLog2);
    UpdateFilterGain(h_highpass_[ch], &st_ch);
    st_ch.filter_length_blocks =
        static_cast<int>(static_cast<float>(filters_time_domain[ch].size()) * kOneByBlockSize);

    st_ch.consistent_estimate = st_ch.consistent_filter_detector.Detect(
        h_highpass_[ch], region_, render_buffer.GetBlock(-filter_delays_blocks_[ch]), st_ch.peak_index,
        filter_delays_blocks_[ch]);
  }
}

void FilterAnalyzer::UpdateFilterGain(std::span<const float> filter_time_domain, FilterAnalysisState* st) {
  bool sufficient_time_to_converge = blocks_since_reset_ > 5 * kNumBlocksPerSecond;

  if (sufficient_time_to_converge && st->consistent_estimate) {
    st->gain = std::fabs(filter_time_domain[st->peak_index]);
  } else {
    // libwebrtc: "TODO(peah): Verify whether this check against a float is ok."
    if (st->gain != 0.f) {
      st->gain = std::max(st->gain, std::fabs(filter_time_domain[st->peak_index]));
    }
  }

  if (bounded_erl_ && st->gain != 0.f) {
    st->gain = std::max(st->gain, 0.01f);
  }
}

void FilterAnalyzer::PreProcessFilters(std::span<const std::vector<float>> filters_time_domain) {
  for (size_t ch = 0; ch < filters_time_domain.size(); ++ch) {
    h_highpass_[ch].resize(filters_time_domain[ch].size());
    // Minimum phase high-pass filter with cutoff frequency at about 600 Hz.
    constexpr std::array<float, 3> h = {{0.7929742f, -0.36072128f, -0.47047766f}};

    std::fill(h_highpass_[ch].begin() + static_cast<std::ptrdiff_t>(region_.start_sample_),
              h_highpass_[ch].begin() + static_cast<std::ptrdiff_t>(region_.end_sample_ + 1), 0.f);
    float* h_highpass_ch = h_highpass_[ch].data();
    const float* filters_time_domain_ch = filters_time_domain[ch].data();
    const size_t region_end = region_.end_sample_;
    for (size_t k = std::max(h.size() - 1, region_.start_sample_); k <= region_end; ++k) {
      float tmp = h_highpass_ch[k];
      for (size_t j = 0; j < h.size(); ++j) tmp += filters_time_domain_ch[k - j] * h[j];
      h_highpass_ch[k] = tmp;
    }
  }
}

void FilterAnalyzer::ResetRegion() {
  region_.start_sample_ = 0;
  region_.end_sample_ = 0;
}

void FilterAnalyzer::SetRegionToAnalyze(size_t filter_size) {
  constexpr size_t kNumberBlocksToUpdate = 1;
  auto& r = region_;
  r.start_sample_ = r.end_sample_ >= filter_size - 1 ? 0 : r.end_sample_ + 1;
  r.end_sample_ = std::min(r.start_sample_ + kNumberBlocksToUpdate * kBlockSize - 1, filter_size - 1);
}

FilterAnalyzer::ConsistentFilterDetector::ConsistentFilterDetector(const Aec3Config& config)
    : active_render_threshold_(config.active_render_limit * config.active_render_limit * kFftLengthBy2) {
  Reset();
}

void FilterAnalyzer::ConsistentFilterDetector::Reset() {
  significant_peak_ = false;
  filter_floor_accum_ = 0.f;
  filter_secondary_peak_ = 0.f;
  filter_floor_low_limit_ = 0;
  filter_floor_high_limit_ = 0;
  consistent_estimate_counter_ = 0;
  consistent_delay_reference_ = -10;
}

bool FilterAnalyzer::ConsistentFilterDetector::Detect(std::span<const float> filter_to_analyze,
                                                      const FilterRegion& region, const Block& x_block,
                                                      size_t peak_index, int delay_blocks) {
  if (region.start_sample_ == 0) {
    filter_floor_accum_ = 0.f;
    filter_secondary_peak_ = 0.f;
    filter_floor_low_limit_ = peak_index < 64 ? 0 : peak_index - 64;
    filter_floor_high_limit_ = peak_index > filter_to_analyze.size() - 129 ? 0 : peak_index + 128;
  }

  float filter_floor_accum = filter_floor_accum_;
  float filter_secondary_peak = filter_secondary_peak_;
  for (size_t k = region.start_sample_; k < std::min(region.end_sample_ + 1, filter_floor_low_limit_); ++k) {
    float abs_h = std::fabs(filter_to_analyze[k]);
    filter_floor_accum += abs_h;
    filter_secondary_peak = std::max(filter_secondary_peak, abs_h);
  }

  for (size_t k = std::max(filter_floor_high_limit_, region.start_sample_); k <= region.end_sample_; ++k) {
    float abs_h = std::fabs(filter_to_analyze[k]);
    filter_floor_accum += abs_h;
    filter_secondary_peak = std::max(filter_secondary_peak, abs_h);
  }
  filter_floor_accum_ = filter_floor_accum;
  filter_secondary_peak_ = filter_secondary_peak;

  if (region.end_sample_ == filter_to_analyze.size() - 1) {
    float filter_floor = filter_floor_accum_ / static_cast<float>(filter_floor_low_limit_ + filter_to_analyze.size() -
                                                                  filter_floor_high_limit_);

    float abs_peak = std::fabs(filter_to_analyze[peak_index]);
    significant_peak_ = abs_peak > 10.f * filter_floor && abs_peak > 2.f * filter_secondary_peak_;
  }

  if (significant_peak_) {
    bool active_render_block = false;
    for (int ch = 0; ch < x_block.NumChannels(); ++ch) {
      std::span<const float, kBlockSize> x_channel = x_block.View(/*band=*/0, ch);
      const float x_energy = std::inner_product(x_channel.begin(), x_channel.end(), x_channel.begin(), 0.f);
      if (x_energy > active_render_threshold_) {
        active_render_block = true;
        break;
      }
    }

    if (consistent_delay_reference_ == delay_blocks) {
      if (active_render_block) ++consistent_estimate_counter_;
    } else {
      consistent_estimate_counter_ = 0;
      consistent_delay_reference_ = delay_blocks;
    }
  }
  return static_cast<float>(consistent_estimate_counter_) > 1.5f * kNumBlocksPerSecond;
}

// --- stationarity_estimator.cc -----------------------------------------------

namespace {
constexpr float kMinNoisePower = 10.f;
constexpr int kHangoverBlocks = kNumBlocksPerSecond / 20;
constexpr int kNBlocksAverageInitPhase = 20;
constexpr int kNBlocksInitialPhase = static_cast<int>(kNumBlocksPerSecond * 2.);
}  // namespace

StationarityEstimator::StationarityEstimator() { Reset(); }

void StationarityEstimator::Reset() {
  noise_.Reset();
  hangovers_.fill(0);
  stationarity_flags_.fill(false);
}

void StationarityEstimator::UpdateNoiseEstimator(std::span<const std::array<float, kFftLengthBy2Plus1>> spectrum) {
  noise_.Update(spectrum);
}

void StationarityEstimator::UpdateStationarityFlags(const SpectrumBuffer& spectrum_buffer,
                                                    std::span<const float> render_reverb_contribution_spectrum,
                                                    int idx_current, int num_lookahead) {
  std::array<int, kWindowLength> indexes;
  int num_lookahead_bounded = std::min(num_lookahead, kWindowLength - 1);
  int idx = idx_current;

  if (num_lookahead_bounded < kWindowLength - 1) {
    int num_lookback = (kWindowLength - 1) - num_lookahead_bounded;
    idx = spectrum_buffer.OffsetIndex(idx_current, num_lookback);
  }
  // The indexes of the spectra accumulated per band in
  // EstimateBandStationarity, computed once for all bands.
  indexes[0] = idx;
  for (size_t k = 1; k < indexes.size(); ++k) indexes[k] = spectrum_buffer.DecIndex(indexes[k - 1]);

  for (size_t k = 0; k < stationarity_flags_.size(); ++k) {
    stationarity_flags_[k] =
        EstimateBandStationarity(spectrum_buffer, render_reverb_contribution_spectrum, indexes, k);
  }
  UpdateHangover();
  SmoothStationaryPerFreq();
}

bool StationarityEstimator::IsBlockStationary() const {
  float acum_stationarity = 0.f;
  for (size_t band = 0; band < stationarity_flags_.size(); ++band) {
    bool st = IsBandStationary(band);
    acum_stationarity += static_cast<float>(st);
  }
  return ((acum_stationarity * (1.f / kFftLengthBy2Plus1)) > 0.75f);
}

bool StationarityEstimator::EstimateBandStationarity(const SpectrumBuffer& spectrum_buffer,
                                                     std::span<const float> average_reverb,
                                                     const std::array<int, kWindowLength>& indexes,
                                                     size_t band) const {
  constexpr float kThrStationarity = 10.f;
  float acum_power = 0.f;
  const int num_render_channels = static_cast<int>(spectrum_buffer.buffer[0].size());
  const float one_by_num_channels = 1.f / static_cast<float>(num_render_channels);
  for (auto idx : indexes) {
    for (int ch = 0; ch < num_render_channels; ++ch) {
      acum_power += spectrum_buffer.buffer[static_cast<size_t>(idx)][static_cast<size_t>(ch)][band] *
                    one_by_num_channels;
    }
  }
  acum_power += average_reverb[band];
  float noise = kWindowLength * GetStationarityPowerBand(band);
  bool stationary = acum_power < kThrStationarity * noise;
  return stationary;
}

bool StationarityEstimator::AreAllBandsStationary() {
  for (auto b : stationarity_flags_) {
    if (!b) return false;
  }
  return true;
}

void StationarityEstimator::UpdateHangover() {
  bool reduce_hangover = AreAllBandsStationary();
  for (size_t k = 0; k < stationarity_flags_.size(); ++k) {
    if (!stationarity_flags_[k]) {
      hangovers_[k] = kHangoverBlocks;
    } else if (reduce_hangover) {
      hangovers_[k] = std::max(hangovers_[k] - 1, 0);
    }
  }
}

void StationarityEstimator::SmoothStationaryPerFreq() {
  std::array<bool, kFftLengthBy2Plus1> all_ahead_stationary_smooth;
  for (size_t k = 1; k < kFftLengthBy2Plus1 - 1; ++k) {
    all_ahead_stationary_smooth[k] = stationarity_flags_[k - 1] && stationarity_flags_[k] && stationarity_flags_[k + 1];
  }

  all_ahead_stationary_smooth[0] = all_ahead_stationary_smooth[1];
  all_ahead_stationary_smooth[kFftLengthBy2Plus1 - 1] = all_ahead_stationary_smooth[kFftLengthBy2Plus1 - 2];

  stationarity_flags_ = all_ahead_stationary_smooth;
}

StationarityEstimator::NoiseSpectrum::NoiseSpectrum() { Reset(); }

void StationarityEstimator::NoiseSpectrum::Reset() {
  block_counter_ = 0;
  noise_spectrum_.fill(kMinNoisePower);
}

void StationarityEstimator::NoiseSpectrum::Update(std::span<const std::array<float, kFftLengthBy2Plus1>> spectrum) {
  const int num_render_channels = static_cast<int>(spectrum.size());

  std::array<float, kFftLengthBy2Plus1> avg_spectrum_data;
  std::span<const float> avg_spectrum;
  if (num_render_channels == 1) {
    avg_spectrum = spectrum[0];
  } else {
    // For multiple channels, average the channel spectra before passing to the
    // noise spectrum estimator.
    avg_spectrum = avg_spectrum_data;
    std::copy(spectrum[0].begin(), spectrum[0].end(), avg_spectrum_data.begin());
    for (int ch = 1; ch < num_render_channels; ++ch) {
      for (size_t k = 1; k < kFftLengthBy2Plus1; ++k) avg_spectrum_data[k] += spectrum[static_cast<size_t>(ch)][k];
    }

    const float one_by_num_channels = 1.f / static_cast<float>(num_render_channels);
    for (size_t k = 1; k < kFftLengthBy2Plus1; ++k) avg_spectrum_data[k] *= one_by_num_channels;
  }

  ++block_counter_;
  float alpha = GetAlpha();
  for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
    if (block_counter_ <= kNBlocksAverageInitPhase) {
      noise_spectrum_[k] += (1.f / kNBlocksAverageInitPhase) * avg_spectrum[k];
    } else {
      noise_spectrum_[k] = UpdateBandBySmoothing(avg_spectrum[k], noise_spectrum_[k], alpha);
    }
  }
}

float StationarityEstimator::NoiseSpectrum::GetAlpha() const {
  constexpr float kAlpha = 0.004f;
  constexpr float kAlphaInit = 0.04f;
  constexpr float kTiltAlpha = (kAlphaInit - kAlpha) / kNBlocksInitialPhase;

  if (block_counter_ > (kNBlocksInitialPhase + kNBlocksAverageInitPhase)) {
    return kAlpha;
  } else {
    // size_t arithmetic as libwebrtc's (wraps while block_counter_ <= 20, when
    // the value isn't used).
    return kAlphaInit -
           kTiltAlpha * static_cast<float>(block_counter_ - static_cast<size_t>(kNBlocksAverageInitPhase));
  }
}

float StationarityEstimator::NoiseSpectrum::UpdateBandBySmoothing(float power_band, float power_band_noise,
                                                                  float alpha) const {
  float power_band_noise_updated = power_band_noise;
  if (power_band_noise < power_band) {
    float alpha_inc = alpha * (power_band_noise / power_band);
    if (block_counter_ > kNBlocksInitialPhase) {
      if (10.f * power_band_noise < power_band) alpha_inc *= 0.1f;
    }
    power_band_noise_updated += alpha_inc * (power_band - power_band_noise);
  } else {
    power_band_noise_updated += alpha * (power_band - power_band_noise);
    power_band_noise_updated = std::max(power_band_noise_updated, kMinNoisePower);
  }
  return power_band_noise_updated;
}

// --- echo_audibility.cc ------------------------------------------------------

EchoAudibility::EchoAudibility(bool use_render_stationarity_at_init)
    : use_render_stationarity_at_init_(use_render_stationarity_at_init) {
  Reset();
}

void EchoAudibility::Update(const RenderBuffer& render_buffer, std::span<const float> average_reverb,
                            int min_channel_delay_blocks, bool external_delay_seen) {
  UpdateRenderNoiseEstimator(render_buffer.GetSpectrumBuffer(), render_buffer.GetBlockBuffer(), external_delay_seen);

  if (external_delay_seen || use_render_stationarity_at_init_) {
    UpdateRenderStationarityFlags(render_buffer, average_reverb, min_channel_delay_blocks);
  }
}

void EchoAudibility::Reset() {
  render_stationarity_.Reset();
  non_zero_render_seen_ = false;
  render_spectrum_write_prev_ = std::nullopt;
}

void EchoAudibility::UpdateRenderStationarityFlags(const RenderBuffer& render_buffer,
                                                   std::span<const float> average_reverb,
                                                   int min_channel_delay_blocks) {
  const SpectrumBuffer& spectrum_buffer = render_buffer.GetSpectrumBuffer();
  int idx_at_delay = spectrum_buffer.OffsetIndex(spectrum_buffer.read, min_channel_delay_blocks);

  int num_lookahead = render_buffer.Headroom() - min_channel_delay_blocks + 1;
  num_lookahead = std::max(0, num_lookahead);

  render_stationarity_.UpdateStationarityFlags(spectrum_buffer, average_reverb, idx_at_delay, num_lookahead);
}

void EchoAudibility::UpdateRenderNoiseEstimator(const SpectrumBuffer& spectrum_buffer,
                                                const BlockBuffer& block_buffer, bool external_delay_seen) {
  if (!render_spectrum_write_prev_) {
    render_spectrum_write_prev_ = spectrum_buffer.write;
    render_block_write_prev_ = block_buffer.write;
    return;
  }
  int render_spectrum_write_current = spectrum_buffer.write;
  if (!non_zero_render_seen_ && !external_delay_seen) {
    non_zero_render_seen_ = !IsRenderTooLow(block_buffer);
  }
  if (non_zero_render_seen_) {
    for (int idx = render_spectrum_write_prev_.value(); idx != render_spectrum_write_current;
         idx = spectrum_buffer.DecIndex(idx)) {
      render_stationarity_.UpdateNoiseEstimator(spectrum_buffer.buffer[static_cast<size_t>(idx)]);
    }
  }
  render_spectrum_write_prev_ = render_spectrum_write_current;
}

bool EchoAudibility::IsRenderTooLow(const BlockBuffer& block_buffer) {
  const int num_render_channels = block_buffer.buffer[0].NumChannels();
  bool too_low = false;
  const int render_block_write_current = block_buffer.write;
  if (render_block_write_current == render_block_write_prev_) {
    too_low = true;
  } else {
    for (int idx = render_block_write_prev_; idx != render_block_write_current; idx = block_buffer.IncIndex(idx)) {
      float max_abs_over_channels = 0.f;
      for (int ch = 0; ch < num_render_channels; ++ch) {
        std::span<const float, kBlockSize> block =
            block_buffer.buffer[static_cast<size_t>(idx)].View(/*band=*/0, /*channel=*/ch);
        auto r = std::minmax_element(block.begin(), block.end());
        float max_abs_channel = std::max(std::fabs(*r.first), std::fabs(*r.second));
        max_abs_over_channels = std::max(max_abs_over_channels, max_abs_channel);
      }
      if (max_abs_over_channels < 10.f) {
        too_low = true;  // Discards all blocks if one of them is too low.
        break;
      }
    }
  }
  render_block_write_prev_ = render_block_write_current;
  return too_low;
}

// --- transparent_mode.cc -----------------------------------------------------

namespace {

constexpr size_t kBlocksSinceConvergencedFilterInit = 10000;
constexpr size_t kBlocksSinceConsistentEstimateInit = 10000;
constexpr float kInitialTransparentStateProbability = 0.2f;

// Classifier that toggles transparent mode, which reduces echo suppression
// when headsets are used.
class TransparentModeImpl : public TransparentMode {
 public:
  bool Active() const override { return transparency_activated_; }

  void Reset() override {
    // Determines if transparent mode is used.
    transparency_activated_ = false;
    // The estimated probability of being transparent mode.
    prob_transparent_state_ = kInitialTransparentStateProbability;
  }

  void Update(int /* filter_delay_blocks */, bool /* any_filter_consistent */, bool /* any_filter_converged */,
              bool any_coarse_filter_converged, bool /* all_filters_diverged */, bool active_render,
              bool /* saturated_capture */) override {
    // A Hidden Markov Model with two hidden states, "normal" and
    // "transparent", whose probabilities are updated by observing filter
    // convergence during active render. The filters are less likely to be
    // reported as converged when there is no echo in the microphone signal.
    // The constants are hand tuned to prefer the normal state during uncertain
    // regions (to avoid echo leaks).

    // The model is only updated during active render.
    if (!active_render) return;

    // Probability of switching from one state to the other.
    constexpr float kSwitch = 0.000001f;

    // Probability of observing converged filters in states "normal" and
    // "transparent" during active render.
    constexpr float kConvergedNormal = 0.01f;
    constexpr float kConvergedTransparent = 0.001f;

    // Probability of transitioning to transparent state from normal state and
    // transparent state respectively.
    constexpr float kA[2] = {kSwitch, 1.f - kSwitch};

    // Probability of the two observations (converged filter or not converged
    // filter) in normal state and transparent state respectively.
    constexpr float kB[2][2] = {{1.f - kConvergedNormal, kConvergedNormal},
                                {1.f - kConvergedTransparent, kConvergedTransparent}};

    // Probability of the two states before the update.
    const float prob_transparent = prob_transparent_state_;
    const float prob_normal = 1.f - prob_transparent;

    // Probability of transitioning to transparent state.
    const float prob_transition_transparent = prob_normal * kA[0] + prob_transparent * kA[1];
    const float prob_transition_normal = 1.f - prob_transition_transparent;

    // Observed output.
    const int out = static_cast<int>(any_coarse_filter_converged);

    // Joint probabilites of the observed output and respective states.
    const float prob_joint_normal = prob_transition_normal * kB[0][out];
    const float prob_joint_transparent = prob_transition_transparent * kB[1][out];

    // Conditional probability of transparent state and the observed output.
    prob_transparent_state_ = prob_joint_transparent / (prob_joint_normal + prob_joint_transparent);

    // Transparent mode is only activated when its state probability is high.
    // Dead zone between activation/deactivation thresholds to avoid switching
    // back and forth.
    if (prob_transparent_state_ > 0.95f) {
      transparency_activated_ = true;
    } else if (prob_transparent_state_ < 0.5f) {
      transparency_activated_ = false;
    }
  }

 private:
  bool transparency_activated_ = false;
  float prob_transparent_state_ = kInitialTransparentStateProbability;
};

// Legacy classifier for toggling transparent mode.
class LegacyTransparentModeImpl : public TransparentMode {
 public:
  explicit LegacyTransparentModeImpl(const Aec3Config& config)
      : linear_and_stable_echo_path_(config.echo_removal_control.linear_and_stable_echo_path),
        active_blocks_since_sane_filter_(kBlocksSinceConsistentEstimateInit),
        non_converged_sequence_size_(kBlocksSinceConvergencedFilterInit) {}

  bool Active() const override { return transparency_activated_; }

  void Reset() override {
    non_converged_sequence_size_ = kBlocksSinceConvergencedFilterInit;
    diverged_sequence_size_ = 0;
    strong_not_saturated_render_blocks_ = 0;
    if (linear_and_stable_echo_path_) recent_convergence_during_activity_ = false;
  }

  void Update(int filter_delay_blocks, bool any_filter_consistent, bool any_filter_converged,
              bool /* any_coarse_filter_converged */, bool all_filters_diverged, bool active_render,
              bool saturated_capture) override {
    ++capture_block_counter_;
    strong_not_saturated_render_blocks_ += active_render && !saturated_capture ? 1 : 0;

    if (any_filter_consistent && filter_delay_blocks < 5) {
      sane_filter_observed_ = true;
      active_blocks_since_sane_filter_ = 0;
    } else if (active_render) {
      ++active_blocks_since_sane_filter_;
    }

    bool sane_filter_recently_seen;
    if (!sane_filter_observed_) {
      sane_filter_recently_seen = capture_block_counter_ <= 5 * kNumBlocksPerSecond;
    } else {
      sane_filter_recently_seen = active_blocks_since_sane_filter_ <= 30 * kNumBlocksPerSecond;
    }

    if (any_filter_converged) {
      recent_convergence_during_activity_ = true;
      active_non_converged_sequence_size_ = 0;
      non_converged_sequence_size_ = 0;
      ++num_converged_blocks_;
    } else {
      if (++non_converged_sequence_size_ > 20 * kNumBlocksPerSecond) num_converged_blocks_ = 0;

      if (active_render && ++active_non_converged_sequence_size_ > 60 * kNumBlocksPerSecond) {
        recent_convergence_during_activity_ = false;
      }
    }

    if (!all_filters_diverged) {
      diverged_sequence_size_ = 0;
    } else if (++diverged_sequence_size_ >= 60) {
      // libwebrtc: "TODO(peah): Change these lines to ensure proper triggering
      // of usable filter."
      non_converged_sequence_size_ = kBlocksSinceConvergencedFilterInit;
    }

    if (active_non_converged_sequence_size_ > 60 * kNumBlocksPerSecond) finite_erl_recently_detected_ = false;
    if (num_converged_blocks_ > 50) finite_erl_recently_detected_ = true;

    if (finite_erl_recently_detected_) {
      transparency_activated_ = false;
    } else if (sane_filter_recently_seen && recent_convergence_during_activity_) {
      transparency_activated_ = false;
    } else {
      const bool filter_should_have_converged = strong_not_saturated_render_blocks_ > 6 * kNumBlocksPerSecond;
      transparency_activated_ = filter_should_have_converged;
    }
  }

 private:
  const bool linear_and_stable_echo_path_;
  size_t capture_block_counter_ = 0;
  bool transparency_activated_ = false;
  size_t active_blocks_since_sane_filter_;
  bool sane_filter_observed_ = false;
  bool finite_erl_recently_detected_ = false;
  size_t non_converged_sequence_size_;
  size_t diverged_sequence_size_ = 0;
  size_t active_non_converged_sequence_size_ = 0;
  size_t num_converged_blocks_ = 0;
  bool recent_convergence_during_activity_ = false;
  size_t strong_not_saturated_render_blocks_ = 0;
};

}  // namespace

std::unique_ptr<TransparentMode> TransparentMode::Create(const Aec3Config& config) {
  // "AEC3 Transparent Mode: Disabled"
  if (config.ep_strength.bounded_erl) return nullptr;
  // M157 returns the HMM classifier when the field trial
  // WebRTC-Aec3TransparentModeHmm is enabled; it isn't by default.
  return std::make_unique<LegacyTransparentModeImpl>(config);
}

std::unique_ptr<TransparentMode> TransparentMode::CreateHmm() { return std::make_unique<TransparentModeImpl>(); }

// --- subtractor_output_analyzer.cc -------------------------------------------

SubtractorOutputAnalyzer::SubtractorOutputAnalyzer(size_t num_capture_channels)
    : filters_converged_(num_capture_channels, false) {}

void SubtractorOutputAnalyzer::Update(std::span<const SubtractorOutput> subtractor_output,
                                      bool* any_filter_converged, bool* any_coarse_filter_converged,
                                      bool* all_filters_diverged) {
  *any_filter_converged = false;
  *any_coarse_filter_converged = false;
  *all_filters_diverged = true;

  for (size_t ch = 0; ch < subtractor_output.size(); ++ch) {
    const float y2 = subtractor_output[ch].y2;
    const float e2_refined = subtractor_output[ch].e2_refined;
    const float e2_coarse = subtractor_output[ch].e2_coarse;

    constexpr float kConvergenceThreshold = 50 * 50 * kBlockSize;
    constexpr float kConvergenceThresholdLowLevel = 20 * 20 * kBlockSize;
    bool refined_filter_converged = e2_refined < 0.5f * y2 && y2 > kConvergenceThreshold;
    bool coarse_filter_converged_strict = e2_coarse < 0.05f * y2 && y2 > kConvergenceThreshold;
    bool coarse_filter_converged_relaxed = e2_coarse < 0.3f * y2 && y2 > kConvergenceThresholdLowLevel;
    float min_e2 = std::min(e2_refined, e2_coarse);
    bool filter_diverged = min_e2 > 1.5f * y2 && y2 > 30.f * 30.f * kBlockSize;
    filters_converged_[ch] = refined_filter_converged || coarse_filter_converged_strict;

    *any_filter_converged = *any_filter_converged || filters_converged_[ch];
    *any_coarse_filter_converged = *any_coarse_filter_converged || coarse_filter_converged_relaxed;
    *all_filters_diverged = *all_filters_diverged && filter_diverged;
  }
}

void SubtractorOutputAnalyzer::HandleEchoPathChange() {
  std::fill(filters_converged_.begin(), filters_converged_.end(), false);
}

}  // namespace sm::audio::aec3
