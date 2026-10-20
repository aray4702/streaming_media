#include "sm/timing/neteq_expand.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

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

}  // namespace

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
  int16_t random_vector[48000 / 8000 * 120 + 30];
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
  // BackgroundNoise::GenerateBackgroundNoise is not ported: no noise is added.

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

}  // namespace sm::timing
