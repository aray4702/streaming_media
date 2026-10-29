// 6.3 Noise suppression as libwebrtc M157's NoiseSuppressor. `apm v2` (#241).
//
// Ported from compare/webrtc/reference/modules/audio_processing/ns/ with
// libwebrtc's constants and defaults (ns_common.h, suppression_params.cc):
//  - Filter bank (noise_suppressor.cc): 160-sample frames (10 ms of the
//    0-8 kHz band) extended with the previous 96 samples to a 256-point
//    frame, a hybrid Hann/flat window (kBlocks160w256FirstHalf) applied on
//    analysis and on synthesis, overlap-add. Algorithmic delay 96 samples
//    (6 ms at 16 kHz).
//  - QuantileNoiseEstimator (quantile_noise_estimator.cc): three interleaved
//    log-quantile estimates (quantile 0.25, 200-frame periods), density
//    adapted step.
//  - NoiseEstimator (noise_estimator.cc): during the first 50 frames the
//    quantile estimate is mixed with a parametric white/pink noise model;
//    afterwards it is updated per bin weighted by the speech probability
//    (0.9, or 0.99 while speech is likely, decreases always allowed), plus
//    a conservative estimate updated only in pauses.
//  - SignalModelEstimator, PriorSignalModelEstimator, Histograms
//    (signal_model_estimator.cc, prior_signal_model_estimator.cc,
//    histograms.cc): the LRT, spectral flatness and spectral difference
//    features, with thresholds and weights re-learnt from 500-frame
//    histograms.
//  - SpeechProbabilityEstimator (speech_probability_estimator.cc): the
//    features through tanh maps into a smoothed prior, combined per bin
//    with the LRT.
//  - WienerFilter (wiener_filter.cc): decision-directed prior SNR (0.98),
//    gain snr / (over_subtraction + snr) floored at the level's minimum
//    gain, blended with a spectral-subtraction filter on the parametric
//    noise during start-up, and the overall scaling ("attenuation
//    adjustment") after 200 frames.
//  - NrFft (ns_fft.cc): the 256-point real DFT is Ooura's fft4g
//    (common_audio/third_party/ooura/fft_size_256/fft4g.cc, sm/audio/fft4g.h),
//    with NrFft's layout and its 2/n scaling of the inverse. ns.cc and
//    fft4g.cc build with -ffp-contract=off, as Chromium does, so the output
//    is bit-exact with libwebrtc's.
//  - Upper bands (32/48 kHz): delayed by 96 samples and scaled by one gain
//    from the speech probability and Wiener gain of the top 32 bins of the
//    lowest band (ComputeUpperBandsGain).
//
// As in AudioProcessingImpl::ProcessCaptureStreamLocked, Analyze() reads the
// capture before the echo canceller and Process() suppresses after it. Bands
// are laid out as SplittingFilter's (band b at [b * 160, (b + 1) * 160)), and
// channels one after the other (channel c at [c * frame_samples(), ...)).
// Signals are FloatS16 inside (libwebrtc's scale); the API takes and returns
// this library's [-1, 1] floats.
//
// Channels (noise_suppressor.cc): each channel has its own estimators and
// filter. Analyze() learns from all of them unless every channel's frame is
// zero. Process() applies one Wiener filter to every channel, the minimum
// over the channels per bin (AggregateWienerFilters), and likewise the
// minimum of the channels' overall scaling factors and upper-band gains.
//
// The level defaults to k18dB, libwebrtc's kHigh: what
// WebRtcVoiceEngine::ApplyOptions sets in a call
// (media/engine/webrtc_voice_engine.cc). NsConfig's own default is k12dB.
//
// Verified against libwebrtc M157's prebuilt NoiseSuppressor (the
// compare/webrtc/fetch.sh build, driven through its AudioBuffer): the output
// is identical bit for bit at 16, 32 and 48 kHz, with one to three channels
// (#241; ns_test.cc pins it).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "sm/core/metrics.h"

namespace sm::audio::ns {

// ns_common.h.
inline constexpr size_t kFftSize = 256;
inline constexpr size_t kFftSizeBy2Plus1 = kFftSize / 2 + 1;
inline constexpr size_t kNsFrameSize = 160;
inline constexpr size_t kOverlapSize = kFftSize - kNsFrameSize;
inline constexpr int kShortStartupPhaseBlocks = 50;
inline constexpr int kLongStartupPhaseBlocks = 200;
inline constexpr int kFeatureUpdateWindowSize = 500;
inline constexpr float kLtrFeatureThr = 0.5f;
inline constexpr float kBinSizeLrt = 0.1f;
inline constexpr float kBinSizeSpecFlat = 0.05f;
inline constexpr float kBinSizeSpecDiff = 0.1f;
inline constexpr int kHistogramSize = 1000;  // histograms.h
inline constexpr int kSimult = 3;            // quantile_noise_estimator.h

using Spectrum = std::array<float, kFftSizeBy2Plus1>;

// ns_config.h.
enum class SuppressionLevel { k6dB, k12dB, k18dB, k21dB };

// suppression_params.cc.
struct SuppressionParams {
  explicit SuppressionParams(SuppressionLevel level);
  float over_subtraction_factor;
  float minimum_attenuating_gain;
  bool use_attenuation_adjustment;
};

// fast_math.cc.
float LogApproximation(float x);
float ExpApproximation(float x);
float PowApproximation(float x, float p);

class QuantileNoiseEstimator {
 public:
  QuantileNoiseEstimator();
  void Estimate(std::span<const float, kFftSizeBy2Plus1> signal_spectrum,
                std::span<float, kFftSizeBy2Plus1> noise_spectrum);

