// 4.5 NetEq's Expand: the concealment of `neteq v2` (issue #62).
//
// A port of libwebrtc M157's modules/audio_coding/neteq/expand.cc (with
// random_vector.cc and the DspHelper pieces it calls: PeakDetection,
// ParabolicFit, MinDistortion, CrossFade, MuteSignal and the 4 kHz
// downsampling filters of dsp_helper.cc). On the first call of an expand
// period AnalyzeSignal reads the last 256 * fs/8000 samples of the sync
// buffer and picks:
//   - three pitch lags between 2.5 and 15 ms, from the correlation peaks at
//     4 kHz refined by the smallest distortion (kNumCorrelationCandidates 3,
//     kDistortionLength 20 * fs/8000);
//   - two expansion vectors (the last lag + overlap samples, and the period
//     before it scaled to the same energy when the energies are within 4x);
//   - a 6th-order AR model of the unvoiced part (kLpcAnalysisLength 160 * fs/8000);
//   - the voice mix factor from the correlation coefficient (the
//     -5179 + 19931x - 16422x^2 + 5776x^3 polynomial above x = 0.48);
//   - the mute slope from the energy ratio of the two periods, and whether
//     the signal is an onset.
// Each Process call then appends one period of `current lag` samples, the
// lag cycling through the three, as a voiced/unvoiced mix muted by the mute
// factor. The slope is at least 0.001 / (fs/8000) per sample from the 4th
// call and 0.002 / (fs/8000) from the 8th, and a mute factor that stops
// falling after the 4th call goes to 0 (Muted()). DecisionLogic reads the
// mute factor: decoding is postponed after an expand below 0.5.
//
// All of it is libwebrtc's fixed-point arithmetic on the int16 samples
// (#783): the 4 kHz correlation normalized to 14 bits, DspHelper's
// PeakDetection and MinDistortion, WebRtcSpl_LevinsonDurbin for the AR
// filter (Q12), the residual gain from WebRtcSpl_FilterMAFastQ12, the
// unvoiced part through WebRtcSpl_FilterARFastQ12, and the Q14 crossfade
// and muting. On int16 input it is bit-exact with libwebrtc's expand.cc
// (the unit test's golden hashes come from the reference sources compiled
// unchanged).
//
// NetEqBackgroundNoise ports background_noise.cc (one channel). After every
// GetAudio whose operation wrote decoded audio unchanged (normal, failed
// accelerate or preemptive expand) it reads the last 256 samples of the sync
// buffer. When their energy per sample is under the update threshold (500000
// at start, then the lowest energy seen, rising by a factor 1 + 229/65536
// per update otherwise, 4x in 4 s, and never below max energy - 60 dB) and
// the order-8 LPC residual is flat enough (5 * residual >= 16 * energy), it
// keeps the filter, the energy and the residual gain. Expand mixes the noise
// it generates into each period, scaled by its mute factor, which Expand
// sets to 0 at the start of every expand period and M157 never raises: the
// noise added is zero, as in libwebrtc. Its energy (2500 until the first
// update) sets where Normal's first decode after an expand starts its
// unmuting (NetEqNormalMuteFactor).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sm::timing {

// modules/audio_coding/neteq/random_vector.cc.
class NetEqRandomVector {
 public:
  static constexpr size_t kRandomTableSize = 256;
  static const int16_t kRandomTable[kRandomTableSize];

  void Reset() {
    seed_ = 777;
    seed_increment_ = 1;
  }
  void Generate(size_t length, int16_t* output);
  void IncreaseSeedIncrement(int16_t increase_by);
  int16_t seed_increment() const { return seed_increment_; }
  void set_seed_increment(int16_t value) { seed_increment_ = value; }

 private:
  uint32_t seed_ = 777;
  int16_t seed_increment_ = 1;
};

// modules/audio_coding/neteq/background_noise.cc, one channel. The sync
// buffer is float on the [-1, 1) scale; Update reads it as int16 and the
// estimate keeps libwebrtc's integer arithmetic, except the order-8
// Levinson-Durbin recursion (double, rounded to Q12 as WebRtcSpl's).
class NetEqBackgroundNoise {
 public:
  static constexpr size_t kMaxLpcOrder = 8;
  static constexpr size_t kVecLen = 256;
  static constexpr size_t kResidualLength = 64;
  static constexpr int kThresholdIncrement = 229;  // 0.0035 in Q16.

  NetEqBackgroundNoise() { Reset(); }
  void Reset();
  // BackgroundNoise::Update on the last kVecLen samples of `sync` (zeros
  // before its start). Returns true if the filter parameters were saved.
  bool Update(std::span<const float> sync);
  // BackgroundNoise::GenerateBackgroundNoise: `n` noise samples (int16 scale)
  // from `random_vector`, scaled by MuteFactor(); zeros until initialized.
  void GenerateBackgroundNoise(const int16_t* random_vector, size_t n, int16_t* out);

