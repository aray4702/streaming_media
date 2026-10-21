#include "sm/audio/agc1.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace sm::audio::agc1 {

namespace {

// common_audio/signal_processing (include/spl_inl.h,
// include/signal_processing_library.h, division_operations.c, spl_sqrt.c,
// resample_by_2.c), as the legacy AGC uses them.
int CountLeadingZeros32(uint32_t n) { return std::countl_zero(n); }
int16_t NormW32(int32_t a) {
  return static_cast<int16_t>(a == 0 ? 0 : CountLeadingZeros32(static_cast<uint32_t>(a < 0 ? ~a : a)) - 1);
}
int16_t NormU32(uint32_t a) { return static_cast<int16_t>(a == 0 ? 0 : CountLeadingZeros32(a)); }
int16_t SatW32ToW16(int32_t v) {
  if (v > 32767) return 32767;
  if (v < -32768) return -32768;
  return static_cast<int16_t>(v);
}
int16_t AddSatW16(int16_t a, int16_t b) { return SatW32ToW16(static_cast<int32_t>(a) + static_cast<int32_t>(b)); }
int32_t DivW32W16(int32_t num, int16_t den) { return den != 0 ? num / den : 0x7FFFFFFF; }
int16_t DivW32W16ResW16(int32_t num, int16_t den) { return den != 0 ? static_cast<int16_t>(num / den) : 0x7FFF; }
int32_t MulAccum(int32_t a, int32_t b, int32_t c) {  // WEBRTC_SPL_SCALEDIFF32 (unsigned low part).
  return c + (b >> 16) * a + static_cast<int32_t>((static_cast<uint32_t>(b & 0x0000FFFF) * static_cast<uint32_t>(a)) >> 16);
}
int32_t MulU16(int32_t a, uint16_t b) {  // WEBRTC_SPL_MUL_16_U16.
  return static_cast<int32_t>(static_cast<int16_t>(a)) * b;
}
int32_t ShiftW32(int32_t x, int c) {  // WEBRTC_SPL_SHIFT_W32.
  return c >= 0 ? static_cast<int32_t>(static_cast<uint32_t>(x) << c) : x >> -c;
}

int32_t SqrtLocal(int32_t in) {
  int32_t b = in / 2;
  b = b - 0x40000000;
  const auto x_half = static_cast<int16_t>(b >> 16);
  b = b + 0x40000000;
  b = b + 0x40000000;
  const int32_t x2 = static_cast<int32_t>(x_half) * x_half * 2;
  int32_t a = -x2;
  b = b + (a >> 1);
  a >>= 16;
  a = a * a * 2;
  auto t16 = static_cast<int16_t>(a >> 16);
  b += -20480 * t16 * 2;
  a = x_half * t16 * 2;
  t16 = static_cast<int16_t>(a >> 16);
  b += 28672 * t16 * 2;
  t16 = static_cast<int16_t>(x2 >> 16);
  a = x_half * t16 * 2;
  b = b + (a >> 1);
  b = b + 32768;
  return b;
}

int32_t Sqrt(int32_t value) {
  constexpr int16_t kSqrt2 = 23170;  // 1/sqrt(2).
  int32_t a = value;
  if (a < 0) {
    a = a == INT32_MIN ? INT32_MAX : -a;
  } else if (a == 0) {
    return 0;
  }
  const int16_t sh = NormW32(a);
  a = static_cast<int32_t>(static_cast<uint32_t>(a) << sh);
  a = a < INT32_MAX - 32767 ? a + 32768 : INT32_MAX;
  const auto x_norm = static_cast<int16_t>(a >> 16);
  const int16_t nshift = static_cast<int16_t>(sh / 2);
  a = static_cast<int32_t>(static_cast<uint32_t>(static_cast<int32_t>(x_norm)) << 16);
  a = a >= 0 ? a : -a;
  a = SqrtLocal(a);
  if (2 * nshift == sh) {
    const auto t16 = static_cast<int16_t>(a >> 16);
    a = kSqrt2 * t16 * 2;
    a = a + 32768;
    a = a & 0x7fff0000;
    a >>= 15;
  } else {
    a >>= 16;
  }
  a = a & 0x0000ffff;
  a >>= nshift;
  return a;
}

// WebRtcSpl_DownsampleBy2, the portable version.
constexpr uint16_t kResampleAllpass1[3] = {3284, 24441, 49528};
constexpr uint16_t kResampleAllpass2[3] = {12199, 37471, 60255};

void DownsampleBy2(const int16_t* in, size_t len, int16_t* out, int32_t* st) {
  int32_t s0 = st[0], s1 = st[1], s2 = st[2], s3 = st[3], s4 = st[4], s5 = st[5], s6 = st[6], s7 = st[7];
  for (size_t i = len >> 1; i > 0; --i) {
    int32_t in32 = static_cast<int32_t>(*in++) * (1 << 10);
    int32_t diff = in32 - s1;
    int32_t tmp1 = MulAccum(kResampleAllpass2[0], diff, s0);
    s0 = in32;
    diff = tmp1 - s2;
    int32_t tmp2 = MulAccum(kResampleAllpass2[1], diff, s1);
    s1 = tmp1;
    diff = tmp2 - s3;
    s3 = MulAccum(kResampleAllpass2[2], diff, s2);
    s2 = tmp2;

    in32 = static_cast<int32_t>(*in++) * (1 << 10);
    diff = in32 - s5;
    tmp1 = MulAccum(kResampleAllpass1[0], diff, s4);
    s4 = in32;
    diff = tmp1 - s6;
    tmp2 = MulAccum(kResampleAllpass1[1], diff, s5);
    s5 = tmp1;
    diff = tmp2 - s7;
    s7 = MulAccum(kResampleAllpass1[2], diff, s6);
    s6 = tmp2;

    *out++ = SatW32ToW16((s3 + s7 + 1024) >> 11);
  }
  st[0] = s0, st[1] = s1, st[2] = s2, st[3] = s3, st[4] = s4, st[5] = s5, st[6] = s6, st[7] = s7;
}

// digital_agc.cc.
// Generator table for y = log2(1 + e^x) in Q8.
constexpr int kGenFuncTableSize = 128;
constexpr uint16_t kGenFuncTable[kGenFuncTableSize] = {
    256,   485,   786,   1126,  1484,  1849,  2217,  2586,  2955,  3324,  3693,  4063,  4432,  4801,  5171,  5540,
    5909,  6279,  6648,  7017,  7387,  7756,  8125,  8495,  8864,  9233,  9603,  9972,  10341, 10711, 11080, 11449,
    11819, 12188, 12557, 12927, 13296, 13665, 14035, 14404, 14773, 15143, 15512, 15881, 16251, 16620, 16989, 17359,
    17728, 18097, 18466, 18836, 19205, 19574, 19944, 20313, 20682, 21052, 21421, 21790, 22160, 22529, 22898, 23268,
    23637, 24006, 24376, 24745, 25114, 25484, 25853, 26222, 26592, 26961, 27330, 27700, 28069, 28438, 28808, 29177,
    29546, 29916, 30285, 30654, 31024, 31393, 31762, 32132, 32501, 32870, 33240, 33609, 33978, 34348, 34717, 35086,
    35456, 35825, 36194, 36564, 36933, 37302, 37672, 38041, 38410, 38780, 39149, 39518, 39888, 40257, 40626, 40996,
    41365, 41734, 42104, 42473, 42842, 43212, 43581, 43950, 44320, 44689, 45058, 45428, 45797, 46166, 46536, 46905};

constexpr int16_t kAvgDecayTime = 250;  // Frames.

// C + the 32 most significant bits of A * B: digital_agc.cc's own
// AGC_SCALEDIFF32, signed (SPL's WEBRTC_SPL_SCALEDIFF32 is unsigned).
int32_t AgcScaleDiff32(int32_t a, int32_t b, int32_t c) { return c + (b >> 16) * a + (((0x0000FFFF & b) * a) >> 16); }

// The 32 most significant bits of A(19) * B(26) >> 13.
int32_t AgcMul32(int32_t a, int32_t b) { return (b >> 13) * a + (((0x00001FFF & b) * a) >> 13); }

// analog_agc.cc.
constexpr int kAgcDefaultTargetLevel = 3;
constexpr int kAgcDefaultCompGain = 9;

float FloatS16ToS16Float(float v) { return std::min(32767.f, std::max(-32768.f, v)); }
int16_t FloatS16ToS16(float v) {  // common_audio/include/audio_util.h.
  v = FloatS16ToS16Float(v);
  return static_cast<int16_t>(v + std::copysign(0.5f, v));
}

// gain_control_impl.cc: the sub-frame gains (Q16) on every band, 16 linear
// steps per millisecond, clamped to S16.
void ApplyDigitalGain(const std::array<int32_t, 11>& gains, std::span<const std::span<float>> bands) {
  constexpr float kScaling = 1.f / 65536.f;
  constexpr int kNumSubSections = 16;
  constexpr float kOneByNumSubSections = 1.f / kNumSubSections;
  float gains_scaled[11];
  for (int k = 0; k < 11; ++k) gains_scaled[k] = static_cast<float>(gains[k]) * kScaling;
  for (const std::span<float>& out_band : bands) {
    for (int k = 0, sample = 0; k < 10; ++k) {
      const float delta = (gains_scaled[k + 1] - gains_scaled[k]) * kOneByNumSubSections;
      float gain = gains_scaled[k];
      for (int n = 0; n < kNumSubSections && static_cast<size_t>(sample) < out_band.size(); ++n, ++sample) {
        out_band[sample] *= gain;
        out_band[sample] = std::min(32767.f, std::max(-32768.f, out_band[sample]));
        gain += delta;
      }
    }
  }
}

}  // namespace

