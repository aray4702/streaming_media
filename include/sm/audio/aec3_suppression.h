// 6.1 AEC3's residual echo suppression (`apm v2`, #240 step 4), ported from
// compare/webrtc/reference/modules/audio_processing/aec3/
// {moving_average_spectrum,nearend_detector,dominant_nearend_detector,
// subband_nearend_detector,residual_echo_estimator,suppression_gain,
// suppression_filter,comfort_noise_generator}.{h,cc} (M157), with the
// defaults of EchoCanceller3Config::Suppressor, ::ComfortNoise, ::EchoModel
// and ::EpStrength (Aec3Config).
//
//   ResidualEchoEstimator  the echo power R2 left after the linear filter:
//                       the filter's echo estimate S2 over the ERLE when the
//                       linear estimate is usable, otherwise the render power
//                       around the direct-path delay (max over 1 + 1 + 1
//                       blocks, soft noise gate at 27509, minus 10x the
//                       render noise floor held for 50 blocks) times the echo
//                       path gain 1 (0.01^2 in transparent mode); plus the
//                       exponential reverb tail; the capture spectrum when
//                       the echo saturates.
//   DominantNearendDetector  the nearend state: 12 blocks in which bins 1-15
//                       have echo under 0.25x and nearend over 30x the noise,
//                       held for 50 blocks; left early when the echo is over
//                       10x the nearend.
//   SuppressionGain     per-bin gain that makes the echo inaudible: 1 while
//                       echo-to-nearend and echo-to-noise ratios are under
//                       the transparent thresholds (0.3 / 0.3 low bands,
//                       0.07 high; 1.09 / 0.1 in the nearend state), falling
//                       to 0 at the suppress threshold (0.4 / 0.1; 1.1 / 0.3);
//                       rises at most 2x per block, the lowest 6 bins fall at
//                       most 4x after nearend; bins above 16 capped at bin
//                       16's gain outside the nearend state; the upper bands
//                       get the minimum gain over 4-8 kHz.
//   SuppressionFilter   applies the gain to the 128-point sqrt-Hanning
//                       windowed error spectrum, fills sqrt(1 - g^2) of
//                       comfort noise, overlap-adds the output and delays the
//                       upper bands by one block to match.
//   ComfortNoiseGenerator  the background noise spectrum N2 (minimum
//                       statistics on the smoothed capture spectrum, a slow
//                       initial estimate for the first 1000 blocks, floor
//                       -96 dBFS) and random-phase noise shaped to it.
//
// Differences that remain (tools/catalog.py 6.1): no neural residual echo
// estimator (libwebrtc's is optional and absent in a call, so its default
// path is the one ported) and no data dumps. The field trials
// WebRTC-Aec3UseLowEarlyReflectionsDefaultGain,
// WebRTC-Aec3UseLowLateReflectionsDefaultGain and
// WebRTC-Aec3UseErleOnsetCompensationInDominantNearend are read as not set
// (M157's default). The portable C paths, where x86 libwebrtc runs SSE2/AVX2
// (same arithmetic). EchoRemover, BlockProcessor and EchoCanceller3 are
// #240's next step, so nothing calls this module in a call yet.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "sm/audio/aec3_filter.h"
#include "sm/audio/aec3_reverb.h"
#include "sm/audio/aec3_state.h"
#include "sm/core/metrics.h"

namespace sm::audio::aec3 {

// moving_average_spectrum.h: the average of the last mem_len inputs.
class MovingAverageSpectrum {
 public:
  MovingAverageSpectrum(size_t num_elem, size_t mem_len);
  // Average of this input and up to mem_len - 1 previous ones.
  void Average(std::span<const float> input, std::span<float> output);
  // A new memory length clears the memory.
  void UpdateMemoryLength(size_t mem_len);

