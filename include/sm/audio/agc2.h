// 6.4 Automatic gain control as libwebrtc M157's AGC2 (GainController2):
// adaptive digital gain, saturation protector and limiter. `apm v2` (#242).
//
// Ported from compare/webrtc/reference/modules/audio_processing/agc2/ with
// libwebrtc's constants and defaults (agc2_common.h and
// AudioProcessing::Config::GainController2::AdaptiveDigital):
//  - FixedDigitalLevelEstimator (fixed_digital_level_estimator.cc): per 10 ms
//    frame, 20 sub-frame peak envelopes, raised one sub-frame early, instant
//    attack and a 0.9971259 decay.
//  - InterpolatedGainCurve (interpolated_gain_curve.{h,cc}): the limiter's
//    piece-wise linear gain curve, the same 32-point table (knee from
//    about -0.75 dBFS, compression ratio 5 up to +1 dBFS, then saturation).
//  - Limiter (limiter.cc): per-sample gain interpolated between sub-frames,
//    with the (1 - t)^8 attack shape on the first sub-frame, then clamped
//    to the S16 range.
//  - GainApplier (gain_applier.cc): gain ramped linearly across a frame.
//  - NoiseFloorEstimator (noise_level_estimator.cc): minimum frame energy
//    over 5 s periods; instant decay, 0.5 attack.
//  - SpeechLevelEstimator (speech_level_estimator_impl.cc): speech-probability
//    weighted mean of the frame RMS (dBFS) with a 400 ms time to confidence,
//    committed only after 12 adjacent speech frames.
//  - SaturationProtector (saturation_protector{,_buffer}.cc): headroom from
//    the max peak per 400 ms, delayed by a 4-slot ring buffer, clamped to
//    12..25 dB, starting at 20 dB.
//  - AdaptiveDigitalGainController (adaptive_digital_gain_controller.cc):
//    gain to bring speech level + headroom to -headroom_db (5 dB), at most
//    50 dB, starting at 15 dB, capped by noise (-50 dBFS out) and by the
//    limiter envelope while the level is not confident; changes at most
//    6 dB/s, increases only after 12 frames with speech probability >= 0.95
//    (then a 12x faster first step).
//  - GainController2 (gain_controller2.cc) composes them for one mono 10 ms
//    frame in its order: speech probability from AGC2's RNN VAD
//    (agc2_rnn_vad.h, VoiceActivityDetectorWrapper reset every 1500 ms, as
//    APM creates it with `use_internal_vad`), peak/RMS levels, noise floor,
//    speech level, saturation protector, adaptive digital gain, then the
//    fixed gain and the limiter. Process(frame, speech_probability) takes
//    the probability from the caller instead.
//
// Signals are FloatS16 inside (libwebrtc's scale); Process() takes and
// returns our [-1, 1] floats.
//
// Difference from libwebrtc that remains (catalog.py, #242): no input
// volume controller (AGC2's analog part is off in a call; the call default
// is AGC1).
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "sm/core/metrics.h"

namespace sm::audio::agc2 {

// agc2_common.h.
inline constexpr float kMinFloatS16Value = -32768.0f;
inline constexpr float kMaxFloatS16Value = 32767.0f;
inline constexpr float kMaxAbsFloatS16Value = 32768.0f;
inline constexpr float kMinLevelDbfs = -90.31f;
inline constexpr int kFrameDurationMs = 10;
inline constexpr int kSubFramesInFrame = 20;
inline constexpr int kMaximalNumberOfSamplesPerChannel = 480;
inline constexpr float kLimiterThresholdForAgcGainDbfs = -1.0f;
inline constexpr float kVadConfidenceThreshold = 0.95f;
inline constexpr int kVadResetPeriodMs = 1500;
inline constexpr int kAdjacentSpeechFramesThreshold = 12;
inline constexpr float kLevelEstimatorTimeToConfidenceMs = 400;
inline constexpr float kLevelEstimatorLeakFactor = 1.0f - 1.0f / kLevelEstimatorTimeToConfidenceMs;
inline constexpr float kSaturationProtectorInitialHeadroomDb = 20.0f;
inline constexpr int kSaturationProtectorBufferSize = 4;
inline constexpr int kInterpolatedGainCurveKneePoints = 22;
inline constexpr int kInterpolatedGainCurveBeyondKneePoints = 10;
inline constexpr int kInterpolatedGainCurveTotalPoints =
    kInterpolatedGainCurveKneePoints + kInterpolatedGainCurveBeyondKneePoints;
// agc2_testing_common.h: the limiter's curve parameters.
inline constexpr float kLimiterMaxInputLevelDbFs = 1.0f;

// FloatS16 <-> dBFS and dB <-> ratio, as libwebrtc's audio_util.h and
// agc2 use them.
float FloatS16ToDbfs(float v);
float DbfsToFloatS16(float dbfs);
float DbToRatio(float db);

class FixedDigitalLevelEstimator {
 public:
  explicit FixedDigitalLevelEstimator(int samples_per_channel);
  std::array<float, kSubFramesInFrame> ComputeLevel(std::span<const float> frame);
  void SetSamplesPerChannel(int samples_per_channel);
  void Reset() { filter_state_level_ = 0.0f; }
  float LastAudioLevel() const { return filter_state_level_; }

