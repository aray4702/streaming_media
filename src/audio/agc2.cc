#include "sm/audio/agc2.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace sm::audio::agc2 {

float FloatS16ToDbfs(float v) {
  // -20 * log10(32768).
  constexpr float kMinDbfs = -90.30899869919436f;
  if (v <= 1.0f) return kMinDbfs;
  return 20.0f * std::log10(v) + kMinDbfs;
}

float DbfsToFloatS16(float dbfs) { return kMaxAbsFloatS16Value * std::pow(10.0f, dbfs / 20.0f); }

float DbToRatio(float db) { return std::pow(10.0f, db / 20.0f); }

// fixed_digital_level_estimator.cc
namespace {
constexpr float kAttackFilterConstant = 0.0f;
constexpr float kDecayFilterConstant = 0.9971259f;
}  // namespace

FixedDigitalLevelEstimator::FixedDigitalLevelEstimator(int samples_per_channel) {
  SetSamplesPerChannel(samples_per_channel);
}

void FixedDigitalLevelEstimator::SetSamplesPerChannel(int samples_per_channel) {
  samples_in_frame_ = samples_per_channel;
  samples_in_sub_frame_ = samples_per_channel / kSubFramesInFrame;
}

std::array<float, kSubFramesInFrame> FixedDigitalLevelEstimator::ComputeLevel(std::span<const float> frame) {
  // Max envelope without smoothing.
  std::array<float, kSubFramesInFrame> envelope{};
  for (int sub_frame = 0; sub_frame < kSubFramesInFrame; ++sub_frame) {
    for (int i = 0; i < samples_in_sub_frame_; ++i) {
      envelope[sub_frame] = std::max(envelope[sub_frame], std::abs(frame[sub_frame * samples_in_sub_frame_ + i]));
    }
  }
  // Envelope increases happen one step earlier so the gain decrease doesn't
  // miss a sudden signal increase due to interpolation.
  for (int sub_frame = 0; sub_frame < kSubFramesInFrame - 1; ++sub_frame) {
    if (envelope[sub_frame] < envelope[sub_frame + 1]) envelope[sub_frame] = envelope[sub_frame + 1];
  }
  // Attack / decay smoothing.
  for (int sub_frame = 0; sub_frame < kSubFramesInFrame; ++sub_frame) {
    const float v = envelope[sub_frame];
    if (v > filter_state_level_) {
      envelope[sub_frame] = v * (1 - kAttackFilterConstant) + filter_state_level_ * kAttackFilterConstant;
    } else {
      envelope[sub_frame] = v * (1 - kDecayFilterConstant) + filter_state_level_ * kDecayFilterConstant;
    }
    filter_state_level_ = envelope[sub_frame];
  }
  return envelope;
}

