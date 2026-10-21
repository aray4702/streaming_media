// 6.4 Automatic gain control as libwebrtc M157's AGC1 in a call: the
// adaptive analog controller `AgcManagerDirect` (with one `MonoAgc` per
// channel) and APM's capture levels adjuster with its analog mic gain
// emulation. `apm v2` (#242).
//
// In a call (`WebRtcVoiceEngine::ApplyOptions` on desktop: GainController1
// in `kAdaptiveAnalog`, `AnalogGainController::enabled` by default),
// `AudioProcessingImpl` runs, per 10 ms capture frame:
//   1. `CaptureLevelsAdjuster::ApplyPreLevelAdjustment` (with the analog mic
//      gain emulation on, the capture is scaled by volume / 255 first, and
//      that volume is passed to the AGC as the applied input volume);
//   2. `AgcManagerDirect::AnalyzePreProcess` (clipping detection) before the
//      AEC, then `Process` on split band 0 after the NS;
//   3. the new compression gain, if any, into `GainControlImpl`
//      (`agc1::GainControl`, agc1.h), which then applies its digital gain;
//   4. `ApplyPostLevelAdjustment`, and the recommended input volume becomes
//      the emulated volume of the next frame.
// `CaptureLevels` below does steps 1 and 4; `AgcManagerDirect` step 2. The
// apm v2 chain (tests/e2e/audio_sims.h) wires them in this order.
//
// The modelled mic volume: there is no device in the test bench, so the
// volume is APM's own emulation (`capture_level_adjustment.
// analog_mic_gain_emulation`, initial level 255): a linear gain of
// volume / 255 on the capture, ramped over the 10 ms frame when it changes.
// `sm_apm_compare` runs libwebrtc's APM with the same emulation.
//
// Ported from compare/webrtc/reference/modules/audio_processing/ with
// libwebrtc's constants and defaults:
//  - agc/agc_manager_direct.{cc,h}: `AgcManagerDirect`, `MonoAgc`
//    (compression gain 2-12 dB, default 7, step 0.05 dB per frame; volume
//    12-255 through agc2/gain_map_internal.h's `kGainMap`, residual change
//    at most 15 dB; clipping: ratio 0.1, step 15, minimum 70, 300 frames'
//    wait, 6 dB of surplus compression gain; manual-change slack 25).
//  - capture_levels_adjuster/: `CaptureLevelsAdjuster`,
//    `AudioSamplesScaler`.
//  - api/audio/audio_processing.h: `AnalogGainController` and
//    `CaptureLevelAdjustment` defaults.
//
// Differences from libwebrtc (scope, not behaviour, catalog.py #242):
//  - The clipping predictor (agc2/clipping_predictor.*) is not ported: it
//    is off by default (`ClippingPredictor::enabled = false`), so a call
//    never creates it.
//  - The `WebRTC-Audio-2ndAgcMinMicLevelExperiment` field trial is the
//    config field `min_mic_level_override`.
//  - No histograms or logs (`RTC_HISTOGRAM_*`, `ApmDataDumper`); the
//    decisions are events and metrics instead.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "sm/audio/agc1_vad.h"
#include "sm/core/event_log.h"
#include "sm/core/metrics.h"

namespace sm::audio::agc1 {

// agc2/gain_map_internal.h: the gain (dB) of each analog volume 0..255.
inline constexpr int kGainMapSize = 256;
extern const int kGainMap[kGainMapSize];

// AudioProcessing::Config::GainController1::AnalogGainController.
struct AnalogGainControllerConfig {
  bool enabled = true;
  int clipped_level_min = 70;  // Lowest volume applied after clipping.
  bool enable_digital_adaptive = true;
  int clipped_level_step = 15;            // (0, 255].
  float clipped_ratio_threshold = 0.1f;   // (0, 1).
  int clipped_wait_frames = 300;          // > 0.
  // `WebRTC-Audio-2ndAgcMinMicLevelExperiment/Enabled-N` (off by default).
  std::optional<int> min_mic_level_override;
  EventSink* events = nullptr;  // "agc1" / "input_volume", "clipping".
};

// agc_manager_direct.h: `MonoAgc`, one channel.
class MonoAgc {
 public:
  MonoAgc(int clipped_level_min, bool disable_digital_adaptive, int min_mic_level);
  MonoAgc(const MonoAgc&) = delete;
  MonoAgc& operator=(const MonoAgc&) = delete;

  void Initialize();
  void HandleCaptureOutputUsedChange(bool capture_output_used);
  void set_stream_analog_level(int level) { recommended_input_volume_ = level; }
  void HandleClipping(int clipped_level_step);
  // Split band 0 of a 10 ms frame in S16 (160 samples at 16 kHz and up).
  void Process(std::span<const int16_t> audio, std::optional<int> rms_error_override);
  int recommended_analog_level() const { return recommended_input_volume_; }
  float voice_probability() const { return agc_->voice_probability(); }
  std::optional<int> new_compression() const { return new_compression_to_set_; }
  // Tests: replaces the loudness meter (owned from then on).
  void set_agc(std::unique_ptr<Agc> agc) { agc_ = std::move(agc); }
  int min_mic_level() const { return min_mic_level_; }

 private:
  void SetLevel(int new_level);
  void SetMaxLevel(int level);
  int CheckVolumeAndReset();
  void UpdateGain(int rms_error_db);
  void UpdateCompressor();

