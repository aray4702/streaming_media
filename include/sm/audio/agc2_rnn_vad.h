// 6.4 AGC2's voice activity detector as libwebrtc M157's
// (`VoiceActivityDetectorWrapper` and `rnn_vad::RnnVad`), which gives
// `agc2::GainController2` its per-frame speech probability. `apm v2` (#242).
//
// Ported from compare/webrtc/reference/modules/audio_processing/agc2/
// vad_wrapper.cc and rnn_vad/, with the rnnoise weights of
// third_party/rnnoise/src/ (agc2_rnn_vad_weights.cc) and libwebrtc's
// constants:
//  - VoiceActivityDetectorWrapper (vad_wrapper.cc): resamples the first
//    channel of each 10 ms frame to 24 kHz (PushSincResampler, as
//    PushResampler<float> does) and resets the VAD every 1500 ms
//    (agc2_common.h kVadResetPeriodMs).
//  - FeaturesExtractor (features_extraction.cc): a 864-sample 24 kHz pitch
//    buffer, the LP residual (lp_residual.cc: 5 LPC coefficients from a
//    denoised auto-correlation, post-processed with 0.9^k and 0.8), the pitch
//    period at 48 kHz (pitch_search*.cc: 2x decimation, the 12 kHz
//    auto-correlation, two candidates, refinement at 24 kHz with
//    pseudo-interpolation, the sub-harmonic search with pitch tracking) and
//    the spectral features of the reference and pitch-lagged 20 ms frames
//    (spectral_features*.cc: half Vorbis window, 480-point FFT, 20 Opus-scale
//    triangular bands, smoothed log spectrum, 22-point DCT, cepstral
//    average, derivatives, cross-correlation and variability over 8 frames).
//    The HPF in front stays off, as in libwebrtc.
//  - RnnVad (rnn.cc, rnn_fc.cc, rnn_gru.cc): 42 features -> FC(24, tansig)
//    -> GRU(24) -> FC(1, sigmoid), weights int8 / 256, rnnoise's tabulated
//    activations; silence (band energy < 0.04) resets the GRU and gives 0.
//
// Differences from libwebrtc that remain (catalog.py, #242):
//  - Dot products sum in order (std::inner_product, libwebrtc's portable
//    path); libwebrtc picks SSE2, AVX2 or NEON at run time, which sum in
//    another order.
//  - The FFTs are this library's, not PFFFT (its source is not in the
//    reference sources): a mixed-radix (2, 3, 5) DFT for the 480-point
//    spectrum and sm::RealFft for the 512-point auto-correlation. Same
//    transforms and scaling, rounding differs in the last bits.
// So the speech probability matches libwebrtc's to float precision, not bit
// for bit.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "sm/audio/sinc_resampler.h"
#include "sm/core/fft.h"

