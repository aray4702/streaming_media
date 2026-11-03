// AppleAudioDevice (#1105): the audio device on Apple platforms, after
// libwebrtc's sdk/objc/native/src/audio/audio_device_ios.mm and
// voice_processing_audio_unit.mm (M157), followed through Apple's documented
// Audio Unit and AVAudioSession APIs.
//
// The state machine is plain C++ over two interfaces, so it is tested with a
// fake unit and a fake session (tests/unit/device/): the real ones are
// VoiceProcessingAudioUnit (AudioToolbox, macOS and iOS) and the iOS
// AVAudioSession (src/device/apple/).
//
//  - One VoiceProcessingIO unit does both directions; it starts when the first
//    direction starts and stops when the last one stops (audio_device_ios).
//  - 16-bit mono at the session's sample rate (48 kHz preferred); FineAudioBuffer
//    turns the unit's callbacks into 10 ms frames.
//  - Echo canceller switch: EchoCanceller::kLibrary bypasses the unit's voice
//    processing (kAUVoiceIOProperty_BypassVoiceProcessing), kSystem keeps it
//    and its AGC on.
//  - Interruptions (a phone call, Siri): the unit stops on the begin and
//    restarts on the end; a route change that changes the sample rate
//    re-initializes the unit at the new rate and restarts it; a media services
//    reset recreates the unit (audio_device_ios HandleInterruptionBegin/End,
//    HandleSampleRateChange).
//  - Delay estimate: a fixed 30 ms each way (kFixedPlayoutDelayEstimate,
//    kFixedRecordDelayEstimate in audio_device_ios.mm).
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

#include "sm/device/audio_device.h"
#include "sm/device/fine_audio_buffer.h"

namespace sm::device {

// What AppleAudioDevice needs from the I/O unit (VoiceProcessingAudioUnit).
class AudioUnitIO {
 public:
  enum class State { kInitRequired, kUninitialized, kInitialized, kStarted };
  class Observer {
   public:
    virtual ~Observer() = default;
    // The unit's input callback, after it rendered `frames` mono samples.
    virtual void OnRecordedData(const int16_t* samples, size_t frames) = 0;
    // The unit's render callback: fill `frames` samples. Returns false when
    // the output is silence (kAudioUnitRenderAction_OutputIsSilence).
    virtual bool OnPlayoutData(int16_t* out, size_t frames) = 0;
  };
  virtual ~AudioUnitIO() = default;
  virtual bool Init(Observer* observer) = 0;  // kInitRequired -> kUninitialized
  virtual bool Initialize(int sample_rate_hz, bool bypass_voice_processing) = 0;  // -> kInitialized
  virtual bool Start() = 0;                                                      // -> kStarted
  virtual bool Stop() = 0;                                                       // -> kInitialized
  virtual bool Uninitialize() = 0;                                               // -> kUninitialized
  virtual State state() const = 0;
};

// What AppleAudioDevice needs from the platform's audio session.
class AudioSessionControl {
 public:
  virtual ~AudioSessionControl() = default;
  // iOS: category playAndRecord, mode voiceChat, preferred rate and I/O
  // buffer, then activate. macOS: nothing to do.
  virtual bool Activate(const AudioDeviceConfig& config) = 0;
  virtual void Deactivate() = 0;
  // The hardware sample rate now (iOS: AVAudioSession.sampleRate).
  virtual int SampleRateHz() const = 0;
  // False while another app holds the session (interrupted).
  virtual bool CanPlayOrRecord() const = 0;
};

class AppleAudioDevice : public AudioDevice, private AudioUnitIO::Observer {
 public:
  static constexpr int kFixedPlayoutDelayEstimateMs = 30;
  static constexpr int kFixedRecordDelayEstimateMs = 30;

  using UnitFactory = std::unique_ptr<AudioUnitIO> (*)();

  AppleAudioDevice(const AudioDeviceConfig& config, UnitFactory unit_factory,
                   std::unique_ptr<AudioSessionControl> session);
  ~AppleAudioDevice() override;

  void RegisterAudioCallback(AudioTransport* transport) override;
  bool InitPlayout() override;
  bool InitRecording() override;
  bool StartPlayout() override;
  bool StopPlayout() override;
  bool StartRecording() override;
  bool StopRecording() override;
  bool Playing() const override { return playing_.load(); }
  bool Recording() const override { return recording_.load(); }
  int SampleRateHz() const override;
  bool BuiltInAecEnabled() const override { return config_.echo_canceller == EchoCanceller::kSystem; }

  // Session events (forwarded from the AVAudioSession notifications on iOS).
  void OnInterruptionBegan();
  void OnInterruptionEnded();
  void OnRouteChanged();          // re-reads the session's sample rate
  void OnMediaServicesReset();    // the unit is gone: recreate it

  bool interrupted() const;
  AudioUnitIO::State unit_state() const;
  int restarts() const;  // how many times the unit was re-initialized

 private:
  void OnRecordedData(const int16_t* samples, size_t frames) override;
  bool OnPlayoutData(int16_t* out, size_t frames) override;

  bool InitPlayOrRecordLocked();
  void ShutdownPlayOrRecordLocked();
  bool CreateAndInitializeUnitLocked();
  bool RestartUnitLocked(bool recreate);

  const AudioDeviceConfig config_;
  UnitFactory unit_factory_;
  std::unique_ptr<AudioSessionControl> session_;
  mutable std::mutex mu_;  // control calls and session events
  std::unique_ptr<AudioUnitIO> unit_;
  std::unique_ptr<FineAudioBuffer> fine_buffer_;
  AudioTransport* transport_ = nullptr;
  int sample_rate_hz_ = 0;
  bool initialized_ = false;  // audio_device_ios's audio_is_initialized_: one unit for both directions
  bool interrupted_ = false;
  int restarts_ = 0;
  std::atomic<bool> playing_{false};
  std::atomic<bool> recording_{false};
};

}  // namespace sm::device