void InitVad(AgcVad& state) {
  state.hp_state = 0;
  state.log_ratio = 0;
  state.mean_long_term = 15 << 10;
  state.variance_long_term = 500 << 8;
  state.std_long_term = 0;
  state.mean_short_term = 15 << 10;
  state.variance_short_term = 500 << 8;
  state.std_short_term = 0;
  state.counter = 3;
  state.down_state.fill(0);
}

int16_t ProcessVad(AgcVad& state, const int16_t* in, size_t num_samples) {
  int16_t buf1[8];
  int16_t buf2[4];
  // 10 sub-frames of 1 ms.
  uint32_t nrg = 0;
  int16_t hp_state = state.hp_state;
  for (int subfr = 0; subfr < 10; ++subfr) {
    // Downsample to 4 kHz.
    if (num_samples == 160) {
      for (int k = 0; k < 8; ++k) {
        int32_t tmp32 = static_cast<int32_t>(in[2 * k]) + static_cast<int32_t>(in[2 * k + 1]);
        tmp32 >>= 1;
        buf1[k] = static_cast<int16_t>(tmp32);
      }
      in += 16;
      DownsampleBy2(buf1, 8, buf2, state.down_state.data());
    } else {
      DownsampleBy2(in, 8, buf2, state.down_state.data());
      in += 8;
    }
    // High-pass filter and energy.
    for (int k = 0; k < 4; ++k) {
      const int32_t out = buf2[k] + hp_state;
      const int32_t tmp32 = 600 * out;
      hp_state = static_cast<int16_t>((tmp32 >> 10) - buf2[k]);
      // out * out / 2^6 without overflow.
      nrg += static_cast<uint32_t>(out * (out / (1 << 6)));
      nrg += static_cast<uint32_t>(out * (out % (1 << 6)) / (1 << 6));
    }
  }
  state.hp_state = hp_state;

  // Leading zeros.
  int16_t zeros = (0xFFFF0000 & nrg) ? 0 : 16;
  if (!(0xFF000000 & (nrg << zeros))) zeros += 8;
  if (!(0xF0000000 & (nrg << zeros))) zeros += 4;
  if (!(0xC0000000 & (nrg << zeros))) zeros += 2;
  if (!(0x80000000 & (nrg << zeros))) zeros += 1;

  // Energy level (range {-32..30}), Q10.
  const auto db = static_cast<int16_t>((15 - zeros) * (1 << 11));

  if (state.counter < kAvgDecayTime) ++state.counter;

  // Short-term mean (Q10), variance (Q8) and standard deviation (Q10).
  int32_t tmp32 = state.mean_short_term * 15 + db;
  state.mean_short_term = static_cast<int16_t>(tmp32 >> 4);
  tmp32 = (db * db) >> 12;
  tmp32 += state.variance_short_term * 15;
  state.variance_short_term = tmp32 / 16;
  tmp32 = state.mean_short_term * state.mean_short_term;
  tmp32 = static_cast<int32_t>(static_cast<uint32_t>(state.variance_short_term) << 12) - tmp32;
  state.std_short_term = static_cast<int16_t>(Sqrt(tmp32));

  // Long-term mean (Q10), variance (Q8) and standard deviation (Q10).
  tmp32 = state.mean_long_term * state.counter + db;
  state.mean_long_term = DivW32W16ResW16(tmp32, AddSatW16(state.counter, 1));
  tmp32 = (db * db) >> 12;
  tmp32 += state.variance_long_term * state.counter;
  state.variance_long_term = DivW32W16(tmp32, AddSatW16(state.counter, 1));
  tmp32 = state.mean_long_term * state.mean_long_term;
  tmp32 = static_cast<int32_t>(static_cast<uint32_t>(state.variance_long_term) << 12) - tmp32;
  state.std_long_term = static_cast<int16_t>(Sqrt(tmp32));

  // Voice activity measure (Q10). libwebrtc keeps the int16_t truncation of
  // (dB - meanLongTerm), a known bug of little significance.
  const int16_t tmp16 = 3 << 12;
  tmp32 = tmp16 * static_cast<int16_t>(db - state.mean_long_term);
  tmp32 = DivW32W16(tmp32, state.std_long_term);
  const uint16_t tmp_u16 = 13 << 12;
  const int32_t tmp32b = MulU16(state.log_ratio, tmp_u16);
  int64_t tmp64 = tmp32;
  tmp64 += tmp32b >> 10;
  tmp64 >>= 6;
  tmp64 = std::clamp<int64_t>(tmp64, -2048, 2048);
  state.log_ratio = static_cast<int16_t>(tmp64);
  return state.log_ratio;
}

