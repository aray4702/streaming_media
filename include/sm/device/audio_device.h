// Audio I/O devices (C11 part 2, #1105): the interface an app uses to capture
// the microphone and play the far end, in 10 ms frames.
//
// It follows libwebrtc's AudioDeviceModule / AudioTransport split
// (modules/audio_device/include/audio_device.h, audio_device_defines.h): the
// device pulls 10 ms of playout from the transport and pushes 10 ms of
// recorded audio to it, with the delay estimate the echo canceller needs.
//
// Unlike the algorithms in this library, a device runs on the OS audio thread:
// the AudioTransport callbacks arrive on that thread, and the control calls
// (Init/Start/Stop) come from the app's thread. The callbacks must not block.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace sm::device {

// Which echo canceller runs (#1105).
enum class EchoCanceller {
  // The library's AEC3 in the app's audio processing: the device delivers the
  // raw microphone (VoiceProcessingIO with kAUVoiceIOProperty_BypassVoiceProcessing
  // on Apple platforms), so APM parity with libwebrtc holds. Default.
  kLibrary,
  // The platform's voice processing (VoiceProcessingIO's echo canceller and
  // AGC); the app turns its own AEC off.
  kSystem,
};

struct AudioDeviceConfig {
  int sample_rate_hz = 48000;  // libwebrtc's iOS high-performance rate
  int channels = 1;            // mono, as audio_device_ios
  EchoCanceller echo_canceller = EchoCanceller::kLibrary;
};

// libwebrtc's AudioTransport, reduced to what a one-to-one call uses.
class AudioTransport {
 public:
  virtual ~AudioTransport() = default;
  // 10 ms of interleaved microphone samples. `total_delay_ms` is the playout
  // plus record delay estimate (APM's set_stream_delay_ms).
  virtual void RecordedDataIsAvailable(const int16_t* samples, size_t samples_per_channel, int channels,
                                       int sample_rate_hz, int total_delay_ms) = 0;
  // Fill `out` with 10 ms of interleaved playout. Returns the samples per
  // channel written; fewer than asked means silence for the rest.
  virtual size_t NeedMorePlayData(int16_t* out, size_t samples_per_channel, int channels, int sample_rate_hz) = 0;
};

class AudioDevice {
 public:
  virtual ~AudioDevice() = default;
  virtual void RegisterAudioCallback(AudioTransport* transport) = 0;
  virtual bool InitPlayout() = 0;
  virtual bool InitRecording() = 0;
  virtual bool StartPlayout() = 0;
  virtual bool StopPlayout() = 0;
  virtual bool StartRecording() = 0;
  virtual bool StopRecording() = 0;
  virtual bool Playing() const = 0;
  virtual bool Recording() const = 0;
  // The rate the device runs at now (it can change on a route change).
  virtual int SampleRateHz() const = 0;
  // True when the platform's echo canceller is on (EchoCanceller::kSystem).
  virtual bool BuiltInAecEnabled() const = 0;
};

// The platform's device: Audio Units on Apple platforms (VoiceProcessingIO,
// plus the AVAudioSession on iOS). Returns nullptr where there is none.
std::unique_ptr<AudioDevice> CreatePlatformAudioDevice(const AudioDeviceConfig& config);

}  // namespace sm::device
