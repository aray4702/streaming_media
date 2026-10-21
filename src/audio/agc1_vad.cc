// Ported from libwebrtc M157 (BSD license, see compare/webrtc/reference/):
// common_audio/vad/{webrtc_vad,vad_core,vad_filterbank,vad_gmm,vad_sp}.c,
// common_audio/signal_processing/{energy,get_scaling_square,
// division_operations}.c and include/spl_inl.h (the helpers used),
// modules/audio_processing/vad/{standalone_vad,pole_zero_filter,
// vad_circular_buffer,gmm,pitch_internal,pitch_based_vad,vad_audio_proc,
// voice_activity_detector}.cc with noise_gmm_tables.h, voice_gmm_tables.h
// and vad_audio_proc_internal.h, and
// modules/audio_processing/agc/{loudness_histogram,utility,agc}.cc.
// The iSAC pitch analysis is in agc1_vad_isac.cc; fft4g is fft4g.cc.
//
// The fixed-point code keeps libwebrtc's arithmetic; where the C relies on
// signed overflow wrapping (it runs those under RTC_NO_SANITIZE), the sums
// and products go through uint32_t, which gives the same bits.
#include "sm/audio/agc1_vad.h"

#include "sm/audio/fft4g.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <numbers>

namespace sm::audio::agc1 {

namespace detail {

namespace {

// ---- common_audio/signal_processing ---------------------------------------

int16_t NormW32(int32_t a) {
  return static_cast<int16_t>(a == 0 ? 0 : std::countl_zero(static_cast<uint32_t>(a < 0 ? ~a : a)) - 1);
}
int16_t NormU32(uint32_t a) { return static_cast<int16_t>(a == 0 ? 0 : std::countl_zero(a)); }
int16_t GetSizeInBits(uint32_t n) { return static_cast<int16_t>(32 - std::countl_zero(n)); }
int32_t DivW32W16(int32_t num, int16_t den) { return den != 0 ? num / den : 0x7FFFFFFF; }
// a * b and a + b as the C computes them when they overflow (wrapping).
int32_t MulWrap(int32_t a, int32_t b) {
  return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
}
int32_t AddWrap(int32_t a, int32_t b) {
  return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

// get_scaling_square.c.
int16_t GetScalingSquare(const int16_t* in_vector, size_t in_vector_length, size_t times) {
  const int16_t nbits = GetSizeInBits(static_cast<uint32_t>(times));
  int16_t smax = -1;
  const int16_t* sptr = in_vector;
  for (size_t i = in_vector_length; i > 0; i--) {
    const int16_t sabs = static_cast<int16_t>(*sptr > 0 ? *sptr : -*sptr);
    ++sptr;
    smax = (sabs > smax ? sabs : smax);
  }
  const int16_t t = NormW32(static_cast<int32_t>(smax) * static_cast<int32_t>(smax));
  if (smax == 0) {
    return 0;  // Since norm(0) returns 0
  }
  return static_cast<int16_t>((t > nbits) ? 0 : nbits - t);
}

// energy.c.
int32_t Energy(const int16_t* vector, size_t vector_length, int* scale_factor) {
  uint32_t en = 0;
  const int scaling = GetScalingSquare(vector, vector_length, vector_length);
  for (size_t i = 0; i < vector_length; i++) {
    en += static_cast<uint32_t>((vector[i] * vector[i]) >> scaling);
  }
  *scale_factor = scaling;
  return static_cast<int32_t>(en);
}

// ---- vad_filterbank.c ------------------------------------------------------

constexpr int16_t kLogConst = 24660;          // 160*log10(2) in Q9.
constexpr int16_t kLogEnergyIntPart = 14336;  // 14 in Q10
constexpr int16_t kHpZeroCoefs[3] = {6631, -13262, 6631};  // Q14.
constexpr int16_t kHpPoleCoefs[3] = {16384, -7756, 5620};  // Q14.
constexpr int16_t kAllPassCoefsQ15[2] = {20972, 5571};      // Upper 0.64, lower 0.17.
constexpr int16_t kOffsetVector[6] = {368, 368, 272, 176, 176, 176};

// High pass filtering, with a cut-off frequency at 80 Hz, if `data_in` is
// sampled at 500 Hz.
void HighPassFilter(const int16_t* data_in, size_t data_length, int16_t* filter_state, int16_t* data_out) {
  const int16_t* in_ptr = data_in;
  int16_t* out_ptr = data_out;
  for (size_t i = 0; i < data_length; i++) {
    // All-zero section (filter coefficients in Q14).
    int32_t tmp32 = kHpZeroCoefs[0] * *in_ptr;
    tmp32 += kHpZeroCoefs[1] * filter_state[0];
    tmp32 += kHpZeroCoefs[2] * filter_state[1];
    filter_state[1] = filter_state[0];
    filter_state[0] = *in_ptr++;
    // All-pole section (filter coefficients in Q14).
    tmp32 -= kHpPoleCoefs[1] * filter_state[2];
    tmp32 -= kHpPoleCoefs[2] * filter_state[3];
    filter_state[3] = filter_state[2];
    filter_state[2] = static_cast<int16_t>(tmp32 >> 14);
    *out_ptr++ = filter_state[2];
  }
}

// All pass filtering of every other sample of `data_in` (Q0) into
// `data_out` (Q(-1)); the state is in Q(-1). Wraps like the C, which runs
// with RTC_NO_SANITIZE("signed-integer-overflow").
void AllPassFilter(const int16_t* data_in, size_t data_length, int16_t filter_coefficient, int16_t* filter_state,
                   int16_t* data_out) {
  int32_t state32 = static_cast<int32_t>(*filter_state) * (1 << 16);  // Q15
  for (size_t i = 0; i < data_length; i++) {
    const int32_t tmp32 = AddWrap(state32, filter_coefficient * *data_in);
    const auto tmp16 = static_cast<int16_t>(tmp32 >> 16);  // Q(-1)
    *data_out++ = tmp16;
    state32 = (*data_in * (1 << 14)) - filter_coefficient * tmp16;  // Q14
    state32 = MulWrap(state32, 2);                                  // Q15.
    data_in += 2;
  }
  *filter_state = static_cast<int16_t>(state32 >> 16);  // Q(-1)
}

// Splits `data_in` into a high pass and a low pass half, each downsampled
// by 2.
void SplitFilter(const int16_t* data_in, size_t data_length, int16_t* upper_state, int16_t* lower_state,
                 int16_t* hp_data_out, int16_t* lp_data_out) {
  const size_t half_length = data_length >> 1;  // Downsampling by 2.
  AllPassFilter(&data_in[0], half_length, kAllPassCoefsQ15[0], upper_state, hp_data_out);
  AllPassFilter(&data_in[1], half_length, kAllPassCoefsQ15[1], lower_state, lp_data_out);
  for (size_t i = 0; i < half_length; i++) {
    const int16_t tmp_out = *hp_data_out;
    *hp_data_out = static_cast<int16_t>(*hp_data_out - *lp_data_out);
    ++hp_data_out;
    *lp_data_out = static_cast<int16_t>(*lp_data_out + tmp_out);
    ++lp_data_out;
  }
}

// 10 * log10(energy of `data_in`) in Q4 plus `offset`; updates
// `total_energy` while it is at most kMinEnergy.
void LogOfEnergy(const int16_t* data_in, size_t data_length, int16_t offset, int16_t* total_energy,
                 int16_t* log_energy) {
  int tot_rshifts = 0;
  uint32_t energy = static_cast<uint32_t>(Energy(data_in, data_length, &tot_rshifts));
  if (energy != 0) {
    // Normalizing to 15 bits is 17 leading zeros of a 32 bit value.
    const int normalizing_rshifts = 17 - NormU32(energy);
    int16_t log2_energy = kLogEnergyIntPart;
    tot_rshifts += normalizing_rshifts;
    if (normalizing_rshifts < 0) {
      energy <<= -normalizing_rshifts;
    } else {
      energy >>= normalizing_rshifts;
    }
    // log2(energy) in Q10 ~= (14 << 10) + (frac_Q15 >> 4).
    log2_energy = static_cast<int16_t>(log2_energy + static_cast<int16_t>((energy & 0x00003FFF) >> 4));
    *log_energy = static_cast<int16_t>(((kLogConst * log2_energy) >> 19) + ((tot_rshifts * kLogConst) >> 9));
    if (*log_energy < 0) {
      *log_energy = 0;
    }
  } else {
    *log_energy = offset;
    return;
  }

  *log_energy = static_cast<int16_t>(*log_energy + offset);

  if (*total_energy <= kVadMinEnergy) {
    if (tot_rshifts >= 0) {
      *total_energy = static_cast<int16_t>(*total_energy + kVadMinEnergy + 1);
    } else {
      *total_energy = static_cast<int16_t>(*total_energy + static_cast<int16_t>(energy >> -tot_rshifts));  // Q0.
    }
  }
}

// ---- vad_sp.c ---------------------------------------------------------------

constexpr int16_t kAllPassCoefsQ13[2] = {5243, 1392};  // Q13, upper 0.64, lower 0.17.
constexpr int16_t kSmoothingDown = 6553;               // 0.2 in Q15.
constexpr int16_t kSmoothingUp = 32439;                // 0.99 in Q15.

// ---- vad_gmm.c ------------------------------------------------------------

constexpr int32_t kCompVar = 22005;
constexpr int16_t kLog2Exp = 5909;  // log2(exp(1)) in Q12.

// ---- vad_core.c -------------------------------------------------------------

constexpr int kNumChannels = kVadNumChannels;
constexpr int kNumGaussians = kVadNumGaussians;
constexpr int kTableSize = kVadTableSize;

constexpr int16_t kSpectrumWeight[kNumChannels] = {6, 8, 10, 12, 14, 16};
constexpr int16_t kNoiseUpdateConst = 655;    // Q15
constexpr int16_t kSpeechUpdateConst = 6554;  // Q15
constexpr int16_t kBackEta = 154;             // Q8
// Minimum difference between the two models, Q5
constexpr int16_t kMinimumDifference[kNumChannels] = {544, 544, 576, 576, 576, 576};
// Upper limit of mean value for speech model, Q7
constexpr int16_t kMaximumSpeech[kNumChannels] = {11392, 11392, 11520, 11520, 11520, 11520};
// Minimum value for mean value
constexpr int16_t kMinimumMean[kNumGaussians] = {640, 768};
// Upper limit of mean value for noise model, Q7
constexpr int16_t kMaximumNoise[kNumChannels] = {9216, 9088, 8960, 8832, 8704, 8576};
// Start values for the Gaussian models, Q7.
constexpr int16_t kNoiseDataWeights[kTableSize] = {34, 62, 72, 66, 53, 25, 94, 66, 56, 62, 75, 103};
constexpr int16_t kSpeechDataWeights[kTableSize] = {48, 82, 45, 87, 50, 47, 80, 46, 83, 41, 78, 81};
constexpr int16_t kNoiseDataMeans[kTableSize] = {6738, 4892, 7065, 6715, 6771, 3369,
                                                 7646, 3863, 7820, 7266, 5020, 4362};
constexpr int16_t kSpeechDataMeans[kTableSize] = {8306, 10085, 10078, 11823, 11843, 6309,
                                                  9473, 9571,  10879, 7581,  8180,  7483};
constexpr int16_t kNoiseDataStds[kTableSize] = {378, 1064, 493, 582, 688, 593, 474, 697, 475, 688, 421, 455};
constexpr int16_t kSpeechDataStds[kTableSize] = {555, 505, 567, 524, 585, 1231, 509, 828, 492, 1540, 1079, 850};

// Maximum number of counted speech (VAD = 1) frames in a row.
constexpr int16_t kMaxSpeechFrames = 6;
// Minimum standard deviation for both speech and noise.
constexpr int16_t kMinStd = 384;
constexpr short kDefaultMode = 0;
constexpr int kInitCheck = 42;

// Thresholds per mode for 10, 20 and 30 ms frames.
constexpr int16_t kOverHangMax1Q[3] = {8, 4, 3};
constexpr int16_t kOverHangMax2Q[3] = {14, 7, 5};
constexpr int16_t kLocalThresholdQ[3] = {24, 21, 24};
constexpr int16_t kGlobalThresholdQ[3] = {57, 48, 57};
constexpr int16_t kOverHangMax1LBR[3] = {8, 4, 3};
constexpr int16_t kOverHangMax2LBR[3] = {14, 7, 5};
constexpr int16_t kLocalThresholdLBR[3] = {37, 32, 37};
constexpr int16_t kGlobalThresholdLBR[3] = {100, 80, 100};
constexpr int16_t kOverHangMax1AGG[3] = {6, 3, 2};
constexpr int16_t kOverHangMax2AGG[3] = {9, 5, 3};
constexpr int16_t kLocalThresholdAGG[3] = {82, 78, 82};
constexpr int16_t kGlobalThresholdAGG[3] = {285, 260, 285};
constexpr int16_t kOverHangMax1VAG[3] = {6, 3, 2};
constexpr int16_t kOverHangMax2VAG[3] = {9, 5, 3};
constexpr int16_t kLocalThresholdVAG[3] = {94, 94, 94};
constexpr int16_t kGlobalThresholdVAG[3] = {1100, 1050, 1100};

// The weighted average over the Gaussians of `data`, after adding `offset`.
int32_t WeightedAverage(int16_t* data, int16_t offset, const int16_t* weights) {
  int32_t weighted_average = 0;
  for (int k = 0; k < kNumGaussians; k++) {
    data[k * kNumChannels] = static_cast<int16_t>(data[k * kNumChannels] + offset);
    weighted_average += data[k * kNumChannels] * weights[k * kNumChannels];
  }
  return weighted_average;
}

// The GMM likelihood ratio test (H0 noise, H1 speech) per band and over all
// bands, then the model update. Returns the VAD decision with hangover.
int16_t GmmProbability(VadInst* self, int16_t* features, int16_t total_power, size_t frame_length) {
  int16_t vadflag = 0;
  int16_t deltaN[kTableSize], deltaS[kTableSize];
  int16_t ngprvec[kTableSize] = {0};  // Conditional probability = 0.
  int16_t sgprvec[kTableSize] = {0};  // Conditional probability = 0.
  int32_t sum_log_likelihood_ratios = 0;
  int32_t noise_probability[kNumGaussians], speech_probability[kNumGaussians];
  int16_t overhead1, overhead2, individualTest, totalTest;

  // Set various thresholds based on frame lengths (80, 160 or 240 samples).
  if (frame_length == 80) {
    overhead1 = self->over_hang_max_1[0];
    overhead2 = self->over_hang_max_2[0];
    individualTest = self->individual[0];
    totalTest = self->total[0];
  } else if (frame_length == 160) {
    overhead1 = self->over_hang_max_1[1];
    overhead2 = self->over_hang_max_2[1];
    individualTest = self->individual[1];
    totalTest = self->total[1];
  } else {
    overhead1 = self->over_hang_max_1[2];
    overhead2 = self->over_hang_max_2[2];
    individualTest = self->individual[2];
    totalTest = self->total[2];
  }

  if (total_power > kVadMinEnergy) {
    for (int channel = 0; channel < kNumChannels; channel++) {
      int32_t h0_test = 0;
      int32_t h1_test = 0;
      for (int k = 0; k < kNumGaussians; k++) {
        const int gaussian = channel + k * kNumChannels;
        // Probability under H0 (noise), Q27 = Q7 * Q20.
        int32_t tmp1_s32 = VadGaussianProbability(features[channel], self->noise_means[gaussian],
                                                  self->noise_stds[gaussian], &deltaN[gaussian]);
        noise_probability[k] = kNoiseDataWeights[gaussian] * tmp1_s32;
        h0_test += noise_probability[k];  // Q27
        // Probability under H1 (speech), Q27.
        tmp1_s32 = VadGaussianProbability(features[channel], self->speech_means[gaussian],
                                          self->speech_stds[gaussian], &deltaS[gaussian]);
        speech_probability[k] = kSpeechDataWeights[gaussian] * tmp1_s32;
        h1_test += speech_probability[k];  // Q27
      }

      // log2(Pr{X|H1} / Pr{X|H0}) ~= shifts_h0 - shifts_h1.
      int16_t shifts_h0 = NormW32(h0_test);
      int16_t shifts_h1 = NormW32(h1_test);
      if (h0_test == 0) {
        shifts_h0 = 31;
      }
      if (h1_test == 0) {
        shifts_h1 = 31;
      }
      const auto log_likelihood_ratio = static_cast<int16_t>(shifts_h0 - shifts_h1);

      sum_log_likelihood_ratios += static_cast<int32_t>(log_likelihood_ratio * kSpectrumWeight[channel]);

      // Local VAD decision.
      if ((log_likelihood_ratio * 4) > individualTest) {
        vadflag = 1;
      }

      // Local noise probabilities used later when updating the GMM.
      const auto h0 = static_cast<int16_t>(h0_test >> 12);  // Q15
      if (h0 > 0) {
        const auto tmp1_s32 =
            static_cast<int32_t>((static_cast<uint32_t>(noise_probability[0]) & 0xFFFFF000u) << 2);  // Q29
        ngprvec[channel] = static_cast<int16_t>(DivW32W16(tmp1_s32, h0));                          // Q14
        ngprvec[channel + kNumChannels] = static_cast<int16_t>(16384 - ngprvec[channel]);
      } else {
        ngprvec[channel] = 16384;
      }

      // Local speech probabilities used later when updating the GMM.
      const auto h1 = static_cast<int16_t>(h1_test >> 12);  // Q15
      if (h1 > 0) {
        const auto tmp1_s32 =
            static_cast<int32_t>((static_cast<uint32_t>(speech_probability[0]) & 0xFFFFF000u) << 2);  // Q29
        sgprvec[channel] = static_cast<int16_t>(DivW32W16(tmp1_s32, h1));                           // Q14
        sgprvec[channel + kNumChannels] = static_cast<int16_t>(16384 - sgprvec[channel]);
      }
    }

    // Make a global VAD decision.
    vadflag = static_cast<int16_t>(vadflag | (sum_log_likelihood_ratios >= totalTest));

    // Update the model parameters.
    int16_t maxspe = 12800;
    for (int channel = 0; channel < kNumChannels; channel++) {
      // Minimum value in past, for long term correction, Q4.
      const int16_t feature_minimum = VadFindMinimum(*self, features[channel], channel);

      // The "global" mean: the two means weighted.
      int32_t noise_global_mean = WeightedAverage(&self->noise_means[channel], 0, &kNoiseDataWeights[channel]);
      int16_t tmp1_s16 = static_cast<int16_t>(noise_global_mean >> 6);  // Q8

      for (int k = 0; k < kNumGaussians; k++) {
        const int gaussian = channel + k * kNumChannels;
        const int16_t nmk = self->noise_means[gaussian];
        const int16_t smk = self->speech_means[gaussian];
        int16_t nsk = self->noise_stds[gaussian];
        int16_t ssk = self->speech_stds[gaussian];

        // Update noise mean vector if the frame consists of noise only.
        int16_t nmk2 = nmk;
        if (!vadflag) {
          // (Q14 * Q11 >> 11) = Q14.
          const auto delt = static_cast<int16_t>((ngprvec[gaussian] * deltaN[gaussian]) >> 11);
          // Q7 + (Q14 * Q15 >> 22) = Q7.
          nmk2 = static_cast<int16_t>(nmk + static_cast<int16_t>((delt * kNoiseUpdateConst) >> 22));
        }

        // Long term correction of the noise mean. Q8 - Q8 = Q8.
        const auto ndelt = static_cast<int16_t>(feature_minimum * 16 - tmp1_s16);
        // Q7 + (Q8 * Q8) >> 9 = Q7.
        int16_t nmk3 = static_cast<int16_t>(nmk2 + static_cast<int16_t>((ndelt * kBackEta) >> 9));

        // Control that the noise mean does not drift to much.
        int16_t tmp_s16 = static_cast<int16_t>((k + 5) << 7);
        if (nmk3 < tmp_s16) {
          nmk3 = tmp_s16;
        }
        tmp_s16 = static_cast<int16_t>((72 + k - channel) << 7);
        if (nmk3 > tmp_s16) {
          nmk3 = tmp_s16;
        }
        self->noise_means[gaussian] = nmk3;

        if (vadflag) {
          // Update speech mean vector. (Q14 * Q11) >> 11 = Q14.
          const auto delt = static_cast<int16_t>((sgprvec[gaussian] * deltaS[gaussian]) >> 11);
          // Q14 * Q15 >> 21 = Q8.
          tmp_s16 = static_cast<int16_t>((delt * kSpeechUpdateConst) >> 21);
          // Q7 + (Q8 >> 1) = Q7. With rounding.
          int16_t smk2 = static_cast<int16_t>(smk + ((tmp_s16 + 1) >> 1));

          // Control that the speech mean does not drift to much.
          const auto maxmu = static_cast<int16_t>(maxspe + 640);
          if (smk2 < kMinimumMean[k]) {
            smk2 = kMinimumMean[k];
          }
          if (smk2 > maxmu) {
            smk2 = maxmu;
          }
          self->speech_means[gaussian] = smk2;  // Q7.

          // (Q7 >> 3) = Q4. With rounding.
          tmp_s16 = static_cast<int16_t>((smk + 4) >> 3);
          tmp_s16 = static_cast<int16_t>(features[channel] - tmp_s16);  // Q4
          // (Q11 * Q4 >> 3) = Q12.
          int32_t tmp1_s32 = (deltaS[gaussian] * tmp_s16) >> 3;
          int32_t tmp2_s32 = tmp1_s32 - 4096;
          tmp_s16 = static_cast<int16_t>(sgprvec[gaussian] >> 2);
          // (Q14 >> 2) * Q12 = Q24.
          tmp1_s32 = MulWrap(tmp_s16, tmp2_s32);
          tmp2_s32 = tmp1_s32 >> 4;  // Q20

          // 0.1 * Q20 / Q7 = Q13.
          if (tmp2_s32 > 0) {
            tmp_s16 = static_cast<int16_t>(DivW32W16(tmp2_s32, static_cast<int16_t>(ssk * 10)));
          } else {
            tmp_s16 = static_cast<int16_t>(DivW32W16(-tmp2_s32, static_cast<int16_t>(ssk * 10)));
            tmp_s16 = static_cast<int16_t>(-tmp_s16);
          }
          // Divide by 4 giving an update factor of 0.025; (Q13 >> 8) = Q7.
          tmp_s16 = static_cast<int16_t>(tmp_s16 + 128);  // Rounding.
          ssk = static_cast<int16_t>(ssk + (tmp_s16 >> 8));
          if (ssk < kMinStd) {
            ssk = kMinStd;
          }
          self->speech_stds[gaussian] = ssk;
        } else {
          // Update GMM variance vectors. Q4 - (Q7 >> 3) = Q4.
          tmp_s16 = static_cast<int16_t>(features[channel] - (nmk >> 3));
          // (Q11 * Q4 >> 3) = Q12.
          int32_t tmp1_s32 = (deltaN[gaussian] * tmp_s16) >> 3;
          tmp1_s32 -= 4096;

          // (Q14 >> 2) * Q12 = Q24.
          tmp_s16 = static_cast<int16_t>((ngprvec[gaussian] + 2) >> 2);
          const int32_t tmp2_s32 = MulWrap(tmp_s16, tmp1_s32);  // OverflowingMulS16ByS32ToS32
          // (Q24 >> 14) = (Q24 >> 4) / 2^10 = Q20.
          tmp1_s32 = tmp2_s32 >> 14;

          // Q20 / Q7 = Q13.
          if (tmp1_s32 > 0) {
            tmp_s16 = static_cast<int16_t>(DivW32W16(tmp1_s32, nsk));
          } else {
            tmp_s16 = static_cast<int16_t>(DivW32W16(-tmp1_s32, nsk));
            tmp_s16 = static_cast<int16_t>(-tmp_s16);
          }
          tmp_s16 = static_cast<int16_t>(tmp_s16 + 32);  // Rounding
          nsk = static_cast<int16_t>(nsk + (tmp_s16 >> 6));  // Q13 >> 6 = Q7.
          if (nsk < kMinStd) {
            nsk = kMinStd;
          }
          self->noise_stds[gaussian] = nsk;
        }
      }

      // Separate models if they are too close. Global means in Q14.
      noise_global_mean = WeightedAverage(&self->noise_means[channel], 0, &kNoiseDataWeights[channel]);
      int32_t speech_global_mean = WeightedAverage(&self->speech_means[channel], 0, &kSpeechDataWeights[channel]);

      // (Q14 >> 9) - (Q14 >> 9) = Q5.
      const auto diff =
          static_cast<int16_t>(static_cast<int16_t>(speech_global_mean >> 9) - static_cast<int16_t>(noise_global_mean >> 9));
      if (diff < kMinimumDifference[channel]) {
        const auto tmp_s16 = static_cast<int16_t>(kMinimumDifference[channel] - diff);
        // ~0.8 and ~0.2 of (kMinimumDifference - diff), Q7.
        tmp1_s16 = static_cast<int16_t>((13 * tmp_s16) >> 2);
        const auto tmp2_s16 = static_cast<int16_t>((3 * tmp_s16) >> 2);
        // Move the speech means by `tmp1_s16` and the noise means by
        // -`tmp2_s16`.
        speech_global_mean = WeightedAverage(&self->speech_means[channel], tmp1_s16, &kSpeechDataWeights[channel]);
        noise_global_mean = WeightedAverage(&self->noise_means[channel], static_cast<int16_t>(-tmp2_s16),
                                            &kNoiseDataWeights[channel]);
      }

      // Control that the speech & noise means do not drift to much.
      maxspe = kMaximumSpeech[channel];
      int16_t tmp2_s16 = static_cast<int16_t>(speech_global_mean >> 7);
      if (tmp2_s16 > maxspe) {
        // Upper limit of speech model.
        tmp2_s16 = static_cast<int16_t>(tmp2_s16 - maxspe);
        for (int k = 0; k < kNumGaussians; k++) {
          int16_t& m = self->speech_means[channel + k * kNumChannels];
          m = static_cast<int16_t>(m - tmp2_s16);
        }
      }

      tmp2_s16 = static_cast<int16_t>(noise_global_mean >> 7);
      if (tmp2_s16 > kMaximumNoise[channel]) {
        tmp2_s16 = static_cast<int16_t>(tmp2_s16 - kMaximumNoise[channel]);
        for (int k = 0; k < kNumGaussians; k++) {
          int16_t& m = self->noise_means[channel + k * kNumChannels];
          m = static_cast<int16_t>(m - tmp2_s16);
        }
      }
    }
    self->frame_counter++;
  }

  // Smooth with respect to transition hysteresis.
  if (!vadflag) {
    if (self->over_hang > 0) {
      vadflag = static_cast<int16_t>(2 + self->over_hang);
      self->over_hang--;
    }
    self->num_of_speech = 0;
  } else {
    self->num_of_speech++;
    if (self->num_of_speech > kMaxSpeechFrames) {
      self->num_of_speech = kMaxSpeechFrames;
      self->over_hang = overhead2;
    } else {
      self->over_hang = overhead1;
    }
  }
  return vadflag;
}

}  // namespace

int32_t VadGaussianProbability(int16_t input, int16_t mean, int16_t std, int16_t* delta) {
  int16_t exp_value = 0;
  // `inv_std` = 1 / s, in Q10; (`std` >> 1) for rounding.
  int32_t tmp32 = static_cast<int32_t>(131072) + static_cast<int32_t>(std >> 1);
  const auto inv_std = static_cast<int16_t>(DivW32W16(tmp32, std));

  // `inv_std2` = 1 / s^2, in Q14.
  int16_t tmp16 = static_cast<int16_t>(inv_std >> 2);  // Q10 -> Q8.
  const auto inv_std2 = static_cast<int16_t>((tmp16 * tmp16) >> 2);

  tmp16 = static_cast<int16_t>(input * 8);     // Q4 -> Q7
  tmp16 = static_cast<int16_t>(tmp16 - mean);  // Q7 - Q7 = Q7

  // `delta` = (x - m) / s^2, in Q11.
  *delta = static_cast<int16_t>((inv_std2 * tmp16) >> 10);

  // The exponent (x - m)^2 / (2 * s^2), in Q10.
  tmp32 = (*delta * tmp16) >> 9;

  if (tmp32 < kCompVar) {
    // `tmp16` = log2(exp(1)) * `tmp32`, in Q10.
    tmp16 = static_cast<int16_t>((kLog2Exp * tmp32) >> 12);
    tmp16 = static_cast<int16_t>(-tmp16);
    exp_value = static_cast<int16_t>(0x0400 | (tmp16 & 0x03FF));
    tmp16 = static_cast<int16_t>(tmp16 ^ 0xFFFF);
    tmp16 = static_cast<int16_t>(tmp16 >> 10);
    tmp16 = static_cast<int16_t>(tmp16 + 1);
    // `exp_value` = exp(-`tmp32`) in Q10.
    exp_value = static_cast<int16_t>(exp_value >> tmp16);
  }

  // (1 / s) * exp(-(x - m)^2 / (2 * s^2)), in Q20.
  return inv_std * exp_value;
}

void VadDownsampling(const int16_t* signal_in, int16_t* signal_out, int32_t* filter_state, size_t in_length) {
  int32_t tmp32_1 = filter_state[0];
  int32_t tmp32_2 = filter_state[1];
  const size_t half_length = (in_length >> 1);
  for (size_t n = 0; n < half_length; n++) {
    // All-pass filtering upper branch.
    const auto tmp16_1 = static_cast<int16_t>((tmp32_1 >> 1) + ((kAllPassCoefsQ13[0] * *signal_in) >> 14));
    *signal_out = tmp16_1;
    tmp32_1 = static_cast<int32_t>(*signal_in++) - ((kAllPassCoefsQ13[0] * tmp16_1) >> 12);

    // All-pass filtering lower branch.
    const auto tmp16_2 = static_cast<int16_t>((tmp32_2 >> 1) + ((kAllPassCoefsQ13[1] * *signal_in) >> 14));
    *signal_out = static_cast<int16_t>(*signal_out + tmp16_2);
    ++signal_out;
    tmp32_2 = static_cast<int32_t>(*signal_in++) - ((kAllPassCoefsQ13[1] * tmp16_2) >> 12);
  }
  filter_state[0] = tmp32_1;
  filter_state[1] = tmp32_2;
}

int16_t VadFindMinimum(VadInst& self, int16_t feature_value, int channel) {
  int position = -1;
  // Offset to beginning of the 16 minimum values in memory.
  const int offset = (channel << 4);
  int16_t current_median = 1600;
  int16_t alpha = 0;
  int16_t* age = &self.index_vector[offset];
  int16_t* smallest_values = &self.low_value_vector[offset];

  // Each value in `smallest_values` is getting 1 loop older. Update `age`,
  // and remove old values.
  for (int i = 0; i < 16; i++) {
    if (age[i] != 100) {
      age[i]++;
    } else {
      // Too old value. Remove from memory and shift larger values downwards.
      for (int j = i; j < 15; j++) {
        smallest_values[j] = smallest_values[j + 1];
        age[j] = age[j + 1];
      }
      age[15] = 101;
      smallest_values[15] = 10000;
    }
  }

  // Find the `position` where to insert `feature_value`, if it is among the
  // 16 smallest.
  if (feature_value < smallest_values[7]) {
    if (feature_value < smallest_values[3]) {
      if (feature_value < smallest_values[1]) {
        if (feature_value < smallest_values[0]) {
          position = 0;
        } else {
          position = 1;
        }
      } else if (feature_value < smallest_values[2]) {
        position = 2;
      } else {
        position = 3;
      }
    } else if (feature_value < smallest_values[5]) {
      if (feature_value < smallest_values[4]) {
        position = 4;
      } else {
        position = 5;
      }
    } else if (feature_value < smallest_values[6]) {
      position = 6;
    } else {
      position = 7;
    }
  } else if (feature_value < smallest_values[15]) {
    if (feature_value < smallest_values[11]) {
      if (feature_value < smallest_values[9]) {
        if (feature_value < smallest_values[8]) {
          position = 8;
        } else {
          position = 9;
        }
      } else if (feature_value < smallest_values[10]) {
        position = 10;
      } else {
        position = 11;
      }
    } else if (feature_value < smallest_values[13]) {
      if (feature_value < smallest_values[12]) {
        position = 12;
      } else {
        position = 13;
      }
    } else if (feature_value < smallest_values[14]) {
      position = 14;
    } else {
      position = 15;
    }
  }

  // Insert a new small value and shift larger values up.
  if (position > -1) {
    for (int i = 15; i > position; i--) {
      smallest_values[i] = smallest_values[i - 1];
      age[i] = age[i - 1];
    }
    smallest_values[position] = feature_value;
    age[position] = 1;
  }

  // Get `current_median`.
  if (self.frame_counter > 2) {
    current_median = smallest_values[2];
  } else if (self.frame_counter > 0) {
    current_median = smallest_values[0];
  }

  // Smooth the median value.
  if (self.frame_counter > 0) {
    if (current_median < self.mean_value[channel]) {
      alpha = kSmoothingDown;  // 0.2 in Q15.
    } else {
      alpha = kSmoothingUp;  // 0.99 in Q15.
    }
  }
  int32_t tmp32 = (alpha + 1) * self.mean_value[channel];
  tmp32 += (32767 - alpha) * current_median;
  tmp32 += 16384;
  self.mean_value[channel] = static_cast<int16_t>(tmp32 >> 15);

  return self.mean_value[channel];
}

int16_t VadCalculateFeatures(VadInst& self, const int16_t* data_in, size_t data_length, int16_t* features) {
  int16_t total_energy = 0;
  // `data_length` is 80, 160 or 240 (10, 20 or 30 ms at 8 kHz).
  int16_t hp_120[120], lp_120[120];
  int16_t hp_60[60], lp_60[60];
  const size_t half_data_length = data_length >> 1;
  size_t length = half_data_length;  // Bandwidth 2000 Hz after downsampling.

  // Split at 2000 Hz and downsample.
  int frequency_band = 0;
  SplitFilter(data_in, data_length, &self.upper_state[frequency_band], &self.lower_state[frequency_band], hp_120,
              lp_120);

  // For the upper band (2000 Hz - 4000 Hz) split at 3000 Hz and downsample.
  frequency_band = 1;
  SplitFilter(hp_120, length, &self.upper_state[frequency_band], &self.lower_state[frequency_band], hp_60, lp_60);

  // Energy in 3000 Hz - 4000 Hz.
  length >>= 1;  // `data_length` / 4 <=> bandwidth = 1000 Hz.
  LogOfEnergy(hp_60, length, kOffsetVector[5], &total_energy, &features[5]);
  // Energy in 2000 Hz - 3000 Hz.
  LogOfEnergy(lp_60, length, kOffsetVector[4], &total_energy, &features[4]);

  // For the lower band (0 Hz - 2000 Hz) split at 1000 Hz and downsample.
  frequency_band = 2;
  length = half_data_length;  // `data_length` / 2 <=> bandwidth = 2000 Hz.
  SplitFilter(lp_120, length, &self.upper_state[frequency_band], &self.lower_state[frequency_band], hp_60, lp_60);

  // Energy in 1000 Hz - 2000 Hz.
  length >>= 1;  // `data_length` / 4 <=> bandwidth = 1000 Hz.
  LogOfEnergy(hp_60, length, kOffsetVector[3], &total_energy, &features[3]);

  // For the lower band (0 Hz - 1000 Hz) split at 500 Hz and downsample.
  frequency_band = 3;
  SplitFilter(lp_60, length, &self.upper_state[frequency_band], &self.lower_state[frequency_band], hp_120, lp_120);

  // Energy in 500 Hz - 1000 Hz.
  length >>= 1;  // `data_length` / 8 <=> bandwidth = 500 Hz.
  LogOfEnergy(hp_120, length, kOffsetVector[2], &total_energy, &features[2]);

  // For the lower band (0 Hz - 500 Hz) split at 250 Hz and downsample.
  frequency_band = 4;
  SplitFilter(lp_120, length, &self.upper_state[frequency_band], &self.lower_state[frequency_band], hp_60, lp_60);

  // Energy in 250 Hz - 500 Hz.
  length >>= 1;  // `data_length` / 16 <=> bandwidth = 250 Hz.
  LogOfEnergy(hp_60, length, kOffsetVector[1], &total_energy, &features[1]);

  // Remove 0 Hz - 80 Hz, by high pass filtering the lower band.
  HighPassFilter(lp_60, length, self.hp_filter_state, hp_120);

  // Energy in 80 Hz - 250 Hz.
  LogOfEnergy(hp_120, length, kOffsetVector[0], &total_energy, &features[0]);

  return total_energy;
}

int VadInitCore(VadInst& self) {
  self.vad = 1;  // Speech active (=1).
  self.frame_counter = 0;
  self.over_hang = 0;
  self.num_of_speech = 0;
  std::memset(self.downsampling_filter_states, 0, sizeof(self.downsampling_filter_states));
  for (int i = 0; i < kTableSize; i++) {
    self.noise_means[i] = kNoiseDataMeans[i];
    self.speech_means[i] = kSpeechDataMeans[i];
    self.noise_stds[i] = kNoiseDataStds[i];
    self.speech_stds[i] = kSpeechDataStds[i];
  }
  for (int i = 0; i < 16 * kNumChannels; i++) {
    self.low_value_vector[i] = 10000;
    self.index_vector[i] = 0;
  }
  std::memset(self.upper_state, 0, sizeof(self.upper_state));
  std::memset(self.lower_state, 0, sizeof(self.lower_state));
  std::memset(self.hp_filter_state, 0, sizeof(self.hp_filter_state));
  for (int i = 0; i < kNumChannels; i++) {
    self.mean_value[i] = 1600;
  }
  if (VadSetModeCore(self, kDefaultMode) != 0) {
    return -1;
  }
  self.init_flag = kInitCheck;
  return 0;
}

int VadSetModeCore(VadInst& self, int mode) {
  const int16_t *max1, *max2, *local, *global;
  switch (mode) {
    case 0:  // Quality mode.
      max1 = kOverHangMax1Q, max2 = kOverHangMax2Q, local = kLocalThresholdQ, global = kGlobalThresholdQ;
      break;
    case 1:  // Low bitrate mode.
      max1 = kOverHangMax1LBR, max2 = kOverHangMax2LBR, local = kLocalThresholdLBR, global = kGlobalThresholdLBR;
      break;
    case 2:  // Aggressive mode.
      max1 = kOverHangMax1AGG, max2 = kOverHangMax2AGG, local = kLocalThresholdAGG, global = kGlobalThresholdAGG;
      break;
    case 3:  // Very aggressive mode.
      max1 = kOverHangMax1VAG, max2 = kOverHangMax2VAG, local = kLocalThresholdVAG, global = kGlobalThresholdVAG;
      break;
    default:
      return -1;
  }
  std::memcpy(self.over_hang_max_1, max1, sizeof(self.over_hang_max_1));
  std::memcpy(self.over_hang_max_2, max2, sizeof(self.over_hang_max_2));
  std::memcpy(self.individual, local, sizeof(self.individual));
  std::memcpy(self.total, global, sizeof(self.total));
  return 0;
}

int VadCalcVad32khz(VadInst& self, const int16_t* speech_frame, size_t frame_length) {
  int16_t speechWB[480];  // Downsampled speech frame: 960 samples (30ms in SWB)
  int16_t speechNB[240];  // Downsampled speech frame: 480 samples (30ms in WB)
  // Downsample signal 32->16->8 before doing VAD.
  VadDownsampling(speech_frame, speechWB, &self.downsampling_filter_states[2], frame_length);
  size_t len = frame_length / 2;
  VadDownsampling(speechWB, speechNB, self.downsampling_filter_states, len);
  len /= 2;
  return VadCalcVad8khz(self, speechNB, len);
}

int VadCalcVad16khz(VadInst& self, const int16_t* speech_frame, size_t frame_length) {
  int16_t speechNB[240];  // Downsampled speech frame: 480 samples (30ms in WB)
  VadDownsampling(speech_frame, speechNB, self.downsampling_filter_states, frame_length);
  const size_t len = frame_length / 2;
  return VadCalcVad8khz(self, speechNB, len);
}

int VadCalcVad8khz(VadInst& self, const int16_t* speech_frame, size_t frame_length) {
  int16_t feature_vector[kNumChannels];
  const int16_t total_power = VadCalculateFeatures(self, speech_frame, frame_length, feature_vector);
  self.vad = GmmProbability(&self, feature_vector, total_power, frame_length);
  return self.vad;
}

int VadValidRateAndFrameLength(int rate, size_t frame_length) {
  static constexpr int kValidRates[] = {8000, 16000, 32000, 48000};
  static constexpr int kMaxFrameLengthMs = 30;
  for (int valid_rate : kValidRates) {
    if (valid_rate == rate) {
      for (int valid_length_ms = 10; valid_length_ms <= kMaxFrameLengthMs; valid_length_ms += 10) {
        if (frame_length == static_cast<size_t>(valid_rate / 1000 * valid_length_ms)) {
          return 0;
        }
      }
      return -1;
    }
  }
  return -1;
}

int WebRtcVad::Process(int fs, const int16_t* audio_frame, size_t frame_length) {
  if (inst_.init_flag != kInitCheck || audio_frame == nullptr) {
    return -1;
  }
  if (VadValidRateAndFrameLength(fs, frame_length) != 0) {
    return -1;
  }
  int vad = -1;
  if (fs == 32000) {
    vad = VadCalcVad32khz(inst_, audio_frame, frame_length);
  } else if (fs == 16000) {
    vad = VadCalcVad16khz(inst_, audio_frame, frame_length);
  } else if (fs == 8000) {
    vad = VadCalcVad8khz(inst_, audio_frame, frame_length);
  }  // 48 kHz is not ported (see the header).
  if (vad > 0) {
    vad = 1;
  }
  return vad;
}

// ---- standalone_vad.cc ------------------------------------------------------

namespace {
constexpr int kDefaultStandaloneVadMode = 3;
}  // namespace

StandaloneVad::StandaloneVad() : mode_(kDefaultStandaloneVadMode) { vad_.set_mode(kDefaultStandaloneVadMode); }

int StandaloneVad::AddAudio(const int16_t* data, size_t length) {
  if (length != kVadLength10Ms) return -1;
  if (index_ + length > kVadLength10Ms * kMaxNum10msFrames) {
    // Reset the buffer if it's full.
    index_ = 0;
  }
  std::memcpy(&buffer_[index_], data, sizeof(int16_t) * length);
  index_ += length;
  return 0;
}

int StandaloneVad::GetActivity(double* p, size_t length_p) {
  if (index_ == 0) return -1;
  const size_t num_frames = index_ / kVadLength10Ms;
  if (num_frames > length_p) return -1;
  const int activity = vad_.Process(kVadSampleRateHz, buffer_, index_);
  if (activity < 0) {
    return -1;
  } else if (activity == 0) {
    p[0] = 0.01;  // Arbitrary but small and non-zero.
  } else {
    p[0] = 0.5;  // 0.5 is neutral values when combinned by other probabilities.
  }
  for (size_t n = 1; n < num_frames; n++) p[n] = p[0];
  // Reset the buffer to start from the beginning.
  index_ = 0;
  return activity;
}

int StandaloneVad::set_mode(int mode) {
  if (mode < 0 || mode > 3) return -1;
  if (vad_.set_mode(mode) != 0) return -1;
  mode_ = mode;
  return 0;
}

// ---- pole_zero_filter.cc ----------------------------------------------------

PoleZeroFilter::PoleZeroFilter(const float* numerator_coefficients, size_t order_numerator,
                               const float* denominator_coefficients, size_t order_denominator)
    : order_numerator_(order_numerator),
      order_denominator_(order_denominator),
      highest_order_(std::max(order_denominator, order_numerator)) {
  std::memcpy(numerator_coefficients_, numerator_coefficients, sizeof(numerator_coefficients_[0]) * (order_numerator_ + 1));
  std::memcpy(denominator_coefficients_, denominator_coefficients,
              sizeof(denominator_coefficients_[0]) * (order_denominator_ + 1));
  if (denominator_coefficients_[0] != 1) {
    for (size_t n = 0; n <= order_numerator_; n++) numerator_coefficients_[n] /= denominator_coefficients_[0];
    for (size_t n = 0; n <= order_denominator_; n++) denominator_coefficients_[n] /= denominator_coefficients_[0];
  }
}

namespace {
template <typename T>
float FilterArPast(const T* past, size_t order, const float* coefficients) {
  float sum = 0.0f;
  size_t past_index = order - 1;
  for (size_t k = 1; k <= order; k++, past_index--) sum += coefficients[k] * past[past_index];
  return sum;
}
}  // namespace

int PoleZeroFilter::Filter(const int16_t* in, size_t num_input_samples, float* output) {
  if (in == nullptr || output == nullptr) return -1;
  const size_t k = std::min(num_input_samples, highest_order_);
  size_t n;
  for (n = 0; n < k; n++) {
    output[n] = in[n] * numerator_coefficients_[0];
    output[n] += FilterArPast(&past_input_[n], order_numerator_, numerator_coefficients_);
    output[n] -= FilterArPast(&past_output_[n], order_denominator_, denominator_coefficients_);
    past_input_[n + order_numerator_] = in[n];
    past_output_[n + order_denominator_] = output[n];
  }
  if (highest_order_ < num_input_samples) {
    for (size_t m = 0; n < num_input_samples; n++, m++) {
      output[n] = in[n] * numerator_coefficients_[0];
      output[n] += FilterArPast(&in[m], order_numerator_, numerator_coefficients_);
      output[n] -= FilterArPast(&output[m], order_denominator_, denominator_coefficients_);
    }
    std::memcpy(past_input_, &in[num_input_samples - order_numerator_], sizeof(in[0]) * order_numerator_);
    std::memcpy(past_output_, &output[num_input_samples - order_denominator_], sizeof(output[0]) * order_denominator_);
  } else {
    std::memmove(past_input_, &past_input_[num_input_samples], order_numerator_ * sizeof(past_input_[0]));
    std::memmove(past_output_, &past_output_[num_input_samples], order_denominator_ * sizeof(past_output_[0]));
  }
  return 0;
}

// ---- vad_circular_buffer.cc -------------------------------------------------

VadCircularBuffer::VadCircularBuffer(int buffer_size)
    : buffer_(static_cast<size_t>(buffer_size)), buffer_size_(buffer_size) {}

void VadCircularBuffer::Reset() {
  is_full_ = false;
  index_ = 0;
  sum_ = 0;
}

double VadCircularBuffer::Oldest() const { return !is_full_ ? buffer_[0] : buffer_[static_cast<size_t>(index_)]; }

double VadCircularBuffer::Mean() {
  double m;
  if (is_full_) {
    m = sum_ / buffer_size_;
  } else {
    if (index_ > 0)
      m = sum_ / index_;
    else
      m = 0;
  }
  return m;
}

void VadCircularBuffer::Insert(double value) {
  if (is_full_) {
    sum_ -= buffer_[static_cast<size_t>(index_)];
  }
  sum_ += value;
  buffer_[static_cast<size_t>(index_)] = value;
  index_++;
  if (index_ >= buffer_size_) {
    is_full_ = true;
    index_ = 0;
  }
}

int VadCircularBuffer::BufferLevel() { return is_full_ ? buffer_size_ : index_; }

int VadCircularBuffer::Get(int index, double* value) const {
  if (ConvertToLinearIndex(&index) < 0) return -1;
  *value = buffer_[static_cast<size_t>(index)];
  return 0;
}

int VadCircularBuffer::Set(int index, double value) {
  if (ConvertToLinearIndex(&index) < 0) return -1;
  sum_ -= buffer_[static_cast<size_t>(index)];
  buffer_[static_cast<size_t>(index)] = value;
  sum_ += value;
  return 0;
}

int VadCircularBuffer::ConvertToLinearIndex(int* index) const {
  if (*index < 0 || *index >= buffer_size_) return -1;
  if (!is_full_ && *index >= index_) return -1;
  *index = index_ - 1 - *index;
  if (*index < 0) *index += buffer_size_;
  return 0;
}

int VadCircularBuffer::RemoveTransient(int width_threshold, double val_threshold) {
  if (!is_full_ && index_ < width_threshold + 2) return 0;
  const int index_1 = 0;
  const int index_2 = width_threshold + 1;
  double v = 0;
  if (Get(index_1, &v) < 0) return -1;
  if (v < val_threshold) {
    Set(index_1, 0);
    int index;
    for (index = index_2; index > index_1; index--) {
      if (Get(index, &v) < 0) return -1;
      if (v < val_threshold) break;
    }
    for (; index > index_1; index--) {
      if (Set(index, 0.0) < 0) return -1;
    }
  }
  return 0;
}

// ---- gmm.cc, noise_gmm_tables.h, voice_gmm_tables.h -------------------------

namespace {

constexpr int kMaxDimension = 10;

void RemoveMean(const double* in, const double* mean_vec, int dimension, double* out) {
  for (int n = 0; n < dimension; ++n) out[n] = in[n] - mean_vec[n];
}

double ComputeExponent(const double* in, const double* covar_inv, int dimension) {
  double q = 0;
  for (int i = 0; i < dimension; ++i) {
    double v = 0;
    for (int j = 0; j < dimension; j++) v += (*covar_inv++) * in[j];
    q += v * in[i];
  }
  q *= -0.5;
  return q;
}

constexpr int kNoiseGmmNumMixtures = 12;
constexpr int kNoiseGmmDim = 3;
constexpr double kNoiseGmmCovarInverse[kNoiseGmmNumMixtures][kNoiseGmmDim][kNoiseGmmDim] = {
        {{7.36219567592941e+00, 4.83060785179861e-03, 1.23335151497610e-02},
         {4.83060785179861e-03, 1.65289507047817e-04, -2.41490588169997e-04},
         {1.23335151497610e-02, -2.41490588169997e-04, 6.59472060689382e-03}},
        {{8.70265239309140e+00, -5.30636201431086e-04, 5.44014966585347e-03},
         {-5.30636201431086e-04, 3.11095453521008e-04, -1.86287206836035e-04},
         {5.44014966585347e-03, -1.86287206836035e-04, 6.29493388790744e-04}},
        {{4.53467851955055e+00, -3.92977536695197e-03, -2.46521420693317e-03},
         {-3.92977536695197e-03, 4.94650752632750e-05, -1.08587438501826e-05},
         {-2.46521420693317e-03, -1.08587438501826e-05, 9.28793975422261e-05}},
        {{9.26817997114275e-01, -4.03976069276753e-04, -3.56441427392165e-03},
         {-4.03976069276753e-04, 2.51976251631430e-06, 1.46914206734572e-07},
         {-3.56441427392165e-03, 1.46914206734572e-07, 8.19914567685373e-05}},
        {{7.61715986787441e+00, -1.54889041216888e-04, 2.41756280071656e-02},
         {-1.54889041216888e-04, 3.50282550461672e-07, -6.27251196972490e-06},
         {2.41756280071656e-02, -6.27251196972490e-06, 1.45061847649872e-02}},
        {{8.31193642663158e+00, -3.84070508164323e-04, -3.09750630821876e-02},
         {-3.84070508164323e-04, 3.80433432277336e-07, -1.14321142836636e-06},
         {-3.09750630821876e-02, -1.14321142836636e-06, 8.35091486289997e-04}},
        {{9.67283151270894e-01, 5.82465812445039e-05, -3.18350798617053e-03},
         {5.82465812445039e-05, 2.23762672000318e-07, -7.74196587408623e-07},
         {-3.18350798617053e-03, -7.74196587408623e-07, 3.85120938338325e-04}},
        {{8.28066236985388e+00, 5.87634508319763e-05, 6.99303090891743e-03},
         {5.87634508319763e-05, 2.93746018618058e-07, 3.40843332882272e-07},
         {6.99303090891743e-03, 3.40843332882272e-07, 1.99379171190344e-04}},
        {{6.07488998675646e+00, -1.11494526618473e-02, 5.10013111123381e-03},
         {-1.11494526618473e-02, 6.99238879921751e-04, 5.36718550370870e-05},
         {5.10013111123381e-03, 5.36718550370870e-05, 5.26909853276753e-04}},
        {{6.90492021419175e+00, 4.20639355257863e-04, -2.38612752336481e-03},
         {4.20639355257863e-04, 3.31246767338153e-06, -2.42052288150859e-08},
         {-2.38612752336481e-03, -2.42052288150859e-08, 4.46608368363412e-04}},
        {{1.31069150869715e+01, -1.73718583865670e-04, -1.97591814508578e-02},
         {-1.73718583865670e-04, 2.80451716300124e-07, 9.96570755379865e-07},
         {-1.97591814508578e-02, 9.96570755379865e-07, 2.41361900868847e-03}},
        {{4.69566344239814e+00, -2.61077567563690e-04, 5.26359000761433e-03},
         {-2.61077567563690e-04, 1.82420859823767e-06, -7.83645887541601e-07},
         {5.26359000761433e-03, -7.83645887541601e-07, 1.33586288288802e-02}}};
constexpr double kNoiseGmmMean[kNoiseGmmNumMixtures][kNoiseGmmDim] = {
    {-2.01386094766163e+00, 1.69702162045397e+02, 7.41715804872181e+01},
    {-1.94684591777290e+00, 1.42398396732668e+02, 1.64186321157831e+02},
    {-2.29319297562437e+00, 3.86415425589868e+02, 2.13452215267125e+02},
    {-3.25487177070268e+00, 1.08668712553616e+03, 2.33119949467419e+02},
    {-2.13159632447467e+00, 4.83821702557717e+03, 6.86786166673740e+01},
    {-2.26171410780526e+00, 4.79420193982422e+03, 1.53222513286450e+02},
    {-3.32166740703185e+00, 4.35161135834358e+03, 1.33206448431316e+02},
    {-2.19290322814343e+00, 3.98325506609408e+03, 2.13249167359934e+02},
    {-2.02898459255404e+00, 7.37039893155007e+03, 1.12518527491926e+02},
    {-2.26150236399500e+00, 1.54896745196145e+03, 1.49717357868579e+02},
    {-2.00417668301790e+00, 3.82434760310304e+03, 1.07438913004312e+02},
    {-2.30193040814533e+00, 1.43953696546439e+03, 7.04085275122649e+01}};
constexpr double kNoiseGmmWeights[kNoiseGmmNumMixtures] = {
    -1.09422832086193e+01, -1.10847897513425e+01, -1.36767587732187e+01,
    -1.79789356118641e+01, -1.42830169160894e+01, -1.56500228061379e+01,
    -1.83124990950113e+01, -1.69979436177477e+01, -1.12329424387828e+01,
    -1.41311785780639e+01, -1.47171861448585e+01, -1.35963362781839e+01};
constexpr int kVoiceGmmNumMixtures = 12;
constexpr int kVoiceGmmDim = 3;
constexpr double kVoiceGmmCovarInverse[kVoiceGmmNumMixtures][kVoiceGmmDim][kVoiceGmmDim] = {
        {{1.83673825579513e+00, -8.09791637570095e-04, 4.60106414365986e-03},
         {-8.09791637570095e-04, 8.89351738394608e-04, -9.80188953277734e-04},
         {4.60106414365986e-03, -9.80188953277734e-04, 1.38706060206582e-03}},
        {{6.76228912850703e+01, -1.98893120119660e-02, -3.53548357253551e-03},
         {-1.98893120119660e-02, 3.96216858500530e-05, -4.08492938394097e-05},
         {-3.53548357253551e-03, -4.08492938394097e-05, 9.31864352856416e-04}},
        {{9.98612435944558e+00, -5.27880954316893e-03, -6.30342541619017e-03},
         {-5.27880954316893e-03, 4.54359480225226e-05, 6.30804591626044e-05},
         {-6.30342541619017e-03, 6.30804591626044e-05, 5.36466441382942e-04}},
        {{3.39917474216349e+01, -1.56213579433191e-03, -4.01459014990225e-02},
         {-1.56213579433191e-03, 6.40415424897724e-05, 6.20076342427833e-05},
         {-4.01459014990225e-02, 6.20076342427833e-05, 3.51199070103063e-03}},
        {{1.34545062271428e+01, -7.94513610147144e-03, -5.34401019341728e-02},
         {-7.94513610147144e-03, 1.16511820098649e-04, 4.66063702069293e-05},
         {-5.34401019341728e-02, 4.66063702069293e-05, 2.72354323774163e-03}},
        {{1.08557844314806e+02, -1.54885805673668e-02, -1.88029692674851e-02},
         {-1.54885805673668e-02, 1.16404042786406e-04, 6.45579292702802e-06},
         {-1.88029692674851e-02, 6.45579292702802e-06, 4.32330478391416e-04}},
        {{8.22940066541450e+01, -1.15903110231303e-02, -4.92166764865343e-02},
         {-1.15903110231303e-02, 7.42510742165261e-05, 3.73007314191290e-06},
         {-4.92166764865343e-02, 3.73007314191290e-06, 3.64005221593244e-03}},
        {{2.31133605685660e+00, -7.83261568950254e-04, 7.45744012346313e-04},
         {-7.83261568950254e-04, 1.29460648214142e-05, -2.22774455093730e-06},
         {7.45744012346313e-04, -2.22774455093730e-06, 1.05117294093010e-04}},
        {{3.78767849189611e+02, 1.57759761011568e-03, -2.08551217988774e-02},
         {1.57759761011568e-03, 4.76066236886865e-05, -2.33977412299324e-05},
         {-2.08551217988774e-02, -2.33977412299324e-05, 5.24261005371196e-04}},
        {{6.98580096506135e-01, -5.13850255217378e-04, -4.01124551717056e-04},
         {-5.13850255217378e-04, 1.40501021984840e-06, -2.09496928716569e-06},
         {-4.01124551717056e-04, -2.09496928716569e-06, 2.82879357740037e-04}},
        {{2.62770945162399e+00, -2.31825753241430e-03, -5.30447217466318e-03},
         {-2.31825753241430e-03, 4.59108572227649e-05, 7.67631886355405e-05},
         {-5.30447217466318e-03, 7.67631886355405e-05, 2.28521601674098e-03}},
        {{1.89940391362152e+02, -4.23280856852379e-03, -2.70608873541399e-02},
         {-4.23280856852379e-03, 6.77547582742563e-05, 2.69154203800467e-05},
         {-2.70608873541399e-02, 2.69154203800467e-05, 3.88574543373470e-03}}};
constexpr double kVoiceGmmMean[kVoiceGmmNumMixtures][kVoiceGmmDim] = {
    {-2.15020241646536e+00, 4.97079062999877e+02, 4.77078119504505e+02},
    {-8.92097680029190e-01, 5.92064964199921e+02, 1.81045145941059e+02},
    {-1.29435784144398e+00, 4.98450293410611e+02, 1.71991263804064e+02},
    {-1.03925228397884e+00, 4.99511274321571e+02, 1.05838336539105e+02},
    {-1.29229047206129e+00, 4.15026762566707e+02, 1.12861119017125e+02},
    {-7.88748114599810e-01, 4.48739336688113e+02, 1.89784216956337e+02},
    {-8.77777402332642e-01, 4.86620285054533e+02, 1.13477708016491e+02},
    {-2.06465957063057e+00, 6.33385049870607e+02, 2.32758546796149e+02},
    {-6.98893789231685e-01, 5.93622051503385e+02, 1.92536982473203e+02},
    {-2.55901217508894e+00, 1.55914919756205e+03, 1.39769980835570e+02},
    {-1.92070024165837e+00, 4.87983940444185e+02, 1.02745468128289e+02},
    {-7.29187507662854e-01, 5.22717685022855e+02, 1.16377942283991e+02}};
constexpr double kVoiceGmmWeights[kVoiceGmmNumMixtures] = {
    -1.39789694361035e+01, -1.19527720202104e+01, -1.32396317929055e+01,
    -1.09436815209238e+01, -1.13440027478149e+01, -1.12200721834504e+01,
    -1.02537324043693e+01, -1.60789861938302e+01, -1.03394494048344e+01,
    -1.83207938586818e+01, -1.31186044948288e+01, -9.52479998673554e+00};
static_assert(kNoiseGmmDim == kVoiceGmmDim, "noise and voice gmm dimension not equal");

}  // namespace

double EvaluateGmm(const double* x, const GmmParameters& gmm_parameters) {
  if (gmm_parameters.dimension > kMaxDimension) {
    return -1;  // This is invalid pdf so the caller can check this.
  }
  double f = 0;
  double v[kMaxDimension];
  const double* mean_vec = gmm_parameters.mean;
  const double* covar_inv = gmm_parameters.covar_inverse;
  for (int n = 0; n < gmm_parameters.num_mixtures; n++) {
    RemoveMean(x, mean_vec, gmm_parameters.dimension, v);
    const double q = ComputeExponent(v, covar_inv, gmm_parameters.dimension) + gmm_parameters.weight[n];
    f += std::exp(q);
    mean_vec += gmm_parameters.dimension;
    covar_inv += gmm_parameters.dimension * gmm_parameters.dimension;
  }
  return f;
}

const GmmParameters& NoiseGmm() {
  static constexpr GmmParameters kNoise = {kNoiseGmmWeights, &kNoiseGmmMean[0][0], &kNoiseGmmCovarInverse[0][0][0],
                                           kNoiseGmmDim, kNoiseGmmNumMixtures};
  return kNoise;
}

const GmmParameters& VoiceGmm() {
  static constexpr GmmParameters kVoice = {kVoiceGmmWeights, &kVoiceGmmMean[0][0], &kVoiceGmmCovarInverse[0][0][0],
                                           kVoiceGmmDim, kVoiceGmmNumMixtures};
  return kVoice;
}

// ---- pitch_internal.cc ----------------------------------------------------

namespace {
void PitchInterpolation(double old_val, const double* in, double* out) {
  out[0] = 1. / 6. * old_val + 5. / 6. * in[0];
  out[1] = 5. / 6. * in[1] + 1. / 6. * in[2];
  out[2] = 0.5 * in[2] + 0.5 * in[3];
}
}  // namespace

void GetSubframesPitchParameters(int sampling_rate_hz, double* gains, double* lags, int num_in_frames,
                                 int num_out_frames, double* log_old_gain, double* old_lag, double* log_pitch_gain,
                                 double* pitch_lag_hz) {
  // Gain interpolation is in log-domain, also returned in log-domain.
  for (int n = 0; n < num_in_frames; n++) gains[n] = std::log(gains[n] + 1e-12);

  // Interpolate lags and gains.
  PitchInterpolation(*log_old_gain, gains, log_pitch_gain);
  *log_old_gain = gains[num_in_frames - 1];
  PitchInterpolation(*old_lag, lags, pitch_lag_hz);
  *old_lag = lags[num_in_frames - 1];

  // Convert pitch-lags to Hertz.
  for (int n = 0; n < num_out_frames; n++) {
    pitch_lag_hz[n] = (sampling_rate_hz) / (pitch_lag_hz[n]);
  }
}

// ---- pitch_based_vad.cc -----------------------------------------------------

namespace {

constexpr int kPosteriorHistorySize = 500;  // 5 sec of 10 ms frames.
constexpr double kInitialPriorProbability = 0.3;
constexpr int kTransientWidthThreshold = 7;
constexpr double kLowProbabilityThreshold = 0.2;

double LimitProbability(double p) {
  const double kLimHigh = 0.99;
  const double kLimLow = 0.01;
  if (p > kLimHigh)
    p = kLimHigh;
  else if (p < kLimLow)
    p = kLimLow;
  return p;
}

}  // namespace

PitchBasedVad::PitchBasedVad()
    : noise_gmm_(NoiseGmm()),
      voice_gmm_(VoiceGmm()),
      p_prior_(kInitialPriorProbability),
      circular_buffer_(kPosteriorHistorySize) {}

int PitchBasedVad::VoicingProbability(const AudioFeatures& features, double* p_combined) {
  double gmm_features[3];
  const double kLimLowLogPitchGain = -2.0;
  const double kLimHighLogPitchGain = -0.9;
  const double kLimLowSpectralPeak = 200;
  const double kLimHighSpectralPeak = 2000;
  const double kEps = 1e-12;
  for (size_t n = 0; n < features.num_frames; n++) {
    gmm_features[0] = features.log_pitch_gain[n];
    gmm_features[1] = features.spectral_peak[n];
    gmm_features[2] = features.pitch_lag_hz[n];

    double pdf_features_given_voice = EvaluateGmm(gmm_features, voice_gmm_);
    double pdf_features_given_noise = EvaluateGmm(gmm_features, noise_gmm_);

    if (features.spectral_peak[n] < kLimLowSpectralPeak || features.spectral_peak[n] > kLimHighSpectralPeak ||
        features.log_pitch_gain[n] < kLimLowLogPitchGain) {
      pdf_features_given_voice = kEps * pdf_features_given_noise;
    } else if (features.log_pitch_gain[n] > kLimHighLogPitchGain) {
      pdf_features_given_noise = kEps * pdf_features_given_voice;
    }

    double p = p_prior_ * pdf_features_given_voice /
               (pdf_features_given_voice * p_prior_ + pdf_features_given_noise * (1 - p_prior_));

    p = LimitProbability(p);

    // Combine pitch-based probability with standalone probability, before
    // updating prior probabilities.
    const double prod_active = p * p_combined[n];
    const double prod_inactive = (1 - p) * (1 - p_combined[n]);
    p_combined[n] = prod_active / (prod_active + prod_inactive);

    if (UpdatePrior(p_combined[n]) < 0) return -1;
    // Limit prior probability. With a zero prior probability the posterior
    // probability is always zero.
    p_prior_ = LimitProbability(p_prior_);
  }
  return 0;
}

int PitchBasedVad::UpdatePrior(double p) {
  circular_buffer_.Insert(p);
  if (circular_buffer_.RemoveTransient(kTransientWidthThreshold, kLowProbabilityThreshold) < 0) return -1;
  p_prior_ = circular_buffer_.Mean();
  return 0;
}

// ---- vad_audio_proc.cc, vad_audio_proc_internal.h -------------------------

namespace {

// These values should match MATLAB counterparts for unit-tests to pass.
constexpr double kCorrWeight[] = {1.000000, 0.985000, 0.970225, 0.955672, 0.941337, 0.927217,
                                  0.913308, 0.899609, 0.886115, 0.872823, 0.859730, 0.846834,
                                  0.834132, 0.821620, 0.809296, 0.797156, 0.785199};

constexpr double kLpcAnalWin[] = {
    0.00000000, 0.01314436, 0.02628645, 0.03942400, 0.05255473, 0.06567639,
    0.07878670, 0.09188339, 0.10496421, 0.11802689, 0.13106918, 0.14408883,
    0.15708358, 0.17005118, 0.18298941, 0.19589602, 0.20876878, 0.22160547,
    0.23440387, 0.24716177, 0.25987696, 0.27254725, 0.28517045, 0.29774438,
    0.31026687, 0.32273574, 0.33514885, 0.34750406, 0.35979922, 0.37203222,
    0.38420093, 0.39630327, 0.40833713, 0.42030043, 0.43219112, 0.44400713,
    0.45574642, 0.46740697, 0.47898676, 0.49048379, 0.50189608, 0.51322164,
    0.52445853, 0.53560481, 0.54665854, 0.55761782, 0.56848075, 0.57924546,
    0.58991008, 0.60047278, 0.61093173, 0.62128512, 0.63153117, 0.64166810,
    0.65169416, 0.66160761, 0.67140676, 0.68108990, 0.69065536, 0.70010148,
    0.70942664, 0.71862923, 0.72770765, 0.73666033, 0.74548573, 0.75418233,
    0.76274862, 0.77118312, 0.77948437, 0.78765094, 0.79568142, 0.80357442,
    0.81132858, 0.81894256, 0.82641504, 0.83374472, 0.84093036, 0.84797069,
    0.85486451, 0.86161063, 0.86820787, 0.87465511, 0.88095122, 0.88709512,
    0.89308574, 0.89892206, 0.90460306, 0.91012776, 0.91549520, 0.92070447,
    0.92575465, 0.93064488, 0.93537432, 0.93994213, 0.94434755, 0.94858979,
    0.95266814, 0.95658189, 0.96033035, 0.96391289, 0.96732888, 0.97057773,
    0.97365889, 0.97657181, 0.97931600, 0.98189099, 0.98429632, 0.98653158,
    0.98859639, 0.99049038, 0.99221324, 0.99376466, 0.99514438, 0.99635215,
    0.99738778, 0.99825107, 0.99894188, 0.99946010, 0.99980562, 0.99997840,
    0.99997840, 0.99980562, 0.99946010, 0.99894188, 0.99825107, 0.99738778,
    0.99635215, 0.99514438, 0.99376466, 0.99221324, 0.99049038, 0.98859639,
    0.98653158, 0.98429632, 0.98189099, 0.97931600, 0.97657181, 0.97365889,
    0.97057773, 0.96732888, 0.96391289, 0.96033035, 0.95658189, 0.95266814,
    0.94858979, 0.94434755, 0.93994213, 0.93537432, 0.93064488, 0.92575465,
    0.92070447, 0.91549520, 0.91012776, 0.90460306, 0.89892206, 0.89308574,
    0.88709512, 0.88095122, 0.87465511, 0.86820787, 0.86161063, 0.85486451,
    0.84797069, 0.84093036, 0.83374472, 0.82641504, 0.81894256, 0.81132858,
    0.80357442, 0.79568142, 0.78765094, 0.77948437, 0.77118312, 0.76274862,
    0.75418233, 0.74548573, 0.73666033, 0.72770765, 0.71862923, 0.70942664,
    0.70010148, 0.69065536, 0.68108990, 0.67140676, 0.66160761, 0.65169416,
    0.64166810, 0.63153117, 0.62128512, 0.61093173, 0.60047278, 0.58991008,
    0.57924546, 0.56848075, 0.55761782, 0.54665854, 0.53560481, 0.52445853,
    0.51322164, 0.50189608, 0.49048379, 0.47898676, 0.46740697, 0.45574642,
    0.44400713, 0.43219112, 0.42030043, 0.40833713, 0.39630327, 0.38420093,
    0.37203222, 0.35979922, 0.34750406, 0.33514885, 0.32273574, 0.31026687,
    0.29774438, 0.28517045, 0.27254725, 0.25987696, 0.24716177, 0.23440387,
    0.22160547, 0.20876878, 0.19589602, 0.18298941, 0.17005118, 0.15708358,
    0.14408883, 0.13106918, 0.11802689, 0.10496421, 0.09188339, 0.07878670,
    0.06567639, 0.05255473, 0.03942400, 0.02628645, 0.01314436, 0.00000000};

constexpr size_t kFilterOrder = 2;
constexpr float kCoeffNumerator[kFilterOrder + 1] = {0.974827f, -1.949650f, 0.974827f};
constexpr float kCoeffDenominator[kFilterOrder + 1] = {1.0f, -1.971999f, 0.972457f};

constexpr float kFrequencyResolution = kVadSampleRateHz / static_cast<float>(VadAudioProc::kDftSize);
constexpr int kSilenceRms = 5;

// Fit a second order curve to these 3 points and find the location of the
// extremum. The points are inverted before curve fitting.
float QuadraticInterpolation(float prev_val, float curr_val, float next_val) {
  // Doing the interpolation in |1 / A(z)|^2.
  float fractional_index = 0;
  next_val = 1.0f / next_val;
  prev_val = 1.0f / prev_val;
  curr_val = 1.0f / curr_val;
  fractional_index = -(next_val - prev_val) * 0.5f / (next_val + prev_val - 2.f * curr_val);
  return fractional_index;
}

}  // namespace

VadAudioProc::VadAudioProc()
    : num_buffer_samples_(kNumPastSignalSamples),
      log_old_gain_(-2),
      old_lag_(50),  // Arbitrary but valid as pitch-lag (in samples).
      high_pass_filter_(kCoeffNumerator, kFilterOrder, kCoeffDenominator, kFilterOrder) {
  static_assert(kNumPastSignalSamples + kNumSubframeSamples == sizeof(kLpcAnalWin) / sizeof(kLpcAnalWin[0]),
                "lpc analysis window incorrect size");
  static_assert(kLpcOrder + 1 == sizeof(kCorrWeight) / sizeof(kCorrWeight[0]), "correlation weight incorrect size");
  // Make FFT to initialize.
  float data[kDftSize] = {};
  ip_[0] = 0;
  ooura::Rdft(kDftSize, 1, data, ip_, w_fft_);
  // Initialize iSAC components.
  isac::InitPreFilterbank(pre_filter_);
  isac::InitPitchAnalysis(pitch_analysis_);
}

void VadAudioProc::ResetBuffer() {
  std::memcpy(audio_buffer_, &audio_buffer_[kNumSamplesToProcess], sizeof(audio_buffer_[0]) * kNumPastSignalSamples);
  num_buffer_samples_ = kNumPastSignalSamples;
}

int VadAudioProc::ExtractFeatures(const int16_t* frame, size_t length, AudioFeatures* features) {
  features->num_frames = 0;
  if (length != kNumSubframeSamples) {
    return -1;
  }

  // High-pass filter to remove the DC component and very low frequency
  // content.
  if (high_pass_filter_.Filter(frame, kNumSubframeSamples, &audio_buffer_[num_buffer_samples_]) != 0) {
    return -1;
  }

  num_buffer_samples_ += kNumSubframeSamples;
  if (num_buffer_samples_ < kBufferLength) {
    return 0;
  }
  features->num_frames = kNum10msSubframes;
  features->silence = false;

  Rms(features->rms, kVadMaxNumFrames);
  for (size_t i = 0; i < kNum10msSubframes; ++i) {
    if (features->rms[i] < kSilenceRms) {
      // PitchAnalysis can cause NaNs in the pitch gain if it's fed silence.
      features->silence = true;
      ResetBuffer();
      return 0;
    }
  }

  PitchAnalysis(features->log_pitch_gain, features->pitch_lag_hz, kVadMaxNumFrames);
  FindFirstSpectralPeaks(features->spectral_peak, kVadMaxNumFrames);
  ResetBuffer();
  return 0;
}

// Computes |kLpcOrder + 1| correlation coefficients.
void VadAudioProc::SubframeCorrelation(double* corr, size_t length_corr, size_t subframe_index) {
  double windowed_audio[kNumSubframeSamples + kNumPastSignalSamples];
  size_t buffer_index = subframe_index * kNumSubframeSamples;
  for (size_t n = 0; n < kNumSubframeSamples + kNumPastSignalSamples; n++)
    windowed_audio[n] = audio_buffer_[buffer_index++] * kLpcAnalWin[n];
  isac::AutoCorr(corr, windowed_audio, kNumSubframeSamples + kNumPastSignalSamples, kLpcOrder);
}

// `kNum10msSubframes` sets of LPC coefficients, one per 10 ms input, over a
// 15 ms window centered on the first half of each subframe.
void VadAudioProc::GetLpcPolynomials(double* lpc, size_t length_lpc) {
  double corr[kLpcOrder + 1];
  double reflec_coeff[kLpcOrder];
  for (size_t i = 0, offset_lpc = 0; i < kNum10msSubframes; i++, offset_lpc += kLpcOrder + 1) {
    SubframeCorrelation(corr, kLpcOrder + 1, i);
    corr[0] *= 1.0001;
    // This makes Lev-Durb a bit more stable.
    for (size_t k = 0; k < kLpcOrder + 1; k++) {
      corr[k] *= kCorrWeight[k];
    }
    isac::LevDurb(&lpc[offset_lpc], reflec_coeff, corr, kLpcOrder);
  }
}

// The first local minimum of |A(z)|^2 (the first spectral envelope peak).
void VadAudioProc::FindFirstSpectralPeaks(double* f_peak, size_t length_f_peak) {
  double lpc[kNum10msSubframes * (kLpcOrder + 1)];
  // For all sub-frames.
  GetLpcPolynomials(lpc, kNum10msSubframes * (kLpcOrder + 1));

  const size_t kNumDftCoefficients = kDftSize / 2 + 1;
  float data[kDftSize];

  for (size_t i = 0; i < kNum10msSubframes; i++) {
    // Convert to float with zero pad.
    std::memset(data, 0, sizeof(data));
    for (size_t n = 0; n < kLpcOrder + 1; n++) {
      data[n] = static_cast<float>(lpc[i * (kLpcOrder + 1) + n]);
    }
    // Transform to frequency domain.
    ooura::Rdft(kDftSize, 1, data, ip_, w_fft_);

    size_t index_peak = 0;
    float prev_magn_sqr = data[0] * data[0];
    float curr_magn_sqr = data[2] * data[2] + data[3] * data[3];
    float next_magn_sqr;
    bool found_peak = false;
    for (size_t n = 2; n < kNumDftCoefficients - 1; n++) {
      next_magn_sqr = data[2 * n] * data[2 * n] + data[2 * n + 1] * data[2 * n + 1];
      if (curr_magn_sqr < prev_magn_sqr && curr_magn_sqr < next_magn_sqr) {
        found_peak = true;
        index_peak = n - 1;
        break;
      }
      prev_magn_sqr = curr_magn_sqr;
      curr_magn_sqr = next_magn_sqr;
    }
    float fractional_index = 0;
    if (!found_peak) {
      // Checking if |kNumDftCoefficients - 1| is the local minimum.
      next_magn_sqr = data[1] * data[1];
      if (curr_magn_sqr < prev_magn_sqr && curr_magn_sqr < next_magn_sqr) {
        index_peak = kNumDftCoefficients - 1;
      }
    } else {
      // A peak is found, do a simple quadratic interpolation to get a more
      // accurate estimate of the peak location.
      fractional_index = QuadraticInterpolation(prev_magn_sqr, curr_magn_sqr, next_magn_sqr);
    }
    f_peak[i] = (static_cast<float>(index_peak) + fractional_index) * kFrequencyResolution;
  }
}

// iSAC's pitch gains and lags.
void VadAudioProc::PitchAnalysis(double* log_pitch_gains, double* pitch_lags_hz, size_t length) {
  const int kNumPitchSubframes = 4;
  double gains[kNumPitchSubframes];
  double lags[kNumPitchSubframes];

  const int kNumSubbandFrameSamples = 240;
  const int kNumLookaheadSamples = 24;

  float lower[kNumSubbandFrameSamples];
  float upper[kNumSubbandFrameSamples];
  double lower_lookahead[kNumSubbandFrameSamples];
  double upper_lookahead[kNumSubbandFrameSamples];
  double lower_lookahead_pre_filter[kNumSubbandFrameSamples + kNumLookaheadSamples];

  // Split signal to lower and upper bands.
  isac::SplitAndFilterFloat(&audio_buffer_[kNumPastSignalSamples], lower, upper, lower_lookahead, upper_lookahead,
                            pre_filter_);
  isac::PitchAnalysis(lower_lookahead, lower_lookahead_pre_filter, pitch_analysis_, lags, gains);

  // Lags are computed on lower-band signal with sampling rate half of the
  // input signal.
  GetSubframesPitchParameters(kVadSampleRateHz / 2, gains, lags, kNumPitchSubframes, kNum10msSubframes,
                              &log_old_gain_, &old_lag_, log_pitch_gains, pitch_lags_hz);
}

void VadAudioProc::Rms(double* rms, size_t length_rms) {
  size_t offset = kNumPastSignalSamples;
  for (size_t i = 0; i < kNum10msSubframes; i++) {
    rms[i] = 0;
    for (size_t n = 0; n < kNumSubframeSamples; n++, offset++) rms[i] += audio_buffer_[offset] * audio_buffer_[offset];
    rms[i] = std::sqrt(rms[i] / kNumSubframeSamples);
  }
}

}  // namespace detail

// ---- voice_activity_detector.cc -------------------------------------------

namespace {
constexpr double kDefaultVoiceValue = 1.0;
constexpr double kNeutralProbability = 0.5;
constexpr double kLowProbability = 0.01;
}  // namespace

VoiceActivityDetector::VoiceActivityDetector() : last_voice_probability_(kDefaultVoiceValue) {}

void VoiceActivityDetector::ProcessChunk(std::span<const int16_t> audio) {
  if (audio.size() != detail::kVadLength10Ms) return;  // 16 kHz only (see the header).
  const size_t length = audio.size();
  standalone_vad_.AddAudio(audio.data(), length);

  audio_processing_.ExtractFeatures(audio.data(), length, &features_);

  num_chunkwise_ = features_.num_frames;
  std::copy(features_.rms, features_.rms + num_chunkwise_, rms_.begin());
  if (features_.num_frames > 0) {
    if (features_.silence) {
      std::fill(probabilities_.begin(), probabilities_.begin() + num_chunkwise_, kLowProbability);
    } else {
      std::fill(probabilities_.begin(), probabilities_.begin() + num_chunkwise_, kNeutralProbability);
      standalone_vad_.GetActivity(probabilities_.data(), num_chunkwise_);
      pitch_based_vad_.VoicingProbability(features_, probabilities_.data());
    }
    last_voice_probability_ = static_cast<float>(probabilities_[num_chunkwise_ - 1]);
  }
}

// ---- loudness_histogram.cc ----------------------------------------------------

namespace {

constexpr double kHistBinCenters[] = {
    7.59621091765857e-02, 9.02036021061016e-02, 1.07115112009343e-01, 1.27197217770508e-01, 1.51044347572047e-01,
    1.79362373905283e-01, 2.12989507320644e-01, 2.52921107370304e-01, 3.00339145144454e-01, 3.56647189489147e-01,
    4.23511952494003e-01, 5.02912623991786e-01, 5.97199455365749e-01, 7.09163326739184e-01, 8.42118356728544e-01,
    1.00000000000000e+00, 1.18748153630660e+00, 1.41011239906908e+00, 1.67448243801153e+00, 1.98841697800836e+00,
    2.36120844786349e+00, 2.80389143520905e+00, 3.32956930911896e+00, 3.95380207843188e+00, 4.69506696634852e+00,
    5.57530533426190e+00, 6.62057214370769e+00, 7.86180718043869e+00, 9.33575086877358e+00, 1.10860317842269e+01,
    1.31644580546776e+01, 1.56325508754123e+01, 1.85633655299256e+01, 2.20436538184971e+01, 2.61764319021997e+01,
    3.10840295702492e+01, 3.69117111886792e+01, 4.38319755100383e+01, 5.20496616180135e+01, 6.18080121423973e+01,
    7.33958732149108e+01, 8.71562442838066e+01, 1.03496430860848e+02, 1.22900100720889e+02, 1.45941600416277e+02,
    1.73302955873365e+02, 2.05794060286978e+02, 2.44376646872353e+02, 2.90192756065437e+02, 3.44598539797631e+02,
    4.09204403447902e+02, 4.85922673669740e+02, 5.77024203055553e+02, 6.85205587130498e+02, 8.13668983291589e+02,
    9.66216894324125e+02, 1.14736472207740e+03, 1.36247442287647e+03, 1.61791322085579e+03, 1.92124207711260e+03,
    2.28143949334655e+03, 2.70916727454970e+03, 3.21708611729384e+03, 3.82023036499473e+03, 4.53645302286906e+03,
    5.38695420497926e+03, 6.39690865534207e+03, 7.59621091765857e+03, 9.02036021061016e+03, 1.07115112009343e+04,
    1.27197217770508e+04, 1.51044347572047e+04, 1.79362373905283e+04, 2.12989507320644e+04, 2.52921107370304e+04,
    3.00339145144454e+04, 3.56647189489147e+04};

constexpr double kProbQDomain = 1024.0;
// Loudness of -15 dB (smallest expected loudness) in log domain,
// loudness_db = 13.5 * log10(rms);
constexpr double kLogDomainMinBinCenter = -2.57752062648587;
// Loudness step of 1 dB in log domain
constexpr double kLogDomainStepSizeInverse = 5.81954605750359;

constexpr int kHistTransientWidthThreshold = 7;
constexpr double kHistLowProbabilityThreshold = 0.2;
constexpr int kLowProbThresholdQ10 = static_cast<int>(kHistLowProbabilityThreshold * kProbQDomain);

}  // namespace

LoudnessHistogram::LoudnessHistogram() {
  static_assert(kHistSize == sizeof(kHistBinCenters) / sizeof(kHistBinCenters[0]),
                "histogram bin centers incorrect size");
}

LoudnessHistogram::LoudnessHistogram(int window_size)
    : activity_probability_(static_cast<size_t>(std::max(window_size, 0))),
      hist_bin_index_(static_cast<size_t>(std::max(window_size, 0))),
      len_circular_buffer_(std::max(window_size, 0)) {}

void LoudnessHistogram::Update(double rms, double activity_probaility) {
  // If circular histogram is activated then remove the oldest entry.
  if (len_circular_buffer_ > 0) RemoveOldestEntryAndUpdate();

  // Find the corresponding bin.
  const int hist_index = GetBinIndex(rms);
  // To Q10 domain.
  const int prob_q10 = static_cast<int16_t>(std::floor(activity_probaility * kProbQDomain));
  InsertNewestEntryAndUpdate(prob_q10, hist_index);
}

// Doing nothing if buffer is not full, yet.
void LoudnessHistogram::RemoveOldestEntryAndUpdate() {
  if (!buffer_is_full_) return;
  const auto i = static_cast<size_t>(buffer_index_);
  const int oldest_prob = activity_probability_[i];
  const int oldest_hist_index = hist_bin_index_[i];
  UpdateHist(-oldest_prob, oldest_hist_index);
}

void LoudnessHistogram::RemoveTransient() {
  int index = (buffer_index_ > 0) ? (buffer_index_ - 1) : len_circular_buffer_ - 1;
  while (len_high_activity_ > 0) {
    const auto i = static_cast<size_t>(index);
    UpdateHist(-activity_probability_[i], hist_bin_index_[i]);
    activity_probability_[i] = 0;
    index = (index > 0) ? (index - 1) : (len_circular_buffer_ - 1);
    len_high_activity_--;
  }
}

void LoudnessHistogram::InsertNewestEntryAndUpdate(int activity_prob_q10, int hist_index) {
  // Update the circular buffer if it is enabled.
  if (len_circular_buffer_ > 0) {
    // Removing transient.
    if (activity_prob_q10 <= kLowProbThresholdQ10) {
      // Lower than threshold probability, set it to zero.
      activity_prob_q10 = 0;
      // Check if this has been a transient.
      if (len_high_activity_ <= kHistTransientWidthThreshold) RemoveTransient();  // Remove this transient.
      len_high_activity_ = 0;
    } else if (len_high_activity_ <= kHistTransientWidthThreshold) {
      len_high_activity_++;
    }
    // Updating the circular buffer.
    const auto i = static_cast<size_t>(buffer_index_);
    activity_probability_[i] = activity_prob_q10;
    hist_bin_index_[i] = hist_index;
    // Increment the buffer index and check for wrap-around.
    buffer_index_++;
    if (buffer_index_ >= len_circular_buffer_) {
      buffer_index_ = 0;
      buffer_is_full_ = true;
    }
  }

  num_updates_++;
  if (num_updates_ < 0) num_updates_--;

  UpdateHist(activity_prob_q10, hist_index);
}

void LoudnessHistogram::UpdateHist(int activity_prob_q10, int hist_index) {
  bin_count_q10_[hist_index] += activity_prob_q10;
  audio_content_q10_ += activity_prob_q10;
}

double LoudnessHistogram::AudioContent() const { return static_cast<double>(audio_content_q10_) / kProbQDomain; }

void LoudnessHistogram::Reset() {
  // Reset the histogram, audio-content and number of updates.
  std::memset(bin_count_q10_, 0, sizeof(bin_count_q10_));
  audio_content_q10_ = 0;
  num_updates_ = 0;
  // Empty the circular buffer.
  buffer_index_ = 0;
  buffer_is_full_ = false;
  len_high_activity_ = 0;
}

int LoudnessHistogram::GetBinIndex(double rms) {
  // First exclude overload cases.
  if (rms <= kHistBinCenters[0]) {
    return 0;
  } else if (rms >= kHistBinCenters[kHistSize - 1]) {
    return kHistSize - 1;
  } else {
    // The quantizer is uniform in log domain.
    const double rms_log = std::log(rms);
    const int index = static_cast<int>(std::floor((rms_log - kLogDomainMinBinCenter) * kLogDomainStepSizeInverse));
    // The final decision is in linear domain.
    const double b = 0.5 * (kHistBinCenters[index] + kHistBinCenters[index + 1]);
    if (rms > b) {
      return index + 1;
    }
    return index;
  }
}

double LoudnessHistogram::CurrentRms() const {
  double mean_val = 0;
  if (audio_content_q10_ > 0) {
    const double p_total_inverse = 1. / static_cast<double>(audio_content_q10_);
    for (int n = 0; n < kHistSize; n++) {
      const double p = static_cast<double>(bin_count_q10_[n]) * p_total_inverse;
      mean_val += p * kHistBinCenters[n];
    }
  } else {
    mean_val = kHistBinCenters[0];
  }
  return mean_val;
}

// ---- utility.cc ---------------------------------------------------------------

namespace {
constexpr double kLog10 = std::numbers::ln10;
constexpr double kLinear2DbScale = 20.0 / kLog10;
constexpr double kLinear2LoudnessScale = 13.4 / kLog10;
}  // namespace

double Loudness2Db(double loudness) { return loudness * kLinear2DbScale / kLinear2LoudnessScale; }

double Linear2Loudness(double rms) {
  if (rms == 0) return -15;
  return kLinear2LoudnessScale * std::log(rms);
}

double Db2Loudness(double db) { return db * kLinear2LoudnessScale / kLinear2DbScale; }

double Dbfs2Loudness(double dbfs) { return Db2Loudness(90 + dbfs); }

// ---- agc.cc ---------------------------------------------------------------------

namespace {
constexpr int kDefaultLevelDbfs = -18;
constexpr int kNumAnalysisFrames = 100;
constexpr double kActivityThreshold = 0.3;
}  // namespace

Agc::Agc()
    : target_level_loudness_(Dbfs2Loudness(kDefaultLevelDbfs)),
      target_level_dbfs_(kDefaultLevelDbfs),
      histogram_(kNumAnalysisFrames) {}

void Agc::Process(std::span<const int16_t> audio) {
  vad_.ProcessChunk(audio);
  const std::span<const double> rms = vad_.chunkwise_rms();
  const std::span<const double> probabilities = vad_.chunkwise_voice_probabilities();
  for (size_t i = 0; i < rms.size(); ++i) {
    histogram_.Update(rms[i], probabilities[i]);
  }
}

bool Agc::GetRmsErrorDb(int* error) {
  if (!error) {
    return false;
  }
  if (histogram_.num_updates() < kNumAnalysisFrames) {
    // We haven't yet received enough frames.
    return false;
  }
  if (histogram_.AudioContent() < kNumAnalysisFrames * kActivityThreshold) {
    // We are likely in an inactive segment.
    return false;
  }
  const double loudness = Linear2Loudness(histogram_.CurrentRms());
  *error = static_cast<int>(std::floor(Loudness2Db(target_level_loudness_ - loudness) + 0.5));
  histogram_.Reset();
  return true;
}

void Agc::Reset() { histogram_.Reset(); }

int Agc::set_target_level_dbfs(int level) {
  if (level >= 0 || level <= -100) return -1;
  target_level_dbfs_ = level;
  target_level_loudness_ = Dbfs2Loudness(level);
  return 0;
}

}  // namespace sm::audio::agc1