// interpolated_gain_curve.{h,cc}
namespace {
// Defined as DbfsToLinear(kLimiterMaxInputLevelDbFs).
constexpr float kMaxInputLevelLinear = static_cast<float>(36766.300710566735);

constexpr std::array<float, kInterpolatedGainCurveTotalPoints> kApproximationParamsX = {
    {30057.296875,    30148.986328125, 30240.67578125,  30424.052734375, 30607.4296875,  30790.806640625,
     30974.18359375,  31157.560546875, 31340.939453125, 31524.31640625,  31707.693359375, 31891.0703125,
     32074.447265625, 32257.82421875,  32441.201171875, 32624.580078125, 32807.95703125, 32991.33203125,
     33174.7109375,   33358.08984375,  33541.46484375,  33724.84375,     33819.53515625, 34009.5390625,
     34200.05859375,  34389.81640625,  34674.48828125,  35054.375,       35434.86328125, 35814.81640625,
     36195.16796875,  36575.03125}};
constexpr std::array<float, kInterpolatedGainCurveTotalPoints> kApproximationParamsM = {
    {-3.515235675877192989e-07, -1.050251626111275982e-06, -2.085213736791047268e-06, -3.443004743530764244e-06,
     -4.773849468620028347e-06, -6.077375928725814447e-06, -7.353257842623861507e-06, -8.601219633419532329e-06,
     -9.821013009059242904e-06, -1.101243378798244521e-05, -1.217532644659513608e-05, -1.330956911260727793e-05,
     -1.441507538402220234e-05, -1.549179251014720649e-05, -1.653970684856176376e-05, -1.755882840370759368e-05,
     -1.854918446042574942e-05, -1.951086778717581183e-05, -2.044398024736437947e-05, -2.1348627342376858e-05,
     -2.222496914328075945e-05, -2.265374678245279938e-05, -2.242570917587727308e-05, -2.220122041762806475e-05,
     -2.19802095671184361e-05,  -2.176260204578284174e-05, -2.133731686626560986e-05, -2.092481918225530535e-05,
     -2.052459603874012828e-05, -2.013615448959171772e-05, -1.975903069251216948e-05, -1.939277899509761482e-05}};
constexpr std::array<float, kInterpolatedGainCurveTotalPoints> kApproximationParamsQ = {
    {1.010565876960754395, 1.031631827354431152, 1.062929749488830566, 1.104239225387573242, 1.144973039627075195,
     1.185109615325927734, 1.224629044532775879, 1.263512492179870605, 1.301741957664489746, 1.339300632476806641,
     1.376173257827758789, 1.412345528602600098, 1.447803974151611328, 1.482536554336547852, 1.516532182693481445,
     1.549780607223510742, 1.582272171974182129, 1.613999366760253906, 1.644955039024353027, 1.675132393836975098,
     1.704526185989379883, 1.718986630439758301, 1.711274504661560059, 1.703639745712280273, 1.696081161499023438,
     1.688597679138183594, 1.673851132392883301, 1.659391283988952637, 1.645209431648254395, 1.631297469139099121,
     1.617647409439086914, 1.604251742362976074}};
}  // namespace

const std::array<float, kInterpolatedGainCurveTotalPoints>& InterpolatedGainCurve::params_x() {
  return kApproximationParamsX;
}

void InterpolatedGainCurve::UpdateStats(float input_level) const {
  if (input_level < kApproximationParamsX[0]) {
    ++stats_.look_ups_identity_region;
    stats_.region = Region::kIdentity;
  } else if (input_level < kApproximationParamsX[kInterpolatedGainCurveKneePoints - 1]) {
    ++stats_.look_ups_knee_region;
    stats_.region = Region::kKnee;
  } else if (input_level < kMaxInputLevelLinear) {
    ++stats_.look_ups_limiter_region;
    stats_.region = Region::kLimiter;
  } else {
    ++stats_.look_ups_saturation_region;
    stats_.region = Region::kSaturation;
  }
}

float InterpolatedGainCurve::LookUpGainToApply(float input_level) const {
  UpdateStats(input_level);
  if (input_level <= kApproximationParamsX[0]) return 1.0f;  // Identity region.
  if (input_level >= kMaxInputLevelLinear) {
    // Saturating samples exactly hit the clipping level.
    return 32768.0f / input_level;
  }
  // Knee and limiter regions: find the linear piece.
  auto it = std::lower_bound(kApproximationParamsX.begin(), kApproximationParamsX.end(), input_level);
  const size_t index = static_cast<size_t>(std::distance(kApproximationParamsX.begin(), it)) - 1;
  return kApproximationParamsM[index] * input_level + kApproximationParamsQ[index];
}

// limiter.cc
namespace {
constexpr float kAttackFirstSubframeInterpolationPower = 8.0f;

void InterpolateFirstSubframe(float last_factor, float current_factor, std::span<float> subframe) {
  const int n = static_cast<int>(subframe.size());
  constexpr float p = kAttackFirstSubframeInterpolationPower;
  for (int i = 0; i < n; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(n);
    subframe[i] = std::pow(1.0f - t, p) * (last_factor - current_factor) + current_factor;
  }
}

void ComputePerSampleSubframeFactors(const std::array<float, kSubFramesInFrame + 1>& scaling_factors,
                                     std::span<float> per_sample) {
  const size_t num_subframes = scaling_factors.size() - 1;
  const int subframe_size = static_cast<int>(per_sample.size() / num_subframes);
  // The first sub-frame is handled differently on attack.
  const bool is_attack = scaling_factors[0] > scaling_factors[1];
  if (is_attack) {
    InterpolateFirstSubframe(scaling_factors[0], scaling_factors[1],
                             per_sample.subspan(0, static_cast<size_t>(subframe_size)));
  }
  for (size_t i = is_attack ? 1 : 0; i < num_subframes; ++i) {
    const int start = static_cast<int>(i) * subframe_size;
    const float scaling_start = scaling_factors[i];
    const float scaling_end = scaling_factors[i + 1];
    const float scaling_diff = (scaling_end - scaling_start) / static_cast<float>(subframe_size);
    for (int j = 0; j < subframe_size; ++j) {
      per_sample[static_cast<size_t>(start + j)] = scaling_start + scaling_diff * static_cast<float>(j);
    }
  }
}
}  // namespace

