#include "sm/audio/agc_manager.h"

#include <algorithm>
#include <cmath>

#include "sm/core/units.h"

namespace sm::audio::agc1 {

// agc2/gain_map_internal.h.
const int kGainMap[kGainMapSize] = {
    -56, -54, -52, -50, -48, -47, -45, -43, -42, -40, -38, -37, -35, -34, -33, -31, -30, -29, -27, -26, -25, -24,
    -23, -22, -20, -19, -18, -17, -16, -15, -14, -14, -13, -12, -11, -10, -9,  -8,  -8,  -7,  -6,  -5,  -5,  -4,
    -3,  -2,  -2,  -1,  0,   0,   1,   1,   2,   3,   3,   4,   4,   5,   5,   6,   6,   7,   7,   8,   8,   9,
    9,   10,  10,  11,  11,  12,  12,  13,  13,  13,  14,  14,  15,  15,  15,  16,  16,  17,  17,  17,  18,  18,
    18,  19,  19,  19,  20,  20,  21,  21,  21,  22,  22,  22,  23,  23,  23,  24,  24,  24,  24,  25,  25,  25,
    26,  26,  26,  27,  27,  27,  28,  28,  28,  28,  29,  29,  29,  30,  30,  30,  30,  31,  31,  31,  32,  32,
    32,  32,  33,  33,  33,  33,  34,  34,  34,  35,  35,  35,  35,  36,  36,  36,  36,  37,  37,  37,  38,  38,
    38,  38,  39,  39,  39,  39,  40,  40,  40,  40,  41,  41,  41,  41,  42,  42,  42,  42,  43,  43,  43,  44,
    44,  44,  44,  45,  45,  45,  45,  46,  46,  46,  46,  47,  47,  47,  47,  48,  48,  48,  48,  49,  49,  49,
    49,  50,  50,  50,  50,  51,  51,  51,  51,  52,  52,  52,  52,  53,  53,  53,  53,  54,  54,  54,  54,  55,
    55,  55,  55,  56,  56,  56,  56,  57,  57,  57,  57,  58,  58,  58,  58,  59,  59,  59,  59,  60,  60,  60,
    60,  61,  61,  61,  61,  62,  62,  62,  62,  63,  63,  63,  63,  64};

namespace {

// agc_manager_direct.cc.
constexpr int kLevelQuantizationSlack = 25;
constexpr int kDefaultCompressionGain = 7;
constexpr int kMaxCompressionGain = 12;
constexpr int kMinCompressionGain = 2;
constexpr float kCompressionGainStep = 0.05f;
constexpr int kMaxMicLevel = 255;
static_assert(kGainMapSize > kMaxMicLevel, "gain map too small");
constexpr int kMinMicLevel = 12;
constexpr int kMaxResidualGainChange = 15;
constexpr int kSurplusCompressionGain = 6;
constexpr float kOverrideTargetSpeechLevelDbfs = -18.0f;
constexpr float kOverrideSpeechProbabilitySilenceThreshold = 0.5f;
constexpr int kOverrideWaitFrames = 0;

int LevelFromGainError(int gain_error, int level, int min_mic_level) {
  if (gain_error == 0) return level;
  int new_level = level;
  if (gain_error > 0) {
    while (kGainMap[new_level] - kGainMap[level] < gain_error && new_level < kMaxMicLevel) ++new_level;
  } else {
    while (kGainMap[new_level] - kGainMap[level] > gain_error && new_level > min_mic_level) --new_level;
  }
  return new_level;
}

// The proportion of full-scale (clipped) samples, the most of any channel.
float ComputeClippedRatio(std::span<const std::span<const float>> audio) {
  int num_clipped = 0;
  size_t samples_per_channel = 0;
  for (const auto& ch : audio) {
    int num_clipped_in_ch = 0;
    for (float v : ch) {
      if (v >= 32767.0f || v <= -32768.0f) ++num_clipped_in_ch;
    }
    num_clipped = std::max(num_clipped, num_clipped_in_ch);
    samples_per_channel = ch.size();
  }
  return samples_per_channel ? static_cast<float>(num_clipped) / static_cast<float>(samples_per_channel) : 0.f;
}

int GetSpeechLevelErrorDb(float speech_level_dbfs, float speech_probability) {
  constexpr float kMinSpeechLevelDbfs = -90.0f;
  constexpr float kMaxSpeechLevelDbfs = 30.0f;
  if (speech_probability < kOverrideSpeechProbabilitySilenceThreshold) return 0;
  const float speech_level = std::clamp(speech_level_dbfs, kMinSpeechLevelDbfs, kMaxSpeechLevelDbfs);
  return static_cast<int>(std::round(kOverrideTargetSpeechLevelDbfs - speech_level));
}

// common_audio/include/audio_util.h: FloatS16ToS16.
int16_t FloatS16ToS16(float v) {
  v = std::min(v, 32767.f);
  v = std::max(v, -32768.f);
  return static_cast<int16_t>(v + std::copysign(0.5f, v));
}

}  // namespace

MonoAgc::MonoAgc(int clipped_level_min, bool disable_digital_adaptive, int min_mic_level)
    : min_mic_level_(min_mic_level),
      disable_digital_adaptive_(disable_digital_adaptive),
      agc_(std::make_unique<Agc>()),
      max_level_(kMaxMicLevel),
      max_compression_gain_(kMaxCompressionGain),
      target_compression_(kDefaultCompressionGain),
      compression_(target_compression_),
      compression_accumulator_(static_cast<float>(compression_)),
      clipped_level_min_(clipped_level_min) {}

void MonoAgc::Initialize() {
  max_level_ = kMaxMicLevel;
  max_compression_gain_ = kMaxCompressionGain;
  target_compression_ = disable_digital_adaptive_ ? 0 : kDefaultCompressionGain;
  compression_ = disable_digital_adaptive_ ? 0 : target_compression_;
  compression_accumulator_ = static_cast<float>(compression_);
  capture_output_used_ = true;
  check_volume_on_next_process_ = true;
  frames_since_update_gain_ = 0;
  is_first_frame_ = true;
}

void MonoAgc::Process(std::span<const int16_t> audio, std::optional<int> rms_error_override) {
  new_compression_to_set_ = std::nullopt;
  if (check_volume_on_next_process_) {
    check_volume_on_next_process_ = false;
    // The volume is only valid from the first process call on.
    CheckVolumeAndReset();
  }
  agc_->Process(audio);
  // Always read the error, which resets `agc_` when one is available.
  int rms_error = 0;
  bool update_gain = agc_->GetRmsErrorDb(&rms_error);
  if (rms_error_override.has_value()) {
    if (is_first_frame_ || frames_since_update_gain_ < kOverrideWaitFrames) {
      update_gain = false;
    } else {
      rms_error = *rms_error_override;
      update_gain = true;
    }
  }
  if (update_gain) UpdateGain(rms_error);
  if (!disable_digital_adaptive_) UpdateCompressor();
  is_first_frame_ = false;
  if (frames_since_update_gain_ < kOverrideWaitFrames) ++frames_since_update_gain_;
}

void MonoAgc::HandleClipping(int clipped_level_step) {
  // Always lower the maximum level, even below the threshold.
  SetMaxLevel(std::max(clipped_level_min_, max_level_ - clipped_level_step));
  if (level_ > clipped_level_min_) {
    // Below the limit there is no adjustment, so a level the user raised above
    // it is only acted on by the next Process().
    SetLevel(std::max(clipped_level_min_, level_ - clipped_level_step));
    agc_->Reset();
    frames_since_update_gain_ = 0;
    is_first_frame_ = false;
  }
}

void MonoAgc::SetLevel(int new_level) {
  const int voe_level = recommended_input_volume_;
  if (voe_level == 0) return;  // Muted: no action.
  if (voe_level < 0 || voe_level > kMaxMicLevel) return;
  // A volume outside [level_ - slack, level_ + slack] was changed by the user.
  if (voe_level > level_ + kLevelQuantizationSlack || voe_level < level_ - kLevelQuantizationSlack) {
    level_ = voe_level;
    // Always allow the user to raise the volume.
    if (level_ > max_level_) SetMaxLevel(level_);
    // No action, since the time of the manual change is unknown; the
    // compressor still provides some of the gain change.
    agc_->Reset();
    frames_since_update_gain_ = 0;
    is_first_frame_ = false;
    return;
  }
  new_level = std::min(new_level, max_level_);
  if (new_level == level_) return;
  recommended_input_volume_ = new_level;
  level_ = new_level;
}

void MonoAgc::SetMaxLevel(int level) {
  max_level_ = level;
  // `kSurplusCompressionGain` scaled linearly across the restricted range.
  max_compression_gain_ =
      kMaxCompressionGain + static_cast<int>(std::floor((1.f * kMaxMicLevel - static_cast<float>(max_level_)) /
                                                            static_cast<float>(kMaxMicLevel - clipped_level_min_) *
                                                            kSurplusCompressionGain +
                                                        0.5f));
}

void MonoAgc::HandleCaptureOutputUsedChange(bool capture_output_used) {
  if (capture_output_used_ == capture_output_used) return;
  capture_output_used_ = capture_output_used;
  // Reset when the output is used again.
  if (capture_output_used) check_volume_on_next_process_ = true;
}

int MonoAgc::CheckVolumeAndReset() {
  int level = recommended_input_volume_;
  // At startup a zero volume is raised too: a person starting a call expects
  // to be heard.
  if (level == 0 && !startup_) return 0;
  if (level < 0 || level > kMaxMicLevel) return -1;
  if (level < min_mic_level_) {
    level = min_mic_level_;
    recommended_input_volume_ = level;
  }
  agc_->Reset();
  level_ = level;
  startup_ = false;
  frames_since_update_gain_ = 0;
  is_first_frame_ = true;
  return 0;
}

// Splits the gain change between the compressor, which goes first with a
// slack region around the current volume, and the volume.
void MonoAgc::UpdateGain(int rms_error_db) {
  int rms_error = rms_error_db;
  frames_since_update_gain_ = 0;
  // The compressor always adds at least kMinCompressionGain.
  rms_error += kMinCompressionGain;
  const int raw_compression = std::clamp(rms_error, kMinCompressionGain, max_compression_gain_);
  // Move the target halfway to the new one; the special case lets it reach
  // the ends of the range, where halving would stop 1 dB short.
  if ((raw_compression == max_compression_gain_ && target_compression_ == max_compression_gain_ - 1) ||
      (raw_compression == kMinCompressionGain && target_compression_ == kMinCompressionGain + 1)) {
    target_compression_ = raw_compression;
  } else {
    target_compression_ = (raw_compression - target_compression_) / 2 + target_compression_;
  }
  // The residual goes to the volume (from the raw compression, to keep the
  // compressor's slack).
  const int residual_gain =
      std::clamp(rms_error - raw_compression, -kMaxResidualGainChange, kMaxResidualGainChange);
  if (residual_gain == 0) return;
  const int old_level = level_;
  SetLevel(LevelFromGainError(residual_gain, level_, min_mic_level_));
  if (old_level != level_) agc_->Reset();
}

void MonoAgc::UpdateCompressor() {
  if (compression_ == target_compression_) return;
  // Slowly towards the target, to avoid audible changes.
  if (target_compression_ > compression_) {
    compression_accumulator_ += kCompressionGainStep;
  } else {
    compression_accumulator_ -= kCompressionGainStep;
  }
  // Integer gains only: change once within half a step of an integer.
  int new_compression = compression_;
  const int nearest_neighbor = static_cast<int>(std::floor(compression_accumulator_ + 0.5));
  if (std::fabs(compression_accumulator_ - static_cast<float>(nearest_neighbor)) < kCompressionGainStep / 2) {
    new_compression = nearest_neighbor;
  }
  if (new_compression != compression_) {
    compression_ = new_compression;
    compression_accumulator_ = static_cast<float>(new_compression);
    new_compression_to_set_ = compression_;
  }
}

AgcManagerDirect::AgcManagerDirect(int num_capture_channels, AnalogGainControllerConfig cfg)
    : cfg_(cfg),
      analog_controller_enabled_(cfg.enabled),
      num_capture_channels_(std::max(1, num_capture_channels)),
      disable_digital_adaptive_(!cfg.enable_digital_adaptive),
      frames_since_clipped_(cfg.clipped_wait_frames),
      new_compressions_to_set_(static_cast<size_t>(num_capture_channels_)) {
  int min_mic_level = kMinMicLevel;
  if (cfg_.min_mic_level_override && *cfg_.min_mic_level_override >= 0 && *cfg_.min_mic_level_override <= 255) {
    min_mic_level = *cfg_.min_mic_level_override;
  }
  for (int ch = 0; ch < num_capture_channels_; ++ch) {
    channel_agcs_.push_back(std::make_unique<MonoAgc>(cfg_.clipped_level_min, disable_digital_adaptive_, min_mic_level));
  }
}

int AgcManagerDirect::digital_compression_gain_db() const {
  return disable_digital_adaptive_ ? 0 : kDefaultCompressionGain;
}

void AgcManagerDirect::Initialize() {
  for (auto& agc : channel_agcs_) agc->Initialize();
  capture_output_used_ = true;
  AggregateChannelLevels();
}

void AgcManagerDirect::AnalyzePreProcess(std::span<const std::span<const float>> channels) {
  AggregateChannelLevels();
  if (!capture_output_used_) return;
  // Clipped samples hide the pitch from the AGC; checked before the AEC so
  // that clipped echo counts too. A clipped frame lowers the volume and the
  // maximum volume by the same step (compensated by a higher maximum
  // compression gain, SetMaxLevel()), to avoid repeated clipped echo.
  const float clipped_ratio = ComputeClippedRatio(channels);
  if (frames_since_clipped_ < cfg_.clipped_wait_frames) {
    ++frames_since_clipped_;
    return;
  }
  if (clipped_ratio > cfg_.clipped_ratio_threshold) {
    for (auto& agc : channel_agcs_) agc->HandleClipping(cfg_.clipped_level_step);
    frames_since_clipped_ = 0;
    ++stats_.clipping_events;
    SM_EVENT(cfg_.events, Timestamp::Micros(0), "agc1", "clipping", clipped_ratio, cfg_.clipped_level_step);
  }
  AggregateChannelLevels();
}

void AgcManagerDirect::Process(std::span<const std::span<const float>> band0, std::optional<float> speech_probability,
                               std::optional<float> speech_level_dbfs) {
  AggregateChannelLevels();
  const int volume_after_clipping_handling = recommended_input_volume_;
  if (!capture_output_used_) return;
  ++stats_.frames;
  std::optional<int> rms_error_override;
  if (speech_probability.has_value() && speech_level_dbfs.has_value()) {
    rms_error_override = GetSpeechLevelErrorDb(*speech_level_dbfs, *speech_probability);
  }
  for (size_t ch = 0; ch < channel_agcs_.size() && ch < band0.size(); ++ch) {
    s16_.resize(band0[ch].size());
    for (size_t i = 0; i < s16_.size(); ++i) s16_[i] = FloatS16ToS16(band0[ch][i]);
    channel_agcs_[ch]->Process(s16_, rms_error_override);
    new_compressions_to_set_[ch] = channel_agcs_[ch]->new_compression();
  }
  AggregateChannelLevels();
  if (volume_after_clipping_handling != recommended_input_volume_) {
    ++stats_.volume_changes;
    SM_EVENT(cfg_.events, Timestamp::Micros(0), "agc1", "input_volume", recommended_input_volume_,
             volume_after_clipping_handling);
  }
  if (auto gain = GetDigitalCompressionGain()) stats_.compression_gain_db = *gain;
}

void AgcManagerDirect::HandleCaptureOutputUsedChange(bool capture_output_used) {
  for (auto& agc : channel_agcs_) agc->HandleCaptureOutputUsedChange(capture_output_used);
  capture_output_used_ = capture_output_used;
}

float AgcManagerDirect::voice_probability() const {
  float max_prob = 0.f;
  for (const auto& agc : channel_agcs_) max_prob = std::max(max_prob, agc->voice_probability());
  return max_prob;
}

void AgcManagerDirect::set_stream_analog_level(int level) {
  if (!analog_controller_enabled_) recommended_input_volume_ = level;
  for (auto& agc : channel_agcs_) agc->set_stream_analog_level(level);
  AggregateChannelLevels();
}

void AgcManagerDirect::AggregateChannelLevels() {
  int new_recommended_input_volume = channel_agcs_[0]->recommended_analog_level();
  channel_controlling_gain_ = 0;
  for (size_t ch = 1; ch < channel_agcs_.size(); ++ch) {
    const int level = channel_agcs_[ch]->recommended_analog_level();
    if (level < new_recommended_input_volume) {
      new_recommended_input_volume = level;
      channel_controlling_gain_ = static_cast<int>(ch);
    }
  }
  if (cfg_.min_mic_level_override.has_value() && *cfg_.min_mic_level_override >= 0 &&
      *cfg_.min_mic_level_override <= 255 && new_recommended_input_volume > 0) {
    new_recommended_input_volume = std::max(new_recommended_input_volume, *cfg_.min_mic_level_override);
  }
  if (analog_controller_enabled_) recommended_input_volume_ = new_recommended_input_volume;
  stats_.recommended_input_volume = recommended_input_volume_;
}

void AgcManagerDirect::ExportMetrics(MetricsWriter& w) const {
  w.Counter("frames", stats_.frames);
  w.Counter("clipping_events", stats_.clipping_events);
  w.Counter("volume_changes", stats_.volume_changes);
  w.Gauge("recommended_input_volume", static_cast<double>(stats_.recommended_input_volume));
  w.Gauge("compression_gain_db", static_cast<double>(stats_.compression_gain_db));
}

void AudioSamplesScaler::Process(std::span<const std::span<float>> channels) {
  if (channels.empty()) return;
  if (static_cast<int>(channels[0].size()) != samples_per_channel_) {
    samples_per_channel_ = static_cast<int>(channels[0].size());
    one_by_samples_per_channel_ = 1.f / static_cast<float>(samples_per_channel_);
  }
  // Unity gain: nothing to do.
  if (target_gain_ == 1.f && previous_gain_ == target_gain_) return;
  if (previous_gain_ == target_gain_) {
    for (const auto& ch : channels) {
      for (float& sample : ch) sample *= previous_gain_;
    }
  } else {
    const float increment = (target_gain_ - previous_gain_) * one_by_samples_per_channel_;
    for (const auto& ch : channels) {
      float gain = previous_gain_;
      for (float& sample : ch) {
        gain = increment > 0.f ? std::min(gain + increment, target_gain_) : std::max(gain + increment, target_gain_);
        sample *= gain;
      }
    }
  }
  previous_gain_ = target_gain_;
  for (const auto& ch : channels) {
    for (float& sample : ch) sample = std::clamp(sample, -32768.f, 32767.f);
  }
}

namespace {
constexpr int kMinAnalogMicGainLevel = 0;
constexpr int kMaxAnalogMicGainLevel = 255;

float ComputePreGain(float pre_gain, int level, bool emulation) {
  constexpr float kGainToLevelMultiplier = 1.f / kMaxAnalogMicGainLevel;
  return emulation ? pre_gain * (kGainToLevelMultiplier * static_cast<float>(level)) : pre_gain;
}
}  // namespace

CaptureLevels::CaptureLevels(CaptureLevelsConfig cfg)
    : emulated_analog_mic_gain_enabled_(cfg.analog_mic_gain_emulation),
      emulated_analog_mic_gain_level_(std::clamp(cfg.initial_level, kMinAnalogMicGainLevel, kMaxAnalogMicGainLevel)),
      pre_gain_(cfg.pre_gain),
      pre_adjustment_gain_(ComputePreGain(pre_gain_, emulated_analog_mic_gain_level_, emulated_analog_mic_gain_enabled_)),
      pre_scaler_(pre_adjustment_gain_),
      post_scaler_(cfg.post_gain) {}

void CaptureLevels::SetPreGain(float pre_gain) {
  pre_gain_ = pre_gain;
  UpdatePreAdjustmentGain();
}

void CaptureLevels::SetAnalogMicGainLevel(int level) {
  emulated_analog_mic_gain_level_ = std::clamp(level, kMinAnalogMicGainLevel, kMaxAnalogMicGainLevel);
  UpdatePreAdjustmentGain();
}

void CaptureLevels::UpdatePreAdjustmentGain() {
  pre_adjustment_gain_ = ComputePreGain(pre_gain_, emulated_analog_mic_gain_level_, emulated_analog_mic_gain_enabled_);
  pre_scaler_.SetGain(pre_adjustment_gain_);
}

}  // namespace sm::audio::agc1
