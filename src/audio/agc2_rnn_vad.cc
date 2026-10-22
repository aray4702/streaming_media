// Ported from libwebrtc M157 (BSD license, see compare/webrtc/reference/):
// modules/audio_processing/agc2/vad_wrapper.cc and rnn_vad/{auto_correlation,
// features_extraction,lp_residual,pitch_search,pitch_search_internal,rnn,
// rnn_fc,rnn_gru,spectral_features,spectral_features_internal}.cc, with
// ring_buffer.h, sequence_buffer.h, symmetric_matrix_buffer.h and
// third_party/rnnoise/src/rnn_activations.h. Same float types and operation
// order; the differences (portable dot product, our FFTs) are in the header.
#include "sm/audio/agc2_rnn_vad.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>
#include <numeric>
#include <utility>

namespace sm::audio::agc2 {

namespace rnnoise {

float TansigApproximated(float x) {
  static constexpr float kTansigTable[201] = {
      0.000000f, 0.039979f, 0.079830f, 0.119427f, 0.158649f, 0.197375f, 0.235496f, 0.272905f, 0.309507f,
      0.345214f, 0.379949f, 0.413644f, 0.446244f, 0.477700f, 0.507977f, 0.537050f, 0.564900f, 0.591519f,
      0.616909f, 0.641077f, 0.664037f, 0.685809f, 0.706419f, 0.725897f, 0.744277f, 0.761594f, 0.777888f,
      0.793199f, 0.807569f, 0.821040f, 0.833655f, 0.845456f, 0.856485f, 0.866784f, 0.876393f, 0.885352f,
      0.893698f, 0.901468f, 0.908698f, 0.915420f, 0.921669f, 0.927473f, 0.932862f, 0.937863f, 0.942503f,
      0.946806f, 0.950795f, 0.954492f, 0.957917f, 0.961090f, 0.964028f, 0.966747f, 0.969265f, 0.971594f,
      0.973749f, 0.975743f, 0.977587f, 0.979293f, 0.980869f, 0.982327f, 0.983675f, 0.984921f, 0.986072f,
      0.987136f, 0.988119f, 0.989027f, 0.989867f, 0.990642f, 0.991359f, 0.992020f, 0.992631f, 0.993196f,
      0.993718f, 0.994199f, 0.994644f, 0.995055f, 0.995434f, 0.995784f, 0.996108f, 0.996407f, 0.996682f,
      0.996937f, 0.997172f, 0.997389f, 0.997590f, 0.997775f, 0.997946f, 0.998104f, 0.998249f, 0.998384f,
      0.998508f, 0.998623f, 0.998728f, 0.998826f, 0.998916f, 0.999000f, 0.999076f, 0.999147f, 0.999213f,
      0.999273f, 0.999329f, 0.999381f, 0.999428f, 0.999472f, 0.999513f, 0.999550f, 0.999585f, 0.999617f,
      0.999646f, 0.999673f, 0.999699f, 0.999722f, 0.999743f, 0.999763f, 0.999781f, 0.999798f, 0.999813f,
      0.999828f, 0.999841f, 0.999853f, 0.999865f, 0.999875f, 0.999885f, 0.999893f, 0.999902f, 0.999909f,
      0.999916f, 0.999923f, 0.999929f, 0.999934f, 0.999939f, 0.999944f, 0.999948f, 0.999952f, 0.999956f,
      0.999959f, 0.999962f, 0.999965f, 0.999968f, 0.999970f, 0.999973f, 0.999975f, 0.999977f, 0.999978f,
      0.999980f, 0.999982f, 0.999983f, 0.999984f, 0.999986f, 0.999987f, 0.999988f, 0.999989f, 0.999990f,
      0.999990f, 0.999991f, 0.999992f, 0.999992f, 0.999993f, 0.999994f, 0.999994f, 0.999994f, 0.999995f,
      0.999995f, 0.999996f, 0.999996f, 0.999996f, 0.999997f, 0.999997f, 0.999997f, 0.999997f, 0.999997f,
      0.999998f, 0.999998f, 0.999998f, 0.999998f, 0.999998f, 0.999998f, 0.999999f, 0.999999f, 0.999999f,
      0.999999f, 0.999999f, 0.999999f, 0.999999f, 0.999999f, 0.999999f, 0.999999f, 0.999999f, 0.999999f,
      0.999999f, 1.000000f, 1.000000f, 1.000000f, 1.000000f, 1.000000f, 1.000000f, 1.000000f, 1.000000f,
      1.000000f, 1.000000f, 1.000000f,
  };
  // Tests are reversed to catch NaNs.
  if (!(x < 8.f)) return 1.f;
  if (!(x > -8.f)) return -1.f;
  float sign = 1.f;
  if (x < 0.f) {
    x = -x;
    sign = -1.f;
  }
  // Look-up.
  int i = static_cast<int>(std::floor(0.5f + 25 * x));
  float y = kTansigTable[i];
  // Map i back to x's scale (undo 25 factor).
  x -= 0.04f * i;
  y = y + x * (1.f - y * y) * (1.f - y * x);
  return sign * y;
}

}  // namespace rnnoise

namespace rnn_vad {

namespace {
constexpr double kPi = std::numbers::pi;
}  // namespace

float DotProduct(std::span<const float> x, std::span<const float> y) {
  assert(x.size() == y.size());
  return std::inner_product(x.begin(), x.end(), y.begin(), 0.f);
}

// lp_residual.cc.

namespace {

// Auto-correlation for the lags 0..kNumLpcCoefficients-1.
void ComputeLpcAutoCorrelation(std::span<const float> x, std::span<float, kNumLpcCoefficients> auto_corr) {
  constexpr int max_lag = static_cast<int>(auto_corr.size());
  for (int lag = 0; lag < max_lag; ++lag) {
    auto_corr[lag] = std::inner_product(x.begin(), x.end() - lag, x.begin() + lag, 0.f);
  }
}

// Assumes a -40 dB white noise floor.
void DenoiseAutoCorrelation(std::span<float, kNumLpcCoefficients> auto_corr) {
  auto_corr[0] *= 1.0001f;
  // [np.float32((0.008*0.008*i*i)) for i in range(1,5)].
  auto_corr[1] -= auto_corr[1] * 0.000064f;
  auto_corr[2] -= auto_corr[2] * 0.000256f;
  auto_corr[3] -= auto_corr[3] * 0.000576f;
  auto_corr[4] -= auto_corr[4] * 0.001024f;
  static_assert(kNumLpcCoefficients == 5, "Update `auto_corr`.");
}

// Initial inverse filter coefficients (Levinson-Durbin).
void ComputeInitialInverseFilterCoefficients(std::span<const float, kNumLpcCoefficients> auto_corr,
                                             std::span<float, kNumLpcCoefficients - 1> lpc_coeffs) {
  float error = auto_corr[0];
  for (int i = 0; i < kNumLpcCoefficients - 1; ++i) {
    float reflection_coeff = 0.f;
    for (int j = 0; j < i; ++j) reflection_coeff += lpc_coeffs[j] * auto_corr[i - j];
    reflection_coeff += auto_corr[i + 1];
    // Avoid division by numbers close to zero.
    constexpr float kMinErrorMagnitude = 1e-6f;
    if (std::fabs(error) < kMinErrorMagnitude) error = std::copysign(kMinErrorMagnitude, error);
    reflection_coeff /= -error;
    // Update LPC coefficients and total error.
    lpc_coeffs[i] = reflection_coeff;
    for (int j = 0; j < ((i + 1) >> 1); ++j) {
      const float tmp1 = lpc_coeffs[j];
      const float tmp2 = lpc_coeffs[i - 1 - j];
      lpc_coeffs[j] = tmp1 + reflection_coeff * tmp2;
      lpc_coeffs[i - 1 - j] = tmp2 + reflection_coeff * tmp1;
    }
    error -= reflection_coeff * reflection_coeff * error;
    if (error < 0.001f * auto_corr[0]) break;
  }
}

}  // namespace

void ComputeAndPostProcessLpcCoefficients(std::span<const float> x,
                                          std::span<float, kNumLpcCoefficients> lpc_coeffs) {
  std::array<float, kNumLpcCoefficients> auto_corr;
  ComputeLpcAutoCorrelation(x, auto_corr);
  if (auto_corr[0] == 0.f) {  // Empty frame.
    std::fill(lpc_coeffs.begin(), lpc_coeffs.end(), 0);
    return;
  }
  DenoiseAutoCorrelation(auto_corr);
  std::array<float, kNumLpcCoefficients - 1> lpc_coeffs_pre{};
  ComputeInitialInverseFilterCoefficients(auto_corr, lpc_coeffs_pre);
  // LPC coefficients post-processing.
  lpc_coeffs_pre[0] *= 0.9f;
  lpc_coeffs_pre[1] *= 0.9f * 0.9f;
  lpc_coeffs_pre[2] *= 0.9f * 0.9f * 0.9f;
  lpc_coeffs_pre[3] *= 0.9f * 0.9f * 0.9f * 0.9f;
  constexpr float kC = 0.8f;
  lpc_coeffs[0] = lpc_coeffs_pre[0] + kC;
  lpc_coeffs[1] = lpc_coeffs_pre[1] + kC * lpc_coeffs_pre[0];
  lpc_coeffs[2] = lpc_coeffs_pre[2] + kC * lpc_coeffs_pre[1];
  lpc_coeffs[3] = lpc_coeffs_pre[3] + kC * lpc_coeffs_pre[2];
  lpc_coeffs[4] = kC * lpc_coeffs_pre[3];
  static_assert(kNumLpcCoefficients == 5, "Update `lpc_coeffs(_pre)`.");
}

void ComputeLpResidual(std::span<const float, kNumLpcCoefficients> lpc_coeffs, std::span<const float> x,
                       std::span<float> y) {
  assert(x.size() > static_cast<size_t>(kNumLpcCoefficients));
  assert(x.size() == y.size());
  // y[i] = x[i] + dot_product({x[i], ..., x[i - kNumLpcCoefficients + 1]}, lpc_coeffs)
  // Edge case: i < kNumLpcCoefficients.
  y[0] = x[0];
  for (int i = 1; i < kNumLpcCoefficients; ++i) {
    y[i] = std::inner_product(x.rend() - i, x.rend(), lpc_coeffs.begin(), x[i]);
  }
  // Regular case.
  auto last = x.rend();
  for (size_t i = kNumLpcCoefficients; i < y.size(); ++i, --last) {
    y[i] = std::inner_product(last - kNumLpcCoefficients, last, lpc_coeffs.begin(), x[i]);
  }
}

// auto_correlation.cc.

namespace {
constexpr int kAutoCorrelationFftOrder = 9;  // Length-512 FFT.
constexpr int kFftFrameSize = 1 << kAutoCorrelationFftOrder;
static_assert(kFftFrameSize > kNumLags12kHz + kBufSize12kHz - kMaxPitch12kHz);
}  // namespace

AutoCorrelationCalculator::AutoCorrelationCalculator()
    : fft_(kFftFrameSize), tmp_(kFftFrameSize), x_(kFftFrameSize / 2 + 1), h_(kFftFrameSize / 2 + 1) {}

// |.........|...........|  <- pitch buffer
//           [ x (fixed) ]
// [   y_0   ]
//         [ y_{m-1} ]
// The cross-correlation between y_0 and x is the auto-correlation for the
// maximum pitch period, so auto_corr[0] (inverted lag 0) is that lag.
void AutoCorrelationCalculator::ComputeOnPitchBuffer(std::span<const float, kBufSize12kHz> pitch_buf,
                                                     std::span<float, kNumLags12kHz> auto_corr) {
  constexpr int kConvolutionLength = kBufSize12kHz - kMaxPitch12kHz;
  static_assert(kConvolutionLength == kFrameSize20ms12kHz);
  static_assert(kFftFrameSize > kNumLags12kHz + kConvolutionLength);
  // FFT of the reversed reference frame, pitch_buf[-kConvolutionLength:].
  std::reverse_copy(pitch_buf.end() - kConvolutionLength, pitch_buf.end(), tmp_.begin());
  std::fill(tmp_.begin() + kConvolutionLength, tmp_.end(), 0.f);
  fft_.Forward(tmp_.data(), h_.data());
  // FFT of the chunk holding all the sliding frames,
  // pitch_buf[:kNumLags12kHz + kConvolutionLength].
  std::copy(pitch_buf.begin(), pitch_buf.begin() + kConvolutionLength + kNumLags12kHz, tmp_.begin());
  std::fill(tmp_.begin() + kNumLags12kHz + kConvolutionLength, tmp_.end(), 0.f);
  fft_.Forward(tmp_.data(), x_.data());
  // Convolve in the frequency domain; Inverse() scales by 1 / kFftFrameSize,
  // as libwebrtc's kScalingFactor does.
  for (size_t k = 0; k < x_.size(); ++k) x_[k] *= h_[k];
  fft_.Inverse(x_.data(), tmp_.data());
  std::copy(tmp_.begin() + kConvolutionLength - 1, tmp_.begin() + kConvolutionLength + kNumLags12kHz - 1,
            auto_corr.begin());
}

// pitch_search_internal.cc.

namespace {

float ComputeAutoCorrelation(int inverted_lag, std::span<const float, kBufSize24kHz> pitch_buffer) {
  static_assert(kMaxPitch24kHz < kBufSize24kHz);
  return DotProduct(pitch_buffer.subspan(/*offset=*/kMaxPitch24kHz),
                    pitch_buffer.subspan(static_cast<size_t>(inverted_lag), kFrameSize20ms24kHz));
}

// A pseudo-interpolation offset in {-1, 0, +1} for the pitch period of
// `curr_auto_correlation` given its neighbours.
int GetPitchPseudoInterpolationOffset(float prev_auto_correlation, float curr_auto_correlation,
                                      float next_auto_correlation) {
  if ((next_auto_correlation - prev_auto_correlation) > 0.7f * (curr_auto_correlation - prev_auto_correlation)) {
    return 1;
  } else if ((prev_auto_correlation - next_auto_correlation) >
             0.7f * (curr_auto_correlation - next_auto_correlation)) {
    return -1;
  }
  return 0;
}

// Refines `lag` with pseudo-interpolation; the output is at twice the rate.
int PitchPseudoInterpolationLagPitchBuf(int lag, std::span<const float, kBufSize24kHz> pitch_buffer) {
  int offset = 0;
  // Cannot apply pseudo-interpolation at the boundaries.
  if (lag > 0 && lag < kMaxPitch24kHz) {
    const int inverted_lag = kMaxPitch24kHz - lag;
    offset = GetPitchPseudoInterpolationOffset(ComputeAutoCorrelation(inverted_lag + 1, pitch_buffer),
                                               ComputeAutoCorrelation(inverted_lag, pitch_buffer),
                                               ComputeAutoCorrelation(inverted_lag - 1, pitch_buffer));
  }
  return 2 * lag + offset;
}

// For T/k with k in {2, ..., 15}, the multiple n*T/k also checked, chosen so
// that each n*T/k is used once over all k.
constexpr std::array<int, 14> kSubHarmonicMultipliers = {{3, 2, 3, 2, 5, 2, 3, 2, 3, 2, 5, 2, 3, 2}};

struct Range {
  int min;
  int max;
};

// Number of analyzed pitches to the left (right) of a pitch candidate.
constexpr int kPitchNeighborhoodRadius = 2;

// A pitch period interval centred on `inverted_lag`, clipped to a valid
// interval for a 24 kHz pitch buffer.
Range CreateInvertedLagRange(int inverted_lag) {
  return {std::max(inverted_lag - kPitchNeighborhoodRadius, 0),
          std::min(inverted_lag + kPitchNeighborhoodRadius, kInitialNumLags24kHz - 1)};
}

constexpr int kNumPitchCandidates = 2;  // Best and second best.
constexpr int kMaxPitchPeriods24kHz = kNumPitchCandidates * (2 * kPitchNeighborhoodRadius + 1);

class InvertedLagsIndex {
 public:
  void Append(int inverted_lag) {
    assert(num_entries_ < kMaxPitchPeriods24kHz);
    inverted_lags_[static_cast<size_t>(num_entries_++)] = inverted_lag;
  }
  std::span<const int> view() const { return {inverted_lags_.data(), static_cast<size_t>(num_entries_)}; }