 private:
  const size_t num_elem_;
  size_t mem_len_;
  std::vector<float> memory_;
  size_t mem_index_ = 0;
  int number_updates_ = 0;
};

// nearend_detector.h: whether the suppressor is in the nearend or echo state.
class NearendDetector {
 public:
  virtual ~NearendDetector() = default;
  virtual bool IsNearendState() const = 0;
  virtual void Update(std::span<const std::array<float, kFftLengthBy2Plus1>> nearend_spectrum,
                      std::span<const std::array<float, kFftLengthBy2Plus1>> residual_echo_spectrum,
                      std::span<const std::array<float, kFftLengthBy2Plus1>> comfort_noise_spectrum,
                      bool initial_state) = 0;
  virtual void SetConfig(const SuppressorConfig& config) = 0;
};

// dominant_nearend_detector.h
class DominantNearendDetector : public NearendDetector {
 public:
  DominantNearendDetector(const SuppressorConfig::DominantNearendDetection& config, size_t num_capture_channels);
  bool IsNearendState() const override { return nearend_state_; }
  void Update(std::span<const std::array<float, kFftLengthBy2Plus1>> nearend_spectrum,
              std::span<const std::array<float, kFftLengthBy2Plus1>> residual_echo_spectrum,
              std::span<const std::array<float, kFftLengthBy2Plus1>> comfort_noise_spectrum,
              bool initial_state) override;
  void SetConfig(const SuppressorConfig& config) override { config_ = config.dominant_nearend_detection; }

 private:
  SuppressorConfig::DominantNearendDetection config_;
  const size_t num_capture_channels_;
  bool nearend_state_ = false;
  std::vector<int> trigger_counters_;
  std::vector<int> hold_counters_;
};

// subband_nearend_detector.h (config.suppressor.use_subband_nearend_detection,
// off by default).
class SubbandNearendDetector : public NearendDetector {
 public:
  SubbandNearendDetector(const SuppressorConfig& config, size_t num_capture_channels);
  bool IsNearendState() const override { return nearend_state_; }
  void Update(std::span<const std::array<float, kFftLengthBy2Plus1>> nearend_spectrum,
              std::span<const std::array<float, kFftLengthBy2Plus1>> residual_echo_spectrum,
              std::span<const std::array<float, kFftLengthBy2Plus1>> comfort_noise_spectrum,
              bool initial_state) override;
  void SetConfig(const SuppressorConfig& config) override;

 private:
  SuppressorConfig::SubbandNearendDetection config_;
  const size_t num_capture_channels_;
  std::vector<MovingAverageSpectrum> nearend_smoothers_;
  float one_over_subband_length1_ = 1.f;
  float one_over_subband_length2_ = 1.f;
  bool nearend_state_ = false;
};

// residual_echo_estimator.h
class ResidualEchoEstimator : public MetricsSource {
 public:
  ResidualEchoEstimator(const Aec3Config& config, size_t num_render_channels);
  ResidualEchoEstimator(const ResidualEchoEstimator&) = delete;
  ResidualEchoEstimator& operator=(const ResidualEchoEstimator&) = delete;

  // R2 and R2_unbounded per capture channel, from the linear echo estimate
  // S2_linear, the capture Y2 and the error E2 spectra.
  void Estimate(const AecState& aec_state, const RenderBuffer& render_buffer,
                std::span<const std::array<float, kFftLengthBy2Plus1>> S2_linear,
                std::span<const std::array<float, kFftLengthBy2Plus1>> Y2,
                std::span<const std::array<float, kFftLengthBy2Plus1>> E2, bool dominant_nearend,
                std::span<std::array<float, kFftLengthBy2Plus1>> R2,
                std::span<std::array<float, kFftLengthBy2Plus1>> R2_unbounded);
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  enum class ReverbType { kLinear, kNonLinear };
  void Reset();
  void UpdateRenderNoisePower(const RenderBuffer& render_buffer);
  void UpdateReverb(ReverbType reverb_type, const AecState& aec_state, const RenderBuffer& render_buffer,
                    bool dominant_nearend);
  void AddReverb(std::span<std::array<float, kFftLengthBy2Plus1>> R2) const;
  float GetEchoPathGain(const AecState& aec_state, bool gain_for_early_reflections) const;

