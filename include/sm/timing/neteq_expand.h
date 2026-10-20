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
// The signal arithmetic is float on the int16 scale; the factors that drive
// decisions (mute factor and voice mix factor in Q14, mute slope in Q20,
// lags) keep libwebrtc's integer arithmetic. BackgroundNoise is not ported
// yet, so the noise added to the expansion is zero (tools/catalog.py 4.5).
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
  // StatisticsCalculator::LogDelayedPacketOutageEvent's duration, set when
  // an expand period ends with a normal decode.
  size_t last_outage_duration_samples() const { return last_outage_duration_samples_; }

 private:
  void AnalyzeSignal(std::span<const float> sync, int16_t* random_vector);
  void InitializeForAnExpandPeriod();
  void UpdateLagIndex();
  // Expand::Correlation: 54 lags of the 4 kHz signal from lag 10.
  void Correlation(std::span<const float> history, float* output) const;
  void GenerateRandomVector(int16_t seed_increment, size_t length, int16_t* random_vector);

  int fs_hz_;
  int fs_mult_;
  NetEqRandomVector random_vector_;
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
  std::vector<float> expand_vector0_;
  std::vector<float> expand_vector1_;
  int16_t mute_factor_ = 16384;
  std::array<float, kUnvoicedLpcOrder + 1> ar_filter_{1, 0, 0, 0, 0, 0, 0};
  std::array<float, kUnvoicedLpcOrder> ar_filter_state_{};
  float ar_gain_ = 0;  // Scale of the random vector: residual rms / 8192.
  int16_t voice_mix_factor_ = 0;
  int16_t current_voice_mix_factor_ = 0;
  bool onset_ = false;
  int mute_slope_ = 0;
};

}  // namespace sm::timing