 private:
  std::array<int, kMaxPitchPeriods24kHz> inverted_lags_{};
  int num_entries_ = 0;
};

// Auto-correlation for the inverted lags in the closed interval
// `inverted_lags`, appended to `inverted_lags_index`.
void ComputeAutoCorrelation(Range inverted_lags, std::span<const float, kBufSize24kHz> pitch_buffer,
                            std::span<float, kInitialNumLags24kHz> auto_correlation,
                            InvertedLagsIndex& inverted_lags_index) {
  assert(inverted_lags.min <= inverted_lags.max);
  // Avoids zero-initializing `auto_correlation`; the pseudo-interpolation
  // reads the neighbours.
  if (inverted_lags.min > 0) auto_correlation[inverted_lags.min - 1] = 0.f;
  if (inverted_lags.max < kInitialNumLags24kHz - 1) auto_correlation[inverted_lags.max + 1] = 0.f;
  for (int inverted_lag = inverted_lags.min; inverted_lag <= inverted_lags.max; ++inverted_lag) {
    auto_correlation[inverted_lag] = ComputeAutoCorrelation(inverted_lag, pitch_buffer);
    inverted_lags_index.Append(inverted_lag);
  }
}

// The strongest pitch period at 24 kHz, as an inverted lag at 48 kHz.
int ComputePitchPeriod48kHz(std::span<const int> inverted_lags,
                            std::span<const float, kInitialNumLags24kHz> auto_correlation,
                            std::span<const float, kRefineNumLags24kHz> y_energy) {
  static_assert(kMaxPitch24kHz > kInitialNumLags24kHz);
  int best_inverted_lag = 0;     // Pitch period.
  float best_numerator = -1.f;   // Pitch strength numerator.
  float best_denominator = 0.f;  // Pitch strength denominator.
  for (int inverted_lag : inverted_lags) {
    // A pitch candidate must have positive correlation.
    if (auto_correlation[inverted_lag] > 0.f) {
      // Auto-correlation energy normalized by frame energy.
      const float numerator = auto_correlation[inverted_lag] * auto_correlation[inverted_lag];
      const float denominator = y_energy[inverted_lag];
      // Compare numerator/denominator ratios without using divisions.
      if (numerator * best_denominator > best_numerator * denominator) {
        best_inverted_lag = inverted_lag;
        best_numerator = numerator;
        best_denominator = denominator;
      }
    }
  }
  // Pseudo-interpolation from the 24 kHz pitch to a 48 kHz period.
  if (best_inverted_lag == 0 || best_inverted_lag >= kInitialNumLags24kHz - 1) {
    return best_inverted_lag * 2;
  }
  int offset = GetPitchPseudoInterpolationOffset(auto_correlation[best_inverted_lag + 1],
                                                 auto_correlation[best_inverted_lag],
                                                 auto_correlation[best_inverted_lag - 1]);
  // As libwebrtc: `offset` is a lag but is added to an inverted lag
  // (bugs.webrtc.org/9076).
  return 2 * best_inverted_lag + offset;
}

// round(multiplier * pitch_period / divisor).
constexpr int GetAlternativePitchPeriod(int pitch_period, int multiplier, int divisor) {
  return (2 * multiplier * pitch_period + divisor) / (2 * divisor);
}

bool IsAlternativePitchStrongerThanInitial(PitchInfo last, PitchInfo initial, PitchInfo alternative,
                                           int period_divisor) {
  // [5*k*k for k in range(16)], for a 24 kHz sample rate.
  constexpr std::array<int, 14> kInitialPitchPeriodThresholds = {
      {20, 45, 80, 125, 180, 245, 320, 405, 500, 605, 720, 845, 980, 1125}};
  static_assert(kInitialPitchPeriodThresholds.size() == kSubHarmonicMultipliers.size());
  // Lower the threshold when `alternative.period` is close to the last
  // estimated period (pitch tracking).
  float lower_threshold_term = 0.f;
  if (std::abs(alternative.period - last.period) <= 1) {
    lower_threshold_term = last.strength;
  } else if (std::abs(alternative.period - last.period) == 2 &&
             initial.period > kInitialPitchPeriodThresholds[static_cast<size_t>(period_divisor - 2)]) {
    lower_threshold_term = 0.5f * last.strength;
  }
  // The threshold follows the strength of the initial estimate, raised for
  // high frequencies (short-term correlations bias towards them).
  float threshold = std::max(0.3f, 0.7f * initial.strength - lower_threshold_term);
  if (alternative.period < 3 * kMinPitch24kHz) {
    // High frequency.
    threshold = std::max(0.4f, 0.85f * initial.strength - lower_threshold_term);
  } else if (alternative.period < 2 * kMinPitch24kHz) {
    // Even higher frequency (never reached, as in libwebrtc).
    threshold = std::max(0.5f, 0.9f * initial.strength - lower_threshold_term);
  }
  return alternative.strength > threshold;
}

}  // namespace

void Decimate2x(std::span<const float, kBufSize24kHz> src, std::span<float, kBufSize12kHz> dst) {
  // No anti-aliasing filter, as libwebrtc.
  static_assert(2 * kBufSize12kHz == kBufSize24kHz);
  for (int i = 0; i < kBufSize12kHz; ++i) dst[i] = src[2 * i];
}

void ComputeSlidingFrameSquareEnergies24kHz(std::span<const float, kBufSize24kHz> pitch_buffer,
                                            std::span<float, kRefineNumLags24kHz> y_energy) {
  const auto frame_20ms_view = pitch_buffer.subspan(0, kFrameSize20ms24kHz);
  float yy = DotProduct(frame_20ms_view, frame_20ms_view);
  y_energy[0] = yy;
  static_assert(kMaxPitch24kHz - 1 + kFrameSize20ms24kHz < kBufSize24kHz);
  static_assert(kMaxPitch24kHz < kRefineNumLags24kHz);
  for (int inverted_lag = 0; inverted_lag < kMaxPitch24kHz; ++inverted_lag) {
    yy -= pitch_buffer[inverted_lag] * pitch_buffer[inverted_lag];
    yy += pitch_buffer[inverted_lag + kFrameSize20ms24kHz] * pitch_buffer[inverted_lag + kFrameSize20ms24kHz];
    yy = std::max(1.f, yy);
    y_energy[inverted_lag + 1] = yy;
  }
}

CandidatePitchPeriods ComputePitchPeriod12kHz(std::span<const float, kBufSize12kHz> pitch_buffer,
                                              std::span<const float, kNumLags12kHz> auto_correlation) {
  static_assert(kMaxPitch12kHz > kNumLags12kHz);
  static_assert(kMaxPitch12kHz < kBufSize12kHz);
  struct PitchCandidate {
    int period_inverted_lag = 0;
    float strength_numerator = -1.f;
    float strength_denominator = 0.f;
    bool HasStrongerPitchThan(const PitchCandidate& b) const {
      return strength_numerator * b.strength_denominator > b.strength_numerator * strength_denominator;
    }
  };
  static_assert(kFrameSize20ms12kHz + 1 < kBufSize12kHz);
  const auto frame_view = pitch_buffer.subspan(0, kFrameSize20ms12kHz + 1);
  float denominator = 1.f + DotProduct(frame_view, frame_view);
  // The best and second best pitches by scaled auto-correlation.
  PitchCandidate best;
  PitchCandidate second_best;
  second_best.period_inverted_lag = 1;
  for (int inverted_lag = 0; inverted_lag < kNumLags12kHz; ++inverted_lag) {
    // A pitch candidate must have positive correlation.
    if (auto_correlation[inverted_lag] > 0.f) {
      PitchCandidate candidate{inverted_lag, auto_correlation[inverted_lag] * auto_correlation[inverted_lag],
                               denominator};
      if (candidate.HasStrongerPitchThan(second_best)) {
        if (candidate.HasStrongerPitchThan(best)) {
          second_best = best;
          best = candidate;
        } else {
          second_best = candidate;
        }
      }
    }
    // The squared energy of `y` for the next inverted lag.
    const float y_old = pitch_buffer[inverted_lag];
    const float y_new = pitch_buffer[inverted_lag + kFrameSize20ms12kHz];
    denominator -= y_old * y_old;
    denominator += y_new * y_new;
    denominator = std::max(0.f, denominator);
  }
  return {best.period_inverted_lag, second_best.period_inverted_lag};
}

int ComputePitchPeriod48kHz(std::span<const float, kBufSize24kHz> pitch_buffer,
                            std::span<const float, kRefineNumLags24kHz> y_energy,
                            CandidatePitchPeriods pitch_candidates) {
  // Auto-correlation only for the neighbours of the two candidates.
  std::array<float, kInitialNumLags24kHz> auto_correlation;
  InvertedLagsIndex inverted_lags_index;
  // Two inverted lag ranges with `r1` before `r2`.
  const bool swap_candidates = pitch_candidates.best > pitch_candidates.second_best;
  const Range r1 = CreateInvertedLagRange(swap_candidates ? pitch_candidates.second_best : pitch_candidates.best);
  const Range r2 = CreateInvertedLagRange(swap_candidates ? pitch_candidates.best : pitch_candidates.second_best);
  if (r1.max + 1 >= r2.min) {
    // Overlapping or adjacent ranges.
    ComputeAutoCorrelation({r1.min, r2.max}, pitch_buffer, auto_correlation, inverted_lags_index);
  } else {
    // Disjoint ranges.
    ComputeAutoCorrelation(r1, pitch_buffer, auto_correlation, inverted_lags_index);
    ComputeAutoCorrelation(r2, pitch_buffer, auto_correlation, inverted_lags_index);
  }
  return ComputePitchPeriod48kHz(inverted_lags_index.view(), auto_correlation, y_energy);
}

PitchInfo ComputeExtendedPitchPeriod48kHz(std::span<const float, kBufSize24kHz> pitch_buffer,
                                          std::span<const float, kRefineNumLags24kHz> y_energy,
                                          int initial_pitch_period_48kHz, PitchInfo last_pitch_48kHz) {
  assert(kMinPitch48kHz <= initial_pitch_period_48kHz && initial_pitch_period_48kHz <= kMaxPitch48kHz);
  struct RefinedPitchCandidate {
    int period;
    float strength;
    float xy;        // Auto-correlation.
    float y_energy;  // Energy of the sliding frame `y`.
  };
  const float x_energy = y_energy[kMaxPitch24kHz];
  const auto pitch_strength = [x_energy](float xy, float yy) {
    return xy / std::sqrt(1.f + x_energy * yy);
  };
  // Start from `initial_pitch_period_48kHz`.
  RefinedPitchCandidate best_pitch;
  best_pitch.period = std::min(initial_pitch_period_48kHz / 2, kMaxPitch24kHz - 1);
  best_pitch.xy = ComputeAutoCorrelation(kMaxPitch24kHz - best_pitch.period, pitch_buffer);
  best_pitch.y_energy = y_energy[kMaxPitch24kHz - best_pitch.period];
  best_pitch.strength = pitch_strength(best_pitch.xy, best_pitch.y_energy);
  const PitchInfo initial_pitch{best_pitch.period, best_pitch.strength};
  // 24 kHz version of the last estimated pitch.
  const PitchInfo last_pitch{last_pitch_48kHz.period / 2, last_pitch_48kHz.strength};
  // `max_period_divisor` such that GetAlternativePitchPeriod(initial, 1,
  // max_period_divisor) equals kMinPitch24kHz.
  const int max_period_divisor = (2 * initial_pitch.period) / (2 * kMinPitch24kHz - 1);
  for (int period_divisor = 2; period_divisor <= max_period_divisor; ++period_divisor) {
    PitchInfo alternative_pitch;
    alternative_pitch.period = GetAlternativePitchPeriod(initial_pitch.period, /*multiplier=*/1, period_divisor);
    // One sub-harmonic of `alternative_pitch.period` is checked too. For
    // `period_divisor` 2 it may exceed the maximum pitch period.
    int dual_alternative_period = GetAlternativePitchPeriod(
        initial_pitch.period, kSubHarmonicMultipliers[static_cast<size_t>(period_divisor - 2)], period_divisor);
    if (period_divisor == 2 && dual_alternative_period > kMaxPitch24kHz) {
      dual_alternative_period = initial_pitch.period;
    }
    const float xy_primary_period = ComputeAutoCorrelation(kMaxPitch24kHz - alternative_pitch.period, pitch_buffer);
    const float xy_secondary_period = ComputeAutoCorrelation(kMaxPitch24kHz - dual_alternative_period, pitch_buffer);
    const float xy = 0.5f * (xy_primary_period + xy_secondary_period);
    const float yy = 0.5f * (y_energy[kMaxPitch24kHz - alternative_pitch.period] +
                             y_energy[kMaxPitch24kHz - dual_alternative_period]);
    alternative_pitch.strength = pitch_strength(xy, yy);
    if (IsAlternativePitchStrongerThanInitial(last_pitch, initial_pitch, alternative_pitch, period_divisor)) {
      best_pitch = {alternative_pitch.period, alternative_pitch.strength, xy, yy};
    }
  }
  // Final pitch strength and period.
  best_pitch.xy = std::max(0.f, best_pitch.xy);
  float final_pitch_strength =
      (best_pitch.y_energy <= best_pitch.xy) ? 1.f : best_pitch.xy / (best_pitch.y_energy + 1.f);
  final_pitch_strength = std::min(best_pitch.strength, final_pitch_strength);
  int final_pitch_period_48kHz =
      std::max(kMinPitch48kHz, PitchPseudoInterpolationLagPitchBuf(best_pitch.period, pitch_buffer));
  return {final_pitch_period_48kHz, final_pitch_strength};
}

// pitch_search.cc.

PitchEstimator::PitchEstimator()
    : y_energy_24kHz_(kRefineNumLags24kHz, 0.f),
      pitch_buffer_12kHz_(kBufSize12kHz),
      auto_correlation_12kHz_(kNumLags12kHz) {}

int PitchEstimator::Estimate(std::span<const float, kBufSize24kHz> pitch_buffer) {
  std::span<float, kBufSize12kHz> pitch_buffer_12kHz_view(pitch_buffer_12kHz_.data(), kBufSize12kHz);
  std::span<float, kNumLags12kHz> auto_correlation_12kHz_view(auto_correlation_12kHz_.data(), kNumLags12kHz);
  // The initial pitch search at 12 kHz.
  Decimate2x(pitch_buffer, pitch_buffer_12kHz_view);
  auto_corr_calculator_.ComputeOnPitchBuffer(pitch_buffer_12kHz_view, auto_correlation_12kHz_view);
  CandidatePitchPeriods pitch_periods = ComputePitchPeriod12kHz(pitch_buffer_12kHz_view, auto_correlation_12kHz_view);
  // The refinement runs on the 24 kHz pitch buffer, so the inverted lags go
  // from 12 to 24 kHz.
  pitch_periods.best *= 2;
  pitch_periods.second_best *= 2;
  // Frame energies at 24 kHz.
  std::span<float, kRefineNumLags24kHz> y_energy_24kHz_view(y_energy_24kHz_.data(), kRefineNumLags24kHz);
  ComputeSlidingFrameSquareEnergies24kHz(pitch_buffer, y_energy_24kHz_view);
  // Estimation at 48 kHz.
  const int pitch_lag_48kHz = ComputePitchPeriod48kHz(pitch_buffer, y_energy_24kHz_view, pitch_periods);
  last_pitch_48kHz_ = ComputeExtendedPitchPeriod48kHz(pitch_buffer, y_energy_24kHz_view,
                                                      /*initial_pitch_period_48kHz=*/kMaxPitch48kHz - pitch_lag_48kHz,
                                                      last_pitch_48kHz_);
  return last_pitch_48kHz_.period;
}

// spectral_features_internal.cc.

namespace {

// Weights of each FFT coefficient for each Opus band (Nyquist excluded),
// the band sizes being GetOpusScaleNumBins24kHz20ms(); j / band_size within
// each band, written out as libwebrtc's table.
constexpr std::array<float, kFrameSize20ms24kHz / 2> kOpusBandWeights24kHz20ms = {{
    0.f,       0.25f,      0.5f,       0.75f,                                // Band 0
    0.f,       0.25f,      0.5f,       0.75f,                                // Band 1
    0.f,       0.25f,      0.5f,       0.75f,                                // Band 2
    0.f,       0.25f,      0.5f,       0.75f,                                // Band 3
    0.f,       0.25f,      0.5f,       0.75f,                                // Band 4
    0.f,       0.25f,      0.5f,       0.75f,                                // Band 5
    0.f,       0.25f,      0.5f,       0.75f,                                // Band 6
    0.f,       0.25f,      0.5f,       0.75f,                                // Band 7
    0.f,       0.125f,     0.25f,      0.375f,    0.5f,
    0.625f,    0.75f,      0.875f,                                           // Band 8
    0.f,       0.125f,     0.25f,      0.375f,    0.5f,
    0.625f,    0.75f,      0.875f,                                           // Band 9
    0.f,       0.125f,     0.25f,      0.375f,    0.5f,
    0.625f,    0.75f,      0.875f,                                           // Band 10
    0.f,       0.125f,     0.25f,      0.375f,    0.5f,
    0.625f,    0.75f,      0.875f,                                           // Band 11
    0.f,       0.0625f,    0.125f,     0.1875f,   0.25f,
    0.3125f,   0.375f,     0.4375f,    0.5f,      0.5625f,
    0.625f,    0.6875f,    0.75f,      0.8125f,   0.875f,
    0.9375f,                                                                 // Band 12
    0.f,       0.0625f,    0.125f,     0.1875f,   0.25f,
    0.3125f,   0.375f,     0.4375f,    0.5f,      0.5625f,
    0.625f,    0.6875f,    0.75f,      0.8125f,   0.875f,
    0.9375f,                                                                 // Band 13
    0.f,       0.0625f,    0.125f,     0.1875f,   0.25f,
    0.3125f,   0.375f,     0.4375f,    0.5f,      0.5625f,
    0.625f,    0.6875f,    0.75f,      0.8125f,   0.875f,
    0.9375f,                                                                 // Band 14
    0.f,       0.0416667f, 0.0833333f, 0.125f,    0.166667f,
    0.208333f, 0.25f,      0.291667f,  0.333333f, 0.375f,
    0.416667f, 0.458333f,  0.5f,       0.541667f, 0.583333f,
    0.625f,    0.666667f,  0.708333f,  0.75f,     0.791667f,
    0.833333f, 0.875f,     0.916667f,  0.958333f,                            // Band 15
    0.f,       0.0416667f, 0.0833333f, 0.125f,    0.166667f,
    0.208333f, 0.25f,      0.291667f,  0.333333f, 0.375f,
    0.416667f, 0.458333f,  0.5f,       0.541667f, 0.583333f,
    0.625f,    0.666667f,  0.708333f,  0.75f,     0.791667f,
    0.833333f, 0.875f,     0.916667f,  0.958333f,                            // Band 16
    0.f,       0.03125f,   0.0625f,    0.09375f,  0.125f,
    0.15625f,  0.1875f,    0.21875f,   0.25f,     0.28125f,
    0.3125f,   0.34375f,   0.375f,     0.40625f,  0.4375f,
    0.46875f,  0.5f,       0.53125f,   0.5625f,   0.59375f,
    0.625f,    0.65625f,   0.6875f,    0.71875f,  0.75f,
    0.78125f,  0.8125f,    0.84375f,   0.875f,    0.90625f,
    0.9375f,   0.96875f,                                                     // Band 17
    0.f,       0.0208333f, 0.0416667f, 0.0625f,   0.0833333f,
    0.104167f, 0.125f,     0.145833f,  0.166667f, 0.1875f,
    0.208333f, 0.229167f,  0.25f,      0.270833f, 0.291667f,
    0.3125f,   0.333333f,  0.354167f,  0.375f,    0.395833f,
    0.416667f, 0.4375f,    0.458333f,  0.479167f, 0.5f,
    0.520833f, 0.541667f,  0.5625f,    0.583333f, 0.604167f,
    0.625f,    0.645833f,  0.666667f,  0.6875f,   0.708333f,
    0.729167f, 0.75f,      0.770833f,  0.791667f, 0.8125f,
    0.833333f, 0.854167f,  0.875f,     0.895833f, 0.916667f,
    0.9375f,   0.958333f,  0.979167f                                         // Band 18
}};

}  // namespace

void SpectralCorrelator::ComputeAutoCorrelation(std::span<const float> x,
                                                std::span<float, kOpusBands24kHz> auto_corr) const {
  ComputeCrossCorrelation(x, x, auto_corr);
}

void SpectralCorrelator::ComputeCrossCorrelation(std::span<const float> x, std::span<const float> y,
                                                 std::span<float, kOpusBands24kHz> cross_corr) const {
  assert(x.size() == static_cast<size_t>(kFrameSize20ms24kHz) && x.size() == y.size());
  constexpr auto kOpusScaleNumBins24kHz20ms = GetOpusScaleNumBins24kHz20ms();
  int k = 0;  // Next Fourier coefficient index.
  cross_corr[0] = 0.f;
  for (int i = 0; i < kOpusBands24kHz - 1; ++i) {
    cross_corr[i + 1] = 0.f;
    for (int j = 0; j < kOpusScaleNumBins24kHz20ms[static_cast<size_t>(i)]; ++j) {  // Band size.
      const float v = x[2 * k] * y[2 * k] + x[2 * k + 1] * y[2 * k + 1];
      const float tmp = kOpusBandWeights24kHz20ms[static_cast<size_t>(k)] * v;
      cross_corr[i] += v - tmp;
      cross_corr[i + 1] += tmp;
      k++;
    }
  }
  cross_corr[0] *= 2.f;  // The first band only gets half contribution.
  assert(k == kFrameSize20ms24kHz / 2);  // Nyquist coefficient never used.
}

void ComputeSmoothedLogMagnitudeSpectrum(std::span<const float> bands_energy,
                                         std::span<float, kNumBands> log_bands_energy) {
  assert(bands_energy.size() <= static_cast<size_t>(kNumBands));
  constexpr float kOneByHundred = 1e-2f;
  constexpr float kLogOneByHundred = -2.f;
  float log_max = kLogOneByHundred;
  float follow = kLogOneByHundred;
  const auto smooth = [&log_max, &follow](float x) {
    x = std::max(log_max - 7.f, std::max(follow - 1.5f, x));
    log_max = std::max(log_max, x);
    follow = std::max(follow - 1.5f, x);
    return x;
  };
  // Smoothing over the bands for which the band energy is defined.
  for (size_t i = 0; i < bands_energy.size(); ++i) {
    log_bands_energy[i] = smooth(std::log10(kOneByHundred + bands_energy[i]));
  }
  // Smoothing over the remaining bands (zero energy).
  for (size_t i = bands_energy.size(); i < static_cast<size_t>(kNumBands); ++i) {
    log_bands_energy[i] = smooth(kLogOneByHundred);
  }
}

std::array<float, kNumBands * kNumBands> ComputeDctTable() {
  std::array<float, kNumBands * kNumBands> dct_table;
  const double k = std::sqrt(0.5);
  for (int i = 0; i < kNumBands; ++i) {
    for (int j = 0; j < kNumBands; ++j) {
      dct_table[static_cast<size_t>(i * kNumBands + j)] =
          static_cast<float>(std::cos((i + 0.5) * j * kPi / kNumBands));
    }
    // float *= double, as libwebrtc.
    dct_table[static_cast<size_t>(i * kNumBands)] = static_cast<float>(dct_table[static_cast<size_t>(i * kNumBands)] * k);
  }
  return dct_table;
}

void ComputeDct(std::span<const float> in, std::span<const float, kNumBands * kNumBands> dct_table,
                std::span<float> out) {
  // sqrt(2 / kNumBands).
  constexpr float kDctScalingFactor = 0.301511345f;
  assert(in.data() != out.data());
  assert(in.size() <= static_cast<size_t>(kNumBands) && 1 <= out.size() && out.size() <= in.size());
  for (size_t i = 0; i < out.size(); ++i) {
    out[i] = 0.f;
    for (size_t j = 0; j < in.size(); ++j) out[i] += in[j] * dct_table[j * kNumBands + i];
    out[i] *= kDctScalingFactor;
  }
}

// The 480-point DFT (PFFFT's ordered real transform in libwebrtc).

RealDft480::RealDft480()
    : twiddle_(kFrameSize20ms24kHz),
      in_(kFrameSize20ms24kHz),
      out_(kFrameSize20ms24kHz) {
  int n = kFrameSize20ms24kHz;
  for (int p : {2, 3, 5}) {
    while (n % p == 0) {
      factors_[static_cast<size_t>(num_factors_++)] = p;
      n /= p;
    }
  }
  assert(n == 1);
  for (int k = 0; k < kFrameSize20ms24kHz; ++k) {
    const double a = -2.0 * kPi * k / kFrameSize20ms24kHz;
    twiddle_[static_cast<size_t>(k)] = Complex(static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a)));
  }
}