Limiter::Limiter(int samples_per_channel) : level_estimator_(samples_per_channel) {}

void Limiter::Process(std::span<float> frame) {
  const auto level_estimate = level_estimator_.ComputeLevel(frame);
  scaling_factors_[0] = last_scaling_factor_;
  std::transform(level_estimate.begin(), level_estimate.end(), scaling_factors_.begin() + 1,
                 [this](float x) { return interp_gain_curve_.LookUpGainToApply(x); });
  std::span<float> per_sample(per_sample_scaling_factors_.data(), frame.size());
  ComputePerSampleSubframeFactors(scaling_factors_, per_sample);
  for (size_t j = 0; j < frame.size(); ++j) {
    frame[j] = std::clamp(frame[j] * per_sample[j], kMinFloatS16Value, kMaxFloatS16Value);
  }
  last_scaling_factor_ = scaling_factors_.back();
}

// gain_applier.cc
namespace {
// True when the gain is so close to 1 that it would not affect int16 samples.
bool GainCloseToOne(float gain_factor) {
  return 1.0f - 1.0f / kMaxFloatS16Value <= gain_factor && gain_factor <= 1.0f + 1.0f / kMaxFloatS16Value;
}
}  // namespace

GainApplier::GainApplier(bool hard_clip_samples, float initial_gain_factor)
    : hard_clip_samples_(hard_clip_samples),
      last_gain_factor_(initial_gain_factor),
      current_gain_factor_(initial_gain_factor) {}

void GainApplier::ApplyGain(std::span<float> frame) {
  if (static_cast<int>(frame.size()) != samples_per_channel_) {
    samples_per_channel_ = static_cast<int>(frame.size());
    inverse_samples_per_channel_ = 1.0f / static_cast<float>(samples_per_channel_);
  }
  const float last = last_gain_factor_, end = current_gain_factor_;
  if (last == end && GainCloseToOne(end)) {
    // Do not modify the signal.
  } else if (last == end) {
    for (float& s : frame) s *= end;
  } else {
    // The gain changes: ramp it to avoid discontinuities.
    const float increment = (end - last) * inverse_samples_per_channel_;
    float gain = last;
    for (float& s : frame) {
      s *= gain;
      gain += increment;
    }
  }
  last_gain_factor_ = current_gain_factor_;
  if (hard_clip_samples_) {
    for (float& s : frame) s = std::clamp(s, kMinFloatS16Value, kMaxFloatS16Value);
  }
}

// noise_level_estimator.cc
namespace {
constexpr int kFramesPerSecond = 100;

float EnergyToDbfs(float signal_energy, int num_samples) {
  const float rms_square = signal_energy / static_cast<float>(num_samples);
  constexpr float kMinDbfs = -90.30899869919436f;
  if (rms_square <= 1.0f) return kMinDbfs;
  return 10.0f * std::log10(rms_square) + kMinDbfs;
}

// Instant decay and slow attack.
float SmoothNoiseFloorEstimate(float current_estimate, float new_estimate) {
  constexpr float kAttack = 0.5f;
  if (current_estimate < new_estimate) return kAttack * new_estimate + (1.0f - kAttack) * current_estimate;
  return new_estimate;
}
}  // namespace

void NoiseFloorEstimator::Initialize(int sample_rate_hz) {
  sample_rate_hz_ = sample_rate_hz;
  first_period_ = true;
  preliminary_noise_energy_set_ = false;
  // Minimum noise energy: -84 dBFS.
  min_noise_energy_ = static_cast<float>(sample_rate_hz) * 2.0f * 2.0f / kFramesPerSecond;
  preliminary_noise_energy_ = min_noise_energy_;
  noise_energy_ = min_noise_energy_;
  counter_ = kUpdatePeriodNumFrames;
}

