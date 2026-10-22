// AEC3's model of the reverberant echo (`apm v2`, #240 step 3), ported from
// compare/webrtc/reference/modules/audio_processing/aec3/
// {reverb_model,reverb_decay_estimator,reverb_frequency_response,
// reverb_model_estimator}.{h,cc} (M157).
//
//   ReverbModel         exponential reverb over a power spectrum: per bin,
//                       reverb = (reverb + scaling * power) * decay.
//   ReverbDecayEstimator  the decay of the late reverb, read off the refined
//                       linear filter's impulse response: one 64-tap block
//                       analyzed per call, its log2 energy fitted by linear
//                       regression over the late-reverb region (after the
//                       early reflections, found from the tilt of 6-block
//                       sections over the first 9), smoothed by 0.2 times
//                       the filter quality, kept within [0.02, 0.95] and
//                       falling by at most 3% per estimate. Adaptive only when
//                       ep_strength.default_len is negative; otherwise the
//                       decay stays |default_len| (0.83), the mild one
//                       |nearend_len| (0.83).
//   ReverbFrequencyResponse  the tail's frequency response: the direct path
//                       bins times the tail/direct energy ratio (smoothed by
//                       0.2 times the filter quality), at least the last
//                       partition's response (conservative tail, the
//                       default), and at least the mean of its neighbours.
//   ReverbModelEstimator  one of each estimator per capture channel; the
//                       decay and the response are channel 0's.
//
// Differences that remain (tools/catalog.py 6.1): no data dumps (Dump() isn't
// ported), and the RTC_DCHECKs are not checked. None of these files reads a
// field trial.
#pragma once

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

// reverb_model.h
class ReverbModel {
 public:
  ReverbModel();

  // Resets the state.
  void Reset();

  // Returns the reverb.
  std::span<const float, kFftLengthBy2Plus1> reverb() const { return reverb_; }

  // Update the reverb estimate of a power spectrum pre-scaled by
  // power_spectrum_scaling (the same for all bins, or one per bin).
  void UpdateReverbNoFreqShaping(std::span<const float> power_spectrum, float power_spectrum_scaling,
                                 float reverb_decay);
  void UpdateReverb(std::span<const float> power_spectrum, std::span<const float> power_spectrum_scaling,
                    float reverb_decay);

 private:
  std::array<float, kFftLengthBy2Plus1> reverb_;
};

// reverb_decay_estimator.h
class ReverbDecayEstimator {
 public:
  explicit ReverbDecayEstimator(const Aec3Config& config);
  // Updates the decay estimate.
  void Update(std::span<const float> filter, const std::optional<float>& filter_quality, int filter_delay_blocks,
              bool usable_linear_filter, bool stationary_signal);
  // Returns the decay for the exponential model. The parameter `mild` indicates
  // which exponential decay to return, the default one or a milder one.
  float Decay(bool mild) const {
    if (use_adaptive_echo_decay_) {
      return decay_;
    } else {
      return mild ? mild_decay_ : decay_;
    }
  }

 private:
  void EstimateDecay(std::span<const float> filter, int peak_block);
  void AnalyzeFilter(std::span<const float> filter);

  void ResetDecayEstimation();

  // Estimates the decay of the late reverb from the linear filter.
  class LateReverbLinearRegressor {
   public:
    // Resets the estimator to receive a specified number of data points.
    void Reset(int num_data_points);
    // Accumulates estimation data.
    void Accumulate(float z);
    // Estimates the decay.
    float Estimate();
    // Returns whether an estimate is available.
    bool EstimateAvailable() const { return n_ == N_ && N_ != 0; }

    float nz_ = 0.f;
    float nn_ = 0.f;
    float count_ = 0.f;
    int N_ = 0;
    int n_ = 0;
  };

  // Identifies the length of the early reverb from the linear filter: the
  // impulse response is divided in sections and the tilt of each section is
  // computed by a linear regressor.
  class EarlyReverbLengthEstimator {
   public:
    explicit EarlyReverbLengthEstimator(int max_blocks);

    // Resets the estimator.
    void Reset();
    // Accumulates estimation data.
    void Accumulate(float value, float smoothing);
    // Estimates the size in blocks of the early reverb.
    int Estimate();

   private:
    std::vector<float> numerators_smooth_;
    std::vector<float> numerators_;
    int coefficients_counter_;
    int block_counter_ = 0;
    int n_sections_ = 0;
  };

  const int filter_length_blocks_;
  const int filter_length_coefficients_;
  const bool use_adaptive_echo_decay_;
  LateReverbLinearRegressor late_reverb_decay_estimator_;
  EarlyReverbLengthEstimator early_reverb_estimator_;
  int late_reverb_start_;
  int late_reverb_end_;
  int block_to_analyze_ = 0;
  int estimation_region_candidate_size_ = 0;
  bool estimation_region_identified_ = false;
  std::vector<float> previous_gains_;
  float decay_;
  float mild_decay_;
  float tail_gain_ = 0.f;
  float smoothing_constant_ = 0.f;
};

// reverb_frequency_response.h
class ReverbFrequencyResponse {
 public:
  explicit ReverbFrequencyResponse(bool use_conservative_tail_frequency_response);

  // Updates the frequency response estimate of the reverb.
  void Update(const std::vector<std::array<float, kFftLengthBy2Plus1>>& frequency_response, int filter_delay_blocks,
              const std::optional<float>& linear_filter_quality, bool stationary_block);

  // Returns the estimated frequency response for the reverb.
  std::span<const float> FrequencyResponse() const { return tail_response_; }

 private:
  void Update(const std::vector<std::array<float, kFftLengthBy2Plus1>>& frequency_response, int filter_delay_blocks,
              float linear_filter_quality);

  const bool use_conservative_tail_frequency_response_;
  float average_decay_ = 0.f;
  std::array<float, kFftLengthBy2Plus1> tail_response_;
};

// reverb_model_estimator.h
class ReverbModelEstimator {
 public:
  ReverbModelEstimator(const Aec3Config& config, size_t num_capture_channels);

  // Updates the estimates based on new data.
  void Update(std::span<const std::vector<float>> impulse_responses,
              std::span<const std::vector<std::array<float, kFftLengthBy2Plus1>>> frequency_responses,
              std::span<const std::optional<float>> linear_filter_qualities,
              std::span<const int> filter_delays_blocks, const std::vector<bool>& usable_linear_estimates,
              bool stationary_block);

  // Returns the exponential decay of the reverberant echo. The parameter `mild`
  // indicates which exponential decay to return, the default one or a milder
  // one. (libwebrtc's TODO: only channel 0's.)
  float ReverbDecay(bool mild) const { return reverb_decay_estimators_[0]->Decay(mild); }

  // Return the frequency response of the reverberant echo (channel 0's).
  std::span<const float> GetReverbFrequencyResponse() const {
    return reverb_frequency_responses_[0].FrequencyResponse();
  }

 private:
  std::vector<std::unique_ptr<ReverbDecayEstimator>> reverb_decay_estimators_;
  std::vector<ReverbFrequencyResponse> reverb_frequency_responses_;
};

}  // namespace sm::audio::aec3