 private:
  float filter_state_level_ = 0.0f;
  int samples_in_frame_ = 0;
  int samples_in_sub_frame_ = 0;
};

class InterpolatedGainCurve {
 public:
  enum class Region { kIdentity = 0, kKnee = 1, kLimiter = 2, kSaturation = 3 };
  struct Stats {
    uint64_t look_ups_identity_region = 0;
    uint64_t look_ups_knee_region = 0;
    uint64_t look_ups_limiter_region = 0;
    uint64_t look_ups_saturation_region = 0;
    Region region = Region::kIdentity;
  };
  // Given a non-negative input level (FloatS16), the gain to apply.
  float LookUpGainToApply(float input_level) const;
  const Stats& stats() const { return stats_; }

  static const std::array<float, kInterpolatedGainCurveTotalPoints>& params_x();

 private:
  void UpdateStats(float input_level) const;
  mutable Stats stats_;
};

class Limiter {
 public:
  explicit Limiter(int samples_per_channel);
  // Applies the limiter and hard-clips to the S16 range.
  void Process(std::span<float> frame);
  void SetSamplesPerChannel(int samples_per_channel) { level_estimator_.SetSamplesPerChannel(samples_per_channel); }
  void Reset() { level_estimator_.Reset(); }
  float LastAudioLevel() const { return level_estimator_.LastAudioLevel(); }
  float last_scaling_factor() const { return last_scaling_factor_; }
  const InterpolatedGainCurve::Stats& gain_curve_stats() const { return interp_gain_curve_.stats(); }

 private:
  InterpolatedGainCurve interp_gain_curve_;
  FixedDigitalLevelEstimator level_estimator_;
  std::array<float, kSubFramesInFrame + 1> scaling_factors_ = {};
  std::array<float, kMaximalNumberOfSamplesPerChannel> per_sample_scaling_factors_ = {};
  float last_scaling_factor_ = 1.0f;
};

class GainApplier {
 public:
  GainApplier(bool hard_clip_samples, float initial_gain_factor);
  void ApplyGain(std::span<float> frame);
  void SetGainFactor(float gain_factor) { current_gain_factor_ = gain_factor; }
  float GetGainFactor() const { return current_gain_factor_; }

 private:
  const bool hard_clip_samples_;
  float last_gain_factor_;
  float current_gain_factor_;
  int samples_per_channel_ = -1;
  float inverse_samples_per_channel_ = -1.0f;
};

class NoiseFloorEstimator {
 public:
  static constexpr int kUpdatePeriodNumFrames = 500;  // 5 s.
  NoiseFloorEstimator() { Initialize(48000); }
  // Analyzes a 10 ms frame and returns the noise level in dBFS.
  float Analyze(std::span<const float> frame);

 private:
  void Initialize(int sample_rate_hz);
  int sample_rate_hz_ = 0;
  float min_noise_energy_ = 0;
  bool first_period_ = true;
  bool preliminary_noise_energy_set_ = false;
  float preliminary_noise_energy_ = 0;
  float noise_energy_ = 0;
  int counter_ = 0;
};

// AudioProcessing::Config::GainController2::AdaptiveDigital defaults.
struct AdaptiveDigitalConfig {
  float headroom_db = 5.0f;
  float max_gain_db = 50.0f;
  float initial_gain_db = 15.0f;
  float max_gain_change_db_per_second = 6.0f;
  float max_output_noise_level_dbfs = -50.0f;
};

class SpeechLevelEstimator {
 public:
  SpeechLevelEstimator(const AdaptiveDigitalConfig& config, int adjacent_speech_frames_threshold);
  void Update(float rms_dbfs, float speech_probability);
  float GetLevelDbfs() const { return level_dbfs_; }
  bool IsConfident() const { return is_confident_; }
  void Reset();

 private:
  struct State {
    int time_to_confidence_ms;
    float numerator;
    float denominator;
  };
  void UpdateIsConfident();
  void ResetState(State& s) const;
  const float initial_speech_level_dbfs_;
  const int adjacent_speech_frames_threshold_;
  State preliminary_state_{};
  State reliable_state_{};
  float level_dbfs_;
  bool is_confident_ = false;
  int num_adjacent_speech_frames_ = 0;
};

// Ring buffer: push back, read oldest.
class SaturationProtectorBuffer {
 public:
  bool operator==(const SaturationProtectorBuffer& b) const;
  int Capacity() const { return static_cast<int>(buffer_.size()); }
  int Size() const { return size_; }
  void Reset() { next_ = 0, size_ = 0; }
  void PushBack(float v);
  std::optional<float> Front() const;