  const int min_mic_level_;
  const bool disable_digital_adaptive_;
  std::unique_ptr<Agc> agc_;
  int level_ = 0;
  int max_level_;
  int max_compression_gain_;
  int target_compression_;
  int compression_;
  float compression_accumulator_;
  bool capture_output_used_ = true;
  bool check_volume_on_next_process_ = true;
  bool startup_ = true;
  int recommended_input_volume_ = 0;
  std::optional<int> new_compression_to_set_;
  const int clipped_level_min_;
  int frames_since_update_gain_ = 0;
  bool is_first_frame_ = true;
};

struct AgcManagerStats {
  uint64_t frames = 0;
  uint64_t clipping_events = 0;
  uint64_t volume_changes = 0;  // Recommended input volume changed by Process().
  int recommended_input_volume = 0;
  int compression_gain_db = 0;  // Last compression gain chosen.
};

// agc_manager_direct.h: `AgcManagerDirect`. Audio is FloatS16 (libwebrtc's
// scale, [-32768, 32767]), one span per channel.
class AgcManagerDirect : public MetricsSource {
 public:
  AgcManagerDirect(int num_capture_channels, AnalogGainControllerConfig cfg = {});

  void Initialize();
  // The `GainControl` configuration `SetupDigitalGainControl` sets
  // (kFixedDigital): 2 dB target, 7 dB compression gain and the limiter on,
  // or 0, 0 and off without the digital adaptive part.
  int digital_target_level_dbfs() const { return disable_digital_adaptive_ ? 0 : 2; }
  int digital_compression_gain_db() const;
  bool digital_limiter_enabled() const { return !disable_digital_adaptive_; }

  void set_stream_analog_level(int level);
  // Full-band capture before the AEC (clipping detection).
  void AnalyzePreProcess(std::span<const std::span<const float>> channels);
  // Split band 0 of each channel after the NS. With both `speech_probability`
  // and `speech_level_dbfs`, they override the RMS error.
  void Process(std::span<const std::span<const float>> band0, std::optional<float> speech_probability = std::nullopt,
               std::optional<float> speech_level_dbfs = std::nullopt);
  int recommended_analog_level() const { return recommended_input_volume_; }
  void HandleCaptureOutputUsedChange(bool capture_output_used);
  float voice_probability() const;
  int num_channels() const { return num_capture_channels_; }
  // The latest compression gain chosen by Process(), if it changed.
  std::optional<int> GetDigitalCompressionGain() const { return new_compressions_to_set_[channel_controlling_gain_]; }
  MonoAgc& channel(int ch) { return *channel_agcs_[static_cast<size_t>(ch)]; }
  const AgcManagerStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  void AggregateChannelLevels();

  const AnalogGainControllerConfig cfg_;
  const bool analog_controller_enabled_;
  const int num_capture_channels_;
  const bool disable_digital_adaptive_;
  int frames_since_clipped_;
  int recommended_input_volume_ = 0;
  bool capture_output_used_ = true;
  int channel_controlling_gain_ = 0;
  std::vector<std::unique_ptr<MonoAgc>> channel_agcs_;
  std::vector<std::optional<int>> new_compressions_to_set_;
  std::vector<int16_t> s16_;
  AgcManagerStats stats_;
};

// capture_levels_adjuster/audio_samples_scaler.h: a gain ramped linearly
// over the frame when it changes, then saturation to S16.
class AudioSamplesScaler {
 public:
  explicit AudioSamplesScaler(float initial_gain) : previous_gain_(initial_gain), target_gain_(initial_gain) {}
  // FloatS16 channels of one frame.
  void Process(std::span<const std::span<float>> channels);
  void SetGain(float gain) { target_gain_ = gain; }

 private:
  float previous_gain_;
  float target_gain_;
  int samples_per_channel_ = -1;
  float one_by_samples_per_channel_ = -1.f;
};

// capture_levels_adjuster/capture_levels_adjuster.h, configured as
// `CaptureLevelAdjustment` (pre and post gain 1, emulation off, initial level
// 255 by default).
struct CaptureLevelsConfig {
  bool analog_mic_gain_emulation = false;
  int initial_level = 255;  // [0, 255].
  float pre_gain = 1.0f;
  float post_gain = 1.0f;
};

class CaptureLevels {
 public:
  explicit CaptureLevels(CaptureLevelsConfig cfg = {});
  void ApplyPreLevelAdjustment(std::span<const std::span<float>> channels) { pre_scaler_.Process(channels); }
  void ApplyPostLevelAdjustment(std::span<const std::span<float>> channels) { post_scaler_.Process(channels); }
  void SetPreGain(float pre_gain);
  void SetPostGain(float post_gain) { post_scaler_.SetGain(post_gain); }
  float GetPreAdjustmentGain() const { return pre_adjustment_gain_; }
  void SetAnalogMicGainLevel(int level);
  int GetAnalogMicGainLevel() const { return emulated_analog_mic_gain_level_; }

 private:
  void UpdatePreAdjustmentGain();

  const bool emulated_analog_mic_gain_enabled_;
  int emulated_analog_mic_gain_level_;
  float pre_gain_;
  float pre_adjustment_gain_;
  AudioSamplesScaler pre_scaler_;
  AudioSamplesScaler post_scaler_;
};

}  // namespace sm::audio::agc1