// Decimation in time: `n` points of `in` spaced by `stride` into `out`.
void RealDft480::Dft(const Complex* in, int stride, int n, int factor_index, Complex* out) {
  if (n == 1) {
    out[0] = in[0];
    return;
  }
  const int p = factors_[static_cast<size_t>(factor_index)];
  const int m = n / p;
  for (int r = 0; r < p; ++r) Dft(in + r * stride, stride * p, m, factor_index + 1, out + r * m);
  // out[k + q m] = sum_r sub_r[k] W_n^(r (k + q m)); for each k the p inputs
  // and the p outputs share the same slots.
  const int twiddle_step = kFrameSize20ms24kHz / n;
  std::array<Complex, 5> t;
  for (int k = 0; k < m; ++k) {
    for (int r = 0; r < p; ++r) t[static_cast<size_t>(r)] = out[r * m + k];
    for (int q = 0; q < p; ++q) {
      const int j = k + q * m;
      Complex sum = t[0];
      for (int r = 1; r < p; ++r) {
        sum += t[static_cast<size_t>(r)] * twiddle_[static_cast<size_t>((r * j % n) * twiddle_step)];
      }
      out[j] = sum;
    }
  }
}

void RealDft480::Forward(std::span<const float, kFrameSize20ms24kHz> in, std::span<float, kFrameSize20ms24kHz> out) {
  for (int i = 0; i < kFrameSize20ms24kHz; ++i) in_[static_cast<size_t>(i)] = Complex(in[i], 0.f);
  Dft(in_.data(), 1, kFrameSize20ms24kHz, 0, out_.data());
  out[0] = out_[0].real();
  out[1] = out_[kFrameSize20ms24kHz / 2].real();
  for (int k = 1; k < kFrameSize20ms24kHz / 2; ++k) {
    out[2 * k] = out_[static_cast<size_t>(k)].real();
    out[2 * k + 1] = out_[static_cast<size_t>(k)].imag();
  }
}