float NoiseFloorEstimator::Analyze(std::span<const float> frame) {
  const int n = static_cast<int>(frame.size());
  const int sample_rate_hz = n * kFramesPerSecond;
  if (sample_rate_hz != sample_rate_hz_) Initialize(sample_rate_hz);
  float frame_energy = 0.0f;
  for (float v : frame) frame_energy += v * v;
  if (frame_energy <= min_noise_energy_) {
    // Muted or below the minimum measurable energy.
    return EnergyToDbfs(noise_energy_, n);
  }
  if (preliminary_noise_energy_set_) {
    preliminary_noise_energy_ = std::min(preliminary_noise_energy_, frame_energy);
  } else {
    preliminary_noise_energy_ = frame_energy;
    preliminary_noise_energy_set_ = true;
  }
  if (counter_ == 0) {
    // Full period observed.
    first_period_ = false;
    noise_energy_ = SmoothNoiseFloorEstimate(noise_energy_, preliminary_noise_energy_);
    counter_ = kUpdatePeriodNumFrames;
    preliminary_noise_energy_set_ = false;
  } else if (first_period_) {
    // During the initial period, follow the (monotonic) preliminary energy.
    noise_energy_ = preliminary_noise_energy_;
    --counter_;
  } else {
    // During the observation period it's only allowed to lower the energy.
    noise_energy_ = std::min(noise_energy_, preliminary_noise_energy_);
    --counter_;
  }
  return EnergyToDbfs(noise_energy_, n);
}

// speech_level_estimator_impl.cc
namespace {
float ClampLevelEstimateDbfs(float level_estimate_dbfs) { return std::clamp(level_estimate_dbfs, -90.0f, 30.0f); }

float GetInitialSpeechLevelEstimateDbfs(const AdaptiveDigitalConfig& config) {
  return ClampLevelEstimateDbfs(-kSaturationProtectorInitialHeadroomDb - config.initial_gain_db - config.headroom_db);
}
}  // namespace

SpeechLevelEstimator::SpeechLevelEstimator(const AdaptiveDigitalConfig& config, int adjacent_speech_frames_threshold)
    : initial_speech_level_dbfs_(GetInitialSpeechLevelEstimateDbfs(config)),
      adjacent_speech_frames_threshold_(adjacent_speech_frames_threshold),
      level_dbfs_(initial_speech_level_dbfs_) {
  Reset();
}

void SpeechLevelEstimator::Update(float rms_dbfs, float speech_probability) {
  if (speech_probability < kVadConfidenceThreshold) {
    // Not a speech frame.
    if (adjacent_speech_frames_threshold_ > 1) {
      if (num_adjacent_speech_frames_ >= adjacent_speech_frames_threshold_) {
        // First non-speech frame after a long enough speech sequence.
        reliable_state_ = preliminary_state_;
      } else if (num_adjacent_speech_frames_ > 0) {
        // First non-speech frame after a too short speech sequence.
        preliminary_state_ = reliable_state_;
      }
    }
    num_adjacent_speech_frames_ = 0;
  } else {
    ++num_adjacent_speech_frames_;
    const bool buffer_is_full = preliminary_state_.time_to_confidence_ms == 0;
    if (!buffer_is_full) preliminary_state_.time_to_confidence_ms -= kFrameDurationMs;
    // Weighted average of levels with speech probability as weight.
    const float leak_factor = buffer_is_full ? kLevelEstimatorLeakFactor : 1.0f;
    preliminary_state_.numerator = preliminary_state_.numerator * leak_factor + rms_dbfs * speech_probability;
    preliminary_state_.denominator = preliminary_state_.denominator * leak_factor + speech_probability;
    const float level_dbfs = preliminary_state_.numerator / preliminary_state_.denominator;
    if (num_adjacent_speech_frames_ >= adjacent_speech_frames_threshold_) {
      level_dbfs_ = ClampLevelEstimateDbfs(level_dbfs);
    }
  }
  UpdateIsConfident();
}