  int32_t Energy() const { return energy_; }
  void SetMuteFactor(int16_t value) { mute_factor_ = value; }
  int16_t MuteFactor() const { return mute_factor_; }
  bool initialized() const { return initialized_; }
  int32_t energy_update_threshold() const { return energy_update_threshold_; }
  int16_t scale() const { return scale_; }
  int16_t scale_shift() const { return scale_shift_; }
  const std::array<int16_t, kMaxLpcOrder + 1>& filter() const { return filter_; }

 private:
  void IncrementEnergyThreshold(int32_t sample_energy);
  void SaveParameters(const int16_t* lpc_coefficients, const int16_t* filter_state, int32_t sample_energy,
                      int32_t residual_energy);

  bool initialized_ = false;
  // BackgroundNoise::ChannelParameters.
  int32_t energy_ = 2500;
  int32_t max_energy_ = 0;
  int32_t energy_update_threshold_ = 500000;
  int32_t low_energy_update_threshold_ = 0;
  std::array<int16_t, kMaxLpcOrder> filter_state_{};
  std::array<int16_t, kMaxLpcOrder + 1> filter_{4096, 0, 0, 0, 0, 0, 0, 0, 0};
  int16_t mute_factor_ = 0;
  int16_t scale_ = 20000;
  int16_t scale_shift_ = 24;
};

// Normal::Process after an expand: the mute factor the decoded audio starts
// unmuting from, Expand's raised to sqrt(background noise energy / energy of
// the first 8 ms) when the audio is louder than the noise, else 1.0 (Q14).
int16_t NetEqNormalMuteFactor(std::span<const float> decoded, int fs_hz, int16_t expand_mute_factor,
                              int32_t background_noise_energy);

class NetEqExpand {
 public:
  static constexpr int kMaxConsecutiveExpands = 200;
  static constexpr size_t kUnvoicedLpcOrder = 6;
  static constexpr size_t kNumCorrelationCandidates = 3;
  static constexpr size_t kDistortionLength = 20;
  static constexpr size_t kLpcAnalysisLength = 160;
  static constexpr int kNumLags = 3;

  // `sample_rate` is 8000, 16000, 32000 or 48000.
  explicit NetEqExpand(int sample_rate);

  void Reset();
  // Expand::Process. `sync` is the sync buffer, oldest sample first (played
  // and future samples, at least history_length() of them); its last
  // overlap_length() samples may be rewritten (the overlap-add into the
  // expansion). Appends one period to `out` and returns its length.
  size_t Process(std::span<float> sync, std::vector<float>& out);
  // The expand period ends with a normal decode (the packet was late) or a
  // merge (it was lost): muting stops.
  void SetParametersForNormalAfterExpand();
  void SetParametersForMergeAfterExpand();
  bool Muted() const;

  int16_t MuteFactor() const { return mute_factor_; }  // Q14.
  size_t overlap_length() const { return overlap_length_; }
  size_t max_lag() const { return max_lag_; }
  // Samples of history AnalyzeSignal reads (Expand's signal_length).
  size_t history_length() const { return 256 * static_cast<size_t>(fs_mult_); }
  int consecutive_expands() const { return consecutive_expands_; }
  size_t expand_lag(int i) const { return expand_lags_[static_cast<size_t>(i)]; }
  int mute_slope() const { return mute_slope_; }              // Q20 per sample.
  int16_t voice_mix_factor() const { return voice_mix_factor_; }  // Q14.
  bool onset() const { return onset_; }
  // NetEqImpl's BackgroundNoise, which Expand mixes in; Reset() keeps it.
  NetEqBackgroundNoise& background_noise() { return background_noise_; }
  const NetEqBackgroundNoise& background_noise() const { return background_noise_; }
  // StatisticsCalculator::LogDelayedPacketOutageEvent's duration, set when
  // an expand period ends with a normal decode.
  size_t last_outage_duration_samples() const { return last_outage_duration_samples_; }

 private:
  void AnalyzeSignal(std::span<const float> sync, int16_t* random_vector);
  void InitializeForAnExpandPeriod();
  void UpdateLagIndex();
  // Expand::Correlation: 54 lags of the 4 kHz signal from lag 10, from the
  // end of `input`, normalized to 14 bits.
  void Correlation(const int16_t* input, size_t input_length, int16_t* output) const;
  void GenerateRandomVector(int16_t seed_increment, size_t length, int16_t* random_vector);