// spectral_features.cc.

namespace {

constexpr float kSilenceThreshold = 0.04f;

std::array<float, kFrameSize20ms24kHz / 2> ComputeScaledHalfVorbisWindow(float scaling = 1.f) {
  constexpr int kHalfSize = kFrameSize20ms24kHz / 2;
  std::array<float, kHalfSize> half_window{};
  for (int i = 0; i < kHalfSize; ++i) {
    half_window[static_cast<size_t>(i)] = static_cast<float>(
        scaling * std::sin(0.5 * kPi * std::sin(0.5 * kPi * (i + 0.5) / kHalfSize) *
                           std::sin(0.5 * kPi * (i + 0.5) / kHalfSize)));
  }
  return half_window;
}

void ComputeWindowedForwardFft(std::span<const float, kFrameSize20ms24kHz> frame,
                               const std::array<float, kFrameSize20ms24kHz / 2>& half_window,
                               std::span<float, kFrameSize20ms24kHz> fft_input_buffer,
                               std::span<float, kFrameSize20ms24kHz> fft_output_buffer, RealDft480& fft) {
  for (int i = 0, j = kFrameSize20ms24kHz - 1; i < static_cast<int>(half_window.size()); ++i, --j) {
    fft_input_buffer[i] = frame[i] * half_window[static_cast<size_t>(i)];
    fft_input_buffer[j] = frame[j] * half_window[static_cast<size_t>(i)];
  }
  fft.Forward(fft_input_buffer, fft_output_buffer);
  fft_output_buffer[1] = 0.f;
}

}  // namespace