bool CalculateGainTable(std::array<int32_t, 32>& gain_table, int16_t dig_comp_gain_db, int16_t target_level_dbfs,
                        bool limiter_enable, int16_t analog_target) {
  constexpr uint16_t kLog10 = 54426;    // log2(10), Q14.
  constexpr uint16_t kLog10_2 = 49321;  // 10 * log10(2), Q14.
  constexpr uint16_t kLogE_1 = 23637;   // log2(e), Q14.
  constexpr int16_t kCompRatio = 3;
  constexpr int16_t kLimiterOffset = 0;

  // Maximum digital gain and zero gain level.
  int32_t tmp32no1 = (dig_comp_gain_db - analog_target) * (kCompRatio - 1);
  int16_t tmp16no1 = static_cast<int16_t>(analog_target - target_level_dbfs);
  tmp16no1 = static_cast<int16_t>(tmp16no1 + DivW32W16ResW16(tmp32no1 + (kCompRatio >> 1), kCompRatio));
  const int16_t max_gain = std::max<int16_t>(tmp16no1, static_cast<int16_t>(analog_target - target_level_dbfs));

  // Difference between maximum gain and gain at 0 dBov.
  tmp32no1 = dig_comp_gain_db * (kCompRatio - 1);
  const int16_t diff_gain = DivW32W16ResW16(tmp32no1 + (kCompRatio >> 1), kCompRatio);
  if (diff_gain < 0 || diff_gain >= kGenFuncTableSize) return false;

  // Limiter level and index.
  const auto limiter_lvl_x = static_cast<int16_t>(analog_target - kLimiterOffset);
  const auto limiter_idx =
      static_cast<int16_t>(2 + DivW32W16ResW16(static_cast<int32_t>(limiter_lvl_x) * (1 << 13), kLog10_2 / 2));
  tmp16no1 = DivW32W16ResW16(kLimiterOffset + (kCompRatio >> 1), kCompRatio);
  const int32_t limiter_lvl = target_level_dbfs + tmp16no1;

  // constMaxGain = log2(1 + 2^(log2(e) * diffGain)), Q8.
  const uint16_t const_max_gain = kGenFuncTable[diff_gain];
  // Piecewise linear approximation of the fractional part of 2^x, Q14.
  constexpr int16_t kConstLinApprox = 22817;
  // den = 20 * constMaxGain, Q8.
  const int32_t den = MulU16(20, const_max_gain);

  for (int16_t i = 0; i < 32; ++i) {
    // Scaled input level (compressor).
    auto tmp16 = static_cast<int16_t>((kCompRatio - 1) * (i - 1));  // Q0.
    int32_t tmp32 = MulU16(tmp16, kLog10_2) + 1;                    // Q14.
    int32_t in_level = DivW32W16(tmp32, kCompRatio);                // Q14.
    in_level = static_cast<int32_t>(diff_gain) * (1 << 14) - in_level;
    const auto abs_in_level = static_cast<uint32_t>(in_level >= 0 ? in_level : -in_level);  // Q14.

    // Table lookup with interpolation.
    uint16_t int_part = static_cast<uint16_t>(abs_in_level >> 14);
    uint16_t frac_part = static_cast<uint16_t>(abs_in_level & 0x00003FFF);
    const uint16_t tmp_u16 = static_cast<uint16_t>(kGenFuncTable[int_part + 1] - kGenFuncTable[int_part]);  // Q8.
    uint32_t tmp_u32no1 = static_cast<uint32_t>(tmp_u16) * frac_part;                                       // Q22.
    tmp_u32no1 += static_cast<uint32_t>(kGenFuncTable[int_part]) << 14;                                     // Q22.
    uint32_t log_approx = tmp_u32no1 >> 8;                                                                  // Q14.
    // Negative exponent: log2(1 + 2^-x) = log2(1 + 2^x) - x.
    if (in_level < 0) {
      int zeros = NormU32(abs_in_level);
      int zeros_scale = 0;
      uint32_t tmp_u32no2;
      if (zeros < 15) {
        tmp_u32no2 = abs_in_level >> (15 - zeros);
        tmp_u32no2 = tmp_u32no2 * kLogE_1;
        if (zeros < 9) {
          zeros_scale = 9 - zeros;
          tmp_u32no1 >>= zeros_scale;
        } else {
          tmp_u32no2 >>= zeros - 9;
        }
      } else {
        tmp_u32no2 = abs_in_level * kLogE_1;
        tmp_u32no2 >>= 6;
      }
      log_approx = 0;
      if (tmp_u32no2 < tmp_u32no1) log_approx = (tmp_u32no1 - tmp_u32no2) >> (8 - zeros_scale);
    }
    int32_t num_fix = (max_gain * const_max_gain) * (1 << 6);  // Q14.
    num_fix -= static_cast<int32_t>(log_approx) * diff_gain;   // Q14.

    // Ratio, with num_fix shifted as far as possible and no wrap-around in den.
    int zeros;
    if (num_fix > (den >> 8) || -num_fix > (den >> 8)) {
      zeros = NormW32(num_fix);
    } else {
      zeros = NormW32(den) + 8;
    }
    num_fix = static_cast<int32_t>(static_cast<uint32_t>(num_fix) << zeros);  // Q(14 + zeros).
    tmp32no1 = ShiftW32(den, zeros - 9);                                      // Q(zeros - 1).
    int32_t y32 = num_fix / tmp32no1;                                         // Q15.
    y32 = y32 >= 0 ? (y32 + 1) >> 1 : -((-y32 + 1) >> 1);                    // Rounded, Q14.

    if (limiter_enable && i < limiter_idx) {
      tmp32 = MulU16(i - 1, kLog10_2);  // Q14.
      tmp32 -= limiter_lvl * (1 << 14);
      y32 = DivW32W16(tmp32 + 10, 20);
    }
    if (y32 > 39000) {
      tmp32 = (y32 >> 1) * kLog10 + 4096;  // Q27.
      tmp32 >>= 13;                        // Q14.
    } else {
      tmp32 = y32 * kLog10 + 8192;  // Q28.
      tmp32 >>= 14;                 // Q14.
    }
    tmp32 += 16 << 14;  // Q14, so that the result is Q16.

    // Power.
    if (tmp32 > 0) {
      int_part = static_cast<uint16_t>(static_cast<int16_t>(tmp32 >> 14));
      frac_part = static_cast<uint16_t>(tmp32 & 0x00003FFF);
      int32_t tmp32no2;
      if ((frac_part >> 13) != 0) {
        tmp16 = static_cast<int16_t>((2 << 14) - kConstLinApprox);
        tmp32no2 = (1 << 14) - frac_part;
        tmp32no2 *= tmp16;
        tmp32no2 >>= 13;
        tmp32no2 = (1 << 14) - tmp32no2;
      } else {
        tmp16 = static_cast<int16_t>(kConstLinApprox - (1 << 14));
        tmp32no2 = (frac_part * tmp16) >> 13;
      }
      frac_part = static_cast<uint16_t>(tmp32no2);
      gain_table[i] = (1 << int_part) + ShiftW32(frac_part, int_part - 14);
    } else {
      gain_table[i] = 0;
    }
  }
  return true;
}