 private:
  std::array<float, kSimult * kFftSizeBy2Plus1> density_;
  std::array<float, kSimult * kFftSizeBy2Plus1> log_quantile_;
  Spectrum quantile_;
  std::array<int, kSimult> counter_;
  int num_updates_ = 1;
};

class NoiseEstimator {
 public:
  explicit NoiseEstimator(const SuppressionParams& params);
  void PrepareAnalysis() { prev_noise_spectrum_ = noise_spectrum_; }
  void PreUpdate(int32_t num_analyzed_frames, std::span<const float, kFftSizeBy2Plus1> signal_spectrum,
                 float signal_spectral_sum);
  void PostUpdate(std::span<const float, kFftSizeBy2Plus1> speech_probability,
                  std::span<const float, kFftSizeBy2Plus1> signal_spectrum);
  const Spectrum& noise_spectrum() const { return noise_spectrum_; }
  const Spectrum& prev_noise_spectrum() const { return prev_noise_spectrum_; }
  const Spectrum& parametric_noise_spectrum() const { return parametric_noise_spectrum_; }
  const Spectrum& conservative_noise_spectrum() const { return conservative_noise_spectrum_; }

 private:
  const SuppressionParams& params_;
  float white_noise_level_ = 0.f;
  float pink_noise_numerator_ = 0.f;
  float pink_noise_exp_ = 0.f;
  Spectrum prev_noise_spectrum_{}, conservative_noise_spectrum_{}, parametric_noise_spectrum_{}, noise_spectrum_{};
  QuantileNoiseEstimator quantile_noise_estimator_;
};

// signal_model.h.
struct SignalModel {
  SignalModel();
  float lrt;
  float spectral_diff;
  float spectral_flatness;
  Spectrum avg_log_lrt;
};

// prior_signal_model.h.
struct PriorSignalModel {
  float lrt = kLtrFeatureThr;
  float flatness_threshold = .5f;
  float template_diff_threshold = .5f;
  float lrt_weighting = 1.f;
  float flatness_weighting = 0.f;
  float difference_weighting = 0.f;
};

class Histograms {
 public:
  Histograms() { Clear(); }
  void Clear();
  void Update(const SignalModel& features);
  std::span<const int, kHistogramSize> lrt() const { return lrt_; }
  std::span<const int, kHistogramSize> spectral_flatness() const { return spectral_flatness_; }
  std::span<const int, kHistogramSize> spectral_diff() const { return spectral_diff_; }

 private:
  std::array<int, kHistogramSize> lrt_, spectral_flatness_, spectral_diff_;
};

// prior_signal_model_estimator.cc.
void UpdatePriorSignalModel(const Histograms& histograms, PriorSignalModel* prior_model);

class SignalModelEstimator {
 public:
  void AdjustNormalization(int32_t num_analyzed_frames, float signal_energy);
  void Update(std::span<const float, kFftSizeBy2Plus1> prior_snr, std::span<const float, kFftSizeBy2Plus1> post_snr,
              std::span<const float, kFftSizeBy2Plus1> conservative_noise_spectrum,
              std::span<const float, kFftSizeBy2Plus1> signal_spectrum, float signal_spectral_sum,
              float signal_energy);
  const PriorSignalModel& prior_model() const { return prior_model_; }
  const SignalModel& model() const { return features_; }

 private:
  float diff_normalization_ = 0.f;
  float signal_energy_sum_ = 0.f;
  Histograms histograms_;
  int histogram_analysis_counter_ = 500;
  PriorSignalModel prior_model_;
  SignalModel features_;
};

class SpeechProbabilityEstimator {
 public:
  void Update(int32_t num_analyzed_frames, std::span<const float, kFftSizeBy2Plus1> prior_snr,
              std::span<const float, kFftSizeBy2Plus1> post_snr,
              std::span<const float, kFftSizeBy2Plus1> conservative_noise_spectrum,
              std::span<const float, kFftSizeBy2Plus1> signal_spectrum, float signal_spectral_sum,
              float signal_energy);
  float prior_probability() const { return prior_speech_prob_; }
  const Spectrum& probability() const { return speech_probability_; }
  const SignalModelEstimator& signal_model() const { return signal_model_estimator_; }