SpectralFeaturesExtractor::SpectralFeaturesExtractor()
    : half_window_(ComputeScaledHalfVorbisWindow(1.f / static_cast<float>(kFrameSize20ms24kHz))),
      dct_table_(ComputeDctTable()) {}

void SpectralFeaturesExtractor::Reset() {
  cepstral_coeffs_ring_buf_.fill(0.f);
  cepstral_diffs_buf_.fill(0.f);
}

std::span<const float, kNumBands> SpectralFeaturesExtractor::Cepstrum(int delay) const {
  assert(0 <= delay && delay < kCepstralCoeffsHistorySize);
  int offset = ring_tail_ - 1 - delay;
  if (offset < 0) offset += kCepstralCoeffsHistorySize;
  return std::span<const float, kNumBands>(cepstral_coeffs_ring_buf_.data() + kNumBands * offset, kNumBands);
}

float SpectralFeaturesExtractor::CepstralDistance(int delay1, int delay2) const {
  constexpr int S = kCepstralCoeffsHistorySize;
  int row = S - 1 - delay1;
  int col = S - 1 - delay2;
  assert(row != col);
  if (row > col) std::swap(row, col);  // The upper-right triangular part.
  return cepstral_diffs_buf_[static_cast<size_t>(row * (S - 1) + (col - 1))];
}