namespace sm::audio::agc2 {

// third_party/rnnoise/src/rnn_vad_weights.h.
namespace rnnoise {
inline constexpr float kWeightsScale = 1.f / 256.f;
inline constexpr size_t kInputLayerInputSize = 42;
inline constexpr size_t kInputLayerOutputSize = 24;
inline constexpr size_t kInputLayerWeights = kInputLayerInputSize * kInputLayerOutputSize;
extern const int8_t kInputDenseWeights[kInputLayerWeights];
extern const int8_t kInputDenseBias[kInputLayerOutputSize];
inline constexpr size_t kHiddenLayerOutputSize = 24;
inline constexpr size_t kHiddenLayerWeights = 3 * kInputLayerOutputSize * kHiddenLayerOutputSize;
inline constexpr size_t kHiddenLayerBiases = 3 * kHiddenLayerOutputSize;
extern const int8_t kHiddenGruWeights[kHiddenLayerWeights];
extern const int8_t kHiddenGruRecurrentWeights[kHiddenLayerWeights];
extern const int8_t kHiddenGruBias[kHiddenLayerBiases];
inline constexpr size_t kOutputLayerOutputSize = 1;
inline constexpr size_t kOutputLayerWeights = kHiddenLayerOutputSize * kOutputLayerOutputSize;
extern const int8_t kOutputDenseWeights[kOutputLayerWeights];
extern const int8_t kOutputDenseBias[kOutputLayerOutputSize];

// rnn_activations.h.
float TansigApproximated(float x);
inline float SigmoidApproximated(float x) { return 0.5f + 0.5f * TansigApproximated(0.5f * x); }
}  // namespace rnnoise

namespace rnn_vad {

// common.h.
inline constexpr int kSampleRate24kHz = 24000;
inline constexpr int kFrameSize10ms24kHz = kSampleRate24kHz / 100;
inline constexpr int kFrameSize20ms24kHz = kFrameSize10ms24kHz * 2;
inline constexpr int kMinPitch24kHz = kSampleRate24kHz / 800;                          // 0.00125 s.
inline constexpr int kMaxPitch24kHz = static_cast<int>(kSampleRate24kHz / 62.5);       // 0.016 s.
inline constexpr int kBufSize24kHz = kMaxPitch24kHz + kFrameSize20ms24kHz;
inline constexpr int kInitialMinPitch24kHz = 3 * kMinPitch24kHz;
inline constexpr int kInitialNumLags24kHz = kMaxPitch24kHz - kInitialMinPitch24kHz;
inline constexpr int kRefineNumLags24kHz = kMaxPitch24kHz + 1;
inline constexpr int kSampleRate12kHz = 12000;
inline constexpr int kFrameSize10ms12kHz = kSampleRate12kHz / 100;
inline constexpr int kFrameSize20ms12kHz = kFrameSize10ms12kHz * 2;
inline constexpr int kBufSize12kHz = kBufSize24kHz / 2;
inline constexpr int kInitialMinPitch12kHz = kInitialMinPitch24kHz / 2;
inline constexpr int kMaxPitch12kHz = kMaxPitch24kHz / 2;
inline constexpr int kNumLags12kHz = kMaxPitch12kHz - kInitialMinPitch12kHz;
inline constexpr int kMinPitch48kHz = kMinPitch24kHz * 2;
inline constexpr int kMaxPitch48kHz = kMaxPitch24kHz * 2;
inline constexpr int kNumBands = 22;
inline constexpr int kNumLowerBands = 6;
inline constexpr int kCepstralCoeffsHistorySize = 8;
inline constexpr int kFeatureVectorSize = 42;
// lp_residual.h.
inline constexpr int kNumLpcCoefficients = 5;
// spectral_features_internal.h: at 24 kHz the last Opus bands are beyond
// Nyquist; band 19 gets band 18's contribution.
inline constexpr int kOpusBands24kHz = 20;
constexpr std::array<int, kOpusBands24kHz - 1> GetOpusScaleNumBins24kHz20ms() {
  return {4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 16, 16, 16, 24, 24, 32, 48};
}
// rnn_fc.h, rnn_gru.h.
inline constexpr int kFullyConnectedLayerMaxUnits = 24;
inline constexpr int kGruLayerMaxUnits = 24;

// vector_math.h, portable path.
float DotProduct(std::span<const float> x, std::span<const float> y);

// lp_residual.h.
void ComputeAndPostProcessLpcCoefficients(std::span<const float> x,
                                          std::span<float, kNumLpcCoefficients> lpc_coeffs);
void ComputeLpResidual(std::span<const float, kNumLpcCoefficients> lpc_coeffs, std::span<const float> x,
                       std::span<float> y);

// auto_correlation.h: auto-correlation of the 12 kHz pitch buffer for the
// inverted lags 0..kNumLags12kHz-1, through a 512-point FFT.
class AutoCorrelationCalculator {
 public:
  AutoCorrelationCalculator();
  void ComputeOnPitchBuffer(std::span<const float, kBufSize12kHz> pitch_buf,
                            std::span<float, kNumLags12kHz> auto_corr);

 private:
  RealFft fft_;
  std::vector<float> tmp_;
  std::vector<Complex> x_, h_;
};

// pitch_search_internal.h. Lags are "inverted lags": the distance from the
// start of the most recent 20 ms frame `x` to the end of the sliding frame.
void Decimate2x(std::span<const float, kBufSize24kHz> src, std::span<float, kBufSize12kHz> dst);
void ComputeSlidingFrameSquareEnergies24kHz(std::span<const float, kBufSize24kHz> pitch_buffer,
                                            std::span<float, kRefineNumLags24kHz> y_energy);
struct CandidatePitchPeriods {
  int best;
  int second_best;
};
CandidatePitchPeriods ComputePitchPeriod12kHz(std::span<const float, kBufSize12kHz> pitch_buffer,
                                              std::span<const float, kNumLags12kHz> auto_correlation);
int ComputePitchPeriod48kHz(std::span<const float, kBufSize24kHz> pitch_buffer,
                            std::span<const float, kRefineNumLags24kHz> y_energy,
                            CandidatePitchPeriods pitch_candidates_24kHz);
struct PitchInfo {
  int period;
  float strength;
};
PitchInfo ComputeExtendedPitchPeriod48kHz(std::span<const float, kBufSize24kHz> pitch_buffer,
                                          std::span<const float, kRefineNumLags24kHz> y_energy,
                                          int initial_pitch_period_48kHz, PitchInfo last_pitch_48kHz);

// pitch_search.h.
class PitchEstimator {
 public:
  PitchEstimator();
  // Returns the estimated pitch period at 48 kHz.
  int Estimate(std::span<const float, kBufSize24kHz> pitch_buffer);
  float last_pitch_strength() const { return last_pitch_48kHz_.strength; }