 private:
  SignalModelEstimator signal_model_estimator_;
  float prior_speech_prob_ = .5f;
  Spectrum speech_probability_{};
};

class WienerFilter {
 public:
  explicit WienerFilter(const SuppressionParams& params);
  void Update(int32_t num_analyzed_frames, std::span<const float, kFftSizeBy2Plus1> noise_spectrum,
              std::span<const float, kFftSizeBy2Plus1> prev_noise_spectrum,
              std::span<const float, kFftSizeBy2Plus1> parametric_noise_spectrum,
              std::span<const float, kFftSizeBy2Plus1> signal_spectrum);
  float ComputeOverallScalingFactor(int32_t num_analyzed_frames, float prior_speech_probability,
                                    float energy_before_filtering, float energy_after_filtering) const;
  const Spectrum& filter() const { return filter_; }

 private:
  const SuppressionParams& params_;
  Spectrum spectrum_prev_process_{}, initial_spectral_estimate_{}, filter_;
};

struct NoiseSuppressorConfig {
  // 16000, 32000 or 48000, as libwebrtc's NumBandsForRate DCHECKs; at any
  // other rate the suppressor is not valid() (#594).
  int sample_rate = 16000;
  SuppressionLevel level = SuppressionLevel::k18dB;
  size_t num_channels = 1;
};

struct NoiseSuppressorStats {
  uint64_t analyzed_frames = 0;  // Non-zero frames Analyze() learnt from.
  uint64_t processed_frames = 0;
  float last_mean_gain_db = 0;   // Mean Wiener gain over the bins, last frame.
  float last_upper_band_gain = 1;
  float last_gain_adjustment = 1;
  float prior_speech_probability = .5f;  // Channel 0's.
  Histogram gain_db;
};

class NoiseSuppressor : public MetricsSource {
 public:
  explicit NoiseSuppressor(NoiseSuppressorConfig cfg = {});
  NoiseSuppressor(const NoiseSuppressor&) = delete;  // The filters hold a reference to params_.
  NoiseSuppressor& operator=(const NoiseSuppressor&) = delete;
  // False when built for a rate other than 16, 32 or 48 kHz: Analyze and
  // Process then leave the audio untouched.
  bool valid() const { return valid_; }
  size_t num_bands() const { return num_bands_; }
  size_t num_channels() const { return channels_.size(); }
  // Samples per channel per call: num_bands() * 160.
  size_t frame_samples() const { return num_bands_ * kNsFrameSize; }
  // Learns the noise and speech models from the capture (before the echo
  // canceller, as APM does). num_channels() * frame_samples() samples; a
  // shorter span is ignored.
  void Analyze(std::span<const float> bands);
  // Suppresses noise in place. Output is delayed by latency_samples() (per
  // band). A shorter span is left untouched.
  void Process(std::span<float> bands);
  static constexpr size_t latency_samples() { return kOverlapSize; }
  const NoiseSuppressorStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  // NoiseSuppressor::ChannelState.
  struct ChannelState {
    ChannelState(const SuppressionParams& params, size_t num_bands);
    SpeechProbabilityEstimator speech_probability_estimator;
    WienerFilter wiener_filter;
    NoiseEstimator noise_estimator;
    Spectrum prev_analysis_signal_spectrum;
    std::array<float, kOverlapSize> analyze_analysis_memory{};
    std::array<float, kOverlapSize> process_analysis_memory{};
    std::array<float, kOverlapSize> process_synthesis_memory{};
    std::vector<std::array<float, kOverlapSize>> process_delay_memory;
  };
  // FilterBankState: one channel's frame through Process().
  struct FilterBankState {
    std::array<float, kNsFrameSize> y_band0;
    std::array<float, kFftSize> real, imag, extended_frame;
  };

  // NrFft (ns_fft.cc): real/imag halves, Ooura layout.
  void Fft(std::span<const float, kFftSize> time, std::span<float, kFftSize> real, std::span<float, kFftSize> imag);
  void Ifft(std::span<const float, kFftSize> real, std::span<const float, kFftSize> imag,
            std::span<float, kFftSize> time);

  bool accepts(std::span<const float> bands) const {
    return valid_ && bands.size() >= channels_.size() * frame_samples();
  }

  NoiseSuppressorConfig cfg_;
  bool valid_;
  size_t num_bands_;
  SuppressionParams params_;
  int32_t num_analyzed_frames_ = -1;
  std::array<size_t, kFftSize / 2> bit_reversal_state_{};
  std::array<float, kFftSize / 2> tables_{};
  // unique_ptr: the filters hold a reference to params_ and must not move.
  std::vector<std::unique_ptr<ChannelState>> channels_;
  std::vector<FilterBankState> filter_bank_states_;
  std::vector<float> upper_band_gains_, energies_before_filtering_, gain_adjustments_;
  NoiseSuppressorStats stats_;
};

}  // namespace sm::audio::ns