  const Aec3Config config_;
  const size_t num_render_channels_;
  const float early_reflections_transparent_mode_gain_;
  const float late_reflections_transparent_mode_gain_;
  const float early_reflections_general_gain_;
  const float late_reflections_general_gain_;
  const bool erle_onset_compensation_in_dominant_nearend_;
  std::array<float, kFftLengthBy2Plus1> X2_noise_floor_;
  std::array<int, kFftLengthBy2Plus1> X2_noise_floor_counter_;
  ReverbModel echo_reverb_;
  uint64_t linear_blocks_ = 0;
  uint64_t nonlinear_blocks_ = 0;
  uint64_t saturated_blocks_ = 0;
};

// suppression_gain.h
class SuppressionGain : public MetricsSource {
 public:
  SuppressionGain(const Aec3Config& config, int sample_rate_hz, size_t num_capture_channels);
  SuppressionGain(const SuppressionGain&) = delete;
  SuppressionGain& operator=(const SuppressionGain&) = delete;

  // The low band gain per bin (amplitude) and the gain of the upper bands.
  void GetGain(const SuppressorConfig& suppressor_config, bool config_changed,
               std::span<const std::array<float, kFftLengthBy2Plus1>> nearend_spectrum,
               std::span<const std::array<float, kFftLengthBy2Plus1>> echo_spectrum,
               std::span<const std::array<float, kFftLengthBy2Plus1>> residual_echo_spectrum,
               std::span<const std::array<float, kFftLengthBy2Plus1>> residual_echo_spectrum_unbounded,
               std::span<const std::array<float, kFftLengthBy2Plus1>> comfort_noise_spectrum,
               const RenderSignalAnalyzer& render_signal_analyzer, const AecState& aec_state, const Block& render,
               bool clock_drift, float* high_bands_gain, std::array<float, kFftLengthBy2Plus1>* low_band_gain);
  bool IsDominantNearend() const { return dominant_nearend_detector_->IsNearendState(); }
  // Toggles the usage of the initial state.
  void SetInitialState(bool state) { initial_state_ = state; }
  void ExportMetrics(MetricsWriter& w) const override;

  struct GainParameters {
    GainParameters(int last_lf_band, int first_hf_band, const SuppressorConfig::Tuning& tuning);
    void SetConfig(int last_lf_band, int first_hf_band, const SuppressorConfig::Tuning& tuning);
    float max_inc_factor;
    float max_dec_factor_lf;
    std::array<float, kFftLengthBy2Plus1> enr_transparent_;
    std::array<float, kFftLengthBy2Plus1> enr_suppress_;
    std::array<float, kFftLengthBy2Plus1> emr_transparent_;
  };
  // Updates the sizes and parameters when the config changes (public for
  // libwebrtc's UpdateStateDependingOnConfig test, a friend there).
  void UpdateStateDependingOnConfig(const SuppressorConfig& suppressor_config);
  const GainParameters& nearend_params() const { return nearend_params_; }
  const GainParameters& normal_params() const { return normal_params_; }

 private:
  float UpperBandsGain(const SuppressorConfig& suppressor_config,
                       std::span<const std::array<float, kFftLengthBy2Plus1>> echo_spectrum,
                       std::span<const std::array<float, kFftLengthBy2Plus1>> comfort_noise_spectrum,
                       const std::optional<int>& narrow_peak_band, bool saturated_echo, const Block& render,
                       const std::array<float, kFftLengthBy2Plus1>& low_band_gain) const;
  void GainToNoAudibleEcho(const std::array<float, kFftLengthBy2Plus1>& nearend,
                           const std::array<float, kFftLengthBy2Plus1>& echo,
                           const std::array<float, kFftLengthBy2Plus1>& masker,
                           std::array<float, kFftLengthBy2Plus1>* gain) const;
  void LowerBandGain(const SuppressorConfig& suppressor_config, bool low_noise_render, const AecState& aec_state,
                     std::span<const std::array<float, kFftLengthBy2Plus1>> suppressor_input,
                     std::span<const std::array<float, kFftLengthBy2Plus1>> residual_echo,
                     std::span<const std::array<float, kFftLengthBy2Plus1>> comfort_noise, bool clock_drift,
                     std::array<float, kFftLengthBy2Plus1>* gain);
  void GetMinGain(const SuppressorConfig& suppressor_config, std::span<const float> weighted_residual_echo,
                  std::span<const float> last_nearend, std::span<const float> last_echo, bool low_noise_render,
                  bool saturated_echo, std::span<float> min_gain) const;
  void GetMaxGain(float floor_first_increase, std::span<float> max_gain) const;