void SpeechLevelEstimator::UpdateIsConfident() {
  if (adjacent_speech_frames_threshold_ == 1) {
    is_confident_ = preliminary_state_.time_to_confidence_ms == 0;
    return;
  }
  // Once confident, it remains confident. During the first long enough
  // speech sequence, `reliable_state_` is ignored.
  is_confident_ = reliable_state_.time_to_confidence_ms == 0 ||
                  (num_adjacent_speech_frames_ >= adjacent_speech_frames_threshold_ &&
                   preliminary_state_.time_to_confidence_ms == 0);
}

void SpeechLevelEstimator::Reset() {
  ResetState(preliminary_state_);
  ResetState(reliable_state_);
  level_dbfs_ = initial_speech_level_dbfs_;
  num_adjacent_speech_frames_ = 0;
}

void SpeechLevelEstimator::ResetState(State& s) const {
  s.time_to_confidence_ms = static_cast<int>(kLevelEstimatorTimeToConfidenceMs);
  s.numerator = initial_speech_level_dbfs_;
  s.denominator = 1.0f;
}

// saturation_protector_buffer.cc
bool SaturationProtectorBuffer::operator==(const SaturationProtectorBuffer& b) const {
  if (size_ != b.size_) return false;
  const size_t n = buffer_.size();
  for (int i = 0, i0 = FrontIndex(), i1 = b.FrontIndex(); i < size_; ++i, ++i0, ++i1) {
    if (buffer_[static_cast<size_t>(i0) % n] != b.buffer_[static_cast<size_t>(i1) % n]) return false;
  }
  return true;
}

void SaturationProtectorBuffer::PushBack(float v) {
  buffer_[static_cast<size_t>(next_++)] = v;
  if (next_ == static_cast<int>(buffer_.size())) next_ = 0;
  if (size_ < static_cast<int>(buffer_.size())) ++size_;
}

std::optional<float> SaturationProtectorBuffer::Front() const {
  if (size_ == 0) return std::nullopt;
  return buffer_[static_cast<size_t>(FrontIndex())];
}

// saturation_protector.cc
namespace {
constexpr int kPeakEnveloperSuperFrameLengthMs = 400;
constexpr float kMinMarginDb = 12.0f;
constexpr float kMaxMarginDb = 25.0f;
constexpr float kAttack = 0.9988493699365052f;
constexpr float kDecay = 0.9997697679981565f;

void ResetSaturationProtectorState(float initial_headroom_db, SaturationProtector::State& state) {
  state.headroom_db = initial_headroom_db;
  state.peak_delay_buffer.Reset();
  state.max_peaks_dbfs = kMinLevelDbfs;
  state.time_since_push_ms = 0;
}

void UpdateSaturationProtectorState(float peak_dbfs, float speech_level_dbfs, SaturationProtector::State& state) {
  // Max peak over `kPeakEnveloperSuperFrameLengthMs`.
  state.max_peaks_dbfs = std::max(state.max_peaks_dbfs, peak_dbfs);
  state.time_since_push_ms += kFrameDurationMs;
  if (state.time_since_push_ms > kPeakEnveloperSuperFrameLengthMs) {
    state.peak_delay_buffer.PushBack(state.max_peaks_dbfs);
    state.max_peaks_dbfs = kMinLevelDbfs;
    state.time_since_push_ms = 0;
  }
  // Compare the speech level with the delayed max speech peak.
  const float delayed_peak_dbfs = state.peak_delay_buffer.Front().value_or(state.max_peaks_dbfs);
  const float difference_db = delayed_peak_dbfs - speech_level_dbfs;
  if (difference_db > state.headroom_db) {
    state.headroom_db = state.headroom_db * kAttack + difference_db * (1.0f - kAttack);
  } else {
    state.headroom_db = state.headroom_db * kDecay + difference_db * (1.0f - kDecay);
  }
  state.headroom_db = std::clamp(state.headroom_db, kMinMarginDb, kMaxMarginDb);
}
}  // namespace

SaturationProtector::SaturationProtector(float initial_headroom_db, int adjacent_speech_frames_threshold)
    : initial_headroom_db_(initial_headroom_db), adjacent_speech_frames_threshold_(adjacent_speech_frames_threshold) {
  Reset();
}