bool SpectralFeaturesExtractor::CheckSilenceComputeFeatures(
    std::span<const float, kFrameSize20ms24kHz> reference_frame,
    std::span<const float, kFrameSize20ms24kHz> lagged_frame,
    std::span<float, kNumBands - kNumLowerBands> higher_bands_cepstrum, std::span<float, kNumLowerBands> average,
    std::span<float, kNumLowerBands> first_derivative, std::span<float, kNumLowerBands> second_derivative,
    std::span<float, kNumLowerBands> bands_cross_corr, float* variability) {
  // The reference frame's band energies decide silence.
  ComputeWindowedForwardFft(reference_frame, half_window_, fft_buffer_, reference_frame_fft_, fft_);
  spectral_correlator_.ComputeAutoCorrelation(reference_frame_fft_, reference_frame_bands_energy_);
  const float tot_energy =
      std::accumulate(reference_frame_bands_energy_.begin(), reference_frame_bands_energy_.end(), 0.f);
  if (tot_energy < kSilenceThreshold) return true;
  // The pitch-lagged frame's band energies.
  ComputeWindowedForwardFft(lagged_frame, half_window_, fft_buffer_, lagged_frame_fft_, fft_);
  spectral_correlator_.ComputeAutoCorrelation(lagged_frame_fft_, lagged_frame_bands_energy_);
  // The cepstrum.
  std::array<float, kNumBands> log_bands_energy;
  ComputeSmoothedLogMagnitudeSpectrum(reference_frame_bands_energy_, log_bands_energy);
  std::array<float, kNumBands> cepstrum;
  ComputeDct(log_bands_energy, dct_table_, cepstrum);
  // Ad-hoc correction terms for the first two cepstral coefficients.
  cepstrum[0] -= 12.f;
  cepstrum[1] -= 4.f;
  // Push the cepstrum (ring_buffer.h) and the squared distances to the 7
  // previous ones (UpdateCepstralDifferenceStats, symmetric_matrix_buffer.h).
  std::memcpy(cepstral_coeffs_ring_buf_.data() + kNumBands * ring_tail_, cepstrum.data(), sizeof(cepstrum));
  if (++ring_tail_ == kCepstralCoeffsHistorySize) ring_tail_ = 0;
  constexpr int S = kCepstralCoeffsHistorySize;
  std::array<float, S - 1> distances;
  for (int i = 0; i < S - 1; ++i) {
    auto old_cepstral_coeffs = Cepstrum(i + 1);
    distances[static_cast<size_t>(i)] = 0.f;
    for (int k = 0; k < kNumBands; ++k) {
      const float c = cepstrum[static_cast<size_t>(k)] - old_cepstral_coeffs[k];
      distances[static_cast<size_t>(i)] += c * c;
    }
  }
  std::memmove(cepstral_diffs_buf_.data(), cepstral_diffs_buf_.data() + S,
               (cepstral_diffs_buf_.size() - S) * sizeof(float));
  for (int i = 0; i < S - 1; ++i) {
    cepstral_diffs_buf_[static_cast<size_t>((S - 1 - i) * (S - 1) - 1)] = distances[static_cast<size_t>(i)];
  }
  // Write the features.
  std::copy(cepstrum.begin() + kNumLowerBands, cepstrum.end(), higher_bands_cepstrum.begin());
  ComputeAvgAndDerivatives(average, first_derivative, second_derivative);
  ComputeNormalizedCepstralCorrelation(bands_cross_corr);
  *variability = ComputeVariability();
  return false;
}

void SpectralFeaturesExtractor::ComputeAvgAndDerivatives(std::span<float, kNumLowerBands> average,
                                                         std::span<float, kNumLowerBands> first_derivative,
                                                         std::span<float, kNumLowerBands> second_derivative) const {
  auto curr = Cepstrum(0);
  auto prev1 = Cepstrum(1);
  auto prev2 = Cepstrum(2);
  for (size_t i = 0; i < average.size(); ++i) {
    average[i] = curr[i] + prev1[i] + prev2[i];
    first_derivative[i] = curr[i] - prev2[i];
    second_derivative[i] = curr[i] - 2 * prev1[i] + prev2[i];
  }
}