 private:
  int FrontIndex() const { return size_ == static_cast<int>(buffer_.size()) ? next_ : 0; }
  std::array<float, kSaturationProtectorBufferSize> buffer_{};
  int next_ = 0;
  int size_ = 0;
};

class SaturationProtector {
 public:
  SaturationProtector(float initial_headroom_db, int adjacent_speech_frames_threshold);
  float HeadroomDb() const { return headroom_db_; }
  void Analyze(float speech_probability, float peak_dbfs, float speech_level_dbfs);
  void Reset();

  struct State {
    float headroom_db;
    SaturationProtectorBuffer peak_delay_buffer;
    float max_peaks_dbfs;
    int time_since_push_ms;
  };

 private:
  const float initial_headroom_db_;
  const int adjacent_speech_frames_threshold_;
  int num_adjacent_speech_frames_ = 0;
  float headroom_db_ = 0;
  State preliminary_state_{};
  State reliable_state_{};
};

class AdaptiveDigitalGainController {
 public:
  struct FrameInfo {
    float speech_probability;
    float speech_level_dbfs;
    bool speech_level_reliable;
    float noise_rms_dbfs;
    float headroom_db;
    float limiter_envelope_dbfs;
  };
  AdaptiveDigitalGainController(const AdaptiveDigitalConfig& config, int adjacent_speech_frames_threshold);
  // Updates the gain from `info` and applies it to a 10 ms FloatS16 frame.
  void Process(const FrameInfo& info, std::span<float> frame);
  float last_gain_db() const { return last_gain_db_; }

 private:
  GainApplier gain_applier_;
  const AdaptiveDigitalConfig config_;
  const int adjacent_speech_frames_threshold_;
  const float max_gain_change_db_per_10ms_;
  int frames_to_gain_increase_allowed_;
  float last_gain_db_;
};

struct GainController2Config {
  // 8000 to 48000: APM's lowest supported rate (ValidateAudioFormat,
  // audio_processing_impl.cc) up to the limiter's 480 samples per 10 ms
  // (kMaximalNumberOfSamplesPerChannel). Outside it the controller is not
  // valid() (#594).
  int sample_rate = 16000;
  float fixed_gain_db = 0.0f;  // GainController2::FixedDigital default.
  AdaptiveDigitalConfig adaptive_digital;
};

struct GainController2Stats {
  uint64_t frames = 0;
  uint64_t speech_frames = 0;  // Speech probability >= 0.95.
  float speech_probability = 0;  // Of the last frame.
  float speech_level_dbfs = 0;
  bool speech_level_confident = false;
  float noise_rms_dbfs = 0;
  float headroom_db = 0;
  float gain_db = 0;
  uint64_t limiter_frames = 0;  // Frames where the limiter reduced the gain.
};

class VoiceActivityDetectorWrapper;

class GainController2 : public MetricsSource {
 public:
  explicit GainController2(GainController2Config cfg = {});
  ~GainController2() override;
  // False when built for a rate outside 8000-48000: Process then leaves the
  // audio untouched.
  bool valid() const { return valid_; }
  // One 10 ms mono frame in [-1, 1] at `cfg.sample_rate`, with the speech
  // probability from AGC2's RNN VAD, as APM runs it. A frame of another
  // length is left untouched.
  void Process(std::span<float> frame);
  // The same with the caller's `speech_probability` in [0, 1].
  void Process(std::span<float> frame, float speech_probability);
  float gain_db() const { return adaptive_digital_.last_gain_db(); }
  const GainController2Stats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  void ToFloatS16(std::span<const float> frame);
  void ProcessFloatS16(std::span<float> frame, float speech_probability);

  bool accepts(std::span<const float> frame) const {
    return valid_ && frame.size() == static_cast<size_t>(cfg_.sample_rate / 100);
  }

  GainController2Config cfg_;
  bool valid_;
  std::unique_ptr<VoiceActivityDetectorWrapper> vad_;  // Created on first use.
  GainApplier fixed_gain_applier_;
  NoiseFloorEstimator noise_level_estimator_;
  SpeechLevelEstimator speech_level_estimator_;
  SaturationProtector saturation_protector_;
  AdaptiveDigitalGainController adaptive_digital_;
  Limiter limiter_;
  std::vector<float> s16_;
  GainController2Stats stats_;
};

}  // namespace sm::audio::agc2