  class LowNoiseRenderDetector {
   public:
    bool Detect(const Block& render);

   private:
    float average_power_ = 32768.f * 32768.f;
  };

  const size_t num_capture_channels_;
  const EchoAudibilityConfig echo_audibility_config_;
  const bool use_subband_nearend_detection_;
  std::array<float, kFftLengthBy2Plus1> last_gain_;
  std::vector<std::array<float, kFftLengthBy2Plus1>> last_nearend_;
  std::vector<std::array<float, kFftLengthBy2Plus1>> last_echo_;
  LowNoiseRenderDetector low_render_detector_;
  bool initial_state_ = true;
  std::vector<MovingAverageSpectrum> nearend_smoothers_;
  GainParameters nearend_params_;
  GainParameters normal_params_;
  std::unique_ptr<NearendDetector> dominant_nearend_detector_;
  uint64_t nearend_blocks_ = 0;
  uint64_t nearend_entries_ = 0;
  bool last_nearend_state_ = false;
};

// suppression_filter.h
class SuppressionFilter {
 public:
  SuppressionFilter(int sample_rate_hz, size_t num_capture_channels);
  SuppressionFilter(const SuppressionFilter&) = delete;
  SuppressionFilter& operator=(const SuppressionFilter&) = delete;
  // Applies the gains to E_lowest_band and the upper bands of e, adds the
  // comfort noise, and writes the output to e (all bands).
  void ApplyGain(std::span<const FftData> comfort_noise, std::span<const FftData> comfort_noise_high_bands,
                 const std::array<float, kFftLengthBy2Plus1>& suppression_gain, float high_bands_gain,
                 std::span<const FftData> E_lowest_band, Block* e);

 private:
  const int sample_rate_hz_;
  const size_t num_capture_channels_;
  const Aec3Fft fft_;
  std::vector<std::vector<std::array<float, kFftLengthBy2>>> e_output_old_;
};

// comfort_noise_generator.h
class ComfortNoiseGenerator {
 public:
  ComfortNoiseGenerator(const Aec3Config& config, size_t num_capture_channels);
  ComfortNoiseGenerator(const ComfortNoiseGenerator&) = delete;
  ComfortNoiseGenerator& operator=(const ComfortNoiseGenerator&) = delete;
  // Updates the noise estimate from the capture spectrum (unless saturated)
  // and generates the noise for the lower and upper bands.
  void Compute(bool saturated_capture, std::span<const std::array<float, kFftLengthBy2Plus1>> capture_spectrum,
               std::span<FftData> lower_band_noise, std::span<FftData> upper_band_noise);
  // The estimate of the background noise spectrum.
  std::span<const std::array<float, kFftLengthBy2Plus1>> NoiseSpectrum() const { return N2_; }

 private:
  uint32_t seed_ = 42;
  const size_t num_capture_channels_;
  const float noise_floor_;
  std::unique_ptr<std::vector<std::array<float, kFftLengthBy2Plus1>>> N2_initial_;
  std::vector<std::array<float, kFftLengthBy2Plus1>> Y2_smoothed_;
  std::vector<std::array<float, kFftLengthBy2Plus1>> N2_;
  int N2_counter_ = 0;
};

}  // namespace sm::audio::aec3