 private:
  PitchInfo last_pitch_48kHz_{};
  AutoCorrelationCalculator auto_corr_calculator_;
  std::vector<float> y_energy_24kHz_;
  std::vector<float> pitch_buffer_12kHz_;
  std::vector<float> auto_correlation_12kHz_;
};

// spectral_features_internal.h.
class SpectralCorrelator {
 public:
  // `x` and `y`: kFrameSize20ms24kHz interleaved real/imaginary FFT
  // coefficients with x[1] = y[1] = 0 (the Nyquist coefficient omitted).
  void ComputeAutoCorrelation(std::span<const float> x, std::span<float, kOpusBands24kHz> auto_corr) const;
  void ComputeCrossCorrelation(std::span<const float> x, std::span<const float> y,
                               std::span<float, kOpusBands24kHz> cross_corr) const;
};
void ComputeSmoothedLogMagnitudeSpectrum(std::span<const float> bands_energy,
                                         std::span<float, kNumBands> log_bands_energy);
std::array<float, kNumBands * kNumBands> ComputeDctTable();
void ComputeDct(std::span<const float> in, std::span<const float, kNumBands * kNumBands> dct_table,
                std::span<float> out);

// A forward real DFT of kFrameSize20ms24kHz points in PFFFT's ordered
// layout: out[0] = DC, out[1] = Nyquist, then (re, im) of bins 1..N/2-1.
class RealDft480 {
 public:
  RealDft480();
  void Forward(std::span<const float, kFrameSize20ms24kHz> in, std::span<float, kFrameSize20ms24kHz> out);

 private:
  void Dft(const Complex* in, int stride, int n, int factor_index, Complex* out);
  std::array<int, 7> factors_{};
  int num_factors_ = 0;
  std::vector<Complex> twiddle_;  // e^{-2 pi i k / N}, k < N.
  std::vector<Complex> in_, out_;
};

// spectral_features.h.
class SpectralFeaturesExtractor {
 public:
  SpectralFeaturesExtractor();
  void Reset();
  // Returns true if silence is detected; the outputs are written only
  // otherwise.
  bool CheckSilenceComputeFeatures(std::span<const float, kFrameSize20ms24kHz> reference_frame,
                                   std::span<const float, kFrameSize20ms24kHz> lagged_frame,
                                   std::span<float, kNumBands - kNumLowerBands> higher_bands_cepstrum,
                                   std::span<float, kNumLowerBands> average,
                                   std::span<float, kNumLowerBands> first_derivative,
                                   std::span<float, kNumLowerBands> second_derivative,
                                   std::span<float, kNumLowerBands> bands_cross_corr, float* variability);

 private:
  void ComputeAvgAndDerivatives(std::span<float, kNumLowerBands> average,
                                std::span<float, kNumLowerBands> first_derivative,
                                std::span<float, kNumLowerBands> second_derivative) const;
  void ComputeNormalizedCepstralCorrelation(std::span<float, kNumLowerBands> bands_cross_corr);
  float ComputeVariability() const;
  // ring_buffer.h: the last 8 cepstra; delay 0 is the most recent.
  std::span<const float, kNumBands> Cepstrum(int delay) const;
  // symmetric_matrix_buffer.h: squared distances between pairs of the last
  // 8 cepstra.
  float CepstralDistance(int delay1, int delay2) const;