void DigitalAgc::Init() {
  // Fixed-digital mode starts at the minimum to find the gain faster.
  capacitor_slow_ = 0;
  capacitor_fast_ = 0;
  gain_ = 65536;
  gate_previous_ = 0;
  InitVad(vad_nearend_);
  InitVad(vad_farend_);
}

void DigitalAgc::AddFarend(const int16_t* in_far, size_t num_samples) {
  ProcessVad(vad_farend_, in_far, num_samples);
}

void DigitalAgc::ComputeDigitalGains(const int16_t* in_near, size_t samples_per_ms, std::array<int32_t, 11>& gains) {
  const size_t l = samples_per_ms;
  int32_t env[10];

  // VAD for the near end, then the far end's.
  int16_t logratio = ProcessVad(vad_nearend_, in_near, l * 10);
  if (vad_farend_.counter > 10) {
    const int32_t tmp32 = 3 * logratio;
    logratio = static_cast<int16_t>((tmp32 - vad_farend_.log_ratio) >> 2);
  }

  // Decay factor from the VAD (thresholds 1.0 and 0, Q10). The long-silence
  // and low-level adjustments apply only in the adaptive modes.
  constexpr int16_t kUpperThr = 1024;
  constexpr int16_t kLowerThr = 0;
  int16_t decay;
  if (logratio > kUpperThr) {
    decay = -65;  // -2^17 / DecayTime.
  } else if (logratio < kLowerThr) {
    decay = 0;
  } else {
    const int32_t tmp32 = (kLowerThr - logratio) * 65;
    decay = static_cast<int16_t>(tmp32 >> 10);
  }

  // Maximum energy per 1 ms sub-frame.
  for (int k = 0; k < 10; ++k) {
    int32_t max_nrg = 0;
    for (size_t n = 0; n < l; ++n) {
      const int32_t nrg = in_near[k * l + n] * in_near[k * l + n];
      max_nrg = std::max(max_nrg, nrg);
    }
    env[k] = max_nrg;
  }

  // Gain per sub-frame.
  int16_t zeros = 0;
  int16_t frac = 0;
  gains[0] = gain_;
  for (int k = 0; k < 10; ++k) {
    // Fast envelope follower: decay time 131 ms.
    capacitor_fast_ = AgcScaleDiff32(-1000, capacitor_fast_, capacitor_fast_);
    if (env[k] > capacitor_fast_) capacitor_fast_ = env[k];
    // Slow envelope follower.
    if (env[k] > capacitor_slow_) {
      capacitor_slow_ = AgcScaleDiff32(500, env[k] - capacitor_slow_, capacitor_slow_);
    } else {
      capacitor_slow_ = AgcScaleDiff32(decay, capacitor_slow_, capacitor_slow_);
    }
    const int32_t cur_level = std::max(capacitor_fast_, capacitor_slow_);
    // Level to gain: piecewise linear in the leading zeros.
    zeros = NormU32(static_cast<uint32_t>(cur_level));
    if (cur_level == 0) zeros = 31;
    int32_t tmp32 = static_cast<int32_t>((static_cast<uint32_t>(cur_level) << zeros) & 0x7FFFFFFF);
    frac = static_cast<int16_t>(tmp32 >> 19);  // Q12.
    tmp32 = static_cast<int32_t>(((gain_table_[zeros - 1] - gain_table_[zeros]) * static_cast<int64_t>(frac)) >> 12);
    gains[k + 1] = gain_table_[zeros] + tmp32;
  }

  // Gate: lower the gain without speech.
  zeros = static_cast<int16_t>((zeros << 9) - (frac >> 3));
  int16_t zeros_fast = NormU32(static_cast<uint32_t>(capacitor_fast_));
  if (capacitor_fast_ == 0) zeros_fast = 31;
  int32_t tmp32 = static_cast<int32_t>((static_cast<uint32_t>(capacitor_fast_) << zeros_fast) & 0x7FFFFFFF);
  zeros_fast = static_cast<int16_t>(zeros_fast << 9);
  zeros_fast = static_cast<int16_t>(zeros_fast - static_cast<int16_t>(tmp32 >> 22));
  auto gate = static_cast<int16_t>(1000 + zeros_fast - zeros - vad_nearend_.std_short_term);
  if (gate < 0) {
    gate_previous_ = 0;
  } else {
    tmp32 = gate_previous_ * 7;
    gate = static_cast<int16_t>((gate + tmp32) >> 3);
    gate_previous_ = gate;
  }
  // gate < 0: no gate; gate > 2500: maximum gate.
  if (gate > 0) {
    const int16_t gain_adj = gate < 2500 ? static_cast<int16_t>((2500 - gate) >> 5) : 0;
    for (int k = 0; k < 10; ++k) {
      if ((gains[k + 1] - gain_table_[0]) > 8388608) {
        tmp32 = (gains[k + 1] - gain_table_[0]) >> 8;  // No wrap-around.
        tmp32 *= 178 + gain_adj;
      } else {
        tmp32 = (gains[k + 1] - gain_table_[0]) * (178 + gain_adj);
        tmp32 >>= 8;
      }
      gains[k + 1] = gain_table_[0] + tmp32;
    }
  }

  // Limit the gain to avoid overload distortion.
  for (int k = 0; k < 10; ++k) {
    // A shift of gains[k + 1] that can be squared without overflow, at
    // least 10 bits.
    zeros = 10;
    if (gains[k + 1] > 47452159) zeros = static_cast<int16_t>(16 - NormW32(gains[k + 1]));
    int32_t gain32 = (gains[k + 1] >> zeros) + 1;
    gain32 *= gain32;
    while (AgcMul32((env[k] >> 12) + 1, gain32) > ShiftW32(32767, 2 * (1 - zeros + 10))) {
      // 253/256: -0.1 dB.
      if (gains[k + 1] > 8388607) {
        gains[k + 1] = (gains[k + 1] / 256) * 253;  // No wrap-around.
      } else {
        gains[k + 1] = (gains[k + 1] * 253) / 256;
      }
      gain32 = (gains[k + 1] >> zeros) + 1;
      gain32 *= gain32;
    }
  }
  // Gain reductions 1 ms earlier than gain increases.
  for (int k = 1; k < 10; ++k) {
    if (gains[k] > gains[k + 1]) gains[k] = gains[k + 1];
  }
  gain_ = gains[10];
}