void SaturationProtector::Analyze(float speech_probability, float peak_dbfs, float speech_level_dbfs) {
  if (speech_probability < kVadConfidenceThreshold) {
    if (adjacent_speech_frames_threshold_ > 1) {
      if (num_adjacent_speech_frames_ >= adjacent_speech_frames_threshold_) {
        reliable_state_ = preliminary_state_;
      } else if (num_adjacent_speech_frames_ > 0) {
        preliminary_state_ = reliable_state_;
      }
    }
    num_adjacent_speech_frames_ = 0;
  } else {
    ++num_adjacent_speech_frames_;
    UpdateSaturationProtectorState(peak_dbfs, speech_level_dbfs, preliminary_state_);
    if (num_adjacent_speech_frames_ >= adjacent_speech_frames_threshold_) {
      headroom_db_ = preliminary_state_.headroom_db;
    }
  }
}

void SaturationProtector::Reset() {
  num_adjacent_speech_frames_ = 0;
  headroom_db_ = initial_headroom_db_;
  ResetSaturationProtectorState(initial_headroom_db_, preliminary_state_);
  ResetSaturationProtectorState(initial_headroom_db_, reliable_state_);
}

// adaptive_digital_gain_controller.cc
namespace {
// Gain for `input_level_dbfs` to reach `-headroom_db`, in [0, max_gain_db].
float ComputeGainDb(float input_level_dbfs, const AdaptiveDigitalConfig& config) {
  if (input_level_dbfs < -(config.headroom_db + config.max_gain_db)) return config.max_gain_db;
  if (input_level_dbfs < -config.headroom_db) return -config.headroom_db - input_level_dbfs;
  return 0.0f;
}

float LimitGainByNoise(float target_gain_db, float input_noise_level_dbfs, float max_output_noise_level_dbfs) {
  const float max_allowed_gain_db = max_output_noise_level_dbfs - input_noise_level_dbfs;
  return std::min(target_gain_db, std::max(max_allowed_gain_db, 0.0f));
}

float LimitGainByLowConfidence(float target_gain_db, float last_gain_db, float limiter_audio_level_dbfs,
                               bool estimate_is_confident) {
  if (estimate_is_confident || limiter_audio_level_dbfs <= kLimiterThresholdForAgcGainDbfs) return target_gain_db;
  const float limiter_level_dbfs_before_gain = limiter_audio_level_dbfs - last_gain_db;
  const float new_target_gain_db = std::max(kLimiterThresholdForAgcGainDbfs - limiter_level_dbfs_before_gain, 0.0f);
  return std::min(new_target_gain_db, target_gain_db);
}

float ComputeGainChangeThisFrameDb(float target_gain_db, float last_gain_db, bool gain_increase_allowed,
                                   float max_gain_decrease_db, float max_gain_increase_db) {
  float diff = target_gain_db - last_gain_db;
  if (!gain_increase_allowed) diff = std::min(diff, 0.0f);
  return std::clamp(diff, -max_gain_decrease_db, max_gain_increase_db);
}
}  // namespace

AdaptiveDigitalGainController::AdaptiveDigitalGainController(const AdaptiveDigitalConfig& config,
                                                             int adjacent_speech_frames_threshold)
    : gain_applier_(/*hard_clip_samples=*/false, DbToRatio(config.initial_gain_db)),
      config_(config),
      adjacent_speech_frames_threshold_(adjacent_speech_frames_threshold),
      max_gain_change_db_per_10ms_(config.max_gain_change_db_per_second * kFrameDurationMs / 1000.0f),
      frames_to_gain_increase_allowed_(adjacent_speech_frames_threshold),
      last_gain_db_(config.initial_gain_db) {}