  const std::array<float, kFrameSize20ms24kHz / 2> half_window_;
  RealDft480 fft_;
  std::array<float, kFrameSize20ms24kHz> fft_buffer_{};
  std::array<float, kFrameSize20ms24kHz> reference_frame_fft_{};
  std::array<float, kFrameSize20ms24kHz> lagged_frame_fft_{};
  SpectralCorrelator spectral_correlator_;
  std::array<float, kOpusBands24kHz> reference_frame_bands_energy_{};
  std::array<float, kOpusBands24kHz> lagged_frame_bands_energy_{};
  std::array<float, kOpusBands24kHz> bands_cross_corr_{};
  const std::array<float, kNumBands * kNumBands> dct_table_;
  std::array<float, kNumBands * kCepstralCoeffsHistorySize> cepstral_coeffs_ring_buf_{};
  int ring_tail_ = 0;
  std::array<float, (kCepstralCoeffsHistorySize - 1) * (kCepstralCoeffsHistorySize - 1)> cepstral_diffs_buf_{};
};

// features_extraction.h.
class FeaturesExtractor {
 public:
  FeaturesExtractor();
  void Reset();
  // Returns true if silence is detected; then `feature_vector` is partially
  // written and must not feed the RNN.
  bool CheckSilenceComputeFeatures(std::span<const float, kFrameSize10ms24kHz> samples,
                                   std::span<float, kFeatureVectorSize> feature_vector);

 private:
  std::vector<float> pitch_buf_24kHz_;  // sequence_buffer.h: oldest first.
  std::vector<float> lp_residual_;
  PitchEstimator pitch_estimator_;
  SpectralFeaturesExtractor spectral_features_extractor_;
  int pitch_period_48kHz_ = 0;
};

// rnn_fc.h.
enum class ActivationFunction { kTansigApproximated, kSigmoidApproximated };

class FullyConnectedLayer {
 public:
  FullyConnectedLayer(int input_size, int output_size, std::span<const int8_t> bias,
                      std::span<const int8_t> weights, ActivationFunction activation_function);
  int input_size() const { return input_size_; }
  int size() const { return output_size_; }
  std::span<const float> output() const { return {output_.data(), static_cast<size_t>(output_size_)}; }
  void ComputeOutput(std::span<const float> input);

 private:
  const int input_size_;
  const int output_size_;
  const std::vector<float> bias_;
  const std::vector<float> weights_;
  const ActivationFunction activation_function_;
  std::array<float, kFullyConnectedLayerMaxUnits> output_{};
};

// rnn_gru.h.
class GatedRecurrentLayer {
 public:
  GatedRecurrentLayer(int input_size, int output_size, std::span<const int8_t> bias,
                      std::span<const int8_t> weights, std::span<const int8_t> recurrent_weights);
  int input_size() const { return input_size_; }
  int size() const { return output_size_; }
  std::span<const float> output() const { return {state_.data(), static_cast<size_t>(output_size_)}; }
  void Reset() { state_.fill(0.f); }
  void ComputeOutput(std::span<const float> input);

 private:
  const int input_size_;
  const int output_size_;
  const std::vector<float> bias_;
  const std::vector<float> weights_;
  const std::vector<float> recurrent_weights_;
  std::array<float, kGruLayerMaxUnits> state_{};
};

// rnn.h.
class RnnVad {
 public:
  RnnVad();
  void Reset() { hidden_.Reset(); }
  float ComputeVadProbability(std::span<const float, kFeatureVectorSize> feature_vector, bool is_silence);

 private:
  FullyConnectedLayer input_;
  GatedRecurrentLayer hidden_;
  FullyConnectedLayer output_;
};

}  // namespace rnn_vad

// vad_wrapper.h.
class VoiceActivityDetectorWrapper {
 public:
  // The wrapped single-channel VAD (MonoVad); tests inject their own.
  class MonoVad {
   public:
    virtual ~MonoVad() = default;
    virtual int SampleRateHz() const = 0;
    virtual void Reset() = 0;
    virtual float Analyze(std::span<const float> frame) = 0;
  };

  // libwebrtc's RNN VAD, reset every `vad_reset_period_ms` (1500 in
  // GainController2), on 10 ms frames at `sample_rate_hz`.
  explicit VoiceActivityDetectorWrapper(int sample_rate_hz, int vad_reset_period_ms = 1500);
  VoiceActivityDetectorWrapper(int vad_reset_period_ms, std::unique_ptr<MonoVad> vad, int sample_rate_hz);
  // Speech probability of one 10 ms mono frame, in libwebrtc's FloatS16
  // scale.
  float Analyze(std::span<const float> frame);

 private:
  const int vad_reset_period_frames_;
  const int frame_size_;
  int time_to_vad_reset_;
  std::unique_ptr<MonoVad> vad_;
  std::vector<float> resampled_buffer_;
  std::unique_ptr<PushSincResampler> resampler_;  // Null when no rate change.
};

}  // namespace sm::audio::agc2