GainControl::GainControl(GainControlConfig cfg) : cfg_(cfg) {
  // WebRtcAgc_Init: the digital state and the default config (3 dB target,
  // 9 dB compression, limiter); then GainControlImpl::Configure() with ours.
  digital_.Init();
  CalculateGainTable(digital_.gain_table(), kAgcDefaultCompGain + kAgcDefaultTargetLevel, kAgcDefaultTargetLevel,
                     true, kAgcDefaultCompGain + kAgcDefaultTargetLevel);
  cfg_.target_level_dbfs = std::clamp(cfg_.target_level_dbfs, 0, 31);
  cfg_.compression_gain_db = std::clamp(cfg_.compression_gain_db, 0, 90);
  Configure();
}

void GainControl::Configure() {
  // WebRtcAgc_set_config in fixed-digital mode: the compression gain is
  // offset by the target, and the gain table's analog target is that gain.
  const auto compression = static_cast<int16_t>(cfg_.compression_gain_db + cfg_.target_level_dbfs);
  CalculateGainTable(digital_.gain_table(), compression, static_cast<int16_t>(cfg_.target_level_dbfs),
                     cfg_.enable_limiter, compression);
}

bool GainControl::set_target_level_dbfs(int level) {
  if (level > 31 || level < 0) return false;
  cfg_.target_level_dbfs = level;
  Configure();
  return true;
}

