// VoiceProcessingAudioUnit (#1105): one kAudioUnitSubType_VoiceProcessingIO
// unit for capture (bus 1) and playout (bus 0), 16-bit mono, as libwebrtc's
// sdk/objc/native/src/audio/voice_processing_audio_unit.mm. AudioToolbox's C
// API only, so it builds for macOS and iOS alike.
#pragma once

#include <AudioToolbox/AudioUnit.h>

#include <atomic>
#include <memory>
#include <vector>

#include "sm/device/apple_audio_device.h"

namespace sm::device {

class VoiceProcessingAudioUnit : public AudioUnitIO {
 public:
  // audio_device_ios: up to 5 AudioUnitInitialize attempts, 100 ms apart.
  static constexpr int kMaxInitializeAttempts = 5;

  VoiceProcessingAudioUnit() = default;
  ~VoiceProcessingAudioUnit() override;

  bool Init(Observer* observer) override;
  bool Initialize(int sample_rate_hz, bool bypass_voice_processing) override;
  bool Start() override;
  bool Stop() override;
  bool Uninitialize() override;
  State state() const override { return state_.load(); }

  static std::unique_ptr<AudioUnitIO> Create();

 private:
  static OSStatus OnRender(void* ref, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* ts, UInt32 bus,
                           UInt32 frames, AudioBufferList* io_data);
  static OSStatus OnInput(void* ref, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* ts, UInt32 bus,
                          UInt32 frames, AudioBufferList* io_data);

  AudioUnit unit_ = nullptr;
  Observer* observer_ = nullptr;
  std::atomic<State> state_{State::kInitRequired};
  std::vector<int16_t> record_buffer_;
};

}  // namespace sm::device