void AdaptiveDigitalGainController::Process(const FrameInfo& info, std::span<float> frame) {
  const float input_level_dbfs = info.speech_level_dbfs + info.headroom_db;
  const float target_gain_db = LimitGainByLowConfidence(
      LimitGainByNoise(ComputeGainDb(input_level_dbfs, config_), info.noise_rms_dbfs,
                       config_.max_output_noise_level_dbfs),
      last_gain_db_, info.limiter_envelope_dbfs, info.speech_level_reliable);
  // Forbid increasing the gain until enough adjacent speech frames are seen.
  bool first_confident_speech_frame = false;
  if (info.speech_probability < kVadConfidenceThreshold) {
    frames_to_gain_increase_allowed_ = adjacent_speech_frames_threshold_;
  } else if (frames_to_gain_increase_allowed_ > 0) {
    --frames_to_gain_increase_allowed_;
    first_confident_speech_frame = frames_to_gain_increase_allowed_ == 0;
  }
  const bool gain_increase_allowed = frames_to_gain_increase_allowed_ == 0;
  float max_gain_increase_db = max_gain_change_db_per_10ms_;
  if (first_confident_speech_frame) {
    // No increase happened while waiting for the speech sequence: allow a
    // faster one now.
    max_gain_increase_db *= static_cast<float>(adjacent_speech_frames_threshold_);
  }
  const float change_db = ComputeGainChangeThisFrameDb(target_gain_db, last_gain_db_, gain_increase_allowed,
                                                       max_gain_change_db_per_10ms_, max_gain_increase_db);
  if (change_db != 0.0f) gain_applier_.SetGainFactor(DbToRatio(last_gain_db_ + change_db));
  gain_applier_.ApplyGain(frame);
  last_gain_db_ = last_gain_db_ + change_db;
}

// GainController2 (modules/audio_processing/gain_controller2.cc, not in the
// reference sources yet: see the header).
GainController2::GainController2(GainController2Config cfg)
    : cfg_(cfg),
      fixed_gain_applier_(/*hard_clip_samples=*/false, DbToRatio(cfg.fixed_gain_db)),
      speech_level_estimator_(cfg.adaptive_digital, kAdjacentSpeechFramesThreshold),
      saturation_protector_(kSaturationProtectorInitialHeadroomDb, kAdjacentSpeechFramesThreshold),
      adaptive_digital_(cfg.adaptive_digital, kAdjacentSpeechFramesThreshold),
      limiter_(cfg.sample_rate / 100) {}

void GainController2::Process(std::span<float> frame, float speech_probability) {
  s16_.assign(frame.begin(), frame.end());
  for (float& v : s16_) v *= kMaxAbsFloatS16Value;
  std::span<float> f(s16_);
  fixed_gain_applier_.ApplyGain(f);
  // Peak and RMS levels.
  float peak = 0.0f, energy = 0.0f;
  for (float v : f) {
    peak = std::max(std::abs(v), peak);
    energy += v * v;
  }
  const float peak_dbfs = FloatS16ToDbfs(peak);
  const float rms_dbfs = FloatS16ToDbfs(std::sqrt(energy / static_cast<float>(f.size())));
  const float noise_rms_dbfs = noise_level_estimator_.Analyze(f);
  speech_level_estimator_.Update(rms_dbfs, speech_probability);
  const float speech_level_dbfs = speech_level_estimator_.GetLevelDbfs();
  saturation_protector_.Analyze(speech_probability, peak_dbfs, speech_level_dbfs);
  const AdaptiveDigitalGainController::FrameInfo info{
      speech_probability,
      speech_level_dbfs,
      speech_level_estimator_.IsConfident(),
      noise_rms_dbfs,
      saturation_protector_.HeadroomDb(),
      FloatS16ToDbfs(limiter_.LastAudioLevel()),
  };
  adaptive_digital_.Process(info, f);
  limiter_.Process(f);
  for (size_t i = 0; i < frame.size(); ++i) frame[i] = f[i] / kMaxAbsFloatS16Value;

  ++stats_.frames;
  if (speech_probability >= kVadConfidenceThreshold) ++stats_.speech_frames;
  if (limiter_.last_scaling_factor() < 1.0f) ++stats_.limiter_frames;
  stats_.speech_level_dbfs = speech_level_dbfs;
  stats_.speech_level_confident = info.speech_level_reliable;
  stats_.noise_rms_dbfs = noise_rms_dbfs;
  stats_.headroom_db = info.headroom_db;
  stats_.gain_db = adaptive_digital_.last_gain_db();
}

void GainController2::ExportMetrics(MetricsWriter& w) const {
  w.Counter("frames", stats_.frames);
  w.Counter("speech_frames", stats_.speech_frames);
  w.Counter("limiter_frames", stats_.limiter_frames);
  w.Gauge("gain_db", stats_.gain_db);
  w.Gauge("speech_level_dbfs", stats_.speech_level_dbfs);
  w.Gauge("noise_rms_dbfs", stats_.noise_rms_dbfs);
  w.Gauge("headroom_db", stats_.headroom_db);
}

}  // namespace sm::audio::agc2