bool GainControl::set_compression_gain_db(int gain) {
  if (gain < 0 || gain > 90) return false;
  if (gain != cfg_.compression_gain_db) {
    SM_EVENT(cfg_.events, Timestamp::Micros(0), "agc1", "compression_gain", gain, cfg_.compression_gain_db);
  }
  cfg_.compression_gain_db = gain;
  Configure();
  return true;
}

void GainControl::enable_limiter(bool enable) {
  cfg_.enable_limiter = enable;
  Configure();
}

void GainControl::ProcessRenderAudio(std::span<const int16_t> band0) {
  const size_t expected = cfg_.sample_rate == 8000 ? 80 : 160;
  if (band0.size() != expected) return;  // WebRtcAgc_GetAddFarendError.
  digital_.AddFarend(band0.data(), band0.size());
}

void GainControl::ProcessBands(std::span<const std::span<float>> bands) {
  if (bands.empty()) return;
  const size_t samples_per_ms = cfg_.sample_rate == 8000 ? 8 : 16;
  if (bands[0].size() != samples_per_ms * 10) return;  // WebRtcAgc_Analyze's check.
  band0_s16_.resize(bands[0].size());
  for (size_t i = 0; i < bands[0].size(); ++i) band0_s16_[i] = FloatS16ToS16(bands[0][i]);
  digital_.ComputeDigitalGains(band0_s16_.data(), samples_per_ms, gains_);
  ApplyDigitalGain(gains_, bands);
  ++stats_.frames;
  stats_.gain_db = 20.0f * std::log10(std::max(1, gains_[10]) / 65536.0f);
  stats_.vad_log_ratio = digital_.vad_nearend().log_ratio;
}

void GainControl::Process(std::span<float> frame) {
  s16_.assign(frame.begin(), frame.end());
  for (float& v : s16_) v *= 32768.0f;
  const std::span<float> bands[1] = {std::span<float>(s16_)};
  ProcessBands(bands);
  for (size_t i = 0; i < frame.size(); ++i) frame[i] = s16_[i] / 32768.0f;
}

void GainControl::ExportMetrics(MetricsWriter& w) const {
  w.Counter("frames", stats_.frames);
  w.Gauge("gain_db", stats_.gain_db);
  w.Gauge("compression_gain_db", static_cast<double>(cfg_.compression_gain_db));
  w.Gauge("vad_log_ratio_q10", static_cast<double>(stats_.vad_log_ratio));
}

}  // namespace sm::audio::agc1