  int fs_hz_;
  int fs_mult_;
  NetEqRandomVector random_vector_;
  NetEqBackgroundNoise background_noise_;
  bool first_expand_ = true;
  int consecutive_expands_ = 0;
  size_t overlap_length_;
  size_t max_lag_ = 0;
  std::array<size_t, kNumLags> expand_lags_{};
  int lag_index_direction_ = 0;
  int current_lag_index_ = 0;
  bool stop_muting_ = false;
  size_t expand_duration_samples_ = 0;
  size_t last_outage_duration_samples_ = 0;
  // Expand::ChannelParameters (one channel).
  std::vector<int16_t> expand_vector0_;
  std::vector<int16_t> expand_vector1_;
  int16_t mute_factor_ = 16384;
  std::array<int16_t, kUnvoicedLpcOrder + 1> ar_filter_{};  // Q12; all zero until the first stable estimate.
  std::array<int16_t, kUnvoicedLpcOrder> ar_filter_state_{};
  int16_t ar_gain_ = 0;  // The random vector's scale: residual rms in Q(ar_gain_scale_ - 13).
  int ar_gain_scale_ = 0;
  int16_t voice_mix_factor_ = 0;
  int16_t current_voice_mix_factor_ = 0;
  bool onset_ = false;
  int mute_slope_ = 0;
};

// modules/audio_coding/neteq/merge.cc, one channel: the join of a decoded
// packet onto an expansion when the packet after a gap arrives (the gap was
// a loss, not a late packet). Expand runs once more, without muting, and its
// period is repeated after the sync buffer's unplayed samples to 202 ms * fs/8000.
// The decoded audio is placed at the lag where its first 10 ms correlate best
// with that signal at 4 kHz (CorrelateAndPeakSearch: at most max_lag, and far
// enough on that the sync buffer then holds 10 ms plus Expand's overlap and
// all its old samples). The decoded audio starts from the larger of Expand's
// mute factor and SignalScaling's sqrt(expanded energy / decoded energy) over
// the first 8 ms, unmuting by at least 4194 / (fs/8000) in Q20 per sample, and
// is crossfaded in linearly over min(60 * fs/8000 samples, what is left of
// the expansion, the decoded length).
//
// After GetExpandedSignal (MergeExpanded) it is libwebrtc's fixed-point
// arithmetic on the int16 samples (#783): Downsample's Q12 filter rounded and
// saturated, CrossCorrelationWithAutoShift normalized to 14 bits for
// DspHelper::PeakDetection, and RampSignal, UnmuteSignal and CrossFade
// rounded in Q14. On int16 input it is bit-exact with libwebrtc's merge.cc
// (the unit test's golden hashes come from the reference sources compiled
// unchanged).
class NetEqMerge {
 public:
  static constexpr size_t kMaxSampleRate = 48000;
  static constexpr size_t kExpandDownsampLength = 100;
  static constexpr size_t kInputDownsampLength = 40;
  static constexpr size_t kMaxCorrelationLength = 60;
  // GetExpandedSignal keeps at most this many unplayed samples.
  static constexpr size_t kMaxOldLength = 210 * kMaxSampleRate / 8000;

  // `sample_rate` is 8000, 16000, 32000 or 48000.
  explicit NetEqMerge(int sample_rate);

  // Merge::Process. `sync` is the sync buffer, oldest sample first, whose
  // last `future_length` samples (at least Expand's overlap) are not played
  // yet. The first old_length() samples of the merged signal overwrite the
  // unplayed ones, and the rest is appended to `out`; returns that length.
  // An unplayed part longer than kMaxOldLength is cut to its first
  // kMaxOldLength samples, with zeros put before it, as SyncBuffer's
  // InsertZerosAtIndex does.
  size_t Process(std::span<const float> input, std::vector<float>& sync, size_t future_length, NetEqExpand& expand,
                 std::vector<float>& out);
  // Merge::SignalScaling on int16 samples: sqrt(expanded / input energy) in
  // Q14 over the first min(64 * fs/8000, input_length) samples, or 16384
  // when the expansion is the louder.
  static int16_t SignalScaling(const int16_t* input, size_t input_length, const int16_t* expanded, int fs_mult);

  // The int16 part of Merge::Process, after GetExpandedSignal: `expanded`
  // is the sync buffer's `old_length` unplayed samples followed by the
  // expansion (period `expand_period`) repeated to at least 202 * fs/8000
  // samples, and the other arguments are Expand's state. Returns the merged
  // length (merged()); its first `old_length` samples replace the unplayed
  // ones.
  size_t MergeExpanded(std::span<const int16_t> input, std::span<const int16_t> expanded, size_t old_length,
                       size_t expand_period, int16_t expand_mute_factor, size_t max_lag, size_t overlap_length);
  const std::vector<int16_t>& merged() const { return temp_data_; }

  size_t old_length() const { return old_length_; }
  size_t best_correlation_index() const { return best_correlation_index_; }
  int16_t mute_factor() const { return mute_factor_; }  // Q14, where the decoded audio started.