void SpectralFeaturesExtractor::ComputeNormalizedCepstralCorrelation(
    std::span<float, kNumLowerBands> bands_cross_corr) {
  spectral_correlator_.ComputeCrossCorrelation(reference_frame_fft_, lagged_frame_fft_, bands_cross_corr_);
  for (size_t i = 0; i < bands_cross_corr_.size(); ++i) {
    bands_cross_corr_[i] =
        bands_cross_corr_[i] / std::sqrt(0.001f + reference_frame_bands_energy_[i] * lagged_frame_bands_energy_[i]);
  }
  ComputeDct(bands_cross_corr_, dct_table_, bands_cross_corr);
  bands_cross_corr[0] -= 1.3f;
  bands_cross_corr[1] -= 0.9f;
}

float SpectralFeaturesExtractor::ComputeVariability() const {
  float variability = 0.f;
  for (int delay1 = 0; delay1 < kCepstralCoeffsHistorySize; ++delay1) {
    float min_dist = std::numeric_limits<float>::max();
    for (int delay2 = 0; delay2 < kCepstralCoeffsHistorySize; ++delay2) {
      if (delay1 == delay2) continue;  // The distance would be 0.
      min_dist = std::min(min_dist, CepstralDistance(delay1, delay2));
    }
    variability += min_dist;
  }
  return variability / kCepstralCoeffsHistorySize - 2.1f;
}

// features_extraction.cc. libwebrtc's high-pass filter in front is off
// (`use_high_pass_filter_` false), so it is left out.

FeaturesExtractor::FeaturesExtractor() : pitch_buf_24kHz_(kBufSize24kHz), lp_residual_(kBufSize24kHz) { Reset(); }

void FeaturesExtractor::Reset() {
  std::fill(pitch_buf_24kHz_.begin(), pitch_buf_24kHz_.end(), 0.f);
  spectral_features_extractor_.Reset();
}

bool FeaturesExtractor::CheckSilenceComputeFeatures(std::span<const float, kFrameSize10ms24kHz> samples,
                                                    std::span<float, kFeatureVectorSize> feature_vector) {
  // Feed the pitch buffer (sequence_buffer.h: shift left, append).
  std::memmove(pitch_buf_24kHz_.data(), pitch_buf_24kHz_.data() + kFrameSize10ms24kHz,
               (kBufSize24kHz - kFrameSize10ms24kHz) * sizeof(float));
  std::memcpy(pitch_buf_24kHz_.data() + kBufSize24kHz - kFrameSize10ms24kHz, samples.data(),
              kFrameSize10ms24kHz * sizeof(float));
  const std::span<const float, kBufSize24kHz> pitch_buf(pitch_buf_24kHz_.data(), kBufSize24kHz);
  // The LP residual.
  float lpc_coeffs[kNumLpcCoefficients];
  ComputeAndPostProcessLpcCoefficients(pitch_buf, lpc_coeffs);
  ComputeLpResidual(lpc_coeffs, pitch_buf, lp_residual_);
  // The pitch on the LP residual, normalized by the training data stats.
  pitch_period_48kHz_ = pitch_estimator_.Estimate(std::span<const float, kBufSize24kHz>(lp_residual_.data(), kBufSize24kHz));
  feature_vector[kFeatureVectorSize - 2] = 0.01f * static_cast<float>(pitch_period_48kHz_ - 300);
  // The frame lagged by the pitch period.
  assert(pitch_period_48kHz_ / 2 <= kMaxPitch24kHz);
  auto lagged_frame = pitch_buf.subspan(static_cast<size_t>(kMaxPitch24kHz - pitch_period_48kHz_ / 2),
                                        kFrameSize20ms24kHz);
  const auto reference_frame = pitch_buf.last<kFrameSize20ms24kHz>();
  return spectral_features_extractor_.CheckSilenceComputeFeatures(
      reference_frame, lagged_frame.first<kFrameSize20ms24kHz>(),
      feature_vector.subspan<kNumLowerBands, kNumBands - kNumLowerBands>(), feature_vector.first<kNumLowerBands>(),
      feature_vector.subspan<kNumBands, kNumLowerBands>(),
      feature_vector.subspan<kNumBands + kNumLowerBands, kNumLowerBands>(),
      feature_vector.subspan<kNumBands + 2 * kNumLowerBands, kNumLowerBands>(),
      &feature_vector[kFeatureVectorSize - 1]);
}

// rnn_fc.cc.

namespace {

std::vector<float> GetScaledParams(std::span<const int8_t> params) {
  std::vector<float> scaled_params(params.size());
  std::transform(params.begin(), params.end(), scaled_params.begin(),
                 [](int8_t x) -> float { return rnnoise::kWeightsScale * static_cast<float>(x); });
  return scaled_params;
}

// Casts, scales and transposes `weights` to [output][input].
std::vector<float> PreprocessWeights(std::span<const int8_t> weights, int output_size) {
  if (output_size == 1) return GetScaledParams(weights);
  const int input_size = static_cast<int>(weights.size()) / output_size;
  std::vector<float> w(weights.size());
  for (int o = 0; o < output_size; ++o) {
    for (int i = 0; i < input_size; ++i) {
      w[static_cast<size_t>(o * input_size + i)] =
          rnnoise::kWeightsScale * static_cast<float>(weights[static_cast<size_t>(i * output_size + o)]);
    }
  }
  return w;
}

}  // namespace

FullyConnectedLayer::FullyConnectedLayer(int input_size, int output_size, std::span<const int8_t> bias,
                                         std::span<const int8_t> weights, ActivationFunction activation_function)
    : input_size_(input_size),
      output_size_(output_size),
      bias_(GetScaledParams(bias)),
      weights_(PreprocessWeights(weights, output_size)),
      activation_function_(activation_function) {
  assert(output_size_ <= kFullyConnectedLayerMaxUnits);
  assert(bias_.size() == static_cast<size_t>(output_size_));
  assert(weights_.size() == static_cast<size_t>(input_size_ * output_size_));
}

void FullyConnectedLayer::ComputeOutput(std::span<const float> input) {
  assert(input.size() == static_cast<size_t>(input_size_));
  std::span<const float> weights(weights_);
  for (int o = 0; o < output_size_; ++o) {
    const float x = bias_[static_cast<size_t>(o)] +
                    DotProduct(input, weights.subspan(static_cast<size_t>(o * input_size_), static_cast<size_t>(input_size_)));
    output_[static_cast<size_t>(o)] = activation_function_ == ActivationFunction::kTansigApproximated
                                          ? rnnoise::TansigApproximated(x)
                                          : rnnoise::SigmoidApproximated(x);
  }
}

// rnn_gru.cc.

namespace {

constexpr int kNumGruGates = 3;  // Update, reset, output.

// Transposes, casts and scales a [n][gate][output] tensor to
// [gate][output][n].
std::vector<float> PreprocessGruTensor(std::span<const int8_t> tensor_src, int output_size) {
  const int n = static_cast<int>(tensor_src.size()) / (output_size * kNumGruGates);
  const int stride_src = kNumGruGates * output_size;
  const int stride_dst = n * output_size;
  std::vector<float> tensor_dst(tensor_src.size());
  for (int g = 0; g < kNumGruGates; ++g) {
    for (int o = 0; o < output_size; ++o) {
      for (int i = 0; i < n; ++i) {
        tensor_dst[static_cast<size_t>(g * stride_dst + o * n + i)] =
            rnnoise::kWeightsScale * static_cast<float>(tensor_src[static_cast<size_t>(i * stride_src + g * output_size + o)]);
      }
    }
  }
  return tensor_dst;
}

// The update or reset gate: g = sigmoid(W^T i + R^T s + b).
void ComputeUpdateResetGate(int input_size, int output_size, std::span<const float> input,
                            std::span<const float> state, std::span<const float> bias,
                            std::span<const float> weights, std::span<const float> recurrent_weights,
                            std::span<float> gate) {
  for (int o = 0; o < output_size; ++o) {
    float x = bias[static_cast<size_t>(o)];
    x += DotProduct(input, weights.subspan(static_cast<size_t>(o * input_size), static_cast<size_t>(input_size)));
    x += DotProduct(state, recurrent_weights.subspan(static_cast<size_t>(o * output_size), static_cast<size_t>(output_size)));
    gate[static_cast<size_t>(o)] = rnnoise::SigmoidApproximated(x);
  }
}

// The state gate: s' = u .* s + (1 - u) .* ReLU(W^T i + R^T (s .* r) + b).
void ComputeStateGate(int input_size, int output_size, std::span<const float> input, std::span<const float> update,
                      std::span<const float> reset, std::span<const float> bias, std::span<const float> weights,
                      std::span<const float> recurrent_weights, std::span<float> state) {
  std::array<float, kGruLayerMaxUnits> reset_x_state;
  for (int o = 0; o < output_size; ++o) {
    reset_x_state[static_cast<size_t>(o)] = state[static_cast<size_t>(o)] * reset[static_cast<size_t>(o)];
  }
  for (int o = 0; o < output_size; ++o) {
    float x = bias[static_cast<size_t>(o)];
    x += DotProduct(input, weights.subspan(static_cast<size_t>(o * input_size), static_cast<size_t>(input_size)));
    x += DotProduct({reset_x_state.data(), static_cast<size_t>(output_size)},
                    recurrent_weights.subspan(static_cast<size_t>(o * output_size), static_cast<size_t>(output_size)));
    state[static_cast<size_t>(o)] =
        update[static_cast<size_t>(o)] * state[static_cast<size_t>(o)] + (1.f - update[static_cast<size_t>(o)]) * std::max(0.f, x);
  }
}

}  // namespace

