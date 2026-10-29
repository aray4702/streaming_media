#include "sm/timing/neteq_expand.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iterator>
#include <limits>
#include <numeric>

#include "sm/core/dsp.h"

namespace sm::timing {

const int16_t NetEqRandomVector::kRandomTable[NetEqRandomVector::kRandomTableSize] = {
    2680,   5532,   441,    5520,   16170,  -5146,  -1024,  -8733,  3115,   9598,   -10380, -4959,  -1280,  -21716,
    7133,   -1522,  13458,  -3902,  2789,   -675,   3441,   5016,   -13599, -4003,  -2739,  3922,   -7209,  13352,
    -11617, -7241,  12905,  -2314,  5426,   10121,  -9702,  11207,  -13542, 1373,   816,    -5934,  -12504, 4798,
    1811,   4112,   -613,   201,    -10367, -2960,  -2419,  3442,   4299,   -6116,  -6092,  1552,   -1650,  -480,
    -1237,  18720,  -11858, -8303,  -8212,  865,    -2890,  -16968, 12052,  -5845,  -5912,  9777,   -5665,  -6294,
    5426,   -4737,  -6335,  1652,   761,    3832,   641,    -8552,  -9084,  -5753,  8146,   12156,  -4915,  15086,
    -1231,  -1869,  11749,  -9319,  -6403,  11407,  6232,   -1683,  24340,  -11166, 4017,   -10448, 3153,   -2936,
    6212,   2891,   -866,   -404,   -4807,  -2324,  -1917,  -2388,  -6470,  -3895,  -10300, 5323,   -5403,  2205,
    4640,   7022,   -21186, -6244,  -882,   -10031, -3395,  -12885, 7155,   -5339,  5079,   -2645,  -9515,  6622,
    14651,  15852,  359,    122,    8246,   -3502,  -6696,  -3679,  -13535, -1409,  -704,   -7403,  -4007,  1798,
    279,    -420,   -12796, -14219, 1141,   3359,   11434,  7049,   -6684,  -7473,  14283,  -4115,  -9123,  -8969,
    4152,   4117,   13792,  5742,   16168,  8661,   -1609,  -6095,  1881,   14380,  -5588,  6758,   -6425,  -22969,
    -7269,  7031,   1119,   -1611,  -5850,  -11281, 3559,   -8952,  -10146, -4667,  -16251, -1538,  2062,   -1012,
    -13073, 227,    -3142,  -5265,  20,     5770,   -7559,  4740,   -4819,  992,    -8208,  -7130,  -4652,  6725,
    7369,   -1036,  13144,  -1588,  -5304,  -2344,  -449,   -5705,  -8894,  5205,   -17904, -11188, -1022,  4852,
    10101,  -5255,  -4200,  -752,   7941,   -1543,  5959,   14719,  13346,  17045,  -15605, -1678,  -1600,  -9230,
    68,     23348,  1172,   7750,   11212,  -18227, 9956,   4161,   883,    3947,   4341,   1014,   -4889,  -2603,
    1246,   -5630,  -3596,  -870,   -1298,  2784,   -3317,  -6612,  -20541, 4166,   4181,   -8625,  3562,   12890,
    4761,   3205,   -12259, -8579};

void NetEqRandomVector::Generate(size_t length, int16_t* output) {
  for (size_t i = 0; i < length; i++) {
    seed_ += static_cast<uint32_t>(seed_increment_);
    output[i] = kRandomTable[seed_ & (kRandomTableSize - 1)];
  }
}

void NetEqRandomVector::IncreaseSeedIncrement(int16_t increase_by) {
  seed_increment_ = static_cast<int16_t>((seed_increment_ + increase_by) & (kRandomTableSize - 1));
}

namespace {

constexpr float kInt16Scale = 32768.0f;

// DspHelper's downsampling filters to 4 kHz (Q12) and factors.
struct Downsample {
  const int16_t* coefficients;
  size_t num_coefficients;
  size_t factor;
};
constexpr int16_t kDownsample8kHzTbl[3] = {1229, 1638, 1229};
constexpr int16_t kDownsample16kHzTbl[5] = {614, 819, 1229, 819, 614};
constexpr int16_t kDownsample32kHzTbl[7] = {584, 512, 625, 667, 625, 512, 584};
constexpr int16_t kDownsample48kHzTbl[7] = {1019, 390, 427, 440, 427, 390, 1019};

Downsample DownsampleFor(int fs_hz) {
  switch (fs_hz) {
    case 8000: return {kDownsample8kHzTbl, 3, 2};
    case 16000: return {kDownsample16kHzTbl, 5, 4};
    case 32000: return {kDownsample32kHzTbl, 7, 8};
    default: return {kDownsample48kHzTbl, 7, 12};
  }
}

// DspHelper's tapering windows for the overlap-add (Q15).
struct Windows {
  int mute_start, mute_increment, unmute_start, unmute_increment;
};
Windows WindowsFor(int fs_hz) {
  switch (fs_hz) {
    case 8000: return {27307, -5461, 5461, 5461};
    case 16000: return {29789, -2979, 2979, 2979};
    case 32000: return {31208, -1560, 1560, 1560};
    default: return {31711, -1057, 1057, 1057};
  }
}

// DspHelper::ParabolicFit: the vertex of the parabola through three points
// around peak `peak` of a 4 kHz vector, at the full rate (2 * fs_mult samples
// per 4 kHz sample) and at most fs_mult samples from the peak.
void ParabolicFit(float y0, float y1, float y2, size_t peak, int fs_mult, size_t* peak_index, float* peak_value) {
  const float den = y0 - 2 * y1 + y2;
  const size_t scale = 2 * static_cast<size_t>(fs_mult);
  if (den >= 0) {  // Not a maximum: keep the sample.
    *peak_index = peak * scale;
    *peak_value = y1;
    return;
  }
  const float delta = 0.5f * (y0 - y2) / den;  // In 4 kHz samples, |delta| <= 0.5.
  const long offset = std::clamp(std::lround(delta * static_cast<float>(scale)), -static_cast<long>(fs_mult),
                                 static_cast<long>(fs_mult));
  *peak_index = static_cast<size_t>(static_cast<long>(peak * scale) + offset);
  *peak_value = y1 - 0.25f * (y0 - y2) * delta;
}

// DspHelper::PeakDetection: the `num_peaks` largest peaks of `data`, each one
// zeroed with two samples either side before looking for the next.
void PeakDetection(float* data, size_t data_length, size_t num_peaks, int fs_mult, size_t* peak_index, float* peak_value) {
  const bool single_peak = num_peaks == 1;
  const size_t search_length = single_peak ? data_length : data_length - 1;
  const size_t edge_index = search_length - 1;
  for (size_t i = 0; i < num_peaks; i++) {
    const size_t peak = static_cast<size_t>(std::max_element(data, data + search_length) - data);
    const float next = (single_peak && peak == edge_index) ? 0.0f : data[peak + 1];
    if (peak == 0) {
      peak_index[i] = 0;
      peak_value[i] = data[0];
    } else if (peak == edge_index && data[peak] <= next) {
      // Linear approximation.
      peak_value[i] = 0.5f * (data[peak] + next);
      peak_index[i] = (peak * 2 + 1) * static_cast<size_t>(fs_mult);
    } else {
      ParabolicFit(data[peak - 1], data[peak], next, peak, fs_mult, &peak_index[i], &peak_value[i]);
    }
    if (i != num_peaks - 1) {
      const size_t min_index = peak > 2 ? peak - 2 : 0;
      const size_t max_index = std::min(data_length - 1, peak + 2);
      std::fill(data + min_index, data + max_index + 1, 0.0f);
    }
  }
}

// DspHelper::MinDistortion: the lag in [min_lag, max_lag] with the smallest
// sum of absolute differences over `length` samples.
size_t MinDistortion(const float* signal, size_t min_lag, size_t max_lag, size_t length, float* distortion_value) {
  size_t best_index = 0;
  float min_distortion = std::numeric_limits<float>::max();
  for (size_t i = min_lag; i <= max_lag; i++) {
    float sum_diff = 0;
    for (size_t j = 0; j < length; j++) sum_diff += std::fabs(signal[j] - signal[static_cast<std::ptrdiff_t>(j) - static_cast<std::ptrdiff_t>(i)]);
    if (sum_diff < min_distortion) {
      min_distortion = sum_diff;
      best_index = i;
    }
  }
  *distortion_value = min_distortion;
  return best_index;
}

float Dot(const float* a, const float* b, size_t n) {
  double s = 0;
  for (size_t i = 0; i < n; ++i) s += static_cast<double>(a[i]) * b[i];
  return static_cast<float>(s);
}

// WebRtcSpl_NormW32: the left shifts that normalize a 32-bit value.
int NormW32(int32_t a) {
  if (a == 0) return 0;
  const uint32_t v = static_cast<uint32_t>(a < 0 ? ~a : a);
  return std::countl_zero(v) - 1;
}

// WEBRTC_SPL_SHIFT_W32.
int32_t ShiftW32(int32_t x, int c) { return c >= 0 ? static_cast<int32_t>(static_cast<uint32_t>(x) << c) : x >> -c; }

// WebRtcSpl_SqrtFloor.
int32_t SqrtFloor(int32_t value) {
  if (value <= 0) return 0;
  int32_t r = static_cast<int32_t>(std::sqrt(static_cast<double>(value)));
  while (static_cast<int64_t>(r) * r > value) --r;
  while (static_cast<int64_t>(r + 1) * (r + 1) <= value) ++r;
  return r;
}

int16_t SaturateInt16(int64_t v) {
  return static_cast<int16_t>(std::clamp<int64_t>(v, std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max()));
}

int16_t ToInt16(float v) { return SaturateInt16(std::lround(v * kInt16Scale)); }

}  // namespace

void NetEqBackgroundNoise::Reset() {
  initialized_ = false;
  energy_ = 2500;
  max_energy_ = 0;
  energy_update_threshold_ = 500000;
  low_energy_update_threshold_ = 0;
  filter_state_.fill(0);
  filter_.fill(0);
  filter_[0] = 4096;
  mute_factor_ = 0;
  scale_ = 20000;
  scale_shift_ = 24;
}

bool NetEqBackgroundNoise::Update(std::span<const float> sync) {
  int16_t temp_signal_array[kVecLen + kMaxLpcOrder] = {0};
  int16_t* temp_signal = &temp_signal_array[kMaxLpcOrder];
  const size_t have = std::min(kVecLen, sync.size());
  for (size_t i = 0; i < have; ++i) temp_signal[kVecLen - have + i] = ToInt16(sync[sync.size() - have + i]);

  // CalculateAutoCorrelation: CrossCorrelationWithAutoShift with step -1.
  int32_t max_abs = 0;
  for (size_t i = 0; i < kVecLen; ++i) max_abs = std::max(max_abs, std::abs(static_cast<int32_t>(temp_signal[i])));
  const int64_t max_value = static_cast<int64_t>(max_abs) * max_abs * static_cast<int64_t>(kVecLen);
  const int32_t factor = static_cast<int32_t>(max_value >> 31);
  const int correlation_scale = factor == 0 ? 0 : 31 - NormW32(factor);
  int32_t auto_correlation[kMaxLpcOrder + 1];
  for (size_t k = 0; k <= kMaxLpcOrder; ++k) {
    int32_t sum = 0;
    for (size_t i = 0; i < kVecLen; ++i) {
      sum += (static_cast<int32_t>(temp_signal[i]) * temp_signal[static_cast<std::ptrdiff_t>(i) - static_cast<std::ptrdiff_t>(k)]) >>
             correlation_scale;
    }
    auto_correlation[k] = sum;
  }
  constexpr int kLogVecLen = 8;
  const int32_t sample_energy = auto_correlation[0] >> (kLogVecLen - correlation_scale);

  if (sample_energy >= energy_update_threshold_) {
    // Not low enough: raise the threshold by 4x in 4 seconds.
    IncrementEnergyThreshold(sample_energy);
    return false;
  }
  if (auto_correlation[0] <= 0) return false;
  // A low-energy signal was seen, whether or not the filter is updated.
  energy_update_threshold_ = std::max(sample_energy, 1);
  low_energy_update_threshold_ = 0;

  // WebRtcSpl_LevinsonDurbin: only a stable filter is used.
  double r[kMaxLpcOrder + 1], a[kMaxLpcOrder + 1], k[kMaxLpcOrder] = {};
  for (size_t i = 0; i <= kMaxLpcOrder; ++i) r[i] = auto_correlation[i];
  const double err = dsp::LevinsonDurbin(r, static_cast<int>(kMaxLpcOrder), a, k);
  bool stable = err > 0;
  for (double ki : k) stable = stable && std::fabs(ki) < 1;
  if (!stable) return false;
  int16_t lpc_coefficients[kMaxLpcOrder + 1];
  for (size_t i = 0; i <= kMaxLpcOrder; ++i) lpc_coefficients[i] = SaturateInt16(std::lround(a[i] * 4096.0));

  // The gain from the residual energy (WebRtcSpl_FilterMAFastQ12).
  int64_t residual = 0;
  const int16_t* x = temp_signal + kVecLen - kResidualLength;
  for (size_t n = 0; n < kResidualLength; ++n) {
    int32_t o = 0;
    for (size_t j = 0; j <= kMaxLpcOrder; ++j) o += lpc_coefficients[j] * x[static_cast<std::ptrdiff_t>(n) - static_cast<std::ptrdiff_t>(j)];
    const int16_t out = SaturateInt16((static_cast<int64_t>(o) + 2048) >> 12);
    residual += static_cast<int32_t>(out) * out;
  }
  const int32_t residual_energy = static_cast<int32_t>(std::min<int64_t>(residual, std::numeric_limits<int32_t>::max()));

  // Spectral flatness: the residual variance against the signal's.
  if (sample_energy > 0 && int64_t{5} * residual_energy >= int64_t{16} * sample_energy) {
    SaveParameters(lpc_coefficients, temp_signal + kVecLen - kMaxLpcOrder, sample_energy, residual_energy);
    return true;
  }
  return false;
}

void NetEqBackgroundNoise::GenerateBackgroundNoise(const int16_t* random_vector, size_t n, int16_t* out) {
  if (!initialized_) {
    std::fill(out, out + n, int16_t{0});
    return;
  }
  // WebRtcSpl_AffineTransformVector to the noise energy, then
  // WebRtcSpl_FilterARFastQ12 from the saved filter state.
  const int32_t dc_offset = scale_shift_ > 1 ? 1 << (scale_shift_ - 1) : 0;
  std::vector<int16_t> y(kMaxLpcOrder + n);
  std::copy(filter_state_.begin(), filter_state_.end(), y.begin());
  for (size_t i = 0; i < n; ++i) {
    const int16_t scaled = static_cast<int16_t>((random_vector[i] * scale_ + dc_offset) >> scale_shift_);
    int64_t sum = 0;
    for (size_t j = kMaxLpcOrder; j > 0; --j) sum += static_cast<int64_t>(filter_[j]) * y[kMaxLpcOrder + i - j];
    int64_t output = static_cast<int64_t>(filter_[0]) * scaled - sum;
    output = std::clamp<int64_t>(output, -134217728, 134215679);
    y[kMaxLpcOrder + i] = static_cast<int16_t>((output + 2048) >> 12);
  }
  if (n >= kMaxLpcOrder) std::copy(y.end() - kMaxLpcOrder, y.end(), filter_state_.begin());
  // The noise's mute factor (0 from the start of every expand period).
  for (size_t i = 0; i < n; ++i) {
    const int16_t v = y[kMaxLpcOrder + i];
    out[i] = mute_factor_ < 16384 ? static_cast<int16_t>((v * mute_factor_ + 8192) >> 14) : v;
  }
}

void NetEqBackgroundNoise::IncrementEnergyThreshold(int32_t sample_energy) {
  // threshold += (increment * threshold) >> 16 in limited-width operations,
  // as libwebrtc (not bit-exact with the plain product).
  int32_t temp_energy = (kThresholdIncrement * low_energy_update_threshold_) >> 16;
  temp_energy += kThresholdIncrement * (energy_update_threshold_ & 0xFF);
  temp_energy += (kThresholdIncrement * ((energy_update_threshold_ >> 8) & 0xFF)) << 8;
  low_energy_update_threshold_ += temp_energy;
  energy_update_threshold_ += kThresholdIncrement * (energy_update_threshold_ >> 16);
  energy_update_threshold_ += low_energy_update_threshold_ >> 16;
  low_energy_update_threshold_ = low_energy_update_threshold_ & 0x0FFFF;

  // The maximum energy decays by 1/1024 per update; the threshold stays
  // within 60 dB of it.
  max_energy_ = max_energy_ - (max_energy_ >> 10);
  if (sample_energy > max_energy_) max_energy_ = sample_energy;
  const int32_t threshold = (max_energy_ + 524288) >> 20;
  if (threshold > energy_update_threshold_) energy_update_threshold_ = threshold;
}

void NetEqBackgroundNoise::SaveParameters(const int16_t* lpc_coefficients, const int16_t* filter_state,
                                          int32_t sample_energy, int32_t residual_energy) {
  std::copy(lpc_coefficients, lpc_coefficients + kMaxLpcOrder + 1, filter_.begin());
  std::copy(filter_state, filter_state + kMaxLpcOrder, filter_state_.begin());
  energy_ = std::max(sample_energy, 1);
  energy_update_threshold_ = energy_;
  low_energy_update_threshold_ = 0;
  // Normalize the residual energy to 29 or 30 bits before the square root.
  int norm_shift = NormW32(residual_energy) - 1;
  if (norm_shift & 0x1) norm_shift -= 1;
  residual_energy = ShiftW32(residual_energy, norm_shift);
  scale_ = static_cast<int16_t>(SqrtFloor(residual_energy));
  // + 13 for the random table's Q13; kLogResidualLength 6.
  scale_shift_ = static_cast<int16_t>(13 + ((6 + norm_shift) / 2));
  initialized_ = true;
}

int16_t NetEqNormalMuteFactor(std::span<const float> decoded, int fs_hz, int16_t expand_mute_factor,
                              int32_t background_noise_energy) {
  const int fs_mult = fs_hz / 8000;
  const int fs_shift = 30 - NormW32(fs_mult);
  std::vector<int16_t> signal(decoded.size());
  int32_t decoded_max = 0;
  for (size_t i = 0; i < decoded.size(); ++i) {
    signal[i] = ToInt16(decoded[i]);
    decoded_max = std::max(decoded_max, std::abs(static_cast<int32_t>(signal[i])));
  }
  const size_t energy_length = std::min(static_cast<size_t>(fs_mult * 64), signal.size());
  int scaling = std::max(6 + fs_shift - NormW32(decoded_max * decoded_max), 0);
  int32_t energy = 0;
  for (size_t i = 0; i < energy_length; ++i) energy += (static_cast<int32_t>(signal[i]) * signal[i]) >> scaling;
  const int32_t scaled_energy_length = static_cast<int32_t>(energy_length >> scaling);
  energy = scaled_energy_length > 0 ? energy / scaled_energy_length : 0;

  int32_t local_mute_factor = 16384;  // 1.0 in Q14.
  if (energy != 0 && energy > background_noise_energy) {
    // background noise energy / energy in Q14, with energy normalized to 15 bits.
    scaling = NormW32(energy) - 16;
    const int32_t bgn_energy = ShiftW32(background_noise_energy, scaling + 14);
    const int16_t energy_scaled = static_cast<int16_t>(ShiftW32(energy, scaling));
    const int32_t ratio = energy_scaled != 0 ? bgn_energy / energy_scaled : std::numeric_limits<int32_t>::max();
    local_mute_factor = std::min(local_mute_factor, SqrtFloor(ShiftW32(ratio, 14)));
  }
  return static_cast<int16_t>(std::max<int32_t>(expand_mute_factor, local_mute_factor));
}

NetEqExpand::NetEqExpand(int sample_rate)
    : fs_hz_(sample_rate), fs_mult_(sample_rate / 8000), overlap_length_(static_cast<size_t>(5 * sample_rate / 8000)) {
  Reset();
}

void NetEqExpand::Reset() {
  first_expand_ = true;
  consecutive_expands_ = 0;
  max_lag_ = 0;
  expand_vector0_.clear();
  expand_vector1_.clear();
}

void NetEqExpand::GenerateRandomVector(int16_t seed_increment, size_t length, int16_t* random_vector) {
  size_t generated = 0;
  while (generated < length) {
    const size_t n = std::min(length - generated, NetEqRandomVector::kRandomTableSize);
    random_vector_.IncreaseSeedIncrement(seed_increment);
    random_vector_.Generate(n, random_vector + generated);
    generated += n;
  }
}

size_t NetEqExpand::Process(std::span<float> sync, std::vector<float>& out) {
  int16_t random_vector[48000 / 8000 * 120 + 30] = {};
  if (first_expand_) {
    AnalyzeSignal(sync, random_vector);
    first_expand_ = false;
    expand_duration_samples_ = 0;
  } else {
    GenerateRandomVector(2, max_lag_, random_vector);
  }
  UpdateLagIndex();

  // Voiced part: the current lag (plus the overlap) from the expansion vectors.
  const size_t expansion_vector_length = max_lag_ + overlap_length_;
  const size_t current_lag = expand_lags_[static_cast<size_t>(current_lag_index_)];
  const size_t position = expansion_vector_length - current_lag - overlap_length_;
  const size_t temp_length = current_lag + overlap_length_;
  std::vector<float> voiced(temp_length);
  for (size_t i = 0; i < temp_length; ++i) {
    const float v0 = expand_vector0_[position + i], v1 = expand_vector1_[position + i];
    voiced[i] = current_lag_index_ == 0 ? v0 : current_lag_index_ == 1 ? (3 * v0 + v1) / 4 : (v0 + v1) / 2;
  }

  // Overlap-add into the end of the sync buffer, unless muted to a low
  // amplitude or mostly unvoiced.
  if (mute_factor_ > 819 && current_voice_mix_factor_ > 8192 && sync.size() >= overlap_length_) {
    Windows w = WindowsFor(fs_hz_);
    const size_t start = sync.size() - overlap_length_;
    for (size_t i = 0; i < overlap_length_; i++) {
      const float s = sync[start + i] * kInt16Scale;
      const float x = (s * static_cast<float>(w.mute_start) +
                       mute_factor_ * voiced[i] / 16384.0f * static_cast<float>(w.unmute_start)) / 32768.0f;
      sync[start + i] = x / kInt16Scale;
      w.mute_start += w.mute_increment;
      w.unmute_start += w.unmute_increment;
    }
  }
  const float* voiced_vector = voiced.data() + overlap_length_;

  // Unvoiced part: the random vector through the AR filter.
  std::vector<float> unvoiced(current_lag);
  for (size_t i = 0; i < current_lag; ++i) {
    float y = random_vector[i] * ar_gain_;
    for (size_t k = 1; k <= kUnvoicedLpcOrder; ++k) {
      const float prev = i >= k ? unvoiced[i - k] : ar_filter_state_[kUnvoicedLpcOrder + i - k];
      y -= ar_filter_[k] * prev;
    }
    unvoiced[i] = y;
  }
  for (size_t k = 0; k < kUnvoicedLpcOrder; ++k) {
    ar_filter_state_[k] = current_lag >= kUnvoicedLpcOrder ? unvoiced[current_lag - kUnvoicedLpcOrder + k] : 0.0f;
  }

  // Combine voiced and unvoiced, shifting in more of the unvoiced part:
  // from 1 to 0 in about 8, 16 or 32 ms depending on the lag.
  int temp_shift = std::bit_width(max_lag_) - 5;
  int16_t mix_factor_increment = static_cast<int16_t>(256 >> temp_shift);
  if (stop_muting_) mix_factor_increment = 0;
  temp_shift = 8 - temp_shift;
  size_t fade_length = static_cast<size_t>((current_voice_mix_factor_ - voice_mix_factor_) >> temp_shift);
  fade_length = std::min(fade_length, current_lag);
  std::vector<float> temp(current_lag);
  for (size_t i = 0; i < fade_length; ++i) {  // DspHelper::CrossFade.
    temp[i] = (current_voice_mix_factor_ * voiced_vector[i] + (16384 - current_voice_mix_factor_) * unvoiced[i]) / 16384.0f;
    current_voice_mix_factor_ = static_cast<int16_t>(current_voice_mix_factor_ - mix_factor_increment);
  }
  if (fade_length < current_lag) {
    if (mix_factor_increment != 0) current_voice_mix_factor_ = voice_mix_factor_;
    const int scale = 16384 - current_voice_mix_factor_;
    for (size_t i = fade_length; i < current_lag; ++i) {
      temp[i] = (current_voice_mix_factor_ * voiced_vector[i] + scale * unvoiced[i]) / 16384.0f;
    }
  }

  // Muting slope by the number of consecutive expands.
  if (consecutive_expands_ == 3) mute_slope_ = std::max(mute_slope_, 1049 / fs_mult_);  // 1 -> 0.95 in 6.25 ms.
  if (consecutive_expands_ == 7) mute_slope_ = std::max(mute_slope_, 2097 / fs_mult_);  // 1 -> 0.90 in 6.25 ms.

  if (consecutive_expands_ != 0 || !onset_) {
    // Mute to the previous level, then continue with the muting.
    for (float& v : temp) v = v * mute_factor_ / 16384.0f;
    if (!stop_muting_) {
      int32_t factor = (16384 << 6) + 32;  // DspHelper::MuteSignal, Q20.
      for (float& v : temp) {
        v = v * static_cast<float>(factor >> 6) / 16384.0f;
        factor -= mute_slope_;
      }
      int16_t gain = static_cast<int16_t>(16384 - ((static_cast<int>(current_lag) * mute_slope_ + 8192) >> 6));
      gain = static_cast<int16_t>((gain * mute_factor_ + 8192) >> 14);
      // Guard against getting stuck with a very small (but audible) gain.
      mute_factor_ = (consecutive_expands_ > 3 && gain >= mute_factor_) ? 0 : gain;
    }
  }
  // Background noise part.
  int16_t noise[48000 / 8000 * 125];
  background_noise_.GenerateBackgroundNoise(random_vector, current_lag, noise);
  for (size_t i = 0; i < current_lag; ++i) temp[i] += noise[i];

  for (float v : temp) out.push_back(v / kInt16Scale);
  consecutive_expands_ = std::min(consecutive_expands_ + 1, kMaxConsecutiveExpands);
  expand_duration_samples_ = std::min(expand_duration_samples_ + current_lag, static_cast<size_t>(fs_hz_) * 2);
  return current_lag;
}

void NetEqExpand::SetParametersForNormalAfterExpand() {
  current_lag_index_ = 0;
  lag_index_direction_ = 0;
  stop_muting_ = true;
  last_outage_duration_samples_ = expand_duration_samples_;
}

void NetEqExpand::SetParametersForMergeAfterExpand() {
  current_lag_index_ = -1;
  lag_index_direction_ = 1;
  stop_muting_ = true;
}

bool NetEqExpand::Muted() const {
  if (first_expand_ || stop_muting_) return false;
  return mute_factor_ == 0;
}

void NetEqExpand::InitializeForAnExpandPeriod() {
  lag_index_direction_ = 1;
  current_lag_index_ = -1;
  stop_muting_ = false;
  random_vector_.set_seed_increment(1);
  consecutive_expands_ = 0;
  current_voice_mix_factor_ = 16384;
  mute_factor_ = 16384;
  background_noise_.SetMuteFactor(0);  // Start with 0 gain for background noise.
}

void NetEqExpand::UpdateLagIndex() {
  current_lag_index_ += lag_index_direction_;
  if (current_lag_index_ <= 0) lag_index_direction_ = 1;
  if (current_lag_index_ >= kNumLags - 1) lag_index_direction_ = -1;
}

void NetEqExpand::Correlation(std::span<const float> history, float* output) const {
  constexpr size_t kCorrelationStartLag = 10;
  constexpr size_t kNumCorrelationLags = 54;
  constexpr size_t kCorrelationLength = 60;
  constexpr size_t kDownsampledLength = kCorrelationStartLag + kNumCorrelationLags + kCorrelationLength;
  const Downsample d = DownsampleFor(fs_hz_);
  // WebRtcSpl_DownsampleFast with no delay: output k filters the input
  // backwards from sample k * factor of the last kDownsampledLength * factor.
  float downsampled[kDownsampledLength];
  const size_t base = history.size() - kDownsampledLength * d.factor;
  for (size_t k = 0; k < kDownsampledLength; ++k) {
    float s = 0;
    for (size_t j = 0; j < d.num_coefficients; ++j) s += d.coefficients[j] * history[base + k * d.factor - j];
    downsampled[k] = s / 4096.0f;
  }
  const float* seq1 = &downsampled[kDownsampledLength - kCorrelationLength];
  for (size_t k = 0; k < kNumCorrelationLags; ++k) {
    output[k] = Dot(seq1, seq1 - kCorrelationStartLag - k, kCorrelationLength);
  }
}

void NetEqExpand::AnalyzeSignal(std::span<const float> sync, int16_t* random_vector) {
  const size_t fs_mult = static_cast<size_t>(fs_mult_);
  const size_t fs_mult_4 = fs_mult * 4, fs_mult_20 = fs_mult * 20, fs_mult_120 = fs_mult * 120;
  const size_t dist_len = fs_mult * kDistortionLength;
  const size_t lpc_len = fs_mult * kLpcAnalysisLength;
  const size_t signal_length = history_length();

  // The last signal_length samples of the sync buffer, zeros before its start.
  std::vector<float> h(signal_length, 0.0f);
  const size_t have = std::min(signal_length, sync.size());
  for (size_t i = 0; i < have; ++i) h[signal_length - have + i] = sync[sync.size() - have + i] * kInt16Scale;

  InitializeForAnExpandPeriod();

  // Correlation peaks at 4 kHz (the legacy 51 of the 54 lags).
  float correlation[54];
  Correlation(h, correlation);
  size_t best_correlation_index[kNumCorrelationCandidates];
  float best_correlation[kNumCorrelationCandidates];
  PeakDetection(correlation, 51, kNumCorrelationCandidates, fs_mult_, best_correlation_index, best_correlation);
  // The correlation lags start at 2.5 ms.
  for (size_t& i : best_correlation_index) i += fs_mult_20;

  // Distortion around each candidate; keep the best correlation / distortion.
  size_t best_distortion_index[kNumCorrelationCandidates];
  float best_distortion[kNumCorrelationCandidates];
  for (size_t i = 0; i < kNumCorrelationCandidates; i++) {
    const size_t min_index = std::max(fs_mult_20, best_correlation_index[i] - fs_mult_4);
    const size_t max_index = std::min(fs_mult_120 - 1, best_correlation_index[i] + fs_mult_4);
    best_distortion_index[i] = MinDistortion(&h[signal_length - dist_len], min_index, max_index, dist_len, &best_distortion[i]);
  }
  float best_ratio = -std::numeric_limits<float>::max();
  size_t best_index = 0;
  for (size_t i = 0; i < kNumCorrelationCandidates; ++i) {
    float ratio;
    if (best_distortion[i] > 0) {
      ratio = best_correlation[i] / best_distortion[i];
    } else if (best_correlation[i] == 0) {
      ratio = 0;
    } else {
      ratio = std::numeric_limits<float>::max();
    }
    if (ratio > best_ratio) {
      best_index = i;
      best_ratio = ratio;
    }
  }
  const size_t distortion_lag = best_distortion_index[best_index];
  const size_t correlation_lag = best_correlation_index[best_index];
  max_lag_ = std::max(distortion_lag, correlation_lag);

  // The exact best correlation between the two lags.
  const size_t correlation_length = std::max(std::min(distortion_lag + 10, fs_mult_120), 60 * fs_mult);
  const size_t start_index = std::min(distortion_lag, correlation_lag);
  const size_t correlation_lags = (distortion_lag > correlation_lag ? distortion_lag - correlation_lag : correlation_lag - distortion_lag) + 1;
  const float* tail = &h[signal_length - correlation_length];
  size_t best = 0;
  float max_correlation = -std::numeric_limits<float>::max();
  for (size_t k = 0; k < correlation_lags; ++k) {
    const float c = Dot(tail, tail - start_index - k, correlation_length);
    if (c > max_correlation) {
      max_correlation = c;
      best = k;
    }
  }
  best += start_index;
  const float energy1 = Dot(tail, tail, correlation_length);
  const float energy2 = Dot(tail - best, tail - best, correlation_length);
  int corr_coefficient = 0;  // Q14.
  if (energy1 > 0 && energy2 > 0) {
    corr_coefficient = std::min(16384, static_cast<int>(max_correlation / std::sqrt(energy1 * energy2) * 16384.0f));
  }

  // The two expansion vectors: the last max_lag + overlap samples, and the
  // period before them at the same energy.
  const size_t expansion_length = max_lag_ + overlap_length_;
  const float* vector1 = &h[signal_length - expansion_length];
  const float* vector2 = vector1 - distortion_lag;
  const float e1 = Dot(vector1, vector1, expansion_length);
  const float e2 = Dot(vector2, vector2, expansion_length);
  int16_t amplitude_ratio;  // Q13.
  expand_vector0_.assign(vector1, vector1 + expansion_length);
  if (e1 / 4 < e2 && e1 > e2 / 4) {
    const float ratio = std::sqrt(e1 / e2);
    amplitude_ratio = static_cast<int16_t>(ratio * 8192.0f);
    expand_vector1_.resize(expansion_length);
    for (size_t i = 0; i < expansion_length; ++i) expand_vector1_[i] = vector2[i] * ratio;
  } else {
    expand_vector1_ = expand_vector0_;
    amplitude_ratio = (e1 / 4 < e2 || e2 == 0) ? 4096 : 16384;
  }

  // The three lags.
  if (distortion_lag == correlation_lag) {
    expand_lags_ = {distortion_lag, distortion_lag, distortion_lag};
  } else {
    expand_lags_[0] = distortion_lag;
    expand_lags_[1] = (distortion_lag + correlation_lag) / 2;
    expand_lags_[2] = distortion_lag > correlation_lag ? (distortion_lag + correlation_lag - 1) / 2
                                                      : (distortion_lag + correlation_lag + 1) / 2;
  }

  // AR model of the unvoiced part: LPC of the last lpc_len samples, kept only
  // if the filter is stable.
  double r[kUnvoicedLpcOrder + 1];
  dsp::Autocorrelation(std::span<const float>(&h[signal_length - lpc_len], lpc_len), static_cast<int>(kUnvoicedLpcOrder), r);
  if (r[0] > 0) {
    double a[kUnvoicedLpcOrder + 1], k[kUnvoicedLpcOrder] = {};
    const double err = dsp::LevinsonDurbin(r, static_cast<int>(kUnvoicedLpcOrder), a, k);
    bool stable = err > 0;
    for (double ki : k) stable = stable && std::fabs(ki) < 1;
    for (size_t i = 0; i <= kUnvoicedLpcOrder; ++i) ar_filter_[i] = stable ? static_cast<float>(a[i]) : (i == 0 ? 1.0f : 0.0f);
  }

  // The noise segment for the first expansion.
  const size_t noise_length = distortion_lag < 40 ? 2 * distortion_lag + 30 : distortion_lag + 30;
  if (noise_length <= NetEqRandomVector::kRandomTableSize) {
    std::copy(NetEqRandomVector::kRandomTable, NetEqRandomVector::kRandomTable + noise_length, random_vector);
  } else {
    std::copy(NetEqRandomVector::kRandomTable, NetEqRandomVector::kRandomTable + NetEqRandomVector::kRandomTableSize,
              random_vector);
    random_vector_.IncreaseSeedIncrement(2);
    random_vector_.Generate(noise_length - NetEqRandomVector::kRandomTableSize,
                            random_vector + NetEqRandomVector::kRandomTableSize);
  }

  // Filter state and gain of the unvoiced part, from the residual of the last
  // 128 samples.
  for (size_t i = 0; i < kUnvoicedLpcOrder; ++i) ar_filter_state_[i] = h[signal_length - kUnvoicedLpcOrder + i];
  double residual_energy = 0;
  for (size_t n = signal_length - 128; n < signal_length; ++n) {
    double e = 0;
    for (size_t j = 0; j <= kUnvoicedLpcOrder; ++j) e += static_cast<double>(ar_filter_[j]) * h[n - j];
    residual_energy += e * e;
  }
  ar_gain_ = static_cast<float>(std::sqrt(residual_energy / 128.0) / 8192.0);

  // voice_mix_factor from the correlation coefficient x:
  // (-5179 + 19931x - 16422x^2 + 5776x^3) / 4096 above x = 0.48, else 0.
  if (corr_coefficient > 7875) {
    const int16_t x1 = static_cast<int16_t>(corr_coefficient);
    const int16_t x2 = static_cast<int16_t>((x1 * x1) >> 14);
    const int16_t x3 = static_cast<int16_t>((x1 * x2) >> 14);
    static constexpr int kCoefficients[4] = {-5179, 19931, -16422, 5776};
    int32_t sum = kCoefficients[0] * 16384 + kCoefficients[1] * x1 + kCoefficients[2] * x2 + kCoefficients[3] * x3;
    voice_mix_factor_ = static_cast<int16_t>(std::max(0, std::min(sum / 4096, 16384)));
  } else {
    voice_mix_factor_ = 0;
  }

  // Muting slope from the energy ratio of the two periods (Q20 per sample).
  const int16_t slope = amplitude_ratio;
  if (slope > 12288) {
    // (1 - 1 / slope) / distortion_lag: an onset.
    const int32_t product = static_cast<int32_t>(distortion_lag) * slope >> 8;
    const int16_t denom = static_cast<int16_t>(std::min<int32_t>(product, std::numeric_limits<int16_t>::max()));
    const int64_t temp_ratio = denom > 0 ? ((slope - 8192) << 12) / denom : std::numeric_limits<int32_t>::max();
    mute_slope_ = static_cast<int>(slope > 14746 ? (temp_ratio + 1) / 2 : (temp_ratio + 4) / 8);
    onset_ = true;
  } else {
    // (1 - slope) / distortion_lag.
    mute_slope_ = ((8192 - slope) * 128) / static_cast<int>(distortion_lag);
    if (voice_mix_factor_ <= 13107) {
      // From 1.0 to 0.9 in no more than 6.25 ms.
      mute_slope_ = std::max(5243 / fs_mult_, mute_slope_);
    } else if (slope > 8028) {
      mute_slope_ = 0;
    }
    onset_ = false;
  }
}

namespace {

// WebRtcSpl_MaxAbsValueW16.
int16_t MaxAbsValueW16(const int16_t* v, size_t n) {
  int32_t m = 0;
  for (size_t i = 0; i < n; ++i) m = std::max(m, std::abs(static_cast<int32_t>(v[i])));
  return static_cast<int16_t>(std::min<int32_t>(m, std::numeric_limits<int16_t>::max()));
}

// WebRtcSpl_DotProductWithScale.
int32_t DotProductWithScale(const int16_t* a, const int16_t* b, size_t n, int scaling) {
  int64_t sum = 0;
  for (size_t i = 0; i < n; ++i) sum += (static_cast<int32_t>(a[i]) * b[i]) >> scaling;
  return static_cast<int32_t>(
      std::clamp<int64_t>(sum, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
}

// WebRtcSpl_DownsampleFast with no delay: output k filters `in` backwards
// from sample k * factor (`in` starts num_coefficients - 1 samples in).
void DownsampleFast(const float* in, float* out, size_t out_length, const Downsample& d) {
  for (size_t k = 0; k < out_length; ++k) {
    float s = 0;
    for (size_t j = 0; j < d.num_coefficients; ++j) s += d.coefficients[j] * in[k * d.factor - j];
    out[k] = s / 4096.0f;
  }
}

}  // namespace

NetEqMerge::NetEqMerge(int sample_rate)
    : fs_hz_(sample_rate), fs_mult_(sample_rate / 8000), timestamps_per_call_(static_cast<size_t>(sample_rate / 100)) {}

int16_t NetEqMerge::SignalScaling(const int16_t* input, size_t input_length, const int16_t* expanded, int fs_mult) {
  const size_t mod_input_length = std::min<size_t>(64 * static_cast<size_t>(fs_mult), input_length);
  if (mod_input_length == 0) return 16384;
  const int32_t length_divisor = std::numeric_limits<int32_t>::max() / static_cast<int32_t>(mod_input_length);
  const int32_t expanded_max = MaxAbsValueW16(expanded, mod_input_length);
  int32_t factor = (expanded_max * expanded_max) / length_divisor;
  const int expanded_shift = factor == 0 ? 0 : 31 - NormW32(factor);
  int32_t energy_expanded = DotProductWithScale(expanded, expanded, mod_input_length, expanded_shift);

  const int32_t input_max = MaxAbsValueW16(input, mod_input_length);
  factor = (input_max * input_max) / length_divisor;
  const int input_shift = factor == 0 ? 0 : 31 - NormW32(factor);
  int32_t energy_input = DotProductWithScale(input, input, mod_input_length, input_shift);

  // Align to the same Q-domain.
  if (input_shift > expanded_shift) {
    energy_expanded = energy_expanded >> (input_shift - expanded_shift);
  } else {
    energy_input = energy_input >> (expanded_shift - input_shift);
  }
  if (energy_input <= energy_expanded) return 16384;  // The expansion is the louder.
  // energy_input to 14 bits, energy_expanded 14 higher: the ratio in Q14.
  const int temp_shift = NormW32(energy_input) - 17;
  energy_input = ShiftW32(energy_input, temp_shift);
  energy_expanded = ShiftW32(energy_expanded, temp_shift + 14);
  return static_cast<int16_t>(SqrtFloor(static_cast<int32_t>(static_cast<uint32_t>(energy_expanded / energy_input) << 14)));
}

void NetEqMerge::Downsample(const float* input, size_t input_length, const float* expanded, size_t expanded_length) {
  const struct Downsample d = DownsampleFor(fs_hz_);
  const size_t signal_offset = d.num_coefficients - 1;
  const size_t length_limit = static_cast<size_t>(fs_hz_ / 100);  // 10 ms.
  (void)expanded_length;
  DownsampleFast(&expanded[signal_offset], expanded_downsampled_, kExpandDownsampLength, d);
  if (input_length <= length_limit) {
    // Too short: downsample what there is and zero the rest (no samples at
    // all if the input is shorter than the filter's offset).
    const size_t temp_len = input_length > signal_offset ? input_length - signal_offset : 0;
    const size_t downsamp_temp_len = temp_len / d.factor;
    if (downsamp_temp_len > 0) DownsampleFast(&input[signal_offset], input_downsampled_, downsamp_temp_len, d);
    std::fill(input_downsampled_ + downsamp_temp_len, input_downsampled_ + kInputDownsampLength, 0.0f);
  } else {
    DownsampleFast(&input[signal_offset], input_downsampled_, kInputDownsampLength, d);
  }
}

size_t NetEqMerge::CorrelateAndPeakSearch(size_t start_position, size_t input_length, size_t expand_period,
                                          const NetEqExpand& expand) const {
  const size_t fs_mult_2 = static_cast<size_t>(fs_mult_) * 2;
  const size_t stop_position_downsamp = std::min(kMaxCorrelationLength, expand.max_lag() / fs_mult_2 + 1);
  // Unnormalized correlation, with zeros either side for the peak search.
  const size_t pad_length = expand.overlap_length() - 1;
  std::vector<float> correlation(2 * pad_length + kMaxCorrelationLength + 1, 0.0f);
  float* correlation_ptr = &correlation[pad_length];
  for (size_t k = 0; k < stop_position_downsamp; ++k) {
    correlation_ptr[k] = Dot(input_downsampled_, expanded_downsampled_ + k, kInputDownsampLength);
  }
  // The peak must leave the sync buffer with 10 ms plus the overlap, and
  // with all of its old samples.
  size_t start_index = std::max(start_position, timestamps_per_call_ + expand.overlap_length());
  start_index = input_length > start_index ? 0 : start_index - input_length;
  const size_t start_index_downsamp = start_index / fs_mult_2;
  const size_t modified_stop_pos = std::min(
      stop_position_downsamp, kMaxCorrelationLength + pad_length - std::min(start_index_downsamp, kMaxCorrelationLength + pad_length));
  size_t best_correlation_index = 0;
  float best_correlation = 0;
  if (modified_stop_pos > 0) {
    PeakDetection(&correlation_ptr[start_index_downsamp], modified_stop_pos, 1, fs_mult_, &best_correlation_index,
                  &best_correlation);
  }
  best_correlation_index += start_index;
  // Should not happen thanks to the start index; jump one lag ahead if it does.
  while (best_correlation_index + input_length < timestamps_per_call_ + expand.overlap_length() ||
         best_correlation_index + input_length < start_position) {
    best_correlation_index += std::max<size_t>(1, expand_period);
  }
  return best_correlation_index;
}

size_t NetEqMerge::Process(std::span<const float> input, std::vector<float>& sync, size_t future_length,
                           NetEqExpand& expand, std::vector<float>& out) {
  if (input.empty()) return 0;
  const size_t input_length = input.size();

  // GetExpandedSignal.
  old_length_ = std::min(future_length, sync.size());
  expand.SetParametersForMergeAfterExpand();
  if (old_length_ >= kMaxOldLength) {
    const size_t length_diff = old_length_ - kMaxOldLength;
    sync.resize(sync.size() - length_diff);
    sync.insert(sync.end() - static_cast<std::ptrdiff_t>(kMaxOldLength), length_diff, 0.0f);
    old_length_ = kMaxOldLength;
  }
  expanded_temp_.clear();
  const size_t expand_period = expand.Process(sync, expanded_temp_);
  expanded_.assign(sync.end() - static_cast<std::ptrdiff_t>(old_length_), sync.end());
  // Repeat the expansion period to have enough to correlate (not
  // interpolate) with.
  const size_t required_length = static_cast<size_t>((120 + 80 + 2) * fs_mult_);
  if (expanded_.size() < required_length) {
    while (expanded_.size() < required_length && !expanded_temp_.empty()) {
      expanded_.insert(expanded_.end(), expanded_temp_.begin(), expanded_temp_.end());
    }
    expanded_.resize(required_length, 0.0f);
  }
  const size_t expanded_length = required_length;

  // On the int16 scale.
  std::vector<float> input_channel(input_length);
  std::vector<int16_t> input16(input_length), expanded16(expanded_length);
  for (size_t i = 0; i < input_length; ++i) {
    input_channel[i] = input[i] * kInt16Scale;
    input16[i] = ToInt16(input[i]);
  }
  std::vector<float> expanded_channel(expanded_.size());
  for (size_t i = 0; i < expanded_.size(); ++i) expanded_channel[i] = expanded_[i] * kInt16Scale;
  for (size_t i = 0; i < expanded_length; ++i) expanded16[i] = ToInt16(expanded_[i]);

  const int16_t new_mute_factor =
      std::min<int16_t>(16384, SignalScaling(input16.data(), input_length, expanded16.data(), fs_mult_));
  Downsample(input_channel.data(), input_length, expanded_channel.data(), expanded_length);
  best_correlation_index_ = CorrelateAndPeakSearch(old_length_, input_length, expand_period, expand);
  const size_t best = best_correlation_index_;

  temp_data_.assign(input_length + best, 0.0f);
  float* decoded_output = temp_data_.data() + best;
  // The part of the decoded audio that overlaps the expansion.
  size_t interpolation_length =
      std::min(kMaxCorrelationLength * static_cast<size_t>(fs_mult_), expanded_length > best ? expanded_length - best : 0);
  interpolation_length = std::min(interpolation_length, input_length);

  int mute_factor = std::max<int>(expand.MuteFactor(), new_mute_factor);
  mute_factor_ = static_cast<int16_t>(mute_factor);
  if (mute_factor < 16384) {
    // 0.004 for NB, 0.002 for WB and so on (Q20 per sample), or as fast as
    // it takes to be back at full gain within the frame.
    const int back_to_fullscale_inc = ((16384 - mute_factor) << 6) / static_cast<int>(input_length);
    const int increment = std::max(4194 / fs_mult_, back_to_fullscale_inc);
    // DspHelper::RampSignal in place over the interpolation.
    int factor_q20 = (mute_factor << 6) + 32;
    for (size_t i = 0; i < interpolation_length; ++i) {
      input_channel[i] = static_cast<float>(mute_factor) * input_channel[i] / 16384.0f;
      factor_q20 = std::max(factor_q20 + increment, 0);
      mute_factor = std::min(factor_q20 >> 6, 16384);
    }
    // DspHelper::UnmuteSignal over the rest.
    int32_t factor_32b = (mute_factor << 6) + 32;
    for (size_t i = interpolation_length; i < input_length; ++i) {
      decoded_output[i] = static_cast<float>(mute_factor) * input_channel[i] / 16384.0f;
      factor_32b = std::max(factor_32b + increment, 0);
      mute_factor = std::min(16384, factor_32b >> 6);
    }
  } else {
    std::copy(input_channel.begin() + static_cast<std::ptrdiff_t>(interpolation_length), input_channel.end(),
              decoded_output + interpolation_length);
  }

  // Overlap and mix linearly (DspHelper::CrossFade from the expansion).
  const int16_t increment = static_cast<int16_t>(16384 / (interpolation_length + 1));  // Q14.
  int16_t local_mute_factor = static_cast<int16_t>(16384 - increment);
  std::copy(expanded_channel.begin(), expanded_channel.begin() + static_cast<std::ptrdiff_t>(std::min(best, expanded_channel.size())),
            temp_data_.begin());
  for (size_t i = 0; i < interpolation_length; ++i) {
    decoded_output[i] = (static_cast<float>(local_mute_factor) * expanded_channel[best + i] +
                         static_cast<float>(16384 - local_mute_factor) * input_channel[i]) / 16384.0f;
    local_mute_factor = static_cast<int16_t>(local_mute_factor - increment);
  }

  // The first old_length samples replace the unplayed part of the sync
  // buffer; the rest is new.
  const size_t output_length = best + input_length;
  for (size_t i = 0; i < old_length_; ++i) {
    sync[sync.size() - old_length_ + i] = temp_data_[i] / kInt16Scale;
  }
  for (size_t i = old_length_; i < output_length; ++i) out.push_back(temp_data_[i] / kInt16Scale);
  return output_length - old_length_;
}


namespace {

// WebRtcSpl_MaxAbsValueW32.
int32_t MaxAbsValueW32(const int32_t* v, size_t n) {
  uint32_t m = 0;
  for (size_t i = 0; i < n; ++i) {
    const uint32_t a = v[i] < 0 ? 0u - static_cast<uint32_t>(v[i]) : static_cast<uint32_t>(v[i]);
    m = std::max(m, a);
  }
  return static_cast<int32_t>(std::min<uint32_t>(m, std::numeric_limits<int32_t>::max()));
}

// WebRtcSpl_DivW32W16.
int32_t DivW32W16(int32_t num, int16_t den) { return den != 0 ? num / den : 0x7FFFFFFF; }

// DspHelper::kParabolaCoefficients.
constexpr int16_t kParabolaCoefficients[17][3] = {
    {120, 32, 64},   {140, 44, 75},   {150, 50, 80},   {160, 57, 85},   {180, 72, 96},   {200, 89, 107},
    {210, 98, 112},  {220, 108, 117}, {240, 128, 128}, {260, 150, 139}, {270, 162, 144}, {280, 174, 149},
    {300, 200, 160}, {320, 228, 171}, {330, 242, 176}, {340, 257, 181}, {360, 288, 192}};

// DspHelper::ParabolicFit in fixed point: the vertex of the parabola through
// three points around the peak of a 4 kHz vector, at the full rate (2 *
// fs_mult samples per 4 kHz sample), as a table lookup. `peak_index` holds
// the 4 kHz peak on entry.
void ParabolicFitW16(const int16_t* signal_points, int fs_mult, size_t* peak_index, int16_t* peak_value) {
  uint16_t fit_index[13];
  if (fs_mult == 1) {
    const uint16_t f[] = {0, 8, 16};
    std::copy(std::begin(f), std::end(f), fit_index);
  } else if (fs_mult == 2) {
    const uint16_t f[] = {0, 4, 8, 12, 16};
    std::copy(std::begin(f), std::end(f), fit_index);
  } else if (fs_mult == 4) {
    const uint16_t f[] = {0, 2, 4, 6, 8, 10, 12, 14, 16};
    std::copy(std::begin(f), std::end(f), fit_index);
  } else {
    const uint16_t f[] = {0, 1, 3, 4, 5, 7, 8, 9, 11, 12, 13, 15, 16};
    std::copy(std::begin(f), std::end(f), fit_index);
  }
  const int32_t num = (signal_points[0] * -3) + (signal_points[1] * 4) - signal_points[2];
  const int32_t den = signal_points[0] + (signal_points[1] * -2) + signal_points[2];
  const int32_t temp = num * 120;
  int flag = 1;
  const int16_t stp = static_cast<int16_t>(kParabolaCoefficients[fit_index[fs_mult]][0] -
                                           kParabolaCoefficients[fit_index[fs_mult - 1]][0]);
  const int16_t strt = static_cast<int16_t>(
      (kParabolaCoefficients[fit_index[fs_mult]][0] + kParabolaCoefficients[fit_index[fs_mult - 1]][0]) / 2);
  int16_t lmt;
  if (temp < -den * strt) {
    lmt = static_cast<int16_t>(strt - stp);
    while (flag) {
      if ((flag == fs_mult) || (temp > -den * lmt)) {
        *peak_value = static_cast<int16_t>((den * kParabolaCoefficients[fit_index[fs_mult - flag]][1] +
                                            num * kParabolaCoefficients[fit_index[fs_mult - flag]][2] +
                                            signal_points[0] * 256) /
                                           256);
        *peak_index = *peak_index * 2 * static_cast<size_t>(fs_mult) - static_cast<size_t>(flag);
        flag = 0;
      } else {
        flag++;
        lmt = static_cast<int16_t>(lmt - stp);
      }
    }
  } else if (temp > -den * (strt + stp)) {
    lmt = static_cast<int16_t>(strt + 2 * stp);
    while (flag) {
      if ((flag == fs_mult) || (temp < -den * lmt)) {
        *peak_value = static_cast<int16_t>((den * kParabolaCoefficients[fit_index[fs_mult + flag]][1] +
                                            num * kParabolaCoefficients[fit_index[fs_mult + flag]][2] +
                                            signal_points[0] * 256) /
                                           256);
        *peak_index = *peak_index * 2 * static_cast<size_t>(fs_mult) + static_cast<size_t>(flag);
        flag = 0;
      } else {
        flag++;
        lmt = static_cast<int16_t>(lmt + stp);
      }
    }
  } else {
    *peak_value = signal_points[1];
    *peak_index = *peak_index * 2 * static_cast<size_t>(fs_mult);
  }
}

// DspHelper::PeakDetection with one peak (RefinePeak), in fixed point: the
// first largest sample (WebRtcSpl_MaxIndexW16), refined by ParabolicFitW16;
// a peak on the last sample has a zero past it, and is placed half a 4 kHz
// sample to the right when it isn't above that zero.
void PeakDetectionW16(const int16_t* data, size_t data_length, int fs_mult, size_t* peak_index, int16_t* peak_value) {
  const size_t edge_index = data_length - 1;
  size_t peak = 0;
  int16_t maximum = std::numeric_limits<int16_t>::min();
  for (size_t i = 0; i < data_length; ++i) {
    if (data[i] > maximum) {
      maximum = data[i];
      peak = i;
    }
  }
  const int16_t next = peak == edge_index ? 0 : data[peak + 1];
  *peak_index = peak;
  if (peak == 0) {
    *peak_value = data[0];
    *peak_index = 0;
  } else if (peak == edge_index) {
    if (data[peak] > next) {
      const int16_t points[3] = {data[peak - 1], data[peak], next};
      ParabolicFitW16(points, fs_mult, peak_index, peak_value);
    } else {
      *peak_value = std::midpoint(data[peak], next);
      *peak_index = (peak * 2 + 1) * static_cast<size_t>(fs_mult);
    }
  } else {
    ParabolicFitW16(&data[peak - 1], fs_mult, peak_index, peak_value);
  }
}

// AudioVector::CrossFade on int16 samples: the `n` samples of `fade_out`
// (the last `n` of `out`) fade into `fade_in` with `fade_out`'s weight
// falling from 16384 in steps of 16384 / (n + 1), Q14 rounded. The result
// replaces the last `n` samples of `out`, on the [-1, 1) scale.
void CrossFade(std::vector<float>& out, const int16_t* fade_out, const int16_t* fade_in, size_t n) {
  n = std::min(n, out.size());
  const int alpha_step = 16384 / (static_cast<int>(n) + 1);
  int alpha = 16384;
  const size_t position = out.size() - n;
  for (size_t i = 0; i < n; ++i) {
    alpha -= alpha_step;
    const int16_t v = static_cast<int16_t>((alpha * fade_out[i] + (16384 - alpha) * fade_in[i] + 8192) >> 14);
    out[position + i] = static_cast<float>(v) / kInt16Scale;
  }
}

}  // namespace

NetEqTimeStretch::NetEqTimeStretch(int sample_rate) : fs_hz_(sample_rate), fs_mult_(sample_rate / 8000) {}

void NetEqTimeStretch::Analyze(std::span<const float> input, const NetEqBackgroundNoise& background_noise,
                               bool preemptive, size_t old_data_length) {
  const size_t fs_mult = static_cast<size_t>(fs_mult_);
  const size_t fs_mult_120 = fs_mult * 120;  // 15 ms.
  const size_t signal_len = input.size();
  signal_.resize(signal_len);
  for (size_t i = 0; i < signal_len; ++i) signal_[i] = ToInt16(input[i]);
  const int16_t* signal = signal_.data();
  const int16_t max_input_value = MaxAbsValueW16(signal, signal_len);

  // DspHelper::DownsampleTo4kHz with the delay compensated: WebRtcSpl_DownsampleFast
  // from sample filter_length - 1, output k at sample delay + k * factor.
  const Downsample d = DownsampleFor(fs_hz_);
  const size_t filter_delay = (d.num_coefficients - 1) / 2 + 1;
  const int16_t* in = signal + d.num_coefficients - 1;
  for (size_t k = 0; k < kDownsampledLen; ++k) {
    const size_t i = filter_delay + k * d.factor;
    int32_t s = 2048;  // 0.5 in Q12.
    for (size_t j = 0; j < d.num_coefficients; ++j) s += d.coefficients[j] * in[i - j];
    downsampled_input_[k] = SaturateInt16(s >> 12);
  }
  AutoCorrelation();

  // The strongest correlation peak.
  size_t peak_index = 0;
  int16_t peak_value = 0;
  PeakDetectionW16(auto_correlation_, kCorrelationLen, fs_mult_, &peak_index, &peak_value);
  // AutoCorrelation starts at kMinLag (4 kHz).
  peak_index += kMinLag * fs_mult * 2;  // At most 119 * fs_mult, under 15 ms.

  // Scaling so that `peak_index` squared samples sum without overflow.
  int scaling = 31 - NormW32(static_cast<int32_t>(max_input_value) * max_input_value) -
                NormW32(static_cast<int32_t>(peak_index));
  scaling = std::max(0, scaling);
  // `vec1` starts at 15 ms minus one pitch period, `vec2` at 15 ms.
  const int16_t* vec1 = &signal[fs_mult_120 - peak_index];
  const int16_t* vec2 = &signal[fs_mult_120];
  const int32_t vec1_energy = DotProductWithScale(vec1, vec1, peak_index, scaling);
  const int32_t vec2_energy = DotProductWithScale(vec2, vec2, peak_index, scaling);
  int32_t cross_corr = DotProductWithScale(vec1, vec2, peak_index, scaling);

  active_speech_ = SpeechDetection(vec1_energy, vec2_energy, peak_index, scaling, background_noise);
  if (!active_speech_) {
    // SetParametersForPassiveSpeech: the correlation doesn't matter.
    best_correlation_ = 0;
    if (preemptive) peak_index = std::min(peak_index, signal_len - old_data_length);
  } else {
    // cross_corr / sqrt(vec1_energy * vec2_energy) in Q14.
    int energy1_scale = std::max(0, 16 - NormW32(vec1_energy));
    int energy2_scale = std::max(0, 16 - NormW32(vec2_energy));
    if ((energy1_scale + energy2_scale) & 1) energy1_scale += 1;  // Even total, for the sqrt.
    const int16_t vec1_energy_int16 = static_cast<int16_t>(vec1_energy >> energy1_scale);
    const int16_t vec2_energy_int16 = static_cast<int16_t>(vec2_energy >> energy2_scale);
    const int16_t sqrt_energy_prod =
        static_cast<int16_t>(SqrtFloor(static_cast<int32_t>(vec1_energy_int16) * vec2_energy_int16));
    const int temp_scale = 14 - (energy1_scale + energy2_scale) / 2;
    cross_corr = ShiftW32(cross_corr, temp_scale);
    cross_corr = std::max(0, cross_corr);  // Not used if negative.
    best_correlation_ = static_cast<int16_t>(DivW32W16(cross_corr, sqrt_energy_prod));
    best_correlation_ = std::min<int16_t>(16384, best_correlation_);
  }
  peak_index_ = peak_index;
}

void NetEqTimeStretch::AutoCorrelation() {
  // CrossCorrelationWithAutoShift from lag kMinLag to kMaxLag - 1 at 4 kHz.
  const int16_t* seq1 = &downsampled_input_[kMaxLag];
  const int16_t* seq2 = &downsampled_input_[kMaxLag - kMinLag];
  const size_t correlation_length = kMaxLag - kMinLag;
  int32_t max_1 = 0, max_2 = 0;
  for (size_t j = 0; j < kCorrelationLen; ++j) max_1 = std::max(max_1, std::abs(static_cast<int32_t>(seq1[j])));
  const int16_t* seq2_start = seq2 - (correlation_length - 1);
  for (size_t j = 0; j < kCorrelationLen + correlation_length - 1; ++j) {
    max_2 = std::max(max_2, std::abs(static_cast<int32_t>(seq2_start[j])));
  }
  const int64_t max_value = static_cast<int64_t>(max_1) * max_2 * static_cast<int64_t>(kCorrelationLen);
  const int32_t factor = static_cast<int32_t>(max_value >> 31);
  const int shift = factor == 0 ? 0 : 31 - NormW32(factor);
  int32_t auto_corr[kCorrelationLen];
  for (size_t i = 0; i < correlation_length; ++i) {
    int32_t corr = 0;
    for (size_t j = 0; j < kCorrelationLen; ++j) {
      corr += (static_cast<int32_t>(seq1[j]) * (seq2 - static_cast<std::ptrdiff_t>(i))[j]) >> shift;
    }
    auto_corr[i] = corr;
  }
  // Normalized to 14 bits.
  const int32_t max_corr = MaxAbsValueW32(auto_corr, kCorrelationLen);
  const int scaling = std::max(0, 17 - NormW32(max_corr));
  for (size_t i = 0; i < kCorrelationLen; ++i) auto_correlation_[i] = SaturateInt16(auto_corr[i] >> scaling);
  auto_correlation_[kCorrelationLen] = 0;
}

bool NetEqTimeStretch::SpeechDetection(int32_t vec1_energy, int32_t vec2_energy, size_t peak_index, int scaling,
                                       const NetEqBackgroundNoise& background_noise) const {
  // Passive speech when (vec1_energy + vec2_energy) / (2 * peak_index) <=
  // 8 * background noise energy, written as
  // (vec1_energy + vec2_energy) / 16 <= peak_index * background noise energy.
  int32_t left_side = static_cast<int32_t>(std::clamp<int64_t>(
      (static_cast<int64_t>(vec1_energy) + vec2_energy) / 16, std::numeric_limits<int32_t>::min(),
      std::numeric_limits<int32_t>::max()));
  // A fixed threshold until the noise is estimated.
  int32_t right_side = background_noise.initialized() ? background_noise.Energy() : 75000;
  const int right_scale = std::max(0, 16 - NormW32(right_side));
  left_side = left_side >> right_scale;
  right_side = static_cast<int32_t>(peak_index) * (right_side >> right_scale);
  // The energies are scaled by 2 * scaling.
  if (NormW32(left_side) < 2 * scaling) {
    const int temp_scale = NormW32(left_side);
    left_side = ShiftW32(left_side, temp_scale);
    right_side = right_side >> (2 * scaling - temp_scale);
  } else {
    left_side = ShiftW32(left_side, 2 * scaling);
  }
  return left_side > right_side;
}

NetEqTimeStretch::Result NetEqTimeStretch::Accelerate(std::span<const float> input, bool fast_mode,
                                                      const NetEqBackgroundNoise& background_noise,
                                                      std::vector<float>& out, size_t* length_change) {
  *length_change = 0;
  const size_t fs_mult_120 = static_cast<size_t>(fs_mult_) * 120;  // 15 ms.
  // The input must be (almost) 30 ms.
  if (input.size() < (2 * 120 - 1) * static_cast<size_t>(fs_mult_)) {
    out.insert(out.end(), input.begin(), input.end());
    return Result::kError;
  }
  Analyze(input, background_noise, false, 0);
  const int correlation_threshold = fast_mode ? kFastCorrelationThreshold : kCorrelationThreshold;
  if (best_correlation_ <= correlation_threshold && active_speech_) {
    out.insert(out.end(), input.begin(), input.end());
    return Result::kNoStretch;
  }
  size_t peak_index = peak_index_;
  // Fast mode removes as many whole periods as fit in 15 ms. As in
  // TimeStretch::Process, `length_change` is still one period: the multiple
  // is local to Accelerate::CheckCriteriaAndStretch.
  if (fast_mode) peak_index = (fs_mult_120 / peak_index) * peak_index;
  // 0 to 15 ms, then the period from 15 ms crossfaded onto its end, then
  // the rest from 15 ms plus one period.
  out.insert(out.end(), input.begin(), input.begin() + static_cast<std::ptrdiff_t>(fs_mult_120));
  CrossFade(out, &signal_[fs_mult_120 - peak_index], &signal_[fs_mult_120], peak_index);
  out.insert(out.end(), input.begin() + static_cast<std::ptrdiff_t>(fs_mult_120 + peak_index), input.end());
  *length_change = peak_index_;
  return active_speech_ ? Result::kSuccess : Result::kSuccessLowEnergy;
}

NetEqTimeStretch::Result NetEqTimeStretch::PreemptiveExpand(std::span<const float> input, size_t old_data_length,
                                                            size_t overlap_samples,
                                                            const NetEqBackgroundNoise& background_noise,
                                                            std::vector<float>& out, size_t* length_change) {
  *length_change = 0;
  const size_t fs_mult_120 = static_cast<size_t>(fs_mult_) * 120;  // 15 ms.
  // The input must be (almost) 30 ms, with at least `overlap_samples` new.
  if (input.size() < (2 * 120 - 1) * static_cast<size_t>(fs_mult_) ||
      old_data_length + overlap_samples >= input.size()) {
    out.insert(out.end(), input.begin(), input.end());
    return Result::kError;
  }
  Analyze(input, background_noise, true, old_data_length);
  // Strong correlation and at most 15 ms played already, or passive speech.
  if (!((best_correlation_ > kCorrelationThreshold && old_data_length <= fs_mult_120) || !active_speech_)) {
    out.insert(out.end(), input.begin(), input.end());
    return Result::kNoStretch;
  }
  const size_t peak_index = peak_index_;
  const size_t unmodified_length = std::max(old_data_length, fs_mult_120);
  // The first part with the crossfade region, the period before it
  // crossfaded onto its end, then the rest from the unmodified part on.
  out.insert(out.end(), input.begin(), input.begin() + static_cast<std::ptrdiff_t>(unmodified_length + peak_index));
  CrossFade(out, &signal_[unmodified_length], &signal_[unmodified_length - peak_index], peak_index);
  out.insert(out.end(), input.begin() + static_cast<std::ptrdiff_t>(unmodified_length), input.end());
  *length_change = peak_index;
  return active_speech_ ? Result::kSuccess : Result::kSuccessLowEnergy;
}

}  // namespace sm::timing