 private:
  // Merge::Downsample: both signals to 4 kHz.
  void Downsample(const int16_t* input, size_t input_length, const int16_t* expanded, size_t expanded_length);
  size_t CorrelateAndPeakSearch(size_t start_position, size_t input_length, size_t expand_period, size_t max_lag,
                                size_t overlap_length) const;

  int fs_hz_;
  int fs_mult_;
  size_t timestamps_per_call_;
  int16_t expanded_downsampled_[kExpandDownsampLength] = {};
  int16_t input_downsampled_[kInputDownsampLength] = {};
  std::vector<float> expanded_, expanded_temp_;
  std::vector<int16_t> temp_data_;
  size_t old_length_ = 0;
  size_t best_correlation_index_ = 0;
  int16_t mute_factor_ = 16384;
};

// modules/audio_coding/neteq/time_stretch.cc with accelerate.cc and
// preemptive_expand.cc, one channel: the DSP of `neteq v2`'s accelerate and
// preemptive expand. The input is at least 30 ms (NetEqImpl borrows from the
// sync buffer to get there). It is downsampled to 4 kHz with the filter delay
// compensated, and the strongest autocorrelation peak between lags 10 and 59
// (2.5-15 ms) gives the pitch period L. The two periods either side of 15 ms
// are compared:
//   - a simple VAD calls the block passive speech when their energy per
//     sample is at most 8x the background noise energy (75000 / 16 per sample
//     before the noise is estimated); then any period works;
//   - otherwise their correlation in Q14 must exceed 0.9 (14746; 0.5 for
//     fast accelerate), and preemptive expand also needs at most 15 ms of
//     already-played samples at the start.
// Accelerate crossfades the period before 15 ms into the one after it, which
// removes L samples (fast mode removes as many whole periods as fit in
// 15 ms). Preemptive expand repeats the period before max(15 ms, old data),
// which adds L samples. The crossfade is AudioVector::CrossFade's linear ramp
// of step 16384 / (L + 1).
//
// All of it is libwebrtc's fixed-point arithmetic on the int16 samples
// (#644): the energies, the VAD, the correlation, the scaling shifts,
// DspHelper::PeakDetection with the table ParabolicFit, and the crossfade
// rounded as AudioVector's ((a x + (16384 - a) y + 8192) >> 14). On int16
// input the output is bit-exact with libwebrtc's (the unit test's golden
// hashes come from the reference sources compiled unchanged). Fast mode's
// length change is one period, as TimeStretch::Process reports it.
class NetEqTimeStretch {
 public:
  enum class Result { kSuccess, kSuccessLowEnergy, kNoStretch, kError };
  static constexpr size_t kCorrelationLen = 50;
  static constexpr size_t kMinLag = 10;
  static constexpr size_t kMaxLag = 60;
  static constexpr size_t kDownsampledLen = kCorrelationLen + kMaxLag;
  static constexpr int kCorrelationThreshold = 14746;  // 0.9 in Q14.
  static constexpr int kFastCorrelationThreshold = 8192;  // 0.5 in Q14.

  // `sample_rate` is 8000, 16000, 32000 or 48000.
  explicit NetEqTimeStretch(int sample_rate);

  // Accelerate::Process: appends the stretched `input` to `out`, and sets
  // `length_change` to the samples removed (0 unless it succeeded; one
  // period in fast mode, which removes as many as fit in 15 ms).
  Result Accelerate(std::span<const float> input, bool fast_mode, const NetEqBackgroundNoise& background_noise,
                    std::vector<float>& out, size_t* length_change);
  // PreemptiveExpand::Process: the first `old_data_length` samples of
  // `input` were played already. `overlap_samples` is Expand's overlap.
  Result PreemptiveExpand(std::span<const float> input, size_t old_data_length, size_t overlap_samples,
                          const NetEqBackgroundNoise& background_noise, std::vector<float>& out,
                          size_t* length_change);

  // From the last Process: the pitch period and its correlation (Q14), and
  // the VAD's decision.
  size_t peak_index() const { return peak_index_; }
  int16_t best_correlation() const { return best_correlation_; }
  bool active_speech() const { return active_speech_; }

 private:
  // TimeStretch::Process up to CheckCriteriaAndStretch.
  void Analyze(std::span<const float> input, const NetEqBackgroundNoise& background_noise, bool preemptive,
               size_t old_data_length);
  void AutoCorrelation();
  bool SpeechDetection(int32_t vec1_energy, int32_t vec2_energy, size_t peak_index, int scaling,
                       const NetEqBackgroundNoise& background_noise) const;

  int fs_hz_;
  int fs_mult_;
  std::vector<int16_t> signal_;
  int16_t downsampled_input_[kDownsampledLen] = {};
  int16_t auto_correlation_[kCorrelationLen + 1] = {};
  size_t peak_index_ = 0;
  int16_t best_correlation_ = 0;
  bool active_speech_ = false;
};

}  // namespace sm::timing