GatedRecurrentLayer::GatedRecurrentLayer(int input_size, int output_size, std::span<const int8_t> bias,
                                         std::span<const int8_t> weights, std::span<const int8_t> recurrent_weights)
    : input_size_(input_size),
      output_size_(output_size),
      bias_(PreprocessGruTensor(bias, output_size)),
      weights_(PreprocessGruTensor(weights, output_size)),
      recurrent_weights_(PreprocessGruTensor(recurrent_weights, output_size)) {
  assert(output_size_ <= kGruLayerMaxUnits);
  assert(bias_.size() == static_cast<size_t>(kNumGruGates * output_size_));
  assert(weights_.size() == static_cast<size_t>(kNumGruGates * input_size_ * output_size_));
  assert(recurrent_weights_.size() == static_cast<size_t>(kNumGruGates * output_size_ * output_size_));
  Reset();
}

void GatedRecurrentLayer::ComputeOutput(std::span<const float> input) {
  assert(input.size() == static_cast<size_t>(input_size_));
  // [update, reset, state] gates, flattened one after the other.
  std::span<const float> bias(bias_);
  std::span<const float> weights(weights_);
  std::span<const float> recurrent_weights(recurrent_weights_);
  const size_t out = static_cast<size_t>(output_size_);
  const size_t stride_weights = static_cast<size_t>(input_size_ * output_size_);
  const size_t stride_recurrent_weights = static_cast<size_t>(output_size_ * output_size_);
  std::span<float> state(state_.data(), out);
  std::array<float, kGruLayerMaxUnits> update;
  ComputeUpdateResetGate(input_size_, output_size_, input, state, bias.subspan(0, out),
                         weights.subspan(0, stride_weights), recurrent_weights.subspan(0, stride_recurrent_weights),
                         update);
  std::array<float, kGruLayerMaxUnits> reset;
  ComputeUpdateResetGate(input_size_, output_size_, input, state, bias.subspan(out, out),
                         weights.subspan(stride_weights, stride_weights),
                         recurrent_weights.subspan(stride_recurrent_weights, stride_recurrent_weights), reset);
  ComputeStateGate(input_size_, output_size_, input, update, reset, bias.subspan(2 * out, out),
                   weights.subspan(2 * stride_weights, stride_weights),
                   recurrent_weights.subspan(2 * stride_recurrent_weights, stride_recurrent_weights), state);
}

// rnn.cc.

static_assert(kFeatureVectorSize == static_cast<int>(rnnoise::kInputLayerInputSize));
static_assert(rnnoise::kInputLayerOutputSize <= kFullyConnectedLayerMaxUnits);
static_assert(rnnoise::kHiddenLayerOutputSize <= kGruLayerMaxUnits);
static_assert(rnnoise::kOutputLayerOutputSize <= kFullyConnectedLayerMaxUnits);

RnnVad::RnnVad()
    : input_(static_cast<int>(rnnoise::kInputLayerInputSize), static_cast<int>(rnnoise::kInputLayerOutputSize),
             rnnoise::kInputDenseBias, rnnoise::kInputDenseWeights, ActivationFunction::kTansigApproximated),
      hidden_(static_cast<int>(rnnoise::kInputLayerOutputSize), static_cast<int>(rnnoise::kHiddenLayerOutputSize),
              rnnoise::kHiddenGruBias, rnnoise::kHiddenGruWeights, rnnoise::kHiddenGruRecurrentWeights),
      output_(static_cast<int>(rnnoise::kHiddenLayerOutputSize), static_cast<int>(rnnoise::kOutputLayerOutputSize),
              rnnoise::kOutputDenseBias, rnnoise::kOutputDenseWeights, ActivationFunction::kSigmoidApproximated) {
  assert(input_.size() == hidden_.input_size());
  assert(hidden_.size() == output_.input_size());
}

float RnnVad::ComputeVadProbability(std::span<const float, kFeatureVectorSize> feature_vector, bool is_silence) {
  if (is_silence) {
    Reset();
    return 0.f;
  }
  input_.ComputeOutput(feature_vector);
  hidden_.ComputeOutput(input_.output());
  output_.ComputeOutput(hidden_.output());
  return output_.output()[0];
}

}  // namespace rnn_vad

// vad_wrapper.cc.

namespace {

constexpr int kNumFramesPerSecond = 100;
constexpr int kFrameDurationMs = 10;

class MonoVadImpl : public VoiceActivityDetectorWrapper::MonoVad {
 public:
  int SampleRateHz() const override { return rnn_vad::kSampleRate24kHz; }
  void Reset() override { rnn_vad_.Reset(); }
  float Analyze(std::span<const float> frame) override {
    assert(frame.size() == static_cast<size_t>(rnn_vad::kFrameSize10ms24kHz));
    std::array<float, rnn_vad::kFeatureVectorSize> feature_vector;
    const bool is_silence = features_extractor_.CheckSilenceComputeFeatures(
        frame.first<rnn_vad::kFrameSize10ms24kHz>(), feature_vector);
    return rnn_vad_.ComputeVadProbability(feature_vector, is_silence);
  }

 private:
  rnn_vad::FeaturesExtractor features_extractor_;
  rnn_vad::RnnVad rnn_vad_;
};

}  // namespace

VoiceActivityDetectorWrapper::VoiceActivityDetectorWrapper(int sample_rate_hz, int vad_reset_period_ms)
    : VoiceActivityDetectorWrapper(vad_reset_period_ms, std::make_unique<MonoVadImpl>(), sample_rate_hz) {}

VoiceActivityDetectorWrapper::VoiceActivityDetectorWrapper(int vad_reset_period_ms, std::unique_ptr<MonoVad> vad,
                                                           int sample_rate_hz)
    : vad_reset_period_frames_(vad_reset_period_ms / kFrameDurationMs),
      frame_size_(sample_rate_hz / kNumFramesPerSecond),
      time_to_vad_reset_(vad_reset_period_frames_),
      vad_(std::move(vad)),
      resampled_buffer_(static_cast<size_t>(vad_->SampleRateHz() / kNumFramesPerSecond)) {
  assert(vad_reset_period_frames_ > 1);
  if (resampled_buffer_.size() != static_cast<size_t>(frame_size_)) {
    resampler_ = std::make_unique<PushSincResampler>(static_cast<size_t>(frame_size_), resampled_buffer_.size());
  }
  vad_->Reset();
}

float VoiceActivityDetectorWrapper::Analyze(std::span<const float> frame) {
  // Periodically reset the VAD.
  time_to_vad_reset_--;
  if (time_to_vad_reset_ <= 0) {
    vad_->Reset();
    time_to_vad_reset_ = vad_reset_period_frames_;
  }
  // Resample the frame (PushResampler copies when the rates match).
  assert(frame.size() == static_cast<size_t>(frame_size_));
  if (resampler_) {
    resampler_->Resample(frame, resampled_buffer_);
  } else {
    std::copy(frame.begin(), frame.end(), resampled_buffer_.begin());
  }
  return vad_->Analyze(resampled_buffer_);
}

}  // namespace sm::audio::agc2
